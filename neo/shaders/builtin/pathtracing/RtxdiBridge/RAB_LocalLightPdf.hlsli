// Purpose:
//     Supplies rbdoom data for RTXDI/RAB local-light source PDF evaluation.
//
// NVIDIA reference:
//     E:\prog\references\RTXDI-main\Samples\FullSample\Shaders\LightingPasses\RtxdiApplicationBridge\RAB_LightSampling.hlsli
//     E:\prog\references\RTXDI-main\Samples\FullSample\Shaders\LightingPasses\RtxdiApplicationBridge\PathTracer\RAB_MISCallbacks.hlsli
//
// Current rbdoom supplier:
//     Smoke emissive triangle sampleWeightAndPdf values and current emissive
//     light-domain counts.
//
// Current deviation:
//     rbdoom does not have the reference local-light PDF texture path here; it
//     uses the uploaded emissive triangle PDF with a uniform fallback.
//
// Extraction rule:
//     This file must preserve existing behavior until a later correction task.

#ifndef RB_PATH_TRACING_RAB_LOCAL_LIGHT_PDF_HLSLI
#define RB_PATH_TRACING_RAB_LOCAL_LIGHT_PDF_HLSLI

float RAB_EvaluateLocalLightSourcePdf(uint lightIndex)
{
    const uint emissiveTriangleCount = RAB_GetCurrentEmissiveTriangleCount();
    if (lightIndex >= emissiveTriangleCount || emissiveTriangleCount == 0u)
    {
        return 0.0;
    }

    const PathTraceSmokeEmissiveTriangle emissiveTriangle = SmokeEmissiveTriangles[lightIndex];
    const float uniformFallbackPdf = 1.0 / max((float)emissiveTriangleCount, 1.0);
    return max(emissiveTriangle.sampleWeightAndPdf.y, uniformFallbackPdf);
}

#endif
