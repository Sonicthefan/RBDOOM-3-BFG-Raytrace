// Purpose:
//     Supplies rbdoom data for local custom DI/GI ReSTIR debug reservoirs.
//
// NVIDIA reference:
//     E:\prog\references\RTXDI-main\Libraries\Rtxdi\Include\Rtxdi\PT\TemporalResampling.hlsli
//     E:\prog\references\RTXDI-main\Libraries\Rtxdi\Include\Rtxdi\PT\SpatialResampling.hlsli
//     E:\prog\references\RTXDI-main\Samples\FullSample\Shaders\LightingPasses\PT\TemporalResampling.hlsl
//     E:\prog\references\RTXDI-main\Samples\FullSample\Shaders\LightingPasses\PT\SpatialResampling.hlsl
//
// Current rbdoom supplier:
//     Local debug reservoir buffers, primary-surface history, motion vectors,
//     custom temporal/spatial accumulation, and DI/GI debug view dispatch.
//
// Current deviation:
//     This is rbdoom's local debug reservoir chain, not NVIDIA's reference PT
//     temporal/spatial reservoir path.
//
// Extraction rule:
//     This file must preserve existing behavior until a later correction task.

#ifndef RB_PATH_TRACING_RESTIR_LOCAL_DEBUG_RESERVOIRS_HLSLI
#define RB_PATH_TRACING_RESTIR_LOCAL_DEBUG_RESERVOIRS_HLSLI
uint RestirPTGiDebugView()
{
    return clamp((uint)max(RestirPTGiDebugInfo.x, 0.0), 0u, 4u);
}

uint RestirPTDiDebugView()
{
    return clamp((uint)max(RestirPTDiDebugInfo.x, 0.0), 0u, 67u);
}

bool RestirPTDiTemporalPrepassEnabled()
{
    return RestirPTDiDebugInfo.y >= 0.5;
}

uint RestirPTGiTemporalCurrentBufferIndex()
{
    return ((uint)max(RestirPTInfo.x, 0.0)) & 1u;
}

uint RestirPTGiTemporalPreviousBufferIndex()
{
    return 1u - RestirPTGiTemporalCurrentBufferIndex();
}

float RestirPTGiHash01(uint2 pixel, uint salt)
{
    uint h = pixel.x * 1664525u + pixel.y * 1013904223u + ((uint)max(RestirPTInfo.x, 0.0)) * 747796405u + salt;
    h ^= h >> 16;
    h *= 2246822519u;
    h ^= h >> 13;
    h *= 3266489917u;
    h ^= h >> 16;
    return ((float)(h & 0x00ffffffu) + 0.5) / 16777216.0;
}

uint RestirPTGiReservoirPointer(uint2 pixel, uint reservoirArrayIndex)
{
    const uint2 reservoirPosition = RTXDI_PixelPosToReservoirPos(pixel, 0u);
    return RTXDI_ReservoirPositionToPointer(RestirPTParams.reservoirBuffer, reservoirPosition, reservoirArrayIndex);
}

RTXDI_PTReservoir LoadRestirPTGiTemporalReservoir(uint2 pixel, uint reservoirArrayIndex)
{
    return RTXDI_UnpackPTReservoir(RestirPTGiReservoirs[RestirPTGiReservoirPointer(pixel, reservoirArrayIndex)]);
}

void StoreRestirPTGiTemporalReservoir(uint2 pixel, uint reservoirArrayIndex, RTXDI_PTReservoir reservoir)
{
    RestirPTGiReservoirs[RestirPTGiReservoirPointer(pixel, reservoirArrayIndex)] = RTXDI_PackPTReservoir(reservoir);
}

RTXDI_PTReservoir LoadRestirPTDiTemporalReservoir(uint2 pixel, uint reservoirArrayIndex)
{
    return RTXDI_UnpackPTReservoir(RestirPTDiReservoirs[RestirPTGiReservoirPointer(pixel, reservoirArrayIndex)]);
}

void StoreRestirPTDiTemporalReservoir(uint2 pixel, uint reservoirArrayIndex, RTXDI_PTReservoir reservoir)
{
    RestirPTDiReservoirs[RestirPTGiReservoirPointer(pixel, reservoirArrayIndex)] = RTXDI_PackPTReservoir(reservoir);
}

static const uint RESTIR_PT_TEMPORAL_UNSAFE_RESET_MASK =
    RT_RR_RESET_INVALID_SURFACE |
    RT_RR_RESET_MISSING_PREVIOUS |
    RT_RR_RESET_REJECTED_PREVIOUS |
    RT_RR_RESET_MATERIAL_MISMATCH |
    RT_RR_RESET_OBJECT_MOTION_UNAVAILABLE |
    RT_RR_RESET_STOCHASTIC_TRANSLUCENT |
    RT_RR_RESET_OTHER_INVALID;

bool RestirPTTryProjectMotionVectorToPreviousPixel(uint2 pixel, out int2 previousPixel, out float2 motionPixels)
{
    previousPixel = int2(-1, -1);
    motionPixels = float2(0.0, 0.0);

    const uint motionMask = PathTraceMotionVectorMask[pixel];
    if ((motionMask & PT_MOTION_VECTOR_MASK_VALID) == 0u)
    {
        return false;
    }

    const uint resetMask = PathTraceRRGuideResetMask[pixel];
    if ((resetMask & RESTIR_PT_TEMPORAL_UNSAFE_RESET_MASK) != 0u)
    {
        return false;
    }

    motionPixels = PathTraceMotionVectors[pixel].xy;
    const float2 previousPixelFloat = float2(pixel) + 0.5 + motionPixels;
    previousPixel = int2(floor(previousPixelFloat));

    const uint2 dimensions = PathTraceFullOutputSize();
    return previousPixel.x >= 0 && previousPixel.y >= 0 &&
        (uint)previousPixel.x < dimensions.x && (uint)previousPixel.y < dimensions.y;
}

bool RestirPTTemporalSamePixelFallbackAllowed(RAB_Surface surface, uint2 pixel, bool projected, float2 motionPixels)
{
    bool candidate = projected && dot(motionPixels, motionPixels) <= 0.25;
    if (!candidate)
    {
        const uint resetMask = PathTraceRRGuideResetMask[pixel];
        const uint hardResetMask = RESTIR_PT_TEMPORAL_UNSAFE_RESET_MASK & ~RT_RR_RESET_OBJECT_MOTION_UNAVAILABLE;
        candidate =
            (resetMask & hardResetMask) == 0u &&
            (resetMask & RT_RR_RESET_OBJECT_MOTION_UNAVAILABLE) != 0u;
    }

    if (!candidate)
    {
        return false;
    }

    const RAB_Surface previousSurface = LoadPathTracePrimarySurfaceRecord(int2(pixel), true);
    uint similarityStatus;
    return PathTracePrimarySurfacesAreSimilar(surface, previousSurface, similarityStatus);
}

