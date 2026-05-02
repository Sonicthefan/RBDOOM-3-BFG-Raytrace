#pragma once

#include "PathTraceDynamicMaterialState.h"
#include "PathTraceSceneCapture.h"
#include "../Image.h"
#include "../Material.h"
#include "../Model.h"

#include <vector>

const int RT_SMOKE_DEBUG_TEXTURE_COVERAGE_CLASS_COUNT = 5;

struct viewDef_t;

struct RtSmokeTextureCoverageClassStats
{
    int triangles = 0;
    int boundTriangles = 0;
    int fallbackTriangles = 0;
    int invalidMaterialTriangles = 0;
};

struct RtSmokeTextureCoverageStats
{
    RtSmokeTextureCoverageClassStats classes[RT_SMOKE_DEBUG_TEXTURE_COVERAGE_CLASS_COUNT];
    int materials = 0;
    int boundMaterials = 0;
    int fallbackMaterials = 0;
};

struct RtSmokeSlowSceneBuildLogDesc
{
    int sceneMs = 0;
    int captureMs = 0;
    int captureValidationMs = 0;
    int captureAppendMs = 0;
    int captureBucketMergeMs = 0;
    int metadataMs = 0;
    int metadataValidationMs = 0;
    int metadataRegistrationMs = 0;
    int materialMs = 0;
    int emissiveMs = 0;
    int bufferCreateMs = 0;
    int bufferUploadMs = 0;
    int accelSubmitMs = 0;
    int blasSubmitMs = 0;
    int tlasSubmitMs = 0;
    int sourceSurfaces = 0;
    int sourceVerts = 0;
    int sourceIndexes = 0;
    int dynamicIndexCount = 0;
    int skinnedRtCpuSurfaces = 0;
    int skinnedRtCpuIndexes = 0;
    bool staticBlasCacheHit = false;
    bool materialTableCacheHit = false;
    int materialTableCacheHits = 0;
    int materialTableCacheMisses = 0;
    bool materialMetadataCacheEnabled = false;
    int metadataCacheRefreshes = 0;
    int metadataFullDiscovers = 0;
    int metadataNewEntries = 0;
    int metadataRegistrations = 0;
    int metadataDuplicateSkips = 0;
    int metadataRegistrySize = 0;
    int guiTextureCandidates = 0;
    int guiTexturesAccepted = 0;
    int guiTexturesRejected = 0;
    int additiveDecals = 0;
    int lightCount = 0;
    int debugMode = 0;
};

struct RtSmokeSceneBuildSummaryLogDesc
{
    int sourceSurfaces = 0;
    int sourceVerts = 0;
    int sourceIndexes = 0;
    int anchorTriangle = -1;
    int staticIndexCount = 0;
    int dynamicIndexCount = 0;
    int instanceCount = 0;
    RtSmokeSurfaceClassStats classStats;
    RtSmokeSurfaceSkipStats skipStats;
    RtSmokeDynamicGeometryStats dynamicStats;
    bool allowGuiSurfaces = false;
    bool skipCallbackEntities = false;
    bool staticBlasCacheHit = false;
    uint64 staticBlasSignature = 0;
    int staticSurfaceCacheSize = 0;
    int staticBlasCacheHitCount = 0;
    int staticBlasCacheMissCount = 0;
    bool materialTableCacheHit = false;
    uint64 materialTableSignature = 0;
    RtSmokeMaterialTableCacheStats materialTableCacheStats;
    const RtSmokeMaterialStats* materialStats = nullptr;
    const RtSmokeMaterialTableBuild* materialTable = nullptr;
    bool enableTextureProbe = false;
    const RtSmokeTextureCoverageStats* textureCoverageStats = nullptr;
    const RtSmokeAttributeStats* attributeStats = nullptr;
    const RtSmokeBucketRanges* bucketRanges = nullptr;
};

struct RtSmokeMaterialDiagnosticTriggerDesc
{
    const viewDef_t* viewDef = nullptr;
    const RtSmokeMaterialTableBuild* materialTable = nullptr;
    const RtSmokeMaterialStats* materialStats = nullptr;
    bool enableTextureProbe = false;
};

struct RtSmokeEmissiveInventoryDiagnosticTriggerDesc
{
    const RtSmokeMaterialTableBuild* materialTable = nullptr;
    const std::vector<PathTraceSmokeEmissiveTriangle>* emissiveTriangles = nullptr;
    const RtSmokeEmissiveInventoryStats* emissiveInventoryStats = nullptr;
};

