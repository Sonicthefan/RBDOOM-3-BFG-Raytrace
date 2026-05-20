#ifndef RB_PATH_TRACING_REMIX_RAB_GI_RESERVOIR_BRIDGE_HLSLI
#define RB_PATH_TRACING_REMIX_RAB_GI_RESERVOIR_BRIDGE_HLSLI

// RTX Remix-shaped GI reservoir page bridge for future ReSTIR GI passes.
//
// This header owns only the packed RTXDI GI reservoir ABI and callback-shaped
// page access. GI initial-sample construction, temporal validation, visibility,
// spatial reuse, and final shading are deliberately left to their file-
// equivalent slices.

#ifndef RTXDI_ENABLE_STORE_RESERVOIR
#define RTXDI_ENABLE_STORE_RESERVOIR 1
#endif

#define RTXDI_GI_RESERVOIR_BUFFER RemixRAB_GIReservoirs

#include "Rtxdi/GI/Reservoir.hlsli"
#include "Rtxdi/Utils/ReservoirAddressing.hlsli"

#ifndef REMIX_RAB_GI_RESERVOIR_BRIDGE_EXTERNAL_BINDINGS
RWStructuredBuffer<RTXDI_PackedGIReservoir> RemixRAB_GIReservoirs;

cbuffer RemixRABGIReservoirBridgeConstants
{
    RTXDI_ReservoirBufferParameters RemixRAB_GIReservoirParams;
    uint4 RemixRAB_GIReservoirPageInfo;
};
#endif

uint RemixRAB_GetGIInitSampleReservoirIndex()
{
    return RemixRAB_GIReservoirPageInfo.x;
}

uint RemixRAB_GetGITemporalInputReservoirIndex()
{
    return RemixRAB_GIReservoirPageInfo.y;
}

uint RemixRAB_GetGITemporalOutputReservoirIndex()
{
    return RemixRAB_GIReservoirPageInfo.z;
}

uint RemixRAB_GetGISpatialOutputReservoirIndex()
{
    return RemixRAB_GIReservoirPageInfo.w;
}

uint RemixRAB_GIReservoirAddress(uint2 reservoirPosition, uint reservoirArrayIndex)
{
    return RTXDI_ReservoirPositionToPointer(
        RemixRAB_GIReservoirParams,
        reservoirPosition,
        reservoirArrayIndex);
}

RTXDI_PackedGIReservoir RemixRAB_LoadPackedGIReservoir(uint2 reservoirPosition, uint reservoirArrayIndex)
{
    return RemixRAB_GIReservoirs[RemixRAB_GIReservoirAddress(reservoirPosition, reservoirArrayIndex)];
}

void RemixRAB_StorePackedGIReservoir(
    RTXDI_PackedGIReservoir reservoir,
    uint2 reservoirPosition,
    uint reservoirArrayIndex)
{
    RemixRAB_GIReservoirs[RemixRAB_GIReservoirAddress(reservoirPosition, reservoirArrayIndex)] = reservoir;
}

RTXDI_GIReservoir RemixRAB_LoadGIReservoir(uint2 reservoirPosition, uint reservoirArrayIndex)
{
    return RTXDI_UnpackGIReservoir(RemixRAB_LoadPackedGIReservoir(reservoirPosition, reservoirArrayIndex));
}

void RemixRAB_StoreGIReservoir(
    RTXDI_GIReservoir reservoir,
    uint2 reservoirPosition,
    uint reservoirArrayIndex)
{
    RemixRAB_StorePackedGIReservoir(
        RTXDI_PackGIReservoir(reservoir, 0u),
        reservoirPosition,
        reservoirArrayIndex);
}

RTXDI_GIReservoir RAB_LoadGIReservoir(int2 pixel, int page)
{
    if (any(pixel < int2(0, 0)) || page < 0)
    {
        return RTXDI_EmptyGIReservoir();
    }
    return RemixRAB_LoadGIReservoir(uint2(pixel), uint(page));
}

void RAB_StoreGIReservoir(RTXDI_GIReservoir reservoir, int2 pixel, int page)
{
    if (any(pixel < int2(0, 0)) || page < 0)
    {
        return;
    }
    RemixRAB_StoreGIReservoir(reservoir, uint2(pixel), uint(page));
}

#endif
