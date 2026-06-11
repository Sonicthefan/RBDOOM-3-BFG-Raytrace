// Purpose:
//     Supplies rbdoom data for ReSTIR PT temporal/page-flow/remap diagnostic views.
//
// NVIDIA reference:
//     E:\prog\references\RTXDI-main\Libraries\Rtxdi\Include\Rtxdi\PT\TemporalResampling.hlsli
//     E:\prog\references\RTXDI-main\Samples\FullSample\Shaders\LightingPasses\PT\TemporalResampling.hlsl
//     E:\prog\references\RTXDI-main\Libraries\Rtxdi\Include\Rtxdi\PT\HybridShift.hlsli
//
// Current rbdoom supplier:
//     Primary-surface history, motion-vector projection, temporal reservoir pages,
//     smoke emissive remap tables, and Doom analytic light remap tables.
//
// Current deviation:
//     These helpers diagnose rbdoom's temporal inputs and remap state. They do
//     not run NVIDIA temporal resampling or repair reservoir reuse.
//
// Extraction rule:
//     This file must preserve existing behavior until a later correction task.

#ifndef RB_PATH_TRACING_RESTIR_REFERENCE_TEMPORAL_DEBUG_HLSLI
#define RB_PATH_TRACING_RESTIR_REFERENCE_TEMPORAL_DEBUG_HLSLI

float4 RestirPTReferenceResetReasonColor(uint resetMask)
{
    if ((resetMask & RT_RR_RESET_STOCHASTIC_TRANSLUCENT) != 0u)
    {
        return float4(0.75, 0.15, 0.85, 1.0);
    }
    if ((resetMask & RT_RR_RESET_MATERIAL_MISMATCH) != 0u)
    {
        return float4(1.00, 0.60, 0.05, 1.0);
    }
    if ((resetMask & RT_RR_RESET_REJECTED_PREVIOUS) != 0u)
    {
        return float4(0.95, 0.05, 0.05, 1.0);
    }
    if ((resetMask & RT_RR_RESET_MISSING_PREVIOUS) != 0u)
    {
        return float4(0.60, 0.10, 0.10, 1.0);
    }
    if ((resetMask & RT_RR_RESET_OBJECT_MOTION_UNAVAILABLE) != 0u)
    {
        return float4(0.80, 0.80, 0.15, 1.0);
    }
    if ((resetMask & RT_RR_RESET_INVALID_SURFACE) != 0u)
    {
        return float4(0.02, 0.02, 0.02, 1.0);
    }
    return float4(0.80, 0.30, 0.10, 1.0);
}

float4 EvaluateRestirPTReferenceTemporalReadinessView(RAB_Surface surface, uint2 pixel)
{
    if (!RAB_IsSurfaceValid(surface) || !RAB_SurfaceSupportsOpaqueDiffuseBrdf(surface))
    {
        return PathTracePrimarySurfaceDebugColor(RT_PRIMARY_SURFACE_DEBUG_MISSING_CURRENT, surface);
    }

    const uint resetMask = PathTraceRRGuideResetMask[pixel];
    if ((resetMask & RESTIR_PT_TEMPORAL_UNSAFE_RESET_MASK) != 0u)
    {
        return RestirPTReferenceResetReasonColor(resetMask);
    }

    int2 previousPixel;
    float2 motionPixels;
    if (!RestirPTTryProjectMotionVectorToPreviousPixel(pixel, previousPixel, motionPixels))
    {
        return float4(0.95, 0.05, 0.05, 1.0);
    }

    const RAB_Surface previousSurface = LoadPathTracePrimarySurfaceRecord(previousPixel, true);
    uint similarityStatus;
    if (!PathTracePrimarySurfacesAreSimilar(surface, previousSurface, similarityStatus))
    {
        return PathTracePrimarySurfaceDebugColor(similarityStatus, surface);
    }

    const uint2 reservoirPixel = PathTraceFullPixelToRestirDirectPixel(pixel);
    const RTXDI_PTReservoir initialReservoir = LoadRestirPTInitialDirectReservoir(reservoirPixel);
    const RTXDI_PTReservoir temporalReservoir = LoadRestirPTTemporalOutputReservoir(reservoirPixel);
    if (!RTXDI_IsValidPTReservoir(temporalReservoir))
    {
        return float4(0.05, 0.12, 0.55, 1.0);
    }

    const float initialM = RTXDI_IsValidPTReservoir(initialReservoir) ? (float)initialReservoir.M : 0.0;
    const float temporalM = (float)temporalReservoir.M;
    const float history = saturate(temporalM / max((float)RestirPTParamsFlat.temporalResampling_maxHistoryLength, 1.0));
    if (temporalM <= initialM + 0.5)
    {
        return float4(0.05, 0.26 + 0.35 * history, 0.34 + 0.35 * history, 1.0);
    }

    return float4(0.02, 0.34 + 0.45 * history, 0.08 + 0.70 * history, 1.0);
}

