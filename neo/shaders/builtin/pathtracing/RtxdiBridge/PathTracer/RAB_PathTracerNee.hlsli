// Purpose:
//     Supplies rbdoom's current local-RIS NEE path for the RTXDI/RAB path
//     tracer bridge.
//
// NVIDIA reference:
//     E:\prog\references\RTXDI-main\Samples\FullSample\Shaders\LightingPasses\RtxdiApplicationBridge\PathTracer\RAB_PathTracer.hlsli
//     E:\prog\references\RTXDI-main\Samples\FullSample\Shaders\LightingPasses\RtxdiApplicationBridge\PathTracer\RAB_MISCallbacks.hlsli
//     E:\prog\references\RTXDI-main\Libraries\Rtxdi\Include\Rtxdi\PT\PathTracerContext.hlsli
//
// Current rbdoom supplier:
//     Smoke emissive triangles, Doom analytic light candidates, RAB light
//     sampling helpers, RAB visibility callbacks, and local path-tracer
//     policy.
//
// Current deviation:
//     rbdoom uses a hand-written local RIS stream instead of the NVIDIA sample's
//     SampleDirectLightsForIndirectSurface / CalculateNEE path backed by RTXDI_DI
//     sampling.
//
// Extraction rule:
//     This file must preserve existing behavior until a later correction task.

#ifndef RB_PATH_TRACING_RAB_PATH_TRACER_NEE_HLSLI
#define RB_PATH_TRACING_RAB_PATH_TRACER_NEE_HLSLI

#undef RTXDI_ENABLE_PRESAMPLING
#define RTXDI_ENABLE_PRESAMPLING 0
#include "Rtxdi/DI/InitialSampling.hlsli"

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

static const uint RAB_UNIFIED_NEE_SAMPLE_COUNT = 32u;

bool RAB_UnifiedNeeProducerEnabled()
{
    if ((((uint)max(UnifiedLightInfo.z, 0.0)) & 4u) == 0u)
    {
        return false;
    }

    return RAB_RestirLightManagerRABEnabled() ||
        (RAB_UnifiedLightLoadEnabled() && RAB_UnifiedLightSampleEnabled());
}

uint RAB_GetCurrentUnifiedNeeLightCount()
{
    return RAB_RestirLightManagerRABEnabled() ? RAB_GetCurrentRestirLightManagerCount() : RAB_GetCurrentUnifiedLightCount();
}

RTXDI_DIInitialSamplingParameters RAB_BuildUnifiedNeeInitialSamplingParameters()
{
    RTXDI_DIInitialSamplingParameters sampleParams = (RTXDI_DIInitialSamplingParameters)0;
    sampleParams.numLocalLightSamples = min(RAB_GetCurrentUnifiedNeeLightCount(), RAB_UNIFIED_NEE_SAMPLE_COUNT);
    sampleParams.numInfiniteLightSamples = 0u;
    sampleParams.numEnvironmentSamples = 0u;
    sampleParams.numBrdfSamples = 0u;
    sampleParams.brdfCutoff = 0.0;
    sampleParams.brdfRayMinT = 0.0;
    sampleParams.localLightSamplingMode = ReSTIRDI_LocalLightSamplingMode_UNIFORM;
    sampleParams.enableInitialVisibility = 0u;
    sampleParams.environmentMapImportanceSampling = 0u;
    return sampleParams;
}

RTXDI_LightBufferParameters RAB_BuildUnifiedNeeLightBufferParameters()
{
    RTXDI_LightBufferParameters lightBufferParams = (RTXDI_LightBufferParameters)0;
    lightBufferParams.localLightBufferRegion.firstLightIndex = 0u;
    lightBufferParams.localLightBufferRegion.numLights = RAB_GetCurrentUnifiedNeeLightCount();
    lightBufferParams.infiniteLightBufferRegion.firstLightIndex = 0u;
    lightBufferParams.infiniteLightBufferRegion.numLights = 0u;
    lightBufferParams.environmentLightParams.lightPresent = 0u;
    lightBufferParams.environmentLightParams.lightIndex = RAB_INVALID_LIGHT_INDEX;
    return lightBufferParams;
}

