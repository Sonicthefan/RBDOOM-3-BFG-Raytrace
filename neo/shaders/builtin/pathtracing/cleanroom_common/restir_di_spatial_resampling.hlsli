#ifndef RB_PATH_TRACING_CLEANROOM_COMMON_RESTIR_DI_SPATIAL_RESAMPLING_HLSLI
#define RB_PATH_TRACING_CLEANROOM_COMMON_RESTIR_DI_SPATIAL_RESAMPLING_HLSLI

#include "cleanroom_common/restir_di_parameters.hlsli"
#include "cleanroom_common/restir_di_reservoir.hlsli"
#include "cleanroom_common/restir_di_reservoir_storage.hlsli"
#include "cleanroom_common/restir_shared_spatial_neighbors.hlsli"
#include "cleanroom_common/restir_shared_math.hlsli"
#include "Rtxdi/Utils/RandomSamplerState.hlsli"

// rbdoom-owned DI spatial resampling, replacing Rtxdi/DI/SpatialResampling.hlsli.
// Math: streaming RIS over disc-sampled screen neighbors (Bitterli et al.
// 2020 Alg. 5) with 1/Z bias correction from the SIGGRAPH course notes.
// Neighbor selection goes through restir_shared_spatial_neighbors.hlsli,
// which also carries the ReSTIR PT Enhanced reciprocal reuse-texture hooks
// (paper Sec. 3): define RBPT_SPATIAL_RECIPROCAL plus the
// RBPT_SPATIAL_REUSE_* macros to switch neighbor pairing from the random
// disc to precomputed reciprocal pairs, mirroring the GI spatial lane.
//
// Required including-shader surface (inferred from repo call sites only):
//   RTXDI_LIGHT_RESERVOIR_BUFFER + Rtxdi/DI/ReservoirStorage.hlsli setup
//   RAB_GetGBufferSurface(int2 pixel, bool previousFrame)   (bounds-safe)
//   RAB_IsSurfaceValid / RAB_GetSurfaceNormal / RAB_GetSurfaceLinearDepth
//   RAB_LoadLightInfo / RAB_IsLightInfoValid
//   RAB_SamplePolymorphicLight / RAB_IsReplayableLightSample
//   RAB_GetLightSampleTargetPdfForSurface
//
// Scope notes:
//   - RAY_TRACED bias correction clamps to BASIC (the only caller, the remix
//     spatial contract, clamps before calling anyway; its header documents
//     ray-traced spatial bias correction as deferred).
//   - enableMaterialSimilarityTest uses the overridable macro below; the
//     default accepts all materials because the remix include chain does not
//     guarantee RAB_AreMaterialsSimilar. Lanes that have it can
//     #define RBPT_DI_SPATIAL_MATERIALS_SIMILAR(a, b) RAB_AreMaterialsSimilar(a, b)
//   - discountNaiveSamples is accepted but not implemented (no repo call
//     site documents its semantics; flag is recorded here for the reviewer).

#ifndef RTXDI_LIGHT_RESERVOIR_BUFFER
#error "RTXDI_DISpatialResampling requires RTXDI_LIGHT_RESERVOIR_BUFFER / Rtxdi/DI/ReservoirStorage.hlsli to be set up by the including shader"
#endif

#ifndef RBPT_DI_SPATIAL_MAX_SAMPLES
#define RBPT_DI_SPATIAL_MAX_SAMPLES 16u
#endif

#ifndef RBPT_DI_SPATIAL_MATERIALS_SIMILAR
#define RBPT_DI_SPATIAL_MATERIALS_SIMILAR(a, b) true
#endif

bool RBPT_DISpatialReplaySelectedSample(
    RTXDI_DIReservoir reservoir,
    RAB_Surface surface,
    out RAB_LightSample selectedLightSample)
{
    selectedLightSample = RAB_EmptyLightSample();
    if (!RAB_IsSurfaceValid(surface) || !RTXDI_IsValidDIReservoir(reservoir))
    {
        return false;
    }

    const RAB_LightInfo lightInfo = RAB_LoadLightInfo(
        RTXDI_GetDIReservoirLightIndex(reservoir),
        false);
    if (!RAB_IsLightInfoValid(lightInfo))
    {
        return false;
    }

    const RAB_LightSample lightSample = RAB_SamplePolymorphicLight(
        lightInfo,
        surface,
        RTXDI_GetDIReservoirSampleUV(reservoir));
    if (!RAB_IsReplayableLightSample(lightSample) || lightSample.solidAnglePdf <= 0.0)
    {
        return false;
    }

    selectedLightSample = lightSample;
    return true;
}