float4 EvaluateRestirPTReferenceTemporalPageFlowView(uint2 pixel)
{
    const uint2 reservoirPixel = PathTraceFullPixelToRestirDirectPixel(pixel);
    const RTXDI_PTReservoir initialReservoir = LoadRestirPTInitialDirectReservoir(reservoirPixel);
    const RTXDI_PTReservoir temporalInputReservoir = LoadRestirPTTemporalInputReservoir(reservoirPixel);
    const RTXDI_PTReservoir temporalOutputReservoir = LoadRestirPTTemporalOutputReservoir(reservoirPixel);

    const bool initialValid = RTXDI_IsValidPTReservoir(initialReservoir);
    const bool temporalInputValid = RTXDI_IsValidPTReservoir(temporalInputReservoir);
    const bool temporalOutputValid = RTXDI_IsValidPTReservoir(temporalOutputReservoir);
    if (!initialValid)
    {
        return float4(0.55, 0.05, 0.04, 1.0);
    }
    if (!temporalInputValid)
    {
        return float4(0.05, 0.12, 0.75, 1.0);
    }
    if (!temporalOutputValid)
    {
        return float4(0.62, 0.08, 0.65, 1.0);
    }

    const float inputM = (float)temporalInputReservoir.M;
    const float initialM = (float)initialReservoir.M;
    const float outputM = (float)temporalOutputReservoir.M;
    const float history = saturate(outputM / max((float)RestirPTParamsFlat.temporalResampling_maxHistoryLength, 1.0));
    if (outputM <= initialM + 0.5)
    {
        return float4(0.05, 0.65, 0.65, 1.0);
    }
    if (outputM <= max(inputM, initialM) + 0.5)
    {
        return float4(0.75, 0.45, 0.95, 1.0);
    }
    return float4(0.02, 0.30 + 0.45 * history, 0.08 + 0.75 * history, 1.0);
}

int2 RestirPTReferenceTemporalOffset(int sampleIdx, int radius)
{
    sampleIdx &= 7;
    if (sampleIdx == 0) return int2(-radius, -radius);
    if (sampleIdx == 1) return int2( radius,  radius);
    if (sampleIdx == 2) return int2(-radius,  radius);
    if (sampleIdx == 3) return int2( radius, -radius);
    if (sampleIdx == 4) return int2(-radius,  0);
    if (sampleIdx == 5) return int2( radius,  0);
    if (sampleIdx == 6) return int2( 0, -radius);
    return int2(0, radius);
}

bool RestirPTReferenceSurfaceMaterialSimilar(RAB_Surface surface, RAB_Surface temporalSurface)
{
    return surface.materialId == temporalSurface.materialId &&
        surface.materialIndex == temporalSurface.materialIndex &&
        surface.surfaceClass == temporalSurface.surfaceClass &&
        abs(GetRoughness(surface.material) - GetRoughness(temporalSurface.material)) <= 0.20;
}

uint RestirPTReferenceSurfaceMaterialMismatchReason(RAB_Surface surface, RAB_Surface temporalSurface)
{
    if (RAB_AreMaterialsSimilar(RAB_GetMaterial(surface), RAB_GetMaterial(temporalSurface)))
    {
        return 0u;
    }

    const uint materialSimilarityMode = RAB_MaterialSimilarityMode();
    if (materialSimilarityMode != 0u)
    {
        const uint rtxdiReason = RAB_MaterialSimilarityFailureReasonRTXDI(
            RAB_GetMaterial(surface), RAB_GetMaterial(temporalSurface));
        if (rtxdiReason == 1u)
        {
            return 28u;
        }
        if (rtxdiReason == 2u)
        {
            return 29u;
        }
        if (rtxdiReason == 3u)
        {
            return 30u;
        }
        return 26u;
    }

    if (surface.materialId != temporalSurface.materialId)
    {
        return 23u;
    }
    if (surface.materialIndex != temporalSurface.materialIndex)
    {
        return 24u;
    }
    if (surface.surfaceClass != temporalSurface.surfaceClass)
    {
        return 25u;
    }
    if (abs(GetRoughness(surface.material) - GetRoughness(temporalSurface.material)) > 0.20)
    {
        return 26u;
    }
    return 26u;
}

bool RestirPTReferenceReservoirConnectsToNeeLight(RTXDI_PTReservoir reservoir)
{
    return RTXDI_IsValidPTReservoir(reservoir) && RTXDI_ConnectsToNeeLight(reservoir);
}

bool RestirPTReferenceDuplicationMapRequested()
{
    return RestirPTParamsFlat.temporalResampling_duplicationBasedHistoryReduction != 0u &&
        RestirPTParamsFlat.temporalResampling_historyReductionStrength > 0.0;
}

bool RestirPTReferenceSmokeEmissiveIdentityNonZero(PathTraceSmokeEmissiveTriangle emissiveTriangle)
{
    return emissiveTriangle.identityHashLo != 0u || emissiveTriangle.identityHashHi != 0u;
}

bool RestirPTReferenceSmokeEmissiveRecordsCompatible(
    PathTraceSmokeEmissiveTriangle currentTriangle,
    PathTraceSmokeEmissiveTriangle previousTriangle)
{
    return currentTriangle.identityHashLo == previousTriangle.identityHashLo &&
        currentTriangle.identityHashHi == previousTriangle.identityHashHi &&
        currentTriangle.materialId == previousTriangle.materialId &&
        currentTriangle.universeMaterialIndex == previousTriangle.universeMaterialIndex &&
        currentTriangle.emissiveTextureIndex == previousTriangle.emissiveTextureIndex;
}