bool RAB_RecordUnifiedNeeSample(inout RTXDI_PathTracerContext ctx, RAB_Surface surface, inout RTXDI_PathTracerRandomContext ptRandContext)
{
    if (!RAB_UnifiedNeeProducerEnabled() ||
        RAB_GetCurrentUnifiedNeeLightCount() == 0u ||
        !RAB_SurfaceSupportsOpaqueDiffuseBrdf(surface))
    {
        return false;
    }

    RAB_LightSample lightSample = RAB_EmptyLightSample();
    RTXDI_DIReservoir diReservoir = RTXDI_SampleLightsForSurface(
        ptRandContext.initialRandomSamplerState,
        ptRandContext.initialCoherentRandomSamplerState,
        surface,
        RAB_BuildUnifiedNeeInitialSamplingParameters(),
        RAB_BuildUnifiedNeeLightBufferParameters(),
        lightSample);

    if (!RTXDI_IsValidDIReservoir(diReservoir) ||
        diReservoir.weightSum <= 0.0 ||
        lightSample.valid == 0u ||
        lightSample.solidAnglePdf <= 0.0)
    {
        return false;
    }

    if (!RAB_GetSelectedNeeVisibility(surface, lightSample))
    {
        return false;
    }

    const float3 reflectedRadiance = RAB_GetReflectedBsdfRadianceForSurface(
        lightSample.position,
        lightSample.radiance,
        surface);
    const float3 radianceOverPdf =
        reflectedRadiance *
        RTXDI_GetDIReservoirInvPdf(diReservoir) /
        max(lightSample.solidAnglePdf, 1.0e-6);
    if (RAB_Luminance(radianceOverPdf) <= 0.0)
    {
        return false;
    }

    RTXDI_SampledLightData sampledLightData = RTXDI_SampledLightData_CreateInvalidData();
    sampledLightData.lightData = diReservoir.lightData;
    sampledLightData.uvData = diReservoir.uvData;

    const float3 lightDir = RAB_SafeNormalize(
        RAB_LightSamplePosition(lightSample) - RAB_GetSurfaceWorldPos(surface),
        RAB_GetSurfaceNormal(surface));
    const float scatterPdf = RAB_SurfaceEvaluateBrdfPdf(surface, lightDir);
    const float neePdf = 1.0 / max(diReservoir.weightSum, 1.0e-6);
    if (scatterPdf <= 0.0 || neePdf <= 0.0)
    {
        return false;
    }
    const float3 weightedRadianceOverPdf =
        radianceOverPdf *
        GetMISWeightForNEELight(
            RTXDI_SampledLightData_GetLightIndex(sampledLightData),
            lightSample,
            RAB_GetSurfaceWorldPos(surface),
            scatterPdf);

    return ctx.RecordNeeLightSample(
        sampledLightData,
        weightedRadianceOverPdf,
        neePdf,
        scatterPdf,
        lightSample,
        ptRandContext.initialRandomSamplerState);
}

bool RAB_RecordSmokeRisNeeSample(inout RTXDI_PathTracerContext ctx, RAB_Surface surface, inout RTXDI_PathTracerRandomContext ptRandContext)
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

bool RAB_NeeCacheSecondaryConsumerReady()
{
    return NeeCacheConsumerInfo.x >= 0.5 &&
        NeeCacheInfo0.x >= 0.5 &&
        NeeCacheInfo3.y >= 0.5 &&
        NeeCacheInfo3.z > 0.0 &&
        NeeCacheInfo1.w > 0.0 &&
        NeeCacheInfo2.w > 0.0;
}

float RAB_NeeCacheSecondaryEmissiveWeight(RAB_LightInfo lightInfo, PathTraceNeeCacheCellDebug cell)
{
    if (!RAB_IsLightInfoValid(lightInfo) ||
        lightInfo.unifiedLightType != PATH_TRACE_UNIFIED_LIGHT_TYPE_EMISSIVE_TRIANGLE ||
        lightInfo.area <= 1.0e-6)
    {
        return 0.0;
    }

    const float3 cellCenter = (float3(cell.coord) + 0.5) * max(cell.cellSize, 1.0);
    const float3 toCell = cellCenter - lightInfo.position;
    const float distanceSquared = dot(toCell, toCell);
    const float normalFacing = saturate(dot(RAB_LightInfoSafeNormalize(toCell, lightInfo.normal), lightInfo.normal));
    const float cellRadius = max(cell.cellSize, 1.0) * 0.8660254;
    const float luminance = max(RAB_Luminance(lightInfo.radiance), 0.0);
    const float sourceWeight = max(lightInfo.weight, luminance);
    return sourceWeight * lightInfo.area * (0.2 + 0.8 * normalFacing) /
        max(distanceSquared + cellRadius * cellRadius, 1.0);
}