// Target function of an arbitrary (light, uv) sample at an arbitrary
// surface, used both for neighbor replay and the 1/Z denominator pass.
float RBPT_DISpatialTargetAtSurface(uint lightIndex, float2 sampleUv, RAB_Surface surface)
{
    const RAB_LightInfo lightInfo = RAB_LoadLightInfo(lightIndex, false);
    if (!RAB_IsLightInfoValid(lightInfo))
    {
        return 0.0;
    }

    const RAB_LightSample lightSample = RAB_SamplePolymorphicLight(lightInfo, surface, sampleUv);
    if (lightSample.valid == 0u || lightSample.solidAnglePdf <= 0.0)
    {
        return 0.0;
    }

    return max(RAB_GetLightSampleTargetPdfForSurface(lightSample, surface), 0.0);
}

bool RBPT_DISpatialNeighborSurfaceCompatible(
    RAB_Surface surface,
    RAB_Surface neighborSurface,
    RTXDI_DISpatialResamplingParameters params)
{
    if (!RAB_IsSurfaceValid(neighborSurface))
    {
        return false;
    }

    if (!RTXDI_IsValidNeighbor(
        RAB_GetSurfaceNormal(surface),
        RAB_GetSurfaceNormal(neighborSurface),
        RAB_GetSurfaceLinearDepth(surface),
        RAB_GetSurfaceLinearDepth(neighborSurface),
        params.normalThreshold,
        params.depthThreshold))
    {
        return false;
    }

    if (params.enableMaterialSimilarityTest != 0u &&
        !RBPT_DI_SPATIAL_MATERIALS_SIMILAR(surface, neighborSurface))
    {
        return false;
    }

    return true;
}

