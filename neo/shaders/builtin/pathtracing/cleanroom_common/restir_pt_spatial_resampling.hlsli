#ifndef RB_PATH_TRACING_CLEANROOM_COMMON_RESTIR_PT_SPATIAL_RESAMPLING_HLSLI
#define RB_PATH_TRACING_CLEANROOM_COMMON_RESTIR_PT_SPATIAL_RESAMPLING_HLSLI

#ifndef RBPT_RESTIR_PT_SPATIAL_MAX_SAMPLES
#define RBPT_RESTIR_PT_SPATIAL_MAX_SAMPLES 8u
#endif

#ifndef RBPT_RESTIR_PT_SPATIAL_ENHANCED_HOOKS
#define RBPT_RESTIR_PT_SPATIAL_ENHANCED_HOOKS 0
#endif

struct RTXDI_PTSpatialResamplingRuntimeParameters
{
    uint2 PixelPosition;
    uint2 ReservoirPosition;
    float3 cameraPos;
    float3 prevCameraPos;
    float3 prevPrevCameraPos;
};

RTXDI_PTSpatialResamplingRuntimeParameters RTXDI_EmptyPTSpatialResamplingRuntimeParameters()
{
    RTXDI_PTSpatialResamplingRuntimeParameters result = (RTXDI_PTSpatialResamplingRuntimeParameters)0;
    return result;
}

bool RBPT_RestirPTFiniteFloat(float value)
{
    return value == value && abs(value) < 1.0e20;
}

bool RBPT_RestirPTFiniteFloat3(float3 value)
{
    return all(value == value) && all(abs(value) < float3(1.0e20, 1.0e20, 1.0e20));
}

bool RBPT_RestirPTUsefulReservoir(RTXDI_PTReservoir reservoir)
{
    return RTXDI_IsValidPTReservoir(reservoir) &&
        RBPT_RestirPTFiniteFloat(reservoir.WeightSum) &&
        RBPT_RestirPTFiniteFloat3(reservoir.TargetFunction) &&
        RBPT_RestirPTFiniteFloat3(reservoir.Radiance) &&
        RBPT_RestirPTFiniteFloat3(reservoir.TranslatedWorldPosition) &&
        RBPT_RestirPTFiniteFloat3(reservoir.WorldNormal) &&
        RBPT_RestirPTFiniteFloat(reservoir.RcWiPdf) &&
        RBPT_RestirPTFiniteFloat(reservoir.PartialJacobian) &&
        RTXDI_Luminance(max(reservoir.TargetFunction, float3(0.0, 0.0, 0.0))) > 0.0;
}

bool RBPT_RestirPTReservoirsCompatible(
    RTXDI_PTReservoir currentReservoir,
    RTXDI_PTReservoir candidateReservoir,
    RTXDI_PTSpatialResamplingParameters params,
    RTXDI_PTSpatialResamplingRuntimeParameters runtimeParams)
{
    if (!RBPT_RestirPTUsefulReservoir(candidateReservoir))
    {
        return false;
    }

    const float3 currentNormal = normalize(currentReservoir.WorldNormal);
    const float3 candidateNormal = normalize(candidateReservoir.WorldNormal);
    if (!RBPT_RestirPTFiniteFloat3(currentNormal) || !RBPT_RestirPTFiniteFloat3(candidateNormal))
    {
        return false;
    }

    const float normalThreshold = params.normalThreshold > 0.0 ? params.normalThreshold : -1.0;
    if (dot(currentNormal, candidateNormal) < normalThreshold)
    {
        return false;
    }

    if (params.depthThreshold > 0.0)
    {
        const float currentDepth = length(currentReservoir.TranslatedWorldPosition - runtimeParams.cameraPos);
        const float candidateDepth = length(candidateReservoir.TranslatedWorldPosition - runtimeParams.cameraPos);
        const float depthDenominator = max(max(currentDepth, candidateDepth), 1.0e-3);
        if (abs(currentDepth - candidateDepth) / depthDenominator > params.depthThreshold)
        {
            return false;
        }
    }

    return true;
}

int2 RBPT_RestirPTSpatialNeighborPixel(
    RTXDI_PTSpatialResamplingRuntimeParameters runtimeParams,
    RTXDI_PTSpatialResamplingParameters params,
    uint neighborOffsetMask,
    uint sampleIndex,
    inout RTXDI_RandomSamplerState rng)
{
    const uint offsetMask = min(neighborOffsetMask, 31u);
    const uint offsetIndex = ((uint)(RTXDI_GetNextRandom(rng) * 65536.0) + sampleIndex) & offsetMask;
#ifdef RTXDI_NEIGHBOR_OFFSETS_BUFFER
    const float2 unitOffset = RTXDI_NEIGHBOR_OFFSETS_BUFFER[offsetIndex];
#else
    const float angle = RTXDI_GetNextRandom(rng) * 6.28318530718;
    const float2 unitOffset = float2(cos(angle), sin(angle));
#endif
    const float radius = max(1.0, sqrt(RTXDI_GetNextRandom(rng)) * max(params.samplingRadius, 1.0));
    const int2 pixelOffset = int2(round(unitOffset * radius));
    const int2 unclampedPixel = int2(runtimeParams.PixelPosition) + pixelOffset;
    return RAB_ClampSamplePositionIntoView(unclampedPixel, false);
}

