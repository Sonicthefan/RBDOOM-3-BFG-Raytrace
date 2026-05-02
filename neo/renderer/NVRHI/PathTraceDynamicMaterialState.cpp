#include "precompiled.h"
#pragma hdrstop

#include "PathTraceCVars.h"
#include "PathTraceDynamicMaterialState.h"
#include "PathTraceTextureRegistry.h"

#include <algorithm>


namespace {

struct RtSmokeMaterialTableCache
{
    bool valid = false;
    uint64 signature = 0;
    RtSmokeMaterialTableBuild table;
    int hits = 0;
    int misses = 0;
};

RtSmokeMaterialTableCache g_smokeMaterialTableCache;

uint64 HashSmokeMaterialCacheValue(uint64 hash, uint64 value)
{
    hash ^= value + 0x9e3779b97f4a7c15ull + (hash << 6) + (hash >> 2);
    return hash;
}

uint64 ComputeSmokeMaterialTableSignature(const std::vector<uint32_t>& staticMaterialIds, const std::vector<uint32_t>& dynamicMaterialIds, bool enableTextureProbe, uint32_t latchedTextureProbeMaterialId, int latchedTextureProbeRequestedIndex)
{
    std::vector<uint32_t> uniqueMaterialIds;
    uniqueMaterialIds.reserve(staticMaterialIds.size() + dynamicMaterialIds.size());
    uniqueMaterialIds.insert(uniqueMaterialIds.end(), staticMaterialIds.begin(), staticMaterialIds.end());
    uniqueMaterialIds.insert(uniqueMaterialIds.end(), dynamicMaterialIds.begin(), dynamicMaterialIds.end());
    std::sort(uniqueMaterialIds.begin(), uniqueMaterialIds.end());
    uniqueMaterialIds.erase(std::unique(uniqueMaterialIds.begin(), uniqueMaterialIds.end()), uniqueMaterialIds.end());

    uint64 hash = 1469598103934665603ull;
    hash = HashSmokeMaterialCacheValue(hash, static_cast<uint64>(uniqueMaterialIds.size()));
    for (uint32_t materialId : uniqueMaterialIds)
    {
        hash = HashSmokeMaterialCacheValue(hash, materialId);
    }

    hash = HashSmokeMaterialCacheValue(hash, enableTextureProbe ? 1u : 0u);
    hash = HashSmokeMaterialCacheValue(hash, static_cast<uint64>(GetSmokeTextureTableRequestedLimit()));
    hash = HashSmokeMaterialCacheValue(hash, static_cast<uint64>(Max(0, r_pathTracingTextureTableStart.GetInteger())));
    hash = HashSmokeMaterialCacheValue(hash, static_cast<uint64>(r_pathTracingTextureSampleEnable.GetInteger() != 0 ? 1 : 0));
    hash = HashSmokeMaterialCacheValue(hash, static_cast<uint64>(idMath::ClampInt(0, 2, r_pathTracingTextureSampleMethod.GetInteger())));
    hash = HashSmokeMaterialCacheValue(hash, static_cast<uint64>(r_pathTracingTextureFilter.GetInteger() != 0 ? 1 : 0));
    hash = HashSmokeMaterialCacheValue(hash, static_cast<uint64>(r_pathTracingTextureDecode.GetInteger() != 0 ? 1 : 0));
    hash = HashSmokeMaterialCacheValue(hash, static_cast<uint64>(r_pathTracingTextureBindlessEnable.GetInteger() != 0 ? 1 : 0));
    hash = HashSmokeMaterialCacheValue(hash, static_cast<uint64>(r_pathTracingTextureForceFallback.GetInteger() != 0 ? 1 : 0));
    hash = HashSmokeMaterialCacheValue(hash, static_cast<uint64>(r_pathTracingTextureProbeIndex.GetInteger() + 0x80000000u));
    hash = HashSmokeMaterialCacheValue(hash, static_cast<uint64>(r_pathTracingTextureProbeReset.GetInteger() != 0 ? 1 : 0));
    hash = HashSmokeMaterialCacheValue(hash, latchedTextureProbeMaterialId);
    hash = HashSmokeMaterialCacheValue(hash, static_cast<uint64>(latchedTextureProbeRequestedIndex + 0x80000000u));
    hash = HashSmokeMaterialCacheValue(hash, static_cast<uint64>(SmokeMaterialTextureRegistrySize()));
    return hash;
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

}

int GetSmokeTextureTableRequestedLimit()
{
    return idMath::ClampInt(0, RT_SMOKE_TEXTURE_DESCRIPTOR_CAPACITY, r_pathTracingTextureTableLimit.GetInteger());
}

int GetSmokeTextureTableEffectiveLimit()
{
    return Min(GetSmokeTextureTableRequestedLimit(), RT_SMOKE_TEXTURE_EXPERIMENTAL_ACTIVE_CAP);
}

uint32_t HashSmokeMaterialName(const char* materialName)
{
    uint32_t hash = 2166136261u;
    const char* cursor = materialName ? materialName : "<none>";
    while (*cursor)
    {
        hash ^= static_cast<uint8_t>(*cursor);
        hash *= 16777619u;
        ++cursor;
    }

    return hash != 0u ? hash : 1u;
}

uint32_t SmokeMaterialId(const idMaterial* material)
{
    return HashSmokeMaterialName(material ? material->GetName() : "<none>");
}

uint32_t AddSmokeMaterialTableEntry(RtSmokeMaterialTableBuild& table, uint32_t materialId)
{
    std::vector<uint32_t>::iterator existing = std::find(table.materialIds.begin(), table.materialIds.end(), materialId);
    if (existing != table.materialIds.end())
    {
        return static_cast<uint32_t>(existing - table.materialIds.begin());
    }

    const idVec3 color = SmokeMaterialIdToDebugColor(materialId);
    PathTraceSmokeMaterial material = {};
    material.debugAlbedo[0] = color.x;
    material.debugAlbedo[1] = color.y;
    material.debugAlbedo[2] = color.z;
    material.debugAlbedo[3] = 1.0f;
    material.emissiveColor[0] = 0.0f;
    material.emissiveColor[1] = 0.0f;
    material.emissiveColor[2] = 0.0f;
    material.emissiveColor[3] = 1.0f;
    const RtSmokeMaterialTextureInfo info = ResolveSmokeMaterialTextureInfo(materialId, static_cast<int>(table.materials.size()));
    if (info.hasFallbackAlbedo)
    {
        material.debugAlbedo[0] = info.fallbackAlbedo.x;
        material.debugAlbedo[1] = info.fallbackAlbedo.y;
        material.debugAlbedo[2] = info.fallbackAlbedo.z;
        material.debugAlbedo[3] = info.fallbackAlbedo.w;
    }
    if (info.hasAlphaTest && info.hasAlphaImage)
    {
        material.flags |= RT_SMOKE_MATERIAL_ALPHA_TEST;
        material.alphaCutoff = info.alphaCutoff;
    }
    if (info.diffuseColorFormat == CFM_YCOCG_DXT5)
    {
        material.flags |= RT_SMOKE_MATERIAL_DIFFUSE_YCOCG;
    }
    const bool useAdditiveDecalKey = info.additiveDecalWhiteKey || (info.additiveDecal && r_pathTracingAdditiveDecalKey.GetInteger() != 0);
    if (useAdditiveDecalKey)
    {
        material.flags |= RT_SMOKE_MATERIAL_ADDITIVE_DECAL;
        if (info.additiveDecalWhiteKey)
        {
            material.flags |= RT_SMOKE_MATERIAL_ADDITIVE_DECAL_WHITE_KEY;
        }
        ++table.materialsAdditiveDecals;
    }
    if (info.filterDecal)
    {
        material.flags |= RT_SMOKE_MATERIAL_FILTER_DECAL;
    }
    if (info.filterDecalBlackKey)
    {
        material.flags |= RT_SMOKE_MATERIAL_FILTER_DECAL_BLACK_KEY;
    }
    if (info.alphaFromDiffuseLuma)
    {
        material.flags |= RT_SMOKE_MATERIAL_ALPHA_FROM_DIFFUSE_LUMA;
    }
    if (info.forceFallbackAlbedo)
    {
        material.flags |= RT_SMOKE_MATERIAL_FORCE_DEBUG_ALBEDO;
    }
    if (info.alphaFromDiffuseDarkKey)
    {
        material.flags |= RT_SMOKE_MATERIAL_ALPHA_FROM_DIFFUSE_DARK_KEY;
    }
    if (info.portalWindowFallback)
    {
        material.flags |= RT_SMOKE_MATERIAL_PORTAL_WINDOW_FALLBACK;
    }
    if (info.objectGlassFallback)
    {
        material.flags |= RT_SMOKE_MATERIAL_OBJECT_GLASS_FALLBACK;
    }
    if (info.emissive && (info.hasSafeEmissiveTexture || !info.hasEmissiveImage))
    {
        material.flags |= RT_SMOKE_MATERIAL_EMISSIVE;
        material.emissiveColor[0] = info.emissiveColor.x;
        material.emissiveColor[1] = info.emissiveColor.y;
        material.emissiveColor[2] = info.emissiveColor.z;
        material.emissiveColor[3] = info.emissiveColor.w;
    }
    table.materialIds.push_back(materialId);
    table.materials.push_back(material);
    return static_cast<uint32_t>(table.materials.size() - 1);
}

bool ValidateSmokeMaterialIndexes(const RtSmokeMaterialTableBuild& table)
{
    const uint32_t materialCount = static_cast<uint32_t>(table.materials.size());
    for (uint32_t materialIndex : table.staticMaterialIndexes)
    {
        if (materialIndex >= materialCount)
        {
            return false;
        }
    }

    for (uint32_t materialIndex : table.dynamicMaterialIndexes)
    {
        if (materialIndex >= materialCount)
        {
            return false;
        }
    }

    return table.materialIds.size() == table.materials.size();
}

bool SmokeMaterialTableIndexIsValid(const RtSmokeMaterialTableBuild& table, int tableIndex)
{
    return tableIndex >= 0 &&
        tableIndex < static_cast<int>(table.materialIds.size()) &&
        tableIndex < static_cast<int>(table.materials.size());
}

std::vector<int> BuildSmokeSafeMaterialIndexOrder(const RtSmokeMaterialTableBuild& table)
{
    const int materialTableCount = Min(static_cast<int>(table.materialIds.size()), static_cast<int>(table.materials.size()));

    std::vector<int> safeMaterialIndexes;
    safeMaterialIndexes.reserve(materialTableCount);

    for (int tableIndex = 0; tableIndex < materialTableCount; ++tableIndex)
    {
        const RtSmokeMaterialTextureInfo info = ResolveSmokeMaterialTextureInfo(table.materialIds[tableIndex], tableIndex);
        if (info.diffuseImage && info.hasTextureHandle && info.hasSafeTexture)
        {
            safeMaterialIndexes.push_back(tableIndex);
            continue;
        }

        if (info.alphaImage && info.hasAlphaTextureHandle && info.hasSafeAlphaTexture)
        {
            safeMaterialIndexes.push_back(tableIndex);
            continue;
        }

        if (info.normalImage && info.hasNormalTextureHandle && info.hasSafeNormalTexture)
        {
            safeMaterialIndexes.push_back(tableIndex);
            continue;
        }

        if (info.specularImage && info.hasSpecularTextureHandle && info.hasSafeSpecularTexture)
        {
            safeMaterialIndexes.push_back(tableIndex);
            continue;
        }

        if (info.emissiveImage && info.hasEmissiveTextureHandle && info.hasSafeEmissiveTexture)
        {
            safeMaterialIndexes.push_back(tableIndex);
        }
    }

    std::stable_sort(safeMaterialIndexes.begin(), safeMaterialIndexes.end(),
        [&table](int lhs, int rhs)
        {
            if (!SmokeMaterialTableIndexIsValid(table, lhs) || !SmokeMaterialTableIndexIsValid(table, rhs))
            {
                return lhs < rhs;
            }

            const RtSmokeMaterialTextureInfo leftInfo = ResolveSmokeMaterialTextureInfo(table.materialIds[lhs], lhs);
            const RtSmokeMaterialTextureInfo rightInfo = ResolveSmokeMaterialTextureInfo(table.materialIds[rhs], rhs);

            const idStr& leftName = SmokeBestSafeTextureName(leftInfo);
            const idStr& rightName = SmokeBestSafeTextureName(rightInfo);
            const int imageCompare = leftName.Icmp(rightName);
            if (imageCompare != 0)
            {
                return imageCompare < 0;
            }

            const int materialCompare = leftInfo.materialName.Icmp(rightInfo.materialName);
            if (materialCompare != 0)
            {
                return materialCompare < 0;
            }

            return lhs < rhs;
        });

    return safeMaterialIndexes;
}

uint32_t AddSmokeMaterialTextureSlot(RtSmokeMaterialTableBuild& table, nvrhi::TextureHandle texture, int textureTableLimit, int textureTableStart, int& skippedUniqueTextures, std::vector<nvrhi::TextureHandle>& skippedTextures)
{
    if (!texture || !IsSmokeTextureHandleSafeForDescriptor(texture))
    {
        ++table.materialsRejectedAtFinalCheck;
        return UINT32_MAX;
    }

    for (int textureIndex = 0; textureIndex < static_cast<int>(table.diffuseTextures.size()); ++textureIndex)
    {
        if (table.diffuseTextures[textureIndex].Get() == texture.Get())
        {
            return static_cast<uint32_t>(textureIndex);
        }
    }

    for (int skippedIndex = 0; skippedIndex < static_cast<int>(skippedTextures.size()); ++skippedIndex)
    {
        if (skippedTextures[skippedIndex].Get() == texture.Get())
        {
            return UINT32_MAX;
        }
    }

    if (skippedUniqueTextures < textureTableStart)
    {
        skippedTextures.push_back(texture);
        ++skippedUniqueTextures;
        ++table.materialsOverTextureSlotLimit;
        return UINT32_MAX;
    }

    if (static_cast<int>(table.diffuseTextures.size()) >= textureTableLimit)
    {
        ++table.materialsOverTextureSlotLimit;
        return UINT32_MAX;
    }

    const uint32_t descriptorIndex = static_cast<uint32_t>(table.diffuseTextures.size());
    table.diffuseTextures.push_back(texture);
    return descriptorIndex;
}

void AccumulateSmokeGuiTextureDiagnostic(RtSmokeMaterialTableBuild& table, idImage* image, bool safe)
{
    if (!image || !IsSmokeImageNameGuiLike(image->GetName()))
    {
        return;
    }

    ++table.guiTextureCandidates;
    if (safe)
    {
        ++table.guiTexturesAccepted;
    }
    else
    {
        ++table.guiTexturesRejected;
    }
}

void PopulateSmokeMaterialTextureSlots(RtSmokeMaterialTableBuild& table, uint32_t& latchedMaterialId, int& latchedRequestedIndex, bool enableTextureProbe)
{
    const int materialTableCount = Min(static_cast<int>(table.materialIds.size()), static_cast<int>(table.materials.size()));

    table.diffuseTextures.clear();
    table.materialsWithTextures = 0;
    table.materialsWithNormalTextures = 0;
    table.materialsWithSpecularTextures = 0;
    table.materialsWithEmissiveTextures = 0;
    table.materialsEmissive = 0;
    table.materialsMissingTextures = 0;
    table.materialsRejectedTextures = 0;
    table.materialsRejectedAtFinalCheck = 0;
    table.descriptorsReplacedWithFallback = 0;
    table.materialsOverTextureSlotLimit = 0;
    table.materialsWithAlphaTextures = 0;
    table.materialsAlphaTested = 0;
    table.guiTextureCandidates = 0;
    table.guiTexturesAccepted = 0;
    table.guiTexturesRejected = 0;
    table.textureProbeRequestedIndex = r_pathTracingTextureProbeIndex.GetInteger();
    table.textureProbeBoundIndex = -1;
    table.textureProbeBoundMaterialId = 0;
    table.textureProbeUsedLatch = false;

    for (int tableIndex = 0; tableIndex < static_cast<int>(table.materials.size()); ++tableIndex)
    {
        table.materials[tableIndex].diffuseTextureIndex = UINT32_MAX;
        table.materials[tableIndex].alphaTextureIndex = UINT32_MAX;
        table.materials[tableIndex].normalTextureIndex = UINT32_MAX;
        table.materials[tableIndex].specularTextureIndex = UINT32_MAX;
        table.materials[tableIndex].emissiveTextureIndex = UINT32_MAX;
        table.materials[tableIndex].textureWidth = 1;
        table.materials[tableIndex].textureHeight = 1;
        table.materials[tableIndex].alphaTextureWidth = 1;
        table.materials[tableIndex].alphaTextureHeight = 1;
        table.materials[tableIndex].normalTextureWidth = 1;
        table.materials[tableIndex].normalTextureHeight = 1;
        table.materials[tableIndex].specularTextureWidth = 1;
        table.materials[tableIndex].specularTextureHeight = 1;
        table.materials[tableIndex].emissiveTextureWidth = 1;
        table.materials[tableIndex].emissiveTextureHeight = 1;
    }

    if (!enableTextureProbe)
    {
        return;
    }

    const int textureTableLimit = GetSmokeTextureTableEffectiveLimit();
    const int textureTableStart = Max(0, r_pathTracingTextureTableStart.GetInteger());

    if (r_pathTracingTextureProbeReset.GetInteger() != 0 || latchedRequestedIndex != table.textureProbeRequestedIndex)
    {
        latchedMaterialId = 0;
        latchedRequestedIndex = table.textureProbeRequestedIndex;
        r_pathTracingTextureProbeReset.SetInteger(0);
    }

    for (int tableIndex = 0; tableIndex < materialTableCount; ++tableIndex)
    {
        const RtSmokeMaterialTextureInfo info = ResolveSmokeMaterialTextureInfo(table.materialIds[tableIndex], tableIndex);
        if (info.hasAlphaTest)
        {
            ++table.materialsAlphaTested;
        }
        if (info.emissive)
        {
            ++table.materialsEmissive;
        }

        AccumulateSmokeGuiTextureDiagnostic(table, info.diffuseImage, info.hasSafeTexture);
        AccumulateSmokeGuiTextureDiagnostic(table, info.alphaImage, info.hasSafeAlphaTexture);
        AccumulateSmokeGuiTextureDiagnostic(table, info.normalImage, info.hasSafeNormalTexture);
        AccumulateSmokeGuiTextureDiagnostic(table, info.specularImage, info.hasSafeSpecularTexture);
        AccumulateSmokeGuiTextureDiagnostic(table, info.emissiveImage, info.hasSafeEmissiveTexture);

        if (!info.diffuseImage || !info.hasTextureHandle)
        {
            ++table.materialsMissingTextures;
            continue;
        }
        if (!info.hasSafeTexture)
        {
            ++table.materialsRejectedTextures;
        }

    }

    const std::vector<int> safeMaterialIndexes = BuildSmokeSafeMaterialIndexOrder(table);

    if (textureTableLimit <= 0)
    {
        table.materialsOverTextureSlotLimit = static_cast<int>(safeMaterialIndexes.size());
        if (!safeMaterialIndexes.empty())
        {
            int selectedMaterialIndex = -1;
            if (table.textureProbeRequestedIndex >= 0 &&
                std::find(safeMaterialIndexes.begin(), safeMaterialIndexes.end(), table.textureProbeRequestedIndex) != safeMaterialIndexes.end())
            {
                selectedMaterialIndex = table.textureProbeRequestedIndex;
            }
            else
            {
                selectedMaterialIndex = safeMaterialIndexes.front();
            }

            table.textureProbeBoundIndex = selectedMaterialIndex;
            table.textureProbeBoundMaterialId = table.materialIds[selectedMaterialIndex];
        }
        return;
    }

    int skippedUniqueTextures = 0;
    std::vector<nvrhi::TextureHandle> skippedTextures;
    for (int safeIndex : safeMaterialIndexes)
    {
        if (!SmokeMaterialTableIndexIsValid(table, safeIndex))
        {
            ++table.materialsRejectedAtFinalCheck;
            continue;
        }

        const RtSmokeMaterialTextureInfo info = ResolveSmokeMaterialTextureInfo(table.materialIds[safeIndex], safeIndex);
        const nvrhi::TextureHandle texture = info.diffuseImage ? info.diffuseImage->GetTextureHandle() : nullptr;
        if (texture && IsSmokeDiffuseImageSafeForRayTracing(info.diffuseImage))
        {
            const uint32_t descriptorIndex = AddSmokeMaterialTextureSlot(table, texture, textureTableLimit, textureTableStart, skippedUniqueTextures, skippedTextures);
            if (descriptorIndex != UINT32_MAX)
            {
                table.materials[safeIndex].diffuseTextureIndex = descriptorIndex;
                const nvrhi::TextureDesc& textureDesc = texture->getDesc();
                table.materials[safeIndex].textureWidth = Max(1u, textureDesc.width);
                table.materials[safeIndex].textureHeight = Max(1u, textureDesc.height);
                ++table.materialsWithTextures;
            }
        }

        const nvrhi::TextureHandle alphaTexture = info.alphaImage ? info.alphaImage->GetTextureHandle() : nullptr;
        if (info.hasAlphaTest && alphaTexture && IsSmokeDiffuseImageSafeForRayTracing(info.alphaImage))
        {
            const uint32_t alphaDescriptorIndex = AddSmokeMaterialTextureSlot(table, alphaTexture, textureTableLimit, textureTableStart, skippedUniqueTextures, skippedTextures);
            if (alphaDescriptorIndex != UINT32_MAX)
            {
                table.materials[safeIndex].alphaTextureIndex = alphaDescriptorIndex;
                const nvrhi::TextureDesc& alphaTextureDesc = alphaTexture->getDesc();
                table.materials[safeIndex].alphaTextureWidth = Max(1u, alphaTextureDesc.width);
                table.materials[safeIndex].alphaTextureHeight = Max(1u, alphaTextureDesc.height);
                ++table.materialsWithAlphaTextures;
            }
        }

        const nvrhi::TextureHandle normalTexture = info.normalImage ? info.normalImage->GetTextureHandle() : nullptr;
        if (normalTexture && IsSmokeDiffuseImageSafeForRayTracing(info.normalImage))
        {
            const uint32_t normalDescriptorIndex = AddSmokeMaterialTextureSlot(table, normalTexture, textureTableLimit, textureTableStart, skippedUniqueTextures, skippedTextures);
            if (normalDescriptorIndex != UINT32_MAX)
            {
                table.materials[safeIndex].normalTextureIndex = normalDescriptorIndex;
                const nvrhi::TextureDesc& normalTextureDesc = normalTexture->getDesc();
                table.materials[safeIndex].normalTextureWidth = Max(1u, normalTextureDesc.width);
                table.materials[safeIndex].normalTextureHeight = Max(1u, normalTextureDesc.height);
                ++table.materialsWithNormalTextures;
            }
        }

        const nvrhi::TextureHandle specularTexture = info.specularImage ? info.specularImage->GetTextureHandle() : nullptr;
        if (specularTexture && IsSmokeDiffuseImageSafeForRayTracing(info.specularImage))
        {
            const uint32_t specularDescriptorIndex = AddSmokeMaterialTextureSlot(table, specularTexture, textureTableLimit, textureTableStart, skippedUniqueTextures, skippedTextures);
            if (specularDescriptorIndex != UINT32_MAX)
            {
                table.materials[safeIndex].specularTextureIndex = specularDescriptorIndex;
                const nvrhi::TextureDesc& specularTextureDesc = specularTexture->getDesc();
                table.materials[safeIndex].specularTextureWidth = Max(1u, specularTextureDesc.width);
                table.materials[safeIndex].specularTextureHeight = Max(1u, specularTextureDesc.height);
                ++table.materialsWithSpecularTextures;
            }
        }

        const nvrhi::TextureHandle emissiveTexture = info.emissiveImage ? info.emissiveImage->GetTextureHandle() : nullptr;
        if (emissiveTexture && IsSmokeDiffuseImageSafeForRayTracing(info.emissiveImage))
        {
            const uint32_t emissiveDescriptorIndex = AddSmokeMaterialTextureSlot(table, emissiveTexture, textureTableLimit, textureTableStart, skippedUniqueTextures, skippedTextures);
            if (emissiveDescriptorIndex != UINT32_MAX)
            {
                table.materials[safeIndex].emissiveTextureIndex = emissiveDescriptorIndex;
                const nvrhi::TextureDesc& emissiveTextureDesc = emissiveTexture->getDesc();
                table.materials[safeIndex].emissiveTextureWidth = Max(1u, emissiveTextureDesc.width);
                table.materials[safeIndex].emissiveTextureHeight = Max(1u, emissiveTextureDesc.height);
                ++table.materialsWithEmissiveTextures;
            }
        }
        if (info.emissiveImage && info.emissiveImage == info.diffuseImage && table.materials[safeIndex].emissiveTextureIndex == UINT32_MAX && table.materials[safeIndex].diffuseTextureIndex != UINT32_MAX)
        {
            table.materials[safeIndex].emissiveTextureIndex = table.materials[safeIndex].diffuseTextureIndex;
            table.materials[safeIndex].emissiveTextureWidth = table.materials[safeIndex].textureWidth;
            table.materials[safeIndex].emissiveTextureHeight = table.materials[safeIndex].textureHeight;
            ++table.materialsWithEmissiveTextures;
        }

        if (info.hasEmissiveImage && table.materials[safeIndex].emissiveTextureIndex == UINT32_MAX)
        {
            table.materials[safeIndex].flags &= ~RT_SMOKE_MATERIAL_EMISSIVE;
            table.materials[safeIndex].emissiveColor[0] = 0.0f;
            table.materials[safeIndex].emissiveColor[1] = 0.0f;
            table.materials[safeIndex].emissiveColor[2] = 0.0f;
            table.materials[safeIndex].emissiveColor[3] = 1.0f;
        }
    }

    int selectedMaterialIndex = -1;
    if (latchedMaterialId != 0)
    {
        std::vector<uint32_t>::const_iterator latchedMaterial = std::find(table.materialIds.begin(), table.materialIds.end(), latchedMaterialId);
        if (latchedMaterial != table.materialIds.end())
        {
            const int latchedIndex = static_cast<int>(latchedMaterial - table.materialIds.begin());
            if (std::find(safeMaterialIndexes.begin(), safeMaterialIndexes.end(), latchedIndex) != safeMaterialIndexes.end())
            {
                selectedMaterialIndex = latchedIndex;
                table.textureProbeUsedLatch = true;
            }
        }
    }

    if (selectedMaterialIndex < 0 && table.textureProbeRequestedIndex >= 0)
    {
        if (std::find(safeMaterialIndexes.begin(), safeMaterialIndexes.end(), table.textureProbeRequestedIndex) != safeMaterialIndexes.end())
        {
            selectedMaterialIndex = table.textureProbeRequestedIndex;
        }
    }

    if (selectedMaterialIndex < 0 && !safeMaterialIndexes.empty())
    {
        selectedMaterialIndex = safeMaterialIndexes.front();
    }

    if (selectedMaterialIndex < 0)
    {
        return;
    }

    if (!SmokeMaterialTableIndexIsValid(table, selectedMaterialIndex))
    {
        return;
    }
    const RtSmokeMaterialTextureInfo selectedInfo = ResolveSmokeMaterialTextureInfo(table.materialIds[selectedMaterialIndex], selectedMaterialIndex);
    if (!selectedInfo.diffuseImage || !selectedInfo.hasSafeTexture)
    {
        return;
    }

    table.textureProbeBoundIndex = selectedMaterialIndex;
    table.textureProbeBoundMaterialId = table.materialIds[selectedMaterialIndex];
    if (latchedMaterialId != table.textureProbeBoundMaterialId)
    {
        latchedMaterialId = table.textureProbeBoundMaterialId;
    }
}

void BuildSmokeMaterialTable(RtSmokeMaterialTableBuild& table, const std::vector<uint32_t>& staticMaterialIds, const std::vector<uint32_t>& dynamicMaterialIds, uint32_t& latchedTextureProbeMaterialId, int& latchedTextureProbeRequestedIndex, bool enableTextureProbe)
{
    table = RtSmokeMaterialTableBuild();
    table.materialIds.reserve(staticMaterialIds.size() + dynamicMaterialIds.size());
    table.materials.reserve(staticMaterialIds.size() + dynamicMaterialIds.size());
    table.staticMaterialIndexes.reserve(staticMaterialIds.size());
    table.dynamicMaterialIndexes.reserve(dynamicMaterialIds.size());

    for (uint32_t materialId : staticMaterialIds)
    {
        table.staticMaterialIndexes.push_back(AddSmokeMaterialTableEntry(table, materialId));
    }

    for (uint32_t materialId : dynamicMaterialIds)
    {
        table.dynamicMaterialIndexes.push_back(AddSmokeMaterialTableEntry(table, materialId));
    }

    PopulateSmokeMaterialTextureSlots(table, latchedTextureProbeMaterialId, latchedTextureProbeRequestedIndex, enableTextureProbe);
}

void RebuildSmokeMaterialIndexesFromCachedTable(RtSmokeMaterialTableBuild& table, const std::vector<uint32_t>& staticMaterialIds, const std::vector<uint32_t>& dynamicMaterialIds)
{
    table.staticMaterialIndexes.clear();
    table.dynamicMaterialIndexes.clear();
    table.staticMaterialIndexes.reserve(staticMaterialIds.size());
    table.dynamicMaterialIndexes.reserve(dynamicMaterialIds.size());

    for (uint32_t materialId : staticMaterialIds)
    {
        const std::vector<uint32_t>::iterator existing = std::lower_bound(table.materialIds.begin(), table.materialIds.end(), materialId);
        table.staticMaterialIndexes.push_back(existing != table.materialIds.end() && *existing == materialId ? static_cast<uint32_t>(existing - table.materialIds.begin()) : UINT32_MAX);
    }

    for (uint32_t materialId : dynamicMaterialIds)
    {
        const std::vector<uint32_t>::iterator existing = std::lower_bound(table.materialIds.begin(), table.materialIds.end(), materialId);
        table.dynamicMaterialIndexes.push_back(existing != table.materialIds.end() && *existing == materialId ? static_cast<uint32_t>(existing - table.materialIds.begin()) : UINT32_MAX);
    }
}

bool BuildSmokeMaterialTableCached(RtSmokeMaterialTableBuild& table, const std::vector<uint32_t>& staticMaterialIds, const std::vector<uint32_t>& dynamicMaterialIds, uint32_t& latchedTextureProbeMaterialId, int& latchedTextureProbeRequestedIndex, bool enableTextureProbe, uint64& signature, bool& cacheHit)
{
    // Disabled while validating a frame-local material-index mapping that remains
    // compatible with cached static BLAS metadata.
    signature = 0;
    cacheHit = false;
    BuildSmokeMaterialTable(table, staticMaterialIds, dynamicMaterialIds, latchedTextureProbeMaterialId, latchedTextureProbeRequestedIndex, enableTextureProbe);
    return false;
}

RtSmokeMaterialTableCacheStats GetSmokeMaterialTableCacheStats()
{
    RtSmokeMaterialTableCacheStats stats;
    stats.hits = g_smokeMaterialTableCache.hits;
    stats.misses = g_smokeMaterialTableCache.misses;
    return stats;
}
