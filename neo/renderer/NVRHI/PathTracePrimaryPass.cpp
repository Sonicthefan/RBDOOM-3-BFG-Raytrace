#include "precompiled.h"
#pragma hdrstop

#include "PathTracePrimaryPass.h"
#include "../RenderCommon.h"
#include "../RenderBackend.h"
#include "../../framework/Common_local.h"
#include "../../sys/DeviceManager.h"

#include <nvrhi/utils.h>
#include <vector>

extern DeviceManager* deviceManager;

namespace {

const int RT_SMOKE_MAX_SURFACES = 8;
const int RT_SMOKE_MAX_VERTS = 4096;
const int RT_SMOKE_MAX_INDEXES = 12288;

bool FindSmokeRayAnchorTriangle(const srfTriangles_t* tri, int& sourceTriangle, idVec3& centroid)
{
    for (int index = 0; index + 2 < tri->numIndexes; index += 3)
    {
        const int i0 = tri->indexes[index + 0];
        const int i1 = tri->indexes[index + 1];
        const int i2 = tri->indexes[index + 2];
        if (i0 < 0 || i1 < 0 || i2 < 0 || i0 >= tri->numVerts || i1 >= tri->numVerts || i2 >= tri->numVerts)
        {
            continue;
        }

        const idVec3& p0 = tri->verts[i0].xyz;
        const idVec3& p1 = tri->verts[i1].xyz;
        const idVec3& p2 = tri->verts[i2].xyz;
        const idVec3 normal = (p1 - p0).Cross(p2 - p0);
        if (normal.LengthSqr() < 1.0e-8f || idMath::Fabs(normal.z) < 0.1f)
        {
            continue;
        }

        sourceTriangle = index / 3;
        centroid = (p0 + p1 + p2) * (1.0f / 3.0f);
        return true;
    }

    return false;
}

bool CaptureDoomSurfacesForSmokeTest(const viewDef_t* viewDef, std::vector<float>& vertexData, std::vector<uint32_t>& indexData, int& sourceSurfaces, int& sourceVerts, int& sourceIndexes, int& anchorTriangle)
{
    sourceSurfaces = 0;
    sourceVerts = 0;
    sourceIndexes = 0;
    anchorTriangle = -1;

    if (!viewDef || !viewDef->drawSurfs)
    {
        return false;
    }

    bool foundAnchor = false;
    idVec3 anchorCentroid = vec3_origin;

    vertexData.clear();
    indexData.clear();
    vertexData.reserve(RT_SMOKE_MAX_VERTS * 3);
    indexData.reserve(RT_SMOKE_MAX_INDEXES);

    for (int surfaceIndex = 0; surfaceIndex < viewDef->numDrawSurfs; ++surfaceIndex)
    {
        const drawSurf_t* drawSurf = viewDef->drawSurfs[surfaceIndex];
        if (!drawSurf || !drawSurf->frontEndGeo)
        {
            continue;
        }

        const srfTriangles_t* tri = drawSurf->frontEndGeo;
        if (!tri->verts || !tri->indexes || tri->numVerts < 3 || tri->numIndexes < 3)
        {
            continue;
        }

        int surfaceAnchorTriangle = -1;
        idVec3 surfaceCentroid = vec3_origin;
        if (!FindSmokeRayAnchorTriangle(tri, surfaceAnchorTriangle, surfaceCentroid))
        {
            continue;
        }

        if (!foundAnchor)
        {
            foundAnchor = true;
            anchorCentroid = surfaceCentroid;
            anchorTriangle = surfaceAnchorTriangle;
        }

        if (sourceSurfaces >= RT_SMOKE_MAX_SURFACES ||
            sourceVerts + tri->numVerts > RT_SMOKE_MAX_VERTS ||
            sourceIndexes + tri->numIndexes > RT_SMOKE_MAX_INDEXES)
        {
            break;
        }

        const uint32_t indexBase = static_cast<uint32_t>(sourceVerts);
        for (int vertexIndex = 0; vertexIndex < tri->numVerts; ++vertexIndex)
        {
            const idVec3 position = tri->verts[vertexIndex].xyz - anchorCentroid;
            vertexData.push_back(position.x);
            vertexData.push_back(position.y);
            vertexData.push_back(position.z);
        }

        for (int sourceIndex = 0; sourceIndex + 2 < tri->numIndexes; sourceIndex += 3)
        {
            const int i0 = tri->indexes[sourceIndex + 0];
            const int i1 = tri->indexes[sourceIndex + 1];
            const int i2 = tri->indexes[sourceIndex + 2];
            if (i0 < 0 || i1 < 0 || i2 < 0 || i0 >= tri->numVerts || i1 >= tri->numVerts || i2 >= tri->numVerts)
            {
                continue;
            }

            indexData.push_back(indexBase + static_cast<uint32_t>(i0));
            indexData.push_back(indexBase + static_cast<uint32_t>(i1));
            indexData.push_back(indexBase + static_cast<uint32_t>(i2));
        }

        ++sourceSurfaces;
        sourceVerts += tri->numVerts;
        sourceIndexes = static_cast<int>(indexData.size());
    }

    return sourceSurfaces > 0 && !vertexData.empty() && !indexData.empty();
}

}

