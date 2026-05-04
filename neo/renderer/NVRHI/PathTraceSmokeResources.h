#pragma once

// NVRHI resource descriptions and helpers for the RT smoke path.
//
// Owns creation/packaging of smoke resources: geometry buffers, binding
// resources, output/readback textures, shader state, and the final commit
// descriptor consumed by PathTracePrimaryPass state.

#include "PathTraceDynamicMaterialState.h"
#include "PathTraceReservoirs.h"

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
    nvrhi::BufferHandle lightCandidateBuffer;

    bool IsValid() const;
};

struct RtSmokeSceneBufferCreateDesc
{
    nvrhi::IDevice* device = nullptr;
    RtSmokeSceneBufferHandles existingBuffers;
    size_t staticVertexBytes = 0;
    size_t staticIndexBytes = 0;
    size_t staticTriangleClassBytes = 0;
    size_t staticTriangleMaterialBytes = 0;
    size_t staticTriangleMaterialIndexBytes = 0;
    size_t dynamicVertexBytes = 0;
    size_t dynamicIndexBytes = 0;
    size_t dynamicTriangleClassBytes = 0;
    size_t dynamicTriangleMaterialBytes = 0;
    size_t dynamicTriangleMaterialIndexBytes = 0;
    size_t materialTableBytes = 0;
    size_t emissiveTriangleBytes = 0;
    size_t lightCandidateBytes = 0;
};

struct RtSmokeSceneBufferCreateResult
{
    RtSmokeSceneBufferHandles buffers;
    const char* errorMessage = nullptr;

    bool Succeeded() const { return buffers.IsValid() && errorMessage == nullptr; }
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
    RtSmokeReservoirBufferHandles reservoirBuffers;
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
    int lightCandidateCount = 0;
    int texturedLightCandidateCount = 0;
    int lightCandidateBytes = 0;
    uint64 reservoirSceneSignature = 0;
};

struct RtSmokeSceneResourceCommitBuildDesc
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
    const std::vector<nvrhi::TextureHandle>* activeTextureTable = nullptr;
    int materialTableEntryCount = 0;
    int emissiveTriangleCount = 0;
    int emissiveStaticTriangleCount = 0;
    int emissiveDynamicTriangleCount = 0;
    int lightCandidateCount = 0;
    int texturedLightCandidateCount = 0;
    int lightCandidateBytes = 0;
    uint64 reservoirSceneSignature = 0;
};

RtSmokeSceneBufferCreateResult CreateSmokeSceneBuffers(const RtSmokeSceneBufferCreateDesc& desc);
RtSmokeBindingBuildResult CreateSmokeBindingResources(const RtSmokeBindingBuildDesc& desc, RtSmokeMaterialTableBuild& materialTable);
RtSmokeSceneResourceCommitDesc CreateSmokeSceneResourceCommitDesc(const RtSmokeSceneResourceCommitBuildDesc& desc);
