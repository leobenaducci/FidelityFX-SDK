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

#include <FidelityFX/host/ffx_brixelizer.h>

#include <float.h> // FLT_MIN, FLT_MAX
#include <string.h> // memset
#include <math.h> // floorf
#include <stdbool.h>
#include <stdlib.h> // malloc

#define ifor(n) for (uint32_t i = 0; i < n; ++i)
#define jfor(n) for (uint32_t j = 0; j < n; ++j)

#define RETURN_ON_FAIL(func) \
    do { \
        FfxErrorCode errorCode = func; \
        if (errorCode != FFX_OK) { \
            return errorCode; \
        } \
    } while(0)

static bool aabbsOverlap(FfxBrixelizerAABB x, FfxBrixelizerAABB y)
{
    ifor (3) {
        if (x.min[i] > y.max[i]) { return false; }
        if (y.min[i] > x.max[i]) { return false; }
    }
    return true;
}

typedef struct FfxBrixelizerBakedUpdateDescription_Private {
    FfxBrixelizerResources                      resources;
    FfxBrixelizerRawCascadeUpdateDescription    cascadeUpdateDesc;
    FfxBrixelizerPopulateDebugAABBsFlags        populateDebugAABBsFlags;
    FfxBrixelizerStats*                         outStats;
    FfxBrixelizerDebugVisualizationDescription* debugVisualizationDesc;
    uint32_t                            numStaticJobs;
    FfxBrixelizerRawJobDescription                 staticJobs[3 * FFX_BRIXELIZER_MAX_INSTANCES];
    uint32_t                            numDynamicJobs;
    FfxBrixelizerRawJobDescription                 dynamicJobs[FFX_BRIXELIZER_MAX_INSTANCES];
    // vkfw patch: the mover cascade's jobs: its instances and its invalidations.
    uint32_t                            numMoverJobs;
    FfxBrixelizerRawJobDescription                 moverJobs[2 * FFX_BRIXELIZER_MAX_INSTANCES];
    // vkfw patch: FfxBrixelizerUpdateDescription::extraLevelMask / extraStaticLevelMask, as baked (without the scheduled level).
    uint32_t                            extraLevelMask;
    uint32_t                            extraStaticLevelMask;
} FfxBrixelizerBakedUpdateDescription_Private;

FFX_STATIC_ASSERT(sizeof(FfxBrixelizerBakedUpdateDescription) == sizeof(FfxBrixelizerBakedUpdateDescription_Private));

typedef struct FfxBrixelizerCascadePrivate {
    FfxBrixelizerCascadeFlag flags;
    float                    voxelSize;
    uint32_t                 staticIndex;
    uint32_t                 dynamicIndex;
    uint32_t                 mergedIndex;
    uint32_t                 moverIndex;    // vkfw patch
    // vkfw patch: where the level's last scheduled update placed it, which its extra updates (extraLevelMask) keep.
    uint32_t                 placed;
    int32_t                  placedClipmapOffset[3];
    float                    placedCascadeMin[3];
} FfxBrixelizerCascadePrivate;

typedef struct FfxBrixelizerInvalidation {
    uint32_t          cascades;
    FfxBrixelizerAABB aabb;
} FfxBrixelizerInvalidation;

typedef struct FfxBrixelizerInstance {
    FfxBrixelizerInstanceID id;
    FfxBrixelizerAABB       aabb;
    // vkfw patch: the logical cascade band, inclusive, resolved and clamped once at creation so the
    // job loops never have to look at the flag again. Without a band this is the whole context,
    // which is what every instance used to get.
    uint32_t                minCascade;
    uint32_t                maxCascade;
    // vkfw patch: FFX_BRIXELIZER_INSTANCE_FLAG_MOVER: baked into the mover cascades (the dynamic one at a level without), never the static.
    uint32_t                isMover;
} FfxBrixelizerInstance;

// vkfw patch: whether an instance is submitted to this logical cascade at all.
static bool instanceInCascade(const FfxBrixelizerInstance *instance, uint32_t cascadeIndex)
{
    return cascadeIndex >= instance->minCascade && cascadeIndex <= instance->maxCascade;
}

// vkfw patch: the source cascades of a level.
enum FfxBrixelizerLevelSource {
    FFX_BRIXELIZER_LEVEL_SOURCE_STATIC,
    FFX_BRIXELIZER_LEVEL_SOURCE_MOVER,
    FFX_BRIXELIZER_LEVEL_SOURCE_DYNAMIC,
};

typedef struct FfxBrixelizerScratchSpace {
    union {
        struct {
            FfxBrixelizerRawInstanceDescription rawInstanceDescs[FFX_BRIXELIZER_MAX_INSTANCES];
            FfxBrixelizerInstanceID             instanceIDs[FFX_BRIXELIZER_MAX_INSTANCES];
        } createInstances;
        struct {
            FfxBrixelizerInstanceID instanceIDs[FFX_BRIXELIZER_MAX_INSTANCES];
        } update;
    };
} FfxBrixelizerScratchSpace;

typedef struct FfxBrixelizerContext_Private {
    FfxBrixelizerRawContext     context;
    uint32_t                    numCascades;
    FfxBrixelizerCascadePrivate cascades[FFX_BRIXELIZER_MAX_CASCADES];
    uint32_t                    numInvalidations;
    FfxBrixelizerInvalidation   invalidations[FFX_BRIXELIZER_MAX_INSTANCES];
    uint32_t                    numStaticInstances;
    uint32_t                    dynamicInstanceStartIndex;
    uint32_t                    instanceIndices[FFX_BRIXELIZER_MAX_INSTANCES];
    FfxBrixelizerInstance       instances[FFX_BRIXELIZER_MAX_INSTANCES];
    FfxBrixelizerScratchSpace   scratchSpace;
    // vkfw patch: the jobs of one cascade of an extra level (extraLevelMask), built as each is updated (the raw update copies them out).
    FfxBrixelizerRawJobDescription* levelJobs;
    // vkfw patch: the movers' records (FfxBrixelizerMoverRecord), the record of each instance id (FFX_BRIXELIZER_NO_MOVER_RECORD if none),
    // the free records, and the remaps a level's rebuild hands the raw context.
    struct FfxBrixelizerMoverRecord* moverRecords;
    uint32_t*                        moverRecordOfInstance;
    uint32_t*                        freeMoverRecords;
    uint32_t                         numFreeMoverRecords;
    FfxBrixelizerRawMoverRemap*      remapScratch;
} FfxBrixelizerContext_Private;

// vkfw patch: what a mover's transform was the last time each level rebuilt it, so that the next rebuild can tell the caches how far it moved.
#define FFX_BRIXELIZER_MAX_MOVER_RECORDS 4096
#define FFX_BRIXELIZER_NO_MOVER_RECORD   0xffffffffu
typedef struct FfxBrixelizerMoverRecord {
    FfxFloat32x3x4    current;
    FfxBrixelizerAABB currentAABB;
    FfxFloat32x3x4    baked[FFX_BRIXELIZER_MAX_CASCADES];
    uint32_t          bakedLevelMask;
    FfxBrixelizerInstanceID instanceID;
} FfxBrixelizerMoverRecord;

// vkfw patch: the capacity of levelJobs: every instance, and every invalidation.
#define FFX_BRIXELIZER_LEVEL_JOBS_CAPACITY (2 * FFX_BRIXELIZER_MAX_INSTANCES)

FFX_STATIC_ASSERT(sizeof(FfxBrixelizerContext) >= sizeof(FfxBrixelizerContext_Private));

// vkfw patch: a * b for 3x4 row-major affine transforms (the missing row 0 0 0 1).
static void affineMultiply(const float* a, const float* b, float* out)
{
    for (uint32_t r = 0; r < 3; ++r) {
        for (uint32_t c = 0; c < 4; ++c) {
            float v = a[r * 4 + 0] * b[0 * 4 + c] + a[r * 4 + 1] * b[1 * 4 + c] + a[r * 4 + 2] * b[2 * 4 + c];
            if (c == 3) v += a[r * 4 + 3];
            out[r * 4 + c] = v;
        }
    }
}

