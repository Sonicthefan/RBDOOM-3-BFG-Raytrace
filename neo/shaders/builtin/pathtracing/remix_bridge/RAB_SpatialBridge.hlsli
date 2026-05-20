#ifndef RB_PATH_TRACING_REMIX_RAB_SPATIAL_BRIDGE_HLSLI
#define RB_PATH_TRACING_REMIX_RAB_SPATIAL_BRIDGE_HLSLI

// RTXDI DI spatial reuse needs a neighbor-offset table and a viewport clamp.
// The active dispatch slice must replace these fallback callbacks/resources
// before enabling this path. The defaults keep this inactive contract
// self-contained without claiming a real resource binding.

#ifndef REMIX_RAB_SPATIAL_BRIDGE_EXTERNAL_RESOURCES
static const float2 RemixRAB_DefaultNeighborOffsets[32] =
{
    float2( 0.125,  0.375), float2(-0.250,  0.625),
    float2( 0.500, -0.125), float2(-0.625, -0.375),
    float2( 0.875,  0.125), float2(-0.875,  0.250),
    float2( 0.250, -0.875), float2(-0.125, -0.625),
    float2( 0.625,  0.625), float2(-0.500,  0.875),
    float2( 0.375, -0.500), float2(-0.750, -0.125),
    float2( 0.125,  0.875), float2(-0.375,  0.125),
    float2( 0.750, -0.625), float2(-0.875, -0.750),
    float2( 0.375,  0.250), float2(-0.125,  0.500),
    float2( 0.625, -0.875), float2(-0.625, -0.250),
    float2( 0.875,  0.750), float2(-0.250,  0.875),
    float2( 0.500, -0.375), float2(-0.500, -0.625),
    float2( 0.250,  0.625), float2(-0.750,  0.375),
    float2( 0.750, -0.125), float2(-0.125, -0.875),
    float2( 0.125,  0.125), float2(-0.375,  0.750),
    float2( 0.875, -0.500), float2(-0.875, -0.375)
};

#define RTXDI_NEIGHBOR_OFFSETS_BUFFER RemixRAB_DefaultNeighborOffsets
#endif

#ifndef REMIX_RAB_SPATIAL_BRIDGE_EXTERNAL_CALLBACKS
int2 RAB_ClampSamplePositionIntoView(int2 pixelPosition, bool previousFrame)
{
    return pixelPosition;
}
#endif

#endif
