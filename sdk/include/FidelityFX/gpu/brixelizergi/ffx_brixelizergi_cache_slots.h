// vkfw patch: FFX_BRIXELIZER_GI_FLAG_CACHE_SLOTS -- the passes that keep the GI's per-brick caches in slots that outlive the bricks.
//
// A bake rebuilds a merged cascade and hands out new brick IDs, so a cache keyed by brick restarts from nothing wherever it rebuilt. The caches
// are in slots instead (the brick -> slot map the application owns), and each merged cascade remembers which slot each of its voxels had (the
// level slot map, laid out like the cascade's brick map: a world voxel is at the same index whatever the cascade has scrolled to). After a bake
// of one cascade:
//
// ClaimSame   a voxel with a brick keeps the slot its voxel had, where the voxel was in the cascade's window before the bake
// ClaimMover  a voxel still without one, inside a body that has moved since the cascade's last bake, takes the slot at the point the body's
//             motion takes it back to
// Free        the slots of the cascade's voxels nobody claimed go back on the free list
// Alloc       a brick still without a slot takes one (a freed one, else a never used one), and is listed to be filled
// Apply       the cascade's slot map and the brick -> slot map say what the bake and the claims decided
// Seed        each slot Alloc listed is filled from the next coarser cascade's brick there, if it has been sampled and agrees on which way
//             the surface faces, or else zeroed (what the context's own clear pass does, which does not run with application-owned slots)
// Stats       the counters' bricks, and the cascade's bricks binned by their irradiance's sample count

#ifndef FFX_BRIXELIZERGI_CACHE_SLOTS_H
#define FFX_BRIXELIZERGI_CACHE_SLOTS_H

#include "../ffx_core.h"
#include "ffx_brixelizer_brick_common_private.h"
#include "ffx_brixelizergi_common.h"
#include "ffx_brixelizergi_probe_shading.h"

#define FFX_BRIXELIZER_GI_SLOT_INVALID 0xffffffffu

#define FFX_BRIXELIZER_GI_SLOT_VOXELS (FFX_BRIXELIZER_CASCADE_RESOLUTION * FFX_BRIXELIZER_CASCADE_RESOLUTION * FFX_BRIXELIZER_CASCADE_RESOLUTION)

// Slot state indices.
#define FFX_BRIXELIZER_GI_SLOT_STATE_FREE_COUNT 0u   // the free stack's length
#define FFX_BRIXELIZER_GI_SLOT_STATE_NEXT_FRESH 1u   // the next never used slot

// Counters (the application's buffer, 64 uints, zeroed before each dispatch); the first 32 are read back as statistics.
#define FFX_BRIXELIZER_GI_SLOT_COUNTER_CLEARS 1u             // slots zeroed: no coarser brick to start from
#define FFX_BRIXELIZER_GI_SLOT_COUNTER_SAME_POSITION 2u      // bricks that kept their voxel's slot
#define FFX_BRIXELIZER_GI_SLOT_COUNTER_MOVER_CLAIMED 3u      // bricks that took a slot from where a mover was
#define FFX_BRIXELIZER_GI_SLOT_COUNTER_SEEDED 5u             // slots filled from the coarser cascade
#define FFX_BRIXELIZER_GI_SLOT_COUNTER_ALLOC_FAILED 6u       // bricks left without a slot: the pool is full
#define FFX_BRIXELIZER_GI_SLOT_COUNTER_BRICKS 7u             // bricks in the rebuilt cascade, after the passes
#define FFX_BRIXELIZER_GI_SLOT_COUNTER_SEED_REJECTED 8u      // a coarser brick that the compatibility test turned down
#define FFX_BRIXELIZER_GI_SLOT_COUNTER_FREED 9u              // slots the cascade gave back
#define FFX_BRIXELIZER_GI_SLOT_COUNTER_ALLOCATED 10u         // slots the cascade took
#define FFX_BRIXELIZER_GI_SLOT_COUNTER_HISTOGRAM 16u         // 8 buckets: no samples, then one a doubling
#define FFX_BRIXELIZER_GI_SLOT_COUNTER_SEEDS 32u             // the seed list's length

