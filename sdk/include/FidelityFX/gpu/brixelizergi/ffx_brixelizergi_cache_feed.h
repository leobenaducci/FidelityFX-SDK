// This file is part of the FidelityFX SDK.
//
// vkfw patch: the application's cache feed (FfxBrixelizerGIDispatchDescription::cacheFeed): its own estimate of the world cache, for the
// bricks the screen never shows, written into the brick SH the GI keeps and, FFX_BRIXELIZER_GI_CACHE_FEED_RADIANCE, its radiance texels.

#ifndef FFX_BRIXELIZERGI_CACHE_FEED_H
#define FFX_BRIXELIZERGI_CACHE_FEED_H

#include "../ffx_core.h"
#include "../brixelizer/ffx_brixelizer_brick_common_private.h"
#include "ffx_brixelizergi_common.h"
#include "ffx_brixelizergi_probe_shading.h"

#define FFX_BRIXELIZER_GI_CACHE_FEED_ENTRY_UINTS 24u
#define FFX_BRIXELIZER_GI_CACHE_FEED_EMPTY_ONLY_FLAG 1u
#define FFX_BRIXELIZER_GI_CACHE_FEED_DIRECT_FLAG 2u
#define FFX_BRIXELIZER_GI_CACHE_FEED_RADIANCE_FLAG 4u
// A fed radiance texel is kept at this or more: a zero texel is one nothing has lit (FfxBrixelizerGISampleRadianceTexels), not a dark one.
#define FFX_BRIXELIZER_GI_CACHE_FEED_MIN_RADIANCE 6.103515625e-5

// A brick map load with the descriptor index made wave-uniform by a sweep (as ffx_brixelizergi_cache_slots.h's): the lanes of a wave walk
// the cascades to different depths, and an index that is not uniform faults the GPU.
FfxUInt32 FfxBrixelizerGICacheFeedLoadBrickMap(FfxUInt32 cascade_idx, FfxUInt32 element_index)
{
    FfxUInt32 value = FFX_BRIXELIZER_INVALID_ID;
    for (;;)
    {
        const FfxUInt32 uniform_idx = ffxWaveReadLaneFirstU1(cascade_idx);
        if (uniform_idx == cascade_idx)
        {
            value = LoadCascadeBrickMapArrayUniform(min(uniform_idx, FfxUInt32(FFX_BRIXELIZER_MAX_CASCADES - 1)), element_index);
            break;
        }
    }
    return value;
}

// The brick at a world position: the finest of the GI's cascades that has one there.
FfxUInt32 FfxBrixelizerGICacheFeedBrickAt(FfxFloat32x3 world_pos)
{
    for (FfxUInt32 cascade_id = GetTracingConstantsStartCascade(); cascade_id <= GetTracingConstantsEndCascade(); cascade_id++)
    {
        const FfxBrixelizerCascadeInfo cinfo = GetCascadeInfo(cascade_id);
        if (cinfo.is_enabled != 1u || any(FFX_LESS_THAN_EQUAL(world_pos, cinfo.grid_min)) || any(FFX_GREATER_THAN_EQUAL(world_pos, cinfo.grid_max)))
            continue;
        const FfxFloat32x3 rel       = (world_pos - cinfo.grid_min) / cinfo.voxel_size;
        const FfxUInt32    voxel_idx = FfxBrixelizerFlattenPOT(FfxBrixelizerWrapCoords(FfxInt32x3(cinfo.clipmap_offset), FFX_BRIXELIZER_CASCADE_WRAP_MASK, FfxUInt32x3(rel)), FFX_BRIXELIZER_CASCADE_DEGREE);
        const FfxUInt32    brick_id  = FfxBrixelizerGICacheFeedLoadBrickMap(cascade_id, voxel_idx);
        if (FfxBrixelizerIsValidID(brick_id))
            return brick_id;
    }
    return FFX_BRIXELIZER_INVALID_ID;
}

// vkfw patch: an entry's SH coefficient i.
FfxFloat32x4 FfxBrixelizerGICacheFeedLoadSH(FfxUInt32 base, FfxUInt32 i)
{
    return ffxUnpackF32x2(FfxUInt32x2(LoadCacheFeed(base + 4u + i * 2u), LoadCacheFeed(base + 5u + i * 2u)));
}

FfxBoolean FfxBrixelizerGICacheFeedFinite(FfxFloat32x3 v)
{
    return FfxBrixelizerGIFinite(FfxFloat32x4(v, 0.0));
}

// The entry's SH into the brick's (direct: its direct-light SH), blended by sample count. A brick with no samples, or one with a coefficient
// that is not finite, takes the entry's SH outright, at the entry's samples: a lerp keeps a NaN however small its weight, and the readers skip
// a brick only below a sample, so a fed NaN would stay black for good.
void FfxBrixelizerGICacheFeedBlendSH(FfxUInt32 base, FfxUInt32 slot, FfxBoolean direct, FfxFloat32 own_samples, FfxFloat32 samples)
{
    FfxBoolean keep = own_samples > FfxFloat32(0.0);
    for (FfxUInt32 i = 0u; i < 9u && keep; ++i)
        keep = FfxBrixelizerGICacheFeedFinite(ffxUnpackF32x2(direct ? LoadBricksDirectSH(slot * 9u + i) : LoadBricksSH(slot * 9u + i)).xyz);
    const FfxFloat32 kept  = keep ? own_samples : FfxFloat32(0.0);
    const FfxFloat32 t     = samples / (kept + samples);
    const FfxFloat32 total = ffxMin(kept + samples, GetCacheFeedMaxSamples());
    for (FfxUInt32 i = 0u; i < 9u; ++i)
    {
        const FfxFloat32x4 fed = FfxBrixelizerGICacheFeedLoadSH(base, i);
        FfxFloat32x4       sh  = ffxUnpackF32x2(direct ? LoadBricksDirectSH(slot * 9u + i) : LoadBricksSH(slot * 9u + i));
        sh.xyz = keep ? ffxLerp(sh.xyz, fed.xyz, t) : fed.xyz;
        sh.w   = total;
        if (direct)
            StoreBricksDirectSH(slot * 9u + i, ffxFloat16x4ToUint32x2(FFX_MIN16_F4(sh)));
        else
            StoreBricksSH(slot * 9u + i, ffxFloat16x4ToUint32x2(FFX_MIN16_F4(sh)));
    }
}

