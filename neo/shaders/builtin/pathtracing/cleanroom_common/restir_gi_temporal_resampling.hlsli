#ifndef RB_PATH_TRACING_CLEANROOM_COMMON_RESTIR_GI_TEMPORAL_RESAMPLING_HLSLI
#define RB_PATH_TRACING_CLEANROOM_COMMON_RESTIR_GI_TEMPORAL_RESAMPLING_HLSLI

#include "cleanroom_common/restir_gi_parameters.hlsli"
#include "cleanroom_common/restir_gi_reservoir.hlsli"
#include "Rtxdi/Utils/RandomSamplerState.hlsli"

// rbdoom-owned GI temporal resampling, replacing
// Rtxdi/GI/TemporalResampling.hlsli. Math: Ouyang et al. 2021 (ReSTIR GI) -
// two-candidate streaming merge of the fresh initial sample with the
// back-projected history sample, with the reconnection-shift jacobian in the
// resampling weight and 1/Z (BASIC) bias correction. Conventions match the
// rbdoom GI spatial replacement (GISpatialResampling.hlsli): candidate
// weight = m * p-hat_q(T(s)) * |J| * W, finalize
// W_out = wSum / (normalization * p-hat_q(selected)).
//
// The including shader must define, before this header:
//   RAB_GetGBufferSurface(int2 pixel, bool previousFrame)   (bounds-safe)
//   RAB_IsSurfaceValid / RAB_GetSurfaceNormal / RAB_GetSurfaceLinearDepth /
//   RAB_GetSurfaceWorldPos
//   RAB_GetGISampleTargetPdfForSurface(samplePosition, sampleRadiance, surface)
//   RTXDI_LoadGIReservoir (Rtxdi/GI/Reservoir.hlsli storage, i.e. the GI
//   reservoir bridge with RTXDI_GI_RESERVOIR_BUFFER set up)
//
// Scope notes:
//   - biasCorrectionMode: OFF = 1/M, BASIC+ = 1/Z. PAIRWISE/RAY_TRACED clamp
//     to BASIC (the only caller clamps to BASIC anyway; a temporal pairwise
//     MIS over two candidates adds nothing, and ray-traced revalidation of
//     the history sample belongs to the deferred lighting-validation slice).
//   - enableFallbackSampling: when the back-projected pixel fails its gates,
//     one extra candidate pixel is tried from a small jitter disc around it
//     (recovers history across small reprojection misses at thin geometry).
//   - The jacobian cutoff uses the same default (10.0) as the GI spatial
//     replacement; the temporal parameter struct has no cutoff field at the
//     repo call sites.

#ifndef RBPT_GI_TEMPORAL_JACOBIAN_CUTOFF
#define RBPT_GI_TEMPORAL_JACOBIAN_CUTOFF 10.0
#endif

#ifndef RBPT_GI_TEMPORAL_FALLBACK_RADIUS
#define RBPT_GI_TEMPORAL_FALLBACK_RADIUS 3.0
#endif

// Reconnection-shift jacobian (Ouyang et al. 2021, Eq. 11). Same math as
// RBPT_GIReconnectionJacobian in the GI spatial replacement; named
// differently so both headers stay self-contained without an include-order
// contract between them.
float RBPT_GITemporalReconnectionJacobian(
    float3 receiverFromPos,
    float3 receiverToPos,
    RTXDI_GIReservoir reservoir)
{
    const float3 toFrom = receiverFromPos - reservoir.position;
    const float3 toTo = receiverToPos - reservoir.position;
    const float dFrom2 = dot(toFrom, toFrom);
    const float dTo2 = dot(toTo, toTo);
    if (dFrom2 <= 1.0e-12 || dTo2 <= 1.0e-12)
    {
        return 0.0;
    }

    const float cosFrom = dot(reservoir.normal, toFrom) * rsqrt(dFrom2);
    const float cosTo = dot(reservoir.normal, toTo) * rsqrt(dTo2);
    if (cosFrom <= 1.0e-4 || cosTo <= 1.0e-4)
    {
        return 0.0;
    }

    const float jacobian = (cosTo / cosFrom) * (dFrom2 / dTo2);
    if (jacobian < 1.0 / RBPT_GI_TEMPORAL_JACOBIAN_CUTOFF ||
        jacobian > RBPT_GI_TEMPORAL_JACOBIAN_CUTOFF)
    {
        return 0.0;
    }
    return jacobian;
}

