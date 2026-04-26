#include "precompiled.h"
#pragma hdrstop

#include "PathTracePrimaryPass.h"
#include "../RenderCommon.h"
#include "../RenderBackend.h"
#include "../../framework/Common_local.h"
#include "../../sys/DeviceManager.h"

#include <nvrhi/utils.h>

extern DeviceManager* deviceManager;

PathTracePrimaryPass::PathTracePrimaryPass(idRenderBackend* backend)
    : m_backend(backend)
    , m_reportedMode(false)
    , m_rayTracingSupported(false)
    , m_smokeTestInitialized(false)
    , m_smokeSceneBuilt(false)
    , m_smokeTestDispatched(false)
{
    nvrhi::IDevice* device = deviceManager ? deviceManager->GetDevice() : nullptr;
    if (device)
    {
        m_rayTracingSupported =
            device->queryFeatureSupport(nvrhi::Feature::RayTracingAccelStruct) &&
            device->queryFeatureSupport(nvrhi::Feature::RayTracingPipeline);
    }

    common->Printf("PathTracePrimaryPass: initialized, ray tracing %s\n",
        m_rayTracingSupported ? "available" : "unavailable");
}

PathTracePrimaryPass::~PathTracePrimaryPass()
{
}

void PathTracePrimaryPass::Execute()
{
    const int mode = r_pathTracing.GetInteger();

    if (!m_reportedMode)
    {
        common->Printf("PathTracePrimaryPass: mode %d (%s)\n",
            mode, mode == 2 ? "pure primary rays" : "hybrid");

        if (!m_rayTracingSupported)
        {
            common->Printf("PathTracePrimaryPass: RT device features are not available; restart with r_pathTracing enabled before device creation\n");
        }

        m_reportedMode = true;
    }

    if (!m_rayTracingSupported)
    {
        return;
    }

    InitRayTracingSmokeTest();
    BuildRayTracingSmokeTestScene();
    ExecuteRayTracingSmokeTest();
}

