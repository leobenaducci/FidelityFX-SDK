// This file is part of the FidelityFX SDK.
//
// Copyright (C) 2024 Advanced Micro Devices, Inc.
// 
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files(the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and /or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions :
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

#include <FidelityFX/host/ffx_brixelizergi.h>

#define FFX_CPU
#include <FidelityFX/gpu/ffx_core.h>
#include <FidelityFX/gpu/brixelizergi/ffx_brixelizergi_resources.h>
#include <FidelityFX/gpu/brixelizergi/ffx_brixelizergi_host_interface.h>
#include <FidelityFX/host/ffx_brixelizer_raw.h>
#include <ffx_object_management.h>

#include "../brixelizer/ffx_brixelizer_raw_private.h"
#include "ffx_brixelizergi_private.h"

// lists to map shader resource bindpoint name to resource identifier
typedef struct ResourceBinding
{
    uint32_t index;
    wchar_t  name[64];
} ResourceBinding;

static const ResourceBinding srvResourceBindingTable[] = {
    {FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_DISOCCLUSION_MASK, L"g_r_disocclusion_mask"},
    {FFX_BRIXELIZER_GI_PING_PONG_RESOURCE_STATIC_GI_TARGET_READ, L"g_sdfgi_r_static_gitarget"},
    {FFX_BRIXELIZER_GI_PING_PONG_RESOURCE_STATIC_SCREEN_PROBES_READ, L"g_sdfgi_r_static_screen_probes"},
    {FFX_BRIXELIZER_GI_PING_PONG_RESOURCE_STATIC_SPECULAR_TARGET_READ, L"g_sdfgi_r_specular_target"},
    {FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_RADIANCE_CACHE, L"g_bctx_radiance_cache"},
    {FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_BRICK_SLOT_MAP, L"g_bctx_brick_slot_map"},
    {FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_CACHE_FEED, L"r_cache_feed"},
    {FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_CACHE_FEED_COUNT, L"r_cache_feed_count"},
    {FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_INPUT_ENVIRONMENT_MAP, L"g_environment_map"},
    {FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_INPUT_PREV_LIT_OUTPUT, L"g_prev_lit_output"},
    {FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_INPUT_DEPTH, L"g_depth"},
    {FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_INPUT_HISTORY_DEPTH, L"g_history_depth"},
    {FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_INPUT_NORMAL, L"g_normal"},
    {FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_INPUT_HISTORY_NORMAL, L"g_history_normal"},
    {FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_INPUT_ROUGHNESS, L"g_roughness"},
    {FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_INPUT_MOTION_VECTORS, L"g_motion_vectors"},
    {FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_INPUT_BLUE_NOISE, L"g_blue_noise"},
    {FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_INPUT_SDF_ATLAS, L"r_sdf_atlas"},
    {FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_INPUT_CONTEXT_BRICKS_AABB, L"r_bctx_bricks_aabb"},
    {FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_INPUT_CONTEXT_BRICKS_VOXEL_MAP, L"r_bctx_bricks_voxel_map"},
    {FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_INPUT_CONTEXT_COUNTERS, L"r_bctx_counters"},
    {FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_INPUT_BRICKS_CLEAR_LIST, L"r_bctx_bricks_clear_list"},
    {FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_INPUT_CASCADE_AABB_TREES, L"r_cascade_aabbtrees"},
    {FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_INPUT_CASCADE_BRICK_MAPS, L"r_cascade_brick_maps"},
    {FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_SOURCE_DEPTH, L"g_src_depth"},
    {FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_SOURCE_HISTORY_DEPTH, L"g_src_history_depth"},
    {FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_SOURCE_NORMAL, L"g_src_normal"},
    {FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_SOURCE_HISTORY_NORMAL, L"g_src_history_normal"},
    {FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_SOURCE_ROUGHNESS, L"g_src_roughness"},
    {FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_SOURCE_MOTION_VECTORS, L"g_src_motion_vectors"},
    {FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_SOURCE_LIT_OUTPUT, L"g_src_prev_lit_output"},
    {FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_OUTPUT_DIFFUSE_GI, L"g_downsampled_diffuse_gi"},
    {FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_OUTPUT_SPECULAR_GI, L"g_downsampled_specular_gi"},
    {FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_SKY_VISIBILITY_HISTORY_READ, L"g_sdfgi_r_sky_visibility_history"},
};

static const ResourceBinding uavResourceBindingTable[] = {
    {FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_DISOCCLUSION_MASK, L"g_rw_disocclusion_mask"},
    {FFX_BRIXELIZER_GI_PING_PONG_RESOURCE_STATIC_SCREEN_PROBES_WRITE, L"g_sdfgi_rw_static_screen_probes"},
    {FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_STATIC_PUSHOFF_MAP, L"g_sdfgi_rw_static_pushoff_map"},
    {FFX_BRIXELIZER_GI_PING_PONG_RESOURCE_STATIC_GI_TARGET_WRITE, L"g_sdfgi_rw_static_gitarget"},
    {FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_DEBUG_TARGET, L"g_sdfgi_rw_debug_target"},
    {FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_OUTPUT_SKY_VISIBILITY, L"g_sdfgi_rw_sky_visibility_target"},
    {FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_SLOT_STATE, L"g_rw_slot_state"},
    {FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_SLOT_CLAIM, L"g_rw_slot_claim"},
    {FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_SLOT_FREE_LIST, L"g_rw_slot_free_list"},
    {FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_LEVEL_SLOT_MAPS, L"g_rw_level_slot_maps"},
    {FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_NEW_SLOTS, L"g_rw_new_slots"},
    {FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_SEED_LIST, L"g_rw_seed_list"},
    {FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_MOVER_CLAIMS, L"g_rw_mover_claims"},
    {FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_SLOT_COUNTERS, L"g_rw_slot_counters"},
    {FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_EMPTY_BRICKS, L"g_rw_empty_bricks"},
    {FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_BRICK_SLOT_MAP, L"g_rw_bctx_brick_slot_map"},
{FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_SKY_VISIBILITY_HISTORY_WRITE, L"g_sdfgi_rw_sky_visibility_history"},
    {FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_RADIANCE_CACHE, L"g_rw_bctx_radiance_cache"},
    {FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_TEMP_SPAWN_MASK, L"g_sdfgi_rw_temp_spawn_mask"},
    {FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_TEMP_RAND_SEED, L"g_sdfgi_rw_temp_rand_seed"},
    {FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_TEMP_SPECULAR_PRETRACE_TARGET, L"g_sdfgi_rw_temp_specular_pretrace_target"},
    {FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_TEMP_BLUR_MASK, L"g_sdfgi_rw_temp_blur_mask"},
    {FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_STATIC_SCREEN_PROBES_STAT, L"g_sdfgi_rw_static_screen_probes_stat"},
    {FFX_BRIXELIZER_GI_PING_PONG_RESOURCE_STATIC_SPECULAR_TARGET_WRITE, L"g_sdfgi_rw_specular_target"},
    {FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_STATIC_PROBE_INFO, L"g_sdfgi_rw_static_probe_info"},
    {FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_STATIC_PROBE_SH, L"g_sdfgi_rw_static_probe_sh_buffer"},
    {FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_TEMP_PROBE_INFO, L"g_sdfgi_rw_temp_probe_info"},
    {FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_TEMP_PROBE_SH, L"g_sdfgi_rw_temp_probe_sh_buffer"},
    {FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_RAY_SWAP_INDIRECT_ARGS, L"g_sdfgi_rw_ray_swap_indirect_args"},
    {FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_BRICKS_DIRECT_SH, L"g_bctx_bricks_direct_sh"},
    {FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_BRICKS_SH, L"g_bctx_bricks_sh"},
    {FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_BRICKS_SH_STATE, L"g_bctx_bricks_sh_state"},
    {FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_TEMP_SPECULAR_RAY_SWAP, L"g_sdfgi_rw_temp_specular_ray_swap"},
    {FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_OUTPUT_DIFFUSE_GI, L"g_rw_diffuse_output_copy"},
    {FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_OUTPUT_SPECULAR_GI, L"g_rw_specular_output_copy"},
    {FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_OUTPUT_DEBUG_VISUALIZATION, L"g_rw_debug_visualization"},
    {FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_DOWNSAMPLED_LIT_OUTPUT, L"g_downsampled_prev_lit_output"},
    {FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_DOWNSAMPLED_DEPTH, L"g_downsampled_depth"},
    {FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_DOWNSAMPLED_HISTORY_DEPTH, L"g_downsampled_history_depth"},
    {FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_DOWNSAMPLED_NORMAL, L"g_downsampled_normal"},
    {FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_DOWNSAMPLED_HISTORY_NORMAL, L"g_downsampled_history_normal"},
    {FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_DOWNSAMPLED_ROUGHNESS, L"g_downsampled_roughness"},
    {FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_DOWNSAMPLED_MOTION_VECTORS, L"g_downsampled_motion_vectors"},
    {FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_UPSAMPLED_DIFFUSE_GI, L"g_upsampled_diffuse_gi"},
    {FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_UPSAMPLED_SPECULAR_GI, L"g_upsampled_specular_gi"},
};

static const ResourceBinding cbvResourceBindingTable[] = {
    {FFX_BRIXELIZER_GI_CONSTANTBUFFER_IDENTIFIER_GI_CONSTANTS, L"g_sdfgi_constants"},
    {FFX_BRIXELIZER_GI_CONSTANTBUFFER_IDENTIFIER_PASS_CONSTANTS, L"g_pass_constants"},
    {FFX_BRIXELIZER_GI_CONSTANTBUFFER_IDENTIFIER_SCALING_CONSTANTS, L"g_scaling_constants"},
    {FFX_BRIXELIZER_GI_CONSTANTBUFFER_IDENTIFIER_CONTEXT_INFO, L"g_bx_context_info"},
    {FFX_BRIXELIZER_GI_CONSTANTBUFFER_IDENTIFIER_SLOT_CONSTANTS, L"g_slot_constants"},
};

static size_t cbSizes[] = {
    sizeof(FfxBrixelizerGIConstants), 
    sizeof(FfxBrixelizerGIPassConstants), 
    sizeof(FfxBrixelizerGIScalingConstants), 
    sizeof(FfxBrixelizerContextInfo),
    sizeof(FfxBrixelizerGISlotConstants)
};

static bool isPingPongResource(uint32_t resourceId)
{
    return resourceId == FFX_BRIXELIZER_GI_PING_PONG_RESOURCE_STATIC_GI_TARGET_READ ||
           resourceId == FFX_BRIXELIZER_GI_PING_PONG_RESOURCE_STATIC_SCREEN_PROBES_READ || 
           resourceId == FFX_BRIXELIZER_GI_PING_PONG_RESOURCE_STATIC_SPECULAR_TARGET_READ || 
           resourceId == FFX_BRIXELIZER_GI_PING_PONG_RESOURCE_STATIC_SCREEN_PROBES_WRITE ||
           resourceId == FFX_BRIXELIZER_GI_PING_PONG_RESOURCE_STATIC_GI_TARGET_WRITE ||
           resourceId == FFX_BRIXELIZER_GI_PING_PONG_RESOURCE_STATIC_SPECULAR_TARGET_WRITE;
}

// Code taken from MESA implementation of GLU
// Under terms of the SGI FREE SOFTWARE LICENSE B (Version 2.0, Sept. 18, 2008)
// https://cgit.freedesktop.org/mesa/glu/tree/src/libutil/project.c
static void matrixInvert(const FfxFloat32x4x4 m, FfxFloat32x4x4 out)
{
    float inv[16], det;
    int i;

    inv[0] = m[5]  * m[10] * m[15] - 
             m[5]  * m[11] * m[14] - 
             m[9]  * m[6]  * m[15] + 
             m[9]  * m[7]  * m[14] +
             m[13] * m[6]  * m[11] - 
             m[13] * m[7]  * m[10];

    inv[4] = -m[4]  * m[10] * m[15] + 
              m[4]  * m[11] * m[14] + 
              m[8]  * m[6]  * m[15] - 
              m[8]  * m[7]  * m[14] - 
              m[12] * m[6]  * m[11] + 
              m[12] * m[7]  * m[10];

    inv[8] = m[4]  * m[9] * m[15] - 
             m[4]  * m[11] * m[13] - 
             m[8]  * m[5] * m[15] + 
             m[8]  * m[7] * m[13] + 
             m[12] * m[5] * m[11] - 
             m[12] * m[7] * m[9];

    inv[12] = -m[4]  * m[9] * m[14] + 
               m[4]  * m[10] * m[13] +
               m[8]  * m[5] * m[14] - 
               m[8]  * m[6] * m[13] - 
               m[12] * m[5] * m[10] + 
               m[12] * m[6] * m[9];

    inv[1] = -m[1]  * m[10] * m[15] + 
              m[1]  * m[11] * m[14] + 
              m[9]  * m[2] * m[15] - 
              m[9]  * m[3] * m[14] - 
              m[13] * m[2] * m[11] + 
              m[13] * m[3] * m[10];

    inv[5] = m[0]  * m[10] * m[15] - 
             m[0]  * m[11] * m[14] - 
             m[8]  * m[2] * m[15] + 
             m[8]  * m[3] * m[14] + 
             m[12] * m[2] * m[11] - 
             m[12] * m[3] * m[10];

    inv[9] = -m[0]  * m[9] * m[15] + 
              m[0]  * m[11] * m[13] + 
              m[8]  * m[1] * m[15] - 
              m[8]  * m[3] * m[13] - 
              m[12] * m[1] * m[11] + 
              m[12] * m[3] * m[9];

    inv[13] = m[0]  * m[9] * m[14] - 
              m[0]  * m[10] * m[13] - 
              m[8]  * m[1] * m[14] + 
              m[8]  * m[2] * m[13] + 
              m[12] * m[1] * m[10] - 
              m[12] * m[2] * m[9];

    inv[2] = m[1]  * m[6] * m[15] - 
             m[1]  * m[7] * m[14] - 
             m[5]  * m[2] * m[15] + 
             m[5]  * m[3] * m[14] + 
             m[13] * m[2] * m[7] - 
             m[13] * m[3] * m[6];

    inv[6] = -m[0]  * m[6] * m[15] + 
              m[0]  * m[7] * m[14] + 
              m[4]  * m[2] * m[15] - 
              m[4]  * m[3] * m[14] - 
              m[12] * m[2] * m[7] + 
              m[12] * m[3] * m[6];

    inv[10] = m[0]  * m[5] * m[15] - 
              m[0]  * m[7] * m[13] - 
              m[4]  * m[1] * m[15] + 
              m[4]  * m[3] * m[13] + 
              m[12] * m[1] * m[7] - 
              m[12] * m[3] * m[5];

    inv[14] = -m[0]  * m[5] * m[14] + 
               m[0]  * m[6] * m[13] + 
               m[4]  * m[1] * m[14] - 
               m[4]  * m[2] * m[13] - 
               m[12] * m[1] * m[6] + 
               m[12] * m[2] * m[5];

    inv[3] = -m[1] * m[6] * m[11] + 
              m[1] * m[7] * m[10] + 
              m[5] * m[2] * m[11] - 
              m[5] * m[3] * m[10] - 
              m[9] * m[2] * m[7] + 
              m[9] * m[3] * m[6];

    inv[7] = m[0] * m[6] * m[11] - 
             m[0] * m[7] * m[10] - 
             m[4] * m[2] * m[11] + 
             m[4] * m[3] * m[10] + 
             m[8] * m[2] * m[7] - 
             m[8] * m[3] * m[6];

    inv[11] = -m[0] * m[5] * m[11] + 
               m[0] * m[7] * m[9] + 
               m[4] * m[1] * m[11] - 
               m[4] * m[3] * m[9] - 
               m[8] * m[1] * m[7] + 
               m[8] * m[3] * m[5];

    inv[15] = m[0] * m[5] * m[10] - 
              m[0] * m[6] * m[9] - 
              m[4] * m[1] * m[10] + 
              m[4] * m[2] * m[9] + 
              m[8] * m[1] * m[6] - 
              m[8] * m[2] * m[5];

    det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];

    // FFX_ASSERT(det != 0);
    if (det == 0) {
        memset(out, 0, sizeof(out));
        return;
    }

    det = 1.0 / det;

    for (i = 0; i < 16; i++)
        out[i] = inv[i] * det;
}

static void matrixMul(const FfxFloat32x4x4 a, const FfxFloat32x4x4 b, FfxFloat32x4x4 out)
{
    for (uint32_t a_row = 0; a_row < 4; ++a_row) {
        for (uint32_t b_col = 0; b_col < 4; ++b_col) {
            float sum = 0.0f;
            for (uint32_t i = 0; i < 4; ++i) {
                sum += a[a_row*4 + i] * b[i*4 + b_col];
            }
            out[a_row*4 + b_col] = sum;
        }
    }
}

static uint32_t getPingPongResourceId(FfxBrixelizerGIContext_Private* pContext, uint32_t pingPongId)
{
    return pContext->pingPongResourceIds[pingPongId - FFX_BRIXELIZER_GI_PING_PONG_RESOURCE_STATIC_GI_TARGET_READ];
}

static void setPingPongResourceId(FfxBrixelizerGIContext_Private* pContext, uint32_t pingPongId, uint32_t resourceId)
{
    pContext->pingPongResourceIds[pingPongId - FFX_BRIXELIZER_GI_PING_PONG_RESOURCE_STATIC_GI_TARGET_READ] = resourceId;
}

static uint32_t getNextScreenProbesId(uint32_t currentId)
{
    if (currentId == FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_STATIC_SCREEN_PROBES_0)
        return FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_STATIC_SCREEN_PROBES_1;
    else
        return FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_STATIC_SCREEN_PROBES_0;
}

static uint32_t getNextGITargetId(uint32_t currentId)
{
    if (currentId == FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_STATIC_GI_TARGET_0)
        return FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_STATIC_GI_TARGET_1;
    else
        return FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_STATIC_GI_TARGET_0;
}

static uint32_t getNextSpecularTargetId(uint32_t currentId)
{
    if (currentId == FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_SPECULAR_TARGET_0)
        return FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_SPECULAR_TARGET_1;
    else
        return FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_SPECULAR_TARGET_0;
}

static uint32_t getCurrentScreenProbesId(FfxBrixelizerGIContext_Private* pContext)
{
    return getNextScreenProbesId(pContext->historyScreenProbesId);
}

static uint32_t getCurrentGITargetId(FfxBrixelizerGIContext_Private* pContext)
{
    return getNextGITargetId(pContext->historyGITargetId);
}

static uint32_t getCurrentSpecularTargetId(FfxBrixelizerGIContext_Private* pContext)
{
    return getNextSpecularTargetId(pContext->historySpecularTargetId);
}