bool RBPT_GITemporalSurfacesSimilar(
    RAB_Surface surface,
    RAB_Surface previousSurface,
    float expectedPreviousDepth,
    RTXDI_GITemporalResamplingParameters tparams)
{
    const float previousDepth = RAB_GetSurfaceLinearDepth(previousSurface);
    if (abs(previousDepth - expectedPreviousDepth) >
        tparams.depthThreshold * max(abs(expectedPreviousDepth), 1.0e-4))
    {
        return false;
    }
    return dot(RAB_GetSurfaceNormal(surface), RAB_GetSurfaceNormal(previousSurface)) >=
        tparams.normalThreshold;
}

// Find a history reservoir for this pixel: the back-projected pixel first,
// optionally one jittered fallback. Returns false when no candidate passes
// the surface gates with a valid, young-enough reservoir.
bool RBPT_GITemporalFindHistory(
    uint2 pixelPosition,
    RAB_Surface surface,
    float3 screenSpaceMotion,
    uint temporalInputPage,
    RTXDI_RuntimeParameters runtimeParams,
    RTXDI_ReservoirBufferParameters reservoirParams,
    RTXDI_GITemporalResamplingParameters tparams,
    inout RTXDI_RandomSamplerState rng,
    out RTXDI_GIReservoir historyReservoir,
    out float3 historySurfacePos)
{
    historyReservoir = RTXDI_EmptyGIReservoir();
    historySurfacePos = float3(0.0, 0.0, 0.0);

    int2 basePixel = int2(round(float2(pixelPosition) + screenSpaceMotion.xy));
    if (tparams.enablePermutationSampling != 0u)
    {
        RTXDI_ApplyPermutationSampling(basePixel, tparams.uniformRandomNumber);
    }
    const float expectedPreviousDepth =
        RAB_GetSurfaceLinearDepth(surface) + screenSpaceMotion.z;

    const uint attemptCount = tparams.enableFallbackSampling != 0u ? 2u : 1u;
    [loop]
    for (uint attempt = 0u; attempt < attemptCount; ++attempt)
    {
        int2 candidatePixel = basePixel;
        if (attempt > 0u)
        {
            const float2 jitter = float2(
                RTXDI_GetNextRandom(rng) * 2.0 - 1.0,
                RTXDI_GetNextRandom(rng) * 2.0 - 1.0) * RBPT_GI_TEMPORAL_FALLBACK_RADIUS;
            candidatePixel = basePixel + int2(round(jitter));
        }

        // Bounds-safe loader: off-screen pixels return an invalid surface.
        const RAB_Surface previousSurface = RAB_GetGBufferSurface(candidatePixel, true);
        if (!RAB_IsSurfaceValid(previousSurface) ||
            !RBPT_GITemporalSurfacesSimilar(surface, previousSurface, expectedPreviousDepth, tparams))
        {
            continue;
        }

        const RTXDI_GIReservoir candidate = RTXDI_LoadGIReservoir(
            reservoirParams,
            RTXDI_PixelPosToReservoirPos(uint2(candidatePixel), runtimeParams.activeCheckerboardField),
            temporalInputPage);
        if (!RTXDI_IsValidGIReservoir(candidate) ||
            candidate.weightSum <= 0.0 ||
            (tparams.maxReservoirAge > 0u && candidate.age >= tparams.maxReservoirAge))
        {
            continue;
        }

        historyReservoir = candidate;
        historySurfacePos = RAB_GetSurfaceWorldPos(previousSurface);
        return true;
    }
    return false;
}

