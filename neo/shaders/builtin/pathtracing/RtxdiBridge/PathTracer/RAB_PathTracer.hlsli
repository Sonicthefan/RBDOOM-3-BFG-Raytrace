#ifndef RB_PATH_TRACING_RAB_PATH_TRACER_HLSLI
#define RB_PATH_TRACING_RAB_PATH_TRACER_HLSLI

#ifndef RTXDI_RESTIR_PT_INITIAL_SAMPLING
#define RTXDI_RESTIR_PT_INITIAL_SAMPLING
#define RB_RESTIR_PT_PATH_TRACER_DEFINED_INITIAL_SAMPLING 1
#endif

#include "../PathTraceRtxdiBridgeCore.hlsli"
#include "../RAB_Brdf.hlsli"
#include "../RAB_LightInfo.hlsli"
#include "../RAB_LightSamplingCore.hlsli"
#include "../RAB_MISCallbacks.hlsli"
#include "../RAB_Visibility.hlsli"
#include "Rtxdi/PT/PathTracerContext.hlsli"
#include "Rtxdi/PT/PathTracerRandomContext.hlsli"
#include "RAB_PathTracerUserData.hlsli"

RAB_RayPayload RAB_BuildRayPayloadFromSmokePayload(PathTraceSmokePayload payload, float3 rayDirection)
{
    RAB_RayPayload rayPayload = RAB_EmptyRayPayload();
    rayPayload.hit = payload.value != 0u ? 1u : 0u;
    rayPayload.hitT = payload.hitT;
    rayPayload.instanceId = payload.instanceId;
    rayPayload.geometryIndex = payload.geometryIndex;
    rayPayload.primitiveId = payload.primitiveIndex;
    rayPayload.barycentrics = payload.hitBarycentrics;
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
    if (PathTraceSafetyDisabled(RT_PT_SAFETY_DISABLE_DIFFUSE_SECONDARY_RAY))
    {
        return payload;
    }
    TraceRay(SmokeScene, RAY_FLAG_NONE, 0xff, 0, 1, 0, ray, payload);
    return payload;
}

struct RAB_SmokeNeeRisSelection
{
    uint lightIndex;
    RAB_LightSample lightSample;
    float2 lightUv;
    float scatterPdf;
    float effectivePdf;
    float risWeight;
    float risWeightSum;
    float3 reflectedRadiance;
    float3 radianceOverPdf;
    uint candidateCount;
};

// Local NEE candidate selection uses a small RIS stream. It must not keep the
// max-scoring analytic candidate and then report the raw uniform light PDF:
// once candidate selection depends on target score, the recorded sample needs
// the RIS estimator weight folded into radianceOverPdf and a matching effective
// scalar PDF for the path-tracer metadata.
RAB_SmokeNeeRisSelection RAB_EmptySmokeNeeRisSelection()
{
    RAB_SmokeNeeRisSelection selection = (RAB_SmokeNeeRisSelection)0;
    selection.lightIndex = RAB_INVALID_LIGHT_INDEX;
    selection.lightSample = RAB_EmptyLightSample();
    selection.lightUv = float2(0.5, 0.5);
    return selection;
}

void RAB_StreamSmokeNeeRisCandidate(
    RAB_Surface surface,
    uint lightIndex,
    float2 lightUv,
    float lightSelectionPdf,
    float domainAverageScale,
    inout RTXDI_PathTracerRandomContext ptRandContext,
    inout RAB_SmokeNeeRisSelection selection)
{
    const RAB_LightInfo lightInfo = RAB_LoadLightInfo(lightIndex, false);
    const RAB_LightSample lightSample = RAB_SamplePolymorphicLight(lightInfo, surface, lightUv);
    if (lightSelectionPdf <= 1.0e-6 || domainAverageScale <= 0.0 || !RAB_GetCandidateNeeVisibility(surface, lightSample))
    {
        return;
    }

    float3 lightDir;
    float lightDistance;
    RAB_GetLightDirDistance(surface, lightSample, lightDir, lightDistance);
    const float3 normal = RAB_SafeNormalize(RAB_GetSurfaceNormal(surface), RAB_GetSurfaceGeoNormal(surface));
    const float ndotl = saturate(dot(normal, lightDir));
    if (ndotl <= 0.0)
    {
        return;
    }

    const float scatterPdf = RAB_GetSurfaceBrdfPdf(surface, lightDir);
    const float proposalPdf = lightSample.solidAnglePdf * lightSelectionPdf;
    if (scatterPdf <= 1.0e-6 || proposalPdf <= 1.0e-6)
    {
        return;
    }

    const float3 reflectedRadiance = RAB_EvaluateSurfaceBrdf(surface, lightDir, RAB_GetSurfaceViewDir(surface)) * lightSample.radiance * ndotl;
    const float3 candidateRadianceOverPdf = reflectedRadiance * (domainAverageScale / proposalPdf);
    const float risWeight = RAB_Luminance(candidateRadianceOverPdf);
    if (risWeight <= 0.0)
    {
        return;
    }

    selection.candidateCount += 1u;
    selection.risWeightSum += risWeight;
    if (RTXDI_GetNextRandom(ptRandContext.initialRandomSamplerState) * selection.risWeightSum <= risWeight)
    {
        selection.lightIndex = lightIndex;
        selection.lightSample = lightSample;
        selection.lightUv = lightUv;
        selection.scatterPdf = scatterPdf;
        selection.effectivePdf = proposalPdf;
        selection.risWeight = risWeight;
        selection.reflectedRadiance = reflectedRadiance;
        selection.radianceOverPdf = candidateRadianceOverPdf;
    }
}

