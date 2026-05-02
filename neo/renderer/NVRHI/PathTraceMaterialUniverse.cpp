#include "precompiled.h"
#pragma hdrstop

// Persistent material-record cache for the RT smoke/path tracing path.
//
// The records here intentionally mirror the stable part of the shader-facing
// material table. Per-frame texture descriptor indexes are still assigned by
// PathTraceDynamicMaterialState after the current visible texture set is known.

#include "PathTraceCVars.h"
#include "PathTraceDynamicMaterialState.h"
#include "PathTraceMaterialUniverse.h"

#include <cstring>
#include <unordered_map>

namespace {

std::unordered_map<uint32_t, RtSmokePersistentMaterialRecord> g_smokePersistentMaterialRecords;
RtSmokeMaterialUniverseStats g_smokeMaterialUniverseStats;
int g_smokeMaterialUniverseValidationLogs = 0;

uint64 HashSmokeMaterialUniverseValue(uint64 hash, uint64 value)
{
    hash ^= value + 0x9e3779b97f4a7c15ull + (hash << 6) + (hash >> 2);
    return hash;
}

uint64 HashSmokeMaterialUniverseFloat(uint64 hash, float value)
{
    uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    return HashSmokeMaterialUniverseValue(hash, bits);
}

uint64 HashSmokeMaterialUniverseString(uint64 hash, const idStr& value)
{
    const char* cursor = value.c_str();
    while (*cursor)
    {
        hash = HashSmokeMaterialUniverseValue(hash, static_cast<uint8_t>(*cursor));
        ++cursor;
    }
    return HashSmokeMaterialUniverseValue(hash, 0xffu);
}

idVec3 SmokeMaterialIdToDebugColor(uint32_t materialId)
{
    uint32_t hash = materialId;
    hash ^= hash >> 16;
    hash *= 2246822519u;
    hash ^= hash >> 13;
    hash *= 3266489917u;
    hash ^= hash >> 16;

    return idVec3(
        0.15f + static_cast<float>((hash >> 0) & 255u) * (0.85f / 255.0f),
        0.15f + static_cast<float>((hash >> 8) & 255u) * (0.85f / 255.0f),
        0.15f + static_cast<float>((hash >> 16) & 255u) * (0.85f / 255.0f));
}

uint64 ComputeSmokePersistentMaterialSignature(uint32_t materialId, const RtSmokeMaterialTextureInfo& info)
{
    uint64 hash = 1469598103934665603ull;
    hash = HashSmokeMaterialUniverseValue(hash, materialId);
    hash = HashSmokeMaterialUniverseString(hash, info.materialName);
    hash = HashSmokeMaterialUniverseValue(hash, info.hasFallbackAlbedo ? 1u : 0u);
    hash = HashSmokeMaterialUniverseFloat(hash, info.fallbackAlbedo.x);
    hash = HashSmokeMaterialUniverseFloat(hash, info.fallbackAlbedo.y);
    hash = HashSmokeMaterialUniverseFloat(hash, info.fallbackAlbedo.z);
    hash = HashSmokeMaterialUniverseFloat(hash, info.fallbackAlbedo.w);
    hash = HashSmokeMaterialUniverseValue(hash, info.hasAlphaTest ? 1u : 0u);
    hash = HashSmokeMaterialUniverseValue(hash, info.hasAlphaImage ? 1u : 0u);
    hash = HashSmokeMaterialUniverseFloat(hash, info.alphaCutoff);
    hash = HashSmokeMaterialUniverseValue(hash, static_cast<uint64>(info.diffuseColorFormat));
    hash = HashSmokeMaterialUniverseValue(hash, info.additiveDecal ? 1u : 0u);
    hash = HashSmokeMaterialUniverseValue(hash, info.additiveDecalWhiteKey ? 1u : 0u);
    hash = HashSmokeMaterialUniverseValue(hash, r_pathTracingAdditiveDecalKey.GetInteger() != 0 ? 1u : 0u);
    hash = HashSmokeMaterialUniverseValue(hash, info.filterDecal ? 1u : 0u);
    hash = HashSmokeMaterialUniverseValue(hash, info.filterDecalBlackKey ? 1u : 0u);
    hash = HashSmokeMaterialUniverseValue(hash, info.alphaFromDiffuseLuma ? 1u : 0u);
    hash = HashSmokeMaterialUniverseValue(hash, info.forceFallbackAlbedo ? 1u : 0u);
    hash = HashSmokeMaterialUniverseValue(hash, info.alphaFromDiffuseDarkKey ? 1u : 0u);
    hash = HashSmokeMaterialUniverseValue(hash, info.portalWindowFallback ? 1u : 0u);
    hash = HashSmokeMaterialUniverseValue(hash, info.objectGlassFallback ? 1u : 0u);
    hash = HashSmokeMaterialUniverseValue(hash, info.emissive ? 1u : 0u);
    hash = HashSmokeMaterialUniverseValue(hash, info.hasEmissiveImage ? 1u : 0u);
    hash = HashSmokeMaterialUniverseValue(hash, info.hasSafeEmissiveTexture ? 1u : 0u);
    hash = HashSmokeMaterialUniverseFloat(hash, info.emissiveColor.x);
    hash = HashSmokeMaterialUniverseFloat(hash, info.emissiveColor.y);
    hash = HashSmokeMaterialUniverseFloat(hash, info.emissiveColor.z);
    hash = HashSmokeMaterialUniverseFloat(hash, info.emissiveColor.w);
    return hash;
}

RtSmokePersistentMaterialRecord BuildSmokePersistentMaterialRecord(uint32_t materialId, const RtSmokeMaterialTextureInfo& info, uint64 signature)
{
    RtSmokePersistentMaterialRecord record;
    record.valid = true;
    record.signature = signature;

    const idVec3 color = SmokeMaterialIdToDebugColor(materialId);
    record.material.debugAlbedo[0] = color.x;
    record.material.debugAlbedo[1] = color.y;
    record.material.debugAlbedo[2] = color.z;
    record.material.debugAlbedo[3] = 1.0f;
    record.material.emissiveColor[0] = 0.0f;
    record.material.emissiveColor[1] = 0.0f;
    record.material.emissiveColor[2] = 0.0f;
    record.material.emissiveColor[3] = 1.0f;

    if (info.hasFallbackAlbedo)
    {
        record.material.debugAlbedo[0] = info.fallbackAlbedo.x;
        record.material.debugAlbedo[1] = info.fallbackAlbedo.y;
        record.material.debugAlbedo[2] = info.fallbackAlbedo.z;
        record.material.debugAlbedo[3] = info.fallbackAlbedo.w;
    }
    if (info.hasAlphaTest && info.hasAlphaImage)
    {
        record.material.flags |= RT_SMOKE_MATERIAL_ALPHA_TEST;
        record.material.alphaCutoff = info.alphaCutoff;
    }
    if (info.diffuseColorFormat == CFM_YCOCG_DXT5)
    {
        record.material.flags |= RT_SMOKE_MATERIAL_DIFFUSE_YCOCG;
    }
    const bool useAdditiveDecalKey = info.additiveDecalWhiteKey || (info.additiveDecal && r_pathTracingAdditiveDecalKey.GetInteger() != 0);
    if (useAdditiveDecalKey)
    {
        record.material.flags |= RT_SMOKE_MATERIAL_ADDITIVE_DECAL;
        if (info.additiveDecalWhiteKey)
        {
            record.material.flags |= RT_SMOKE_MATERIAL_ADDITIVE_DECAL_WHITE_KEY;
        }
        record.additiveDecalContribution = 1;
    }
    if (info.filterDecal)
    {
        record.material.flags |= RT_SMOKE_MATERIAL_FILTER_DECAL;
    }
    if (info.filterDecalBlackKey)
    {
        record.material.flags |= RT_SMOKE_MATERIAL_FILTER_DECAL_BLACK_KEY;
    }
    if (info.alphaFromDiffuseLuma)
    {
        record.material.flags |= RT_SMOKE_MATERIAL_ALPHA_FROM_DIFFUSE_LUMA;
    }
    if (info.forceFallbackAlbedo)
    {
        record.material.flags |= RT_SMOKE_MATERIAL_FORCE_DEBUG_ALBEDO;
    }
    if (info.alphaFromDiffuseDarkKey)
    {
        record.material.flags |= RT_SMOKE_MATERIAL_ALPHA_FROM_DIFFUSE_DARK_KEY;
    }
    if (info.portalWindowFallback)
    {
        record.material.flags |= RT_SMOKE_MATERIAL_PORTAL_WINDOW_FALLBACK;
    }
    if (info.objectGlassFallback)
    {
        record.material.flags |= RT_SMOKE_MATERIAL_OBJECT_GLASS_FALLBACK;
    }
    if (info.emissive && (info.hasSafeEmissiveTexture || !info.hasEmissiveImage))
    {
        record.material.flags |= RT_SMOKE_MATERIAL_EMISSIVE;
        record.material.emissiveColor[0] = info.emissiveColor.x;
        record.material.emissiveColor[1] = info.emissiveColor.y;
        record.material.emissiveColor[2] = info.emissiveColor.z;
        record.material.emissiveColor[3] = info.emissiveColor.w;
    }

    return record;
}

bool SmokePersistentMaterialRecordsEqual(const RtSmokePersistentMaterialRecord& lhs, const RtSmokePersistentMaterialRecord& rhs)
{
    return lhs.additiveDecalContribution == rhs.additiveDecalContribution &&
        std::memcmp(&lhs.material, &rhs.material, sizeof(lhs.material)) == 0;
}

}

