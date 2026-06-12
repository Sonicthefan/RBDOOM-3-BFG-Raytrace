#ifndef RB_PATH_TRACING_CLEANROOM_COMMON_RESTIR_GI_RESERVOIR_STORAGE_HLSLI
#define RB_PATH_TRACING_CLEANROOM_COMMON_RESTIR_GI_RESERVOIR_STORAGE_HLSLI

#include "cleanroom_common/restir_gi_reservoir.hlsli"

// GI reservoir page storage against the buffer named by
// RTXDI_GI_RESERVOIR_BUFFER, which the GI reservoir bridge #defines before
// including Rtxdi/GI/Reservoir.hlsli. Kept in its own translation unit (not
// in restir_gi_reservoir.hlsli) because the ReSTIRGIParameters.h shim pulls
// the reservoir types into shaders before the bridge defines the buffer
// macro - an #ifdef inside the include-guarded reservoir header would never
// re-evaluate.

#ifndef RTXDI_GI_RESERVOIR_BUFFER
#error "GI reservoir storage requires RTXDI_GI_RESERVOIR_BUFFER to name the packed GI reservoir buffer before including Rtxdi/GI/Reservoir.hlsli"
#endif

RTXDI_GIReservoir RTXDI_LoadGIReservoir(
    RTXDI_ReservoirBufferParameters reservoirParams,
    uint2 reservoirPosition,
    uint reservoirArrayIndex)
{
    const uint pointer = RTXDI_ReservoirPositionToPointer(
        reservoirParams,
        reservoirPosition,
        reservoirArrayIndex);
    return RTXDI_UnpackGIReservoir(RTXDI_GI_RESERVOIR_BUFFER[pointer]);
}

#if !defined(RTXDI_ENABLE_STORE_RESERVOIR) || RTXDI_ENABLE_STORE_RESERVOIR
void RTXDI_StoreGIReservoir(
    RTXDI_GIReservoir reservoir,
    RTXDI_ReservoirBufferParameters reservoirParams,
    uint2 reservoirPosition,
    uint reservoirArrayIndex)
{
    const uint pointer = RTXDI_ReservoirPositionToPointer(
        reservoirParams,
        reservoirPosition,
        reservoirArrayIndex);
    RTXDI_GI_RESERVOIR_BUFFER[pointer] = RTXDI_PackGIReservoir(reservoir, 0u);
}
#endif

#endif
