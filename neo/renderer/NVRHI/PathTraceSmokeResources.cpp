#include "precompiled.h"
#pragma hdrstop

// Persistent NVRHI resource lifetime for the RT smoke path.
//
// This module creates the long-lived pipeline/output resources and commits each
// successfully built scene into PathTracePrimaryPass state. Scene build code may
// package handles here, but it should not reset or mutate persistent ownership
// directly.

#include "PathTracePrimaryPass.h"
#include "PathTraceDoomLights.h"
#include "PathTraceReservoirs.h"
#include "PathTraceSmokeDispatch.h"
#include "PathTraceSmokeResources.h"
#include "PathTraceTextureRegistry.h"
#include "../../sys/DeviceManager.h"

extern DeviceManager* deviceManager;

bool RtSmokeSceneBufferHandles::IsValid() const
{
    return staticVertexBuffer && staticIndexBuffer && staticTriangleClassBuffer && staticTriangleMaterialBuffer && staticTriangleMaterialIndexBuffer &&
        dynamicVertexBuffer && dynamicIndexBuffer && dynamicTriangleClassBuffer && dynamicTriangleMaterialBuffer && dynamicTriangleMaterialIndexBuffer &&
        materialTableBuffer && emissiveTriangleBuffer && lightCandidateBuffer && doomAnalyticLightBuffer &&
        rigidRouteVertexBuffer && rigidRouteIndexBuffer && rigidRouteTriangleMaterialBuffer && rigidRouteTriangleMaterialIndexBuffer && rigidRouteInstanceBuffer;
}

static size_t SmokeBufferRequiredBytes(size_t byteSize, uint32_t structStride)
{
    return byteSize > structStride ? byteSize : structStride;
}

static bool SmokeBufferHasCapacity(nvrhi::BufferHandle buffer, size_t byteSize, uint32_t structStride)
{
    return buffer && buffer->getDesc().byteSize >= SmokeBufferRequiredBytes(byteSize, structStride);
}

static nvrhi::BufferHandle CreateSmokeGeometryBuffer(nvrhi::IDevice* device, const char* debugName, size_t byteSize, uint32_t structStride, bool vertexBuffer, bool indexBuffer, bool accelStructInput)
{
    if (!device)
    {
        return nullptr;
    }

    nvrhi::BufferDesc desc;
    desc.byteSize = SmokeBufferRequiredBytes(byteSize, structStride);
    desc.debugName = debugName;
    desc.structStride = structStride;
    desc.isVertexBuffer = vertexBuffer;
    desc.isIndexBuffer = indexBuffer;
    desc.isAccelStructBuildInput = accelStructInput;
    desc.initialState = nvrhi::ResourceStates::Common;
    desc.keepInitialState = true;
    return device->createBuffer(desc);
}

static nvrhi::BufferHandle ReuseOrCreateSmokeGeometryBuffer(nvrhi::IDevice* device, nvrhi::BufferHandle existingBuffer, const char* debugName, size_t byteSize, uint32_t structStride, bool vertexBuffer, bool indexBuffer, bool accelStructInput)
{
    if (SmokeBufferHasCapacity(existingBuffer, byteSize, structStride))
    {
        return existingBuffer;
    }

    return CreateSmokeGeometryBuffer(device, debugName, byteSize, structStride, vertexBuffer, indexBuffer, accelStructInput);
}