// A sweep over the wave's cascade indices, so that the descriptor index is wave-uniform at every load whatever the caller passes: FidelityFX's own
// traversal makes its cascade index uniform with wave intrinsics (FfxBrixelizerTraverseRaw), and the brick maps are indexed here inside divergent
// branches, where an index that only happens to be uniform (or one marked non-uniform, without the device feature) faulted the GPU ("Shader
// Instruction Error").
FfxUInt32 FfxBrixelizerGISlotsLoadBrickMap(FfxUInt32 cascade_idx, FfxUInt32 element_index)
{
    FfxUInt32 value = FFX_BRIXELIZER_INVALID_ID;
    for (;;)
    {
        const FfxUInt32 uniform_idx = ffxWaveReadLaneFirstU1(cascade_idx);
        if (uniform_idx == cascade_idx)
        {
            value = LoadCascadeBrickMapArrayUniform(uniform_idx, element_index);
            break;
        }
    }
    return value;
}

FfxUInt32 FfxBrixelizerGISlotCell(FfxUInt32x3 voxel, FfxBrixelizerCascadeInfo cascade)
{
    return FfxBrixelizerFlattenPOT((voxel + cascade.clipmap_offset) & FFX_BRIXELIZER_CASCADE_WRAP_MASK, FFX_BRIXELIZER_CASCADE_DEGREE);
}

FfxUInt32 FfxBrixelizerGISlotLevelBase()
{
    return GetSlotConstants().level_index * FFX_BRIXELIZER_GI_SLOT_VOXELS;
}

#if defined FFX_BRIXELIZER_GI_SLOTS_PASS_CLAIM_SAME
void FfxBrixelizerGISlotsClaimSame(FfxUInt32 tid)
{
    if (tid >= FFX_BRIXELIZER_GI_SLOT_VOXELS)
        return;

const FfxUInt32                cascade_idx = GetSlotConstants().cascade_index;
    const FfxBrixelizerCascadeInfo cascade     = GetCascadeInfo(cascade_idx);
    const FfxUInt32x3              voxel       = FfxBrixelizerUnflattenPOT(tid, FFX_BRIXELIZER_CASCADE_DEGREE);
    const FfxUInt32                cell        = FfxBrixelizerGISlotCell(voxel, cascade);
NewSlotStore(cell, FFX_BRIXELIZER_GI_SLOT_INVALID);
const FfxUInt32 brick = FfxBrixelizerGISlotsLoadBrickMap(cascade_idx, cell);
if (!FfxBrixelizerIsValidID(brick) || GetSlotConstants().previous_valid == 0u || GetSlotConstants().carry == 0u)
        return;

    // Where the voxel was in the cascade's window before the bake; the slot map's cell is the same one, but holds what the voxel was only
    // while the window still held it (a cell the window scrolled past is another voxel's, 64 away). By the windows' world placement:
    // clipmap_offset is only the window's place in the brick map, modulo 64, and says nothing of how far it scrolled.
    const FfxInt32x3 shift          = FfxInt32x3(round((cascade.grid_min - GetSlotConstants().previous_grid_min) / cascade.voxel_size));
    const FfxInt32x3 previous_voxel = FfxInt32x3(voxel) + shift;
    if (any(FFX_LESS_THAN(previous_voxel, FFX_BROADCAST_INT32X3(0))) || any(FFX_GREATER_THAN_EQUAL(previous_voxel, FFX_BROADCAST_INT32X3(FFX_BRIXELIZER_CASCADE_RESOLUTION))))
        return;

    const FfxUInt32 slot = LevelSlotLoad(FfxBrixelizerGISlotLevelBase() + cell);
    if (slot != FFX_BRIXELIZER_GI_SLOT_INVALID && SlotClaimExchange(slot, GetSlotConstants().claim_tag) != GetSlotConstants().claim_tag)
    {
        NewSlotStore(cell, slot);
        SlotCounterAdd(FFX_BRIXELIZER_GI_SLOT_COUNTER_SAME_POSITION, 1u);
    }
}
#endif // FFX_BRIXELIZER_GI_SLOTS_PASS_CLAIM_SAME

