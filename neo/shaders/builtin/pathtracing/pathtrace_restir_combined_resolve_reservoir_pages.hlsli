// Purpose:
//     Supplies rbdoom data for ReSTIR PT reservoir page loading and reference
//     page debug readouts.
//
// NVIDIA reference:
//     E:\prog\references\RTXDI-main\Samples\FullSample\Shaders\LightingPasses\PT\FinalShading.hlsl
//     E:\prog\references\RTXDI-main\Libraries\Rtxdi\Include\Rtxdi\Utils\ReservoirAddressing.hlsli
//
// Current rbdoom supplier:
//     RTXDI reservoir buffer indices, reservoir addressing helpers, and local
//     reference-chain debug view helpers.
//
// Current deviation:
//     rbdoom reads several reference/debug reservoir pages from one combined
//     resolve shader instead of NVIDIA's small FinalShading.hlsl pass.
//
// Extraction rule:
//     This file must preserve existing behavior until a later correction task.

#ifndef RB_PATH_TRACING_RESTIR_COMBINED_RESOLVE_RESERVOIR_PAGES_HLSLI
#define RB_PATH_TRACING_RESTIR_COMBINED_RESOLVE_RESERVOIR_PAGES_HLSLI

RTXDI_PTReservoir LoadRestirPTSpatialOutputReservoir(uint2 pixel)
{
    const uint2 reservoirPosition = RTXDI_PixelPosToReservoirPos(pixel, 0u);
    return RTXDI_LoadPTReservoir(
        RestirPTParams.reservoirBuffer,
        reservoirPosition,
        RestirPTParams.bufferIndices.spatialResamplingOutputBufferIndex);
}

RTXDI_PTReservoir LoadRestirPTTemporalInputReservoir(uint2 pixel)
{
    const uint2 reservoirPosition = RTXDI_PixelPosToReservoirPos(pixel, 0u);
    return RTXDI_LoadPTReservoir(
        RestirPTParams.reservoirBuffer,
        reservoirPosition,
        RestirPTParams.bufferIndices.temporalResamplingInputBufferIndex);
}

RTXDI_PTReservoir LoadRestirPTTemporalOutputReservoir(uint2 pixel)
{
    const uint2 reservoirPosition = RTXDI_PixelPosToReservoirPos(pixel, 0u);
    return RTXDI_LoadPTReservoir(
        RestirPTParams.reservoirBuffer,
        reservoirPosition,
        RestirPTParams.bufferIndices.temporalResamplingOutputBufferIndex);
}

RTXDI_PTReservoir LoadRestirPTFinalShadingInputReservoir(uint2 pixel)
{
    const uint2 reservoirPosition = RTXDI_PixelPosToReservoirPos(pixel, 0u);
    return RTXDI_LoadPTReservoir(
        RestirPTParams.reservoirBuffer,
        reservoirPosition,
        RestirPTParams.bufferIndices.finalShadingInputBufferIndex);
}

RTXDI_PTReservoir LoadRestirPTInitialDirectReservoir(uint2 pixel)
{
    return RTXDI_LoadPTReservoir(
        RestirPTParams.reservoirBuffer,
        pixel,
        RestirPTParams.bufferIndices.initialPathTracerOutputBufferIndex);
}

RTXDI_PTReservoir LoadRestirPTInitialReservoir(uint2 pixel)
{
    pixel = PathTraceRestirPTIndirectRepresentativePixel(pixel);
    return RTXDI_LoadPTReservoir(
        RestirPTParams.reservoirBuffer,
        pixel,
        RestirPTParams.bufferIndices.initialPathTracerOutputBufferIndex);
}

uint RestirPTReferencePageQuadrant(uint2 pixel)
{
    const uint2 dimensions = PathTraceFullOutputSize();
    const bool right = pixel.x >= dimensions.x / 2u;
    const bool bottom = pixel.y >= dimensions.y / 2u;
    return (bottom ? 2u : 0u) + (right ? 1u : 0u);
}

RTXDI_PTReservoir LoadRestirPTReferencePageReservoir(uint2 reservoirPixel, uint page)
{
    if (page == 0u)
    {
        return LoadRestirPTInitialDirectReservoir(reservoirPixel);
    }
    if (page == 1u)
    {
        return LoadRestirPTTemporalOutputReservoir(reservoirPixel);
    }
    if (page == 2u)
    {
        return LoadRestirPTSpatialOutputReservoir(reservoirPixel);
    }
    return LoadRestirPTFinalShadingInputReservoir(reservoirPixel);
}

float4 EvaluateRestirPTReferencePageStateView(uint2 pixel)
{
    const uint2 reservoirPixel = PathTraceFullPixelToRestirDirectPixel(pixel);
    const uint page = RestirPTReferencePageQuadrant(pixel);
    const RTXDI_PTReservoir reservoir = LoadRestirPTReferencePageReservoir(reservoirPixel, page);
    return RestirPTReferenceFinalShadingStateColor(reservoir);
}

float4 EvaluateRestirPTReferencePageContributionView(uint2 pixel)
{
    const uint2 reservoirPixel = PathTraceFullPixelToRestirDirectPixel(pixel);
    const uint page = RestirPTReferencePageQuadrant(pixel);
    const RTXDI_PTReservoir reservoir = LoadRestirPTReferencePageReservoir(reservoirPixel, page);
    const float3 contribution = RestirPTReferenceFinalShadingHasUsefulSample(reservoir)
        ? RestirPTReferenceFinalShadingContribution(reservoir)
        : float3(0.0, 0.0, 0.0);
    return float4(RestirPTToneMapPreview(contribution), 1.0);
}

#endif
