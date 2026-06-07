#include "precompiled.h"
#pragma hdrstop

// Doom material texture discovery for the RT smoke texture registry.
//
// This pass walks visible material declarations and records the texture metadata
// that the dynamic material table can safely bind. Rejections are preserved as
// diagnostics because many Doom materials are still intentionally unsupported.

#include "PathTraceCVars.h"
#include "PathTraceMaterialTextureDiscovery.h"
#include "PathTraceDebugDumps.h"
#include "PathTraceDoomMaterialClassifier.h"
#include "PathTraceDynamicMaterialState.h"
#include "PathTraceMaterialClassifier.h"
#include "PathTraceSceneCapture.h"
#include "PathTraceTextureRegistry.h"

#include <algorithm>

RtSmokeMaterialMetadataFrameStats g_smokeMaterialMetadataFrameStats;

namespace {
bool IsSmokePortalWindowFallbackMaterial(const idMaterial* material);
bool IsSmokeObjectGlassFallbackMaterial(const idMaterial* material);
bool IsSmokeTranslucentOverlayCardMaterial(const idMaterial* material, const RtSmokeTranslucentClassifierInfo& classifier);

bool PathTraceMaterialClassifierRequested()
{
    return r_pathTracingMatClassEnable.GetInteger() != 0 ||
        r_pathTracingMatClassDebugList.GetInteger() != 0;
}

enum class RtSmokeTextureCodeHint
{
    Unknown,
    Specular0000,
    DiffuseAlpha0100,
    Normal0300,
    AlphaClip1000
};

const char* SmokeTextureCodeHintName(RtSmokeTextureCodeHint hint)
{
    switch (hint)
    {
        case RtSmokeTextureCodeHint::Specular0000:
            return "0000/specular";
        case RtSmokeTextureCodeHint::DiffuseAlpha0100:
            return "0100/diffuse-alpha";
        case RtSmokeTextureCodeHint::Normal0300:
            return "0300/normal";
        case RtSmokeTextureCodeHint::AlphaClip1000:
            return "1000/alpha-clip";
        default:
            return "unknown";
    }
}

RtSmokeTextureCodeHint SmokeTextureCodeHintFromImageName(const char* imageName)
{
    if (!imageName || !imageName[0])
    {
        return RtSmokeTextureCodeHint::Unknown;
    }

    idStr name = imageName;
    name.BackSlashesToSlashes();
    const int length = name.Length();
    for (int start = Max(0, length - 12); start <= length - 4; ++start)
    {
        const bool fourDigits =
            name[start + 0] >= '0' && name[start + 0] <= '9' &&
            name[start + 1] >= '0' && name[start + 1] <= '9' &&
            name[start + 2] >= '0' && name[start + 2] <= '9' &&
            name[start + 3] >= '0' && name[start + 3] <= '9';
        if (!fourDigits)
        {
            continue;
        }

        const bool boundedBefore = start == 0 || name[start - 1] == '_' || name[start - 1] == '-' || name[start - 1] == '/' || name[start - 1] == '.';
        const bool boundedAfter = start + 4 >= length || name[start + 4] == '_' || name[start + 4] == '-' || name[start + 4] == '.' || name[start + 4] == '/';
        if (!boundedBefore || !boundedAfter)
        {
            continue;
        }

        if (idStr::Cmpn(name.c_str() + start, "0000", 4) == 0)
        {
            return RtSmokeTextureCodeHint::Specular0000;
        }
        if (idStr::Cmpn(name.c_str() + start, "0100", 4) == 0)
        {
            return RtSmokeTextureCodeHint::DiffuseAlpha0100;
        }
        if (idStr::Cmpn(name.c_str() + start, "0300", 4) == 0)
        {
            return RtSmokeTextureCodeHint::Normal0300;
        }
        if (idStr::Cmpn(name.c_str() + start, "1000", 4) == 0)
        {
            return RtSmokeTextureCodeHint::AlphaClip1000;
        }
    }

    return RtSmokeTextureCodeHint::Unknown;
}

idImage* FindSmokeImageByTextureCode(const idMaterial* material, RtSmokeTextureCodeHint wantedHint, idStr& reason)
{
    if (!material || wantedHint == RtSmokeTextureCodeHint::Unknown || r_pathTracingForceTextureCodeUse.GetInteger() == 0)
    {
        return nullptr;
    }

    for (int stageIndex = 0; stageIndex < material->GetNumStages(); ++stageIndex)
    {
        const shaderStage_t* stage = material->GetStage(stageIndex);
        if (!stage || !stage->texture.image)
        {
            continue;
        }

        const RtSmokeTextureCodeHint hint = SmokeTextureCodeHintFromImageName(stage->texture.image->GetName());
        if (hint == wantedHint)
        {
            reason = va("stage %d texture code %s", stageIndex, SmokeTextureCodeHintName(hint));
            return stage->texture.image;
        }
    }

    return nullptr;
}

idImage* FindSmokeImageByUsageAndFormat(const idMaterial* material, textureUsage_t wantedUsage, textureColor_t wantedFormat, stageLighting_t wantedLighting, idStr& reason)
{
    if (!material)
    {
        return nullptr;
    }

    for (int stageIndex = 0; stageIndex < material->GetNumStages(); ++stageIndex)
    {
        const shaderStage_t* stage = material->GetStage(stageIndex);
        if (!stage || !stage->texture.image)
        {
            continue;
        }
        if (wantedLighting != SL_AMBIENT && stage->lighting != wantedLighting)
        {
            continue;
        }

        idImage* image = stage->texture.image;
        if (image->GetUsage() == wantedUsage || image->GetOpts().colorFormat == wantedFormat)
        {
            reason = va("stage %d %s/%s", stageIndex, SmokeTextureUsageName(image->GetUsage()), SmokeTextureColorFormatName(image->GetOpts().colorFormat));
            return image;
        }
    }

    return nullptr;
}

idImage* FindSmokeDiffuseImage(const idMaterial* material, idStr& reason)
{
    if (!material)
    {
        reason = "null material";
        return nullptr;
    }

    idImage* image = material->GetFastPathDiffuseImage();
    if (image)
    {
        reason = "fastPathDiffuse";
        return image;
    }

    image = FindSmokeImageByUsageAndFormat(material, TD_DIFFUSE, CFM_YCOCG_DXT5, SL_DIFFUSE, reason);
    if (image)
    {
        return image;
    }

    image = FindSmokeImageByTextureCode(material, RtSmokeTextureCodeHint::DiffuseAlpha0100, reason);
    if (image)
    {
        return image;
    }

    for (int stageIndex = 0; stageIndex < material->GetNumStages(); ++stageIndex)
    {
        const shaderStage_t* stage = material->GetStage(stageIndex);
        if (!stage || stage->lighting != SL_DIFFUSE || !stage->texture.image)
        {
            continue;
        }

        reason = va("stage %d SL_DIFFUSE", stageIndex);
        return stage->texture.image;
    }

    const RtSmokeTranslucentClassifierInfo classifier = BuildSmokeTranslucentClassifierInfo(material);
    if (r_pathTracingAllowGuiTextures.GetInteger() != 0 && (material->HasGui() || classifier.nameLooksGui || classifier.sortIsGuiOrSubview))
    {
        for (int stageIndex = 0; stageIndex < material->GetNumStages(); ++stageIndex)
        {
            const shaderStage_t* stage = material->GetStage(stageIndex);
            if (!stage || !stage->texture.image)
            {
                continue;
            }

            if (stage->texture.texgen == TG_SCREEN ||
                stage->texture.texgen == TG_SCREEN2 ||
                stage->texture.dynamic == DI_GUI_RENDER ||
                stage->texture.dynamic == DI_RENDER_TARGET)
            {
                reason = va("stage %d GUI screen/dynamic", stageIndex);
                return stage->texture.image;
            }
        }

        for (int stageIndex = 0; stageIndex < material->GetNumStages(); ++stageIndex)
        {
            const shaderStage_t* stage = material->GetStage(stageIndex);
            if (!stage || !stage->texture.image)
            {
                continue;
            }

            if (stage->lighting == SL_AMBIENT || stage->lighting == SL_DIFFUSE)
            {
                reason = va("stage %d GUI ambient/diffuse", stageIndex);
                return stage->texture.image;
            }
        }
    }

    if (IsSmokeTranslucentOverlayCardMaterial(material, classifier))
    {
        for (int stageIndex = 0; stageIndex < material->GetNumStages(); ++stageIndex)
        {
            const shaderStage_t* stage = material->GetStage(stageIndex);
            if (!stage || stage->lighting != SL_AMBIENT || !stage->texture.image)
            {
                continue;
            }

            reason = va("stage %d SL_AMBIENT translucent blend", stageIndex);
            return stage->texture.image;
        }
    }

    reason = "no fast-path or diffuse-stage image";
    return nullptr;
}

idImage* FindSmokeNormalImage(const idMaterial* material, idStr& reason)
{
    if (!material)
    {
        reason = "null material";
        return nullptr;
    }

    idImage* image = material->GetFastPathBumpImage();
    if (image)
    {
        reason = "fastPathBump";
        return image;
    }

    image = FindSmokeImageByUsageAndFormat(material, TD_BUMP, CFM_NORMAL_DXT5, SL_BUMP, reason);
    if (image)
    {
        return image;
    }

    image = FindSmokeImageByTextureCode(material, RtSmokeTextureCodeHint::Normal0300, reason);
    if (image)
    {
        return image;
    }

    for (int stageIndex = 0; stageIndex < material->GetNumStages(); ++stageIndex)
    {
        const shaderStage_t* stage = material->GetStage(stageIndex);
        if (!stage || stage->lighting != SL_BUMP || !stage->texture.image)
        {
            continue;
        }

        reason = va("stage %d SL_BUMP", stageIndex);
        return stage->texture.image;
    }

    reason = "no fast-path or bump-stage image";
    return nullptr;
}

idImage* FindSmokeSpecularImage(const idMaterial* material, idStr& reason)
{
    if (!material)
    {
        reason = "null material";
        return nullptr;
    }

    idImage* image = material->GetFastPathSpecularImage();
    if (image)
    {
        reason = "fastPathSpecular";
        return image;
    }

    image = FindSmokeImageByUsageAndFormat(material, TD_SPECULAR, CFM_DEFAULT, SL_SPECULAR, reason);
    if (image)
    {
        return image;
    }

    image = FindSmokeImageByTextureCode(material, RtSmokeTextureCodeHint::Specular0000, reason);
    if (image)
    {
        return image;
    }

    for (int stageIndex = 0; stageIndex < material->GetNumStages(); ++stageIndex)
    {
        const shaderStage_t* stage = material->GetStage(stageIndex);
        if (!stage || stage->lighting != SL_SPECULAR || !stage->texture.image)
        {
            continue;
        }

        reason = va("stage %d SL_SPECULAR", stageIndex);
        return stage->texture.image;
    }

    reason = "no fast-path or specular-stage image";
    return nullptr;
}

bool SmokeStageIsAdditiveOrGlowLike(const shaderStage_t* stage)
{
    if (!stage)
    {
        return false;
    }

    const uint64 srcBlend = stage->drawStateBits & GLS_SRCBLEND_BITS;
    const uint64 dstBlend = stage->drawStateBits & GLS_DSTBLEND_BITS;
    return (stage->lighting == SL_AMBIENT && dstBlend == GLS_DSTBLEND_ONE) ||
        (srcBlend == GLS_SRCBLEND_ONE && dstBlend == GLS_DSTBLEND_ONE);
}

bool SmokeStageTextureIsAnimatedOrViewDependent(const shaderStage_t* stage)
{
    if (!stage)
    {
        return false;
    }

    return stage->texture.cinematic != nullptr ||
        stage->texture.dynamic != DI_STATIC ||
        stage->texture.dynamicFrameCount > 0 ||
        stage->texture.texgen == TG_SCREEN ||
        stage->texture.texgen == TG_SCREEN2 ||
        stage->texture.texgen == TG_GLASSWARP;
}

bool SmokeStageConstantColor(const idMaterial* material, const shaderStage_t* stage, idVec4& color)
{
    color = idVec4(1.0f, 1.0f, 1.0f, 1.0f);
    if (!material || !stage)
    {
        return false;
    }

    const float* constantRegisters = material->ConstantRegisters();
    const int registerCount = material->GetNumRegisters();
    if (!constantRegisters)
    {
        return false;
    }

    for (int component = 0; component < 4; ++component)
    {
        const int registerIndex = stage->color.registers[component];
        if (registerIndex < 0 || registerIndex >= registerCount)
        {
            return false;
        }
        color[component] = constantRegisters[registerIndex];
    }
    color.x = Max(0.0f, color.x);
    color.y = Max(0.0f, color.y);
    color.z = Max(0.0f, color.z);
    color.w = idMath::ClampFloat(0.0f, 1.0f, color.w);
    return true;
}

bool IsSmokeReflectiveEyewearMaterial(const idMaterial* material)
{
    if (!material || material->Coverage() != MC_PERFORATED)
    {
        return false;
    }

    idStr materialName = material->GetName();
    static const char* eyewearTokens[] = { "glasses", "goggles", "gogs", "visor" };
    if (!SmokeNameContainsAny(materialName, eyewearTokens, sizeof(eyewearTokens) / sizeof(eyewearTokens[0])))
    {
        return false;
    }

    for (int stageIndex = 0; stageIndex < material->GetNumStages(); ++stageIndex)
    {
        const shaderStage_t* stage = material->GetStage(stageIndex);
        if (stage && stage->lighting == SL_SPECULAR && stage->texture.image)
        {
            return true;
        }
    }
    return false;
}

bool IsSmokeAbsorbingBlackMaterial(const idMaterial* material)
{
    if (!material)
    {
        return false;
    }

    idStr materialName = material->GetName();
    materialName.BackSlashesToSlashes();
    return materialName.Icmp("textures/sfx/black") == 0;
}

void ForceSmokeAbsorbingBlackMaterialInfo(RtSmokeMaterialTextureInfo& info)
{
    info.diffuseImage = nullptr;
    info.alphaImage = nullptr;
    info.normalImage = nullptr;
    info.specularImage = nullptr;
    info.emissiveImage = nullptr;
    info.hasDiffuseImage = false;
    info.hasAlphaImage = false;
    info.hasNormalImage = false;
    info.hasSpecularImage = false;
    info.hasEmissiveImage = false;
    info.hasTextureHandle = false;
    info.hasAlphaTextureHandle = false;
    info.hasNormalTextureHandle = false;
    info.hasSpecularTextureHandle = false;
    info.hasEmissiveTextureHandle = false;
    info.hasSafeTexture = false;
    info.hasSafeAlphaTexture = false;
    info.hasSafeNormalTexture = false;
    info.hasSafeSpecularTexture = false;
    info.hasSafeEmissiveTexture = false;
    info.hasAlphaTest = false;
    info.additiveDecal = false;
    info.additiveDecalWhiteKey = false;
    info.filterDecal = false;
    info.filterDecalBlackKey = false;
    info.alphaFromDiffuseLuma = false;
    info.forceFallbackAlbedo = true;
    info.alphaFromDiffuseDarkKey = false;
    info.portalWindowFallback = false;
    info.objectGlassFallback = false;
    info.emissive = false;
    info.alphaCutoff = 0.0f;
    info.emissiveColor = idVec4(0.0f, 0.0f, 0.0f, 1.0f);
    info.fallbackAlbedo = idVec4(0.0f, 0.0f, 0.0f, 1.0f);
    info.hasFallbackAlbedo = true;
    info.diffuseUsage = TD_DEFAULT;
    info.alphaUsage = TD_DEFAULT;
    info.normalUsage = TD_DEFAULT;
    info.specularUsage = TD_DEFAULT;
    info.emissiveUsage = TD_DEFAULT;
    info.diffuseColorFormat = CFM_DEFAULT;
    info.alphaColorFormat = CFM_DEFAULT;
    info.normalColorFormat = CFM_DEFAULT;
    info.specularColorFormat = CFM_DEFAULT;
    info.emissiveColorFormat = CFM_DEFAULT;
    info.diffuseImageName = "<none>";
    info.alphaImageName = "<none>";
    info.normalImageName = "<none>";
    info.specularImageName = "<none>";
    info.emissiveImageName = "<none>";
    info.fallbackReason = "absorbing black material";
    info.alphaReason = "absorbing black material";
    info.normalReason = "absorbing black material";
    info.specularReason = "absorbing black material";
    info.emissiveReason = "absorbing black material";
}

bool IsSmokePortalWindowFallbackMaterial(const idMaterial* material)
{
    if (!material || material->Coverage() != MC_TRANSLUCENT)
    {
        return false;
    }

    const RtSmokeTranslucentClassifierInfo classifier = BuildSmokeTranslucentClassifierInfo(material);
    return classifier.sortIsGuiOrSubview || classifier.sortIsPostProcess || classifier.hasScreenTexgen;
}

bool IsSmokeObjectGlassFallbackMaterial(const idMaterial* material)
{
    if (!material || material->Coverage() != MC_TRANSLUCENT)
    {
        return false;
    }

    const RtSmokeTranslucentClassifierInfo classifier = BuildSmokeTranslucentClassifierInfo(material);
    return classifier.nameLooksGlass && !IsSmokePortalWindowFallbackMaterial(material);
}

bool IsSmokeTranslucentOverlayCardMaterial(const idMaterial* material, const RtSmokeTranslucentClassifierInfo& classifier)
{
    if (!material || material->Coverage() != MC_TRANSLUCENT)
    {
        return false;
    }
    if (IsSmokePortalWindowFallbackMaterial(material))
    {
        return false;
    }
    if (classifier.hasAddDefault0200Texture)
    {
        return false;
    }

    return classifier.sortIsDecal ||
        classifier.polygonOffsetDecal ||
        classifier.nameLooksDecal ||
        (classifier.hasAmbientBlendStage && !classifier.hasDiffuseStage);
}

bool FindSmokeMaterialFallbackAlbedo(const idMaterial* material, idVec4& albedo)
{
    albedo = idVec4(0.0f, 0.0f, 0.0f, 1.0f);
    if (!material)
    {
        return false;
    }

    if (IsSmokeReflectiveEyewearMaterial(material))
    {
        albedo = idVec4(0.08f, 0.10f, 0.11f, 1.0f);
        return true;
    }

    const RtSmokeTranslucentClassifierInfo classifier = BuildSmokeTranslucentClassifierInfo(material);
    for (int stageIndex = 0; stageIndex < material->GetNumStages(); ++stageIndex)
    {
        const shaderStage_t* stage = material->GetStage(stageIndex);
        if (!stage)
        {
            continue;
        }

        if (stage->texture.texgen == TG_SKYBOX_CUBE || stage->texture.texgen == TG_WOBBLESKY_CUBE)
        {
            albedo = idVec4(0.0f, 0.0f, 0.0f, 1.0f);
            return true;
        }
    }

    if (!IsSmokePortalWindowFallbackMaterial(material))
    {
        return false;
    }

    albedo = idVec4(0.55f, 0.70f, 0.72f, 1.0f);
    for (int stageIndex = 0; stageIndex < material->GetNumStages(); ++stageIndex)
    {
        const shaderStage_t* stage = material->GetStage(stageIndex);
        if (!stage || (stage->lighting != SL_AMBIENT && stage->lighting != SL_DIFFUSE))
        {
            continue;
        }

        idVec4 stageColor;
        if (!SmokeStageConstantColor(material, stage, stageColor))
        {
            continue;
        }

        if (stageColor.x <= 0.0f && stageColor.y <= 0.0f && stageColor.z <= 0.0f)
        {
            continue;
        }

        albedo = stageColor;
        return true;
    }

    return true;
}

idImage* FindSmokeEmissiveImage(const idMaterial* material, idStr& reason, idVec4& emissiveColor)
{
    emissiveColor = idVec4(0.0f, 0.0f, 0.0f, 1.0f);
    if (!material)
    {
        reason = "null material";
        return nullptr;
    }

    for (int stageIndex = 0; stageIndex < material->GetNumStages(); ++stageIndex)
    {
        const shaderStage_t* stage = material->GetStage(stageIndex);
        if (!stage)
        {
            continue;
        }

        const bool skyOrCubeTexgen =
            stage->texture.texgen == TG_DIFFUSE_CUBE ||
            stage->texture.texgen == TG_REFLECT_CUBE ||
            stage->texture.texgen == TG_REFLECT_CUBE2 ||
            stage->texture.texgen == TG_SKYBOX_CUBE ||
            stage->texture.texgen == TG_WOBBLESKY_CUBE;
        if (skyOrCubeTexgen)
        {
            reason = va("stage %d rejected sky/cube material emissive texgen", stageIndex);
            return nullptr;
        }
    }

    const RtSmokeTranslucentClassifierInfo classifier = BuildSmokeTranslucentClassifierInfo(material);
    if (classifier.hasScreenTexgen ||
        classifier.hasAddDefault0200Texture ||
        classifier.sortIsGuiOrSubview ||
        classifier.sortIsPostProcess ||
        classifier.nameLooksGui ||
        classifier.nameLooksParticle ||
        classifier.nameLooksGlass)
    {
        reason = "rejected gui/particle/glass/view-dependent material";
        return nullptr;
    }

    const bool nameLooksEmissive = !classifier.hasAddDefault0200Texture && (classifier.nameLooksGlow || classifier.nameLooksSignage);
    const float* constantRegisters = material->ConstantRegisters();
    const int registerCount = material->GetNumRegisters();
    for (int stageIndex = 0; stageIndex < material->GetNumStages(); ++stageIndex)
    {
        const shaderStage_t* stage = material->GetStage(stageIndex);
        if (!stage || stage->lighting != SL_AMBIENT)
        {
            continue;
        }
        if (constantRegisters && stage->conditionRegister >= 0 && stage->conditionRegister < registerCount && constantRegisters[stage->conditionRegister] == 0.0f)
        {
            continue;
        }

        const bool additiveStage = SmokeStageIsAdditiveOrGlowLike(stage);
        bool filterBlackKey = false;
        const bool filterStage = SmokeStageIsFilterBlend(stage, filterBlackKey);
        const bool skyOrCubeTexgen =
            stage->texture.texgen == TG_DIFFUSE_CUBE ||
            stage->texture.texgen == TG_REFLECT_CUBE ||
            stage->texture.texgen == TG_REFLECT_CUBE2 ||
            stage->texture.texgen == TG_SKYBOX_CUBE ||
            stage->texture.texgen == TG_WOBBLESKY_CUBE;
        if (skyOrCubeTexgen)
        {
            reason = va("stage %d rejected sky/cube emissive texgen", stageIndex);
            continue;
        }
        const bool stageLooksEmissive = additiveStage || (nameLooksEmissive && classifier.hasAmbientBlendStage && !classifier.nameLooksDecal && !filterStage);
        if (!stageLooksEmissive)
        {
            continue;
        }
        if (SmokeStageTextureIsAnimatedOrViewDependent(stage))
        {
            reason = va("stage %d rejected animated/view-dependent emissive texture", stageIndex);
            continue;
        }

        idVec4 stageColor;
        if (SmokeStageConstantColor(material, stage, stageColor))
        {
            emissiveColor = stageColor;
        }
        else
        {
            emissiveColor = idVec4(1.0f, 1.0f, 1.0f, 1.0f);
        }

        if (stage->texture.image)
        {
            reason = va("stage %d SL_AMBIENT glow/additive", stageIndex);
            return stage->texture.image;
        }

        if (additiveStage && (emissiveColor.x > 0.0f || emissiveColor.y > 0.0f || emissiveColor.z > 0.0f))
        {
            reason = va("stage %d SL_AMBIENT constant glow/additive", stageIndex);
            return nullptr;
        }
    }

    reason = "no emissive ambient/additive stage";
    return nullptr;
}

idImage* FindSmokeAlphaImage(const idMaterial* material, idStr& reason)
{
    if (!material)
    {
        reason = "null material";
        return nullptr;
    }

    const RtSmokeTranslucentClassifierInfo classifier = BuildSmokeTranslucentClassifierInfo(material);
    const bool allowTranslucentCutout =
        material->Coverage() == MC_TRANSLUCENT &&
        !classifier.hasScreenTexgen &&
        !classifier.hasAddDefault0200Texture &&
        !classifier.nameLooksGui &&
        !classifier.nameLooksParticle &&
        (classifier.nameLooksGlass || classifier.nameLooksSignage || classifier.nameLooksGlow);
    if (material->Coverage() != MC_PERFORATED && !allowTranslucentCutout)
    {
        reason = "not perforated or translucent cutout";
        return nullptr;
    }

    idImage* image = FindSmokeImageByUsageAndFormat(material, TD_COVERAGE, CFM_GREEN_ALPHA, SL_COVERAGE, reason);
    if (image)
    {
        return image;
    }

    for (int stageIndex = 0; stageIndex < material->GetNumStages(); ++stageIndex)
    {
        const shaderStage_t* stage = material->GetStage(stageIndex);
        if (!stage || stage->ignoreAlphaTest || !stage->texture.image)
        {
            continue;
        }

        if (stage->lighting == SL_COVERAGE)
        {
            reason = va("stage %d SL_COVERAGE", stageIndex);
            return stage->texture.image;
        }
    }

    image = FindSmokeImageByTextureCode(material, RtSmokeTextureCodeHint::AlphaClip1000, reason);
    if (image)
    {
        return image;
    }

    if (allowTranslucentCutout)
    {
        for (int stageIndex = 0; stageIndex < material->GetNumStages(); ++stageIndex)
        {
            const shaderStage_t* stage = material->GetStage(stageIndex);
            if (!stage || !stage->hasAlphaTest || stage->ignoreAlphaTest || !stage->texture.image)
            {
                continue;
            }

            if (stage->texture.image->GetOpts().colorFormat == CFM_YCOCG_DXT5)
            {
                continue;
            }

            reason = va("stage %d translucent alpha-test", stageIndex);
            return stage->texture.image;
        }
    }

    reason = "no SL_COVERAGE image";
    return nullptr;
}

}

