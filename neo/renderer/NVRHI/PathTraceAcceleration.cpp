#include "precompiled.h"
#pragma hdrstop

#include "PathTraceAcceleration.h"
#include "PathTraceAccelerationPlan.h"

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
    OPTICK_EVENT("PT Upload Scene Buffers Detail");

    if (!desc.commandList || !desc.items || desc.itemCount <= 0)
    {
        return 0;
    }

    {
        OPTICK_GPU_EVENT("PT GPU Begin Upload Tracking");
        for (int itemIndex = 0; itemIndex < desc.itemCount; ++itemIndex)
        {
            const RtSmokeBufferUploadItem& item = desc.items[itemIndex];
            if (!item.skip && item.buffer)
            {
                desc.commandList->beginTrackingBufferState(item.buffer, nvrhi::ResourceStates::Common);
            }
        }
    }

    const int bufferUploadStartMs = Sys_Milliseconds();
    {
        OPTICK_GPU_EVENT("PT GPU Write Scene Buffers");
        for (int itemIndex = 0; itemIndex < desc.itemCount; ++itemIndex)
        {
            const RtSmokeBufferUploadItem& item = desc.items[itemIndex];
            if (!item.skip && item.buffer && item.data && item.byteSize > 0)
            {
                const byte* sourceBytes = static_cast<const byte*>(item.data) + item.sourceOffsetBytes;
                desc.commandList->writeBuffer(item.buffer, sourceBytes, item.byteSize, item.destOffsetBytes);
            }
        }
    }

    {
        OPTICK_GPU_EVENT("PT GPU Upload Buffer Barriers");
        for (int itemIndex = 0; itemIndex < desc.itemCount; ++itemIndex)
        {
            const RtSmokeBufferUploadItem& item = desc.items[itemIndex];
            if (!item.skip && item.buffer)
            {
                desc.commandList->setBufferState(item.buffer, item.finalState);
            }
        }
        desc.commandList->commitBarriers();
    }

    return Sys_Milliseconds() - bufferUploadStartMs;
}

bool SubmitSmokeAccelerationBuilds(const RtSmokeAccelSubmitDesc& desc, RtSmokeAccelSubmitTiming& timing)
{
    OPTICK_EVENT("PT Submit Acceleration Builds Detail");

    timing = RtSmokeAccelSubmitTiming();
    if (!desc.commandList || !desc.tlas || (!desc.hasStaticBlas && !desc.hasDynamicBlas))
    {
        return false;
    }

    RtSmokeAccelerationSubmitPlanInput submitPlanInput;
    submitPlanInput.hasStaticBlas = desc.hasStaticBlas;
    submitPlanInput.hasDynamicBlas = desc.hasDynamicBlas;
    submitPlanInput.staticBlasCacheHit = desc.staticBlasCacheHit;
    const RtSmokeAccelerationSubmitPlan submitPlan =
        BuildSmokeAccelerationSubmitPlan(submitPlanInput);
    if (!submitPlan.submitTlas)
    {
        return false;
    }

    const int accelSubmitStartMs = Sys_Milliseconds();
    const int blasSubmitStartMs = Sys_Milliseconds();
    if (submitPlan.buildStaticBlas)
    {
        OPTICK_GPU_EVENT("PT GPU Build Static BLAS");
        nvrhi::utils::BuildBottomLevelAccelStruct(desc.commandList, desc.staticBlas, desc.staticBlasDesc);
    }

    if (submitPlan.buildDynamicBlas)
    {
        OPTICK_GPU_EVENT("PT GPU Build Dynamic BLAS");
        nvrhi::utils::BuildBottomLevelAccelStruct(desc.commandList, desc.dynamicBlas, desc.dynamicBlasDesc);
    }
    timing.blasSubmitMs = Sys_Milliseconds() - blasSubmitStartMs;

    std::vector<nvrhi::rt::InstanceDesc> instanceDescs;
    instanceDescs.reserve(2 + (desc.extraTlasInstances ? desc.extraTlasInstances->size() : 0));
    for (int plannedIndex = 0; plannedIndex < submitPlan.baseTlasPlan.instanceCount; ++plannedIndex)
    {
        const RtSmokePlanTlasInstance& plannedInstance = submitPlan.baseTlasPlan.instances[plannedIndex];
        nvrhi::rt::InstanceDesc instanceDesc;
        instanceDesc
            .setInstanceID(plannedInstance.instanceId)
            .setInstanceMask(plannedInstance.instanceMask)
            .setInstanceContributionToHitGroupIndex(plannedInstance.hitGroupContribution)
            .setFlags(nvrhi::rt::InstanceFlags::TriangleCullDisable)
            .setBLAS(plannedInstance.kind == RT_SMOKE_PLAN_TLAS_STATIC_BLAS ? desc.staticBlas : desc.dynamicBlas);
        instanceDescs.push_back(instanceDesc);
    }
    if (desc.extraTlasInstances)
    {
        for (const nvrhi::rt::InstanceDesc& instanceDesc : *desc.extraTlasInstances)
        {
            if (instanceDesc.bottomLevelAS)
            {
                instanceDescs.push_back(instanceDesc);
            }
        }
    }

    const int tlasSubmitStartMs = Sys_Milliseconds();
    {
        OPTICK_GPU_EVENT("PT GPU Build TLAS");
        desc.commandList->buildTopLevelAccelStruct(desc.tlas, instanceDescs.data(), instanceDescs.size(), nvrhi::rt::AccelStructBuildFlags::PreferFastTrace);
    }
    timing.tlasSubmitMs = Sys_Milliseconds() - tlasSubmitStartMs;
    timing.accelSubmitMs = Sys_Milliseconds() - accelSubmitStartMs;
    timing.instanceCount = static_cast<int>(instanceDescs.size());
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
    RtSmokePlanStaticBlasSignatureDesc desc;
    desc.vertices = vertexData.empty() ? nullptr : vertexData.data();
    desc.vertexStride = sizeof(PathTraceSmokeVertex);
    desc.totalVertexCount = static_cast<int>(vertexData.size());
    desc.indexes = indexData.empty() ? nullptr : indexData.data();
    desc.totalIndexCount = static_cast<int>(indexData.size());
    desc.triangleClasses = triangleClassData.empty() ? nullptr : triangleClassData.data();
    desc.triangleMaterials = triangleMaterialData.empty() ? nullptr : triangleMaterialData.data();
    desc.totalTriangleCount = static_cast<int>(Min(triangleClassData.size(), triangleMaterialData.size()));
    desc.staticRange.vertexOffset = staticRange.vertexOffset;
    desc.staticRange.vertexCount = staticRange.vertexCount;
    desc.staticRange.indexOffset = staticRange.indexOffset;
    desc.staticRange.indexCount = staticRange.indexCount;
    desc.staticRange.triangleOffset = staticRange.triangleOffset;
    desc.staticRange.triangleCount = staticRange.triangleCount;
    desc.sceneOrigin.x = sceneOrigin.x;
    desc.sceneOrigin.y = sceneOrigin.y;
    desc.sceneOrigin.z = sceneOrigin.z;

    const RtSmokePlanStaticBlasSignature planSignature = ComputeSmokeStaticBlasSignaturePlan(desc);
    RtSmokeStaticBlasSignature signature;
    signature.hash = static_cast<uint64>(planSignature.hash);
    signature.vertexCount = planSignature.vertexCount;
    signature.indexCount = planSignature.indexCount;
    signature.triangleCount = planSignature.triangleCount;
    return signature;
}
