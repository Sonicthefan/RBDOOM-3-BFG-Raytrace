#include "precompiled.h"
#pragma hdrstop

#include "PathTraceDebugDumps.h"
#include "PathTraceDoomMaterialClassifier.h"
#include "PathTraceAcceleration.h"
#include "PathTraceCVars.h"
#include "PathTraceDynamicMaterialState.h"
#include "PathTraceEmissiveCandidates.h"
#include "PathTraceGuiSurfaces.h"
#include "PathTraceMaterialTextureDiscovery.h"
#include "PathTracePrimaryPass.h"
#include "PathTraceSceneCapture.h"
#include "PathTraceSkinning.h"
#include "PathTraceSmokeDispatch.h"
#include "PathTraceSmokeResources.h"
#include "PathTraceSurfaceClassification.h"
#include "PathTraceSurfaceDebugDumps.h"
#include "PathTraceTextureRegistry.h"
#include "../RenderCommon.h"
#include "../RenderBackend.h"
#include "../GLMatrix.h"
#include "../Image.h"
#include "../Model_local.h"
#include "../Passes/CommonPasses.h"
#include "../../framework/Common_local.h"
#include "../../sys/DeviceManager.h"

#include <vector>

extern DeviceManager* deviceManager;

namespace {

const int RT_SMOKE_MIN_OUTPUT_WIDTH = 16;
const int RT_SMOKE_MIN_OUTPUT_HEIGHT = 16;
const int RT_SMOKE_MAX_OUTPUT_WIDTH = 3840;
const int RT_SMOKE_MAX_OUTPUT_HEIGHT = 2160;
const int RT_SMOKE_SCENE_LOG_INTERVAL_FRAMES = 120;
const int RT_SMOKE_TEXTURE_PROBE_CANDIDATE_SAMPLES = 24;
const int RT_SMOKE_TEXTURE_PROBE_DUMP_CANDIDATES = 64;
const int RT_SMOKE_MAX_EMISSIVE_TRIANGLE_RECORDS = 65536;

int g_smokeLastSceneTimingLogMs = -1000000;

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
    , m_smokeSceneLogCooldownFrames(0)
    , m_smokeOutputWidth(0)
    , m_smokeOutputHeight(0)
    , m_smokeStaticBlasCacheValid(false)
    , m_smokeStaticBlasSignature(0)
    , m_smokeStaticBlasCacheHitCount(0)
    , m_smokeStaticBlasCacheMissCount(0)
    , m_smokeTextureProbeMaterialId(0)
    , m_smokeTextureProbeRequestedIndex(-1)
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
    const int outputWidth = idMath::ClampInt(RT_SMOKE_MIN_OUTPUT_WIDTH, RT_SMOKE_MAX_OUTPUT_WIDTH, r_pathTracingDebugWidth.GetInteger());
    const int outputHeight = idMath::ClampInt(RT_SMOKE_MIN_OUTPUT_HEIGHT, RT_SMOKE_MAX_OUTPUT_HEIGHT, r_pathTracingDebugHeight.GetInteger());
    if (!ResizeRayTracingSmokeOutput(outputWidth, outputHeight))
    {
        return;
    }

    BuildRayTracingSmokeTestScene(viewDef);
    ExecuteRayTracingSmokeTest(viewDef);
    ReadBackRayTracingSmokeTest();
}

