#ifndef RB_PATH_TRACING_RAB_MIS_CALLBACKS_HLSLI
#define RB_PATH_TRACING_RAB_MIS_CALLBACKS_HLSLI

#include "Rtxdi/Utils/BrdfRaySample.hlsli"
#include "RAB_LightSampling.hlsli"
#include "RAB_RayPayload.hlsli"

float RAB_GetMISWeightForNEE(
    uint lightIndex,
    RAB_LightSample lightSample,
    float3 lightDirection,
    float lightSolidAnglePdf,
    float scatterPdf)
{
    if (lightIndex == RAB_INVALID_LIGHT_INDEX ||
        lightSample.valid == 0u ||
        lightSolidAnglePdf <= 0.0 ||
        scatterPdf <= 0.0 ||
        RAB_IsAnalyticLightSample(lightSample))
    {
        return 1.0;
    }

    const float neeTerm = lightSolidAnglePdf;
    return neeTerm / max(neeTerm + scatterPdf, 1.0e-6);
}

float GetMISWeightForNEELight(
    uint lightIndex,
    RAB_LightSample lightSample,
    float3 shadingWorldPos,
    float scatterPdf)
{
    const float3 lightDirection = RAB_SafeNormalize(lightSample.position - shadingWorldPos, float3(0.0, 0.0, 1.0));
    return RAB_GetMISWeightForNEE(lightIndex, lightSample, lightDirection, RAB_LightSampleSolidAnglePdf(lightSample), scatterPdf);
}

float GetMISWeightForEmissiveSurface(
    RAB_RayPayload rayPayload,
    RAB_Surface prevSurface,
    RTXDI_BrdfRaySample brs)
{
    if (!RAB_IsRayPayloadHit(rayPayload) || !RAB_IsSurfaceValid(prevSurface))
    {
        return 1.0;
    }

    return brs.OutPdf > 0.0 ? 1.0 : 0.0;
}

float GetMISWeightForEnvironmentMap(float3 direction, RAB_Surface prevSurface, RTXDI_BrdfRaySample brs)
{
    return 1.0;
}

float RAB_EvaluateEnvironmentMapSamplingPdf(float3 direction)
{
    return 0.0;
}

float2 RAB_GetEnvironmentMapRandXYFromDir(float3 direction)
{
    return float2(0.0, 0.0);
}

#endif