void PathTracePrimaryPass::InitRayTracingSmokeTest()
{
    if (m_smokeTestInitialized)
    {
        return;
    }

    m_smokeTestInitialized = true;

    nvrhi::IDevice* device = deviceManager ? deviceManager->GetDevice() : nullptr;
    if (!device)
    {
        common->Printf("PathTracePrimaryPass: cannot initialize RT smoke test without an NVRHI device\n");
        return;
    }

    const char* shaderPath = nullptr;
    if (deviceManager->GetGraphicsAPI() == nvrhi::GraphicsAPI::D3D12)
    {
        shaderPath = "renderprogs2/dxil/builtin/pathtracing/pathtrace_smoke.rt.bin";
    }
    else if (deviceManager->GetGraphicsAPI() == nvrhi::GraphicsAPI::VULKAN)
    {
        shaderPath = "renderprogs2/spirv/builtin/pathtracing/pathtrace_smoke.rt.bin";
    }
    else
    {
        common->Printf("PathTracePrimaryPass: RT smoke test does not support this graphics API\n");
        return;
    }

    void* shaderData = nullptr;
    const int shaderSize = fileSystem->ReadFile(shaderPath, &shaderData);
    if (shaderSize <= 0 || !shaderData)
    {
        common->Printf("PathTracePrimaryPass: couldn't read %s\n", shaderPath);
        return;
    }

    m_smokeShaderLibrary = device->createShaderLibrary(shaderData, shaderSize);
    Mem_Free(shaderData);

    if (!m_smokeShaderLibrary)
    {
        common->Printf("PathTracePrimaryPass: failed to create RT smoke shader library\n");
        return;
    }

    const float triangleVertices[] = {
         0.0f,  0.6f, 0.0f,
         0.6f, -0.6f, 0.0f,
        -0.6f, -0.6f, 0.0f
    };

    const uint32_t triangleIndices[] = { 0, 1, 2 };

    nvrhi::BufferDesc vertexBufferDesc;
    vertexBufferDesc.byteSize = sizeof(triangleVertices);
    vertexBufferDesc.debugName = "PathTraceSmokeTriangleVertices";
    vertexBufferDesc.isVertexBuffer = true;
    vertexBufferDesc.isAccelStructBuildInput = true;
    vertexBufferDesc.initialState = nvrhi::ResourceStates::Common;
    vertexBufferDesc.keepInitialState = true;
    m_smokeVertexBuffer = device->createBuffer(vertexBufferDesc);

    nvrhi::BufferDesc indexBufferDesc;
    indexBufferDesc.byteSize = sizeof(triangleIndices);
    indexBufferDesc.debugName = "PathTraceSmokeTriangleIndices";
    indexBufferDesc.isIndexBuffer = true;
    indexBufferDesc.isAccelStructBuildInput = true;
    indexBufferDesc.initialState = nvrhi::ResourceStates::Common;
    indexBufferDesc.keepInitialState = true;
    m_smokeIndexBuffer = device->createBuffer(indexBufferDesc);

    if (!m_smokeVertexBuffer || !m_smokeIndexBuffer)
    {
        common->Printf("PathTracePrimaryPass: failed to create RT smoke triangle buffers\n");
        return;
    }

    nvrhi::rt::GeometryTriangles triangleGeometry;
    triangleGeometry.indexBuffer = m_smokeIndexBuffer;
    triangleGeometry.vertexBuffer = m_smokeVertexBuffer;
    triangleGeometry.indexFormat = nvrhi::Format::R32_UINT;
    triangleGeometry.vertexFormat = nvrhi::Format::RGB32_FLOAT;
    triangleGeometry.indexCount = 3;
    triangleGeometry.vertexCount = 3;
    triangleGeometry.vertexStride = sizeof(float) * 3;

    nvrhi::rt::GeometryDesc geometryDesc;
    geometryDesc.setTriangles(triangleGeometry);
    geometryDesc.setFlags(nvrhi::rt::GeometryFlags::Opaque);

    m_smokeBlasDesc = nvrhi::rt::AccelStructDesc()
        .addBottomLevelGeometry(geometryDesc)
        .setBuildFlags(nvrhi::rt::AccelStructBuildFlags::PreferFastTrace)
        .setDebugName("PathTraceSmokeBLAS");

    m_smokeBlas = device->createAccelStruct(m_smokeBlasDesc);
    m_smokeTlas = device->createAccelStruct(nvrhi::rt::AccelStructDesc()
        .setTopLevelMaxInstances(1)
        .setBuildFlags(nvrhi::rt::AccelStructBuildFlags::PreferFastTrace)
        .setDebugName("PathTraceSmokeTLAS"));

    if (!m_smokeBlas || !m_smokeTlas)
    {
        common->Printf("PathTracePrimaryPass: failed to create RT smoke acceleration structures\n");
        return;
    }

    nvrhi::BindingSetDesc bindingSetDesc;
    bindingSetDesc.bindings = {
        nvrhi::BindingSetItem::RayTracingAccelStruct(0, m_smokeTlas)
    };

    if (!nvrhi::utils::CreateBindingSetAndLayout(device, nvrhi::ShaderType::All, 0, bindingSetDesc, m_smokeBindingLayout, m_smokeBindingSet))
    {
        common->Printf("PathTracePrimaryPass: failed to create RT smoke TLAS binding set\n");
        return;
    }

    nvrhi::rt::PipelineDesc pipelineDesc;
    pipelineDesc.globalBindingLayouts = { m_smokeBindingLayout };
    pipelineDesc.shaders = {
        { "", m_smokeShaderLibrary->getShader("RayGen", nvrhi::ShaderType::RayGeneration), nullptr },
        { "", m_smokeShaderLibrary->getShader("Miss", nvrhi::ShaderType::Miss), nullptr }
    };
    pipelineDesc.hitGroups = {
        {
            "HitGroup",
            m_smokeShaderLibrary->getShader("ClosestHit", nvrhi::ShaderType::ClosestHit),
            nullptr,
            nullptr,
            nullptr,
            false
        }
    };
    pipelineDesc.maxPayloadSize = 4;
    pipelineDesc.maxAttributeSize = 8;
    pipelineDesc.maxRecursionDepth = 1;

    m_smokePipeline = device->createRayTracingPipeline(pipelineDesc);
    if (!m_smokePipeline)
    {
        common->Printf("PathTracePrimaryPass: failed to create RT smoke pipeline\n");
        return;
    }

    m_smokeShaderTable = m_smokePipeline->createShaderTable();
    if (!m_smokeShaderTable)
    {
        common->Printf("PathTracePrimaryPass: failed to create RT smoke shader table\n");
        return;
    }

    m_smokeShaderTable->setRayGenerationShader("RayGen");
    m_smokeShaderTable->addMissShader("Miss");
    m_smokeShaderTable->addHitGroup("HitGroup");

    common->Printf("PathTracePrimaryPass: RT smoke pipeline initialized\n");
}