static void patchResourceBindings(FfxPipelineState* inoutPipeline)
{
    for (uint32_t srvTextureIndex = 0; srvTextureIndex < inoutPipeline->srvTextureCount; ++srvTextureIndex)
    {
        FfxResourceBinding& binding = inoutPipeline->srvTextureBindings[srvTextureIndex];

        int32_t mapIndex = 0;
        for (mapIndex = 0; mapIndex < _countof(srvResourceBindingTable); ++mapIndex)
        {
            if (0 == wcscmp(srvResourceBindingTable[mapIndex].name, binding.name))
                break;
        }
        if (mapIndex == _countof(srvResourceBindingTable))
            return;

        binding.resourceIdentifier = srvResourceBindingTable[mapIndex].index + binding.arrayIndex;
    }

    for (uint32_t srvBufferIndex = 0; srvBufferIndex < inoutPipeline->srvBufferCount; ++srvBufferIndex)
    {
        FfxResourceBinding& binding = inoutPipeline->srvBufferBindings[srvBufferIndex];

        int32_t mapIndex = 0;
        for (mapIndex = 0; mapIndex < _countof(srvResourceBindingTable); ++mapIndex)
        {
            if (0 == wcscmp(srvResourceBindingTable[mapIndex].name, binding.name))
                break;
        }
        if (mapIndex == _countof(srvResourceBindingTable))
            return;

        binding.resourceIdentifier = srvResourceBindingTable[mapIndex].index + binding.arrayIndex;
    }

    for (uint32_t uavTextureIndex = 0; uavTextureIndex < inoutPipeline->uavTextureCount; ++uavTextureIndex)
    {
        int32_t mapIndex = 0;
        for (mapIndex = 0; mapIndex < _countof(uavResourceBindingTable); ++mapIndex)
        {
            if (0 == wcscmp(uavResourceBindingTable[mapIndex].name, inoutPipeline->uavTextureBindings[uavTextureIndex].name))
                break;
        }
        if (mapIndex == _countof(uavResourceBindingTable))
            return;

        inoutPipeline->uavTextureBindings[uavTextureIndex].resourceIdentifier = uavResourceBindingTable[mapIndex].index;
    }

    for (uint32_t uavBufferIndex = 0; uavBufferIndex < inoutPipeline->uavBufferCount; ++uavBufferIndex)
    {
        FfxResourceBinding& binding = inoutPipeline->uavBufferBindings[uavBufferIndex];

        int32_t mapIndex = 0;
        for (mapIndex = 0; mapIndex < _countof(uavResourceBindingTable); ++mapIndex)
        {
            if (0 == wcscmp(uavResourceBindingTable[mapIndex].name, binding.name))
                break;
        }
        if (mapIndex == _countof(uavResourceBindingTable))
            return;

        binding.resourceIdentifier = uavResourceBindingTable[mapIndex].index + binding.arrayIndex;
    }

    for (uint32_t cbvIndex = 0; cbvIndex < inoutPipeline->constCount; ++cbvIndex)
    {
        FfxResourceBinding& binding = inoutPipeline->constantBufferBindings[cbvIndex];

        int32_t mapIndex = 0;
        for (mapIndex = 0; mapIndex < _countof(cbvResourceBindingTable); ++mapIndex)
        {
            if (0 == wcscmp(cbvResourceBindingTable[mapIndex].name, binding.name))
                break;
        }
        if (mapIndex == _countof(cbvResourceBindingTable))
            return;

        binding.resourceIdentifier = cbvResourceBindingTable[mapIndex].index;
    }
}

static uint32_t getPipelinePermutationFlags(uint32_t contextFlags, bool fp16, bool force64)
{
    // work out what permutation to load.
    uint32_t flags = 0;
    flags |= (contextFlags & FFX_BRIXELIZER_GI_FLAG_DEPTH_INVERTED) ? BRIXELIZER_GI_SHADER_PERMUTATION_DEPTH_INVERTED : 0;
    flags |= (contextFlags & FFX_BRIXELIZER_GI_FLAG_DISABLE_SPECULAR) ? BRIXELIZER_GI_SHADER_PERMUTATION_DISABLE_SPECULAR : 0;
    flags |= (contextFlags & FFX_BRIXELIZER_GI_FLAG_DISABLE_DENOISER) ? BRIXELIZER_GI_SHADER_PERMUTATION_DISABLE_DENOISER : 0;
    flags |= (force64) ? BRIXELIZER_GI_SHADER_PERMUTATION_FORCE_WAVE64 : 0;
    flags |= (fp16) ? BRIXELIZER_GI_SHADER_PERMUTATION_ALLOW_FP16 : 0;

    return flags;
}

static FfxErrorCode createPipelineStates(FfxBrixelizerGIContext_Private* pContext)
{
    FFX_ASSERT(pContext);

    const size_t          samplerCount = 4;
    FfxSamplerDescription samplers[samplerCount];

    samplers[0].filter       = FFX_FILTER_TYPE_MINMAGMIP_LINEAR;
    samplers[0].addressModeU = FFX_ADDRESS_MODE_CLAMP;
    samplers[0].addressModeV = FFX_ADDRESS_MODE_CLAMP;
    samplers[0].addressModeW = FFX_ADDRESS_MODE_CLAMP;
    samplers[0].stage        = FFX_BIND_COMPUTE_SHADER_STAGE;

    samplers[1].filter       = FFX_FILTER_TYPE_MINMAGMIP_POINT;
    samplers[1].addressModeU = FFX_ADDRESS_MODE_CLAMP;
    samplers[1].addressModeV = FFX_ADDRESS_MODE_CLAMP;
    samplers[1].addressModeW = FFX_ADDRESS_MODE_CLAMP;
    samplers[1].stage        = FFX_BIND_COMPUTE_SHADER_STAGE;

    samplers[2].filter       = FFX_FILTER_TYPE_MINMAGMIP_LINEAR;
    samplers[2].addressModeU = FFX_ADDRESS_MODE_WRAP;
    samplers[2].addressModeV = FFX_ADDRESS_MODE_WRAP;
    samplers[2].addressModeW = FFX_ADDRESS_MODE_WRAP;
    samplers[2].stage        = FFX_BIND_COMPUTE_SHADER_STAGE;

    samplers[3].filter       = FFX_FILTER_TYPE_MINMAGMIP_POINT;
    samplers[3].addressModeU = FFX_ADDRESS_MODE_WRAP;
    samplers[3].addressModeV = FFX_ADDRESS_MODE_WRAP;
    samplers[3].addressModeW = FFX_ADDRESS_MODE_WRAP;
    samplers[3].stage        = FFX_BIND_COMPUTE_SHADER_STAGE;

    FfxPipelineDescription pipelineDescription;
    memset(&pipelineDescription, 0, sizeof(FfxPipelineDescription));

    pipelineDescription.samplerCount = 4;
    pipelineDescription.samplers     = samplers;

    // Query device capabilities
    FfxDeviceCapabilities capabilities;
    pContext->contextDescription.backendInterface.fpGetDeviceCapabilities(&pContext->contextDescription.backendInterface, &capabilities);

    // Setup a few options used to determine permutation flags
    bool haveShaderModel66 = capabilities.maximumSupportedShaderModel >= FFX_SHADER_MODEL_6_6;
    bool supportedFP16     = capabilities.fp16Supported;
    bool canForceWave64    = false;

    const uint32_t waveLaneCountMin = capabilities.waveLaneCountMin;
    const uint32_t waveLaneCountMax = capabilities.waveLaneCountMax;
    if (waveLaneCountMin <= 64 && waveLaneCountMax >= 64)
        canForceWave64 = haveShaderModel66;
    else
        canForceWave64 = false;

    // Wave64 disabled due to negative impact on performance
    uint32_t pipelineFlags = getPipelinePermutationFlags(pContext->contextDescription.flags, supportedFP16, false /*canForceWave64*/); 

    // Set up pipeline descriptor (basically RootSignature and binding)
    wcscpy_s(pipelineDescription.name, L"FFX_BRIXELIZER_GI_PASS_PREPARE_CLEAR_CACHE");
    FFX_VALIDATE(pContext->contextDescription.backendInterface.fpCreatePipeline(&pContext->contextDescription.backendInterface,
                                                                                FFX_EFFECT_BRIXELIZER_GI,
                                                                                FFX_BRIXELIZER_GI_PASS_PREPARE_CLEAR_CACHE,
                                                                                pipelineFlags,
                                                                                &pipelineDescription,
                                                                                pContext->effectContextId,
                                                                                &pContext->pipelinePrepareClearCache));
    pipelineDescription.indirectWorkload = 1;
    wcscpy_s(pipelineDescription.name, L"FFX_BRIXELIZER_GI_PASS_CLEAR_CACHE");
    FFX_VALIDATE(pContext->contextDescription.backendInterface.fpCreatePipeline(&pContext->contextDescription.backendInterface,
                                                                                FFX_EFFECT_BRIXELIZER_GI,
                                                                                FFX_BRIXELIZER_GI_PASS_CLEAR_CACHE,
                                                                                pipelineFlags,
                                                                                &pipelineDescription,
                                                                                pContext->effectContextId,
                                                                                &pContext->pipelineClearCache));
    pipelineDescription.indirectWorkload = 0;
    wcscpy_s(pipelineDescription.name, L"FFX_BRIXELIZER_GI_PASS_EMIT_PRIMARY_RAY_RADIANCE");
    FFX_VALIDATE(pContext->contextDescription.backendInterface.fpCreatePipeline(&pContext->contextDescription.backendInterface,
                                                                                FFX_EFFECT_BRIXELIZER_GI,
                                                                                FFX_BRIXELIZER_GI_PASS_EMIT_PRIMARY_RAY_RADIANCE,
                                                                                pipelineFlags,
                                                                                &pipelineDescription,
                                                                                pContext->effectContextId,
                                                                                &pContext->pipelineEmitPrimaryRayRadiance));
    wcscpy_s(pipelineDescription.name, L"FFX_BRIXELIZER_GI_PASS_PROPAGATE_SH");
    FFX_VALIDATE(pContext->contextDescription.backendInterface.fpCreatePipeline(&pContext->contextDescription.backendInterface,
                                                                                FFX_EFFECT_BRIXELIZER_GI,
                                                                                FFX_BRIXELIZER_GI_PASS_PROPAGATE_SH,
                                                                                pipelineFlags,
                                                                                &pipelineDescription,
                                                                                pContext->effectContextId,
                                                                                &pContext->pipelinePropagateSH));
    pipelineDescription.indirectWorkload = 0;
    wcscpy_s(pipelineDescription.name, L"FFX_BRIXELIZER_GI_PASS_SLOTS_CLAIM_SAME");
    FFX_VALIDATE(pContext->contextDescription.backendInterface.fpCreatePipeline(&pContext->contextDescription.backendInterface,
                                                                                FFX_EFFECT_BRIXELIZER_GI,
                                                                                FFX_BRIXELIZER_GI_PASS_SLOTS_CLAIM_SAME,
                                                                                pipelineFlags,
                                                                                &pipelineDescription,
                                                                                pContext->effectContextId,
                                                                                &pContext->pipelineSlotsClaimSame));
    wcscpy_s(pipelineDescription.name, L"FFX_BRIXELIZER_GI_PASS_SLOTS_CLAIM_MOVER");
    FFX_VALIDATE(pContext->contextDescription.backendInterface.fpCreatePipeline(&pContext->contextDescription.backendInterface,
                                                                                FFX_EFFECT_BRIXELIZER_GI,
                                                                                FFX_BRIXELIZER_GI_PASS_SLOTS_CLAIM_MOVER,
                                                                                pipelineFlags,
                                                                                &pipelineDescription,
                                                                                pContext->effectContextId,
                                                                                &pContext->pipelineSlotsClaimMover));
    wcscpy_s(pipelineDescription.name, L"FFX_BRIXELIZER_GI_PASS_SLOTS_FREE");
    FFX_VALIDATE(pContext->contextDescription.backendInterface.fpCreatePipeline(&pContext->contextDescription.backendInterface,
                                                                                FFX_EFFECT_BRIXELIZER_GI,
                                                                                FFX_BRIXELIZER_GI_PASS_SLOTS_FREE,
                                                                                pipelineFlags,
                                                                                &pipelineDescription,
                                                                                pContext->effectContextId,
                                                                                &pContext->pipelineSlotsFree));
    wcscpy_s(pipelineDescription.name, L"FFX_BRIXELIZER_GI_PASS_SLOTS_ALLOC");
    FFX_VALIDATE(pContext->contextDescription.backendInterface.fpCreatePipeline(&pContext->contextDescription.backendInterface,
                                                                                FFX_EFFECT_BRIXELIZER_GI,
                                                                                FFX_BRIXELIZER_GI_PASS_SLOTS_ALLOC,
                                                                                pipelineFlags,
                                                                                &pipelineDescription,
                                                                                pContext->effectContextId,
                                                                                &pContext->pipelineSlotsAlloc));
    wcscpy_s(pipelineDescription.name, L"FFX_BRIXELIZER_GI_PASS_SLOTS_APPLY");
    FFX_VALIDATE(pContext->contextDescription.backendInterface.fpCreatePipeline(&pContext->contextDescription.backendInterface,
                                                                                FFX_EFFECT_BRIXELIZER_GI,
                                                                                FFX_BRIXELIZER_GI_PASS_SLOTS_APPLY,
                                                                                pipelineFlags,
                                                                                &pipelineDescription,
                                                                                pContext->effectContextId,
                                                                                &pContext->pipelineSlotsApply));
    wcscpy_s(pipelineDescription.name, L"FFX_BRIXELIZER_GI_PASS_SLOTS_SEED");
    FFX_VALIDATE(pContext->contextDescription.backendInterface.fpCreatePipeline(&pContext->contextDescription.backendInterface,
                                                                                FFX_EFFECT_BRIXELIZER_GI,
                                                                                FFX_BRIXELIZER_GI_PASS_SLOTS_SEED,
                                                                                pipelineFlags,
                                                                                &pipelineDescription,
                                                                                pContext->effectContextId,
                                                                                &pContext->pipelineSlotsSeed));
    wcscpy_s(pipelineDescription.name, L"FFX_BRIXELIZER_GI_PASS_SLOTS_STATS");
    FFX_VALIDATE(pContext->contextDescription.backendInterface.fpCreatePipeline(&pContext->contextDescription.backendInterface,
                                                                                FFX_EFFECT_BRIXELIZER_GI,
                                                                                FFX_BRIXELIZER_GI_PASS_SLOTS_STATS,
                                                                                pipelineFlags,
                                                                                &pipelineDescription,
                                                                                pContext->effectContextId,
                                                                                &pContext->pipelineSlotsStats));
    wcscpy_s(pipelineDescription.name, L"FFX_BRIXELIZER_GI_PASS_CACHE_FEED");
    FFX_VALIDATE(pContext->contextDescription.backendInterface.fpCreatePipeline(&pContext->contextDescription.backendInterface,
                                                                                FFX_EFFECT_BRIXELIZER_GI,
                                                                                FFX_BRIXELIZER_GI_PASS_CACHE_FEED,
                                                                                pipelineFlags,
                                                                                &pipelineDescription,
                                                                                pContext->effectContextId,
                                                                                &pContext->pipelineCacheFeed));
    wcscpy_s(pipelineDescription.name, L"FFX_BRIXELIZER_GI_PASS_SLOTS_RESAMPLE");
    FFX_VALIDATE(pContext->contextDescription.backendInterface.fpCreatePipeline(&pContext->contextDescription.backendInterface,
                                                                                FFX_EFFECT_BRIXELIZER_GI,
                                                                                FFX_BRIXELIZER_GI_PASS_SLOTS_RESAMPLE,
                                                                                pipelineFlags,
                                                                                &pipelineDescription,
                                                                                pContext->effectContextId,
                                                                                &pContext->pipelineSlotsResample));
wcscpy_s(pipelineDescription.name, L"FFX_BRIXELIZER_GI_PASS_SPAWN_SCREEN_PROBES");
    FFX_VALIDATE(pContext->contextDescription.backendInterface.fpCreatePipeline(&pContext->contextDescription.backendInterface,
                                                                                FFX_EFFECT_BRIXELIZER_GI,
                                                                                FFX_BRIXELIZER_GI_PASS_SPAWN_SCREEN_PROBES,
                                                                                pipelineFlags,
                                                                                &pipelineDescription,
                                                                                pContext->effectContextId,
                                                                                &pContext->pipelineSpawnScreenProbes));
    wcscpy_s(pipelineDescription.name, L"FFX_BRIXELIZER_GI_PASS_REPROJECT_SCREEN_PROBES");
    FFX_VALIDATE(pContext->contextDescription.backendInterface.fpCreatePipeline(&pContext->contextDescription.backendInterface,
                                                                                FFX_EFFECT_BRIXELIZER_GI,
                                                                                FFX_BRIXELIZER_GI_PASS_REPROJECT_SCREEN_PROBES,
                                                                                pipelineFlags,
                                                                                &pipelineDescription,
                                                                                pContext->effectContextId,
                                                                                &pContext->pipelineReprojectScreenProbes));
    wcscpy_s(pipelineDescription.name, L"FFX_BRIXELIZER_GI_PASS_FILL_SCREEN_PROBES");
    FFX_VALIDATE(pContext->contextDescription.backendInterface.fpCreatePipeline(&pContext->contextDescription.backendInterface,
                                                                                FFX_EFFECT_BRIXELIZER_GI,
                                                                                FFX_BRIXELIZER_GI_PASS_FILL_SCREEN_PROBES,
                                                                                pipelineFlags,
                                                                                &pipelineDescription,
                                                                                pContext->effectContextId,
                                                                                &pContext->pipelineFillScreenProbes));
    wcscpy_s(pipelineDescription.name, L"FFX_BRIXELIZER_GI_PASS_SPECULAR_PRE_TRACE");
    FFX_VALIDATE(pContext->contextDescription.backendInterface.fpCreatePipeline(&pContext->contextDescription.backendInterface,
                                                                                FFX_EFFECT_BRIXELIZER_GI,
                                                                                FFX_BRIXELIZER_GI_PASS_SPECULAR_PRE_TRACE,
                                                                                pipelineFlags,
                                                                                &pipelineDescription,
                                                                                pContext->effectContextId,
                                                                                &pContext->pipelineSpecularPreTrace));
    wcscpy_s(pipelineDescription.name, L"FFX_BRIXELIZER_GI_PASS_SPECULAR_TRACE");
    FFX_VALIDATE(pContext->contextDescription.backendInterface.fpCreatePipeline(&pContext->contextDescription.backendInterface,
                                                                                FFX_EFFECT_BRIXELIZER_GI,
                                                                                FFX_BRIXELIZER_GI_PASS_SPECULAR_TRACE,
                                                                                pipelineFlags,
                                                                                &pipelineDescription,
                                                                                pContext->effectContextId,
                                                                                &pContext->pipelineSpecularTrace));
    wcscpy_s(pipelineDescription.name, L"FFX_BRIXELIZER_GI_PASS_REPROJECT_GI");
    FFX_VALIDATE(pContext->contextDescription.backendInterface.fpCreatePipeline(&pContext->contextDescription.backendInterface,
                                                                                FFX_EFFECT_BRIXELIZER_GI,
                                                                                FFX_BRIXELIZER_GI_PASS_REPROJECT_GI,
                                                                                pipelineFlags,
                                                                                &pipelineDescription,
                                                                                pContext->effectContextId,
                                                                                &pContext->pipelineReprojectGI));
    wcscpy_s(pipelineDescription.name, L"FFX_BRIXELIZER_GI_PASS_PROJECT_SCREEN_PROBES");
    FFX_VALIDATE(pContext->contextDescription.backendInterface.fpCreatePipeline(&pContext->contextDescription.backendInterface,
                                                                                FFX_EFFECT_BRIXELIZER_GI,
                                                                                FFX_BRIXELIZER_GI_PASS_PROJECT_SCREEN_PROBES,
                                                                                pipelineFlags,
                                                                                &pipelineDescription,
                                                                                pContext->effectContextId,
                                                                                &pContext->pipelineProjectScreenProbes));
    wcscpy_s(pipelineDescription.name, L"FFX_BRIXELIZER_GI_PASS_EMIT_IRRADIANCE_CACHE");
    FFX_VALIDATE(pContext->contextDescription.backendInterface.fpCreatePipeline(&pContext->contextDescription.backendInterface,
                                                                                FFX_EFFECT_BRIXELIZER_GI,
                                                                                FFX_BRIXELIZER_GI_PASS_EMIT_IRRADIANCE_CACHE,
                                                                                pipelineFlags,
                                                                                &pipelineDescription,
                                                                                pContext->effectContextId,
                                                                                &pContext->pipelineEmitIrradianceCache));
    wcscpy_s(pipelineDescription.name, L"FFX_BRIXELIZER_GI_PASS_INTERPOLATE_SCREEN_PROBES");
    FFX_VALIDATE(pContext->contextDescription.backendInterface.fpCreatePipeline(&pContext->contextDescription.backendInterface,
                                                                                FFX_EFFECT_BRIXELIZER_GI,
                                                                                FFX_BRIXELIZER_GI_PASS_INTERPOLATE_SCREEN_PROBES,
                                                                                pipelineFlags,
                                                                                &pipelineDescription,
                                                                                pContext->effectContextId,
                                                                                &pContext->pipelineInterpolateScreenProbes));
    wcscpy_s(pipelineDescription.name, L"FFX_BRIXELIZER_GI_PASS_BLUR_X");
    FFX_VALIDATE(pContext->contextDescription.backendInterface.fpCreatePipeline(&pContext->contextDescription.backendInterface,
                                                                                FFX_EFFECT_BRIXELIZER_GI,
                                                                                FFX_BRIXELIZER_GI_PASS_BLUR_X,
                                                                                pipelineFlags,
                                                                                &pipelineDescription,
                                                                                pContext->effectContextId,
                                                                                &pContext->pipelineBlurX));
    wcscpy_s(pipelineDescription.name, L"FFX_BRIXELIZER_GI_PASS_BLUR_Y");
    FFX_VALIDATE(pContext->contextDescription.backendInterface.fpCreatePipeline(&pContext->contextDescription.backendInterface,
                                                                                FFX_EFFECT_BRIXELIZER_GI,
                                                                                FFX_BRIXELIZER_GI_PASS_BLUR_Y,
                                                                                pipelineFlags,
                                                                                &pipelineDescription,
                                                                                pContext->effectContextId,
                                                                                &pContext->pipelineBlurY));
    wcscpy_s(pipelineDescription.name, L"FFX_BRIXELIZER_GI_PASS_DEBUG_VISUALIZATION");
    FFX_VALIDATE(pContext->contextDescription.backendInterface.fpCreatePipeline(&pContext->contextDescription.backendInterface,
                                                                                FFX_EFFECT_BRIXELIZER_GI,
                                                                                FFX_BRIXELIZER_GI_PASS_DEBUG_VISUALIZATION,
                                                                                pipelineFlags,
                                                                                &pipelineDescription,
                                                                                pContext->effectContextId,
                                                                                &pContext->pipelineDebugVisualization));
    wcscpy_s(pipelineDescription.name, L"FFX_BRIXELIZER_GI_PASS_GENERATE_DISOCCLUSION_MASK");
    FFX_VALIDATE(pContext->contextDescription.backendInterface.fpCreatePipeline(&pContext->contextDescription.backendInterface,
                                                                                FFX_EFFECT_BRIXELIZER_GI,
                                                                                FFX_BRIXELIZER_GI_PASS_GENERATE_DISOCCLUSION_MASK,
                                                                                pipelineFlags,
                                                                                &pipelineDescription,
                                                                                pContext->effectContextId,
                                                                                &pContext->pipelineGenerateDisocclusionMask));
    wcscpy_s(pipelineDescription.name, L"FFX_BRIXELIZER_GI_PASS_DOWNSAMPLE");
    FFX_VALIDATE(pContext->contextDescription.backendInterface.fpCreatePipeline(&pContext->contextDescription.backendInterface,
                                                                                FFX_EFFECT_BRIXELIZER_GI,
                                                                                FFX_BRIXELIZER_GI_PASS_DOWNSAMPLE,
                                                                                pipelineFlags,
                                                                                &pipelineDescription,
                                                                                pContext->effectContextId,
                                                                                &pContext->pipelineDownsample));
    wcscpy_s(pipelineDescription.name, L"FFX_BRIXELIZER_GI_PASS_UPSAMPLE");
    FFX_VALIDATE(pContext->contextDescription.backendInterface.fpCreatePipeline(&pContext->contextDescription.backendInterface,
                                                                                FFX_EFFECT_BRIXELIZER_GI,
                                                                                FFX_BRIXELIZER_GI_PASS_UPSAMPLE,
                                                                                pipelineFlags,
                                                                                &pipelineDescription,
                                                                                pContext->effectContextId,
                                                                                &pContext->pipelineUpsample));
    patchResourceBindings(&pContext->pipelinePrepareClearCache);
    patchResourceBindings(&pContext->pipelineClearCache);
    patchResourceBindings(&pContext->pipelineEmitPrimaryRayRadiance);
    patchResourceBindings(&pContext->pipelinePropagateSH);
    patchResourceBindings(&pContext->pipelineSlotsClaimSame);
    patchResourceBindings(&pContext->pipelineSlotsClaimMover);
    patchResourceBindings(&pContext->pipelineSlotsFree);
    patchResourceBindings(&pContext->pipelineSlotsAlloc);
    patchResourceBindings(&pContext->pipelineSlotsApply);
    patchResourceBindings(&pContext->pipelineSlotsSeed);
    patchResourceBindings(&pContext->pipelineSlotsStats);
    patchResourceBindings(&pContext->pipelineSlotsResample);
    patchResourceBindings(&pContext->pipelineCacheFeed);
patchResourceBindings(&pContext->pipelineSpawnScreenProbes);
    patchResourceBindings(&pContext->pipelineReprojectScreenProbes);
    patchResourceBindings(&pContext->pipelineFillScreenProbes);
    patchResourceBindings(&pContext->pipelineSpecularPreTrace);
    patchResourceBindings(&pContext->pipelineSpecularTrace);
    patchResourceBindings(&pContext->pipelineReprojectGI);
    patchResourceBindings(&pContext->pipelineProjectScreenProbes);
    patchResourceBindings(&pContext->pipelineEmitIrradianceCache);
    patchResourceBindings(&pContext->pipelineInterpolateScreenProbes);
    patchResourceBindings(&pContext->pipelineBlurX);
    patchResourceBindings(&pContext->pipelineBlurY);
    patchResourceBindings(&pContext->pipelineDebugVisualization);
    patchResourceBindings(&pContext->pipelineGenerateDisocclusionMask);
    patchResourceBindings(&pContext->pipelineDownsample);
    patchResourceBindings(&pContext->pipelineUpsample);

    return FFX_OK;
}

