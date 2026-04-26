#include "precompiled.h"
#pragma hdrstop

#include "PathTracePrimaryPass.h"
#include "../RenderCommon.h"
#include "../RenderBackend.h"
#include "../GLMatrix.h"
#include "../Passes/CommonPasses.h"
#include "../../framework/Common_local.h"
#include "../../sys/DeviceManager.h"

#include <nvrhi/utils.h>
#include <vector>

extern DeviceManager* deviceManager;

idCVar r_pathTracingDebugMode(
    "r_pathTracingDebugMode",
    "0",
    CVAR_RENDERER | CVAR_INTEGER | CVAR_ARCHIVE,
    "RT smoke debug output mode: 0 = hit/miss, 1 = depth, 2 = geometric normal" );

namespace {

const int RT_SMOKE_MAX_SURFACES = 8;
const int RT_SMOKE_MAX_VERTS = 4096;
const int RT_SMOKE_MAX_INDEXES = 12288;
const int RT_SMOKE_OUTPUT_WIDTH = 320;
const int RT_SMOKE_OUTPUT_HEIGHT = 180;
const int RT_SMOKE_READBACK_INTERVAL_FRAMES = 120;

struct PathTraceSmokeConstants
{
    float cameraOriginAndTMax[4];
    float cameraForwardAndTanX[4];
    float cameraLeftAndTanY[4];
    float cameraUpAndDebugMode[4];
};

bool TransformSurfacePointToWorld(const drawSurf_t* drawSurf, const idVec3& localPoint, idVec3& worldPoint)
{
    if (drawSurf->space)
    {
        R_LocalPointToGlobal(drawSurf->space->modelMatrix, localPoint, worldPoint);
        return true;
    }

    worldPoint = localPoint;
    return true;
}

bool IntersectRayTriangle(const idVec3& rayOrigin, const idVec3& rayDirection, const idVec3& p0, const idVec3& p1, const idVec3& p2, float& hitDistance)
{
    const idVec3 edge1 = p1 - p0;
    const idVec3 edge2 = p2 - p0;
    const idVec3 pvec = rayDirection.Cross(edge2);
    const float det = edge1 * pvec;
    if (idMath::Fabs(det) < 1.0e-8f)
    {
        return false;
    }

    const float invDet = 1.0f / det;
    const idVec3 tvec = rayOrigin - p0;
    const float u = (tvec * pvec) * invDet;
    if (u < 0.0f || u > 1.0f)
    {
        return false;
    }

    const idVec3 qvec = tvec.Cross(edge1);
    const float v = (rayDirection * qvec) * invDet;
    if (v < 0.0f || u + v > 1.0f)
    {
        return false;
    }

    const float t = (edge2 * qvec) * invDet;
    if (t <= 0.1f)
    {
        return false;
    }

    hitDistance = t;
    return true;
}

bool FindCenterCameraRayAnchor(const viewDef_t* viewDef, idVec3& anchorPoint, int& anchorSurface, int& anchorTriangle)
{
    const idVec3 rayOrigin = viewDef->renderView.vieworg;
    idVec3 rayDirection = viewDef->renderView.viewaxis[0];
    rayDirection.Normalize();

    bool foundHit = false;
    float closestHit = 1.0e30f;
    anchorSurface = -1;
    anchorTriangle = -1;

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

        for (int index = 0; index + 2 < tri->numIndexes; index += 3)
        {
            const int i0 = tri->indexes[index + 0];
            const int i1 = tri->indexes[index + 1];
            const int i2 = tri->indexes[index + 2];
            if (i0 < 0 || i1 < 0 || i2 < 0 || i0 >= tri->numVerts || i1 >= tri->numVerts || i2 >= tri->numVerts)
            {
                continue;
            }

            idVec3 p0;
            idVec3 p1;
            idVec3 p2;
            TransformSurfacePointToWorld(drawSurf, tri->verts[i0].xyz, p0);
            TransformSurfacePointToWorld(drawSurf, tri->verts[i1].xyz, p1);
            TransformSurfacePointToWorld(drawSurf, tri->verts[i2].xyz, p2);

            float hitDistance = 0.0f;
            if (IntersectRayTriangle(rayOrigin, rayDirection, p0, p1, p2, hitDistance) && hitDistance < closestHit)
            {
                closestHit = hitDistance;
                anchorPoint = rayOrigin + rayDirection * hitDistance;
                anchorSurface = surfaceIndex;
                anchorTriangle = index / 3;
                foundHit = true;
            }
        }
    }

