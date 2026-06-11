#ifndef RB_PATH_TRACING_CLEANROOM_COMMON_RESTIR_PT_INITIAL_SAMPLING_HLSLI
#define RB_PATH_TRACING_CLEANROOM_COMMON_RESTIR_PT_INITIAL_SAMPLING_HLSLI

#include "restir_pt_reservoir.hlsli"
#include "restir_pt_path_tracer_context.hlsli"
#include "restir_pt_path_tracer_random_context.hlsli"
#include "../RtxdiBridge/PathTracer/RAB_PathTracer.hlsli"

struct RTXDI_PTInitialSamplingRuntimeParameters
{
    RTXDI_RandomSamplerState RngState;
    RTXDI_RandomSamplerState CoherentRngState;
    RTXDI_RandomSamplerState RngStateForReplay;
    uint2 ReservoirPosition;
    float HitDistance;
    bool DebugSurfaceValid;
    float3 cameraPos;
    float3 prevCameraPos;
    float3 prevPrevCameraPos;
};

RTXDI_PTInitialSamplingRuntimeParameters RTXDI_EmptyPTInitialSamplingRuntimeParameters()
{
    RTXDI_PTInitialSamplingRuntimeParameters params = (RTXDI_PTInitialSamplingRuntimeParameters)0;
    return params;
}

bool RBPT_IsUsableInitialSample(RTXDI_PTReservoir reservoir)
{
    if (!RTXDI_IsValidPTReservoir(reservoir))
    {
        return false;
    }

    const float targetLuminance = RTXDI_Luminance(max(reservoir.TargetFunction, float3(0.0, 0.0, 0.0)));
    return targetLuminance > 0.0 && reservoir.WeightSum > 0.0 && all(reservoir.TargetFunction == reservoir.TargetFunction);
}

float RBPT_InitialReservoirSelectionWeight(RTXDI_PTReservoir reservoir)
{
    if (!RBPT_IsUsableInitialSample(reservoir))
    {
        return 0.0;
    }

    return RTXDI_Luminance(max(reservoir.TargetFunction, float3(0.0, 0.0, 0.0))) * max(reservoir.WeightSum, 0.0);
}

void RBPT_MergeInitialCandidate(
    inout RTXDI_PTReservoir selectedReservoir,
    RTXDI_PTReservoir candidateReservoir,
    inout uint candidateCount,
    inout float candidateWeightSum,
    inout RTXDI_RandomSamplerState rng)
{
    const float candidateWeight = RBPT_InitialReservoirSelectionWeight(candidateReservoir);
    if (candidateWeight <= 0.0)
    {
        return;
    }

    candidateCount += max(candidateReservoir.M, 1u);
    candidateWeightSum += candidateWeight;
    if (!RTXDI_IsValidPTReservoir(selectedReservoir) ||
        RTXDI_GetNextRandom(rng) * candidateWeightSum <= candidateWeight)
    {
        selectedReservoir = candidateReservoir;
    }
}

void RBPT_FinalizeInitialReservoir(
    inout RTXDI_PTReservoir reservoir,
    uint candidateCount,
    float candidateWeightSum)
{
    if (!RBPT_IsUsableInitialSample(reservoir) || candidateCount == 0u || candidateWeightSum <= 0.0)
    {
        reservoir = RTXDI_EmptyPTReservoir();
        return;
    }

    const float selectedTarget = RTXDI_Luminance(max(reservoir.TargetFunction, float3(0.0, 0.0, 0.0)));
    if (selectedTarget <= 0.0)
    {
        reservoir = RTXDI_EmptyPTReservoir();
        return;
    }

    reservoir.M = min(candidateCount, RTXDI_PTReservoir::MaxM);
    reservoir.WeightSum = candidateWeightSum / (selectedTarget * max((float)candidateCount, 1.0));
    reservoir.Age = 0u;
    reservoir.ShouldBoostSpatialSamples = false;
}