RtSmokeSceneBufferCreateResult CreateSmokeSceneBuffers(const RtSmokeSceneBufferCreateDesc& desc)
{
    RtSmokeSceneBufferCreateResult result;
    result.buffers.staticVertexBuffer = ReuseOrCreateSmokeGeometryBuffer(desc.device, desc.existingBuffers.staticVertexBuffer, "PathTraceSmokeStaticWorldVertices", desc.staticVertexBytes, sizeof(PathTraceSmokeVertex), true, false, true);
    result.buffers.staticIndexBuffer = ReuseOrCreateSmokeGeometryBuffer(desc.device, desc.existingBuffers.staticIndexBuffer, "PathTraceSmokeStaticWorldIndices", desc.staticIndexBytes, sizeof(uint32_t), false, true, true);
    result.buffers.staticTriangleClassBuffer = ReuseOrCreateSmokeGeometryBuffer(desc.device, desc.existingBuffers.staticTriangleClassBuffer, "PathTraceSmokeStaticWorldTriangleClasses", desc.staticTriangleClassBytes, sizeof(uint32_t), false, false, false);
    result.buffers.staticTriangleMaterialBuffer = ReuseOrCreateSmokeGeometryBuffer(desc.device, desc.existingBuffers.staticTriangleMaterialBuffer, "PathTraceSmokeStaticWorldTriangleMaterials", desc.staticTriangleMaterialBytes, sizeof(uint32_t), false, false, false);
    result.buffers.staticTriangleMaterialIndexBuffer = ReuseOrCreateSmokeGeometryBuffer(desc.device, desc.existingBuffers.staticTriangleMaterialIndexBuffer, "PathTraceSmokeStaticWorldTriangleMaterialIndexes", desc.staticTriangleMaterialIndexBytes, sizeof(uint32_t), false, false, false);
    result.buffers.dynamicVertexBuffer = ReuseOrCreateSmokeGeometryBuffer(desc.device, desc.existingBuffers.dynamicVertexBuffer, "PathTraceSmokeDynamicCandidateVertices", desc.dynamicVertexBytes, sizeof(PathTraceSmokeVertex), true, false, true);
    result.buffers.dynamicIndexBuffer = ReuseOrCreateSmokeGeometryBuffer(desc.device, desc.existingBuffers.dynamicIndexBuffer, "PathTraceSmokeDynamicCandidateIndices", desc.dynamicIndexBytes, sizeof(uint32_t), false, true, true);
    result.buffers.dynamicTriangleClassBuffer = ReuseOrCreateSmokeGeometryBuffer(desc.device, desc.existingBuffers.dynamicTriangleClassBuffer, "PathTraceSmokeDynamicCandidateTriangleClasses", desc.dynamicTriangleClassBytes, sizeof(uint32_t), false, false, false);
    result.buffers.dynamicTriangleMaterialBuffer = ReuseOrCreateSmokeGeometryBuffer(desc.device, desc.existingBuffers.dynamicTriangleMaterialBuffer, "PathTraceSmokeDynamicCandidateTriangleMaterials", desc.dynamicTriangleMaterialBytes, sizeof(uint32_t), false, false, false);
    result.buffers.dynamicTriangleMaterialIndexBuffer = ReuseOrCreateSmokeGeometryBuffer(desc.device, desc.existingBuffers.dynamicTriangleMaterialIndexBuffer, "PathTraceSmokeDynamicCandidateTriangleMaterialIndexes", desc.dynamicTriangleMaterialIndexBytes, sizeof(uint32_t), false, false, false);
    result.buffers.materialTableBuffer = ReuseOrCreateSmokeGeometryBuffer(desc.device, desc.existingBuffers.materialTableBuffer, "PathTraceSmokeMaterialTable", desc.materialTableBytes, sizeof(PathTraceSmokeMaterial), false, false, false);
    result.buffers.emissiveTriangleBuffer = ReuseOrCreateSmokeGeometryBuffer(desc.device, desc.existingBuffers.emissiveTriangleBuffer, "PathTraceSmokeEmissiveTriangles", desc.emissiveTriangleBytes, sizeof(PathTraceSmokeEmissiveTriangle), false, false, false);
    result.buffers.lightCandidateBuffer = ReuseOrCreateSmokeGeometryBuffer(desc.device, desc.existingBuffers.lightCandidateBuffer, "PathTraceSmokeLightCandidates", desc.lightCandidateBytes, sizeof(PathTraceSmokeLightCandidate), false, false, false);
    result.buffers.doomAnalyticLightBuffer = ReuseOrCreateSmokeGeometryBuffer(desc.device, desc.existingBuffers.doomAnalyticLightBuffer, "PathTraceDoomAnalyticLights", desc.doomAnalyticLightBytes, sizeof(PathTraceDoomAnalyticLightCandidate), false, false, false);
    result.buffers.rigidRouteVertexBuffer = ReuseOrCreateSmokeGeometryBuffer(desc.device, desc.existingBuffers.rigidRouteVertexBuffer, "PathTraceRigidRouteVertices", desc.rigidRouteVertexBytes, sizeof(PathTraceSmokeVertex), false, false, false);
    result.buffers.rigidRouteIndexBuffer = ReuseOrCreateSmokeGeometryBuffer(desc.device, desc.existingBuffers.rigidRouteIndexBuffer, "PathTraceRigidRouteIndices", desc.rigidRouteIndexBytes, sizeof(uint32_t), false, false, false);
    result.buffers.rigidRouteTriangleMaterialBuffer = ReuseOrCreateSmokeGeometryBuffer(desc.device, desc.existingBuffers.rigidRouteTriangleMaterialBuffer, "PathTraceRigidRouteTriangleMaterials", desc.rigidRouteTriangleMaterialBytes, sizeof(uint32_t), false, false, false);
    result.buffers.rigidRouteTriangleMaterialIndexBuffer = ReuseOrCreateSmokeGeometryBuffer(desc.device, desc.existingBuffers.rigidRouteTriangleMaterialIndexBuffer, "PathTraceRigidRouteTriangleMaterialIndexes", desc.rigidRouteTriangleMaterialIndexBytes, sizeof(uint32_t), false, false, false);
    result.buffers.rigidRouteInstanceBuffer = ReuseOrCreateSmokeGeometryBuffer(desc.device, desc.existingBuffers.rigidRouteInstanceBuffer, "PathTraceRigidRouteInstances", desc.rigidRouteInstanceBytes, sizeof(PathTraceRigidRouteInstance), false, false, false);

    if (!result.buffers.IsValid())
    {
        result.errorMessage = "failed to create RT smoke geometry buffers";
    }
    return result;
}