    return foundHit;
}

bool CaptureDoomSurfacesForSmokeTest(const viewDef_t* viewDef, std::vector<float>& vertexData, std::vector<uint32_t>& indexData, idVec3& sceneOrigin, int& sourceSurfaces, int& sourceVerts, int& sourceIndexes, int& anchorTriangle)
{
    sourceSurfaces = 0;
    sourceVerts = 0;
    sourceIndexes = 0;
    int anchorSurface = -1;
    anchorTriangle = -1;

    if (!viewDef || !viewDef->drawSurfs)
    {
        return false;
    }

    if (!FindCenterCameraRayAnchor(viewDef, sceneOrigin, anchorSurface, anchorTriangle))
    {
        return false;
    }

    vertexData.clear();
    indexData.clear();
    vertexData.reserve(RT_SMOKE_MAX_VERTS * 3);
    indexData.reserve(RT_SMOKE_MAX_INDEXES);

    for (int surfaceOffset = 0; surfaceOffset < viewDef->numDrawSurfs; ++surfaceOffset)
    {
        const int surfaceIndex = (anchorSurface + surfaceOffset) % viewDef->numDrawSurfs;
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

        if (sourceSurfaces >= RT_SMOKE_MAX_SURFACES ||
            sourceVerts + tri->numVerts > RT_SMOKE_MAX_VERTS ||
            sourceIndexes + tri->numIndexes > RT_SMOKE_MAX_INDEXES)
        {
            break;
        }

        const uint32_t indexBase = static_cast<uint32_t>(sourceVerts);
        for (int vertexIndex = 0; vertexIndex < tri->numVerts; ++vertexIndex)
        {
            idVec3 worldPosition;
            TransformSurfacePointToWorld(drawSurf, tri->verts[vertexIndex].xyz, worldPosition);
            const idVec3 position = worldPosition - sceneOrigin;
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
    , m_smokeSceneRebuildLogged(false)
    , m_smokeTestDispatched(false)
    , m_smokeWaitingForDoomSurfaceLogged(false)
    , m_smokeReadbackQueued(false)
    , m_smokeReadbackLogged(false)
    , m_smokeReadbackDelayFrames(0)
    , m_smokeReadbackCooldownFrames(0)
    , m_smokeSceneOrigin(vec3_origin)
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
    ExecuteRayTracingSmokeTest(viewDef);
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

    nvrhi::BufferDesc constantsDesc;
    constantsDesc.byteSize = sizeof(PathTraceSmokeConstants);
    constantsDesc.debugName = "PathTraceSmokeConstants";
    constantsDesc.isConstantBuffer = true;
    constantsDesc.initialState = nvrhi::ResourceStates::ConstantBuffer;
    constantsDesc.keepInitialState = true;
    m_smokeConstantsBuffer = device->createBuffer(constantsDesc);

    if (!m_smokeConstantsBuffer)
    {
        common->Printf("PathTracePrimaryPass: failed to create RT smoke constants buffer\n");
        return;
    }

    nvrhi::TextureDesc outputDesc;
    outputDesc.width = RT_SMOKE_OUTPUT_WIDTH;
    outputDesc.height = RT_SMOKE_OUTPUT_HEIGHT;
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
        .setConstantBufferOffset(0)
        .setUnorderedAccessViewOffset(0);
    bindingLayoutDesc.bindings = {
        nvrhi::BindingLayoutItem::RayTracingAccelStruct(0),
        nvrhi::BindingLayoutItem::Texture_UAV(1),
        nvrhi::BindingLayoutItem::ConstantBuffer(2),
        nvrhi::BindingLayoutItem::StructuredBuffer_SRV(3),
        nvrhi::BindingLayoutItem::StructuredBuffer_SRV(4)
    };
    m_smokeBindingLayout = device->createBindingLayout(bindingLayoutDesc);

    if (!m_smokeBindingLayout)
    {
        common->Printf("PathTracePrimaryPass: failed to create RT smoke binding layout\n");
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
    pipelineDesc.maxPayloadSize = 32;
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
    common->Printf("PathTracePrimaryPass: RT smoke output UAV initialized (%dx%d)\n", RT_SMOKE_OUTPUT_WIDTH, RT_SMOKE_OUTPUT_HEIGHT);
}

void PathTracePrimaryPass::BuildRayTracingSmokeTestScene(const viewDef_t* viewDef)
{
    m_smokeSceneBuilt = false;

    if (!m_smokeTlas)
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
    const bool usingDoomSurfaces = CaptureDoomSurfacesForSmokeTest(viewDef, vertexData, indexData, m_smokeSceneOrigin, sourceSurfaces, sourceVerts, sourceIndexes, anchorTriangle);
    if (!usingDoomSurfaces)
    {
        if (!m_smokeWaitingForDoomSurfaceLogged)
        {
            common->Printf("PathTracePrimaryPass: waiting for center camera ray Doom surface hit to build RT smoke BLAS\n");
            m_smokeWaitingForDoomSurfaceLogged = true;
        }
        return;
    }

    nvrhi::BufferDesc vertexBufferDesc;
    vertexBufferDesc.byteSize = vertexData.size() * sizeof(vertexData[0]);
    vertexBufferDesc.debugName = "PathTraceSmokeDoomSurfaceVertices";
    vertexBufferDesc.structStride = sizeof(float) * 3;
    vertexBufferDesc.isVertexBuffer = true;
    vertexBufferDesc.isAccelStructBuildInput = true;
    vertexBufferDesc.initialState = nvrhi::ResourceStates::Common;
    vertexBufferDesc.keepInitialState = true;
    nvrhi::BufferHandle smokeVertexBuffer = device->createBuffer(vertexBufferDesc);

    nvrhi::BufferDesc indexBufferDesc;
    indexBufferDesc.byteSize = indexData.size() * sizeof(indexData[0]);
    indexBufferDesc.debugName = "PathTraceSmokeDoomSurfaceIndices";
    indexBufferDesc.structStride = sizeof(uint32_t);
    indexBufferDesc.isIndexBuffer = true;
    indexBufferDesc.isAccelStructBuildInput = true;
    indexBufferDesc.initialState = nvrhi::ResourceStates::Common;
    indexBufferDesc.keepInitialState = true;
    nvrhi::BufferHandle smokeIndexBuffer = device->createBuffer(indexBufferDesc);

    if (!smokeVertexBuffer || !smokeIndexBuffer)
    {
        common->Printf("PathTracePrimaryPass: failed to create RT smoke geometry buffers\n");
        return;
    }

    nvrhi::rt::GeometryTriangles triangleGeometry;
    triangleGeometry.indexBuffer = smokeIndexBuffer;
    triangleGeometry.vertexBuffer = smokeVertexBuffer;
    triangleGeometry.indexFormat = nvrhi::Format::R32_UINT;
    triangleGeometry.vertexFormat = nvrhi::Format::RGB32_FLOAT;
    triangleGeometry.indexCount = static_cast<uint32_t>(indexData.size());
    triangleGeometry.vertexCount = static_cast<uint32_t>(vertexData.size() / 3);
    triangleGeometry.vertexStride = sizeof(float) * 3;

    nvrhi::rt::GeometryDesc geometryDesc;
    geometryDesc.setTriangles(triangleGeometry);
    geometryDesc.setFlags(nvrhi::rt::GeometryFlags::Opaque);

    nvrhi::rt::AccelStructDesc smokeBlasDesc = nvrhi::rt::AccelStructDesc()
        .addBottomLevelGeometry(geometryDesc)
        .setBuildFlags(nvrhi::rt::AccelStructBuildFlags::PreferFastTrace)
        .setDebugName("PathTraceSmokeDoomSurfaceBLAS");

    nvrhi::rt::AccelStructHandle smokeBlas = device->createAccelStruct(smokeBlasDesc);
    if (!smokeBlas)
    {
        common->Printf("PathTracePrimaryPass: failed to create RT smoke BLAS\n");
        return;
    }

    commandList->beginTrackingBufferState(smokeVertexBuffer, nvrhi::ResourceStates::Common);
    commandList->beginTrackingBufferState(smokeIndexBuffer, nvrhi::ResourceStates::Common);
    commandList->writeBuffer(smokeVertexBuffer, vertexData.data(), vertexData.size() * sizeof(vertexData[0]));
    commandList->writeBuffer(smokeIndexBuffer, indexData.data(), indexData.size() * sizeof(indexData[0]));
    commandList->setBufferState(smokeVertexBuffer, nvrhi::ResourceStates::AccelStructBuildInput);
    commandList->setBufferState(smokeIndexBuffer, nvrhi::ResourceStates::AccelStructBuildInput);
    commandList->commitBarriers();

    nvrhi::utils::BuildBottomLevelAccelStruct(commandList, smokeBlas, smokeBlasDesc);

    nvrhi::rt::InstanceDesc instanceDesc;
    instanceDesc.setInstanceMask(0xff);
    instanceDesc.setInstanceContributionToHitGroupIndex(0);
    instanceDesc.setFlags(nvrhi::rt::InstanceFlags::TriangleCullDisable);
    instanceDesc.setBLAS(smokeBlas);

    commandList->buildTopLevelAccelStruct(m_smokeTlas, &instanceDesc, 1, nvrhi::rt::AccelStructBuildFlags::PreferFastTrace);

    nvrhi::BindingSetDesc bindingSetDesc;
    bindingSetDesc.bindings = {
        nvrhi::BindingSetItem::RayTracingAccelStruct(0, m_smokeTlas),
        nvrhi::BindingSetItem::Texture_UAV(1, m_smokeOutputTexture),
        nvrhi::BindingSetItem::ConstantBuffer(2, m_smokeConstantsBuffer),
        nvrhi::BindingSetItem::StructuredBuffer_SRV(3, smokeVertexBuffer),
        nvrhi::BindingSetItem::StructuredBuffer_SRV(4, smokeIndexBuffer)
    };
    nvrhi::BindingSetHandle smokeBindingSet = device->createBindingSet(bindingSetDesc, m_smokeBindingLayout);
    if (!smokeBindingSet)
    {
        common->Printf("PathTracePrimaryPass: failed to create RT smoke binding set\n");
        return;
    }

    m_smokeVertexBuffer = smokeVertexBuffer;
    m_smokeIndexBuffer = smokeIndexBuffer;
    m_smokeBlasDesc = smokeBlasDesc;
    m_smokeBlas = smokeBlas;
    m_smokeBindingSet = smokeBindingSet;
    m_smokeSceneBuilt = true;

    if (!m_smokeSceneRebuildLogged)
    {
        common->Printf("PathTracePrimaryPass: rebuilding RT smoke BLAS/TLAS every frame from current visible Doom surfaces (first frame: %d surfaces, %d verts, %d indexes, anchor triangle %d)\n",
            sourceSurfaces, sourceVerts, sourceIndexes, anchorTriangle);
        m_smokeSceneRebuildLogged = true;
    }
}

void PathTracePrimaryPass::ExecuteRayTracingSmokeTest(const viewDef_t* viewDef)
{
    if (!viewDef || !m_smokeSceneBuilt || !m_smokeShaderTable || !m_smokeBindingSet || !m_smokeOutputTexture || !m_smokeReadbackTexture || !m_smokeConstantsBuffer)
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
    const int debugMode = idMath::ClampInt(0, 2, r_pathTracingDebugMode.GetInteger());

    idVec3 cameraOrigin = viewDef->renderView.vieworg - m_smokeSceneOrigin;
    idVec3 cameraForward = viewDef->renderView.viewaxis[0];
    idVec3 cameraLeft = viewDef->renderView.viewaxis[1];
    idVec3 cameraUp = viewDef->renderView.viewaxis[2];
    cameraForward.Normalize();
    cameraLeft.Normalize();
    cameraUp.Normalize();

    PathTraceSmokeConstants constants = {};
    constants.cameraOriginAndTMax[0] = cameraOrigin.x;
    constants.cameraOriginAndTMax[1] = cameraOrigin.y;
    constants.cameraOriginAndTMax[2] = cameraOrigin.z;
    constants.cameraOriginAndTMax[3] = 100000.0f;
    constants.cameraForwardAndTanX[0] = cameraForward.x;
    constants.cameraForwardAndTanX[1] = cameraForward.y;
    constants.cameraForwardAndTanX[2] = cameraForward.z;
    constants.cameraForwardAndTanX[3] = idMath::Tan(DEG2RAD(viewDef->renderView.fov_x * 0.5f));
    constants.cameraLeftAndTanY[0] = cameraLeft.x;
    constants.cameraLeftAndTanY[1] = cameraLeft.y;
    constants.cameraLeftAndTanY[2] = cameraLeft.z;
    constants.cameraLeftAndTanY[3] = idMath::Tan(DEG2RAD(viewDef->renderView.fov_y * 0.5f));
    constants.cameraUpAndDebugMode[0] = cameraUp.x;
    constants.cameraUpAndDebugMode[1] = cameraUp.y;
    constants.cameraUpAndDebugMode[2] = cameraUp.z;
    constants.cameraUpAndDebugMode[3] = static_cast<float>(debugMode);

    commandList->writeBuffer(m_smokeConstantsBuffer, &constants, sizeof(constants));
    commandList->setBufferState(m_smokeVertexBuffer, nvrhi::ResourceStates::ShaderResource);
    commandList->setBufferState(m_smokeIndexBuffer, nvrhi::ResourceStates::ShaderResource);
    commandList->setTextureState(m_smokeOutputTexture, nvrhi::AllSubresources, nvrhi::ResourceStates::UnorderedAccess);
    commandList->commitBarriers();
    commandList->clearTextureFloat(m_smokeOutputTexture, nvrhi::AllSubresources, nvrhi::Color(0.25f, 0.50f, 0.75f, 1.0f));
    commandList->setRayTracingState(state);

    nvrhi::rt::DispatchRaysArguments args;
    args.width = RT_SMOKE_OUTPUT_WIDTH;
    args.height = RT_SMOKE_OUTPUT_HEIGHT;
    args.depth = 1;
    commandList->dispatchRays(args);

    if (!m_smokeReadbackQueued && m_smokeReadbackCooldownFrames <= 0)
    {
        commandList->setTextureState(m_smokeOutputTexture, nvrhi::AllSubresources, nvrhi::ResourceStates::CopySource);
        commandList->commitBarriers();
        commandList->copyTexture(m_smokeReadbackTexture, nvrhi::TextureSlice(), m_smokeOutputTexture, nvrhi::TextureSlice());
        m_smokeReadbackQueued = true;
        m_smokeReadbackDelayFrames = 2;
        common->Printf("PathTracePrimaryPass: queued RT smoke UAV readback\n");
    }

    if (!m_smokeTestDispatched)
    {
        common->Printf("PathTracePrimaryPass: dispatched RT smoke camera raygen (%dx%d, debugMode=%d)\n", RT_SMOKE_OUTPUT_WIDTH, RT_SMOKE_OUTPUT_HEIGHT, debugMode);
    }
    m_smokeTestDispatched = true;
}

void PathTracePrimaryPass::ReadBackRayTracingSmokeTest()
{
    if (!m_smokeReadbackQueued || !m_smokeReadbackTexture)
    {
        if (m_smokeReadbackCooldownFrames > 0)
        {
            --m_smokeReadbackCooldownFrames;
        }
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
        m_smokeReadbackQueued = false;
        m_smokeReadbackCooldownFrames = RT_SMOKE_READBACK_INTERVAL_FRAMES;
        return;
    }

    const int sampleX = RT_SMOKE_OUTPUT_WIDTH / 2;
    const int sampleY = RT_SMOKE_OUTPUT_HEIGHT / 2;
    const byte* readbackBytes = static_cast<const byte*>(readbackData);
    const float* centerRgba = reinterpret_cast<const float*>(readbackBytes + rowPitch * sampleY + sizeof(float) * 4 * sampleX);

    int greenHits = 0;
    int redMisses = 0;
    for (int y = 0; y < RT_SMOKE_OUTPUT_HEIGHT; ++y)
    {
        const float* row = reinterpret_cast<const float*>(readbackBytes + rowPitch * y);
        for (int x = 0; x < RT_SMOKE_OUTPUT_WIDTH; ++x)
        {
            const float* rgba = row + x * 4;
            if (rgba[1] > 0.5f)
            {
                ++greenHits;
            }
            else if (rgba[0] > 0.5f)
            {
                ++redMisses;
            }
        }
    }

    common->Printf("PathTracePrimaryPass: RT smoke UAV readback %dx%d center rgba=(%.3f, %.3f, %.3f, %.3f), hits=%d, misses=%d, rowPitch=%u\n",
        RT_SMOKE_OUTPUT_WIDTH, RT_SMOKE_OUTPUT_HEIGHT,
        centerRgba[0], centerRgba[1], centerRgba[2], centerRgba[3],
        greenHits, redMisses, static_cast<unsigned int>(rowPitch));

    device->unmapStagingTexture(m_smokeReadbackTexture);
    m_smokeReadbackLogged = true;
    m_smokeReadbackQueued = false;
    m_smokeReadbackCooldownFrames = RT_SMOKE_READBACK_INTERVAL_FRAMES;
}

void PathTracePrimaryPass::PresentDebugOutput()
{
    if (!deviceManager)
    {
        return;
    }

    nvrhi::IFramebuffer* targetFramebuffer = deviceManager->GetCurrentFramebuffer();
    if (!targetFramebuffer)
    {
        return;
    }

    BlitDebugOutput(targetFramebuffer, nvrhi::Viewport(renderSystem->GetNativeWidth(), renderSystem->GetNativeHeight()));
}

void PathTracePrimaryPass::BlitDebugOutput(nvrhi::IFramebuffer* targetFramebuffer, const nvrhi::Viewport& targetViewport)
{
    if (!m_smokeTestDispatched || !m_smokeOutputTexture || !m_backend || !targetFramebuffer)
    {
        return;
    }

    nvrhi::ICommandList* commandList = m_backend->GL_GetCommandList();
    if (!commandList)
    {
        return;
    }

    commandList->setTextureState(m_smokeOutputTexture, nvrhi::AllSubresources, nvrhi::ResourceStates::ShaderResource);
    commandList->commitBarriers();

    BlitParameters blitParms;
    blitParms.sourceTexture = m_smokeOutputTexture;
    blitParms.targetFramebuffer = targetFramebuffer;
    blitParms.targetViewport = targetViewport;
    blitParms.sampler = BlitSampler::Point;
    m_backend->GetCommonPasses().BlitTexture(commandList, blitParms, nullptr);
}
