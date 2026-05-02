#include "precompiled.h"
#pragma hdrstop

// Per-frame scene build orchestration for the RT smoke/path tracing path.
//
// This file keeps the top-level build order visible: capture Doom surfaces,
// build material/emissive data, create and upload buffers, submit acceleration
// structures, create bindings, commit resources, then run scene diagnostics.
// Lower-level classification, capture, resource, and diagnostic work stays in
// the narrower PathTrace* modules.

#include "PathTraceAcceleration.h"
#include "PathTraceCVars.h"
#include "PathTraceDebugDumps.h"
#include "PathTraceDynamicMaterialState.h"
#include "PathTraceEmissiveCandidates.h"
#include "PathTraceMaterialTextureDiscovery.h"
#include "PathTracePrimaryPass.h"
#include "PathTraceSceneCapture.h"
#include "PathTraceSmokeResources.h"
#include "PathTraceSurfaceClassification.h"
#include "../RenderBackend.h"
#include "../Image.h"
#include "../Passes/CommonPasses.h"
#include "../../framework/Common_local.h"
#include "../../sys/DeviceManager.h"

#include <vector>

extern DeviceManager* deviceManager;

namespace {

const int RT_SMOKE_SCENE_LOG_INTERVAL_FRAMES = 120;
const int RT_SMOKE_MAX_EMISSIVE_TRIANGLE_RECORDS = 65536;

int g_smokeLastSceneTimingLogMs = -1000000;

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
    RtSmokeMaterialTableCompareStats materialUniverseTableCompareStats;
    const bool useMaterialUniverseTable = r_pathTracingMaterialUniverseTable.GetInteger() != 0;
    const bool validateMaterialUniverseTable = r_pathTracingMaterialUniverseTableValidate.GetInteger() != 0;
    const char* materialTablePath = useMaterialUniverseTable ? "universe" : "legacy";
    if (useMaterialUniverseTable)
    {
        if (validateMaterialUniverseTable)
        {
            RtSmokeMaterialTableBuild legacyMaterialTable;
            uint32_t legacyLatchedTextureProbeMaterialId = m_smokeTextureProbeMaterialId;
            int legacyLatchedTextureProbeRequestedIndex = m_smokeTextureProbeRequestedIndex;
            BuildSmokeMaterialTableCached(legacyMaterialTable, m_smokeStaticTriangleMaterialCache, dynamicTriangleMaterialData, legacyLatchedTextureProbeMaterialId, legacyLatchedTextureProbeRequestedIndex, enableTextureProbe, materialTableSignature, materialTableCacheHit);
            BuildSmokeMaterialTableFromUniverseCached(materialTable, m_smokeStaticTriangleMaterialCache, dynamicTriangleMaterialData, m_smokeTextureProbeMaterialId, m_smokeTextureProbeRequestedIndex, enableTextureProbe, materialTableSignature, materialTableCacheHit);
            materialUniverseTableCompareStats = CompareSmokeMaterialTables(legacyMaterialTable, materialTable);
            if (materialUniverseTableCompareStats.mismatches > 0)
            {
                common->Printf("PathTracePrimaryPass: RT smoke material universe table mismatch, falling back to legacy table for this frame (mismatches=%d material=%d/%d/%d indexes=%d/%d textures=%d/%d)\n",
                    materialUniverseTableCompareStats.mismatches,
                    materialUniverseTableCompareStats.materialCountMismatches,
                    materialUniverseTableCompareStats.materialIdMismatches,
                    materialUniverseTableCompareStats.materialRecordMismatches,
                    materialUniverseTableCompareStats.staticIndexMismatches,
                    materialUniverseTableCompareStats.dynamicIndexMismatches,
                    materialUniverseTableCompareStats.textureCountMismatches,
                    materialUniverseTableCompareStats.textureHandleMismatches);
                materialTable = legacyMaterialTable;
                m_smokeTextureProbeMaterialId = legacyLatchedTextureProbeMaterialId;
                m_smokeTextureProbeRequestedIndex = legacyLatchedTextureProbeRequestedIndex;
                materialTablePath = "legacyFallback";
            }
        }
        else
        {
            BuildSmokeMaterialTableFromUniverseCached(materialTable, m_smokeStaticTriangleMaterialCache, dynamicTriangleMaterialData, m_smokeTextureProbeMaterialId, m_smokeTextureProbeRequestedIndex, enableTextureProbe, materialTableSignature, materialTableCacheHit);
        }
    }
    else
    {
        BuildSmokeMaterialTableCached(materialTable, m_smokeStaticTriangleMaterialCache, dynamicTriangleMaterialData, m_smokeTextureProbeMaterialId, m_smokeTextureProbeRequestedIndex, enableTextureProbe, materialTableSignature, materialTableCacheHit);
        if (validateMaterialUniverseTable)
        {
            RtSmokeMaterialTableBuild universeMaterialTable;
            uint32_t universeLatchedTextureProbeMaterialId = m_smokeTextureProbeMaterialId;
            int universeLatchedTextureProbeRequestedIndex = m_smokeTextureProbeRequestedIndex;
            BuildSmokeMaterialTableFromUniverse(universeMaterialTable, m_smokeStaticTriangleMaterialCache, dynamicTriangleMaterialData, universeLatchedTextureProbeMaterialId, universeLatchedTextureProbeRequestedIndex, enableTextureProbe);
            materialUniverseTableCompareStats = CompareSmokeMaterialTables(materialTable, universeMaterialTable);
        }
    }
    const RtSmokeMaterialTableCacheStats materialTableCacheStats = GetSmokeMaterialTableCacheStats();
    const RtSmokeMaterialTableBuildStats materialTableBuildStats = GetSmokeMaterialTableBuildStats();
    const RtSmokeMaterialUniverseStats materialUniverseStats = GetSmokeMaterialUniverseStats();
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
    RtSmokeMaterialDiagnosticTriggerDesc materialDiagnosticDesc;
    materialDiagnosticDesc.viewDef = viewDef;
    materialDiagnosticDesc.materialTable = &materialTable;
    materialDiagnosticDesc.materialStats = &materialStats;
    materialDiagnosticDesc.enableTextureProbe = enableTextureProbe;
    RunSmokeMaterialDiagnosticTriggers(materialDiagnosticDesc);