// vkfw patch: the inverse of a 3x4 row-major affine transform; false if it is singular.
static bool affineInverse(const float* m, float* out)
{
    const float a = m[0], b = m[1], c = m[2], d = m[4], e = m[5], f = m[6], g = m[8], h = m[9], k = m[10];
    const float det = a * (e * k - f * h) - b * (d * k - f * g) + c * (d * h - e * g);
    if (fabsf(det) < 1.0e-12f) return false;
    const float inv = 1.0f / det;
    const float r[9] = {
        (e * k - f * h) * inv, (c * h - b * k) * inv, (b * f - c * e) * inv,
        (f * g - d * k) * inv, (a * k - c * g) * inv, (c * d - a * f) * inv,
        (d * h - e * g) * inv, (b * g - a * h) * inv, (a * e - b * d) * inv,
    };
    for (uint32_t row = 0; row < 3; ++row) {
        out[row * 4 + 0] = r[row * 3 + 0];
        out[row * 4 + 1] = r[row * 3 + 1];
        out[row * 4 + 2] = r[row * 3 + 2];
        out[row * 4 + 3] = -(r[row * 3 + 0] * m[3] + r[row * 3 + 1] * m[7] + r[row * 3 + 2] * m[11]);
    }
    return true;
}

// vkfw patch: a level's rebuild, for the caches: the merged cascade rebuilt, and the move since the level's last rebuild of each mover in it.
static void recordLevelCacheUpdate(FfxBrixelizerContext_Private* context, uint32_t level)
{
    uint32_t numRemaps = 0;
    for (uint32_t r = 0; r < FFX_BRIXELIZER_MAX_MOVER_RECORDS; ++r) {
        FfxBrixelizerMoverRecord *record = &context->moverRecords[r];
        if (record->instanceID == FFX_BRIXELIZER_NO_MOVER_RECORD) continue;
        const FfxBrixelizerInstance *instance = &context->instances[context->instanceIndices[record->instanceID]];
        if (!instanceInCascade(instance, level)) continue;
        const float *current = (const float*)&record->current;
        float *baked = (float*)&record->baked[level];
        if ((record->bakedLevelMask & (1u << level)) && memcmp(current, baked, 12 * sizeof(float)) != 0) {
            float currentInverse[12];
            if (affineInverse(current, currentInverse) && numRemaps < FFX_BRIXELIZER_MAX_MOVER_RECORDS) {
                FfxBrixelizerRawMoverRemap *remap = &context->remapScratch[numRemaps++];
                affineMultiply(baked, currentInverse, remap->currentToPrevious);
                memcpy(remap->boundsMin, record->currentAABB.min, sizeof(remap->boundsMin));
                memcpy(remap->boundsMax, record->currentAABB.max, sizeof(remap->boundsMax));
                remap->cascadeIndex = 0;
            }
        }
        memcpy(baked, current, 12 * sizeof(float));
        record->bakedLevelMask |= 1u << level;
    }
    ffxBrixelizerRawContextAddCacheUpdate(&context->context, context->cascades[level].mergedIndex, context->remapScratch, numRemaps);
}

// vkfw patch: the jobs of one source cascade of a level, for an extra update (extraLevelMask), as the scheduled update's are made in
// ffxBrixelizerBakeUpdate: the static cascade's instances and, consumed if consume is set, its pending invalidations; the mover cascade's
// instances; the dynamic cascade's, and the movers at a level without a mover cascade.
static uint32_t buildLevelJobs(FfxBrixelizerContext_Private* context, uint32_t level, FfxBrixelizerLevelSource source, FfxBrixelizerAABB cascadeAABB,
                               FfxBrixelizerRawJobDescription* jobs, bool consume)
{
    const FfxBrixelizerCascadeFlag flags = context->cascades[level].flags;
    uint32_t numJobs = 0;
    const auto addInstance = [&](const FfxBrixelizerInstance *instance) {
        if (numJobs >= FFX_BRIXELIZER_LEVEL_JOBS_CAPACITY) return;
        FfxBrixelizerRawJobDescription job = {};
        ifor (3) {
            job.aabbMin[i] = instance->aabb.min[i];
            job.aabbMax[i] = instance->aabb.max[i];
        }
        job.instanceIdx = instance->id;
        jobs[numJobs++] = job;
    };

    switch (source) {
    case FFX_BRIXELIZER_LEVEL_SOURCE_STATIC: {
        ifor (context->numStaticInstances) {
            const FfxBrixelizerInstance *instance = &context->instances[i];
            if (!instance->isMover && instanceInCascade(instance, level) && aabbsOverlap(instance->aabb, cascadeAABB))
                addInstance(instance);
        }
        const uint32_t cascadeMask = 1u << level;
        uint32_t curInvalidation = 0;
        while (curInvalidation < context->numInvalidations) {
            FfxBrixelizerInvalidation *invalidation = &context->invalidations[curInvalidation];
            if (invalidation->cascades & cascadeMask) {
                if (aabbsOverlap(invalidation->aabb, cascadeAABB) && numJobs < FFX_BRIXELIZER_LEVEL_JOBS_CAPACITY) {
                    FfxBrixelizerRawJobDescription job = {};
                    ifor (3) {
                        job.aabbMin[i] = invalidation->aabb.min[i];
                        job.aabbMax[i] = invalidation->aabb.max[i];
                    }
                    job.flags = FFX_BRIXELIZER_RAW_JOB_FLAG_INVALIDATE;
                    jobs[numJobs++] = job;
                }
                if (consume) {
                    invalidation->cascades &= ~cascadeMask;
                    if (!invalidation->cascades) {
                        context->invalidations[curInvalidation] = context->invalidations[--context->numInvalidations];
                        continue;
                    }
                }
            }
            ++curInvalidation;
        }
        break;
    }
    case FFX_BRIXELIZER_LEVEL_SOURCE_MOVER:
        ifor (context->numStaticInstances) {
            const FfxBrixelizerInstance *instance = &context->instances[i];
            if (instance->isMover && instanceInCascade(instance, level) && aabbsOverlap(instance->aabb, cascadeAABB))
                addInstance(instance);
        }
        break;
    case FFX_BRIXELIZER_LEVEL_SOURCE_DYNAMIC:
        for (uint32_t i = context->dynamicInstanceStartIndex; i < FFX_ARRAY_ELEMENTS(context->instances); ++i) {
            if (instanceInCascade(&context->instances[i], level))
                addInstance(&context->instances[i]);
        }
        if (!(flags & FFX_BRIXELIZER_CASCADE_MOVER)) {
            ifor (context->numStaticInstances) {
                const FfxBrixelizerInstance *instance = &context->instances[i];
                if (instance->isMover && instanceInCascade(instance, level) && aabbsOverlap(instance->aabb, cascadeAABB))
                    addInstance(instance);
            }
        }
        break;
    }
    return numJobs;
}

// vkfw patch: an extra level's placement, as its last scheduled update left it.
static FfxBrixelizerAABB placedCascadeAABB(const FfxBrixelizerCascadePrivate* cascade)
{
    FfxBrixelizerAABB aabb = {};
    ifor (3) {
        aabb.min[i] = cascade->placedCascadeMin[i];
        aabb.max[i] = aabb.min[i] + cascade->voxelSize * (float)FFX_BRIXELIZER_CASCADE_RESOLUTION;
    }
    return aabb;
}

// vkfw patch: the cascade updates one ffxBrixelizerUpdate may make. The VK backend keeps a ring of descriptor sets a pipeline a frame
// (PIPELINE_DESCRIPTOR_SETS_PER_FRAME, 24, in ffx_vk.cpp) and reuses one still in flight past that; every cascade update dispatches each
// of the cascade passes once, and every merged level the AABB tree pass three times (eight levels at most: 24).
#define FFX_BRIXELIZER_MAX_CASCADE_UPDATES_PER_UPDATE 24