#if defined FFX_BRIXELIZER_GI_SLOTS_PASS_CLAIM_MOVER
void FfxBrixelizerGISlotsClaimMover(FfxUInt32 tid)
{
    if (tid >= FFX_BRIXELIZER_GI_SLOT_VOXELS || GetSlotConstants().num_movers == 0u || GetSlotConstants().previous_valid == 0u || GetSlotConstants().carry == 0u)
        return;

    const FfxUInt32                cascade_idx = GetSlotConstants().cascade_index;
    const FfxBrixelizerCascadeInfo cascade     = GetCascadeInfo(cascade_idx);
    const FfxUInt32x3              voxel       = FfxBrixelizerUnflattenPOT(tid, FFX_BRIXELIZER_CASCADE_DEGREE);
    const FfxUInt32                cell        = FfxBrixelizerGISlotCell(voxel, cascade);
    if (NewSlotLoad(cell) != FFX_BRIXELIZER_GI_SLOT_INVALID || !FfxBrixelizerIsValidID(FfxBrixelizerGISlotsLoadBrickMap(cascade_idx, cell)))
        return;

    // A new brick on a body that has moved since this cascade's last bake: its content was where the motion takes it back to.
    const FfxFloat32x3 position = cascade.grid_min + (FfxFloat32x3(voxel) + FFX_BROADCAST_FLOAT32X3(0.5)) * cascade.voxel_size;
    const FfxFloat32x4 position4 = FfxFloat32x4(position, 1.0);
    for (FfxUInt32 m = 0u; m < GetSlotConstants().num_movers; ++m)
    {
        // A voxel's bricks hold the body's distance field out to a voxel past its surface.
        if (all(FFX_GREATER_THAN_EQUAL(position, GetSlotMoverBoundsMin(m) - FFX_BROADCAST_FLOAT32X3(cascade.voxel_size))) &&
            all(FFX_LESS_THAN_EQUAL(position, GetSlotMoverBoundsMax(m) + FFX_BROADCAST_FLOAT32X3(cascade.voxel_size))))
        {
            const FfxFloat32x3 previous_position = FfxFloat32x3(dot(GetSlotMoverRow(m, 0u), position4), dot(GetSlotMoverRow(m, 1u), position4), dot(GetSlotMoverRow(m, 2u), position4));
            const FfxFloat32x3 rel               = (previous_position - GetSlotConstants().previous_grid_min) / GetSlotConstants().previous_voxel_size;
            if (any(FFX_LESS_THAN(rel, FFX_BROADCAST_FLOAT32X3(0.0))) || any(FFX_GREATER_THAN_EQUAL(rel, FFX_BROADCAST_FLOAT32X3(FfxFloat32(FFX_BRIXELIZER_CASCADE_RESOLUTION)))))
                return;
            const FfxUInt32x3 previous_voxel = FfxUInt32x3(rel);
            const FfxUInt32   previous_cell  = FfxBrixelizerFlattenPOT((previous_voxel + GetSlotConstants().previous_clipmap_offset) & FFX_BRIXELIZER_CASCADE_WRAP_MASK, FFX_BRIXELIZER_CASCADE_DEGREE);
            const FfxUInt32   slot           = LevelSlotLoad(FfxBrixelizerGISlotLevelBase() + previous_cell);
            if (slot != FFX_BRIXELIZER_GI_SLOT_INVALID && SlotClaimExchange(slot, GetSlotConstants().claim_tag) != GetSlotConstants().claim_tag)
            {
                NewSlotStore(cell, slot);
                SlotCounterAdd(FFX_BRIXELIZER_GI_SLOT_COUNTER_MOVER_CLAIMED, 1u);
            }
            return;
        }
    }
}
#endif // FFX_BRIXELIZER_GI_SLOTS_PASS_CLAIM_MOVER

