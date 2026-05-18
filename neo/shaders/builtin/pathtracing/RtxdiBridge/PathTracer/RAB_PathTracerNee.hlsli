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
    return (((uint)max(UnifiedLightInfo.z, 0.0)) & 4u) != 0u &&
        RAB_UnifiedLightLoadEnabled() &&
        RAB_UnifiedLightSampleEnabled();
}

RTXDI_DIInitialSamplingParameters RAB_BuildUnifiedNeeInitialSamplingParameters()
{
    RTXDI_DIInitialSamplingParameters sampleParams = (RTXDI_DIInitialSamplingParameters)0;
    sampleParams.numLocalLightSamples = min(RAB_GetCurrentUnifiedLightCount(), RAB_UNIFIED_NEE_SAMPLE_COUNT);
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
    lightBufferParams.localLightBufferRegion.numLights = RAB_GetCurrentUnifiedLightCount();
    lightBufferParams.infiniteLightBufferRegion.firstLightIndex = 0u;
    lightBufferParams.infiniteLightBufferRegion.numLights = 0u;
    lightBufferParams.environmentLightParams.lightPresent = 0u;
    lightBufferParams.environmentLightParams.lightIndex = RAB_INVALID_LIGHT_INDEX;
    return lightBufferParams;
}

bool RAB_RecordUnifiedNeeSample(inout RTXDI_PathTracerContext ctx, RAB_Surface surface, inout RTXDI_PathTracerRandomContext ptRandContext)
{
    if (!RAB_UnifiedNeeProducerEnabled() ||
        RAB_GetCurrentUnifiedLightCount() == 0u ||
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

bool RAB_RecordSmokeNeeSample(inout RTXDI_PathTracerContext ctx, RAB_Surface surface, inout RTXDI_PathTracerRandomContext ptRandContext)
{
    if (RAB_UnifiedNeeProducerEnabled())
    {
        return RAB_RecordUnifiedNeeSample(ctx, surface, ptRandContext);
    }
    return RAB_RecordSmokeRisNeeSample(ctx, surface, ptRandContext);
}

#undef RTXDI_ENABLE_PRESAMPLING
#define RTXDI_ENABLE_PRESAMPLING 1

#endif