static uint32_t countSources(FfxBrixelizerCascadeFlag flags)
{
    return ((flags & FFX_BRIXELIZER_CASCADE_STATIC) ? 1u : 0u) + ((flags & FFX_BRIXELIZER_CASCADE_MOVER) ? 1u : 0u) + ((flags & FFX_BRIXELIZER_CASCADE_DYNAMIC) ? 1u : 0u);
}

// vkfw patch: the extra levels (and of them the static ones) this update can afford: their mover and dynamic cascades first, every level
// that asks, then their static cascades, finest first, while FFX_BRIXELIZER_MAX_CASCADE_UPDATES_PER_UPDATE allows.
static void extraLevels(const FfxBrixelizerContext_Private* context, uint32_t mask, uint32_t staticMask, uint32_t scheduledLevel,
                        uint32_t* outLevels, uint32_t* outStaticLevels)
{
    uint32_t budget = FFX_BRIXELIZER_MAX_CASCADE_UPDATES_PER_UPDATE - countSources(context->cascades[scheduledLevel].flags);
    uint32_t levels = 0, staticLevels = 0;
    ifor (context->numCascades) {
        const FfxBrixelizerCascadePrivate *level = &context->cascades[i];
        if (!(mask & (1u << i)) || i == scheduledLevel || !level->placed)
            continue;
        const uint32_t cost = countSources(FfxBrixelizerCascadeFlag(level->flags & (FFX_BRIXELIZER_CASCADE_MOVER | FFX_BRIXELIZER_CASCADE_DYNAMIC)));
        if (cost > budget)
            continue;
        budget -= cost;
        levels |= 1u << i;
    }
    ifor (context->numCascades) {
        const FfxBrixelizerCascadePrivate *level = &context->cascades[i];
        if (!(staticMask & mask & (1u << i)) || i == scheduledLevel || !level->placed || !(level->flags & FFX_BRIXELIZER_CASCADE_STATIC) || budget == 0)
            continue;
        --budget;
        levels |= 1u << i;
        staticLevels |= 1u << i;
    }
    *outLevels = levels;
    *outStaticLevels = staticLevels;
}

FfxErrorCode ffxBrixelizerContextCreate(const FfxBrixelizerContextDescription* desc, FfxBrixelizerContext* uncastOutContext)
{
    FfxBrixelizerContext_Private *outContext = (FfxBrixelizerContext_Private*)uncastOutContext;

    FfxBrixelizerRawContextDescription rawDesc = {};
    rawDesc.maxDebugAABBs    = 2048;
    rawDesc.flags            = desc->flags;
    rawDesc.backendInterface = desc->backendInterface;

    memset(outContext, 0, sizeof(*outContext));
    outContext->dynamicInstanceStartIndex = FFX_ARRAY_ELEMENTS(outContext->instances);
    RETURN_ON_FAIL(ffxBrixelizerRawContextCreate(&outContext->context, &rawDesc));

    // vkfw patch: [static][mover][dynamic][merged], each in level order, and a merged cascade for every level (ffxBrixelizerGetCascadeIndices).
    const uint32_t sourceFlags = FFX_BRIXELIZER_CASCADE_STATIC | FFX_BRIXELIZER_CASCADE_MOVER | FFX_BRIXELIZER_CASCADE_DYNAMIC;
    uint32_t numStatic = 0, numMover = 0, numDynamic = 0;
    ifor (desc->numCascades) {
        FfxBrixelizerCascadeFlag cascadeFlags = desc->cascadeDescs[i].flags;
        if (!(cascadeFlags & sourceFlags)) { return FFX_ERROR_INVALID_ARGUMENT; }
        numStatic  += (cascadeFlags & FFX_BRIXELIZER_CASCADE_STATIC) ? 1 : 0;
        numMover   += (cascadeFlags & FFX_BRIXELIZER_CASCADE_MOVER) ? 1 : 0;
        numDynamic += (cascadeFlags & FFX_BRIXELIZER_CASCADE_DYNAMIC) ? 1 : 0;
    }
    if (numStatic + numMover + numDynamic + desc->numCascades > FFX_BRIXELIZER_MAX_CASCADES) {
        return FFX_ERROR_INVALID_ARGUMENT;
    }

    uint32_t staticCascadeIndex  = 0;
    uint32_t moverCascadeIndex   = numStatic;
    uint32_t dynamicCascadeIndex = numStatic + numMover;
    uint32_t mergedCascadeIndex  = numStatic + numMover + numDynamic;
    ifor (desc->numCascades) {
        const FfxBrixelizerCascadeDescription *cascadeDesc = &desc->cascadeDescs[i];

        FfxBrixelizerCascadePrivate *cascadePrivate = &outContext->cascades[i];
        cascadePrivate->flags = cascadeDesc->flags;
        cascadePrivate->voxelSize = cascadeDesc->voxelSize;
        cascadePrivate->staticIndex  = (cascadeDesc->flags & FFX_BRIXELIZER_CASCADE_STATIC) ? staticCascadeIndex++ : (uint32_t)-1;
        cascadePrivate->moverIndex   = (cascadeDesc->flags & FFX_BRIXELIZER_CASCADE_MOVER) ? moverCascadeIndex++ : (uint32_t)-1;
        cascadePrivate->dynamicIndex = (cascadeDesc->flags & FFX_BRIXELIZER_CASCADE_DYNAMIC) ? dynamicCascadeIndex++ : (uint32_t)-1;
        cascadePrivate->mergedIndex  = mergedCascadeIndex++;

        FfxBrixelizerRawCascadeDescription rawCascadeDesc = {};
        rawCascadeDesc.brickSize = cascadeDesc->voxelSize;
        ifor(3) {
            rawCascadeDesc.cascadeMin[i] = (floorf(desc->sdfCenter[i] / cascadeDesc->voxelSize) - (0.5f * (float)FFX_BRIXELIZER_CASCADE_RESOLUTION)) * cascadePrivate->voxelSize;
        }

        const uint32_t indices[] = { cascadePrivate->staticIndex, cascadePrivate->moverIndex, cascadePrivate->dynamicIndex, cascadePrivate->mergedIndex };
        for (uint32_t index : indices) {
            if (index == (uint32_t)-1) continue;
            rawCascadeDesc.index = index;
            RETURN_ON_FAIL(ffxBrixelizerRawContextCreateCascade(&outContext->context, &rawCascadeDesc));
        }
    }

    outContext->numCascades = desc->numCascades;
    outContext->levelJobs = (FfxBrixelizerRawJobDescription*)malloc(FFX_BRIXELIZER_LEVEL_JOBS_CAPACITY * sizeof(FfxBrixelizerRawJobDescription));
    outContext->moverRecords = (FfxBrixelizerMoverRecord*)malloc(FFX_BRIXELIZER_MAX_MOVER_RECORDS * sizeof(FfxBrixelizerMoverRecord));
    outContext->moverRecordOfInstance = (uint32_t*)malloc(FFX_BRIXELIZER_MAX_INSTANCES * sizeof(uint32_t));
    outContext->freeMoverRecords = (uint32_t*)malloc(FFX_BRIXELIZER_MAX_MOVER_RECORDS * sizeof(uint32_t));
    outContext->remapScratch = (FfxBrixelizerRawMoverRemap*)malloc(FFX_BRIXELIZER_MAX_MOVER_RECORDS * sizeof(FfxBrixelizerRawMoverRemap));
    if (!outContext->levelJobs || !outContext->moverRecords || !outContext->moverRecordOfInstance || !outContext->freeMoverRecords || !outContext->remapScratch) {
        return FFX_ERROR_OUT_OF_MEMORY;
    }
    ifor (FFX_BRIXELIZER_MAX_INSTANCES) {
        outContext->moverRecordOfInstance[i] = FFX_BRIXELIZER_NO_MOVER_RECORD;
    }
    ifor (FFX_BRIXELIZER_MAX_MOVER_RECORDS) {
        outContext->freeMoverRecords[i] = FFX_BRIXELIZER_MAX_MOVER_RECORDS - 1 - i;
        outContext->moverRecords[i].instanceID = FFX_BRIXELIZER_NO_MOVER_RECORD;
    }
    outContext->numFreeMoverRecords = FFX_BRIXELIZER_MAX_MOVER_RECORDS;

    return FFX_OK;
}