static void scheduleDispatchInternal(FfxBrixelizerGIContext_Private* pContext,
                                     const FfxPipelineState*         pipeline,
                                     uint32_t                        dispatchX,
                                     uint32_t                        dispatchY,
                                     uint32_t                        dispatchZ,
                                     FfxResourceInternal             indirectArgsBuffer,
                                     uint32_t                        indirectArgsOffset)
{
    pContext->gpuJobDescription = {FFX_GPU_JOB_COMPUTE};

    wcscpy_s(pContext->gpuJobDescription.jobLabel, pipeline->name);

    FFX_ASSERT(pipeline->srvTextureCount < FFX_MAX_NUM_SRVS);

    for (uint32_t currentShaderResourceViewIndex = 0; currentShaderResourceViewIndex < pipeline->srvTextureCount; ++currentShaderResourceViewIndex)
    {
        const uint32_t            baseResourceId    = pipeline->srvTextureBindings[currentShaderResourceViewIndex].resourceIdentifier;
        const uint32_t            currentResourceId = isPingPongResource(baseResourceId) ? getPingPongResourceId(pContext, baseResourceId) : baseResourceId;
        const FfxResourceInternal currentResource   = pContext->resources[currentResourceId];

        pContext->gpuJobDescription.computeJobDescriptor.srvTextures[currentShaderResourceViewIndex].resource = currentResource;
#ifdef FFX_DEBUG
        wcscpy_s(pContext->gpuJobDescription.computeJobDescriptor.srvTextures[currentShaderResourceViewIndex].name,
                 pipeline->srvTextureBindings[currentShaderResourceViewIndex].name);
#endif
    }

    FFX_ASSERT(pipeline->srvBufferCount < FFX_MAX_NUM_SRVS);

    for (uint32_t currentShaderResourceViewIndex = 0; currentShaderResourceViewIndex < pipeline->srvBufferCount; ++currentShaderResourceViewIndex)
    {
        const uint32_t            baseResourceId    = pipeline->srvBufferBindings[currentShaderResourceViewIndex].resourceIdentifier;
        const uint32_t            currentResourceId = isPingPongResource(baseResourceId) ? getPingPongResourceId(pContext, baseResourceId) : baseResourceId;
        const FfxResourceInternal currentResource   = pContext->resources[currentResourceId];

        pContext->gpuJobDescription.computeJobDescriptor.srvBuffers[currentShaderResourceViewIndex].resource = currentResource;
        pContext->gpuJobDescription.computeJobDescriptor.srvBuffers[currentShaderResourceViewIndex].offset   = 0;
        pContext->gpuJobDescription.computeJobDescriptor.srvBuffers[currentShaderResourceViewIndex].size     = 0;
        pContext->gpuJobDescription.computeJobDescriptor.srvBuffers[currentShaderResourceViewIndex].stride   = 0;
#ifdef FFX_DEBUG
        wcscpy_s(pContext->gpuJobDescription.computeJobDescriptor.srvBuffers[currentShaderResourceViewIndex].name, pipeline->srvBufferBindings[currentShaderResourceViewIndex].name);
#endif
    }

    FFX_ASSERT(pipeline->uavTextureCount < FFX_MAX_NUM_UAVS);

    for (uint32_t currentUnorderedAccessViewIndex = 0; currentUnorderedAccessViewIndex < pipeline->uavTextureCount; ++currentUnorderedAccessViewIndex)
    {
        const uint32_t            baseResourceId    = pipeline->uavTextureBindings[currentUnorderedAccessViewIndex].resourceIdentifier;
        const uint32_t            currentResourceId = isPingPongResource(baseResourceId) ? getPingPongResourceId(pContext, baseResourceId) : baseResourceId;
        const FfxResourceInternal currentResource   = pContext->resources[currentResourceId];

        pContext->gpuJobDescription.computeJobDescriptor.uavTextures[currentUnorderedAccessViewIndex].resource = currentResource;
        pContext->gpuJobDescription.computeJobDescriptor.uavTextures[currentUnorderedAccessViewIndex].mip      = 0;
#ifdef FFX_DEBUG
        wcscpy_s(pContext->gpuJobDescription.computeJobDescriptor.uavTextures[currentUnorderedAccessViewIndex].name, pipeline->uavTextureBindings[currentUnorderedAccessViewIndex].name);
#endif
    }

    FFX_ASSERT(pipeline->uavBufferCount < FFX_MAX_NUM_UAVS);

    for (uint32_t currentUnorderedAccessViewIndex = 0; currentUnorderedAccessViewIndex < pipeline->uavBufferCount; ++currentUnorderedAccessViewIndex)
    {
        const uint32_t            baseResourceId    = pipeline->uavBufferBindings[currentUnorderedAccessViewIndex].resourceIdentifier;
        const uint32_t            currentResourceId = isPingPongResource(baseResourceId) ? getPingPongResourceId(pContext, baseResourceId) : baseResourceId;
        const FfxResourceInternal currentResource   = pContext->resources[currentResourceId];

        pContext->gpuJobDescription.computeJobDescriptor.uavBuffers[currentUnorderedAccessViewIndex].resource = currentResource;
        pContext->gpuJobDescription.computeJobDescriptor.uavBuffers[currentUnorderedAccessViewIndex].offset   = 0;
        pContext->gpuJobDescription.computeJobDescriptor.uavBuffers[currentUnorderedAccessViewIndex].size     = 0;
        pContext->gpuJobDescription.computeJobDescriptor.uavBuffers[currentUnorderedAccessViewIndex].stride   = 0;
#ifdef FFX_DEBUG
        wcscpy_s(pContext->gpuJobDescription.computeJobDescriptor.uavBuffers[currentUnorderedAccessViewIndex].name, pipeline->uavBufferBindings[currentUnorderedAccessViewIndex].name);
#endif
    }

    FFX_ASSERT(pipeline->constCount < FFX_MAX_NUM_CONST_BUFFERS);

    for (uint32_t currentConstantBufferViewIndex = 0; currentConstantBufferViewIndex < pipeline->constCount; ++currentConstantBufferViewIndex)
    {
        const uint32_t currentResourceId = pipeline->constantBufferBindings[currentConstantBufferViewIndex].resourceIdentifier;

        uint32_t cbvInfoIdx = 0;

        for (cbvInfoIdx = 0; cbvInfoIdx < _countof(cbvResourceBindingTable); cbvInfoIdx++)
        {
            if (cbvResourceBindingTable[cbvInfoIdx].index == currentResourceId)
                break;
        }

        pContext->gpuJobDescription.computeJobDescriptor.cbs[currentConstantBufferViewIndex] = pContext->constantBuffers[cbvInfoIdx];
#ifdef FFX_DEBUG
        wcscpy_s(pContext->gpuJobDescription.computeJobDescriptor.cbNames[currentConstantBufferViewIndex], pipeline->constantBufferBindings[currentConstantBufferViewIndex].name);
#endif
    }

    pContext->gpuJobDescription.computeJobDescriptor.dimensions[0]     = dispatchX;
    pContext->gpuJobDescription.computeJobDescriptor.dimensions[1]     = dispatchY;
    pContext->gpuJobDescription.computeJobDescriptor.dimensions[2]     = dispatchZ;
    pContext->gpuJobDescription.computeJobDescriptor.pipeline          = *pipeline;
    pContext->gpuJobDescription.computeJobDescriptor.cmdArgument       = indirectArgsBuffer;
    pContext->gpuJobDescription.computeJobDescriptor.cmdArgumentOffset = indirectArgsOffset;

    pContext->contextDescription.backendInterface.fpScheduleGpuJob(&pContext->contextDescription.backendInterface, &pContext->gpuJobDescription);
}

static void scheduleCopy(FfxBrixelizerGIContext_Private* pContext,
                         FfxResourceInternal             src,
                         uint32_t                        srcOffset,
                         FfxResourceInternal             dst,
                         uint32_t                        dstOffset,
                         uint32_t                        size,
                         const wchar_t*                  name)
{
    pContext->gpuJobDescription = {FFX_GPU_JOB_COPY};

    wcscpy_s(pContext->gpuJobDescription.jobLabel, name);

    pContext->gpuJobDescription.copyJobDescriptor.src       = src;
    pContext->gpuJobDescription.copyJobDescriptor.srcOffset = srcOffset;
    pContext->gpuJobDescription.copyJobDescriptor.dst       = dst;
    pContext->gpuJobDescription.copyJobDescriptor.dstOffset = dstOffset;
    pContext->gpuJobDescription.copyJobDescriptor.size      = size;

    pContext->contextDescription.backendInterface.fpScheduleGpuJob(&pContext->contextDescription.backendInterface, &pContext->gpuJobDescription);
}

static void scheduleDispatch(FfxBrixelizerGIContext_Private* context, 
                             const FfxPipelineState*         pipeline, 
                             uint32_t                        dispatchX, 
                             uint32_t                        dispatchY, 
                             uint32_t                        dispatchZ)
{
    scheduleDispatchInternal(context, pipeline, dispatchX, dispatchY, dispatchZ, {FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_NULL}, 0);
}

static void scheduleIndirectDispatch(FfxBrixelizerGIContext_Private* context,
                                     const FfxPipelineState*          pipeline,
                                     FfxResourceInternal              indirectArgsBuffer,
                                     uint32_t                         indirectArgsOffset)
{
    scheduleDispatchInternal(context, pipeline, 0, 0, 0, indirectArgsBuffer, indirectArgsOffset);
}

bool isResourceNull(FfxResource resource)
{
    return resource.resource == NULL;
}

static void updateConstantBuffer(FfxBrixelizerGIContext_Private* pContext, uint32_t id, void* data)
{
    pContext->contextDescription.backendInterface.fpStageConstantBufferDataFunc(&pContext->contextDescription.backendInterface, data, cbSizes[id], &pContext->constantBuffers[id]);
}

// vkfw: the resources FFX_BRIXELIZER_GI_FLAG_EXTERNAL_RADIANCE_CACHE / _EXTERNAL_BRICK_CACHE hand to the application.
static bool brixelizerGIIsExternalResource(uint32_t flags, uint32_t resourceId)
{
    if ((flags & FFX_BRIXELIZER_GI_FLAG_EXTERNAL_RADIANCE_CACHE) && resourceId == FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_RADIANCE_CACHE)
        return true;
    if ((flags & FFX_BRIXELIZER_GI_FLAG_CACHE_SLOTS) &&
        (resourceId == FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_BRICK_SLOT_MAP || resourceId == FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_SLOT_COUNTERS ||
         resourceId == FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_SLOT_STATE || resourceId == FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_SLOT_CLAIM ||
         resourceId == FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_SLOT_FREE_LIST || resourceId == FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_LEVEL_SLOT_MAPS))
        return true;
    if ((flags & FFX_BRIXELIZER_GI_FLAG_EXTERNAL_BRICK_CACHE) &&
        (resourceId == FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_BRICKS_SH|| resourceId == FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_BRICKS_DIRECT_SH ||
         resourceId == FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_BRICKS_SH_STATE))
        return true;
    return false;
}

// vkfw: register the application's caches in place of the internal resources they replace.
// slotState: the dispatch's slotState and the three after it (the slot passes'), or NULL where nothing runs them (the debug view).
static void brixelizerGIRegisterExternalResources(FfxBrixelizerGIContext_Private* pContext, const FfxResource* radianceCache,
    const FfxResource* bricksSH, const FfxResource* bricksDirectSH, const FfxResource* bricksSHState, const FfxResource* brickSlotMap, const FfxResource* slotCounters,
    const FfxResource* slotState, const FfxResource* emptyBricks)
{
    FfxInterface& backend = pContext->contextDescription.backendInterface;
    if (pContext->contextDescription.flags & FFX_BRIXELIZER_GI_FLAG_CACHE_SLOTS)
    {
        backend.fpRegisterResource(&backend, brickSlotMap, pContext->effectContextId, &pContext->resources[FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_BRICK_SLOT_MAP]);
        if (slotCounters->resource != NULL)
            backend.fpRegisterResource(&backend, slotCounters, pContext->effectContextId, &pContext->resources[FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_SLOT_COUNTERS]);
        // vkfw patch: the empty-brick list, or the counters in its place (the passes write it only with empty_max).
        const FfxResource* emptyList = (emptyBricks && emptyBricks->resource != NULL) ? emptyBricks : slotCounters;
        if (emptyList->resource != NULL)
            backend.fpRegisterResource(&backend, emptyList, pContext->effectContextId, &pContext->resources[FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_EMPTY_BRICKS]);
        static const uint32_t stateIds[] = {FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_SLOT_STATE, FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_SLOT_CLAIM,
                                            FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_SLOT_FREE_LIST, FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_LEVEL_SLOT_MAPS};
        for (uint32_t i = 0; slotState && i < FFX_ARRAY_ELEMENTS(stateIds); ++i)
            if (slotState[i].resource != NULL)
                backend.fpRegisterResource(&backend, &slotState[i], pContext->effectContextId, &pContext->resources[stateIds[i]]);
    }
    if (pContext->contextDescription.flags & FFX_BRIXELIZER_GI_FLAG_EXTERNAL_RADIANCE_CACHE)
        backend.fpRegisterResource(&backend, radianceCache, pContext->effectContextId, &pContext->resources[FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_RADIANCE_CACHE]);
    if (pContext->contextDescription.flags & FFX_BRIXELIZER_GI_FLAG_EXTERNAL_BRICK_CACHE)
    {
        backend.fpRegisterResource(&backend, bricksSH, pContext->effectContextId, &pContext->resources[FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_BRICKS_SH]);
        backend.fpRegisterResource(&backend, bricksDirectSH, pContext->effectContextId, &pContext->resources[FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_BRICKS_DIRECT_SH]);
        backend.fpRegisterResource(&backend, bricksSHState, pContext->effectContextId, &pContext->resources[FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_BRICKS_SH_STATE]);
    }
}