RTXDI_PTReservoir GenerateRestirPTGiTemporalReservoir(RAB_Surface surface, uint2 pixel, out uint status)
{
    status = 0u;
    const uint currentIndex = RestirPTGiTemporalCurrentBufferIndex();
    const RTXDI_PTReservoir currentReservoir = LoadRestirPTInitialReservoir(pixel);
    if (!RAB_IsSurfaceValid(surface) || !RAB_SurfaceSupportsOpaqueDiffuseBrdf(surface) || !RestirPTReservoirHasUsefulSample(currentReservoir))
    {
        StoreRestirPTGiTemporalReservoir(pixel, currentIndex, RTXDI_EmptyPTReservoir());
        status = 1u;
        return RTXDI_EmptyPTReservoir();
    }

    RTXDI_PTReservoir temporalReservoir = RTXDI_EmptyPTReservoir();
    float3 selectedTargetFunction = float3(0.0, 0.0, 0.0);
    if (CombineReservoirs(temporalReservoir, currentReservoir, RestirPTGiHash01(pixel, 0x314159u), currentReservoir.TargetFunction))
    {
        selectedTargetFunction = currentReservoir.TargetFunction;
    }

    float piSum = RTXDI_Luminance(currentReservoir.TargetFunction) * max((float)currentReservoir.M, 1.0);
    int2 previousPixel;
    float2 motionPixels;
    const bool projected = RestirPTTryProjectMotionVectorToPreviousPixel(pixel, previousPixel, motionPixels);
    const bool samePixelFallbackAllowed = RestirPTTemporalSamePixelFallbackAllowed(surface, pixel, projected, motionPixels);
    bool acceptedPrevious = false;
    if (projected)
    {
        const RAB_Surface previousSurface = LoadPathTracePrimarySurfaceRecord(previousPixel, true);
        uint similarityStatus;
        if (PathTracePrimarySurfacesAreSimilar(surface, previousSurface, similarityStatus))
        {
            const RTXDI_PTReservoir previousReservoir = LoadRestirPTGiTemporalReservoir(uint2(previousPixel), RestirPTGiTemporalPreviousBufferIndex());
            if (RestirPTReservoirHasUsefulSample(previousReservoir))
            {
                acceptedPrevious = true;
                const uint cappedPreviousM = min(previousReservoir.M, RestirPTParams.temporalResampling.maxHistoryLength);
                RTXDI_PTReservoir previousCandidate = previousReservoir;
                previousCandidate.M = max(cappedPreviousM, 1u);
                if (CombineReservoirs(temporalReservoir, previousCandidate, RestirPTGiHash01(pixel, 0x271828u), previousCandidate.TargetFunction))
                {
                    selectedTargetFunction = previousCandidate.TargetFunction;
                }
                piSum += RTXDI_Luminance(previousCandidate.TargetFunction) * max((float)previousCandidate.M, 1.0);
            }
        }
    }
    if (!acceptedPrevious && samePixelFallbackAllowed)
    {
        const RTXDI_PTReservoir previousReservoir = LoadRestirPTGiTemporalReservoir(pixel, RestirPTGiTemporalPreviousBufferIndex());
        if (RestirPTReservoirHasUsefulSample(previousReservoir))
        {
            acceptedPrevious = true;
            const uint cappedPreviousM = min(previousReservoir.M, RestirPTParams.temporalResampling.maxHistoryLength);
            RTXDI_PTReservoir previousCandidate = previousReservoir;
            previousCandidate.M = max(cappedPreviousM, 1u);
            if (CombineReservoirs(temporalReservoir, previousCandidate, RestirPTGiHash01(pixel, 0x618033u), previousCandidate.TargetFunction))
            {
                selectedTargetFunction = previousCandidate.TargetFunction;
            }
            piSum += RTXDI_Luminance(previousCandidate.TargetFunction) * max((float)previousCandidate.M, 1.0);
        }
    }

    const float pi = RTXDI_Luminance(selectedTargetFunction);
    RTXDI_FinalizeResampling(temporalReservoir, pi, piSum * max(pi, 1.0e-6));
    temporalReservoir.M = min(max(temporalReservoir.M, currentReservoir.M), RestirPTParams.temporalResampling.maxHistoryLength);
    StoreRestirPTGiTemporalReservoir(pixel, currentIndex, temporalReservoir);
    status = acceptedPrevious ? 3u : 2u;
    return temporalReservoir;
}

float4 EvaluateRestirPTGiDebugView(RAB_Surface surface, uint2 pixel, uint view)
{
    if (view == 3u)
    {
        return float4(0.0, 0.0, 0.0, 1.0);
    }

    if (!RAB_IsSurfaceValid(surface) || !RAB_SurfaceSupportsOpaqueDiffuseBrdf(surface))
    {
        return view == 4u ? float4(0.20, 0.0, 0.0, 1.0) : float4(0.0, 0.0, 0.0, 1.0);
    }

    const RTXDI_PTReservoir reservoir = LoadRestirPTInitialReservoir(pixel);
    const bool valid = RestirPTReservoirHasUsefulSample(reservoir);

    if (view == 1u)
    {
        const float3 contribution = valid ? RestirPTReservoirPreviewContribution(reservoir) : float3(0.0, 0.0, 0.0);
        return float4(RestirPTToneMapPreview(contribution), 1.0);
    }

    if (view == 2u)
    {
        uint temporalStatus;
        const RTXDI_PTReservoir temporalReservoir = GenerateRestirPTGiTemporalReservoir(surface, pixel, temporalStatus);
        const bool temporalValid = RestirPTReservoirHasUsefulSample(temporalReservoir);
        const float3 contribution = temporalValid ? RestirPTReservoirPreviewContribution(temporalReservoir) : float3(0.0, 0.0, 0.0);
        return float4(RestirPTToneMapPreview(contribution), 1.0);
    }

    if (view == 4u)
    {
        const float mNorm = saturate((float)reservoir.M / max((float)RestirPTParams.initialSampling.numInitialSamples, 1.0));
        const float weightHeat = saturate(max(reservoir.WeightSum, 0.0) / (1.0 + max(reservoir.WeightSum, 0.0)));
        const float targetHeat = saturate(RTXDI_Luminance(max(reservoir.TargetFunction, float3(0.0, 0.0, 0.0))));
        return valid ? float4(targetHeat, mNorm, weightHeat, 1.0) : float4(0.35, mNorm * 0.25, weightHeat * 0.25, 1.0);
    }

    return float4(0.0, 0.0, 0.0, 1.0);
}

RTXDI_PTReservoir LoadRestirPTRawDirectReservoir(uint2 pixel)
{
    const uint2 reservoirPixel = PathTraceFullPixelToRestirDirectPixel(pixel);
    return LoadRestirPTSpatialOutputReservoir(reservoirPixel);
}

bool RestirPTDirectReservoirHasUsefulSample(RTXDI_PTReservoir reservoir)
{
    const float targetLuminance = RTXDI_Luminance(max(reservoir.TargetFunction, float3(0.0, 0.0, 0.0)));
    return RTXDI_IsValidPTReservoir(reservoir) &&
        RTXDI_ConnectsToNeeLight(reservoir) &&
        reservoir.WeightSum > 0.0 &&
        targetLuminance > 0.0;
}

