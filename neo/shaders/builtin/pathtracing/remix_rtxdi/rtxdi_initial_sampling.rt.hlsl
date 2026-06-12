#ifndef RB_PATH_TRACING_REMIX_RTXDI_INITIAL_SAMPLING_RT_HLSL
#define RB_PATH_TRACING_REMIX_RTXDI_INITIAL_SAMPLING_RT_HLSL

// RTX Remix-shaped DI initial sampling contract.
//
// This file is intentionally not routed into the current smoke/ReSTIR shader
// set yet. It samples current-frame lights through the Remix RAB bridge and
// stores the resulting unpacked RTXDI DI reservoir through the Remix reservoir
// callbacks.

#undef RTXDI_ENABLE_PRESAMPLING
#define RTXDI_ENABLE_PRESAMPLING 0

#include "../RtxdiBridge/RAB_Surface.hlsli"
#define REMIX_RAB_EXPORT_RAB_NAMES 1
#include "../remix_bridge/RAB_LightBridge.hlsli"
#include "../remix_bridge/RAB_LightSamplingBridge.hlsli"
#include "../remix_bridge/RAB_ReservoirBridge.hlsli"

void RAB_GetLightDirDistance(RAB_Surface surface, RAB_LightSample lightSample, out float3 lightDir, out float lightDistance)
{
    RemixRAB_GetLightDirDistance(surface, lightSample, lightDir, lightDistance);
}

bool RemixRtxdiGetConservativeLightSampleGeometry(RAB_Surface surface, RAB_LightSample lightSample)
{
    float3 lightDir;
    float lightDistance;
    RemixRAB_GetLightDirDistance(surface, lightSample, lightDir, lightDistance);

    if (!RAB_IsSurfaceValid(surface) || !RAB_IsReplayableLightSample(lightSample))
    {
        return false;
    }

    const float3 normal = RAB_SafeNormalize(RAB_GetSurfaceNormal(surface), RAB_GetSurfaceGeoNormal(surface));
    return dot(normal, lightDir) > 0.0 && dot(RAB_GetSurfaceGeoNormal(surface), lightDir) > 0.0;
}

bool RAB_GetConservativeVisibility(RAB_Surface surface, RAB_LightSample lightSample)
{
    return RemixRtxdiGetConservativeLightSampleGeometry(surface, lightSample);
}

float RAB_GetConservativeVisibility(RAB_Surface surface, float3 samplePosition)
{
    RAB_LightSample lightSample = RAB_EmptyLightSample();
    lightSample.valid = 1u;
    lightSample.position = samplePosition;
    lightSample.distance = length(samplePosition - RAB_GetSurfaceWorldPos(surface));
    lightSample.solidAnglePdf = 1.0;
    return RAB_GetConservativeVisibility(surface, lightSample) ? 1.0 : 0.0;
}

bool RAB_TraceRayForLocalLight(float3 origin, float3 direction, float tMin, float tMax, out uint lightIndex, out float2 randXY)
{
    lightIndex = RAB_INVALID_LIGHT_INDEX;
    randXY = float2(0.0, 0.0);
    return false;
}

float RAB_EvaluateLocalLightSourcePdf(uint lightIndex)
{
    return 0.0;
}

float RAB_EvaluateEnvironmentMapSamplingPdf(float3 direction)
{
    return 0.0;
}

float2 RAB_GetEnvironmentMapRandXYFromDir(float3 direction)
{
    return float2(0.0, 0.0);
}

#include "Rtxdi/DI/InitialSampling.hlsli"

static const uint REMIX_RTXDI_INITIAL_RNG_PASS = 0x52525805u;
static const uint REMIX_RTXDI_INVALID_PORTAL_INDEX = 0xffffffffu;
static const uint REMIX_RTXDI_REFERENCE_INVALID_PORTAL_INDEX = 7u;

struct RemixRtxdiInitialSamplingResult
{
    RTXDI_DIReservoir reservoir;
    RAB_LightSample selectedSample;
};

struct RemixRtxdiInitialLightRangeContract
{
    uint2 emissiveRange;
    uint emissiveSampleCount;
    uint2 doomAnalyticRange;
    uint doomAnalyticSampleCount;
    uint totalSampleCount;
    uint nonEmptyRangeCount;
};