    RtSmokeEmissiveInventoryStats emissiveInventoryStats;
    const int emissiveStartMs = Sys_Milliseconds();
    std::vector<PathTraceSmokeEmissiveTriangle> emissiveTriangles = BuildSmokeEmissiveTriangleInventory(
        materialTable.materialIds,
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
    std::vector<PathTraceSmokeLightCandidate> lightCandidates = BuildSmokeLightCandidateBufferRecords(emissiveInventoryStats);
    const int emissiveMs = Sys_Milliseconds() - emissiveStartMs;
    RtSmokeEmissiveInventoryDiagnosticTriggerDesc emissiveInventoryDiagnosticDesc;
    emissiveInventoryDiagnosticDesc.materialTable = &materialTable;
    emissiveInventoryDiagnosticDesc.emissiveTriangles = &emissiveTriangles;
    emissiveInventoryDiagnosticDesc.emissiveInventoryStats = &emissiveInventoryStats;
    RunSmokeEmissiveInventoryDiagnosticTriggers(emissiveInventoryDiagnosticDesc);

    const int bufferCreateStartMs = Sys_Milliseconds();
    RtSmokeSceneBufferCreateDesc bufferCreateDesc;
    bufferCreateDesc.device = device;
    bufferCreateDesc.existingBuffers.staticVertexBuffer = m_smokeStaticVertexBuffer;
    bufferCreateDesc.existingBuffers.staticIndexBuffer = m_smokeStaticIndexBuffer;
    bufferCreateDesc.existingBuffers.staticTriangleClassBuffer = m_smokeStaticTriangleClassBuffer;
    bufferCreateDesc.existingBuffers.staticTriangleMaterialBuffer = m_smokeStaticTriangleMaterialBuffer;
    bufferCreateDesc.existingBuffers.staticTriangleMaterialIndexBuffer = m_smokeStaticTriangleMaterialIndexBuffer;
    bufferCreateDesc.existingBuffers.dynamicVertexBuffer = m_smokeDynamicVertexBuffer;
    bufferCreateDesc.existingBuffers.dynamicIndexBuffer = m_smokeDynamicIndexBuffer;
    bufferCreateDesc.existingBuffers.dynamicTriangleClassBuffer = m_smokeDynamicTriangleClassBuffer;
    bufferCreateDesc.existingBuffers.dynamicTriangleMaterialBuffer = m_smokeDynamicTriangleMaterialBuffer;
    bufferCreateDesc.existingBuffers.dynamicTriangleMaterialIndexBuffer = m_smokeDynamicTriangleMaterialIndexBuffer;
    bufferCreateDesc.existingBuffers.materialTableBuffer = m_smokeMaterialTableBuffer;
    bufferCreateDesc.existingBuffers.emissiveTriangleBuffer = m_smokeEmissiveTriangleBuffer;
    bufferCreateDesc.existingBuffers.lightCandidateBuffer = m_smokeLightCandidateBuffer;
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
    bufferCreateDesc.lightCandidateBytes = lightCandidates.size() * sizeof(lightCandidates[0]);
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
    nvrhi::BufferHandle smokeLightCandidateBuffer = smokeBuffers.lightCandidateBuffer;
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
    RtSmokeStaticBlasSignature staticSignature;
    staticSignature.vertexCount = staticGeometryRange.vertexCount;
    staticSignature.indexCount = staticGeometryRange.indexCount;
    staticSignature.triangleCount = staticGeometryRange.triangleCount;
    if (!staticCacheChanged && m_smokeStaticBlasCacheValid && m_smokeStaticBlasSignature != 0)
    {
        staticSignature.hash = m_smokeStaticBlasSignature;
    }
    else
    {
        staticSignature = ComputeSmokeStaticBlasSignature(m_smokeStaticVertexCache, m_smokeStaticIndexCache, m_smokeStaticTriangleClassCache, m_smokeStaticTriangleMaterialCache, staticGeometryRange, vec3_origin);
    }
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
        { smokeEmissiveTriangleBuffer, emissiveTriangles.data(), emissiveTriangles.size() * sizeof(PathTraceSmokeEmissiveTriangle), nvrhi::ResourceStates::ShaderResource, false },
        { smokeLightCandidateBuffer, lightCandidates.data(), lightCandidates.size() * sizeof(PathTraceSmokeLightCandidate), nvrhi::ResourceStates::ShaderResource, false }
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

    RtSmokeSceneResourceCommitBuildDesc resourceCommitBuildDesc;
    resourceCommitBuildDesc.buffers = smokeBuffers;
    resourceCommitBuildDesc.staticBlasDesc = smokeStaticBlasDesc;
    resourceCommitBuildDesc.dynamicBlasDesc = smokeDynamicBlasDesc;
    resourceCommitBuildDesc.staticBlas = smokeStaticBlas;
    resourceCommitBuildDesc.dynamicBlas = smokeDynamicBlas;
    resourceCommitBuildDesc.hasStaticBlas = hasStaticBlas;
    resourceCommitBuildDesc.staticBlasSignature = staticSignature.hash;
    resourceCommitBuildDesc.bindingSet = bindingBuildResult.bindingSet;
    resourceCommitBuildDesc.textureDescriptorTable = bindingBuildResult.textureDescriptorTable;
    resourceCommitBuildDesc.activeTextureTable = &bindingBuildResult.activeTextureTable;
    resourceCommitBuildDesc.materialTableEntryCount = static_cast<int>(materialTable.materials.size());
    resourceCommitBuildDesc.emissiveTriangleCount = emissiveInventoryStats.capturedTriangles;
    resourceCommitBuildDesc.emissiveStaticTriangleCount = emissiveInventoryStats.staticTriangles;
    resourceCommitBuildDesc.emissiveDynamicTriangleCount = emissiveInventoryStats.dynamicTriangles;
    resourceCommitBuildDesc.lightCandidateCount = emissiveInventoryStats.candidateMaterials;
    resourceCommitBuildDesc.texturedLightCandidateCount = emissiveInventoryStats.texturedCandidateMaterials;
    resourceCommitBuildDesc.lightCandidateBytes = static_cast<int>(lightCandidates.size() * sizeof(lightCandidates[0]));
    const RtSmokeSceneResourceCommitDesc resourceCommitDesc = CreateSmokeSceneResourceCommitDesc(resourceCommitBuildDesc);
    CommitRayTracingSmokeSceneResources(resourceCommitDesc);

    const int sceneMs = Sys_Milliseconds() - sceneStartMs;
    RtSmokeSceneBuildDiagnosticLogDesc sceneLogDesc;
    sceneLogDesc.sceneMs = sceneMs;
    sceneLogDesc.captureMs = captureMs;
    sceneLogDesc.metadataMs = metadataMs;
    sceneLogDesc.metadataValidationMs = metadataValidationMs;
    sceneLogDesc.metadataRegistrationMs = metadataRegistrationMs;
    sceneLogDesc.materialMs = materialMs;
    sceneLogDesc.emissiveMs = emissiveMs;
    sceneLogDesc.bufferCreateMs = bufferCreateMs;
    sceneLogDesc.bufferUploadMs = bufferUploadMs;
    sceneLogDesc.accelSubmitMs = accelSubmitMs;
    sceneLogDesc.blasSubmitMs = blasSubmitMs;
    sceneLogDesc.tlasSubmitMs = tlasSubmitMs;
    sceneLogDesc.sourceSurfaces = sourceSurfaces;
    sceneLogDesc.sourceVerts = sourceVerts;
    sceneLogDesc.sourceIndexes = sourceIndexes;
    sceneLogDesc.anchorTriangle = anchorTriangle;
    sceneLogDesc.staticIndexCount = staticIndexCount;
    sceneLogDesc.dynamicIndexCount = dynamicIndexCount;
    sceneLogDesc.instanceCount = instanceCount;
    sceneLogDesc.requestedDebugMode = requestedDebugMode;
    sceneLogDesc.staticSurfaceCacheSize = static_cast<int>(m_smokeStaticSurfaceKeys.size());
    sceneLogDesc.staticBlasCacheHitCount = m_smokeStaticBlasCacheHitCount;
    sceneLogDesc.staticBlasCacheMissCount = m_smokeStaticBlasCacheMissCount;
    sceneLogDesc.sceneCaptureLogIntervalFrames = RT_SMOKE_SCENE_LOG_INTERVAL_FRAMES;
    sceneLogDesc.staticBlasCacheHit = staticBlasCacheHit;
    sceneLogDesc.materialTableCacheHit = materialTableCacheHit;
    sceneLogDesc.enableTextureProbe = enableTextureProbe;
    sceneLogDesc.dumpClassReasons = dumpClassReasons;
    sceneLogDesc.staticBlasSignature = staticSignature.hash;
    sceneLogDesc.materialTableSignature = materialTableSignature;
    sceneLogDesc.materialTablePath = materialTablePath;
    sceneLogDesc.captureTiming = captureTiming;
    sceneLogDesc.classStats = &classStats;
    sceneLogDesc.skipStats = &skipStats;
    sceneLogDesc.dynamicStats = &dynamicStats;
    sceneLogDesc.attributeStats = &attributeStats;
    sceneLogDesc.materialStats = &materialStats;
    sceneLogDesc.bucketRanges = &bucketRanges;
    sceneLogDesc.materialTable = &materialTable;
    sceneLogDesc.emissiveInventoryStats = &emissiveInventoryStats;
    sceneLogDesc.lightCandidateBytes = static_cast<int>(lightCandidates.size() * sizeof(lightCandidates[0]));
    sceneLogDesc.materialTableCacheStats = &materialTableCacheStats;
    sceneLogDesc.materialTableBuildStats = &materialTableBuildStats;
    sceneLogDesc.materialUniverseStats = &materialUniverseStats;
    sceneLogDesc.materialUniverseTableCompareStats = &materialUniverseTableCompareStats;
    sceneLogDesc.textureCoverageStats = &textureCoverageStats;
    sceneLogDesc.reasonSamples = &reasonSamples;
    sceneLogDesc.lastSceneTimingLogMs = &g_smokeLastSceneTimingLogMs;
    sceneLogDesc.sceneRebuildLogged = &m_smokeSceneRebuildLogged;
    sceneLogDesc.sceneLogCooldownFrames = &m_smokeSceneLogCooldownFrames;
    RunSmokeSceneBuildDiagnosticLogs(sceneLogDesc);
}