RtSmokeBindingBuildResult CreateSmokeBindingResources(const RtSmokeBindingBuildDesc& desc, RtSmokeMaterialTableBuild& materialTable)
{
    OPTICK_EVENT("PT Binding Resources Detail");

    RtSmokeBindingBuildResult result;
    result.textureDescriptorTable = desc.existingTextureDescriptorTable;

    if (!desc.device || !desc.bindingLayout || !desc.tlas || !desc.outputTexture || !desc.accumulationTexture || !desc.fallbackTexture || !desc.constantsBuffer || !desc.restirPTConstantsBuffer || !desc.boundsOverlayLineBuffer || !desc.sampler || !desc.buffers.IsValid() || !desc.reservoirBuffers.IsValidFor(desc.reservoirBuffers.width, desc.reservoirBuffers.height) || !desc.restirPTReservoirBuffers.IsValidFor(desc.restirPTReservoirBuffers.width, desc.restirPTReservoirBuffers.height, rtxdi::CheckerboardMode::Off) || !desc.primarySurfaceHistoryBuffers.IsValidFor(desc.primarySurfaceHistoryBuffers.width, desc.primarySurfaceHistoryBuffers.height))
    {
        result.errorMessage = "failed to create RT smoke binding set";
        return result;
    }

    nvrhi::BindingSetDesc bindingSetDesc;
    {
        OPTICK_EVENT("PT Binding Set Desc");
        bindingSetDesc.bindings = {
            nvrhi::BindingSetItem::RayTracingAccelStruct(0, desc.tlas),
            nvrhi::BindingSetItem::Texture_UAV(1, desc.outputTexture),
            nvrhi::BindingSetItem::ConstantBuffer(2, desc.constantsBuffer),
            nvrhi::BindingSetItem::StructuredBuffer_SRV(3, desc.buffers.staticVertexBuffer),
            nvrhi::BindingSetItem::StructuredBuffer_SRV(4, desc.buffers.staticIndexBuffer),
            nvrhi::BindingSetItem::StructuredBuffer_SRV(5, desc.buffers.staticTriangleClassBuffer),
            nvrhi::BindingSetItem::StructuredBuffer_SRV(6, desc.buffers.dynamicVertexBuffer),
            nvrhi::BindingSetItem::StructuredBuffer_SRV(7, desc.buffers.dynamicIndexBuffer),
            nvrhi::BindingSetItem::StructuredBuffer_SRV(8, desc.buffers.dynamicTriangleClassBuffer),
            nvrhi::BindingSetItem::StructuredBuffer_SRV(9, desc.buffers.staticTriangleMaterialBuffer),
            nvrhi::BindingSetItem::StructuredBuffer_SRV(10, desc.buffers.dynamicTriangleMaterialBuffer),
            nvrhi::BindingSetItem::StructuredBuffer_SRV(11, desc.buffers.staticTriangleMaterialIndexBuffer),
            nvrhi::BindingSetItem::StructuredBuffer_SRV(12, desc.buffers.dynamicTriangleMaterialIndexBuffer),
            nvrhi::BindingSetItem::StructuredBuffer_SRV(13, desc.buffers.materialTableBuffer)
        };
    }

    result.activeTextureTable.push_back(desc.fallbackTexture);
    if (desc.enableTextureProbe)
    {
        if (!result.textureDescriptorTable)
        {
            OPTICK_EVENT("PT Create Texture Descriptor Table");
            result.textureDescriptorTable = desc.device->createDescriptorTable(desc.textureBindlessLayout);
        }
        if (!result.textureDescriptorTable)
        {
            result.errorMessage = "failed to create RT smoke texture descriptor table";
            return result;
        }

        const int textureSlotCount = idMath::ClampInt(1, desc.maxActiveTextures, Max(static_cast<int>(materialTable.diffuseTextures.size()), 1));
        result.activeTextureTable.reserve(textureSlotCount + 1);
        {
            OPTICK_EVENT("PT Build Active Texture Table");
            for (int textureSlot = 0; textureSlot < textureSlotCount; ++textureSlot)
            {
                nvrhi::TextureHandle texture = desc.fallbackTexture;
                if (!desc.forceFallbackTexture && textureSlot >= 0 && textureSlot < static_cast<int>(materialTable.diffuseTextures.size()))
                {
                    const nvrhi::TextureHandle candidateTexture = materialTable.diffuseTextures[textureSlot];
                    if (candidateTexture)
                    {
                        texture = candidateTexture;
                    }
                }
                if (!IsSmokeTextureHandleSafeForDescriptor(texture))
                {
                    ++materialTable.descriptorsReplacedWithFallback;
                    texture = desc.fallbackTexture;
                }

                result.activeTextureTable.push_back(texture);
            }
        }

        {
            OPTICK_EVENT("PT Write Texture Descriptor Table");
            for (int textureSlot = 0; textureSlot < textureSlotCount; ++textureSlot)
            {
                nvrhi::TextureHandle texture = result.activeTextureTable[textureSlot + 1];
                if (!desc.device->writeDescriptorTable(result.textureDescriptorTable, nvrhi::BindingSetItem::Texture_SRV(textureSlot, texture)))
                {
                    result.errorMessage = "failed to write RT smoke bindless texture descriptor slot";
                    result.failedTextureSlot = textureSlot;
                    return result;
                }
            }
        }
    }

    {
        OPTICK_EVENT("PT Final Binding Set Items");
        bindingSetDesc.addItem(nvrhi::BindingSetItem::Texture_SRV(14, desc.fallbackTexture));
        bindingSetDesc.addItem(nvrhi::BindingSetItem::Texture_UAV(15, desc.accumulationTexture));
        bindingSetDesc.addItem(nvrhi::BindingSetItem::StructuredBuffer_SRV(16, desc.buffers.emissiveTriangleBuffer));
        bindingSetDesc.addItem(nvrhi::BindingSetItem::StructuredBuffer_SRV(17, desc.buffers.lightCandidateBuffer));
        bindingSetDesc.addItem(nvrhi::BindingSetItem::StructuredBuffer_UAV(18, desc.reservoirBuffers.current));
        bindingSetDesc.addItem(nvrhi::BindingSetItem::StructuredBuffer_UAV(19, desc.reservoirBuffers.previous));
        bindingSetDesc.addItem(nvrhi::BindingSetItem::StructuredBuffer_UAV(20, desc.reservoirBuffers.spatialScratch));
        bindingSetDesc.addItem(nvrhi::BindingSetItem::StructuredBuffer_SRV(21, desc.boundsOverlayLineBuffer));
        bindingSetDesc.addItem(nvrhi::BindingSetItem::StructuredBuffer_SRV(22, desc.buffers.rigidRouteVertexBuffer));
        bindingSetDesc.addItem(nvrhi::BindingSetItem::StructuredBuffer_SRV(23, desc.buffers.rigidRouteIndexBuffer));
        bindingSetDesc.addItem(nvrhi::BindingSetItem::StructuredBuffer_SRV(24, desc.buffers.rigidRouteTriangleMaterialBuffer));
        bindingSetDesc.addItem(nvrhi::BindingSetItem::StructuredBuffer_SRV(25, desc.buffers.rigidRouteTriangleMaterialIndexBuffer));
        bindingSetDesc.addItem(nvrhi::BindingSetItem::StructuredBuffer_SRV(26, desc.buffers.rigidRouteInstanceBuffer));
        bindingSetDesc.addItem(nvrhi::BindingSetItem::StructuredBuffer_SRV(27, desc.buffers.doomAnalyticLightBuffer));
        bindingSetDesc.addItem(nvrhi::BindingSetItem::ConstantBuffer(28, desc.restirPTConstantsBuffer));
        bindingSetDesc.addItem(nvrhi::BindingSetItem::StructuredBuffer_UAV(29, desc.restirPTReservoirBuffers.reservoirs));
        bindingSetDesc.addItem(nvrhi::BindingSetItem::StructuredBuffer_UAV(30, desc.primarySurfaceHistoryBuffers.current));
        bindingSetDesc.addItem(nvrhi::BindingSetItem::StructuredBuffer_UAV(31, desc.primarySurfaceHistoryBuffers.previous));
        bindingSetDesc.addItem(nvrhi::BindingSetItem::Sampler(0, desc.sampler));
    }

    {
        OPTICK_EVENT("PT Create Binding Set");
        result.bindingSet = desc.device->createBindingSet(bindingSetDesc, desc.bindingLayout);
    }
    if (!result.bindingSet)
    {
        result.errorMessage = "failed to create RT smoke binding set";
    }

    return result;
}