static FfxErrorCode brixelizerGICreate(FfxBrixelizerGIContext_Private* pContext, const FfxBrixelizerGIContextDescription* pContextDescription)
{
    FFX_ASSERT(pContext);
    FFX_ASSERT(pContextDescription);

    if (pContextDescription->internalResolution != FFX_BRIXELIZER_GI_INTERNAL_RESOLUTION_NATIVE &&
        (pContextDescription->flags & FFX_BRIXELIZER_GI_FLAG_DISABLE_DENOISER) == FFX_BRIXELIZER_GI_FLAG_DISABLE_DENOISER)
    {
        // Denoiser can only be disabled at Native resolution.
        FFX_ASSERT(false);
        return FFX_ERROR_INVALID_ARGUMENT;
    }

    // Setup the data for implementation.
    memset(pContext, 0, sizeof(FfxBrixelizerGIContext_Private));
    pContext->device = pContextDescription->backendInterface.device;

    memcpy(&pContext->contextDescription, pContextDescription, sizeof(FfxBrixelizerGIContextDescription));

    // Create the device.
    FfxErrorCode errorCode = pContext->contextDescription.backendInterface.fpCreateBackendContext(&pContext->contextDescription.backendInterface, FFX_EFFECT_BRIXELIZER_GI, nullptr, &pContext->effectContextId);
    FFX_RETURN_ON_ERROR(errorCode == FFX_OK, errorCode);

    // call out for device caps.
    errorCode = pContext->contextDescription.backendInterface.fpGetDeviceCapabilities(&pContext->contextDescription.backendInterface, &pContext->deviceCapabilities);
    FFX_RETURN_ON_ERROR(errorCode == FFX_OK, errorCode);

    errorCode = createPipelineStates(pContext);
    FFX_RETURN_ON_ERROR(errorCode == FFX_OK, errorCode);

    pContext->currentScreenProbesId   = FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_STATIC_SCREEN_PROBES_0;
    pContext->currentGITargetId       = FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_STATIC_GI_TARGET_0;
    pContext->currentSpecularTargetId = FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_SPECULAR_TARGET_0;

    const float scalingOptions[] = {1.0f, 0.75f, 0.5f, 0.25f};

    pContext->internalSize.width  = static_cast<float>(pContextDescription->displaySize.width) * scalingOptions[pContextDescription->internalResolution];
    pContext->internalSize.height = static_cast<float>(pContextDescription->displaySize.height) * scalingOptions[pContextDescription->internalResolution];

    uint32_t probeBufferWidth  = FFX_BRIXELIZER_GI_SCREEN_PROBE_SIZE * ((pContext->internalSize.width + FFX_BRIXELIZER_GI_SCREEN_PROBE_SIZE - 1) / FFX_BRIXELIZER_GI_SCREEN_PROBE_SIZE);
    uint32_t probeBufferHeight = FFX_BRIXELIZER_GI_SCREEN_PROBE_SIZE * ((pContext->internalSize.height + FFX_BRIXELIZER_GI_SCREEN_PROBE_SIZE - 1) / FFX_BRIXELIZER_GI_SCREEN_PROBE_SIZE);
    
    uint32_t tileBufferWidth  = ((pContext->internalSize.width + FFX_BRIXELIZER_GI_SCREEN_PROBE_SIZE - 1) / FFX_BRIXELIZER_GI_SCREEN_PROBE_SIZE);
    uint32_t tileBufferHeight = ((pContext->internalSize.height + FFX_BRIXELIZER_GI_SCREEN_PROBE_SIZE - 1) / FFX_BRIXELIZER_GI_SCREEN_PROBE_SIZE);

    const FfxResourceInitData initData = {FFX_RESOURCE_INIT_DATA_TYPE_UNINITIALIZED, 0, nullptr};

    // Create GPU-local resources.
    {
        const FfxInternalResourceDescription internalSurfaceDesc[] = {
            {FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_RADIANCE_CACHE,
             L"BrixelizerGI_RadianceCache",
             FFX_RESOURCE_TYPE_TEXTURE3D,
             FFX_RESOURCE_USAGE_UAV,
             FFX_SURFACE_FORMAT_R11G11B10_FLOAT,
             FFX_BRIXELIZER_STATIC_CONFIG_SDF_ATLAS_SIZE / 2,
             FFX_BRIXELIZER_STATIC_CONFIG_SDF_ATLAS_SIZE / 2,
             FFX_BRIXELIZER_STATIC_CONFIG_SDF_ATLAS_SIZE / 2,
             FFX_RESOURCE_FLAGS_NONE},
            {FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_STATIC_GI_TARGET_0,
             L"BrixelizerGI_StaticGITarget0",
             FFX_RESOURCE_TYPE_TEXTURE2D,
             FFX_RESOURCE_USAGE_UAV,
             FFX_SURFACE_FORMAT_R16G16B16A16_FLOAT,
             pContext->internalSize.width,
             pContext->internalSize.height,
             1,
             FFX_RESOURCE_FLAGS_NONE},
            {FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_STATIC_GI_TARGET_1,
             L"BrixelizerGI_StaticGITarget1",
             FFX_RESOURCE_TYPE_TEXTURE2D,
             FFX_RESOURCE_USAGE_UAV,
             FFX_SURFACE_FORMAT_R16G16B16A16_FLOAT,
             pContext->internalSize.width,
             pContext->internalSize.height,
             1,
             FFX_RESOURCE_FLAGS_NONE},
            {FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_STATIC_SCREEN_PROBES_0,
             L"BrixelizerGI_StaticScreenProbes0",
             FFX_RESOURCE_TYPE_TEXTURE2D,
             FFX_RESOURCE_USAGE_UAV,
             FFX_SURFACE_FORMAT_R16G16B16A16_FLOAT,
             probeBufferWidth,
             probeBufferHeight,
             1,
             FFX_RESOURCE_FLAGS_NONE},
            {FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_STATIC_SCREEN_PROBES_1,
             L"BrixelizerGI_StaticScreenProbes1",
             FFX_RESOURCE_TYPE_TEXTURE2D,
             FFX_RESOURCE_USAGE_UAV,
             FFX_SURFACE_FORMAT_R16G16B16A16_FLOAT,
             probeBufferWidth,
             probeBufferHeight,
             1,
             FFX_RESOURCE_FLAGS_NONE},
            {FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_SPECULAR_TARGET_0,
             L"BrixelizerGI_SpecularTarget0",
             FFX_RESOURCE_TYPE_TEXTURE2D,
             FFX_RESOURCE_USAGE_UAV,
             FFX_SURFACE_FORMAT_R16G16B16A16_FLOAT,
             pContext->internalSize.width,
             pContext->internalSize.height,
             1,
             FFX_RESOURCE_FLAGS_NONE},
            {FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_SPECULAR_TARGET_1,
             L"BrixelizerGI_SpecularTarget1",
             FFX_RESOURCE_TYPE_TEXTURE2D,
             FFX_RESOURCE_USAGE_UAV,
             FFX_SURFACE_FORMAT_R16G16B16A16_FLOAT,
             pContext->internalSize.width,
             pContext->internalSize.height,
             1,
             FFX_RESOURCE_FLAGS_NONE},
            {FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_DISOCCLUSION_MASK,
             L"BrixelizerGI_DisocclusionMask",
             FFX_RESOURCE_TYPE_TEXTURE2D,
             FFX_RESOURCE_USAGE_UAV,
             FFX_SURFACE_FORMAT_R8_UNORM,
             pContext->internalSize.width,
             pContext->internalSize.height,
             1,
             FFX_RESOURCE_FLAGS_NONE},
            // vkfw patch: the sky visibility's history (InterpolateScreenProbes), 3.5 bytes a pixel of the internal size in all.
            {FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_SKY_VISIBILITY_HISTORY_0,
             L"BrixelizerGI_SkyVisibilityHistory0",
             FFX_RESOURCE_TYPE_TEXTURE2D,
             FFX_RESOURCE_USAGE_UAV,
             FFX_SURFACE_FORMAT_R16G16B16A16_FLOAT,
             pContext->internalSize.width,
             pContext->internalSize.height,
             1,
             FFX_RESOURCE_FLAGS_NONE},
            {FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_SKY_VISIBILITY_HISTORY_1,
             L"BrixelizerGI_SkyVisibilityHistory1",
             FFX_RESOURCE_TYPE_TEXTURE2D,
             FFX_RESOURCE_USAGE_UAV,
             FFX_SURFACE_FORMAT_R16G16B16A16_FLOAT,
             pContext->internalSize.width,
             pContext->internalSize.height,
             1,
             FFX_RESOURCE_FLAGS_NONE},
            {FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_DEBUG_TARGET,
             L"BrixelizerGI_DebugTarget",
             FFX_RESOURCE_TYPE_TEXTURE2D,
             FFX_RESOURCE_USAGE_UAV,
             FFX_SURFACE_FORMAT_R16G16B16A16_FLOAT,
             probeBufferWidth,
             probeBufferHeight,
             1,
             FFX_RESOURCE_FLAGS_NONE},
            {FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_STATIC_SCREEN_PROBES_STAT,
             L"BrixelizerGI_StaticScreenProbesStat",
             FFX_RESOURCE_TYPE_TEXTURE2D,
             FFX_RESOURCE_USAGE_UAV,
             FFX_SURFACE_FORMAT_R16G16B16A16_FLOAT,
             tileBufferWidth,
             tileBufferHeight,
             1,
             FFX_RESOURCE_FLAGS_NONE},
            {FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_TEMP_SPAWN_MASK,
             L"BrixelizerGI_TempSpawnMask",
             FFX_RESOURCE_TYPE_TEXTURE2D,
             FFX_RESOURCE_USAGE_UAV,
             FFX_SURFACE_FORMAT_R32_UINT,
             tileBufferWidth,
             tileBufferHeight,
             1,
             FFX_RESOURCE_FLAGS_NONE},
            {FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_TEMP_SPECULAR_PRETRACE_TARGET,
             L"BrixelizerGI_TempSpecularPretraceTarget",
             FFX_RESOURCE_TYPE_TEXTURE2D,
             FFX_RESOURCE_USAGE_UAV,
             FFX_SURFACE_FORMAT_R32G32B32A32_UINT,
             tileBufferWidth * 2,
             tileBufferHeight * 2,
             1,
             FFX_RESOURCE_FLAGS_NONE},
            {FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_TEMP_BLUR_MASK,
             L"BrixelizerGI_TempBlurMask",
             FFX_RESOURCE_TYPE_TEXTURE2D,
             FFX_RESOURCE_USAGE_UAV,
             FFX_SURFACE_FORMAT_R8_UNORM,
             pContext->internalSize.width,
             pContext->internalSize.height * 2,
             1,
             FFX_RESOURCE_FLAGS_NONE},
            {FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_TEMP_RAND_SEED,
             L"BrixelizerGI_TempRandSeed",
             FFX_RESOURCE_TYPE_TEXTURE2D,
             FFX_RESOURCE_USAGE_UAV,
             FFX_SURFACE_FORMAT_R8_UINT,
             tileBufferWidth,
             tileBufferHeight,
             1,
             FFX_RESOURCE_FLAGS_NONE},
            {FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_BRICKS_SH,
             L"BrixelizerGI_BrickSH",
             FFX_RESOURCE_TYPE_BUFFER,
             FFX_RESOURCE_USAGE_UAV,
             FFX_SURFACE_FORMAT_R32_FLOAT,
             FFX_BRIXELIZER_MAX_BRICKS_X8 * sizeof(FfxUInt32x2) * 9,
             sizeof(FfxUInt32x2),
             1,
             FFX_RESOURCE_FLAGS_NONE},
            {FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_BRICKS_DIRECT_SH,
             L"BrixelizerGI_BrickDirectSH",
             FFX_RESOURCE_TYPE_BUFFER,
             FFX_RESOURCE_USAGE_UAV,
             FFX_SURFACE_FORMAT_R32_FLOAT,
             FFX_BRIXELIZER_MAX_BRICKS_X8 * sizeof(FfxUInt32x2) * 9,
             sizeof(FfxUInt32x2),
             1,
             FFX_RESOURCE_FLAGS_NONE},
            {FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_BRICKS_SH_STATE,
             L"BrixelizerGI_BrickSHState",
             FFX_RESOURCE_TYPE_BUFFER,
             FFX_RESOURCE_USAGE_UAV,
             FFX_SURFACE_FORMAT_R32_FLOAT,
             FFX_BRIXELIZER_MAX_BRICKS_X8 * sizeof(FfxUInt32x4),
             sizeof(FfxUInt32x4),
             1,
             FFX_RESOURCE_FLAGS_NONE},
            {FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_STATIC_PROBE_SH,
             L"BrixelizerGI_StaticProbeSH",
             FFX_RESOURCE_TYPE_BUFFER,
             FFX_RESOURCE_USAGE_UAV,
             FFX_SURFACE_FORMAT_R32_FLOAT,
             tileBufferWidth * tileBufferHeight * sizeof(FfxUInt32x2) * 9,
             sizeof(FfxUInt32x2),
             1,
             FFX_RESOURCE_FLAGS_NONE},
            {FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_STATIC_PROBE_INFO,
             L"BrixelizerGI_StaticProbeInfo",
             FFX_RESOURCE_TYPE_BUFFER,
             FFX_RESOURCE_USAGE_UAV,
             FFX_SURFACE_FORMAT_R32_FLOAT,
             tileBufferWidth * tileBufferHeight * sizeof(FfxUInt32x4),
             sizeof(FfxUInt32x4),
             1,
             FFX_RESOURCE_FLAGS_NONE},
            {FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_TEMP_PROBE_INFO,
             L"BrixelizerGI_TempProbeInfo",
             FFX_RESOURCE_TYPE_BUFFER,
             FFX_RESOURCE_USAGE_UAV,
             FFX_SURFACE_FORMAT_R32_FLOAT,
             tileBufferWidth * tileBufferHeight * sizeof(FfxUInt32x4),
             sizeof(FfxUInt32x4),
             1,
             FFX_RESOURCE_FLAGS_NONE},
            {FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_TEMP_SPECULAR_RAY_SWAP,
             L"BrixelizerGI_TempSpecularRaySwap",
             FFX_RESOURCE_TYPE_BUFFER,
             FFX_RESOURCE_USAGE_UAV,
             FFX_SURFACE_FORMAT_R32_FLOAT,
             pContext->internalSize.width * pContext->internalSize.height * sizeof(FfxUInt32),
             sizeof(FfxUInt32),
             1,
             FFX_RESOURCE_FLAGS_NONE},
            {FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_TEMP_PROBE_SH,
             L"BrixelizerGI_TempProbeSH",
             FFX_RESOURCE_TYPE_BUFFER,
             FFX_RESOURCE_USAGE_UAV,
             FFX_SURFACE_FORMAT_R32_FLOAT,
             tileBufferWidth * tileBufferHeight * sizeof(FfxUInt32x2) * 9,
             sizeof(FfxUInt32x2),
             1,
             FFX_RESOURCE_FLAGS_NONE},
            {FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_SLOT_STATE,
             L"BrixelizerGI_SlotState",
             FFX_RESOURCE_TYPE_BUFFER,
             FFX_RESOURCE_USAGE_UAV,
             FFX_SURFACE_FORMAT_R32_FLOAT,
             16 * sizeof(FfxUInt32),
             sizeof(FfxUInt32),
             1,
             FFX_RESOURCE_FLAGS_NONE},
            {FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_SLOT_CLAIM,
             L"BrixelizerGI_SlotClaim",
             FFX_RESOURCE_TYPE_BUFFER,
             FFX_RESOURCE_USAGE_UAV,
             FFX_SURFACE_FORMAT_R32_FLOAT,
             FFX_BRIXELIZER_MAX_BRICKS * sizeof(FfxUInt32),
             sizeof(FfxUInt32),
             1,
             FFX_RESOURCE_FLAGS_NONE},
            {FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_SLOT_FREE_LIST,
             L"BrixelizerGI_SlotFreeList",
             FFX_RESOURCE_TYPE_BUFFER,
             FFX_RESOURCE_USAGE_UAV,
             FFX_SURFACE_FORMAT_R32_FLOAT,
             FFX_BRIXELIZER_MAX_BRICKS * sizeof(FfxUInt32),
             sizeof(FfxUInt32),
             1,
             FFX_RESOURCE_FLAGS_NONE},
            {FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_LEVEL_SLOT_MAPS,
             L"BrixelizerGI_LevelSlotMaps",
             FFX_RESOURCE_TYPE_BUFFER,
             FFX_RESOURCE_USAGE_UAV,
             FFX_SURFACE_FORMAT_R32_FLOAT,
             FFX_BRIXELIZER_GI_SLOT_LEVELS * FFX_BRIXELIZER_CASCADE_RESOLUTION * FFX_BRIXELIZER_CASCADE_RESOLUTION * FFX_BRIXELIZER_CASCADE_RESOLUTION * sizeof(FfxUInt32),
             sizeof(FfxUInt32),
             1,
             FFX_RESOURCE_FLAGS_NONE},
            {FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_NEW_SLOTS,
             L"BrixelizerGI_NewSlots",
             FFX_RESOURCE_TYPE_BUFFER,
             FFX_RESOURCE_USAGE_UAV,
             FFX_SURFACE_FORMAT_R32_FLOAT,
             FFX_BRIXELIZER_CASCADE_RESOLUTION * FFX_BRIXELIZER_CASCADE_RESOLUTION * FFX_BRIXELIZER_CASCADE_RESOLUTION * sizeof(FfxUInt32),
             sizeof(FfxUInt32),
             1,
             FFX_RESOURCE_FLAGS_NONE},
            {FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_MOVER_CLAIMS,
             L"BrixelizerGI_MoverClaims",
             FFX_RESOURCE_TYPE_BUFFER,
             FFX_RESOURCE_USAGE_UAV,
             FFX_SURFACE_FORMAT_R32_FLOAT,
             FFX_BRIXELIZER_CASCADE_RESOLUTION * FFX_BRIXELIZER_CASCADE_RESOLUTION * FFX_BRIXELIZER_CASCADE_RESOLUTION * sizeof(FfxUInt32),
             sizeof(FfxUInt32),
             1,
             FFX_RESOURCE_FLAGS_NONE},
            {FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_SEED_LIST,
             L"BrixelizerGI_SeedList",
             FFX_RESOURCE_TYPE_BUFFER,
             FFX_RESOURCE_USAGE_UAV,
             FFX_SURFACE_FORMAT_R32_FLOAT,
             3 * FFX_BRIXELIZER_CASCADE_RESOLUTION * FFX_BRIXELIZER_CASCADE_RESOLUTION * FFX_BRIXELIZER_CASCADE_RESOLUTION * sizeof(FfxUInt32),
             sizeof(FfxUInt32),
             1,
             FFX_RESOURCE_FLAGS_NONE},
            {FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_BRICK_SLOT_MAP,
             L"BrixelizerGI_BrickSlotMap",
             FFX_RESOURCE_TYPE_BUFFER,
             FFX_RESOURCE_USAGE_UAV,
             FFX_SURFACE_FORMAT_R32_FLOAT,
             FFX_BRIXELIZER_MAX_BRICKS * sizeof(FfxUInt32),
             sizeof(FfxUInt32),
             1,
             FFX_RESOURCE_FLAGS_NONE},
            {FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_RAY_SWAP_INDIRECT_ARGS,
             L"BrixelizerGI_RaySwapIndirectArgs",
             FFX_RESOURCE_TYPE_BUFFER,
             FfxResourceUsage(FFX_RESOURCE_USAGE_UAV | FFX_RESOURCE_USAGE_INDIRECT),
             FFX_SURFACE_FORMAT_R32_FLOAT,
             4 * sizeof(FfxUInt32),
             sizeof(FfxUInt32),
             1,
             FFX_RESOURCE_FLAGS_NONE},
        };

        for (int32_t currentSurfaceIndex = 0; currentSurfaceIndex < FFX_ARRAY_ELEMENTS(internalSurfaceDesc); ++currentSurfaceIndex)
        {
            const FfxInternalResourceDescription* currentSurfaceDescription = &internalSurfaceDesc[currentSurfaceIndex];

            // The application's own, registered with each dispatch.
            if (brixelizerGIIsExternalResource(pContextDescription->flags, currentSurfaceDescription->id))
            {
                memset(&pContext->resources[currentSurfaceDescription->id], 0, sizeof(FfxResourceInternal));
                continue;
            }
            const FfxResourceDescription          resourceDescription       = {currentSurfaceDescription->type,
                                                                               currentSurfaceDescription->format,
                                                                               currentSurfaceDescription->width,
                                                                               currentSurfaceDescription->height,
                                                                               currentSurfaceDescription->mipCount,
                                                                               1,
                                                                               currentSurfaceDescription->flags,
                                                                               currentSurfaceDescription->usage};

            FfxResourceStates initialState = (currentSurfaceDescription->usage == FFX_RESOURCE_USAGE_READ_ONLY) ? FFX_RESOURCE_STATE_COMPUTE_READ : FFX_RESOURCE_STATE_UNORDERED_ACCESS;

            // vkfw patch: the slot state exists only with FFX_BRIXELIZER_GI_FLAG_CACHE_SLOTS, zero-filled (the maps store a slot plus one), and the
            // identity brick -> slot map only without it.
            const FfxUInt32 resourceId = currentSurfaceDescription->id;
            const bool      bSlotsState = (resourceId >= FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_SLOT_STATE && resourceId <= FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_SEED_LIST) ||
                                          resourceId == FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_MOVER_CLAIMS;
            if ((bSlotsState && !(pContextDescription->flags & FFX_BRIXELIZER_GI_FLAG_CACHE_SLOTS)) ||
                (resourceId == FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_BRICK_SLOT_MAP && (pContextDescription->flags & FFX_BRIXELIZER_GI_FLAG_CACHE_SLOTS)))
            {
                memset(&pContext->resources[resourceId], 0, sizeof(FfxResourceInternal));
                continue;
            }
            FfxResourceInitData entryInitData = initData;
            FfxUInt32*          identityMap   = NULL;
            if (bSlotsState)
                entryInitData = FfxResourceInitData::FfxResourceInitValue((size_t)currentSurfaceDescription->width, 0);
            if (currentSurfaceDescription->id == FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_BRICK_SLOT_MAP)
            {
                identityMap = (FfxUInt32*)malloc(FFX_BRIXELIZER_MAX_BRICKS * sizeof(FfxUInt32));
                for (FfxUInt32 i = 0; i < FFX_BRIXELIZER_MAX_BRICKS; ++i)
                    identityMap[i] = i;
                entryInitData = {FFX_RESOURCE_INIT_DATA_TYPE_BUFFER, FFX_BRIXELIZER_MAX_BRICKS * sizeof(FfxUInt32), identityMap};
            }

            const FfxCreateResourceDescription createResourceDescription = { FFX_HEAP_TYPE_DEFAULT, resourceDescription, initialState, currentSurfaceDescription->name, currentSurfaceDescription->id, entryInitData};

            memset(&pContext->resources[currentSurfaceDescription->id], 0, sizeof(FfxResourceInternal));

            FFX_VALIDATE(pContext->contextDescription.backendInterface.fpCreateResource(&pContext->contextDescription.backendInterface,
                                                                                        &createResourceDescription,
                                                                                        pContext->effectContextId,
                                                                                        &pContext->resources[currentSurfaceDescription->id]));
            free(identityMap);
        }
    }
    
    // Create downsampling resources.
    if (pContextDescription->internalResolution != FFX_BRIXELIZER_GI_INTERNAL_RESOLUTION_NATIVE)
    {
        const FfxInternalResourceDescription internalSurfaceDesc[] = {
            {FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_DOWNSAMPLED_DEPTH,
             L"BrixelizerGI_DownsampledDepth",
             FFX_RESOURCE_TYPE_TEXTURE2D,
             FFX_RESOURCE_USAGE_UAV,
             FFX_SURFACE_FORMAT_R32_FLOAT,
             pContext->internalSize.width,
             pContext->internalSize.height,
             1,
             FFX_RESOURCE_FLAGS_NONE},
            {FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_DOWNSAMPLED_HISTORY_DEPTH,
             L"BrixelizerGI_DownsampledHistoryDepth",
             FFX_RESOURCE_TYPE_TEXTURE2D,
             FFX_RESOURCE_USAGE_UAV,
             FFX_SURFACE_FORMAT_R32_FLOAT,
             pContext->internalSize.width,
             pContext->internalSize.height,
             1,
             FFX_RESOURCE_FLAGS_NONE},
            {FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_DOWNSAMPLED_NORMAL,
             L"BrixelizerGI_DownsampledNormals",
             FFX_RESOURCE_TYPE_TEXTURE2D,
             FFX_RESOURCE_USAGE_UAV,
             FFX_SURFACE_FORMAT_R16G16B16A16_FLOAT,
             pContext->internalSize.width,
             pContext->internalSize.height,
             1,
             FFX_RESOURCE_FLAGS_NONE},
            {FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_DOWNSAMPLED_HISTORY_NORMAL,
             L"BrixelizerGI_DownsampledHistoryNormals",
             FFX_RESOURCE_TYPE_TEXTURE2D,
             FFX_RESOURCE_USAGE_UAV,
             FFX_SURFACE_FORMAT_R16G16B16A16_FLOAT,
             pContext->internalSize.width,
             pContext->internalSize.height,
             1,
             FFX_RESOURCE_FLAGS_NONE},
            {FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_DOWNSAMPLED_ROUGHNESS,
             L"BrixelizerGI_DownsampledRoughness",
             FFX_RESOURCE_TYPE_TEXTURE2D,
             FFX_RESOURCE_USAGE_UAV,
             FFX_SURFACE_FORMAT_R8_UNORM,
             pContext->internalSize.width,
             pContext->internalSize.height,
             1,
             FFX_RESOURCE_FLAGS_NONE},
            {FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_DOWNSAMPLED_MOTION_VECTORS,
             L"BrixelizerGI_DownsampledMotionVectors",
             FFX_RESOURCE_TYPE_TEXTURE2D,
             FFX_RESOURCE_USAGE_UAV,
             FFX_SURFACE_FORMAT_R16G16_FLOAT,
             pContext->internalSize.width,
             pContext->internalSize.height,
             1,
             FFX_RESOURCE_FLAGS_NONE},
            {FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_DOWNSAMPLED_LIT_OUTPUT,
             L"BrixelizerGI_DownsampledLitOutput",
             FFX_RESOURCE_TYPE_TEXTURE2D,
             FFX_RESOURCE_USAGE_UAV,
             FFX_SURFACE_FORMAT_R16G16B16A16_FLOAT,
             pContext->internalSize.width,
             pContext->internalSize.height,
             1,
             FFX_RESOURCE_FLAGS_NONE},
            {FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_DOWNSAMPLED_DIFFUSE_GI,
             L"BrixelizerGI_DownsampledDiffuseGI",
             FFX_RESOURCE_TYPE_TEXTURE2D,
             FFX_RESOURCE_USAGE_UAV,
             FFX_SURFACE_FORMAT_R16G16B16A16_FLOAT,
             pContext->internalSize.width,
             pContext->internalSize.height,
             1,
             FFX_RESOURCE_FLAGS_NONE},
            {FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_DOWNSAMPLED_SPECULAR_GI,
             L"BrixelizerGI_DownsampledSpecularGI",
             FFX_RESOURCE_TYPE_TEXTURE2D,
             FFX_RESOURCE_USAGE_UAV,
             FFX_SURFACE_FORMAT_R16G16B16A16_FLOAT,
             pContext->internalSize.width,
             pContext->internalSize.height,
             1,
             FFX_RESOURCE_FLAGS_NONE},
        };

        for (int32_t currentSurfaceIndex = 0; currentSurfaceIndex < FFX_ARRAY_ELEMENTS(internalSurfaceDesc); ++currentSurfaceIndex)
        {
            const FfxInternalResourceDescription* currentSurfaceDescription = &internalSurfaceDesc[currentSurfaceIndex];
            const FfxResourceDescription          resourceDescription       = {currentSurfaceDescription->type,
                                                                               currentSurfaceDescription->format,
                                                                               currentSurfaceDescription->width,
                                                                               currentSurfaceDescription->height,
                                                                               currentSurfaceDescription->mipCount,
                                                                               1,
                                                                               currentSurfaceDescription->flags,
                                                                               currentSurfaceDescription->usage};

            FfxResourceStates initialState =
                (currentSurfaceDescription->usage == FFX_RESOURCE_USAGE_READ_ONLY) ? FFX_RESOURCE_STATE_COMPUTE_READ : FFX_RESOURCE_STATE_UNORDERED_ACCESS;

            const FfxCreateResourceDescription createResourceDescription = {
                FFX_HEAP_TYPE_DEFAULT, resourceDescription, initialState, currentSurfaceDescription->name, currentSurfaceDescription->id, initData};

            memset(&pContext->resources[currentSurfaceDescription->id], 0, sizeof(FfxResourceInternal));

            FFX_VALIDATE(pContext->contextDescription.backendInterface.fpCreateResource(&pContext->contextDescription.backendInterface,
                                                                                        &createResourceDescription,
                                                                                        pContext->effectContextId,
                                                                                        &pContext->resources[currentSurfaceDescription->id]));
        }
    }

    return FFX_OK;
}

