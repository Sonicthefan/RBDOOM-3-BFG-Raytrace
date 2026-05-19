#ifndef RB_PATH_TRACING_REMIX_RAB_RESERVOIR_BRIDGE_HLSLI
#define RB_PATH_TRACING_REMIX_RAB_RESERVOIR_BRIDGE_HLSLI

// RTX Remix-shaped reservoir page bridge for future ReSTIR DI passes.
//
// This header deliberately exposes DI reservoir page access only. GI callbacks
// and GI final-shading bridge work are deferred to the dedicated GI slices.

#ifndef RTXDI_ENABLE_STORE_RESERVOIR
#define RTXDI_ENABLE_STORE_RESERVOIR 1
#endif

#define RTXDI_LIGHT_RESERVOIR_BUFFER RemixRAB_DIReservoirs

#include "Rtxdi/DI/ReservoirStorage.hlsli"
#include "Rtxdi/Utils/ReservoirAddressing.hlsli"

#ifndef REMIX_RAB_RESERVOIR_BRIDGE_EXTERNAL_BINDINGS
RWStructuredBuffer<RTXDI_PackedDIReservoir> RemixRAB_DIReservoirs;

cbuffer RemixRABReservoirBridgeConstants
{
    RTXDI_ReservoirBufferParameters RemixRAB_DIReservoirParams;
    uint4 RemixRAB_DIReservoirPageInfo;
};
#endif

uint RemixRAB_GetDIInitialOutputReservoirIndex()
{
    return RemixRAB_DIReservoirPageInfo.x;
}

uint RemixRAB_GetDITemporalInputReservoirIndex()
{
    return RemixRAB_DIReservoirPageInfo.y;
}

uint RemixRAB_GetDITemporalOutputReservoirIndex()
{
    return RemixRAB_DIReservoirPageInfo.z;
}

uint RemixRAB_GetDISpatialOutputReservoirIndex()
{
    return RemixRAB_DIReservoirPageInfo.w;
}

uint RemixRAB_DIReservoirAddress(uint2 reservoirPosition, uint reservoirArrayIndex)
{
    return RTXDI_ReservoirPositionToPointer(
        RemixRAB_DIReservoirParams,
        reservoirPosition,
        reservoirArrayIndex);
}

RTXDI_PackedDIReservoir RemixRAB_LoadPackedDIReservoir(uint2 reservoirPosition, uint reservoirArrayIndex)
{
    return RemixRAB_DIReservoirs[RemixRAB_DIReservoirAddress(reservoirPosition, reservoirArrayIndex)];
}

void RemixRAB_StorePackedDIReservoir(
    RTXDI_PackedDIReservoir reservoir,
    uint2 reservoirPosition,
    uint reservoirArrayIndex)
{
    RemixRAB_DIReservoirs[RemixRAB_DIReservoirAddress(reservoirPosition, reservoirArrayIndex)] = reservoir;
}

RTXDI_DIReservoir RemixRAB_LoadDIReservoir(uint2 reservoirPosition, uint reservoirArrayIndex)
{
    return RTXDI_UnpackDIReservoir(RemixRAB_LoadPackedDIReservoir(reservoirPosition, reservoirArrayIndex));
}

void RemixRAB_StoreDIReservoir(
    RTXDI_DIReservoir reservoir,
    uint2 reservoirPosition,
    uint reservoirArrayIndex)
{
    RemixRAB_StorePackedDIReservoir(RTXDI_PackDIReservoir(reservoir), reservoirPosition, reservoirArrayIndex);
}

RTXDI_DIReservoir RAB_LoadReservoir(int2 pixel, int page)
{
    if (any(pixel < int2(0, 0)) || page < 0)
    {
        return RTXDI_EmptyDIReservoir();
    }
    return RemixRAB_LoadDIReservoir(uint2(pixel), uint(page));
}

void RAB_StoreReservoir(RTXDI_DIReservoir reservoir, int2 pixel, int page)
{
    if (any(pixel < int2(0, 0)) || page < 0)
    {
        return;
    }
    RemixRAB_StoreDIReservoir(reservoir, uint2(pixel), uint(page));
}

#endif