float RAB_NeeCacheSecondaryAnalyticWeight(RAB_LightInfo lightInfo, RAB_Surface surface, PathTraceNeeCacheCellDebug cell)
{
    if (!RAB_IsLightInfoValid(lightInfo) ||
        lightInfo.unifiedLightType != PATH_TRACE_UNIFIED_LIGHT_TYPE_DOOM_ANALYTIC ||
        lightInfo.radius <= 0.0 ||
        lightInfo.influenceRadius <= 0.0)
    {
        return 0.0;
    }

    const float3 cellCenter = (float3(cell.coord) + 0.5) * max(cell.cellSize, 1.0);
    const float3 samplePoint = RAB_GetSurfaceWorldPos(surface);
    const float3 toCellCenter = cellCenter - samplePoint;
    const float3 lightPosition = lightInfo.position;
    const float3 toLight = lightPosition - samplePoint;
    const float lightDistanceSquared = max(dot(toLight, toLight), 1.0e-4);
    const float lightDistance = sqrt(lightDistanceSquared);
    const float3 lightDir = toLight / lightDistance;
    const float3 normal = RAB_SafeNormalize(RAB_GetSurfaceNormal(surface), RAB_GetSurfaceGeoNormal(surface));
    const float ndotl = saturate(dot(normal, lightDir));
    if (ndotl <= 0.0 || dot(RAB_GetSurfaceGeoNormal(surface), lightDir) <= 0.0)
    {
        return 0.0;
    }

    const float cellRadius = max(cell.cellSize, 1.0) * 0.8660254;
    const float distanceToCell = length(lightPosition - cellCenter);
    const float edgeDistance = max(distanceToCell - max(lightInfo.radius, 1.0e-3), 0.0);
    const float influenceDistance = max(lightInfo.influenceRadius + cellRadius, lightInfo.radius + 1.0);
    const float influence = saturate(1.0 - edgeDistance / max(influenceDistance - lightInfo.radius, 1.0));
    const float radius = min(max(lightInfo.radius, 1.0e-3), max(lightInfo.influenceRadius, 1.0e-3));
    const float solidAngleApprox = max(2.0 * RTXDI_PI * (1.0 - sqrt(max(0.0, 1.0 - saturate((radius * radius) / lightDistanceSquared)))), 1.0e-5);
    const float3 brdf = RAB_EvaluateSurfaceBrdf(surface, lightDir, RAB_GetSurfaceViewDir(surface));
    const float diffuseWeight = RAB_Luminance(brdf * lightInfo.radiance) * ndotl * solidAngleApprox;
    const float distanceAnchor = 1.0 / max(dot(toCellCenter, toCellCenter) + cellRadius * cellRadius, 1.0);
    return diffuseWeight * max(influence * influence, 0.0) * max(1.0, cellRadius * cellRadius) * distanceAnchor;
}

bool RAB_NeeCacheSecondaryCandidateUsable(
    PathTraceNeeCacheCandidateRecord candidate,
    uint currentRluLightCount,
    uint requiredClass,
    out RAB_LightInfo lightInfo)
{
    lightInfo = RAB_EmptyLightInfo();
    if (candidate.flags == 0u ||
        candidate.denseRluIndex >= currentRluLightCount ||
        candidate.sourcePdf <= 0.0 ||
        candidate.invSourcePdf <= 0.0)
    {
        return false;
    }
    if (requiredClass != 0u && candidate.lightClass != requiredClass)
    {
        return false;
    }
    if (candidate.lightClass != PATH_TRACE_UNIFIED_LIGHT_TYPE_EMISSIVE_TRIANGLE &&
        candidate.lightClass != PATH_TRACE_UNIFIED_LIGHT_TYPE_DOOM_ANALYTIC)
    {
        return false;
    }

    lightInfo = RAB_LoadLightInfo(candidate.denseRluIndex, false);
    return RAB_IsLightInfoValid(lightInfo) &&
        lightInfo.unifiedLightType == candidate.lightClass;
}

