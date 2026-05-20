#ifndef RB_PATH_TRACING_REMIX_RTXDI_SPATIAL_REUSE_RT_HLSL
#define RB_PATH_TRACING_REMIX_RTXDI_SPATIAL_REUSE_RT_HLSL

// RTX Remix-shaped DI spatial reuse contract.
//
// This file is intentionally inactive until a later dispatch/binding slice
// supplies real surface, neighbor-offset, light, reservoir, and clear bindings.
// Spatial reuse math is delegated to RTXDI_DISpatialResampling.
// Ray-traced spatial bias correction is explicitly deferred; this inactive
// contract clamps requested bias correction to RTXDI_BIAS_CORRECTION_BASIC.

#define RTXDI_ALLOWED_BIAS_CORRECTION RTXDI_BIAS_CORRECTION_BASIC

#include "../remix_bridge/RAB_SurfaceBridge.hlsli"
#include "../remix_bridge/RAB_SpatialBridge.hlsli"
#define REMIX_RAB_EXPORT_RAB_NAMES 1
#include "../remix_bridge/RAB_LightBridge.hlsli"
#include "../remix_bridge/RAB_LightSamplingBridge.hlsli"
#include "../remix_bridge/RAB_ReservoirBridge.hlsli"
#include "../remix_bridge/RAB_TemporalOutputBridge.hlsli"

#include "Rtxdi/DI/SpatialResampling.hlsli"

static const uint REMIX_RTXDI_SPATIAL_RNG_PASS = 0x52525807u;

struct RemixRtxdiSpatialReuseDesc
{
    uint2 pixel;
    uint frameIndex;
    uint spatialInputPage;
    uint spatialOutputPage;
    uint activeCheckerboardField;
    uint neighborOffsetMask;
    uint numSamples;
    uint numDisocclusionBoostSamples;
    uint disocclusionBoostFrameThreshold;
    uint reprojectionConfidenceHistoryLength;
    float samplingRadius;
    uint biasCorrectionMode;
    float depthThreshold;
    float normalThreshold;
    uint targetHistoryLength;
    uint enableMaterialSimilarityTest;
    uint discountNaiveSamples;
};

struct RemixRtxdiSpatialReuseResult
{
    RTXDI_DIReservoir reservoir;
    RAB_LightSample selectedLightSample;
};

RTXDI_RuntimeParameters RemixRtxdiSpatialRuntimeParameters(RemixRtxdiSpatialReuseDesc desc)
{
    RTXDI_RuntimeParameters params = (RTXDI_RuntimeParameters)0;
    params.activeCheckerboardField = desc.activeCheckerboardField;
#ifdef REMIX_RAB_SPATIAL_BRIDGE_EXTERNAL_RESOURCES
    params.neighborOffsetMask = desc.neighborOffsetMask;
#else
    params.neighborOffsetMask = min(desc.neighborOffsetMask, 31u);
#endif
    params.frameIndex = desc.frameIndex;
    return params;
}

uint RemixRtxdiSpatialSampleCountFromConfidence(RemixRtxdiSpatialReuseDesc desc)
{
    const int filterRadius = 1;
    const float filterArea = float((filterRadius * 2 + 1) * (filterRadius * 2 + 1));
    float sumConfidence = 0.0;

    for (int yy = -filterRadius; yy <= filterRadius; ++yy)
    {
        for (int xx = -filterRadius; xx <= filterRadius; ++xx)
        {
            sumConfidence += RemixRAB_LoadReprojectionConfidence(int2(desc.pixel) + int2(xx, yy));
        }
    }

    const float historyLength =
        sumConfidence * (float(desc.reprojectionConfidenceHistoryLength) / max(filterArea, 1.0));

    return historyLength < float(desc.disocclusionBoostFrameThreshold)
        ? desc.numDisocclusionBoostSamples
        : desc.numSamples;
}

RTXDI_DISpatialResamplingParameters RemixRtxdiSpatialParameters(RemixRtxdiSpatialReuseDesc desc)
{
    RTXDI_DISpatialResamplingParameters params = (RTXDI_DISpatialResamplingParameters)0;
    params.numSamples = RemixRtxdiSpatialSampleCountFromConfidence(desc);
    params.numDisocclusionBoostSamples = desc.numDisocclusionBoostSamples;
    params.samplingRadius = desc.samplingRadius;
    params.biasCorrectionMode = min(desc.biasCorrectionMode, uint(RTXDI_BIAS_CORRECTION_BASIC));
    params.depthThreshold = desc.depthThreshold;
    params.normalThreshold = desc.normalThreshold;
    params.targetHistoryLength = desc.targetHistoryLength;
    params.enableMaterialSimilarityTest = desc.enableMaterialSimilarityTest;
    params.discountNaiveSamples = desc.discountNaiveSamples;
    return params;
}

RemixRtxdiSpatialReuseResult RemixRtxdiRunSpatialReuse(
    RAB_Surface currentSurface,
    RemixRtxdiSpatialReuseDesc desc)
{
    RemixRtxdiSpatialReuseResult result = (RemixRtxdiSpatialReuseResult)0;
    result.reservoir = RTXDI_EmptyDIReservoir();
    result.selectedLightSample = RAB_EmptyLightSample();

    if (!RAB_IsSurfaceValid(currentSurface))
    {
        RAB_StoreReservoir(result.reservoir, int2(desc.pixel), int(desc.spatialOutputPage));
        return result;
    }

    RTXDI_DIReservoir centerSample = RAB_LoadReservoir(int2(desc.pixel), int(desc.spatialInputPage));

    RTXDI_RandomSamplerState rng = RTXDI_InitRandomSampler(
        desc.pixel,
        desc.frameIndex,
        REMIX_RTXDI_SPATIAL_RNG_PASS);

    result.reservoir = RTXDI_DISpatialResampling(
        desc.pixel,
        currentSurface,
        centerSample,
        rng,
        RemixRtxdiSpatialRuntimeParameters(desc),
        RemixRAB_DIReservoirParams,
        desc.spatialInputPage,
        RemixRtxdiSpatialParameters(desc),
        result.selectedLightSample);

    RAB_StoreReservoir(result.reservoir, int2(desc.pixel), int(desc.spatialOutputPage));
    return result;
}

#endif