static FfxErrorCode brixelizerGIRelease(FfxBrixelizerGIContext_Private* pContext)
{
    FFX_ASSERT(pContext);

    ffxSafeReleasePipeline(&pContext->contextDescription.backendInterface, &pContext->pipelinePrepareClearCache, pContext->effectContextId);
    ffxSafeReleasePipeline(&pContext->contextDescription.backendInterface, &pContext->pipelineClearCache, pContext->effectContextId);
    ffxSafeReleasePipeline(&pContext->contextDescription.backendInterface, &pContext->pipelineEmitPrimaryRayRadiance, pContext->effectContextId);
    ffxSafeReleasePipeline(&pContext->contextDescription.backendInterface, &pContext->pipelinePropagateSH, pContext->effectContextId);
    ffxSafeReleasePipeline(&pContext->contextDescription.backendInterface, &pContext->pipelineSlotsClaimSame, pContext->effectContextId);
    ffxSafeReleasePipeline(&pContext->contextDescription.backendInterface, &pContext->pipelineSlotsClaimMover, pContext->effectContextId);
    ffxSafeReleasePipeline(&pContext->contextDescription.backendInterface, &pContext->pipelineSlotsFree, pContext->effectContextId);
    ffxSafeReleasePipeline(&pContext->contextDescription.backendInterface, &pContext->pipelineSlotsAlloc, pContext->effectContextId);
    ffxSafeReleasePipeline(&pContext->contextDescription.backendInterface, &pContext->pipelineSlotsApply, pContext->effectContextId);
    ffxSafeReleasePipeline(&pContext->contextDescription.backendInterface, &pContext->pipelineSlotsSeed, pContext->effectContextId);
    ffxSafeReleasePipeline(&pContext->contextDescription.backendInterface, &pContext->pipelineSlotsStats, pContext->effectContextId);
    ffxSafeReleasePipeline(&pContext->contextDescription.backendInterface, &pContext->pipelineSlotsResample, pContext->effectContextId);
    ffxSafeReleasePipeline(&pContext->contextDescription.backendInterface, &pContext->pipelineCacheFeed, pContext->effectContextId);
ffxSafeReleasePipeline(&pContext->contextDescription.backendInterface, &pContext->pipelineSpawnScreenProbes, pContext->effectContextId);
    ffxSafeReleasePipeline(&pContext->contextDescription.backendInterface, &pContext->pipelineReprojectScreenProbes, pContext->effectContextId);
    ffxSafeReleasePipeline(&pContext->contextDescription.backendInterface, &pContext->pipelineFillScreenProbes, pContext->effectContextId);
    ffxSafeReleasePipeline(&pContext->contextDescription.backendInterface, &pContext->pipelineSpecularPreTrace, pContext->effectContextId);
    ffxSafeReleasePipeline(&pContext->contextDescription.backendInterface, &pContext->pipelineSpecularTrace, pContext->effectContextId);
    ffxSafeReleasePipeline(&pContext->contextDescription.backendInterface, &pContext->pipelineReprojectGI, pContext->effectContextId);
    ffxSafeReleasePipeline(&pContext->contextDescription.backendInterface, &pContext->pipelineProjectScreenProbes, pContext->effectContextId);
    ffxSafeReleasePipeline(&pContext->contextDescription.backendInterface, &pContext->pipelineEmitIrradianceCache, pContext->effectContextId);
    ffxSafeReleasePipeline(&pContext->contextDescription.backendInterface, &pContext->pipelineInterpolateScreenProbes, pContext->effectContextId);
    ffxSafeReleasePipeline(&pContext->contextDescription.backendInterface, &pContext->pipelineBlurX, pContext->effectContextId);
    ffxSafeReleasePipeline(&pContext->contextDescription.backendInterface, &pContext->pipelineBlurY, pContext->effectContextId);
    ffxSafeReleasePipeline(&pContext->contextDescription.backendInterface, &pContext->pipelineDebugVisualization, pContext->effectContextId);
    ffxSafeReleasePipeline(&pContext->contextDescription.backendInterface, &pContext->pipelineGenerateDisocclusionMask, pContext->effectContextId);
    ffxSafeReleasePipeline(&pContext->contextDescription.backendInterface, &pContext->pipelineDownsample, pContext->effectContextId);
    ffxSafeReleasePipeline(&pContext->contextDescription.backendInterface, &pContext->pipelineUpsample, pContext->effectContextId);

    // Release internal resources.
    for (int32_t currentResourceIndex = 0; currentResourceIndex < FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_COUNT_INTERNAL; ++currentResourceIndex)
    {
        if (brixelizerGIIsExternalResource(pContext->contextDescription.flags, currentResourceIndex))
            continue;
        ffxSafeReleaseResource(&pContext->contextDescription.backendInterface, pContext->resources[currentResourceIndex], pContext->effectContextId);
    }
    // vkfw patch: internal, but numbered after the application's resources.
    ffxSafeReleaseResource(&pContext->contextDescription.backendInterface, pContext->resources[FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_SKY_VISIBILITY_HISTORY_0], pContext->effectContextId);
    ffxSafeReleaseResource(&pContext->contextDescription.backendInterface, pContext->resources[FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_SKY_VISIBILITY_HISTORY_1], pContext->effectContextId);
    if (pContext->contextDescription.flags & FFX_BRIXELIZER_GI_FLAG_CACHE_SLOTS)
    {
        for (uint32_t id = FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_SLOT_STATE; id <= FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_SEED_LIST; ++id)
            if (!brixelizerGIIsExternalResource(pContext->contextDescription.flags, id))
                ffxSafeReleaseResource(&pContext->contextDescription.backendInterface, pContext->resources[id], pContext->effectContextId);
        ffxSafeReleaseResource(&pContext->contextDescription.backendInterface, pContext->resources[FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_MOVER_CLAIMS], pContext->effectContextId);
    }
    else
    {
        ffxSafeReleaseResource(&pContext->contextDescription.backendInterface, pContext->resources[FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_BRICK_SLOT_MAP], pContext->effectContextId);
    }

    // Destroy the context.
    pContext->contextDescription.backendInterface.fpDestroyBackendContext(&pContext->contextDescription.backendInterface, pContext->effectContextId);

    return FFX_OK;
}

FfxErrorCode ffxBrixelizerGIGetDroppedMovers(FfxBrixelizerGIContext* context, uint32_t* outDropped)
{
    FFX_RETURN_ON_ERROR(context && outDropped, FFX_ERROR_INVALID_POINTER);
    *outDropped = ((const FfxBrixelizerGIContext_Private*)context)->droppedMovers;
    return FFX_OK;
}

// vkfw patch: FFX_BRIXELIZER_GI_FLAG_CACHE_SLOTS: for each merged cascade the bake before this dispatch rebuilt, the slot passes
// (ffx_brixelizergi_cache_slots.h), then the placement it now has is what the next time compares against. The slot state, host half
// (slotState) and GPU half (the dispatch's buffers), is the Brixelizer context's and the application's: every GI context on one SDF hands
// out the same slots, and a GI context made anew (a resize) carries on with the cache as it stands.
static void brixelizerGIScheduleSlots(FfxBrixelizerGIContext_Private* pContext, const FfxBrixelizerGIDispatchDescription* pDispatchDescription, const FfxBrixelizerContextInfo* pContextInfo,
                                      FfxBrixelizerRawCacheSlotState* slotState, uint32_t updatedLevelMask, const FfxBrixelizerRawMoverRemap* remaps, uint32_t numRemaps)
{
    FfxBrixelizerGISlotConstants& constants = pContext->slotConstants;
    constexpr uint32_t voxels = FFX_BRIXELIZER_CASCADE_RESOLUTION * FFX_BRIXELIZER_CASCADE_RESOLUTION * FFX_BRIXELIZER_CASCADE_RESOLUTION;
    const bool bEmptyList  = !isResourceNull(pDispatchDescription->emptyBricks) && pDispatchDescription->maxEmptyBricks > 0;
    const FfxBrixelizerGICacheSettings& settings = pDispatchDescription->cacheSettings;
    bool       bFirstLevel = true;

    for (uint32_t level = 0; level < FFX_BRIXELIZER_GI_SLOT_LEVELS; ++level)
    {
        const uint32_t cascadeIndex = pDispatchDescription->startCascade + level;
        if (!(updatedLevelMask & (1u << level)) || cascadeIndex > pDispatchDescription->endCascade || cascadeIndex >= FFX_BRIXELIZER_MAX_CASCADES)
            continue;

        auto&                                      previous = slotState->levels[level];
        const FfxBrixelizerCascadeInfo&            cascade  = pContextInfo->cascades[cascadeIndex];

        memset(&constants, 0, sizeof(constants));
        constants.cascade_index         = cascadeIndex;
        constants.level_index           = level;
        constants.previous_valid        = previous.valid ? 1u : 0u;
        constants.carry                 = (settings.flags & FFX_BRIXELIZER_GI_CACHE_CARRY) ? 1u : 0u;
        constants.previous_voxel_size   = previous.voxelSize;
        for (uint32_t i = 0; i < 3; ++i)
        {
            constants.previous_grid_min[i]       = previous.gridMin[i];
            constants.previous_clipmap_offset[i] = previous.clipmapOffset[i];
        }
        // Only one cascade bakes a frame, so the coarser one is as its own last bake left it, cache and all.
        const bool bCoarser = (settings.flags & FFX_BRIXELIZER_GI_CACHE_SEED_COARSER)&& level + 1 < FFX_BRIXELIZER_GI_SLOT_LEVELS &&
                              cascadeIndex + 1 <= pDispatchDescription->endCascade && slotState->levels[level + 1].valid;
        constants.coarser_cascade_index = bCoarser ? cascadeIndex + 1 : 0xffffffffu;
        // The finer one was rebuilt more recently (finer levels rebuild more often) and sees the voxel in more detail.
        const bool bFiner = (settings.flags & FFX_BRIXELIZER_GI_CACHE_SEED_FINER)&& level > 0 && cascadeIndex > pDispatchDescription->startCascade &&
                            slotState->levels[level - 1].valid;
        constants.finer_cascade_index   = bFiner ? cascadeIndex - 1 : 0xffffffffu;
        constants.seed_samples          = settings.seedSamples > 0.0f ? settings.seedSamples : 2.0f;
        constants.seed_radiance         = (settings.flags & FFX_BRIXELIZER_GI_CACHE_SEED_RADIANCE) ? 1u : 0u;
        constants.seed_min_samples      = settings.seedMinSamples > 0.0f ? settings.seedMinSamples : 4.0f;
        constants.mover_padding         = settings.moverPaddingVoxels > 0.0f ? settings.moverPaddingVoxels : 1.0f;
        constants.park_bakes            = settings.parkBakes > 0u ? (settings.parkBakes < 15u ? settings.parkBakes : 15u) : 8u;
        constants.claim_tag             = ++slotState->claimTag;
        constants.static_source_cascade = (settings.flags & FFX_BRIXELIZER_GI_CACHE_MOVERS_SKIP_STATIC) ? level : 0xffffffffu;
        constants.empty_max             = bEmptyList ? pDispatchDescription->maxEmptyBricks : 0u;
        constants.empty_threshold       = settings.emptyBrickMaxSamples > 0.0f ? settings.emptyBrickMaxSamples : 1.0f;
        constants.empty_reset           = bEmptyList && bFirstLevel && !(settings.flags & FFX_BRIXELIZER_GI_CACHE_EMPTY_ACCUMULATE) ? 1u : 0u;
        constants.stats_unlit           = (settings.flags & FFX_BRIXELIZER_GI_CACHE_STATS) ? 1u : 0u;
        bFirstLevel                     = false;

        // This level's movers: each transform is from the last bake of the levels its mask names.
        constants.num_movers = 0;
        for (uint32_t m = 0; pDispatchDescription->movers && m < pDispatchDescription->numMovers; ++m)
        {
            const FfxBrixelizerGIMover& mover = pDispatchDescription->movers[m];
            if (mover.levelMask != 0 && !(mover.levelMask & (1u << level)))
                continue;
            if (constants.num_movers >= FFX_BRIXELIZER_GI_MAX_SLOT_MOVERS)
            {
                ++pContext->droppedMovers;
                continue;
            }
            FfxBrixelizerGISlotMover& slotMover = constants.movers[constants.num_movers++];
            memcpy(slotMover.transform_row0, mover.currentToPrevious + 0, 4 * sizeof(float));
            memcpy(slotMover.transform_row1, mover.currentToPrevious + 4, 4 * sizeof(float));
            memcpy(slotMover.transform_row2, mover.currentToPrevious + 8, 4 * sizeof(float));
            memcpy(slotMover.bounds_min, mover.boundsMin, 3 * sizeof(float));
            memcpy(slotMover.bounds_max, mover.boundsMax, 3 * sizeof(float));
        }
        // vkfw patch: and the SDK's own, of the mover instances (ffxBrixelizerUpdateInstanceTransforms), for this merged cascade.
        for (uint32_t m = 0; m < numRemaps; ++m)
        {
            const FfxBrixelizerRawMoverRemap& remap = remaps[m];
            if (remap.cascadeIndex != cascadeIndex)
                continue;
            if (constants.num_movers >= FFX_BRIXELIZER_GI_MAX_SLOT_MOVERS)
            {
                ++pContext->droppedMovers;
                continue;
            }
            FfxBrixelizerGISlotMover& slotMover = constants.movers[constants.num_movers++];
            memcpy(slotMover.transform_row0, remap.currentToPrevious + 0, 4 * sizeof(float));
            memcpy(slotMover.transform_row1, remap.currentToPrevious + 4, 4 * sizeof(float));
            memcpy(slotMover.transform_row2, remap.currentToPrevious + 8, 4 * sizeof(float));
            memcpy(slotMover.bounds_min, remap.boundsMin, 3 * sizeof(float));
            memcpy(slotMover.bounds_max, remap.boundsMax, 3 * sizeof(float));
        }
        updateConstantBuffer(pContext, FFX_BRIXELIZER_GI_CONSTANTBUFFER_IDENTIFIER_SLOT_CONSTANTS, (void*)&constants);

        const uint32_t groups = voxels / 64;
        scheduleDispatch(pContext, &pContext->pipelineSlotsClaimMover, groups, 1, 1);
        scheduleDispatch(pContext, &pContext->pipelineSlotsClaimSame, groups, 1, 1);
        scheduleDispatch(pContext, &pContext->pipelineSlotsFree, groups, 1, 1);
        scheduleDispatch(pContext, &pContext->pipelineSlotsAlloc, groups, 1, 1);
        scheduleDispatch(pContext, &pContext->pipelineSlotsApply, groups, 1, 1);
        scheduleDispatch(pContext, &pContext->pipelineSlotsSeed, groups, 1, 1);
        scheduleDispatch(pContext, &pContext->pipelineSlotsResample, groups, 1, 1);
        // The statistics pass also lists the empty bricks.
        if ((settings.flags & FFX_BRIXELIZER_GI_CACHE_STATS) || bEmptyList)
            scheduleDispatch(pContext, &pContext->pipelineSlotsStats, groups, 1, 1);

        previous.valid = 1u;
        for (uint32_t i = 0; i < 3; ++i)
        {
            previous.gridMin[i]       = cascade.grid_min[i];
            previous.clipmapOffset[i] = cascade.clipmap_offset[i];
        }
        previous.voxelSize = cascade.voxel_size;
    }
}