uint RestirPTReferencePreviousNeeLightRemapFailureReason(RTXDI_PTReservoir reservoir)
{
    if (!RestirPTReferenceReservoirConnectsToNeeLight(reservoir))
    {
        return 0u;
    }

    const RTXDI_SampledLightData sampledLightData = RTXDI_GetSampledLightData(reservoir);
    if (!RTXDI_SampledLightData_IsValidLightData(sampledLightData))
    {
        return 10u;
    }

    const uint lightIndex = RTXDI_SampledLightData_GetLightIndex(sampledLightData);
    if (RAB_RestirLightManagerRABEnabled())
    {
        if (lightIndex >= RAB_GetPreviousRestirLightManagerCount())
        {
            return 12u;
        }

        const int currentLightIndex = RAB_TranslateLightIndex(lightIndex, false);
        if (currentLightIndex < 0)
        {
            const RAB_LightInfo previousLightInfo = RAB_LoadLightInfo(lightIndex, true);
            if (previousLightInfo.lightType == RAB_LIGHT_TYPE_EMISSIVE_TRIANGLE)
            {
                return 31u;
            }
            if (previousLightInfo.lightType == RAB_LIGHT_TYPE_DOOM_ANALYTIC_SPHERE)
            {
                return 32u;
            }
            return 11u;
        }

        return (uint)currentLightIndex < RAB_GetCurrentRestirLightManagerCount() ? 0u : 15u;
    }

    if (RAB_UnifiedLightLoadEnabled())
    {
        if (lightIndex >= RAB_GetPreviousUnifiedLightCount())
        {
            return 12u;
        }

        const int currentUnifiedIndex = RAB_TranslateLightIndex(lightIndex, false);
        if (currentUnifiedIndex < 0)
        {
            const PathTraceUnifiedLightRecord previousUnifiedLight = PathTraceUnifiedPreviousLights[lightIndex];
            if (previousUnifiedLight.type == PATH_TRACE_UNIFIED_LIGHT_TYPE_EMISSIVE_TRIANGLE)
            {
                return 31u;
            }
            if (previousUnifiedLight.type == PATH_TRACE_UNIFIED_LIGHT_TYPE_DOOM_ANALYTIC)
            {
                return 32u;
            }
            return 11u;
        }

        return (uint)currentUnifiedIndex < RAB_GetCurrentUnifiedLightCount() ? 0u : 15u;
    }

    const uint previousEmissiveTriangleCount = RAB_GetPreviousEmissiveTriangleCount();
    const uint analyticCount = RAB_GetCurrentDoomAnalyticLightCount();
    const uint currentIdentityCount = (uint)max(DoomAnalyticLightRemapInfo.x, 0.0);
    const uint previousIdentityCount = (uint)max(DoomAnalyticLightRemapInfo.y, 0.0);
    const uint remapCount = (uint)max(DoomAnalyticLightRemapInfo.z, 0.0);

    if (lightIndex < previousEmissiveTriangleCount)
    {
        const PathTraceSmokeEmissiveTriangle previousTriangle = SmokePreviousEmissiveTriangles[lightIndex];
        if (!RestirPTReferenceSmokeEmissiveIdentityNonZero(previousTriangle))
        {
            return 17u;
        }

        const PathTraceEmissiveLightRemap remap = SmokeEmissiveRemap[lightIndex];
        if (!RAB_SmokeEmissiveRemapValid(remap) || remap.previousToCurrentIndex < 0)
        {
            if ((remap.flags & RAB_SMOKE_EMISSIVE_REMAP_PREVIOUS_DUPLICATE) != 0u)
            {
                return 18u;
            }
            if ((remap.flags & RAB_SMOKE_EMISSIVE_REMAP_CURRENT_DUPLICATE) != 0u)
            {
                return 19u;
            }
            if ((remap.flags & RAB_SMOKE_EMISSIVE_REMAP_CURRENT_MISSING) != 0u)
            {
                return 20u;
            }
            if ((remap.flags & RAB_SMOKE_EMISSIVE_REMAP_INCOMPATIBLE) != 0u)
            {
                return 21u;
            }
            return 11u;
        }

        const uint currentEmissiveIndex = (uint)remap.previousToCurrentIndex;
        if (currentEmissiveIndex >= RAB_GetCurrentEmissiveTriangleCount())
        {
            return 22u;
        }

        const PathTraceSmokeEmissiveTriangle currentTriangle = SmokeEmissiveTriangles[currentEmissiveIndex];
        return RestirPTReferenceSmokeEmissiveRecordsCompatible(currentTriangle, previousTriangle) ? 0u : 21u;
    }

    const uint previousAnalyticIndex = lightIndex - previousEmissiveTriangleCount;
    if (previousAnalyticIndex >= previousIdentityCount)
    {
        return 12u;
    }

    const PathTraceDoomAnalyticLightCandidateIdentity previousIdentity = DoomAnalyticPreviousIdentities[previousAnalyticIndex];
    const uint remapIndex = RAB_DoomAnalyticIdentityRemapIndex(previousIdentity);
    if (!RAB_DoomAnalyticIdentityValid(previousIdentity) || remapIndex >= remapCount)
    {
        return 13u;
    }

    const PathTraceDoomAnalyticLightRemap remap = DoomAnalyticRemap[remapIndex];
    if (!RAB_DoomAnalyticRemapValid(remap) || remap.previousToCurrentCandidateIndex < 0)
    {
        return 14u;
    }

    const uint currentAnalyticIndex = (uint)remap.previousToCurrentCandidateIndex;
    if (currentAnalyticIndex >= analyticCount || currentAnalyticIndex >= currentIdentityCount)
    {
        return 15u;
    }
    if (!RAB_DoomAnalyticLightStateCompatible(DoomAnalyticLights[currentAnalyticIndex], DoomAnalyticPreviousLights[previousAnalyticIndex]))
    {
        return 16u;
    }

    return 0u;
}

