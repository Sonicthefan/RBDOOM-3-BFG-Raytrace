#ifndef RB_PATH_TRACING_CLEANROOM_COMMON_RESTIR_PT_TEMPORAL_RESAMPLING_HLSLI
#define RB_PATH_TRACING_CLEANROOM_COMMON_RESTIR_PT_TEMPORAL_RESAMPLING_HLSLI

#include "restir_pt_reservoir.hlsli"
#include "restir_pt_path_tracer_context.hlsli"
#include "restir_random_sampler_state.hlsli"
#include "../RtxdiBridge/PathTracer/RAB_PathTracer.hlsli"

struct RTXDI_PTTemporalResamplingRuntimeParameters
{
    uint2 pixelPosition;
    uint2 reservoirPosition;
    uint2 reservoirDimensions;
    float3 motionVector;
    float3 cameraPos;
    float3 prevCameraPos;
    float3 prevPrevCameraPos;
};

RTXDI_PTTemporalResamplingRuntimeParameters RTXDI_EmptyPTTemporalResamplingRuntimeParameters()
{
    RTXDI_PTTemporalResamplingRuntimeParameters result = (RTXDI_PTTemporalResamplingRuntimeParameters)0;
    return result;
}

bool RBPT_TemporalFiniteFloat(float value)
{
    return value == value && abs(value) < 1.0e20;
}

bool RBPT_TemporalFiniteFloat3(float3 value)
{
    return all(value == value) && all(abs(value) < float3(1.0e20, 1.0e20, 1.0e20));
}

bool RBPT_TemporalUsefulReservoir(RTXDI_PTReservoir reservoir)
{
    return RTXDI_IsValidPTReservoir(reservoir) &&
        reservoir.M > 0u &&
        RBPT_TemporalFiniteFloat(reservoir.WeightSum) &&
        RBPT_TemporalFiniteFloat3(reservoir.TargetFunction) &&
        RBPT_TemporalFiniteFloat3(reservoir.Radiance) &&
        RBPT_TemporalFiniteFloat3(reservoir.TranslatedWorldPosition) &&
        RBPT_TemporalFiniteFloat3(reservoir.WorldNormal) &&
        RBPT_TemporalFiniteFloat(reservoir.RcWiPdf) &&
        RBPT_TemporalFiniteFloat(reservoir.PartialJacobian) &&
        RTXDI_Luminance(max(reservoir.TargetFunction, float3(0.0, 0.0, 0.0))) > 0.0;
}

bool RBPT_TemporalNormalsCompatible(
    RTXDI_PTReservoir currentReservoir,
    RTXDI_PTReservoir previousReservoir,
    RTXDI_PTTemporalResamplingParameters params)
{
    const float3 currentNormal = normalize(currentReservoir.WorldNormal);
    const float3 previousNormal = normalize(previousReservoir.WorldNormal);
    if (!RBPT_TemporalFiniteFloat3(currentNormal) || !RBPT_TemporalFiniteFloat3(previousNormal))
    {
        return false;
    }

    const float normalThreshold = params.normalThreshold > 0.0 ? params.normalThreshold : -1.0;
    return dot(currentNormal, previousNormal) >= normalThreshold;
}

bool RBPT_TemporalDepthCompatible(
    RTXDI_PTReservoir currentReservoir,
    RTXDI_PTReservoir previousReservoir,
    RTXDI_PTTemporalResamplingRuntimeParameters runtimeParams,
    RTXDI_PTTemporalResamplingParameters params)
{
    if (params.depthThreshold <= 0.0)
    {
        return true;
    }

    const float currentDepth = length(currentReservoir.TranslatedWorldPosition - runtimeParams.cameraPos);
    const float previousDepth = length(previousReservoir.TranslatedWorldPosition - runtimeParams.prevCameraPos);
    const float expectedPreviousDepth = currentDepth + runtimeParams.motionVector.z;
    const float referenceDepth = expectedPreviousDepth > 1.0e-4 ? expectedPreviousDepth : currentDepth;
    if (!RBPT_TemporalFiniteFloat(currentDepth) ||
        !RBPT_TemporalFiniteFloat(previousDepth) ||
        !RBPT_TemporalFiniteFloat(referenceDepth))
    {
        return false;
    }

    const float denominator = max(max(abs(referenceDepth), abs(previousDepth)), 1.0e-3);
    return abs(previousDepth - referenceDepth) / denominator <= params.depthThreshold;
}

bool RBPT_TemporalReservoirsCompatible(
    RTXDI_PTReservoir currentReservoir,
    RTXDI_PTReservoir previousReservoir,
    RTXDI_PTTemporalResamplingRuntimeParameters runtimeParams,
    RTXDI_PTTemporalResamplingParameters params)
{
    if (!RBPT_TemporalUsefulReservoir(previousReservoir))
    {
        return false;
    }

    if (params.maxReservoirAge == 0u || previousReservoir.Age >= min(params.maxReservoirAge, RTXDI_PTRESERVOIR_AGE_MAX))
    {
        return false;
    }

    return RBPT_TemporalNormalsCompatible(currentReservoir, previousReservoir, params) &&
        RBPT_TemporalDepthCompatible(currentReservoir, previousReservoir, runtimeParams, params);
}

