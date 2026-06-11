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
#include "../RAB_LightTarget.hlsli"
#include "../RAB_MISCallbacks.hlsli"
#include "../RAB_Visibility.hlsli"
#include "../../cleanroom_common/restir_pt_path_tracer_context.hlsli"
#include "../../cleanroom_common/restir_pt_path_tracer_random_context.hlsli"
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

#include "RAB_PathTracerNee.hlsli"

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