void RegisterSmokeMaterialTextureInfo(const idMaterial* material)
{
    OPTICK_EVENT("PT Material Metadata Discover One");

    ++g_smokeMaterialMetadataFrameStats.registrations;

    const char* materialName = material ? material->GetName() : "<none>";
    const uint32_t materialId = HashSmokeMaterialName(materialName);

    RtSmokeMaterialTextureInfo* info = FindSmokeMaterialTextureInfo(materialId);
    if (info && r_pathTracingMaterialMetadataCache.GetInteger() != 0)
    {
        const RtSmokeTranslucentClassifierInfo classifier = BuildSmokeTranslucentClassifierInfo(material);
        const bool forceRediscoverAbsorbingBlack = IsSmokeAbsorbingBlackMaterial(material);
        const bool rediscoverGuiDiffuse =
            r_pathTracingAllowGuiTextures.GetInteger() != 0 &&
            (material && (material->HasGui() || classifier.nameLooksGui || classifier.sortIsGuiOrSubview)) &&
            !info->hasDiffuseImage;
        if (!rediscoverGuiDiffuse && !forceRediscoverAbsorbingBlack)
        {
            ++g_smokeMaterialMetadataFrameStats.cacheRefreshes;
            RefreshSmokeMaterialTextureHandleState(*info);
            if (PathTraceMaterialClassifierRequested())
            {
                RegisterPathTraceMaterialRecord(material, *info);
            }
            return;
        }
    }

    if (!info)
    {
        ++g_smokeMaterialMetadataFrameStats.newEntries;
        info = &AddSmokeMaterialTextureInfo(materialId, materialName);
    }

    ++g_smokeMaterialMetadataFrameStats.fullDiscovers;

    idStr reason;
    idImage* diffuseImage = FindSmokeDiffuseImage(material, reason);
    idStr alphaReason;
    idImage* alphaImage = FindSmokeAlphaImage(material, alphaReason);
    idStr normalReason;
    idImage* normalImage = FindSmokeNormalImage(material, normalReason);
    idStr specularReason;
    idImage* specularImage = FindSmokeSpecularImage(material, specularReason);
    idStr emissiveReason;
    idVec4 emissiveColor;
    idImage* emissiveImage = FindSmokeEmissiveImage(material, emissiveReason, emissiveColor);
    idVec4 fallbackAlbedo;
    const bool hasFallbackAlbedo = FindSmokeMaterialFallbackAlbedo(material, fallbackAlbedo);
    info->materialName = materialName;
    info->fallbackReason = reason;
    info->alphaReason = alphaReason;
    info->normalReason = normalReason;
    info->specularReason = specularReason;
    info->emissiveReason = emissiveReason;
    info->diffuseImage = diffuseImage;
    info->alphaImage = alphaImage;
    info->normalImage = normalImage;
    info->specularImage = specularImage;
    info->emissiveImage = emissiveImage;
    info->coverage = material ? material->Coverage() : MC_BAD;
    info->hasDiffuseImage = diffuseImage != nullptr;
    info->hasAlphaImage = alphaImage != nullptr;
    info->hasNormalImage = normalImage != nullptr;
    info->hasSpecularImage = specularImage != nullptr;
    info->hasEmissiveImage = emissiveImage != nullptr;
    info->hasTextureHandle = diffuseImage && diffuseImage->GetTextureHandle();
    info->hasAlphaTextureHandle = alphaImage && alphaImage->GetTextureHandle();
    info->hasNormalTextureHandle = normalImage && normalImage->GetTextureHandle();
    info->hasSpecularTextureHandle = specularImage && specularImage->GetTextureHandle();
    info->hasEmissiveTextureHandle = emissiveImage && emissiveImage->GetTextureHandle();
    info->diffuseImageName = diffuseImage ? diffuseImage->GetName() : "<none>";
    info->alphaImageName = alphaImage ? alphaImage->GetName() : "<none>";
    info->normalImageName = normalImage ? normalImage->GetName() : "<none>";
    info->specularImageName = specularImage ? specularImage->GetName() : "<none>";
    info->emissiveImageName = emissiveImage ? emissiveImage->GetName() : "<none>";
    info->diffuseUsage = diffuseImage ? diffuseImage->GetUsage() : TD_DEFAULT;
    info->alphaUsage = alphaImage ? alphaImage->GetUsage() : TD_DEFAULT;
    info->normalUsage = normalImage ? normalImage->GetUsage() : TD_DEFAULT;
    info->specularUsage = specularImage ? specularImage->GetUsage() : TD_DEFAULT;
    info->emissiveUsage = emissiveImage ? emissiveImage->GetUsage() : TD_DEFAULT;
    info->diffuseColorFormat = diffuseImage ? diffuseImage->GetOpts().colorFormat : CFM_DEFAULT;
    info->alphaColorFormat = alphaImage ? alphaImage->GetOpts().colorFormat : CFM_DEFAULT;
    info->normalColorFormat = normalImage ? normalImage->GetOpts().colorFormat : CFM_DEFAULT;
    info->specularColorFormat = specularImage ? specularImage->GetOpts().colorFormat : CFM_DEFAULT;
    info->emissiveColorFormat = emissiveImage ? emissiveImage->GetOpts().colorFormat : CFM_DEFAULT;
    ResolveSmokeMaterialAlphaInfo(material, info->hasAlphaTest, info->alphaCutoff);
    info->additiveDecal = IsSmokeAdditiveDecalMaterial(material);
    const RtSmokeTranslucentClassifierInfo classifier = BuildSmokeTranslucentClassifierInfo(material);
    info->additiveDecalWhiteKey = IsSmokeAdditiveWhiteKeyMaterial(material, classifier);
    const bool rgbKeyedBlendDecal = IsSmokeRgbKeyedBlendDecalMaterial(material, classifier);
    const bool yCoCgDiffuseMapDecal = IsSmokeYCoCgDiffuseMapDecalMaterial(material, classifier);
    info->filterDecal = !info->additiveDecal && (IsSmokeTranslucentOverlayCardMaterial(material, classifier) || rgbKeyedBlendDecal);
    info->filterDecalBlackKey = false;
    if (info->filterDecal)
    {
        bool foundFilterBlend = false;
        for (int stageIndex = 0; stageIndex < material->GetNumStages(); ++stageIndex)
        {
            const shaderStage_t* stage = material->GetStage(stageIndex);
            if (!stage || stage->lighting != SL_AMBIENT || !stage->texture.image)
            {
                continue;
            }

            bool blackKey = false;
            if (SmokeStageIsFilterBlend(stage, blackKey))
            {
                info->filterDecalBlackKey = blackKey;
                foundFilterBlend = true;
                break;
            }
        }
        if (!foundFilterBlend && yCoCgDiffuseMapDecal)
        {
            info->filterDecalBlackKey = true;
        }
    }
    info->alphaFromDiffuseLuma =
        info->hasAlphaTest &&
        diffuseImage != nullptr &&
        alphaImage == diffuseImage &&
        (diffuseImage->GetUsage() == TD_DIFFUSE || diffuseImage->GetOpts().colorFormat == CFM_YCOCG_DXT5);
    info->forceFallbackAlbedo = IsSmokeReflectiveEyewearMaterial(material);
    info->alphaFromDiffuseDarkKey = info->alphaFromDiffuseLuma && info->forceFallbackAlbedo;
    info->portalWindowFallback = IsSmokePortalWindowFallbackMaterial(material);
    info->objectGlassFallback = IsSmokeObjectGlassFallbackMaterial(material);
    info->emissiveColor = emissiveColor;
    info->emissive = info->hasEmissiveImage || emissiveColor.x > 0.0f || emissiveColor.y > 0.0f || emissiveColor.z > 0.0f;
    if (info->emissive)
    {
        info->hasAlphaTest = false;
        info->alphaImage = nullptr;
        info->alphaImageName = "<none>";
        info->alphaReason = "emissive stage ignores alpha/blend semantics";
        info->hasAlphaImage = false;
        info->hasAlphaTextureHandle = false;
        info->hasSafeAlphaTexture = false;
        info->alphaUsage = TD_DEFAULT;
        info->alphaColorFormat = CFM_DEFAULT;
        info->alphaCutoff = 0.0f;
        info->additiveDecal = false;
        info->additiveDecalWhiteKey = false;
        info->filterDecal = false;
        info->filterDecalBlackKey = false;
        info->alphaFromDiffuseLuma = false;
        info->alphaFromDiffuseDarkKey = false;
    }
    info->fallbackAlbedo = fallbackAlbedo;
    info->hasFallbackAlbedo = hasFallbackAlbedo;
    if (IsSmokeAbsorbingBlackMaterial(material))
    {
        ForceSmokeAbsorbingBlackMaterialInfo(*info);
    }
    RefreshSmokeMaterialTextureHandleState(*info);
    if (info->hasDiffuseImage && !info->hasSafeTexture)
    {
        if (diffuseImage && !IsSmokeImageNameSafeForRayTracing(diffuseImage->GetName()))
        {
            info->fallbackReason = va("%s; rejected dynamic image name", reason.c_str());
        }
        else if (diffuseImage && (diffuseImage->GetOpts().isRenderTarget || diffuseImage->GetOpts().isUAV))
        {
            info->fallbackReason = va("%s; rejected image opts rt=%d uav=%d", reason.c_str(), diffuseImage->GetOpts().isRenderTarget ? 1 : 0, diffuseImage->GetOpts().isUAV ? 1 : 0);
        }
        else
        {
            info->fallbackReason = va("%s; rejected texture desc", reason.c_str());
        }
    }
    if (PathTraceMaterialClassifierRequested())
    {
        RegisterPathTraceMaterialRecord(material, *info);
    }
}