#if defined FFX_BRIXELIZER_GI_SLOTS_PASS_FREE
void FfxBrixelizerGISlotsFree(FfxUInt32 tid)
{
    if (tid >= FFX_BRIXELIZER_GI_SLOT_VOXELS)
        return;

    // The slot map's cells, whichever voxel they hold now.
    const FfxUInt32 slot = LevelSlotLoad(FfxBrixelizerGISlotLevelBase() + tid);
    if (slot == FFX_BRIXELIZER_GI_SLOT_INVALID || SlotClaimLoad(slot) == GetSlotConstants().claim_tag)
        return;

    const FfxUInt32 index = SlotStateAdd(FFX_BRIXELIZER_GI_SLOT_STATE_FREE_COUNT, 1u);
    SlotFreeListStore(index, slot);
    SlotCounterAdd(FFX_BRIXELIZER_GI_SLOT_COUNTER_FREED, 1u);
}
#endif // FFX_BRIXELIZER_GI_SLOTS_PASS_FREE

#if defined FFX_BRIXELIZER_GI_SLOTS_PASS_ALLOC
void FfxBrixelizerGISlotsAlloc(FfxUInt32 tid)
{
    if (tid >= FFX_BRIXELIZER_GI_SLOT_VOXELS)
        return;

    const FfxUInt32                cascade_idx = GetSlotConstants().cascade_index;
    const FfxBrixelizerCascadeInfo cascade     = GetCascadeInfo(cascade_idx);
    const FfxUInt32x3              voxel       = FfxBrixelizerUnflattenPOT(tid, FFX_BRIXELIZER_CASCADE_DEGREE);
    const FfxUInt32                cell        = FfxBrixelizerGISlotCell(voxel, cascade);
    if (NewSlotLoad(cell) != FFX_BRIXELIZER_GI_SLOT_INVALID || !FfxBrixelizerIsValidID(FfxBrixelizerGISlotsLoadBrickMap(cascade_idx, cell)))
        return;

    // A freed slot if there is one: popped by a compare-and-swap, so that threads racing for the last one cannot both read past the stack's end.
    FfxUInt32 slot = FFX_BRIXELIZER_GI_SLOT_INVALID;
    for (;;)
    {
        const FfxUInt32 count = SlotStateAdd(FFX_BRIXELIZER_GI_SLOT_STATE_FREE_COUNT, 0u);
        if (count == 0u)
            break;
        if (SlotStateCompareSwap(FFX_BRIXELIZER_GI_SLOT_STATE_FREE_COUNT, count, count - 1u) == count)
        {
            slot = SlotFreeListLoad(count - 1u);
            break;
        }
    }
    // Else one never used.
    if (slot == FFX_BRIXELIZER_GI_SLOT_INVALID)
    {
        const FfxUInt32 fresh = SlotStateAdd(FFX_BRIXELIZER_GI_SLOT_STATE_NEXT_FRESH, 1u);
        if (fresh < FFX_BRIXELIZER_MAX_BRICKS)
            slot = fresh;
    }
    if (slot == FFX_BRIXELIZER_GI_SLOT_INVALID)
    {
        // None left: the brick has no cache, and reads as nothing.
        SlotCounterAdd(FFX_BRIXELIZER_GI_SLOT_COUNTER_ALLOC_FAILED, 1u);
        return;
    }

    NewSlotStore(cell, slot);
    SlotCounterAdd(FFX_BRIXELIZER_GI_SLOT_COUNTER_ALLOCATED, 1u);
    const FfxUInt32 entry = SlotCounterAdd(FFX_BRIXELIZER_GI_SLOT_COUNTER_SEEDS, 1u);
    SeedListStore(entry * 2u + 0u, slot);
    SeedListStore(entry * 2u + 1u, cell);
}
#endif // FFX_BRIXELIZER_GI_SLOTS_PASS_ALLOC

