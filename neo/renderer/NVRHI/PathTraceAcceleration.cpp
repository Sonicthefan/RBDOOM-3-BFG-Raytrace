#include "precompiled.h"
#pragma hdrstop

#include "PathTraceAcceleration.h"

#include <nvrhi/utils.h>

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

RtSmokeBlasCreateResult CreateSmokeBlas(const RtSmokeBlasCreateDesc& desc)
{
    RtSmokeBlasCreateResult result;
    if (!desc.device || !desc.vertexBuffer || !desc.indexBuffer || desc.vertexCount <= 0 || desc.indexCount <= 0)
    {
        result.errorMessage = "invalid RT smoke BLAS create inputs";
        return result;
    }

    nvrhi::rt::GeometryTriangles triangleGeometry;
    InitSmokeTriangleGeometry(triangleGeometry, desc.vertexBuffer, desc.indexBuffer, desc.vertexCount, 0, desc.indexCount);

    nvrhi::rt::GeometryDesc geometryDesc;
    geometryDesc.setTriangles(triangleGeometry);

    result.accelStructDesc = nvrhi::rt::AccelStructDesc()
        .addBottomLevelGeometry(geometryDesc)
        .setBuildFlags(nvrhi::rt::AccelStructBuildFlags::PreferFastTrace)
        .setDebugName(desc.debugName);
    result.accelStruct = desc.device->createAccelStruct(result.accelStructDesc);
    if (!result.accelStruct)
    {
        result.errorMessage = "failed to create RT smoke BLAS";
    }
    return result;
}

int UploadSmokeAccelerationBuffers(const RtSmokeBufferUploadBatchDesc& desc)
{
    if (!desc.commandList || !desc.items || desc.itemCount <= 0)
    {
        return 0;
    }

    for (int itemIndex = 0; itemIndex < desc.itemCount; ++itemIndex)
    {
        const RtSmokeBufferUploadItem& item = desc.items[itemIndex];
        if (!item.skip && item.buffer)
        {
            desc.commandList->beginTrackingBufferState(item.buffer, nvrhi::ResourceStates::Common);
        }
    }

    const int bufferUploadStartMs = Sys_Milliseconds();
    for (int itemIndex = 0; itemIndex < desc.itemCount; ++itemIndex)
    {
        const RtSmokeBufferUploadItem& item = desc.items[itemIndex];
        if (!item.skip && item.buffer && item.data && item.byteSize > 0)
        {
            desc.commandList->writeBuffer(item.buffer, item.data, item.byteSize);
        }
    }

    for (int itemIndex = 0; itemIndex < desc.itemCount; ++itemIndex)
    {
        const RtSmokeBufferUploadItem& item = desc.items[itemIndex];
        if (!item.skip && item.buffer)
        {
            desc.commandList->setBufferState(item.buffer, item.finalState);
        }
    }
    desc.commandList->commitBarriers();

    return Sys_Milliseconds() - bufferUploadStartMs;
}

bool SubmitSmokeAccelerationBuilds(const RtSmokeAccelSubmitDesc& desc, RtSmokeAccelSubmitTiming& timing)
{
    timing = RtSmokeAccelSubmitTiming();
    if (!desc.commandList || !desc.tlas || (!desc.hasStaticBlas && !desc.hasDynamicBlas))
    {
        return false;
    }

    const int accelSubmitStartMs = Sys_Milliseconds();
    const int blasSubmitStartMs = Sys_Milliseconds();
    if (desc.hasStaticBlas && !desc.staticBlasCacheHit)
    {
        nvrhi::utils::BuildBottomLevelAccelStruct(desc.commandList, desc.staticBlas, desc.staticBlasDesc);
    }

    if (desc.hasDynamicBlas)
    {
        nvrhi::utils::BuildBottomLevelAccelStruct(desc.commandList, desc.dynamicBlas, desc.dynamicBlasDesc);
    }
    timing.blasSubmitMs = Sys_Milliseconds() - blasSubmitStartMs;

    nvrhi::rt::InstanceDesc instanceDescs[2];
    int instanceCount = 0;
    if (desc.hasStaticBlas)
    {
        instanceDescs[instanceCount]
            .setInstanceID(0)
            .setInstanceMask(0xff)
            .setInstanceContributionToHitGroupIndex(0)
            .setFlags(nvrhi::rt::InstanceFlags::TriangleCullDisable)
            .setBLAS(desc.staticBlas);
        ++instanceCount;
    }
    if (desc.hasDynamicBlas)
    {
        instanceDescs[instanceCount]
            .setInstanceID(1)
            .setInstanceMask(0xff)
            .setInstanceContributionToHitGroupIndex(0)
            .setFlags(nvrhi::rt::InstanceFlags::TriangleCullDisable)
            .setBLAS(desc.dynamicBlas);
        ++instanceCount;
    }

    const int tlasSubmitStartMs = Sys_Milliseconds();
    desc.commandList->buildTopLevelAccelStruct(desc.tlas, instanceDescs, instanceCount, nvrhi::rt::AccelStructBuildFlags::PreferFastTrace);
    timing.tlasSubmitMs = Sys_Milliseconds() - tlasSubmitStartMs;
    timing.accelSubmitMs = Sys_Milliseconds() - accelSubmitStartMs;
    timing.instanceCount = instanceCount;
    return true;
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
