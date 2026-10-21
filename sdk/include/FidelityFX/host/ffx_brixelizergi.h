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

#pragma once

// Include the interface for the backend of the Brixelizer GI API.
#include <FidelityFX/host/ffx_interface.h>
#include <FidelityFX/host/ffx_brixelizer_raw.h>

/// @defgroup ffxBrixgi FidelityFX Brixelizer GI
/// FidelityFX Brixelizer GI runtime library
///
/// @ingroup SDKComponents


/// FidelityFX Brixelizer GI major version.
///
/// @ingroup ffxBrixgi
#define FFX_BRIXELIZER_GI_VERSION_MAJOR (1)

/// FidelityFX Brixelizer GI minor version.
///
/// @ingroup ffxBrixgi
#define FFX_BRIXELIZER_GI_VERSION_MINOR (0)

/// FidelityFX Brixelizer GI patch version.
///
/// @ingroup ffxBrixgi
#define FFX_BRIXELIZER_GI_VERSION_PATCH (1)

/// The size of the context specified in 32bit values.
///
/// @ingroup ffxBrixgi
#define FFX_BRIXELIZER_GI_CONTEXT_SIZE FFX_PLATFORM_CONTEXT_SIZE(332000)

/// FidelityFX Brixelizer GI context count
/// 
/// Defines the number of internal effect contexts required by Brixelizer
///
/// @ingroup ffxBrixgi
#define FFX_BRIXELIZER_GI_CONTEXT_COUNT 1

