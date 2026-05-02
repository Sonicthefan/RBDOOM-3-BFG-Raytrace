#pragma once

// Persistent RT smoke material records.
//
// This layer owns stable per-material data that can outlive a single frame's
// GPU-facing material table. DynamicMaterialState still assembles the current
// frame table and texture slots; the universe provides the cached base records.

#include "PathTraceEmissiveCandidates.h"
#include "PathTraceTextureRegistry.h"

struct RtSmokeMaterialUniverseFacts
{
    uint32_t materialId = 0;
    uint32_t materialFlags = 0;
    bool hasFallbackAlbedo = false;
    idVec4 fallbackAlbedo = idVec4(0.0f, 0.0f, 0.0f, 1.0f);
    bool alphaTested = false;
    float alphaCutoff = 0.0f;
    bool diffuseYCoCg = false;
    bool additiveDecal = false;
    bool additiveDecalWhiteKey = false;
    bool filterDecal = false;
    bool filterDecalBlackKey = false;
    bool alphaFromDiffuseLuma = false;
    bool forceFallbackAlbedo = false;
    bool alphaFromDiffuseDarkKey = false;
    bool portalWindowFallback = false;
    bool objectGlassFallback = false;
    bool emissive = false;
    idVec4 emissiveColor = idVec4(0.0f, 0.0f, 0.0f, 1.0f);
    float emissiveLuminance = 0.0f;
    bool hasDiffuseImage = false;
    bool hasSafeDiffuseTexture = false;
    bool hasAlphaImage = false;
    bool hasSafeAlphaTexture = false;
    bool hasNormalImage = false;
    bool hasSafeNormalTexture = false;
    bool hasSpecularImage = false;
    bool hasSafeSpecularTexture = false;
    bool hasEmissiveImage = false;
    bool hasSafeEmissiveTexture = false;
    bool guiTextureCandidate = false;
};

struct RtSmokePersistentMaterialRecord
{
    bool valid = false;
    uint64 signature = 0;
    PathTraceSmokeMaterial material = {};
    RtSmokeMaterialUniverseFacts facts;
    int additiveDecalContribution = 0;
};

struct RtSmokeMaterialUniverseStats
{
    int records = 0;
    int hits = 0;
    int misses = 0;
    int rebuilds = 0;
    int validationChecks = 0;
    int validationMismatches = 0;
};

const RtSmokePersistentMaterialRecord& GetSmokePersistentMaterialRecord(uint32_t materialId, const RtSmokeMaterialTextureInfo& info);
const RtSmokeMaterialUniverseFacts& GetSmokeMaterialUniverseFacts(uint32_t materialId, const RtSmokeMaterialTextureInfo& info);
RtSmokeMaterialUniverseStats GetSmokeMaterialUniverseStats();
