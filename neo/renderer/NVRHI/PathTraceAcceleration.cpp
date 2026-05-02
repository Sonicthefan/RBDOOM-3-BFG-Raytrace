#include "precompiled.h"
#pragma hdrstop

#include "PathTraceAcceleration.h"

void InitSmokeTriangleGeometry(nvrhi::rt::GeometryTriangles& triangleGeometry, nvrhi::IBuffer* vertexBuffer, nvrhi::IBuffer* indexBuffer, int totalVertexCount, int indexOffset, int indexCount)
{
    triangleGeometry.indexBuffer = indexBuffer;
    triangleGeometry.vertexBuffer = vertexBuffer;
    triangleGeometry.indexFormat = nvrhi::Format::R32_UINT;
    triangleGeometry.vertexFormat = nvrhi::Format::RGB32_FLOAT;
    triangleGeometry.indexOffset = static_cast<uint64_t>(indexOffset) * sizeof(uint32_t);
    triangleGeometry.vertexOffset = 0;
    triangleGeometry.indexCount = static_cast<uint32_t>(indexCount);
    triangleGeometry.vertexCount = static_cast<uint32_t>(totalVertexCount);
    triangleGeometry.vertexStride = sizeof(PathTraceSmokeVertex);
}

uint64 HashSmokeBytes(uint64 hash, const void* data, size_t size)
{
    const byte* bytes = static_cast<const byte*>(data);
    for (size_t index = 0; index < size; ++index)
    {
        hash ^= static_cast<uint64>(bytes[index]);
        hash *= 1099511628211ull;
    }
    return hash;
}

uint64 HashSmokeFloatQuantized(uint64 hash, float value, float scale)
{
    const int quantized = idMath::Ftoi(value * scale);
    return HashSmokeBytes(hash, &quantized, sizeof(quantized));
}

RtSmokeStaticBlasSignature ComputeSmokeStaticBlasSignature(
    const std::vector<PathTraceSmokeVertex>& vertexData,
    const std::vector<uint32_t>& indexData,
    const std::vector<uint32_t>& triangleClassData,
    const std::vector<uint32_t>& triangleMaterialData,
    const RtSmokeGeometryRange& staticRange,
    const idVec3& sceneOrigin)
{
    RtSmokeStaticBlasSignature signature;
    signature.vertexCount = staticRange.vertexCount;
    signature.indexCount = staticRange.indexCount;
    signature.triangleCount = staticRange.triangleCount;

    uint64 hash = 14695981039346656037ull;
    hash = HashSmokeBytes(hash, &sceneOrigin.x, sizeof(sceneOrigin.x));
    hash = HashSmokeBytes(hash, &sceneOrigin.y, sizeof(sceneOrigin.y));
    hash = HashSmokeBytes(hash, &sceneOrigin.z, sizeof(sceneOrigin.z));
    hash = HashSmokeBytes(hash, &signature.vertexCount, sizeof(signature.vertexCount));
    hash = HashSmokeBytes(hash, &signature.indexCount, sizeof(signature.indexCount));
    hash = HashSmokeBytes(hash, &signature.triangleCount, sizeof(signature.triangleCount));

    if (staticRange.vertexCount > 0)
    {
        hash = HashSmokeBytes(hash, vertexData.data() + staticRange.vertexOffset, static_cast<size_t>(staticRange.vertexCount) * sizeof(vertexData[0]));
    }

    if (staticRange.indexCount > 0)
    {
        hash = HashSmokeBytes(hash, indexData.data() + staticRange.indexOffset, static_cast<size_t>(staticRange.indexCount) * sizeof(indexData[0]));
    }

    if (staticRange.triangleCount > 0)
    {
        hash = HashSmokeBytes(hash, triangleClassData.data() + staticRange.triangleOffset, static_cast<size_t>(staticRange.triangleCount) * sizeof(triangleClassData[0]));
        hash = HashSmokeBytes(hash, triangleMaterialData.data() + staticRange.triangleOffset, static_cast<size_t>(staticRange.triangleCount) * sizeof(triangleMaterialData[0]));
    }

    signature.hash = hash;
    return signature;
}
