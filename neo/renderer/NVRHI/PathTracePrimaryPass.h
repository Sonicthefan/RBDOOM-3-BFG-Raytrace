#pragma once

#include <nvrhi/nvrhi.h>

class idRenderBackend;

class PathTracePrimaryPass {
public:
    explicit PathTracePrimaryPass(idRenderBackend* backend);
    ~PathTracePrimaryPass();

    // Called every frame when r_pathTracing >= 1
    void Execute();

private:
    void InitRayTracingSmokeTest();
    void BuildRayTracingSmokeTestScene();
    void ExecuteRayTracingSmokeTest();

    idRenderBackend* m_backend;
    bool m_reportedMode;
    bool m_rayTracingSupported;
    bool m_smokeTestInitialized;
    bool m_smokeSceneBuilt;
    bool m_smokeTestDispatched;
    nvrhi::BufferHandle m_smokeVertexBuffer;
    nvrhi::BufferHandle m_smokeIndexBuffer;
    nvrhi::rt::AccelStructDesc m_smokeBlasDesc;
    nvrhi::rt::AccelStructHandle m_smokeBlas;
    nvrhi::rt::AccelStructHandle m_smokeTlas;
    nvrhi::BindingLayoutHandle m_smokeBindingLayout;
    nvrhi::BindingSetHandle m_smokeBindingSet;
    nvrhi::ShaderLibraryHandle m_smokeShaderLibrary;
    nvrhi::rt::PipelineHandle m_smokePipeline;
    nvrhi::rt::ShaderTableHandle m_smokeShaderTable;
};
