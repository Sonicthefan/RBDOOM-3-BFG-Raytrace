#pragma once

#include "PathTraceDynamicMaterialState.h"

#include <nvrhi/nvrhi.h>

#include <vector>

struct RtSmokeSceneBufferHandles
{
    nvrhi::BufferHandle staticVertexBuffer;
    nvrhi::BufferHandle staticIndexBuffer;
    nvrhi::BufferHandle staticTriangleClassBuffer;
    nvrhi::BufferHandle staticTriangleMaterialBuffer;
    nvrhi::BufferHandle staticTriangleMaterialIndexBuffer;
    nvrhi::BufferHandle dynamicVertexBuffer;
    nvrhi::BufferHandle dynamicIndexBuffer;
    nvrhi::BufferHandle dynamicTriangleClassBuffer;
    nvrhi::BufferHandle dynamicTriangleMaterialBuffer;
    nvrhi::BufferHandle dynamicTriangleMaterialIndexBuffer;
    nvrhi::BufferHandle materialTableBuffer;
    nvrhi::BufferHandle emissiveTriangleBuffer;

    bool IsValid() const;
};

struct RtSmokeBindingBuildDesc
{
    nvrhi::IDevice* device = nullptr;
    nvrhi::rt::AccelStructHandle tlas;
    nvrhi::TextureHandle outputTexture;
    nvrhi::TextureHandle accumulationTexture;
    nvrhi::TextureHandle fallbackTexture;
    nvrhi::BufferHandle constantsBuffer;
    nvrhi::BindingLayoutHandle bindingLayout;
    nvrhi::BindingLayoutHandle textureBindlessLayout;
    nvrhi::DescriptorTableHandle existingTextureDescriptorTable;
    nvrhi::SamplerHandle sampler;
    RtSmokeSceneBufferHandles buffers;
    bool enableTextureProbe = false;
    bool forceFallbackTexture = false;
    int maxActiveTextures = RT_SMOKE_TEXTURE_EXPERIMENTAL_ACTIVE_CAP;
};

struct RtSmokeBindingBuildResult
{
    nvrhi::BindingSetHandle bindingSet;
    nvrhi::DescriptorTableHandle textureDescriptorTable;
    std::vector<nvrhi::TextureHandle> activeTextureTable;
    const char* errorMessage = nullptr;
    int failedTextureSlot = -1;

    bool Succeeded() const { return bindingSet && textureDescriptorTable && errorMessage == nullptr; }
};

struct RtSmokeSceneResourceCommitDesc
{
    RtSmokeSceneBufferHandles buffers;
    nvrhi::rt::AccelStructDesc staticBlasDesc;
    nvrhi::rt::AccelStructDesc dynamicBlasDesc;
    nvrhi::rt::AccelStructHandle staticBlas;
    nvrhi::rt::AccelStructHandle dynamicBlas;
    bool hasStaticBlas = false;
    uint64 staticBlasSignature = 0;
    nvrhi::BindingSetHandle bindingSet;
    nvrhi::DescriptorTableHandle textureDescriptorTable;
    std::vector<nvrhi::TextureHandle> activeTextureTable;
    int materialTableEntryCount = 0;
    int emissiveTriangleCount = 0;
    int emissiveStaticTriangleCount = 0;
    int emissiveDynamicTriangleCount = 0;
};

nvrhi::BufferHandle CreateSmokeGeometryBuffer(nvrhi::IDevice* device, const char* debugName, size_t byteSize, uint32_t structStride, bool vertexBuffer, bool indexBuffer, bool accelStructInput);
RtSmokeBindingBuildResult CreateSmokeBindingResources(const RtSmokeBindingBuildDesc& desc, RtSmokeMaterialTableBuild& materialTable);