RTXDI_PTReservoir GenerateRestirPTDiTemporalReservoir(RAB_Surface surface, uint2 pixel, out uint status)
{
    status = 0u;
    const uint currentIndex = RestirPTGiTemporalCurrentBufferIndex();
    const RTXDI_PTReservoir currentReservoir = LoadRestirPTRawDirectReservoir(pixel);
    if (!RAB_IsSurfaceValid(surface) || !RAB_SurfaceSupportsOpaqueDiffuseBrdf(surface))
    {
        StoreRestirPTDiTemporalReservoir(pixel, currentIndex, RTXDI_EmptyPTReservoir());
        status = 1u;
        return RTXDI_EmptyPTReservoir();
    }

    RTXDI_PTReservoir temporalReservoir = RTXDI_EmptyPTReservoir();
    float3 selectedTargetFunction = float3(0.0, 0.0, 0.0);
    const bool currentValid = RestirPTDirectReservoirHasUsefulSample(currentReservoir);
    if (currentValid && CombineReservoirs(temporalReservoir, currentReservoir, RestirPTGiHash01(pixel, 0x444901u), currentReservoir.TargetFunction))
    {
        selectedTargetFunction = currentReservoir.TargetFunction;
    }

    float piSum = currentValid ? RTXDI_Luminance(currentReservoir.TargetFunction) * max((float)currentReservoir.M, 1.0) : 0.0;
    int2 previousPixel;
    float2 motionPixels;
    const bool projected = RestirPTTryProjectMotionVectorToPreviousPixel(pixel, previousPixel, motionPixels);
    const bool samePixelFallbackAllowed = RestirPTTemporalSamePixelFallbackAllowed(surface, pixel, projected, motionPixels);
    bool acceptedPrevious = false;
    uint acceptedPreviousStatus = 0u;
    if (projected)
    {
        const RAB_Surface previousSurface = LoadPathTracePrimarySurfaceRecord(previousPixel, true);
        uint similarityStatus;
        if (PathTracePrimarySurfacesAreSimilar(surface, previousSurface, similarityStatus))
        {
            const RTXDI_PTReservoir previousReservoir = LoadRestirPTDiTemporalReservoir(uint2(previousPixel), RestirPTGiTemporalPreviousBufferIndex());
            if (RestirPTDirectReservoirHasUsefulSample(previousReservoir))
            {
                acceptedPrevious = true;
                acceptedPreviousStatus = 3u;
                const uint cappedPreviousM = min(previousReservoir.M, RestirPTParams.temporalResampling.maxHistoryLength);
                RTXDI_PTReservoir previousCandidate = previousReservoir;
                previousCandidate.M = max(cappedPreviousM, 1u);
                if (CombineReservoirs(temporalReservoir, previousCandidate, RestirPTGiHash01(pixel, 0x554411u), previousCandidate.TargetFunction))
                {
                    selectedTargetFunction = previousCandidate.TargetFunction;
                }
                piSum += RTXDI_Luminance(previousCandidate.TargetFunction) * max((float)previousCandidate.M, 1.0);
            }
        }
    }
    if (!acceptedPrevious && samePixelFallbackAllowed)
    {
        const RTXDI_PTReservoir previousReservoir = LoadRestirPTDiTemporalReservoir(pixel, RestirPTGiTemporalPreviousBufferIndex());
        if (RestirPTDirectReservoirHasUsefulSample(previousReservoir))
        {
            acceptedPrevious = true;
            acceptedPreviousStatus = 4u;
            const uint cappedPreviousM = min(previousReservoir.M, RestirPTParams.temporalResampling.maxHistoryLength);
            RTXDI_PTReservoir previousCandidate = previousReservoir;
            previousCandidate.M = max(cappedPreviousM, 1u);
            if (CombineReservoirs(temporalReservoir, previousCandidate, RestirPTGiHash01(pixel, 0x884221u), previousCandidate.TargetFunction))
            {
                selectedTargetFunction = previousCandidate.TargetFunction;
            }
            piSum += RTXDI_Luminance(previousCandidate.TargetFunction) * max((float)previousCandidate.M, 1.0);
        }
    }

    const float pi = RTXDI_Luminance(selectedTargetFunction);
    RTXDI_FinalizeResampling(temporalReservoir, pi, piSum * max(pi, 1.0e-6));
    if (!RTXDI_IsValidPTReservoir(temporalReservoir))
    {
        StoreRestirPTDiTemporalReservoir(pixel, currentIndex, RTXDI_EmptyPTReservoir());
        status = 1u;
        return RTXDI_EmptyPTReservoir();
    }

    const float currentM = currentValid ? currentReservoir.M : 0.0;
    temporalReservoir.M = min(max(temporalReservoir.M, currentM), RestirPTParams.temporalResampling.maxHistoryLength);
    StoreRestirPTDiTemporalReservoir(pixel, currentIndex, temporalReservoir);
    status = acceptedPrevious ? acceptedPreviousStatus : 2u;
    return temporalReservoir;
}

float4 RestirPTDiTemporalStatusColor(RTXDI_PTReservoir reservoir, uint status)
{
    const float history = saturate((float)reservoir.M / max((float)RestirPTParams.temporalResampling.maxHistoryLength, 1.0));
    if (status == 1u)
    {
        return float4(0.45, 0.02, 0.02, 1.0);
    }
    if (status == 3u)
    {
        return float4(0.05, 0.25 + 0.75 * history, 0.08, 1.0);
    }
    if (status == 4u)
    {
        return float4(0.85, 0.55 + 0.35 * history, 0.05, 1.0);
    }
    return float4(0.04, 0.10 + 0.35 * history, 0.75, 1.0);
}

float4 RestirPTDiSpatialStatusColor(RTXDI_PTReservoir reservoir, uint status)
{
    const float history = saturate((float)reservoir.M / max((float)RestirPTParams.temporalResampling.maxHistoryLength, 1.0));
    if (status == 1u)
    {
        return float4(0.45, 0.02, 0.02, 1.0);
    }
    if (status == 3u)
    {
        return float4(0.05, 0.65 + 0.35 * history, 0.75, 1.0);
    }
    return float4(0.04, 0.10 + 0.35 * history, 0.75, 1.0);
}

uint RestirPTLightManagerReservoirProbeHash(uint2 pixel, uint salt)
{
    uint h = pixel.x * 747796405u + pixel.y * 2891336453u + salt;
    h ^= h >> 16;
    h *= 2246822519u;
    h ^= h >> 13;
    h *= 3266489917u;
    h ^= h >> 16;
    return h;
}

bool RestirPTLightManagerReservoirProbeSelectLight(uint2 pixel, out uint selectedLightIndex)
{
    selectedLightIndex = PATH_TRACE_UNIFIED_LIGHT_INVALID_INDEX;
    if (!RAB_RestirLightManagerRABEnabled())
    {
        return false;
    }

    const uint currentCount = RAB_GetCurrentRestirLightManagerCount();
    if (currentCount == 0u)
    {
        return false;
    }

    const uint start = RestirPTLightManagerReservoirProbeHash(pixel, 0x52525859u) % currentCount;
    const uint probeCount = min(currentCount, 16u);
    for (uint probe = 0u; probe < probeCount; ++probe)
    {
        const uint lightIndex = (start + probe) % currentCount;
        const RAB_LightInfo lightInfo = RAB_LoadRestirLightManagerLightInfo(lightIndex, false);
        if (RAB_IsLightInfoValid(lightInfo))
        {
            selectedLightIndex = lightIndex;
            return true;
        }
    }

    return false;
}

bool RestirPTActiveRabTranslationParitySelectLight(uint2 pixel, out uint selectedLightIndex)
{
    selectedLightIndex = PATH_TRACE_UNIFIED_LIGHT_INVALID_INDEX;
    if (!RAB_RestirLightManagerRABEnabled())
    {
        return false;
    }

    const uint currentCount = RAB_GetCurrentRestirLightManagerCount();
    if (currentCount == 0u)
    {
        return false;
    }

    const uint start = RestirPTLightManagerReservoirProbeHash(pixel, 0x43504633u) % currentCount;
    const uint probeCount = min(currentCount, 16u);
    for (uint probe = 0u; probe < probeCount; ++probe)
    {
        const uint lightIndex = (start + probe) % currentCount;
        const RAB_LightInfo lightInfo = RAB_LoadLightInfo(lightIndex, false);
        if (RAB_IsLightInfoValid(lightInfo))
        {
            selectedLightIndex = lightIndex;
            return true;
        }
    }

    return false;
}

float3 RestirPTLightManagerParityIndexColor(uint index)
{
    uint h = index * 747796405u + 0x9e3779b9u;
    h ^= h >> 16;
    h *= 2246822519u;
    h ^= h >> 13;
    return float3(
        0.18 + 0.78 * (float)(h & 255u) / 255.0,
        0.18 + 0.78 * (float)((h >> 8) & 255u) / 255.0,
        0.18 + 0.78 * (float)((h >> 16) & 255u) / 255.0);
}

float4 RestirPTLightManagerParityStatusColor(
    bool currentLoadValid,
    int previousIndex,
    bool previousLoadValid,
    int roundTripCurrentIndex,
    uint currentLightIndex)
{
    if (!currentLoadValid)
    {
        return float4(0.85, 0.02, 0.02, 1.0);
    }
    if (previousIndex < 0)
    {
        return float4(0.05, 0.24, 0.82, 1.0);
    }
    if (!previousLoadValid)
    {
        return float4(0.55, 0.05, 0.80, 1.0);
    }
    if (roundTripCurrentIndex != int(currentLightIndex))
    {
        return float4(0.95, 0.55, 0.04, 1.0);
    }
    return float4(0.05, 0.75, 0.12, 1.0);
}