RAB_Surface RestirPTReferenceLoadDirectDomainSurface(int2 directPixel, bool previousFrame)
{
    const uint2 directSize = max(PathTraceRestirDirectSize(), uint2(1u, 1u));
    if (directPixel.x < 0 || directPixel.y < 0 ||
        (uint)directPixel.x >= directSize.x || (uint)directPixel.y >= directSize.y)
    {
        return RAB_EmptySurface();
    }

    const uint2 representativeFullPixel = PathTraceRestirDirectPixelToRepresentativeFullPixel(uint2(directPixel));
    return RAB_GetGBufferSurface(int2(representativeFullPixel), previousFrame);
}

bool RestirPTReferencePreviousNeeReservoirFailsLightRemap(RTXDI_PTReservoir reservoir)
{
    return RestirPTReferencePreviousNeeLightRemapFailureReason(reservoir) != 0u;
}

uint RestirPTReferenceClassifyTemporalCandidate(
    RAB_Surface surface,
    int2 temporalSurfacePos,
    bool isFallbackSample,
    float expectedPrevLinearDepth,
    out RTXDI_PTReservoir previousReservoir)
{
    previousReservoir = RTXDI_EmptyPTReservoir();

    const uint2 dimensions = PathTraceRestirDirectSize();
    if (temporalSurfacePos.x < 0 || temporalSurfacePos.y < 0 ||
        (uint)temporalSurfacePos.x >= dimensions.x || (uint)temporalSurfacePos.y >= dimensions.y)
    {
        return 2u;
    }

    const RAB_Surface temporalSurface = RestirPTReferenceLoadDirectDomainSurface(temporalSurfacePos, true);
    if (!RAB_IsSurfaceValid(temporalSurface))
    {
        return 2u;
    }

    if (!isFallbackSample && !RTXDI_IsValidNeighbor(
        RAB_GetSurfaceNormal(surface), RAB_GetSurfaceNormal(temporalSurface),
        expectedPrevLinearDepth, RAB_GetSurfaceLinearDepth(temporalSurface),
        RestirPTParamsFlat.temporalResampling_normalThreshold,
        RestirPTParamsFlat.temporalResampling_depthThreshold))
    {
        return 3u;
    }

    const uint materialMismatchReason = RestirPTReferenceSurfaceMaterialMismatchReason(surface, temporalSurface);
    if (materialMismatchReason != 0u)
    {
        return materialMismatchReason;
    }

    previousReservoir = LoadRestirPTTemporalInputReservoir(uint2(temporalSurfacePos));
    if (!RTXDI_IsValidPTReservoir(previousReservoir))
    {
        return 5u;
    }

    const uint nextAge = previousReservoir.Age + 1u;
    if (nextAge > min(RTXDI_PTRESERVOIR_AGE_MAX, RestirPTParamsFlat.temporalResampling_maxReservoirAge))
    {
        return 6u;
    }

    return 7u;
}

float4 RestirPTReferenceTemporalNeighborColor(uint status, float history)
{
    if (status == 1u) return float4(0.90, 0.04, 0.04, 1.0);
    if (status == 2u) return float4(0.04, 0.08, 0.42, 1.0);
    if (status == 3u) return float4(0.58, 0.08, 0.78, 1.0);
    if (status == 4u) return float4(0.80, 0.60, 0.04, 1.0);
    if (status == 5u) return float4(0.06, 0.24, 0.95, 1.0);
    if (status == 6u) return float4(0.95, 0.36, 0.04, 1.0);
    if (status == 8u) return float4(0.05, 0.65, 0.65, 1.0);
    if (status == 9u) return float4(1.00, 0.55, 0.00, 1.0);
    if (status == 10u) return float4(1.00, 1.00, 1.00, 1.0);
    if (status == 11u) return float4(0.00, 1.00, 0.25, 1.0);
    if (status == 12u) return float4(0.25, 0.45, 1.00, 1.0);
    if (status == 13u) return float4(0.55, 0.25, 1.00, 1.0);
    if (status == 14u) return float4(1.00, 0.00, 1.00, 1.0);
    if (status == 15u) return float4(1.00, 0.35, 0.65, 1.0);
    if (status == 16u) return float4(0.45, 0.45, 0.45, 1.0);
    if (status == 17u) return float4(0.00, 0.45, 0.15, 1.0);
    if (status == 18u) return float4(0.20, 1.00, 0.10, 1.0);
    if (status == 19u) return float4(0.55, 1.00, 0.15, 1.0);
    if (status == 20u) return float4(0.00, 0.85, 0.65, 1.0);
    if (status == 21u) return float4(0.95, 0.95, 0.10, 1.0);
    if (status == 22u) return float4(0.00, 0.25, 0.18, 1.0);
    if (status == 23u) return float4(0.95, 0.75, 0.05, 1.0);
    if (status == 24u) return float4(0.95, 0.45, 0.05, 1.0);
    if (status == 25u) return float4(0.65, 0.55, 0.00, 1.0);
    if (status == 26u) return float4(0.95, 0.90, 0.45, 1.0);
    if (status == 27u) return float4(1.00, 0.12, 0.00, 1.0);
    if (status == 28u) return float4(1.00, 0.85, 0.15, 1.0);
    if (status == 29u) return float4(1.00, 0.15, 0.65, 1.0);
    if (status == 30u) return float4(0.35, 0.85, 1.00, 1.0);
    return float4(0.02, 0.30 + 0.45 * history, 0.08 + 0.75 * history, 1.0);
}