FfxErrorCode ffxBrixelizerContextDestroy(FfxBrixelizerContext* uncastContext)
{
    FfxBrixelizerContext_Private *context = (FfxBrixelizerContext_Private*)uncastContext;

    FfxErrorCode result = ffxBrixelizerRawContextDestroy(&context->context);
    if (result != FFX_OK) {
        return result;
    }
    free(context->levelJobs);
    free(context->moverRecords);
    free(context->moverRecordOfInstance);
    free(context->freeMoverRecords);
    free(context->remapScratch);
    memset(context, 0, sizeof(*context));
    return FFX_OK;
}

FfxErrorCode ffxBrixelizerBakeUpdate(FfxBrixelizerContext* uncastContext, const FfxBrixelizerUpdateDescription* desc, FfxBrixelizerBakedUpdateDescription* uncastOutDesc)
{
    FfxBrixelizerContext_Private *context = (FfxBrixelizerContext_Private*)uncastContext;
    FfxBrixelizerBakedUpdateDescription_Private *outDesc = (FfxBrixelizerBakedUpdateDescription_Private*)uncastOutDesc;

    memset(outDesc, 0, sizeof(*outDesc));

    uint32_t cascadeIndex = ffxBrixelizerRawGetCascadeToUpdate(desc->frameIndex, context->numCascades);

    outDesc->resources = desc->resources;
    outDesc->cascadeUpdateDesc.cascadeIndex = cascadeIndex;
    outDesc->debugVisualizationDesc = desc->debugVisualizationDesc;
    outDesc->populateDebugAABBsFlags = desc->populateDebugAABBsFlags;
    outDesc->outStats = desc->outStats;

    FfxBrixelizerCascadePrivate *cascadePrivate = &context->cascades[cascadeIndex];

    FfxBrixelizerRawCascadeUpdateDescription *cascadeUpdateDesc = &outDesc->cascadeUpdateDesc;
    ifor(3) {
        float cascadeCenter = floorf(desc->sdfCenter[i] / cascadePrivate->voxelSize);
        cascadeUpdateDesc->clipmapOffset[i] = (int32_t)cascadeCenter;
        cascadeUpdateDesc->cascadeMin[i] = (cascadeCenter - (0.5f * (float)FFX_BRIXELIZER_CASCADE_RESOLUTION)) * cascadePrivate->voxelSize;
    }
    // vkfw patch: the placement this level keeps until its next turn (its extra updates use it).
    cascadePrivate->placed = 1;
    ifor (3) {
        cascadePrivate->placedClipmapOffset[i] = cascadeUpdateDesc->clipmapOffset[i];
        cascadePrivate->placedCascadeMin[i]    = cascadeUpdateDesc->cascadeMin[i];
    }
    extraLevels(context, desc->extraLevelMask, desc->extraStaticLevelMask, cascadeIndex, &outDesc->extraLevelMask, &outDesc->extraStaticLevelMask);
    if (desc->outExtraLevelMask)
        *desc->outExtraLevelMask = outDesc->extraLevelMask;

    // XXX -- tmp just keep these values for now
    cascadeUpdateDesc->maxReferences = desc->maxReferences;
    cascadeUpdateDesc->maxBricksPerBake = desc->maxBricksPerBake;
    cascadeUpdateDesc->triangleSwapSize = desc->triangleSwapSize;

    outDesc->numStaticJobs = 0;
    outDesc->numDynamicJobs = 0;

    FfxBrixelizerAABB casacadeAABB = {};
    ifor (3) {
        casacadeAABB.min[i] = cascadeUpdateDesc->cascadeMin[i];
        casacadeAABB.max[i] = casacadeAABB.min[i] + (cascadePrivate->voxelSize * (float)FFX_BRIXELIZER_CASCADE_RESOLUTION);
    }

    // create static jobs
    if (cascadePrivate->flags & FFX_BRIXELIZER_CASCADE_STATIC) {
        FfxBrixelizerRawJobDescription *curJob = outDesc->staticJobs;

        // Create instance jobs
        ifor (context->numStaticInstances) {
            FfxBrixelizerInstance *instance = &context->instances[i];
            // vkfw patch: cascadeIndex is the logical cascade, which is the granularity the band names.
            if (!instance->isMover && instanceInCascade(instance, cascadeIndex) && aabbsOverlap(instance->aabb, casacadeAABB)) {
                FfxBrixelizerRawJobDescription job = {};
                ifor (3) {
                    job.aabbMin[i] = instance->aabb.min[i];
                    job.aabbMax[i] = instance->aabb.max[i];
                }
                job.instanceIdx = instance->id;
                *curJob++ = job;
                outDesc->numStaticJobs++;
                FFX_ASSERT(outDesc->numStaticJobs <= FFX_ARRAY_ELEMENTS(outDesc->staticJobs));
            }
        }

        // Create invalidations
        uint32_t cascadeMask = 1 << cascadeIndex;
        uint32_t curInvalidation= 0;
        while (curInvalidation < context->numInvalidations) {
            FfxBrixelizerInvalidation *invalidation = &context->invalidations[curInvalidation];
            if (invalidation->cascades & cascadeMask) {
                if (aabbsOverlap(invalidation->aabb, casacadeAABB)) {
                    FfxBrixelizerRawJobDescription job = {};
                    ifor (3) {
                        job.aabbMin[i] = invalidation->aabb.min[i];
                        job.aabbMax[i] = invalidation->aabb.max[i];
                    }
                    job.flags = FFX_BRIXELIZER_RAW_JOB_FLAG_INVALIDATE;
                    *curJob++ = job;
                    outDesc->numStaticJobs++;
                    FFX_ASSERT(outDesc->numStaticJobs <= FFX_ARRAY_ELEMENTS(outDesc->staticJobs));
                }
                invalidation->cascades &= ~cascadeMask;
                if (!invalidation->cascades) {
                    context->invalidations[curInvalidation] = context->invalidations[--context->numInvalidations];
                    continue;
                }
            }
            ++curInvalidation;
        }
    }

    // vkfw patch: create mover jobs: every mover instance in band. The mover cascade is reset on each of its turns, like the dynamic one,
    // so a mover's move needs no invalidation: a moving body re-made every frame would otherwise queue two a frame, each held until every level
    // of its band had taken it, and overrun the invalidation array within seconds.
    outDesc->numMoverJobs = 0;
    if (cascadePrivate->flags & FFX_BRIXELIZER_CASCADE_MOVER) {
        FfxBrixelizerRawJobDescription *curJob = outDesc->moverJobs;

        ifor (context->numStaticInstances) {
            FfxBrixelizerInstance *instance = &context->instances[i];
            if (instance->isMover && instanceInCascade(instance, cascadeIndex) && aabbsOverlap(instance->aabb, casacadeAABB)) {
                FfxBrixelizerRawJobDescription job = {};
                ifor (3) {
                    job.aabbMin[i] = instance->aabb.min[i];
                    job.aabbMax[i] = instance->aabb.max[i];
                }
                job.instanceIdx = instance->id;
                *curJob++ = job;
                outDesc->numMoverJobs++;
                FFX_ASSERT(outDesc->numMoverJobs <= FFX_ARRAY_ELEMENTS(outDesc->moverJobs));
            }
        }

    }

    // create dynamic jobs
    if (cascadePrivate->flags & FFX_BRIXELIZER_CASCADE_DYNAMIC) {
        FfxBrixelizerRawJobDescription *job = outDesc->dynamicJobs;

        // Create instance jobs
        // vkfw patch: counted as they are emitted rather than assumed to be every dynamic instance,
        // because a band (FFX_BRIXELIZER_INSTANCE_FLAG_CASCADE_RANGE) can exclude this cascade.
        outDesc->numDynamicJobs = 0;
        for (uint32_t i = context->dynamicInstanceStartIndex; i < FFX_ARRAY_ELEMENTS(context->instances); ++i) {
            if (!instanceInCascade(&context->instances[i], cascadeIndex)) {
                continue;
            }
            jfor (3) {
                job->aabbMin[j] = context->instances[i].aabb.min[j];
                job->aabbMax[j] = context->instances[i].aabb.max[j];
            }
            job->instanceIdx = context->instances[i].id;
            ++job;
            ++outDesc->numDynamicJobs;
        }
        // vkfw patch: at a level without a mover cascade the movers are baked as the dynamic instances are.
        if (!(cascadePrivate->flags & FFX_BRIXELIZER_CASCADE_MOVER)) {
            ifor (context->numStaticInstances) {
                const FfxBrixelizerInstance *instance = &context->instances[i];
                if (!instance->isMover || !instanceInCascade(instance, cascadeIndex) || !aabbsOverlap(instance->aabb, casacadeAABB)) {
                    continue;
                }
                jfor (3) {
                    job->aabbMin[j] = instance->aabb.min[j];
                    job->aabbMax[j] = instance->aabb.max[j];
                }
                job->instanceIdx = instance->id;
                ++job;
                ++outDesc->numDynamicJobs;
            }
        }
    }

    if (desc->outScratchBufferSize) {
        size_t staticSize = 0, dynamicSize = 0, moverSize = 0;

        FfxBrixelizerRawCascadeUpdateDescription *cascadeUpdateDesc = &outDesc->cascadeUpdateDesc;
        uint32_t cascadeIndex = cascadeUpdateDesc->cascadeIndex;

        if (context->cascades[cascadeIndex].flags & FFX_BRIXELIZER_CASCADE_STATIC) {
            cascadeUpdateDesc->numJobs = outDesc->numStaticJobs;
            cascadeUpdateDesc->jobs = outDesc->staticJobs;
            cascadeUpdateDesc->flags = FFX_BRIXELIZER_CASCADE_UPDATE_FLAG_NONE;
            FFX_VALIDATE(ffxBrixelizerRawContextGetScratchMemorySize(&context->context, cascadeUpdateDesc, &staticSize));
        }
        if (context->cascades[cascadeIndex].flags & FFX_BRIXELIZER_CASCADE_DYNAMIC) {
            cascadeUpdateDesc->numJobs = outDesc->numDynamicJobs;
            cascadeUpdateDesc->jobs = outDesc->dynamicJobs;
            cascadeUpdateDesc->flags = FFX_BRIXELIZER_CASCADE_UPDATE_FLAG_RESET;
            FFX_VALIDATE(ffxBrixelizerRawContextGetScratchMemorySize(&context->context, cascadeUpdateDesc, &dynamicSize));
        }

        if (context->cascades[cascadeIndex].flags & FFX_BRIXELIZER_CASCADE_MOVER) {
            cascadeUpdateDesc->numJobs = outDesc->numMoverJobs;
            cascadeUpdateDesc->jobs = outDesc->moverJobs;
            cascadeUpdateDesc->flags = FFX_BRIXELIZER_CASCADE_UPDATE_FLAG_RESET;
            FFX_VALIDATE(ffxBrixelizerRawContextGetScratchMemorySize(&context->context, cascadeUpdateDesc, &moverSize));
        }

        size_t scratchSize = staticSize > dynamicSize ? staticSize : dynamicSize;
        scratchSize = scratchSize > moverSize ? scratchSize : moverSize;

        // vkfw patch: and the extra levels' updates, which run one after another on the same scratch.
        ifor (context->numCascades) {
            if (!(outDesc->extraLevelMask & (1u << i))) continue;
            const FfxBrixelizerCascadePrivate *level = &context->cascades[i];
            FfxBrixelizerRawCascadeUpdateDescription extraDesc = *cascadeUpdateDesc;
            jfor (3) {
                extraDesc.clipmapOffset[j] = level->placedClipmapOffset[j];
                extraDesc.cascadeMin[j]    = level->placedCascadeMin[j];
            }
            const FfxBrixelizerAABB aabb = placedCascadeAABB(level);
            const struct { FfxBrixelizerCascadeFlag flag; FfxBrixelizerLevelSource source; uint32_t index; uint32_t updateFlags; } sources[] = {
                { FFX_BRIXELIZER_CASCADE_STATIC, FFX_BRIXELIZER_LEVEL_SOURCE_STATIC, level->staticIndex, FFX_BRIXELIZER_CASCADE_UPDATE_FLAG_NONE },
                { FFX_BRIXELIZER_CASCADE_MOVER, FFX_BRIXELIZER_LEVEL_SOURCE_MOVER, level->moverIndex, FFX_BRIXELIZER_CASCADE_UPDATE_FLAG_RESET },
                { FFX_BRIXELIZER_CASCADE_DYNAMIC, FFX_BRIXELIZER_LEVEL_SOURCE_DYNAMIC, level->dynamicIndex, FFX_BRIXELIZER_CASCADE_UPDATE_FLAG_RESET },
            };
            for (const auto& source : sources) {
                if (!(level->flags & source.flag)) continue;
                if (source.source == FFX_BRIXELIZER_LEVEL_SOURCE_STATIC && !(outDesc->extraStaticLevelMask & (1u << i))) continue;
                extraDesc.cascadeIndex = source.index;
                extraDesc.numJobs      = buildLevelJobs(context, i, source.source, aabb, context->levelJobs, false);
                extraDesc.jobs         = context->levelJobs;
                extraDesc.flags        = source.updateFlags;
                size_t size = 0;
                FFX_VALIDATE(ffxBrixelizerRawContextGetScratchMemorySize(&context->context, &extraDesc, &size));
                scratchSize = scratchSize > size ? scratchSize : size;
            }
        }
        *desc->outScratchBufferSize = scratchSize;
    }

    return FFX_OK;
}