float4 EvaluateRestirPTActiveRabTranslationParityView(uint2 pixel)
{
    if (!RAB_RestirLightManagerRABEnabled())
    {
        return float4(0.85, 0.02, 0.02, 1.0);
    }

    uint currentLightIndex;
    if (!RestirPTActiveRabTranslationParitySelectLight(pixel, currentLightIndex))
    {
        return float4(0.85, 0.02, 0.02, 1.0);
    }

    const RAB_LightInfo currentLightInfo = RAB_LoadLightInfo(currentLightIndex, false);
    const bool currentLoadValid = RAB_IsLightInfoValid(currentLightInfo);
    const int previousIndex = currentLoadValid ? RAB_TranslateLightIndex(currentLightIndex, true) : -1;
    RAB_LightInfo previousLightInfo = RAB_EmptyLightInfo();
    if (previousIndex >= 0)
    {
        previousLightInfo = RAB_LoadLightInfo((uint)previousIndex, true);
    }
    const bool previousLoadValid = previousIndex >= 0 && RAB_IsLightInfoValid(previousLightInfo);
    const int roundTripCurrentIndex = previousLoadValid ? RAB_TranslateLightIndex((uint)previousIndex, false) : -1;

    const uint cellSize = 12u;
    const uint2 localPixel = pixel % cellSize;
    const uint paneX = min(localPixel.x / 4u, 2u);
    const uint paneY = localPixel.y >= (cellSize / 2u) ? 1u : 0u;
    if (paneY == 0u && paneX == 0u)
    {
        return currentLoadValid
            ? float4(RestirPTLightManagerParityIndexColor(currentLightIndex), 1.0)
            : float4(0.85, 0.02, 0.02, 1.0);
    }
    if (paneY == 0u && paneX == 1u)
    {
        return previousIndex >= 0
            ? float4(RestirPTLightManagerParityIndexColor((uint)previousIndex), 1.0)
            : float4(0.05, 0.24, 0.82, 1.0);
    }
    if (paneY == 0u)
    {
        if (previousIndex >= 0 && !previousLoadValid)
        {
            return float4(0.55, 0.05, 0.80, 1.0);
        }
        return previousLoadValid
            ? float4(0.05, 0.75, 0.12, 1.0)
            : float4(0.05, 0.24, 0.82, 1.0);
    }
    if (paneX == 0u)
    {
        return roundTripCurrentIndex >= 0
            ? float4(RestirPTLightManagerParityIndexColor((uint)roundTripCurrentIndex), 1.0)
            : float4(0.95, 0.55, 0.04, 1.0);
    }
    if (paneX == 1u)
    {
        return RestirPTLightManagerParityStatusColor(
            currentLoadValid,
            previousIndex,
            previousLoadValid,
            roundTripCurrentIndex,
            currentLightIndex);
    }
    return float4(
        currentLoadValid ? 0.05 : 0.85,
        previousLoadValid ? 0.75 : 0.05,
        roundTripCurrentIndex == int(currentLightIndex) ? 0.12 : 0.82,
        1.0);
}

RTXDI_PTReservoir RestirPTBuildLightManagerReservoirProbe(uint lightIndex, uint m)
{
    RTXDI_PTReservoir reservoir = RTXDI_EmptyPTReservoir();
    reservoir.M = max(m, 1u);
    reservoir.WeightSum = 1.0;
    reservoir.TargetFunction = float3(0.0, 1.0, 0.0);
    reservoir.Radiance = float3(asfloat(0x7f800000u), asfloat(RTXDI_LightIndexToLightData(lightIndex)), 0.0);
    reservoir.PathLength = 1u;
    reservoir.RcVertexLength = 1u;
    return reservoir;
}

uint RestirPTLightManagerReservoirProbeStoredLight(RTXDI_PTReservoir reservoir)
{
    if (!RTXDI_IsValidPTReservoir(reservoir) || !RTXDI_ConnectsToNeeLight(reservoir))
    {
        return PATH_TRACE_UNIFIED_LIGHT_INVALID_INDEX;
    }
    return RTXDI_SampledLightData_GetLightIndex(RTXDI_GetSampledLightData(reservoir));
}

float4 EvaluateRestirPTLightManagerReservoirProbeView(uint2 pixel)
{
    const uint currentPage = RestirPTGiTemporalCurrentBufferIndex();
    const uint previousPage = RestirPTGiTemporalPreviousBufferIndex();

    uint currentLightIndex;
    if (!RestirPTLightManagerReservoirProbeSelectLight(pixel, currentLightIndex))
    {
        StoreRestirPTDiTemporalReservoir(pixel, currentPage, RTXDI_EmptyPTReservoir());
        return RAB_RestirLightManagerRABEnabled()
            ? float4(0.25, 0.05, 0.45, 1.0)
            : float4(0.45, 0.02, 0.02, 1.0);
    }

    const int previousIndex = RAB_TranslateRestirLightManagerIndex(currentLightIndex, true);
    if (previousIndex < 0)
    {
        StoreRestirPTDiTemporalReservoir(pixel, currentPage, RestirPTBuildLightManagerReservoirProbe(currentLightIndex, 1u));
        return float4(0.90, 0.16, 0.04, 1.0);
    }

    const RTXDI_PTReservoir previousReservoir = LoadRestirPTDiTemporalReservoir(pixel, previousPage);
    const uint previousStoredLight = RestirPTLightManagerReservoirProbeStoredLight(previousReservoir);
    bool acceptedPrevious = false;
    if (previousStoredLight != PATH_TRACE_UNIFIED_LIGHT_INVALID_INDEX)
    {
        const int remappedCurrent = RAB_TranslateRestirLightManagerIndex(previousStoredLight, false);
        acceptedPrevious = remappedCurrent == int(currentLightIndex);
    }

    const uint previousM = RTXDI_IsValidPTReservoir(previousReservoir) ? (uint)previousReservoir.M : 0u;
    const uint maxHistory = max(RestirPTParams.temporalResampling.maxHistoryLength, 1u);
    const uint nextM = acceptedPrevious ? min(previousM + 1u, maxHistory) : 1u;
    StoreRestirPTDiTemporalReservoir(pixel, currentPage, RestirPTBuildLightManagerReservoirProbe(currentLightIndex, nextM));

    const float history = saturate((float)nextM / (float)maxHistory);
    if (acceptedPrevious)
    {
        return float4(0.02, 0.22 + 0.78 * history, 0.08, 1.0);
    }
    if (previousM == 0u)
    {
        return float4(0.05, 0.24, 0.82, 1.0);
    }
    return float4(0.95, 0.62, 0.04, 1.0);
}

static const uint RESTIR_PT_RRX_DI_RESERVOIR_AVAILABLE = 1u << 0;
static const uint RESTIR_PT_RRX_DI_RESERVOIR_CLEAR_PENDING = 1u << 1;
static const uint RESTIR_PT_RRX_DI_RESERVOIR_BAD_DIMENSIONS = 1u << 2;
static const uint RESTIR_PT_RRX_DI_PROBE_MAGIC_UV = 0x6a5cc35au;

bool RestirPTRrxDiReservoirAvailable()
{
    const uint flags = RestirPTRemixDiReservoirInfo.w;
    return (flags & RESTIR_PT_RRX_DI_RESERVOIR_AVAILABLE) != 0u &&
        (flags & (RESTIR_PT_RRX_DI_RESERVOIR_CLEAR_PENDING | RESTIR_PT_RRX_DI_RESERVOIR_BAD_DIMENSIONS)) == 0u &&
        RestirPTRemixDiReservoirInfo.x > 0u &&
        RestirPTRemixDiReservoirInfo.y > 0u &&
        RestirPTRemixDiReservoirInfo.z >= 2u;
}

RTXDI_ReservoirBufferParameters RestirPTRrxDiReservoirParams()
{
    RTXDI_ReservoirBufferParameters params;
    params.reservoirBlockRowPitch = RestirPTRemixDiReservoirInfo.x;
    params.reservoirArrayPitch = RestirPTRemixDiReservoirInfo.y;
    params.pad1 = 0u;
    params.pad2 = 0u;
    return params;
}