#if defined FFX_BRIXELIZER_GI_SLOTS_PASS_APPLY
void FfxBrixelizerGISlotsApply(FfxUInt32 tid)
{
    if (tid >= FFX_BRIXELIZER_GI_SLOT_VOXELS)
        return;

    const FfxUInt32                cascade_idx = GetSlotConstants().cascade_index;
    const FfxBrixelizerCascadeInfo cascade     = GetCascadeInfo(cascade_idx);
    const FfxUInt32x3              voxel       = FfxBrixelizerUnflattenPOT(tid, FFX_BRIXELIZER_CASCADE_DEGREE);
    const FfxUInt32                cell        = FfxBrixelizerGISlotCell(voxel, cascade);
    const FfxUInt32                slot        = NewSlotLoad(cell);
    LevelSlotStore(FfxBrixelizerGISlotLevelBase() + cell, slot);
    const FfxUInt32 brick = FfxBrixelizerGISlotsLoadBrickMap(cascade_idx, cell);
    if (FfxBrixelizerIsValidID(brick))
        BrickSlotMapStore(FfxBrixelizerBrickGetIndex(brick), slot);
}
#endif // FFX_BRIXELIZER_GI_SLOTS_PASS_APPLY

#if defined FFX_BRIXELIZER_GI_SLOTS_PASS_SEED

// The brick of a cascade at a world position; invalid outside it.
FfxUInt32 FfxBrixelizerGISlotsBrickAt(FfxUInt32 cascade_idx, FfxBrixelizerCascadeInfo cascade, FfxFloat32x3 position)
{
    const FfxFloat32x3 rel = (position - cascade.grid_min) / cascade.voxel_size;
    if (any(FFX_LESS_THAN(rel, FFX_BROADCAST_FLOAT32X3(0.0))) || any(FFX_GREATER_THAN_EQUAL(rel, FFX_BROADCAST_FLOAT32X3(FfxFloat32(FFX_BRIXELIZER_CASCADE_RESOLUTION)))))
        return FFX_BRIXELIZER_INVALID_ID;
    return FfxBrixelizerGISlotsLoadBrickMap(cascade_idx, FfxBrixelizerGISlotCell(FfxUInt32x3(rel), cascade));
}

FfxUInt32 FfxBrixelizerGISlotsCoarserBrick(FfxBrixelizerCascadeInfo coarser, FfxInt32x3 cell)
{
    if (any(FFX_LESS_THAN(cell, FFX_BROADCAST_INT32X3(0))) || any(FFX_GREATER_THAN_EQUAL(cell, FFX_BROADCAST_INT32X3(FFX_BRIXELIZER_CASCADE_RESOLUTION))))
        return FFX_BRIXELIZER_INVALID_ID;
    return FfxBrixelizerGISlotsLoadBrickMap(GetSlotConstants().coarser_cascade_index, FfxBrixelizerGISlotCell(FfxUInt32x3(cell), coarser));
}

// The slot a brick's caches are in, or invalid.
FfxUInt32 FfxBrixelizerGISlotsSlotOf(FfxUInt32 brick)
{
    return FfxBrixelizerIsValidID(brick) ? BrickSlotMapLoad(FfxBrixelizerBrickGetIndex(brick)) : FFX_BRIXELIZER_GI_SLOT_INVALID;
}

// A packed SH coefficient (fp16x4, w the sample count the GI blends its history by) with that count capped.
FfxUInt32x2 FfxBrixelizerGISlotsCapSamples(FfxUInt32x2 coefficient, FfxFloat32 max_samples)
{
    FfxFloat32x4 value = ffxUnpackF32x2(coefficient);
    value.w            = ffxMin(value.w, max_samples);
    return ffxFloat16x4ToUint32x2(FFX_MIN16_F4(value));
}