FfxErrorCode ffxBrixelizerUpdate(FfxBrixelizerContext* uncastContext, FfxBrixelizerBakedUpdateDescription* uncastDesc, FfxResource scratchBuffer, FfxCommandList commandList)
{
    FfxBrixelizerContext_Private *context = (FfxBrixelizerContext_Private*)uncastContext;
    FfxBrixelizerBakedUpdateDescription_Private *desc = (FfxBrixelizerBakedUpdateDescription_Private*)uncastDesc;

    ffxBrixelizerRawContextFlushInstances(&context->context, commandList);

    uint32_t cascadeIndex = desc->cascadeUpdateDesc.cascadeIndex;
    FfxBrixelizerCascadePrivate *cascadePrivate = &context->cascades[cascadeIndex];

    uint32_t staticCascadeIndex  = cascadePrivate->staticIndex;
    uint32_t moverCascadeIndex   = cascadePrivate->moverIndex;
    uint32_t dynamicCascadeIndex = cascadePrivate->dynamicIndex;
    uint32_t mergedCascadeIndex  = cascadePrivate->mergedIndex;
    uint32_t flags = cascadePrivate->flags;

    ffxBrixelizerRawContextBegin(&context->context, desc->resources);

    ffxBrixelizerRawContextRegisterScratchBuffer(&context->context, scratchBuffer);

    // update static cascade
    if (flags & FFX_BRIXELIZER_CASCADE_STATIC) {
        desc->cascadeUpdateDesc.cascadeIndex = staticCascadeIndex;
        desc->cascadeUpdateDesc.numJobs = desc->numStaticJobs;
        desc->cascadeUpdateDesc.jobs = desc->staticJobs;
        desc->cascadeUpdateDesc.flags = FFX_BRIXELIZER_CASCADE_UPDATE_FLAG_NONE;
        ffxBrixelizerRawContextUpdateCascade(&context->context, &desc->cascadeUpdateDesc);
    }

    // vkfw patch: update mover cascade (reset, as the dynamic one: its instances persist, its bricks do not)
    if (flags & FFX_BRIXELIZER_CASCADE_MOVER) {
        desc->cascadeUpdateDesc.cascadeIndex = moverCascadeIndex;
        desc->cascadeUpdateDesc.numJobs = desc->numMoverJobs;
        desc->cascadeUpdateDesc.jobs = desc->moverJobs;
        desc->cascadeUpdateDesc.flags = FFX_BRIXELIZER_CASCADE_UPDATE_FLAG_RESET;
        ffxBrixelizerRawContextUpdateCascade(&context->context, &desc->cascadeUpdateDesc);
    }

    // update dynamic cascade
    if (flags & FFX_BRIXELIZER_CASCADE_DYNAMIC) {
        desc->cascadeUpdateDesc.cascadeIndex = dynamicCascadeIndex;
        desc->cascadeUpdateDesc.numJobs = desc->numDynamicJobs;
        desc->cascadeUpdateDesc.jobs = desc->dynamicJobs;
        desc->cascadeUpdateDesc.flags = FFX_BRIXELIZER_CASCADE_UPDATE_FLAG_RESET;
        ffxBrixelizerRawContextUpdateCascade(&context->context, &desc->cascadeUpdateDesc);
    }


    // vkfw patch: every level merges its sources three ways into its own merged cascade.
    if (desc->debugVisualizationDesc)
    {
        const uint32_t sources[] = { staticCascadeIndex, moverCascadeIndex, dynamicCascadeIndex };
        for (uint32_t source : sources)
        {
            if (source != (uint32_t)-1)
                ffxBrixelizerRawContextBuildAABBTree(&context->context, source);
        }
    }
    ffxBrixelizerRawContextMergeCascades3(&context->context, staticCascadeIndex, moverCascadeIndex, dynamicCascadeIndex, mergedCascadeIndex);
    ffxBrixelizerRawContextBuildAABBTree(&context->context, mergedCascadeIndex);
    recordLevelCacheUpdate(context, cascadeIndex);

    // vkfw patch: the extra levels(extraLevelMask), at their own placement: their mover and dynamic cascades rebuilt, their static one too
    // where it is asked for and has invalidations pending, then merged.
    ifor (context->numCascades) {
        if (!(desc->extraLevelMask & (1u << i))) continue;
        const FfxBrixelizerCascadePrivate *level = &context->cascades[i];
        FfxBrixelizerRawCascadeUpdateDescription extraDesc = desc->cascadeUpdateDesc;
        jfor (3) {
            extraDesc.clipmapOffset[j] = level->placedClipmapOffset[j];
            extraDesc.cascadeMin[j]    = level->placedCascadeMin[j];
        }
        const FfxBrixelizerAABB aabb = placedCascadeAABB(level);

        if ((level->flags & FFX_BRIXELIZER_CASCADE_STATIC) && (desc->extraStaticLevelMask & (1u << i))) {
            extraDesc.cascadeIndex = level->staticIndex;
            extraDesc.numJobs      = buildLevelJobs(context, i, FFX_BRIXELIZER_LEVEL_SOURCE_STATIC, aabb, context->levelJobs, true);
            extraDesc.jobs         = context->levelJobs;
            extraDesc.flags        = FFX_BRIXELIZER_CASCADE_UPDATE_FLAG_NONE;
            ffxBrixelizerRawContextUpdateCascade(&context->context, &extraDesc);
        }
        if (level->flags & FFX_BRIXELIZER_CASCADE_MOVER) {
            extraDesc.cascadeIndex = level->moverIndex;
            extraDesc.numJobs      = buildLevelJobs(context, i, FFX_BRIXELIZER_LEVEL_SOURCE_MOVER, aabb, context->levelJobs, false);
            extraDesc.jobs         = context->levelJobs;
            extraDesc.flags        = FFX_BRIXELIZER_CASCADE_UPDATE_FLAG_RESET;
            ffxBrixelizerRawContextUpdateCascade(&context->context, &extraDesc);
        }
        if (level->flags & FFX_BRIXELIZER_CASCADE_DYNAMIC) {
            extraDesc.cascadeIndex = level->dynamicIndex;
            extraDesc.numJobs      = buildLevelJobs(context, i, FFX_BRIXELIZER_LEVEL_SOURCE_DYNAMIC, aabb, context->levelJobs, false);
            extraDesc.jobs         = context->levelJobs;
            extraDesc.flags        = FFX_BRIXELIZER_CASCADE_UPDATE_FLAG_RESET;
            ffxBrixelizerRawContextUpdateCascade(&context->context, &extraDesc);
        }
        ffxBrixelizerRawContextMergeCascades3(&context->context, level->staticIndex, level->moverIndex, level->dynamicIndex, level->mergedIndex);
        ffxBrixelizerRawContextBuildAABBTree(&context->context, level->mergedIndex);
        recordLevelCacheUpdate(context, i);
    }

    ffxBrixelizerRawContextEnd(&context->context);

    FfxBrixelizerInstanceID instanceIDs[FFX_BRIXELIZER_MAX_INSTANCES] = {};
    if (desc->debugVisualizationDesc) {
        FfxBrixelizerDebugVisualizationDescription debugVisDesc = *desc->debugVisualizationDesc;
        debugVisDesc.commandList = commandList;

        if (desc->populateDebugAABBsFlags & FFX_BRIXELIZER_POPULATE_AABBS_INSTANCES) {
            FFX_ASSERT(desc->debugVisualizationDesc->numDebugAABBInstanceIDs == 0);

            debugVisDesc.debugAABBInstanceIDs = instanceIDs;
            debugVisDesc.numDebugAABBInstanceIDs = 0; // context->numStaticInstances + (FFX_BRIXELIZER_MAX_INSTANCES - context->dynamicInstanceStartIndex);

            FfxBrixelizerInstanceID *instanceID = instanceIDs;
            if (desc->populateDebugAABBsFlags & FFX_BRIXELIZER_POPULATE_AABBS_STATIC_INSTANCES) {
                debugVisDesc.numDebugAABBInstanceIDs += context->numStaticInstances;
                for (uint32_t i = 0; i < context->numStaticInstances; ++i) {
                    *instanceID++ = context->instances[i].id;
                }
            }
            if (desc->populateDebugAABBsFlags & FFX_BRIXELIZER_POPULATE_AABBS_DYNAMIC_INSTANCES) {
                debugVisDesc.numDebugAABBInstanceIDs += FFX_BRIXELIZER_MAX_INSTANCES - context->dynamicInstanceStartIndex;
                for (uint32_t i = context->dynamicInstanceStartIndex; i < FFX_BRIXELIZER_MAX_INSTANCES; ++i) {
                    *instanceID++ = context->instances[i].id;
                }
            }
        }

        if (desc->populateDebugAABBsFlags & (FFX_BRIXELIZER_POPULATE_AABBS_CASCADE_AABBS)) {
            for (uint32_t i = 0; i < context->numCascades; ++i) {
                FfxBrixelizerCascadePrivate *cascade = &context->cascades[i];
                if (!debugVisDesc.cascadeDebugAABB[cascade->mergedIndex]) {
                    debugVisDesc.cascadeDebugAABB[cascade->mergedIndex] = FFX_BRIXELIZER_CASCADE_DEBUG_AABB_BOUNDING_BOX;
                }
            }
        }

        ffxBrixelizerRawContextDebugVisualization(&context->context, &debugVisDesc);
    }

    ffxBrixelizerRawContextSubmit(&context->context, commandList);

    // clear dynamic instances
    {
        FfxBrixelizerInstanceID *instanceIDs = context->scratchSpace.update.instanceIDs;
        for (uint32_t i = context->dynamicInstanceStartIndex, j = 0; i < FFX_ARRAY_ELEMENTS(context->instances); ++i, ++j) {
            instanceIDs[j] = context->instances[i].id;
        }
        ffxBrixelizerRawContextDestroyInstances(&context->context, instanceIDs, FFX_ARRAY_ELEMENTS(context->instances) - context->dynamicInstanceStartIndex);
        context->dynamicInstanceStartIndex = FFX_ARRAY_ELEMENTS(context->instances);
    }

    if (desc->outStats) {
        memset(desc->outStats, 0, sizeof(*desc->outStats));

        desc->outStats->cascadeIndex = cascadeIndex;

        FfxBrixelizerDebugCounters debugCounters = {};
        ffxBrixelizerRawContextGetDebugCounters(&context->context, &debugCounters);
        desc->outStats->contextStats.brickAllocationsAttempted = debugCounters.brickCount;
        desc->outStats->contextStats.brickAllocationsSucceeded = 0;
        desc->outStats->contextStats.bricksCleared = debugCounters.clearBricks;
        desc->outStats->contextStats.bricksMerged = debugCounters.mergeBricks;
        desc->outStats->contextStats.freeBricks = debugCounters.freeBricks;

        if (flags & FFX_BRIXELIZER_CASCADE_STATIC) {
            FfxBrixelizerScratchCounters scratchCounters = {};
            ffxBrixelizerRawContextGetCascadeCounters(&context->context, staticCascadeIndex, &scratchCounters);
            desc->outStats->staticCascadeStats.trianglesAllocated = scratchCounters.triangles;
            desc->outStats->staticCascadeStats.referencesAllocated = scratchCounters.references;
            desc->outStats->staticCascadeStats.bricksAllocated = scratchCounters.numBricksAllocated;
            desc->outStats->contextStats.brickAllocationsSucceeded += scratchCounters.clearBricks;
        }

        // vkfw patch: the mover cascade's bake counts into the static cascade's stats.
        if (flags & FFX_BRIXELIZER_CASCADE_MOVER) {
            FfxBrixelizerScratchCounters scratchCounters = {};
            ffxBrixelizerRawContextGetCascadeCounters(&context->context, moverCascadeIndex, &scratchCounters);
            desc->outStats->staticCascadeStats.trianglesAllocated += scratchCounters.triangles;
            desc->outStats->staticCascadeStats.referencesAllocated += scratchCounters.references;
            desc->outStats->staticCascadeStats.bricksAllocated += scratchCounters.numBricksAllocated;
            desc->outStats->contextStats.brickAllocationsSucceeded += scratchCounters.clearBricks;
        }

        if (flags & FFX_BRIXELIZER_CASCADE_DYNAMIC) {
            FfxBrixelizerScratchCounters scratchCounters = {};
            ffxBrixelizerRawContextGetCascadeCounters(&context->context, dynamicCascadeIndex, &scratchCounters);
            desc->outStats->dynamicCascadeStats.trianglesAllocated = scratchCounters.triangles;
            desc->outStats->dynamicCascadeStats.referencesAllocated = scratchCounters.references;
            desc->outStats->dynamicCascadeStats.bricksAllocated = scratchCounters.numBricksAllocated;
            desc->outStats->contextStats.brickAllocationsSucceeded += scratchCounters.clearBricks;
        }
    }

    return FFX_OK;
}