RTXDI_DIReservoir RTXDI_DISpatialResampling(
    uint2 pixelPosition,
    RAB_Surface surface,
    RTXDI_DIReservoir centerReservoir,
    inout RTXDI_RandomSamplerState rng,
    RTXDI_RuntimeParameters runtimeParams,
    RTXDI_ReservoirBufferParameters reservoirParams,
    uint inputBufferIndex,
    RTXDI_DISpatialResamplingParameters params,
    inout RAB_LightSample selectedLightSample)
{
    selectedLightSample = RAB_EmptyLightSample();

    if (!RAB_IsSurfaceValid(surface))
    {
        return centerReservoir;
    }

    RTXDI_DIReservoir center = centerReservoir;
    if (!RTXDI_IsValidDIReservoir(center))
    {
        center = RTXDI_LoadDIReservoir(
            reservoirParams,
            RTXDI_PixelPosToReservoirPos(pixelPosition, runtimeParams.activeCheckerboardField),
            inputBufferIndex);
    }

    // Disocclusion boost: starved history widens the search (same rule the
    // clean DI spatial shader applies in its local loop).
    uint sampleCount = params.numSamples;
    if (params.targetHistoryLength > 0u && center.M < (float)params.targetHistoryLength)
    {
        sampleCount = max(sampleCount, params.numDisocclusionBoostSamples);
    }
    sampleCount = min(sampleCount, RBPT_DI_SPATIAL_MAX_SAMPLES);

    if (sampleCount == 0u || params.samplingRadius <= 0.0)
    {
        if (RTXDI_IsValidDIReservoir(center))
        {
            RBPT_DISpatialReplaySelectedSample(center, surface, selectedLightSample);
        }
        return center;
    }

    // RAY_TRACED spatial bias correction is deferred; see scope notes.
    const uint biasCorrectionMode = min(params.biasCorrectionMode, uint(RTXDI_BIAS_CORRECTION_BASIC));

    RTXDI_DIReservoir spatialReservoir = RTXDI_EmptyDIReservoir();
    bool selectedFromCenter = false;
    if (RTXDI_IsValidDIReservoir(center))
    {
        selectedFromCenter = RTXDI_CombineDIReservoirs(
            spatialReservoir, center, RTXDI_GetNextRandom(rng), center.targetPdf);
    }

    int2 acceptedNeighborPixels[RBPT_DI_SPATIAL_MAX_SAMPLES];
    float acceptedNeighborM[RBPT_DI_SPATIAL_MAX_SAMPLES];
    uint acceptedNeighborCount = 0u;
    int selectedNeighborSlot = -1;

    [loop]
    for (uint sampleIndex = 0u; sampleIndex < sampleCount; ++sampleIndex)
    {
#if RBPT_SPATIAL_RECIPROCAL && defined(RBPT_SPATIAL_REUSE_SIZE) && defined(RBPT_SPATIAL_REUSE_DELTA) && defined(RBPT_SPATIAL_REUSE_TRANSFORM_BITS)
        const int2 neighborDelta = RBPT_SpatialReciprocalNeighborDelta(
            sampleIndex, pixelPosition, runtimeParams.frameIndex);
#else
        const int2 neighborDelta = RBPT_SpatialDiscNeighborDelta(
            rng, params.samplingRadius, runtimeParams.neighborOffsetMask);
#endif
        const int2 neighborPixel = int2(pixelPosition) + neighborDelta;
        if (all(neighborPixel == int2(pixelPosition)))
        {
            continue;
        }

        // The RAB surface loader is bounds-safe and returns invalid surfaces
        // off-screen, so this gate also rejects out-of-range neighbors before
        // any reservoir read.
        const RAB_Surface neighborSurface = RAB_GetGBufferSurface(neighborPixel, false);
        if (!RBPT_DISpatialNeighborSurfaceCompatible(surface, neighborSurface, params))
        {
            continue;
        }

        RTXDI_DIReservoir neighborReservoir = RTXDI_LoadDIReservoir(
            reservoirParams,
            RTXDI_PixelPosToReservoirPos(uint2(neighborPixel), runtimeParams.activeCheckerboardField),
            inputBufferIndex);
        if (!RTXDI_IsValidDIReservoir(neighborReservoir))
        {
            continue;
        }

        // Replay the neighbor's sample at the receiver surface; its weight in
        // the merge uses the receiver-domain target function.
        const float neighborTargetAtCenter = RBPT_DISpatialTargetAtSurface(
            RTXDI_GetDIReservoirLightIndex(neighborReservoir),
            RTXDI_GetDIReservoirSampleUV(neighborReservoir),
            surface);

        // Track provenance of the selected sample across passes.
        neighborReservoir.spatialDistance += neighborPixel - int2(pixelPosition);

        const bool selectedNeighbor = RTXDI_CombineDIReservoirs(
            spatialReservoir, neighborReservoir, RTXDI_GetNextRandom(rng), neighborTargetAtCenter);
        if (selectedNeighbor)
        {
            selectedNeighborSlot = (int)acceptedNeighborCount;
            selectedFromCenter = false;
        }

        acceptedNeighborPixels[acceptedNeighborCount] = neighborPixel;
        acceptedNeighborM[acceptedNeighborCount] = neighborReservoir.M;
        ++acceptedNeighborCount;
    }

    if (!RTXDI_IsValidDIReservoir(spatialReservoir) || spatialReservoir.weightSum <= 0.0)
    {
        return center;
    }

    // Normalization. OFF: plain 1/M. BASIC: 1/Z, counting each contributing
    // domain's M only where the selected sample's target function is nonzero.
    // The domain the winner actually came from is always counted - its
    // existence proves that domain produces the sample - which keeps a failed
    // re-evaluation from inflating W (the same defense as the temporal pass).
    float normalizationDenominator = max(spatialReservoir.M, 1.0);
    if (biasCorrectionMode >= RTXDI_BIAS_CORRECTION_BASIC && acceptedNeighborCount > 0u)
    {
        const uint selectedLightIndex = RTXDI_GetDIReservoirLightIndex(spatialReservoir);
        const float2 selectedUv = RTXDI_GetDIReservoirSampleUV(spatialReservoir);

        float normalizationZ = 0.0;
        if (RTXDI_IsValidDIReservoir(center))
        {
            // The winner has nonzero target at the receiver by construction,
            // and the center domain IS the receiver domain.
            normalizationZ += center.M;
        }

        [loop]
        for (uint neighborSlot = 0u; neighborSlot < acceptedNeighborCount; ++neighborSlot)
        {
            if ((int)neighborSlot == selectedNeighborSlot)
            {
                normalizationZ += acceptedNeighborM[neighborSlot];
                continue;
            }

            const RAB_Surface neighborSurface = RAB_GetGBufferSurface(acceptedNeighborPixels[neighborSlot], false);
            if (RBPT_DISpatialTargetAtSurface(selectedLightIndex, selectedUv, neighborSurface) > 0.0)
            {
                normalizationZ += acceptedNeighborM[neighborSlot];
            }
        }
        normalizationDenominator = max(normalizationZ, 1.0);
    }
    RTXDI_FinalizeResampling(spatialReservoir, 1.0, normalizationDenominator);

    if (!RTXDI_IsValidDIReservoir(spatialReservoir))
    {
        return center;
    }

    // Stored visibility only describes the sample at its source pixel; once a
    // neighbor's sample wins here it must not be reused at this receiver.
    if (!selectedFromCenter)
    {
        spatialReservoir.packedVisibility = 0u;
    }

    RBPT_DISpatialReplaySelectedSample(spatialReservoir, surface, selectedLightSample);
    return spatialReservoir;
}

#endif