struct RemixRtxdiPreviousBestLightContract
{
    uint valid;
    uint previousLightIndex;
    uint currentLightIndex;
    uint portalIndex;
    uint portalRejected;
};

struct RemixRtxdiInitialSamplingContract
{
    uint2 pixel;
    uint frameIndex;
    uint enableInitialVisibility;
    uint enableBestLightSampling;
    RemixRtxdiInitialLightRangeContract lightRanges;
    RemixRtxdiPreviousBestLightContract previousBestLight;
};

RTXDI_DIInitialSamplingParameters RemixRtxdiBuildLocalInitialSamplingParameters(uint sampleCount)
{
    RTXDI_DIInitialSamplingParameters params = (RTXDI_DIInitialSamplingParameters)0;
    params.numLocalLightSamples = sampleCount;
    params.numInfiniteLightSamples = 0u;
    params.numEnvironmentSamples = 0u;
    params.numBrdfSamples = 0u;
    params.brdfCutoff = 0.0;
    params.brdfRayMinT = 0.0;
    params.localLightSamplingMode = ReSTIRDI_LocalLightSamplingMode_UNIFORM;
    params.enableInitialVisibility = 0u;
    params.environmentMapImportanceSampling = 0u;
    return params;
}

RemixRtxdiInitialLightRangeContract RemixRtxdiGetInitialLightRangeContract()
{
    RemixRtxdiInitialLightRangeContract contract = (RemixRtxdiInitialLightRangeContract)0;
    contract.emissiveRange = RemixRAB_GetEmissiveRange();
    contract.emissiveSampleCount = RemixRAB_GetEmissiveSampleCount();
    contract.doomAnalyticRange = RemixRAB_GetDoomAnalyticRange();
    contract.doomAnalyticSampleCount = RemixRAB_GetDoomAnalyticSampleCount();
    contract.totalSampleCount = RemixRAB_GetTotalSampleCount();
    contract.nonEmptyRangeCount = RemixRAB_GetNonEmptyRangeCount();
    return contract;
}

bool RemixRtxdiIsInvalidPortalIndex(uint portalIndex)
{
    return portalIndex == REMIX_RTXDI_INVALID_PORTAL_INDEX ||
        portalIndex == REMIX_RTXDI_REFERENCE_INVALID_PORTAL_INDEX;
}

RemixRtxdiPreviousBestLightContract RemixRtxdiResolvePreviousBestLightContract(
    uint previousBestLightIndex,
    uint previousBestPortalIndex,
    bool enableBestLightSampling)
{
    RemixRtxdiPreviousBestLightContract contract = (RemixRtxdiPreviousBestLightContract)0;
    contract.previousLightIndex = REMIX_RAB_INVALID_LIGHT_INDEX;
    contract.currentLightIndex = REMIX_RAB_INVALID_LIGHT_INDEX;
    contract.portalIndex = RemixRtxdiIsInvalidPortalIndex(previousBestPortalIndex)
        ? REMIX_RTXDI_INVALID_PORTAL_INDEX
        : previousBestPortalIndex;

    if (!enableBestLightSampling || previousBestLightIndex == REMIX_RAB_INVALID_LIGHT_INDEX)
    {
        return contract;
    }

    // rbdoom's inactive Remix bridge has no portal transform path yet. Reject
    // cross-portal best lights instead of silently sampling them as local.
    if (!RemixRtxdiIsInvalidPortalIndex(previousBestPortalIndex))
    {
        contract.portalRejected = 1u;
        return contract;
    }

    uint currentLightIndex = REMIX_RAB_INVALID_LIGHT_INDEX;
    if (RemixRAB_TranslateLightIndex(previousBestLightIndex, false, currentLightIndex))
    {
        contract.valid = 1u;
        contract.previousLightIndex = previousBestLightIndex;
        contract.currentLightIndex = currentLightIndex;
    }

    return contract;
}