PathTracePrimaryPass::PathTracePrimaryPass(idRenderBackend* backend)
    : m_backend(backend)
    , m_reportedMode(false)
    , m_rayTracingSupported(false)
    , m_smokeTestInitialized(false)
    , m_smokeSceneBuilt(false)
    , m_smokeTestDispatched(false)
    , m_smokeWaitingForDoomSurfaceLogged(false)
    , m_smokeReadbackQueued(false)
    , m_smokeReadbackLogged(false)
    , m_smokeReadbackDelayFrames(0)
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

void PathTracePrimaryPass::Execute(const viewDef_t* viewDef)
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
    BuildRayTracingSmokeTestScene(viewDef);
    ExecuteRayTracingSmokeTest();
    ReadBackRayTracingSmokeTest();
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
    ID_TIME_T shaderTimestamp = 0;
    const int shaderSize = fileSystem->ReadFile(shaderPath, &shaderData, &shaderTimestamp);
    if (shaderSize <= 0 || !shaderData)
    {
        common->Printf("PathTracePrimaryPass: couldn't read %s\n", shaderPath);
        return;
    }

    common->Printf("PathTracePrimaryPass: loaded RT smoke shader %s (%d bytes, timestamp %u)\n",
        shaderPath, shaderSize, static_cast<unsigned int>(shaderTimestamp));

    m_smokeShaderLibrary = device->createShaderLibrary(shaderData, shaderSize);
    Mem_Free(shaderData);

    if (!m_smokeShaderLibrary)
    {
        common->Printf("PathTracePrimaryPass: failed to create RT smoke shader library\n");
        return;
    }

    m_smokeTlas = device->createAccelStruct(nvrhi::rt::AccelStructDesc()
        .setTopLevelMaxInstances(1)
        .setBuildFlags(nvrhi::rt::AccelStructBuildFlags::PreferFastTrace)
        .setDebugName("PathTraceSmokeTLAS"));

    if (!m_smokeTlas)
    {
        common->Printf("PathTracePrimaryPass: failed to create RT smoke TLAS\n");
        return;
    }

    nvrhi::TextureDesc outputDesc;
    outputDesc.width = 1;
    outputDesc.height = 1;
    outputDesc.mipLevels = 1;
    outputDesc.arraySize = 1;
    outputDesc.format = nvrhi::Format::RGBA32_FLOAT;
    outputDesc.dimension = nvrhi::TextureDimension::Texture2D;
    outputDesc.isUAV = true;
    outputDesc.initialState = nvrhi::ResourceStates::UnorderedAccess;
    outputDesc.keepInitialState = true;
    outputDesc.debugName = "PathTraceSmokeOutput";
    m_smokeOutputTexture = device->createTexture(outputDesc);

    if (!m_smokeOutputTexture)
    {
        common->Printf("PathTracePrimaryPass: failed to create RT smoke output UAV\n");
        return;
    }

    nvrhi::TextureDesc readbackDesc = outputDesc;
    readbackDesc.isShaderResource = false;
    readbackDesc.isUAV = false;
    readbackDesc.initialState = nvrhi::ResourceStates::Unknown;
    readbackDesc.keepInitialState = false;
    readbackDesc.debugName = "PathTraceSmokeReadback";
    m_smokeReadbackTexture = device->createStagingTexture(readbackDesc, nvrhi::CpuAccessMode::Read);

    if (!m_smokeReadbackTexture)
    {
        common->Printf("PathTracePrimaryPass: failed to create RT smoke readback texture\n");
        return;
    }

    nvrhi::BindingLayoutDesc bindingLayoutDesc;
    bindingLayoutDesc.visibility = nvrhi::ShaderType::AllRayTracing;
    bindingLayoutDesc.bindingOffsets = nvrhi::VulkanBindingOffsets()
        .setShaderResourceOffset(0)
        .setUnorderedAccessViewOffset(0);
    bindingLayoutDesc.bindings = {
        nvrhi::BindingLayoutItem::RayTracingAccelStruct(0),
        nvrhi::BindingLayoutItem::Texture_UAV(1)
    };
    m_smokeBindingLayout = device->createBindingLayout(bindingLayoutDesc);

    nvrhi::BindingSetDesc bindingSetDesc;
    bindingSetDesc.bindings = {
        nvrhi::BindingSetItem::RayTracingAccelStruct(0, m_smokeTlas),
        nvrhi::BindingSetItem::Texture_UAV(1, m_smokeOutputTexture)
    };
    m_smokeBindingSet = device->createBindingSet(bindingSetDesc, m_smokeBindingLayout);

    if (!m_smokeBindingLayout || !m_smokeBindingSet)
    {
        common->Printf("PathTracePrimaryPass: failed to create RT smoke binding set\n");
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
    common->Printf("PathTracePrimaryPass: RT smoke output UAV initialized\n");
}

