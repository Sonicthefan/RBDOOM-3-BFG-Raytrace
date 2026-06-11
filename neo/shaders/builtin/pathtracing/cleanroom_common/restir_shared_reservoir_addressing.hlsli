#ifndef RB_PATH_TRACING_CLEANROOM_COMMON_RESTIR_SHARED_RESERVOIR_ADDRESSING_HLSLI
#define RB_PATH_TRACING_CLEANROOM_COMMON_RESTIR_SHARED_RESERVOIR_ADDRESSING_HLSLI

#include "cleanroom_common/restir_shared_parameters.hlsli"

uint2 RTXDI_PixelPosToReservoirPos(uint2 pixelPosition, uint activeCheckerboardField)
{
    if (activeCheckerboardField == 0u)
    {
        return pixelPosition;
    }

    return uint2(pixelPosition.x >> 1u, pixelPosition.y);
}

uint2 RTXDI_ReservoirPosToPixelPos(uint2 reservoirPosition, uint activeCheckerboardField)
{
    if (activeCheckerboardField == 0u)
    {
        return reservoirPosition;
    }

    uint2 pixelPosition = uint2(reservoirPosition.x << 1u, reservoirPosition.y);
    pixelPosition.x += (pixelPosition.y + activeCheckerboardField) & 1u;
    return pixelPosition;
}

uint RTXDI_ReservoirPositionToPointer(
    RTXDI_ReservoirBufferParameters reservoirParams,
    uint2 reservoirPosition,
    uint reservoirArrayIndex)
{
    const uint blockX = reservoirPosition.x / RTXDI_RESERVOIR_BLOCK_SIZE;
    const uint blockY = reservoirPosition.y / RTXDI_RESERVOIR_BLOCK_SIZE;
    const uint inBlockX = reservoirPosition.x & (RTXDI_RESERVOIR_BLOCK_SIZE - 1u);
    const uint inBlockY = reservoirPosition.y & (RTXDI_RESERVOIR_BLOCK_SIZE - 1u);
    const uint blockIndexInRow = blockX * RTXDI_RESERVOIR_BLOCK_SIZE * RTXDI_RESERVOIR_BLOCK_SIZE;
    const uint blockRowOffset = blockY * reservoirParams.reservoirBlockRowPitch;
    const uint inBlockOffset = inBlockY * RTXDI_RESERVOIR_BLOCK_SIZE + inBlockX;
    return reservoirArrayIndex * reservoirParams.reservoirArrayPitch + blockRowOffset + blockIndexInRow + inBlockOffset;
}

void RTXDI_ApplyPermutationSampling(inout int2 prevPixelPos, uint uniformRandomNumber)
{
    const int2 offset = int2(uniformRandomNumber & 3u, (uniformRandomNumber >> 2u) & 3u);
    prevPixelPos += offset;
    prevPixelPos.x ^= 3;
    prevPixelPos.y ^= 3;
    prevPixelPos -= offset;
}

#endif
