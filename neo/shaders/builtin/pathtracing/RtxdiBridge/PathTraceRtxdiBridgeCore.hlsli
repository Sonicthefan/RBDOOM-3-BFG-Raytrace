#ifndef RB_PATH_TRACING_RTXDI_BRIDGE_CORE_HLSLI
#define RB_PATH_TRACING_RTXDI_BRIDGE_CORE_HLSLI

// Minimal RAB surface/material/light record boundary. Use this from shaders
// that need shared primary-surface types but do not need BRDF, light sampling,
// visibility, MIS, or path-tracer callbacks.

#include "RAB_Material.hlsli"
#include "RAB_SurfaceCore.hlsli"
#include "RAB_RayPayload.hlsli"
#include "RAB_LightSample.hlsli"
#include "RAB_LightInfoCore.hlsli"

#endif
