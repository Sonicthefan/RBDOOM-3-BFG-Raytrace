#ifndef RB_PATH_TRACING_CLEANROOM_COMMON_RESTIR_PT_PATH_TRACER_CONTEXT_HLSLI
#define RB_PATH_TRACING_CLEANROOM_COMMON_RESTIR_PT_PATH_TRACER_CONTEXT_HLSLI

#include "restir_pt_path_reconnectibility.hlsli"
#include "restir_pt_path_tracer_random_context.hlsli"

struct RTXDI_PTBufferIndices
{
    uint initialPathTracerOutputBufferIndex;
    uint temporalResamplingInputBufferIndex;
    uint temporalResamplingOutputBufferIndex;
    uint spatialResamplingInputBufferIndex;
    uint spatialResamplingOutputBufferIndex;
    uint finalShadingInputBufferIndex;
};

struct RTXDI_PTInitialSamplingParameters
{
    uint numInitialSamples;
    uint maxBounceDepth;
    uint maxRcVertexLength;
};

struct RTXDI_PTReconnectionParameters
{
    float minConnectionFootprint;
    float minConnectionFootprintSigma;
    float minPdfRoughness;
    float minPdfRoughnessSigma;
    float roughnessThreshold;
    float distanceThreshold;
    uint reconnectionMode;
};

struct RTXDI_PTHybridShiftPerFrameParameters
{
    uint maxBounceDepth;
    uint maxRcVertexLength;
};

struct RTXDI_PTTemporalResamplingParameters
{
    float depthThreshold;
    float normalThreshold;
    uint enablePermutationSampling;
    uint maxHistoryLength;
    uint maxReservoirAge;
    uint enableFallbackSampling;
    uint uniformRandomNumber;
    uint duplicationBasedHistoryReduction;
    float historyReductionStrength;
    uint enableVisibilityBeforeCombine;
};

struct RTXDI_PTSpatialResamplingParameters
{
    uint numSpatialSamples;
    uint numDisocclusionBoostSamples;
    uint maxTemporalHistory;
    uint duplicationBasedHistoryReduction;
    float samplingRadius;
    float normalThreshold;
    float depthThreshold;
};

static const uint RTXDI_PTPathTraceInvocationType_Initial = 1u;
static const uint RTXDI_PTPathTraceInvocationType_Temporal = 2u;
static const uint RTXDI_PTPathTraceInvocationType_TemporalInverse = 3u;
static const uint RTXDI_PTPathTraceInvocationType_Spatial = 4u;
static const uint RTXDI_PTPathTraceInvocationType_SpatialInverse = 5u;

struct RTXDI_PathTracerRayReconstructionParameters
{
    bool isPrevFrame;
    float3 cameraPos;
    float3 prevCameraPos;
    float3 prevPrevCameraPos;
};

struct RTXDI_PathTracerContextParameters
{
    uint maxBounces;
    uint maxBounceDepth;
    uint RcVertexLength;
    uint SelectedPathLength;
    uint maxRcVertexLength;
    RTXDI_PTReconnectionParameters rParams;
    RTXDI_PathTracerRayReconstructionParameters rrParams;
    RTXDI_PTReconnectionParameters reconnectionParams;
};

RTXDI_PathTracerContextParameters RTXDI_DefaultPathTracerContextParameters()
{
    RTXDI_PathTracerContextParameters result = (RTXDI_PathTracerContextParameters)0;
    result.maxBounces = 3u;
    result.maxBounceDepth = 3u;
    result.RcVertexLength = 3u;
    result.SelectedPathLength = 3u;
    result.maxRcVertexLength = 3u;
    return result;
}

struct RTXDI_PathTracerContext
{
    uint bounceDepth;
    uint maxPathBounce;
    uint maxRcVertexLength;
    float3 pathThroughput;
    RAB_Surface intersectionSurface;
    RAB_Surface previousSurface;
    RAB_RayPayload traceResult;
    RayDesc continuationRay;
    RTXDI_BrdfRaySample brdfRaySample;
    RTXDI_PTReservoir reservoir;
    RTXDI_PathReconnectibility reconnectibility;
    RTXDI_PTReconnectionParameters reconnectionParams;
    RAB_Surface selectedRcSurface;
    RAB_Surface rcPrevSurface;
    float3 selectedTargetFunction;
    float runningWeightSum;
    uint selectedRcVertexLength;
    uint selectedPathLength;
    float selectedPartialJacobian;
    float selectedRcWiPdf;
    float checkPreRcInverseGeoTerm;

    uint GetMaxPathBounce()
    {
        return maxPathBounce;
    }

    void SetMaxPathBounce(uint value)
    {
        maxPathBounce = value;
    }

    uint GetBounceDepth()
    {
        return bounceDepth;
    }

    void IncreaseBounceDepth()
    {
        bounceDepth += 1u;
    }

    void SetRcVertexLength(uint value)
    {
        selectedRcVertexLength = value;
    }