float RAB_NeeCacheSecondaryCandidateWeight(
    RAB_LightInfo lightInfo,
    RAB_Surface surface,
    PathTraceNeeCacheCellDebug cell)
{
    if (lightInfo.unifiedLightType == PATH_TRACE_UNIFIED_LIGHT_TYPE_EMISSIVE_TRIANGLE)
    {
        return RAB_NeeCacheSecondaryEmissiveWeight(lightInfo, cell);
    }
    if (lightInfo.unifiedLightType == PATH_TRACE_UNIFIED_LIGHT_TYPE_DOOM_ANALYTIC)
    {
        return RAB_NeeCacheSecondaryAnalyticWeight(lightInfo, surface, cell);
    }
    return 0.0;
}

uint RAB_NeeCacheSecondarySourceDomain()
{
    return clamp((uint)max(NeeCacheInfo0.w, 0.0), 0u, 3u);
}

uint2 RAB_NeeCacheSecondaryEmissiveRange(uint currentRluLightCount)
{
    const uint offset = min((uint)max(RestirLightManagerRangeInfo.x, 0.0), currentRluLightCount);
    const uint count = min((uint)max(RestirLightManagerRangeInfo.y, 0.0), currentRluLightCount - offset);
    return uint2(offset, count);
}

uint2 RAB_NeeCacheSecondaryAnalyticRange(uint currentRluLightCount)
{
    const uint offset = min((uint)max(RestirLightManagerRangeInfo.z, 0.0), currentRluLightCount);
    const uint count = min((uint)max(RestirLightManagerRangeInfo.w, 0.0), currentRluLightCount - offset);
    return uint2(offset, count);
}

bool RAB_NeeCacheSecondaryFallbackAnalyticSampleable(uint denseRluIndex)
{
    const RAB_LightInfo lightInfo = RAB_LoadLightInfo(denseRluIndex, false);
    return RAB_IsLightInfoValid(lightInfo) &&
        lightInfo.unifiedLightType == PATH_TRACE_UNIFIED_LIGHT_TYPE_DOOM_ANALYTIC;
}

uint RAB_NeeCacheSecondaryFallbackAnalyticCount(uint currentRluLightCount)
{
    const uint2 analyticRange = RAB_NeeCacheSecondaryAnalyticRange(currentRluLightCount);
    uint count = 0u;
    [loop]
    for (uint localIndex = 0u; localIndex < analyticRange.y; ++localIndex)
    {
        if (RAB_NeeCacheSecondaryFallbackAnalyticSampleable(analyticRange.x + localIndex))
        {
            ++count;
        }
    }
    return count;
}

float RAB_NeeCacheSecondaryFallbackIdentityPdf(uint denseRluIndex, uint currentRluLightCount)
{
    if (currentRluLightCount == 0u || denseRluIndex >= currentRluLightCount)
    {
        return 0.0;
    }

    const uint sourceDomain = RAB_NeeCacheSecondarySourceDomain();
    const uint2 emissiveRange = RAB_NeeCacheSecondaryEmissiveRange(currentRluLightCount);
    const uint2 analyticRange = RAB_NeeCacheSecondaryAnalyticRange(currentRluLightCount);
    const bool inEmissiveRange = denseRluIndex >= emissiveRange.x && denseRluIndex < emissiveRange.x + emissiveRange.y;
    const bool inAnalyticRange = denseRluIndex >= analyticRange.x && denseRluIndex < analyticRange.x + analyticRange.y &&
        RAB_NeeCacheSecondaryFallbackAnalyticSampleable(denseRluIndex);

    if (sourceDomain == 0u)
    {
        return 1.0 / max((float)currentRluLightCount, 1.0);
    }
    if (sourceDomain == 1u)
    {
        return inEmissiveRange ? 1.0 / max((float)emissiveRange.y, 1.0) : 0.0;
    }

    const uint analyticCount = RAB_NeeCacheSecondaryFallbackAnalyticCount(currentRluLightCount);
    if (sourceDomain == 2u)
    {
        return inAnalyticRange ? 1.0 / max((float)analyticCount, 1.0) : 0.0;
    }

    if (inEmissiveRange)
    {
        const float classProbability = (emissiveRange.y > 0u && analyticCount > 0u) ? 0.5 : 1.0;
        return classProbability / max((float)emissiveRange.y, 1.0);
    }
    if (inAnalyticRange)
    {
        const float classProbability = (emissiveRange.y > 0u && analyticCount > 0u) ? 0.5 : 1.0;
        return classProbability / max((float)analyticCount, 1.0);
    }
    return 0.0;
}

