#ifndef RB_PATH_TRACING_REMIX_RAB_GRADIENT_BRIDGE_HLSLI
#define RB_PATH_TRACING_REMIX_RAB_GRADIENT_BRIDGE_HLSLI

// RTX Remix-shaped gradient, best-light, and confidence output bridge.
//
// RRX-07 keeps these shaders inactive. The active dispatch slice must provide
// real image bindings for best-light history, gradients, filtered gradients,
// confidence history, hit distance, luminance inputs, and the RTXDI/Remix
// gradient/confidence helpers before enabling them.

#include "RAB_TemporalOutputBridge.hlsli"

static const uint REMIX_RTXDI_GRAD_FACTOR = 3u;
static const uint REMIX_RTXDI_INVALID_LIGHT_INDEX = 0xffffffffu;
static const uint REMIX_RTXDI_INVALID_PORTAL_INDEX = 0xffffffffu;

struct RemixRtxdiBestLightPayload
{
    uint lightIndex;
    uint portalIndex;
};

struct RemixRtxdiGradientPayload
{
    float normalizedGradient;
    float referenceHitDistance;
};

struct RemixRtxdiConfidencePayload
{
    float confidence;
};

#ifndef REMIX_RAB_GRADIENT_BRIDGE_EXTERNAL_CALLBACKS
float RemixRAB_LoadCurrentIlluminance(int2 pixel)
{
    return 0.0;
}

float RemixRAB_LoadPreviousIlluminance(int2 pixel)
{
    return 0.0;
}

float RemixRAB_LoadCurrentHitDistance(int2 pixel)
{
    return 0.0;
}

float RemixRAB_LoadPreviousConfidence(int2 pixel)
{
    return 1.0;
}

void RemixRAB_StoreBestLight(uint2 gradientPixel, RemixRtxdiBestLightPayload payload)
{
}

void RemixRAB_StoreGradient(uint2 gradientPixel, RemixRtxdiGradientPayload payload, uint bufferIndex)
{
}

RemixRtxdiGradientPayload RemixRAB_LoadGradient(uint2 gradientPixel, uint bufferIndex)
{
    return (RemixRtxdiGradientPayload)0;
}

void RemixRAB_StoreConfidence(uint2 pixel, RemixRtxdiConfidencePayload payload)
{
}
#endif

RemixRtxdiBestLightPayload RemixRtxdiBuildInvalidBestLight()
{
    RemixRtxdiBestLightPayload payload = (RemixRtxdiBestLightPayload)0;
    payload.lightIndex = REMIX_RTXDI_INVALID_LIGHT_INDEX;
    payload.portalIndex = REMIX_RTXDI_INVALID_PORTAL_INDEX;
    return payload;
}

RemixRtxdiGradientPayload RemixRtxdiBuildEmptyGradient()
{
    return (RemixRtxdiGradientPayload)0;
}

RemixRtxdiConfidencePayload RemixRtxdiBuildConfidence(float confidence)
{
    RemixRtxdiConfidencePayload payload = (RemixRtxdiConfidencePayload)0;
    payload.confidence = saturate(confidence);
    return payload;
}

#endif
