#ifndef RB_PATH_TRACING_REMIX_RTXDI_TEMPORAL_REUSE_RT_HLSL
#define RB_PATH_TRACING_REMIX_RTXDI_TEMPORAL_REUSE_RT_HLSL

// RTX Remix-shaped DI temporal reuse contract.
//
// This file is intentionally inactive until a later dispatch/binding slice
// supplies real surface, motion-vector, light, reservoir, and clear bindings.
// Temporal math is delegated to RTXDI_DITemporalResampling; this file only
// wires rbdoom's Remix bridge contracts into that helper.

#define RTXDI_ALLOWED_BIAS_CORRECTION RTXDI_BIAS_CORRECTION_BASIC

#include "../remix_bridge/RAB_SurfaceBridge.hlsli"
#define REMIX_RAB_EXPORT_RAB_NAMES 1
#include "../remix_bridge/RAB_LightBridge.hlsli"
#include "../remix_bridge/RAB_LightSamplingBridge.hlsli"
#include "../remix_bridge/RAB_ReservoirBridge.hlsli"
#include "../remix_bridge/RAB_TemporalOutputBridge.hlsli"

#include "Rtxdi/DI/TemporalResampling.hlsli"

static const uint REMIX_RTXDI_TEMPORAL_RNG_PASS = 0x52525806u;

struct RemixRtxdiTemporalReuseDesc
{
    uint2 pixel;
    uint frameIndex;
    float3 screenSpaceMotion;
    uint temporalInputPage;
    uint temporalOutputPage;
    uint activeCheckerboardField;
    uint maxHistoryLength;
    uint biasCorrectionMode;
    float depthThreshold;
    float normalThreshold;
    uint enablePermutationSampling;
    uint uniformRandomNumber;
    uint reprojectionConfidenceHistoryLength;
};

struct RemixRtxdiTemporalReuseResult
{
    RTXDI_DIReservoir reservoir;
    RAB_LightSample selectedLightSample;
    int2 temporalSamplePixel;
    RemixRAB_TemporalOutput temporalOutput;
};

RTXDI_RuntimeParameters RemixRtxdiTemporalRuntimeParameters(RemixRtxdiTemporalReuseDesc desc)
{
    RTXDI_RuntimeParameters params = (RTXDI_RuntimeParameters)0;
    params.activeCheckerboardField = desc.activeCheckerboardField;
    return params;
}

RTXDI_DITemporalResamplingParameters RemixRtxdiTemporalParameters(RemixRtxdiTemporalReuseDesc desc)
{
    RTXDI_DITemporalResamplingParameters params = (RTXDI_DITemporalResamplingParameters)0;
    params.maxHistoryLength = desc.maxHistoryLength;
    params.biasCorrectionMode = desc.biasCorrectionMode;
    params.depthThreshold = desc.depthThreshold;
    params.normalThreshold = desc.normalThreshold;
    params.enableVisibilityShortcut = 0u;
    params.enablePermutationSampling = desc.enablePermutationSampling;
    params.uniformRandomNumber = desc.uniformRandomNumber;
    params.permutationSamplingThreshold = 0.0;
    return params;
}

RemixRtxdiTemporalReuseResult RemixRtxdiRunTemporalReuse(
    RAB_Surface currentSurface,
    RemixRtxdiTemporalReuseDesc desc)
{
    RemixRtxdiTemporalReuseResult result = (RemixRtxdiTemporalReuseResult)0;
    result.reservoir = RAB_LoadReservoir(int2(desc.pixel), int(desc.temporalOutputPage));
    result.selectedLightSample = RAB_EmptyLightSample();
    result.temporalSamplePixel = int2(-1, -1);
    result.temporalOutput = RemixRAB_BuildTemporalOutput(
        REMIX_RAB_INVALID_TEMPORAL_SAMPLE_PIXEL,
        desc.reprojectionConfidenceHistoryLength);

    if (!RAB_IsSurfaceValid(currentSurface))
    {
        RemixRAB_StoreTemporalOutput(int2(desc.pixel), result.temporalOutput);
        RAB_StoreReservoir(result.reservoir, int2(desc.pixel), int(desc.temporalOutputPage));
        return result;
    }

    RTXDI_RandomSamplerState rng = RTXDI_InitRandomSampler(
        desc.pixel,
        desc.frameIndex,
        REMIX_RTXDI_TEMPORAL_RNG_PASS);

    result.reservoir = RTXDI_DITemporalResampling(
        desc.pixel,
        currentSurface,
        result.reservoir,
        rng,
        RemixRtxdiTemporalRuntimeParameters(desc),
        RemixRAB_DIReservoirParams,
        desc.screenSpaceMotion,
        desc.temporalInputPage,
        RemixRtxdiTemporalParameters(desc),
        result.temporalSamplePixel,
        result.selectedLightSample);

    result.temporalOutput = RemixRAB_BuildTemporalOutput(
        result.temporalSamplePixel,
        desc.reprojectionConfidenceHistoryLength);

    RemixRAB_StoreTemporalOutput(int2(desc.pixel), result.temporalOutput);
    RAB_StoreReservoir(result.reservoir, int2(desc.pixel), int(desc.temporalOutputPage));
    return result;
}

#endif