static FfxErrorCode brixelizerGIDispatch(FfxBrixelizerGIContext_Private*           pContext,
                                         const FfxBrixelizerGIDispatchDescription* pDispatchDescription,
                                         FfxCommandList                            pCommandList)
{
    FfxBrixelizerRawContext_Private *rawContext = (FfxBrixelizerRawContext_Private*)pDispatchDescription->brixelizerContext;

    const FfxResource bricksVoxelMap  = rawContext->contextDescription.backendInterface.fpGetResource(&rawContext->contextDescription.backendInterface, rawContext->resources[FFX_BRIXELIZER_RESOURCE_IDENTIFIER_CONTEXT_BRICKS_VOXEL_MAP]);
    const FfxResource bricksClearList = rawContext->contextDescription.backendInterface.fpGetResource(&rawContext->contextDescription.backendInterface, rawContext->resources[FFX_BRIXELIZER_RESOURCE_IDENTIFIER_CONTEXT_BRICKS_CLEAR_LIST]);
    const FfxResource contextCounters = rawContext->contextDescription.backendInterface.fpGetResource(&rawContext->contextDescription.backendInterface, rawContext->resources[FFX_BRIXELIZER_RESOURCE_IDENTIFIER_CONTEXT_COUNTERS]);

    // Register Application Resources
    if (pContext->contextDescription.internalResolution == FFX_BRIXELIZER_GI_INTERNAL_RESOLUTION_NATIVE)
    {
        pContext->contextDescription.backendInterface.fpRegisterResource(&pContext->contextDescription.backendInterface,
                                                                         &pDispatchDescription->outputDiffuseGI,
                                                                         pContext->effectContextId,
                                                                         &pContext->resources[FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_OUTPUT_DIFFUSE_GI]);
        pContext->contextDescription.backendInterface.fpRegisterResource(&pContext->contextDescription.backendInterface,
                                                                         &pDispatchDescription->outputSpecularGI,
                                                                         pContext->effectContextId,
                                                                         &pContext->resources[FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_OUTPUT_SPECULAR_GI]);

        pContext->contextDescription.backendInterface.fpRegisterResource(&pContext->contextDescription.backendInterface,
                                                                         &pDispatchDescription->prevLitOutput,
                                                                         pContext->effectContextId,
                                                                         &pContext->resources[FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_INPUT_PREV_LIT_OUTPUT]);
        pContext->contextDescription.backendInterface.fpRegisterResource(&pContext->contextDescription.backendInterface,
                                                                         &pDispatchDescription->depth,
                                                                         pContext->effectContextId,
                                                                         &pContext->resources[FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_INPUT_DEPTH]);
        pContext->contextDescription.backendInterface.fpRegisterResource(&pContext->contextDescription.backendInterface,
                                                                         &pDispatchDescription->historyDepth,
                                                                         pContext->effectContextId,
                                                                         &pContext->resources[FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_INPUT_HISTORY_DEPTH]);
        pContext->contextDescription.backendInterface.fpRegisterResource(&pContext->contextDescription.backendInterface,
                                                                         &pDispatchDescription->normal,
                                                                         pContext->effectContextId,
                                                                         &pContext->resources[FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_INPUT_NORMAL]);
        pContext->contextDescription.backendInterface.fpRegisterResource(&pContext->contextDescription.backendInterface,
                                                                         &pDispatchDescription->historyNormal,
                                                                         pContext->effectContextId,
                                                                         &pContext->resources[FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_INPUT_HISTORY_NORMAL]);
        pContext->contextDescription.backendInterface.fpRegisterResource(&pContext->contextDescription.backendInterface,
                                                                         &pDispatchDescription->roughness,
                                                                         pContext->effectContextId,
                                                                         &pContext->resources[FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_INPUT_ROUGHNESS]);
        pContext->contextDescription.backendInterface.fpRegisterResource(&pContext->contextDescription.backendInterface,
                                                                         &pDispatchDescription->motionVectors,
                                                                         pContext->effectContextId,
                                                                         &pContext->resources[FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_INPUT_MOTION_VECTORS]);
    } 
    else
    {
        // Register the downsampled internal resources as outputs and inputs.
        pContext->resources[FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_OUTPUT_DIFFUSE_GI]     = pContext->resources[FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_DOWNSAMPLED_DIFFUSE_GI];
        pContext->resources[FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_OUTPUT_SPECULAR_GI]    = pContext->resources[FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_DOWNSAMPLED_SPECULAR_GI];
        pContext->resources[FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_INPUT_PREV_LIT_OUTPUT] = pContext->resources[FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_DOWNSAMPLED_LIT_OUTPUT];
        pContext->resources[FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_INPUT_DEPTH]           = pContext->resources[FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_DOWNSAMPLED_DEPTH];
        pContext->resources[FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_INPUT_HISTORY_DEPTH]   = pContext->resources[FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_DOWNSAMPLED_HISTORY_DEPTH];
        pContext->resources[FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_INPUT_NORMAL]          = pContext->resources[FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_DOWNSAMPLED_NORMAL];
        pContext->resources[FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_INPUT_HISTORY_NORMAL]  = pContext->resources[FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_DOWNSAMPLED_HISTORY_NORMAL];
        pContext->resources[FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_INPUT_ROUGHNESS]       = pContext->resources[FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_DOWNSAMPLED_ROUGHNESS];
        pContext->resources[FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_INPUT_MOTION_VECTORS]  = pContext->resources[FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_DOWNSAMPLED_MOTION_VECTORS];

        // Register the original output and input resources as upsampled and source resources, respectively.
        pContext->contextDescription.backendInterface.fpRegisterResource(&pContext->contextDescription.backendInterface,
                                                                         &pDispatchDescription->outputDiffuseGI,
                                                                         pContext->effectContextId,
                                                                         &pContext->resources[FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_UPSAMPLED_DIFFUSE_GI]);
        pContext->contextDescription.backendInterface.fpRegisterResource(&pContext->contextDescription.backendInterface,
                                                                         &pDispatchDescription->outputSpecularGI,
                                                                         pContext->effectContextId,
                                                                         &pContext->resources[FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_UPSAMPLED_SPECULAR_GI]);

        pContext->contextDescription.backendInterface.fpRegisterResource(&pContext->contextDescription.backendInterface,
                                                                         &pDispatchDescription->prevLitOutput,
                                                                         pContext->effectContextId,
                                                                         &pContext->resources[FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_SOURCE_LIT_OUTPUT]);
        pContext->contextDescription.backendInterface.fpRegisterResource(&pContext->contextDescription.backendInterface,
                                                                         &pDispatchDescription->depth,
                                                                         pContext->effectContextId,
                                                                         &pContext->resources[FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_SOURCE_DEPTH]);
        pContext->contextDescription.backendInterface.fpRegisterResource(&pContext->contextDescription.backendInterface,
                                                                         &pDispatchDescription->historyDepth,
                                                                         pContext->effectContextId,
                                                                         &pContext->resources[FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_SOURCE_HISTORY_DEPTH]);
        pContext->contextDescription.backendInterface.fpRegisterResource(&pContext->contextDescription.backendInterface,
                                                                         &pDispatchDescription->normal,
                                                                         pContext->effectContextId,
                                                                         &pContext->resources[FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_SOURCE_NORMAL]);
        pContext->contextDescription.backendInterface.fpRegisterResource(&pContext->contextDescription.backendInterface,
                                                                         &pDispatchDescription->historyNormal,
                                                                         pContext->effectContextId,
                                                                         &pContext->resources[FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_SOURCE_HISTORY_NORMAL]);
        pContext->contextDescription.backendInterface.fpRegisterResource(&pContext->contextDescription.backendInterface,
                                                                         &pDispatchDescription->roughness,
                                                                         pContext->effectContextId,
                                                                         &pContext->resources[FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_SOURCE_ROUGHNESS]);
        pContext->contextDescription.backendInterface.fpRegisterResource(&pContext->contextDescription.backendInterface,
                                                                         &pDispatchDescription->motionVectors,
                                                                         pContext->effectContextId,
                                                                         &pContext->resources[FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_SOURCE_MOTION_VECTORS]);
    }

    pContext->contextDescription.backendInterface.fpRegisterResource(&pContext->contextDescription.backendInterface,
                                                                     &pDispatchDescription->environmentMap,
                                                                     pContext->effectContextId,
                                                                     &pContext->resources[FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_INPUT_ENVIRONMENT_MAP]);
    pContext->contextDescription.backendInterface.fpRegisterResource(&pContext->contextDescription.backendInterface,
                                                                     &pDispatchDescription->noiseTexture,
                                                                     pContext->effectContextId,
                                                                     &pContext->resources[FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_INPUT_BLUE_NOISE]);

    pContext->contextDescription.backendInterface.fpRegisterResource(&pContext->contextDescription.backendInterface,
                                                                     &pDispatchDescription->sdfAtlas,
                                                                     pContext->effectContextId,
                                                                     &pContext->resources[FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_INPUT_SDF_ATLAS]);
    brixelizerGIRegisterExternalResources(pContext, &pDispatchDescription->radianceCache, &pDispatchDescription->bricksSH,
                                          &pDispatchDescription->bricksDirectSH, &pDispatchDescription->bricksSHState, &pDispatchDescription->brickSlotMap, &pDispatchDescription->slotCounters,
                                          &pDispatchDescription->slotState, &pDispatchDescription->emptyBricks);

    // vkfw patch: the sky visibility target, or, while there is none, the interpolation's debug target standing in for the binding it
    // declares anyway (the shaders only write it with sky_visibility_enabled).
    const bool bSkyVisibility = !isResourceNull(pDispatchDescription->skyVisibility);
    if (bSkyVisibility)
    {
        pContext->contextDescription.backendInterface.fpRegisterResource(&pContext->contextDescription.backendInterface,
                                                                         &pDispatchDescription->skyVisibility,
                                                                         pContext->effectContextId,
                                                                         &pContext->resources[FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_OUTPUT_SKY_VISIBILITY]);
    }
    else
    {
        pContext->resources[FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_OUTPUT_SKY_VISIBILITY] = pContext->resources[FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_DEBUG_TARGET];
    }
    // Its history alternates between the two targets each frame; it holds a previous frame's values only if that frame wrote it.
    const uint32_t skyHistoryWrite = pContext->skyVisibilityHistoryIndex ^ 1u;
    pContext->resources[FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_SKY_VISIBILITY_HISTORY_READ] =
        pContext->resources[FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_SKY_VISIBILITY_HISTORY_0 + pContext->skyVisibilityHistoryIndex];
    pContext->resources[FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_SKY_VISIBILITY_HISTORY_WRITE] =
        pContext->resources[FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_SKY_VISIBILITY_HISTORY_0 + skyHistoryWrite];
    const bool bSkyVisibilityHistory      = bSkyVisibility && pContext->bSkyVisibilityHistoryValid;
    pContext->skyVisibilityHistoryIndex   = skyHistoryWrite;
    pContext->bSkyVisibilityHistoryValid  = bSkyVisibility;
pContext->contextDescription.backendInterface.fpRegisterResource(&pContext->contextDescription.backendInterface,
                                                                     &pDispatchDescription->bricksAABBs,
                                                                     pContext->effectContextId,
                                                                     &pContext->resources[FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_INPUT_CONTEXT_BRICKS_AABB]);
    pContext->contextDescription.backendInterface.fpRegisterResource(&pContext->contextDescription.backendInterface,
                                                                     &bricksVoxelMap,
                                                                     pContext->effectContextId,
                                                                     &pContext->resources[FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_INPUT_CONTEXT_BRICKS_VOXEL_MAP]);
    pContext->contextDescription.backendInterface.fpRegisterResource(&pContext->contextDescription.backendInterface,
                                                                     &bricksClearList,
                                                                     pContext->effectContextId,
                                                                     &pContext->resources[FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_INPUT_BRICKS_CLEAR_LIST]);
    pContext->contextDescription.backendInterface.fpRegisterResource(&pContext->contextDescription.backendInterface,
                                                                     &contextCounters,
                                                                     pContext->effectContextId,
                                                                     &pContext->resources[FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_INPUT_CONTEXT_COUNTERS]);
    // Register Brixelizer Resources
    
    for (uint32_t i = 0; i < FFX_ARRAY_ELEMENTS(pDispatchDescription->cascadeAABBTrees); ++i)
    {
        if (isResourceNull(pDispatchDescription->cascadeAABBTrees[i]) || isResourceNull(pDispatchDescription->cascadeBrickMaps[i]))
            continue;

        pContext->contextDescription.backendInterface.fpRegisterResource(&pContext->contextDescription.backendInterface,
                                                                         &pDispatchDescription->cascadeAABBTrees[i],
                                                                         pContext->effectContextId,
                                                                         &pContext->resources[FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_INPUT_CASCADE_AABB_TREES + i]);
        pContext->contextDescription.backendInterface.fpRegisterResource(&pContext->contextDescription.backendInterface,
                                                                         &pDispatchDescription->cascadeBrickMaps[i],
                                                                         pContext->effectContextId,
                                                                         &pContext->resources[FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_INPUT_CASCADE_BRICK_MAPS + i]);
    }

    uint32_t bufferWidth = pContext->internalSize.width;
    uint32_t bufferHeight = pContext->internalSize.height;
 
    uint32_t probeBufferWidth  = FFX_BRIXELIZER_GI_SCREEN_PROBE_SIZE * ((pContext->internalSize.width + FFX_BRIXELIZER_GI_SCREEN_PROBE_SIZE - 1) / FFX_BRIXELIZER_GI_SCREEN_PROBE_SIZE);
    uint32_t probeBufferHeight = FFX_BRIXELIZER_GI_SCREEN_PROBE_SIZE * ((pContext->internalSize.height + FFX_BRIXELIZER_GI_SCREEN_PROBE_SIZE - 1) / FFX_BRIXELIZER_GI_SCREEN_PROBE_SIZE);
    
    uint32_t tileBufferWidth  = ((pContext->internalSize.width + FFX_BRIXELIZER_GI_SCREEN_PROBE_SIZE - 1) / FFX_BRIXELIZER_GI_SCREEN_PROBE_SIZE);
    uint32_t tileBufferHeight = ((pContext->internalSize.height + FFX_BRIXELIZER_GI_SCREEN_PROBE_SIZE - 1) / FFX_BRIXELIZER_GI_SCREEN_PROBE_SIZE);

    FfxBrixelizerGIConstants giConstants = {};

    // Calculate matrices
    {
        FfxFloat32x4x4 view = {};
        FfxFloat32x4x4 projection = {};
        FfxFloat32x4x4 viewProjection = {};
        FfxFloat32x4x4 prevView = {};
        FfxFloat32x4x4 prevProjection = {};
        FfxFloat32x4x4 prevViewProjection = {};

        FfxFloat32x4x4 invView = {};
        FfxFloat32x4x4 invProj = {};
        FfxFloat32x4x4 invViewProj = {};
        FfxFloat32x4x4 prevInvView = {};
        FfxFloat32x4x4 prevInvProj = {};

        memcpy(view,           pDispatchDescription->view,           sizeof(view));
        memcpy(projection,     pDispatchDescription->projection,     sizeof(projection));
        memcpy(prevView,       pDispatchDescription->prevView,       sizeof(prevView));
        memcpy(prevProjection, pDispatchDescription->prevProjection, sizeof(prevProjection));

        matrixMul(view,     projection,     viewProjection);
        matrixMul(prevView, prevProjection, prevViewProjection);

        matrixInvert(view,           invView);
        matrixInvert(projection,     invProj);
        matrixInvert(viewProjection, invViewProj);
        matrixInvert(prevView,       prevInvView);
        matrixInvert(prevProjection, prevInvProj);

        memcpy(&giConstants.view,           view,               sizeof(FfxFloat32x4x4));
        memcpy(&giConstants.view_proj,      viewProjection,     sizeof(FfxFloat32x4x4));
        memcpy(&giConstants.inv_view,       invView,            sizeof(FfxFloat32x4x4));
        memcpy(&giConstants.inv_proj,       invProj,            sizeof(FfxFloat32x4x4));
        memcpy(&giConstants.inv_view_proj,  invViewProj,        sizeof(FfxFloat32x4x4));
        memcpy(&giConstants.prev_view_proj, prevViewProjection, sizeof(FfxFloat32x4x4));
        memcpy(&giConstants.prev_inv_view,  prevInvView,        sizeof(FfxFloat32x4x4));
        memcpy(&giConstants.prev_inv_proj,  prevInvProj,        sizeof(FfxFloat32x4x4));
    }

    memcpy(giConstants.camera_position, pDispatchDescription->cameraPosition, sizeof(FfxFloat32x3));

    giConstants.target_width                        = bufferWidth;
    giConstants.target_height                       = bufferHeight;
    giConstants.buffer_dimensions[0]                = bufferWidth;
    giConstants.buffer_dimensions[1]                = bufferHeight;
    giConstants.buffer_dimensions_f32[0]            = static_cast<float>(giConstants.buffer_dimensions[0]);
    giConstants.buffer_dimensions_f32[1]            = static_cast<float>(giConstants.buffer_dimensions[1]);
    giConstants.ibuffer_dimensions[0]               = 1.0f / giConstants.buffer_dimensions_f32[0];
    giConstants.ibuffer_dimensions[1]               = 1.0f / giConstants.buffer_dimensions_f32[1];
    giConstants.probe_buffer_dimensions[0]          = probeBufferWidth;
    giConstants.probe_buffer_dimensions[1]          = probeBufferHeight;
    giConstants.probe_buffer_dimensions_f32[0]      = static_cast<float>(giConstants.probe_buffer_dimensions[0]);
    giConstants.probe_buffer_dimensions_f32[1]      = static_cast<float>(giConstants.probe_buffer_dimensions[1]);
    giConstants.iprobe_buffer_dimensions[0]         = 1.0f / giConstants.probe_buffer_dimensions_f32[0];
    giConstants.iprobe_buffer_dimensions[1]         = 1.0f / giConstants.probe_buffer_dimensions_f32[1];
    giConstants.tile_buffer_dimensions[0]           = tileBufferWidth;
    giConstants.tile_buffer_dimensions[1]           = tileBufferHeight;
    giConstants.tile_buffer_dimensions_f32[0]       = static_cast<float>(giConstants.tile_buffer_dimensions[0]);
    giConstants.tile_buffer_dimensions_f32[1]       = static_cast<float>(giConstants.tile_buffer_dimensions[1]);
    giConstants.brick_tile_buffer_dimensions[0]     = (giConstants.buffer_dimensions[0] + FFX_BRIXELIZER_GI_BRICK_TILE_SIZE - 1) / FFX_BRIXELIZER_GI_BRICK_TILE_SIZE;
    giConstants.brick_tile_buffer_dimensions[1]     = (giConstants.buffer_dimensions[1] + FFX_BRIXELIZER_GI_BRICK_TILE_SIZE - 1) / FFX_BRIXELIZER_GI_BRICK_TILE_SIZE;
    giConstants.brick_tile_buffer_dimensions_f32[0] = static_cast<float>(giConstants.brick_tile_buffer_dimensions[0]);
    giConstants.brick_tile_buffer_dimensions_f32[1] = static_cast<float>(giConstants.brick_tile_buffer_dimensions[1]);
    giConstants.environmentMapIntensity             = pDispatchDescription->environmentMapIntensity;
    giConstants.roughnessChannel                    = pContext->contextDescription.internalResolution == FFX_BRIXELIZER_GI_INTERNAL_RESOLUTION_NATIVE ? pDispatchDescription->roughnessChannel : 0;
    giConstants.isRoughnessPerceptual               = static_cast<uint32_t>(pDispatchDescription->isRoughnessPerceptual);
    giConstants.roughnessThreshold                  = pDispatchDescription->roughnessThreshold;
    giConstants.normalsUnpackMul                    = pDispatchDescription->normalsUnpackMul;
    giConstants.normalsUnpackAdd                    = pDispatchDescription->normalsUnpackAdd;
    giConstants.motionVectorScale[0]                = pDispatchDescription->motionVectorScale.x;
    giConstants.motionVectorScale[1]                = pDispatchDescription->motionVectorScale.y;
    giConstants.frame_index                         = pContext->frameIndex;

    giConstants.tracing_constants.start_cascade          = pDispatchDescription->startCascade;
    giConstants.tracing_constants.end_cascade            = pDispatchDescription->endCascade;
    giConstants.tracing_constants.debug_state            = 0;
    giConstants.tracing_constants.debug_traversal_state  = 0;
    giConstants.tracing_constants.ray_pushoff            = pDispatchDescription->rayPushoff;
    giConstants.tracing_constants.sdf_solve_eps          = pDispatchDescription->sdfSolveEps;
    giConstants.tracing_constants.specular_ray_pushoff   = pDispatchDescription->specularRayPushoff;
    giConstants.tracing_constants.specular_sdf_solve_eps = pDispatchDescription->specularSDFSolveEps;
    giConstants.tracing_constants.preview_ray_pushoff    = 0.0f;
    giConstants.tracing_constants.preview_sdf_solve_eps  = 0.0f;
    giConstants.tracing_constants.t_min                  = pDispatchDescription->tMin;
    giConstants.tracing_constants.t_max                  = pDispatchDescription->tMax;
    giConstants.sky_visibility_enabled                   = bSkyVisibility ? (bSkyVisibilityHistory ? 2u : 1u) : 0u;
    giConstants.world_ao_radius                          = pDispatchDescription->worldAORadius > 0.0f ? pDispatchDescription->worldAORadius : 1.0f;
    giConstants.cache_feed_max                           = isResourceNull(pDispatchDescription->cacheFeed) ? 0u : pDispatchDescription->maxCacheFeedEntries;
    giConstants.cache_feed_flags                         = pDispatchDescription->cacheFeedFlags;
    giConstants.cache_feed_max_samples                   = pDispatchDescription->cacheSettings.feedMaxSamples > 0.0f ? pDispatchDescription->cacheSettings.feedMaxSamples : 16.0f;

    FfxBrixelizerContextInfo contextInfo = {};
    FFX_VALIDATE(ffxBrixelizerRawContextGetInfo((FfxBrixelizerRawContext*)rawContext, &contextInfo));

    updateConstantBuffer(pContext, FFX_BRIXELIZER_GI_CONSTANTBUFFER_IDENTIFIER_GI_CONSTANTS, (void*)&giConstants);
    updateConstantBuffer(pContext, FFX_BRIXELIZER_GI_CONSTANTBUFFER_IDENTIFIER_CONTEXT_INFO, &contextInfo);
    
    // Setup ping pong resource IDs
    pContext->historyScreenProbesId   = pContext->currentScreenProbesId;
    pContext->historyGITargetId       = pContext->currentGITargetId;
    pContext->historySpecularTargetId = pContext->currentSpecularTargetId;

    uint32_t reprojectedGITargetId        = 0;
    uint32_t reprojectedSpecularTargetId  = 0;
    uint32_t spatialFilterTargetId        = 0;
    uint32_t specularTraceTargetId        = 0;
    uint32_t interpolatedGITargetId       = 0;
    uint32_t interpolatedSpecularTargetId = 0;
    uint32_t blurXGITargetId              = 0;
    uint32_t blurXSpecularTargetId        = 0;
    uint32_t blurYGITargetId              = 0;
    uint32_t blurYSpecularTargetId        = 0;
    uint32_t reprojectedScreenProbesId    = 0;
    uint32_t filledScreenProbesId         = 0;

    if (pContext->frameIndex == 0) {
        FfxGpuJobDescription job        = {};
        job.jobType                     = FFX_GPU_JOB_CLEAR_FLOAT;
        wcscpy_s(job.jobLabel, L"Clear Brixelizer GI Resource");
        job.clearJobDescriptor.color[0] = 0.0f;
        job.clearJobDescriptor.color[1] = 0.0f;
        job.clearJobDescriptor.color[2] = 0.0f;
        job.clearJobDescriptor.color[3] = 0.0f;

        uint32_t resourceIDs[] = {
            FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_SPECULAR_TARGET_0,
            FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_SPECULAR_TARGET_1,
            FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_STATIC_GI_TARGET_0,
            FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_STATIC_GI_TARGET_1,
            FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_STATIC_SCREEN_PROBES_0,
            FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_STATIC_SCREEN_PROBES_1,
            FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_RADIANCE_CACHE,
        };

        for (const uint32_t* resourceID = resourceIDs; resourceID < resourceIDs + FFX_ARRAY_ELEMENTS(resourceIDs); ++resourceID)
        {
            // vkfw: the application's caches outlive this context (a resize recreates it), so its first frame must not wipe them.
            if (brixelizerGIIsExternalResource(pContext->contextDescription.flags, *resourceID))
                continue;
            job.clearJobDescriptor.target = pContext->resources[*resourceID];
            pContext->contextDescription.backendInterface.fpScheduleGpuJob(&pContext->contextDescription.backendInterface, &job);
        }
    }

    if (pContext->contextDescription.internalResolution != FFX_BRIXELIZER_GI_INTERNAL_RESOLUTION_NATIVE)
    {
        FfxBrixelizerGIScalingConstants scalingConstants = {};

        scalingConstants.sourceSize[0]      = pContext->contextDescription.displaySize.width;
        scalingConstants.sourceSize[1]      = pContext->contextDescription.displaySize.height;
        scalingConstants.downsampledSize[0] = pContext->internalSize.width;
        scalingConstants.downsampledSize[1] = pContext->internalSize.height;
        scalingConstants.roughnessChannel   = pDispatchDescription->roughnessChannel;

        updateConstantBuffer(pContext, FFX_BRIXELIZER_GI_CONSTANTBUFFER_IDENTIFIER_SCALING_CONSTANTS, (void*)&scalingConstants);

        scheduleDispatch(pContext, &pContext->pipelineDownsample, (bufferWidth + 7) / 8, (bufferHeight + 7) / 8, 1);
    }

    scheduleDispatch(pContext, &pContext->pipelineGenerateDisocclusionMask, (bufferWidth + 7) / 8, (bufferHeight + 7) / 8, 1);
    // vkfw: with an external brick cache the application initialises a new brick's cache itself -- cleared, or carried
    // over from the brick that covered the same place before -- and has done so by now.
    if (!(pContext->contextDescription.flags & FFX_BRIXELIZER_GI_FLAG_EXTERNAL_BRICK_CACHE))
    {
        scheduleDispatch(pContext, &pContext->pipelinePrepareClearCache, 1, 1, 1);
        scheduleIndirectDispatch(pContext, &pContext->pipelineClearCache, pContext->resources[FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_RAY_SWAP_INDIRECT_ARGS], 0);
    }
    else if (pContext->contextDescription.flags & FFX_BRIXELIZER_GI_FLAG_CACHE_SLOTS)
    {
        // vkfw patch: the merged cascades the SDK rebuilt since the last dispatch, and the moves of its mover instances, with the application's.
        uint32_t                          rebuiltCascades = 0;
        const FfxBrixelizerRawMoverRemap* remaps          = NULL;
        uint32_t                          numRemaps       = 0;
        ffxBrixelizerRawContextTakeCacheUpdates(pDispatchDescription->brixelizerContext, &rebuiltCascades, &remaps, &numRemaps);
        uint32_t updatedLevelMask = pDispatchDescription->updatedCascadeMask;
        for (uint32_t level = 0; level < FFX_BRIXELIZER_GI_SLOT_LEVELS; ++level)
        {
            const uint32_t cascadeIndex = pDispatchDescription->startCascade + level;
            if (cascadeIndex < FFX_BRIXELIZER_MAX_CASCADES && (rebuiltCascades & (1u << cascadeIndex)))
                updatedLevelMask |= 1u << level;
        }
        FfxBrixelizerRawCacheSlotState* slotState = ffxBrixelizerRawContextGetCacheSlotState(pDispatchDescription->brixelizerContext);
        if (updatedLevelMask != 0 && slotState)
            brixelizerGIScheduleSlots(pContext, pDispatchDescription, &contextInfo, slotState, updatedLevelMask, remaps, numRemaps);
    }
    // vkfw patch: the application's cache feed, after the slots are handed out and before anything shades.
    if (!isResourceNull(pDispatchDescription->cacheFeed) && !isResourceNull(pDispatchDescription->cacheFeedCount) && pDispatchDescription->maxCacheFeedEntries > 0)
    {
        pContext->contextDescription.backendInterface.fpRegisterResource(&pContext->contextDescription.backendInterface, &pDispatchDescription->cacheFeed,
                                                                         pContext->effectContextId, &pContext->resources[FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_CACHE_FEED]);
        pContext->contextDescription.backendInterface.fpRegisterResource(&pContext->contextDescription.backendInterface, &pDispatchDescription->cacheFeedCount,
                                                                         pContext->effectContextId, &pContext->resources[FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_CACHE_FEED_COUNT]);
        scheduleDispatch(pContext, &pContext->pipelineCacheFeed, (pDispatchDescription->maxCacheFeedEntries + 63) / 64, 1, 1);
    }
scheduleDispatch(pContext, &pContext->pipelineEmitPrimaryRayRadiance, (bufferWidth / 4 + 7) / 8, (bufferHeight / 4 + 7) / 8, 1);

    FfxBrixelizerGIPassConstants passConstants = {};

    uint32_t cascadeOffset = pDispatchDescription->startCascade;
    uint32_t numCascades = pDispatchDescription->endCascade - pDispatchDescription->startCascade;

    passConstants.cascade_idx    = cascadeOffset + (numCascades > 0 ? ffxBrixelizerRawGetCascadeToUpdate(pContext->frameIndex, numCascades) : 0);
    passConstants.energy_decay_k = pow(float(1.0 - 2.0e-1), float(passConstants.cascade_idx - cascadeOffset));

    updateConstantBuffer(pContext, FFX_BRIXELIZER_GI_CONSTANTBUFFER_IDENTIFIER_PASS_CONSTANTS, (void*)&passConstants);

    scheduleDispatch(pContext, &pContext->pipelinePropagateSH, (FFX_BRIXELIZER_CASCADE_RESOLUTION * FFX_BRIXELIZER_CASCADE_RESOLUTION * FFX_BRIXELIZER_CASCADE_RESOLUTION + 63) / 64, 1, 1);

    {
        FfxUInt32x2    tileSize   = {8, 8};
        uint32_t const numGroupsX = (tileBufferWidth + tileSize[0] - 1) / tileSize[0];
        uint32_t const numGroupsY = (tileBufferHeight + tileSize[1] - 1) / tileSize[1];

        scheduleDispatch(pContext, &pContext->pipelineSpawnScreenProbes, numGroupsX, numGroupsY, 1);
    }

    {
        FfxUInt32x2    tileSize   = {8, 8};
        uint32_t const numGroupsX = (probeBufferWidth + tileSize[0] - 1) / tileSize[0];
        uint32_t const numGroupsY = (probeBufferHeight + tileSize[1] - 1) / tileSize[1];

        reprojectedScreenProbesId = getNextScreenProbesId(pContext->historyScreenProbesId);

        setPingPongResourceId(pContext, FFX_BRIXELIZER_GI_PING_PONG_RESOURCE_STATIC_SCREEN_PROBES_READ, pContext->historyScreenProbesId);
        setPingPongResourceId(pContext, FFX_BRIXELIZER_GI_PING_PONG_RESOURCE_STATIC_SCREEN_PROBES_WRITE, reprojectedScreenProbesId);

        scheduleDispatch(pContext, &pContext->pipelineReprojectScreenProbes, numGroupsX, numGroupsY, 1);
    }

    {
        FfxUInt32x2    tileSize   = {8, 4};
        uint32_t const numGroupsX = (probeBufferWidth + tileSize[0] - 1) / tileSize[0];
        uint32_t const numGroupsY = (probeBufferHeight + tileSize[1] - 1) / tileSize[1];

        filledScreenProbesId = getNextScreenProbesId(reprojectedScreenProbesId);

        setPingPongResourceId(pContext, FFX_BRIXELIZER_GI_PING_PONG_RESOURCE_STATIC_SCREEN_PROBES_READ, reprojectedScreenProbesId);
        setPingPongResourceId(pContext, FFX_BRIXELIZER_GI_PING_PONG_RESOURCE_STATIC_SCREEN_PROBES_WRITE, filledScreenProbesId);

        scheduleDispatch(pContext, &pContext->pipelineFillScreenProbes, numGroupsX, numGroupsY, 1);
    }
    
    if ((pContext->contextDescription.flags & FFX_BRIXELIZER_GI_FLAG_DISABLE_SPECULAR) == 0)
    {
        FfxUInt32x2    tileSize   = {8, 4};
        uint32_t const numGroupsX = (tileBufferWidth * 2 + tileSize[0] - 1) / tileSize[0];
        uint32_t const numGroupsY = (tileBufferHeight * 2 + tileSize[1] - 1) / tileSize[1];

        scheduleDispatch(pContext, &pContext->pipelineSpecularPreTrace, numGroupsX, numGroupsY, 1);
    }

    if ((pContext->contextDescription.flags & FFX_BRIXELIZER_GI_FLAG_DISABLE_DENOISER) == 0)
    {
        FfxUInt32x2    tileSize   = {8, 8};
        uint32_t const numGroupsX = (bufferWidth + tileSize[0] - 1) / tileSize[0];
        uint32_t const numGroupsY = (bufferHeight + tileSize[1] - 1) / tileSize[1];

        reprojectedGITargetId = getNextGITargetId(pContext->historyGITargetId);

        setPingPongResourceId(pContext, FFX_BRIXELIZER_GI_PING_PONG_RESOURCE_STATIC_GI_TARGET_READ, pContext->historyGITargetId);
        setPingPongResourceId(pContext, FFX_BRIXELIZER_GI_PING_PONG_RESOURCE_STATIC_GI_TARGET_WRITE, reprojectedGITargetId);
        
        reprojectedSpecularTargetId = getNextSpecularTargetId(pContext->historySpecularTargetId);

        setPingPongResourceId(pContext, FFX_BRIXELIZER_GI_PING_PONG_RESOURCE_STATIC_SPECULAR_TARGET_READ, pContext->historySpecularTargetId);
        setPingPongResourceId(pContext, FFX_BRIXELIZER_GI_PING_PONG_RESOURCE_STATIC_SPECULAR_TARGET_WRITE, reprojectedSpecularTargetId);

        scheduleDispatch(pContext, &pContext->pipelineReprojectGI, numGroupsX, numGroupsY, 1);
    }

    if ((pContext->contextDescription.flags & FFX_BRIXELIZER_GI_FLAG_DISABLE_SPECULAR) == 0)
    {
        FfxUInt32x2    tileSize   = {8, 4};
        uint32_t const numGroupsX = (bufferWidth + tileSize[0] - 1) / tileSize[0];
        uint32_t const numGroupsY = (bufferHeight + tileSize[1] - 1) / tileSize[1];

        specularTraceTargetId = pContext->historySpecularTargetId;

        setPingPongResourceId(pContext, FFX_BRIXELIZER_GI_PING_PONG_RESOURCE_STATIC_SPECULAR_TARGET_WRITE, specularTraceTargetId);

        scheduleDispatch(pContext, &pContext->pipelineSpecularTrace, numGroupsX, numGroupsY, 1);
    }

    {
        FfxUInt32x2    tileSize   = {8, 8};
        uint32_t const numGroupsX = (probeBufferWidth + tileSize[0] - 1) / tileSize[0];
        uint32_t const numGroupsY = (probeBufferHeight + tileSize[1] - 1) / tileSize[1];

        setPingPongResourceId(pContext, FFX_BRIXELIZER_GI_PING_PONG_RESOURCE_STATIC_SCREEN_PROBES_READ, filledScreenProbesId);

        scheduleDispatch(pContext, &pContext->pipelineProjectScreenProbes, numGroupsX, numGroupsY, 1);
    }

    {
        FfxUInt32x2    tileSize   = {8, 8};
        uint32_t const numGroupsX = (tileBufferWidth + tileSize[0] - 1) / tileSize[0];
        uint32_t const numGroupsY = (tileBufferHeight + tileSize[1] - 1) / tileSize[1];

        scheduleDispatch(pContext, &pContext->pipelineEmitIrradianceCache, numGroupsX, numGroupsY, 1);
    }

    {
        FfxUInt32x2    tileSize   = {8, 8};
        uint32_t const numGroupsX = (bufferWidth + tileSize[0] - 1) / tileSize[0];
        uint32_t const numGroupsY = (bufferHeight + tileSize[1] - 1) / tileSize[1];

        setPingPongResourceId(pContext, FFX_BRIXELIZER_GI_PING_PONG_RESOURCE_STATIC_SCREEN_PROBES_READ, filledScreenProbesId);

        interpolatedGITargetId = getNextGITargetId(reprojectedGITargetId);

        if ((pContext->contextDescription.flags & FFX_BRIXELIZER_GI_FLAG_DISABLE_DENOISER) == FFX_BRIXELIZER_GI_FLAG_DISABLE_DENOISER)
            interpolatedGITargetId = FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_OUTPUT_DIFFUSE_GI;

        setPingPongResourceId(pContext, FFX_BRIXELIZER_GI_PING_PONG_RESOURCE_STATIC_GI_TARGET_READ, reprojectedGITargetId);
        setPingPongResourceId(pContext, FFX_BRIXELIZER_GI_PING_PONG_RESOURCE_STATIC_GI_TARGET_WRITE, interpolatedGITargetId);

        interpolatedSpecularTargetId = getNextSpecularTargetId(reprojectedSpecularTargetId);

        if ((pContext->contextDescription.flags & FFX_BRIXELIZER_GI_FLAG_DISABLE_DENOISER) == FFX_BRIXELIZER_GI_FLAG_DISABLE_DENOISER)
            interpolatedSpecularTargetId = FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_OUTPUT_SPECULAR_GI;

        setPingPongResourceId(pContext, FFX_BRIXELIZER_GI_PING_PONG_RESOURCE_STATIC_SPECULAR_TARGET_READ, reprojectedSpecularTargetId);
        setPingPongResourceId(pContext, FFX_BRIXELIZER_GI_PING_PONG_RESOURCE_STATIC_SPECULAR_TARGET_WRITE, interpolatedSpecularTargetId);

        scheduleDispatch(pContext, &pContext->pipelineInterpolateScreenProbes, numGroupsX, numGroupsY, 1);

        pContext->currentGITargetId       = interpolatedGITargetId;
        pContext->currentSpecularTargetId = interpolatedSpecularTargetId;
    }

    if ((pContext->contextDescription.flags & FFX_BRIXELIZER_GI_FLAG_DISABLE_DENOISER) == 0)
    {
        FfxUInt32x2    tileSize   = {8, 8};
        uint32_t const numGroupsX = (bufferWidth + tileSize[0] - 1) / tileSize[0];
        uint32_t const numGroupsY = (bufferHeight + tileSize[1] - 1) / tileSize[1];

        blurXGITargetId = getNextGITargetId(interpolatedGITargetId);

        setPingPongResourceId(pContext, FFX_BRIXELIZER_GI_PING_PONG_RESOURCE_STATIC_GI_TARGET_READ, interpolatedGITargetId);
        setPingPongResourceId(pContext, FFX_BRIXELIZER_GI_PING_PONG_RESOURCE_STATIC_GI_TARGET_WRITE, blurXGITargetId);

        blurXSpecularTargetId = getNextSpecularTargetId(interpolatedSpecularTargetId);

        setPingPongResourceId(pContext, FFX_BRIXELIZER_GI_PING_PONG_RESOURCE_STATIC_SPECULAR_TARGET_READ, interpolatedSpecularTargetId);
        setPingPongResourceId(pContext, FFX_BRIXELIZER_GI_PING_PONG_RESOURCE_STATIC_SPECULAR_TARGET_WRITE, blurXSpecularTargetId);

        scheduleDispatch(pContext, &pContext->pipelineBlurX, numGroupsX, numGroupsY, 1);
    }

    if ((pContext->contextDescription.flags & FFX_BRIXELIZER_GI_FLAG_DISABLE_DENOISER) == 0)
    {
        FfxUInt32x2    tileSize   = {8, 8};
        uint32_t const numGroupsX = (bufferWidth + tileSize[0] - 1) / tileSize[0];
        uint32_t const numGroupsY = (bufferHeight + tileSize[1] - 1) / tileSize[1];

        setPingPongResourceId(pContext, FFX_BRIXELIZER_GI_PING_PONG_RESOURCE_STATIC_GI_TARGET_READ, blurXGITargetId);
        setPingPongResourceId(pContext, FFX_BRIXELIZER_GI_PING_PONG_RESOURCE_STATIC_GI_TARGET_WRITE, FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_OUTPUT_DIFFUSE_GI);

        setPingPongResourceId(pContext, FFX_BRIXELIZER_GI_PING_PONG_RESOURCE_STATIC_SPECULAR_TARGET_READ, blurXSpecularTargetId);
        setPingPongResourceId(pContext, FFX_BRIXELIZER_GI_PING_PONG_RESOURCE_STATIC_SPECULAR_TARGET_WRITE, FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_OUTPUT_SPECULAR_GI);

        scheduleDispatch(pContext, &pContext->pipelineBlurY, numGroupsX, numGroupsY, 1);
    }

    if (pContext->contextDescription.internalResolution != FFX_BRIXELIZER_GI_INTERNAL_RESOLUTION_NATIVE)
        scheduleDispatch(pContext, &pContext->pipelineUpsample, (pContext->contextDescription.displaySize.width + 7) / 8, (pContext->contextDescription.displaySize.height + 7) / 8, 1);

    // Execute jobs
    pContext->contextDescription.backendInterface.fpExecuteGpuJobs(&pContext->contextDescription.backendInterface, pCommandList, pContext->effectContextId);

    // Release dynamic resources
    pContext->contextDescription.backendInterface.fpUnregisterResources(&pContext->contextDescription.backendInterface, pCommandList, pContext->effectContextId);

    pContext->frameIndex++;

    pContext->currentScreenProbesId = filledScreenProbesId;

    return FFX_OK;
}