// Trilinear between a slot's radiance texels, which sit at uvw = k/3, so the outer ones are on its faces. Over the lit ones only: a texel the GI
// never reached is zero, not dark, and would pull its neighbours' light down with it.
FfxFloat32x3 FfxBrixelizerGISlotsSampleRadiance(FfxUInt32 slot, FfxFloat32x3 uvw)
{
    const FfxFloat32x3 t    = ffxSaturate(uvw) * FfxFloat32(3.0);
    const FfxUInt32x3  lo   = min(FfxUInt32x3(t), FFX_BROADCAST_UINT32X3(2u));
    const FfxFloat32x3 f    = t - FfxFloat32x3(lo);
    const FfxUInt32x3  base = FfxBrixelizerGetSDFAtlasOffset(slot) / 2 + lo;
    FfxFloat32x3       radiance = FFX_BROADCAST_FLOAT32X3(0.0);
    FfxFloat32         weight   = FfxFloat32(0.0);
    for (FfxUInt32 c = 0u; c < 8u; ++c)
    {
        const FfxUInt32x3  corner = FfxUInt32x3(c & 1u, (c >> 1u) & 1u, c >> 2u);
        const FfxFloat32x3 w3     = ffxLerp(FFX_BROADCAST_FLOAT32X3(1.0) - f, f, FfxFloat32x3(corner));
        const FfxFloat32x3 texel  = LoadRadianceCache(base + corner);
        if (any(FFX_GREATER_THAN(texel, FFX_BROADCAST_FLOAT32X3(0.0))))
        {
            const FfxFloat32 w = w3.x * w3.y * w3.z;
            radiance += texel * w;
            weight += w;
        }
    }
    return weight > FfxFloat32(0.0) ? radiance / weight : FFX_BROADCAST_FLOAT32X3(0.0);
}

// The SDF's gradient at a brick's centre: the outward normal of the surface nearest it.
FfxFloat32x3 FfxBrixelizerGISlotsBrickCentreNormal(FfxUInt32 brick)
{
    const FfxFloat32 atlas_size = FfxFloat32(FFX_BRIXELIZER_STATIC_CONFIG_SDF_ATLAS_SIZE);
    const FfxFloat32x3 atlas_min = (FfxFloat32x3(FfxBrixelizerGetSDFAtlasOffset(brick)) + FFX_BROADCAST_FLOAT32X3(0.5)) / atlas_size;
    const FfxFloat32x3 atlas_max = atlas_min + FFX_BROADCAST_FLOAT32X3(7.0 / atlas_size);
    const FfxFloat32x3 p = ffxLerp(atlas_min, atlas_max, FFX_BROADCAST_FLOAT32X3(0.5));
    const FfxFloat32 e = 1.0 / atlas_size;
    const FfxFloat32x3 gradient = FfxFloat32x3(
        SampleSDFAtlas(clamp(p + FfxFloat32x3(e, 0.0, 0.0), atlas_min, atlas_max)) - SampleSDFAtlas(clamp(p - FfxFloat32x3(e, 0.0, 0.0), atlas_min, atlas_max)),
        SampleSDFAtlas(clamp(p + FfxFloat32x3(0.0, e, 0.0), atlas_min, atlas_max)) - SampleSDFAtlas(clamp(p - FfxFloat32x3(0.0, e, 0.0), atlas_min, atlas_max)),
        SampleSDFAtlas(clamp(p + FfxFloat32x3(0.0, 0.0, e), atlas_min, atlas_max)) - SampleSDFAtlas(clamp(p - FfxFloat32x3(0.0, 0.0, e), atlas_min, atlas_max)));
    const FfxFloat32 length_sq = dot(gradient, gradient);
    return length_sq > FfxFloat32(1.0e-12) ? gradient * inversesqrt(length_sq) : FFX_BROADCAST_FLOAT32X3(0.0);
}

// Whether a coarser brick's cache is this new brick's to start from. A coarser voxel can span both faces of a wall, and its irradiance is then the
// other face's as often as this one's: the GI's own test for a neighbour's (FfxBrixelizerGIPropagateSH) is the direction its probes saw it from
// (the state's .xy), against which this brick's outward normal has to agree. And it has to have been sampled a few times itself, or it is no
// better a start than none.
FfxBoolean FfxBrixelizerGISlotsIsSeedCompatible(FfxUInt32 seed_slot, FfxUInt32 brick)
{
    if (ffxUnpackF32x2(LoadBricksSH(seed_slot * 9u)).w < GetSlotConstants().seed_min_samples)
        return false;
    const FfxFloat32x3 seen_from = ffxUnpackF32x2(LoadBricksSHState(seed_slot).xy).xyz;
    return dot(seen_from, FfxBrixelizerGISlotsBrickCentreNormal(brick)) > FfxFloat32(0.0);
}

