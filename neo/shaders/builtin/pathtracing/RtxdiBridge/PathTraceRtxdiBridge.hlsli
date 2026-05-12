#ifndef RB_PATH_TRACING_RTXDI_BRIDGE_HLSLI
#define RB_PATH_TRACING_RTXDI_BRIDGE_HLSLI

// Compatibility umbrella for RTXDI/ReSTIR PT RAB callbacks. New pass-local
// shaders should prefer PathTraceRtxdiBridgeCore.hlsli plus the specific RAB
// capability files they need, instead of pulling this whole callback surface.

#include "PathTraceRtxdiBridgeCore.hlsli"
#include "RAB_Brdf.hlsli"
#include "RAB_LightInfo.hlsli"
#include "RAB_RandomSamplerState.hlsli"
#include "RAB_LightSampling.hlsli"
#include "RAB_MISCallbacks.hlsli"
#include "PathTracer/RAB_PathTracerUserData.hlsli"

#endif