RtSmokeMaterialMetadataRegistrationTiming RegisterSmokeMaterialTextureInfoForFrame(const viewDef_t* viewDef, bool enabled)
{
    OPTICK_EVENT("PT Material Metadata Frame");

    RtSmokeMaterialMetadataRegistrationTiming timing;
    g_smokeMaterialMetadataFrameStats = RtSmokeMaterialMetadataFrameStats();
    if (PathTraceMaterialClassifierRequested())
    {
        BeginPathTraceMaterialClassifierFrame();
        MaybeDumpPathTraceMaterialDeclSurfaceTypeDistribution();
    }

    const int metadataStartMs = Sys_Milliseconds();
    if (enabled && viewDef)
    {
        OPTICK_EVENT("PT Material Metadata Surface Loop");
        std::vector<uint32_t> registeredMaterialIds;
        registeredMaterialIds.reserve(viewDef->numDrawSurfs);
        for (int surfaceIndex = 0; surfaceIndex < viewDef->numDrawSurfs; ++surfaceIndex)
        {
            const drawSurf_t* drawSurf = viewDef->drawSurfs[surfaceIndex];
            const srfTriangles_t* tri = nullptr;
            const int validationStartMs = Sys_Milliseconds();
            if (!ValidateSmokeDrawSurface(viewDef, drawSurf, tri, nullptr))
            {
                timing.validationMs += Sys_Milliseconds() - validationStartMs;
                continue;
            }
            timing.validationMs += Sys_Milliseconds() - validationStartMs;

            const uint32_t materialId = SmokeMaterialId(drawSurf->material);
            if (std::find(registeredMaterialIds.begin(), registeredMaterialIds.end(), materialId) != registeredMaterialIds.end())
            {
                ++g_smokeMaterialMetadataFrameStats.duplicateSkips;
                continue;
            }

            registeredMaterialIds.push_back(materialId);
            const int registrationStartMs = Sys_Milliseconds();
            RegisterSmokeMaterialTextureInfo(drawSurf->material);
            timing.registrationMs += Sys_Milliseconds() - registrationStartMs;
        }
    }
    timing.metadataMs = Sys_Milliseconds() - metadataStartMs;
    return timing;
}