    void SetSelectedPathLength(uint value)
    {
        selectedPathLength = value;
    }

    void BeginPathState()
    {
    }

    RAB_Surface GetIntersectionSurface()
    {
        return intersectionSurface;
    }

    bool ShouldSampleNee()
    {
        return bounceDepth <= maxPathBounce;
    }

    void SetBrdfRaySample(RTXDI_BrdfRaySample value)
    {
        brdfRaySample = value;
    }

    RTXDI_BrdfRaySample GetBrdfRaySample()
    {
        return brdfRaySample;
    }

    bool ValidContinuationRayBrdfOverPdf()
    {
        return brdfRaySample.OutPdf > 1.0e-6 &&
            all(brdfRaySample.BrdfTimesNoL == brdfRaySample.BrdfTimesNoL);
    }

    float3 GetContinuationRayBrdfOverPdf()
    {
        return brdfRaySample.BrdfTimesNoL / max(brdfRaySample.OutPdf, 1.0e-6);
    }

    void MultiplyPathThroughput(float3 value)
    {
        pathThroughput *= max(value, float3(0.0, 0.0, 0.0));
    }

    void SetContinuationRay(RayDesc value)
    {
        continuationRay = value;
    }

    bool AnalyzePathReconnectibilityBeforeTrace()
    {
        reconnectibility.valid = 1u;
        reconnectibility.pathLength = bounceDepth + 1u;
        reconnectibility.rcVertexLength = min(reconnectibility.pathLength, maxRcVertexLength);
        reconnectibility.partialJacobian = 1.0;
        reconnectibility.rcWiPdf = max(brdfRaySample.OutPdf, 1.0e-6);
        return true;
    }

    RayDesc GetContinuationRay()
    {
        return continuationRay;
    }

    void SetTraceResult(RAB_RayPayload value)
    {
        traceResult = value;
    }

    void RecordPathRadianceMiss(inout RTXDI_RandomSamplerState rng)
    {
    }

    void RecordPathIntersection(RAB_Surface surface)
    {
        previousSurface = intersectionSurface;
        rcPrevSurface = previousSurface;
        intersectionSurface = surface;
    }

    bool ShouldSampleEmissiveSurfaces()
    {
        return true;
    }

    void RecordEmissiveLightSample(float3 radiance, RAB_Surface surface, inout RTXDI_RandomSamplerState rng)
    {
        const float3 contribution = max(pathThroughput * radiance, float3(0.0, 0.0, 0.0));
        if (RTXDI_Luminance(contribution) <= 0.0)
        {
            return;
        }

        reservoir.Radiance = contribution;
        reservoir.TargetFunction = contribution;
        reservoir.WeightSum = 1.0;
        reservoir.M = max(reservoir.M + 1u, 1u);
        reservoir.TranslatedWorldPosition = RAB_GetSurfaceWorldPos(surface);
        reservoir.WorldNormal = RAB_GetSurfaceNormal(surface);
        reservoir.PathLength = bounceDepth + 1u;
        reservoir.RcVertexLength = min(reservoir.PathLength, maxRcVertexLength);
        reservoir.PartialJacobian = max(reconnectibility.partialJacobian, 1.0e-6);
        reservoir.RcWiPdf = max(reconnectibility.rcWiPdf, 1.0e-6);
        reservoir.RandomSeed = rng.seed;
        reservoir.RandomIndex = rng.index;
        selectedTargetFunction = contribution;
        runningWeightSum = max(runningWeightSum + 1.0, 1.0);
        selectedRcSurface = surface;
        selectedRcVertexLength = reservoir.RcVertexLength;
        selectedPathLength = reservoir.PathLength;
        selectedPartialJacobian = reservoir.PartialJacobian;
        selectedRcWiPdf = reservoir.RcWiPdf;
    }

    bool RecordNeeLightSample(
        RTXDI_SampledLightData sampledLightData,
        float3 radianceOverPdf,
        float neePdf,
        float scatterPdf,
        RAB_LightSample lightSample,
        inout RTXDI_RandomSamplerState rng)
    {
        const float3 contribution = max(pathThroughput * radianceOverPdf, float3(0.0, 0.0, 0.0));
        if (neePdf <= 0.0 || scatterPdf <= 0.0 || RTXDI_Luminance(contribution) <= 0.0)
        {
            return false;
        }

        reservoir.Radiance = float3(asfloat(0x7f800000u), asfloat(sampledLightData.lightData), asfloat(sampledLightData.uvData));
        reservoir.TargetFunction = contribution;
        reservoir.WeightSum = 1.0;
        reservoir.M = max(reservoir.M + 1u, 1u);
        reservoir.TranslatedWorldPosition = lightSample.position;
        reservoir.WorldNormal = RAB_GetSurfaceNormal(intersectionSurface);
        reservoir.PathLength = bounceDepth + 1u;
        reservoir.RcVertexLength = reservoir.PathLength;
        reservoir.PartialJacobian = 1.0;
        reservoir.RcWiPdf = max(scatterPdf, 1.0e-6);
        reservoir.RandomSeed = rng.seed;
        reservoir.RandomIndex = rng.index;
        selectedTargetFunction = contribution;
        runningWeightSum = max(runningWeightSum + 1.0, 1.0);
        selectedRcSurface = intersectionSurface;
        selectedRcVertexLength = reservoir.RcVertexLength;
        selectedPathLength = reservoir.PathLength;
        selectedPartialJacobian = reservoir.PartialJacobian;
        selectedRcWiPdf = reservoir.RcWiPdf;
        return true;
    }