void FfxBrixelizerGISlotsSeed(FfxUInt32 tid)
{
    if (tid >= SlotCounterLoad(FFX_BRIXELIZER_GI_SLOT_COUNTER_SEEDS))
        return;

    const FfxUInt32                slot        = SeedListLoad(tid * 2u + 0u);
    const FfxUInt32                cell        = SeedListLoad(tid * 2u + 1u);
    const FfxUInt32                cascade_idx = GetSlotConstants().cascade_index;
    const FfxBrixelizerCascadeInfo cascade     = GetCascadeInfo(cascade_idx);
    // The cell is the window's voxel plus the clipmap offset, per axis.
    const FfxUInt32x3  voxel    = (FfxBrixelizerUnflattenPOT(cell, FFX_BRIXELIZER_CASCADE_DEGREE) - cascade.clipmap_offset) & FFX_BRIXELIZER_CASCADE_WRAP_MASK;
    const FfxUInt32    brick    = FfxBrixelizerGISlotsLoadBrickMap(cascade_idx, cell);
    const FfxFloat32x3 position = cascade.grid_min + (FfxFloat32x3(voxel) + FFX_BROADCAST_FLOAT32X3(0.5)) * cascade.voxel_size;

    // Nothing here before: the next coarser cascade's brick at the voxel's centre, which this bake left alone.
    FfxUInt32                seed_slot = FFX_BRIXELIZER_GI_SLOT_INVALID;
    FfxBrixelizerCascadeInfo coarser   = cascade;
    if (GetSlotConstants().carry != 0u && GetSlotConstants().coarser_cascade_index != 0xffffffffu)
    {
        coarser   = GetCascadeInfo(GetSlotConstants().coarser_cascade_index);
        seed_slot = FfxBrixelizerGISlotsSlotOf(FfxBrixelizerGISlotsBrickAt(GetSlotConstants().coarser_cascade_index, coarser, position));
        if (seed_slot != FFX_BRIXELIZER_GI_SLOT_INVALID && !FfxBrixelizerGISlotsIsSeedCompatible(seed_slot, brick))
        {
            seed_slot = FFX_BRIXELIZER_GI_SLOT_INVALID;
            SlotCounterAdd(FFX_BRIXELIZER_GI_SLOT_COUNTER_SEED_REJECTED, 1u);
        }
    }

    if (seed_slot == FFX_BRIXELIZER_GI_SLOT_INVALID)
    {
        for (FfxUInt32 i = 0u; i < 9u; ++i)
        {
            StoreBricksSH(slot * 9u + i, FFX_BROADCAST_UINT32X2(0u));
            StoreBricksDirectSH(slot * 9u + i, FFX_BROADCAST_UINT32X2(0u));
        }
        StoreBricksSHState(slot, FFX_BROADCAST_UINT32X4(0u));
        for (FfxUInt32 texel = 0u; texel < 64u; ++texel)
            StoreRadianceCache(FfxBrixelizerGetSDFAtlasOffset(slot) / 2 + FfxBrixelizerUnflattenPOT(texel, 2), FFX_BROADCAST_FLOAT32X3(0.0));
        SlotCounterAdd(FFX_BRIXELIZER_GI_SLOT_COUNTER_CLEARS, 1u);
        return;
    }

    // The irradiance is a brick's whole, so the coarser brick's is the best there is, its sample count capped so its own samples soon outweigh
    // it; the radiance is sampled at each texel's own place, which can fall in a neighbouring coarser brick, or in none.
    SlotCounterAdd(FFX_BRIXELIZER_GI_SLOT_COUNTER_SEEDED, 1u);
    for (FfxUInt32 i = 0u; i < 9u; ++i)
    {
        StoreBricksSH(slot * 9u + i, FfxBrixelizerGISlotsCapSamples(LoadBricksSH(seed_slot * 9u + i), GetSlotConstants().seed_samples));
        StoreBricksDirectSH(slot * 9u + i, FfxBrixelizerGISlotsCapSamples(LoadBricksDirectSH(seed_slot * 9u + i), GetSlotConstants().seed_samples));
    }
    StoreBricksSHState(slot, LoadBricksSHState(seed_slot));

    // The voxel spans at most one coarser voxel per axis (the cascades only grow), so its texels fall in at most 2x2x2 coarser bricks: looked up once each.
    const FfxFloat32x3 voxel_min = cascade.grid_min + FfxFloat32x3(voxel) * cascade.voxel_size;
    const FfxFloat32x3 rel_min   = (voxel_min - coarser.grid_min) / coarser.voxel_size;
    const FfxFloat32x3 rel_step  = FFX_BROADCAST_FLOAT32X3(cascade.voxel_size / (3.0 * coarser.voxel_size));
    const FfxInt32x3   first     = FfxInt32x3(floor(rel_min));
    FfxUInt32          slots[8];
    for (FfxUInt32 c = 0u; c < 8u; ++c)
        slots[c] = FfxBrixelizerGISlotsSlotOf(FfxBrixelizerGISlotsCoarserBrick(coarser, first + FfxInt32x3(c & 1u, (c >> 1u) & 1u, c >> 2u)));
    for (FfxUInt32 texel = 0u; texel < 64u; ++texel)
    {
        FfxFloat32x3 radiance = FFX_BROADCAST_FLOAT32X3(0.0);
        if (GetSlotConstants().seed_radiance != 0u)
        {
            const FfxFloat32x3 rel      = rel_min + FfxFloat32x3(FfxBrixelizerUnflattenPOT(texel, 2)) * rel_step;
            const FfxUInt32x3  offset   = FfxUInt32x3(clamp(FfxInt32x3(floor(rel)) - first, FFX_BROADCAST_INT32X3(0), FFX_BROADCAST_INT32X3(1)));
            const FfxUInt32    texel_slot = slots[offset.x + offset.y * 2u + offset.z * 4u];
            if (texel_slot != FFX_BRIXELIZER_GI_SLOT_INVALID)
                radiance = FfxBrixelizerGISlotsSampleRadiance(texel_slot, rel - FfxFloat32x3(first + FfxInt32x3(offset)));
        }
        StoreRadianceCache(FfxBrixelizerGetSDFAtlasOffset(slot) / 2 + FfxBrixelizerUnflattenPOT(texel, 2), radiance);
    }
}