RemixRtxdiInitialSamplingContract RemixRtxdiBuildInitialSamplingContract(
    uint2 pixel,
    uint frameIndex,
    uint previousBestLightIndex,
    uint previousBestPortalIndex,
    bool enableBestLightSampling,
    bool enableInitialVisibility)
{
    RemixRtxdiInitialSamplingContract contract = (RemixRtxdiInitialSamplingContract)0;
    contract.pixel = pixel;
    contract.frameIndex = frameIndex;
    contract.enableInitialVisibility = enableInitialVisibility ? 1u : 0u;
    contract.enableBestLightSampling = enableBestLightSampling ? 1u : 0u;
    contract.lightRanges = RemixRtxdiGetInitialLightRangeContract();
    contract.previousBestLight = RemixRtxdiResolvePreviousBestLightContract(
        previousBestLightIndex,
        previousBestPortalIndex,
        enableBestLightSampling);
    return contract;
}

bool RemixRtxdiIsExcludedPreviousBestCandidate(
    uint currentLightIndex,
    uint candidatePortalIndex,
    RemixRtxdiPreviousBestLightContract previousBestLight)
{
    return previousBestLight.valid != 0u &&
        previousBestLight.currentLightIndex == currentLightIndex &&
        previousBestLight.portalIndex == candidatePortalIndex;
}

RTXDI_DIReservoir RemixRtxdiSampleStridedLightRange(
    inout RTXDI_RandomSamplerState rng,
    RAB_Surface surface,
    uint2 lightRange,
    uint sampleCount,
    RemixRtxdiPreviousBestLightContract previousBestLight,
    out RAB_LightSample selectedSample)
{
    selectedSample = RAB_EmptyLightSample();

    if (lightRange.y == 0u || sampleCount == 0u)
    {
        return RTXDI_EmptyDIReservoir();
    }

    RTXDI_DIInitialSamplingParameters sampleParams = RemixRtxdiBuildLocalInitialSamplingParameters(sampleCount);
    RTXDI_InitialSamplingMisData misData = RTXDI_ComputeInitialSamplingMisData(sampleParams);
    RTXDI_DIReservoir reservoir = RTXDI_EmptyDIReservoir();

    const float rangeInvSourcePdf = float(lightRange.y);
    float lightIndexInRange = RTXDI_GetNextRandom(rng) * float(lightRange.y);
    const float stride = max(1.0, float(lightRange.y) / float(sampleCount));

    for (uint sampleIndex = 0u; sampleIndex < sampleCount; ++sampleIndex)
    {
        const uint currentLightIndex = uint(lightIndexInRange) + lightRange.x;
        const uint candidatePortalIndex = REMIX_RTXDI_INVALID_PORTAL_INDEX;

        if (!RemixRtxdiIsExcludedPreviousBestCandidate(currentLightIndex, candidatePortalIndex, previousBestLight))
        {
            RAB_LightInfo lightInfo = RemixRAB_LoadLightInfo(currentLightIndex, false);
            if (RAB_IsLightInfoValid(lightInfo))
            {
                const float2 uv = RTXDI_RandomlySelectLocalLightUV(rng);
                RTXDI_StreamLocalLightAtUVIntoReservoir(
                    rng,
                    misData,
                    surface,
                    sampleParams.brdfCutoff,
                    misData.localLightMisWeight,
                    currentLightIndex,
                    uv,
                    rangeInvSourcePdf,
                    lightInfo,
                    reservoir,
                    selectedSample);
            }
        }

        lightIndexInRange += stride;
        if (lightIndexInRange >= float(lightRange.y))
        {
            lightIndexInRange -= float(lightRange.y);
        }
    }

    RTXDI_FinalizeResampling(reservoir, 1.0, reservoir.M);
    reservoir.M = 1;
    return reservoir;
}

RTXDI_DIReservoir RemixRtxdiSamplePreviousBestLight(
    inout RTXDI_RandomSamplerState rng,
    RAB_Surface surface,
    uint currentLightIndex,
    out RAB_LightSample selectedSample)
{
    selectedSample = RAB_EmptyLightSample();

    RAB_LightInfo lightInfo = RemixRAB_LoadLightInfo(currentLightIndex, false);
    if (!RAB_IsLightInfoValid(lightInfo))
    {
        return RTXDI_EmptyDIReservoir();
    }

    RTXDI_DIReservoir reservoir = RTXDI_EmptyDIReservoir();
    const float2 uv = RTXDI_RandomlySelectLocalLightUV(rng);
    const float invSourcePdf = 1.0;
    RAB_LightSample candidateSample = RAB_SamplePolymorphicLight(lightInfo, surface, uv);
    const float targetPdf = RAB_GetLightSampleTargetPdfForSurface(candidateSample, surface);

    if (RTXDI_StreamSample(reservoir, currentLightIndex, uv, RTXDI_GetNextRandom(rng), targetPdf, invSourcePdf))
    {
        selectedSample = candidateSample;
    }

    RTXDI_FinalizeResampling(reservoir, 1.0, reservoir.M);
    reservoir.M = 1;
    return reservoir;
}

