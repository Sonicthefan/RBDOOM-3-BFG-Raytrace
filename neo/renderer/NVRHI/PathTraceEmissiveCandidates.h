#pragma once

// Emissive triangle inventory for mode 19 diagnostics and future sampling work.
//
// Scans static and dynamic triangle buckets after material table creation,
// records compact emissive candidate metadata, and keeps summary data for debug
// dumps and later ReSTIR/PT integration boundaries.

#include "PathTraceGeometry.h"

#include <vector>

struct PathTraceSmokeMaterial
{
    float debugAlbedo[4];
    float emissiveColor[4];
    uint32_t diffuseTextureIndex = UINT32_MAX;
    uint32_t alphaTextureIndex = UINT32_MAX;
    uint32_t normalTextureIndex = UINT32_MAX;
    uint32_t specularTextureIndex = UINT32_MAX;
    uint32_t emissiveTextureIndex = UINT32_MAX;
    float alphaCutoff = 0.0f;
    uint32_t flags = 0;
    uint32_t textureWidth = 1;
    uint32_t textureHeight = 1;
    uint32_t alphaTextureWidth = 1;
    uint32_t alphaTextureHeight = 1;
    uint32_t normalTextureWidth = 1;
    uint32_t normalTextureHeight = 1;
    uint32_t specularTextureWidth = 1;
    uint32_t specularTextureHeight = 1;
    uint32_t emissiveTextureWidth = 1;
    uint32_t emissiveTextureHeight = 1;
    uint32_t padding0 = 0;
    uint32_t padding1 = 0;
    uint32_t padding2 = 0;
};
static_assert((sizeof(PathTraceSmokeMaterial) % 16) == 0, "PathTraceSmokeMaterial must stay 16-byte aligned for HLSL StructuredBuffer reads");

struct PathTraceSmokeEmissiveTriangle
{
    float centerAndArea[4];
    float normalAndLuminance[4];
    float uvBounds[4];
    float centroidUvAndWeight[4];
    float estimatedRadianceAndLuminance[4];
    uint32_t materialIndex = 0;
    uint32_t instanceId = 0;
    uint32_t primitiveIndex = 0;
    uint32_t flags = 0;
    uint32_t emissiveTextureIndex = UINT32_MAX;
    uint32_t emissiveTextureWidth = 1;
    uint32_t emissiveTextureHeight = 1;
    uint32_t padding0 = 0;
};
static_assert((sizeof(PathTraceSmokeEmissiveTriangle) % 16) == 0, "PathTraceSmokeEmissiveTriangle must stay 16-byte aligned for HLSL StructuredBuffer reads");

const uint32_t RT_SMOKE_LIGHT_CANDIDATE_TEXTURED = 0x00000001u;
const uint32_t RT_SMOKE_LIGHT_CANDIDATE_SAFE_TEXTURE = 0x00000002u;
const uint32_t RT_SMOKE_LIGHT_CANDIDATE_HAS_STATIC_TRIANGLES = 0x00000004u;
const uint32_t RT_SMOKE_LIGHT_CANDIDATE_HAS_DYNAMIC_TRIANGLES = 0x00000008u;

struct PathTraceSmokeLightCandidate
{
    float emissiveColorAndLuminance[4];
    float areaAndWeightedLuminance[4];
    uint32_t materialId = 0;
    uint32_t universeMaterialIndex = UINT32_MAX;
    uint32_t materialIndex = 0;
    uint32_t triangleCount = 0;
    uint32_t flags = 0;
    uint32_t staticTriangleCount = 0;
    uint32_t dynamicTriangleCount = 0;
    uint32_t emissiveTextureIndex = UINT32_MAX;
    uint32_t emissiveTextureWidth = 1;
    uint32_t emissiveTextureHeight = 1;
    uint32_t padding1 = 0;
    uint32_t padding2 = 0;
};
static_assert((sizeof(PathTraceSmokeLightCandidate) % 16) == 0, "PathTraceSmokeLightCandidate must stay 16-byte aligned for HLSL StructuredBuffer reads");

struct RtSmokeEmissiveLightCandidateSummary
{
    uint32_t materialId = 0;
    uint32_t universeMaterialIndex = UINT32_MAX;
    uint32_t materialIndex = 0;
    int triangles = 0;
    int staticTriangles = 0;
    int dynamicTriangles = 0;
    float area = 0.0f;
    float weightedLuminance = 0.0f;
    float emissiveLuminance = 0.0f;
    idVec4 emissiveColor = idVec4(0.0f, 0.0f, 0.0f, 1.0f);
    bool hasEmissiveTexture = false;
    bool hasSafeEmissiveTexture = false;
    uint32_t emissiveTextureIndex = UINT32_MAX;
    uint32_t emissiveTextureWidth = 1;
    uint32_t emissiveTextureHeight = 1;
};

struct RtSmokeEmissiveInventoryStats
{
    int totalTriangles = 0;
    int staticTriangles = 0;
    int dynamicTriangles = 0;
    int capturedTriangles = 0;
    int skippedSkinnedTriangles = 0;
    int skippedInvalidMaterialTriangles = 0;
    int cappedTriangles = 0;
    int uniqueMaterials = 0;
    float totalArea = 0.0f;
    float totalWeightedLuminance = 0.0f;
    std::vector<uint32_t> materialIndexes;
    std::vector<RtSmokeEmissiveLightCandidateSummary> lightCandidates;
    int candidateMaterials = 0;
    int texturedCandidateMaterials = 0;
    int untexturedCandidateMaterials = 0;
};

struct RtSmokeEmissiveInventoryMaterialSummary
{
    uint32_t materialIndex = 0;
    int triangles = 0;
    int staticTriangles = 0;
    int dynamicTriangles = 0;
    float area = 0.0f;
    float weightedLuminance = 0.0f;
    uint32_t emissiveTextureIndex = UINT32_MAX;
    uint32_t emissiveTextureWidth = 1;
    uint32_t emissiveTextureHeight = 1;
};

float SmokeMaterialEmissiveLuminance(const PathTraceSmokeMaterial& material);
std::vector<PathTraceSmokeLightCandidate> BuildSmokeLightCandidateBufferRecords(
    const RtSmokeEmissiveInventoryStats& stats);
std::vector<PathTraceSmokeEmissiveTriangle> BuildSmokeEmissiveTriangleInventory(
    const std::vector<uint32_t>& materialIds,
    const std::vector<PathTraceSmokeMaterial>& materials,
    const std::vector<PathTraceSmokeVertex>& staticVertices,
    const std::vector<uint32_t>& staticIndexes,
    const std::vector<uint32_t>& staticTriangleClasses,
    const std::vector<uint32_t>& staticTriangleMaterialIndexes,
    const std::vector<PathTraceSmokeVertex>& dynamicVertices,
    const std::vector<uint32_t>& dynamicIndexes,
    const std::vector<uint32_t>& dynamicTriangleClasses,
    const std::vector<uint32_t>& dynamicTriangleMaterialIndexes,
    uint32_t emissiveMaterialFlag,
    uint32_t triangleClassMask,
    uint32_t skinnedSurfaceClassId,
    int maxRecords,
    RtSmokeEmissiveInventoryStats& stats);
void LogSmokeEmissiveInventoryDump(
    const std::vector<uint32_t>& materialIds,
    const std::vector<PathTraceSmokeEmissiveTriangle>& emissiveTriangles,
    const RtSmokeEmissiveInventoryStats& stats);
