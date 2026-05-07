#pragma once

// NVIDIA ReSTIR PT packed reservoir resource ownership.
//
// This is deliberately separate from PathTraceReservoirs.*. The existing smoke
// reservoirs are an engine-side debug scaffold; this module uses RTXDI's packed
// PT reservoir ABI and RTXDI reservoir addressing parameters.

#include <nvrhi/nvrhi.h>

#include <cstdint>

#include <Rtxdi/PT/ReSTIRPT.h>

struct RtRestirPTReservoirBufferHandles
{
    nvrhi::BufferHandle reservoirs;
    RTXDI_ReservoirBufferParameters reservoirParams = {};
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t reservoirElementCount = 0;
    uint64_t reservoirBytes = 0;

    bool IsValidFor(uint32_t requestedWidth, uint32_t requestedHeight, rtxdi::CheckerboardMode checkerboardMode) const;
    void Reset();
};

struct RtRestirPTReservoirBufferCreateDesc
{
    nvrhi::IDevice* device = nullptr;
    RtRestirPTReservoirBufferHandles existingBuffers;
    uint32_t width = 0;
    uint32_t height = 0;
    rtxdi::CheckerboardMode checkerboardMode = rtxdi::CheckerboardMode::Off;
};

struct RtRestirPTReservoirBufferCreateResult
{
    RtRestirPTReservoirBufferHandles buffers;
    const char* errorMessage = nullptr;

    bool Succeeded() const { return errorMessage == nullptr && buffers.reservoirs != nullptr; }
};

RtRestirPTReservoirBufferCreateResult CreateRestirPTReservoirBuffers(const RtRestirPTReservoirBufferCreateDesc& desc);
bool ClearRestirPTReservoirBuffers(nvrhi::ICommandList* commandList, const RtRestirPTReservoirBufferHandles& buffers);
