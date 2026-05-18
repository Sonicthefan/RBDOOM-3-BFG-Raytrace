// Purpose:
//     Debug-only LU-02A feed from rbdoom's unified local-light universe into
//     RTXDI_SampleLightsForSurface. This does not replace the path tracer NEE
//     record path and does not write DI/PT reservoirs.

#ifndef RB_PATH_TRACING_RESTIR_DI_INITIAL_DEBUG_HLSLI
#define RB_PATH_TRACING_RESTIR_DI_INITIAL_DEBUG_HLSLI

#undef RTXDI_ENABLE_PRESAMPLING
#define RTXDI_ENABLE_PRESAMPLING 0
#include "Rtxdi/DI/InitialSampling.hlsli"

struct RestirPTDiInitialDebugResult
{
    RTXDI_DIReservoir reservoir;
    RAB_LightSample lightSample;
    uint status;
};

static const uint RESTIR_PT_DI_INITIAL_STATUS_VALID = 0u;
static const uint RESTIR_PT_DI_INITIAL_STATUS_DISABLED = 1u;
static const uint RESTIR_PT_DI_INITIAL_STATUS_INVALID_SURFACE = 2u;
static const uint RESTIR_PT_DI_INITIAL_STATUS_NO_LIGHTS = 3u;
static const uint RESTIR_PT_DI_INITIAL_STATUS_INVALID_RESERVOIR = 4u;
static const uint RESTIR_PT_DI_INITIAL_STATUS_INVALID_SAMPLE = 5u;
static const uint RESTIR_PT_DI_INITIAL_DEBUG_LOCAL_SAMPLE_COUNT = 32u;

bool RestirPTDiInitialDebugEnabled()
{
    return RAB_UnifiedLightLoadEnabled() && RAB_UnifiedLightSampleEnabled();
}

float4 RestirPTDiInitialDebugStatusColor(uint status)
{
    if (status == RESTIR_PT_DI_INITIAL_STATUS_DISABLED)
    {
        return float4(0.85, 0.35, 0.04, 1.0);
    }
    if (status == RESTIR_PT_DI_INITIAL_STATUS_INVALID_SURFACE)
    {
        return float4(0.0, 0.0, 0.0, 1.0);
    }
    if (status == RESTIR_PT_DI_INITIAL_STATUS_NO_LIGHTS)
    {
        return float4(0.25, 0.02, 0.35, 1.0);
    }
    if (status == RESTIR_PT_DI_INITIAL_STATUS_INVALID_RESERVOIR)
    {
        return float4(0.45, 0.02, 0.02, 1.0);
    }
    if (status == RESTIR_PT_DI_INITIAL_STATUS_INVALID_SAMPLE)
    {
        return float4(0.85, 0.85, 0.85, 1.0);
    }
    return float4(0.05, 0.75, 0.12, 1.0);
}

RTXDI_DIInitialSamplingParameters RestirPTDiInitialDebugSamplingParameters(bool enableVisibility)
{
    RTXDI_DIInitialSamplingParameters sampleParams = (RTXDI_DIInitialSamplingParameters)0;
    sampleParams.numLocalLightSamples = min(RAB_GetCurrentUnifiedLightCount(), RESTIR_PT_DI_INITIAL_DEBUG_LOCAL_SAMPLE_COUNT);
    sampleParams.numInfiniteLightSamples = 0u;
    sampleParams.numEnvironmentSamples = 0u;
    sampleParams.numBrdfSamples = 0u;
    sampleParams.brdfCutoff = 0.0;
    sampleParams.brdfRayMinT = 0.0;
    sampleParams.localLightSamplingMode = ReSTIRDI_LocalLightSamplingMode_UNIFORM;
    sampleParams.enableInitialVisibility = enableVisibility ? 1u : 0u;
    sampleParams.environmentMapImportanceSampling = 0u;
    return sampleParams;
}

RTXDI_LightBufferParameters RestirPTDiInitialDebugLightBufferParameters()
{
    RTXDI_LightBufferParameters lightBufferParams = (RTXDI_LightBufferParameters)0;
    lightBufferParams.localLightBufferRegion.firstLightIndex = 0u;
    lightBufferParams.localLightBufferRegion.numLights = RAB_GetCurrentUnifiedLightCount();
    lightBufferParams.infiniteLightBufferRegion.firstLightIndex = 0u;
    lightBufferParams.infiniteLightBufferRegion.numLights = 0u;
    lightBufferParams.environmentLightParams.lightPresent = 0u;
    lightBufferParams.environmentLightParams.lightIndex = RAB_INVALID_LIGHT_INDEX;
    return lightBufferParams;
}

