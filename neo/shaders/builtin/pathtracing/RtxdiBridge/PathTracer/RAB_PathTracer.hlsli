#ifndef RB_PATH_TRACING_RAB_PATH_TRACER_HLSLI
#define RB_PATH_TRACING_RAB_PATH_TRACER_HLSLI

#ifndef RTXDI_RESTIR_PT_INITIAL_SAMPLING
#define RTXDI_RESTIR_PT_INITIAL_SAMPLING
#define RB_RESTIR_PT_PATH_TRACER_DEFINED_INITIAL_SAMPLING 1
#endif

#include "../PathTraceRtxdiBridge.hlsli"
#include "Rtxdi/PT/PathTracerContext.hlsli"
#include "Rtxdi/PT/PathTracerRandomContext.hlsli"
#include "RAB_PathTracerUserData.hlsli"

RAB_RayPayload RAB_BuildRayPayloadFromSmokePayload(PathTraceSmokePayload payload, float3 rayDirection)
{
    RAB_RayPayload rayPayload = RAB_EmptyRayPayload();
    rayPayload.hit = payload.value != 0u ? 1u : 0u;
    rayPayload.hitT = payload.hitT;
    rayPayload.instanceId = payload.instanceId;
    rayPayload.primitiveId = payload.primitiveIndex;
    rayPayload.barycentrics = float2(0.0, 0.0);
    rayPayload.materialId = payload.materialId;
    rayPayload.materialIndex = payload.materialIndex;
    rayPayload.surfaceClass = payload.surfaceClass;
    rayPayload.flags = payload.triangleClassAndFlags;
    rayPayload.frontFace = dot(payload.geometricNormal, -rayDirection) >= 0.0 ? 1u : 0u;
    return rayPayload;
}

RTXDI_BrdfRaySample RAB_SampleSmokeBrdfForPathTrace(RAB_Surface surface, inout RTXDI_RandomSamplerState rng)
{
    RTXDI_BrdfRaySample brdfSample = RTXDI_EmptyBrdfRaySample();

    RAB_RandomSamplerState rabRng = RAB_CreateRandomSamplerFromDirectSeed(rng.seed, rng.index);
    float3 outDir = float3(0.0, 0.0, 0.0);
    const bool validDirection = RAB_GetSurfaceBrdfSample(surface, rabRng, outDir);
    rng.seed = rabRng.seed;
    rng.index = rabRng.index;

    if (!validDirection)
    {
        return brdfSample;
    }

    const float outPdf = RAB_GetSurfaceBrdfPdf(surface, outDir);
    if (outPdf <= 1.0e-6)
    {
        return brdfSample;
    }

    const float3 normal = RAB_SafeNormalize(RAB_GetSurfaceNormal(surface), RAB_GetSurfaceGeoNormal(surface));
    const float ndotOut = saturate(dot(normal, outDir));
    brdfSample.OutDirection = outDir;
    brdfSample.OutPdf = outPdf;
    brdfSample.BrdfTimesNoL = RAB_EvaluateSurfaceBrdf(surface, outDir, RAB_GetSurfaceViewDir(surface)) * ndotOut;
    brdfSample.properties = RTXDI_DefaultBrdfRaySampleProperties();
    brdfSample.properties.SetContinuous();
    brdfSample.properties.SetDiffuse();
    brdfSample.properties.SetReflection();
    return brdfSample;
}

RayDesc RAB_SetupSmokeContinuationRay(RAB_Surface surface, float3 direction)
{
    RayDesc ray;
    const float3 normal = RAB_SafeNormalize(RAB_GetSurfaceNormal(surface), RAB_GetSurfaceGeoNormal(surface));
    const float normalOffsetSign = dot(normal, direction) >= 0.0 ? 1.0 : -1.0;
    ray.Origin = RAB_GetSurfaceWorldPos(surface) + normal * (normalOffsetSign * 0.75) + direction * 0.25;
    ray.Direction = RAB_SafeNormalize(direction, normal);
    ray.TMin = 0.01;
    ray.TMax = CameraOriginAndTMax.w;
    return ray;
}

PathTraceSmokePayload RAB_TraceSmokePathRay(RayDesc ray)
{
    PathTraceSmokePayload payload = InitSmokePayload();
    TraceRay(SmokeScene, RAY_FLAG_NONE, 0xff, 0, 1, 0, ray, payload);
    return payload;
}