#endif // FFX_BRIXELIZER_GI_SLOTS_PASS_SEED

#if defined FFX_BRIXELIZER_GI_SLOTS_PASS_STATS
void FfxBrixelizerGISlotsStats(FfxUInt32 tid)
{
    if (tid >= FFX_BRIXELIZER_GI_SLOT_VOXELS)
        return;

    const FfxUInt32                cascade_idx = GetSlotConstants().cascade_index;
    const FfxBrixelizerCascadeInfo cascade     = GetCascadeInfo(cascade_idx);
    const FfxUInt32x3              voxel       = FfxBrixelizerUnflattenPOT(tid, FFX_BRIXELIZER_CASCADE_DEGREE);
    const FfxUInt32                brick       = FfxBrixelizerGISlotsLoadBrickMap(cascade_idx, FfxBrixelizerGISlotCell(voxel, cascade));
    if (!FfxBrixelizerIsValidID(brick))
        return;

    SlotCounterAdd(FFX_BRIXELIZER_GI_SLOT_COUNTER_BRICKS, 1u);
    const FfxUInt32 slot = BrickSlotMapLoad(FfxBrixelizerBrickGetIndex(brick));
    if (slot == FFX_BRIXELIZER_GI_SLOT_INVALID)
        return;

    const FfxFloat32 samples = ffxUnpackF32x2(LoadBricksSH(slot * 9u)).w;
    // 0: none (or not a number); then one bucket a doubling, the last open-ended.
    const FfxUInt32 bucket = !(samples >= FfxFloat32(1.0)) ? 0u : min(1u + FfxUInt32(log2(samples)), 7u);
    SlotCounterAdd(FFX_BRIXELIZER_GI_SLOT_COUNTER_HISTOGRAM + bucket, 1u);
}
#endif // FFX_BRIXELIZER_GI_SLOTS_PASS_STATS

#endif // FFX_BRIXELIZERGI_CACHE_SLOTS_H