// One brick's share of an entry: its SH and its radiance texels.
void FfxBrixelizerGICacheFeedBrick(FfxUInt32 base, FfxUInt32 slot, FfxFloat32 samples)
{
    // The brick's own sample count is coefficient 0's .w (the GI blends its history by it); one that is not finite counts as none.EMPTY_ONLY keeps the
    // SH of a brick with as many samples as the entry or more; its unlit radiance texels are still fed (below). An entry whose SH is not
    // finite feeds no SH.
    const FfxFloat32 own         = ffxUnpackF32x2(LoadBricksSH(slot * 9u)).w;
    const FfxFloat32 own_samples = FfxBrixelizerGICacheFeedFinite(FfxFloat32x3(own, 0.0, 0.0)) ? ffxMax(own, FfxFloat32(0.0)) : FfxFloat32(0.0);
    const FfxBoolean empty_only  = (GetCacheFeedFlags() & FFX_BRIXELIZER_GI_CACHE_FEED_EMPTY_ONLY_FLAG) != 0u;
    FfxBoolean       feed_sh     = !empty_only || own_samples < samples;
    for (FfxUInt32 i = 0u; i < 9u && feed_sh; ++i)
        feed_sh = FfxBrixelizerGICacheFeedFinite(FfxBrixelizerGICacheFeedLoadSH(base, i).xyz);
    if (feed_sh)
    {
        FfxBrixelizerGICacheFeedBlendSH(base, slot, false, own_samples, samples);
        if ((GetCacheFeedFlags() & FFX_BRIXELIZER_GI_CACHE_FEED_DIRECT_FLAG) != 0u)
            FfxBrixelizerGICacheFeedBlendSH(base, slot, true, own_samples, samples);
    }

    // The radiance texels: an unlit one takes the entry's radiance; a lit one (none under EMPTY_ONLY: the screen lit it) blends it in, worth
    // at least the entry's samples, so the entry never takes more than half of what the screen lit (the brick's SH count says nothing of its
    // texels: a brick cleared or seeded has none and lit texels all the same). A black entry is kept just above zero, dark rather than unlit.
    if ((GetCacheFeedFlags() & FFX_BRIXELIZER_GI_CACHE_FEED_RADIANCE_FLAG) != 0u)
    {
        const FfxFloat32x3 fed = ffxUnpackF32x2(FfxUInt32x2(LoadCacheFeed(base + 22u), LoadCacheFeed(base + 23u))).xyz;
        if (!FfxBrixelizerGICacheFeedFinite(fed))
            return;
        const FfxFloat32x3 floor_radiance = FFX_BROADCAST_FLOAT32X3(FFX_BRIXELIZER_GI_CACHE_FEED_MIN_RADIANCE);
        const FfxFloat32x3 radiance       = all(FFX_LESS_THAN(fed, floor_radiance)) ? floor_radiance : max(fed, FFX_BROADCAST_FLOAT32X3(0.0));
        const FfxFloat32   lit_t          = samples / (ffxMax(own_samples, samples) + samples);
        const FfxUInt32x3  offset         = FfxBrixelizerGetSDFAtlasOffset(slot) / 2;
        for (FfxUInt32 texel = 0u; texel < 64u; ++texel)
        {
            const FfxUInt32x3  coord = offset + FfxBrixelizerUnflattenPOT(texel, 2);
            const FfxFloat32x3 old   = LoadRadianceCache(coord);
            const FfxBoolean   lit   = FfxBrixelizerGIRadianceTexelLit(old);
            if (lit && empty_only)
                continue;
            StoreRadianceCache(coord, lit ? ffxLerp(old, radiance, lit_t) : radiance);
        }
    }
}

void FfxBrixelizerGICacheFeed(FfxUInt32 tid)
{
    if (tid >= min(LoadCacheFeedCount(), GetCacheFeedMax()))
        return;

    const FfxUInt32    base     = tid * FFX_BRIXELIZER_GI_CACHE_FEED_ENTRY_UINTS;
    const FfxFloat32x3 position = FfxFloat32x3(ffxAsFloat(LoadCacheFeed(base + 0u)), ffxAsFloat(LoadCacheFeed(base + 1u)), ffxAsFloat(LoadCacheFeed(base + 2u)));
    const FfxFloat32   samples  = ffxAsFloat(LoadCacheFeed(base + 3u));
    if (!(samples > FfxFloat32(0.0)) || !FfxBrixelizerGICacheFeedFinite(position) || !FfxBrixelizerGICacheFeedFinite(FfxFloat32x3(samples, 0.0, 0.0)))
        return;

    const FfxUInt32 brick = FfxBrixelizerGICacheFeedBrickAt(position);
    const FfxUInt32 slot  = FfxBrixelizerIsValidID(brick) ? FfxBrixelizerGICacheSlot(brick) : FFX_BRIXELIZER_GI_INVALID_SLOT;
    if (slot != FFX_BRIXELIZER_GI_INVALID_SLOT)
        FfxBrixelizerGICacheFeedBrick(base, slot, samples);
}

#endif // FFX_BRIXELIZERGI_CACHE_FEED_H