uint RestirPTReferenceTemporalNeighborDebugMode()
{
    return clamp((uint)max(RestirPTSurfaceInfo.y, 0.0), 0u, 2u);
}

float4 RestirPTReferenceTemporalReuseBucketColor(
    uint bestFailure,
    bool acceptedCandidate,
    bool duplicationMapRequested,
    bool outputGrewHistory,
    bool previousNeeReuseDisabledForAcceptedCandidate,
    uint acceptedRemapFailureReason,
    float history)
{
    if (!acceptedCandidate)
    {
        if (bestFailure == 5u || bestFailure == 6u)
        {
            return float4(0.05, 0.22, 0.95, 1.0);
        }
        return float4(0.95, 0.05, 0.05, 1.0);
    }

    if (duplicationMapRequested)
    {
        return float4(1.00, 0.12, 0.00, 1.0);
    }

    if (outputGrewHistory)
    {
        return float4(0.02, 0.30 + 0.45 * history, 0.08 + 0.75 * history, 1.0);
    }

    if (previousNeeReuseDisabledForAcceptedCandidate || acceptedRemapFailureReason != 0u)
    {
        return float4(0.95, 0.90, 0.05, 1.0);
    }

    return float4(0.05, 0.65, 0.65, 1.0);
}

float4 RestirPTReferenceTemporalNeeRemapBucketColor(
    uint bestFailure,
    bool acceptedCandidate,
    bool duplicationMapRequested,
    bool outputGrewHistory,
    bool previousNeeReuseDisabledForAcceptedCandidate,
    uint acceptedRemapFailureReason,
    float history)
{
    if (!acceptedCandidate)
    {
        if (bestFailure == 5u || bestFailure == 6u)
        {
            return float4(0.05, 0.22, 0.95, 1.0);
        }
        return float4(0.95, 0.05, 0.05, 1.0);
    }

    if (duplicationMapRequested)
    {
        return float4(1.00, 0.12, 0.00, 1.0);
    }

    if (previousNeeReuseDisabledForAcceptedCandidate)
    {
        return float4(1.00, 0.80, 0.05, 1.0);
    }

    if (acceptedRemapFailureReason == 10u)
    {
        return float4(1.00, 1.00, 1.00, 1.0);
    }

    if (acceptedRemapFailureReason == 11u)
    {
        return float4(1.00, 0.00, 1.00, 1.0);
    }

    if (acceptedRemapFailureReason == 12u)
    {
        return float4(0.25, 0.45, 1.00, 1.0);
    }

    if (acceptedRemapFailureReason == 13u)
    {
        return float4(0.55, 0.25, 1.00, 1.0);
    }

    if (acceptedRemapFailureReason == 14u)
    {
        return float4(0.85, 0.00, 0.55, 1.0);
    }

    if (acceptedRemapFailureReason == 15u)
    {
        return float4(1.00, 0.35, 0.65, 1.0);
    }

    if (acceptedRemapFailureReason == 16u)
    {
        return float4(0.45, 0.45, 0.45, 1.0);
    }

    if (acceptedRemapFailureReason == 17u)
    {
        return float4(0.00, 0.45, 0.15, 1.0);
    }

    if (acceptedRemapFailureReason == 18u)
    {
        return float4(0.20, 1.00, 0.10, 1.0);
    }

    if (acceptedRemapFailureReason == 19u)
    {
        return float4(0.55, 1.00, 0.15, 1.0);
    }

    if (acceptedRemapFailureReason == 20u)
    {
        return float4(0.00, 0.85, 0.65, 1.0);
    }

    if (acceptedRemapFailureReason == 21u)
    {
        return float4(0.95, 0.95, 0.10, 1.0);
    }

    if (acceptedRemapFailureReason == 22u)
    {
        return float4(0.00, 0.25, 0.18, 1.0);
    }

    if (acceptedRemapFailureReason == 31u)
    {
        return float4(1.00, 0.45, 0.00, 1.0);
    }

    if (acceptedRemapFailureReason == 32u)
    {
        return float4(1.00, 0.00, 1.00, 1.0);
    }

    if (acceptedRemapFailureReason != 0u)
    {
        return float4(0.95, 0.90, 0.05, 1.0);
    }

    if (outputGrewHistory)
    {
        return float4(0.02, 0.30 + 0.45 * history, 0.08 + 0.75 * history, 1.0);
    }

    return float4(0.05, 0.65, 0.65, 1.0);
}

