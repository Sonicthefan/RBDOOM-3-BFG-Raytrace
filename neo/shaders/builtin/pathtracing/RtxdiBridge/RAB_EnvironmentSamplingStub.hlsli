// Purpose:
//     Supplies rbdoom stubs for RTXDI/RAB environment sampling callbacks.
//
// NVIDIA reference:
//     E:\prog\references\RTXDI-main\Samples\FullSample\Shaders\LightingPasses\RtxdiApplicationBridge\RAB_LightSampling.hlsli
//     E:\prog\references\RTXDI-main\Samples\FullSample\Shaders\LightingPasses\RtxdiApplicationBridge\PathTracer\RAB_MISCallbacks.hlsli
//
// Current rbdoom supplier:
//     No environment light records, radiance, or PDF textures are bridged.
//
// Current deviation:
//     rbdoom intentionally returns a zero environment PDF while retaining a
//     direction-to-environment UV helper for existing call sites.
//
// Extraction rule:
//     This file must preserve existing behavior until a later correction task.

#ifndef RB_PATH_TRACING_RAB_ENVIRONMENT_SAMPLING_STUB_HLSLI
#define RB_PATH_TRACING_RAB_ENVIRONMENT_SAMPLING_STUB_HLSLI

float RAB_EvaluateEnvironmentMapSamplingPdf(float3 direction)
{
    // Environment lighting is intentionally unbridged; see pathtrace_smoke_rab_environment_stub.hlsli.
    return 0.0;
}

float2 RAB_GetEnvironmentMapRandXYFromDir(float3 direction)
{
    const float3 dir = RAB_SafeNormalize(direction, float3(0.0, 0.0, 1.0));
    const float invTwoPi = 0.15915494309189535;
    const float invPi = 0.3183098861837907;
    const float u = atan2(dir.y, dir.x) * invTwoPi + 0.5;
    const float v = acos(clamp(dir.z, -1.0, 1.0)) * invPi;
    return frac(float2(u, v));
}

#endif