FfxErrorCode ffxBrixelizerRegisterBuffers(FfxBrixelizerContext* uncastContext, const FfxBrixelizerBufferDescription *bufferDescs, uint32_t numBufferDescs)
{
    FfxBrixelizerContext_Private *context = (FfxBrixelizerContext_Private*)uncastContext;

    return ffxBrixelizerRawContextRegisterBuffers(&context->context, bufferDescs, numBufferDescs);
}

FfxErrorCode ffxBrixelizerUnregisterBuffers(FfxBrixelizerContext* uncastContext, const uint32_t* indices, uint32_t numIndices)
{
    FfxBrixelizerContext_Private* context = (FfxBrixelizerContext_Private*)uncastContext;

    return ffxBrixelizerRawContextUnregisterBuffers(&context->context, indices, numIndices);
}

// vkfw patch: resolve a description's cascade band against this context, clamped into range.
static void resolveCascadeRange(const FfxBrixelizerContext_Private *context,
                                const FfxBrixelizerInstanceDescription *desc,
                                uint32_t *outMinCascade,
                                uint32_t *outMaxCascade)
{
    uint32_t minCascade = 0;
    uint32_t maxCascade = context->numCascades - 1;

    if (desc->flags & FFX_BRIXELIZER_INSTANCE_FLAG_CASCADE_RANGE) {
        minCascade = desc->minCascade;
        maxCascade = desc->maxCascade;

        // An inverted or out-of-range band would drop the instance from the SDF everywhere, which is
        // a far quieter failure than it deserves -- the geometry simply would not be there, with
        // nothing logged. Clamp into the context, and say so in a debug build.
        FFX_ASSERT(minCascade <= maxCascade);
        FFX_ASSERT(maxCascade < context->numCascades);
        if (maxCascade >= context->numCascades) {
            maxCascade = context->numCascades - 1;
        }
        if (minCascade > maxCascade) {
            minCascade = maxCascade;
        }
    }

    *outMinCascade = minCascade;
    *outMaxCascade = maxCascade;
}

