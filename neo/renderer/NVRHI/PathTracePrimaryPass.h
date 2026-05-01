#pragma once

#include <nvrhi/nvrhi.h>
#include <vector>

class idRenderBackend;
struct viewDef_t;

struct PathTraceSmokeVertex
{
    float position[4];
    float normal[4];
    float texCoord[4];
    float color[4];
    float color2[4];
};

class PathTracePrimaryPass {
public:
    explicit PathTracePrimaryPass(idRenderBackend* backend);
    ~PathTracePrimaryPass();

    // Called every frame when r_pathTracing >= 1
    void Execute(const viewDef_t* viewDef);
    void PresentDebugOutput();
    void BlitDebugOutput(nvrhi::IFramebuffer* targetFramebuffer, const nvrhi::Viewport& targetViewport);

private:
    void InitRayTracingSmokeTest();
    bool ResizeRayTracingSmokeOutput(int width, int height);
    void BuildRayTracingSmokeTestScene(const viewDef_t* viewDef);
    void ExecuteRayTracingSmokeTest(const viewDef_t* viewDef);
    void ReadBackRayTracingSmokeTest();

    idRenderBackend* m_backend;
    bool m_reportedMode;
    bool m_rayTracingSupported;
    bool m_smokeTestInitialized;
    bool m_smokeSceneBuilt;
    bool m_smokeSceneRebuildLogged;
    bool m_smokeTestDispatched;
    bool m_smokeWaitingForDoomSurfaceLogged;
    bool m_smokeReadbackQueued;
    bool m_smokeReadbackLogged;
    int m_smokeReadbackDelayFrames;
    int m_smokeReadbackCooldownFrames;
    int m_smokeSceneLogCooldownFrames;
    int m_smokeOutputWidth;
    int m_smokeOutputHeight;
    bool m_smokeStaticBlasCacheValid;
    uint64 m_smokeStaticBlasSignature;
    int m_smokeStaticBlasCacheHitCount;
    int m_smokeStaticBlasCacheMissCount;
    std::vector<uint64> m_smokeStaticSurfaceKeys;
    std::vector<PathTraceSmokeVertex> m_smokeStaticVertexCache;
    std::vector<uint32_t> m_smokeStaticIndexCache;
    std::vector<uint32_t> m_smokeStaticTriangleClassCache;
    std::vector<uint32_t> m_smokeStaticTriangleMaterialCache;
    uint32_t m_smokeTextureProbeMaterialId;
    int m_smokeTextureProbeRequestedIndex;
    idVec3 m_smokeSceneOrigin;
    nvrhi::BufferHandle m_smokeStaticVertexBuffer;
    nvrhi::BufferHandle m_smokeStaticIndexBuffer;
    nvrhi::BufferHandle m_smokeStaticTriangleClassBuffer;
    nvrhi::BufferHandle m_smokeStaticTriangleMaterialBuffer;
    nvrhi::BufferHandle m_smokeStaticTriangleMaterialIndexBuffer;
    nvrhi::BufferHandle m_smokeDynamicVertexBuffer;
    nvrhi::BufferHandle m_smokeDynamicIndexBuffer;
    nvrhi::BufferHandle m_smokeDynamicTriangleClassBuffer;
    nvrhi::BufferHandle m_smokeDynamicTriangleMaterialBuffer;
    nvrhi::BufferHandle m_smokeDynamicTriangleMaterialIndexBuffer;
    nvrhi::BufferHandle m_smokeMaterialTableBuffer;
    nvrhi::BufferHandle m_smokeEmissiveTriangleBuffer;
    nvrhi::BufferHandle m_smokeConstantsBuffer;
    nvrhi::TextureHandle m_smokeOutputTexture;
    nvrhi::TextureHandle m_smokeAccumulationTexture;
    nvrhi::StagingTextureHandle m_smokeReadbackTexture;
    nvrhi::rt::AccelStructDesc m_smokeStaticBlasDesc;
    nvrhi::rt::AccelStructDesc m_smokeDynamicBlasDesc;
    nvrhi::rt::AccelStructHandle m_smokeStaticBlas;
    nvrhi::rt::AccelStructHandle m_smokeDynamicBlas;
    nvrhi::rt::AccelStructHandle m_smokeTlas;
    nvrhi::BindingLayoutHandle m_smokeBindingLayout;
    nvrhi::BindingLayoutHandle m_smokeTextureBindlessLayout;
    nvrhi::BindingSetHandle m_smokeBindingSet;
    nvrhi::DescriptorTableHandle m_smokeTextureDescriptorTable;
    std::vector<nvrhi::TextureHandle> m_smokeActiveTextureTable;
    int m_smokeMaterialTableEntryCount = 0;
    int m_smokeEmissiveTriangleCount = 0;
    int m_smokeEmissiveStaticTriangleCount = 0;
    int m_smokeEmissiveDynamicTriangleCount = 0;
    uint64 m_smokeAccumulationSignature = 0;
    int m_smokeAccumulationFrameCount = 0;
    nvrhi::ShaderLibraryHandle m_smokeShaderLibrary;
    nvrhi::rt::PipelineHandle m_smokePipeline;
    nvrhi::rt::ShaderTableHandle m_smokeShaderTable;
};
