#include "precompiled.h"
#pragma hdrstop

#include "PathTraceSmokeResources.h"
#include "PathTraceTextureRegistry.h"

bool RtSmokeSceneBufferHandles::IsValid() const
{
    return staticVertexBuffer && staticIndexBuffer && staticTriangleClassBuffer && staticTriangleMaterialBuffer && staticTriangleMaterialIndexBuffer &&
        dynamicVertexBuffer && dynamicIndexBuffer && dynamicTriangleClassBuffer && dynamicTriangleMaterialBuffer && dynamicTriangleMaterialIndexBuffer &&
        materialTableBuffer && emissiveTriangleBuffer;
}

nvrhi::BufferHandle CreateSmokeGeometryBuffer(nvrhi::IDevice* device, const char* debugName, size_t byteSize, uint32_t structStride, bool vertexBuffer, bool indexBuffer, bool accelStructInput)
{
    if (!device)
    {
        return nullptr;
    }

    nvrhi::BufferDesc desc;
    desc.byteSize = byteSize > structStride ? byteSize : structStride;
    desc.debugName = debugName;
    desc.structStride = structStride;
    desc.isVertexBuffer = vertexBuffer;
    desc.isIndexBuffer = indexBuffer;
    desc.isAccelStructBuildInput = accelStructInput;
    desc.initialState = nvrhi::ResourceStates::Common;
    desc.keepInitialState = true;
    return device->createBuffer(desc);
}

RtSmokeBindingBuildResult CreateSmokeBindingResources(const RtSmokeBindingBuildDesc& desc, RtSmokeMaterialTableBuild& materialTable)
{
    RtSmokeBindingBuildResult result;
    result.textureDescriptorTable = desc.existingTextureDescriptorTable;

    if (!desc.device || !desc.bindingLayout || !desc.tlas || !desc.outputTexture || !desc.accumulationTexture || !desc.fallbackTexture || !desc.constantsBuffer || !desc.sampler || !desc.buffers.IsValid())
    {
        result.errorMessage = "failed to create RT smoke binding set";
        return result;
    }

    nvrhi::BindingSetDesc bindingSetDesc;
    bindingSetDesc.bindings = {
        nvrhi::BindingSetItem::RayTracingAccelStruct(0, desc.tlas),
        nvrhi::BindingSetItem::Texture_UAV(1, desc.outputTexture),
        nvrhi::BindingSetItem::ConstantBuffer(2, desc.constantsBuffer),
        nvrhi::BindingSetItem::StructuredBuffer_SRV(3, desc.buffers.staticVertexBuffer),
        nvrhi::BindingSetItem::StructuredBuffer_SRV(4, desc.buffers.staticIndexBuffer),
        nvrhi::BindingSetItem::StructuredBuffer_SRV(5, desc.buffers.staticTriangleClassBuffer),
        nvrhi::BindingSetItem::StructuredBuffer_SRV(6, desc.buffers.dynamicVertexBuffer),
        nvrhi::BindingSetItem::StructuredBuffer_SRV(7, desc.buffers.dynamicIndexBuffer),
        nvrhi::BindingSetItem::StructuredBuffer_SRV(8, desc.buffers.dynamicTriangleClassBuffer),
        nvrhi::BindingSetItem::StructuredBuffer_SRV(9, desc.buffers.staticTriangleMaterialBuffer),
        nvrhi::BindingSetItem::StructuredBuffer_SRV(10, desc.buffers.dynamicTriangleMaterialBuffer),
        nvrhi::BindingSetItem::StructuredBuffer_SRV(11, desc.buffers.staticTriangleMaterialIndexBuffer),
        nvrhi::BindingSetItem::StructuredBuffer_SRV(12, desc.buffers.dynamicTriangleMaterialIndexBuffer),
        nvrhi::BindingSetItem::StructuredBuffer_SRV(13, desc.buffers.materialTableBuffer)
    };

    result.activeTextureTable.push_back(desc.fallbackTexture);
    if (desc.enableTextureProbe)
    {
        if (!result.textureDescriptorTable)
        {
            result.textureDescriptorTable = desc.device->createDescriptorTable(desc.textureBindlessLayout);
        }
        if (!result.textureDescriptorTable)
        {
            result.errorMessage = "failed to create RT smoke texture descriptor table";
            return result;
        }

        const int textureSlotCount = idMath::ClampInt(1, desc.maxActiveTextures, Max(static_cast<int>(materialTable.diffuseTextures.size()), 1));
        result.activeTextureTable.reserve(textureSlotCount + 1);
        for (int textureSlot = 0; textureSlot < textureSlotCount; ++textureSlot)
        {
            nvrhi::TextureHandle texture = desc.fallbackTexture;
            if (!desc.forceFallbackTexture && textureSlot >= 0 && textureSlot < static_cast<int>(materialTable.diffuseTextures.size()))
            {
                const nvrhi::TextureHandle candidateTexture = materialTable.diffuseTextures[textureSlot];
                if (candidateTexture)
                {
                    texture = candidateTexture;
                }
            }
            if (!IsSmokeTextureHandleSafeForDescriptor(texture))
            {
                ++materialTable.descriptorsReplacedWithFallback;
                texture = desc.fallbackTexture;
            }

            result.activeTextureTable.push_back(texture);
        }

        for (int textureSlot = 0; textureSlot < textureSlotCount; ++textureSlot)
        {
            nvrhi::TextureHandle texture = result.activeTextureTable[textureSlot + 1];
            if (!desc.device->writeDescriptorTable(result.textureDescriptorTable, nvrhi::BindingSetItem::Texture_SRV(textureSlot, texture)))
            {
                result.errorMessage = "failed to write RT smoke bindless texture descriptor slot";
                result.failedTextureSlot = textureSlot;
                return result;
            }
        }
    }

    bindingSetDesc.addItem(nvrhi::BindingSetItem::Texture_SRV(14, desc.fallbackTexture));
    bindingSetDesc.addItem(nvrhi::BindingSetItem::Texture_UAV(15, desc.accumulationTexture));
    bindingSetDesc.addItem(nvrhi::BindingSetItem::StructuredBuffer_SRV(16, desc.buffers.emissiveTriangleBuffer));
    bindingSetDesc.addItem(nvrhi::BindingSetItem::Sampler(0, desc.sampler));

    result.bindingSet = desc.device->createBindingSet(bindingSetDesc, desc.bindingLayout);
    if (!result.bindingSet)
    {
        result.errorMessage = "failed to create RT smoke binding set";
    }

    return result;
}
