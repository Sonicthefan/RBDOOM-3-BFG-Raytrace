#ifndef RB_PATH_TRACING_CLEANROOM_COMMON_RESTIR_PT_PATH_TRACER_RANDOM_CONTEXT_HLSLI
#define RB_PATH_TRACING_CLEANROOM_COMMON_RESTIR_PT_PATH_TRACER_RANDOM_CONTEXT_HLSLI

#include "Rtxdi/Utils/RandomSamplerState.hlsli"

#ifndef RTXDI_TILE_SIZE_IN_PIXELS
#define RTXDI_TILE_SIZE_IN_PIXELS 8
#endif

struct RTXDI_PathTracerRandomContext
{
    RTXDI_RandomSamplerState initialRandomSamplerState;
    RTXDI_RandomSamplerState initialCoherentRandomSamplerState;
    RTXDI_RandomSamplerState replayRandomSamplerState;
};

RTXDI_PathTracerRandomContext RTXDI_InitializePathTracerRandomContext(
    uint2 pixel,
    uint frameIndex,
    uint initialDimensionBase,
    uint replayDimensionBase)
{
    RTXDI_PathTracerRandomContext result;
    result.initialRandomSamplerState = RTXDI_InitRandomSampler(pixel, frameIndex, initialDimensionBase);
    result.initialCoherentRandomSamplerState = RTXDI_InitRandomSampler(pixel / RTXDI_TILE_SIZE_IN_PIXELS, frameIndex, initialDimensionBase);
    result.replayRandomSamplerState = RTXDI_InitRandomSampler(pixel, frameIndex, replayDimensionBase);
    return result;
}

#endif
