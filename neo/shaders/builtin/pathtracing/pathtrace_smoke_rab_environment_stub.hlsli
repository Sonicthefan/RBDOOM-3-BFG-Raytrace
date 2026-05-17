// Purpose:
//     Documents rbdoom's missing environment-light supplier boundary for
//     RTXDI/RAB path tracing.
//
// NVIDIA reference:
//     E:\prog\references\RTXDI-main\Samples\FullSample\Shaders\LightingPasses\RtxdiApplicationBridge\RAB_LightSampling.hlsli
//     E:\prog\references\RTXDI-main\Samples\FullSample\Shaders\LightingPasses\RtxdiApplicationBridge\PathTracer\RAB_MISCallbacks.hlsli
//
// Current rbdoom supplier:
//     None. pathtrace_smoke.rt.hlsl has miss/debug color paths, but no
//     environment radiance, PDF texture, presampling, or environment light
//     records.
//
// Current deviation:
//     Environment misses are treated as unbridged misses by the RAB path
//     tracer. Environment MIS callbacks remain stubs in RAB_MISCallbacks.hlsli.
//
// Extraction rule:
//     This file must preserve existing behavior until a later correction task.
//     Do not add environment radiance, PDFs, presampling, light records, or MIS
//     behavior here.

#ifndef PATHTRACE_SMOKE_RAB_ENVIRONMENT_STUB_HLSLI
#define PATHTRACE_SMOKE_RAB_ENVIRONMENT_STUB_HLSLI

// Intentionally empty behavior boundary.

#endif
