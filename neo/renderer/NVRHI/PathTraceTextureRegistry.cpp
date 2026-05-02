#include "precompiled.h"
#pragma hdrstop

#include "PathTraceTextureRegistry.h"
#include "../Image.h"

#include <unordered_map>

extern idCVar r_pathTracingAllowGuiTextures;

namespace {

std::vector<RtSmokeMaterialTextureInfo> g_smokeMaterialTextureRegistry;
std::unordered_map<uint32_t, int> g_smokeMaterialTextureRegistryLookup;

}

bool IsSmokeDiffuseTextureSafeForRayTracing(nvrhi::ITexture* texture)
{
    if (!texture)
    {
        return false;
    }

    const nvrhi::TextureDesc& desc = texture->getDesc();
    if (!desc.isShaderResource || desc.isRenderTarget || desc.isUAV)
    {
        return false;
    }
    if (desc.dimension != nvrhi::TextureDimension::Texture2D || desc.sampleCount != 1)
    {
        return false;
    }

    switch (desc.format)
    {
        case nvrhi::Format::UNKNOWN:
        case nvrhi::Format::R8_UINT:
        case nvrhi::Format::R8_SINT:
        case nvrhi::Format::RG8_UINT:
        case nvrhi::Format::RG8_SINT:
        case nvrhi::Format::R16_UINT:
        case nvrhi::Format::R16_SINT:
        case nvrhi::Format::R32_UINT:
        case nvrhi::Format::R32_SINT:
        case nvrhi::Format::RG16_UINT:
        case nvrhi::Format::RG16_SINT:
        case nvrhi::Format::RG32_UINT:
        case nvrhi::Format::RG32_SINT:
        case nvrhi::Format::RGB32_UINT:
        case nvrhi::Format::RGB32_SINT:
        case nvrhi::Format::RGBA8_UINT:
        case nvrhi::Format::RGBA8_SINT:
        case nvrhi::Format::RGBA16_UINT:
        case nvrhi::Format::RGBA16_SINT:
        case nvrhi::Format::RGBA32_UINT:
        case nvrhi::Format::RGBA32_SINT:
        case nvrhi::Format::D16:
        case nvrhi::Format::D24S8:
        case nvrhi::Format::X24G8_UINT:
        case nvrhi::Format::D32:
        case nvrhi::Format::D32S8:
        case nvrhi::Format::X32G8_UINT:
            return false;
        default:
            return true;
    }
}

bool IsSmokeImageNameSafeForRayTracing(const char* imageName)
{
    if (!imageName || !imageName[0])
    {
        return false;
    }

    idStr name = imageName;
    name.BackSlashesToSlashes();

    // Runtime GUI/cinematic/scratch images can be render-target backed or replaced
    // while in-world terminals redraw. Keep mode 8 on stable material textures only.
    if (name[0] == '_' ||
        name.Icmpn("guis/", 5) == 0 ||
        name.Icmpn("gui/", 4) == 0 ||
        name.Icmpn("video/", 6) == 0 ||
        name.Icmpn("videos/", 7) == 0 ||
        name.Icmpn("cinematics/", 11) == 0 ||
        name.Icmpn("generated/", 10) == 0 ||
        name.Find("cinematic", false) >= 0 ||
        name.Find("scratch", false) >= 0 ||
        name.Find("render", false) >= 0)
    {
        return false;
    }

    return true;
}

bool IsSmokeImageNameGuiLike(const char* imageName)
{
    if (!imageName || !imageName[0])
    {
        return false;
    }

    idStr name = imageName;
    name.BackSlashesToSlashes();

    return name[0] == '_' ||
        name.Icmpn("guis/", 5) == 0 ||
        name.Icmpn("gui/", 4) == 0 ||
        name.Icmpn("video/", 6) == 0 ||
        name.Icmpn("videos/", 7) == 0 ||
        name.Icmpn("cinematics/", 11) == 0 ||
        name.Icmpn("generated/", 10) == 0 ||
        name.Find("cinematic", false) >= 0 ||
        name.Find("scratch", false) >= 0 ||
        name.Find("render", false) >= 0 ||
        name.Find(".swf", false) >= 0;
}

bool IsSmokeDiffuseImageSafeForRayTracing(idImage* image)
{
    if (!image)
    {
        return false;
    }

    const idImageOpts& opts = image->GetOpts();
    const bool guiTextureOverride =
        r_pathTracingAllowGuiTextures.GetInteger() != 0 &&
        IsSmokeImageNameGuiLike(image->GetName());
    if (opts.samples != 1 || opts.textureType != DTT_2D)
    {
        return false;
    }

    if ((opts.isRenderTarget || opts.isUAV) && !guiTextureOverride)
    {
        return false;
    }

    if (!IsSmokeImageNameSafeForRayTracing(image->GetName()) &&
        !guiTextureOverride)
    {
        return false;
    }

    return IsSmokeDiffuseTextureSafeForRayTracing(image->GetTextureHandle());
}

