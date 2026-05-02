#pragma once

#include "PathTraceDynamicMaterialState.h"
#include "PathTraceSceneCapture.h"
#include "../Image.h"
#include "../Material.h"
#include "../Model.h"

#include <vector>

const int RT_SMOKE_DEBUG_TEXTURE_COVERAGE_CLASS_COUNT = 5;

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

const char* SmokeCoverageName(materialCoverage_t coverage);
const char* SmokeStageLightingName(stageLighting_t lighting);
const char* SmokeTexgenName(texgen_t texgen);
const char* SmokeTextureUsageName(textureUsage_t usage);
const char* SmokeTextureColorFormatName(textureColor_t colorFormat);
const char* SmokeDeformName(deform_t deform);
const char* SmokeDynamicModelName(dynamicModel_t dynamicModel);

bool ShouldLogSmokeTiming(int elapsedMs, int nowMs, int& lastLogMs);
void LogSmokeSlowSceneBuild(const RtSmokeSlowSceneBuildLogDesc& desc);
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