bool RemixRtxdiCombineReservoir(
    inout RTXDI_RandomSamplerState rng,
    inout RTXDI_DIReservoir reservoir,
    RTXDI_DIReservoir candidate)
{
    return RTXDI_CombineDIReservoirs(
        reservoir,
        candidate,
        RTXDI_GetNextRandom(rng),
        candidate.targetPdf);
}

void RemixRtxdiDeferInitialVisibility(inout RTXDI_DIReservoir reservoir, RAB_Surface surface, RAB_LightSample selectedSample)
{
    // Real Remix initial visibility requires selected-light shadow visibility.
    // This inactive contract has no bound visibility callback yet, so leave the
    // reservoir visibility unset instead of treating facing/geometry as a hit.
}

RTXDI_DIReservoir RemixRtxdiRunInitialSampling(
    RAB_Surface surface,
    uint2 pixel,
    uint frameIndex,
    uint previousBestLightIndex,
    uint previousBestPortalIndex,
    bool enableBestLightSampling,
    bool enableInitialVisibility)
{
    RemixRtxdiInitialSamplingResult result = (RemixRtxdiInitialSamplingResult)0;
    result.reservoir = RTXDI_EmptyDIReservoir();
    result.selectedSample = RAB_EmptyLightSample();

    const RemixRtxdiInitialSamplingContract contract = RemixRtxdiBuildInitialSamplingContract(
        pixel,
        frameIndex,
        previousBestLightIndex,
        previousBestPortalIndex,
        enableBestLightSampling,
        enableInitialVisibility);

    if (RAB_IsSurfaceValid(surface) && contract.lightRanges.totalSampleCount != 0u)
    {
        RTXDI_RandomSamplerState rng = RTXDI_InitRandomSampler(pixel, frameIndex, REMIX_RTXDI_INITIAL_RNG_PASS);

        RAB_LightSample emissiveSample = RAB_EmptyLightSample();
        RTXDI_DIReservoir emissiveReservoir = RemixRtxdiSampleStridedLightRange(
            rng,
            surface,
            contract.lightRanges.emissiveRange,
            contract.lightRanges.emissiveSampleCount,
            contract.previousBestLight,
            emissiveSample);

        RAB_LightSample doomAnalyticSample = RAB_EmptyLightSample();
        RTXDI_DIReservoir doomAnalyticReservoir = RemixRtxdiSampleStridedLightRange(
            rng,
            surface,
            contract.lightRanges.doomAnalyticRange,
            contract.lightRanges.doomAnalyticSampleCount,
            contract.previousBestLight,
            doomAnalyticSample);

        if (RemixRtxdiCombineReservoir(rng, result.reservoir, emissiveReservoir))
        {
            result.selectedSample = emissiveSample;
        }
        if (RemixRtxdiCombineReservoir(rng, result.reservoir, doomAnalyticReservoir))
        {
            result.selectedSample = doomAnalyticSample;
        }

        if (contract.previousBestLight.valid != 0u)
        {
            RAB_LightSample bestLightSample = RAB_EmptyLightSample();
            RTXDI_DIReservoir bestLightReservoir = RemixRtxdiSamplePreviousBestLight(
                rng,
                surface,
                contract.previousBestLight.currentLightIndex,
                bestLightSample);

            if (RemixRtxdiCombineReservoir(rng, result.reservoir, bestLightReservoir))
            {
                result.selectedSample = bestLightSample;
            }
        }

        result.reservoir.M = 1;

        if (enableInitialVisibility)
        {
            RemixRtxdiDeferInitialVisibility(result.reservoir, surface, result.selectedSample);
        }
    }

    RAB_StoreReservoir(
        result.reservoir,
        int2(pixel),
        int(RemixRAB_GetDIInitialOutputReservoirIndex()));

    return result.reservoir;
}

#endif