static FfxErrorCode brixelizerGIDebugVisualization(FfxBrixelizerGIContext_Private*        pContext,
                                                   const FfxBrixelizerGIDebugDescription* pDebugDescription,
                                                   FfxCommandList                         pCommandList)
{
    FfxBrixelizerRawContext_Private* rawContext      = (FfxBrixelizerRawContext_Private*)pDebugDescription->brixelizerContext;
    FfxResource                      bricksVoxelMap  = rawContext->contextDescription.backendInterface.fpGetResource(&rawContext->contextDescription.backendInterface, rawContext->resources[FFX_BRIXELIZER_RESOURCE_IDENTIFIER_CONTEXT_BRICKS_VOXEL_MAP]);
    FfxResource                      bricksClearList = rawContext->contextDescription.backendInterface.fpGetResource(&rawContext->contextDescription.backendInterface, rawContext->resources[FFX_BRIXELIZER_RESOURCE_IDENTIFIER_CONTEXT_BRICKS_CLEAR_LIST]);
    FfxResource                      contextCounters = rawContext->contextDescription.backendInterface.fpGetResource(&rawContext->contextDescription.backendInterface, rawContext->resources[FFX_BRIXELIZER_RESOURCE_IDENTIFIER_CONTEXT_COUNTERS]);

    // Register Application Resources

    pContext->contextDescription.backendInterface.fpRegisterResource(&pContext->contextDescription.backendInterface,
                                                                     &pDebugDescription->outputDebug,
                                                                     pContext->effectContextId,
                                                                     &pContext->resources[FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_OUTPUT_DEBUG_VISUALIZATION]);
    
    pContext->contextDescription.backendInterface.fpRegisterResource(&pContext->contextDescription.backendInterface,
                                                                     &pDebugDescription->depth,
                                                                     pContext->effectContextId,
                                                                     &pContext->resources[FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_INPUT_DEPTH]);
    pContext->contextDescription.backendInterface.fpRegisterResource(&pContext->contextDescription.backendInterface,
                                                                     &pDebugDescription->normal,
                                                                     pContext->effectContextId,
                                                                     &pContext->resources[FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_INPUT_NORMAL]);
    
    pContext->contextDescription.backendInterface.fpRegisterResource(&pContext->contextDescription.backendInterface,
                                                                     &pDebugDescription->sdfAtlas,
                                                                     pContext->effectContextId,
                                                                     &pContext->resources[FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_INPUT_SDF_ATLAS]);
    brixelizerGIRegisterExternalResources(pContext, &pDebugDescription->radianceCache, &pDebugDescription->bricksSH,
                                          &pDebugDescription->bricksDirectSH, &pDebugDescription->bricksSHState, &pDebugDescription->brickSlotMap, &pDebugDescription->slotCounters,
                                          NULL, NULL);
    pContext->contextDescription.backendInterface.fpRegisterResource(&pContext->contextDescription.backendInterface,
                                                                     &pDebugDescription->bricksAABBs,
                                                                     pContext->effectContextId,
                                                                     &pContext->resources[FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_INPUT_CONTEXT_BRICKS_AABB]);
    pContext->contextDescription.backendInterface.fpRegisterResource(&pContext->contextDescription.backendInterface,
                                                                     &bricksVoxelMap,
                                                                     pContext->effectContextId,
                                                                     &pContext->resources[FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_INPUT_CONTEXT_BRICKS_VOXEL_MAP]);
    pContext->contextDescription.backendInterface.fpRegisterResource(&pContext->contextDescription.backendInterface,
                                                                     &bricksClearList,
                                                                     pContext->effectContextId,
                                                                     &pContext->resources[FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_INPUT_BRICKS_CLEAR_LIST]);
    pContext->contextDescription.backendInterface.fpRegisterResource(&pContext->contextDescription.backendInterface,
                                                                     &contextCounters,
                                                                     pContext->effectContextId,
                                                                     &pContext->resources[FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_INPUT_CONTEXT_COUNTERS]);
    // Register Brixelizer Resources
    
    for (uint32_t i = 0; i < FFX_ARRAY_ELEMENTS(pDebugDescription->cascadeAABBTrees); ++i)
    {
        if (isResourceNull(pDebugDescription->cascadeAABBTrees[i]) || isResourceNull(pDebugDescription->cascadeBrickMaps[i]))
            continue;

        pContext->contextDescription.backendInterface.fpRegisterResource(&pContext->contextDescription.backendInterface,
                                                                         &pDebugDescription->cascadeAABBTrees[i],
                                                                         pContext->effectContextId,
                                                                         &pContext->resources[FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_INPUT_CASCADE_AABB_TREES + i]);
        pContext->contextDescription.backendInterface.fpRegisterResource(&pContext->contextDescription.backendInterface,
                                                                         &pDebugDescription->cascadeBrickMaps[i],
                                                                         pContext->effectContextId,
                                                                         &pContext->resources[FFX_BRIXELIZER_GI_RESOURCE_IDENTIFIER_INPUT_CASCADE_BRICK_MAPS + i]);
    }

    FfxBrixelizerGIConstants giConstants = {};

    // Calculate matrices
    {
        FfxFloat32x4x4 view               = {};
        FfxFloat32x4x4 projection         = {};
        FfxFloat32x4x4 viewProjection     = {};

        FfxFloat32x4x4 invView     = {};
        FfxFloat32x4x4 invProj     = {};
        FfxFloat32x4x4 invViewProj = {};

        memcpy(view, pDebugDescription->view, sizeof(view));
        memcpy(projection, pDebugDescription->projection, sizeof(projection));
       
        matrixMul(view, projection, viewProjection);
      
        matrixInvert(view, invView);
        matrixInvert(projection, invProj);
        matrixInvert(viewProjection, invViewProj);

        memcpy(&giConstants.view, view, sizeof(FfxFloat32x4x4));
        memcpy(&giConstants.view_proj, viewProjection, sizeof(FfxFloat32x4x4));
        memcpy(&giConstants.inv_view, invView, sizeof(FfxFloat32x4x4));
        memcpy(&giConstants.inv_proj, invProj, sizeof(FfxFloat32x4x4));
        memcpy(&giConstants.inv_view_proj, invViewProj, sizeof(FfxFloat32x4x4));
    }

    giConstants.target_width     = pDebugDescription->outputSize[0];
    giConstants.target_height    = pDebugDescription->outputSize[1];
    giConstants.normalsUnpackMul = pDebugDescription->normalsUnpackMul;
    giConstants.normalsUnpackAdd = pDebugDescription->normalsUnpackAdd;
    giConstants.debug_type       = (FfxUInt32)pDebugDescription->debugMode;
    giConstants.frame_index      = pContext->frameIndex;

    giConstants.tracing_constants.start_cascade = pDebugDescription->startCascade;
    giConstants.tracing_constants.end_cascade   = pDebugDescription->endCascade;

    FfxBrixelizerContextInfo contextInfo = {};
    FFX_VALIDATE(ffxBrixelizerRawContextGetInfo((FfxBrixelizerRawContext*)rawContext, &contextInfo));

    updateConstantBuffer(pContext, FFX_BRIXELIZER_GI_CONSTANTBUFFER_IDENTIFIER_GI_CONSTANTS, (void*)&giConstants);
    updateConstantBuffer(pContext, FFX_BRIXELIZER_GI_CONSTANTBUFFER_IDENTIFIER_CONTEXT_INFO, (void*)&contextInfo);

    FfxUInt32x2    tileSize   = {8, 8};
    uint32_t const numGroupsX = (pDebugDescription->outputSize[0] + tileSize[0] - 1) / tileSize[0];
    uint32_t const numGroupsY = (pDebugDescription->outputSize[1] + tileSize[1] - 1) / tileSize[1];

    scheduleDispatch(pContext, &pContext->pipelineDebugVisualization, numGroupsX, numGroupsY, 1);

    // Execute jobs
    pContext->contextDescription.backendInterface.fpExecuteGpuJobs(&pContext->contextDescription.backendInterface, pCommandList, pContext->effectContextId);

    // Release dynamic resources
    pContext->contextDescription.backendInterface.fpUnregisterResources(&pContext->contextDescription.backendInterface, pCommandList, pContext->effectContextId);

    return FFX_OK;
}