uint2 RBPT_TemporalPreviousReservoirPosition(
    RTXDI_PTTemporalResamplingRuntimeParameters runtimeParams,
    RTXDI_PTTemporalResamplingParameters params,
    RTXDI_RuntimeParameters rtxdiRuntimeParams,
    out bool valid)
{
    int2 previousPixel = int2(floor(float2(runtimeParams.pixelPosition) + 0.5 + runtimeParams.motionVector.xy));
    if (params.enablePermutationSampling != 0u)
    {
        RTXDI_ApplyPermutationSampling(previousPixel, params.uniformRandomNumber);
    }

    const int2 reservoirDimensions = int2(max(runtimeParams.reservoirDimensions, uint2(1u, 1u)));
    valid = all(previousPixel >= int2(0, 0)) && all(previousPixel < reservoirDimensions);
    return RTXDI_PixelPosToReservoirPos(uint2(max(previousPixel, int2(0, 0))), rtxdiRuntimeParams.activeCheckerboardField);
}

RTXDI_PTReservoir RBPT_TemporalLoadCurrentReservoir(
    RTXDI_ReservoirBufferParameters reservoirParams,
    RTXDI_PTBufferIndices bufferIndices,
    uint2 reservoirPosition)
{
    return RTXDI_LoadPTReservoir(
        reservoirParams,
        reservoirPosition,
        bufferIndices.initialPathTracerOutputBufferIndex);
}

RTXDI_PTReservoir RBPT_TemporalLoadPreviousReservoir(
    RTXDI_ReservoirBufferParameters reservoirParams,
    RTXDI_PTBufferIndices bufferIndices,
    uint2 reservoirPosition)
{
    return RTXDI_LoadPTReservoir(
        reservoirParams,
        reservoirPosition,
        bufferIndices.temporalResamplingInputBufferIndex);
}

RTXDI_PTReservoir RTXDI_PTTemporalResampling(
    RTXDI_PTTemporalResamplingParameters params,
    RTXDI_PTTemporalResamplingRuntimeParameters runtimeParams,
    RTXDI_PTHybridShiftPerFrameParameters hybridShiftParams,
    RTXDI_PTReconnectionParameters reconnectionParams,
    RTXDI_RuntimeParameters rtxdiRuntimeParams,
    RTXDI_ReservoirBufferParameters reservoirParams,
    inout RTXDI_RandomSamplerState rng,
    RTXDI_PTBufferIndices bufferIndices,
    out bool selectedPrevSample,
    inout RAB_PathTracerUserData ptud)
{
    selectedPrevSample = false;

    const RTXDI_PTReservoir currentReservoir = RBPT_TemporalLoadCurrentReservoir(
        reservoirParams,
        bufferIndices,
        runtimeParams.reservoirPosition);
    if (!RBPT_TemporalUsefulReservoir(currentReservoir) ||
        params.maxHistoryLength == 0u ||
        params.maxReservoirAge == 0u)
    {
        return currentReservoir;
    }

    bool previousPositionValid = false;
    const uint2 previousReservoirPosition = RBPT_TemporalPreviousReservoirPosition(
        runtimeParams,
        params,
        rtxdiRuntimeParams,
        previousPositionValid);
    if (!previousPositionValid)
    {
        return currentReservoir;
    }

    RTXDI_PTReservoir previousReservoir = RBPT_TemporalLoadPreviousReservoir(
        reservoirParams,
        bufferIndices,
        previousReservoirPosition);
    if (!RBPT_TemporalReservoirsCompatible(currentReservoir, previousReservoir, runtimeParams, params))
    {
        return currentReservoir;
    }

    const uint maxHistoryLength = min(max(params.maxHistoryLength, 1u), RTXDI_PTReservoir::MaxM);
    previousReservoir.M = max(min(previousReservoir.M, maxHistoryLength), 1u);

    RTXDI_PTReservoir temporalReservoir = RTXDI_EmptyPTReservoir();
    float3 selectedTargetFunction = float3(0.0, 0.0, 0.0);
    if (CombineReservoirs(temporalReservoir, currentReservoir, RTXDI_GetNextRandom(rng), currentReservoir.TargetFunction))
    {
        selectedTargetFunction = currentReservoir.TargetFunction;
        selectedPrevSample = false;
    }

    if (CombineReservoirs(temporalReservoir, previousReservoir, RTXDI_GetNextRandom(rng), previousReservoir.TargetFunction))
    {
        selectedTargetFunction = previousReservoir.TargetFunction;
        selectedPrevSample = true;
    }

    const float currentTarget = RTXDI_Luminance(max(currentReservoir.TargetFunction, float3(0.0, 0.0, 0.0)));
    const float previousTarget = RTXDI_Luminance(max(previousReservoir.TargetFunction, float3(0.0, 0.0, 0.0)));
    const float targetFunctionSum =
        currentTarget * max((float)currentReservoir.M, 1.0) +
        previousTarget * max((float)previousReservoir.M, 1.0);
    const float selectedTarget = RTXDI_Luminance(max(selectedTargetFunction, float3(0.0, 0.0, 0.0)));
    RTXDI_FinalizeResampling(
        temporalReservoir,
        selectedTarget,
        targetFunctionSum * max(selectedTarget, 1.0e-6));
    if (!RBPT_TemporalUsefulReservoir(temporalReservoir))
    {
        selectedPrevSample = false;
        return currentReservoir;
    }

    temporalReservoir.M = min(max(temporalReservoir.M, currentReservoir.M), maxHistoryLength);
    if (selectedPrevSample)
    {
        temporalReservoir.Age = min(previousReservoir.Age + 1u, min(params.maxReservoirAge, RTXDI_PTRESERVOIR_AGE_MAX));
    }
    else
    {
        temporalReservoir.Age = currentReservoir.Age;
    }
    return temporalReservoir;
}

#endif