void PathTracePrimaryPass::BuildRayTracingSmokeTestScene(const viewDef_t* viewDef)
{
    const int sceneStartMs = Sys_Milliseconds();
    m_smokeSceneBuilt = false;
    const int requestedDebugMode = idMath::ClampInt(0, 19, r_pathTracingDebugMode.GetInteger());
    const bool enableTextureProbe = (requestedDebugMode >= 8 && requestedDebugMode <= 19);

    if (!m_smokeTlas || !m_smokeBindingLayout || !m_smokeTextureBindlessLayout || !m_smokeTextureDescriptorTable || !m_smokeOutputTexture || !m_smokeAccumulationTexture || !m_smokeConstantsBuffer)
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

    std::vector<PathTraceSmokeVertex> dynamicVertexData;
    std::vector<uint32_t> dynamicIndexData;
    std::vector<uint32_t> dynamicTriangleClassData;
    std::vector<uint32_t> dynamicTriangleMaterialData;
    int sourceSurfaces = 0;
    int sourceVerts = 0;
    int sourceIndexes = 0;
    int anchorTriangle = -1;
    RtSmokeSurfaceClassStats classStats;
    RtSmokeSurfaceSkipStats skipStats;
    RtSmokeDynamicGeometryStats dynamicStats;
    RtSmokeAttributeStats attributeStats;
    RtSmokeMaterialStats materialStats;
    RtSmokeBucketRanges bucketRanges;
    const bool dumpClassReasons = r_pathTracingClassDump.GetInteger() != 0;
    RtSmokeSurfaceClassReasonSamples reasonSamples;
    bool staticCacheChanged = false;
    RtSmokeSceneCaptureTiming captureTiming;
    const int captureStartMs = Sys_Milliseconds();
    const bool usingDoomSurfaces = CaptureDoomSurfacesForSmokeTest(viewDef, dynamicVertexData, dynamicIndexData, dynamicTriangleClassData, dynamicTriangleMaterialData, m_smokeStaticSurfaceKeys, m_smokeStaticVertexCache, m_smokeStaticIndexCache, m_smokeStaticTriangleClassCache, m_smokeStaticTriangleMaterialCache, staticCacheChanged, m_smokeSceneOrigin, sourceSurfaces, sourceVerts, sourceIndexes, anchorTriangle, classStats, skipStats, dynamicStats, attributeStats, materialStats, bucketRanges, captureTiming, dumpClassReasons ? &reasonSamples : nullptr);
    const int captureMs = Sys_Milliseconds() - captureStartMs;
    if (!usingDoomSurfaces)
    {
        if (!m_smokeWaitingForDoomSurfaceLogged)
        {
            common->Printf("PathTracePrimaryPass: waiting for center camera ray Doom surface hit to build RT smoke BLAS\n");
            m_smokeWaitingForDoomSurfaceLogged = true;
        }
        return;
    }

    const RtSmokeMaterialMetadataRegistrationTiming metadataTiming = RegisterSmokeMaterialTextureInfoForFrame(viewDef, enableTextureProbe);
    const int metadataMs = metadataTiming.metadataMs;
    const int metadataValidationMs = metadataTiming.validationMs;
    const int metadataRegistrationMs = metadataTiming.registrationMs;

    const int materialStartMs = Sys_Milliseconds();
    RtSmokeMaterialTableBuild materialTable;
    uint64 materialTableSignature = 0;
    bool materialTableCacheHit = false;
    BuildSmokeMaterialTableCached(materialTable, m_smokeStaticTriangleMaterialCache, dynamicTriangleMaterialData, m_smokeTextureProbeMaterialId, m_smokeTextureProbeRequestedIndex, enableTextureProbe, materialTableSignature, materialTableCacheHit);
    const RtSmokeMaterialTableCacheStats materialTableCacheStats = GetSmokeMaterialTableCacheStats();
    if (!ValidateSmokeMaterialIndexes(materialTable))
    {
        common->Printf("PathTracePrimaryPass: invalid RT smoke material table, skipping scene build\n");
        return;
    }
    RtSmokeTextureCoverageStats textureCoverageStats;
    const bool needTextureCoverageStats = enableTextureProbe && r_pathTracingSmokeLog.GetInteger() != 0;
    if (needTextureCoverageStats)
    {
        textureCoverageStats = BuildSmokeTextureCoverageStats(
            materialTable,
            m_smokeStaticTriangleClassCache,
            materialTable.staticMaterialIndexes,
            dynamicTriangleClassData,
            materialTable.dynamicMaterialIndexes);
    }
    const int materialMs = Sys_Milliseconds() - materialStartMs;
    static uint32_t lastLoggedTextureProbeMaterialId = 0;
    if (enableTextureProbe && r_pathTracingSmokeLog.GetInteger() != 0 && materialTable.textureProbeBoundMaterialId != lastLoggedTextureProbeMaterialId)
    {
        LogSmokeTextureProbeSwitch(materialTable);
        lastLoggedTextureProbeMaterialId = materialTable.textureProbeBoundMaterialId;
    }
    if (enableTextureProbe && r_pathTracingTextureProbeDump.GetInteger() != 0)
    {
        LogSmokeTextureProbeDump(materialTable);
        r_pathTracingTextureProbeDump.SetInteger(0);
    }
    if (enableTextureProbe && r_pathTracingAlphaDump.GetInteger() != 0)
    {
        LogSmokeAlphaMaterialDump(materialTable);
        r_pathTracingAlphaDump.SetInteger(0);
    }
    if (enableTextureProbe && r_pathTracingTextureFallbackDump.GetInteger() != 0)
    {
        LogSmokeTextureFallbackDump(materialTable);
        r_pathTracingTextureFallbackDump.SetInteger(0);
    }
    if (r_pathTracingTranslucentDump.GetInteger() != 0)
    {
        LogSmokeTranslucentSubtypeDump(materialStats);
        r_pathTracingTranslucentDump.SetInteger(0);
    }
    if (r_pathTracingCrosshairMaterialDump.GetInteger() != 0)
    {
        LogSmokeCrosshairMaterialDump(viewDef, materialTable);
        r_pathTracingCrosshairMaterialDump.SetInteger(0);
    }
    if (r_pathTracingGuiDump.GetInteger() != 0)
    {
        LogSmokeGuiSurfaceDump(viewDef, materialTable);
        r_pathTracingGuiDump.SetInteger(0);
    }
    if (enableTextureProbe && r_pathTracingSmokeLog.GetInteger() != 0)
    {
        LogSmokeTextureActiveWindow(materialTable);
    }

    RtSmokeEmissiveInventoryStats emissiveInventoryStats;
    const int emissiveStartMs = Sys_Milliseconds();
    std::vector<PathTraceSmokeEmissiveTriangle> emissiveTriangles = BuildSmokeEmissiveTriangleInventory(
        materialTable.materials,
        m_smokeStaticVertexCache,
        m_smokeStaticIndexCache,
        m_smokeStaticTriangleClassCache,
        materialTable.staticMaterialIndexes,
        dynamicVertexData,
        dynamicIndexData,
        dynamicTriangleClassData,
        materialTable.dynamicMaterialIndexes,
        RT_SMOKE_MATERIAL_EMISSIVE,
        RT_SMOKE_TRIANGLE_CLASS_MASK,
        static_cast<uint32_t>(RtSmokeSurfaceClass::SkinnedDeformed),
        idMath::ClampInt(1, RT_SMOKE_MAX_EMISSIVE_TRIANGLE_RECORDS, r_pathTracingEmissiveInventoryMaxTriangles.GetInteger()),
        emissiveInventoryStats);
    const int emissiveMs = Sys_Milliseconds() - emissiveStartMs;
    if (r_pathTracingEmissiveInventoryDump.GetInteger() != 0)
    {
        LogSmokeEmissiveInventoryDump(materialTable.materialIds, emissiveTriangles, emissiveInventoryStats);
        r_pathTracingEmissiveInventoryDump.SetInteger(0);
    }

    const int bufferCreateStartMs = Sys_Milliseconds();
    RtSmokeSceneBufferCreateDesc bufferCreateDesc;
    bufferCreateDesc.device = device;
    bufferCreateDesc.staticVertexBytes = m_smokeStaticVertexCache.size() * sizeof(m_smokeStaticVertexCache[0]);
    bufferCreateDesc.staticIndexBytes = m_smokeStaticIndexCache.size() * sizeof(m_smokeStaticIndexCache[0]);
    bufferCreateDesc.staticTriangleClassBytes = m_smokeStaticTriangleClassCache.size() * sizeof(m_smokeStaticTriangleClassCache[0]);
    bufferCreateDesc.staticTriangleMaterialBytes = m_smokeStaticTriangleMaterialCache.size() * sizeof(m_smokeStaticTriangleMaterialCache[0]);
    bufferCreateDesc.staticTriangleMaterialIndexBytes = materialTable.staticMaterialIndexes.size() * sizeof(materialTable.staticMaterialIndexes[0]);
    bufferCreateDesc.dynamicVertexBytes = dynamicVertexData.size() * sizeof(dynamicVertexData[0]);
    bufferCreateDesc.dynamicIndexBytes = dynamicIndexData.size() * sizeof(dynamicIndexData[0]);
    bufferCreateDesc.dynamicTriangleClassBytes = dynamicTriangleClassData.size() * sizeof(dynamicTriangleClassData[0]);
    bufferCreateDesc.dynamicTriangleMaterialBytes = dynamicTriangleMaterialData.size() * sizeof(dynamicTriangleMaterialData[0]);
    bufferCreateDesc.dynamicTriangleMaterialIndexBytes = materialTable.dynamicMaterialIndexes.size() * sizeof(materialTable.dynamicMaterialIndexes[0]);
    bufferCreateDesc.materialTableBytes = materialTable.materials.size() * sizeof(materialTable.materials[0]);
    bufferCreateDesc.emissiveTriangleBytes = emissiveTriangles.size() * sizeof(emissiveTriangles[0]);
    const RtSmokeSceneBufferCreateResult bufferCreateResult = CreateSmokeSceneBuffers(bufferCreateDesc);
    if (!bufferCreateResult.Succeeded())
    {
        common->Printf("PathTracePrimaryPass: %s\n", bufferCreateResult.errorMessage ? bufferCreateResult.errorMessage : "failed to create RT smoke geometry buffers");
        return;
    }
    RtSmokeSceneBufferHandles smokeBuffers = bufferCreateResult.buffers;
    nvrhi::BufferHandle smokeStaticVertexBuffer = smokeBuffers.staticVertexBuffer;
    nvrhi::BufferHandle smokeStaticIndexBuffer = smokeBuffers.staticIndexBuffer;
    nvrhi::BufferHandle smokeStaticTriangleClassBuffer = smokeBuffers.staticTriangleClassBuffer;
    nvrhi::BufferHandle smokeStaticTriangleMaterialBuffer = smokeBuffers.staticTriangleMaterialBuffer;
    nvrhi::BufferHandle smokeStaticTriangleMaterialIndexBuffer = smokeBuffers.staticTriangleMaterialIndexBuffer;
    nvrhi::BufferHandle smokeDynamicVertexBuffer = smokeBuffers.dynamicVertexBuffer;
    nvrhi::BufferHandle smokeDynamicIndexBuffer = smokeBuffers.dynamicIndexBuffer;
    nvrhi::BufferHandle smokeDynamicTriangleClassBuffer = smokeBuffers.dynamicTriangleClassBuffer;
    nvrhi::BufferHandle smokeDynamicTriangleMaterialBuffer = smokeBuffers.dynamicTriangleMaterialBuffer;
    nvrhi::BufferHandle smokeDynamicTriangleMaterialIndexBuffer = smokeBuffers.dynamicTriangleMaterialIndexBuffer;
    nvrhi::BufferHandle smokeMaterialTableBuffer = smokeBuffers.materialTableBuffer;
    nvrhi::BufferHandle smokeEmissiveTriangleBuffer = smokeBuffers.emissiveTriangleBuffer;
    const int bufferCreateMs = Sys_Milliseconds() - bufferCreateStartMs;

    const int staticVertexCount = static_cast<int>(m_smokeStaticVertexCache.size());
    const int dynamicVertexCount = static_cast<int>(dynamicVertexData.size());
    const int staticIndexCount = bucketRanges.buckets[0].indexCount;
    const int dynamicIndexCount =
        bucketRanges.buckets[1].indexCount +
        bucketRanges.buckets[2].indexCount +
        bucketRanges.buckets[3].indexCount +
        bucketRanges.buckets[4].indexCount;
    const bool hasStaticBlas = staticIndexCount > 0;
    const bool hasDynamicBlas = dynamicIndexCount > 0;
    const RtSmokeBucketRange& staticBucketRange = bucketRanges.buckets[0];
    RtSmokeGeometryRange staticGeometryRange;
    staticGeometryRange.vertexOffset = staticBucketRange.vertexOffset;
    staticGeometryRange.vertexCount = staticBucketRange.vertexCount;
    staticGeometryRange.indexOffset = staticBucketRange.indexOffset;
    staticGeometryRange.indexCount = staticBucketRange.indexCount;
    staticGeometryRange.triangleOffset = staticBucketRange.triangleOffset;
    staticGeometryRange.triangleCount = staticBucketRange.triangleCount;
    const RtSmokeStaticBlasSignature staticSignature = ComputeSmokeStaticBlasSignature(m_smokeStaticVertexCache, m_smokeStaticIndexCache, m_smokeStaticTriangleClassCache, m_smokeStaticTriangleMaterialCache, staticGeometryRange, vec3_origin);
    const bool staticBlasCacheHit = hasStaticBlas && m_smokeStaticBlasCacheValid && m_smokeStaticBlas &&
        m_smokeStaticVertexBuffer && m_smokeStaticIndexBuffer && m_smokeStaticTriangleClassBuffer && m_smokeStaticTriangleMaterialBuffer && m_smokeStaticTriangleMaterialIndexBuffer &&
        !staticCacheChanged && m_smokeStaticBlasSignature == staticSignature.hash;
    if (staticBlasCacheHit)
    {
        smokeStaticVertexBuffer = m_smokeStaticVertexBuffer;
        smokeStaticIndexBuffer = m_smokeStaticIndexBuffer;
        smokeStaticTriangleClassBuffer = m_smokeStaticTriangleClassBuffer;
        smokeStaticTriangleMaterialBuffer = m_smokeStaticTriangleMaterialBuffer;
        smokeStaticTriangleMaterialIndexBuffer = m_smokeStaticTriangleMaterialIndexBuffer;
        smokeBuffers.staticVertexBuffer = smokeStaticVertexBuffer;
        smokeBuffers.staticIndexBuffer = smokeStaticIndexBuffer;
        smokeBuffers.staticTriangleClassBuffer = smokeStaticTriangleClassBuffer;
        smokeBuffers.staticTriangleMaterialBuffer = smokeStaticTriangleMaterialBuffer;
        smokeBuffers.staticTriangleMaterialIndexBuffer = smokeStaticTriangleMaterialIndexBuffer;
    }

    if (!hasStaticBlas && !hasDynamicBlas)
    {
        common->Printf("PathTracePrimaryPass: no RT smoke BLAS ranges to build\n");
        return;
    }

    nvrhi::rt::AccelStructDesc smokeStaticBlasDesc;
    nvrhi::rt::AccelStructHandle smokeStaticBlas;
    if (hasStaticBlas)
    {
        if (staticBlasCacheHit)
        {
            smokeStaticBlas = m_smokeStaticBlas;
            smokeStaticBlasDesc = m_smokeStaticBlasDesc;
            ++m_smokeStaticBlasCacheHitCount;
        }
        else
        {
            RtSmokeBlasCreateDesc staticBlasCreateDesc;
            staticBlasCreateDesc.device = device;
            staticBlasCreateDesc.vertexBuffer = smokeStaticVertexBuffer;
            staticBlasCreateDesc.indexBuffer = smokeStaticIndexBuffer;
            staticBlasCreateDesc.vertexCount = staticVertexCount;
            staticBlasCreateDesc.indexCount = staticIndexCount;
            staticBlasCreateDesc.debugName = "PathTraceSmokeStaticWorldBLAS";
            const RtSmokeBlasCreateResult staticBlasCreateResult = CreateSmokeBlas(staticBlasCreateDesc);
            if (!staticBlasCreateResult.Succeeded())
            {
                common->Printf("PathTracePrimaryPass: failed to create RT smoke static BLAS\n");
                return;
            }
            smokeStaticBlasDesc = staticBlasCreateResult.accelStructDesc;
            smokeStaticBlas = staticBlasCreateResult.accelStruct;
            ++m_smokeStaticBlasCacheMissCount;
        }
    }

    nvrhi::rt::AccelStructDesc smokeDynamicBlasDesc;
    nvrhi::rt::AccelStructHandle smokeDynamicBlas;
    if (hasDynamicBlas)
    {
        RtSmokeBlasCreateDesc dynamicBlasCreateDesc;
        dynamicBlasCreateDesc.device = device;
        dynamicBlasCreateDesc.vertexBuffer = smokeDynamicVertexBuffer;
        dynamicBlasCreateDesc.indexBuffer = smokeDynamicIndexBuffer;
        dynamicBlasCreateDesc.vertexCount = dynamicVertexCount;
        dynamicBlasCreateDesc.indexCount = dynamicIndexCount;
        dynamicBlasCreateDesc.debugName = "PathTraceSmokeDynamicCandidateBLAS";
        const RtSmokeBlasCreateResult dynamicBlasCreateResult = CreateSmokeBlas(dynamicBlasCreateDesc);
        if (!dynamicBlasCreateResult.Succeeded())
        {
            common->Printf("PathTracePrimaryPass: failed to create RT smoke dynamic BLAS\n");
            return;
        }
        smokeDynamicBlasDesc = dynamicBlasCreateResult.accelStructDesc;
        smokeDynamicBlas = dynamicBlasCreateResult.accelStruct;
    }

    const RtSmokeBufferUploadItem uploadItems[] = {
        { smokeStaticVertexBuffer, m_smokeStaticVertexCache.data(), m_smokeStaticVertexCache.size() * sizeof(PathTraceSmokeVertex), nvrhi::ResourceStates::AccelStructBuildInput, staticBlasCacheHit },
        { smokeStaticIndexBuffer, m_smokeStaticIndexCache.data(), m_smokeStaticIndexCache.size() * sizeof(uint32_t), nvrhi::ResourceStates::AccelStructBuildInput, staticBlasCacheHit },
        { smokeStaticTriangleClassBuffer, m_smokeStaticTriangleClassCache.data(), m_smokeStaticTriangleClassCache.size() * sizeof(uint32_t), nvrhi::ResourceStates::ShaderResource, staticBlasCacheHit },
        { smokeStaticTriangleMaterialBuffer, m_smokeStaticTriangleMaterialCache.data(), m_smokeStaticTriangleMaterialCache.size() * sizeof(uint32_t), nvrhi::ResourceStates::ShaderResource, staticBlasCacheHit },
        { smokeStaticTriangleMaterialIndexBuffer, materialTable.staticMaterialIndexes.data(), materialTable.staticMaterialIndexes.size() * sizeof(uint32_t), nvrhi::ResourceStates::ShaderResource, staticBlasCacheHit },
        { smokeDynamicVertexBuffer, dynamicVertexData.data(), dynamicVertexData.size() * sizeof(PathTraceSmokeVertex), nvrhi::ResourceStates::AccelStructBuildInput, false },
        { smokeDynamicIndexBuffer, dynamicIndexData.data(), dynamicIndexData.size() * sizeof(uint32_t), nvrhi::ResourceStates::AccelStructBuildInput, false },
        { smokeDynamicTriangleClassBuffer, dynamicTriangleClassData.data(), dynamicTriangleClassData.size() * sizeof(uint32_t), nvrhi::ResourceStates::ShaderResource, false },
        { smokeDynamicTriangleMaterialBuffer, dynamicTriangleMaterialData.data(), dynamicTriangleMaterialData.size() * sizeof(uint32_t), nvrhi::ResourceStates::ShaderResource, false },
        { smokeDynamicTriangleMaterialIndexBuffer, materialTable.dynamicMaterialIndexes.data(), materialTable.dynamicMaterialIndexes.size() * sizeof(uint32_t), nvrhi::ResourceStates::ShaderResource, false },
        { smokeMaterialTableBuffer, materialTable.materials.data(), materialTable.materials.size() * sizeof(PathTraceSmokeMaterial), nvrhi::ResourceStates::ShaderResource, false },
        { smokeEmissiveTriangleBuffer, emissiveTriangles.data(), emissiveTriangles.size() * sizeof(PathTraceSmokeEmissiveTriangle), nvrhi::ResourceStates::ShaderResource, false }
    };
    RtSmokeBufferUploadBatchDesc uploadBatchDesc;
    uploadBatchDesc.commandList = commandList;
    uploadBatchDesc.items = uploadItems;
    uploadBatchDesc.itemCount = static_cast<int>(sizeof(uploadItems) / sizeof(uploadItems[0]));
    const int bufferUploadMs = UploadSmokeAccelerationBuffers(uploadBatchDesc);

    RtSmokeAccelSubmitDesc accelSubmitDesc;
    accelSubmitDesc.commandList = commandList;
    accelSubmitDesc.tlas = m_smokeTlas;
    accelSubmitDesc.staticBlas = smokeStaticBlas;
    accelSubmitDesc.dynamicBlas = smokeDynamicBlas;
    accelSubmitDesc.staticBlasDesc = smokeStaticBlasDesc;
    accelSubmitDesc.dynamicBlasDesc = smokeDynamicBlasDesc;
    accelSubmitDesc.hasStaticBlas = hasStaticBlas;
    accelSubmitDesc.hasDynamicBlas = hasDynamicBlas;
    accelSubmitDesc.staticBlasCacheHit = staticBlasCacheHit;
    RtSmokeAccelSubmitTiming accelSubmitTiming;
    if (!SubmitSmokeAccelerationBuilds(accelSubmitDesc, accelSubmitTiming))
    {
        common->Printf("PathTracePrimaryPass: failed to submit RT smoke acceleration structures\n");
        return;
    }
    const int blasSubmitMs = accelSubmitTiming.blasSubmitMs;
    const int tlasSubmitMs = accelSubmitTiming.tlasSubmitMs;
    const int accelSubmitMs = accelSubmitTiming.accelSubmitMs;
    const int instanceCount = accelSubmitTiming.instanceCount;

    const nvrhi::TextureHandle fallbackTexture = globalImages && globalImages->whiteImage ? globalImages->whiteImage->GetTextureHandle() : nullptr;
    if (!fallbackTexture)
    {
        common->Printf("PathTracePrimaryPass: failed to find RT smoke fallback material texture\n");
        return;
    }

    RtSmokeBindingBuildDesc bindingBuildDesc;
    bindingBuildDesc.device = device;
    bindingBuildDesc.tlas = m_smokeTlas;
    bindingBuildDesc.outputTexture = m_smokeOutputTexture;
    bindingBuildDesc.accumulationTexture = m_smokeAccumulationTexture;
    bindingBuildDesc.fallbackTexture = fallbackTexture;
    bindingBuildDesc.constantsBuffer = m_smokeConstantsBuffer;
    bindingBuildDesc.bindingLayout = m_smokeBindingLayout;
    bindingBuildDesc.textureBindlessLayout = m_smokeTextureBindlessLayout;
    bindingBuildDesc.existingTextureDescriptorTable = m_smokeTextureDescriptorTable;
    bindingBuildDesc.sampler = m_backend->GetCommonPasses().m_AnisotropicWrapSampler;
    bindingBuildDesc.buffers = smokeBuffers;
    bindingBuildDesc.enableTextureProbe = enableTextureProbe;
    bindingBuildDesc.forceFallbackTexture = r_pathTracingTextureForceFallback.GetInteger() != 0;
    bindingBuildDesc.maxActiveTextures = RT_SMOKE_TEXTURE_EXPERIMENTAL_ACTIVE_CAP;

    RtSmokeBindingBuildResult bindingBuildResult = CreateSmokeBindingResources(bindingBuildDesc, materialTable);
    if (!bindingBuildResult.Succeeded())
    {
        if (bindingBuildResult.failedTextureSlot >= 0)
        {
            common->Printf("PathTracePrimaryPass: %s %d\n", bindingBuildResult.errorMessage, bindingBuildResult.failedTextureSlot);
        }
        else
        {
            common->Printf("PathTracePrimaryPass: %s\n", bindingBuildResult.errorMessage ? bindingBuildResult.errorMessage : "failed to create RT smoke binding resources");
        }
        return;
    }

    RtSmokeSceneResourceCommitDesc resourceCommitDesc;
    resourceCommitDesc.buffers = smokeBuffers;
    resourceCommitDesc.staticBlasDesc = smokeStaticBlasDesc;
    resourceCommitDesc.dynamicBlasDesc = smokeDynamicBlasDesc;
    resourceCommitDesc.staticBlas = smokeStaticBlas;
    resourceCommitDesc.dynamicBlas = smokeDynamicBlas;
    resourceCommitDesc.hasStaticBlas = hasStaticBlas;
    resourceCommitDesc.staticBlasSignature = staticSignature.hash;
    resourceCommitDesc.bindingSet = bindingBuildResult.bindingSet;
    resourceCommitDesc.textureDescriptorTable = bindingBuildResult.textureDescriptorTable;
    resourceCommitDesc.activeTextureTable = bindingBuildResult.activeTextureTable;
    resourceCommitDesc.materialTableEntryCount = static_cast<int>(materialTable.materials.size());
    resourceCommitDesc.emissiveTriangleCount = emissiveInventoryStats.capturedTriangles;
    resourceCommitDesc.emissiveStaticTriangleCount = emissiveInventoryStats.staticTriangles;
    resourceCommitDesc.emissiveDynamicTriangleCount = emissiveInventoryStats.dynamicTriangles;
    CommitRayTracingSmokeSceneResources(resourceCommitDesc);

    const int sceneMs = Sys_Milliseconds() - sceneStartMs;
    if (ShouldLogSmokeTiming(sceneMs, Sys_Milliseconds(), g_smokeLastSceneTimingLogMs))
    {
        RtSmokeSlowSceneBuildLogDesc slowLog;
        slowLog.sceneMs = sceneMs;
        slowLog.captureMs = captureMs;
        slowLog.captureValidationMs = captureTiming.validationMs;
        slowLog.captureAppendMs = captureTiming.appendMs;
        slowLog.captureBucketMergeMs = captureTiming.bucketMergeMs;
        slowLog.metadataMs = metadataMs;
        slowLog.metadataValidationMs = metadataValidationMs;
        slowLog.metadataRegistrationMs = metadataRegistrationMs;
        slowLog.materialMs = materialMs;
        slowLog.emissiveMs = emissiveMs;
        slowLog.bufferCreateMs = bufferCreateMs;
        slowLog.bufferUploadMs = bufferUploadMs;
        slowLog.accelSubmitMs = accelSubmitMs;
        slowLog.blasSubmitMs = blasSubmitMs;
        slowLog.tlasSubmitMs = tlasSubmitMs;
        slowLog.sourceSurfaces = sourceSurfaces;
        slowLog.sourceVerts = sourceVerts;
        slowLog.sourceIndexes = sourceIndexes;
        slowLog.dynamicIndexCount = dynamicIndexCount;
        slowLog.skinnedRtCpuSurfaces = dynamicStats.skinnedRtCpuSkinnedSurfaces;
        slowLog.skinnedRtCpuIndexes = dynamicStats.skinnedRtCpuSkinnedIndexes;
        slowLog.staticBlasCacheHit = staticBlasCacheHit;
        slowLog.materialTableCacheHit = materialTableCacheHit;
        slowLog.materialTableCacheHits = materialTableCacheStats.hits;
        slowLog.materialTableCacheMisses = materialTableCacheStats.misses;
        slowLog.materialMetadataCacheEnabled = r_pathTracingMaterialMetadataCache.GetInteger() != 0;
        slowLog.metadataCacheRefreshes = g_smokeMaterialMetadataFrameStats.cacheRefreshes;
        slowLog.metadataFullDiscovers = g_smokeMaterialMetadataFrameStats.fullDiscovers;
        slowLog.metadataNewEntries = g_smokeMaterialMetadataFrameStats.newEntries;
        slowLog.metadataRegistrations = g_smokeMaterialMetadataFrameStats.registrations;
        slowLog.metadataDuplicateSkips = g_smokeMaterialMetadataFrameStats.duplicateSkips;
        slowLog.metadataRegistrySize = SmokeMaterialTextureRegistrySize();
        slowLog.guiTextureCandidates = materialTable.guiTextureCandidates;
        slowLog.guiTexturesAccepted = materialTable.guiTexturesAccepted;
        slowLog.guiTexturesRejected = materialTable.guiTexturesRejected;
        slowLog.additiveDecals = materialTable.materialsAdditiveDecals;
        slowLog.lightCount = r_pathTracingLightCount.GetInteger();
        slowLog.debugMode = requestedDebugMode;
        LogSmokeSlowSceneBuild(slowLog);
    }

    if (r_pathTracingSmokeLog.GetInteger() != 0 && !m_smokeSceneRebuildLogged)
    {
        common->Printf("PathTracePrimaryPass: rebuilding RT smoke BLAS/TLAS every frame from current visible Doom surfaces (first frame: %d surfaces, %d verts, %d indexes, anchor triangle %d)\n",
            sourceSurfaces, sourceVerts, sourceIndexes, anchorTriangle);
        common->Printf("PathTracePrimaryPass: RT smoke capture buckets static-world=%d rigid-entity=%d skinned=%d particle/alpha=%d unknown=%d\n",
            classStats.staticWorldSurfaces,
            classStats.rigidEntitySurfaces,
            classStats.skinnedDeformedSurfaces,
            classStats.particleAlphaSurfaces,
            classStats.unknownSurfaces);
        common->Printf("PathTracePrimaryPass: RT smoke BLAS split static-world=%d indexes, dynamic-candidate=%d indexes, TLAS instances=%d\n",
            staticIndexCount, dynamicIndexCount, instanceCount);
        common->Printf("PathTracePrimaryPass: RT smoke dynamic geometry rigid=%d(%di) skinnedCpu=%d(%di) skinnedRtCpu=%d(%di) skinnedLikelyBasePose=%d(%di) particle/alpha=%d(%di) unknown=%d(%di)\n",
            dynamicStats.rigidSurfaces, dynamicStats.rigidIndexes,
            dynamicStats.skinnedCpuCurrentSurfaces, dynamicStats.skinnedCpuCurrentIndexes,
            dynamicStats.skinnedRtCpuSkinnedSurfaces, dynamicStats.skinnedRtCpuSkinnedIndexes,
            dynamicStats.skinnedLikelyBasePoseSurfaces, dynamicStats.skinnedLikelyBasePoseIndexes,
            dynamicStats.particleAlphaSurfaces, dynamicStats.particleAlphaIndexes,
            dynamicStats.unknownSurfaces, dynamicStats.unknownIndexes);
        common->Printf("PathTracePrimaryPass: RT smoke static BLAS cache %s signature=%llu cacheSurfaces=%d hits=%d misses=%d\n",
            staticBlasCacheHit ? "hit" : "rebuild",
            static_cast<unsigned long long>(staticSignature.hash),
            static_cast<int>(m_smokeStaticSurfaceKeys.size()),
            m_smokeStaticBlasCacheHitCount,
            m_smokeStaticBlasCacheMissCount);
        common->Printf("PathTracePrimaryPass: RT smoke material table cache %s signature=%llu hits=%d misses=%d materials=%d textures=%d\n",
            materialTableCacheHit ? "hit" : "rebuild",
            static_cast<unsigned long long>(materialTableSignature),
            materialTableCacheStats.hits,
            materialTableCacheStats.misses,
            static_cast<int>(materialTable.materials.size()),
            static_cast<int>(materialTable.diffuseTextures.size()));
        LogSmokeMaterialStats(materialStats);
        LogSmokeMaterialTable(materialTable);
        if (enableTextureProbe)
        {
            LogSmokeTextureProbe(materialTable);
            LogSmokeTextureCoverage(textureCoverageStats);
            LogSmokeMaterialTextureDiscovery(materialTable);
        }
        LogSmokeAttributeStats(attributeStats);
        LogSmokeBucketRanges(bucketRanges);
        m_smokeSceneRebuildLogged = true;
    }

    if (dumpClassReasons)
    {
        common->Printf("PathTracePrimaryPass: RT smoke one-shot surface classification sample dump\n");
        LogSmokeSurfaceClassReasonSamples(reasonSamples);
        r_pathTracingClassDump.SetInteger(0);
    }

    if (r_pathTracingSmokeLog.GetInteger() != 0 && m_smokeSceneLogCooldownFrames <= 0)
    {
        common->Printf("PathTracePrimaryPass: RT smoke scene capture %d surfaces (dynamic cap %d), %d/%d verts, %d/%d indexes, anchor triangle %d; skipped null=%d geo=%d material=%d gui=%d allowGuiSurfaces=%d space=%d model=%d callback=%d skipCallbacks=%d indexes=%d cache=%d limits=%d zeroArea=%d emptyClass=%d; buckets static-world=%d(%dv/%di/%dt) rigid-entity=%d(%dv/%di/%dt) skinned=%d(%dv/%di/%dt) particle/alpha=%d(%dv/%di/%dt) unknown=%d(%dv/%di/%dt)\n",
            sourceSurfaces, RT_SMOKE_MAX_SURFACES,
            sourceVerts, RT_SMOKE_MAX_VERTS,
            sourceIndexes, RT_SMOKE_MAX_INDEXES,
            anchorTriangle,
            skipStats.nullSurface,
            skipStats.missingGeometry,
            skipStats.nullMaterial,
            skipStats.guiSurface,
            r_pathTracingAllowGuiSurfaces.GetInteger() != 0 ? 1 : 0,
            skipStats.nullSpace,
            skipStats.nullModel,
            skipStats.callbackEntity,
            r_pathTracingSkipCallbackEntities.GetInteger() != 0 ? 1 : 0,
            skipStats.invalidIndexCount,
            skipStats.nonCurrentCache,
            skipStats.limitExceeded,
            skipStats.zeroAreaOnly,
            skipStats.emptyClassBuffer,
            classStats.staticWorldSurfaces, classStats.staticWorldVerts, classStats.staticWorldIndexes, classStats.staticWorldTriangles,
            classStats.rigidEntitySurfaces, classStats.rigidEntityVerts, classStats.rigidEntityIndexes, classStats.rigidEntityTriangles,
            classStats.skinnedDeformedSurfaces, classStats.skinnedDeformedVerts, classStats.skinnedDeformedIndexes, classStats.skinnedDeformedTriangles,
            classStats.particleAlphaSurfaces, classStats.particleAlphaVerts, classStats.particleAlphaIndexes, classStats.particleAlphaTriangles,
            classStats.unknownSurfaces, classStats.unknownVerts, classStats.unknownIndexes, classStats.unknownTriangles);
        common->Printf("PathTracePrimaryPass: RT smoke BLAS split static-world=%d indexes, dynamic-candidate=%d indexes, TLAS instances=%d\n",
            staticIndexCount, dynamicIndexCount, instanceCount);
        common->Printf("PathTracePrimaryPass: RT smoke dynamic geometry rigid=%d(%di) skinnedCpu=%d(%di) skinnedRtCpu=%d(%di) skinnedLikelyBasePose=%d(%di) particle/alpha=%d(%di) unknown=%d(%di)\n",
            dynamicStats.rigidSurfaces, dynamicStats.rigidIndexes,
            dynamicStats.skinnedCpuCurrentSurfaces, dynamicStats.skinnedCpuCurrentIndexes,
            dynamicStats.skinnedRtCpuSkinnedSurfaces, dynamicStats.skinnedRtCpuSkinnedIndexes,
            dynamicStats.skinnedLikelyBasePoseSurfaces, dynamicStats.skinnedLikelyBasePoseIndexes,
            dynamicStats.particleAlphaSurfaces, dynamicStats.particleAlphaIndexes,
            dynamicStats.unknownSurfaces, dynamicStats.unknownIndexes);
        common->Printf("PathTracePrimaryPass: RT smoke static BLAS cache %s signature=%llu cacheSurfaces=%d hits=%d misses=%d\n",
            staticBlasCacheHit ? "hit" : "rebuild",
            static_cast<unsigned long long>(staticSignature.hash),
            static_cast<int>(m_smokeStaticSurfaceKeys.size()),
            m_smokeStaticBlasCacheHitCount,
            m_smokeStaticBlasCacheMissCount);
        common->Printf("PathTracePrimaryPass: RT smoke material table cache %s signature=%llu hits=%d misses=%d materials=%d textures=%d\n",
            materialTableCacheHit ? "hit" : "rebuild",
            static_cast<unsigned long long>(materialTableSignature),
            materialTableCacheStats.hits,
            materialTableCacheStats.misses,
            static_cast<int>(materialTable.materials.size()),
            static_cast<int>(materialTable.diffuseTextures.size()));
        LogSmokeMaterialStats(materialStats);
        LogSmokeMaterialTable(materialTable);
        if (enableTextureProbe)
        {
            LogSmokeTextureProbe(materialTable);
            LogSmokeTextureCoverage(textureCoverageStats);
            LogSmokeMaterialTextureDiscovery(materialTable);
        }
        LogSmokeAttributeStats(attributeStats);
        LogSmokeBucketRanges(bucketRanges);
        m_smokeSceneLogCooldownFrames = RT_SMOKE_SCENE_LOG_INTERVAL_FRAMES;
    }
    else
    {
        --m_smokeSceneLogCooldownFrames;
    }
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
