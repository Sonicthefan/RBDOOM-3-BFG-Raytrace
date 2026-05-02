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

void InitSmokeTriangleGeometry(nvrhi::rt::GeometryTriangles& triangleGeometry, nvrhi::IBuffer* vertexBuffer, nvrhi::IBuffer* indexBuffer, int totalVertexCount, int indexOffset, int indexCount);
uint64 HashSmokeBytes(uint64 hash, const void* data, size_t size);
uint64 HashSmokeFloatQuantized(uint64 hash, float value, float scale);
RtSmokeStaticBlasSignature ComputeSmokeStaticBlasSignature(
    const std::vector<PathTraceSmokeVertex>& vertexData,
    const std::vector<uint32_t>& indexData,
    const std::vector<uint32_t>& triangleClassData,
    const std::vector<uint32_t>& triangleMaterialData,
    const RtSmokeGeometryRange& staticRange,
    const idVec3& sceneOrigin);
