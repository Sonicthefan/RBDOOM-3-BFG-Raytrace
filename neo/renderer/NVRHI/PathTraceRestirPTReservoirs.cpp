#include "precompiled.h"
#pragma hdrstop

#include "PathTraceRestirPTReservoirs.h"

namespace {

uint32_t RestirPTReservoirDimension(uint32_t value)
{
    return value > 0 ? value : 1;
}

RTXDI_ReservoirBufferParameters RestirPTReservoirParameters(uint32_t width, uint32_t height, rtxdi::CheckerboardMode checkerboardMode)
{
    return rtxdi::CalculateReservoirBufferParameters(
        RestirPTReservoirDimension(width),
        RestirPTReservoirDimension(height),
        checkerboardMode);
}

uint64_t RestirPTReservoirElementCount64(uint32_t width, uint32_t height, rtxdi::CheckerboardMode checkerboardMode)
{
    const RTXDI_ReservoirBufferParameters params = RestirPTReservoirParameters(width, height, checkerboardMode);
    return static_cast<uint64_t>(params.reservoirArrayPitch) * static_cast<uint64_t>(rtxdi::c_NumReSTIRPTReservoirBuffers);
}

uint64_t RestirPTReservoirByteSize(uint32_t width, uint32_t height, rtxdi::CheckerboardMode checkerboardMode)
{
    return RestirPTReservoirElementCount64(width, height, checkerboardMode) * static_cast<uint64_t>(sizeof(RTXDI_PackedPTReservoir));
}

bool RestirPTReservoirBufferHasCapacity(nvrhi::BufferHandle buffer, uint32_t width, uint32_t height, rtxdi::CheckerboardMode checkerboardMode)
{
    return
        buffer &&
        buffer->getDesc().structStride == sizeof(RTXDI_PackedPTReservoir) &&
        buffer->getDesc().byteSize >= RestirPTReservoirByteSize(width, height, checkerboardMode);
}

nvrhi::BufferHandle CreateRestirPTReservoirBuffer(nvrhi::IDevice* device, uint32_t width, uint32_t height, rtxdi::CheckerboardMode checkerboardMode)
{
    if (!device)
    {
        return nullptr;
    }

    nvrhi::BufferDesc desc;
    desc.debugName = "PathTraceRestirPTReservoirs";
    desc.byteSize = RestirPTReservoirByteSize(width, height, checkerboardMode);
    desc.structStride = sizeof(RTXDI_PackedPTReservoir);
    desc.canHaveUAVs = true;
    desc.canHaveTypedViews = false;
    desc.initialState = nvrhi::ResourceStates::UnorderedAccess;
    desc.keepInitialState = true;
    return device->createBuffer(desc);
}

nvrhi::BufferHandle ReuseOrCreateRestirPTReservoirBuffer(nvrhi::IDevice* device, nvrhi::BufferHandle existingBuffer, uint32_t width, uint32_t height, rtxdi::CheckerboardMode checkerboardMode)
{
    if (RestirPTReservoirBufferHasCapacity(existingBuffer, width, height, checkerboardMode))
    {
        return existingBuffer;
    }

    return CreateRestirPTReservoirBuffer(device, width, height, checkerboardMode);
}

}

bool RtRestirPTReservoirBufferHandles::IsValidFor(uint32_t requestedWidth, uint32_t requestedHeight, rtxdi::CheckerboardMode checkerboardMode) const
{
    const uint32_t requiredWidth = RestirPTReservoirDimension(requestedWidth);
    const uint32_t requiredHeight = RestirPTReservoirDimension(requestedHeight);
    const RTXDI_ReservoirBufferParameters requiredParams = RestirPTReservoirParameters(requestedWidth, requestedHeight, checkerboardMode);
    const uint64_t requiredElementCount = RestirPTReservoirElementCount64(requestedWidth, requestedHeight, checkerboardMode);
    const uint64_t requiredBytes = RestirPTReservoirByteSize(requestedWidth, requestedHeight, checkerboardMode);

    return
        reservoirs &&
        width == requiredWidth &&
        height == requiredHeight &&
        reservoirParams.reservoirBlockRowPitch == requiredParams.reservoirBlockRowPitch &&
        reservoirParams.reservoirArrayPitch == requiredParams.reservoirArrayPitch &&
        reservoirElementCount >= requiredElementCount &&
        reservoirBytes >= requiredBytes &&
        reservoirs->getDesc().structStride == sizeof(RTXDI_PackedPTReservoir) &&
        reservoirs->getDesc().byteSize >= requiredBytes;
}

void RtRestirPTReservoirBufferHandles::Reset()
{
    reservoirs = nullptr;
    reservoirParams = {};
    width = 0;
    height = 0;
    reservoirElementCount = 0;
    reservoirBytes = 0;
}

RtRestirPTReservoirBufferCreateResult CreateRestirPTReservoirBuffers(const RtRestirPTReservoirBufferCreateDesc& desc)
{
    RtRestirPTReservoirBufferCreateResult result;
    result.buffers.width = RestirPTReservoirDimension(desc.width);
    result.buffers.height = RestirPTReservoirDimension(desc.height);
    result.buffers.reservoirParams = RestirPTReservoirParameters(desc.width, desc.height, desc.checkerboardMode);

    const uint64_t elementCount = RestirPTReservoirElementCount64(desc.width, desc.height, desc.checkerboardMode);
    if (elementCount > UINT32_MAX)
    {
        result.errorMessage = "RT ReSTIR PT reservoir element count exceeds 32-bit handle metadata";
        return result;
    }

    result.buffers.reservoirElementCount = static_cast<uint32_t>(elementCount);
    result.buffers.reservoirBytes = RestirPTReservoirByteSize(desc.width, desc.height, desc.checkerboardMode);
    result.buffers.reservoirs = ReuseOrCreateRestirPTReservoirBuffer(desc.device, desc.existingBuffers.reservoirs, desc.width, desc.height, desc.checkerboardMode);

    if (!result.buffers.IsValidFor(desc.width, desc.height, desc.checkerboardMode))
    {
        result.errorMessage = "failed to create RT ReSTIR PT packed reservoir buffer";
    }
    return result;
}

bool ClearRestirPTReservoirBuffers(nvrhi::ICommandList* commandList, const RtRestirPTReservoirBufferHandles& buffers)
{
    if (!commandList || !buffers.reservoirs)
    {
        return false;
    }

    commandList->setBufferState(buffers.reservoirs, nvrhi::ResourceStates::UnorderedAccess);
    commandList->commitBarriers();
    commandList->clearBufferUInt(buffers.reservoirs, 0);
    return true;
}
