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
} FfxBrixelizerBakedUpdateDescription_Private;

FFX_STATIC_ASSERT(sizeof(FfxBrixelizerBakedUpdateDescription) == sizeof(FfxBrixelizerBakedUpdateDescription_Private));

typedef struct FfxBrixelizerCascadePrivate {
    FfxBrixelizerCascadeFlag flags;
    float                    voxelSize;
    uint32_t                 staticIndex;
    uint32_t                 dynamicIndex;
    uint32_t                 mergedIndex;
    uint32_t                 moverIndex;    // vkfw patch
} FfxBrixelizerCascadePrivate;

typedef struct FfxBrixelizerInvalidation {
    uint32_t          cascades;         // the levels whose static cascade it still has to clear
    FfxBrixelizerAABB aabb;
    uint32_t          moverCascades;    // vkfw patch: the levels whose mover cascade it still has to clear
} FfxBrixelizerInvalidation;

typedef struct FfxBrixelizerInstance {
    FfxBrixelizerInstanceID id;
    FfxBrixelizerAABB       aabb;
    // vkfw patch: the logical cascade band, inclusive, resolved and clamped once at creation so the
    // job loops never have to look at the flag again. Without a band this is the whole context,
    // which is what every instance used to get.
    uint32_t                minCascade;
    uint32_t                maxCascade;
    // vkfw patch: FFX_BRIXELIZER_INSTANCE_FLAG_MOVER: baked into the mover cascades (the static one at a level without).
    uint32_t                isMover;
} FfxBrixelizerInstance;

// vkfw patch: whether a persistent instance is baked into a level's static cascade: a static one, or a mover at a level without a mover cascade.
static bool instanceInStaticCascade(const FfxBrixelizerInstance *instance, FfxBrixelizerCascadeFlag levelFlags)
{
    return !instance->isMover || !(levelFlags & FFX_BRIXELIZER_CASCADE_MOVER);
}

// vkfw patch: whether an instance is submitted to this logical cascade at all.
static bool instanceInCascade(const FfxBrixelizerInstance *instance, uint32_t cascadeIndex)
{
    return cascadeIndex >= instance->minCascade && cascadeIndex <= instance->maxCascade;
}

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
} FfxBrixelizerContext_Private;

FFX_STATIC_ASSERT(sizeof(FfxBrixelizerContext) >= sizeof(FfxBrixelizerContext_Private));

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

    return FFX_OK;
}

FfxErrorCode ffxBrixelizerContextDestroy(FfxBrixelizerContext* uncastContext)
{
    FfxBrixelizerContext_Private *context = (FfxBrixelizerContext_Private*)uncastContext;

    FfxErrorCode result = ffxBrixelizerRawContextDestroy(&context->context);
    if (result != FFX_OK) {
        return result;
    }
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
            if (instanceInStaticCascade(instance, cascadePrivate->flags) && instanceInCascade(instance, cascadeIndex) && aabbsOverlap(instance->aabb, casacadeAABB)) {
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
                if (!invalidation->cascades && !invalidation->moverCascades) {
                    context->invalidations[curInvalidation] = context->invalidations[--context->numInvalidations];
                    continue;
                }
            }
            ++curInvalidation;
        }
    }

    // vkfw patch: create mover jobs, as the static ones but for the mover instances and the mover invalidations
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

        uint32_t cascadeMask = 1 << cascadeIndex;
        uint32_t curInvalidation = 0;
        while (curInvalidation < context->numInvalidations) {
            FfxBrixelizerInvalidation *invalidation = &context->invalidations[curInvalidation];
            if (invalidation->moverCascades & cascadeMask) {
                if (aabbsOverlap(invalidation->aabb, casacadeAABB)) {
                    FfxBrixelizerRawJobDescription job = {};
                    ifor (3) {
                        job.aabbMin[i] = invalidation->aabb.min[i];
                        job.aabbMax[i] = invalidation->aabb.max[i];
                    }
                    job.flags = FFX_BRIXELIZER_RAW_JOB_FLAG_INVALIDATE;
                    *curJob++ = job;
                    outDesc->numMoverJobs++;
                    FFX_ASSERT(outDesc->numMoverJobs <= FFX_ARRAY_ELEMENTS(outDesc->moverJobs));
                }
                invalidation->moverCascades &= ~cascadeMask;
                if (!invalidation->cascades && !invalidation->moverCascades) {
                    context->invalidations[curInvalidation] = context->invalidations[--context->numInvalidations];
                    continue;
                }
            }
            ++curInvalidation;
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
            cascadeUpdateDesc->flags = FFX_BRIXELIZER_CASCADE_UPDATE_FLAG_NONE;
            FFX_VALIDATE(ffxBrixelizerRawContextGetScratchMemorySize(&context->context, cascadeUpdateDesc, &moverSize));
        }

        size_t scratchSize = staticSize > dynamicSize ? staticSize : dynamicSize;
        *desc->outScratchBufferSize = scratchSize > moverSize ? scratchSize : moverSize;
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

    // vkfw patch: update mover cascade (persistent, as the static one)
    if (flags & FFX_BRIXELIZER_CASCADE_MOVER) {
        desc->cascadeUpdateDesc.cascadeIndex = moverCascadeIndex;
        desc->cascadeUpdateDesc.numJobs = desc->numMoverJobs;
        desc->cascadeUpdateDesc.jobs = desc->moverJobs;
        desc->cascadeUpdateDesc.flags = FFX_BRIXELIZER_CASCADE_UPDATE_FLAG_NONE;
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
    FfxBrixelizerInvalidation invalidation = {};

    uint32_t cascadesMask = 0;
    uint32_t moverCascadesMask = 0;
    ifor (context->numCascades) {
        // vkfw patch: only the cascades this instance's band reaches. An invalidation outside it
        // clears bricks the instance never put anything into, and each one costs a job on that
        // cascade's next turn.
        if (i < minCascade || i > maxCascade) {
            continue;
        }
        // vkfw patch: a mover's goes to the mover cascade of its levels that have one.
        const FfxBrixelizerCascadeFlag levelFlags = context->cascades[i].flags;
        if (isMover && (levelFlags & FFX_BRIXELIZER_CASCADE_MOVER)) {
            moverCascadesMask |= 1 << i;
        } else if (levelFlags & FFX_BRIXELIZER_CASCADE_STATIC) {
            cascadesMask |= 1 << i;
        }
    }
    if (!cascadesMask && !moverCascadesMask) {
        return;
    }
    invalidation.cascades = cascadesMask;
    invalidation.moverCascades = moverCascadesMask;
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