RtSmokeSceneResourceCommitDesc CreateSmokeSceneResourceCommitDesc(const RtSmokeSceneResourceCommitBuildDesc& desc)
{
    RtSmokeSceneResourceCommitDesc commitDesc;
    commitDesc.buffers = desc.buffers;
    commitDesc.staticBlasDesc = desc.staticBlasDesc;
    commitDesc.dynamicBlasDesc = desc.dynamicBlasDesc;
    commitDesc.staticBlas = desc.staticBlas;
    commitDesc.dynamicBlas = desc.dynamicBlas;
    commitDesc.hasStaticBlas = desc.hasStaticBlas;
    commitDesc.staticBlasSignature = desc.staticBlasSignature;
    commitDesc.bindingSet = desc.bindingSet;
    commitDesc.textureDescriptorTable = desc.textureDescriptorTable;
    if (desc.activeTextureTable)
    {
        commitDesc.activeTextureTable = *desc.activeTextureTable;
    }
    commitDesc.materialTableEntryCount = desc.materialTableEntryCount;
    commitDesc.emissiveTriangleCount = desc.emissiveTriangleCount;
    commitDesc.emissiveStaticTriangleCount = desc.emissiveStaticTriangleCount;
    commitDesc.emissiveDynamicTriangleCount = desc.emissiveDynamicTriangleCount;
    commitDesc.lightCandidateCount = desc.lightCandidateCount;
    commitDesc.texturedLightCandidateCount = desc.texturedLightCandidateCount;
    commitDesc.lightCandidateBytes = desc.lightCandidateBytes;
    commitDesc.doomAnalyticLightCount = desc.doomAnalyticLightCount;
    commitDesc.doomAnalyticLightBytes = desc.doomAnalyticLightBytes;
    commitDesc.reservoirSceneSignature = desc.reservoirSceneSignature;
    return commitDesc;
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
        .setTopLevelMaxInstances(512)
        .setBuildFlags(nvrhi::rt::AccelStructBuildFlags::PreferFastTrace)
        .setDebugName("PathTraceSmokeTLAS"));

    if (!m_smokeTlas)
    {
        common->Printf("PathTracePrimaryPass: failed to create RT smoke TLAS\n");
        return;
    }

    nvrhi::BufferDesc constantsDesc;
    constantsDesc.byteSize = GetPathTraceSmokeConstantsSize();
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

    nvrhi::BufferDesc restirPTConstantsDesc;
    restirPTConstantsDesc.byteSize = GetRestirPTParametersSize();
    restirPTConstantsDesc.debugName = "PathTraceRestirPTParameters";
    restirPTConstantsDesc.isConstantBuffer = true;
    restirPTConstantsDesc.initialState = nvrhi::ResourceStates::ConstantBuffer;
    restirPTConstantsDesc.keepInitialState = true;
    m_restirPTConstantsBuffer = device->createBuffer(restirPTConstantsDesc);

    if (!m_restirPTConstantsBuffer)
    {
        common->Printf("PathTracePrimaryPass: failed to create RT ReSTIR PT parameters buffer\n");
        return;
    }

    nvrhi::BufferDesc boundsOverlayDesc;
    boundsOverlayDesc.byteSize = sizeof(RtPathTraceBoundsOverlayLine) * RT_PT_BOUNDS_OVERLAY_MAX_LINES;
    boundsOverlayDesc.debugName = "PathTraceSmokeBoundsOverlayLines";
    boundsOverlayDesc.structStride = sizeof(RtPathTraceBoundsOverlayLine);
    boundsOverlayDesc.initialState = nvrhi::ResourceStates::Common;
    boundsOverlayDesc.keepInitialState = true;
    m_smokeBoundsOverlayLineBuffer = device->createBuffer(boundsOverlayDesc);

    if (!m_smokeBoundsOverlayLineBuffer)
    {
        common->Printf("PathTracePrimaryPass: failed to create RT smoke bounds overlay buffer\n");
        return;
    }

    nvrhi::BindingLayoutDesc bindingLayoutDesc;
    bindingLayoutDesc.visibility = nvrhi::ShaderType::AllRayTracing;
    bindingLayoutDesc.bindingOffsets = nvrhi::VulkanBindingOffsets()
        .setShaderResourceOffset(0)
        .setConstantBufferOffset(0)
        .setUnorderedAccessViewOffset(0);
    bindingLayoutDesc.addItem(nvrhi::BindingLayoutItem::RayTracingAccelStruct(0));
    bindingLayoutDesc.addItem(nvrhi::BindingLayoutItem::Texture_UAV(1));
    bindingLayoutDesc.addItem(nvrhi::BindingLayoutItem::ConstantBuffer(2));
    bindingLayoutDesc.addItem(nvrhi::BindingLayoutItem::StructuredBuffer_SRV(3));
    bindingLayoutDesc.addItem(nvrhi::BindingLayoutItem::StructuredBuffer_SRV(4));
    bindingLayoutDesc.addItem(nvrhi::BindingLayoutItem::StructuredBuffer_SRV(5));
    bindingLayoutDesc.addItem(nvrhi::BindingLayoutItem::StructuredBuffer_SRV(6));
    bindingLayoutDesc.addItem(nvrhi::BindingLayoutItem::StructuredBuffer_SRV(7));
    bindingLayoutDesc.addItem(nvrhi::BindingLayoutItem::StructuredBuffer_SRV(8));
    bindingLayoutDesc.addItem(nvrhi::BindingLayoutItem::StructuredBuffer_SRV(9));
    bindingLayoutDesc.addItem(nvrhi::BindingLayoutItem::StructuredBuffer_SRV(10));
    bindingLayoutDesc.addItem(nvrhi::BindingLayoutItem::StructuredBuffer_SRV(11));
    bindingLayoutDesc.addItem(nvrhi::BindingLayoutItem::StructuredBuffer_SRV(12));
    bindingLayoutDesc.addItem(nvrhi::BindingLayoutItem::StructuredBuffer_SRV(13));
    bindingLayoutDesc.addItem(nvrhi::BindingLayoutItem::Texture_SRV(14));
    bindingLayoutDesc.addItem(nvrhi::BindingLayoutItem::Texture_UAV(15));
    bindingLayoutDesc.addItem(nvrhi::BindingLayoutItem::StructuredBuffer_SRV(16));
    bindingLayoutDesc.addItem(nvrhi::BindingLayoutItem::StructuredBuffer_SRV(17));
    bindingLayoutDesc.addItem(nvrhi::BindingLayoutItem::StructuredBuffer_UAV(18));
    bindingLayoutDesc.addItem(nvrhi::BindingLayoutItem::StructuredBuffer_UAV(19));
    bindingLayoutDesc.addItem(nvrhi::BindingLayoutItem::StructuredBuffer_UAV(20));
    bindingLayoutDesc.addItem(nvrhi::BindingLayoutItem::StructuredBuffer_SRV(21));
    bindingLayoutDesc.addItem(nvrhi::BindingLayoutItem::StructuredBuffer_SRV(22));
    bindingLayoutDesc.addItem(nvrhi::BindingLayoutItem::StructuredBuffer_SRV(23));
    bindingLayoutDesc.addItem(nvrhi::BindingLayoutItem::StructuredBuffer_SRV(24));
    bindingLayoutDesc.addItem(nvrhi::BindingLayoutItem::StructuredBuffer_SRV(25));
    bindingLayoutDesc.addItem(nvrhi::BindingLayoutItem::StructuredBuffer_SRV(26));
    bindingLayoutDesc.addItem(nvrhi::BindingLayoutItem::StructuredBuffer_SRV(27));
    bindingLayoutDesc.addItem(nvrhi::BindingLayoutItem::ConstantBuffer(28));
    bindingLayoutDesc.addItem(nvrhi::BindingLayoutItem::StructuredBuffer_UAV(29));
    bindingLayoutDesc.addItem(nvrhi::BindingLayoutItem::StructuredBuffer_UAV(30));
    bindingLayoutDesc.addItem(nvrhi::BindingLayoutItem::StructuredBuffer_UAV(31));
    bindingLayoutDesc.addItem(nvrhi::BindingLayoutItem::Sampler(0));
    m_smokeBindingLayout = device->createBindingLayout(bindingLayoutDesc);

    if (!m_smokeBindingLayout)
    {
        common->Printf("PathTracePrimaryPass: failed to create RT smoke binding layout\n");
        return;
    }

    nvrhi::BindlessLayoutDesc textureBindlessLayoutDesc;
    textureBindlessLayoutDesc
        .setVisibility(nvrhi::ShaderType::AllRayTracing)
        .setFirstSlot(0)
        .setMaxCapacity(RT_SMOKE_TEXTURE_DESCRIPTOR_CAPACITY)
        .addRegisterSpace(nvrhi::BindingLayoutItem::Texture_SRV(1));
    m_smokeTextureBindlessLayout = device->createBindlessLayout(textureBindlessLayoutDesc);
    if (!m_smokeTextureBindlessLayout)
    {
        common->Printf("PathTracePrimaryPass: failed to create RT smoke bindless texture layout\n");
        return;
    }

    m_smokeTextureDescriptorTable = device->createDescriptorTable(m_smokeTextureBindlessLayout);
    if (!m_smokeTextureDescriptorTable)
    {
        common->Printf("PathTracePrimaryPass: failed to create RT smoke texture descriptor table\n");
        return;
    }

    nvrhi::rt::PipelineDesc pipelineDesc;
    pipelineDesc.globalBindingLayouts = { m_smokeBindingLayout, m_smokeTextureBindlessLayout };
    pipelineDesc.shaders = {
        { "", m_smokeShaderLibrary->getShader("RayGen", nvrhi::ShaderType::RayGeneration), nullptr },
        { "", m_smokeShaderLibrary->getShader("Miss", nvrhi::ShaderType::Miss), nullptr }
    };
    pipelineDesc.hitGroups = {
        {
            "HitGroup",
            m_smokeShaderLibrary->getShader("ClosestHit", nvrhi::ShaderType::ClosestHit),
            m_smokeShaderLibrary->getShader("AnyHit", nvrhi::ShaderType::AnyHit),
            nullptr,
            nullptr,
            false
        }
    };
    pipelineDesc.maxPayloadSize = 64;
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

