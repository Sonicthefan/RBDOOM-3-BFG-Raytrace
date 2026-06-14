#include "precompiled.h"
#pragma hdrstop

// Clean-room Remix ReSTIR GI lane dispatch (docs/restir_remix_gi_cleanroom).
//
// RGI-01: isolated route with its own cvars, reservoir pages, producer
// textures, and a sentinel debug view.
// RGI-02: producer dispatch with the shared scene/light bindings so the GI
// raygen can trace the bounce ray and shade the secondary vertex.
// The lane never reads DI reservoirs and never writes anything when
// r_pathTracingCleanRestirGiEnable is 0.

#include "PathTraceCleanRestirGi.h"
#include "PathTraceCVars.h"
#include "../RenderCommon.h"
#include "../../sys/DeviceManager.h"

#include <cstring>

#include <nvrhi/utils.h>

#include <Rtxdi/RtxdiUtils.h>
#include <Rtxdi/GI/ReSTIRGIParameters.h>

extern DeviceManager* deviceManager;

namespace {

// Page roles inside the single GI reservoir buffer (RAB_GIReservoirBridge
// page info: x=init, y=temporalInput, z=temporalOutput, w=spatialOutput).
const uint32_t CLEAN_RESTIR_GI_PAGE_INIT = 0u;
const uint32_t CLEAN_RESTIR_GI_PAGE_TEMPORAL_INPUT = 1u;
const uint32_t CLEAN_RESTIR_GI_PAGE_TEMPORAL_OUTPUT = 2u;
const uint32_t CLEAN_RESTIR_GI_PAGE_SPATIAL_OUTPUT = 3u;
const uint32_t CLEAN_RESTIR_GI_PAGE_COUNT = 4u;

// Must match the DI sentinel constants blob size mirrored at the head of the
// GI cbuffer (PathTraceCleanRtxdiDiSentinelConstants).
const uint32_t CLEAN_RESTIR_GI_DI_BLOB_SIZE = 480u;

// GI-owned cbuffer tail; layout must match the trailing fields of
// PathTraceCleanRestirGiConstants in pathtrace_clean_restir_gi.rt.hlsl.
struct PathTraceCleanRestirGiConstantsTail
{
    uint32_t view;
    uint32_t temporalEnabled;
    uint32_t spatialEnabled;
    uint32_t biasCorrection;
    uint32_t jacobianEnabled;
    uint32_t maxHistoryLength;
    uint32_t maxReservoirAge;
    float fireflyThreshold;
    uint32_t neeCacheSeedEnabled;
    uint32_t frameIndex;
    uint32_t phase;
    uint32_t resolveEnabled;
    uint32_t specularProducerEnabled;
    uint32_t rrHitDistanceEnabled;
    uint32_t padding0[2];
    RTXDI_ReservoirBufferParameters reservoirParams;
    uint32_t pageInfo[4];
};
static_assert(sizeof(PathTraceCleanRestirGiConstantsTail) == 96, "GI constants tail must match the HLSL cbuffer tail layout");

const uint32_t CLEAN_RESTIR_GI_CONSTANTS_SIZE = CLEAN_RESTIR_GI_DI_BLOB_SIZE + sizeof(PathTraceCleanRestirGiConstantsTail);

bool CleanRestirGiEnsurePipeline(PathTraceCleanRestirGiState& state, const PathTraceCleanRestirGiDispatchInputs& inputs)
{
    if (state.shaderTable)
    {
        return true;
    }
    if (state.pipelineInitAttempted)
    {
        return false;
    }
    state.pipelineInitAttempted = true;

    const char* shaderPath = nullptr;
    if (inputs.isD3D12)
    {
        shaderPath = "renderprogs2/dxil/builtin/pathtracing/remix_restir_gi/pathtrace_clean_restir_gi.rt.bin";
    }
    else if (inputs.isVulkan)
    {
        shaderPath = "renderprogs2/spirv/builtin/pathtracing/remix_restir_gi/pathtrace_clean_restir_gi.rt.bin";
    }
    else
    {
        common->Printf("PathTraceCleanRestirGi: unsupported graphics API\n");
        return false;
    }

    void* shaderData = nullptr;
    ID_TIME_T shaderTimestamp = 0;
    const int shaderSize = fileSystem->ReadFile(shaderPath, &shaderData, &shaderTimestamp);
    if (shaderSize <= 0 || !shaderData)
    {
        common->Printf("PathTraceCleanRestirGi: couldn't read GI shader %s\n", shaderPath);
        return false;
    }
    state.shaderLibrary = inputs.device->createShaderLibrary(shaderData, shaderSize);
    Mem_Free(shaderData);
    if (!state.shaderLibrary)
    {
        common->Printf("PathTraceCleanRestirGi: failed to create GI shader library\n");
        return false;
    }

    if (!state.bindingLayout)
    {
        nvrhi::BindingLayoutDesc layoutDesc;
        layoutDesc.visibility = nvrhi::ShaderType::AllRayTracing;
        layoutDesc.bindingOffsets = nvrhi::VulkanBindingOffsets()
            .setShaderResourceOffset(0)
            .setConstantBufferOffset(0)
            .setUnorderedAccessViewOffset(0);
        layoutDesc.addItem(nvrhi::BindingLayoutItem::RayTracingAccelStruct(0));
        layoutDesc.addItem(nvrhi::BindingLayoutItem::Texture_UAV(1));
        layoutDesc.addItem(nvrhi::BindingLayoutItem::ConstantBuffer(2));
        layoutDesc.addItem(nvrhi::BindingLayoutItem::StructuredBuffer_SRV(3));
        layoutDesc.addItem(nvrhi::BindingLayoutItem::StructuredBuffer_SRV(4));
        layoutDesc.addItem(nvrhi::BindingLayoutItem::StructuredBuffer_SRV(5));
        layoutDesc.addItem(nvrhi::BindingLayoutItem::StructuredBuffer_SRV(6));
        layoutDesc.addItem(nvrhi::BindingLayoutItem::StructuredBuffer_SRV(7));
        layoutDesc.addItem(nvrhi::BindingLayoutItem::StructuredBuffer_SRV(8));
        layoutDesc.addItem(nvrhi::BindingLayoutItem::StructuredBuffer_SRV(9));
        layoutDesc.addItem(nvrhi::BindingLayoutItem::StructuredBuffer_SRV(10));
        layoutDesc.addItem(nvrhi::BindingLayoutItem::StructuredBuffer_SRV(11));
        layoutDesc.addItem(nvrhi::BindingLayoutItem::StructuredBuffer_SRV(12));
        layoutDesc.addItem(nvrhi::BindingLayoutItem::StructuredBuffer_SRV(13));
        layoutDesc.addItem(nvrhi::BindingLayoutItem::Texture_SRV(14));
        layoutDesc.addItem(nvrhi::BindingLayoutItem::StructuredBuffer_SRV(16));
        layoutDesc.addItem(nvrhi::BindingLayoutItem::StructuredBuffer_SRV(22));
        layoutDesc.addItem(nvrhi::BindingLayoutItem::StructuredBuffer_SRV(23));
        layoutDesc.addItem(nvrhi::BindingLayoutItem::StructuredBuffer_SRV(24));
        layoutDesc.addItem(nvrhi::BindingLayoutItem::StructuredBuffer_SRV(25));
        layoutDesc.addItem(nvrhi::BindingLayoutItem::StructuredBuffer_SRV(26));
        layoutDesc.addItem(nvrhi::BindingLayoutItem::StructuredBuffer_SRV(27));
        layoutDesc.addItem(nvrhi::BindingLayoutItem::StructuredBuffer_SRV(46));
        layoutDesc.addItem(nvrhi::BindingLayoutItem::StructuredBuffer_SRV(66));
        layoutDesc.addItem(nvrhi::BindingLayoutItem::StructuredBuffer_SRV(74));
        layoutDesc.addItem(nvrhi::BindingLayoutItem::StructuredBuffer_UAV(30));
        layoutDesc.addItem(nvrhi::BindingLayoutItem::StructuredBuffer_UAV(31));
        layoutDesc.addItem(nvrhi::BindingLayoutItem::Texture_UAV(39));
        layoutDesc.addItem(nvrhi::BindingLayoutItem::Texture_UAV(40));
        layoutDesc.addItem(nvrhi::BindingLayoutItem::StructuredBuffer_UAV(80));
        layoutDesc.addItem(nvrhi::BindingLayoutItem::Texture_UAV(81));
        layoutDesc.addItem(nvrhi::BindingLayoutItem::Texture_UAV(82));
        layoutDesc.addItem(nvrhi::BindingLayoutItem::Texture_UAV(83));
        layoutDesc.addItem(nvrhi::BindingLayoutItem::Texture_UAV(84));
        layoutDesc.addItem(nvrhi::BindingLayoutItem::Texture_UAV(85));
        layoutDesc.addItem(nvrhi::BindingLayoutItem::Texture_UAV(86));
        layoutDesc.addItem(nvrhi::BindingLayoutItem::Texture_UAV(51));
        layoutDesc.addItem(nvrhi::BindingLayoutItem::Texture_UAV(54));
        layoutDesc.addItem(nvrhi::BindingLayoutItem::Sampler(0));
        state.bindingLayout = inputs.device->createBindingLayout(layoutDesc);
        if (!state.bindingLayout)
        {
            common->Printf("PathTraceCleanRestirGi: failed to create GI binding layout\n");
            return false;
        }
    }

    nvrhi::ShaderHandle rayGen = state.shaderLibrary->getShader("RayGen", nvrhi::ShaderType::RayGeneration);
    nvrhi::ShaderHandle miss = state.shaderLibrary->getShader("Miss", nvrhi::ShaderType::Miss);
    nvrhi::ShaderHandle shadowMiss = state.shaderLibrary->getShader("ShadowMiss", nvrhi::ShaderType::Miss);
    nvrhi::ShaderHandle closestHit = state.shaderLibrary->getShader("ClosestHit", nvrhi::ShaderType::ClosestHit);
    nvrhi::ShaderHandle anyHit = state.shaderLibrary->getShader("AnyHit", nvrhi::ShaderType::AnyHit);
    nvrhi::ShaderHandle shadowClosestHit = state.shaderLibrary->getShader("ShadowClosestHit", nvrhi::ShaderType::ClosestHit);
    nvrhi::ShaderHandle shadowAnyHit = state.shaderLibrary->getShader("ShadowAnyHit", nvrhi::ShaderType::AnyHit);
    if (!rayGen || !miss || !shadowMiss || !closestHit || !anyHit || !shadowClosestHit || !shadowAnyHit)
    {
        common->Printf("PathTraceCleanRestirGi: GI shader library is missing required entry points\n");
        return false;
    }

    nvrhi::rt::PipelineDesc pipelineDesc;
    pipelineDesc.globalBindingLayouts = { state.bindingLayout, inputs.textureBindlessLayout };
    pipelineDesc.shaders = {
        { "", rayGen, nullptr },
        { "", miss, nullptr },
        { "", shadowMiss, nullptr }
    };
    pipelineDesc.hitGroups = {
        { "HitGroup", closestHit, anyHit, nullptr, nullptr, false },
        { "ShadowHitGroup", shadowClosestHit, shadowAnyHit, nullptr, nullptr, false }
    };
    pipelineDesc.maxPayloadSize = 64;
    pipelineDesc.maxAttributeSize = 8;
    pipelineDesc.maxRecursionDepth = 1;

    state.pipeline = inputs.device->createRayTracingPipeline(pipelineDesc);
    if (!state.pipeline)
    {
        common->Printf("PathTraceCleanRestirGi: failed to create GI pipeline\n");
        return false;
    }
    state.shaderTable = state.pipeline->createShaderTable();
    if (!state.shaderTable)
    {
        common->Printf("PathTraceCleanRestirGi: failed to create GI shader table\n");
        state.pipeline = nullptr;
        return false;
    }
    state.shaderTable->setRayGenerationShader("RayGen");
    state.shaderTable->addMissShader("Miss");
    state.shaderTable->addMissShader("ShadowMiss");
    state.shaderTable->addHitGroup("HitGroup");
    state.shaderTable->addHitGroup("ShadowHitGroup");
    common->Printf("PathTraceCleanRestirGi: GI pipeline initialized\n");
    return true;
}

struct PathTraceCleanRestirGiBoilingFilterConstants
{
    uint32_t width;
    uint32_t height;
    float threshold;
    uint32_t resolveEnabled;
};

bool CleanRestirGiEnsureBoilingFilterPipeline(PathTraceCleanRestirGiState& state, const PathTraceCleanRestirGiDispatchInputs& inputs)
{
    if (state.boilingFilterPipeline)
    {
        return true;
    }
    if (state.boilingFilterInitAttempted)
    {
        return false;
    }
    state.boilingFilterInitAttempted = true;

    const char* shaderPath = nullptr;
    if (inputs.isD3D12)
    {
        shaderPath = "renderprogs2/dxil/builtin/pathtracing/remix_restir_gi/pathtrace_clean_restir_gi_boiling_filter.cs.bin";
    }
    else if (inputs.isVulkan)
    {
        shaderPath = "renderprogs2/spirv/builtin/pathtracing/remix_restir_gi/pathtrace_clean_restir_gi_boiling_filter.cs.bin";
    }
    else
    {
        return false;
    }

    void* shaderData = nullptr;
    ID_TIME_T shaderTimestamp = 0;
    const int shaderSize = fileSystem->ReadFile(shaderPath, &shaderData, &shaderTimestamp);
    if (shaderSize <= 0 || !shaderData)
    {
        common->Printf("PathTraceCleanRestirGi: couldn't read GI boiling-filter shader %s\n", shaderPath);
        return false;
    }
    nvrhi::ShaderDesc csDesc;
    csDesc.shaderType = nvrhi::ShaderType::Compute;
    csDesc.entryName = "main";
    csDesc.debugName = "PathTraceCleanRestirGiBoilingFilterCS";
    state.boilingFilterShader = inputs.device->createShader(csDesc, shaderData, shaderSize);
    Mem_Free(shaderData);
    if (!state.boilingFilterShader)
    {
        common->Printf("PathTraceCleanRestirGi: failed to create GI boiling-filter shader\n");
        return false;
    }

    nvrhi::BindingLayoutDesc layoutDesc;
    layoutDesc.visibility = nvrhi::ShaderType::Compute;
    layoutDesc.bindingOffsets = nvrhi::VulkanBindingOffsets()
        .setShaderResourceOffset(0)
        .setConstantBufferOffset(0)
        .setUnorderedAccessViewOffset(0);
    layoutDesc.addItem(nvrhi::BindingLayoutItem::ConstantBuffer(0));
    layoutDesc.addItem(nvrhi::BindingLayoutItem::Texture_UAV(1));
    layoutDesc.addItem(nvrhi::BindingLayoutItem::Texture_UAV(2));
    layoutDesc.addItem(nvrhi::BindingLayoutItem::Texture_UAV(3));
    layoutDesc.addItem(nvrhi::BindingLayoutItem::StructuredBuffer_SRV(4));
    state.boilingFilterBindingLayout = inputs.device->createBindingLayout(layoutDesc);
    if (!state.boilingFilterBindingLayout)
    {
        common->Printf("PathTraceCleanRestirGi: failed to create GI boiling-filter binding layout\n");
        return false;
    }

    nvrhi::ComputePipelineDesc pipelineDesc;
    pipelineDesc.CS = state.boilingFilterShader;
    pipelineDesc.bindingLayouts = { state.boilingFilterBindingLayout };
    state.boilingFilterPipeline = inputs.device->createComputePipeline(pipelineDesc);
    if (!state.boilingFilterPipeline)
    {
        common->Printf("PathTraceCleanRestirGi: failed to create GI boiling-filter pipeline\n");
        return false;
    }

    nvrhi::BufferDesc constantsDesc;
    constantsDesc.byteSize = 16;
    constantsDesc.debugName = "PathTraceCleanRestirGiBoilingFilterConstants";
    constantsDesc.isConstantBuffer = true;
    constantsDesc.initialState = nvrhi::ResourceStates::ConstantBuffer;
    constantsDesc.keepInitialState = true;
    state.boilingFilterConstantsBuffer = inputs.device->createBuffer(constantsDesc);
    if (!state.boilingFilterConstantsBuffer)
    {
        common->Printf("PathTraceCleanRestirGi: failed to create GI boiling-filter constants buffer\n");
        state.boilingFilterPipeline = nullptr;
        return false;
    }
    common->Printf("PathTraceCleanRestirGi: GI boiling-filter pipeline initialized\n");
    return true;
}

bool CleanRestirGiEnsureResources(PathTraceCleanRestirGiState& state, const PathTraceCleanRestirGiDispatchInputs& inputs)
{
    const uint32_t width = static_cast<uint32_t>(Max(inputs.width, 1));
    const uint32_t height = static_cast<uint32_t>(Max(inputs.height, 1));

    if (!state.constantsBuffer)
    {
        nvrhi::BufferDesc constantsDesc;
        constantsDesc.byteSize = 768;
        constantsDesc.debugName = "PathTraceCleanRestirGiConstants";
        constantsDesc.isConstantBuffer = true;
        constantsDesc.initialState = nvrhi::ResourceStates::ConstantBuffer;
        constantsDesc.keepInitialState = true;
        state.constantsBuffer = inputs.device->createBuffer(constantsDesc);
        if (!state.constantsBuffer)
        {
            common->Printf("PathTraceCleanRestirGi: failed to create GI constants buffer\n");
            return false;
        }
    }
    if (!state.placeholderSrvBuffer)
    {
        nvrhi::BufferDesc placeholderDesc;
        placeholderDesc.debugName = "PathTraceCleanRestirGiPlaceholderSRV";
        placeholderDesc.byteSize = 256;
        placeholderDesc.structStride = sizeof(uint32_t);
        placeholderDesc.canHaveUAVs = false;
        placeholderDesc.canHaveTypedViews = false;
        placeholderDesc.initialState = nvrhi::ResourceStates::ShaderResource;
        placeholderDesc.keepInitialState = true;
        state.placeholderSrvBuffer = inputs.device->createBuffer(placeholderDesc);
        if (!state.placeholderSrvBuffer)
        {
            common->Printf("PathTraceCleanRestirGi: failed to create GI placeholder SRV buffer\n");
            return false;
        }
    }

    const RTXDI_ReservoirBufferParameters reservoirParams =
        rtxdi::CalculateReservoirBufferParameters(width, height, rtxdi::CheckerboardMode::Off);
    const uint64_t reservoirBytes =
        static_cast<uint64_t>(reservoirParams.reservoirArrayPitch) *
        static_cast<uint64_t>(CLEAN_RESTIR_GI_PAGE_COUNT) *
        static_cast<uint64_t>(sizeof(RTXDI_PackedGIReservoir));
    const bool reservoirValid =
        state.reservoirBuffer &&
        state.reservoirWidth == width &&
        state.reservoirHeight == height &&
        state.reservoirBuffer->getDesc().byteSize >= reservoirBytes;
    if (!reservoirValid)
    {
        nvrhi::BufferDesc reservoirDesc;
        reservoirDesc.byteSize = reservoirBytes;
        reservoirDesc.structStride = sizeof(RTXDI_PackedGIReservoir);
        reservoirDesc.canHaveUAVs = true;
        reservoirDesc.debugName = "PathTraceCleanRestirGiReservoirs";
        reservoirDesc.initialState = nvrhi::ResourceStates::UnorderedAccess;
        reservoirDesc.keepInitialState = true;
        state.reservoirBuffer = inputs.device->createBuffer(reservoirDesc);
        if (!state.reservoirBuffer)
        {
            common->Printf("PathTraceCleanRestirGi: failed to create GI reservoir buffer (%llu bytes)\n",
                static_cast<unsigned long long>(reservoirBytes));
            return false;
        }
        state.reservoirWidth = width;
        state.reservoirHeight = height;
        state.reservoirArrayPitch = reservoirParams.reservoirArrayPitch;
        state.reservoirBlockRowPitch = reservoirParams.reservoirBlockRowPitch;
        state.reservoirBytes = reservoirBytes;
        state.reservoirClearPending = true;
    }

    const bool texturesValid =
        state.producerRadianceTexture &&
        state.producerHitPositionTexture &&
        state.producerHitNormalTexture &&
        state.indirectDiffuseTexture &&
        state.indirectDiffuseLobeTexture &&
        state.indirectSpecularLobeTexture &&
        state.producerRadianceTexture->getDesc().width == width &&
        state.producerRadianceTexture->getDesc().height == height;
    if (!texturesValid)
    {
        nvrhi::TextureDesc producerDesc;
        producerDesc.width = width;
        producerDesc.height = height;
        producerDesc.mipLevels = 1;
        producerDesc.arraySize = 1;
        producerDesc.dimension = nvrhi::TextureDimension::Texture2D;
        producerDesc.isUAV = true;
        producerDesc.initialState = nvrhi::ResourceStates::UnorderedAccess;
        producerDesc.keepInitialState = true;

        producerDesc.format = nvrhi::Format::RGBA16_FLOAT;
        producerDesc.debugName = "PathTraceCleanRestirGiProducerRadiance";
        state.producerRadianceTexture = inputs.device->createTexture(producerDesc);

        producerDesc.format = nvrhi::Format::RGBA32_FLOAT;
        producerDesc.debugName = "PathTraceCleanRestirGiProducerHitPosition";
        state.producerHitPositionTexture = inputs.device->createTexture(producerDesc);

        producerDesc.format = nvrhi::Format::RGBA16_FLOAT;
        producerDesc.debugName = "PathTraceCleanRestirGiProducerHitNormal";
        state.producerHitNormalTexture = inputs.device->createTexture(producerDesc);

        producerDesc.format = nvrhi::Format::RGBA16_FLOAT;
        producerDesc.debugName = "PathTraceCleanRestirGiIndirectDiffuse";
        state.indirectDiffuseTexture = inputs.device->createTexture(producerDesc);

        producerDesc.debugName = "PathTraceCleanRestirGiIndirectDiffuseLobe";
        state.indirectDiffuseLobeTexture = inputs.device->createTexture(producerDesc);

        producerDesc.debugName = "PathTraceCleanRestirGiIndirectSpecularLobe";
        state.indirectSpecularLobeTexture = inputs.device->createTexture(producerDesc);

        if (!state.producerRadianceTexture || !state.producerHitPositionTexture || !state.producerHitNormalTexture ||
            !state.indirectDiffuseTexture || !state.indirectDiffuseLobeTexture || !state.indirectSpecularLobeTexture)
        {
            common->Printf("PathTraceCleanRestirGi: failed to create GI producer textures (%ux%u)\n", width, height);
            return false;
        }
    }

    return true;
}

} // namespace