RtSmokeMaterialMetadataRegistrationTiming RegisterSmokeWorldStaticMaterialTextureInfo(const viewDef_t* viewDef, bool enabled)
{
    OPTICK_EVENT("PT Material Metadata World Static");

    RtSmokeMaterialMetadataRegistrationTiming timing;
    const int metadataStartMs = Sys_Milliseconds();
    if (PathTraceMaterialClassifierRequested())
    {
        BeginPathTraceMaterialClassifierFrame();
        MaybeDumpPathTraceMaterialDeclSurfaceTypeDistribution();
    }
    if (!enabled || !viewDef || !viewDef->renderWorld)
    {
        timing.metadataMs = Sys_Milliseconds() - metadataStartMs;
        return timing;
    }

    std::vector<uint32_t> registeredMaterialIds;
    registeredMaterialIds.reserve(256);
    idRenderWorldLocal* renderWorld = viewDef->renderWorld;
    for (int entityIndex = 0; entityIndex < renderWorld->entityDefs.Num(); ++entityIndex)
    {
        const idRenderEntityLocal* entity = renderWorld->entityDefs[entityIndex];
        const idRenderModel* model = entity ? entity->parms.hModel : nullptr;
        const bool isStaticWorldModel = model && model->IsStaticWorldModel();
        const renderEntity_t* renderEntity = entity ? &entity->parms : nullptr;
        const bool isRigidEntityModel =
            !isStaticWorldModel &&
            r_pathTracingSceneSource2RigidEntities.GetInteger() != 0 &&
            entity &&
            renderEntity &&
            model &&
            model->IsDynamicModel() == DM_STATIC &&
            renderEntity->joints == nullptr &&
            renderEntity->numJoints <= 0 &&
            renderEntity->callback == nullptr &&
            renderEntity->forceUpdate == 0 &&
            !renderEntity->weaponDepthHack &&
            renderEntity->modelDepthHack == 0.0f;
        if (!model || (!isStaticWorldModel && !isRigidEntityModel))
        {
            continue;
        }

        for (int surfaceIndex = 0; surfaceIndex < model->NumSurfaces(); ++surfaceIndex)
        {
            const modelSurface_t* surface = model->Surface(surfaceIndex);
            const idMaterial* baseMaterial = surface ? surface->shader : nullptr;
            const idMaterial* material = baseMaterial;
            if (entity && baseMaterial && entity->parms.customShader != nullptr)
            {
                material = baseMaterial->Deform() ? nullptr : entity->parms.customShader;
            }
            else if (entity && baseMaterial && entity->parms.customSkin)
            {
                material = entity->parms.customSkin->RemapShaderBySkin(baseMaterial);
            }
            if (!material)
            {
                continue;
            }

            const uint32_t materialId = SmokeMaterialId(material);
            if (std::find(registeredMaterialIds.begin(), registeredMaterialIds.end(), materialId) != registeredMaterialIds.end())
            {
                ++g_smokeMaterialMetadataFrameStats.duplicateSkips;
                continue;
            }

            registeredMaterialIds.push_back(materialId);
            const int registrationStartMs = Sys_Milliseconds();
            RegisterSmokeMaterialTextureInfo(material);
            timing.registrationMs += Sys_Milliseconds() - registrationStartMs;
        }
    }

    timing.metadataMs = Sys_Milliseconds() - metadataStartMs;
    return timing;
}