uint RestirPTRrxDiReservoirPointer(uint2 pixel, uint reservoirArrayIndex)
{
    return RTXDI_ReservoirPositionToPointer(RestirPTRrxDiReservoirParams(), pixel, reservoirArrayIndex);
}

RTXDI_DIReservoir LoadRestirPTRrxDiReservoir(uint2 pixel, uint reservoirArrayIndex)
{
    return RTXDI_UnpackDIReservoir(RemixRtxdiDiReservoirs[RestirPTRrxDiReservoirPointer(pixel, reservoirArrayIndex)]);
}

RTXDI_PackedDIReservoir LoadRestirPTRrxPackedDiReservoir(uint2 pixel, uint reservoirArrayIndex)
{
    return RemixRtxdiDiReservoirs[RestirPTRrxDiReservoirPointer(pixel, reservoirArrayIndex)];
}

void StoreRestirPTRrxDiReservoir(uint2 pixel, uint reservoirArrayIndex, RTXDI_DIReservoir reservoir)
{
    RemixRtxdiDiReservoirs[RestirPTRrxDiReservoirPointer(pixel, reservoirArrayIndex)] = RTXDI_PackDIReservoir(reservoir);
}

bool RestirPTRrxDiProbeSelectSingleLight(out uint selectedLightIndex)
{
    selectedLightIndex = PATH_TRACE_UNIFIED_LIGHT_INVALID_INDEX;
    if (!RAB_RestirLightManagerRABEnabled())
    {
        return false;
    }

    const uint currentCount = RAB_GetCurrentRestirLightManagerCount();
    const uint probeCount = min(currentCount, 4096u);
    for (uint lightIndex = 0u; lightIndex < probeCount; ++lightIndex)
    {
        const int previousIndex = RAB_TranslateRestirLightManagerIndex(lightIndex, true);
        if (previousIndex < 0)
        {
            continue;
        }

        const RAB_LightInfo lightInfo = RAB_LoadRestirLightManagerLightInfo(lightIndex, false);
        if (RAB_IsLightInfoValid(lightInfo))
        {
            selectedLightIndex = lightIndex;
            return true;
        }
    }

    return false;
}

RTXDI_DIReservoir RestirPTBuildRrxDiReservoirProbe(uint lightIndex, uint m)
{
    RTXDI_DIReservoir reservoir = RTXDI_EmptyDIReservoir();
    reservoir.lightData = (lightIndex & RTXDI_DIReservoir_LightIndexMask) | RTXDI_DIReservoir_LightValidBit;
    reservoir.uvData = RESTIR_PT_RRX_DI_PROBE_MAGIC_UV;
    reservoir.weightSum = 1.0;
    reservoir.targetPdf = 1.0;
    reservoir.M = (float)max(m, 1u);
    reservoir.packedVisibility = RTXDI_PackedDIReservoir_VisibilityMask;
    reservoir.spatialDistance = int2(0, 0);
    reservoir.age = 0u;
    reservoir.canonicalWeight = 1.0;
    return reservoir;
}

bool RestirPTRrxDiReservoirHasProbeMagic(RTXDI_DIReservoir reservoir)
{
    return reservoir.uvData == RESTIR_PT_RRX_DI_PROBE_MAGIC_UV;
}

bool RestirPTRrxPackedDiReservoirNonZero(RTXDI_PackedDIReservoir reservoir)
{
    return reservoir.lightData != 0u ||
        reservoir.uvData != 0u ||
        reservoir.mVisibility != 0u ||
        reservoir.distanceAge != 0u ||
        reservoir.targetPdf != 0.0 ||
        reservoir.weight != 0.0;
}

float4 EvaluateRestirPTRrxDiRawReservoirHeartbeatView(uint2 pixel)
{
    if (!RestirPTRrxDiReservoirAvailable())
    {
        return float4(0.85, 0.02, 0.02, 1.0);
    }

    const RTXDI_PackedDIReservoir previousPackedReservoir = LoadRestirPTRrxPackedDiReservoir(pixel, RestirPTRemixDiReservoirPageInfo.y);
    const RTXDI_DIReservoir previousReservoir = RTXDI_UnpackDIReservoir(previousPackedReservoir);
    const bool previousNonZero = RestirPTRrxPackedDiReservoirNonZero(previousPackedReservoir);
    const bool previousProbeValid =
        RTXDI_IsValidDIReservoir(previousReservoir) &&
        previousReservoir.M > 0.0 &&
        RestirPTRrxDiReservoirHasProbeMagic(previousReservoir);

    const uint previousM = previousProbeValid ? (uint)previousReservoir.M : 0u;
    const uint maxHistory = max(RestirPTParams.temporalResampling.maxHistoryLength, 1u);
    const uint nextM = previousProbeValid ? min(previousM + 1u, maxHistory) : 1u;
    StoreRestirPTRrxDiReservoir(pixel, RestirPTRemixDiReservoirPageInfo.x, RestirPTBuildRrxDiReservoirProbe(0u, nextM));

    if (previousProbeValid)
    {
        const float history = saturate((float)nextM / (float)maxHistory);
        return float4(0.02, 0.22 + 0.78 * history, 0.08, 1.0);
    }
    if (!previousNonZero)
    {
        return float4(0.05, 0.24, 0.82, 1.0);
    }
    return float4(0.95, 0.50, 0.04, 1.0);
}

float4 EvaluateRestirPTRrxDiSameDispatchEchoView(uint2 pixel)
{
    if (!RestirPTRrxDiReservoirAvailable())
    {
        return float4(0.85, 0.02, 0.02, 1.0);
    }

    const uint echoM = 11u;
    const uint echoLightIndex = 0u;
    StoreRestirPTRrxDiReservoir(pixel, RestirPTRemixDiReservoirPageInfo.x, RestirPTBuildRrxDiReservoirProbe(echoLightIndex, echoM));

    const RTXDI_DIReservoir echoedReservoir = LoadRestirPTRrxDiReservoir(pixel, RestirPTRemixDiReservoirPageInfo.x);
    const bool echoValid =
        RTXDI_IsValidDIReservoir(echoedReservoir) &&
        RestirPTRrxDiReservoirHasProbeMagic(echoedReservoir) &&
        RTXDI_GetDIReservoirLightIndex(echoedReservoir) == echoLightIndex &&
        ((uint)echoedReservoir.M) == echoM &&
        echoedReservoir.weightSum == 1.0 &&
        echoedReservoir.targetPdf == 1.0;

    return echoValid
        ? float4(0.02, 0.95, 0.08, 1.0)
        : float4(0.95, 0.50, 0.04, 1.0);
}

float4 RestirPTRrxDiRawFieldQuadrantColor(uint2 pixel, RTXDI_PackedDIReservoir packedReservoir, RTXDI_DIReservoir reservoir)
{
    const uint2 dimensions = max(PathTraceFullOutputSize(), uint2(1u, 1u));
    const bool right = pixel.x >= dimensions.x / 2u;
    const bool bottom = pixel.y >= dimensions.y / 2u;

    if (!bottom && !right)
    {
        const bool packedNonZero = RestirPTRrxPackedDiReservoirNonZero(packedReservoir);
        return packedNonZero ? float4(1.0, 1.0, 1.0, 1.0) : float4(0.0, 0.0, 0.0, 1.0);
    }

    if (!bottom && right)
    {
        const bool valid = RTXDI_IsValidDIReservoir(reservoir);
        const bool lightDataPresent = packedReservoir.lightData != 0u;
        const float lightIndexBand = valid ? frac((float)RTXDI_GetDIReservoirLightIndex(reservoir) / 17.0) : 0.0;
        return float4(valid ? 1.0 : 0.0, lightDataPresent ? 1.0 : 0.0, lightIndexBand, 1.0);
    }

    if (bottom && !right)
    {
        if (RestirPTRrxDiReservoirHasProbeMagic(reservoir))
        {
            return float4(0.05, 0.35, 1.0, 1.0);
        }
        return packedReservoir.uvData != 0u ? float4(0.95, 0.50, 0.04, 1.0) : float4(0.0, 0.0, 0.0, 1.0);
    }

    const float m = saturate(reservoir.M / max((float)RestirPTParams.temporalResampling.maxHistoryLength, 1.0));
    return float4(reservoir.weightSum > 0.0 ? 1.0 : 0.0, m, reservoir.targetPdf > 0.0 ? 1.0 : 0.0, 1.0);
}