uint RAB_GetRestirPTAnalyticTrialCount(uint analyticCount)
{
    const uint requestedTrialCount = clamp((uint)max(MotionVectorInfo.z, 1.0), 1u, 128u);
    return min(analyticCount, requestedTrialCount);
}

bool RAB_SelectSmokeAnalyticNeeProposal(
    uint analyticCount,
    inout RTXDI_PathTracerRandomContext ptRandContext,
    out uint analyticIndex,
    out float proposalPdf)
{
    analyticIndex = 0u;
    proposalPdf = 0.0;
    if (analyticCount == 0u)
    {
        return false;
    }

    // The compact analytic list is sorted by portal depth/distance for upload
    // stability, not by per-surface light relevance. Biasing toward the first N
    // entries causes hard doorway discontinuities when lights reclassify between
    // portal depths. Use the full candidate domain until a real spatial light
    // proposal structure exists.
    const float xi = RTXDI_GetNextRandom(ptRandContext.initialRandomSamplerState);
    analyticIndex = min((uint)(xi * (float)analyticCount), analyticCount - 1u);
    proposalPdf = 1.0 / (float)analyticCount;
    return true;
}

bool RAB_RecordSmokeNeeSample(inout RTXDI_PathTracerContext ctx, RAB_Surface surface, inout RTXDI_PathTracerRandomContext ptRandContext)
{
    const uint lightCount = RAB_GetCurrentLightCount();
    if (lightCount == 0u || !RAB_SurfaceSupportsOpaqueDiffuseBrdf(surface))
    {
        return false;
    }

    const uint emissiveTriangleCount = PathTraceSafetyDisabled(RT_PT_SAFETY_DISABLE_EMISSIVE_TRIANGLE_SAMPLING) ? 0u : (uint)max(EmissiveInfo.x, 0.0);
    const uint uploadedAnalyticCount = PathTraceSafetyDisabled(RT_PT_SAFETY_DISABLE_ANALYTIC_LIGHT_LOOP) ? 0u : (uint)max(DoomAnalyticLightInfo.x, 0.0);
    const uint analyticTraceCap = (uint)max(DoomAnalyticLightInfo.y, 0.0);
    const uint analyticCount = (((uint)max(DoomAnalyticLightInfo.w, 0.0)) & 1u) != 0u && !PathTraceSafetyDisabled(RT_PT_SAFETY_DISABLE_ANALYTIC_LIGHT_LOOP) ? min(uploadedAnalyticCount, analyticTraceCap) : 0u;

    RAB_SmokeNeeRisSelection selection = RAB_EmptySmokeNeeRisSelection();

    if (emissiveTriangleCount > 0u)
    {
        const uint trialCount = clamp((uint)max(EmissiveInfo.z, 1.0), 1u, 16u);
        const float domainAverageScale = 1.0 / (float)trialCount;
        [loop]
        for (uint trialIndex = 0u; trialIndex < trialCount; ++trialIndex)
        {
            const float xi = RTXDI_GetNextRandom(ptRandContext.initialRandomSamplerState);
            const uint trialLightIndex = SelectSmokeWeightedEmissiveTriangle(emissiveTriangleCount, xi);
            if (trialLightIndex >= emissiveTriangleCount)
            {
                continue;
            }
            const PathTraceSmokeEmissiveTriangle emissiveTriangle = SmokeEmissiveTriangles[trialLightIndex];
            const float lightSelectionPdf = max(emissiveTriangle.sampleWeightAndPdf.y, 1.0 / max((float)emissiveTriangleCount, 1.0));
            RAB_StreamSmokeNeeRisCandidate(surface, trialLightIndex, float2(0.5, 0.5), lightSelectionPdf, domainAverageScale, ptRandContext, selection);
        }
    }

    if (analyticCount > 0u)
    {
        const uint analyticTrialCount = RAB_GetRestirPTAnalyticTrialCount(analyticCount);
        const float domainAverageScale = 1.0 / (float)analyticTrialCount;
        [loop]
        for (uint trialIndex = 0u; trialIndex < analyticTrialCount; ++trialIndex)
        {
            uint analyticIndex;
            float lightSelectionPdf;
            if (!RAB_SelectSmokeAnalyticNeeProposal(analyticCount, ptRandContext, analyticIndex, lightSelectionPdf))
            {
                continue;
            }
            const uint trialLightIndex = emissiveTriangleCount + analyticIndex;
            const float2 sampleUv = float2(
                RTXDI_GetNextRandom(ptRandContext.initialRandomSamplerState),
                RTXDI_GetNextRandom(ptRandContext.initialRandomSamplerState));
            RAB_StreamSmokeNeeRisCandidate(surface, trialLightIndex, sampleUv, lightSelectionPdf, domainAverageScale, ptRandContext, selection);
        }
    }

    if (selection.lightIndex == RAB_INVALID_LIGHT_INDEX || selection.lightSample.valid == 0u || selection.candidateCount == 0u || selection.risWeight <= 0.0 || selection.risWeightSum <= 0.0)
    {
        return false;
    }

    const float3 radianceOverPdf = selection.radianceOverPdf * (selection.risWeightSum / selection.risWeight);
    if (RAB_Luminance(radianceOverPdf) <= 0.0)
    {
        return false;
    }
    if (!RAB_GetSelectedNeeVisibility(surface, selection.lightSample))
    {
        return false;
    }
    const float selectedRadianceLuminance = RAB_Luminance(selection.reflectedRadiance);
    const float risEstimateLuminance = RAB_Luminance(radianceOverPdf);
    const float neePdf = selectedRadianceLuminance > 0.0 ? max(selectedRadianceLuminance / max(risEstimateLuminance, 1.0e-6), 1.0e-6) : max(selection.effectivePdf, 1.0e-6);

    RTXDI_SampledLightData sampledLightData = RTXDI_SampledLightData_CreateInvalidData();
    RTXDI_SampledLightData_SetLightData(sampledLightData, selection.lightIndex);
    RTXDI_SampledLightData_SetUVData(sampledLightData, selection.lightUv);

    return ctx.RecordNeeLightSample(
        sampledLightData,
        radianceOverPdf,
        neePdf,
        selection.scatterPdf,
        selection.lightSample,
        ptRandContext.initialRandomSamplerState);
}

