// Purpose:
//     Supplies rbdoom data for RTXDI/RAB emissive hit-to-light attribution.
//
// NVIDIA reference:
//     E:\prog\references\RTXDI-main\Samples\FullSample\Shaders\LightingPasses\RtxdiApplicationBridge\RAB_LightSampling.hlsli
//     E:\prog\references\RTXDI-main\Samples\FullSample\Shaders\LightingPasses\RtxdiApplicationBridge\PathTracer\RAB_MISCallbacks.hlsli
//     E:\prog\references\RTXDI-main\Libraries\Rtxdi\Include\Rtxdi\DI\InitialSampling.hlsli
//
// Current rbdoom supplier:
//     Current smoke emissive triangle records and RAB ray payload hit identity.
//
// Current deviation:
//     rbdoom uses a linear current-emissive scan instead of the reference
//     geometry-to-light table or RAB_TraceRayForLocalLight callback path.
//
// Extraction rule:
//     This file must preserve existing behavior until a later correction task.

#ifndef RB_PATH_TRACING_RAB_HIT_TO_LIGHT_INDEX_HLSLI
#define RB_PATH_TRACING_RAB_HIT_TO_LIGHT_INDEX_HLSLI

uint RAB_FindCurrentEmissiveLightIndexForRayPayload(RAB_RayPayload rayPayload)
{
    const uint emissiveTriangleCount = RAB_GetCurrentEmissiveTriangleCount();
    [loop]
    for (uint lightIndex = 0u; lightIndex < emissiveTriangleCount; ++lightIndex)
    {
        const PathTraceSmokeEmissiveTriangle emissiveTriangle = SmokeEmissiveTriangles[lightIndex];
        if (emissiveTriangle.instanceId == rayPayload.instanceId &&
            emissiveTriangle.primitiveIndex == rayPayload.primitiveId &&
            emissiveTriangle.materialId == rayPayload.materialId)
        {
            return lightIndex;
        }
    }

    return RAB_INVALID_LIGHT_INDEX;
}

#endif
