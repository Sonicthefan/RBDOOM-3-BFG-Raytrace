#ifndef RB_PATH_TRACING_REMIX_RAB_GI_TEMPORAL_VALIDATION_BRIDGE_HLSLI
#define RB_PATH_TRACING_REMIX_RAB_GI_TEMPORAL_VALIDATION_BRIDGE_HLSLI

// RTX Remix-shaped ReSTIR GI temporal target/validation bridge.
//
// This header owns callback names required by RTXDI GI temporal reuse and the
// Remix GI stale-sample validation boundary. The default implementation is
// deliberately non-functional: it rejects target/visibility/validation work
// rather than approximating Remix GI math.

#include "RAB_SurfaceBridge.hlsli"
#include "RAB_GIReservoirBridge.hlsli"
#include "RAB_GradientBridge.hlsli"

struct RemixRestirGITemporalValidationDesc
{
    uint enableLightingValidation;
    float lightingValidationThreshold;
    float gradientDepthTolerance;
    uint enableRayTracedVisibility;
    uint numActivePortals;
    float2 screenSpaceMotion;
    uint enableScreenSpaceValidation;
};

struct RemixRestirGITemporalValidationResult
{
    RTXDI_GIReservoir reservoir;
    uint staleSampleRejected;
    uint visibilityUpdated;
};

#ifndef REMIX_RAB_GI_TEMPORAL_VALIDATION_EXTERNAL_CALLBACKS
float RemixRAB_GetGISampleTargetPdfForSurface(float3 samplePosition, float3 sampleRadiance, RAB_Surface surface)
{
    return 0.0;
}

bool RemixRAB_ValidateGISampleWithJacobian(float jacobian)
{
    return false;
}

bool RemixRAB_GetTemporalConservativeVisibility(
    RAB_Surface currentSurface,
    RAB_Surface temporalSurface,
    float3 samplePosition)
{
    return false;
}

RemixRestirGITemporalValidationResult RemixRAB_ValidateGITemporalReservoir(
    RAB_Surface currentSurface,
    RTXDI_GIReservoir inputReservoir,
    RTXDI_GIReservoir resultReservoir,
    RemixRestirGITemporalValidationDesc desc)
{
    RemixRestirGITemporalValidationResult result = (RemixRestirGITemporalValidationResult)0;
    result.reservoir = resultReservoir;
    return result;
}
#else
// External-callback override point: the active driver shader provides the
// definitions after including this contract; prototypes keep the wrappers
// below compilable.
float RemixRAB_GetGISampleTargetPdfForSurface(float3 samplePosition, float3 sampleRadiance, RAB_Surface surface);
bool RemixRAB_ValidateGISampleWithJacobian(float jacobian);
bool RemixRAB_GetTemporalConservativeVisibility(
    RAB_Surface currentSurface,
    RAB_Surface temporalSurface,
    float3 samplePosition);
RemixRestirGITemporalValidationResult RemixRAB_ValidateGITemporalReservoir(
    RAB_Surface currentSurface,
    RTXDI_GIReservoir inputReservoir,
    RTXDI_GIReservoir resultReservoir,
    RemixRestirGITemporalValidationDesc desc);
#endif

float RAB_GetGISampleTargetPdfForSurface(float3 samplePosition, float3 sampleRadiance, RAB_Surface surface)
{
    return RemixRAB_GetGISampleTargetPdfForSurface(samplePosition, sampleRadiance, surface);
}

bool RAB_ValidateGISampleWithJacobian(float jacobian)
{
    return RemixRAB_ValidateGISampleWithJacobian(jacobian);
}
#define RBPT_HAS_GI_TEMPORAL_JACOBIAN_VALIDATION_CALLBACK 1

bool RAB_GetTemporalConservativeVisibility(
    RAB_Surface currentSurface,
    RAB_Surface temporalSurface,
    float3 samplePosition)
{
    return RemixRAB_GetTemporalConservativeVisibility(currentSurface, temporalSurface, samplePosition);
}

#endif
