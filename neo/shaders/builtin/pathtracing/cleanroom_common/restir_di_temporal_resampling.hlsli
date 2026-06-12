#ifndef RB_PATH_TRACING_CLEANROOM_COMMON_RESTIR_DI_TEMPORAL_RESAMPLING_HLSLI
#define RB_PATH_TRACING_CLEANROOM_COMMON_RESTIR_DI_TEMPORAL_RESAMPLING_HLSLI

#include "cleanroom_common/restir_di_parameters.hlsli"
#include "cleanroom_common/restir_di_reservoir.hlsli"
#include "cleanroom_common/restir_shared_math.hlsli"
#include "Rtxdi/Utils/RandomSamplerState.hlsli"

// rbdoom-owned DI temporal resampling, replacing
// Rtxdi/DI/TemporalResampling.hlsli. Math follows Bitterli et al. 2020
// (Alg. 4 two-reservoir merge with M-clamped history) and the SIGGRAPH
// course notes' 1/Z bias correction. The public signature is taken from the
// two repo call sites (clean DI sentinel temporal output, remix_rtxdi
// temporal reuse).
//
// The including shader must define, before this header:
//   RTXDI_LIGHT_RESERVOIR_BUFFER + Rtxdi/DI/ReservoirStorage.hlsli include
//   RAB_GetGBufferSurface(int2 pixel, bool previousFrame)   (bounds-safe)
//   RAB_IsSurfaceValid / RAB_GetSurfaceNormal / RAB_GetSurfaceLinearDepth
//   RAB_TranslateLightIndex(uint lightIndex, bool currentToPrevious)
//   RAB_LoadLightInfo(uint index, bool previousFrame)
//   RAB_SamplePolymorphicLight / RAB_GetLightSampleTargetPdfForSurface
//   RAB_GetConservativeVisibility(surface, lightSample)
//
// Bias correction modes:
//   OFF        - plain 1/M normalization (slightly darkens at disocclusions)
//   BASIC      - 1/Z: count only the domains where the selected sample's
//                target function is nonzero (current surface, and the
//                previous surface via the current->previous light remap)
//   PAIRWISE   - not implemented for the two-reservoir temporal merge
//                (pairwise MIS needs >2 strategies); behaves as BASIC
//   RAY_TRACED - BASIC plus a conservative visibility ray for a
//                history-selected sample at the current surface; occluded
//                history samples are discarded
//
// enableVisibilityShortcut is accepted but not implemented - both repo call
// sites pass 0. If a call site enables it, implement the stored-visibility
// fast path (RTXDI_GetDIReservoirVisibility) here at the same time.

#ifndef RTXDI_LIGHT_RESERVOIR_BUFFER
#error "RTXDI_DITemporalResampling requires RTXDI_LIGHT_RESERVOIR_BUFFER / Rtxdi/DI/ReservoirStorage.hlsli to be set up by the including shader"
#endif

// Evaluate the selected sample's target function at the previous frame's
// surface, used by the 1/Z denominator. lightIndexCurrent is in the current
// light list; returns 0 when the light has no previous-frame counterpart.
float RBPT_DITemporalTargetAtPreviousSurface(
    RAB_Surface previousSurface,
    uint lightIndexCurrent,
    float2 sampleUv)
{
    const int previousLightIndex = RAB_TranslateLightIndex(lightIndexCurrent, true);
    if (previousLightIndex < 0)
    {
        return 0.0;
    }

    const RAB_LightInfo previousLightInfo = RAB_LoadLightInfo((uint)previousLightIndex, true);
    if (!RAB_IsLightInfoValid(previousLightInfo))
    {
        return 0.0;
    }

    const RAB_LightSample previousSample = RAB_SamplePolymorphicLight(previousLightInfo, previousSurface, sampleUv);
    if (previousSample.valid == 0u || previousSample.solidAnglePdf <= 0.0)
    {
        return 0.0;
    }

    return max(RAB_GetLightSampleTargetPdfForSurface(previousSample, previousSurface), 0.0);
}