    RTXDI_PTReservoir GetReservoir()
    {
        return reservoir;
    }

    RAB_Surface GetRcPrevSurface()
    {
        return rcPrevSurface;
    }

    float3 GetRadiance()
    {
        return reservoir.Radiance;
    }

    uint GetReconnectionMode()
    {
        return reconnectionParams.reconnectionMode;
    }

    float GetDistanceThreshold()
    {
        return reconnectionParams.distanceThreshold;
    }

    float GetRoughnessThreshold()
    {
        return reconnectionParams.roughnessThreshold;
    }

    float GetFootprintThreshold()
    {
        return reconnectionParams.minConnectionFootprint;
    }

    float GetPdfThreshold()
    {
        return reconnectionParams.minPdfRoughness;
    }

    float GetCheckPreRcInverseGeoTerm()
    {
        return checkPreRcInverseGeoTerm;
    }

    float3 GetSelectedTargetFunction()
    {
        return selectedTargetFunction;
    }

    uint GetSelectedRcVertexLength()
    {
        return selectedRcVertexLength;
    }

    uint GetSelectedPathLength()
    {
        return selectedPathLength;
    }

    float GetSelectedPartialJacobian()
    {
        return selectedPartialJacobian;
    }

    float GetSelectedRcWiPdf()
    {
        return selectedRcWiPdf;
    }

    RAB_Surface GetSelectedRcSurface()
    {
        return selectedRcSurface;
    }

    float GetRunningWeightSum()
    {
        return runningWeightSum;
    }
};

RTXDI_PathTracerContext RTXDI_CreatePathTracerContext(
    RAB_Surface surface,
    uint maxBounceDepth,
    uint maxRcVertexLength,
    RTXDI_PTReconnectionParameters reconnectionParams)
{
    RTXDI_PathTracerContext result = (RTXDI_PathTracerContext)0;
    result.bounceDepth = 2u;
    result.maxPathBounce = max(maxBounceDepth, 2u);
    result.maxRcVertexLength = max(maxRcVertexLength, 1u);
    result.pathThroughput = float3(1.0, 1.0, 1.0);
    result.intersectionSurface = surface;
    result.previousSurface = RAB_EmptySurface();
    result.traceResult = RAB_EmptyRayPayload();
    result.brdfRaySample = RTXDI_EmptyBrdfRaySample();
    result.reservoir = RTXDI_EmptyPTReservoir();
    result.reconnectibility = RTXDI_EmptyPathReconnectibility();
    result.reconnectionParams = reconnectionParams;
    result.selectedRcSurface = surface;
    result.rcPrevSurface = RAB_EmptySurface();
    result.selectedTargetFunction = float3(0.0, 0.0, 0.0);
    result.runningWeightSum = 0.0;
    result.selectedRcVertexLength = 0u;
    result.selectedPathLength = 0u;
    result.selectedPartialJacobian = 1.0;
    result.selectedRcWiPdf = 1.0;
    result.checkPreRcInverseGeoTerm = 0.0;
    return result;
}

RTXDI_PathTracerContext RTXDI_InitializePathTracerContext(
    RAB_Surface surface,
    RTXDI_PTInitialSamplingParameters params,
    RTXDI_PTReconnectionParameters reconnectionParams)
{
    return RTXDI_CreatePathTracerContext(surface, params.maxBounceDepth, params.maxRcVertexLength, reconnectionParams);
}

RTXDI_PathTracerContext RTXDI_InitializePathTracerContext(
    RAB_Surface surface,
    RTXDI_PathTracerContextParameters params)
{
    return RTXDI_CreatePathTracerContext(
        surface,
        max(params.maxBounces, params.maxBounceDepth),
        max(params.RcVertexLength, params.maxRcVertexLength),
        params.rParams);
}

RTXDI_PathTracerContext RTXDI_InitializePathTracerContext(
    RTXDI_PathTracerContextParameters params,
    RAB_Surface surface,
    inout RTXDI_PathTracerRandomContext randomContext)
{
    RTXDI_PathTracerContext result = RTXDI_InitializePathTracerContext(surface, params);
    result.selectedRcVertexLength = params.RcVertexLength;
    result.selectedPathLength = params.SelectedPathLength;
    result.selectedRcSurface = surface;
    return result;
}

#endif
