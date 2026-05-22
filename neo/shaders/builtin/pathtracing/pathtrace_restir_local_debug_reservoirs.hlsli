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

#define REMIX_RTXDI_TEMPORAL_REUSE_EXTERNAL_BINDINGS 1
#include "remix_rtxdi/rtxdi_temporal_reuse.rt.hlsl"

uint RestirPTGiDebugView()
{
    return clamp((uint)max(RestirPTGiDebugInfo.x, 0.0), 0u, 4u);
}

uint RestirPTDiDebugView()
{
    return clamp((uint)max(RestirPTDiDebugInfo.x, 0.0), 0u, 76u);
}

bool RestirPTDiTemporalPrepassEnabled()
{
    return RestirPTDiDebugInfo.y >= 0.5;
}

static const uint RESTIR_PT_RRX_DEBUG_BYPASS_MOTION = 0x00000001u;
static const uint RESTIR_PT_RRX_DEBUG_BYPASS_DEPTH = 0x00000002u;
static const uint RESTIR_PT_RRX_DEBUG_BYPASS_NORMAL = 0x00000004u;
static const uint RESTIR_PT_RRX_DEBUG_BYPASS_SURFACE_SIMILARITY = 0x00000008u;
static const uint RESTIR_PT_RRX_DEBUG_BYPASS_RESET_MASK = 0x00000010u;
static const uint RESTIR_PT_RRX_DEBUG_BYPASS_PORTAL = 0x00000020u;
static const uint RESTIR_PT_RRX_DEBUG_ENABLE_TEMPORAL_PERMUTATION = 0x00000040u;
static const uint RESTIR_PT_RRX_DEBUG_DISABLE_PREVIOUS_BEST = 0x00000080u;

uint RestirPTRrxDebugBypassFlags()
{
    return (uint)max(RestirPTDiDebugInfo.z, 0.0);
}

bool RestirPTRrxDebugBypassEnabled(uint flag)
{
    return (RestirPTRrxDebugBypassFlags() & flag) != 0u;
}

bool RestirPTRrxDebugFlatContribution()
{
    return RestirPTDiDebugInfo.w >= 0.5;
}

bool RestirPTRrxTemporalPermutationEnabled()
{
    return RestirPTRrxDebugBypassEnabled(RESTIR_PT_RRX_DEBUG_ENABLE_TEMPORAL_PERMUTATION);
}

bool RestirPTRrxDisablePreviousBest()
{
    return RestirPTRrxDebugBypassEnabled(RESTIR_PT_RRX_DEBUG_DISABLE_PREVIOUS_BEST);
}

bool RestirPTRrxFinalConsumerOutputEnabled()
{
    return RestirPTGiDebugInfo.z >= 0.5;
}

