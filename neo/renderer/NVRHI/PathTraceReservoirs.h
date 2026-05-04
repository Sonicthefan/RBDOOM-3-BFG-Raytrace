#pragma once

// ReSTIR/PT reservoir resource scaffold.
//
// This module owns the first screen-sized reservoir buffers used by future
// light-sampling passes. The buffers are intentionally not bound into the smoke
// ray tracing shader yet; this step only creates a stable resource boundary.

#include <nvrhi/nvrhi.h>

#include <cstdint>

struct PathTraceSmokeReservoir
{
    float radianceAndTargetPdf[4];
    float weightSumAndSampleCount[4];
    uint32_t lightCandidateIndex = UINT32_MAX;
    uint32_t emissiveTriangleIndex = UINT32_MAX;
    uint32_t flags = 0;
    uint32_t padding0 = 0;
};
static_assert((sizeof(PathTraceSmokeReservoir) % 16) == 0, "PathTraceSmokeReservoir must stay 16-byte aligned for HLSL StructuredBuffer reads");

struct RtSmokeReservoirBufferHandles
{
    nvrhi::BufferHandle current;
    nvrhi::BufferHandle previous;
    nvrhi::BufferHandle spatialScratch;
    int width = 0;
    int height = 0;
    int reservoirCount = 0;
    int reservoirBytes = 0;

    bool IsValidFor(int requestedWidth, int requestedHeight) const;
    void Reset();
};

struct RtSmokeReservoirBufferCreateDesc
{
    nvrhi::IDevice* device = nullptr;
    RtSmokeReservoirBufferHandles existingBuffers;
    int width = 0;
    int height = 0;
};

struct RtSmokeReservoirBufferCreateResult
{
    RtSmokeReservoirBufferHandles buffers;
    const char* errorMessage = nullptr;

    bool Succeeded() const { return buffers.IsValidFor(buffers.width, buffers.height) && errorMessage == nullptr; }
};

RtSmokeReservoirBufferCreateResult CreateSmokeReservoirBuffers(const RtSmokeReservoirBufferCreateDesc& desc);