RTXDI_GIReservoir RTXDI_GITemporalResampling(
    uint2 pixelPosition,
    RAB_Surface surface,
    float3 screenSpaceMotion,
    uint temporalInputPage,
    RTXDI_GIReservoir inputReservoir,
    inout RTXDI_RandomSamplerState rng,
    RTXDI_RuntimeParameters runtimeParams,
    RTXDI_ReservoirBufferParameters reservoirParams,
    RTXDI_GITemporalResamplingParameters tparams)
{
    if (!RAB_IsSurfaceValid(surface) || tparams.maxHistoryLength == 0u)
    {
        return inputReservoir;
    }

    RTXDI_GIReservoir historyReservoir;
    float3 historySurfacePos;
    if (!RBPT_GITemporalFindHistory(
        pixelPosition,
        surface,
        screenSpaceMotion,
        temporalInputPage,
        runtimeParams,
        reservoirParams,
        tparams,
        rng,
        historyReservoir,
        historySurfacePos))
    {
        return inputReservoir;
    }

    // Clamp accumulated confidence so stale history cannot dominate fresh
    // candidates (Bitterli 2020 Sec. 5; deployed-Remix-style low caps).
    historyReservoir.M = min(
        historyReservoir.M,
        tparams.maxHistoryLength * max(inputReservoir.M, 1u));

    const float3 receiverPos = RAB_GetSurfaceWorldPos(surface);
    const float canonicalM = (float)inputReservoir.M;
    const float historyM = (float)historyReservoir.M;
    const bool canonicalValid =
        RTXDI_IsValidGIReservoir(inputReservoir) && inputReservoir.weightSum > 0.0;

    // Shift the history sample into the current receiver's domain.
    const float jHistoryToCurrent = RBPT_GITemporalReconnectionJacobian(
        historySurfacePos, receiverPos, historyReservoir);
    const float historyTargetAtCurrent = jHistoryToCurrent > 0.0
        ? RAB_GetGISampleTargetPdfForSurface(historyReservoir.position, historyReservoir.radiance, surface)
        : 0.0;
    if (!(historyTargetAtCurrent > 0.0))
    {
        return inputReservoir;
    }

    const float canonicalTarget = canonicalValid
        ? RAB_GetGISampleTargetPdfForSurface(inputReservoir.position, inputReservoir.radiance, surface)
        : 0.0;

    // Two-candidate streaming merge; confidence-weighted RIS, bias handled
    // by the normalization below (matches the GI spatial replacement).
    RTXDI_GIReservoir selected = RTXDI_EmptyGIReservoir();
    float selectedTarget = 0.0;
    bool selectedHistory = false;
    float wSum = 0.0;

    if (canonicalValid && canonicalTarget > 0.0)
    {
        wSum += canonicalM * canonicalTarget * inputReservoir.weightSum;
        selected = inputReservoir;
        selectedTarget = canonicalTarget;
    }

    const float historyWeight =
        historyM * historyTargetAtCurrent * jHistoryToCurrent * historyReservoir.weightSum;
    if (historyWeight > 0.0)
    {
        wSum += historyWeight;
        if (RTXDI_GetNextRandom(rng) * wSum <= historyWeight)
        {
            selected = historyReservoir;
            selectedTarget = historyTargetAtCurrent;
            selectedHistory = true;
        }
    }

    if (!(wSum > 0.0) || selectedTarget <= 0.0 || !RTXDI_IsValidGIReservoir(selected))
    {
        return inputReservoir;
    }

    const float mergedM = canonicalM + historyM;
    float normalization;
    if (min(tparams.biasCorrectionMode, uint(RTXDI_BIAS_CORRECTION_BASIC)) >= RTXDI_BIAS_CORRECTION_BASIC)
    {
        // 1/Z: count the confidence of every domain whose shift of the
        // selected sample is valid. The domain the winner came from is
        // always counted - its existence proves that domain produces it.
        float normalizationZ = canonicalValid ? canonicalM : 0.0;
        if (selectedHistory ||
            RBPT_GITemporalReconnectionJacobian(receiverPos, historySurfacePos, selected) > 0.0)
        {
            normalizationZ += historyM;
        }
        normalization = max(normalizationZ, 1.0);
    }
    else
    {
        normalization = max(mergedM, 1.0);
    }

    RTXDI_GIReservoir result = selected;
    result.weightSum = wSum / (normalization * selectedTarget);
    result.M = (uint)min(mergedM, 255.0); // packed-ABI confidence cap
    result.age = selectedHistory ? min(historyReservoir.age + 1u, 0xffu) : inputReservoir.age;
    return result;
}

#endif
