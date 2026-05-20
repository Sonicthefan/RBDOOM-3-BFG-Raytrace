#ifndef RB_PATH_TRACING_REMIX_RTXDI_GRADIENTS_RT_HLSL
#define RB_PATH_TRACING_REMIX_RTXDI_GRADIENTS_RT_HLSL

// RTX Remix-shaped gradient/best-light/confidence contract.
//
// The active compute/filter/confidence dispatches are intentionally deferred.
// This inactive file freezes the rbdoom-side products that later GI validation
// consumes. It does not implement gradient selection, visibility
// re-evaluation, A-trous filtering, or confidence conversion; those must be
// provided by a corrective implementation slice using the Remix/RTXDI helpers.

#include "../remix_bridge/RAB_ReservoirBridge.hlsli"
#include "../remix_bridge/RAB_GradientBridge.hlsli"

struct RemixRtxdiGradientDesc
{
    uint2 pixel;
    uint2 gradientPixel;
    uint spatialReservoirPage;
    uint previousReservoirPage;
    uint currentReservoirPage;
    uint usePreviousIlluminance;
    uint computeGradients;
};

struct RemixRtxdiGradientFilterDesc
{
    uint2 gradientPixel;
    uint inputBufferIndex;
    uint outputBufferIndex;
    uint passIndex;
};

struct RemixRtxdiConfidenceDesc
{
    uint2 pixel;
    uint inputGradientBufferIndex;
    uint2 resolution;
};

RemixRtxdiBestLightPayload RemixRtxdiReadBestLightFromSpatialReservoir(RemixRtxdiGradientDesc desc)
{
    RemixRtxdiBestLightPayload payload = RemixRtxdiBuildInvalidBestLight();
    RTXDI_DIReservoir reservoir = RAB_LoadReservoir(int2(desc.pixel), int(desc.spatialReservoirPage));

    if (RTXDI_IsValidDIReservoir(reservoir))
    {
        payload.lightIndex = RTXDI_GetDIReservoirLightIndex(reservoir);
        payload.portalIndex = REMIX_RTXDI_INVALID_PORTAL_INDEX;
    }

    return payload;
}

void RemixRtxdiRunGradientContract(RemixRtxdiGradientDesc desc)
{
    RemixRtxdiBestLightPayload bestLight = RemixRtxdiReadBestLightFromSpatialReservoir(desc);
    RemixRAB_StoreBestLight(desc.gradientPixel, bestLight);

    // Contract only. The real compute pass must:
    // - call the RTXDI/Remix gradient sample selection helper,
    // - choose current or previous reservoir input,
    // - translate the selected light into the opposite frame,
    // - reject unsupported portal-domain samples,
    // - re-evaluate selected-light visibility/illuminance,
    // - write the resulting gradient payload through RemixRAB_StoreGradient.
}

void RemixRtxdiRunFilterGradientsContract(RemixRtxdiGradientFilterDesc desc)
{
    // Contract only. The real filter pass must call the RTXDI/Remix A-trous
    // gradient filtering helper with input/output gradient buffers.
}

void RemixRtxdiRunConfidenceContract(RemixRtxdiConfidenceDesc desc)
{
    // Contract only. The real confidence pass must call
    // RTXDI_ConvertGradientsToConfidence or its Remix equivalent and store the
    // resulting confidence through RemixRAB_StoreConfidence.
}

#endif