bool PathTracePrimaryPass::ResizeRayTracingSmokeOutput(int width, int height)
{
    if (!m_smokeTestInitialized || !m_smokeShaderTable)
    {
        return false;
    }

    if (m_smokeOutputTexture && m_smokeAccumulationTexture && m_smokeReadbackTexture &&
        m_smokeReservoirBuffers.IsValidFor(width, height) &&
        m_restirPTReservoirBuffers.IsValidFor(width, height, rtxdi::CheckerboardMode::Off) &&
        m_restirPTPrimarySurfaceHistoryBuffers.IsValidFor(width, height) &&
        width == m_smokeOutputWidth && height == m_smokeOutputHeight)
    {
        return true;
    }

    nvrhi::IDevice* device = deviceManager ? deviceManager->GetDevice() : nullptr;
    if (!device)
    {
        return false;
    }

    const bool replacingExistingOutput =
        m_smokeOutputTexture || m_smokeAccumulationTexture || m_smokeReadbackTexture ||
        m_smokeReservoirBuffers.current || m_smokeReservoirBuffers.previous || m_smokeReservoirBuffers.spatialScratch ||
        m_restirPTReservoirBuffers.reservoirs ||
        m_restirPTPrimarySurfaceHistoryBuffers.current || m_restirPTPrimarySurfaceHistoryBuffers.previous;
    if (replacingExistingOutput)
    {
        common->Printf("PathTracePrimaryPass: resizing PT output old=%dx%d new=%dx%d; waiting for GPU idle\n",
            m_smokeOutputWidth, m_smokeOutputHeight, width, height);
        device->waitForIdle();
        device->runGarbageCollection();
    }

    nvrhi::TextureDesc outputDesc;
    outputDesc.width = width;
    outputDesc.height = height;
    outputDesc.mipLevels = 1;
    outputDesc.arraySize = 1;
    outputDesc.format = nvrhi::Format::RGBA32_FLOAT;
    outputDesc.dimension = nvrhi::TextureDimension::Texture2D;
    outputDesc.isUAV = true;
    outputDesc.initialState = nvrhi::ResourceStates::UnorderedAccess;
    outputDesc.keepInitialState = true;
    outputDesc.debugName = "PathTraceSmokeOutput";
    nvrhi::TextureHandle outputTexture = device->createTexture(outputDesc);

    if (!outputTexture)
    {
        common->Printf("PathTracePrimaryPass: failed to create RT smoke output UAV (%dx%d)\n", width, height);
        return false;
    }

    outputDesc.debugName = "PathTraceSmokeAccumulation";
    nvrhi::TextureHandle accumulationTexture = device->createTexture(outputDesc);
    if (!accumulationTexture)
    {
        common->Printf("PathTracePrimaryPass: failed to create RT smoke accumulation UAV (%dx%d)\n", width, height);
        return false;
    }

    nvrhi::TextureDesc readbackDesc = outputDesc;
    readbackDesc.isShaderResource = false;
    readbackDesc.isUAV = false;
    readbackDesc.initialState = nvrhi::ResourceStates::Unknown;
    readbackDesc.keepInitialState = false;
    readbackDesc.debugName = "PathTraceSmokeReadback";
    nvrhi::StagingTextureHandle readbackTexture = device->createStagingTexture(readbackDesc, nvrhi::CpuAccessMode::Read);

    if (!readbackTexture)
    {
        common->Printf("PathTracePrimaryPass: failed to create RT smoke readback texture (%dx%d)\n", width, height);
        return false;
    }

    m_smokeOutputTexture = outputTexture;
    m_smokeAccumulationTexture = accumulationTexture;
    m_smokeReadbackTexture = readbackTexture;
    m_smokeOutputWidth = width;
    m_smokeOutputHeight = height;
    ResetRayTracingSmokeSceneResources();

    RtSmokeReservoirBufferCreateDesc reservoirDesc;
    reservoirDesc.device = device;
    reservoirDesc.existingBuffers = m_smokeReservoirBuffers;
    reservoirDesc.width = width;
    reservoirDesc.height = height;
    const RtSmokeReservoirBufferCreateResult reservoirResult = CreateSmokeReservoirBuffers(reservoirDesc);
    if (!reservoirResult.Succeeded())
    {
        common->Printf("PathTracePrimaryPass: %s (%dx%d)\n", reservoirResult.errorMessage ? reservoirResult.errorMessage : "failed to create RT smoke ReSTIR reservoir buffers", width, height);
        return false;
    }
    m_smokeReservoirBuffers = reservoirResult.buffers;

    RtRestirPTReservoirBufferCreateDesc restirPTReservoirDesc;
    restirPTReservoirDesc.device = device;
    restirPTReservoirDesc.existingBuffers = m_restirPTReservoirBuffers;
    restirPTReservoirDesc.width = static_cast<uint32_t>(width);
    restirPTReservoirDesc.height = static_cast<uint32_t>(height);
    restirPTReservoirDesc.checkerboardMode = rtxdi::CheckerboardMode::Off;
    const RtRestirPTReservoirBufferCreateResult restirPTReservoirResult = CreateRestirPTReservoirBuffers(restirPTReservoirDesc);
    if (!restirPTReservoirResult.Succeeded())
    {
        common->Printf("PathTracePrimaryPass: %s (%dx%d)\n", restirPTReservoirResult.errorMessage ? restirPTReservoirResult.errorMessage : "failed to create RT ReSTIR PT packed reservoir buffer", width, height);
        return false;
    }
    m_restirPTReservoirBuffers = restirPTReservoirResult.buffers;

    RtRestirPTPrimarySurfaceHistoryBufferCreateDesc primaryHistoryDesc;
    primaryHistoryDesc.device = device;
    primaryHistoryDesc.existingBuffers = m_restirPTPrimarySurfaceHistoryBuffers;
    primaryHistoryDesc.width = static_cast<uint32_t>(width);
    primaryHistoryDesc.height = static_cast<uint32_t>(height);
    const RtRestirPTPrimarySurfaceHistoryBufferCreateResult primaryHistoryResult = CreateRestirPTPrimarySurfaceHistoryBuffers(primaryHistoryDesc);
    if (!primaryHistoryResult.Succeeded())
    {
        common->Printf("PathTracePrimaryPass: %s (%dx%d)\n", primaryHistoryResult.errorMessage ? primaryHistoryResult.errorMessage : "failed to create RT ReSTIR PT primary-surface history buffers", width, height);
        return false;
    }
    m_restirPTPrimarySurfaceHistoryBuffers = primaryHistoryResult.buffers;
    m_restirPTPrimarySurfaceHistoryNeedsClear = true;
    m_restirPTPrimarySurfaceHistoryViewValid = false;
    m_restirPTPrimarySurfaceHistoryViewWidth = 0;
    m_restirPTPrimarySurfaceHistoryViewHeight = 0;

    RtRestirPTContextUpdateDesc restirPTContextDesc;
    restirPTContextDesc.width = static_cast<uint32_t>(width);
    restirPTContextDesc.height = static_cast<uint32_t>(height);
    restirPTContextDesc.frameIndex = m_restirPTFrameIndex;
    restirPTContextDesc.checkerboardMode = rtxdi::CheckerboardMode::Off;
    restirPTContextDesc.resamplingMode = rtxdi::ReSTIRPT_ResamplingMode::None;
    if (!UpdateRestirPTContextState(m_restirPTContextState, restirPTContextDesc))
    {
        common->Printf("PathTracePrimaryPass: failed to initialize RT ReSTIR PT context (%dx%d)\n", width, height);
        return false;
    }

    common->Printf("PathTracePrimaryPass: RT ReSTIR PT packed reservoirs output=%dx%d elements=%u bytes=%llu arrayPitch=%u blockRowPitch=%u slices=%u stride=%u\n",
        width,
        height,
        m_restirPTReservoirBuffers.reservoirElementCount,
        static_cast<unsigned long long>(m_restirPTReservoirBuffers.reservoirBytes),
        m_restirPTReservoirBuffers.reservoirParams.reservoirArrayPitch,
        m_restirPTReservoirBuffers.reservoirParams.reservoirBlockRowPitch,
        rtxdi::c_NumReSTIRPTReservoirBuffers,
        static_cast<uint32_t>(sizeof(RTXDI_PackedPTReservoir)));

    common->Printf("PathTracePrimaryPass: RT ReSTIR PT primary-surface history output=%dx%d records=%u bytes=%llu stride=%u\n",
        width,
        height,
        m_restirPTPrimarySurfaceHistoryBuffers.surfaceCount,
        static_cast<unsigned long long>(m_restirPTPrimarySurfaceHistoryBuffers.surfaceBytes),
        RT_RESTIR_PT_PRIMARY_SURFACE_HISTORY_STRIDE);

    common->Printf("PathTracePrimaryPass: RT smoke output UAV initialized (%dx%d)\n", width, height);
    return true;
}