RTXDI_PTReservoir RBPT_RestirPTLoadSpatialInputReservoir(
    RTXDI_ReservoirBufferParameters reservoirParams,
    RTXDI_PTBufferIndices bufferIndices,
    uint2 reservoirPosition)
{
    return RTXDI_LoadPTReservoir(
        reservoirParams,
        reservoirPosition,
        bufferIndices.spatialResamplingInputBufferIndex);
}

RTXDI_PTReservoir RTXDI_PTSpatialResampling(
    RTXDI_PTSpatialResamplingRuntimeParameters runtimeParams,
    RTXDI_PTSpatialResamplingParameters params,
    RTXDI_PTHybridShiftPerFrameParameters hybridShiftParams,
    RTXDI_PTReconnectionParameters reconnectionParams,
    RTXDI_ReservoirBufferParameters reservoirParams,
    RTXDI_PTBufferIndices bufferIndices,
    RTXDI_RuntimeParameters rtxdiRuntimeParams,
    inout RTXDI_RandomSamplerState rng,
    out bool spatialResampled,
    inout RAB_PathTracerUserData ptud)
{
    spatialResampled = false;

    const RTXDI_PTReservoir currentReservoir = RBPT_RestirPTLoadSpatialInputReservoir(
        reservoirParams,
        bufferIndices,
        runtimeParams.ReservoirPosition);
    if (!RBPT_RestirPTUsefulReservoir(currentReservoir) || params.numSpatialSamples == 0u)
    {
        return currentReservoir;
    }

    RTXDI_PTReservoir spatialReservoir = RTXDI_EmptyPTReservoir();
    float3 selectedTargetFunction = float3(0.0, 0.0, 0.0);
    if (CombineReservoirs(spatialReservoir, currentReservoir, RTXDI_GetNextRandom(rng), currentReservoir.TargetFunction))
    {
        selectedTargetFunction = currentReservoir.TargetFunction;
    }

    float targetFunctionSum = RTXDI_Luminance(currentReservoir.TargetFunction) * max((float)currentReservoir.M, 1.0);
    uint acceptedNeighborCount = 0u;
    const uint sampleCount = min(params.numSpatialSamples, RBPT_RESTIR_PT_SPATIAL_MAX_SAMPLES);

    [loop]
    for (uint sampleIndex = 0u; sampleIndex < sampleCount; ++sampleIndex)
    {
        const int2 neighborPixel = RBPT_RestirPTSpatialNeighborPixel(
            runtimeParams,
            params,
            rtxdiRuntimeParams.neighborOffsetMask,
            sampleIndex,
            rng);
        if (all(neighborPixel == int2(runtimeParams.PixelPosition)))
        {
            continue;
        }

        const uint2 neighborReservoirPosition = RTXDI_PixelPosToReservoirPos(uint2(neighborPixel), rtxdiRuntimeParams.activeCheckerboardField);
        const RTXDI_PTReservoir neighborReservoir = RBPT_RestirPTLoadSpatialInputReservoir(
            reservoirParams,
            bufferIndices,
            neighborReservoirPosition);
        if (!RBPT_RestirPTReservoirsCompatible(currentReservoir, neighborReservoir, params, runtimeParams))
        {
            continue;
        }

#if RBPT_RESTIR_PT_SPATIAL_ENHANCED_HOOKS
        // Reserved for future reciprocal/pairwise/adaptive-M experiments. The
        // compatibility path intentionally keeps this inactive by default.
#endif

        if (CombineReservoirs(spatialReservoir, neighborReservoir, RTXDI_GetNextRandom(rng), neighborReservoir.TargetFunction))
        {
            selectedTargetFunction = neighborReservoir.TargetFunction;
        }
        targetFunctionSum += RTXDI_Luminance(neighborReservoir.TargetFunction) * max((float)neighborReservoir.M, 1.0);
        ++acceptedNeighborCount;
    }

    if (acceptedNeighborCount == 0u)
    {
        return currentReservoir;
    }

    const float selectedTargetFunctionLuminance = RTXDI_Luminance(max(selectedTargetFunction, float3(0.0, 0.0, 0.0)));
    RTXDI_FinalizeResampling(
        spatialReservoir,
        selectedTargetFunctionLuminance,
        targetFunctionSum * max(selectedTargetFunctionLuminance, 1.0e-6));
    if (!RTXDI_IsValidPTReservoir(spatialReservoir))
    {
        return currentReservoir;
    }

    spatialReservoir.M = min(max(spatialReservoir.M, currentReservoir.M), RTXDI_PTReservoir::MaxM);
    spatialReservoir.Age = currentReservoir.Age;
    spatialReservoir.ShouldBoostSpatialSamples = currentReservoir.ShouldBoostSpatialSamples;
    spatialResampled = true;
    return spatialReservoir;
}

#endif