float4 EvaluateRestirPTReferenceTemporalNeighborView(RAB_Surface surface, uint2 pixel)
{
    const uint2 reservoirPixel = PathTraceFullPixelToRestirDirectPixel(pixel);
    surface = RestirPTReferenceLoadDirectDomainSurface(int2(reservoirPixel), false);
    if (!RAB_IsSurfaceValid(surface) || !RAB_SurfaceSupportsOpaqueDiffuseBrdf(surface))
    {
        return float4(0.0, 0.0, 0.0, 1.0);
    }

    const RTXDI_PTReservoir initialReservoir = LoadRestirPTInitialDirectReservoir(reservoirPixel);
    const RTXDI_PTReservoir temporalOutputReservoir = LoadRestirPTTemporalOutputReservoir(reservoirPixel);
    if (!RTXDI_IsValidPTReservoir(initialReservoir))
    {
        return float4(0.55, 0.05, 0.04, 1.0);
    }
    if (!RTXDI_IsValidPTReservoir(temporalOutputReservoir))
    {
        return float4(0.62, 0.08, 0.65, 1.0);
    }

    const uint2 representativeFullPixel = PathTraceRestirDirectPixelToRepresentativeFullPixel(reservoirPixel);
    const uint motionMask = PathTraceMotionVectorMask[representativeFullPixel];
    float2 previousFullPixelFloat = float2(representativeFullPixel) + 0.5;
    float expectedPrevLinearDepth = surface.linearDepth;
    if ((motionMask & PT_MOTION_VECTOR_MASK_VALID) != 0u)
    {
        const float3 motionVector = PathTraceMotionVectors[representativeFullPixel].xyz;
        previousFullPixelFloat += motionVector.xy;
        expectedPrevLinearDepth = surface.linearDepth + motionVector.z;
    }
    else if (!ProjectPathTracePrimarySurfaceToPreviousPixelFloatAndDepth(
        surface.worldPos, PathTraceFullOutputSize(), previousFullPixelFloat, expectedPrevLinearDepth))
    {
        return RestirPTReferenceTemporalNeighborColor(1u, 0.0);
    }

    const float2 previousPixelFloat = PathTraceFullPixelFloatToRestirDirectPixelFloat(previousFullPixelFloat);
    int2 previousPixel = int2(floor(previousPixelFloat));
    if (PathTraceRestirSparsityEnabled() && previousPixel.x >= 0 && previousPixel.y >= 0)
    {
        previousPixel = int2(PathTraceRestirSparseRepresentativePixel(uint2(previousPixel), true));
    }

    uint bestFailure = 2u;
    RTXDI_PTReservoir acceptedReservoir = RTXDI_EmptyPTReservoir();
    const int temporalSampleCount = 5;
    const int sampleCount = temporalSampleCount + (RestirPTParamsFlat.temporalResampling_enableFallbackSampling != 0u ? 1 : 0);
    const int radius = 1;
    RTXDI_RuntimeParameters rtxdiRuntimeParams = (RTXDI_RuntimeParameters)0;
    rtxdiRuntimeParams.frameIndex = (uint)max(RestirPTInfo.x, 0.0);
    RTXDI_RandomSamplerState rng = RTXDI_InitRandomSampler(reservoirPixel, rtxdiRuntimeParams.frameIndex, 0x51ed270bu);
    const int temporalSampleStartIdx = int(RTXDI_GetNextRandom(rng) * 8.0);
    for (int i = 0; i < sampleCount; ++i)
    {
        const bool isFallbackSample = i == temporalSampleCount;
        int2 candidatePixel = isFallbackSample ? int2(reservoirPixel) : previousPixel;
        if (!isFallbackSample && i != 0)
        {
            candidatePixel += RestirPTReferenceTemporalOffset(temporalSampleStartIdx + i, radius);
        }

        RTXDI_PTReservoir candidateReservoir;
        const uint candidateStatus = RestirPTReferenceClassifyTemporalCandidate(
            surface, candidatePixel, isFallbackSample, expectedPrevLinearDepth, candidateReservoir);
        if (candidateStatus == 7u)
        {
            acceptedReservoir = candidateReservoir;
            break;
        }
        bestFailure = max(bestFailure, candidateStatus);
    }

    const float initialM = (float)initialReservoir.M;
    const float outputM = (float)temporalOutputReservoir.M;
    const float history = saturate(outputM / max((float)RestirPTParamsFlat.temporalResampling_maxHistoryLength, 1.0));
    if (!RTXDI_IsValidPTReservoir(acceptedReservoir))
    {
        const uint temporalNeighborDebugMode = RestirPTReferenceTemporalNeighborDebugMode();
        if (temporalNeighborDebugMode == 1u)
        {
            return RestirPTReferenceTemporalReuseBucketColor(
                bestFailure, false, false, false, false, 0u, history);
        }
        if (temporalNeighborDebugMode == 2u)
        {
            return RestirPTReferenceTemporalNeeRemapBucketColor(
                bestFailure, false, false, false, false, 0u, history);
        }
        return RestirPTReferenceTemporalNeighborColor(bestFailure, history);
    }

    const bool duplicationMapRequested = RestirPTReferenceDuplicationMapRequested();
    const bool outputGrewHistory = outputM > initialM + 0.5;
    const bool previousNeeReuseDisabledForAcceptedCandidate =
        MotionVectorInfo.y < 0.5 && RestirPTReferenceReservoirConnectsToNeeLight(acceptedReservoir);
    const uint acceptedRemapFailureReason = RestirPTReferencePreviousNeeLightRemapFailureReason(acceptedReservoir);

    const uint temporalNeighborDebugMode = RestirPTReferenceTemporalNeighborDebugMode();
    if (temporalNeighborDebugMode == 1u)
    {
        return RestirPTReferenceTemporalReuseBucketColor(
            bestFailure,
            true,
            duplicationMapRequested,
            outputGrewHistory,
            previousNeeReuseDisabledForAcceptedCandidate,
            acceptedRemapFailureReason,
            history);
    }
    if (temporalNeighborDebugMode == 2u)
    {
        return RestirPTReferenceTemporalNeeRemapBucketColor(
            bestFailure,
            true,
            duplicationMapRequested,
            outputGrewHistory,
            previousNeeReuseDisabledForAcceptedCandidate,
            acceptedRemapFailureReason,
            history);
    }

    if (duplicationMapRequested)
    {
        return RestirPTReferenceTemporalNeighborColor(27u, history);
    }
    if (outputM <= initialM + 0.5)
    {
        if (previousNeeReuseDisabledForAcceptedCandidate)
        {
            return RestirPTReferenceTemporalNeighborColor(9u, history);
        }
        if (acceptedRemapFailureReason != 0u)
        {
            return RestirPTReferenceTemporalNeighborColor(acceptedRemapFailureReason, history);
        }
        return RestirPTReferenceTemporalNeighborColor(8u, history);
    }
    return RestirPTReferenceTemporalNeighborColor(7u, history);
}