void PathTracePrimaryPass::InvalidateForBackBufferResize()
{
    ResetRayTracingSmokeSceneResources();
    m_smokeOutputTexture = nullptr;
    m_smokeAccumulationTexture = nullptr;
    m_smokeReadbackTexture = nullptr;
    m_smokeOutputWidth = 0;
    m_smokeOutputHeight = 0;
}

void PathTracePrimaryPass::ResetRayTracingSmokeSceneResources()
{
    m_smokeAccumulationSignature = 0;
    m_smokeAccumulationFrameCount = 0;
    m_smokeBindingSet = nullptr;
    m_smokeSceneBuilt = false;
    m_smokeTestDispatched = false;
    m_smokeReadbackQueued = false;
    m_smokeReadbackDelayFrames = 0;
    m_smokeReadbackCooldownFrames = 0;
    m_smokeStaticBlasCacheValid = false;
    m_smokeGeometryUniverse.Clear();
    m_sceneUniverse.Clear();
    m_instanceUniverse.Clear();
    m_smokeLightUniverse.Clear();
    m_smokeSceneRenderWorld = nullptr;
    m_smokeSceneMapName.Clear();
    m_smokeSceneMapTimeStamp = 0;
    m_smokeLightUniverseRenderWorld = nullptr;
    m_smokeStaticBlas = nullptr;
    m_smokeDynamicBlas = nullptr;
    m_smokeStaticVertexBuffer = nullptr;
    m_smokeStaticIndexBuffer = nullptr;
    m_smokeStaticTriangleClassBuffer = nullptr;
    m_smokeStaticTriangleMaterialBuffer = nullptr;
    m_smokeStaticTriangleMaterialIndexBuffer = nullptr;
    m_smokeDynamicVertexBuffer = nullptr;
    m_smokeDynamicIndexBuffer = nullptr;
    m_smokeDynamicTriangleClassBuffer = nullptr;
    m_smokeDynamicTriangleMaterialBuffer = nullptr;
    m_smokeDynamicTriangleMaterialIndexBuffer = nullptr;
    m_smokeMaterialTableBuffer = nullptr;
    m_smokeEmissiveTriangleBuffer = nullptr;
    m_smokeLightCandidateBuffer = nullptr;
    m_smokeDoomAnalyticLightBuffer = nullptr;
    m_smokeRigidRouteVertexBuffer = nullptr;
    m_smokeRigidRouteIndexBuffer = nullptr;
    m_smokeRigidRouteTriangleMaterialBuffer = nullptr;
    m_smokeRigidRouteTriangleMaterialIndexBuffer = nullptr;
    m_smokeRigidRouteInstanceBuffer = nullptr;
    m_smokeActiveTextureTable.clear();
    m_smokeMaterialTableEntryCount = 0;
    m_smokeEmissiveTriangleCount = 0;
    m_smokeEmissiveStaticTriangleCount = 0;
    m_smokeEmissiveDynamicTriangleCount = 0;
    m_smokeLightCandidateCount = 0;
    m_smokeTexturedLightCandidateCount = 0;
    m_smokeLightCandidateBytes = 0;
    m_smokeDoomAnalyticLightCount = 0;
    m_smokeDoomAnalyticLightBytes = 0;
    m_smokeReservoirBuffers.Reset();
    m_restirPTReservoirBuffers.Reset();
    m_restirPTPrimarySurfaceHistoryBuffers.Reset();
    m_restirPTContextState.Reset();
    m_smokeReservoirSceneSignature = 0;
    m_smokeReservoirDispatchSignature = 0;
    m_smokeReservoirNeedsClear = false;
    m_restirPTPrimarySurfaceHistoryNeedsClear = true;
    m_restirPTPrimarySurfaceHistoryViewValid = false;
    m_restirPTPrimarySurfaceHistoryViewWidth = 0;
    m_restirPTPrimarySurfaceHistoryViewHeight = 0;
    m_smokeReservoirResetCount = 0;
    m_smokeReservoirClearCount = 0;
}