static void addInvalidationJob(FfxBrixelizerContext_Private* context, FfxBrixelizerAABB aabb, uint32_t minCascade, uint32_t maxCascade, uint32_t isMover)
{
    // vkfw patch: a mover is in no static cascade, and the cascades it is in are reset on each of their turns.
    if (isMover) {
        return;
    }

    FfxBrixelizerInvalidation invalidation = {};

    uint32_t cascadesMask = 0;
    ifor (context->numCascades) {
        // vkfw patch: only the cascades this instance's band reaches. An invalidation outside it
        // clears bricks the instance never put anything into, and each one costs a job on that
        // cascade's next turn.
        if ((context->cascades[i].flags & FFX_BRIXELIZER_CASCADE_STATIC) && i >= minCascade && i <= maxCascade) {
            cascadesMask |= 1 << i;
        }
    }
    invalidation.cascades = cascadesMask;
    invalidation.aabb = aabb;

    FFX_ASSERT(context->numInvalidations < FFX_ARRAY_ELEMENTS(context->invalidations));
    context->invalidations[context->numInvalidations++] = invalidation;
}

FfxErrorCode ffxBrixelizerCreateInstances(FfxBrixelizerContext* uncastContext, const FfxBrixelizerInstanceDescription* descs, uint32_t numDescs)
{
    FfxBrixelizerContext_Private *context = (FfxBrixelizerContext_Private*)uncastContext;

    FFX_ASSERT(context->numStaticInstances + numDescs <= context->dynamicInstanceStartIndex);

    FfxBrixelizerRawInstanceDescription *rawDescs = context->scratchSpace.createInstances.rawInstanceDescs;
    FfxBrixelizerInstanceID *instanceIDs = context->scratchSpace.createInstances.instanceIDs;

    ifor (numDescs) {
        const FfxBrixelizerInstanceDescription *desc = &descs[i];

        FfxBrixelizerRawInstanceDescription *instanceDesc = &rawDescs[i];

        ifor (3) {
            instanceDesc->aabbMin[i] = desc->aabb.min[i];
            instanceDesc->aabbMax[i] = desc->aabb.max[i];
        }

        memcpy(&instanceDesc->transform, &desc->transform, sizeof(instanceDesc->transform));

        instanceDesc->indexFormat        = desc->indexFormat;
        instanceDesc->indexBuffer        = desc->indexBuffer;
        instanceDesc->indexBufferOffset  = desc->indexBufferOffset;
        instanceDesc->triangleCount      = desc->triangleCount;

        instanceDesc->vertexBuffer       = desc->vertexBuffer;
        instanceDesc->vertexStride       = desc->vertexStride;
        instanceDesc->vertexBufferOffset = desc->vertexBufferOffset;
        instanceDesc->vertexCount        = desc->vertexCount;
        instanceDesc->vertexFormat       = desc->vertexFormat;

        instanceDesc->flags = FFX_BRIXELIZER_RAW_INSTANCE_FLAG_NONE;

        instanceDesc->outInstanceID = &instanceIDs[i];
    }

    RETURN_ON_FAIL(ffxBrixelizerRawContextCreateInstances(&context->context, rawDescs, numDescs));

    ifor (numDescs) {
        const FfxBrixelizerInstanceDescription *desc = &descs[i];
        FfxBrixelizerInstanceID instanceID = instanceIDs[i];

        // vkfw patch: resolved once here rather than per bake -- the description is gone by then.
        uint32_t minCascade = 0;
        uint32_t maxCascade = 0;
        resolveCascadeRange(context, desc, &minCascade, &maxCascade);

        if (desc->flags & FFX_BRIXELIZER_INSTANCE_FLAG_DYNAMIC) {
            uint32_t instanceIndex = --context->dynamicInstanceStartIndex;
            FfxBrixelizerInstance *instance = &context->instances[instanceIndex];

            instance->id = instanceID;
            instance->aabb = desc->aabb;
            instance->minCascade = minCascade;
            instance->maxCascade = maxCascade;
            instance->isMover = 0;
        } else {
            uint32_t instanceIndex = context->numStaticInstances++;
            FfxBrixelizerInstance *instance = &context->instances[instanceIndex];

            instance->id = instanceID;
            instance->aabb = desc->aabb;
            instance->minCascade = minCascade;
            instance->maxCascade = maxCascade;
            instance->isMover = (desc->flags & FFX_BRIXELIZER_INSTANCE_FLAG_MOVER) ? 1 : 0;
            context->instanceIndices[instanceID] = instanceIndex;

            // vkfw patch: a mover's record (none past FFX_BRIXELIZER_MAX_MOVER_RECORDS: its moves then carry no cache).
            if (instance->isMover && context->numFreeMoverRecords > 0) {
                const uint32_t recordIndex = context->freeMoverRecords[--context->numFreeMoverRecords];
                FfxBrixelizerMoverRecord *record = &context->moverRecords[recordIndex];
                memcpy(&record->current, &desc->transform, sizeof(record->current));
                record->currentAABB    = desc->aabb;
                record->bakedLevelMask = 0;
                record->instanceID     = instanceID;
                context->moverRecordOfInstance[instanceID] = recordIndex;
            }

            addInvalidationJob(context, desc->aabb, minCascade, maxCascade, instance->isMover);

            if (desc->outInstanceID) {
                *desc->outInstanceID = instanceID;
            }
        }
    }

    return FFX_OK;
}