struct RtSmokeSceneBuildDiagnosticLogDesc
{
    int sceneMs = 0;
    int captureMs = 0;
    int metadataMs = 0;
    int metadataValidationMs = 0;
    int metadataRegistrationMs = 0;
    int materialMs = 0;
    int emissiveMs = 0;
    int bufferCreateMs = 0;
    int bufferUploadMs = 0;
    int accelSubmitMs = 0;
    int blasSubmitMs = 0;
    int tlasSubmitMs = 0;
    int sourceSurfaces = 0;
    int sourceVerts = 0;
    int sourceIndexes = 0;
    int anchorTriangle = -1;
    int staticIndexCount = 0;
    int dynamicIndexCount = 0;
    int instanceCount = 0;
    int requestedDebugMode = 0;
    int staticSurfaceCacheSize = 0;
    int staticBlasCacheHitCount = 0;
    int staticBlasCacheMissCount = 0;
    int sceneCaptureLogIntervalFrames = 0;
    bool staticBlasCacheHit = false;
    bool materialTableCacheHit = false;
    bool enableTextureProbe = false;
    bool dumpClassReasons = false;
    uint64 staticBlasSignature = 0;
    uint64 materialTableSignature = 0;
    RtSmokeSceneCaptureTiming captureTiming;
    const RtSmokeSurfaceClassStats* classStats = nullptr;
    const RtSmokeSurfaceSkipStats* skipStats = nullptr;
    const RtSmokeDynamicGeometryStats* dynamicStats = nullptr;
    const RtSmokeAttributeStats* attributeStats = nullptr;
    const RtSmokeMaterialStats* materialStats = nullptr;
    const RtSmokeBucketRanges* bucketRanges = nullptr;
    const RtSmokeMaterialTableBuild* materialTable = nullptr;
    const RtSmokeMaterialTableCacheStats* materialTableCacheStats = nullptr;
    const RtSmokeTextureCoverageStats* textureCoverageStats = nullptr;
    const RtSmokeSurfaceClassReasonSamples* reasonSamples = nullptr;
    int* lastSceneTimingLogMs = nullptr;
    bool* sceneRebuildLogged = nullptr;
    int* sceneLogCooldownFrames = nullptr;
};

const char* SmokeCoverageName(materialCoverage_t coverage);
const char* SmokeStageLightingName(stageLighting_t lighting);
const char* SmokeTexgenName(texgen_t texgen);
const char* SmokeTextureUsageName(textureUsage_t usage);
const char* SmokeTextureColorFormatName(textureColor_t colorFormat);
const char* SmokeDeformName(deform_t deform);
const char* SmokeDynamicModelName(dynamicModel_t dynamicModel);

bool ShouldLogSmokeTiming(int elapsedMs, int nowMs, int& lastLogMs);
void LogSmokeSlowSceneBuild(const RtSmokeSlowSceneBuildLogDesc& desc);
void LogSmokeSceneRebuildSummary(const RtSmokeSceneBuildSummaryLogDesc& desc);
void LogSmokeSceneCaptureSummary(const RtSmokeSceneBuildSummaryLogDesc& desc);
void LogSmokeSurfaceClassReasonSamples(const RtSmokeSurfaceClassReasonSamples& samples);
void LogSmokeBucketRanges(const RtSmokeBucketRanges& ranges);
void LogSmokeAttributeStats(const RtSmokeAttributeStats& stats);
void LogSmokeMaterialStats(const RtSmokeMaterialStats& stats);
void LogSmokeTranslucentSubtypeDump(const RtSmokeMaterialStats& stats);
RtSmokeTextureCoverageStats BuildSmokeTextureCoverageStats(
    const RtSmokeMaterialTableBuild& table,
    const std::vector<uint32_t>& staticTriangleClassData,
    const std::vector<uint32_t>& staticTriangleMaterialIndexes,
    const std::vector<uint32_t>& dynamicTriangleClassData,
    const std::vector<uint32_t>& dynamicTriangleMaterialIndexes);
void LogSmokeMaterialTable(const RtSmokeMaterialTableBuild& table);
void LogSmokeTextureProbe(const RtSmokeMaterialTableBuild& table);
void LogSmokeTextureProbeSwitch(const RtSmokeMaterialTableBuild& table);
void LogSmokeAlphaMaterialDump(const RtSmokeMaterialTableBuild& table);
void LogSmokeTextureProbeDump(const RtSmokeMaterialTableBuild& table);
void LogSmokeTextureActiveWindow(const RtSmokeMaterialTableBuild& table);
void LogSmokeTextureCoverage(const RtSmokeTextureCoverageStats& stats);
void LogSmokeTextureFallbackDump(const RtSmokeMaterialTableBuild& table);
void LogSmokeMaterialTextureDiscovery(const RtSmokeMaterialTableBuild& table);
void RunSmokeMaterialDiagnosticTriggers(const RtSmokeMaterialDiagnosticTriggerDesc& desc);
void RunSmokeEmissiveInventoryDiagnosticTriggers(const RtSmokeEmissiveInventoryDiagnosticTriggerDesc& desc);
void RunSmokeSceneBuildDiagnosticLogs(const RtSmokeSceneBuildDiagnosticLogDesc& desc);
