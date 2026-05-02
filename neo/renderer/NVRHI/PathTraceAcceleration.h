#pragma once

#include "PathTraceGeometry.h"

#include <nvrhi/nvrhi.h>

#include <vector>

struct RtSmokeGeometryRange
{
    int vertexOffset = 0;
    int vertexCount = 0;
    int indexOffset = 0;
    int indexCount = 0;
    int triangleOffset = 0;
    int triangleCount = 0;
};

struct RtSmokeStaticBlasSignature
{
    uint64 hash = 0;
    int vertexCount = 0;
    int indexCount = 0;
    int triangleCount = 0;
};

struct RtSmokeBlasCreateDesc
{
    nvrhi::IDevice* device = nullptr;
    nvrhi::BufferHandle vertexBuffer;
    nvrhi::BufferHandle indexBuffer;
    int vertexCount = 0;
    int indexCount = 0;
    const char* debugName = nullptr;
};

struct RtSmokeBlasCreateResult
{
    nvrhi::rt::AccelStructDesc accelStructDesc;
    nvrhi::rt::AccelStructHandle accelStruct;
    const char* errorMessage = nullptr;

    bool Succeeded() const { return accelStruct && errorMessage == nullptr; }
};

struct RtSmokeAccelSubmitDesc
{
    nvrhi::ICommandList* commandList = nullptr;
    nvrhi::rt::AccelStructHandle tlas;
    nvrhi::rt::AccelStructHandle staticBlas;
    nvrhi::rt::AccelStructHandle dynamicBlas;
    nvrhi::rt::AccelStructDesc staticBlasDesc;
    nvrhi::rt::AccelStructDesc dynamicBlasDesc;
    bool hasStaticBlas = false;
    bool hasDynamicBlas = false;
    bool staticBlasCacheHit = false;
};

struct RtSmokeAccelSubmitTiming
{
    int blasSubmitMs = 0;
    int tlasSubmitMs = 0;
    int accelSubmitMs = 0;
    int instanceCount = 0;
};

void InitSmokeTriangleGeometry(nvrhi::rt::GeometryTriangles& triangleGeometry, nvrhi::IBuffer* vertexBuffer, nvrhi::IBuffer* indexBuffer, int totalVertexCount, int indexOffset, int indexCount);
RtSmokeBlasCreateResult CreateSmokeBlas(const RtSmokeBlasCreateDesc& desc);
bool SubmitSmokeAccelerationBuilds(const RtSmokeAccelSubmitDesc& desc, RtSmokeAccelSubmitTiming& timing);
uint64 HashSmokeBytes(uint64 hash, const void* data, size_t size);
uint64 HashSmokeFloatQuantized(uint64 hash, float value, float scale);
RtSmokeStaticBlasSignature ComputeSmokeStaticBlasSignature(
    const std::vector<PathTraceSmokeVertex>& vertexData,
    const std::vector<uint32_t>& indexData,
    const std::vector<uint32_t>& triangleClassData,
    const std::vector<uint32_t>& triangleMaterialData,
    const RtSmokeGeometryRange& staticRange,
    const idVec3& sceneOrigin);