RTXDI_DIReservoir RTXDI_DITemporalResampling(
    uint2 pixelPosition,
    RAB_Surface surface,
    RTXDI_DIReservoir currentReservoir,
    inout RTXDI_RandomSamplerState rng,
    RTXDI_RuntimeParameters runtimeParams,
    RTXDI_ReservoirBufferParameters reservoirParams,
    float3 screenSpaceMotion,
    uint inputBufferIndex,
    RTXDI_DITemporalResamplingParameters params,
    inout int2 temporalSamplePixel,
    inout RAB_LightSample selectedLightSample)
{
    temporalSamplePixel = int2(-1, -1);

    if (params.maxHistoryLength == 0u)
    {
        return currentReservoir;
    }

    // Back-project to the previous pixel; optional permutation sampling
    // trades a little reprojection accuracy for decorrelated history.
    int2 previousPixel = int2(round(float2(pixelPosition) + screenSpaceMotion.xy));
    if (params.enablePermutationSampling != 0u)
    {
        RTXDI_ApplyPermutationSampling(previousPixel, params.uniformRandomNumber);
    }

    // The RAB surface loader is bounds-safe in both lanes and returns an
    // invalid surface off-screen, so this also rejects out-of-range pixels
    // before any reservoir-buffer read.
    const RAB_Surface previousSurface = RAB_GetGBufferSurface(previousPixel, true);
    if (!RAB_IsSurfaceValid(previousSurface))
    {
        return currentReservoir;
    }

    const float expectedPreviousDepth = RAB_GetSurfaceLinearDepth(surface) + screenSpaceMotion.z;
    if (!RTXDI_IsValidNeighbor(
        RAB_GetSurfaceNormal(surface),
        RAB_GetSurfaceNormal(previousSurface),
        expectedPreviousDepth,
        RAB_GetSurfaceLinearDepth(previousSurface),
        params.normalThreshold,
        params.depthThreshold))
    {
        return currentReservoir;
    }

    RTXDI_DIReservoir previousReservoir = RTXDI_LoadDIReservoir(
        reservoirParams,
        RTXDI_PixelPosToReservoirPos(uint2(previousPixel), runtimeParams.activeCheckerboardField),
        inputBufferIndex);
    if (!RTXDI_IsValidDIReservoir(previousReservoir))
    {
        return currentReservoir;
    }

    // The stored light index lives in the previous frame's light list; remap
    // it into the current list before replaying. An unmappable light
    // (despawned, reordered out) invalidates the history sample.
    const uint previousFrameLightIndex = RTXDI_GetDIReservoirLightIndex(previousReservoir);
    const int mappedLightIndex = RAB_TranslateLightIndex(previousFrameLightIndex, false);
    if (mappedLightIndex < 0)
    {
        return currentReservoir;
    }
    previousReservoir.lightData = RTXDI_DIReservoir_LightValidBit |
        ((uint)mappedLightIndex & RTXDI_DIReservoir_LightIndexMask);

    // Clamp accumulated confidence so stale history cannot dominate fresh
    // candidates (Bitterli 2020 Sec. 5: M_prev <= k * M_cur).
    previousReservoir.M = min(
        previousReservoir.M,
        (float)params.maxHistoryLength * max(currentReservoir.M, 1.0));

    // Replay the history sample at the current surface for its resampling
    // weight in the merge.
    const float2 previousSampleUv = RTXDI_GetDIReservoirSampleUV(previousReservoir);
    const RAB_LightInfo mappedLightInfo = RAB_LoadLightInfo((uint)mappedLightIndex, false);
    const RAB_LightSample previousSampleAtCurrent = RAB_SamplePolymorphicLight(mappedLightInfo, surface, previousSampleUv);
    float previousTargetAtCurrent = max(RAB_GetLightSampleTargetPdfForSurface(previousSampleAtCurrent, surface), 0.0);
    if (previousSampleAtCurrent.valid == 0u || previousSampleAtCurrent.solidAnglePdf <= 0.0)
    {
        previousTargetAtCurrent = 0.0;
    }

    temporalSamplePixel = previousPixel;

    RTXDI_DIReservoir temporalReservoir = RTXDI_EmptyDIReservoir();
    RTXDI_CombineDIReservoirs(temporalReservoir, currentReservoir, RTXDI_GetNextRandom(rng), currentReservoir.targetPdf);
    const bool selectedPrevious = RTXDI_CombineDIReservoirs(
        temporalReservoir, previousReservoir, RTXDI_GetNextRandom(rng), previousTargetAtCurrent);

    if (temporalReservoir.weightSum <= 0.0)
    {
        return currentReservoir;
    }

    // Normalization: 1/M, or 1/Z counting only domains where the selected
    // sample's target function is nonzero.
    float normalizationDenominator = max(temporalReservoir.M, 1.0);
    if (params.biasCorrectionMode >= RTXDI_BIAS_CORRECTION_BASIC)
    {
        const float selectedTargetAtCurrent = selectedPrevious ? previousTargetAtCurrent : currentReservoir.targetPdf;
        const float selectedTargetAtPrevious = RBPT_DITemporalTargetAtPreviousSurface(
            previousSurface,
            RTXDI_GetDIReservoirLightIndex(temporalReservoir),
            RTXDI_GetDIReservoirSampleUV(temporalReservoir));
        float normalizationZ = 0.0;
        if (selectedTargetAtCurrent > 0.0)
        {
            normalizationZ += currentReservoir.M;
        }
        if (selectedTargetAtPrevious > 0.0)
        {
            normalizationZ += previousReservoir.M;
        }
        normalizationDenominator = max(normalizationZ, 1.0);
    }
    RTXDI_FinalizeResampling(temporalReservoir, 1.0, normalizationDenominator);

    // RAY_TRACED: history-selected samples must also survive a conservative
    // visibility ray at the current surface; occluded ones are discarded
    // instead of leaking shadowed energy forward.
    if (selectedPrevious && params.biasCorrectionMode >= RTXDI_BIAS_CORRECTION_RAY_TRACED)
    {
        if (!RAB_GetConservativeVisibility(surface, previousSampleAtCurrent))
        {
            temporalReservoir.weightSum = 0.0;
        }
    }

    if (!RTXDI_IsValidDIReservoir(temporalReservoir))
    {
        return currentReservoir;
    }

    temporalReservoir.age = selectedPrevious
        ? min(previousReservoir.age + 1u, 0xffffu)
        : currentReservoir.age;
    if (selectedPrevious)
    {
        selectedLightSample = previousSampleAtCurrent;
    }
    return temporalReservoir;
}

#endif