const RtSmokePersistentMaterialRecord& GetSmokePersistentMaterialRecord(uint32_t materialId, const RtSmokeMaterialTextureInfo& info)
{
    const uint64 signature = ComputeSmokePersistentMaterialSignature(materialId, info);
    RtSmokePersistentMaterialRecord& record = g_smokePersistentMaterialRecords[materialId];
    if (!record.valid)
    {
        ++g_smokeMaterialUniverseStats.misses;
        record = BuildSmokePersistentMaterialRecord(materialId, info, signature);
    }
    else if (record.signature != signature)
    {
        ++g_smokeMaterialUniverseStats.rebuilds;
        record = BuildSmokePersistentMaterialRecord(materialId, info, signature);
    }
    else
    {
        ++g_smokeMaterialUniverseStats.hits;
    }

    if (r_pathTracingMaterialUniverseValidate.GetInteger() != 0)
    {
        ++g_smokeMaterialUniverseStats.validationChecks;
        const RtSmokePersistentMaterialRecord validationRecord = BuildSmokePersistentMaterialRecord(materialId, info, signature);
        if (!SmokePersistentMaterialRecordsEqual(record, validationRecord))
        {
            ++g_smokeMaterialUniverseStats.validationMismatches;
            if (g_smokeMaterialUniverseValidationLogs < 8)
            {
                common->Printf("PathTracePrimaryPass: RT smoke material universe validation mismatch materialId=%u material='%s' signature=%llu\n",
                    materialId,
                    info.materialName.c_str(),
                    static_cast<unsigned long long>(signature));
                ++g_smokeMaterialUniverseValidationLogs;
            }
            record = validationRecord;
        }
    }

    return record;
}

RtSmokeMaterialUniverseStats GetSmokeMaterialUniverseStats()
{
    RtSmokeMaterialUniverseStats stats = g_smokeMaterialUniverseStats;
    stats.records = static_cast<int>(g_smokePersistentMaterialRecords.size());
    return stats;
}