bool RAB_NeeCacheSecondarySelectFallback(
    inout RTXDI_RandomSamplerState rng,
    uint currentRluLightCount,
    out uint selectedDenseRluIndex)
{
    selectedDenseRluIndex = RAB_INVALID_LIGHT_INDEX;
    if (currentRluLightCount == 0u)
    {
        return false;
    }

    const uint sourceDomain = RAB_NeeCacheSecondarySourceDomain();
    if (sourceDomain == 0u)
    {
        selectedDenseRluIndex = min((uint)(RTXDI_GetNextRandom(rng) * (float)currentRluLightCount), currentRluLightCount - 1u);
        return true;
    }

    const uint2 emissiveRange = RAB_NeeCacheSecondaryEmissiveRange(currentRluLightCount);
    const uint2 analyticRange = RAB_NeeCacheSecondaryAnalyticRange(currentRluLightCount);
    const uint analyticCount = RAB_NeeCacheSecondaryFallbackAnalyticCount(currentRluLightCount);
    const bool chooseAnalytic =
        sourceDomain == 2u ||
        (sourceDomain == 3u && analyticCount > 0u && (emissiveRange.y == 0u || RTXDI_GetNextRandom(rng) >= 0.5));

    if (!chooseAnalytic)
    {
        if (emissiveRange.y == 0u)
        {
            return false;
        }
        const uint localIndex = min((uint)(RTXDI_GetNextRandom(rng) * (float)emissiveRange.y), emissiveRange.y - 1u);
        selectedDenseRluIndex = emissiveRange.x + localIndex;
        return selectedDenseRluIndex < currentRluLightCount;
    }

    if (analyticCount == 0u)
    {
        return false;
    }

    const uint selectedOrdinal = min((uint)(RTXDI_GetNextRandom(rng) * (float)analyticCount), analyticCount - 1u);
    uint ordinal = 0u;
    [loop]
    for (uint localAnalyticIndex = 0u; localAnalyticIndex < analyticRange.y; ++localAnalyticIndex)
    {
        const uint denseRluIndex = analyticRange.x + localAnalyticIndex;
        if (!RAB_NeeCacheSecondaryFallbackAnalyticSampleable(denseRluIndex))
        {
            continue;
        }
        if (ordinal == selectedOrdinal)
        {
            selectedDenseRluIndex = denseRluIndex;
            return true;
        }
        ++ordinal;
    }
    return false;
}

