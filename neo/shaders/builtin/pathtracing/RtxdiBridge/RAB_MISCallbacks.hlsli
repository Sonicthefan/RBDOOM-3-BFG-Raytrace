#ifndef RB_PATH_TRACING_RAB_MIS_CALLBACKS_HLSLI
#define RB_PATH_TRACING_RAB_MIS_CALLBACKS_HLSLI

#include "Rtxdi/Utils/BrdfRaySample.hlsli"
#include "RAB_LightSamplingCore.hlsli"
#include "RAB_RayPayload.hlsli"
#include "RAB_LocalLightPdf.hlsli"

uint RAB_FindCurrentEmissiveLightIndexForRayPayload(RAB_RayPayload rayPayload)
{
    const uint emissiveTriangleCount = RAB_GetCurrentEmissiveTriangleCount();
    [loop]
    for (uint lightIndex = 0u; lightIndex < emissiveTriangleCount; ++lightIndex)
    {
        const PathTraceSmokeEmissiveTriangle emissiveTriangle = SmokeEmissiveTriangles[lightIndex];
        if (emissiveTriangle.instanceId == rayPayload.instanceId &&
            emissiveTriangle.primitiveIndex == rayPayload.primitiveId &&
            emissiveTriangle.materialId == rayPayload.materialId)
        {
            return lightIndex;
        }
    }

    return RAB_INVALID_LIGHT_INDEX;
}

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

    const uint emissiveTriangleCount = RAB_GetCurrentEmissiveTriangleCount();
    if (lightIndex >= emissiveTriangleCount)
    {
        return 1.0;
    }

    const float lightSourcePdf = RAB_EvaluateLocalLightSourcePdf(lightIndex) * lightSolidAnglePdf;
    const float neeSampleCount = (float)clamp((uint)max(EmissiveInfo.z, 1.0), 1u, 16u);
    const float neeTerm = neeSampleCount * lightSourcePdf;
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

    if (brs.properties.IsDelta() || brs.OutPdf <= 0.0)
    {
        return brs.OutPdf > 0.0 ? 1.0 : 0.0;
    }

    const uint lightIndex = RAB_FindCurrentEmissiveLightIndexForRayPayload(rayPayload);
    if (lightIndex == RAB_INVALID_LIGHT_INDEX)
    {
        return 1.0;
    }

    RAB_LightInfo lightInfo = RAB_LoadLightInfo(lightIndex, false);
    RAB_LightSample lightSample = RAB_SamplePolymorphicLight(lightInfo, prevSurface, rayPayload.barycentrics);
    if (lightSample.valid == 0u || lightSample.solidAnglePdf <= 0.0)
    {
        return 1.0;
    }

    const float lightSourcePdf = RAB_EvaluateLocalLightSourcePdf(lightIndex) * RAB_LightSampleSolidAnglePdf(lightSample);
    const float neeSampleCount = (float)clamp((uint)max(EmissiveInfo.z, 1.0), 1u, 16u);
    const float pdfSum = neeSampleCount * lightSourcePdf + brs.OutPdf;
    return brs.OutPdf / max(pdfSum, 1.0e-6);
}

float GetMISWeightForEnvironmentMap(float3 direction, RAB_Surface prevSurface, RTXDI_BrdfRaySample brs)
{
    // Environment lighting is intentionally unbridged; see pathtrace_smoke_rab_environment_stub.hlsli.
    return 1.0;
}

float RAB_EvaluateEnvironmentMapSamplingPdf(float3 direction)
{
    // Environment lighting is intentionally unbridged; see pathtrace_smoke_rab_environment_stub.hlsli.
    return 0.0;
}

float2 RAB_GetEnvironmentMapRandXYFromDir(float3 direction)
{
    const float3 dir = RAB_SafeNormalize(direction, float3(0.0, 0.0, 1.0));
    const float invTwoPi = 0.15915494309189535;
    const float invPi = 0.3183098861837907;
    const float u = atan2(dir.y, dir.x) * invTwoPi + 0.5;
    const float v = acos(clamp(dir.z, -1.0, 1.0)) * invPi;
    return frac(float2(u, v));
}

#endif