RTXDI_PTReservoir RBPT_RunInitialPathTrace(
    RAB_Surface surface,
    RTXDI_PTInitialSamplingParameters params,
    RTXDI_PTReconnectionParameters reconnectionParams,
    inout RTXDI_PathTracerRandomContext ptRandContext,
    inout RAB_PathTracerUserData ptud)
{
    RTXDI_PathTracerContext pathTracerContext = RTXDI_InitializePathTracerContext(
        surface,
        params,
        reconnectionParams);

    RAB_PathTrace(pathTracerContext, ptRandContext, ptud);
    return pathTracerContext.GetReservoir();
}

RTXDI_PTReservoir GenerateInitialSamples(
    RTXDI_PTInitialSamplingParameters params,
    RTXDI_PTInitialSamplingRuntimeParameters runtimeParams,
    RTXDI_PTReconnectionParameters reconnectionParams,
    inout RTXDI_PathTracerRandomContext ptRandContext,
    RAB_Surface surface,
    inout RAB_PathTracerUserData ptud)
{
    if (!RAB_IsSurfaceValid(surface))
    {
        return RTXDI_EmptyPTReservoir();
    }

    const uint sampleCount = max(params.numInitialSamples, 1u);
    RTXDI_PTReservoir selectedReservoir = RTXDI_EmptyPTReservoir();
    uint candidateCount = 0u;
    float candidateWeightSum = 0.0;

    [loop]
    for (uint sampleIndex = 0u; sampleIndex < sampleCount; ++sampleIndex)
    {
        RTXDI_PathTracerRandomContext candidateRandomContext = ptRandContext;
        const uint sampleSalt = sampleIndex * 0x9e3779b9u;
        candidateRandomContext.initialRandomSamplerState.seed =
            RBPT_RestirHashCombine(candidateRandomContext.initialRandomSamplerState.seed, sampleSalt);
        candidateRandomContext.initialCoherentRandomSamplerState.seed =
            RBPT_RestirHashCombine(candidateRandomContext.initialCoherentRandomSamplerState.seed, sampleSalt ^ 0x85ebca6bu);
        candidateRandomContext.replayRandomSamplerState.seed =
            RBPT_RestirHashCombine(candidateRandomContext.replayRandomSamplerState.seed, sampleSalt ^ 0xc2b2ae35u);

        RAB_PathTracerUserData candidatePtud = ptud;
        const RTXDI_PTReservoir candidateReservoir = RBPT_RunInitialPathTrace(
            surface,
            params,
            reconnectionParams,
            candidateRandomContext,
            candidatePtud);

        RBPT_MergeInitialCandidate(
            selectedReservoir,
            candidateReservoir,
            candidateCount,
            candidateWeightSum,
            ptRandContext.initialRandomSamplerState);

        ptRandContext = candidateRandomContext;
        ptud = candidatePtud;
    }

    RBPT_FinalizeInitialReservoir(selectedReservoir, candidateCount, candidateWeightSum);
    return selectedReservoir;
}

RTXDI_PTReservoir RTXDI_PTInitialSampling(
    RTXDI_PTInitialSamplingParameters params,
    RTXDI_PTInitialSamplingRuntimeParameters runtimeParams,
    RTXDI_PTReconnectionParameters reconnectionParams,
    inout RTXDI_PathTracerRandomContext ptRandContext,
    RAB_Surface surface,
    inout RAB_PathTracerUserData ptud)
{
    return GenerateInitialSamples(
        params,
        runtimeParams,
        reconnectionParams,
        ptRandContext,
        surface,
        ptud);
}

RTXDI_PTReservoir RTXDI_PTInitialSampling(
    RAB_Surface surface,
    RTXDI_PTInitialSamplingParameters params,
    RTXDI_PTInitialSamplingRuntimeParameters runtimeParams,
    RTXDI_PTReconnectionParameters reconnectionParams,
    inout RTXDI_PathTracerRandomContext ptRandContext,
    inout RAB_PathTracerUserData ptud)
{
    return GenerateInitialSamples(
        params,
        runtimeParams,
        reconnectionParams,
        ptRandContext,
        surface,
        ptud);
}

#endif