FfxErrorCode ffxBrixelizerGIContextCreate(FfxBrixelizerGIContext* pContext, const FfxBrixelizerGIContextDescription* pContextDescription)
{
    // zero context memory
    memset(pContext, 0, sizeof(FfxBrixelizerGIContext));

    // check pointers are valid.
    FFX_RETURN_ON_ERROR(pContext, FFX_ERROR_INVALID_POINTER);
    FFX_RETURN_ON_ERROR(pContextDescription, FFX_ERROR_INVALID_POINTER);

    // validate that all callbacks are set for the interface
    FFX_RETURN_ON_ERROR(pContextDescription->backendInterface.fpGetDeviceCapabilities, FFX_ERROR_INCOMPLETE_INTERFACE);
    FFX_RETURN_ON_ERROR(pContextDescription->backendInterface.fpCreateBackendContext, FFX_ERROR_INCOMPLETE_INTERFACE);
    FFX_RETURN_ON_ERROR(pContextDescription->backendInterface.fpDestroyBackendContext, FFX_ERROR_INCOMPLETE_INTERFACE);

    // vkfw: the context's clear pass also zeroes a new brick's radiance, so the application can only take over that pass
    // when it owns the radiance cache too.
    FFX_RETURN_ON_ERROR(!(pContextDescription->flags & FFX_BRIXELIZER_GI_FLAG_CACHE_SLOTS) || (pContextDescription->flags & FFX_BRIXELIZER_GI_FLAG_EXTERNAL_BRICK_CACHE),
                        FFX_ERROR_INVALID_ARGUMENT);
    FFX_RETURN_ON_ERROR(!(pContextDescription->flags & FFX_BRIXELIZER_GI_FLAG_EXTERNAL_BRICK_CACHE) ||
                        (pContextDescription->flags & FFX_BRIXELIZER_GI_FLAG_EXTERNAL_RADIANCE_CACHE), FFX_ERROR_INVALID_ARGUMENT);

    // if a scratch buffer is declared, then we must have a size
    if (pContextDescription->backendInterface.scratchBuffer)
    {
        FFX_RETURN_ON_ERROR(pContextDescription->backendInterface.scratchBufferSize, FFX_ERROR_INCOMPLETE_INTERFACE);
    }

    // ensure the context is large enough for the internal context.
    FFX_STATIC_ASSERT(sizeof(FfxBrixelizerGIContext) >= sizeof(FfxBrixelizerGIContext_Private));

    // create the context.
    FfxBrixelizerGIContext_Private* contextPrivate = (FfxBrixelizerGIContext_Private*)(pContext);
    const FfxErrorCode              errorCode      = brixelizerGICreate(contextPrivate, pContextDescription);

    return errorCode;
}

FfxErrorCode ffxBrixelizerGIGetInternalSize(const FfxBrixelizerGIContext* pContext, FfxDimensions2D* pSize)
{
    FFX_RETURN_ON_ERROR(pContext, FFX_ERROR_INVALID_POINTER);
    FFX_RETURN_ON_ERROR(pSize, FFX_ERROR_INVALID_POINTER);

    const FfxBrixelizerGIContext_Private* pPrivate = (const FfxBrixelizerGIContext_Private*)pContext;
    *pSize = pPrivate->internalSize;
    return FFX_OK;
}

FfxErrorCode ffxBrixelizerGIContextDestroy(FfxBrixelizerGIContext* pContext)
{
    FFX_RETURN_ON_ERROR(pContext, FFX_ERROR_INVALID_POINTER);

    // destroy the context.
    FfxBrixelizerGIContext_Private* contextPrivate = (FfxBrixelizerGIContext_Private*)(pContext);
    const FfxErrorCode              errorCode      = brixelizerGIRelease(contextPrivate);
    return errorCode;
}

FfxErrorCode ffxBrixelizerGIContextDispatch(FfxBrixelizerGIContext*                   pContext,
                                     const FfxBrixelizerGIDispatchDescription* pDispatchDescription,
                                     FfxCommandList                            pCommandList)
{
    FFX_RETURN_ON_ERROR(pContext, FFX_ERROR_INVALID_POINTER);
    FFX_RETURN_ON_ERROR(pDispatchDescription, FFX_ERROR_INVALID_POINTER);

    FfxBrixelizerGIContext_Private* contextPrivate = (FfxBrixelizerGIContext_Private*)(pContext);

    FFX_RETURN_ON_ERROR(contextPrivate->device, FFX_ERROR_NULL_DEVICE);
    // vkfw patch: the slot passes bind the application's slot state; without it they would bind nothing.
    FFX_RETURN_ON_ERROR(!(contextPrivate->contextDescription.flags & FFX_BRIXELIZER_GI_FLAG_CACHE_SLOTS) ||
                            (pDispatchDescription->slotState.resource && pDispatchDescription->slotClaim.resource && pDispatchDescription->slotFreeList.resource &&
                             pDispatchDescription->levelSlotMaps.resource && pDispatchDescription->brickSlotMap.resource &&
                             pDispatchDescription->slotCounters.resource),
                        FFX_ERROR_INVALID_ARGUMENT);

    // dispatch the Brixelizer passes.
    const FfxErrorCode errorCode = brixelizerGIDispatch(contextPrivate, pDispatchDescription, pCommandList);
    return errorCode;
}


FfxErrorCode ffxBrixelizerGIContextDebugVisualization(FfxBrixelizerGIContext* pContext, const FfxBrixelizerGIDebugDescription* pDebugDescription, FfxCommandList pCommandList)
{
    FFX_RETURN_ON_ERROR(pContext, FFX_ERROR_INVALID_POINTER);
    FFX_RETURN_ON_ERROR(pDebugDescription, FFX_ERROR_INVALID_POINTER);

    FfxBrixelizerGIContext_Private* contextPrivate = (FfxBrixelizerGIContext_Private*)(pContext);

    FFX_RETURN_ON_ERROR(contextPrivate->device, FFX_ERROR_NULL_DEVICE);

    // dispatch the Brixelizer passes.
    const FfxErrorCode errorCode = brixelizerGIDebugVisualization(contextPrivate, pDebugDescription, pCommandList);
    return errorCode;
}

FFX_API FfxVersionNumber ffxBrixelizerGIGetEffectVersion()
{
    return FFX_SDK_MAKE_VERSION(FFX_BRIXELIZER_GI_VERSION_MAJOR, FFX_BRIXELIZER_GI_VERSION_MINOR, FFX_BRIXELIZER_GI_VERSION_PATCH);
}
