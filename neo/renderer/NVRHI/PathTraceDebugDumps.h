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

const char* SmokeCoverageName(materialCoverage_t coverage);
const char* SmokeStageLightingName(stageLighting_t lighting);
const char* SmokeTexgenName(texgen_t texgen);
const char* SmokeTextureUsageName(textureUsage_t usage);
const char* SmokeTextureColorFormatName(textureColor_t colorFormat);
const char* SmokeDeformName(deform_t deform);
const char* SmokeDynamicModelName(dynamicModel_t dynamicModel);

bool ShouldLogSmokeTiming(int elapsedMs, int nowMs, int& lastLogMs);
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