#ifdef __cplusplus
extern "C" {
#endif

/// A structure encapsulating the FidelityFX Brixelizer GI context.
///
/// This sets up an object which contains all persistent internal data and
/// resources that are required by Brixelizer GI.
///
/// The <c><i>FfxBrixelizerGIContext</i></c> object should have a lifetime matching
/// your use of Brixelizer GI. Before destroying the Brixelizer GI context care
/// should be taken to ensure the GPU is not accessing the resources created
/// or used by Brixelizer GI. It is therefore recommended that the GPU is idle
/// before destroying the Brixelizer GI context.
///
/// @ingroup ffxBrixgi
typedef struct FfxBrixelizerGIContext
{
    uint32_t data[FFX_BRIXELIZER_GI_CONTEXT_SIZE];
} FfxBrixelizerGIContext;

/// An enumeration of flag bits used when creating an <c><i>FfxBrixelizerGIContext</i></c>.
/// See <c><i>FfxBrixelizerGIContextDescription</i></c>.
///
/// @ingroup ffxBrixgi
typedef enum FfxBrixelizerGIFlags 
{
    FFX_BRIXELIZER_GI_FLAG_DEPTH_INVERTED   = (1 << 0),  ///< Indicates input resources were generated with inverted depth.
    FFX_BRIXELIZER_GI_FLAG_DISABLE_SPECULAR = (1 << 1),  ///< Disable specular GI.
    FFX_BRIXELIZER_GI_FLAG_DISABLE_DENOISER = (1 << 2),  ///< Disable denoising. Only allowed at native resolution.
    FFX_BRIXELIZER_GI_FLAG_EXTERNAL_RADIANCE_CACHE = (1 << 3),  ///< The radiance cache is the application's (radianceCache in the dispatch and debug descriptions), not created by the context.
    FFX_BRIXELIZER_GI_FLAG_CACHE_SLOTS             = (1 << 5),  ///< vkfw patch: the per-brick caches are indexed through brickSlotMap (the dispatch and debug descriptions) instead of by brick index, and the context keeps the map: after a bake of a merged cascade (updatedCascadeMask) a brick keeps the slot its voxel had, or on a moved body (movers) the one at the point its motion takes the voxel back to; the slots nobody claimed are freed, and a brick still without one takes a free slot, which is seeded from the next coarser cascade or zeroed. Requires FFX_BRIXELIZER_GI_FLAG_EXTERNAL_BRICK_CACHE, and brickSlotMap filled with 0xffffffff when it is first handed in. Without it the context uses an identity map of its own.
    FFX_BRIXELIZER_GI_FLAG_EXTERNAL_BRICK_CACHE    = (1 << 4),///< The per-brick irradiance (bricksSH, bricksDirectSH, bricksSHState in the dispatch and debug descriptions) is the application's, and so is clearing a new brick's cache: the context's clear pass does not run. Requires FFX_BRIXELIZER_GI_FLAG_EXTERNAL_RADIANCE_CACHE.
} FfxBrixelizerGIFlags;

/// An enumeration of the quality modes supported by FidelityFX Brixelizer GI.
///
/// @ingroup ffxBrixgi
typedef enum FfxBrixelizerGIInternalResolution
{
    FFX_BRIXELIZER_GI_INTERNAL_RESOLUTION_NATIVE,     ///< Output GI at native resolution.
    FFX_BRIXELIZER_GI_INTERNAL_RESOLUTION_75_PERCENT, ///< Output GI at 75% of native resolution.
    FFX_BRIXELIZER_GI_INTERNAL_RESOLUTION_50_PERCENT, ///< Output GI at 50% of native resolution.
    FFX_BRIXELIZER_GI_INTERNAL_RESOLUTION_25_PERCENT, ///< Output GI at 25% of native resolution.
} FfxBrixelizerGIInternalResolution;

/// A structure encapsulating the parameters used for creating an
/// <c><i>FfxBrixelizerGIContext</i></c>.
///
/// @ingroup ffxBrixgi
typedef struct FfxBrixelizerGIContextDescription
{
    FfxBrixelizerGIFlags              flags;              ///< A bit field representings various options.
    FfxBrixelizerGIInternalResolution internalResolution; ///< The scale at which Brixelizer GI will output GI at internally. The output will be internally upscaled to the specified displaySize.
    FfxDimensions2D                   displaySize;        ///< The size of the presentation resolution targeted by the upscaling process.
    FfxInterface                      backendInterface;   ///< An implementation of the FidelityFX backend for use with Brixelizer.
} FfxBrixelizerGIContextDescription;

/// vkfw patch: what FFX_BRIXELIZER_GI_FLAG_CACHE_SLOTS may do.
typedef enum FfxBrixelizerGICacheFlags
{
    FFX_BRIXELIZER_GI_CACHE_CARRY        = (1 << 0),  ///< A brick keeps its voxel's cache; without, every brick gets a new slot, zeroed.
    FFX_BRIXELIZER_GI_CACHE_SEED_COARSER = (1 << 1),  ///< A new brick is seeded from the next coarser cascade's brick there.
    FFX_BRIXELIZER_GI_CACHE_SEED_RADIANCE = (1 << 2), ///< The seed fills the radiance cache too.
    FFX_BRIXELIZER_GI_CACHE_MOVERS_SKIP_STATIC = (1 << 3), ///< A static brick in a mover's bounds keeps its voxel's slot: level L's static cascade is raw cascade L (ffxBrixelizerGetCascadeIndices' layout, with a static cascade at every level).
    FFX_BRIXELIZER_GI_CACHE_SEED_FINER = (1 << 4),   ///< A new brick is seeded first from the next finer cascade's bricks inside it (their SH averaged by sample count, its radiance from theirs where each texel falls), and only without any from the coarser one.
} FfxBrixelizerGICacheFlags;

/// vkfw patch: what FfxBrixelizerGIDispatchDescription::cacheFeed holds, an entry each: 88 bytes, std430, no padding.
///
/// sh is the brick cache's own form (the irradiance SH Brixelizer GI keeps per brick, FFX_BRIXELIZER_GI_FLAG_EXTERNAL_BRICK_CACHE's bricksSH):
/// 9 L2 coefficients in FfxBrixelizerGISHGetCoefficients' basis and order (Y00; Y1-1 (y), Y10 (z), Y11 (x); Y2-2 (xy), Y2-1 (yz), Y20
/// (3z^2 - 1), Y21 (xz), Y22 (x^2 - y^2)), of the radiance arriving at the brick (not convolved with a cosine), each as fp16x4 rgb plus an
/// unused w, packed two halves to a uint32 (x | y << 16, z | w << 16). Radiance in the units the GI's other inputs use (pre-exposed the same
/// way). samples is how many samples the estimate is worth: it weighs the entry against what the brick already holds.
typedef struct FfxBrixelizerGICacheFeedEntry
{
    float    worldPosition[3];
    float    samples;
    uint32_t sh[18];
} FfxBrixelizerGICacheFeedEntry;

/// vkfw patch: how a cache feed entry meets the brick's own cache.
typedef enum FfxBrixelizerGICacheFeedFlags
{
    FFX_BRIXELIZER_GI_CACHE_FEED_BLEND      = 0,         ///< Blended in by sample count (the brick's and the entry's).
    FFX_BRIXELIZER_GI_CACHE_FEED_EMPTY_ONLY = (1 << 0),  ///< Only into a brick whose irradiance has fewer samples than the entry: what the screen keeps lit is left alone.
    FFX_BRIXELIZER_GI_CACHE_FEED_DIRECT     = (1 << 1),  ///< Into the direct-light SH too (the GI's direct term, which it otherwise takes only from the screen).
} FfxBrixelizerGICacheFeedFlags;

/// vkfw patch: a body that moved since a merged cascade's last bake: a voxel of it inside the bounds was, then, at the point the transform takes it to.
typedef struct FfxBrixelizerGIMover
{
    float currentToPrevious[12];            ///< 3x4, row-major.
    float boundsMin[3];                     ///< The body's world bounds now.
    float boundsMax[3];
    uint32_t levelMask;                     ///< The levels (startCascade-relative merged cascades) whose last bake the transform is from; 0: every level.
} FfxBrixelizerGIMover;

/// A structure encapsulating the parameters used for computing a dispatch by the
/// Brixelizer GI context.
///
/// @ingroup ffxBrixgi
typedef struct FfxBrixelizerGIDispatchDescription
{
    FfxFloat32x4x4 view;                    ///< The view matrix for the scene in row major order.
    FfxFloat32x4x4 projection;              ///< The projection matrix for the scene in row major order.
    FfxFloat32x4x4 prevView;                ///< The view matrix for the previous frame of the scene in row major order.
    FfxFloat32x4x4 prevProjection;          ///< The projection matrix for the scene in row major order.

    FfxFloat32x3   cameraPosition;          ///< A 3-dimensional vector representing the position of the camera.
    FfxUInt32      startCascade;            ///< The index of the start cascade for use with ray marching with Brixelizer.
    FfxUInt32      endCascade;              ///< The index of the end cascade for use with ray marching with Brixelizer.
    FfxFloat32     rayPushoff;              ///< The distance from a surface along the normal vector to offset the diffuse ray origin.
    FfxFloat32     sdfSolveEps;             ///< The epsilon value for ray marching to be used with Brixelizer for diffuse rays.
    FfxFloat32     specularRayPushoff;      ///< The distance from a surface along the normal vector to offset the specular ray origin.
    FfxFloat32     specularSDFSolveEps;     ///< The epsilon value for ray marching to be used with Brixelizer for specular rays.
    FfxFloat32     tMin;                    ///< The TMin value for use with Brixelizer.
    FfxFloat32     tMax;                    ///< The TMax value for use with Brixelizer.

    FfxResource environmentMap;             ///< The environment map.
    FfxResource prevLitOutput;              ///< The lit output from the previous frame.
    FfxResource depth;                      ///< The input depth buffer.
    FfxResource historyDepth;               ///< The previous frame input depth buffer.
    FfxResource normal;                     ///< The input normal buffer.
    FfxResource historyNormal;              ///< The previous frame input normal buffer.
    FfxResource roughness;                  ///< The resource containing roughness information.
    FfxResource motionVectors;              ///< The input motion vectors texture.
    FfxResource noiseTexture;               ///< The input blue noise texture.
    
    FfxFloat32       normalsUnpackMul;       ///< A multiply factor to transform the normal to the space expected by Brixelizer GI.
    FfxFloat32       normalsUnpackAdd;       ///< An offset to transform the normal to the space expected by Brixelizer GI.
    FfxBoolean       isRoughnessPerceptual;  ///< A boolean to describe the space used to store roughness in the materialParameters texture. If false, we assume roughness squared was stored in the Gbuffer.
    FfxUInt32        roughnessChannel;       ///< The channel to read the roughness from the roughness texture
    FfxFloat32       roughnessThreshold;     ///< Regions with a roughness value greater than this threshold won't spawn specular rays.
    FfxFloat32       environmentMapIntensity;///< The value to scale the contribution from the environment map.
    FfxFloatCoords2D motionVectorScale;      ///< The scale factor to apply to motion vectors.

    FfxResource sdfAtlas;                   ///< The SDF Atlas resource used by Brixelizer.
    FfxResource bricksAABBs;                ///< The brick AABBs resource used by Brixelizer.
    FfxResource cascadeAABBTrees[24];       ///< The cascade AABB tree resources used by Brixelizer.
    FfxResource cascadeBrickMaps[24];       ///< The cascade brick map resources used by Brixelizer.

    FfxResource outputDiffuseGI;            ///< A texture to write the output diffuse GI calculated by Brixelizer GI.
    FfxResource outputSpecularGI;           ///< A texture to write the output specular GI calculated by Brixelizer GI.
    FfxResource radianceCache;              ///< With FFX_BRIXELIZER_GI_FLAG_EXTERNAL_RADIANCE_CACHE: the radiance cache, a Texture3D of (FFX_BRIXELIZER_STATIC_CONFIG_SDF_ATLAS_SIZE / 2)^3 R11G11B10_FLOAT.
    FfxResource bricksSH;                   ///< With FFX_BRIXELIZER_GI_FLAG_EXTERNAL_BRICK_CACHE: FFX_BRIXELIZER_MAX_BRICKS * 9 FfxUInt32x2, a brick's L2 SH irradiance (fp16x4 per coefficient).
    FfxResource bricksDirectSH;             ///< With FFX_BRIXELIZER_GI_FLAG_EXTERNAL_BRICK_CACHE: FFX_BRIXELIZER_MAX_BRICKS * 9 FfxUInt32x2, the direct part of the same.
    FfxResource bricksSHState;              ///< With FFX_BRIXELIZER_GI_FLAG_EXTERNAL_BRICK_CACHE: FFX_BRIXELIZER_MAX_BRICKS FfxUInt32x4.
    FfxResource brickSlotMap;               ///< With FFX_BRIXELIZER_GI_FLAG_CACHE_SLOTS: FFX_BRIXELIZER_MAX_BRICKS FfxUInt32, the slot (an index into the three caches above and the radiance cache's atlas, as a brick's own index would be) a brick's cache is in, or 0xffffffff for none. Written by the context.
    FfxResource slotCounters;               ///< With FFX_BRIXELIZER_GI_FLAG_CACHE_SLOTS: 64 FfxUInt32, zeroed by the application before each dispatch with an updatedCascadeMask; the context counts into it (the first 32 are statistics: see ffx_brixelizergi_cache_slots.h).

    /// vkfw patch: optional, a R16G16B16A16_FLOAT UAV of ffxBrixelizerGIGetInternalSize() whose .x receives the sky visibility the screen probes'
    /// rays saw, as 0 to 1 (1 = open sky) for the surface under each pixel; .w is 1. Leave it null (its FfxResource zero-initialised) to skip the
    /// work. Taken from the rays the GI traces anyway: a ray that left the scene without a hit counts as sky, so it knows nothing of what the
    /// TLAS or a celestial body would add, and the rays only reach tMax. Written at the GI's internal resolution (not upsampled).
    FfxResource skyVisibility;
    float       worldAORadius;              ///< vkfw patch: the reach of the world AO the skyVisibility target holds in .y (1 m if 0).

    /// vkfw patch: the application's own estimate of the world cache, for bricks the screen never shows (probes, ray tracing): a buffer of
    /// FfxBrixelizerGICacheFeedEntry, read by the GI's cache feed pass after its slot passes and before it shades. Each entry lands in the
    /// brick at its world position in the finest cascade (of startCascade..endCascade) that has one there; an entry outside every brick
    /// is dropped. cacheFeedCount holds the number of entries in its first uint32 (written on the GPU or the CPU), clamped to
    /// maxCacheFeedEntries, which sizes the dispatch; leave cacheFeed null (or maxCacheFeedEntries 0) for none.
    FfxResource cacheFeed;
    FfxResource cacheFeedCount;
    uint32_t    maxCacheFeedEntries;
    uint32_t    cacheFeedFlags;             ///< FfxBrixelizerGICacheFeedFlags
    float       cacheFeedMaxSamples;        ///< The sample count a brick's irradiance stops at when it is fed (16 if 0): what the screen adds later still counts.

    /// vkfw patch, with FFX_BRIXELIZER_GI_FLAG_CACHE_SLOTS: what the bake before this dispatch did to the merged cascades. Bit i is merged cascade
    /// startCascade + i, rebuilt by that bake (normally one a frame, or none).
    uint32_t updatedCascadeMask;
    uint32_t cacheFlags;                    ///< FfxBrixelizerGICacheFlags
    float    seedSamples;                   ///< The sample count a seeded brick's irradiance is given, so its own samples soon outweigh it (2).
    float    seedMinSamples;                ///< The samples a coarser brick needs before a new one is seeded from it (4).
    const FfxBrixelizerGIMover* movers;     ///< Bodies that moved since the updated cascades' last bakes, at most FFX_BRIXELIZER_GI_MAX_SLOT_MOVERS a level.
    uint32_t numMovers;

    FfxBrixelizerRawContext *brixelizerContext; ///< A pointer to the Brixelizer context for use with Brixelizer GI.
} FfxBrixelizerGIDispatchDescription;

/// An enumeration of which output mode to be used by Brixelizer GI debug visualization.
/// See <c><i>FfxBrixelizerGIDebugDescription</i></c>.
///
/// @ingroup ffxBrixgi
typedef enum FfxBrixelizerGIDebugMode
{
    FFX_BRIXELIZER_GI_DEBUG_MODE_RADIANCE_CACHE,   ///< Draw the radiance cache.
    FFX_BRIXELIZER_GI_DEBUG_MODE_IRRADIANCE_CACHE, ///< Draw the irradiance cache.
} FfxBrixelizerGIDebugMode;

/// A structure encapsulating the parameters for drawing a debug visualization.
///
/// @ingroup ffxBrixgi
typedef struct FfxBrixelizerGIDebugDescription
{
    FfxFloat32x4x4           view;              ///< The view matrix for the scene in row major order.
    FfxFloat32x4x4           projection;        ///< The projection matrix for the scene in row major order.
    FfxUInt32                startCascade;      ///< The index of the start cascade for use with ray marching with Brixelizer.
    FfxUInt32                endCascade;        ///< The index of the end cascade for use with ray marching with Brixelizer.
    FfxUInt32                outputSize[2];     ///< The dimensions of the output texture.
    FfxBrixelizerGIDebugMode debugMode;         ///< The mode for the debug visualization. See <c><i>FfxBrixelizerGIDebugMode</i></c>.
    FfxFloat32               normalsUnpackMul;  ///< A multiply factor to transform the normal to the space expected by Brixelizer GI.
    FfxFloat32               normalsUnpackAdd;  ///< An offset to transform the normal to the space expected by Brixelizer GI.

    FfxResource depth;                   ///< The input depth buffer.
    FfxResource normal;                  ///< The input normal buffer.

    FfxResource sdfAtlas;                ///< The SDF Atlas resource used by Brixelizer.
    FfxResource bricksAABBs;             ///< The brick AABBs resource used by Brixelizer.
    FfxResource cascadeAABBTrees[24];    ///< The cascade AABB tree resources used by Brixelizer.
    FfxResource cascadeBrickMaps[24];    ///< The cascade brick map resources used by Brixelizer.

    FfxResource outputDebug;             ///< The output texture for the debug visualization.
    FfxResource radianceCache;           ///< With FFX_BRIXELIZER_GI_FLAG_EXTERNAL_RADIANCE_CACHE: the radiance cache, as in FfxBrixelizerGIDispatchDescription.
    FfxResource bricksSH;                   ///< With FFX_BRIXELIZER_GI_FLAG_EXTERNAL_BRICK_CACHE: FFX_BRIXELIZER_MAX_BRICKS * 9 FfxUInt32x2, a brick's L2 SH irradiance (fp16x4 per coefficient).
    FfxResource bricksDirectSH;             ///< With FFX_BRIXELIZER_GI_FLAG_EXTERNAL_BRICK_CACHE: FFX_BRIXELIZER_MAX_BRICKS * 9 FfxUInt32x2, the direct part of the same.
    FfxResource bricksSHState;              ///< With FFX_BRIXELIZER_GI_FLAG_EXTERNAL_BRICK_CACHE: FFX_BRIXELIZER_MAX_BRICKS FfxUInt32x4.
    FfxResource brickSlotMap;               ///< With FFX_BRIXELIZER_GI_FLAG_CACHE_SLOTS: FFX_BRIXELIZER_MAX_BRICKS FfxUInt32, the slot (an index into the three caches above and the radiance cache's atlas, as a brick's own index would be) a brick's cache is in, or 0xffffffff for none. Written by the context.
    FfxResource slotCounters;               ///< With FFX_BRIXELIZER_GI_FLAG_CACHE_SLOTS: 64 FfxUInt32, zeroed by the application before each dispatch with an updatedCascadeMask; the context counts into it (the first 32 are statistics: see ffx_brixelizergi_cache_slots.h).

    FfxBrixelizerRawContext* brixelizerContext; ///< A pointer to the Brixelizer context for use with Brixelizer GI.
} FfxBrixelizerGIDebugDescription;

/// An enumeration of all the passes which constitute the Brixelizer algorithm.
///
/// Brixelizer GI is implemented as a composite of several compute passes each
/// computing a key part of the final result. Each call to the
/// <c><i>FfxBrixelizerScheduleGpuJobFunc</i></c> callback function will
/// correspond to a single pass included in <c><i>FfxBrixelizerPass</i></c>. For a
/// more comprehensive description of each pass, please refer to the Brixelizer
/// reference documentation.
///
/// @ingroup ffxBrixgi
typedef enum FfxBrixelizerGIPass
{
    FFX_BRIXELIZER_GI_PASS_BLUR_X,
    FFX_BRIXELIZER_GI_PASS_BLUR_Y,
    FFX_BRIXELIZER_GI_PASS_CLEAR_CACHE,
    FFX_BRIXELIZER_GI_PASS_EMIT_IRRADIANCE_CACHE,
    FFX_BRIXELIZER_GI_PASS_EMIT_PRIMARY_RAY_RADIANCE,
    FFX_BRIXELIZER_GI_PASS_FILL_SCREEN_PROBES,
    FFX_BRIXELIZER_GI_PASS_INTERPOLATE_SCREEN_PROBES,
    FFX_BRIXELIZER_GI_PASS_PREPARE_CLEAR_CACHE,
    FFX_BRIXELIZER_GI_PASS_PROJECT_SCREEN_PROBES,
    FFX_BRIXELIZER_GI_PASS_PROPAGATE_SH,
    FFX_BRIXELIZER_GI_PASS_SLOTS_CLAIM_SAME,    ///< vkfw patch: the passes of FFX_BRIXELIZER_GI_FLAG_CACHE_SLOTS
    FFX_BRIXELIZER_GI_PASS_SLOTS_CLAIM_MOVER,
    FFX_BRIXELIZER_GI_PASS_SLOTS_FREE,
    FFX_BRIXELIZER_GI_PASS_SLOTS_ALLOC,
    FFX_BRIXELIZER_GI_PASS_SLOTS_APPLY,
    FFX_BRIXELIZER_GI_PASS_SLOTS_SEED,
    FFX_BRIXELIZER_GI_PASS_SLOTS_STATS,
    FFX_BRIXELIZER_GI_PASS_SLOTS_RESAMPLE,      ///< vkfw patch: a moved brick's caches turned and shifted with its body's motion
    FFX_BRIXELIZER_GI_PASS_CACHE_FEED,          ///< vkfw patch: the application's cache feed (FfxBrixelizerGIDispatchDescription::cacheFeed)
FFX_BRIXELIZER_GI_PASS_REPROJECT_GI,
    FFX_BRIXELIZER_GI_PASS_REPROJECT_SCREEN_PROBES,
    FFX_BRIXELIZER_GI_PASS_SPAWN_SCREEN_PROBES,
    FFX_BRIXELIZER_GI_PASS_SPECULAR_PRE_TRACE,
    FFX_BRIXELIZER_GI_PASS_SPECULAR_TRACE,
    FFX_BRIXELIZER_GI_PASS_DEBUG_VISUALIZATION,
    FFX_BRIXELIZER_GI_PASS_GENERATE_DISOCCLUSION_MASK,
    FFX_BRIXELIZER_GI_PASS_DOWNSAMPLE,
    FFX_BRIXELIZER_GI_PASS_UPSAMPLE,

    FFX_BRIXELIZER_GI_PASS_COUNT  ///< The number of passes performed by Brixelizer GI.
} FfxBrixelizerGIPass;

/// Get the size in bytes needed for an <c><i>FfxBrixelizerGIContext</i></c> struct.
/// Note that this function is provided for consistency, and the size of the
/// <c><i>FfxBrixelizerGIContext</i></c> is a known compile time value which can be
/// obtained using <c><i>sizeof(FfxBrixelizerGIContext)</i></c>.
///
/// @return  The size in bytes of an <c><i>FfxBrixelizerGIContext</i></c> struct.
///
/// @ingroup ffxBrixgi
inline size_t ffxBrixelizerGIGetContextSize()
{
    return sizeof(FfxBrixelizerGIContext);
}

/// Create a FidelityFX Brixelizer GI context from the parameters
/// specified to the <c><i>FfxBrixelizerGIContextDescription</i></c> struct.
///
/// The context structure is the main object used to interact with the Brixelizer GI API,
/// and is responsible for the management of the internal resources used by the
/// Brixelizer GI algorithm. When this API is called, multiple calls will be made via
/// the pointers contained in the <b><i>backendInterface</i></b> structure. This
/// backend will attempt to retrieve the device capabilities, and create the internal
/// resources, and pipelines required by Brixelizer GI.
///
/// Depending on the parameters passed in via the <b><i>pContextDescription</b></i> a
/// different set of resources and pipelines may be requested by the callback functions.
///
/// The <c><i>FfxBrixelizerGIContext</i></c> should be destroyed when use of it is completed.
/// To destroy the context you should call <c><i>ffxBrixelizerGIContextDestroy</i></c>.
///
/// @param [out] pContext               A pointer to a <c><i>FfxBrixelizerGIContext</i></c> to populate.
/// @param [in]  pContextDescription    A pointer to a <c><i>FfxBrixelizerGIContextDescription</i></c> specifying the parameters for context creation.
///
/// @retval
/// FFX_OK                             The operation completed successfully.
/// @retval
/// FFX_ERROR_INVALID_POINTER          The operation failed because either <c><i>pContext</i></c> or <c><i>pContextDescription</i></c> was <c><i>NULL</i></c>.
/// @retval
/// FFX_ERROR_INCOMPLETE_INTERFACE     The operation failed because <c><i>pContextDescription->backendInterface</i></c> was not fully specified.
/// @retval
/// FFX_ERROR_BACKEND_API_ERROR        The operation failed because of an error from the backend.
///
/// @ingroup ffxBrixgi
FFX_API FfxErrorCode ffxBrixelizerGIContextCreate(FfxBrixelizerGIContext* pContext, const FfxBrixelizerGIContextDescription* pContextDescription);

/// Destroy the FidelityFX Brixelizer GI context.
///
/// @param [out] pContext        A pointer to a <c><i>FfxBrixelizerGIContext</i></c> structure to destroy.
///
/// @retval
/// FFX_OK                      The operation completed successfully.
/// @retval
/// FFX_ERROR_INVALID_POINTER   The <c><i>pContext</i></c> pointer provided was <c><i>NULL</i></c>.
///
/// @ingroup ffxBrixgi
FFX_API FfxErrorCode ffxBrixelizerGIContextDestroy(FfxBrixelizerGIContext* pContext);

/// vkfw patch: the size the GI's internal targets (FfxBrixelizerGIDispatchDescription::skyVisibility) must have: the display size times the
/// internal resolution's scale.
///
/// @param [in]  pContext   An <c><i>FfxBrixelizerGIContext</i></c> containing the Brixelizer GI context.
/// @param [out] pSize      The width and height.
///
/// @ingroup ffxBrixgi
FFX_API FfxErrorCode ffxBrixelizerGIGetInternalSize(const FfxBrixelizerGIContext* pContext, FfxDimensions2D* pSize);

/// Perform an update of Brixelizer GI, recording GPU commands to a command list.
///
/// @param [inout] context              An <c><i>FfxBrixelizerGIContext</i></c> containing the Brixelizer GI context.
/// @param [in]    pDispatchDescription A pointer to a <c><i>FfxBrixelizerGIDispatchDescription</i></c> describing the dispatch to compute.
/// @param [inout] pCommandList         An <c><i>FfxCommandList</i></c> to write GPU commands to.
///
/// @retval
/// FFX_OK                        The operation completed successfully.
///
/// @ingroup ffxBrixgi
FFX_API FfxErrorCode ffxBrixelizerGIContextDispatch(FfxBrixelizerGIContext* pContext, const FfxBrixelizerGIDispatchDescription* pDispatchDescription, FfxCommandList pCommandList);

/// Make a debug visualization from the <c><i>FfxBrixelizerGIContext</i></c>.
///
/// @param [inout] context              An <c><i>FfxBrixelizerGIContext</i></c> containing the Brixelizer GI context.
/// @param [in]    pDebugDescription    A pointer to a <c><i>FfxBrixelizerGIDebugDescription</i></c> describing the debug visualization to draw.
/// @param [inout] pCommandList         An <c><i>FfxCommandList</i></c> to write GPU commands to.
///
/// @retval
/// FFX_OK                        The operation completed successfully.
///
/// @ingroup ffxBrixgi
FFX_API FfxErrorCode ffxBrixelizerGIContextDebugVisualization(FfxBrixelizerGIContext* pContext, const FfxBrixelizerGIDebugDescription* pDebugDescription, FfxCommandList pCommandList);

/// Queries the effect version number.
///
/// @returns
/// The SDK version the effect was built with.
///
/// @ingroup ffxBrixgi
FFX_API FfxVersionNumber ffxBrixelizerGIGetEffectVersion();

#ifdef __cplusplus
}
#endif
