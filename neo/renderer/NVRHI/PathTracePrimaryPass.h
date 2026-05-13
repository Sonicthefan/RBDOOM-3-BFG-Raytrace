#pragma once

// Thin frame-level shell for the experimental RT smoke/path tracing path.
//
// PathTracePrimaryPass owns the persistent smoke test state and exposes the
// frame entry/present hooks used by the renderer. Scene build, resource
// lifetime, dispatch, readback, and diagnostics live in PathTrace* modules.

#include "PathTraceGeometryUniverse.h"
#include "PathTraceDrawSurfCapture.h"
#include "PathTraceFrameResources.h"
#include "PathTraceInstanceUniverse.h"
#include "PathTraceLightUniverse.h"
#include "PathTraceSceneInputs.h"
#include "PathTraceSceneUniverse.h"
#include "PathTraceSmokeResources.h"

#include <nvrhi/nvrhi.h>
#include <deque>
#include <vector>

class idRenderBackend;
struct viewDef_t;

struct RtRetiredSmokeScenePackage
{
    uint64 retireFrame = 0;
    uint64 sceneSignature = 0;
    int currentArea = -1;
    int selectedAreaCount = 0;

    RtPathTraceSceneInputs sceneInputs;
    RtSmokeSceneBufferHandles buffers;

    nvrhi::rt::AccelStructHandle staticBlas;
    nvrhi::rt::AccelStructHandle dynamicBlas;
    nvrhi::rt::AccelStructHandle tlas;
    nvrhi::BindingSetHandle bindingSet;
    nvrhi::DescriptorTableHandle textureDescriptorTable;
    std::vector<nvrhi::TextureHandle> activeTextureTable;
};

class PathTracePrimaryPass {
public:
    explicit PathTracePrimaryPass(idRenderBackend* backend);
    ~PathTracePrimaryPass();

