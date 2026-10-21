// This file is part of the FidelityFX SDK.
//
// vkfw patch: the application's cache feed (FfxBrixelizerGIDispatchDescription::cacheFeed): its own estimate of the world cache, for the
// bricks the screen never shows, written into the brick SH the GI keeps.

#ifndef FFX_BRIXELIZERGI_CACHE_FEED_H
#define FFX_BRIXELIZERGI_CACHE_FEED_H

#include "../ffx_core.h"
#include "../brixelizer/ffx_brixelizer_brick_common_private.h"
#include "ffx_brixelizergi_common.h"
#include "ffx_brixelizergi_probe_shading.h"

#define FFX_BRIXELIZER_GI_CACHE_FEED_ENTRY_UINTS 22u
#define FFX_BRIXELIZER_GI_CACHE_FEED_EMPTY_ONLY_FLAG 1u
#define FFX_BRIXELIZER_GI_CACHE_FEED_DIRECT_FLAG 2u

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

void FfxBrixelizerGICacheFeed(FfxUInt32 tid)
{
    if (tid >= min(LoadCacheFeedCount(), GetCacheFeedMax()))
        return;

    const FfxUInt32    base     = tid * FFX_BRIXELIZER_GI_CACHE_FEED_ENTRY_UINTS;
    const FfxFloat32x3 position = FfxFloat32x3(ffxAsFloat(LoadCacheFeed(base + 0u)), ffxAsFloat(LoadCacheFeed(base + 1u)), ffxAsFloat(LoadCacheFeed(base + 2u)));
    const FfxFloat32   samples  = ffxAsFloat(LoadCacheFeed(base + 3u));
    if (!(samples > FfxFloat32(0.0)) || any(isnan(position)))
        return;

    const FfxUInt32 brick = FfxBrixelizerGICacheFeedBrickAt(position);
    const FfxUInt32 slot  = FfxBrixelizerIsValidID(brick) ? FfxBrixelizerGICacheSlot(brick) : FFX_BRIXELIZER_GI_INVALID_SLOT;
    if (slot == FFX_BRIXELIZER_GI_INVALID_SLOT)
        return;

    // The brick's own sample count is coefficient 0's .w (the GI blends its history by it).
    const FfxFloat32 own = ffxUnpackF32x2(LoadBricksSH(slot * 9u)).w;
    if ((GetCacheFeedFlags() & FFX_BRIXELIZER_GI_CACHE_FEED_EMPTY_ONLY_FLAG) != 0u && !(own < samples))
        return;
    const FfxFloat32 own_samples = isnan(own) ? FfxFloat32(0.0) : ffxMax(own, FfxFloat32(0.0));
    const FfxFloat32 t           = samples / (own_samples + samples);
    const FfxFloat32 total       = ffxMin(own_samples + samples, GetCacheFeedMaxSamples());

    for (FfxUInt32 i = 0u; i < 9u; ++i)
    {
        const FfxFloat32x4 fed = ffxUnpackF32x2(FfxUInt32x2(LoadCacheFeed(base + 4u + i * 2u), LoadCacheFeed(base + 5u + i * 2u)));
        FfxFloat32x4       sh  = ffxUnpackF32x2(LoadBricksSH(slot * 9u + i));
        sh.xyz = ffxLerp(sh.xyz, fed.xyz, t);
        sh.w   = total;
        StoreBricksSH(slot * 9u + i, ffxFloat16x4ToUint32x2(FFX_MIN16_F4(sh)));
        if ((GetCacheFeedFlags() & FFX_BRIXELIZER_GI_CACHE_FEED_DIRECT_FLAG) != 0u)
        {
            FfxFloat32x4 direct = ffxUnpackF32x2(LoadBricksDirectSH(slot * 9u + i));
            direct.xyz = ffxLerp(direct.xyz, fed.xyz, t);
            direct.w   = total;
            StoreBricksDirectSH(slot * 9u + i, ffxFloat16x4ToUint32x2(FFX_MIN16_F4(direct)));
        }
    }
}

#endif // FFX_BRIXELIZERGI_CACHE_FEED_H