bool RestirPTRrxFinalConsumerCurrentOnly()
{
    return RestirPTGiDebugInfo.y >= 0.5;
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

uint RestirPTRrxHashUint(uint value)
{
    value ^= value >> 16;
    value *= 2246822519u;
    value ^= value >> 13;
    value *= 3266489917u;
    value ^= value >> 16;
    return value;
}

float RestirPTRrxHashComponent(uint value, uint salt)
{
    return ((float)(RestirPTRrxHashUint(value ^ salt) & 0x00ffffffu) + 0.5) / 16777216.0;
}

float4 RestirPTRrxHashColor(uint value)
{
    return float4(
        RestirPTRrxHashComponent(value, 0x9e3779b9u),
        RestirPTRrxHashComponent(value, 0x7f4a7c15u),
        RestirPTRrxHashComponent(value, 0x94d049bbu),
        1.0);
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

bool RestirPTTryProjectMotionVectorToPreviousPixelFull(uint2 pixel, out int2 previousPixel, out float3 motionVector)
{
    previousPixel = int2(-1, -1);
    motionVector = float3(0.0, 0.0, 0.0);

    const uint2 dimensions = PathTraceFullOutputSize();
    const uint resetMask = PathTraceRRGuideResetMask[pixel];
    if (!RestirPTRrxDebugBypassEnabled(RESTIR_PT_RRX_DEBUG_BYPASS_RESET_MASK) &&
        (resetMask & RESTIR_PT_TEMPORAL_UNSAFE_RESET_MASK) != 0u)
    {
        return false;
    }

    if (RestirPTRrxDebugBypassEnabled(RESTIR_PT_RRX_DEBUG_BYPASS_MOTION))
    {
        previousPixel = int2(pixel);
        return pixel.x < dimensions.x && pixel.y < dimensions.y;
    }

    const uint motionMask = PathTraceMotionVectorMask[pixel];
    if ((motionMask & PT_MOTION_VECTOR_MASK_VALID) == 0u)
    {
        return false;
    }

    motionVector = PathTraceMotionVectors[pixel].xyz;
    const float2 previousPixelFloat = float2(pixel) + 0.5 + motionVector.xy;
    previousPixel = int2(floor(previousPixelFloat));

    return previousPixel.x >= 0 && previousPixel.y >= 0 &&
        (uint)previousPixel.x < dimensions.x && (uint)previousPixel.y < dimensions.y;
}

bool RestirPTTryProjectMotionVectorToPreviousPixel(uint2 pixel, out int2 previousPixel, out float2 motionPixels)
{
    float3 motionVector;
    const bool projected = RestirPTTryProjectMotionVectorToPreviousPixelFull(pixel, previousPixel, motionVector);
    motionPixels = motionVector.xy;
    return projected;
}

bool RestirPTRrxPrimarySurfacesAreSimilar(RAB_Surface surface, RAB_Surface previousSurface, out uint similarityStatus)
{
    if (RestirPTRrxDebugBypassEnabled(RESTIR_PT_RRX_DEBUG_BYPASS_SURFACE_SIMILARITY))
    {
        if (!RAB_IsSurfaceValid(surface))
        {
            similarityStatus = RT_PRIMARY_SURFACE_DEBUG_MISSING_CURRENT;
            return false;
        }
        if (!RAB_IsSurfaceValid(previousSurface))
        {
            similarityStatus = RT_PRIMARY_SURFACE_DEBUG_MISSING_PREVIOUS;
            return false;
        }
        similarityStatus = RT_PRIMARY_SURFACE_DEBUG_OK;
        return true;
    }

    return PathTracePrimarySurfacesAreSimilar(surface, previousSurface, similarityStatus);
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
    return RestirPTRrxPrimarySurfacesAreSimilar(surface, previousSurface, similarityStatus);
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
        if (RestirPTRrxPrimarySurfacesAreSimilar(surface, previousSurface, similarityStatus))
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
        if (RestirPTRrxPrimarySurfacesAreSimilar(surface, previousSurface, similarityStatus))
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
        const RAB_LightInfo lightInfo = RAB_LoadActiveRrxLightInfo(lightIndex, false);
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

    const RAB_LightInfo currentLightInfo = RAB_LoadActiveRrxLightInfo(currentLightIndex, false);
    const bool currentLoadValid = RAB_IsLightInfoValid(currentLightInfo);
    const int previousIndex = currentLoadValid ? RAB_TranslateActiveRrxLightIndex(currentLightIndex, true) : -1;
    RAB_LightInfo previousLightInfo = RAB_EmptyLightInfo();
    if (previousIndex >= 0)
    {
        previousLightInfo = RAB_LoadActiveRrxLightInfo((uint)previousIndex, true);
    }
    const bool previousLoadValid = previousIndex >= 0 && RAB_IsLightInfoValid(previousLightInfo);
    const int roundTripCurrentIndex = previousLoadValid ? RAB_TranslateActiveRrxLightIndex((uint)previousIndex, false) : -1;

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

bool RestirPTRrxDiTemporalActive()
{
    return RAB_RestirLightManagerRABEnabled() && RestirPTRrxDiReservoirAvailable();
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

bool RestirPTRrxDiReservoirClearPending()
{
    return (RestirPTRemixDiReservoirInfo.w & RESTIR_PT_RRX_DI_RESERVOIR_CLEAR_PENDING) != 0u;
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

float4 RestirPTRrxDiRawFieldQuadrantColor(uint2 pixel, RTXDI_PackedDIReservoir packedReservoir, RTXDI_DIReservoir reservoir);

uint RestirPTRrxDiPackUv(float2 uv)
{
    return uint(saturate(uv.x) * 0xffff) | (uint(saturate(uv.y) * 0xffff) << 16);
}

static const uint RESTIR_PT_RRX_DI_SELECTED_SOURCE_NONE = 0u;
static const uint RESTIR_PT_RRX_DI_SELECTED_SOURCE_EMISSIVE = 1u;
static const uint RESTIR_PT_RRX_DI_SELECTED_SOURCE_DOOM_ANALYTIC = 2u;
static const uint RESTIR_PT_RRX_DI_SELECTED_SOURCE_PREVIOUS_BEST = 3u;

struct RestirPTRrxDiInitialDebugInfo
{
    uint previousBestSourceValid;
    uint previousBestTranslationValid;
    uint previousBestCandidateValid;
    uint previousBestSelectedByCombine;
    uint emissiveCandidateCount;
    uint doomAnalyticCandidateCount;
    uint emissiveLightInfoInvalidCount;
    uint doomAnalyticLightInfoInvalidCount;
    uint emissiveSampleInvalidCount;
    uint doomAnalyticSampleInvalidCount;
    uint emissiveZeroTargetPdfCount;
    uint doomAnalyticZeroTargetPdfCount;
    uint doomAnalyticOutsideInfluenceCount;
    uint doomAnalyticNonFinitePayloadCount;
    uint emissiveStreamedCount;
    uint doomAnalyticStreamedCount;
    uint randomSelectedLightIndexEncoded;
    uint randomSelectedLightType;
    uint randomSelectedSource;
    uint finalSelectedLightType;
    uint finalSelectedSource;
    float emissiveMaxTargetPdf;
    float doomAnalyticMaxTargetPdf;
    float emissiveInvSourcePdf;
    float doomAnalyticInvSourcePdf;
    float randomStreamWeightSum;
    float randomStreamM;
    float randomSelectedTargetPdf;
    float randomSelectedInvSourcePdf;
    float randomSelectedRisWeight;
    float randomSelectedStreamWeightSum;
    float randomFinalWeightSum;
    float combinedPreFinalizeWeightSum;
    float combinedPreFinalizeM;
    float combinedSelectedTargetPdf;
    float finalInitialWeightSum;
    float finalInitialTargetPdf;
    float previousBestTargetPdf;
    float previousBestWeightSum;
    float selectedEffectiveSourceWeight;
    float selectedPreFinalizeWeightSum;
};

RestirPTRrxDiInitialDebugInfo RestirPTRrxDiEmptyInitialDebugInfo()
{
    RestirPTRrxDiInitialDebugInfo debugInfo = (RestirPTRrxDiInitialDebugInfo)0;
    return debugInfo;
}

bool RestirPTRrxDiTryResolvePreviousBestCurrentLight(
    RTXDI_DIReservoir previousReservoir,
    bool previousReservoirValid,
    out uint currentLightIndex)
{
    currentLightIndex = PATH_TRACE_UNIFIED_LIGHT_INVALID_INDEX;
    if (!previousReservoirValid || !RTXDI_IsValidDIReservoir(previousReservoir))
    {
        return false;
    }

    const int translatedLightIndex = RAB_TranslateActiveRrxLightIndex(RTXDI_GetDIReservoirLightIndex(previousReservoir), false);
    if (translatedLightIndex < 0)
    {
        return false;
    }

    currentLightIndex = (uint)translatedLightIndex;
    return true;
}

uint RestirPTRrxDiLightTypeFromInfo(RAB_LightInfo lightInfo)
{
    if (!RAB_IsLightInfoValid(lightInfo))
    {
        return PATH_TRACE_UNIFIED_LIGHT_TYPE_INVALID;
    }
    if (lightInfo.unifiedLightType == PATH_TRACE_UNIFIED_LIGHT_TYPE_EMISSIVE_TRIANGLE ||
        lightInfo.unifiedLightType == PATH_TRACE_UNIFIED_LIGHT_TYPE_DOOM_ANALYTIC)
    {
        return lightInfo.unifiedLightType;
    }
    if (lightInfo.lightType == RAB_LIGHT_TYPE_EMISSIVE_TRIANGLE)
    {
        return PATH_TRACE_UNIFIED_LIGHT_TYPE_EMISSIVE_TRIANGLE;
    }
    if (lightInfo.lightType == RAB_LIGHT_TYPE_DOOM_ANALYTIC_SPHERE)
    {
        return PATH_TRACE_UNIFIED_LIGHT_TYPE_DOOM_ANALYTIC;
    }
    return PATH_TRACE_UNIFIED_LIGHT_TYPE_INVALID;
}

uint RestirPTRrxDiLightTypeFromReservoir(RTXDI_DIReservoir reservoir)
{
    if (!RTXDI_IsValidDIReservoir(reservoir))
    {
        return PATH_TRACE_UNIFIED_LIGHT_TYPE_INVALID;
    }
    return RestirPTRrxDiLightTypeFromInfo(RAB_LoadActiveRrxLightInfo(RTXDI_GetDIReservoirLightIndex(reservoir), false));
}

RTXDI_DIReservoir RestirPTRrxDiBuildPreviousBestReservoir(
    inout RTXDI_RandomSamplerState rng,
    RAB_Surface surface,
    uint currentLightIndex)
{
    const RAB_LightInfo lightInfo = RAB_LoadActiveRrxLightInfo(currentLightIndex, false);
    if (!RAB_IsLightInfoValid(lightInfo))
    {
        return RTXDI_EmptyDIReservoir();
    }

    RTXDI_DIReservoir reservoir = RTXDI_EmptyDIReservoir();
    const float2 uv = RTXDI_RandomlySelectLocalLightUV(rng);
    const RAB_LightSample lightSample = RAB_SampleActiveRrxPolymorphicLight(lightInfo, surface, uv);
    const float targetPdf = max(RAB_GetLightSampleTargetPdfForSurface(lightSample, surface), 0.0);
    if (lightSample.valid != 0u && targetPdf > 0.0)
    {
        RTXDI_StreamSample(
            reservoir,
            currentLightIndex,
            uv,
            RTXDI_GetNextRandom(rng),
            targetPdf,
            1.0);
    }

    if (!RTXDI_IsValidDIReservoir(reservoir))
    {
        return RTXDI_EmptyDIReservoir();
    }

    RTXDI_FinalizeResampling(reservoir, 1.0, reservoir.M);
    reservoir.M = 1.0;
    reservoir.packedVisibility = RTXDI_PackedDIReservoir_VisibilityMask;
    reservoir.spatialDistance = int2(0, 0);
    reservoir.age = 0u;
    reservoir.canonicalWeight = 1.0;
    return reservoir;
}

uint2 RestirPTRrxDiClampLightRange(uint2 lightRange, uint currentCount)
{
    if (lightRange.x >= currentCount)
    {
        return uint2(lightRange.x, 0u);
    }

    lightRange.y = min(lightRange.y, currentCount - lightRange.x);
    return lightRange;
}

void RestirPTRrxDiStreamLightRangeIntoReservoir(
    inout RTXDI_DIReservoir randomReservoir,
    inout RTXDI_RandomSamplerState rng,
    RAB_Surface surface,
    uint2 lightRange,
    uint sampleCount,
    uint totalSampleCount,
    bool previousBestCurrentLightValid,
    uint previousBestCurrentLightIndex,
    uint rangeLightType,
    inout RestirPTRrxDiInitialDebugInfo initialDebugInfo)
{
    if (lightRange.y == 0u || sampleCount == 0u || totalSampleCount == 0u)
    {
        return;
    }

    const uint boundedSampleCount = min(sampleCount, lightRange.y);
    float lightIndexInRange = RTXDI_GetNextRandom(rng) * (float)lightRange.y;
    const float stride = max(1.0, (float)lightRange.y / (float)boundedSampleCount);
    const float invSourcePdf =
        ((float)lightRange.y * (float)totalSampleCount) / max((float)boundedSampleCount, 1.0);
    if (rangeLightType == PATH_TRACE_UNIFIED_LIGHT_TYPE_EMISSIVE_TRIANGLE)
    {
        initialDebugInfo.emissiveInvSourcePdf = invSourcePdf;
    }
    else if (rangeLightType == PATH_TRACE_UNIFIED_LIGHT_TYPE_DOOM_ANALYTIC)
    {
        initialDebugInfo.doomAnalyticInvSourcePdf = invSourcePdf;
    }

    for (uint sampleIndex = 0u; sampleIndex < boundedSampleCount; ++sampleIndex)
    {
        const uint lightIndex = lightRange.x + min((uint)lightIndexInRange, lightRange.y - 1u);
        if (!(previousBestCurrentLightValid && lightIndex == previousBestCurrentLightIndex))
        {
            if (rangeLightType == PATH_TRACE_UNIFIED_LIGHT_TYPE_EMISSIVE_TRIANGLE)
            {
                ++initialDebugInfo.emissiveCandidateCount;
            }
            else if (rangeLightType == PATH_TRACE_UNIFIED_LIGHT_TYPE_DOOM_ANALYTIC)
            {
                ++initialDebugInfo.doomAnalyticCandidateCount;
            }

            const RAB_LightInfo lightInfo = RAB_LoadActiveRrxLightInfo(lightIndex, false);
            if (!RAB_IsLightInfoValid(lightInfo))
            {
                if (rangeLightType == PATH_TRACE_UNIFIED_LIGHT_TYPE_EMISSIVE_TRIANGLE)
                {
                    ++initialDebugInfo.emissiveLightInfoInvalidCount;
                }
                else if (rangeLightType == PATH_TRACE_UNIFIED_LIGHT_TYPE_DOOM_ANALYTIC)
                {
                    ++initialDebugInfo.doomAnalyticLightInfoInvalidCount;
                }
            }
            else
            {
                const float2 uv = RTXDI_RandomlySelectLocalLightUV(rng);
                const RAB_LightSample lightSample = RAB_SampleActiveRrxPolymorphicLight(lightInfo, surface, uv);
                const float targetPdf = max(RAB_GetLightSampleTargetPdfForSurface(lightSample, surface), 0.0);
                if (lightSample.valid != 0u)
                {
                    if (rangeLightType == PATH_TRACE_UNIFIED_LIGHT_TYPE_EMISSIVE_TRIANGLE)
                    {
                        initialDebugInfo.emissiveMaxTargetPdf = max(initialDebugInfo.emissiveMaxTargetPdf, targetPdf);
                    }
                    else if (rangeLightType == PATH_TRACE_UNIFIED_LIGHT_TYPE_DOOM_ANALYTIC)
                    {
                        initialDebugInfo.doomAnalyticMaxTargetPdf = max(initialDebugInfo.doomAnalyticMaxTargetPdf, targetPdf);
                    }
                }
                const bool positiveCandidate = lightSample.valid != 0u && targetPdf > 0.0;
                if (lightSample.valid == 0u)
                {
                    if (rangeLightType == PATH_TRACE_UNIFIED_LIGHT_TYPE_EMISSIVE_TRIANGLE)
                    {
                        ++initialDebugInfo.emissiveSampleInvalidCount;
                    }
                    else if (rangeLightType == PATH_TRACE_UNIFIED_LIGHT_TYPE_DOOM_ANALYTIC)
                    {
                        ++initialDebugInfo.doomAnalyticSampleInvalidCount;
                        if ((lightSample.flags & RAB_LIGHT_SAMPLE_FLAG_DOOM_OUTSIDE_INFLUENCE) != 0u)
                        {
                            ++initialDebugInfo.doomAnalyticOutsideInfluenceCount;
                        }
                        if ((lightSample.flags & RAB_LIGHT_SAMPLE_FLAG_DOOM_NONFINITE_PAYLOAD) != 0u)
                        {
                            ++initialDebugInfo.doomAnalyticNonFinitePayloadCount;
                        }
                    }
                }
                else if (targetPdf <= 0.0)
                {
                    if (rangeLightType == PATH_TRACE_UNIFIED_LIGHT_TYPE_EMISSIVE_TRIANGLE)
                    {
                        ++initialDebugInfo.emissiveZeroTargetPdfCount;
                    }
                    else if (rangeLightType == PATH_TRACE_UNIFIED_LIGHT_TYPE_DOOM_ANALYTIC)
                    {
                        ++initialDebugInfo.doomAnalyticZeroTargetPdfCount;
                    }
                }

                const bool selectedCandidate = RTXDI_StreamSample(
                    randomReservoir,
                    lightIndex,
                    uv,
                    RTXDI_GetNextRandom(rng),
                    positiveCandidate ? targetPdf : 0.0,
                    invSourcePdf);
                if (positiveCandidate)
                {
                    if (selectedCandidate)
                    {
                        initialDebugInfo.randomSelectedLightIndexEncoded = lightIndex + 1u;
                        initialDebugInfo.randomSelectedLightType = rangeLightType;
                        initialDebugInfo.randomSelectedSource =
                            rangeLightType == PATH_TRACE_UNIFIED_LIGHT_TYPE_EMISSIVE_TRIANGLE
                                ? RESTIR_PT_RRX_DI_SELECTED_SOURCE_EMISSIVE
                                : RESTIR_PT_RRX_DI_SELECTED_SOURCE_DOOM_ANALYTIC;
                        initialDebugInfo.randomSelectedTargetPdf = targetPdf;
                        initialDebugInfo.randomSelectedInvSourcePdf = invSourcePdf;
                        initialDebugInfo.randomSelectedRisWeight = targetPdf * invSourcePdf;
                        initialDebugInfo.randomSelectedStreamWeightSum = randomReservoir.weightSum;
                    }
                    if (rangeLightType == PATH_TRACE_UNIFIED_LIGHT_TYPE_EMISSIVE_TRIANGLE)
                    {
                        ++initialDebugInfo.emissiveStreamedCount;
                    }
                    else if (rangeLightType == PATH_TRACE_UNIFIED_LIGHT_TYPE_DOOM_ANALYTIC)
                    {
                        ++initialDebugInfo.doomAnalyticStreamedCount;
                    }
                }
            }
        }

        lightIndexInRange += stride;
        if (lightIndexInRange >= (float)lightRange.y)
        {
            lightIndexInRange -= (float)lightRange.y;
        }
    }
}

RTXDI_DIReservoir RestirPTRrxDiBuildInitialReservoir(
    RAB_Surface surface,
    uint2 pixel,
    RTXDI_DIReservoir previousBestReservoir,
    bool previousBestReservoirValid,
    out RestirPTRrxDiInitialDebugInfo initialDebugInfo)
{
    initialDebugInfo = RestirPTRrxDiEmptyInitialDebugInfo();
    if (!RAB_RestirLightManagerRABEnabled())
    {
        return RTXDI_EmptyDIReservoir();
    }

    const uint currentCount = RAB_GetCurrentRestirLightManagerCount();
    if (currentCount == 0u)
    {
        return RTXDI_EmptyDIReservoir();
    }

    const uint frameIndex = (uint)max(RestirPTInfo.x, 0.0);
    RTXDI_RandomSamplerState rng = RTXDI_InitRandomSampler(pixel, frameIndex, 0x52525805u);
    uint previousBestCurrentLightIndex = PATH_TRACE_UNIFIED_LIGHT_INVALID_INDEX;
    initialDebugInfo.previousBestSourceValid = previousBestReservoirValid ? 1u : 0u;
    const bool previousBestCurrentLightValid = !RestirPTRrxDisablePreviousBest() && RestirPTRrxDiTryResolvePreviousBestCurrentLight(
        previousBestReservoir,
        previousBestReservoirValid,
        previousBestCurrentLightIndex);
    initialDebugInfo.previousBestTranslationValid = previousBestCurrentLightValid ? 1u : 0u;

    const uint2 emissiveRange = RestirPTRrxDiClampLightRange(
        RAB_GetRestirLightManagerEmissiveRange(),
        currentCount);
    const uint2 doomAnalyticRange = RestirPTRrxDiClampLightRange(
        RAB_GetRestirLightManagerDoomAnalyticRange(),
        currentCount);
    const uint emissiveSampleCount = min(RAB_GetRestirLightManagerEmissiveSampleCount(), emissiveRange.y);
    const uint doomAnalyticSampleCount = min(RAB_GetRestirLightManagerDoomAnalyticSampleCount(), doomAnalyticRange.y);
    const uint totalSampleCount = emissiveSampleCount + doomAnalyticSampleCount;

    RTXDI_DIReservoir randomReservoir = RTXDI_EmptyDIReservoir();
    RestirPTRrxDiStreamLightRangeIntoReservoir(
        randomReservoir,
        rng,
        surface,
        emissiveRange,
        emissiveSampleCount,
        totalSampleCount,
        previousBestCurrentLightValid,
        previousBestCurrentLightIndex,
        PATH_TRACE_UNIFIED_LIGHT_TYPE_EMISSIVE_TRIANGLE,
        initialDebugInfo);
    RestirPTRrxDiStreamLightRangeIntoReservoir(
        randomReservoir,
        rng,
        surface,
        doomAnalyticRange,
        doomAnalyticSampleCount,
        totalSampleCount,
        previousBestCurrentLightValid,
        previousBestCurrentLightIndex,
        PATH_TRACE_UNIFIED_LIGHT_TYPE_DOOM_ANALYTIC,
        initialDebugInfo);

    initialDebugInfo.randomStreamWeightSum = randomReservoir.weightSum;
    initialDebugInfo.randomStreamM = randomReservoir.M;
    initialDebugInfo.randomSelectedTargetPdf = randomReservoir.targetPdf;
    if (RTXDI_IsValidDIReservoir(randomReservoir))
    {
        RTXDI_FinalizeResampling(randomReservoir, 1.0, (float)max(totalSampleCount, 1u));
        randomReservoir.M = 1.0;
        initialDebugInfo.randomFinalWeightSum = randomReservoir.weightSum;
        initialDebugInfo.randomSelectedLightType = RestirPTRrxDiLightTypeFromReservoir(randomReservoir);
    }

    RTXDI_DIReservoir reservoir = RTXDI_EmptyDIReservoir();
    if (RTXDI_IsValidDIReservoir(randomReservoir))
    {
        RTXDI_CombineDIReservoirs(
            reservoir,
            randomReservoir,
            RTXDI_GetNextRandom(rng),
            randomReservoir.targetPdf);
        initialDebugInfo.finalSelectedSource = initialDebugInfo.randomSelectedSource;
        initialDebugInfo.selectedEffectiveSourceWeight = randomReservoir.weightSum;
        initialDebugInfo.selectedPreFinalizeWeightSum = reservoir.weightSum;
    }

    if (previousBestCurrentLightValid)
    {
        const RTXDI_DIReservoir previousBestCandidate = RestirPTRrxDiBuildPreviousBestReservoir(
            rng,
            surface,
            previousBestCurrentLightIndex);
        if (RTXDI_IsValidDIReservoir(previousBestCandidate))
        {
            initialDebugInfo.previousBestCandidateValid = 1u;
            initialDebugInfo.previousBestTargetPdf = previousBestCandidate.targetPdf;
            initialDebugInfo.previousBestWeightSum = previousBestCandidate.weightSum;
            const bool selectedPreviousBest = RTXDI_CombineDIReservoirs(
                reservoir,
                previousBestCandidate,
                RTXDI_GetNextRandom(rng),
                previousBestCandidate.targetPdf);
            initialDebugInfo.previousBestSelectedByCombine = selectedPreviousBest ? 1u : 0u;
            if (selectedPreviousBest)
            {
                initialDebugInfo.finalSelectedSource = RESTIR_PT_RRX_DI_SELECTED_SOURCE_PREVIOUS_BEST;
                initialDebugInfo.selectedEffectiveSourceWeight = previousBestCandidate.weightSum;
                initialDebugInfo.selectedPreFinalizeWeightSum = reservoir.weightSum;
            }
        }
    }

    initialDebugInfo.combinedPreFinalizeWeightSum = reservoir.weightSum;
    initialDebugInfo.combinedPreFinalizeM = reservoir.M;
    initialDebugInfo.combinedSelectedTargetPdf = reservoir.targetPdf;
    if (!RTXDI_IsValidDIReservoir(reservoir))
    {
        return RTXDI_EmptyDIReservoir();
    }

    RTXDI_FinalizeResampling(reservoir, 1.0, 1.0);
    reservoir.M = 1.0;
    initialDebugInfo.finalInitialWeightSum = reservoir.weightSum;
    initialDebugInfo.finalInitialTargetPdf = reservoir.targetPdf;
    initialDebugInfo.finalSelectedLightType = RestirPTRrxDiLightTypeFromReservoir(reservoir);
    if (initialDebugInfo.finalSelectedSource == RESTIR_PT_RRX_DI_SELECTED_SOURCE_NONE)
    {
        initialDebugInfo.finalSelectedSource = initialDebugInfo.finalSelectedLightType == PATH_TRACE_UNIFIED_LIGHT_TYPE_EMISSIVE_TRIANGLE
            ? RESTIR_PT_RRX_DI_SELECTED_SOURCE_EMISSIVE
            : (initialDebugInfo.finalSelectedLightType == PATH_TRACE_UNIFIED_LIGHT_TYPE_DOOM_ANALYTIC
                ? RESTIR_PT_RRX_DI_SELECTED_SOURCE_DOOM_ANALYTIC
                : RESTIR_PT_RRX_DI_SELECTED_SOURCE_NONE);
    }
    reservoir.packedVisibility = RTXDI_PackedDIReservoir_VisibilityMask;
    reservoir.spatialDistance = int2(0, 0);
    reservoir.age = 0u;
    reservoir.canonicalWeight = 1.0;
    return reservoir;
}

RemixRtxdiTemporalReuseDesc RestirPTRrxDiTemporalDesc(uint2 pixel, float3 motionVector)
{
    RemixRtxdiTemporalReuseDesc desc = (RemixRtxdiTemporalReuseDesc)0;
    desc.pixel = pixel;
    desc.frameIndex = (uint)max(RestirPTInfo.x, 0.0);
    desc.screenSpaceMotion = motionVector;
    desc.temporalInputPage = RestirPTRemixDiReservoirPageInfo.y;
    desc.temporalOutputPage = RestirPTRemixDiReservoirPageInfo.x;
    desc.activeCheckerboardField = 0u;
    desc.maxHistoryLength = max(RestirPTParams.temporalResampling.maxHistoryLength, 1u);
    desc.biasCorrectionMode = RTXDI_BIAS_CORRECTION_BASIC;
    desc.depthThreshold = RestirPTRrxDebugBypassEnabled(RESTIR_PT_RRX_DEBUG_BYPASS_DEPTH)
        ? 1.0e6
        : RestirPTParams.temporalResampling.depthThreshold;
    desc.normalThreshold = RestirPTRrxDebugBypassEnabled(RESTIR_PT_RRX_DEBUG_BYPASS_NORMAL)
        ? -1.0
        : RestirPTParams.temporalResampling.normalThreshold;
    desc.enablePermutationSampling = RestirPTRrxTemporalPermutationEnabled() ? 1u : 0u;
    desc.uniformRandomNumber = RestirPTRrxHashUint(desc.frameIndex ^ 0x8f3c5d21u);
    desc.reprojectionConfidenceHistoryLength = desc.maxHistoryLength;
    return desc;
}

static const uint RESTIR_PT_RRX_DI_TEMPORAL_STATUS_UNAVAILABLE = 0u;
static const uint RESTIR_PT_RRX_DI_TEMPORAL_STATUS_GLOBAL_CLEAR_PENDING = 1u;
static const uint RESTIR_PT_RRX_DI_TEMPORAL_STATUS_PREVIOUS_ABSENT = 2u;
static const uint RESTIR_PT_RRX_DI_TEMPORAL_STATUS_PREVIOUS_TRANSLATION_VALID = 3u;
static const uint RESTIR_PT_RRX_DI_TEMPORAL_STATUS_PREVIOUS_TRANSLATION_INVALID = 4u;
static const uint RESTIR_PT_RRX_DI_TEMPORAL_STATUS_CURRENT_TO_PREVIOUS_INVALID = 5u;
static const uint RESTIR_PT_RRX_DI_TEMPORAL_STATUS_LOCAL_REJECTED = 6u;
static const uint RESTIR_PT_RRX_DI_TEMPORAL_STATUS_CURRENT_ONLY = 7u;

struct RestirPTRrxDiTemporalDebugInfo
{
    uint projected;
    uint previousSampleUsed;
    uint previousTranslationValid;
    uint currentToPreviousValid;
    uint selectedLightChanged;
    uint currentValid;
    uint previousValid;
    uint outputValid;
    uint currentLightIndexEncoded;
    uint currentToPreviousIndexEncoded;
    uint previousLightIndexEncoded;
    uint previousToCurrentIndexEncoded;
    uint previousBestSourceValid;
    uint previousBestTranslationValid;
    uint previousBestCandidateValid;
    uint previousBestSelectedByCombine;
    uint emissiveCandidateCount;
    uint doomAnalyticCandidateCount;
    uint emissiveLightInfoInvalidCount;
    uint doomAnalyticLightInfoInvalidCount;
    uint emissiveSampleInvalidCount;
    uint doomAnalyticSampleInvalidCount;
    uint emissiveZeroTargetPdfCount;
    uint doomAnalyticZeroTargetPdfCount;
    uint doomAnalyticOutsideInfluenceCount;
    uint doomAnalyticNonFinitePayloadCount;
    uint emissiveStreamedCount;
    uint doomAnalyticStreamedCount;
    uint randomSelectedLightIndexEncoded;
    uint randomSelectedLightType;
    uint randomSelectedSource;
    uint finalInitialSelectedLightType;
    uint finalInitialSelectedSource;
    float emissiveMaxTargetPdf;
    float doomAnalyticMaxTargetPdf;
    float emissiveInvSourcePdf;
    float doomAnalyticInvSourcePdf;
    float randomStreamWeightSum;
    float randomStreamM;
    float randomSelectedTargetPdf;
    float randomSelectedInvSourcePdf;
    float randomSelectedRisWeight;
    float randomSelectedStreamWeightSum;
    float randomFinalWeightSum;
    float combinedPreFinalizeWeightSum;
    float combinedPreFinalizeM;
    float combinedSelectedTargetPdf;
    float finalInitialWeightSum;
    float finalInitialTargetPdf;
    float previousBestTargetPdf;
    float previousBestWeightSum;
    float selectedEffectiveSourceWeight;
    float selectedPreFinalizeWeightSum;
    float currentWeightSum;
    float currentTargetPdf;
    float temporalWeightSum;
    float temporalTargetPdf;
    float previousUsedWeightSum;
    float previousUsedTargetPdf;
};

RestirPTRrxDiTemporalDebugInfo RestirPTRrxDiEmptyTemporalDebugInfo()
{
    RestirPTRrxDiTemporalDebugInfo debugInfo = (RestirPTRrxDiTemporalDebugInfo)0;
    return debugInfo;
}

float4 RestirPTRrxDiTemporalStatusColor(uint status, RTXDI_DIReservoir reservoir)
{
    const float history = saturate(reservoir.M / max((float)RestirPTParams.temporalResampling.maxHistoryLength, 1.0));
    if (status == RESTIR_PT_RRX_DI_TEMPORAL_STATUS_PREVIOUS_TRANSLATION_VALID)
    {
        return float4(0.02, 0.25 + 0.75 * history, 0.08, 1.0);
    }
    if (status == RESTIR_PT_RRX_DI_TEMPORAL_STATUS_PREVIOUS_ABSENT)
    {
        return float4(0.05, 0.24, 0.82, 1.0);
    }
    if (status == RESTIR_PT_RRX_DI_TEMPORAL_STATUS_CURRENT_ONLY)
    {
        return float4(0.05, 0.46, 0.92, 1.0);
    }
    if (status == RESTIR_PT_RRX_DI_TEMPORAL_STATUS_PREVIOUS_TRANSLATION_INVALID)
    {
        return float4(0.55, 0.05, 0.80, 1.0);
    }
    if (status == RESTIR_PT_RRX_DI_TEMPORAL_STATUS_CURRENT_TO_PREVIOUS_INVALID)
    {
        return float4(0.95, 0.55, 0.04, 1.0);
    }
    if (status == RESTIR_PT_RRX_DI_TEMPORAL_STATUS_LOCAL_REJECTED)
    {
        return float4(0.90, 0.18, 0.02, 1.0);
    }
    if (status == RESTIR_PT_RRX_DI_TEMPORAL_STATUS_GLOBAL_CLEAR_PENDING)
    {
        return float4(0.95, 0.95, 0.05, 1.0);
    }
    return float4(0.85, 0.02, 0.02, 1.0);
}

float4 RestirPTRrxDiTemporalOutputColor(uint2 pixel, RTXDI_DIReservoir reservoir)
{
    const bool valid = RTXDI_IsValidDIReservoir(reservoir);
    const float history = saturate(reservoir.M / max((float)RestirPTParams.temporalResampling.maxHistoryLength, 1.0));
    if (!valid)
    {
        return float4(0.0, 0.0, 0.0, 1.0);
    }

    const float pdfHeat = saturate(reservoir.targetPdf / (1.0 + reservoir.targetPdf));
    const float weightHeat = saturate(reservoir.weightSum / (1.0 + reservoir.weightSum));
    return float4(max(pdfHeat, 0.05), max(history, 0.10), max(weightHeat, 0.05), 1.0);
}

float4 RestirPTRrxDiTemporalBooleanColor(bool condition, bool applicable)
{
    if (!applicable)
    {
        return float4(0.04, 0.08, 0.18, 1.0);
    }
    return condition ? float4(0.02, 0.85, 0.10, 1.0) : float4(0.95, 0.18, 0.02, 1.0);
}

float4 RestirPTRrxDiTemporalSelectedStabilityColor(RestirPTRrxDiTemporalDebugInfo debugInfo)
{
    if (debugInfo.previousSampleUsed == 0u || debugInfo.previousValid == 0u)
    {
        return float4(0.05, 0.24, 0.82, 1.0);
    }
    if (debugInfo.previousTranslationValid == 0u)
    {
        return float4(0.55, 0.05, 0.80, 1.0);
    }
    return debugInfo.selectedLightChanged != 0u
        ? float4(0.95, 0.55, 0.04, 1.0)
        : float4(0.02, 0.85, 0.10, 1.0);
}

float4 RestirPTRrxDiTemporalCauseColor(
    uint2 pixel,
    uint status,
    RTXDI_DIReservoir reservoir,
    RestirPTRrxDiTemporalDebugInfo debugInfo)
{
    const uint2 dimensions = max(PathTraceFullOutputSize(), uint2(1u, 1u));
    const uint leftWidth = max(dimensions.x / 2u, 1u);
    const uint bandCount = 9u;
    const uint bandWidth = max(leftWidth / bandCount, 1u);
    const uint bandPixel = pixel.x % bandWidth;
    const uint band = min((pixel.x * bandCount) / leftWidth, bandCount - 1u);
    if (pixel.x < leftWidth && (bandPixel < 8u || bandPixel >= bandWidth - 2u))
    {
        return float4(1.0, 1.0, 1.0, 1.0);
    }
    if (pixel.x < leftWidth && pixel.y < 24u)
    {
        if (band == 0u) return float4(0.15, 0.45, 1.0, 1.0);
        if (band == 1u) return float4(0.0, 0.85, 0.25, 1.0);
        if (band == 2u) return float4(1.0, 0.85, 0.0, 1.0);
        if (band == 3u) return float4(1.0, 0.40, 0.0, 1.0);
        if (band == 4u) return float4(0.70, 0.0, 1.0, 1.0);
        if (band == 5u) return float4(0.00, 0.95, 0.95, 1.0);
        if (band == 6u) return float4(0.75, 0.95, 0.00, 1.0);
        if (band == 7u) return float4(0.95, 0.35, 0.75, 1.0);
        return float4(0.35, 0.95, 0.55, 1.0);
    }
    if (pixel.x < leftWidth && pixel.y < 48u)
    {
        const uint flag =
            band == 0u ? RESTIR_PT_RRX_DEBUG_BYPASS_MOTION :
            band == 1u ? RESTIR_PT_RRX_DEBUG_BYPASS_DEPTH :
            band == 2u ? RESTIR_PT_RRX_DEBUG_BYPASS_NORMAL :
            band == 3u ? RESTIR_PT_RRX_DEBUG_BYPASS_SURFACE_SIMILARITY :
            band == 4u ? RESTIR_PT_RRX_DEBUG_BYPASS_RESET_MASK :
            band == 5u ? RESTIR_PT_RRX_DEBUG_BYPASS_PORTAL :
            0u;
        if (flag == 0u)
        {
            return float4(0.04, 0.08, 0.18, 1.0);
        }
        return RestirPTRrxDebugBypassEnabled(flag)
            ? float4(0.02, 0.85, 0.10, 1.0)
            : float4(0.20, 0.02, 0.02, 1.0);
    }

    if (status == RESTIR_PT_RRX_DI_TEMPORAL_STATUS_GLOBAL_CLEAR_PENDING ||
        status == RESTIR_PT_RRX_DI_TEMPORAL_STATUS_UNAVAILABLE ||
        status == RESTIR_PT_RRX_DI_TEMPORAL_STATUS_LOCAL_REJECTED)
    {
        return RestirPTRrxDiTemporalStatusColor(status, reservoir);
    }

    if (band == 0u)
    {
        return RestirPTRrxDiTemporalBooleanColor(debugInfo.projected != 0u, true);
    }
    if (band == 1u)
    {
        return debugInfo.previousSampleUsed != 0u
            ? float4(0.02, 0.85, 0.10, 1.0)
            : float4(0.05, 0.24, 0.82, 1.0);
    }
    if (band == 2u)
    {
        return RestirPTRrxDiTemporalBooleanColor(
            debugInfo.previousTranslationValid != 0u,
            debugInfo.previousSampleUsed != 0u && debugInfo.previousValid != 0u);
    }
    if (band == 3u)
    {
        return RestirPTRrxDiTemporalBooleanColor(
            debugInfo.currentToPreviousValid != 0u,
            debugInfo.currentValid != 0u);
    }
    if (band == 4u)
    {
        return RestirPTRrxDiTemporalSelectedStabilityColor(debugInfo);
    }
    if (band == 5u)
    {
        return RestirPTRrxDiTemporalBooleanColor(debugInfo.previousBestSourceValid != 0u, debugInfo.projected != 0u);
    }
    if (band == 6u)
    {
        return RestirPTRrxDiTemporalBooleanColor(
            debugInfo.previousBestTranslationValid != 0u,
            debugInfo.previousBestSourceValid != 0u);
    }
    if (band == 7u)
    {
        return RestirPTRrxDiTemporalBooleanColor(
            debugInfo.previousBestCandidateValid != 0u,
            debugInfo.previousBestTranslationValid != 0u);
    }
    return RestirPTRrxDiTemporalBooleanColor(
        debugInfo.previousBestSelectedByCombine != 0u,
        debugInfo.previousBestCandidateValid != 0u);
}

bool RestirPTRrxDiTryGenerateTemporalReservoir(
    RAB_Surface surface,
    uint2 pixel,
    out RTXDI_DIReservoir temporalReservoir,
    out uint status,
    out RestirPTRrxDiTemporalDebugInfo debugInfo)
{
    temporalReservoir = RTXDI_EmptyDIReservoir();
    status = RESTIR_PT_RRX_DI_TEMPORAL_STATUS_CURRENT_ONLY;
    debugInfo = RestirPTRrxDiEmptyTemporalDebugInfo();

    if (RestirPTRrxDiReservoirClearPending())
    {
        status = RESTIR_PT_RRX_DI_TEMPORAL_STATUS_GLOBAL_CLEAR_PENDING;
        return false;
    }
    if (!RestirPTRrxDiReservoirAvailable() || !RAB_IsSurfaceValid(surface) || !RAB_SurfaceSupportsOpaqueDiffuseBrdf(surface))
    {
        status = RESTIR_PT_RRX_DI_TEMPORAL_STATUS_UNAVAILABLE;
        return false;
    }

    int2 previousPixel;
    float3 motionVector;
    const bool projected = RestirPTTryProjectMotionVectorToPreviousPixelFull(pixel, previousPixel, motionVector);
    debugInfo.projected = projected ? 1u : 0u;

    RTXDI_DIReservoir projectedPreviousReservoir = RTXDI_EmptyDIReservoir();
    if (projected)
    {
        // Proof-slice approximation: Remix reads RtxdiBestLights here. The
        // active rbdoom path has no bound best-light feedback buffer yet.
        projectedPreviousReservoir = LoadRestirPTRrxDiReservoir(uint2(previousPixel), RestirPTRemixDiReservoirPageInfo.y);
    }
    const bool projectedPreviousValid = projected && RTXDI_IsValidDIReservoir(projectedPreviousReservoir);

    RestirPTRrxDiInitialDebugInfo initialDebugInfo;
    const RTXDI_DIReservoir currentReservoir = RestirPTRrxDiBuildInitialReservoir(
        surface,
        pixel,
        projectedPreviousReservoir,
        projectedPreviousValid,
        initialDebugInfo);
    debugInfo.previousBestSourceValid = initialDebugInfo.previousBestSourceValid;
    debugInfo.previousBestTranslationValid = initialDebugInfo.previousBestTranslationValid;
    debugInfo.previousBestCandidateValid = initialDebugInfo.previousBestCandidateValid;
    debugInfo.previousBestSelectedByCombine = initialDebugInfo.previousBestSelectedByCombine;
    debugInfo.emissiveCandidateCount = initialDebugInfo.emissiveCandidateCount;
    debugInfo.doomAnalyticCandidateCount = initialDebugInfo.doomAnalyticCandidateCount;
    debugInfo.emissiveLightInfoInvalidCount = initialDebugInfo.emissiveLightInfoInvalidCount;
    debugInfo.doomAnalyticLightInfoInvalidCount = initialDebugInfo.doomAnalyticLightInfoInvalidCount;
    debugInfo.emissiveSampleInvalidCount = initialDebugInfo.emissiveSampleInvalidCount;
    debugInfo.doomAnalyticSampleInvalidCount = initialDebugInfo.doomAnalyticSampleInvalidCount;
    debugInfo.emissiveZeroTargetPdfCount = initialDebugInfo.emissiveZeroTargetPdfCount;
    debugInfo.doomAnalyticZeroTargetPdfCount = initialDebugInfo.doomAnalyticZeroTargetPdfCount;
    debugInfo.doomAnalyticOutsideInfluenceCount = initialDebugInfo.doomAnalyticOutsideInfluenceCount;
    debugInfo.doomAnalyticNonFinitePayloadCount = initialDebugInfo.doomAnalyticNonFinitePayloadCount;
    debugInfo.emissiveStreamedCount = initialDebugInfo.emissiveStreamedCount;
    debugInfo.doomAnalyticStreamedCount = initialDebugInfo.doomAnalyticStreamedCount;
    debugInfo.randomSelectedLightIndexEncoded = initialDebugInfo.randomSelectedLightIndexEncoded;
    debugInfo.randomSelectedLightType = initialDebugInfo.randomSelectedLightType;
    debugInfo.randomSelectedSource = initialDebugInfo.randomSelectedSource;
    debugInfo.finalInitialSelectedLightType = initialDebugInfo.finalSelectedLightType;
    debugInfo.finalInitialSelectedSource = initialDebugInfo.finalSelectedSource;
    debugInfo.emissiveMaxTargetPdf = initialDebugInfo.emissiveMaxTargetPdf;
    debugInfo.doomAnalyticMaxTargetPdf = initialDebugInfo.doomAnalyticMaxTargetPdf;
    debugInfo.emissiveInvSourcePdf = initialDebugInfo.emissiveInvSourcePdf;
    debugInfo.doomAnalyticInvSourcePdf = initialDebugInfo.doomAnalyticInvSourcePdf;
    debugInfo.randomStreamWeightSum = initialDebugInfo.randomStreamWeightSum;
    debugInfo.randomStreamM = initialDebugInfo.randomStreamM;
    debugInfo.randomSelectedTargetPdf = initialDebugInfo.randomSelectedTargetPdf;
    debugInfo.randomSelectedInvSourcePdf = initialDebugInfo.randomSelectedInvSourcePdf;
    debugInfo.randomSelectedRisWeight = initialDebugInfo.randomSelectedRisWeight;
    debugInfo.randomSelectedStreamWeightSum = initialDebugInfo.randomSelectedStreamWeightSum;
    debugInfo.randomFinalWeightSum = initialDebugInfo.randomFinalWeightSum;
    debugInfo.combinedPreFinalizeWeightSum = initialDebugInfo.combinedPreFinalizeWeightSum;
    debugInfo.combinedPreFinalizeM = initialDebugInfo.combinedPreFinalizeM;
    debugInfo.combinedSelectedTargetPdf = initialDebugInfo.combinedSelectedTargetPdf;
    debugInfo.finalInitialWeightSum = initialDebugInfo.finalInitialWeightSum;
    debugInfo.finalInitialTargetPdf = initialDebugInfo.finalInitialTargetPdf;
    debugInfo.previousBestTargetPdf = initialDebugInfo.previousBestTargetPdf;
    debugInfo.previousBestWeightSum = initialDebugInfo.previousBestWeightSum;
    debugInfo.selectedEffectiveSourceWeight = initialDebugInfo.selectedEffectiveSourceWeight;
    debugInfo.selectedPreFinalizeWeightSum = initialDebugInfo.selectedPreFinalizeWeightSum;
    StoreRestirPTRrxDiReservoir(pixel, RestirPTRemixDiReservoirPageInfo.x, currentReservoir);
    const bool currentValid = RTXDI_IsValidDIReservoir(currentReservoir);
    debugInfo.currentValid = currentValid ? 1u : 0u;
    if (currentValid)
    {
        debugInfo.currentLightIndexEncoded = RTXDI_GetDIReservoirLightIndex(currentReservoir) + 1u;
        debugInfo.currentWeightSum = currentReservoir.weightSum;
        debugInfo.currentTargetPdf = currentReservoir.targetPdf;
    }
    if (!currentValid)
    {
        StoreRestirPTRrxDiReservoir(pixel, RestirPTRemixDiReservoirPageInfo.x, RTXDI_EmptyDIReservoir());
        status = RESTIR_PT_RRX_DI_TEMPORAL_STATUS_LOCAL_REJECTED;
        return false;
    }

    const int currentToPrevious = currentValid
        ? RAB_TranslateActiveRrxLightIndex(RTXDI_GetDIReservoirLightIndex(currentReservoir), true)
        : -1;
    debugInfo.currentToPreviousValid = currentToPrevious >= 0 ? 1u : 0u;
    if (currentToPrevious >= 0)
    {
        debugInfo.currentToPreviousIndexEncoded = (uint)currentToPrevious + 1u;
    }

    if (RestirPTRrxFinalConsumerCurrentOnly())
    {
        temporalReservoir = currentReservoir;
        debugInfo.outputValid = 1u;
        status = RESTIR_PT_RRX_DI_TEMPORAL_STATUS_CURRENT_ONLY;
        return true;
    }

    if (!projected)
    {
        temporalReservoir = currentReservoir;
        debugInfo.outputValid = 1u;
        status = RESTIR_PT_RRX_DI_TEMPORAL_STATUS_CURRENT_ONLY;
        return true;
    }

    const RemixRtxdiTemporalReuseCoreResult temporalResult = RemixRtxdiRunTemporalReuseCore(
        surface,
        currentReservoir,
        RestirPTRrxDiTemporalDesc(pixel, motionVector),
        RestirPTRrxDiReservoirParams());
    temporalReservoir = temporalResult.reservoir;
    const int2 temporalSamplePixel = temporalResult.temporalSamplePixel;
    StoreRestirPTRrxDiReservoir(pixel, RestirPTRemixDiReservoirPageInfo.x, temporalReservoir);

    const bool outputValid = RTXDI_IsValidDIReservoir(temporalReservoir);
    if (outputValid)
    {
        debugInfo.temporalWeightSum = temporalReservoir.weightSum;
        debugInfo.temporalTargetPdf = temporalReservoir.targetPdf;
    }
    const bool outputSelectedCurrent =
        outputValid &&
        currentValid &&
        RTXDI_GetDIReservoirLightIndex(temporalReservoir) == RTXDI_GetDIReservoirLightIndex(currentReservoir);
    const bool previousSampleUsed = temporalSamplePixel.x >= 0 && temporalSamplePixel.y >= 0;
    debugInfo.previousSampleUsed = previousSampleUsed ? 1u : 0u;
    RTXDI_DIReservoir usedPreviousReservoir = RTXDI_EmptyDIReservoir();
    if (previousSampleUsed)
    {
        usedPreviousReservoir = LoadRestirPTRrxDiReservoir(uint2(temporalSamplePixel), RestirPTRemixDiReservoirPageInfo.y);
    }
    const bool usedPreviousValid = RTXDI_IsValidDIReservoir(usedPreviousReservoir);
    debugInfo.previousValid = usedPreviousValid ? 1u : 0u;
    if (usedPreviousValid)
    {
        debugInfo.previousLightIndexEncoded = RTXDI_GetDIReservoirLightIndex(usedPreviousReservoir) + 1u;
        debugInfo.previousUsedWeightSum = usedPreviousReservoir.weightSum;
        debugInfo.previousUsedTargetPdf = usedPreviousReservoir.targetPdf;
    }
    const int usedPreviousToCurrent = usedPreviousValid
        ? RAB_TranslateActiveRrxLightIndex(RTXDI_GetDIReservoirLightIndex(usedPreviousReservoir), false)
        : -1;
    if (usedPreviousToCurrent >= 0)
    {
        debugInfo.previousToCurrentIndexEncoded = (uint)usedPreviousToCurrent + 1u;
    }

    const bool previousTranslationValid = previousSampleUsed && usedPreviousValid && usedPreviousToCurrent >= 0;
    debugInfo.outputValid = outputValid ? 1u : 0u;
    debugInfo.previousTranslationValid = previousTranslationValid ? 1u : 0u;
    debugInfo.selectedLightChanged =
        previousTranslationValid &&
        currentValid &&
        usedPreviousToCurrent != int(RTXDI_GetDIReservoirLightIndex(currentReservoir))
            ? 1u
            : 0u;
    if (!outputValid)
    {
        status = RESTIR_PT_RRX_DI_TEMPORAL_STATUS_LOCAL_REJECTED;
    }
    else if (previousSampleUsed && usedPreviousValid && usedPreviousToCurrent < 0)
    {
        status = RESTIR_PT_RRX_DI_TEMPORAL_STATUS_PREVIOUS_TRANSLATION_INVALID;
    }
    else if (outputSelectedCurrent && currentToPrevious < 0)
    {
        status = RESTIR_PT_RRX_DI_TEMPORAL_STATUS_CURRENT_TO_PREVIOUS_INVALID;
    }
    else if (previousTranslationValid)
    {
        status = RESTIR_PT_RRX_DI_TEMPORAL_STATUS_PREVIOUS_TRANSLATION_VALID;
    }
    else if (!previousSampleUsed)
    {
        status = RESTIR_PT_RRX_DI_TEMPORAL_STATUS_PREVIOUS_ABSENT;
    }

    return outputValid;
}

bool RestirPTRrxDiTryEvaluateTemporalContribution(RAB_Surface surface, uint2 pixel, out float3 contribution)
{
    contribution = float3(0.0, 0.0, 0.0);
    if (!RestirPTRrxDiTemporalActive())
    {
        return false;
    }

    RTXDI_DIReservoir reservoir;
    uint status;
    RestirPTRrxDiTemporalDebugInfo debugInfo;
    if (!RestirPTRrxDiTryGenerateTemporalReservoir(surface, pixel, reservoir, status, debugInfo) ||
        !RTXDI_IsValidDIReservoir(reservoir))
    {
        return false;
    }

    if (RestirPTRrxDebugFlatContribution())
    {
        contribution = float3(0.65, 0.95, 0.20);
        return true;
    }

    const uint lightIndex = RTXDI_GetDIReservoirLightIndex(reservoir);
    const RAB_LightInfo lightInfo = RAB_LoadActiveRrxLightInfo(lightIndex, false);
    if (!RAB_IsLightInfoValid(lightInfo))
    {
        return false;
    }

    const RAB_LightSample lightSample = RAB_SampleActiveRrxPolymorphicLight(
        lightInfo,
        surface,
        RTXDI_GetDIReservoirSampleUV(reservoir));
    if (lightSample.valid == 0u || lightSample.solidAnglePdf <= 1.0e-6)
    {
        return false;
    }

    float3 lightDir;
    float lightDistance;
    RAB_GetLightDirDistance(surface, lightSample, lightDir, lightDistance);
    const float3 normal = RAB_SafeNormalize(RAB_GetSurfaceNormal(surface), RAB_GetSurfaceGeoNormal(surface));
    const float ndotl = saturate(dot(normal, lightDir));
    if (ndotl <= 0.0)
    {
        return false;
    }

    if (!PathTraceSafetyDisabled(RT_PT_SAFETY_DISABLE_RESTIR_VISIBILITY_RAY))
    {
        const float normalOffsetSign = dot(normal, lightDir) >= 0.0 ? 1.0 : -1.0;
        const float3 shadowOrigin = surface.worldPos + normal * (normalOffsetSign * 0.75) + lightDir * 0.25;
        const float shadowTMax = max(lightDistance - 0.5, 0.01);
        if (TraceSmokeShadowVisibility(shadowOrigin, lightDir, shadowTMax, 0xffffffffu, 0xffffffffu, 0xffffffffu) <= 0.0)
        {
            return false;
        }
    }

    contribution = RestirPTSanitizePreviewContribution(
        RAB_GetReflectedBsdfRadianceForSurface(
            lightSample.position,
            lightSample.radiance,
            surface) *
        RTXDI_GetDIReservoirInvPdf(reservoir) /
        max(lightSample.solidAnglePdf, 1.0e-6));
    return RAB_Luminance(contribution) > 0.0;
}

static const uint RESTIR_PT_RRX_DI_REPLAY_INVALID_RESERVOIR = 0u;
static const uint RESTIR_PT_RRX_DI_REPLAY_LIGHT_INFO_INVALID = 1u;
static const uint RESTIR_PT_RRX_DI_REPLAY_SAMPLE_INVALID = 2u;
static const uint RESTIR_PT_RRX_DI_REPLAY_ZERO_TARGET_PDF = 3u;
static const uint RESTIR_PT_RRX_DI_REPLAY_ZERO_SOLID_ANGLE = 4u;
static const uint RESTIR_PT_RRX_DI_REPLAY_ZERO_NDOTL = 5u;
static const uint RESTIR_PT_RRX_DI_REPLAY_VISIBILITY_REJECTED = 6u;
static const uint RESTIR_PT_RRX_DI_REPLAY_ZERO_CONTRIBUTION = 7u;
static const uint RESTIR_PT_RRX_DI_REPLAY_EVALUATED = 8u;
static const uint RESTIR_PT_RRX_DI_REPLAY_OUTSIDE_INFLUENCE = 9u;
static const uint RESTIR_PT_RRX_DI_REPLAY_NONFINITE_PAYLOAD = 10u;

float4 RestirPTRrxDiLightTypeColor(uint lightType)
{
    if (lightType == PATH_TRACE_UNIFIED_LIGHT_TYPE_EMISSIVE_TRIANGLE)
    {
        return float4(0.05, 0.75, 0.12, 1.0);
    }
    if (lightType == PATH_TRACE_UNIFIED_LIGHT_TYPE_DOOM_ANALYTIC)
    {
        return float4(0.05, 0.80, 0.95, 1.0);
    }
    return float4(0.30, 0.02, 0.32, 1.0);
}

float4 RestirPTRrxDiTypeMaskColor(bool emissive, bool doomAnalytic)
{
    if (emissive && doomAnalytic)
    {
        return float4(0.95, 0.85, 0.05, 1.0);
    }
    if (emissive)
    {
        return RestirPTRrxDiLightTypeColor(PATH_TRACE_UNIFIED_LIGHT_TYPE_EMISSIVE_TRIANGLE);
    }
    if (doomAnalytic)
    {
        return RestirPTRrxDiLightTypeColor(PATH_TRACE_UNIFIED_LIGHT_TYPE_DOOM_ANALYTIC);
    }
    return float4(0.04, 0.06, 0.08, 1.0);
}

float4 RestirPTRrxDiRangeSurvivalColor(
    uint lightType,
    uint candidateCount,
    uint streamedCount,
    uint lightInfoInvalidCount,
    uint sampleInvalidCount,
    uint zeroTargetPdfCount,
    uint outsideInfluenceCount,
    uint nonFinitePayloadCount)
{
    if (candidateCount == 0u)
    {
        return float4(0.02, 0.03, 0.05, 1.0);
    }
    if (streamedCount > 0u)
    {
        const float heat = saturate((float)streamedCount / max((float)candidateCount, 1.0));
        return RestirPTRrxDiLightTypeColor(lightType) * (0.30 + 0.70 * heat);
    }
    if (lightInfoInvalidCount > 0u)
    {
        return float4(0.70, 0.05, 0.95, 1.0);
    }
    if (nonFinitePayloadCount > 0u)
    {
        return float4(0.95, 0.35, 0.75, 1.0);
    }
    if (outsideInfluenceCount > 0u)
    {
        return float4(0.06, 0.22, 0.80, 1.0);
    }
    if (sampleInvalidCount > 0u)
    {
        return float4(1.0, 0.45, 0.02, 1.0);
    }
    if (zeroTargetPdfCount > 0u)
    {
        return float4(0.95, 0.05, 0.02, 1.0);
    }
    return float4(0.22, 0.22, 0.24, 1.0);
}

uint RestirPTRrxDiClassifyTemporalReplay(RAB_Surface surface, RTXDI_DIReservoir reservoir, out uint lightType)
{
    lightType = PATH_TRACE_UNIFIED_LIGHT_TYPE_INVALID;
    if (!RTXDI_IsValidDIReservoir(reservoir))
    {
        return RESTIR_PT_RRX_DI_REPLAY_INVALID_RESERVOIR;
    }

    const uint lightIndex = RTXDI_GetDIReservoirLightIndex(reservoir);
    const RAB_LightInfo lightInfo = RAB_LoadActiveRrxLightInfo(lightIndex, false);
    lightType = RestirPTRrxDiLightTypeFromInfo(lightInfo);
    if (!RAB_IsLightInfoValid(lightInfo))
    {
        return RESTIR_PT_RRX_DI_REPLAY_LIGHT_INFO_INVALID;
    }

    const RAB_LightSample lightSample = RAB_SampleActiveRrxPolymorphicLight(
        lightInfo,
        surface,
        RTXDI_GetDIReservoirSampleUV(reservoir));
    if (lightSample.valid == 0u)
    {
        if ((lightSample.flags & RAB_LIGHT_SAMPLE_FLAG_DOOM_OUTSIDE_INFLUENCE) != 0u)
        {
            return RESTIR_PT_RRX_DI_REPLAY_OUTSIDE_INFLUENCE;
        }
        if ((lightSample.flags & RAB_LIGHT_SAMPLE_FLAG_DOOM_NONFINITE_PAYLOAD) != 0u)
        {
            return RESTIR_PT_RRX_DI_REPLAY_NONFINITE_PAYLOAD;
        }
        return RESTIR_PT_RRX_DI_REPLAY_SAMPLE_INVALID;
    }

    const float targetPdf = max(RAB_GetLightSampleTargetPdfForSurface(lightSample, surface), 0.0);
    if (targetPdf <= 0.0)
    {
        return RESTIR_PT_RRX_DI_REPLAY_ZERO_TARGET_PDF;
    }
    if (lightSample.solidAnglePdf <= 1.0e-6)
    {
        return RESTIR_PT_RRX_DI_REPLAY_ZERO_SOLID_ANGLE;
    }

    float3 lightDir;
    float lightDistance;
    RAB_GetLightDirDistance(surface, lightSample, lightDir, lightDistance);
    const float3 normal = RAB_SafeNormalize(RAB_GetSurfaceNormal(surface), RAB_GetSurfaceGeoNormal(surface));
    const float ndotl = saturate(dot(normal, lightDir));
    if (ndotl <= 0.0)
    {
        return RESTIR_PT_RRX_DI_REPLAY_ZERO_NDOTL;
    }

    if (!PathTraceSafetyDisabled(RT_PT_SAFETY_DISABLE_RESTIR_VISIBILITY_RAY))
    {
        const float normalOffsetSign = dot(normal, lightDir) >= 0.0 ? 1.0 : -1.0;
        const float3 shadowOrigin = surface.worldPos + normal * (normalOffsetSign * 0.75) + lightDir * 0.25;
        const float shadowTMax = max(lightDistance - 0.5, 0.01);
        if (TraceSmokeShadowVisibility(shadowOrigin, lightDir, shadowTMax, 0xffffffffu, 0xffffffffu, 0xffffffffu) <= 0.0)
        {
            return RESTIR_PT_RRX_DI_REPLAY_VISIBILITY_REJECTED;
        }
    }

    const float3 contribution = RestirPTSanitizePreviewContribution(
        RAB_GetReflectedBsdfRadianceForSurface(
            lightSample.position,
            lightSample.radiance,
            surface) *
        RTXDI_GetDIReservoirInvPdf(reservoir) /
        max(lightSample.solidAnglePdf, 1.0e-6));
    return RAB_Luminance(contribution) > 0.0
        ? RESTIR_PT_RRX_DI_REPLAY_EVALUATED
        : RESTIR_PT_RRX_DI_REPLAY_ZERO_CONTRIBUTION;
}

float4 RestirPTRrxDiReplayStatusColor(uint status, uint lightType)
{
    if (status == RESTIR_PT_RRX_DI_REPLAY_EVALUATED)
    {
        return RestirPTRrxDiLightTypeColor(lightType);
    }
    if (status == RESTIR_PT_RRX_DI_REPLAY_INVALID_RESERVOIR)
    {
        return float4(0.0, 0.0, 0.0, 1.0);
    }
    if (status == RESTIR_PT_RRX_DI_REPLAY_LIGHT_INFO_INVALID)
    {
        return float4(0.70, 0.05, 0.95, 1.0);
    }
    if (status == RESTIR_PT_RRX_DI_REPLAY_SAMPLE_INVALID)
    {
        return float4(1.0, 0.45, 0.02, 1.0);
    }
    if (status == RESTIR_PT_RRX_DI_REPLAY_OUTSIDE_INFLUENCE)
    {
        return float4(0.06, 0.22, 0.80, 1.0);
    }
    if (status == RESTIR_PT_RRX_DI_REPLAY_NONFINITE_PAYLOAD)
    {
        return float4(0.95, 0.35, 0.75, 1.0);
    }
    if (status == RESTIR_PT_RRX_DI_REPLAY_ZERO_TARGET_PDF)
    {
        return float4(0.95, 0.05, 0.02, 1.0);
    }
    if (status == RESTIR_PT_RRX_DI_REPLAY_ZERO_SOLID_ANGLE)
    {
        return float4(1.0, 0.85, 0.04, 1.0);
    }
    if (status == RESTIR_PT_RRX_DI_REPLAY_ZERO_NDOTL)
    {
        return float4(0.05, 0.18, 0.95, 1.0);
    }
    if (status == RESTIR_PT_RRX_DI_REPLAY_VISIBILITY_REJECTED)
    {
        return float4(0.45, 0.02, 0.02, 1.0);
    }
    return float4(0.95, 0.35, 0.75, 1.0);
}

static const uint RESTIR_PT_RRX_DI_FINAL_EVALUATED = 0u;
static const uint RESTIR_PT_RRX_DI_FINAL_RESOURCE_UNAVAILABLE = 1u;
static const uint RESTIR_PT_RRX_DI_FINAL_SURFACE_INVALID = 2u;
static const uint RESTIR_PT_RRX_DI_FINAL_RESERVOIR_INVALID = 3u;
static const uint RESTIR_PT_RRX_DI_FINAL_LIGHT_INFO_INVALID = 4u;
static const uint RESTIR_PT_RRX_DI_FINAL_SAMPLE_INVALID = 5u;
static const uint RESTIR_PT_RRX_DI_FINAL_ZERO_SOLID_ANGLE = 6u;
static const uint RESTIR_PT_RRX_DI_FINAL_ZERO_SELECTION_PDF = 7u;
static const uint RESTIR_PT_RRX_DI_FINAL_ZERO_TARGET_PDF = 8u;
static const uint RESTIR_PT_RRX_DI_FINAL_ZERO_NDOTL = 9u;
static const uint RESTIR_PT_RRX_DI_FINAL_VISIBILITY_REJECTED = 10u;
static const uint RESTIR_PT_RRX_DI_FINAL_ZERO_CONTRIBUTION = 11u;

struct RestirPTRrxDiFinalConsumerResult
{
    uint status;
    uint lightType;
    uint lightIndexEncoded;
    float3 contribution;
    float3 reflectedRadiance;
    float inverseSelectionPdf;
    float solidAnglePdf;
    float targetPdf;
};

RestirPTRrxDiFinalConsumerResult RestirPTRrxDiEmptyFinalConsumerResult(uint status)
{
    RestirPTRrxDiFinalConsumerResult result = (RestirPTRrxDiFinalConsumerResult)0;
    result.status = status;
    result.lightType = PATH_TRACE_UNIFIED_LIGHT_TYPE_INVALID;
    return result;
}

float RestirPTRrxDiFinalVisibility(RAB_Surface surface, RAB_LightSample lightSample)
{
    if (PathTraceSafetyDisabled(RT_PT_SAFETY_DISABLE_RESTIR_VISIBILITY_RAY))
    {
        return 1.0;
    }

    if (lightSample.lightType == RAB_LIGHT_TYPE_DOOM_ANALYTIC_SPHERE &&
        (lightSample.flags & RT_DOOM_ANALYTIC_LIGHT_CASTS_SHADOWS) == 0u)
    {
        return 1.0;
    }

    uint ignoreInstanceId = 0xffffffffu;
    uint ignorePrimitiveIndex = 0xffffffffu;
    uint ignoreMaterialId = 0xffffffffu;
    if (lightSample.lightType == RAB_LIGHT_TYPE_EMISSIVE_TRIANGLE)
    {
        const uint emissiveTriangleCount = PathTraceSafetyDisabled(RT_PT_SAFETY_DISABLE_EMISSIVE_TRIANGLE_SAMPLING) ? 0u : (uint)max(EmissiveInfo.x, 0.0);
        if (lightSample.lightIndex < emissiveTriangleCount)
        {
            const PathTraceSmokeEmissiveTriangle emissiveTriangle = SmokeEmissiveTriangles[lightSample.lightIndex];
            const bool historicalDynamicEmissive = (emissiveTriangle.padding0 & RT_SMOKE_EMISSIVE_TRIANGLE_HISTORY_DYNAMIC) != 0u;
            ignoreInstanceId = historicalDynamicEmissive ? 0xffffffffu : emissiveTriangle.instanceId;
            ignorePrimitiveIndex = historicalDynamicEmissive ? 0xffffffffu : emissiveTriangle.primitiveIndex;
            ignoreMaterialId = emissiveTriangle.materialId;
        }
    }

    float3 lightDir;
    float lightDistance;
    RAB_GetLightDirDistance(surface, lightSample, lightDir, lightDistance);
    const float3 normal = RAB_SafeNormalize(RAB_GetSurfaceNormal(surface), RAB_GetSurfaceGeoNormal(surface));
    const float normalOffsetSign = dot(normal, lightDir) >= 0.0 ? 1.0 : -1.0;
    const float3 shadowOrigin = surface.worldPos + normal * (normalOffsetSign * 0.75) + lightDir * 0.25;
    const float shadowTMax = max(lightDistance - 0.5, 0.01);
    return TraceSmokeShadowVisibility(shadowOrigin, lightDir, shadowTMax, ignoreInstanceId, ignorePrimitiveIndex, ignoreMaterialId);
}

RestirPTRrxDiFinalConsumerResult RestirPTRrxDiEvaluateFinalConsumer(RAB_Surface surface, uint2 pixel)
{
    if (!RestirPTRrxDiTemporalActive())
    {
        return RestirPTRrxDiEmptyFinalConsumerResult(RESTIR_PT_RRX_DI_FINAL_RESOURCE_UNAVAILABLE);
    }
    if (!RAB_IsSurfaceValid(surface) || !RAB_SurfaceSupportsOpaqueDiffuseBrdf(surface))
    {
        return RestirPTRrxDiEmptyFinalConsumerResult(RESTIR_PT_RRX_DI_FINAL_SURFACE_INVALID);
    }

    RTXDI_DIReservoir reservoir;
    uint temporalStatus;
    RestirPTRrxDiTemporalDebugInfo temporalDebugInfo;
    if (!RestirPTRrxDiTryGenerateTemporalReservoir(surface, pixel, reservoir, temporalStatus, temporalDebugInfo) ||
        !RTXDI_IsValidDIReservoir(reservoir))
    {
        return RestirPTRrxDiEmptyFinalConsumerResult(RESTIR_PT_RRX_DI_FINAL_RESERVOIR_INVALID);
    }

    RestirPTRrxDiFinalConsumerResult result = RestirPTRrxDiEmptyFinalConsumerResult(RESTIR_PT_RRX_DI_FINAL_ZERO_CONTRIBUTION);
    result.inverseSelectionPdf = max(reservoir.weightSum, 0.0);
    if (result.inverseSelectionPdf <= 0.0)
    {
        result.status = RESTIR_PT_RRX_DI_FINAL_ZERO_SELECTION_PDF;
        return result;
    }

    const uint lightIndex = RTXDI_GetDIReservoirLightIndex(reservoir);
    result.lightIndexEncoded = lightIndex + 1u;
    const RAB_LightInfo lightInfo = RAB_LoadActiveRrxLightInfo(lightIndex, false);
    result.lightType = RestirPTRrxDiLightTypeFromInfo(lightInfo);
    if (!RAB_IsLightInfoValid(lightInfo))
    {
        result.status = RESTIR_PT_RRX_DI_FINAL_LIGHT_INFO_INVALID;
        return result;
    }

    const RAB_LightSample lightSample = RAB_SampleActiveRrxPolymorphicLight(
        lightInfo,
        surface,
        RTXDI_GetDIReservoirSampleUV(reservoir));
    if (lightSample.valid == 0u)
    {
        result.status = RESTIR_PT_RRX_DI_FINAL_SAMPLE_INVALID;
        return result;
    }

    result.solidAnglePdf = max(lightSample.solidAnglePdf, 0.0);
    if (result.solidAnglePdf * result.inverseSelectionPdf <= 0.0)
    {
        result.status = RESTIR_PT_RRX_DI_FINAL_ZERO_SOLID_ANGLE;
        return result;
    }

    result.targetPdf = max(RAB_GetLightSampleTargetPdfForSurface(lightSample, surface), 0.0);
    if (result.targetPdf <= 0.0)
    {
        result.status = RESTIR_PT_RRX_DI_FINAL_ZERO_TARGET_PDF;
        return result;
    }

    float3 lightDir;
    float lightDistance;
    RAB_GetLightDirDistance(surface, lightSample, lightDir, lightDistance);
    const float3 normal = RAB_SafeNormalize(RAB_GetSurfaceNormal(surface), RAB_GetSurfaceGeoNormal(surface));
    const float ndotl = saturate(dot(normal, lightDir));
    if (ndotl <= 0.0)
    {
        result.status = RESTIR_PT_RRX_DI_FINAL_ZERO_NDOTL;
        return result;
    }

    if (RestirPTRrxDiFinalVisibility(surface, lightSample) <= 0.0)
    {
        result.status = RESTIR_PT_RRX_DI_FINAL_VISIBILITY_REJECTED;
        return result;
    }

    result.reflectedRadiance =
        RAB_EvaluateSurfaceBrdf(surface, lightDir, RAB_GetSurfaceViewDir(surface)) *
        max(lightSample.radiance, float3(0.0, 0.0, 0.0)) *
        ndotl;
    result.contribution = RestirPTSanitizePreviewContribution(
        result.reflectedRadiance * result.inverseSelectionPdf / max(result.solidAnglePdf, 1.0e-6));
    result.status = RAB_Luminance(result.contribution) > 0.0
        ? RESTIR_PT_RRX_DI_FINAL_EVALUATED
        : RESTIR_PT_RRX_DI_FINAL_ZERO_CONTRIBUTION;
    return result;
}

float4 RestirPTRrxDiFinalConsumerStatusColor(uint status, uint lightType)
{
    if (status == RESTIR_PT_RRX_DI_FINAL_EVALUATED)
    {
        return RestirPTRrxDiLightTypeColor(lightType);
    }
    if (status == RESTIR_PT_RRX_DI_FINAL_RESOURCE_UNAVAILABLE)
    {
        return float4(0.28, 0.02, 0.32, 1.0);
    }
    if (status == RESTIR_PT_RRX_DI_FINAL_SURFACE_INVALID)
    {
        return float4(0.32, 0.0, 0.0, 1.0);
    }
    if (status == RESTIR_PT_RRX_DI_FINAL_RESERVOIR_INVALID)
    {
        return float4(0.08, 0.08, 0.08, 1.0);
    }
    if (status == RESTIR_PT_RRX_DI_FINAL_LIGHT_INFO_INVALID)
    {
        return float4(0.70, 0.05, 0.95, 1.0);
    }
    if (status == RESTIR_PT_RRX_DI_FINAL_SAMPLE_INVALID)
    {
        return float4(1.0, 0.45, 0.02, 1.0);
    }
    if (status == RESTIR_PT_RRX_DI_FINAL_ZERO_SOLID_ANGLE)
    {
        return float4(1.0, 0.85, 0.04, 1.0);
    }
    if (status == RESTIR_PT_RRX_DI_FINAL_ZERO_SELECTION_PDF)
    {
        return float4(0.70, 0.45, 0.02, 1.0);
    }
    if (status == RESTIR_PT_RRX_DI_FINAL_ZERO_TARGET_PDF)
    {
        return float4(0.95, 0.05, 0.02, 1.0);
    }
    if (status == RESTIR_PT_RRX_DI_FINAL_ZERO_NDOTL)
    {
        return float4(0.05, 0.18, 0.95, 1.0);
    }
    if (status == RESTIR_PT_RRX_DI_FINAL_VISIBILITY_REJECTED)
    {
        return float4(0.45, 0.02, 0.02, 1.0);
    }
    return float4(0.95, 0.35, 0.75, 1.0);
}

float4 EvaluateRestirPTRrxDiFinalConsumerView(RAB_Surface surface, uint2 pixel)
{
    const RestirPTRrxDiFinalConsumerResult result = RestirPTRrxDiEvaluateFinalConsumer(surface, pixel);
    if (pixel.y < 24u)
    {
        return RestirPTRrxDiFinalConsumerStatusColor(result.status, result.lightType);
    }
    if (result.status != RESTIR_PT_RRX_DI_FINAL_EVALUATED)
    {
        return float4(0.0, 0.0, 0.0, 1.0);
    }
    return float4(RestirPTToneMapPreview(result.contribution), 1.0);
}

float4 RestirPTRrxDiFinalConsumerMagnitudeColor(RestirPTRrxDiFinalConsumerResult result)
{
    if (result.status != RESTIR_PT_RRX_DI_FINAL_EVALUATED)
    {
        return RestirPTRrxDiFinalConsumerStatusColor(result.status, result.lightType);
    }
    const float heat = saturate(log2(1.0 + RAB_Luminance(result.contribution) * max(RestirPTInfo.w, 0.0)) / 8.0);
    const float3 typeColor = RestirPTRrxDiLightTypeColor(result.lightType).rgb;
    return float4(typeColor * (0.15 + heat * 0.85), 1.0);
}

float4 EvaluateRestirPTRrxDiFinalConsumerClassifierView(RAB_Surface surface, uint2 pixel)
{
    const RestirPTRrxDiFinalConsumerResult result = RestirPTRrxDiEvaluateFinalConsumer(surface, pixel);
    const uint2 dimensions = max(PathTraceFullOutputSize(), uint2(1u, 1u));
    if (pixel.x < dimensions.x / 2u)
    {
        return RestirPTRrxDiFinalConsumerStatusColor(result.status, result.lightType);
    }
    return RestirPTRrxDiFinalConsumerMagnitudeColor(result);
}

float4 RestirPTRrxDiSelectedLightIdentityColor(uint lightIndexEncoded, uint lightType)
{
    if (lightIndexEncoded == 0u)
    {
        return float4(0.08, 0.08, 0.08, 1.0);
    }
    const float3 hashColor = RestirPTRrxHashColor(lightIndexEncoded - 1u).rgb;
    const float3 typeColor = RestirPTRrxDiLightTypeColor(lightType).rgb;
    return float4(saturate(hashColor * 0.72 + typeColor * 0.28), 1.0);
}

float4 EvaluateRestirPTRrxDiSelectedLightIdentityView(RAB_Surface surface, uint2 pixel)
{
    const RestirPTRrxDiFinalConsumerResult result = RestirPTRrxDiEvaluateFinalConsumer(surface, pixel);
    if (result.lightIndexEncoded == 0u)
    {
        return RestirPTRrxDiFinalConsumerStatusColor(result.status, result.lightType);
    }

    const uint2 dimensions = max(PathTraceFullOutputSize(), uint2(1u, 1u));
    const float4 identityColor = RestirPTRrxDiSelectedLightIdentityColor(result.lightIndexEncoded, result.lightType);
    if (pixel.x < dimensions.x / 2u)
    {
        return identityColor;
    }

    if (result.status != RESTIR_PT_RRX_DI_FINAL_EVALUATED)
    {
        return RestirPTRrxDiFinalConsumerStatusColor(result.status, result.lightType);
    }
    const float heat = saturate(log2(1.0 + RAB_Luminance(result.contribution) * max(RestirPTInfo.w, 0.0)) / 8.0);
    return float4(identityColor.rgb * (0.15 + heat * 0.85), 1.0);
}

float RestirPTRrxDiPositiveLogHeat(float value)
{
    return saturate(log2(1.0 + max(value, 0.0)) / 12.0);
}

float4 RestirPTRrxDiTwoSourceBalanceColor(float emissiveValue, float doomAnalyticValue)
{
    const float total = max(emissiveValue + doomAnalyticValue, 0.0);
    if (total <= 0.0)
    {
        return float4(0.08, 0.08, 0.08, 1.0);
    }
    const float emissive = emissiveValue / total;
    const float doomAnalytic = doomAnalyticValue / total;
    const float heat = RestirPTRrxDiPositiveLogHeat(max(emissiveValue, doomAnalyticValue));
    const float3 color =
        float3(0.05, 0.75, 0.12) * emissive +
        float3(0.05, 0.80, 0.95) * doomAnalytic;
    return float4(saturate(color * (0.20 + heat * 0.80)), 1.0);
}

float4 EvaluateRestirPTRrxDiWeightAuditView(RAB_Surface surface, uint2 pixel)
{
    RTXDI_DIReservoir temporalReservoir;
    uint status;
    RestirPTRrxDiTemporalDebugInfo debugInfo;
    RestirPTRrxDiTryGenerateTemporalReservoir(surface, pixel, temporalReservoir, status, debugInfo);

    const uint2 dimensions = max(PathTraceFullOutputSize(), uint2(1u, 1u));
    const uint bandCount = 8u;
    const uint band = min((pixel.x * bandCount) / dimensions.x, bandCount - 1u);
    const uint bandWidth = max(dimensions.x / bandCount, 1u);
    const uint bandPixel = pixel.x % bandWidth;
    if (bandPixel < 4u || bandPixel >= bandWidth - 1u)
    {
        return float4(1.0, 1.0, 1.0, 1.0);
    }
    if (pixel.y < 24u)
    {
        if (band == 0u) return float4(0.05, 0.80, 0.65, 1.0);
        if (band == 1u) return float4(0.60, 0.85, 0.95, 1.0);
        if (band == 2u) return float4(1.0, 0.52, 0.04, 1.0);
        if (band == 3u) return float4(0.15, 0.65, 1.0, 1.0);
        if (band == 4u) return float4(0.95, 0.35, 0.95, 1.0);
        if (band == 5u) return float4(0.90, 0.90, 0.90, 1.0);
        if (band == 6u) return float4(0.95, 0.20, 0.08, 1.0);
        return float4(0.25, 0.95, 0.55, 1.0);
    }

    if (band == 0u)
    {
        return RestirPTRrxDiTwoSourceBalanceColor(debugInfo.emissiveMaxTargetPdf, debugInfo.doomAnalyticMaxTargetPdf);
    }
    if (band == 1u)
    {
        return float4(
            max(RestirPTRrxDiPositiveLogHeat(debugInfo.emissiveInvSourcePdf), 0.04),
            max(RestirPTRrxDiPositiveLogHeat(debugInfo.randomStreamM), 0.04),
            max(RestirPTRrxDiPositiveLogHeat(debugInfo.doomAnalyticInvSourcePdf), 0.04),
            1.0);
    }
    if (band == 2u)
    {
        const float heat = RestirPTRrxDiPositiveLogHeat(debugInfo.randomStreamWeightSum);
        return float4(0.95 * heat, 0.35 * heat, 0.04, 1.0);
    }
    if (band == 3u)
    {
        const float weightHeat = RestirPTRrxDiPositiveLogHeat(debugInfo.randomFinalWeightSum);
        const float targetHeat = RestirPTRrxDiPositiveLogHeat(debugInfo.randomSelectedTargetPdf);
        return float4(max(targetHeat, 0.04), max(weightHeat, 0.04), 0.90 * weightHeat, 1.0);
    }
    if (band == 4u)
    {
        const float weightHeat = RestirPTRrxDiPositiveLogHeat(debugInfo.combinedPreFinalizeWeightSum);
        const float targetHeat = RestirPTRrxDiPositiveLogHeat(debugInfo.combinedSelectedTargetPdf);
        return float4(max(weightHeat, 0.04), 0.05, max(targetHeat, 0.04), 1.0);
    }
    if (band == 5u)
    {
        const float weightHeat = RestirPTRrxDiPositiveLogHeat(debugInfo.finalInitialWeightSum);
        const float targetHeat = RestirPTRrxDiPositiveLogHeat(debugInfo.finalInitialTargetPdf);
        return float4(max(weightHeat, 0.04), max(targetHeat, 0.04), max(weightHeat * 0.85, 0.04), 1.0);
    }
    if (band == 6u)
    {
        const float previousPressure = debugInfo.previousBestTargetPdf * max(debugInfo.previousBestWeightSum, 0.0);
        const float randomPressure = max(debugInfo.randomStreamWeightSum, 0.0);
        const float pressure = previousPressure / max(previousPressure + randomPressure, 1.0e-6);
        if (debugInfo.previousBestCandidateValid == 0u)
        {
            return float4(0.04, 0.08, 0.18, 1.0);
        }
        return float4(0.10 + 0.85 * pressure, 0.85 * (1.0 - pressure), 0.05, 1.0);
    }

    const float currentHeat = RestirPTRrxDiPositiveLogHeat(debugInfo.currentWeightSum);
    const float previousHeat = RestirPTRrxDiPositiveLogHeat(debugInfo.previousUsedWeightSum);
    const float temporalHeat = RestirPTRrxDiPositiveLogHeat(debugInfo.temporalWeightSum);
    return float4(max(previousHeat, 0.04), max(currentHeat, 0.04), max(temporalHeat, 0.04), 1.0);
}

float4 RestirPTRrxDiSelectedSourceColor(uint selectedSource)
{
    if (selectedSource == RESTIR_PT_RRX_DI_SELECTED_SOURCE_EMISSIVE)
    {
        return float4(0.05, 0.75, 0.12, 1.0);
    }
    if (selectedSource == RESTIR_PT_RRX_DI_SELECTED_SOURCE_DOOM_ANALYTIC)
    {
        return float4(0.05, 0.80, 0.95, 1.0);
    }
    if (selectedSource == RESTIR_PT_RRX_DI_SELECTED_SOURCE_PREVIOUS_BEST)
    {
        return float4(0.95, 0.35, 0.95, 1.0);
    }
    return float4(0.35, 0.02, 0.02, 1.0);
}

float4 EvaluateRestirPTRrxDiSelectedWeightAuditView(RAB_Surface surface, uint2 pixel)
{
    RTXDI_DIReservoir temporalReservoir;
    uint status;
    RestirPTRrxDiTemporalDebugInfo debugInfo;
    RestirPTRrxDiTryGenerateTemporalReservoir(surface, pixel, temporalReservoir, status, debugInfo);

    const uint2 dimensions = max(PathTraceFullOutputSize(), uint2(1u, 1u));
    const uint bandCount = 8u;
    const uint band = min((pixel.x * bandCount) / dimensions.x, bandCount - 1u);
    const uint bandWidth = max(dimensions.x / bandCount, 1u);
    const uint bandPixel = pixel.x % bandWidth;
    if (bandPixel < 4u || bandPixel >= bandWidth - 1u)
    {
        return float4(1.0, 1.0, 1.0, 1.0);
    }
    if (pixel.y < 24u)
    {
        if (band == 0u) return float4(0.20, 0.95, 0.55, 1.0);
        if (band == 1u) return float4(0.90, 0.90, 0.90, 1.0);
        if (band == 2u) return float4(0.95, 0.20, 0.08, 1.0);
        if (band == 3u) return float4(0.05, 0.65, 0.95, 1.0);
        if (band == 4u) return float4(1.0, 0.60, 0.05, 1.0);
        if (band == 5u) return float4(0.80, 0.35, 0.95, 1.0);
        if (band == 6u) return float4(0.30, 0.95, 0.35, 1.0);
        return float4(0.95, 0.15, 0.15, 1.0);
    }

    const uint selectedSource = debugInfo.finalInitialSelectedSource;
    const bool selectedPreviousBest = selectedSource == RESTIR_PT_RRX_DI_SELECTED_SOURCE_PREVIOUS_BEST;
    const float selectedTargetPdf = selectedPreviousBest ? debugInfo.previousBestTargetPdf : debugInfo.randomSelectedTargetPdf;
    const float selectedSourceWeight = selectedPreviousBest ? debugInfo.selectedEffectiveSourceWeight : debugInfo.randomSelectedInvSourcePdf;
    const float selectedRisWeight = selectedPreviousBest
        ? debugInfo.previousBestTargetPdf * max(debugInfo.previousBestWeightSum, 0.0)
        : debugInfo.randomSelectedRisWeight;

    if (band == 0u)
    {
        return RestirPTRrxDiSelectedSourceColor(selectedSource);
    }
    if (band == 1u)
    {
        return debugInfo.currentLightIndexEncoded != 0u
            ? RestirPTRrxDiSelectedLightIdentityColor(debugInfo.currentLightIndexEncoded, debugInfo.finalInitialSelectedLightType)
            : float4(0.08, 0.08, 0.08, 1.0);
    }
    if (band == 2u)
    {
        const float heat = RestirPTRrxDiPositiveLogHeat(selectedTargetPdf);
        return float4(max(heat, 0.04), 0.08, 0.04, 1.0);
    }
    if (band == 3u)
    {
        const float heat = RestirPTRrxDiPositiveLogHeat(selectedSourceWeight);
        return float4(0.05, max(heat, 0.04), max(0.85 * heat, 0.04), 1.0);
    }
    if (band == 4u)
    {
        const float heat = RestirPTRrxDiPositiveLogHeat(selectedRisWeight);
        return float4(0.95 * heat, 0.45 * heat, 0.04, 1.0);
    }
    if (band == 5u)
    {
        const float heat = RestirPTRrxDiPositiveLogHeat(debugInfo.combinedPreFinalizeWeightSum);
        return float4(max(0.70 * heat, 0.04), 0.04, max(0.95 * heat, 0.04), 1.0);
    }
    if (band == 6u)
    {
        const float heat = RestirPTRrxDiPositiveLogHeat(debugInfo.finalInitialWeightSum);
        return float4(0.04, max(0.95 * heat, 0.04), max(0.25 * heat, 0.04), 1.0);
    }

    const float previousPressure = debugInfo.previousBestTargetPdf * max(debugInfo.previousBestWeightSum, 0.0);
    const float currentPressure = max(debugInfo.randomStreamWeightSum, 0.0);
    const float previousRatio = previousPressure / max(previousPressure + currentPressure, 1.0e-6);
    if (debugInfo.previousBestCandidateValid == 0u)
    {
        return float4(0.04, 0.08, 0.18, 1.0);
    }
    return float4(0.10 + 0.85 * previousRatio, 0.85 * (1.0 - previousRatio), 0.05, 1.0);
}

float4 EvaluateRestirPTRrxDiInitialSurvivalView(RAB_Surface surface, uint2 pixel)
{
    RTXDI_DIReservoir temporalReservoir;
    uint status;
    RestirPTRrxDiTemporalDebugInfo debugInfo;
    RestirPTRrxDiTryGenerateTemporalReservoir(surface, pixel, temporalReservoir, status, debugInfo);

    const uint2 dimensions = max(PathTraceFullOutputSize(), uint2(1u, 1u));
    const uint bandCount = 8u;
    const uint band = min((pixel.x * bandCount) / dimensions.x, bandCount - 1u);
    const uint bandWidth = max(dimensions.x / bandCount, 1u);
    const uint bandPixel = pixel.x % bandWidth;
    if (bandPixel < 4u || bandPixel >= bandWidth - 1u)
    {
        return float4(1.0, 1.0, 1.0, 1.0);
    }
    if (pixel.y < 24u)
    {
        if (band == 0u) return RestirPTRrxDiLightTypeColor(debugInfo.finalInitialSelectedLightType);
        if (band == 1u) return RestirPTRrxDiLightTypeColor(RestirPTRrxDiLightTypeFromReservoir(temporalReservoir));
        if (band == 2u) return float4(0.60, 0.90, 1.0, 1.0);
        if (band == 3u) return RestirPTRrxDiLightTypeColor(PATH_TRACE_UNIFIED_LIGHT_TYPE_EMISSIVE_TRIANGLE);
        if (band == 4u) return RestirPTRrxDiLightTypeColor(PATH_TRACE_UNIFIED_LIGHT_TYPE_DOOM_ANALYTIC);
        if (band == 5u) return float4(0.70, 0.05, 0.95, 1.0);
        if (band == 6u) return float4(1.0, 0.45, 0.02, 1.0);
        return float4(0.95, 0.05, 0.02, 1.0);
    }

    if (band == 0u)
    {
        return debugInfo.currentValid != 0u
            ? RestirPTRrxDiLightTypeColor(debugInfo.finalInitialSelectedLightType)
            : float4(0.32, 0.0, 0.0, 1.0);
    }
    if (band == 1u)
    {
        return RTXDI_IsValidDIReservoir(temporalReservoir)
            ? RestirPTRrxDiLightTypeColor(RestirPTRrxDiLightTypeFromReservoir(temporalReservoir))
            : RestirPTRrxDiTemporalStatusColor(status, temporalReservoir);
    }
    if (band == 2u)
    {
        uint replayLightType;
        const uint replayStatus = RestirPTRrxDiClassifyTemporalReplay(surface, temporalReservoir, replayLightType);
        return RestirPTRrxDiReplayStatusColor(replayStatus, replayLightType);
    }
    if (band == 3u)
    {
        return RestirPTRrxDiRangeSurvivalColor(
            PATH_TRACE_UNIFIED_LIGHT_TYPE_EMISSIVE_TRIANGLE,
            debugInfo.emissiveCandidateCount,
            debugInfo.emissiveStreamedCount,
            debugInfo.emissiveLightInfoInvalidCount,
            debugInfo.emissiveSampleInvalidCount,
            debugInfo.emissiveZeroTargetPdfCount,
            0u,
            0u);
    }
    if (band == 4u)
    {
        return RestirPTRrxDiRangeSurvivalColor(
            PATH_TRACE_UNIFIED_LIGHT_TYPE_DOOM_ANALYTIC,
            debugInfo.doomAnalyticCandidateCount,
            debugInfo.doomAnalyticStreamedCount,
            debugInfo.doomAnalyticLightInfoInvalidCount,
            debugInfo.doomAnalyticSampleInvalidCount,
            debugInfo.doomAnalyticZeroTargetPdfCount,
            debugInfo.doomAnalyticOutsideInfluenceCount,
            debugInfo.doomAnalyticNonFinitePayloadCount);
    }
    if (band == 5u)
    {
        return RestirPTRrxDiTypeMaskColor(
            debugInfo.emissiveLightInfoInvalidCount > 0u,
            debugInfo.doomAnalyticLightInfoInvalidCount > 0u);
    }
    if (band == 6u)
    {
        if (debugInfo.doomAnalyticNonFinitePayloadCount > 0u)
        {
            return float4(0.95, 0.35, 0.75, 1.0);
        }
        if (debugInfo.doomAnalyticOutsideInfluenceCount > 0u)
        {
            return float4(0.06, 0.22, 0.80, 1.0);
        }
        return RestirPTRrxDiTypeMaskColor(
            debugInfo.emissiveSampleInvalidCount > 0u,
            debugInfo.doomAnalyticSampleInvalidCount > 0u);
    }
    return RestirPTRrxDiTypeMaskColor(
        debugInfo.emissiveZeroTargetPdfCount > 0u,
        debugInfo.doomAnalyticZeroTargetPdfCount > 0u);
}

float4 EvaluateRestirPTRrxDiView0ContributionView(RAB_Surface surface, uint2 pixel)
{
    const bool surfaceValid = RAB_IsSurfaceValid(surface);
    const bool surfaceSupportsLighting = RAB_SurfaceSupportsOpaqueDiffuseBrdf(surface);

    RTXDI_DIReservoir temporalReservoir;
    uint status;
    RestirPTRrxDiTemporalDebugInfo debugInfo;
    const bool temporalValid = RestirPTRrxDiTryGenerateTemporalReservoir(surface, pixel, temporalReservoir, status, debugInfo) &&
        RTXDI_IsValidDIReservoir(temporalReservoir);

    const uint2 dimensions = max(PathTraceFullOutputSize(), uint2(1u, 1u));
    const uint bandCount = 8u;
    const uint band = min((pixel.x * bandCount) / dimensions.x, bandCount - 1u);
    const uint bandWidth = max(dimensions.x / bandCount, 1u);
    const uint bandPixel = pixel.x % bandWidth;
    if (bandPixel < 4u || bandPixel >= bandWidth - 1u)
    {
        return float4(1.0, 1.0, 1.0, 1.0);
    }

    uint replayLightType;
    const uint replayStatus = temporalValid
        ? RestirPTRrxDiClassifyTemporalReplay(surface, temporalReservoir, replayLightType)
        : RESTIR_PT_RRX_DI_REPLAY_INVALID_RESERVOIR;
    float3 contribution = float3(0.0, 0.0, 0.0);
    const bool contributionValid = RestirPTRrxDiTryEvaluateTemporalContribution(surface, pixel, contribution);

    float3 reflected = float3(0.0, 0.0, 0.0);
    float3 rrxContribution = float3(0.0, 0.0, 0.0);
    float3 spatialContribution = float3(0.0, 0.0, 0.0);
    float3 giContribution = float3(0.0, 0.0, 0.0);
    float3 lightingRadiance = float3(0.0, 0.0, 0.0);
    float3 view0Preview = float3(0.0, 0.0, 0.0);

    if (temporalValid)
    {
        const uint lightIndex = RTXDI_GetDIReservoirLightIndex(temporalReservoir);
        const RAB_LightInfo lightInfo = RAB_LoadActiveRrxLightInfo(lightIndex, false);
        if (RAB_IsLightInfoValid(lightInfo))
        {
            const RAB_LightSample lightSample = RAB_SampleActiveRrxPolymorphicLight(
                lightInfo,
                surface,
                RTXDI_GetDIReservoirSampleUV(temporalReservoir));
            if (lightSample.valid != 0u && lightSample.solidAnglePdf > 1.0e-6)
            {
                reflected = RAB_GetReflectedBsdfRadianceForSurface(lightSample.position, lightSample.radiance, surface);
            }
        }
    }

    const float3 surfaceBase = RestirPTVisibleSurfaceBase(surface);
    const float3 emissiveRadiance = RestirPTSanitizeHdrRadiance(surfaceValid ? surface.material.emissiveRadiance : float3(0.0, 0.0, 0.0));
    const bool rrxDiValid = surfaceSupportsLighting && RestirPTRrxDiTryEvaluateTemporalContribution(surface, pixel, rrxContribution);
    const bool spatialValid = surfaceSupportsLighting && !rrxDiValid && RestirPTTryEvaluateSpatialDirectLighting(surface, pixel, spatialContribution);
    const bool directValid = rrxDiValid || spatialValid;

    const RTXDI_PTReservoir giReservoir = LoadRestirPTInitialReservoir(pixel);
    const bool giValid = surfaceSupportsLighting && RestirPTReservoirHasUsefulSample(giReservoir);
    const float giVisibility = (RestirPTInfo.z >= 0.5 && giValid) ? RestirPTTraceReservoirVisibility(surface, giReservoir) : 1.0;
    const bool giVisible = giValid && giVisibility > 0.0;
    giContribution = giVisible ? RestirPTReservoirPreviewContribution(giReservoir) * giVisibility : float3(0.0, 0.0, 0.0);
    lightingRadiance =
        (rrxDiValid ? rrxContribution : (spatialValid ? spatialContribution : float3(0.0, 0.0, 0.0))) +
        (giVisible ? giContribution : float3(0.0, 0.0, 0.0));
    view0Preview = surfaceSupportsLighting
        ? ((!directValid && !giVisible) ? surfaceBase : saturate(surfaceBase + RestirPTToneMapPreview(lightingRadiance)))
        : surfaceBase;

    if (pixel.y < 24u)
    {
        if (band == 0u) return float4(0.05, 0.75, 0.12, 1.0);
        if (band == 1u) return rrxDiValid ? RestirPTRrxDiLightTypeColor(replayLightType) : (spatialValid ? float4(1.0, 0.85, 0.04, 1.0) : float4(0.95, 0.05, 0.02, 1.0));
        if (band == 2u) return RestirPTRrxDiReplayStatusColor(replayStatus, replayLightType);
        if (band == 3u) return float4(0.95, 0.35, 0.75, 1.0);
        if (band == 4u) return float4(1.0, 0.85, 0.04, 1.0);
        if (band == 5u) return giVisible ? float4(0.05, 0.18, 0.95, 1.0) : float4(0.0, 0.0, 0.22, 1.0);
        if (band == 6u) return float4(0.05, 0.80, 0.95, 1.0);
        return float4(0.20, 0.95, 0.20, 1.0);
    }

    if (band == 0u)
    {
        if (!surfaceValid)
        {
            return float4(0.32, 0.0, 0.0, 1.0);
        }
        return surfaceSupportsLighting
            ? float4(0.05, 0.75, 0.12, 1.0)
            : float4(0.35, 0.0, 0.45, 1.0);
    }
    if (band == 1u)
    {
        if (rrxDiValid)
        {
            return RestirPTRrxDiLightTypeColor(replayLightType);
        }
        if (spatialValid)
        {
            return float4(1.0, 0.85, 0.04, 1.0);
        }
        return temporalValid && contributionValid
            ? float4(0.05, 0.75, 0.12, 1.0)
            : RestirPTRrxDiTemporalStatusColor(status, temporalReservoir);
    }
    if (band == 2u)
    {
        return RestirPTRrxDiReplayStatusColor(replayStatus, replayLightType);
    }
    if (band == 3u)
    {
        return rrxDiValid
            ? float4(RestirPTToneMapPreview(rrxContribution), 1.0)
            : float4(0.22, 0.0, 0.0, 1.0);
    }
    if (band == 4u)
    {
        return spatialValid
            ? float4(RestirPTToneMapPreview(spatialContribution), 1.0)
            : float4(0.18, 0.12, 0.0, 1.0);
    }
    if (band == 5u)
    {
        return giVisible
            ? float4(RestirPTToneMapPreview(giContribution), 1.0)
            : (giValid ? float4(0.10, 0.10, 0.45, 1.0) : float4(0.0, 0.0, 0.22, 1.0));
    }
    if (band == 6u)
    {
        return float4(RestirPTToneMapPreview(reflected), 1.0);
    }

    return float4(view0Preview, 1.0);
}

float4 EvaluateRestirPTRrxDiTemporalLocalInvalidationView(RAB_Surface surface, uint2 pixel)
{
    if (pixel.x < 64u && pixel.y < 64u)
    {
        return float4(1.0, 0.0, 1.0, 1.0);
    }
    if (RestirPTRrxDebugBypassFlags() != 0u && pixel.y < 96u)
    {
        const uint2 dimensions = max(PathTraceFullOutputSize(), uint2(1u, 1u));
        const uint bandCount = 6u;
        const uint band = min((pixel.x * bandCount) / dimensions.x, bandCount - 1u);
        const uint flag =
            band == 0u ? RESTIR_PT_RRX_DEBUG_BYPASS_MOTION :
            band == 1u ? RESTIR_PT_RRX_DEBUG_BYPASS_DEPTH :
            band == 2u ? RESTIR_PT_RRX_DEBUG_BYPASS_NORMAL :
            band == 3u ? RESTIR_PT_RRX_DEBUG_BYPASS_SURFACE_SIMILARITY :
            band == 4u ? RESTIR_PT_RRX_DEBUG_BYPASS_RESET_MASK :
            RESTIR_PT_RRX_DEBUG_BYPASS_PORTAL;
        return RestirPTRrxDebugBypassEnabled(flag)
            ? float4(0.0, 1.0, 0.0, 1.0)
            : float4(0.35, 0.0, 0.0, 1.0);
    }

    RTXDI_DIReservoir temporalReservoir;
    uint status;
    RestirPTRrxDiTemporalDebugInfo debugInfo;
    RestirPTRrxDiTryGenerateTemporalReservoir(surface, pixel, temporalReservoir, status, debugInfo);

    const uint2 dimensions = max(PathTraceFullOutputSize(), uint2(1u, 1u));
    if (pixel.x >= dimensions.x / 2u)
    {
        return RestirPTRrxDiTemporalOutputColor(pixel, temporalReservoir);
    }
    return RestirPTRrxDiTemporalCauseColor(pixel, status, temporalReservoir, debugInfo);
}

float4 EvaluateRestirPTRrxDiTemporalRawTupleView(RAB_Surface surface, uint2 pixel)
{
    RTXDI_DIReservoir temporalReservoir;
    uint status;
    RestirPTRrxDiTemporalDebugInfo debugInfo;
    RestirPTRrxDiTryGenerateTemporalReservoir(surface, pixel, temporalReservoir, status, debugInfo);
    return float4(
        (float)debugInfo.currentLightIndexEncoded,
        (float)debugInfo.currentToPreviousIndexEncoded,
        (float)debugInfo.previousLightIndexEncoded,
        (float)debugInfo.previousToCurrentIndexEncoded);
}

float4 RestirPTRrxTemporalInputBoolColor(bool value, bool applicable)
{
    if (!applicable)
    {
        return float4(0.04, 0.08, 0.18, 1.0);
    }
    return value ? float4(0.02, 0.85, 0.10, 1.0) : float4(0.95, 0.18, 0.02, 1.0);
}

float4 RestirPTRrxTemporalInputMotionSourceColor(uint motionMask)
{
    if ((motionMask & PT_MOTION_VECTOR_MASK_VALID) == 0u)
    {
        return float4(0.95, 0.18, 0.02, 1.0);
    }

    const uint sourceKind = (motionMask & PT_MOTION_VECTOR_MASK_SOURCE_MASK) >> PT_MOTION_VECTOR_MASK_SOURCE_SHIFT;
    if (sourceKind == PT_MOTION_VECTOR_SOURCE_STATIC)
    {
        return float4(0.02, 0.85, 0.10, 1.0);
    }
    if (sourceKind == PT_MOTION_VECTOR_SOURCE_SKINNED)
    {
        return float4(0.10, 0.30, 0.95, 1.0);
    }
    if (sourceKind == PT_MOTION_VECTOR_SOURCE_RIGID)
    {
        return float4(0.00, 0.85, 0.95, 1.0);
    }
    return float4(0.95, 0.85, 0.05, 1.0);
}

float4 RestirPTRrxTemporalInputMotionVectorColor(float2 motionPixels)
{
    const float maxVisualizedPixels = 48.0;
    const float magnitude = saturate(length(motionPixels) / maxVisualizedPixels);
    const float2 signedDirection = saturate(motionPixels / (maxVisualizedPixels * 2.0) + 0.5);
    return float4(signedDirection.x, signedDirection.y, magnitude, 1.0);
}

float4 RestirPTRrxTemporalInputResetMaskColor(uint resetMask)
{
    if ((resetMask & RESTIR_PT_TEMPORAL_UNSAFE_RESET_MASK) == 0u)
    {
        return float4(0.02, 0.85, 0.10, 1.0);
    }
    return float4(0.95, 0.55, 0.04, 1.0);
}

float4 RestirPTRrxTemporalInputDepthPassColor(bool pass, bool applicable, float expectedDepth, float previousDepth)
{
    if (!applicable)
    {
        return float4(0.04, 0.08, 0.18, 1.0);
    }
    const float threshold = max(RestirPTParams.temporalResampling.depthThreshold, 1.0e-6);
    const float relativeDelta = abs(expectedDepth - previousDepth) / max(max(expectedDepth, previousDepth) * threshold, 1.0e-6);
    const float heat = saturate(relativeDelta);
    return pass
        ? float4(0.02, 0.85 * (1.0 - 0.60 * heat), 0.10 + 0.45 * heat, 1.0)
        : float4(0.95, 0.18 + 0.45 * (1.0 - heat), 0.02, 1.0);
}

float4 RestirPTRrxTemporalInputNormalPassColor(bool pass, bool applicable, float normalDot)
{
    if (!applicable)
    {
        return float4(0.04, 0.08, 0.18, 1.0);
    }
    const float heat = saturate((normalDot + 1.0) * 0.5);
    return pass
        ? float4(0.02, 0.30 + 0.55 * heat, 0.10, 1.0)
        : float4(0.95, 0.18 * heat, 0.02, 1.0);
}

uint RestirPTRrxMotionVectorSourceKindFromSurface(RAB_Surface surface)
{
    if (!RAB_IsSurfaceValid(surface))
    {
        return 0u;
    }
    if (surface.instanceId == 0u && surface.surfaceClass == 0u)
    {
        return PT_MOTION_VECTOR_SOURCE_STATIC;
    }
    if (surface.surfaceClass == RT_SMOKE_SURFACE_CLASS_SKINNED_DEFORMED)
    {
        return PT_MOTION_VECTOR_SOURCE_SKINNED;
    }
    if (surface.surfaceClass == RT_SMOKE_SURFACE_CLASS_RIGID_ENTITY && surface.instanceId >= 2u)
    {
        return PT_MOTION_VECTOR_SOURCE_RIGID;
    }
    return 4u;
}

bool RestirPTRrxTryLoadCurrentPrimarySurfaceRecord(uint2 pixel, out PathTracePrimarySurfaceRecord record)
{
    record = (PathTracePrimarySurfaceRecord)0;
    const int2 loadPixel = PathTracePrimarySurfaceLoadPixel(int2(pixel), false);
    if (loadPixel.x < 0 || loadPixel.y < 0)
    {
        return false;
    }

    const uint2 dimensions = PathTraceFullOutputSize();
    if ((uint)loadPixel.x >= dimensions.x || (uint)loadPixel.y >= dimensions.y)
    {
        return false;
    }

    const uint index = (uint)loadPixel.y * dimensions.x + (uint)loadPixel.x;
    if (index >= PathTracePrimarySurfaceHistoryCount())
    {
        return false;
    }

    record = PrimarySurfaceHistoryCurrent[index];
    return true;
}

bool RestirPTRrxTryPackedObjectExpectedMotion(RAB_Surface surface, uint2 pixel, out float3 expectedMotion, out uint debugStatus)
{
    expectedMotion = float3(0.0, 0.0, 0.0);
    debugStatus = RT_PRIMARY_SURFACE_DEBUG_OK;
    if (!RAB_IsSurfaceValid(surface))
    {
        debugStatus = RT_PRIMARY_SURFACE_DEBUG_MISSING_CURRENT;
        return false;
    }

    PathTracePrimarySurfaceRecord record;
    if (!RestirPTRrxTryLoadCurrentPrimarySurfaceRecord(pixel, record))
    {
        debugStatus = RT_PRIMARY_SURFACE_DEBUG_MISSING_CURRENT;
        return false;
    }

    if (!PathTracePrimarySurfaceRecordHasObjectMotion(record))
    {
        debugStatus = PathTracePrimarySurfaceMissingPackedObjectMotionStatus(surface, record.header.z);
        return false;
    }

    float2 previousPixelFloat;
    float previousLinearDepth;
    if (!ProjectPathTracePrimarySurfaceToPreviousPixelFloatAndDepthWithStatus(record.previousPositionOrMotion.xyz, PathTraceFullOutputSize(), previousPixelFloat, previousLinearDepth, debugStatus))
    {
        return false;
    }

    expectedMotion.xy = previousPixelFloat - (float2(pixel) + 0.5);
    expectedMotion.z = previousLinearDepth - RAB_GetSurfaceLinearDepth(surface);
    return true;
}

float4 RestirPTRrxTemporalInputCombinedExportSourceColor(uint2 pixel)
{
    const uint motionMask = PathTraceMotionVectorMask[pixel];
    return RestirPTRrxTemporalInputMotionSourceColor(motionMask);
}

float4 RestirPTRrxTemporalInputCombinedExportMotionColor(uint2 pixel)
{
    const uint motionMask = PathTraceMotionVectorMask[pixel];
    if ((motionMask & PT_MOTION_VECTOR_MASK_VALID) == 0u)
    {
        return float4(0.95, 0.18, 0.02, 1.0);
    }

    const float3 exportedMotion = PathTraceMotionVectors[pixel].xyz;
    return RestirPTRrxTemporalInputMotionVectorColor(exportedMotion.xy);
}

uint RestirPTRrxTemporalInputMotionMaskFromStatus(bool valid, uint sourceKind, uint debugStatus)
{
    const uint sourceBits = (sourceKind & 0x0fu) << PT_MOTION_VECTOR_MASK_SOURCE_SHIFT;
    if (valid)
    {
        return PT_MOTION_VECTOR_MASK_VALID | sourceBits;
    }
    return sourceBits | ((debugStatus & 0xffu) << 5u);
}

float4 RestirPTRrxTemporalInputCombinedHelperSourceColor(RAB_Surface surface, uint2 pixel)
{
    float3 expectedMotion;
    uint debugStatus;
    const uint sourceKind = RestirPTRrxMotionVectorSourceKindFromSurface(surface);
    const bool projected = RestirPTRrxTryPackedObjectExpectedMotion(surface, pixel, expectedMotion, debugStatus);
    return RestirPTRrxTemporalInputMotionSourceColor(RestirPTRrxTemporalInputMotionMaskFromStatus(projected, sourceKind, debugStatus));
}

float4 RestirPTRrxTemporalInputCombinedHelperMotionColor(RAB_Surface surface, uint2 pixel)
{
    float3 expectedMotion;
    uint debugStatus;
    if (!RestirPTRrxTryPackedObjectExpectedMotion(surface, pixel, expectedMotion, debugStatus))
    {
        return float4(0.95, 0.18, 0.02, 1.0);
    }
    return RestirPTRrxTemporalInputMotionVectorColor(expectedMotion.xy);
}

float4 RestirPTRrxTemporalInputCurrentRecordSourceColor(RAB_Surface surface)
{
    if (!RAB_IsSurfaceValid(surface))
    {
        return float4(0.0, 0.0, 0.0, 1.0);
    }

    const uint sourceKind = RestirPTRrxMotionVectorSourceKindFromSurface(surface);
    return RestirPTRrxTemporalInputMotionSourceColor(RestirPTRrxTemporalInputMotionMaskFromStatus(true, sourceKind, RT_PRIMARY_SURFACE_DEBUG_OK));
}

float4 RestirPTRrxTemporalInputCurrentRecordStatusColor(RAB_Surface surface, uint2 pixel)
{
    PathTracePrimarySurfaceRecord record;
    if (!RestirPTRrxTryLoadCurrentPrimarySurfaceRecord(pixel, record))
    {
        return PathTracePrimarySurfaceDebugColor(RT_PRIMARY_SURFACE_DEBUG_MISSING_CURRENT, surface);
    }
    return PathTracePrimarySurfaceDebugColor(record.header.z, surface);
}

float4 EvaluateRestirPTRrxDiTemporalInputEvidenceView(RAB_Surface surface, uint2 pixel)
{
    const uint2 dimensions = max(PathTraceFullOutputSize(), uint2(1u, 1u));
    const uint bandCount = 14u;
    const uint bandWidth = max(dimensions.x / bandCount, 1u);
    const uint bandPixel = pixel.x % bandWidth;
    const uint band = min((pixel.x * bandCount) / dimensions.x, bandCount - 1u);
    if (bandPixel < 8u || bandPixel >= bandWidth - 2u)
    {
        return float4(1.0, 1.0, 1.0, 1.0);
    }
    if (pixel.y < 24u)
    {
        if (band == 0u) return float4(0.10, 0.95, 0.10, 1.0);
        if (band == 1u) return float4(0.45, 0.45, 0.95, 1.0);
        if (band == 2u) return float4(0.95, 0.65, 0.05, 1.0);
        if (band == 3u) return float4(0.10, 0.85, 0.95, 1.0);
        if (band == 4u) return float4(0.95, 0.95, 0.10, 1.0);
        if (band == 5u) return float4(0.95, 0.40, 0.10, 1.0);
        if (band == 6u) return float4(0.10, 0.95, 0.65, 1.0);
        if (band == 7u) return float4(0.80, 0.30, 0.95, 1.0);
        if (band == 8u) return float4(0.20, 0.55, 0.95, 1.0);
        if (band == 9u) return float4(0.95, 0.35, 0.75, 1.0);
        if (band == 10u) return float4(0.50, 0.95, 0.20, 1.0);
        if (band == 11u) return float4(0.95, 0.90, 0.20, 1.0);
        if (band == 12u) return float4(0.00, 0.85, 0.95, 1.0);
        return float4(0.62, 0.48, 0.02, 1.0);
    }

    if (!RAB_IsSurfaceValid(surface))
    {
        return float4(0.0, 0.0, 0.0, 1.0);
    }

    const uint motionMask = PathTraceMotionVectorMask[pixel];
    const bool motionValid = (motionMask & PT_MOTION_VECTOR_MASK_VALID) != 0u;
    const float3 motion = motionValid ? PathTraceMotionVectors[pixel].xyz : float3(0.0, 0.0, 0.0);
    const int2 previousPixel = int2(round(float2(pixel) + motion.xy));
    const bool previousPixelInBounds = motionValid &&
        previousPixel.x >= 0 && previousPixel.y >= 0 &&
        (uint)previousPixel.x < dimensions.x && (uint)previousPixel.y < dimensions.y;
    RAB_Surface previousSurface = RAB_EmptySurface();
    if (previousPixelInBounds)
    {
        previousSurface = LoadPathTracePrimarySurfaceRecord(previousPixel, true);
    }
    const bool previousSurfaceValid = RAB_IsSurfaceValid(previousSurface);
    const uint resetMask = PathTraceRRGuideResetMask[pixel];
    const float legacyZeroZExpectedDepth = RAB_GetSurfaceLinearDepth(surface);
    const float activeExpectedDepth = RAB_GetSurfaceLinearDepth(surface) + motion.z;
    const float previousDepth = RAB_GetSurfaceLinearDepth(previousSurface);
    const bool legacyZeroZDepthPass = previousSurfaceValid &&
        RTXDI_CompareRelativeDifference(legacyZeroZExpectedDepth, previousDepth, RestirPTParams.temporalResampling.depthThreshold);
    const bool activeDepthPass = previousSurfaceValid &&
        RTXDI_CompareRelativeDifference(activeExpectedDepth, previousDepth, RestirPTParams.temporalResampling.depthThreshold);
    const float3 currentNormal = RAB_SafeNormalize(RAB_GetSurfaceNormal(surface), RAB_GetSurfaceGeoNormal(surface));
    const float3 previousNormal = previousSurfaceValid
        ? RAB_SafeNormalize(RAB_GetSurfaceNormal(previousSurface), RAB_GetSurfaceGeoNormal(previousSurface))
        : float3(0.0, 0.0, 1.0);
    const float normalDot = previousSurfaceValid ? dot(currentNormal, previousNormal) : 0.0;
    const bool normalPass = previousSurfaceValid && normalDot >= RestirPTParams.temporalResampling.normalThreshold;

    RTXDI_DIReservoir temporalReservoir;
    uint status;
    RestirPTRrxDiTemporalDebugInfo debugInfo;
    RestirPTRrxDiTryGenerateTemporalReservoir(surface, pixel, temporalReservoir, status, debugInfo);

    if (band == 0u)
    {
        return RestirPTRrxTemporalInputCombinedHelperSourceColor(surface, pixel);
    }
    if (band == 1u)
    {
        return RestirPTRrxTemporalInputCombinedHelperMotionColor(surface, pixel);
    }
    if (band == 2u)
    {
        return RestirPTRrxTemporalInputResetMaskColor(resetMask);
    }
    if (band == 3u)
    {
        return RestirPTRrxTemporalInputBoolColor(previousSurfaceValid, previousPixelInBounds);
    }
    if (band == 4u)
    {
        return RestirPTRrxTemporalInputDepthPassColor(legacyZeroZDepthPass, previousSurfaceValid, legacyZeroZExpectedDepth, previousDepth);
    }
    if (band == 5u)
    {
        return RestirPTRrxTemporalInputDepthPassColor(activeDepthPass, previousSurfaceValid, activeExpectedDepth, previousDepth);
    }
    if (band == 6u)
    {
        return RestirPTRrxTemporalInputNormalPassColor(normalPass, previousSurfaceValid, normalDot);
    }
    if (band == 7u)
    {
        return RestirPTRrxTemporalInputBoolColor(debugInfo.previousSampleUsed != 0u, debugInfo.projected != 0u);
    }
    if (band == 8u)
    {
        return RestirPTRrxTemporalInputBoolColor(debugInfo.outputValid != 0u, true);
    }
    if (band == 9u)
    {
        if (debugInfo.outputValid == 0u)
        {
            return float4(0.04, 0.08, 0.18, 1.0);
        }
        return RestirPTRrxHashColor(RTXDI_GetDIReservoirLightIndex(temporalReservoir) + 1u);
    }
    if (band == 10u)
    {
        return RestirPTRrxTemporalInputCombinedExportSourceColor(pixel);
    }
    if (band == 11u)
    {
        return RestirPTRrxTemporalInputCombinedExportMotionColor(pixel);
    }
    if (band == 12u)
    {
        return RestirPTRrxTemporalInputCurrentRecordSourceColor(surface);
    }
    if (band == 13u)
    {
        return RestirPTRrxTemporalInputCurrentRecordStatusColor(surface, pixel);
    }
    return float4(0.0, 0.0, 0.0, 1.0);
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
        if (!RestirPTRrxPrimarySurfacesAreSimilar(surface, neighborSurface, similarityStatus))
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
    if (view == 68u)
    {
        return EvaluateRestirPTRrxDiTemporalLocalInvalidationView(surface, pixel);
    }
    if (view == 69u)
    {
        return EvaluateRestirPTRrxDiTemporalRawTupleView(surface, pixel);
    }
    if (view == 70u)
    {
        return EvaluateRestirPTRrxDiTemporalInputEvidenceView(surface, pixel);
    }
    if (view == 61u)
    {
        return EvaluateRestirPTRrxDiInitialSurvivalView(surface, pixel);
    }
    if (view == 62u)
    {
        return EvaluateRestirPTRrxDiView0ContributionView(surface, pixel);
    }
    if (view == 72u)
    {
        return EvaluateRestirPTRrxDiFinalConsumerView(surface, pixel);
    }
    if (view == 73u)
    {
        return EvaluateRestirPTRrxDiFinalConsumerClassifierView(surface, pixel);
    }
    if (view == 74u)
    {
        return EvaluateRestirPTRrxDiSelectedLightIdentityView(surface, pixel);
    }
    if (view == 75u)
    {
        return EvaluateRestirPTRrxDiWeightAuditView(surface, pixel);
    }
    if (view == 76u)
    {
        return EvaluateRestirPTRrxDiSelectedWeightAuditView(surface, pixel);
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
