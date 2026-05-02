#include "precompiled.h"
#pragma hdrstop

#include "PathTraceTextureRegistry.h"
#include "../Image.h"

extern idCVar r_pathTracingAllowGuiTextures;

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
