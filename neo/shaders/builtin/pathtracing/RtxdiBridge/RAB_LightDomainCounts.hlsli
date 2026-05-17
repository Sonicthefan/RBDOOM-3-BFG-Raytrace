// Purpose:
//     Supplies rbdoom data for RTXDI/RAB light-domain counts.
//
// NVIDIA reference:
//     E:\prog\references\RTXDI-main\Samples\FullSample\Shaders\LightingPasses\RtxdiApplicationBridge\RAB_LightInfo.hlsli
//     E:\prog\references\RTXDI-main\Samples\FullSample\Shaders\LightingPasses\RtxdiApplicationBridge\RAB_LightSampling.hlsli
//
// Current rbdoom supplier:
//     Smoke emissive triangle uploads, Doom analytic light uploads, and local
//     safety-disable flags.
//
// Current deviation:
//     rbdoom derives a combined light domain from local emissive and Doom
//     analytic uploads instead of RTXDI light buffer constants and PDF textures.
//
// Extraction rule:
//     This file must preserve existing behavior until a later correction task.

#ifndef RB_PATH_TRACING_RAB_LIGHT_DOMAIN_COUNTS_HLSLI
#define RB_PATH_TRACING_RAB_LIGHT_DOMAIN_COUNTS_HLSLI

uint RAB_GetCurrentEmissiveTriangleCount()
{
    return PathTraceSafetyDisabled(RT_PT_SAFETY_DISABLE_EMISSIVE_TRIANGLE_SAMPLING) ? 0u : (uint)max(EmissiveInfo.x, 0.0);
}

uint RAB_GetPreviousEmissiveTriangleCount()
{
    return PathTraceSafetyDisabled(RT_PT_SAFETY_DISABLE_EMISSIVE_TRIANGLE_SAMPLING) ? 0u : (uint)max(DoomAnalyticLightRemapInfo.w, 0.0);
}

uint RAB_GetCurrentDoomAnalyticLightCount()
{
    const uint uploadedAnalyticCount = PathTraceSafetyDisabled(RT_PT_SAFETY_DISABLE_ANALYTIC_LIGHT_LOOP) ? 0u : (uint)max(DoomAnalyticLightInfo.x, 0.0);
    const uint analyticTraceCap = (uint)max(DoomAnalyticLightInfo.y, 0.0);
    return (((uint)max(DoomAnalyticLightInfo.w, 0.0)) & 1u) != 0u && !PathTraceSafetyDisabled(RT_PT_SAFETY_DISABLE_ANALYTIC_LIGHT_LOOP) ? min(uploadedAnalyticCount, analyticTraceCap) : 0u;
}

uint RAB_GetCurrentLightCount()
{
    return RAB_GetCurrentEmissiveTriangleCount() + RAB_GetCurrentDoomAnalyticLightCount();
}

#endif