bool RAB_RecordSmokeNeeSample(inout RTXDI_PathTracerContext ctx, RAB_Surface surface, inout RTXDI_PathTracerRandomContext ptRandContext)
{
    const uint lightCount = RAB_GetCurrentLightCount();
    if (lightCount == 0u || !RAB_SurfaceSupportsOpaqueDiffuseBrdf(surface))
    {
        return false;
    }

    const uint emissiveTriangleCount = (uint)max(EmissiveInfo.x, 0.0);
    const uint uploadedAnalyticCount = (uint)max(DoomAnalyticLightInfo.x, 0.0);
    const uint analyticTraceCap = (uint)max(DoomAnalyticLightInfo.y, 0.0);
    const uint analyticCount = DoomAnalyticLightInfo.w >= 0.5 ? min(uploadedAnalyticCount, analyticTraceCap) : 0u;

    uint selectedLightIndex = RAB_INVALID_LIGHT_INDEX;
    RAB_LightSample selectedLightSample = RAB_EmptyLightSample();
    float selectedLightPdf = 0.0;
    float selectedScore = 0.0;

    if (emissiveTriangleCount > 0u)
    {
        const uint trialCount = clamp((uint)max(EmissiveInfo.z, 1.0), 1u, 16u);
        [loop]
        for (uint trialIndex = 0u; trialIndex < trialCount; ++trialIndex)
        {
            const float xi = RTXDI_GetNextRandom(ptRandContext.initialRandomSamplerState);
            const uint trialLightIndex = SelectSmokeWeightedEmissiveTriangle(emissiveTriangleCount, xi);
            const PathTraceSmokeEmissiveTriangle emissiveTriangle = SmokeEmissiveTriangles[trialLightIndex];
            const RAB_LightInfo lightInfo = RAB_LoadLightInfo(trialLightIndex, false);
            const RAB_LightSample lightSample = RAB_SamplePolymorphicLight(lightInfo, surface, float2(0.5, 0.5));
            const float score = RAB_GetLightSampleTargetPdfForSurface(lightSample, surface);
            if (score > selectedScore)
            {
                selectedScore = score;
                selectedLightIndex = trialLightIndex;
                selectedLightSample = lightSample;
                selectedLightPdf = max(emissiveTriangle.sampleWeightAndPdf.y, 1.0 / max((float)emissiveTriangleCount, 1.0));
            }
        }
    }

    if (analyticCount > 0u)
    {
        const uint analyticTrialCount = min(analyticCount, 8u);
        [loop]
        for (uint trialIndex = 0u; trialIndex < analyticTrialCount; ++trialIndex)
        {
            const float xi = RTXDI_GetNextRandom(ptRandContext.initialRandomSamplerState);
            const uint analyticIndex = min((uint)(xi * (float)analyticCount), analyticCount - 1u);
            const uint trialLightIndex = emissiveTriangleCount + analyticIndex;
            const float2 sampleUv = float2(
                RTXDI_GetNextRandom(ptRandContext.initialRandomSamplerState),
                RTXDI_GetNextRandom(ptRandContext.initialRandomSamplerState));
            const RAB_LightInfo lightInfo = RAB_LoadLightInfo(trialLightIndex, false);
            const RAB_LightSample lightSample = RAB_SamplePolymorphicLight(lightInfo, surface, sampleUv);
            const float score = RAB_GetLightSampleTargetPdfForSurface(lightSample, surface);
            if (score > selectedScore)
            {
                selectedScore = score;
                selectedLightIndex = trialLightIndex;
                selectedLightSample = lightSample;
                selectedLightPdf = 1.0 / max((float)analyticCount, 1.0);
            }
        }
    }

    if (selectedLightIndex == RAB_INVALID_LIGHT_INDEX || selectedLightSample.valid == 0u || selectedLightSample.solidAnglePdf <= 1.0e-6 || !RAB_GetConservativeVisibility(surface, selectedLightSample))
    {
        return false;
    }

    float3 lightDir;
    float lightDistance;
    RAB_GetLightDirDistance(surface, selectedLightSample, lightDir, lightDistance);
    const float3 normal = RAB_SafeNormalize(RAB_GetSurfaceNormal(surface), RAB_GetSurfaceGeoNormal(surface));
    const float ndotl = saturate(dot(normal, lightDir));
    if (ndotl <= 0.0)
    {
        return false;
    }

    const float scatterPdf = RAB_GetSurfaceBrdfPdf(surface, lightDir);
    if (scatterPdf <= 1.0e-6)
    {
        return false;
    }

    const float neePdf = max(selectedLightSample.solidAnglePdf * max(selectedLightPdf, 1.0e-6), 1.0e-6);
    const float3 reflectedRadiance = RAB_EvaluateSurfaceBrdf(surface, lightDir, RAB_GetSurfaceViewDir(surface)) * selectedLightSample.radiance * ndotl;
    const float3 radianceOverPdf = reflectedRadiance / neePdf;
    if (RAB_Luminance(radianceOverPdf) <= 0.0)
    {
        return false;
    }

    RTXDI_SampledLightData sampledLightData = RTXDI_SampledLightData_CreateInvalidData();
    RTXDI_SampledLightData_SetLightData(sampledLightData, selectedLightIndex);
    RTXDI_SampledLightData_SetUVData(sampledLightData, float2(0.5, 0.5));

    return ctx.RecordNeeLightSample(
        sampledLightData,
        radianceOverPdf,
        neePdf,
        scatterPdf,
        selectedLightSample,
        ptRandContext.initialRandomSamplerState);
}