float4 EvaluateRestirPTReferenceTemporalAcceptanceView(RAB_Surface surface, uint2 pixel)
{
    const float4 rejected = float4(0.0, 0.0, 0.0, 1.0);
    const float4 accepted = float4(1.0, 0.0, 0.0, 1.0);
    const uint2 reservoirPixel = PathTraceFullPixelToRestirDirectPixel(pixel);
    surface = RestirPTReferenceLoadDirectDomainSurface(int2(reservoirPixel), false);
    if (!RAB_IsSurfaceValid(surface) || !RAB_SurfaceSupportsOpaqueDiffuseBrdf(surface))
    {
        return rejected;
    }

    const RTXDI_PTReservoir initialReservoir = LoadRestirPTInitialDirectReservoir(reservoirPixel);
    const RTXDI_PTReservoir temporalOutputReservoir = LoadRestirPTTemporalOutputReservoir(reservoirPixel);
    if (!RTXDI_IsValidPTReservoir(initialReservoir) || !RTXDI_IsValidPTReservoir(temporalOutputReservoir))
    {
        return rejected;
    }

    const uint2 representativeFullPixel = PathTraceRestirDirectPixelToRepresentativeFullPixel(reservoirPixel);
    const uint motionMask = PathTraceMotionVectorMask[representativeFullPixel];
    float2 previousFullPixelFloat = float2(representativeFullPixel) + 0.5;
    float expectedPrevLinearDepth = surface.linearDepth;
    if ((motionMask & PT_MOTION_VECTOR_MASK_VALID) != 0u)
    {
        const float3 motionVector = PathTraceMotionVectors[representativeFullPixel].xyz;
        previousFullPixelFloat += motionVector.xy;
        expectedPrevLinearDepth = surface.linearDepth + motionVector.z;
    }
    else if (!ProjectPathTracePrimarySurfaceToPreviousPixelFloatAndDepth(
        surface.worldPos, PathTraceFullOutputSize(), previousFullPixelFloat, expectedPrevLinearDepth))
    {
        return rejected;
    }

    const float2 previousDirectPixelFloat = PathTraceFullPixelFloatToRestirDirectPixelFloat(previousFullPixelFloat);
    int2 previousDirectPixel = int2(floor(previousDirectPixelFloat));
    if (PathTraceRestirSparsityEnabled() && previousDirectPixel.x >= 0 && previousDirectPixel.y >= 0)
    {
        previousDirectPixel = int2(PathTraceRestirSparseRepresentativePixel(uint2(previousDirectPixel), true));
    }

    RTXDI_PTReservoir acceptedReservoir = RTXDI_EmptyPTReservoir();
    const int temporalSampleCount = 5;
    const int sampleCount = temporalSampleCount + (RestirPTParamsFlat.temporalResampling_enableFallbackSampling != 0u ? 1 : 0);
    const int radius = 1;
    RTXDI_RuntimeParameters rtxdiRuntimeParams = (RTXDI_RuntimeParameters)0;
    rtxdiRuntimeParams.frameIndex = (uint)max(RestirPTInfo.x, 0.0);
    RTXDI_RandomSamplerState rng = RTXDI_InitRandomSampler(reservoirPixel, rtxdiRuntimeParams.frameIndex, 0x51ed270bu);
    const int temporalSampleStartIdx = int(RTXDI_GetNextRandom(rng) * 8.0);
    for (int i = 0; i < sampleCount; ++i)
    {
        const bool isFallbackSample = i == temporalSampleCount;
        int2 candidatePixel = isFallbackSample ? int2(reservoirPixel) : previousDirectPixel;
        if (!isFallbackSample && i != 0)
        {
            candidatePixel += RestirPTReferenceTemporalOffset(temporalSampleStartIdx + i, radius);
        }

        RTXDI_PTReservoir candidateReservoir;
        const uint candidateStatus = RestirPTReferenceClassifyTemporalCandidate(
            surface, candidatePixel, isFallbackSample, expectedPrevLinearDepth, candidateReservoir);
        if (candidateStatus == 7u)
        {
            acceptedReservoir = candidateReservoir;
            break;
        }
    }

    if (!RTXDI_IsValidPTReservoir(acceptedReservoir))
    {
        return rejected;
    }

    const uint remapFailureReason = RestirPTReferencePreviousNeeLightRemapFailureReason(acceptedReservoir);
    if (remapFailureReason != 0u)
    {
        return rejected;
    }

    const float initialM = (float)initialReservoir.M;
    const float outputM = (float)temporalOutputReservoir.M;
    if (outputM <= initialM + 0.5)
    {
        return rejected;
    }

    return accepted;
}