void PathTracePrimaryPass::BuildRayTracingSmokeTestScene()
{
    if (m_smokeSceneBuilt || !m_smokeBlas || !m_smokeTlas || !m_smokeVertexBuffer || !m_smokeIndexBuffer)
    {
        return;
    }

    nvrhi::ICommandList* commandList = m_backend ? m_backend->GL_GetCommandList() : nullptr;
    if (!commandList)
    {
        return;
    }

    const float triangleVertices[] = {
         0.0f,  0.6f, 0.0f,
         0.6f, -0.6f, 0.0f,
        -0.6f, -0.6f, 0.0f
    };

    const uint32_t triangleIndices[] = { 0, 1, 2 };

    commandList->beginTrackingBufferState(m_smokeVertexBuffer, nvrhi::ResourceStates::Common);
    commandList->beginTrackingBufferState(m_smokeIndexBuffer, nvrhi::ResourceStates::Common);
    commandList->writeBuffer(m_smokeVertexBuffer, triangleVertices, sizeof(triangleVertices));
    commandList->writeBuffer(m_smokeIndexBuffer, triangleIndices, sizeof(triangleIndices));
    commandList->setBufferState(m_smokeVertexBuffer, nvrhi::ResourceStates::AccelStructBuildInput);
    commandList->setBufferState(m_smokeIndexBuffer, nvrhi::ResourceStates::AccelStructBuildInput);
    commandList->commitBarriers();

    nvrhi::utils::BuildBottomLevelAccelStruct(commandList, m_smokeBlas, m_smokeBlasDesc);

    nvrhi::rt::InstanceDesc instanceDesc;
    instanceDesc.setInstanceMask(0xff);
    instanceDesc.setInstanceContributionToHitGroupIndex(0);
    instanceDesc.setFlags(nvrhi::rt::InstanceFlags::TriangleCullDisable);
    instanceDesc.setBLAS(m_smokeBlas);

    commandList->buildTopLevelAccelStruct(m_smokeTlas, &instanceDesc, 1, nvrhi::rt::AccelStructBuildFlags::PreferFastTrace);
    m_smokeSceneBuilt = true;

    common->Printf("PathTracePrimaryPass: built RT smoke BLAS/TLAS\n");
}

void PathTracePrimaryPass::ExecuteRayTracingSmokeTest()
{
    if (m_smokeTestDispatched || !m_smokeSceneBuilt || !m_smokeShaderTable || !m_smokeBindingSet)
    {
        return;
    }

    nvrhi::ICommandList* commandList = m_backend ? m_backend->GL_GetCommandList() : nullptr;
    if (!commandList)
    {
        return;
    }

    nvrhi::rt::State state;
    state.shaderTable = m_smokeShaderTable;
    state.bindings = { m_smokeBindingSet };
    commandList->setRayTracingState(state);

    nvrhi::rt::DispatchRaysArguments args;
    args.width = 1;
    args.height = 1;
    args.depth = 1;
    commandList->dispatchRays(args);

    m_smokeTestDispatched = true;
    common->Printf("PathTracePrimaryPass: dispatched RT smoke raygen\n");
}
