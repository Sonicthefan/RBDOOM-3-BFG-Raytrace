// Purpose:
//     Supplies rbdoom data for RTXDI/RAB path-tracer MIS weight callbacks.
//
// NVIDIA reference:
//     E:\prog\references\RTXDI-main\Samples\FullSample\Shaders\LightingPasses\RtxdiApplicationBridge\PathTracer\RAB_MISCallbacks.hlsli
//     E:\prog\references\RTXDI-main\Samples\FullSample\Shaders\LightingPasses\RtxdiApplicationBridge\RAB_MISCallbacks.hlsli
//     E:\prog\references\RTXDI-main\Samples\FullSample\Shaders\LightingPasses\RtxdiApplicationBridge\RAB_LightSampling.hlsli
//
// Current rbdoom supplier:
//     Local-light source PDF helper, emissive hit-to-light helper, environment
//     sampling stubs, RAB light loading/sampling, and BRDF ray samples.
//
// Current deviation:
//     rbdoom does not supply the full reference lightSamplingMode,
//     RTXDI_DIInitialSamplingParameters, local-light PDF texture, or
//     environment-light contract here.
//
// Extraction rule:
//     This file must preserve existing behavior until a later correction task.

#ifndef RB_PATH_TRACING_RAB_PATH_TRACER_MIS_WEIGHTS_HLSLI
#define RB_PATH_TRACING_RAB_PATH_TRACER_MIS_WEIGHTS_HLSLI

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

#endif