void PathTracePrimaryPass::BuildRayTracingSmokeTestScene(const viewDef_t* viewDef)
{
    if (m_smokeSceneBuilt || !m_smokeTlas)
    {
        return;
    }

    nvrhi::IDevice* device = deviceManager ? deviceManager->GetDevice() : nullptr;
    if (!device)
    {
        return;
    }

    nvrhi::ICommandList* commandList = m_backend ? m_backend->GL_GetCommandList() : nullptr;
    if (!commandList)
    {
        return;
    }

    std::vector<float> vertexData;
    std::vector<uint32_t> indexData;
    int sourceSurfaces = 0;
    int sourceVerts = 0;
    int sourceIndexes = 0;
    int anchorTriangle = -1;
    const bool usingDoomSurfaces = CaptureDoomSurfacesForSmokeTest(viewDef, vertexData, indexData, sourceSurfaces, sourceVerts, sourceIndexes, anchorTriangle);
    if (!usingDoomSurfaces)
    {
        if (!m_smokeWaitingForDoomSurfaceLogged)
        {
            common->Printf("PathTracePrimaryPass: waiting for Doom surfaces to build RT smoke BLAS\n");
            m_smokeWaitingForDoomSurfaceLogged = true;
        }
        return;
    }

    nvrhi::BufferDesc vertexBufferDesc;
    vertexBufferDesc.byteSize = vertexData.size() * sizeof(vertexData[0]);
    vertexBufferDesc.debugName = "PathTraceSmokeDoomSurfaceVertices";
    vertexBufferDesc.isVertexBuffer = true;
    vertexBufferDesc.isAccelStructBuildInput = true;
    vertexBufferDesc.initialState = nvrhi::ResourceStates::Common;
    vertexBufferDesc.keepInitialState = true;
    m_smokeVertexBuffer = device->createBuffer(vertexBufferDesc);

    nvrhi::BufferDesc indexBufferDesc;
    indexBufferDesc.byteSize = indexData.size() * sizeof(indexData[0]);
    indexBufferDesc.debugName = "PathTraceSmokeDoomSurfaceIndices";
    indexBufferDesc.isIndexBuffer = true;
    indexBufferDesc.isAccelStructBuildInput = true;
    indexBufferDesc.initialState = nvrhi::ResourceStates::Common;
    indexBufferDesc.keepInitialState = true;
    m_smokeIndexBuffer = device->createBuffer(indexBufferDesc);

    if (!m_smokeVertexBuffer || !m_smokeIndexBuffer)
    {
        common->Printf("PathTracePrimaryPass: failed to create RT smoke geometry buffers\n");
        return;
    }

    nvrhi::rt::GeometryTriangles triangleGeometry;
    triangleGeometry.indexBuffer = m_smokeIndexBuffer;
    triangleGeometry.vertexBuffer = m_smokeVertexBuffer;
    triangleGeometry.indexFormat = nvrhi::Format::R32_UINT;
    triangleGeometry.vertexFormat = nvrhi::Format::RGB32_FLOAT;
    triangleGeometry.indexCount = static_cast<uint32_t>(indexData.size());
    triangleGeometry.vertexCount = static_cast<uint32_t>(vertexData.size() / 3);
    triangleGeometry.vertexStride = sizeof(float) * 3;

    nvrhi::rt::GeometryDesc geometryDesc;
    geometryDesc.setTriangles(triangleGeometry);
    geometryDesc.setFlags(nvrhi::rt::GeometryFlags::Opaque);

    m_smokeBlasDesc = nvrhi::rt::AccelStructDesc()
        .addBottomLevelGeometry(geometryDesc)
        .setBuildFlags(nvrhi::rt::AccelStructBuildFlags::PreferFastTrace)
        .setDebugName("PathTraceSmokeDoomSurfaceBLAS");

    m_smokeBlas = device->createAccelStruct(m_smokeBlasDesc);
    if (!m_smokeBlas)
    {
        common->Printf("PathTracePrimaryPass: failed to create RT smoke BLAS\n");
        return;
    }

    commandList->beginTrackingBufferState(m_smokeVertexBuffer, nvrhi::ResourceStates::Common);
    commandList->beginTrackingBufferState(m_smokeIndexBuffer, nvrhi::ResourceStates::Common);
    commandList->writeBuffer(m_smokeVertexBuffer, vertexData.data(), vertexData.size() * sizeof(vertexData[0]));
    commandList->writeBuffer(m_smokeIndexBuffer, indexData.data(), indexData.size() * sizeof(indexData[0]));
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

    common->Printf("PathTracePrimaryPass: built RT smoke BLAS/TLAS from %d Doom surfaces (%d verts, %d indexes, anchor triangle %d)\n",
        sourceSurfaces, sourceVerts, sourceIndexes, anchorTriangle);
}

