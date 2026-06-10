#pragma once

// Clean-room Remix ReSTIR GI lane (docs/restir_remix_gi_cleanroom).
//
// Owns the GI lane's GPU state: reservoir pages, producer textures, constants,
// pipeline, and the per-frame dispatch. Gated entirely by the
// r_pathTracingCleanRestirGi* cvars; when r_pathTracingCleanRestirGiEnable is
// 0 nothing is dispatched and nothing is written.

#include <nvrhi/nvrhi.h>

#include <cstdint>

struct PathTraceCleanRestirGiState
{
    nvrhi::BufferHandle constantsBuffer;
    nvrhi::BufferHandle reservoirBuffer;
    uint32_t reservoirWidth = 0;
    uint32_t reservoirHeight = 0;
    uint32_t reservoirArrayPitch = 0;
    uint32_t reservoirBlockRowPitch = 0;
    uint64_t reservoirBytes = 0;
    bool reservoirClearPending = true;
    nvrhi::TextureHandle producerRadianceTexture;
    nvrhi::TextureHandle producerHitPositionTexture;
    nvrhi::TextureHandle producerHitNormalTexture;
    nvrhi::BufferHandle placeholderSrvBuffer;
    nvrhi::BindingLayoutHandle bindingLayout;
    nvrhi::ShaderLibraryHandle shaderLibrary;
    nvrhi::rt::PipelineHandle pipeline;
    nvrhi::rt::ShaderTableHandle shaderTable;
    bool pipelineInitAttempted = false;
    uint32_t frameIndex = 0;
    bool dispatchLogged = false;

    void ReleaseResources();
};

struct PathTraceCleanRestirGiDispatchInputs
{
    nvrhi::IDevice* device = nullptr;
    nvrhi::ICommandList* commandList = nullptr;
    nvrhi::ITexture* outputTexture = nullptr;
    nvrhi::IDescriptorTable* textureDescriptorTable = nullptr;
    nvrhi::IBindingLayout* textureBindlessLayout = nullptr;
    bool isD3D12 = false;
    bool isVulkan = false;
    int width = 0;
    int height = 0;

    // Live DI sentinel constants blob (PathTraceCleanRtxdiDiSentinelConstants,
    // 480 bytes). The GI cbuffer mirrors that layout in its leading block so
    // shared DI-lane helper code sees identical values.
    const void* diConstantsBlob = nullptr;
    uint32_t diConstantsSize = 0;

    // Scene resources shared read-only with the DI lane (GI io_whitelist
    // input classes GI-I-01/04/05/06/09).
    nvrhi::rt::IAccelStruct* tlas = nullptr;
    nvrhi::IBuffer* staticVertexBuffer = nullptr;
    nvrhi::IBuffer* staticIndexBuffer = nullptr;
    nvrhi::IBuffer* dynamicVertexBuffer = nullptr;
    nvrhi::IBuffer* dynamicIndexBuffer = nullptr;
    nvrhi::IBuffer* staticTriangleMaterialIndexBuffer = nullptr;
    nvrhi::IBuffer* dynamicTriangleMaterialIndexBuffer = nullptr;
    nvrhi::IBuffer* materialTableBuffer = nullptr;
    nvrhi::ITexture* fallbackTexture = nullptr;
    nvrhi::IBuffer* emissiveTriangleBuffer = nullptr;
    nvrhi::IBuffer* emissiveDistributionBuffer = nullptr;
    nvrhi::IBuffer* rigidRouteVertexBuffer = nullptr;
    nvrhi::IBuffer* rigidRouteIndexBuffer = nullptr;
    nvrhi::IBuffer* rigidRouteTriangleMaterialIndexBuffer = nullptr;
    nvrhi::IBuffer* rigidRouteInstanceBuffer = nullptr;
    nvrhi::IBuffer* doomAnalyticLightBuffer = nullptr;
    nvrhi::IBuffer* rluCurrentLightBuffer = nullptr;
    nvrhi::IBuffer* neeCacheProviderResultBuffer = nullptr;
    nvrhi::IBuffer* primarySurfaceCurrentBuffer = nullptr;
    nvrhi::IBuffer* primarySurfacePreviousBuffer = nullptr;
    nvrhi::ISampler* materialSampler = nullptr;
};

// Runs the GI lane for this frame if its cvars request it. Returns true when
// a GI debug view was written to the output texture (so callers know the
// output now shows the GI lane).
bool PathTraceCleanRestirGiExecute(
    PathTraceCleanRestirGiState& state,
    const PathTraceCleanRestirGiDispatchInputs& inputs);
