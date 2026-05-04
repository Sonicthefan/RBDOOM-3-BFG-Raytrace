#include "precompiled.h"
#pragma hdrstop

#include "PathTraceReservoirs.h"

namespace {

int SmokeReservoirDimension(int value)
{
    return Max(1, value);
}

int SmokeReservoirCount(int width, int height)
{
    return SmokeReservoirDimension(width) * SmokeReservoirDimension(height);
}

uint64_t SmokeReservoirByteSize(int width, int height)
{
    return static_cast<uint64_t>(SmokeReservoirCount(width, height)) * sizeof(PathTraceSmokeReservoir);
}

bool SmokeReservoirBufferHasCapacity(nvrhi::BufferHandle buffer, int width, int height)
{
    return buffer && buffer->getDesc().byteSize >= SmokeReservoirByteSize(width, height);
}

nvrhi::BufferHandle CreateSmokeReservoirBuffer(nvrhi::IDevice* device, const char* debugName, int width, int height)
{
    if (!device)
    {
        return nullptr;
    }

    nvrhi::BufferDesc desc;
    desc.byteSize = SmokeReservoirByteSize(width, height);
    desc.debugName = debugName;
    desc.structStride = sizeof(PathTraceSmokeReservoir);
    desc.canHaveUAVs = true;
    desc.initialState = nvrhi::ResourceStates::UnorderedAccess;
    desc.keepInitialState = true;
    return device->createBuffer(desc);
}

nvrhi::BufferHandle ReuseOrCreateSmokeReservoirBuffer(nvrhi::IDevice* device, nvrhi::BufferHandle existingBuffer, const char* debugName, int width, int height)
{
    if (SmokeReservoirBufferHasCapacity(existingBuffer, width, height))
    {
        return existingBuffer;
    }

    return CreateSmokeReservoirBuffer(device, debugName, width, height);
}

}

bool RtSmokeReservoirBufferHandles::IsValidFor(int requestedWidth, int requestedHeight) const
{
    const int requiredCount = SmokeReservoirCount(requestedWidth, requestedHeight);
    const uint64_t requiredBytes = SmokeReservoirByteSize(requestedWidth, requestedHeight);
    return
        current &&
        previous &&
        spatialScratch &&
        width == SmokeReservoirDimension(requestedWidth) &&
        height == SmokeReservoirDimension(requestedHeight) &&
        reservoirCount >= requiredCount &&
        reservoirBytes >= static_cast<int>(requiredBytes) &&
        current->getDesc().byteSize >= requiredBytes &&
        previous->getDesc().byteSize >= requiredBytes &&
        spatialScratch->getDesc().byteSize >= requiredBytes;
}

void RtSmokeReservoirBufferHandles::Reset()
{
    current = nullptr;
    previous = nullptr;
    spatialScratch = nullptr;
    width = 0;
    height = 0;
    reservoirCount = 0;
    reservoirBytes = 0;
}

RtSmokeReservoirBufferCreateResult CreateSmokeReservoirBuffers(const RtSmokeReservoirBufferCreateDesc& desc)
{
    RtSmokeReservoirBufferCreateResult result;
    result.buffers.width = SmokeReservoirDimension(desc.width);
    result.buffers.height = SmokeReservoirDimension(desc.height);
    result.buffers.reservoirCount = SmokeReservoirCount(desc.width, desc.height);
    result.buffers.reservoirBytes = static_cast<int>(SmokeReservoirByteSize(desc.width, desc.height));
    result.buffers.current = ReuseOrCreateSmokeReservoirBuffer(desc.device, desc.existingBuffers.current, "PathTraceSmokeReservoirCurrent", desc.width, desc.height);
    result.buffers.previous = ReuseOrCreateSmokeReservoirBuffer(desc.device, desc.existingBuffers.previous, "PathTraceSmokeReservoirPrevious", desc.width, desc.height);
    result.buffers.spatialScratch = ReuseOrCreateSmokeReservoirBuffer(desc.device, desc.existingBuffers.spatialScratch, "PathTraceSmokeReservoirSpatialScratch", desc.width, desc.height);

    if (!result.buffers.IsValidFor(desc.width, desc.height))
    {
        result.errorMessage = "failed to create RT smoke ReSTIR reservoir buffers";
    }
    return result;
}