bool RestirPTReferenceReservoirHasBadNumericState(RTXDI_PTReservoir reservoir)
{
    return reservoir.WeightSum != reservoir.WeightSum ||
        abs(reservoir.WeightSum) > 65504.0 ||
        !all(reservoir.TargetFunction == reservoir.TargetFunction) ||
        any(abs(reservoir.TargetFunction) > 65504.0);
}

bool RestirPTReferenceReservoirHasUsefulTarget(RTXDI_PTReservoir reservoir)
{
    return RTXDI_Luminance(max(reservoir.TargetFunction, float3(0.0, 0.0, 0.0))) > 0.0;
}

float4 RestirPTReferenceInitialTemporalReservoirClassColor(RTXDI_PTReservoir reservoir)
{
    if (!RTXDI_IsValidPTReservoir(reservoir))
    {
        return float4(0.55, 0.04, 0.03, 1.0);
    }
    if (RestirPTReferenceReservoirHasBadNumericState(reservoir))
    {
        return float4(1.0, 0.0, 1.0, 1.0);
    }
    if (reservoir.WeightSum <= 0.0)
    {
        return float4(0.95, 0.82, 0.05, 1.0);
    }
    if (!RestirPTReferenceReservoirHasUsefulTarget(reservoir))
    {
        return float4(0.05, 0.18, 0.75, 1.0);
    }
    return RTXDI_ConnectsToNeeLight(reservoir)
        ? float4(0.04, 0.82, 0.22, 1.0)
        : float4(0.05, 0.78, 0.88, 1.0);
}

float4 RestirPTReferenceInitialTemporalHistoryColor(
    RTXDI_PTReservoir initialReservoir,
    RTXDI_PTReservoir temporalReservoir,
    bool showTemporal)
{
    if (!RTXDI_IsValidPTReservoir(initialReservoir))
    {
        return float4(0.35, 0.02, 0.02, 1.0);
    }

    const float initialM = (float)initialReservoir.M;
    if (!showTemporal)
    {
        const float initialHeat = saturate(initialM / max((float)RestirPTParamsFlat.temporalResampling_maxHistoryLength, 1.0));
        return float4(0.04, 0.24 + initialHeat * 0.35, 0.58 + initialHeat * 0.30, 1.0);
    }

    if (!RTXDI_IsValidPTReservoir(temporalReservoir))
    {
        return float4(0.55, 0.04, 0.03, 1.0);
    }

    const float temporalM = (float)temporalReservoir.M;
    const float history = saturate(temporalM / max((float)RestirPTParamsFlat.temporalResampling_maxHistoryLength, 1.0));
    if (temporalM > initialM + 0.5)
    {
        return float4(0.04, 0.42 + history * 0.50, 0.12 + history * 0.55, 1.0);
    }
    return float4(0.95, 0.42, 0.04, 1.0);
}

float4 EvaluateRestirPTReferenceInitialTemporalContributionSplitView(RAB_Surface surface, uint2 pixel)
{
    if (!RAB_IsSurfaceValid(surface) || !RAB_SurfaceSupportsOpaqueDiffuseBrdf(surface))
    {
        return float4(0.0, 0.0, 0.0, 1.0);
    }

    const uint2 reservoirPixel = PathTraceFullPixelToRestirDirectPixel(pixel);
    const RTXDI_PTReservoir initialReservoir = LoadRestirPTInitialDirectReservoir(reservoirPixel);
    const RTXDI_PTReservoir temporalReservoir = LoadRestirPTTemporalOutputReservoir(reservoirPixel);
    const uint2 dimensions = max(PathTraceFullOutputSize(), uint2(1u, 1u));
    const bool showTemporal = pixel.x >= dimensions.x / 2u;

    if (showTemporal)
    {
        if (!RTXDI_IsValidPTReservoir(temporalReservoir))
        {
            return float4(0.35, 0.0, 0.0, 1.0);
        }
        if (!RestirPTReferenceFinalShadingHasUsefulSample(temporalReservoir))
        {
            return float4(0.02, 0.12, 0.35, 1.0);
        }
        return float4(RestirPTToneMapPreview(RestirPTReferenceFinalShadingContribution(temporalReservoir)), 1.0);
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

float4 EvaluateRestirPTReferenceInitialTemporalStateSplitView(RAB_Surface surface, uint2 pixel)
{
    if (!RAB_IsSurfaceValid(surface) || !RAB_SurfaceSupportsOpaqueDiffuseBrdf(surface))
    {
        return float4(0.0, 0.0, 0.0, 1.0);
    }

    const uint2 reservoirPixel = PathTraceFullPixelToRestirDirectPixel(pixel);
    const RTXDI_PTReservoir initialReservoir = LoadRestirPTInitialDirectReservoir(reservoirPixel);
    const RTXDI_PTReservoir temporalReservoir = LoadRestirPTTemporalOutputReservoir(reservoirPixel);
    const uint2 dimensions = max(PathTraceFullOutputSize(), uint2(1u, 1u));
    const bool showTemporal = pixel.x >= dimensions.x / 2u;
    const bool showHistory = pixel.y >= dimensions.y / 2u;
    if (showHistory)
    {
        return RestirPTReferenceInitialTemporalHistoryColor(initialReservoir, temporalReservoir, showTemporal);
    }
    return showTemporal
        ? RestirPTReferenceInitialTemporalReservoirClassColor(temporalReservoir)
        : RestirPTReferenceInitialTemporalReservoirClassColor(initialReservoir);
}

#endif