bool RAB_NeeCacheSecondarySelectCandidate(
    inout RTXDI_RandomSamplerState rng,
    RAB_Surface surface,
    PathTraceNeeCacheCellDebug cell,
    uint currentRluLightCount,
    uint requiredClass,
    out uint selectedDenseRluIndex,
    out float selectedSourcePdf)
{
    selectedDenseRluIndex = RAB_INVALID_LIGHT_INDEX;
    selectedSourcePdf = 0.0;

    const uint candidateSlots = max((uint)max(NeeCacheInfo1.w, 0.0), 1u);
    const uint baseSlot = cell.cellIndex * candidateSlots;
    const PathTraceNeeCacheCellRecord storedCell = PathTraceNeeCacheCells[cell.cellIndex];
    if (storedCell.flags == 0u || storedCell.hash != cell.hash)
    {
        return false;
    }

    float totalWeight = 0.0;
    [loop]
    for (uint slot = 0u; slot < candidateSlots; ++slot)
    {
        RAB_LightInfo lightInfo;
        const PathTraceNeeCacheCandidateRecord candidate = PathTraceNeeCacheCandidates[baseSlot + slot];
        if (RAB_NeeCacheSecondaryCandidateUsable(candidate, currentRluLightCount, requiredClass, lightInfo))
        {
            totalWeight += RAB_NeeCacheSecondaryCandidateWeight(lightInfo, surface, cell);
        }
    }

    PathTraceNeeCacheCandidateRecord selectedCandidate = (PathTraceNeeCacheCandidateRecord)0;
    selectedCandidate.denseRluIndex = RAB_INVALID_LIGHT_INDEX;
    const float fallbackProbability = saturate(NeeCacheInfo2.y);
    const float cacheProbability = 1.0 - fallbackProbability;
    const bool forceFallback = totalWeight <= 0.0 || RTXDI_GetNextRandom(rng) < fallbackProbability;
    if (forceFallback)
    {
        if (!RAB_NeeCacheSecondarySelectFallback(rng, currentRluLightCount, selectedDenseRluIndex))
        {
            return false;
        }
    }
    else
    {
        const float threshold = RTXDI_GetNextRandom(rng) * totalWeight;
        float cumulativeWeight = 0.0;
        [loop]
        for (uint selectSlot = 0u; selectSlot < candidateSlots; ++selectSlot)
        {
            RAB_LightInfo lightInfo;
            const PathTraceNeeCacheCandidateRecord candidate = PathTraceNeeCacheCandidates[baseSlot + selectSlot];
            if (!RAB_NeeCacheSecondaryCandidateUsable(candidate, currentRluLightCount, requiredClass, lightInfo))
            {
                continue;
            }

            const float currentWeight = RAB_NeeCacheSecondaryCandidateWeight(lightInfo, surface, cell);
            if (currentWeight <= 0.0)
            {
                continue;
            }
            cumulativeWeight += currentWeight;
            if (selectedCandidate.denseRluIndex == RAB_INVALID_LIGHT_INDEX && cumulativeWeight >= threshold)
            {
                selectedCandidate = candidate;
            }
        }

        if (selectedCandidate.denseRluIndex == RAB_INVALID_LIGHT_INDEX)
        {
            if (!RAB_NeeCacheSecondarySelectFallback(rng, currentRluLightCount, selectedDenseRluIndex))
            {
                return false;
            }
        }
        else
        {
            selectedDenseRluIndex = selectedCandidate.denseRluIndex;
        }
    }

    float selectedIdentityWeight = 0.0;
    [loop]
    for (uint pdfSlot = 0u; pdfSlot < candidateSlots; ++pdfSlot)
    {
        RAB_LightInfo lightInfo;
        const PathTraceNeeCacheCandidateRecord candidate = PathTraceNeeCacheCandidates[baseSlot + pdfSlot];
        if (candidate.denseRluIndex == selectedCandidate.denseRluIndex &&
            RAB_NeeCacheSecondaryCandidateUsable(candidate, currentRluLightCount, requiredClass, lightInfo))
        {
            selectedIdentityWeight += RAB_NeeCacheSecondaryCandidateWeight(lightInfo, surface, cell);
        }
    }

    const float cacheIdentityPdf = totalWeight > 0.0 ? selectedIdentityWeight / max(totalWeight, 1.0e-8) : 0.0;
    const float fallbackIdentityPdf = RAB_NeeCacheSecondaryFallbackIdentityPdf(selectedDenseRluIndex, currentRluLightCount);
    selectedSourcePdf = totalWeight > 0.0
        ? cacheProbability * cacheIdentityPdf + fallbackProbability * fallbackIdentityPdf
        : fallbackIdentityPdf;
    if (selectedSourcePdf <= 0.0)
    {
        return false;
    }
    return true;
}

