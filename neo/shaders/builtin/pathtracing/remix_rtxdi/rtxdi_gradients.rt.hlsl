#ifndef RB_PATH_TRACING_REMIX_RTXDI_GRADIENTS_RT_HLSL
#define RB_PATH_TRACING_REMIX_RTXDI_GRADIENTS_RT_HLSL

// RTX Remix-shaped gradient/best-light/confidence payload contract.
//
// The compute, filter, and confidence passes are intentionally deferred. This
// file freezes the rbdoom-side payloads they must produce and consume so the
// GI validation slice has a stable contract without inheriting old reservoir
// logic or hand-rolled Remix math.

#include "../remix_bridge/RAB_ReservoirBridge.hlsli"

static const uint REMIX_RTXDI_INVALID_PORTAL_INDEX = 0xffffffffu;

struct RemixRtxdiBestLightPayload
{
    uint lightIndex;
    uint portalIndex;
};

struct RemixRtxdiGradientPayload
{
    float normalizedGradient;
    float referenceIlluminance;
};

struct RemixRtxdiConfidencePayload
{
    float confidence;
};

struct RemixRtxdiGradientDesc
{
    uint2 pixel;
    uint2 gradientPixel;
    uint spatialReservoirPage;
    uint previousReservoirPage;
    uint currentReservoirPage;
    float darknessBias;
};

RemixRtxdiBestLightPayload RemixRtxdiBuildInvalidBestLight()
{
    RemixRtxdiBestLightPayload payload = (RemixRtxdiBestLightPayload)0;
    payload.lightIndex = RTXDI_INVALID_LIGHT_INDEX;
    payload.portalIndex = REMIX_RTXDI_INVALID_PORTAL_INDEX;
    return payload;
}

RemixRtxdiGradientPayload RemixRtxdiBuildEmptyGradient()
{
    return (RemixRtxdiGradientPayload)0;
}

RemixRtxdiConfidencePayload RemixRtxdiBuildEmptyConfidence()
{
    RemixRtxdiConfidencePayload payload = (RemixRtxdiConfidencePayload)0;
    payload.confidence = 1.0;
    return payload;
}

RemixRtxdiBestLightPayload RemixRtxdiReadBestLightFromSpatialReservoir(RemixRtxdiGradientDesc desc)
{
    RemixRtxdiBestLightPayload payload = RemixRtxdiBuildInvalidBestLight();
    RTXDI_DIReservoir reservoir = RAB_LoadReservoir(int2(desc.pixel), int(desc.spatialReservoirPage));

    if (RTXDI_IsValidDIReservoir(reservoir))
    {
        payload.lightIndex = RTXDI_GetDIReservoirLightIndex(reservoir);
        payload.portalIndex = 0u;
    }

    return payload;
}

#endif