void RAB_PathTrace(inout RTXDI_PathTracerContext ctx, inout RTXDI_PathTracerRandomContext ptRandContext, inout RAB_PathTracerUserData ptud)
{
    // RTXDI's PT context starts at bounceDepth 2 for the primary surface.
    // Keep that first NEE-capable vertex alive even when the local integrator
    // has zero or one secondary diffuse bounces configured.
    const bool indirectInitialMode = PathTraceRestirPTIndirectInitialMode();
    if (indirectInitialMode && ctx.GetMaxPathBounce() < 4u)
    {
        // Secondary-surface NEE is recorded as pathLength 4 by RTXDI. The
        // default initial context max is 3, which accepts primary NEE but
        // rejects the first indirect NEE candidate before source attribution.
        ctx.SetMaxPathBounce(4u);
    }

    const uint configuredSecondaryBounces = min(
        PathTraceIntegratorDiffuseBounceLimit(),
        PathTraceIntegratorMaxPathDepth() > 0u ? PathTraceIntegratorMaxPathDepth() - 1u : 0u);
    const uint maxSecondaryBounces = indirectInitialMode ? max(configuredSecondaryBounces, 1u) : configuredSecondaryBounces;
    const uint maxBridgeBounces = min(min(ctx.GetMaxPathBounce(), 3u), 2u + maxSecondaryBounces);

    [loop]
    while (ctx.GetBounceDepth() <= maxBridgeBounces)
    {
        ctx.BeginPathState();

        RAB_Surface currentSurface = ctx.GetIntersectionSurface();
        if (!RAB_IsSurfaceValid(currentSurface))
        {
            break;
        }

        const bool skipPrimaryNeeForIndirectDebug = indirectInitialMode && ctx.GetBounceDepth() <= 2u;
        if (!skipPrimaryNeeForIndirectDebug && PathTraceIntegratorNextEventEstimationEnabled() && ctx.ShouldSampleNee())
        {
            RAB_RecordSmokeNeeSample(ctx, currentSurface, ptRandContext);
        }
        if (ctx.GetBounceDepth() >= maxBridgeBounces)
        {
            break;
        }

        if (PathTraceSafetyDisabled(RT_PT_SAFETY_DISABLE_DIFFUSE_SECONDARY_RAY))
        {
            break;
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
            // Environment lighting is intentionally unbridged; see pathtrace_smoke_rab_environment_stub.hlsli.
            RAB_NoteEnvironmentMapMissUnbridged(ptud);
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
            const float emissiveMisWeight = GetMISWeightForEmissiveSurface(rayPayload, previousSurface, ctx.GetBrdfRaySample());
            ctx.RecordEmissiveLightSample(nextSurface.material.emissiveRadiance * emissiveMisWeight, previousSurface, ptRandContext.initialRandomSamplerState);
        }

        ctx.IncreaseBounceDepth();
    }
}

#endif
