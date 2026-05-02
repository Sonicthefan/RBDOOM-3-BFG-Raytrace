#pragma once

#include "PathTraceEmissiveCandidates.h"

#include <nvrhi/nvrhi.h>

#include <vector>

class idMaterial;

const int RT_SMOKE_TEXTURE_DESCRIPTOR_CAPACITY = 2048;
const int RT_SMOKE_TEXTURE_EXPERIMENTAL_ACTIVE_CAP = 2048;

const uint32_t RT_SMOKE_MATERIAL_ALPHA_TEST = 0x00000001u;
const uint32_t RT_SMOKE_MATERIAL_DIFFUSE_YCOCG = 0x00000002u;
const uint32_t RT_SMOKE_MATERIAL_ADDITIVE_DECAL = 0x00000004u;
const uint32_t RT_SMOKE_MATERIAL_EMISSIVE = 0x00000008u;
const uint32_t RT_SMOKE_MATERIAL_FILTER_DECAL = 0x00000010u;
const uint32_t RT_SMOKE_MATERIAL_FILTER_DECAL_BLACK_KEY = 0x00000020u;
const uint32_t RT_SMOKE_MATERIAL_ALPHA_FROM_DIFFUSE_LUMA = 0x00000040u;
const uint32_t RT_SMOKE_MATERIAL_FORCE_DEBUG_ALBEDO = 0x00000080u;
const uint32_t RT_SMOKE_MATERIAL_ALPHA_FROM_DIFFUSE_DARK_KEY = 0x00000100u;
const uint32_t RT_SMOKE_MATERIAL_PORTAL_WINDOW_FALLBACK = 0x00000200u;
const uint32_t RT_SMOKE_MATERIAL_OBJECT_GLASS_FALLBACK = 0x00000400u;
const uint32_t RT_SMOKE_MATERIAL_ADDITIVE_DECAL_WHITE_KEY = 0x00000800u;

struct RtSmokeMaterialTableBuild
{
    std::vector<uint32_t> materialIds;
    std::vector<PathTraceSmokeMaterial> materials;
    std::vector<uint32_t> staticMaterialIndexes;
    std::vector<uint32_t> dynamicMaterialIndexes;
    std::vector<nvrhi::TextureHandle> diffuseTextures;
    int materialsWithTextures = 0;
    int materialsWithNormalTextures = 0;
    int materialsWithSpecularTextures = 0;
    int materialsWithEmissiveTextures = 0;
    int materialsEmissive = 0;
    int materialsMissingTextures = 0;
    int materialsRejectedTextures = 0;
    int materialsRejectedAtFinalCheck = 0;
    int descriptorsReplacedWithFallback = 0;
    int materialsOverTextureSlotLimit = 0;
    int materialsWithAlphaTextures = 0;
    int materialsAlphaTested = 0;
    int materialsAdditiveDecals = 0;
    int guiTextureCandidates = 0;
    int guiTexturesAccepted = 0;
    int guiTexturesRejected = 0;
    int textureProbeRequestedIndex = -1;
    int textureProbeBoundIndex = -1;
    uint32_t textureProbeBoundMaterialId = 0;
    bool textureProbeUsedLatch = false;
};

struct RtSmokeMaterialTableCacheStats
{
    int hits = 0;
    int misses = 0;
};

int GetSmokeTextureTableRequestedLimit();
int GetSmokeTextureTableEffectiveLimit();
uint32_t HashSmokeMaterialName(const char* materialName);
uint32_t SmokeMaterialId(const idMaterial* material);
bool ValidateSmokeMaterialIndexes(const RtSmokeMaterialTableBuild& table);
bool SmokeMaterialTableIndexIsValid(const RtSmokeMaterialTableBuild& table, int tableIndex);
std::vector<int> BuildSmokeSafeMaterialIndexOrder(const RtSmokeMaterialTableBuild& table);
bool BuildSmokeMaterialTableCached(RtSmokeMaterialTableBuild& table, const std::vector<uint32_t>& staticMaterialIds, const std::vector<uint32_t>& dynamicMaterialIds, uint32_t& latchedTextureProbeMaterialId, int& latchedTextureProbeRequestedIndex, bool enableTextureProbe, uint64& signature, bool& cacheHit);
RtSmokeMaterialTableCacheStats GetSmokeMaterialTableCacheStats();
