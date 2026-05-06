#pragma once

// Thin frame-level shell for the experimental RT smoke/path tracing path.
//
// PathTracePrimaryPass owns the persistent smoke test state and exposes the
// frame entry/present hooks used by the renderer. Scene build, resource
// lifetime, dispatch, readback, and diagnostics live in PathTrace* modules.

#include "PathTraceGeometryUniverse.h"
#include "PathTraceDrawSurfCapture.h"
#include "PathTraceInstanceUniverse.h"
#include "PathTraceLightUniverse.h"
#include "PathTraceReservoirs.h"
#include "PathTraceSceneUniverse.h"

#include <nvrhi/nvrhi.h>
#include <vector>

class idRenderBackend;
struct RtSmokeSceneResourceCommitDesc;
struct viewDef_t;

class PathTracePrimaryPass {
public:
    explicit PathTracePrimaryPass(idRenderBackend* backend);
    ~PathTracePrimaryPass();

    // Called every frame when r_pathTracing >= 1
    void Execute(const viewDef_t* viewDef);
    void PresentDebugOutput();
    void BlitDebugOutput(nvrhi::IFramebuffer* targetFramebuffer, const nvrhi::Viewport& targetViewport);
    void DrawBoundsOverlayRaster(nvrhi::IFramebuffer* targetFramebuffer, const nvrhi::Viewport& targetViewport);

private:
    void InitRayTracingSmokeTest();
    bool ResizeRayTracingSmokeOutput(int width, int height);
    void ResetRayTracingSmokeSceneResources();
    void CommitRayTracingSmokeSceneResources(const RtSmokeSceneResourceCommitDesc& desc);
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
    uint64 m_smokeGeometryFrameIndex;
    int m_smokeSceneSourceLast;
    int m_smokeSceneSource2RigidEntitiesLast;
    uint64 m_smokeSceneUniverseStaticBuildGeneration;
    const void* m_smokeSceneRenderWorld = nullptr;
    idStr m_smokeSceneMapName;
    ID_TIME_T m_smokeSceneMapTimeStamp = 0;
    RtSmokeGeometryUniverse m_smokeGeometryUniverse;
    RtPathTraceSceneUniverse m_sceneUniverse;
    RtPathTraceInstanceUniverse m_instanceUniverse;
    RtSmokeLightUniverse m_smokeLightUniverse;
    const void* m_smokeLightUniverseRenderWorld = nullptr;
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
    nvrhi::BufferHandle m_smokeLightCandidateBuffer;
    nvrhi::BufferHandle m_smokeDoomAnalyticLightBuffer;
    nvrhi::BufferHandle m_smokeRigidRouteVertexBuffer;
    nvrhi::BufferHandle m_smokeRigidRouteIndexBuffer;
    nvrhi::BufferHandle m_smokeRigidRouteTriangleMaterialBuffer;
    nvrhi::BufferHandle m_smokeRigidRouteTriangleMaterialIndexBuffer;
    nvrhi::BufferHandle m_smokeRigidRouteInstanceBuffer;
    nvrhi::BufferHandle m_smokeConstantsBuffer;
    nvrhi::BufferHandle m_smokeBoundsOverlayLineBuffer;
    std::vector<RtPathTraceBoundsOverlayLine> m_smokeBoundsOverlayLines;
    int m_smokeBoundsOverlayLineCount = 0;
    bool m_smokeBoundsOverlayViewValid = false;
    idVec3 m_smokeBoundsOverlayCameraOrigin = vec3_origin;
    idVec3 m_smokeBoundsOverlayCameraForward = idVec3(1.0f, 0.0f, 0.0f);
    idVec3 m_smokeBoundsOverlayCameraLeft = idVec3(0.0f, 1.0f, 0.0f);
    idVec3 m_smokeBoundsOverlayCameraUp = idVec3(0.0f, 0.0f, 1.0f);
    float m_smokeBoundsOverlayTanX = 1.0f;
    float m_smokeBoundsOverlayTanY = 1.0f;
    float m_smokeBoundsOverlayModelViewMatrix[16] = {};
    float m_smokeBoundsOverlayProjectionMatrix[16] = {};
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
    int m_smokeLightCandidateCount = 0;
    int m_smokeTexturedLightCandidateCount = 0;
    int m_smokeLightCandidateBytes = 0;
    int m_smokeDoomAnalyticLightCount = 0;
    int m_smokeDoomAnalyticLightBytes = 0;
    RtSmokeReservoirBufferHandles m_smokeReservoirBuffers;
    uint64 m_smokeReservoirSceneSignature = 0;
    uint64 m_smokeReservoirDispatchSignature = 0;
    bool m_smokeReservoirNeedsClear = false;
    int m_smokeReservoirResetCount = 0;
    int m_smokeReservoirClearCount = 0;
    uint64 m_smokeAccumulationSignature = 0;
    int m_smokeAccumulationFrameCount = 0;
    nvrhi::ShaderLibraryHandle m_smokeShaderLibrary;
    nvrhi::rt::PipelineHandle m_smokePipeline;
    nvrhi::rt::ShaderTableHandle m_smokeShaderTable;
};
