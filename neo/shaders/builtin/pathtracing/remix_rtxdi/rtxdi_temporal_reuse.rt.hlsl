#ifndef RB_PATH_TRACING_REMIX_RTXDI_TEMPORAL_REUSE_RT_HLSL
#define RB_PATH_TRACING_REMIX_RTXDI_TEMPORAL_REUSE_RT_HLSL

// RTX Remix-shaped DI temporal reuse contract.
//
// Temporal math is delegated to RTXDI_DITemporalResampling; this file owns the
// Remix-shaped DI temporal reuse boundary and can be included either by the
// standalone Remix contract shader or by the active rbdoom mode-56 dispatch.

#ifndef RTXDI_ALLOWED_BIAS_CORRECTION
#define RTXDI_ALLOWED_BIAS_CORRECTION RTXDI_BIAS_CORRECTION_BASIC
#endif

#ifndef REMIX_RTXDI_TEMPORAL_REUSE_EXTERNAL_BINDINGS
#include "../remix_bridge/RAB_SurfaceBridge.hlsli"
#define REMIX_RAB_EXPORT_RAB_NAMES 1
#include "../remix_bridge/RAB_LightBridge.hlsli"
#include "../remix_bridge/RAB_LightSamplingBridge.hlsli"
#include "../remix_bridge/RAB_ReservoirBridge.hlsli"
#include "../remix_bridge/RAB_TemporalOutputBridge.hlsli"
#endif

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

#ifndef REMIX_RTXDI_TEMPORAL_REUSE_EXTERNAL_BINDINGS
struct RemixRtxdiTemporalReuseResult
{
    RTXDI_DIReservoir reservoir;
    RAB_LightSample selectedLightSample;
    int2 temporalSamplePixel;
    RemixRAB_TemporalOutput temporalOutput;
};
#endif

struct RemixRtxdiTemporalReuseCoreResult
{
    RTXDI_DIReservoir reservoir;
    RAB_LightSample selectedLightSample;
    int2 temporalSamplePixel;
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

RemixRtxdiTemporalReuseCoreResult RemixRtxdiRunTemporalReuseCore(
    RAB_Surface currentSurface,
    RTXDI_DIReservoir currentReservoir,
    RemixRtxdiTemporalReuseDesc desc,
    RTXDI_ReservoirBufferParameters reservoirParams)
{
    RemixRtxdiTemporalReuseCoreResult result = (RemixRtxdiTemporalReuseCoreResult)0;
    result.reservoir = currentReservoir;
    result.selectedLightSample = RAB_EmptyLightSample();
    result.temporalSamplePixel = int2(-1, -1);

    if (!RAB_IsSurfaceValid(currentSurface))
    {
        return result;
    }

    RTXDI_RandomSamplerState rng = RTXDI_InitRandomSampler(
        desc.pixel,
        desc.frameIndex,
        REMIX_RTXDI_TEMPORAL_RNG_PASS);

    result.reservoir = RTXDI_DITemporalResampling(
        desc.pixel,
        currentSurface,
        currentReservoir,
        rng,
        RemixRtxdiTemporalRuntimeParameters(desc),
        reservoirParams,
        desc.screenSpaceMotion,
        desc.temporalInputPage,
        RemixRtxdiTemporalParameters(desc),
        result.temporalSamplePixel,
        result.selectedLightSample);

    return result;
}

#ifndef REMIX_RTXDI_TEMPORAL_REUSE_EXTERNAL_BINDINGS
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

    const RemixRtxdiTemporalReuseCoreResult coreResult = RemixRtxdiRunTemporalReuseCore(
        currentSurface,
        result.reservoir,
        desc,
        RemixRAB_DIReservoirParams);
    result.reservoir = coreResult.reservoir;
    result.selectedLightSample = coreResult.selectedLightSample;
    result.temporalSamplePixel = coreResult.temporalSamplePixel;

    result.temporalOutput = RemixRAB_BuildTemporalOutput(
        result.temporalSamplePixel,
        desc.reprojectionConfidenceHistoryLength);

    RemixRAB_StoreTemporalOutput(int2(desc.pixel), result.temporalOutput);
    RAB_StoreReservoir(result.reservoir, int2(desc.pixel), int(desc.temporalOutputPage));
    return result;
}
#endif

#endif
