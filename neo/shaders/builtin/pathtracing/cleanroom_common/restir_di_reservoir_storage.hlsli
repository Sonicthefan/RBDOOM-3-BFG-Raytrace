#ifndef RB_PATH_TRACING_CLEANROOM_COMMON_RESTIR_DI_RESERVOIR_STORAGE_HLSLI
#define RB_PATH_TRACING_CLEANROOM_COMMON_RESTIR_DI_RESERVOIR_STORAGE_HLSLI

#include "cleanroom_common/restir_di_reservoir.hlsli"

#ifndef RTXDI_LIGHT_RESERVOIR_BUFFER
#error "RTXDI_LIGHT_RESERVOIR_BUFFER must name the active DI packed-reservoir buffer before including ReservoirStorage.hlsli"
#endif

RTXDI_DIReservoir RTXDI_LoadDIReservoir(
    RTXDI_ReservoirBufferParameters reservoirParams,
    uint2 reservoirPosition,
    uint reservoirArrayIndex)
{
    const uint pointer = RTXDI_ReservoirPositionToPointer(
        reservoirParams,
        reservoirPosition,
        reservoirArrayIndex);
    return RTXDI_UnpackDIReservoir(RTXDI_LIGHT_RESERVOIR_BUFFER[pointer]);
}

void RTXDI_StoreDIReservoir(
    RTXDI_DIReservoir reservoir,
    RTXDI_ReservoirBufferParameters reservoirParams,
    uint2 reservoirPosition,
    uint reservoirArrayIndex)
{
    const uint pointer = RTXDI_ReservoirPositionToPointer(
        reservoirParams,
        reservoirPosition,
        reservoirArrayIndex);
    RTXDI_LIGHT_RESERVOIR_BUFFER[pointer] = RTXDI_PackDIReservoir(reservoir);
}

#endif