bool RAB_RecordNeeCacheSecondarySample(inout RTXDI_PathTracerContext ctx, RAB_Surface surface, inout RTXDI_PathTracerRandomContext ptRandContext)
{
    if (!RAB_NeeCacheSecondaryConsumerReady() ||
        !RAB_SurfaceSupportsOpaqueDiffuseBrdf(surface))
    {
        return false;
    }

    const uint currentRluLightCount = (uint)max(NeeCacheInfo3.z, 0.0);
    const PathTraceNeeCacheCellDebug cell = PathTraceNeeCacheMapWorldPositionToCell(
        RAB_GetSurfaceWorldPos(surface),
        CameraOriginAndTMax.xyz,
        max((uint)NeeCacheInfo1.x, 1u),
        max(NeeCacheInfo1.y, 1.0),
        max((uint)NeeCacheInfo1.z, 1u));
    if (cell.valid == 0u || cell.cellIndex >= (uint)max(NeeCacheInfo2.w, 0.0))
    {
        return false;
    }

    uint selectedDenseRluIndex;
    float selectedSourcePdf;
    if (!RAB_NeeCacheSecondarySelectCandidate(
        ptRandContext.initialRandomSamplerState,
        surface,
        cell,
        currentRluLightCount,
        0u,
        selectedDenseRluIndex,
        selectedSourcePdf))
    {
        return false;
    }

    const RAB_LightInfo lightInfo = RAB_LoadLightInfo(selectedDenseRluIndex, false);
    if (!RAB_IsLightInfoValid(lightInfo) ||
        (lightInfo.unifiedLightType != PATH_TRACE_UNIFIED_LIGHT_TYPE_EMISSIVE_TRIANGLE &&
            lightInfo.unifiedLightType != PATH_TRACE_UNIFIED_LIGHT_TYPE_DOOM_ANALYTIC))
    {
        return false;
    }

    const float2 lightUv = float2(
        RTXDI_GetNextRandom(ptRandContext.initialRandomSamplerState),
        RTXDI_GetNextRandom(ptRandContext.initialRandomSamplerState));
    const RAB_LightSample lightSample = RAB_SamplePolymorphicLight(lightInfo, surface, lightUv);
    if (lightSample.valid == 0u || lightSample.solidAnglePdf <= 0.0)
    {
        return false;
    }

    float3 lightDir;
    float lightDistance;
    RAB_GetLightDirDistance(surface, lightSample, lightDir, lightDistance);
    const float scatterPdf = RAB_GetSurfaceBrdfPdf(surface, lightDir);
    const float neePdf = selectedSourcePdf * lightSample.solidAnglePdf;
    if (scatterPdf <= 0.0 || neePdf <= 0.0)
    {
        return false;
    }

    const float visibility = NeeCacheConsumerInfo.z >= 0.5
        ? (RAB_GetSelectedNeeVisibility(surface, lightSample) ? 1.0 : 0.0)
        : 1.0;
    if (visibility <= 0.0)
    {
        return false;
    }

    const float3 reflectedRadiance = RAB_GetReflectedBsdfRadianceForSurface(
        lightSample.position,
        lightSample.radiance,
        surface);
    const float misWeight = GetMISWeightForNEELight(
        selectedDenseRluIndex,
        lightSample,
        RAB_GetSurfaceWorldPos(surface),
        scatterPdf);
    const float3 radianceOverPdf = reflectedRadiance * visibility * misWeight / max(neePdf, 1.0e-6);
    if (RAB_Luminance(radianceOverPdf) <= 0.0)
    {
        return false;
    }

    RTXDI_SampledLightData sampledLightData = RTXDI_SampledLightData_CreateInvalidData();
    RTXDI_SampledLightData_SetLightData(sampledLightData, selectedDenseRluIndex);
    RTXDI_SampledLightData_SetUVData(sampledLightData, lightUv);

    return ctx.RecordNeeLightSample(
        sampledLightData,
        radianceOverPdf,
        neePdf,
        scatterPdf,
        lightSample,
        ptRandContext.initialRandomSamplerState);
}

bool RAB_RecordSmokeNeeSample(inout RTXDI_PathTracerContext ctx, RAB_Surface surface, inout RTXDI_PathTracerRandomContext ptRandContext)
{
    if (RAB_RecordNeeCacheSecondarySample(ctx, surface, ptRandContext))
    {
        return true;
    }
    if (RAB_UnifiedNeeProducerEnabled())
    {
        return RAB_RecordUnifiedNeeSample(ctx, surface, ptRandContext);
    }
    if (RAB_RestirLightManagerRABEnabled())
    {
        return false;
    }
    return RAB_RecordSmokeRisNeeSample(ctx, surface, ptRandContext);
}

#undef RTXDI_ENABLE_PRESAMPLING
#define RTXDI_ENABLE_PRESAMPLING 1

#endif