FfxErrorCode ffxBrixelizerDeleteInstances(FfxBrixelizerContext* uncastContext, const FfxBrixelizerInstanceID* instanceIDs, uint32_t numInstanceIDs)
{
    FfxBrixelizerContext_Private *context = (FfxBrixelizerContext_Private*)uncastContext;

    RETURN_ON_FAIL(ffxBrixelizerRawContextDestroyInstances(&context->context, instanceIDs, numInstanceIDs));

    ifor (numInstanceIDs) {
        FfxBrixelizerInstanceID instanceID = instanceIDs[i];

        uint32_t index = context->instanceIndices[instanceID];
        FfxBrixelizerInstance instance = context->instances[index];

        addInvalidationJob(context, instance.aabb, instance.minCascade, instance.maxCascade, instance.isMover);

        // vkfw patch: its mover record goes back.
        const uint32_t recordIndex = context->moverRecordOfInstance[instanceID];
        if (recordIndex != FFX_BRIXELIZER_NO_MOVER_RECORD) {
            context->moverRecords[recordIndex].instanceID = FFX_BRIXELIZER_NO_MOVER_RECORD;
            context->freeMoverRecords[context->numFreeMoverRecords++] = recordIndex;
            context->moverRecordOfInstance[instanceID] = FFX_BRIXELIZER_NO_MOVER_RECORD;
        }

        instance = context->instances[--context->numStaticInstances];
        context->instances[index] = instance;
        context->instanceIndices[instance.id] = index;
    }

    return FFX_OK;
}

FfxErrorCode ffxBrixelizerGetContextInfo(FfxBrixelizerContext* uncastContext, FfxBrixelizerContextInfo* contextInfo)
{
    FfxBrixelizerContext_Private *context = (FfxBrixelizerContext_Private*)uncastContext;

    return ffxBrixelizerRawContextGetInfo(&context->context, contextInfo);
}

FfxErrorCode ffxBrixelizerUpdateInstanceTransforms(FfxBrixelizerContext* uncastContext, const FfxBrixelizerInstanceID* instanceIDs, const FfxFloat32x3x4* transforms,
                                                   const FfxBrixelizerAABB* aabbs, uint32_t numInstances)
{
    FFX_RETURN_ON_ERROR(uncastContext && (numInstances == 0 || (instanceIDs && transforms && aabbs)), FFX_ERROR_INVALID_POINTER);
    FfxBrixelizerContext_Private *context = (FfxBrixelizerContext_Private*)uncastContext;

    ifor (numInstances) {
        const FfxBrixelizerInstanceID instanceID = instanceIDs[i];
        FFX_RETURN_ON_ERROR(instanceID < FFX_BRIXELIZER_MAX_INSTANCES, FFX_ERROR_INVALID_ARGUMENT);
        FfxBrixelizerInstance *instance = &context->instances[context->instanceIndices[instanceID]];
        FFX_RETURN_ON_ERROR(instance->id == instanceID && instance->isMover, FFX_ERROR_INVALID_ARGUMENT);
        instance->aabb = aabbs[i];
        const uint32_t recordIndex = context->moverRecordOfInstance[instanceID];
        if (recordIndex != FFX_BRIXELIZER_NO_MOVER_RECORD) {
            memcpy(&context->moverRecords[recordIndex].current, &transforms[i], sizeof(FfxFloat32x3x4));
            context->moverRecords[recordIndex].currentAABB = aabbs[i];
        }
        RETURN_ON_FAIL(ffxBrixelizerRawContextUpdateInstances(&context->context, &instanceID, &transforms[i], &aabbs[i].min, &aabbs[i].max, 1));
    }
    return FFX_OK;
}

FfxErrorCode ffxBrixelizerGetPendingStaticInvalidationMask(FfxBrixelizerContext* uncastContext, uint32_t* outLevelMask)
{
    FFX_RETURN_ON_ERROR(uncastContext != NULL && outLevelMask != NULL, FFX_ERROR_INVALID_POINTER);
    const FfxBrixelizerContext_Private *context = (const FfxBrixelizerContext_Private*)uncastContext;
    uint32_t mask = 0;
    ifor (context->numInvalidations) {
        mask |= context->invalidations[i].cascades;
    }
    *outLevelMask = mask;
    return FFX_OK;
}

FfxErrorCode ffxBrixelizerGetCascadeIndices(FfxBrixelizerContext* uncastContext, uint32_t level, uint32_t* staticIndex, uint32_t* moverIndex, uint32_t* dynamicIndex, uint32_t* mergedIndex)
{
    FFX_RETURN_ON_ERROR(uncastContext != NULL, FFX_ERROR_INVALID_POINTER);
    const FfxBrixelizerContext_Private *context = (const FfxBrixelizerContext_Private*)uncastContext;
    FFX_RETURN_ON_ERROR(level < context->numCascades, FFX_ERROR_INVALID_ARGUMENT);

    const FfxBrixelizerCascadePrivate *cascade = &context->cascades[level];
    if (staticIndex)  *staticIndex  = cascade->staticIndex;
    if (moverIndex)   *moverIndex   = cascade->moverIndex;
    if (dynamicIndex) *dynamicIndex = cascade->dynamicIndex;
    if (mergedIndex)  *mergedIndex  = cascade->mergedIndex;
    return FFX_OK;
}

FfxErrorCode ffxBrixelizerGetRawContext(FfxBrixelizerContext* context, FfxBrixelizerRawContext** outContext)
{
    FFX_RETURN_ON_ERROR(context != NULL, FFX_ERROR_INVALID_POINTER);
    *outContext = &((FfxBrixelizerContext_Private*)context)->context;
    return FFX_OK;
}