void PathTraceCleanRestirGiState::ReleaseResources()
{
    constantsBuffer = nullptr;
    reservoirBuffer = nullptr;
    reservoirWidth = 0;
    reservoirHeight = 0;
    reservoirArrayPitch = 0;
    reservoirBlockRowPitch = 0;
    reservoirBytes = 0;
    reservoirClearPending = true;
    producerRadianceTexture = nullptr;
    producerHitPositionTexture = nullptr;
    producerHitNormalTexture = nullptr;
    indirectDiffuseTexture = nullptr;
    indirectDiffuseLobeTexture = nullptr;
    indirectSpecularLobeTexture = nullptr;
    placeholderSrvBuffer = nullptr;
    boilingFilterConstantsBuffer = nullptr;
    boilingFilterShader = nullptr;
    boilingFilterBindingLayout = nullptr;
    boilingFilterPipeline = nullptr;
    boilingFilterInitAttempted = false;
    bindingLayout = nullptr;
    shaderLibrary = nullptr;
    pipeline = nullptr;
    shaderTable = nullptr;
    pipelineInitAttempted = false;
}

bool PathTraceCleanRestirGiExecute(
    PathTraceCleanRestirGiState& state,
    const PathTraceCleanRestirGiDispatchInputs& inputs)
{
    const bool dumpRequested = r_pathTracingCleanRestirGiDump.GetInteger() != 0;
    auto printDump = [&](const char* earlyReturn)
    {
        if (!dumpRequested)
        {
            return;
        }
        common->Printf(
            "PathTraceCleanRestirGi DUMP enable=%d view=%d temporal=%d spatial=%d biasCorrection=%d jacobian=%d "
            "maxHistory=%d maxAge=%d firefly=%.3f neeSeed=%d specProd=%d rrHitDistance=%d resolve=%d size=%dx%d frame=%u "
            "reservoirBuffer=%s pages[init=%u tIn=%u tOut=%u sOut=%u] arrayPitch=%u producerTex=%d pipeline=%d "
            "diBlob=%d lights=%d earlyReturn=%s\n",
            r_pathTracingCleanRestirGiEnable.GetInteger(),
            r_pathTracingCleanRestirGiView.GetInteger(),
            r_pathTracingCleanRestirGiTemporal.GetInteger(),
            r_pathTracingCleanRestirGiSpatial.GetInteger(),
            r_pathTracingCleanRestirGiTemporalBiasCorrection.GetInteger(),
            r_pathTracingCleanRestirGiJacobian.GetInteger(),
            r_pathTracingCleanRestirGiMaxHistoryLength.GetInteger(),
            r_pathTracingCleanRestirGiMaxReservoirAge.GetInteger(),
            r_pathTracingCleanRestirGiFireflyThreshold.GetFloat(),
            r_pathTracingCleanRestirGiNeeCacheSeed.GetInteger(),
            r_pathTracingCleanRestirGiSpecularProducer.GetInteger(),
            r_pathTracingCleanRestirGiRrHitDistance.GetInteger(),
            r_pathTracingCleanRestirGiResolve.GetInteger(),
            inputs.width,
            inputs.height,
            state.frameIndex,
            state.reservoirBuffer ? "u80" : "none",
            CLEAN_RESTIR_GI_PAGE_INIT,
            CLEAN_RESTIR_GI_PAGE_TEMPORAL_INPUT,
            CLEAN_RESTIR_GI_PAGE_TEMPORAL_OUTPUT,
            CLEAN_RESTIR_GI_PAGE_SPATIAL_OUTPUT,
            state.reservoirArrayPitch,
            (state.producerRadianceTexture && state.producerHitPositionTexture && state.producerHitNormalTexture) ? 1 : 0,
            state.shaderTable ? 1 : 0,
            inputs.diConstantsBlob ? 1 : 0,
            inputs.doomAnalyticLightBuffer ? 1 : 0,
            earlyReturn);
        r_pathTracingCleanRestirGiDump.SetInteger(0);
    };
    auto clearFailureOutput = [&]()
    {
        if (!inputs.commandList || !inputs.outputTexture)
        {
            return;
        }
        inputs.commandList->setTextureState(inputs.outputTexture, nvrhi::AllSubresources, nvrhi::ResourceStates::UnorderedAccess);
        inputs.commandList->commitBarriers();
        inputs.commandList->clearTextureFloat(inputs.outputTexture, nvrhi::AllSubresources, nvrhi::Color(0.75f, 0.0f, 0.75f, 1.0f));
    };
    auto missingResource = [&]() -> const char*
    {
        if (!inputs.device) return "device";
        if (!inputs.commandList) return "command-list";
        if (!inputs.outputTexture) return "output-texture";
        if (!inputs.textureBindlessLayout) return "texture-bindless-layout";
        if (!inputs.textureDescriptorTable) return "texture-descriptor-table";
        if (inputs.width <= 0 || inputs.height <= 0) return "extent";
        if (!inputs.diConstantsBlob) return "di-constants";
        if (inputs.diConstantsSize == 0 || inputs.diConstantsSize > CLEAN_RESTIR_GI_DI_BLOB_SIZE) return "di-constants-size";
        if (!inputs.tlas) return "tlas";
        if (!inputs.staticVertexBuffer) return "static-vertex";
        if (!inputs.staticIndexBuffer) return "static-index";
        if (!inputs.dynamicVertexBuffer) return "dynamic-vertex";
        if (!inputs.dynamicIndexBuffer) return "dynamic-index";
        if (!inputs.staticTriangleClassBuffer) return "static-triangle-class";
        if (!inputs.dynamicTriangleClassBuffer) return "dynamic-triangle-class";
        if (!inputs.staticTriangleMaterialBuffer) return "static-triangle-material";
        if (!inputs.dynamicTriangleMaterialBuffer) return "dynamic-triangle-material";
        if (!inputs.staticTriangleMaterialIndexBuffer) return "static-triangle-material-index";
        if (!inputs.dynamicTriangleMaterialIndexBuffer) return "dynamic-triangle-material-index";
        if (!inputs.materialTableBuffer) return "material-table";
        if (!inputs.fallbackTexture) return "fallback-texture";
        if (!inputs.emissiveTriangleBuffer) return "emissive-triangles";
        if (!inputs.emissiveDistributionBuffer) return "emissive-distribution";
        if (!inputs.rigidRouteVertexBuffer) return "rigid-route-vertex";
        if (!inputs.rigidRouteIndexBuffer) return "rigid-route-index";
        if (!inputs.rigidRouteTriangleMaterialBuffer) return "rigid-route-triangle-material";
        if (!inputs.rigidRouteTriangleMaterialIndexBuffer) return "rigid-route-triangle-material-index";
        if (!inputs.rigidRouteInstanceBuffer) return "rigid-route-instance";
        if (!inputs.doomAnalyticLightBuffer) return "doom-analytic-lights";
        if (!inputs.rluCurrentLightBuffer) return "rlu-current-lights";
        if (!inputs.neeCacheProviderResultBuffer && r_pathTracingCleanRestirGiNeeCacheSeed.GetInteger() != 0) return "nee-cache-provider-results";
        if (!inputs.primarySurfaceCurrentBuffer) return "primary-surface-current";
        if (!inputs.primarySurfacePreviousBuffer) return "primary-surface-previous";
        if (!inputs.motionVectorTexture) return "motion-vectors";
        if (!inputs.motionVectorMaskTexture) return "motion-vector-mask";
        if (!inputs.rrInputColorTexture) return "rr-input-color";
        if (!inputs.rrGuideHitDistanceTexture) return "rr-guide-hit-distance";
        if (!inputs.materialSampler) return "material-sampler";
        return nullptr;
    };

    if (r_pathTracingCleanRestirGiEnable.GetInteger() == 0)
    {
        printDump("disabled");
        return false;
    }

    const int view = idMath::ClampInt(0, 18, r_pathTracingCleanRestirGiView.GetInteger());
    const bool rrHitDistanceRequested =
        r_pathTracingCleanRestirGiRrHitDistance.GetInteger() != 0 &&
        r_pathTracingCleanRestirGiSpecularProducer.GetInteger() != 0;
    if (view == 0 && r_pathTracingCleanRestirGiResolve.GetInteger() == 0 && !rrHitDistanceRequested)
    {
        // Nothing consumes the lane yet without a debug view or resolve.
        printDump("no-view");
        return false;
    }

    if (!inputs.device || !inputs.commandList || !inputs.outputTexture || !inputs.textureBindlessLayout || !inputs.textureDescriptorTable ||
        inputs.width <= 0 || inputs.height <= 0 ||
        !inputs.diConstantsBlob || inputs.diConstantsSize == 0 || inputs.diConstantsSize > CLEAN_RESTIR_GI_DI_BLOB_SIZE ||
        !inputs.tlas || !inputs.staticVertexBuffer || !inputs.staticIndexBuffer ||
        !inputs.dynamicVertexBuffer || !inputs.dynamicIndexBuffer ||
        !inputs.staticTriangleClassBuffer || !inputs.dynamicTriangleClassBuffer ||
        !inputs.staticTriangleMaterialBuffer || !inputs.dynamicTriangleMaterialBuffer ||
        !inputs.staticTriangleMaterialIndexBuffer || !inputs.dynamicTriangleMaterialIndexBuffer ||
        !inputs.materialTableBuffer || !inputs.fallbackTexture || !inputs.emissiveTriangleBuffer ||
        !inputs.rigidRouteVertexBuffer || !inputs.rigidRouteIndexBuffer ||
        !inputs.rigidRouteTriangleMaterialBuffer ||
        !inputs.rigidRouteTriangleMaterialIndexBuffer || !inputs.rigidRouteInstanceBuffer ||
        !inputs.doomAnalyticLightBuffer ||
        !inputs.emissiveDistributionBuffer || !inputs.rluCurrentLightBuffer ||
        (!inputs.neeCacheProviderResultBuffer && r_pathTracingCleanRestirGiNeeCacheSeed.GetInteger() != 0) ||
        !inputs.primarySurfaceCurrentBuffer || !inputs.primarySurfacePreviousBuffer ||
        !inputs.motionVectorTexture || !inputs.motionVectorMaskTexture ||
        !inputs.rrInputColorTexture || !inputs.rrGuideHitDistanceTexture ||
        !inputs.materialSampler)
    {
        clearFailureOutput();
        printDump(missingResource());
        return false;
    }

    if (!CleanRestirGiEnsurePipeline(state, inputs))
    {
        clearFailureOutput();
        printDump("pipeline");
        return false;
    }
    if (!CleanRestirGiEnsureResources(state, inputs))
    {
        clearFailureOutput();
        printDump("resources");
        return false;
    }

    nvrhi::BindingSetDesc bindingSetDesc;
    bindingSetDesc.addItem(nvrhi::BindingSetItem::RayTracingAccelStruct(0, inputs.tlas));
    bindingSetDesc.addItem(nvrhi::BindingSetItem::Texture_UAV(1, inputs.outputTexture));
    bindingSetDesc.addItem(nvrhi::BindingSetItem::ConstantBuffer(2, state.constantsBuffer));
    bindingSetDesc.addItem(nvrhi::BindingSetItem::StructuredBuffer_SRV(3, inputs.staticVertexBuffer));
    bindingSetDesc.addItem(nvrhi::BindingSetItem::StructuredBuffer_SRV(4, inputs.staticIndexBuffer));
    bindingSetDesc.addItem(nvrhi::BindingSetItem::StructuredBuffer_SRV(5, inputs.staticTriangleClassBuffer));
    bindingSetDesc.addItem(nvrhi::BindingSetItem::StructuredBuffer_SRV(6, inputs.dynamicVertexBuffer));
    bindingSetDesc.addItem(nvrhi::BindingSetItem::StructuredBuffer_SRV(7, inputs.dynamicIndexBuffer));
    bindingSetDesc.addItem(nvrhi::BindingSetItem::StructuredBuffer_SRV(8, inputs.dynamicTriangleClassBuffer));
    bindingSetDesc.addItem(nvrhi::BindingSetItem::StructuredBuffer_SRV(9, inputs.staticTriangleMaterialBuffer));
    bindingSetDesc.addItem(nvrhi::BindingSetItem::StructuredBuffer_SRV(10, inputs.dynamicTriangleMaterialBuffer));
    bindingSetDesc.addItem(nvrhi::BindingSetItem::StructuredBuffer_SRV(11, inputs.staticTriangleMaterialIndexBuffer));
    bindingSetDesc.addItem(nvrhi::BindingSetItem::StructuredBuffer_SRV(12, inputs.dynamicTriangleMaterialIndexBuffer));
    bindingSetDesc.addItem(nvrhi::BindingSetItem::StructuredBuffer_SRV(13, inputs.materialTableBuffer));
    bindingSetDesc.addItem(nvrhi::BindingSetItem::Texture_SRV(14, inputs.fallbackTexture));
    bindingSetDesc.addItem(nvrhi::BindingSetItem::StructuredBuffer_SRV(16, inputs.emissiveTriangleBuffer));
    bindingSetDesc.addItem(nvrhi::BindingSetItem::StructuredBuffer_SRV(22, inputs.rigidRouteVertexBuffer));
    bindingSetDesc.addItem(nvrhi::BindingSetItem::StructuredBuffer_SRV(23, inputs.rigidRouteIndexBuffer));
    bindingSetDesc.addItem(nvrhi::BindingSetItem::StructuredBuffer_SRV(24, inputs.rigidRouteTriangleMaterialBuffer));
    bindingSetDesc.addItem(nvrhi::BindingSetItem::StructuredBuffer_SRV(25, inputs.rigidRouteTriangleMaterialIndexBuffer));
    bindingSetDesc.addItem(nvrhi::BindingSetItem::StructuredBuffer_SRV(26, inputs.rigidRouteInstanceBuffer));
    bindingSetDesc.addItem(nvrhi::BindingSetItem::StructuredBuffer_SRV(27, inputs.doomAnalyticLightBuffer));
    bindingSetDesc.addItem(nvrhi::BindingSetItem::StructuredBuffer_SRV(46, inputs.emissiveDistributionBuffer));
    bindingSetDesc.addItem(nvrhi::BindingSetItem::StructuredBuffer_SRV(66, inputs.rluCurrentLightBuffer));
    nvrhi::IBuffer* neeCacheProviderResultBuffer = inputs.neeCacheProviderResultBuffer ? inputs.neeCacheProviderResultBuffer : state.placeholderSrvBuffer.Get();
    bindingSetDesc.addItem(nvrhi::BindingSetItem::StructuredBuffer_SRV(74, neeCacheProviderResultBuffer));
    bindingSetDesc.addItem(nvrhi::BindingSetItem::StructuredBuffer_UAV(30, inputs.primarySurfaceCurrentBuffer));
    bindingSetDesc.addItem(nvrhi::BindingSetItem::StructuredBuffer_UAV(31, inputs.primarySurfacePreviousBuffer));
    bindingSetDesc.addItem(nvrhi::BindingSetItem::Texture_UAV(39, inputs.motionVectorTexture));
    bindingSetDesc.addItem(nvrhi::BindingSetItem::Texture_UAV(40, inputs.motionVectorMaskTexture));
    bindingSetDesc.addItem(nvrhi::BindingSetItem::StructuredBuffer_UAV(80, state.reservoirBuffer));
    bindingSetDesc.addItem(nvrhi::BindingSetItem::Texture_UAV(81, state.producerRadianceTexture));
    bindingSetDesc.addItem(nvrhi::BindingSetItem::Texture_UAV(82, state.producerHitPositionTexture));
    bindingSetDesc.addItem(nvrhi::BindingSetItem::Texture_UAV(83, state.producerHitNormalTexture));
    bindingSetDesc.addItem(nvrhi::BindingSetItem::Texture_UAV(84, state.indirectDiffuseTexture));
    bindingSetDesc.addItem(nvrhi::BindingSetItem::Texture_UAV(85, state.indirectDiffuseLobeTexture));
    bindingSetDesc.addItem(nvrhi::BindingSetItem::Texture_UAV(86, state.indirectSpecularLobeTexture));
    bindingSetDesc.addItem(nvrhi::BindingSetItem::Texture_UAV(51, inputs.rrGuideHitDistanceTexture));
    bindingSetDesc.addItem(nvrhi::BindingSetItem::Texture_UAV(54, inputs.rrInputColorTexture));
    bindingSetDesc.addItem(nvrhi::BindingSetItem::Sampler(0, inputs.materialSampler));
    nvrhi::BindingSetHandle bindingSet = inputs.device->createBindingSet(bindingSetDesc, state.bindingLayout);
    if (!bindingSet)
    {
        clearFailureOutput();
        printDump("binding-set");
        return false;
    }

    nvrhi::ICommandList* commandList = inputs.commandList;
    if (state.reservoirClearPending)
    {
        commandList->setBufferState(state.reservoirBuffer, nvrhi::ResourceStates::UnorderedAccess);
        commandList->commitBarriers();
        commandList->clearBufferUInt(state.reservoirBuffer, 0);
        state.reservoirClearPending = false;
    }

    uint8_t constants[CLEAN_RESTIR_GI_CONSTANTS_SIZE] = {};
    std::memcpy(constants, inputs.diConstantsBlob, inputs.diConstantsSize);
    PathTraceCleanRestirGiConstantsTail tail = {};
    tail.view = static_cast<uint32_t>(view);
    tail.temporalEnabled = r_pathTracingCleanRestirGiTemporal.GetInteger() != 0 ? 1u : 0u;
    tail.spatialEnabled = r_pathTracingCleanRestirGiSpatial.GetInteger() != 0 ? 1u : 0u;
    tail.biasCorrection = static_cast<uint32_t>(idMath::ClampInt(0, 1, r_pathTracingCleanRestirGiTemporalBiasCorrection.GetInteger()));
    tail.jacobianEnabled = r_pathTracingCleanRestirGiJacobian.GetInteger() != 0 ? 1u : 0u;
    tail.maxHistoryLength = static_cast<uint32_t>(idMath::ClampInt(0, 255, r_pathTracingCleanRestirGiMaxHistoryLength.GetInteger()));
    tail.maxReservoirAge = static_cast<uint32_t>(idMath::ClampInt(1, 255, r_pathTracingCleanRestirGiMaxReservoirAge.GetInteger()));
    tail.fireflyThreshold = Max(0.0f, r_pathTracingCleanRestirGiFireflyThreshold.GetFloat());
    tail.neeCacheSeedEnabled = r_pathTracingCleanRestirGiNeeCacheSeed.GetInteger() != 0 ? 1u : 0u;
    tail.frameIndex = state.frameIndex;
    tail.resolveEnabled = r_pathTracingCleanRestirGiResolve.GetInteger() != 0 ? 1u : 0u;
    tail.specularProducerEnabled = r_pathTracingCleanRestirGiSpecularProducer.GetInteger() != 0 ? 1u : 0u;
    tail.rrHitDistanceEnabled = rrHitDistanceRequested ? 1u : 0u;
    tail.reservoirParams.reservoirBlockRowPitch = state.reservoirBlockRowPitch;
    tail.reservoirParams.reservoirArrayPitch = state.reservoirArrayPitch;
    // Page rotation (RGI-04): this frame's temporal output is next frame's
    // temporal input, so the two pages alternate by frame parity. INIT and
    // SPATIAL_OUTPUT are transient within the frame.
    const bool oddFrame = (state.frameIndex & 1u) != 0u;
    tail.pageInfo[0] = CLEAN_RESTIR_GI_PAGE_INIT;
    tail.pageInfo[1] = oddFrame ? CLEAN_RESTIR_GI_PAGE_TEMPORAL_OUTPUT : CLEAN_RESTIR_GI_PAGE_TEMPORAL_INPUT;
    tail.pageInfo[2] = oddFrame ? CLEAN_RESTIR_GI_PAGE_TEMPORAL_INPUT : CLEAN_RESTIR_GI_PAGE_TEMPORAL_OUTPUT;
    tail.pageInfo[3] = CLEAN_RESTIR_GI_PAGE_SPATIAL_OUTPUT;
    std::memcpy(constants + CLEAN_RESTIR_GI_DI_BLOB_SIZE, &tail, sizeof(tail));
    commandList->writeBuffer(state.constantsBuffer, constants, sizeof(constants));

    commandList->setTextureState(inputs.outputTexture, nvrhi::AllSubresources, nvrhi::ResourceStates::UnorderedAccess);
    commandList->setTextureState(state.producerRadianceTexture, nvrhi::AllSubresources, nvrhi::ResourceStates::UnorderedAccess);
    commandList->setTextureState(state.producerHitPositionTexture, nvrhi::AllSubresources, nvrhi::ResourceStates::UnorderedAccess);
    commandList->setTextureState(state.producerHitNormalTexture, nvrhi::AllSubresources, nvrhi::ResourceStates::UnorderedAccess);
    commandList->setBufferState(state.reservoirBuffer, nvrhi::ResourceStates::UnorderedAccess);
    commandList->setBufferState(inputs.primarySurfaceCurrentBuffer, nvrhi::ResourceStates::UnorderedAccess);
    commandList->setBufferState(inputs.primarySurfacePreviousBuffer, nvrhi::ResourceStates::UnorderedAccess);
    commandList->setTextureState(inputs.motionVectorTexture, nvrhi::AllSubresources, nvrhi::ResourceStates::UnorderedAccess);
    commandList->setTextureState(inputs.motionVectorMaskTexture, nvrhi::AllSubresources, nvrhi::ResourceStates::UnorderedAccess);
    commandList->setTextureState(state.indirectDiffuseTexture, nvrhi::AllSubresources, nvrhi::ResourceStates::UnorderedAccess);
    commandList->setTextureState(state.indirectDiffuseLobeTexture, nvrhi::AllSubresources, nvrhi::ResourceStates::UnorderedAccess);
    commandList->setTextureState(state.indirectSpecularLobeTexture, nvrhi::AllSubresources, nvrhi::ResourceStates::UnorderedAccess);
    if (tail.rrHitDistanceEnabled != 0u)
    {
        commandList->setTextureState(inputs.rrGuideHitDistanceTexture, nvrhi::AllSubresources, nvrhi::ResourceStates::UnorderedAccess);
    }
    commandList->setTextureState(inputs.rrInputColorTexture, nvrhi::AllSubresources, nvrhi::ResourceStates::UnorderedAccess);
    commandList->setBufferState(inputs.staticVertexBuffer, nvrhi::ResourceStates::ShaderResource);
    commandList->setBufferState(inputs.staticIndexBuffer, nvrhi::ResourceStates::ShaderResource);
    commandList->setBufferState(inputs.dynamicVertexBuffer, nvrhi::ResourceStates::ShaderResource);
    commandList->setBufferState(inputs.dynamicIndexBuffer, nvrhi::ResourceStates::ShaderResource);
    commandList->setBufferState(inputs.staticTriangleClassBuffer, nvrhi::ResourceStates::ShaderResource);
    commandList->setBufferState(inputs.dynamicTriangleClassBuffer, nvrhi::ResourceStates::ShaderResource);
    commandList->setBufferState(inputs.staticTriangleMaterialBuffer, nvrhi::ResourceStates::ShaderResource);
    commandList->setBufferState(inputs.dynamicTriangleMaterialBuffer, nvrhi::ResourceStates::ShaderResource);
    commandList->setBufferState(inputs.staticTriangleMaterialIndexBuffer, nvrhi::ResourceStates::ShaderResource);
    commandList->setBufferState(inputs.dynamicTriangleMaterialIndexBuffer, nvrhi::ResourceStates::ShaderResource);
    commandList->setBufferState(inputs.materialTableBuffer, nvrhi::ResourceStates::ShaderResource);
    commandList->setTextureState(inputs.fallbackTexture, nvrhi::AllSubresources, nvrhi::ResourceStates::ShaderResource);
    commandList->setBufferState(inputs.emissiveTriangleBuffer, nvrhi::ResourceStates::ShaderResource);
    commandList->setBufferState(inputs.emissiveDistributionBuffer, nvrhi::ResourceStates::ShaderResource);
    commandList->setBufferState(inputs.rigidRouteVertexBuffer, nvrhi::ResourceStates::ShaderResource);
    commandList->setBufferState(inputs.rigidRouteIndexBuffer, nvrhi::ResourceStates::ShaderResource);
    commandList->setBufferState(inputs.rigidRouteTriangleMaterialBuffer, nvrhi::ResourceStates::ShaderResource);
    commandList->setBufferState(inputs.rigidRouteTriangleMaterialIndexBuffer, nvrhi::ResourceStates::ShaderResource);
    commandList->setBufferState(inputs.rigidRouteInstanceBuffer, nvrhi::ResourceStates::ShaderResource);
    commandList->setBufferState(inputs.doomAnalyticLightBuffer, nvrhi::ResourceStates::ShaderResource);
    commandList->setBufferState(inputs.rluCurrentLightBuffer, nvrhi::ResourceStates::ShaderResource);
    commandList->setBufferState(neeCacheProviderResultBuffer, nvrhi::ResourceStates::ShaderResource);
    commandList->commitBarriers();

    nvrhi::rt::State giState;
    giState.shaderTable = state.shaderTable;
    giState.bindings = { bindingSet, inputs.textureDescriptorTable };
    commandList->setRayTracingState(giState);

    nvrhi::rt::DispatchRaysArguments giArgs;
    giArgs.width = inputs.width;
    giArgs.height = inputs.height;
    giArgs.depth = 1;
    commandList->dispatchRays(giArgs);

    nvrhi::utils::TextureUavBarrier(commandList, inputs.outputTexture);
    nvrhi::utils::TextureUavBarrier(commandList, state.producerRadianceTexture);
    nvrhi::utils::TextureUavBarrier(commandList, state.producerHitPositionTexture);
    nvrhi::utils::TextureUavBarrier(commandList, state.producerHitNormalTexture);
    nvrhi::utils::BufferUavBarrier(commandList, state.reservoirBuffer);

    nvrhi::utils::TextureUavBarrier(commandList, state.indirectDiffuseTexture);
    nvrhi::utils::TextureUavBarrier(commandList, state.indirectDiffuseLobeTexture);
    nvrhi::utils::TextureUavBarrier(commandList, state.indirectSpecularLobeTexture);
    if (tail.rrHitDistanceEnabled != 0u)
    {
        nvrhi::utils::TextureUavBarrier(commandList, inputs.rrGuideHitDistanceTexture);
    }

    // RGI-06: spatial reuse runs as a second dispatch so every pixel's
    // TEMPORAL_OUTPUT page write has completed before neighbors read it.
    // With spatial disabled, phase 0 already passed the temporal output
    // through to the SPATIAL_OUTPUT page.
    if (tail.spatialEnabled != 0u)
    {
        tail.phase = 1u;
        std::memcpy(constants + CLEAN_RESTIR_GI_DI_BLOB_SIZE, &tail, sizeof(tail));
        commandList->writeBuffer(state.constantsBuffer, constants, sizeof(constants));
        commandList->setRayTracingState(giState);
        commandList->dispatchRays(giArgs);
        nvrhi::utils::TextureUavBarrier(commandList, inputs.outputTexture);
        nvrhi::utils::BufferUavBarrier(commandList, state.reservoirBuffer);
        nvrhi::utils::TextureUavBarrier(commandList, state.indirectDiffuseTexture);
        nvrhi::utils::TextureUavBarrier(commandList, state.indirectDiffuseLobeTexture);
        nvrhi::utils::TextureUavBarrier(commandList, state.indirectSpecularLobeTexture);
        if (tail.rrHitDistanceEnabled != 0u)
        {
            nvrhi::utils::TextureUavBarrier(commandList, inputs.rrGuideHitDistanceTexture);
        }
    }

    // RGI-08: boiling filter + resolve consumer over the GI output. The
    // compute pass clamps shaded outliers to the group average (Remix
    // final-shading diffuse behavior) and performs the resolve add so the
    // combined outputs receive the filtered contribution. Debug views own
    // SmokeOutput, so the resolve add only runs with the views off.
    const float boilingFilterThreshold = Max(0.0f, r_pathTracingCleanRestirGiBoilingFilter.GetFloat());
    const bool resolveAddRequested = tail.resolveEnabled != 0u && view == 0;
    const bool filterWorkRequested = view != 0 || resolveAddRequested;
    if (filterWorkRequested &&
        (boilingFilterThreshold > 0.0f || resolveAddRequested) &&
        CleanRestirGiEnsureBoilingFilterPipeline(state, inputs))
    {
        nvrhi::BindingSetDesc filterSetDesc;
        filterSetDesc.addItem(nvrhi::BindingSetItem::ConstantBuffer(0, state.boilingFilterConstantsBuffer));
        filterSetDesc.addItem(nvrhi::BindingSetItem::Texture_UAV(1, state.indirectDiffuseTexture));
        filterSetDesc.addItem(nvrhi::BindingSetItem::Texture_UAV(2, inputs.outputTexture));
        filterSetDesc.addItem(nvrhi::BindingSetItem::Texture_UAV(3, inputs.rrInputColorTexture));
        filterSetDesc.addItem(nvrhi::BindingSetItem::StructuredBuffer_SRV(4, inputs.primarySurfaceCurrentBuffer));
        nvrhi::BindingSetHandle filterSet = inputs.device->createBindingSet(filterSetDesc, state.boilingFilterBindingLayout);
        if (filterSet)
        {
            PathTraceCleanRestirGiBoilingFilterConstants filterConstants;
            filterConstants.width = static_cast<uint32_t>(inputs.width);
            filterConstants.height = static_cast<uint32_t>(inputs.height);
            filterConstants.threshold = boilingFilterThreshold;
            filterConstants.resolveEnabled = resolveAddRequested ? 1u : 0u;
            commandList->writeBuffer(state.boilingFilterConstantsBuffer, &filterConstants, sizeof(filterConstants));

            commandList->setBufferState(inputs.primarySurfaceCurrentBuffer, nvrhi::ResourceStates::ShaderResource);
            commandList->commitBarriers();

            nvrhi::ComputeState filterState;
            filterState.pipeline = state.boilingFilterPipeline;
            filterState.bindings = { filterSet };
            commandList->setComputeState(filterState);
            commandList->dispatch(
                static_cast<uint32_t>((inputs.width + 7) / 8),
                static_cast<uint32_t>((inputs.height + 7) / 8),
                1);
            nvrhi::utils::TextureUavBarrier(commandList, state.indirectDiffuseTexture);
            nvrhi::utils::TextureUavBarrier(commandList, inputs.outputTexture);
            nvrhi::utils::TextureUavBarrier(commandList, inputs.rrInputColorTexture);
        }
    }

    if (!state.dispatchLogged)
    {
        common->Printf("PathTraceCleanRestirGi: dispatched GI lane raygen (%dx%d, view=%d)\n", inputs.width, inputs.height, view);
        state.dispatchLogged = true;
    }
    printDump("none");
    state.frameIndex++;
    return view != 0;
}
