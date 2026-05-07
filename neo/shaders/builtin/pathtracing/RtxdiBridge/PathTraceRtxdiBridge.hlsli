#ifndef RB_PATH_TRACING_RTXDI_BRIDGE_HLSLI
#define RB_PATH_TRACING_RTXDI_BRIDGE_HLSLI

// Local RBDoom3 bridge types for future RTXDI/ReSTIR PT RAB callbacks.
// This file intentionally declares only thin data wrappers. Surface loading,
// material evaluation, visibility, and RAB_PathTrace are added in later steps.

#include "RAB_Material.hlsli"
#include "RAB_Surface.hlsli"
#include "RAB_RayPayload.hlsli"
#include "RAB_LightSample.hlsli"
#include "RAB_LightInfo.hlsli"
#include "RAB_RandomSamplerState.hlsli"
#include "RAB_LightSampling.hlsli"
#include "RAB_MISCallbacks.hlsli"
#include "PathTracer/RAB_PathTracerUserData.hlsli"

#endif