float4 EvaluateRestirPTRrxDiSamePageRawFieldsView(uint2 pixel)
{
    if (!RestirPTRrxDiReservoirAvailable())
    {
        return float4(0.85, 0.02, 0.02, 1.0);
    }

    const uint echoM = 11u;
    StoreRestirPTRrxDiReservoir(pixel, RestirPTRemixDiReservoirPageInfo.x, RestirPTBuildRrxDiReservoirProbe(0u, echoM));

    const RTXDI_PackedDIReservoir packedReservoir = LoadRestirPTRrxPackedDiReservoir(pixel, RestirPTRemixDiReservoirPageInfo.x);
    const RTXDI_DIReservoir reservoir = RTXDI_UnpackDIReservoir(packedReservoir);
    return RestirPTRrxDiRawFieldQuadrantColor(pixel, packedReservoir, reservoir);
}

float4 EvaluateRestirPTRrxDiPreviousPageRawFieldsView(uint2 pixel)
{
    if (!RestirPTRrxDiReservoirAvailable())
    {
        return float4(0.85, 0.02, 0.02, 1.0);
    }

    const RTXDI_PackedDIReservoir packedReservoir = LoadRestirPTRrxPackedDiReservoir(pixel, RestirPTRemixDiReservoirPageInfo.y);
    const RTXDI_DIReservoir reservoir = RTXDI_UnpackDIReservoir(packedReservoir);
    const uint nextM = RTXDI_IsValidDIReservoir(reservoir) && RestirPTRrxDiReservoirHasProbeMagic(reservoir)
        ? min((uint)reservoir.M + 1u, max(RestirPTParams.temporalResampling.maxHistoryLength, 1u))
        : 1u;
    StoreRestirPTRrxDiReservoir(pixel, RestirPTRemixDiReservoirPageInfo.x, RestirPTBuildRrxDiReservoirProbe(0u, nextM));
    return RestirPTRrxDiRawFieldQuadrantColor(pixel, packedReservoir, reservoir);
}

float4 EvaluateRestirPTRrxDiSingleLightHeartbeatView(uint2 pixel)
{
    if (!RestirPTRrxDiReservoirAvailable())
    {
        return float4(0.85, 0.02, 0.02, 1.0);
    }

    const RTXDI_DIReservoir previousReservoir = LoadRestirPTRrxDiReservoir(pixel, RestirPTRemixDiReservoirPageInfo.y);
    const bool previousValid =
        RTXDI_IsValidDIReservoir(previousReservoir) &&
        previousReservoir.M > 0.0 &&
        RestirPTRrxDiReservoirHasProbeMagic(previousReservoir);

    uint currentLightIndex;
    if (!RestirPTRrxDiProbeSelectSingleLight(currentLightIndex))
    {
        StoreRestirPTRrxDiReservoir(pixel, RestirPTRemixDiReservoirPageInfo.x, RTXDI_EmptyDIReservoir());
        return RAB_RestirLightManagerRABEnabled()
            ? float4(0.25, 0.05, 0.45, 1.0)
            : float4(0.85, 0.02, 0.02, 1.0);
    }

    bool acceptedPrevious = false;
    if (previousValid)
    {
        const uint previousLightIndex = RTXDI_GetDIReservoirLightIndex(previousReservoir);
        const int remappedCurrent = RAB_TranslateRestirLightManagerIndex(previousLightIndex, false);
        acceptedPrevious = remappedCurrent == int(currentLightIndex);
    }

    const uint previousM = previousValid ? (uint)previousReservoir.M : 0u;
    const uint maxHistory = max(RestirPTParams.temporalResampling.maxHistoryLength, 1u);
    const uint nextM = acceptedPrevious ? min(previousM + 1u, maxHistory) : 1u;
    StoreRestirPTRrxDiReservoir(pixel, RestirPTRemixDiReservoirPageInfo.x, RestirPTBuildRrxDiReservoirProbe(currentLightIndex, nextM));

    const float history = saturate((float)nextM / (float)maxHistory);
    if (acceptedPrevious && nextM > previousM)
    {
        return float4(0.02, 0.22 + 0.78 * history, 0.08, 1.0);
    }
    if (!previousValid)
    {
        return float4(0.05, 0.24, 0.82, 1.0);
    }
    return float4(0.95, 0.50, 0.04, 1.0);
}

RTXDI_PTReservoir GenerateRestirPTDiSpatialReservoir(RAB_Surface surface, uint2 pixel, out uint status)
{
    status = 0u;
    const RTXDI_PTReservoir temporalReservoir = LoadRestirPTDiTemporalReservoir(pixel, RestirPTGiTemporalCurrentBufferIndex());
    if (!RestirPTDirectReservoirHasUsefulSample(temporalReservoir))
    {
        status = 1u;
        return RTXDI_EmptyPTReservoir();
    }

    RTXDI_PTReservoir spatialReservoir = RTXDI_EmptyPTReservoir();
    float3 selectedTargetFunction = float3(0.0, 0.0, 0.0);
    float3 currentTargetFunction;
    if (!RestirPTTryEvaluateNeeReservoirTargetFunction(surface, temporalReservoir, RestirPTInfo.z >= 0.5, currentTargetFunction))
    {
        currentTargetFunction = temporalReservoir.TargetFunction;
    }
    if (CombineReservoirs(spatialReservoir, temporalReservoir, RestirPTGiHash01(pixel, 0x392211u), currentTargetFunction))
    {
        selectedTargetFunction = currentTargetFunction;
    }

    float piSum = RTXDI_Luminance(currentTargetFunction) * max((float)temporalReservoir.M, 1.0);
    uint acceptedNeighbors = 0u;
    const uint2 dimensions = PathTraceFullOutputSize();
    const uint sampleCount = clamp(RestirPTParams.spatialResampling.numSpatialSamples, 1u, 8u);
    const float sampleRadius = max(RestirPTParams.spatialResampling.samplingRadius, 1.0);

    [loop]
    for (uint sampleIndex = 0u; sampleIndex < sampleCount; ++sampleIndex)
    {
        const float angle = RestirPTGiHash01(pixel, 0x6123a1u + sampleIndex * 17u) * 6.28318530718;
        const float radius = max(1.0, sqrt(RestirPTGiHash01(pixel, 0x9427c3u + sampleIndex * 31u)) * sampleRadius);
        int2 neighborPixel = int2(pixel) + int2(round(float2(cos(angle), sin(angle)) * radius));
        neighborPixel = clamp(neighborPixel, int2(0, 0), int2(dimensions) - int2(1, 1));
        if (all(neighborPixel == int2(pixel)))
        {
            continue;
        }

        const RAB_Surface neighborSurface = LoadPathTracePrimarySurfaceRecord(neighborPixel, false);
        uint similarityStatus;
        if (!PathTracePrimarySurfacesAreSimilar(surface, neighborSurface, similarityStatus))
        {
            continue;
        }

        const RTXDI_PTReservoir neighborReservoir = LoadRestirPTDiTemporalReservoir(uint2(neighborPixel), RestirPTGiTemporalCurrentBufferIndex());
        if (!RestirPTDirectReservoirHasUsefulSample(neighborReservoir))
        {
            continue;
        }

        float3 neighborTargetFunction;
        if (!RestirPTTryEvaluateNeeReservoirTargetFunction(surface, neighborReservoir, RestirPTInfo.z >= 0.5, neighborTargetFunction))
        {
            continue;
        }

        if (CombineReservoirs(spatialReservoir, neighborReservoir, RestirPTGiHash01(pixel, 0x827331u + sampleIndex * 43u), neighborTargetFunction))
        {
            selectedTargetFunction = neighborTargetFunction;
        }
        piSum += RTXDI_Luminance(neighborTargetFunction) * max((float)neighborReservoir.M, 1.0);
        ++acceptedNeighbors;
    }

    const float pi = RTXDI_Luminance(selectedTargetFunction);
    RTXDI_FinalizeResampling(spatialReservoir, pi, piSum * max(pi, 1.0e-6));
    spatialReservoir.M = min(max(spatialReservoir.M, temporalReservoir.M), RestirPTParams.temporalResampling.maxHistoryLength);
    status = acceptedNeighbors > 0u ? 3u : 2u;
    return spatialReservoir;
}

