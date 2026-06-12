#ifndef RB_PATH_TRACING_CLEANROOM_COMMON_RESTIR_SHARED_SPATIAL_NEIGHBORS_HLSLI
#define RB_PATH_TRACING_CLEANROOM_COMMON_RESTIR_SHARED_SPATIAL_NEIGHBORS_HLSLI

#include "Rtxdi/Utils/RandomSamplerState.hlsli"

int2 RBPT_SpatialDiscNeighborDelta(
    inout RTXDI_RandomSamplerState rng,
    float samplingRadius,
    uint neighborOffsetMask)
{
    const uint offsetMask = min(neighborOffsetMask, 31u);
    const uint offsetIndex = ((uint)(RTXDI_GetNextRandom(rng) * 65536.0)) & offsetMask;
#ifdef RTXDI_NEIGHBOR_OFFSETS_BUFFER
    const float2 unitOffset = RTXDI_NEIGHBOR_OFFSETS_BUFFER[offsetIndex];
#else
    const float angle = RTXDI_GetNextRandom(rng) * 6.28318530718;
    const float2 unitOffset = float2(cos(angle), sin(angle));
#endif
    const float radius = max(1.0, sqrt(RTXDI_GetNextRandom(rng)) * max(samplingRadius, 1.0));
    return int2(round(unitOffset * radius));
}

#ifndef RBPT_SPATIAL_RECIPROCAL
#define RBPT_SPATIAL_RECIPROCAL 0
#endif

#if RBPT_SPATIAL_RECIPROCAL && \
    defined(RBPT_SPATIAL_REUSE_SIZE) && \
    defined(RBPT_SPATIAL_REUSE_DELTA) && \
    defined(RBPT_SPATIAL_REUSE_TRANSFORM_BITS)

uint2 RBPT_SpatialReciprocalTexel(uint slot, uint2 pixel, uint transformBits)
{
    const uint2 reuseSize = max(RBPT_SPATIAL_REUSE_SIZE(slot), uint2(1u, 1u));
    const uint combinedTransformBits = transformBits ^ RBPT_SPATIAL_REUSE_TRANSFORM_BITS;
    const uint2 offset = uint2(
        (combinedTransformBits >> 3) & 0xffu,
        (combinedTransformBits >> 11) & 0xffu);
    return (pixel + offset) % reuseSize;
}

int2 RBPT_SpatialApplyReciprocalTransform(int2 delta, uint transformBits)
{
    const uint combinedTransformBits = transformBits ^ RBPT_SPATIAL_REUSE_TRANSFORM_BITS;
    int2 transformed = delta;
    if ((combinedTransformBits & 0x4u) != 0u)
    {
        transformed = transformed.yx;
    }
    if ((combinedTransformBits & 0x1u) != 0u)
    {
        transformed.x = -transformed.x;
    }
    if ((combinedTransformBits & 0x2u) != 0u)
    {
        transformed.y = -transformed.y;
    }
    return transformed;
}

int2 RBPT_SpatialReciprocalNeighborDelta(uint slot, uint2 pixel, uint transformBits)
{
    const uint2 texel = RBPT_SpatialReciprocalTexel(slot, pixel, transformBits);
    return RBPT_SpatialApplyReciprocalTransform(
        RBPT_SPATIAL_REUSE_DELTA(slot, texel),
        transformBits);
}

#endif

#endif