RestirPTDiInitialDebugResult RestirPTDiInitialDebugSample(RAB_Surface surface, uint2 pixel, bool enableVisibility)
{
    RestirPTDiInitialDebugResult result = (RestirPTDiInitialDebugResult)0;
    result.reservoir = RTXDI_EmptyDIReservoir();
    result.lightSample = RAB_EmptyLightSample();
    result.status = RESTIR_PT_DI_INITIAL_STATUS_VALID;

    if (!RestirPTDiInitialDebugEnabled())
    {
        result.status = RESTIR_PT_DI_INITIAL_STATUS_DISABLED;
        return result;
    }
    if (!RAB_IsSurfaceValid(surface) || !RAB_SurfaceSupportsOpaqueDiffuseBrdf(surface))
    {
        result.status = RESTIR_PT_DI_INITIAL_STATUS_INVALID_SURFACE;
        return result;
    }
    if (RAB_GetCurrentUnifiedLightCount() == 0u)
    {
        result.status = RESTIR_PT_DI_INITIAL_STATUS_NO_LIGHTS;
        return result;
    }

    const uint frameIndex = (uint)max(RestirPTInfo.x, 0.0);
    RTXDI_RandomSamplerState rng = RTXDI_InitRandomSampler(pixel, frameIndex, 0x6c753241u);
    RTXDI_RandomSamplerState coherentRng = RTXDI_InitRandomSampler(pixel / RTXDI_TILE_SIZE_IN_PIXELS, frameIndex, 0x6c753241u);
    result.reservoir = RTXDI_SampleLightsForSurface(
        rng,
        coherentRng,
        surface,
        RestirPTDiInitialDebugSamplingParameters(enableVisibility),
        RestirPTDiInitialDebugLightBufferParameters(),
        result.lightSample);

    if (!RTXDI_IsValidDIReservoir(result.reservoir) || result.reservoir.weightSum <= 0.0)
    {
        result.status = RESTIR_PT_DI_INITIAL_STATUS_INVALID_RESERVOIR;
        return result;
    }
    if (result.lightSample.valid == 0u || result.lightSample.solidAnglePdf <= 0.0)
    {
        result.status = RESTIR_PT_DI_INITIAL_STATUS_INVALID_SAMPLE;
        return result;
    }
    return result;
}

float3 RestirPTDiInitialDebugContribution(RAB_Surface surface, RestirPTDiInitialDebugResult result)
{
    if (result.status != RESTIR_PT_DI_INITIAL_STATUS_VALID)
    {
        return float3(0.0, 0.0, 0.0);
    }

    const float3 reflectedRadiance = RAB_GetReflectedBsdfRadianceForSurface(
        result.lightSample.position,
        result.lightSample.radiance,
        surface);
    return RestirPTSanitizePreviewContribution(
        reflectedRadiance *
        RTXDI_GetDIReservoirInvPdf(result.reservoir) /
        max(result.lightSample.solidAnglePdf, 1.0e-6));
}

float4 EvaluateRestirPTDiInitialSampleValidityView(RAB_Surface surface, uint2 pixel)
{
    const RestirPTDiInitialDebugResult result = RestirPTDiInitialDebugSample(surface, pixel, false);
    if (result.status != RESTIR_PT_DI_INITIAL_STATUS_VALID)
    {
        return RestirPTDiInitialDebugStatusColor(result.status);
    }

    const uint lightIndex = RTXDI_GetDIReservoirLightIndex(result.reservoir);
    const RAB_LightInfo lightInfo = RAB_LoadLightInfo(lightIndex, false);
    if (!RAB_IsLightInfoValid(lightInfo))
    {
        return float4(0.85, 0.85, 0.85, 1.0);
    }
    return float4(RestirPTDebugUnifiedLightTypeColor(lightInfo.unifiedLightType), 1.0);
}

float4 EvaluateRestirPTDiInitialContributionView(RAB_Surface surface, uint2 pixel)
{
    const RestirPTDiInitialDebugResult result = RestirPTDiInitialDebugSample(surface, pixel, false);
    if (result.status != RESTIR_PT_DI_INITIAL_STATUS_VALID)
    {
        return result.status == RESTIR_PT_DI_INITIAL_STATUS_DISABLED ?
            RestirPTDiInitialDebugStatusColor(result.status) :
            float4(0.0, 0.0, 0.0, 1.0);
    }

    return float4(RestirPTToneMapPreview(RestirPTDiInitialDebugContribution(surface, result)), 1.0);
}

float4 EvaluateRestirPTDiInitialNumericView(RAB_Surface surface, uint2 pixel)
{
    const RestirPTDiInitialDebugResult result = RestirPTDiInitialDebugSample(surface, pixel, false);
    if (result.status != RESTIR_PT_DI_INITIAL_STATUS_VALID)
    {
        return RestirPTDiInitialDebugStatusColor(result.status);
    }

    const float targetHeat = saturate(max(result.reservoir.targetPdf, 0.0) / (1.0 + max(result.reservoir.targetPdf, 0.0)));
    const float mHeat = saturate(max(result.reservoir.M, 0.0));
    const float invPdfHeat = saturate(max(result.reservoir.weightSum, 0.0) / (1.0 + max(result.reservoir.weightSum, 0.0)));
    return float4(targetHeat, mHeat, invPdfHeat, 1.0);
}

float4 EvaluateRestirPTDiInitialVisibilityView(RAB_Surface surface, uint2 pixel)
{
    const RestirPTDiInitialDebugResult unoccludedResult = RestirPTDiInitialDebugSample(surface, pixel, false);
    if (unoccludedResult.status != RESTIR_PT_DI_INITIAL_STATUS_VALID)
    {
        return RestirPTDiInitialDebugStatusColor(unoccludedResult.status);
    }

    const bool directVisible = RAB_GetConservativeVisibility(surface, unoccludedResult.lightSample);
    const RestirPTDiInitialDebugResult visibilityResult = RestirPTDiInitialDebugSample(surface, pixel, true);
    const bool reservoirKeptSample = visibilityResult.status == RESTIR_PT_DI_INITIAL_STATUS_VALID;
    if (directVisible && reservoirKeptSample)
    {
        return float4(0.05, 0.75, 0.12, 1.0);
    }
    if (!directVisible && !reservoirKeptSample)
    {
        return float4(0.45, 0.02, 0.02, 1.0);
    }
    return directVisible ? float4(0.85, 0.85, 0.05, 1.0) : float4(0.85, 0.05, 0.85, 1.0);
}

#undef RTXDI_ENABLE_PRESAMPLING
#define RTXDI_ENABLE_PRESAMPLING 1

#endif