float4 EvaluateRestirPTDiDebugView(RAB_Surface surface, uint2 pixel, uint view)
{
    if (view == 13u)
    {
        return EvaluateRestirPTReferenceBrdfContractView(surface);
    }
    if (view == 14u)
    {
        return EvaluateRestirPTReferenceNeeSampleContractView(surface, pixel);
    }
    if (view == 15u)
    {
        return EvaluateRestirPTReferencePathTraceMetadataView(pixel);
    }
    if (view == 16u)
    {
        const uint2 reservoirPixel = PathTraceFullPixelToRestirDirectPixel(pixel);
        const RTXDI_PTReservoir reservoir = LoadRestirPTFinalShadingInputReservoir(reservoirPixel);
        return RestirPTReferenceFinalShadingContractColor(surface, reservoir);
    }
    if (view == 17u)
    {
        const uint2 reservoirPixel = PathTraceFullPixelToRestirDirectPixel(pixel);
        const RTXDI_PTReservoir reservoir = LoadRestirPTFinalShadingInputReservoir(reservoirPixel);
        return RestirPTReferenceTargetReplayColor(surface, reservoir);
    }
    if (view == 18u)
    {
        return EvaluateRestirPTEnvironmentMisContractView(surface, pixel);
    }
    if (view == 19u)
    {
        const uint2 reservoirPixel = PathTraceFullPixelToRestirDirectPixel(pixel);
        const RTXDI_PTReservoir reservoir = LoadRestirPTFinalShadingInputReservoir(reservoirPixel);
        return RestirPTReferenceNeeSampleContractColor(surface, reservoir);
    }
    if (view == 20u)
    {
        const uint2 reservoirPixel = PathTraceFullPixelToRestirDirectPixel(pixel);
        const RTXDI_PTReservoir reservoir = LoadRestirPTFinalShadingInputReservoir(reservoirPixel);
        return RestirPTReferencePathTraceMetadataColor(reservoir);
    }
    if (view == 21u)
    {
        const uint2 reservoirPixel = PathTraceFullPixelToRestirDirectPixel(pixel);
        const RTXDI_PTReservoir reservoir = LoadRestirPTFinalShadingInputReservoir(reservoirPixel);
        return RestirPTReferenceRandomReplayMetadataColor(reservoir);
    }
    if (view == 22u)
    {
        return RestirPTReferenceRandomSamplerBridgeColor(pixel);
    }
    if (view == 23u)
    {
        return RestirPTReferencePathTracerUserDataBridgeColor();
    }
    if (view == 24u)
    {
        const uint2 reservoirPixel = PathTraceFullPixelToRestirDirectPixel(pixel);
        const RTXDI_PTReservoir reservoir = LoadRestirPTFinalShadingInputReservoir(reservoirPixel);
        return RestirPTReferenceVisibilityPolicyColor(surface, reservoir);
    }
    if (view == 25u)
    {
        const uint2 reservoirPixel = PathTraceFullPixelToRestirDirectPixel(pixel);
        const RTXDI_PTReservoir reservoir = LoadRestirPTFinalShadingInputReservoir(reservoirPixel);
        return RestirPTReferenceLightSampleNumericColor(surface, reservoir);
    }
    if (view == 26u)
    {
        return RestirPTReferenceEmissiveHitMapColor(surface);
    }
    if (view == 27u)
    {
        return RestirPTReferenceLightDomainLoadColor(pixel);
    }
    if (view == 28u)
    {
        return RestirPTReferenceLightDomainSampleColor(surface, pixel);
    }
    if (view == 29u)
    {
        return RestirPTReferenceLightDomainVisibilityColor(surface, pixel);
    }
    if (view == 30u)
    {
        const uint2 reservoirPixel = PathTraceFullPixelToRestirDirectPixel(pixel);
        const RTXDI_PTReservoir reservoir = LoadRestirPTInitialDirectReservoir(reservoirPixel);
        return RestirPTReferenceNeeRecordScalarColor(reservoir);
    }
    if (view == 31u)
    {
        return RestirPTReferencePathTracerEntryColor(surface, pixel);
    }
    if (view == 32u)
    {
        return RestirPTReferencePsrDenoiserPayloadColor(pixel);
    }
    if (view == 33u)
    {
        return RestirPTReferencePathTracerContinuationPolicyColor(pixel);
    }
    if (view == 34u)
    {
        return RestirPTReferenceFinalShadingOutputColor(surface, pixel);
    }
    if (view == 35u)
    {
        return RestirPTReferenceRemainingSupplierGapColor(pixel);
    }
    if (view == 36u)
    {
        return EvaluateRestirPTUnifiedLightTypeView(pixel);
    }
    if (view == 37u)
    {
        return EvaluateRestirPTUnifiedLightRadianceView(pixel);
    }
    if (view == 38u)
    {
        return EvaluateRestirPTUnifiedLightRemapView(pixel);
    }
    if (view == 39u)
    {
        return EvaluateRestirPTUnifiedLightNumericView(pixel);
    }
    if (view == 40u)
    {
        return EvaluateRestirPTCpuUnifiedLightTypeView(pixel);
    }
    if (view == 41u)
    {
        return EvaluateRestirPTCpuUnifiedLightCompareView(pixel);
    }
    if (view == 42u)
    {
        return EvaluateRestirPTCpuUnifiedLightRemapView(pixel);
    }
    if (view == 43u)
    {
        return EvaluateRestirPTUnifiedLoadCurrentCompareView(pixel);
    }
    if (view == 44u)
    {
        return EvaluateRestirPTUnifiedLoadPreviousCompareView(pixel);
    }
    if (view == 45u)
    {
        return EvaluateRestirPTUnifiedSampleCompareView(surface, pixel);
    }
    if (view == 46u)
    {
        return EvaluateRestirPTUnifiedSampleNumericView(surface, pixel);
    }
    if (view == 47u)
    {
        return EvaluateRestirPTDiInitialSampleValidityView(surface, pixel);
    }
    if (view == 48u)
    {
        return EvaluateRestirPTDiInitialContributionView(surface, pixel);
    }
    if (view == 49u)
    {
        return EvaluateRestirPTDiInitialNumericView(surface, pixel);
    }
    if (view == 50u)
    {
        return EvaluateRestirPTDiInitialVisibilityView(surface, pixel);
    }
    if (view == 51u)
    {
        return EvaluateRestirPTNeeRecordVsDiInitialContributionSplitView(surface, pixel);
    }
    if (view == 52u)
    {
        return EvaluateRestirPTNeeRecordVsDiInitialSampleCompareView(surface, pixel);
    }
    if (view == 53u)
    {
        return EvaluateRestirPTNeeRecordVsDiInitialContributionRatioView(surface, pixel);
    }
    if (view == 54u)
    {
        const uint2 reservoirPixel = PathTraceFullPixelToRestirDirectPixel(pixel);
        const RTXDI_PTReservoir initialReservoir = LoadRestirPTInitialDirectReservoir(reservoirPixel);
        const RTXDI_PTReservoir finalReservoir = LoadRestirPTFinalShadingInputReservoir(reservoirPixel);
        const uint2 dimensions = max(PathTraceFullOutputSize(), uint2(1u, 1u));
        const bool showFinalInput = pixel.x >= dimensions.x / 2u;
        if (!RAB_IsSurfaceValid(surface) || !RAB_SurfaceSupportsOpaqueDiffuseBrdf(surface))
        {
            return float4(0.0, 0.0, 0.0, 1.0);
        }
        if (showFinalInput)
        {
            if (!RTXDI_IsValidPTReservoir(finalReservoir))
            {
                return float4(0.35, 0.0, 0.0, 1.0);
            }
            if (!RestirPTReferenceFinalShadingHasUsefulSample(finalReservoir))
            {
                return float4(0.02, 0.12, 0.35, 1.0);
            }
            return float4(RestirPTToneMapPreview(RestirPTReferenceFinalShadingContribution(finalReservoir)), 1.0);
        }
        if (!RTXDI_IsValidPTReservoir(initialReservoir))
        {
            return float4(0.18, 0.0, 0.0, 1.0);
        }
        if (!RestirPTReferenceFinalShadingHasUsefulSample(initialReservoir))
        {
            return float4(0.02, 0.06, 0.22, 1.0);
        }
        return float4(RestirPTToneMapPreview(RestirPTReferenceFinalShadingContribution(initialReservoir)), 1.0);
    }
    if (view == 55u)
    {
        return EvaluateRestirPTReferenceInitialTemporalContributionSplitView(surface, pixel);
    }
    if (view == 56u)
    {
        return EvaluateRestirPTReferenceInitialTemporalStateSplitView(surface, pixel);
    }
    if (view == 57u)
    {
        return EvaluateRestirPTLightManagerMapStatusView(pixel);
    }
    if (view == 58u)
    {
        return EvaluateRestirPTReferenceTemporalAcceptanceView(surface, pixel);
    }
    if (view == 59u)
    {
        return EvaluateRestirPTLightManagerReservoirProbeView(pixel);
    }
    if (view == 60u)
    {
        return EvaluateRestirPTRrxDiSingleLightHeartbeatView(pixel);
    }
    if (view == 63u)
    {
        return EvaluateRestirPTRrxDiRawReservoirHeartbeatView(pixel);
    }
    if (view == 64u)
    {
        return EvaluateRestirPTRrxDiSameDispatchEchoView(pixel);
    }
    if (view == 65u)
    {
        return EvaluateRestirPTRrxDiSamePageRawFieldsView(pixel);
    }
    if (view == 66u)
    {
        return EvaluateRestirPTRrxDiPreviousPageRawFieldsView(pixel);
    }
    if (view == 67u)
    {
        return EvaluateRestirPTActiveRabTranslationParityView(pixel);
    }

    if (!RAB_IsSurfaceValid(surface) || !RAB_SurfaceSupportsOpaqueDiffuseBrdf(surface))
    {
        return view == 4u ? float4(0.20, 0.0, 0.0, 1.0) : float4(0.0, 0.0, 0.0, 1.0);
    }

    const RTXDI_PTReservoir reservoir = LoadRestirPTRawDirectReservoir(pixel);
    const bool valid = RestirPTDirectReservoirHasUsefulSample(reservoir);

    if (view == 1u)
    {
        float3 contribution = float3(0.0, 0.0, 0.0);
        const bool evaluated = valid && RestirPTTryEvaluateNeeReservoirPreview(surface, reservoir, RestirPTInfo.z >= 0.5, contribution);
        return float4(RestirPTToneMapPreview(evaluated ? contribution : float3(0.0, 0.0, 0.0)), 1.0);
    }

    if (view == 2u)
    {
        uint temporalStatus;
        const RTXDI_PTReservoir temporalReservoir = GenerateRestirPTDiTemporalReservoir(surface, pixel, temporalStatus);
        const bool temporalValid = RestirPTDirectReservoirHasUsefulSample(temporalReservoir);
        float3 contribution = float3(0.0, 0.0, 0.0);
        const bool evaluated = temporalValid &&
            RestirPTTryEvaluateNeeReservoirPreview(surface, temporalReservoir, RestirPTInfo.z >= 0.5, contribution);
        if (!evaluated && temporalValid)
        {
            contribution = RestirPTReservoirPreviewContribution(temporalReservoir);
        }
        return float4(RestirPTToneMapPreview(contribution), 1.0);
    }

    if (view == 3u)
    {
        uint spatialStatus;
        const RTXDI_PTReservoir spatialReservoir = GenerateRestirPTDiSpatialReservoir(surface, pixel, spatialStatus);
        const bool spatialValid = RestirPTDirectReservoirHasUsefulSample(spatialReservoir);
        float3 contribution = float3(0.0, 0.0, 0.0);
        const bool evaluated = spatialValid &&
            RestirPTTryEvaluateNeeReservoirPreview(surface, spatialReservoir, RestirPTInfo.z >= 0.5, contribution);
        if (!evaluated && spatialValid)
        {
            contribution = RestirPTReservoirPreviewContribution(spatialReservoir);
        }
        return float4(RestirPTToneMapPreview(contribution), 1.0);
    }

    if (view == 5u)
    {
        const RTXDI_PTReservoir temporalReservoir = LoadRestirPTDiTemporalReservoir(pixel, RestirPTGiTemporalCurrentBufferIndex());
        const uint temporalStatus = RestirPTDirectReservoirHasUsefulSample(temporalReservoir) ? 2u : 1u;
        uint spatialStatus;
        const RTXDI_PTReservoir spatialReservoir = GenerateRestirPTDiSpatialReservoir(surface, pixel, spatialStatus);
        return RestirPTDirectReservoirHasUsefulSample(spatialReservoir)
            ? RestirPTDiSpatialStatusColor(spatialReservoir, spatialStatus)
            : RestirPTDiTemporalStatusColor(temporalReservoir, temporalStatus);
    }

    if (view == 6u)
    {
        const uint2 reservoirPixel = PathTraceFullPixelToRestirDirectPixel(pixel);
        const RTXDI_PTReservoir reservoir = LoadRestirPTFinalShadingInputReservoir(reservoirPixel);
        const float3 contribution = RestirPTReferenceFinalShadingHasUsefulSample(reservoir)
            ? RestirPTReferenceFinalShadingContribution(reservoir)
            : float3(0.0, 0.0, 0.0);
        return float4(RestirPTToneMapPreview(contribution), 1.0);
    }

    if (view == 7u)
    {
        const uint2 reservoirPixel = PathTraceFullPixelToRestirDirectPixel(pixel);
        const RTXDI_PTReservoir reservoir = LoadRestirPTFinalShadingInputReservoir(reservoirPixel);
        return RestirPTReferenceFinalShadingStateColor(reservoir);
    }

    if (view == 8u)
    {
        return EvaluateRestirPTReferencePageStateView(pixel);
    }

    if (view == 9u)
    {
        return EvaluateRestirPTReferencePageContributionView(pixel);
    }

    if (view == 10u)
    {
        return EvaluateRestirPTReferenceTemporalReadinessView(surface, pixel);
    }

    if (view == 11u)
    {
        return EvaluateRestirPTReferenceTemporalPageFlowView(pixel);
    }

    if (view == 12u)
    {
        return EvaluateRestirPTReferenceTemporalNeighborView(surface, pixel);
    }

    if (view == 4u)
    {
        const float mNorm = saturate((float)reservoir.M / max((float)RestirPTParams.initialSampling.numInitialSamples, 1.0));
        const float weightHeat = saturate(max(reservoir.WeightSum, 0.0) / (1.0 + max(reservoir.WeightSum, 0.0)));
        const float targetHeat = saturate(RTXDI_Luminance(max(reservoir.TargetFunction, float3(0.0, 0.0, 0.0))));
        return valid ? float4(targetHeat, mNorm, weightHeat, 1.0) : float4(0.35, mNorm * 0.25, weightHeat * 0.25, 1.0);
    }

    return float4(0.0, 0.0, 0.0, 1.0);
}

#endif