    // Called every frame when r_pathTracing >= 1
    void Execute(const viewDef_t* viewDef);
    void InvalidateForBackBufferResize();
    void PresentDebugOutput();
    void BlitDebugOutput(nvrhi::IFramebuffer* targetFramebuffer, const nvrhi::Viewport& targetViewport);
    void DrawBoundsOverlayRaster(nvrhi::IFramebuffer* targetFramebuffer, const nvrhi::Viewport& targetViewport);

private:
    void InitRayTracingSmokeTest();
    bool InitRayTracingSmokeRestirPipeline(int restirLibraryKind);
    bool ResizeRayTracingSmokeOutput(int width, int height);
    void ResetRayTracingSmokeSceneResources();
    void CommitRayTracingSmokeSceneResources(const RtSmokeSceneResourceCommitDesc& desc);
    bool HasRetainableRayTracingSmokeScenePackage() const;
    RtRetiredSmokeScenePackage CaptureRetiredRayTracingSmokeScenePackage() const;
    void PushRetiredRayTracingSmokeScenePackage(RtRetiredSmokeScenePackage& package, uint64 currentFrame, int retireFrames, const RtPathTraceSceneInputs& nextSceneInputs, bool sceneTransitionChanged, bool portalTransitionChanged, bool waitedForIdle);
    int ReleaseExpiredRetiredRayTracingSmokeScenePackages(uint64 currentFrame, const RtPathTraceSceneInputs& previousSceneInputs, const RtPathTraceSceneInputs& nextSceneInputs, bool sceneTransitionChanged, bool portalTransitionChanged, bool waitedForIdle);
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
    int m_smokeSceneLogCooldownFrames;
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
    std::vector<RtSmokeSkinnedSurfaceRecord> m_smokeSkinnedSurfaceRecords;
    std::vector<RtSmokeSkinnedSurfaceRecord> m_smokePreviousSkinnedSurfaceRecords;
    std::vector<PathTraceSmokeVertex> m_smokePreviousSkinnedVertexData;
    std::vector<PathTraceSkinnedJointMatrix> m_smokePreviousSkinnedJointMatrices;
    RtSmokeSkinnedPreviousFrameStats m_smokeSkinnedPreviousStats;
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
    nvrhi::BufferHandle m_smokePreviousStaticVertexBuffer;
    nvrhi::BufferHandle m_smokePreviousStaticIndexBuffer;
    nvrhi::BufferHandle m_smokePreviousStaticTriangleClassBuffer;
    nvrhi::BufferHandle m_smokePreviousStaticTriangleMaterialBuffer;
    nvrhi::BufferHandle m_smokePreviousStaticTriangleMaterialIndexBuffer;
    nvrhi::BufferHandle m_smokeDynamicVertexBuffer;
    nvrhi::BufferHandle m_smokeDynamicIndexBuffer;
    nvrhi::BufferHandle m_smokeDynamicTriangleClassBuffer;
    nvrhi::BufferHandle m_smokeDynamicTriangleMaterialBuffer;
    nvrhi::BufferHandle m_smokeDynamicTriangleMaterialIndexBuffer;
    nvrhi::BufferHandle m_smokeMaterialTableBuffer;
    nvrhi::BufferHandle m_smokeEmissiveTriangleBuffer;
    nvrhi::BufferHandle m_smokeLightCandidateBuffer;
    nvrhi::BufferHandle m_smokeDoomAnalyticLightBuffer;
    nvrhi::BufferHandle m_smokeDoomAnalyticPreviousLightBuffer;
    nvrhi::BufferHandle m_smokeDoomAnalyticCurrentIdentityBuffer;
    nvrhi::BufferHandle m_smokeDoomAnalyticPreviousIdentityBuffer;
    nvrhi::BufferHandle m_smokeDoomAnalyticRemapBuffer;
    nvrhi::BufferHandle m_smokeRigidRouteVertexBuffer;
    nvrhi::BufferHandle m_smokeRigidRouteIndexBuffer;
    nvrhi::BufferHandle m_smokeRigidRouteTriangleMaterialBuffer;
    nvrhi::BufferHandle m_smokeRigidRouteTriangleMaterialIndexBuffer;
    nvrhi::BufferHandle m_smokeRigidRouteInstanceBuffer;
    nvrhi::BufferHandle m_smokeSkinnedSourceVertexBuffer;
    nvrhi::BufferHandle m_smokeSkinnedCurrentOutputVertexBuffer;
    nvrhi::BufferHandle m_smokeSkinnedPreviousPositionBuffer;
    nvrhi::BufferHandle m_smokeSkinnedSurfaceDispatchBuffer;
    nvrhi::BufferHandle m_smokeSkinnedTriangleDispatchIndexBuffer;
    nvrhi::BufferHandle m_smokeSkinnedCurrentJointMatrixBuffer;
    nvrhi::BufferHandle m_smokeSkinnedPreviousJointMatrixBuffer;
    nvrhi::BufferHandle m_smokeConstantsBuffer;
    nvrhi::BufferHandle m_restirPTConstantsBuffer;
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
    RtPathTraceFrameResources m_frameResources;
    RtPathTraceSceneInputs m_sceneInputs;
    nvrhi::rt::AccelStructDesc m_smokeStaticBlasDesc;
    nvrhi::rt::AccelStructDesc m_smokeDynamicBlasDesc;
    nvrhi::rt::AccelStructHandle m_smokeStaticBlas;
    nvrhi::rt::AccelStructHandle m_smokeDynamicBlas;
    nvrhi::rt::AccelStructHandle m_smokeTlas;
    nvrhi::BindingLayoutHandle m_smokeBindingLayout;
    nvrhi::BindingLayoutHandle m_smokeSkinnedGpuSkinningBindingLayout;
    nvrhi::BindingLayoutHandle m_smokeTextureBindlessLayout;
    nvrhi::BindingSetHandle m_smokeBindingSet;
    nvrhi::BindingSetHandle m_smokeSkinnedGpuSkinningBindingSet;
    nvrhi::BufferHandle m_smokeSkinnedGpuSkinningOutputBuffer;
    nvrhi::BufferHandle m_smokeSkinnedGpuSkinningPreviousPositionBuffer;
    nvrhi::DescriptorTableHandle m_smokeTextureDescriptorTable;
    std::vector<nvrhi::TextureHandle> m_smokeActiveTextureTable;
    std::deque<RtRetiredSmokeScenePackage> m_retiredSmokeScenePackages;
    std::vector<uint32_t> m_smokePreviousStaticTriangleMaterialIndexes;
    uint64 m_smokePreviousStaticSnapshotUploadSignature = 0;
    int m_smokeMaterialTableEntryCount = 0;
    int m_smokeEmissiveTriangleCount = 0;
    int m_smokeEmissiveStaticTriangleCount = 0;
    int m_smokeEmissiveDynamicTriangleCount = 0;
    int m_smokeLightCandidateCount = 0;
    int m_smokeTexturedLightCandidateCount = 0;
    int m_smokeLightCandidateBytes = 0;
    int m_smokeDoomAnalyticLightCount = 0;
    int m_smokeDoomAnalyticLightBytes = 0;
    int m_smokeDoomAnalyticPreviousLightCount = 0;
    int m_smokeDoomAnalyticCurrentIdentityCount = 0;
    int m_smokeDoomAnalyticPreviousIdentityCount = 0;
    int m_smokeDoomAnalyticRemapCount = 0;
    int m_smokeDoomAnalyticInvalidRemapCount = 0;
    int m_smokePreviousEmissiveTriangleCount = 0;
    nvrhi::ShaderLibraryHandle m_smokeShaderLibrary;
    nvrhi::ShaderLibraryHandle m_smokeRestirShaderLibrary;
    nvrhi::ShaderLibraryHandle m_smokeRestirInitialShaderLibrary;
    nvrhi::ShaderLibraryHandle m_smokeRestirTemporalShadingShaderLibrary;
    nvrhi::ShaderLibraryHandle m_smokeRestirAttributionShaderLibrary;
    nvrhi::ShaderLibraryHandle m_smokeRestirSpatialShaderLibrary;
    nvrhi::ShaderLibraryHandle m_smokeRestirSpatialAttributionShaderLibrary;
    nvrhi::ShaderHandle m_smokeSkinnedGpuSkinningShader;
    nvrhi::ComputePipelineHandle m_smokeSkinnedGpuSkinningPipeline;
    nvrhi::rt::PipelineHandle m_smokePipeline;
    nvrhi::rt::PipelineHandle m_smokeRestirPipeline;
    nvrhi::rt::PipelineHandle m_smokeRestirInitialPipeline;
    nvrhi::rt::PipelineHandle m_smokeRestirTemporalShadingPipeline;
    nvrhi::rt::PipelineHandle m_smokeRestirAttributionPipeline;
    nvrhi::rt::PipelineHandle m_smokeRestirSpatialPipeline;
    nvrhi::rt::PipelineHandle m_smokeRestirSpatialAttributionPipeline;
    nvrhi::rt::ShaderTableHandle m_smokeShaderTable;
    nvrhi::rt::ShaderTableHandle m_smokeRestirShaderTable;
    nvrhi::rt::ShaderTableHandle m_smokeRestirInitialShaderTable;
    nvrhi::rt::ShaderTableHandle m_smokeRestirTemporalShadingShaderTable;
    nvrhi::rt::ShaderTableHandle m_smokeRestirAttributionShaderTable;
    nvrhi::rt::ShaderTableHandle m_smokeRestirSpatialShaderTable;
    nvrhi::rt::ShaderTableHandle m_smokeRestirSpatialAttributionShaderTable;
};