void PathTracePrimaryPass::CommitRayTracingSmokeSceneResources(const RtSmokeSceneResourceCommitDesc& desc)
{
    m_smokeStaticVertexBuffer = desc.buffers.staticVertexBuffer;
    m_smokeStaticIndexBuffer = desc.buffers.staticIndexBuffer;
    m_smokeStaticTriangleClassBuffer = desc.buffers.staticTriangleClassBuffer;
    m_smokeStaticTriangleMaterialBuffer = desc.buffers.staticTriangleMaterialBuffer;
    m_smokeStaticTriangleMaterialIndexBuffer = desc.buffers.staticTriangleMaterialIndexBuffer;
    m_smokeDynamicVertexBuffer = desc.buffers.dynamicVertexBuffer;
    m_smokeDynamicIndexBuffer = desc.buffers.dynamicIndexBuffer;
    m_smokeDynamicTriangleClassBuffer = desc.buffers.dynamicTriangleClassBuffer;
    m_smokeDynamicTriangleMaterialBuffer = desc.buffers.dynamicTriangleMaterialBuffer;
    m_smokeDynamicTriangleMaterialIndexBuffer = desc.buffers.dynamicTriangleMaterialIndexBuffer;
    m_smokeMaterialTableBuffer = desc.buffers.materialTableBuffer;
    m_smokeEmissiveTriangleBuffer = desc.buffers.emissiveTriangleBuffer;
    m_smokeLightCandidateBuffer = desc.buffers.lightCandidateBuffer;
    m_smokeDoomAnalyticLightBuffer = desc.buffers.doomAnalyticLightBuffer;
    m_smokeRigidRouteVertexBuffer = desc.buffers.rigidRouteVertexBuffer;
    m_smokeRigidRouteIndexBuffer = desc.buffers.rigidRouteIndexBuffer;
    m_smokeRigidRouteTriangleMaterialBuffer = desc.buffers.rigidRouteTriangleMaterialBuffer;
    m_smokeRigidRouteTriangleMaterialIndexBuffer = desc.buffers.rigidRouteTriangleMaterialIndexBuffer;
    m_smokeRigidRouteInstanceBuffer = desc.buffers.rigidRouteInstanceBuffer;
    m_smokeStaticBlasDesc = desc.staticBlasDesc;
    m_smokeDynamicBlasDesc = desc.dynamicBlasDesc;
    m_smokeStaticBlas = desc.staticBlas;
    m_smokeDynamicBlas = desc.dynamicBlas;
    if (desc.hasStaticBlas)
    {
        m_smokeStaticBlasCacheValid = true;
        m_smokeStaticBlasSignature = desc.staticBlasSignature;
    }
    m_smokeBindingSet = desc.bindingSet;
    m_smokeTextureDescriptorTable = desc.textureDescriptorTable;
    m_smokeActiveTextureTable = desc.activeTextureTable;
    m_smokeMaterialTableEntryCount = desc.materialTableEntryCount;
    m_smokeEmissiveTriangleCount = desc.emissiveTriangleCount;
    m_smokeEmissiveStaticTriangleCount = desc.emissiveStaticTriangleCount;
    m_smokeEmissiveDynamicTriangleCount = desc.emissiveDynamicTriangleCount;
    m_smokeLightCandidateCount = desc.lightCandidateCount;
    m_smokeTexturedLightCandidateCount = desc.texturedLightCandidateCount;
    m_smokeLightCandidateBytes = desc.lightCandidateBytes;
    m_smokeDoomAnalyticLightCount = desc.doomAnalyticLightCount;
    m_smokeDoomAnalyticLightBytes = desc.doomAnalyticLightBytes;
    if (m_smokeReservoirSceneSignature != desc.reservoirSceneSignature)
    {
        m_smokeReservoirSceneSignature = desc.reservoirSceneSignature;
        m_smokeReservoirNeedsClear = true;
    }
    m_smokeSceneBuilt = true;
}