void RAB_PathTrace(inout RTXDI_PathTracerContext ctx, inout RTXDI_PathTracerRandomContext ptRandContext, inout RAB_PathTracerUserData ptud)
{
    const uint maxBridgeBounces = min(ctx.GetMaxPathBounce(), 3u);

    [loop]
    while (ctx.GetBounceDepth() <= maxBridgeBounces)
    {
        ctx.BeginPathState();

        RAB_Surface currentSurface = ctx.GetIntersectionSurface();
        if (!RAB_IsSurfaceValid(currentSurface))
        {
            break;
        }

        if (ctx.ShouldSampleNee())
        {
            RAB_RecordSmokeNeeSample(ctx, currentSurface, ptRandContext);
        }

        RTXDI_BrdfRaySample brdfSample = RAB_SampleSmokeBrdfForPathTrace(currentSurface, ptRandContext.replayRandomSamplerState);
        if (brdfSample.OutPdf <= 1.0e-6 || !all(brdfSample.BrdfTimesNoL == brdfSample.BrdfTimesNoL))
        {
            break;
        }

        ctx.SetBrdfRaySample(brdfSample);
        if (!ctx.ValidContinuationRayBrdfOverPdf())
        {
            break;
        }

        ctx.MultiplyPathThroughput(ctx.GetContinuationRayBrdfOverPdf());
        ctx.SetContinuationRay(RAB_SetupSmokeContinuationRay(currentSurface, brdfSample.OutDirection));

        if (!ctx.AnalyzePathReconnectibilityBeforeTrace())
        {
            break;
        }

        const RayDesc continuationRay = ctx.GetContinuationRay();
        PathTraceSmokePayload smokePayload = RAB_TraceSmokePathRay(continuationRay);
        RAB_RayPayload rayPayload = RAB_BuildRayPayloadFromSmokePayload(smokePayload, continuationRay.Direction);
        ctx.SetTraceResult(rayPayload);

        if (!RAB_IsRayPayloadHit(rayPayload))
        {
            ctx.RecordPathRadianceMiss(ptRandContext.initialRandomSamplerState);
            break;
        }

        RAB_Surface nextSurface = RAB_BuildSurfaceFromSmokePayload(smokePayload, continuationRay.Origin, continuationRay.Direction, true);
        if (!RAB_IsSurfaceValid(nextSurface))
        {
            break;
        }

        RAB_Surface previousSurface = currentSurface;
        ctx.RecordPathIntersection(nextSurface);

        if (ctx.ShouldSampleEmissiveSurfaces() && RAB_Luminance(nextSurface.material.emissiveRadiance) > 0.0)
        {
            ctx.RecordEmissiveLightSample(nextSurface.material.emissiveRadiance, previousSurface, ptRandContext.initialRandomSamplerState);
        }

        ctx.IncreaseBounceDepth();
    }
}

#endif