bool IsSmokeTextureHandleSafeForDescriptor(nvrhi::TextureHandle texture)
{
    return texture && IsSmokeDiffuseTextureSafeForRayTracing(texture);
}

bool SmokeTextureHandleListsEqual(const std::vector<nvrhi::TextureHandle>& lhs, const std::vector<nvrhi::TextureHandle>& rhs)
{
    if (lhs.size() != rhs.size())
    {
        return false;
    }

    for (int i = 0; i < static_cast<int>(lhs.size()); ++i)
    {
        if (lhs[i].Get() != rhs[i].Get())
        {
            return false;
        }
    }

    return true;
}

RtSmokeMaterialTextureInfo* FindSmokeMaterialTextureInfo(uint32_t materialId)
{
    std::unordered_map<uint32_t, int>::const_iterator lookup = g_smokeMaterialTextureRegistryLookup.find(materialId);
    if (lookup == g_smokeMaterialTextureRegistryLookup.end())
    {
        return nullptr;
    }

    const int index = lookup->second;
    if (index < 0 || index >= static_cast<int>(g_smokeMaterialTextureRegistry.size()))
    {
        return nullptr;
    }

    RtSmokeMaterialTextureInfo& info = g_smokeMaterialTextureRegistry[index];
    return info.materialId == materialId ? &info : nullptr;
}

RtSmokeMaterialTextureInfo& AddSmokeMaterialTextureInfo(uint32_t materialId, const char* materialName)
{
    RtSmokeMaterialTextureInfo newInfo;
    newInfo.materialId = materialId;
    newInfo.materialName = materialName ? materialName : "<none>";
    g_smokeMaterialTextureRegistry.push_back(newInfo);
    g_smokeMaterialTextureRegistryLookup[materialId] = static_cast<int>(g_smokeMaterialTextureRegistry.size() - 1);
    return g_smokeMaterialTextureRegistry.back();
}

void RefreshSmokeMaterialTextureHandleState(RtSmokeMaterialTextureInfo& info)
{
    info.hasTextureHandle = info.diffuseImage && info.diffuseImage->GetTextureHandle();
    info.hasAlphaTextureHandle = info.alphaImage && info.alphaImage->GetTextureHandle();
    info.hasNormalTextureHandle = info.normalImage && info.normalImage->GetTextureHandle();
    info.hasSpecularTextureHandle = info.specularImage && info.specularImage->GetTextureHandle();
    info.hasEmissiveTextureHandle = info.emissiveImage && info.emissiveImage->GetTextureHandle();
    info.hasSafeTexture = info.hasTextureHandle && IsSmokeDiffuseImageSafeForRayTracing(info.diffuseImage);
    info.hasSafeAlphaTexture = info.hasAlphaTextureHandle && IsSmokeDiffuseImageSafeForRayTracing(info.alphaImage);
    info.hasSafeNormalTexture = info.hasNormalTextureHandle && IsSmokeDiffuseImageSafeForRayTracing(info.normalImage);
    info.hasSafeSpecularTexture = info.hasSpecularTextureHandle && IsSmokeDiffuseImageSafeForRayTracing(info.specularImage);
    info.hasSafeEmissiveTexture = info.hasEmissiveTextureHandle && IsSmokeDiffuseImageSafeForRayTracing(info.emissiveImage);
}

RtSmokeMaterialTextureInfo ResolveSmokeMaterialTextureInfo(uint32_t materialId, int tableIndex)
{
    const RtSmokeMaterialTextureInfo* existing = FindSmokeMaterialTextureInfo(materialId);
    if (existing)
    {
        RtSmokeMaterialTextureInfo resolved = *existing;
        resolved.tableIndex = tableIndex;
        return resolved;
    }

    RtSmokeMaterialTextureInfo missing;
    missing.materialId = materialId;
    missing.tableIndex = tableIndex;
    missing.materialName = "<unseen material>";
    missing.diffuseImageName = "<none>";
    missing.fallbackReason = "material metadata not seen this session";
    return missing;
}

const idStr& SmokeBestSafeTextureName(const RtSmokeMaterialTextureInfo& info)
{
    if (info.hasSafeTexture)
    {
        return info.diffuseImageName;
    }
    if (info.hasSafeAlphaTexture)
    {
        return info.alphaImageName;
    }
    if (info.hasSafeNormalTexture)
    {
        return info.normalImageName;
    }
    if (info.hasSafeSpecularTexture)
    {
        return info.specularImageName;
    }
    return info.emissiveImageName;
}

int SmokeMaterialTextureRegistrySize()
{
    return static_cast<int>(g_smokeMaterialTextureRegistry.size());
}