void PathTracePrimaryPass::ExecuteRayTracingSmokeTest()
{
    if (m_smokeTestDispatched || !m_smokeSceneBuilt || !m_smokeShaderTable || !m_smokeBindingSet || !m_smokeOutputTexture || !m_smokeReadbackTexture)
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
    commandList->setTextureState(m_smokeOutputTexture, nvrhi::AllSubresources, nvrhi::ResourceStates::UnorderedAccess);
    commandList->commitBarriers();
    commandList->clearTextureFloat(m_smokeOutputTexture, nvrhi::AllSubresources, nvrhi::Color(0.25f, 0.50f, 0.75f, 1.0f));
    commandList->setRayTracingState(state);

    nvrhi::rt::DispatchRaysArguments args;
    args.width = 1;
    args.height = 1;
    args.depth = 1;
    commandList->dispatchRays(args);

    commandList->setTextureState(m_smokeOutputTexture, nvrhi::AllSubresources, nvrhi::ResourceStates::CopySource);
    commandList->commitBarriers();
    commandList->copyTexture(m_smokeReadbackTexture, nvrhi::TextureSlice(), m_smokeOutputTexture, nvrhi::TextureSlice());

    m_smokeTestDispatched = true;
    m_smokeReadbackQueued = true;
    m_smokeReadbackDelayFrames = 2;
    common->Printf("PathTracePrimaryPass: dispatched RT smoke raygen\n");
    common->Printf("PathTracePrimaryPass: queued RT smoke UAV readback\n");
}

void PathTracePrimaryPass::ReadBackRayTracingSmokeTest()
{
    if (!m_smokeReadbackQueued || m_smokeReadbackLogged || !m_smokeReadbackTexture)
    {
        return;
    }

    if (m_smokeReadbackDelayFrames > 0)
    {
        --m_smokeReadbackDelayFrames;
        return;
    }

    nvrhi::IDevice* device = deviceManager ? deviceManager->GetDevice() : nullptr;
    if (!device)
    {
        return;
    }

    device->waitForIdle();

    size_t rowPitch = 0;
    void* readbackData = device->mapStagingTexture(m_smokeReadbackTexture, nvrhi::TextureSlice(), nvrhi::CpuAccessMode::Read, &rowPitch);
    if (!readbackData)
    {
        common->Printf("PathTracePrimaryPass: RT smoke UAV readback map failed\n");
        m_smokeReadbackLogged = true;
        return;
    }

    const float* rgba = static_cast<const float*>(readbackData);
    common->Printf("PathTracePrimaryPass: RT smoke UAV readback rgba=(%.3f, %.3f, %.3f, %.3f), rowPitch=%u\n",
        rgba[0], rgba[1], rgba[2], rgba[3], static_cast<unsigned int>(rowPitch));

    device->unmapStagingTexture(m_smokeReadbackTexture);
    m_smokeReadbackLogged = true;
}
