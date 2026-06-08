#include "precompiled.h"
#pragma hdrstop

#include "../BinaryImage.h"
#include "../DXT/DXTCodec.h"
#include "PathTraceCVars.h"
#include "PathTraceMaterialClassifier.h"

#include <cstring>
#include <cmath>
#include <string>
#include <unordered_map>

namespace {

struct RtMaterialRecordKey
{
    uint32_t materialId = 0;
    uint64 materialNameHash = 0;

    bool operator==(const RtMaterialRecordKey& rhs) const
    {
        return materialId == rhs.materialId && materialNameHash == rhs.materialNameHash;
    }
};

struct RtMaterialRecordKeyHash
{
    size_t operator()(const RtMaterialRecordKey& key) const
    {
        uint64 hash = key.materialNameHash;
        hash ^= static_cast<uint64>(key.materialId) + 0x9e3779b97f4a7c15ull + (hash << 6) + (hash >> 2);
        return static_cast<size_t>(hash ^ (hash >> 32));
    }
};

struct RtMaterialClassTokenRule
{
    const char* token = nullptr;
    RtMaterialSurfaceClass surfaceClass = RtMaterialSurfaceClass::Unknown;
    bool matchFullText = false;
};

struct RtMaterialClassCandidate
{
    RtMaterialSurfaceClass surfaceClass = RtMaterialSurfaceClass::Unknown;
    RtMaterialSurfaceClassReason reason = RtMaterialSurfaceClassReason::Unknown;
    RtMaterialClassConfidence confidence = RtMaterialClassConfidence::FallbackNone;
    idStr evidence;
};

std::unordered_map<RtMaterialRecordKey, RtMaterialRecord, RtMaterialRecordKeyHash> g_materialRecords;
RtMaterialClassifierStats g_materialClassifierStats;
bool g_dumpedDeclSurfaceDistribution = false;
int g_recordDebugLogs = 0;
uint32_t g_materialClassifierGeneration = 1u;

struct RtSpecularAverageCacheEntry
{
    bool valid = false;
    float rgb[3] = { 0.0f, 0.0f, 0.0f };
    idStr evidence;
};

std::unordered_map<std::string, RtSpecularAverageCacheEntry> g_specularAverageCache;

uint64 HashRtMaterialValue(uint64 hash, uint64 value)
{
    hash ^= value + 0x9e3779b97f4a7c15ull + (hash << 6) + (hash >> 2);
    return hash;
}

uint64 HashRtMaterialFloat(uint64 hash, float value)
{
    uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    return HashRtMaterialValue(hash, bits);
}

uint64 HashRtMaterialString(uint64 hash, const idStr& value)
{
    const char* cursor = value.c_str();
    while (*cursor)
    {
        hash = HashRtMaterialValue(hash, static_cast<uint8_t>(*cursor));
        ++cursor;
    }
    return HashRtMaterialValue(hash, 0xffu);
}

RtMaterialRecordKey BuildRtMaterialRecordKey(uint32_t materialId, const idStr& materialName)
{
    RtMaterialRecordKey key;
    key.materialId = materialId;
    key.materialNameHash = HashRtMaterialString(1469598103934665603ull, materialName);
    return key;
}

bool RtMaterialTextHasToken(const idStr& text, const char* token)
{
    return token && token[0] && text.Find(token, false) >= 0;
}

idStr RtMaterialLeafName(const idStr& text)
{
    const char* source = text.c_str();
    const char* leaf = source;
    for (const char* cursor = source; *cursor; ++cursor)
    {
        if (*cursor == '/' || *cursor == '\\')
        {
            leaf = cursor + 1;
        }
    }
    return idStr(leaf);
}

RtMaterialSurfaceClass RtSurfaceClassFromTokenRules(
    const idStr& text,
    const RtMaterialClassTokenRule* rules,
    int ruleCount,
    const char* sourceName,
    idStr& evidence)
{
    if (text.IsEmpty())
    {
        return RtMaterialSurfaceClass::Unknown;
    }

    const idStr leaf = RtMaterialLeafName(text);
    for (int ruleIndex = 0; ruleIndex < ruleCount; ++ruleIndex)
    {
        const RtMaterialClassTokenRule& rule = rules[ruleIndex];
        const idStr& matchText = rule.matchFullText ? text : leaf;
        if (RtMaterialTextHasToken(matchText, rule.token))
        {
            evidence = va("%s:%s", sourceName, rule.token);
            return rule.surfaceClass;
        }
    }
    return RtMaterialSurfaceClass::Unknown;
}

bool RtStageBlendUsesSourceAlpha(const shaderStage_t* stage)
{
    if (!stage)
    {
        return false;
    }
    const uint64 srcBlend = stage->drawStateBits & GLS_SRCBLEND_BITS;
    const uint64 dstBlend = stage->drawStateBits & GLS_DSTBLEND_BITS;
    return srcBlend == GLS_SRCBLEND_SRC_ALPHA ||
        srcBlend == GLS_SRCBLEND_ONE_MINUS_SRC_ALPHA ||
        dstBlend == GLS_DSTBLEND_SRC_ALPHA ||
        dstBlend == GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA;
}

bool RtStageIsAdditiveBlend(const shaderStage_t* stage)
{
    if (!stage)
    {
        return false;
    }
    const uint64 srcBlend = stage->drawStateBits & GLS_SRCBLEND_BITS;
    const uint64 dstBlend = stage->drawStateBits & GLS_DSTBLEND_BITS;
    return (srcBlend == GLS_SRCBLEND_ONE || srcBlend == GLS_SRCBLEND_SRC_ALPHA) && dstBlend == GLS_DSTBLEND_ONE;
}

bool RtStageIsFilterBlend(const shaderStage_t* stage)
{
    if (!stage)
    {
        return false;
    }
    const uint64 srcBlend = stage->drawStateBits & GLS_SRCBLEND_BITS;
    const uint64 dstBlend = stage->drawStateBits & GLS_DSTBLEND_BITS;
    return (srcBlend == GLS_SRCBLEND_DST_COLOR && dstBlend == GLS_DSTBLEND_ZERO) ||
        (srcBlend == GLS_SRCBLEND_ZERO && dstBlend == GLS_DSTBLEND_SRC_COLOR) ||
        (srcBlend == GLS_SRCBLEND_ZERO && dstBlend == GLS_DSTBLEND_ONE_MINUS_SRC_COLOR) ||
        (srcBlend == GLS_SRCBLEND_ONE_MINUS_DST_COLOR && dstBlend == GLS_DSTBLEND_ONE);
}

bool RtStageConditionCanBeActive(const idMaterial* material, const shaderStage_t* stage)
{
    if (!material || !stage)
    {
        return false;
    }

    const float* constantRegisters = material->ConstantRegisters();
    const int registerCount = material->GetNumRegisters();
    if (constantRegisters && stage->conditionRegister >= 0 && stage->conditionRegister < registerCount)
    {
        return constantRegisters[stage->conditionRegister] != 0.0f;
    }
    return true;
}

float RtStageConstantRegisterValue(const idMaterial* material, int registerIndex, float fallback)
{
    if (!material)
    {
        return fallback;
    }

    const float* constantRegisters = material->ConstantRegisters();
    const int registerCount = material->GetNumRegisters();
    if (constantRegisters && registerIndex >= 0 && registerIndex < registerCount)
    {
        return constantRegisters[registerIndex];
    }
    return fallback;
}

const char* RtStageLightingName(stageLighting_t lighting)
{
    switch (lighting)
    {
        case SL_AMBIENT:
            return "ambient";
        case SL_BUMP:
            return "bump";
        case SL_DIFFUSE:
            return "diffuse";
        case SL_SPECULAR:
            return "specular";
        case SL_COVERAGE:
            return "coverage";
        default:
            return "unknown";
    }
}

const char* RtTextureUsageName(textureUsage_t usage)
{
    switch (usage)
    {
        case TD_SPECULAR:
            return "TD_SPECULAR";
        case TD_DIFFUSE:
            return "TD_DIFFUSE";
        case TD_BUMP:
            return "TD_BUMP";
        case TD_COVERAGE:
            return "TD_COVERAGE";
        case TD_SPECULAR_PBR_RMAO:
            return "TD_SPECULAR_PBR_RMAO";
        case TD_SPECULAR_PBR_RMAOD:
            return "TD_SPECULAR_PBR_RMAOD";
        case TD_DEFAULT:
            return "TD_DEFAULT";
        default:
            return "TD_OTHER";
    }
}

const char* RtTextureColorFormatName(textureColor_t format)
{
    switch (format)
    {
        case CFM_DEFAULT:
            return "CFM_DEFAULT";
        case CFM_NORMAL_DXT5:
            return "CFM_NORMAL_DXT5";
        case CFM_YCOCG_DXT5:
            return "CFM_YCOCG_DXT5";
        case CFM_GREEN_ALPHA:
            return "CFM_GREEN_ALPHA";
        default:
            return "CFM_UNKNOWN";
    }
}

RtMaterialStageFacts AnalyzeRtMaterialStages(const idMaterial* material)
{
    RtMaterialStageFacts facts;
    if (!material)
    {
        return facts;
    }

    facts.stageCount = material->GetNumStages();
    for (int stageIndex = 0; stageIndex < material->GetNumStages(); ++stageIndex)
    {
        const shaderStage_t* stage = material->GetStage(stageIndex);
        if (!stage || !RtStageConditionCanBeActive(material, stage))
        {
            continue;
        }

        idImage* image = stage->texture.image;
        const textureUsage_t usage = image ? image->GetUsage() : TD_DEFAULT;
        const bool additiveBlend = RtStageIsAdditiveBlend(stage);
        const bool filterBlend = RtStageIsFilterBlend(stage);
        const bool alphaBlend = RtStageBlendUsesSourceAlpha(stage);
        const bool guiOrScreen =
            stage->texture.texgen == TG_SCREEN ||
            stage->texture.texgen == TG_SCREEN2 ||
            stage->texture.dynamic == DI_GUI_RENDER ||
            stage->texture.dynamic == DI_RENDER_TARGET;
        const bool cubeMap =
            stage->texture.texgen == TG_SKYBOX_CUBE ||
            stage->texture.texgen == TG_WOBBLESKY_CUBE ||
            stage->texture.texgen == TG_REFLECT_CUBE ||
            stage->texture.texgen == TG_REFLECT_CUBE2;

        if (additiveBlend)
        {
            ++facts.additiveBlendStages;
        }
        if (filterBlend)
        {
            ++facts.filterBlendStages;
        }
        if (alphaBlend)
        {
            ++facts.alphaBlendStages;
        }
        if (guiOrScreen)
        {
            ++facts.guiOrScreenStages;
        }
        if (stage->texture.dynamic != DI_STATIC)
        {
            ++facts.dynamicImageStages;
        }
        if (stage->texture.cinematic != nullptr)
        {
            ++facts.cinematicStages;
        }
        if (cubeMap)
        {
            ++facts.cubeMapStages;
        }
        if (stage->newStage != nullptr)
        {
            ++facts.customProgramStages;
        }

        switch (stage->lighting)
        {
            case SL_BUMP:
                ++facts.bumpStages;
                if (facts.firstBumpStage < 0)
                {
                    facts.firstBumpStage = stageIndex;
                }
                break;
            case SL_DIFFUSE:
                ++facts.diffuseStages;
                if (facts.firstDiffuseStage < 0)
                {
                    facts.firstDiffuseStage = stageIndex;
                }
                break;
            case SL_SPECULAR:
                ++facts.specularStages;
                if (facts.firstSpecularStage < 0)
                {
                    facts.firstSpecularStage = stageIndex;
                }
                if (usage == TD_SPECULAR_PBR_RMAO || usage == TD_SPECULAR_PBR_RMAOD)
                {
                    ++facts.pbrRmaoStages;
                    if (facts.routeStage < 0)
                    {
                        facts.routeStage = stageIndex;
                    }
                }
                else if (image != nullptr)
                {
                    ++facts.legacySpecStages;
                    if (facts.routeStage < 0)
                    {
                        facts.routeStage = stageIndex;
                    }
                }
                break;
            case SL_COVERAGE:
                ++facts.coverageStages;
                break;
            case SL_AMBIENT:
                ++facts.ambientStages;
                break;
            default:
                break;
        }

        if (stage->lighting == SL_AMBIENT || additiveBlend || filterBlend || alphaBlend || guiOrScreen || cubeMap || stage->newStage != nullptr || stage->texture.cinematic != nullptr)
        {
            ++facts.effectStages;
        }
    }
    return facts;
}

RtMaterialSurfaceClass RtSurfaceClassFromSurfaceType(surfTypes_t surfaceType)
{
    switch (surfaceType)
    {
        case SURFTYPE_METAL:
            return RtMaterialSurfaceClass::Metal;
        case SURFTYPE_STONE:
            return RtMaterialSurfaceClass::Stone;
        case SURFTYPE_FLESH:
            return RtMaterialSurfaceClass::Flesh;
        case SURFTYPE_WOOD:
            return RtMaterialSurfaceClass::Wood;
        case SURFTYPE_CARDBOARD:
            return RtMaterialSurfaceClass::Cardboard;
        case SURFTYPE_LIQUID:
            return RtMaterialSurfaceClass::Liquid;
        case SURFTYPE_GLASS:
            return RtMaterialSurfaceClass::Glass;
        case SURFTYPE_PLASTIC:
            return RtMaterialSurfaceClass::Plastic;
        case SURFTYPE_RICOCHET:
            return RtMaterialSurfaceClass::Ricochet;
        default:
            return RtMaterialSurfaceClass::Unknown;
    }
}

RtMaterialSurfaceClass RtSurfaceClassFromNameFallback(const idStr& materialName, idStr& evidence)
{
    static const RtMaterialClassTokenRule rules[] = {
        { "textures/decals/", RtMaterialSurfaceClass::Special, true },
        { "textures/decals2/", RtMaterialSurfaceClass::Special, true },
        { "textures/decalsd3xp/", RtMaterialSurfaceClass::Special, true },
        { "textures/skies/", RtMaterialSurfaceClass::Special, true },
        { "textures/sfx/", RtMaterialSurfaceClass::Special, true },
        { "textures/sfxd3xp/", RtMaterialSurfaceClass::Special, true },
        { "textures/particles/", RtMaterialSurfaceClass::Special, true },
        { "textures/particlesd3xp/", RtMaterialSurfaceClass::Special, true },
        { "textures/base_light/", RtMaterialSurfaceClass::Special, true },
        { "particles/", RtMaterialSurfaceClass::Special, true },
        { "lights/", RtMaterialSurfaceClass::Special, true },
        { "lights\\", RtMaterialSurfaceClass::Special, true },
        { "guiobjects", RtMaterialSurfaceClass::Special, true },
        { "guis/", RtMaterialSurfaceClass::Special, true },
        { "guis\\", RtMaterialSurfaceClass::Special, true },
        { "models/mapobjects/signs/", RtMaterialSurfaceClass::Special, true },
        { "models\\mapobjects\\signs\\", RtMaterialSurfaceClass::Special, true },
        { "symbol_letters", RtMaterialSurfaceClass::Special },
        { "spectrumdecal", RtMaterialSurfaceClass::Special },
        { "hologram", RtMaterialSurfaceClass::Special },
        { "emerlight", RtMaterialSurfaceClass::Special },
        { "decal", RtMaterialSurfaceClass::Special },
        { "splat", RtMaterialSurfaceClass::Special },
        { "stain", RtMaterialSurfaceClass::Special },
        { "bloodpool", RtMaterialSurfaceClass::Special },
        { "flare", RtMaterialSurfaceClass::Special },
        { "glow", RtMaterialSurfaceClass::Special },

        { "glass", RtMaterialSurfaceClass::Glass },
        { "window", RtMaterialSurfaceClass::Glass },
        { "visor", RtMaterialSurfaceClass::Glass },
        { "screen", RtMaterialSurfaceClass::Glass },
        { "transparent", RtMaterialSurfaceClass::Glass },

        { "keycard", RtMaterialSurfaceClass::Plastic },
        { "keyboard", RtMaterialSurfaceClass::Plastic },
        { "computer", RtMaterialSurfaceClass::Plastic },
        { "monitor", RtMaterialSurfaceClass::Plastic },
        { "deskcomp", RtMaterialSurfaceClass::Plastic },
        { "laptop", RtMaterialSurfaceClass::Plastic },
        { "phone", RtMaterialSurfaceClass::Plastic },
        { "/pda", RtMaterialSurfaceClass::Plastic, true },
        { "/cd", RtMaterialSurfaceClass::Plastic },
        { "medkit", RtMaterialSurfaceClass::Plastic },
        { "plastic", RtMaterialSurfaceClass::Plastic },
        { "poly", RtMaterialSurfaceClass::Plastic },
        { "hose", RtMaterialSurfaceClass::Plastic },
        { "foamcup", RtMaterialSurfaceClass::Plastic },
        { "cola", RtMaterialSurfaceClass::Plastic },

        { "cardboard", RtMaterialSurfaceClass::Cardboard },
        { "paper", RtMaterialSurfaceClass::Cardboard },
        { "magpage", RtMaterialSurfaceClass::Cardboard },
        { "noteboard", RtMaterialSurfaceClass::Cardboard },
        { "calendar", RtMaterialSurfaceClass::Cardboard },
        { "binder", RtMaterialSurfaceClass::Cardboard },

        { "flesh", RtMaterialSurfaceClass::Flesh },
        { "skin", RtMaterialSurfaceClass::Flesh },
        { "blood", RtMaterialSurfaceClass::Flesh },
        { "character", RtMaterialSurfaceClass::Flesh },
        { "/characters/", RtMaterialSurfaceClass::Flesh, true },
        { "\\characters\\", RtMaterialSurfaceClass::Flesh, true },

        { "wood", RtMaterialSurfaceClass::Wood },
        { "plank", RtMaterialSurfaceClass::Wood },

        { "liquid", RtMaterialSurfaceClass::Liquid },
        { "water", RtMaterialSurfaceClass::Liquid },
        { "slime", RtMaterialSurfaceClass::Liquid },
        { "fluid", RtMaterialSurfaceClass::Liquid },

        { "metal", RtMaterialSurfaceClass::Metal },
        { "textures/mcity/", RtMaterialSurfaceClass::Metal, true },
        { "textures/base_floor/", RtMaterialSurfaceClass::Metal, true },
        { "textures/base_wall/", RtMaterialSurfaceClass::Metal, true },
        { "textures/basewall/", RtMaterialSurfaceClass::Metal, true },
        { "textures/base_trim/", RtMaterialSurfaceClass::Metal, true },
        { "textures/basetrim/", RtMaterialSurfaceClass::Metal, true },
        { "textures/base_door/", RtMaterialSurfaceClass::Metal, true },
        { "textures/basedoor/", RtMaterialSurfaceClass::Metal, true },
        { "textures/alphalabs/", RtMaterialSurfaceClass::Metal, true },
        { "textures/enpro/", RtMaterialSurfaceClass::Metal, true },
        { "textures/outside/", RtMaterialSurfaceClass::Metal, true },
        { "textures/recycle_floor/", RtMaterialSurfaceClass::Metal, true },
        { "textures/recycle_wall/", RtMaterialSurfaceClass::Metal, true },
        { "models/mapobjects/enpro/", RtMaterialSurfaceClass::Metal, true },
        { "models/mapobjects/doors/", RtMaterialSurfaceClass::Metal, true },
        { "models/mapobjects/skmachines/", RtMaterialSurfaceClass::Metal, true },
        { "models/mapobjects/swinglights/", RtMaterialSurfaceClass::Metal, true },
        { "steel", RtMaterialSurfaceClass::Metal },
        { "silver", RtMaterialSurfaceClass::Metal },
        { "gendesk", RtMaterialSurfaceClass::Metal },
        { "desk", RtMaterialSurfaceClass::Metal },
        { "table", RtMaterialSurfaceClass::Metal },
        { "counter", RtMaterialSurfaceClass::Metal },
        { "fridge", RtMaterialSurfaceClass::Metal },
        { "refrigerator", RtMaterialSurfaceClass::Metal },
        { "freezer", RtMaterialSurfaceClass::Metal },
        { "sink", RtMaterialSurfaceClass::Metal },
        { "fixture", RtMaterialSurfaceClass::Metal },
        { "lamp", RtMaterialSurfaceClass::Metal },
        { "offcab", RtMaterialSurfaceClass::Metal },
        { "sopbox", RtMaterialSurfaceClass::Metal },
        { "tbox", RtMaterialSurfaceClass::Metal },
        { "metalcrate", RtMaterialSurfaceClass::Metal },
        { "metal_crate", RtMaterialSurfaceClass::Metal },
        { "artifactcrate", RtMaterialSurfaceClass::Metal },
        { "artifact_crate", RtMaterialSurfaceClass::Metal },
        { "artifactcrates", RtMaterialSurfaceClass::Metal },
        { "artifact_crates", RtMaterialSurfaceClass::Metal },
        { "cube", RtMaterialSurfaceClass::Metal },
        { "diamondbox", RtMaterialSurfaceClass::Metal },
        { "pipe", RtMaterialSurfaceClass::Metal },
        { "grate", RtMaterialSurfaceClass::Metal },
        { "plate", RtMaterialSurfaceClass::Metal },
        { "panel", RtMaterialSurfaceClass::Metal },
        { "sflpanel", RtMaterialSurfaceClass::Metal },
        { "doortrim", RtMaterialSurfaceClass::Metal },
        { "doorframe", RtMaterialSurfaceClass::Metal },
        { "column", RtMaterialSurfaceClass::Metal },
        { "trim", RtMaterialSurfaceClass::Metal },
        { "vent", RtMaterialSurfaceClass::Metal },
        { "rack", RtMaterialSurfaceClass::Metal },
        { "cabinet", RtMaterialSurfaceClass::Metal },
        { "filecabinet", RtMaterialSurfaceClass::Metal },
        { "cart", RtMaterialSurfaceClass::Metal },
        { "heater", RtMaterialSurfaceClass::Metal },
        { "machine", RtMaterialSurfaceClass::Metal },
        { "wheels", RtMaterialSurfaceClass::Metal },
        { "chair", RtMaterialSurfaceClass::Metal },
        { "stool", RtMaterialSurfaceClass::Metal },
        { "seat", RtMaterialSurfaceClass::Metal },

        { "stone", RtMaterialSurfaceClass::Stone },
        { "rock", RtMaterialSurfaceClass::Stone },
        { "concrete", RtMaterialSurfaceClass::Stone },
        { "brick", RtMaterialSurfaceClass::Stone }
    };
    return RtSurfaceClassFromTokenRules(materialName, rules, sizeof(rules) / sizeof(rules[0]), "material", evidence);
}

RtMaterialSurfaceClass RtSurfaceClassFromImageNameFallback(const RtSmokeMaterialTextureInfo& info, idStr& evidence)
{
    RtMaterialSurfaceClass surfaceClass = RtSurfaceClassFromNameFallback(info.diffuseImageName, evidence);
    if (surfaceClass != RtMaterialSurfaceClass::Unknown)
    {
        const idStr matchedEvidence = evidence;
        evidence = va("diffuseImage:%s", matchedEvidence.c_str());
        return surfaceClass;
    }
    surfaceClass = RtSurfaceClassFromNameFallback(info.normalImageName, evidence);
    if (surfaceClass != RtMaterialSurfaceClass::Unknown)
    {
        const idStr matchedEvidence = evidence;
        evidence = va("normalImage:%s", matchedEvidence.c_str());
        return surfaceClass;
    }
    surfaceClass = RtSurfaceClassFromNameFallback(info.specularImageName, evidence);
    if (surfaceClass != RtMaterialSurfaceClass::Unknown)
    {
        const idStr matchedEvidence = evidence;
        evidence = va("specularImage:%s", matchedEvidence.c_str());
        return surfaceClass;
    }
    surfaceClass = RtSurfaceClassFromNameFallback(info.emissiveImageName, evidence);
    if (surfaceClass != RtMaterialSurfaceClass::Unknown)
    {
        const idStr matchedEvidence = evidence;
        evidence = va("emissiveImage:%s", matchedEvidence.c_str());
        return surfaceClass;
    }
    evidence = "";
    return RtMaterialSurfaceClass::Unknown;
}

bool RtMaterialTextHasNonMetalOverrideToken(const idStr& text)
{
    static const char* nonMetalTokens[] = {
        "textures/decals/",
        "textures/decals2/",
        "textures/decalsd3xp/",
        "textures/skies/",
        "textures/sfx/",
        "textures/sfxd3xp/",
        "textures/particles/",
        "textures/particlesd3xp/",
        "textures/base_light/",
        "particles/",
        "lights/",
        "lights\\",
        "guiobjects",
        "guis/",
        "guis\\",
        "glass",
        "window",
        "visor",
        "screen",
        "transparent"
    };

    for (int tokenIndex = 0; tokenIndex < static_cast<int>(sizeof(nonMetalTokens) / sizeof(nonMetalTokens[0])); ++tokenIndex)
    {
        if (RtMaterialTextHasToken(text, nonMetalTokens[tokenIndex]))
        {
            return true;
        }
    }

    return false;
}

bool RtIndustrialMetalFamilyFromNames(const RtSmokeMaterialTextureInfo& info, idStr& evidence)
{
    static const RtMaterialClassTokenRule rules[] = {
        { "textures/mcity/", RtMaterialSurfaceClass::Metal, true },
        { "textures/base_floor/", RtMaterialSurfaceClass::Metal, true },
        { "textures/base_wall/", RtMaterialSurfaceClass::Metal, true },
        { "textures/basewall/", RtMaterialSurfaceClass::Metal, true },
        { "textures/base_trim/", RtMaterialSurfaceClass::Metal, true },
        { "textures/basetrim/", RtMaterialSurfaceClass::Metal, true },
        { "textures/base_door/", RtMaterialSurfaceClass::Metal, true },
        { "textures/basedoor/", RtMaterialSurfaceClass::Metal, true },
        { "textures/alphalabs/", RtMaterialSurfaceClass::Metal, true },
        { "textures/enpro/", RtMaterialSurfaceClass::Metal, true },
        { "textures/outside/", RtMaterialSurfaceClass::Metal, true },
        { "textures/recycle_floor/", RtMaterialSurfaceClass::Metal, true },
        { "textures/recycle_wall/", RtMaterialSurfaceClass::Metal, true },
        { "models/mapobjects/enpro/", RtMaterialSurfaceClass::Metal, true },
        { "models/mapobjects/doors/", RtMaterialSurfaceClass::Metal, true }
    };

    const idStr* fields[] = {
        &info.materialName,
        &info.diffuseImageName,
        &info.normalImageName,
        &info.specularImageName,
        &info.emissiveImageName
    };
    static const char* fieldNames[] = {
        "material",
        "diffuseImage",
        "normalImage",
        "specularImage",
        "emissiveImage"
    };

    for (int fieldIndex = 0; fieldIndex < 5; ++fieldIndex)
    {
        if (fields[fieldIndex]->IsEmpty() || RtMaterialTextHasNonMetalOverrideToken(*fields[fieldIndex]))
        {
            continue;
        }

        idStr familyEvidence;
        const RtMaterialSurfaceClass surfaceClass = RtSurfaceClassFromTokenRules(*fields[fieldIndex], rules, sizeof(rules) / sizeof(rules[0]), fieldNames[fieldIndex], familyEvidence);
        if (surfaceClass == RtMaterialSurfaceClass::Metal)
        {
            evidence = familyEvidence;
            return true;
        }
    }

    return false;
}

bool RtCardboardSurfaceHasMetalPropEvidence(const RtSmokeMaterialTextureInfo& info, idStr& evidence)
{
    static const char* metalBoxTokens[] = {
        "tbox",
        "sopbox",
        "metalcrate",
        "metal_crate",
        "artifactcrate",
        "artifact_crate",
        "artifactcrates",
        "artifact_crates"
    };

    const idStr* fields[] = {
        &info.materialName,
        &info.diffuseImageName,
        &info.normalImageName,
        &info.specularImageName
    };
    static const char* fieldNames[] = {
        "material",
        "diffuseImage",
        "normalImage",
        "specularImage"
    };

    for (int fieldIndex = 0; fieldIndex < 4; ++fieldIndex)
    {
        for (int tokenIndex = 0; tokenIndex < static_cast<int>(sizeof(metalBoxTokens) / sizeof(metalBoxTokens[0])); ++tokenIndex)
        {
            if (RtMaterialTextHasToken(*fields[fieldIndex], metalBoxTokens[tokenIndex]))
            {
                evidence = va("surfaceType:cardboard override:%s:%s", fieldNames[fieldIndex], metalBoxTokens[tokenIndex]);
                return true;
            }
        }
    }

    return false;
}

bool RtCharacterFleshFromNames(const RtSmokeMaterialTextureInfo& info, idStr& evidence)
{
    const idStr* fields[] = {
        &info.materialName,
        &info.diffuseImageName,
        &info.normalImageName,
        &info.specularImageName,
        &info.emissiveImageName
    };
    static const char* fieldNames[] = {
        "material",
        "diffuseImage",
        "normalImage",
        "specularImage",
        "emissiveImage"
    };
    static const char* characterTokens[] = {
        "/characters/",
        "\\characters\\",
        "models/characters/",
        "models\\characters\\"
    };

    for (int fieldIndex = 0; fieldIndex < 5; ++fieldIndex)
    {
        if (fields[fieldIndex]->IsEmpty())
        {
            continue;
        }
        for (int tokenIndex = 0; tokenIndex < static_cast<int>(sizeof(characterTokens) / sizeof(characterTokens[0])); ++tokenIndex)
        {
            if (RtMaterialTextHasToken(*fields[fieldIndex], characterTokens[tokenIndex]))
            {
                evidence = va("%s:%s", fieldNames[fieldIndex], characterTokens[tokenIndex]);
                return true;
            }
        }
    }

    return false;
}

RtMaterialSurfaceClass RtExplicitNonMetalClassFromNames(const RtSmokeMaterialTextureInfo& info, idStr& evidence)
{
    const idStr* fields[] = {
        &info.materialName,
        &info.diffuseImageName,
        &info.normalImageName,
        &info.specularImageName,
        &info.emissiveImageName
    };
    static const char* fieldNames[] = {
        "material",
        "diffuseImage",
        "normalImage",
        "specularImage",
        "emissiveImage"
    };

    for (int fieldIndex = 0; fieldIndex < 5; ++fieldIndex)
    {
        if (fields[fieldIndex]->IsEmpty())
        {
            continue;
        }

        idStr fieldEvidence;
        const RtMaterialSurfaceClass surfaceClass = RtSurfaceClassFromNameFallback(*fields[fieldIndex], fieldEvidence);
        if (surfaceClass != RtMaterialSurfaceClass::Unknown && surfaceClass != RtMaterialSurfaceClass::Metal)
        {
            evidence = va("%s:%s", fieldNames[fieldIndex], fieldEvidence.c_str());
            return surfaceClass;
        }
    }

    return RtMaterialSurfaceClass::Unknown;
}

bool RtFieldHasAnyToken(const idStr& text, const char* const* tokens, int tokenCount)
{
    if (text.IsEmpty())
    {
        return false;
    }

    for (int tokenIndex = 0; tokenIndex < tokenCount; ++tokenIndex)
    {
        if (RtMaterialTextHasToken(text, tokens[tokenIndex]))
        {
            return true;
        }
    }

    return false;
}

bool RtSpecialStageIntentFromNames(const RtSmokeMaterialTextureInfo& info, const RtMaterialStageFacts& stageFacts, idStr& evidence)
{
    const bool hasSpecialStageShape =
        info.emissive ||
        stageFacts.additiveBlendStages > 0 ||
        stageFacts.filterBlendStages > 0 ||
        stageFacts.alphaBlendStages > 0 ||
        stageFacts.coverageStages > 0;
    if (!hasSpecialStageShape)
    {
        return false;
    }

    static const char* specialFolderTokens[] = {
        "textures/decals/",
        "textures/decals2/",
        "textures/decalsd3xp/",
        "textures/sfx/",
        "textures/sfxd3xp/",
        "textures/particles/",
        "textures/particlesd3xp/",
        "textures/base_light/",
        "particles/",
        "lights/",
        "lights\\",
        "guis/",
        "guis\\"
    };
    static const char* effectLeafTokens[] = {
        "_add",
        "decal",
        "splat",
        "stain",
        "bloodpool",
        "light",
        "symbol",
        "letter",
        "flare",
        "glow",
        "beam",
        "bolt",
        "spark",
        "smoke",
        "steam",
        "fog",
        "fire",
        "flame",
        "plasma",
        "mflash",
        "muzzle",
        "sprite",
        "hologram",
        "volum"
    };

    const idStr* fields[] = {
        &info.materialName,
        &info.diffuseImageName,
        &info.normalImageName,
        &info.specularImageName,
        &info.emissiveImageName
    };
    static const char* fieldNames[] = {
        "material",
        "diffuseImage",
        "normalImage",
        "specularImage",
        "emissiveImage"
    };

    for (int fieldIndex = 0; fieldIndex < 5; ++fieldIndex)
    {
        if (RtFieldHasAnyToken(*fields[fieldIndex], specialFolderTokens, static_cast<int>(sizeof(specialFolderTokens) / sizeof(specialFolderTokens[0]))))
        {
            evidence = va("stageSpecial:%s:folder", fieldNames[fieldIndex]);
            return true;
        }
    }

    if (info.emissive &&
        RtFieldHasAnyToken(info.emissiveImageName, effectLeafTokens, static_cast<int>(sizeof(effectLeafTokens) / sizeof(effectLeafTokens[0]))))
    {
        evidence = "stageSpecial:emissiveImage";
        return true;
    }

    const bool hasTranslucentOrEffectBlend =
        stageFacts.additiveBlendStages > 0 ||
        stageFacts.filterBlendStages > 0 ||
        stageFacts.alphaBlendStages > 0 ||
        stageFacts.coverageStages > 0;
    if (hasTranslucentOrEffectBlend)
    {
        for (int fieldIndex = 0; fieldIndex < 5; ++fieldIndex)
        {
            const idStr leaf = RtMaterialLeafName(*fields[fieldIndex]);
            if (RtFieldHasAnyToken(leaf, effectLeafTokens, static_cast<int>(sizeof(effectLeafTokens) / sizeof(effectLeafTokens[0]))))
            {
                evidence = va("stageSpecial:%s:effectToken", fieldNames[fieldIndex]);
                return true;
            }
        }
    }

    return false;
}

bool RtMaterialSortIsDecal(const idMaterial* material)
{
    if (!material)
    {
        return false;
    }

    const float sort = material->GetSort();
    return sort >= SS_DECAL && sort < SS_FAR;
}

bool RtMaterialSpecialFromStageFacts(const RtMaterialStageFacts& stageFacts, idStr& evidence)
{
    if (stageFacts.guiOrScreenStages > 0)
    {
        evidence = "stage:guiScreen";
        return true;
    }
    if (stageFacts.cinematicStages > 0)
    {
        evidence = "stage:cinematic";
        return true;
    }
    if (stageFacts.cubeMapStages > 0)
    {
        evidence = "stage:cubeMap";
        return true;
    }
    if (stageFacts.customProgramStages > 0)
    {
        evidence = "stage:program";
        return true;
    }
    return false;
}

RtMaterialClassCandidate ResolveSurfaceClassCandidate(const idMaterial* material, const RtSmokeMaterialTextureInfo& info, const RtMaterialStageFacts& stageFacts)
{
    RtMaterialClassCandidate candidate;
    if (RtMaterialSortIsDecal(material))
    {
        candidate.surfaceClass = RtMaterialSurfaceClass::Special;
        candidate.reason = RtMaterialSurfaceClassReason::MaterialSort;
        candidate.confidence = RtMaterialClassConfidence::Authoritative;
        candidate.evidence = va("sort:%.2f", material ? material->GetSort() : SS_BAD);
        return candidate;
    }

    if (RtCharacterFleshFromNames(info, candidate.evidence))
    {
        candidate.surfaceClass = RtMaterialSurfaceClass::Flesh;
        candidate.reason = RtMaterialSurfaceClassReason::NameToken;
        candidate.confidence = RtMaterialClassConfidence::Heuristic;
        return candidate;
    }

    candidate.surfaceClass = RtExplicitNonMetalClassFromNames(info, candidate.evidence);
    if (candidate.surfaceClass != RtMaterialSurfaceClass::Unknown)
    {
        candidate.reason = RtMaterialSurfaceClassReason::NameToken;
        candidate.confidence = RtMaterialClassConfidence::Heuristic;
        return candidate;
    }

    if (RtSpecialStageIntentFromNames(info, stageFacts, candidate.evidence))
    {
        candidate.surfaceClass = RtMaterialSurfaceClass::Special;
        candidate.reason = RtMaterialSurfaceClassReason::StageKind;
        candidate.confidence = RtMaterialClassConfidence::Heuristic;
        return candidate;
    }

    const RtMaterialSurfaceClass surfaceTypeClass = RtSurfaceClassFromSurfaceType(material ? material->GetSurfaceType() : SURFTYPE_NONE);
    if (surfaceTypeClass == RtMaterialSurfaceClass::Flesh ||
        surfaceTypeClass == RtMaterialSurfaceClass::Glass ||
        surfaceTypeClass == RtMaterialSurfaceClass::Liquid)
    {
        candidate.surfaceClass = surfaceTypeClass;
        candidate.reason = RtMaterialSurfaceClassReason::SurfaceType;
        candidate.confidence = RtMaterialClassConfidence::Authoritative;
        candidate.evidence = va("surfaceType:%d", material ? static_cast<int>(material->GetSurfaceType()) : static_cast<int>(SURFTYPE_NONE));
        return candidate;
    }

    if (RtIndustrialMetalFamilyFromNames(info, candidate.evidence))
    {
        candidate.surfaceClass = RtMaterialSurfaceClass::Metal;
        candidate.reason = RtMaterialSurfaceClassReason::NameToken;
        candidate.confidence = RtMaterialClassConfidence::Heuristic;
        return candidate;
    }

    if (surfaceTypeClass != RtMaterialSurfaceClass::Unknown)
    {
        if (surfaceTypeClass == RtMaterialSurfaceClass::Cardboard && RtCardboardSurfaceHasMetalPropEvidence(info, candidate.evidence))
        {
            candidate.surfaceClass = RtMaterialSurfaceClass::Metal;
            candidate.reason = RtMaterialSurfaceClassReason::NameToken;
            candidate.confidence = RtMaterialClassConfidence::Heuristic;
            return candidate;
        }
        candidate.surfaceClass = surfaceTypeClass;
        candidate.reason = RtMaterialSurfaceClassReason::SurfaceType;
        candidate.confidence = RtMaterialClassConfidence::Authoritative;
        candidate.evidence = va("surfaceType:%d", material ? static_cast<int>(material->GetSurfaceType()) : static_cast<int>(SURFTYPE_NONE));
        return candidate;
    }

    if (RtMaterialSpecialFromStageFacts(stageFacts, candidate.evidence))
    {
        candidate.surfaceClass = RtMaterialSurfaceClass::Special;
        candidate.reason = RtMaterialSurfaceClassReason::StageKind;
        candidate.confidence = RtMaterialClassConfidence::Heuristic;
        return candidate;
    }

    candidate.surfaceClass = RtSurfaceClassFromNameFallback(info.materialName, candidate.evidence);
    if (candidate.surfaceClass != RtMaterialSurfaceClass::Unknown)
    {
        candidate.reason = RtMaterialSurfaceClassReason::NameToken;
        candidate.confidence = RtMaterialClassConfidence::Heuristic;
        return candidate;
    }

    candidate.surfaceClass = RtSurfaceClassFromImageNameFallback(info, candidate.evidence);
    if (candidate.surfaceClass != RtMaterialSurfaceClass::Unknown)
    {
        candidate.reason = RtMaterialSurfaceClassReason::ImageNameToken;
        candidate.confidence = RtMaterialClassConfidence::Heuristic;
        return candidate;
    }

    candidate.surfaceClass = RtMaterialSurfaceClass::Metal;
    candidate.reason = RtMaterialSurfaceClassReason::FallbackMetal;
    candidate.confidence = RtMaterialClassConfidence::FallbackNone;
    candidate.evidence = "fallback:doomIndustrialDefault";
    return candidate;
}

RtMaterialBsdfParams RtDefaultBsdfForSurfaceClass(RtMaterialSurfaceClass surfaceClass)
{
    RtMaterialBsdfParams bsdf;
    switch (surfaceClass)
    {
        case RtMaterialSurfaceClass::Metal:
            bsdf.metallic = 1.0f;
            bsdf.roughness = 0.25f;
            break;
        case RtMaterialSurfaceClass::Stone:
            bsdf.roughness = 0.85f;
            break;
        case RtMaterialSurfaceClass::Wood:
            bsdf.roughness = 0.75f;
            break;
        case RtMaterialSurfaceClass::Cardboard:
            bsdf.roughness = 0.95f;
            break;
        case RtMaterialSurfaceClass::Liquid:
            bsdf.roughness = 0.02f;
            bsdf.ior = 1.33f;
            bsdf.transmission = 0.9f;
            bsdf.twoSidedBsdf = 1;
            break;
        case RtMaterialSurfaceClass::Glass:
            bsdf.roughness = 0.05f;
            bsdf.ior = 1.5f;
            bsdf.transmission = 0.9f;
            bsdf.twoSidedBsdf = 1;
            break;
        case RtMaterialSurfaceClass::Plastic:
            bsdf.roughness = 0.45f;
            break;
        case RtMaterialSurfaceClass::Flesh:
            bsdf.roughness = 0.55f;
            bsdf.ior = 1.4f;
            bsdf.subsurfaceHint = 1;
            break;
        case RtMaterialSurfaceClass::Ricochet:
            bsdf.metallic = 1.0f;
            bsdf.roughness = 0.35f;
            break;
        case RtMaterialSurfaceClass::Special:
            bsdf.roughness = 0.7f;
            break;
        default:
            break;
    }
    return bsdf;
}

RtMaterialNormalDecodeMode ResolveNormalDecodeMode(const RtSmokeMaterialTextureInfo& info)
{
    if (!info.hasNormalImage)
    {
        return RtMaterialNormalDecodeMode::None;
    }
    if (r_pathTracingMatClassNormalDecodeMode.GetInteger() == 1)
    {
        return RtMaterialNormalDecodeMode::Rgb8Rg;
    }
    if (r_pathTracingMatClassNormalDecodeMode.GetInteger() == 2)
    {
        return RtMaterialNormalDecodeMode::CompressedWy;
    }
    return info.normalColorFormat == CFM_NORMAL_DXT5 ? RtMaterialNormalDecodeMode::CompressedWy : RtMaterialNormalDecodeMode::Rgb8Rg;
}

RtMaterialBsdfRoute ResolveBsdfRoute(const RtSmokeMaterialTextureInfo& info, const RtMaterialStageFacts& stageFacts, RtMaterialBsdfRouteReason& routeReason)
{
    (void)info;
    if (r_pathTracingMatClassUseRmao.GetInteger() != 0 && stageFacts.pbrRmaoStages > 0)
    {
        routeReason = RtMaterialBsdfRouteReason::PbrRmaoStage;
        return RtMaterialBsdfRoute::RealPbrRmao;
    }
    if (stageFacts.legacySpecStages > 0)
    {
        routeReason = RtMaterialBsdfRouteReason::LegacySpecularStage;
        return RtMaterialBsdfRoute::LegacySpecGloss;
    }
    if (info.hasSpecularImage &&
        info.specularUsage != TD_SPECULAR_PBR_RMAO &&
        info.specularUsage != TD_SPECULAR_PBR_RMAOD)
    {
        routeReason = RtMaterialBsdfRouteReason::DiscoveredLegacySpecularImage;
        return RtMaterialBsdfRoute::LegacySpecGloss;
    }
    if (stageFacts.pbrRmaoStages > 0)
    {
        routeReason = RtMaterialBsdfRouteReason::RmaoDisabled;
    }
    else
    {
        routeReason = RtMaterialBsdfRouteReason::NoCompatibleSpecularStage;
    }
    return RtMaterialBsdfRoute::SurfaceTypeFallback;
}

uint32_t QuantizeRtMaterialUnitFloat(float value)
{
    const float clamped = idMath::ClampFloat(0.0f, 1.0f, value);
    return static_cast<uint32_t>(clamped * 255.0f + 0.5f);
}

float RtSmokeLinear1(float value)
{
    return value <= 0.04045f ? value / 12.92f : std::pow((value + 0.055f) / 1.055f, 2.4f);
}

float RtSmokeSpecularLuma(const float rgb[3])
{
    return rgb[0] * 0.2125f + rgb[1] * 0.7154f + rgb[2] * 0.0721f;
}

void RtSmokePbrFromSpecmapCpu(const float specMap[3], float& outF0, float& outRoughness)
{
    const float specLum = RtSmokeSpecularLuma(specMap);
    float f0 = 0.04f;
    const float contrastMid = 0.214f;
    const float contrastAmount = 2.0f;
    float contrast = idMath::ClampFloat(0.0f, 1.0f, (specLum - contrastMid) / (1.0f - contrastMid));
    contrast += idMath::ClampFloat(0.0f, 1.0f, specLum / contrastMid) - 1.0f;
    contrast = std::pow(2.0f, contrastAmount * contrast);
    f0 *= contrast;
    const float linearBrightness = RtSmokeLinear1(2.0f * specLum);
    const float specPow = Max(0.0f, ((8.0f * linearBrightness) / Max(f0, 1.0e-4f)) - 2.0f);
    f0 *= Min(1.0f, linearBrightness / Max(f0 * 0.25f, 1.0e-4f));
    float roughness = std::sqrt(2.0f / (specPow + 2.0f));
    const float glossiness = idMath::ClampFloat(0.0f, 1.0f, 1.0f - roughness);
    const float metallic = glossiness >= 0.7f ? 1.0f : 0.0f;
    const float glossColor[3] = {
        RtSmokeLinear1(specMap[0]),
        RtSmokeLinear1(specMap[1]),
        RtSmokeLinear1(specMap[2])
    };
    const float glossF0 = RtSmokeSpecularLuma(glossColor);
    f0 = f0 * (1.0f - metallic) + glossF0 * metallic;
    roughness = std::sqrt(roughness);

    outF0 = idMath::ClampFloat(0.0f, 1.0f, f0);
    outRoughness = idMath::ClampFloat(0.02f, 1.0f, roughness);
}

bool RtSpecularRouteStageConstantColor(const idMaterial* material, const RtMaterialRecord& record, float color[3])
{
    color[0] = 1.0f;
    color[1] = 1.0f;
    color[2] = 1.0f;

    if (!material || record.stageFacts.routeStage < 0 || record.stageFacts.routeStage >= material->GetNumStages())
    {
        return false;
    }

    const float* constantRegisters = material->ConstantRegisters();
    const int registerCount = material->GetNumRegisters();
    const shaderStage_t* stage = material->GetStage(record.stageFacts.routeStage);
    if (!constantRegisters || registerCount <= 0 || !stage)
    {
        return false;
    }

    for (int component = 0; component < 3; ++component)
    {
        const int registerIndex = stage->color.registers[component];
        if (registerIndex < 0 || registerIndex >= registerCount)
        {
            return false;
        }
        color[component] = idMath::ClampFloat(0.0f, 1.0f, constantRegisters[registerIndex]);
    }
    return true;
}

void RtAverageRgbaPixels(const byte* pixels, int width, int height, int strideBytes, float outRgb[3])
{
    const int pixelCount = width * height;
    const int stride = Max(1, pixelCount / 4096);
    double sum[3] = { 0.0, 0.0, 0.0 };
    int samples = 0;
    for (int pixelIndex = 0; pixelIndex < pixelCount; pixelIndex += stride)
    {
        const byte* pixel = pixels + pixelIndex * strideBytes;
        sum[0] += static_cast<double>(pixel[0]) * (1.0 / 255.0);
        sum[1] += static_cast<double>(pixel[1]) * (1.0 / 255.0);
        sum[2] += static_cast<double>(pixel[2]) * (1.0 / 255.0);
        ++samples;
    }

    if (samples <= 0)
    {
        outRgb[0] = 0.0f;
        outRgb[1] = 0.0f;
        outRgb[2] = 0.0f;
        return;
    }

    outRgb[0] = static_cast<float>(sum[0] / samples);
    outRgb[1] = static_cast<float>(sum[1] / samples);
    outRgb[2] = static_cast<float>(sum[2] / samples);
}

bool RtAverageSourceImageProgramRgb(const idStr& imageName, float outRgb[3], idStr& evidence)
{
    if (imageName.IsEmpty() || imageName.Icmp("<none>") == 0 || imageName[0] == '_')
    {
        evidence = "specularAvg:invalidName";
        return false;
    }

    byte* pic = nullptr;
    int width = 0;
    int height = 0;
    textureUsage_t usage = TD_DEFAULT;
    R_LoadImageProgram(imageName.c_str(), &pic, &width, &height, nullptr, &usage);
    if (!pic || width <= 0 || height <= 0)
    {
        if (pic)
        {
            R_StaticFree(pic);
        }
        evidence = "sourceLoadFailed";
        return false;
    }

    RtAverageRgbaPixels(pic, width, height, 4, outRgb);
    R_StaticFree(pic);

    evidence = va("source:%dx%d/%d", width, height, Min(width * height, 4096));
    return true;
}

bool RtAverageGeneratedImageRgb(const RtMaterialRecord& record, float outRgb[3], idStr& evidence)
{
    idStr generatedName = record.specularImageName;
    idImage::GetGeneratedName(generatedName, record.specularUsage, CF_2D);

    idBinaryImage binaryImage(generatedName.c_str());
    if (binaryImage.LoadFromGeneratedFile(FILE_NOT_FOUND_TIMESTAMP) == FILE_NOT_FOUND_TIMESTAMP)
    {
        evidence = va("bimageMissing:%s", generatedName.c_str());
        return false;
    }

    if (binaryImage.NumImages() <= 0)
    {
        evidence = va("bimageNoImages:%s", generatedName.c_str());
        return false;
    }

    const bimageFile_t& fileHeader = binaryImage.GetFileHeader();
    const bimageImage_t& imageHeader = binaryImage.GetImageHeader(0);
    const byte* imageData = binaryImage.GetImageData(0);
    if (!imageData || imageHeader.width <= 0 || imageHeader.height <= 0)
    {
        evidence = va("bimageInvalid:%s", generatedName.c_str());
        return false;
    }

    const textureFormat_t format = static_cast<textureFormat_t>(fileHeader.format);
    if (format == FMT_DXT1 || format == FMT_DXT5)
    {
        idTempArray<byte> decoded(imageHeader.width * imageHeader.height * 4);
        idDxtDecoder decoder;
        if (format == FMT_DXT1)
        {
            decoder.DecompressImageDXT1(imageData, decoded.Ptr(), imageHeader.width, imageHeader.height);
        }
        else
        {
            decoder.DecompressImageDXT5(imageData, decoded.Ptr(), imageHeader.width, imageHeader.height);
        }
        RtAverageRgbaPixels(decoded.Ptr(), imageHeader.width, imageHeader.height, 4, outRgb);
        evidence = va("bimage:%s:%dx%d/%d", format == FMT_DXT1 ? "DXT1" : "DXT5", imageHeader.width, imageHeader.height, Min(imageHeader.width * imageHeader.height, 4096));
        return true;
    }

    if (format == FMT_RGBA8 || format == FMT_XRGB8)
    {
        RtAverageRgbaPixels(imageData, imageHeader.width, imageHeader.height, 4, outRgb);
        evidence = va("bimage:RGBA8:%dx%d/%d", imageHeader.width, imageHeader.height, Min(imageHeader.width * imageHeader.height, 4096));
        return true;
    }

    if (format == FMT_LUM8 || format == FMT_INT8 || format == FMT_ALPHA)
    {
        const int pixelCount = imageHeader.width * imageHeader.height;
        const int stride = Max(1, pixelCount / 4096);
        double sum = 0.0;
        int samples = 0;
        for (int pixelIndex = 0; pixelIndex < pixelCount; pixelIndex += stride)
        {
            sum += static_cast<double>(imageData[pixelIndex]) * (1.0 / 255.0);
            ++samples;
        }
        const float average = samples > 0 ? static_cast<float>(sum / samples) : 0.0f;
        outRgb[0] = average;
        outRgb[1] = average;
        outRgb[2] = average;
        evidence = va("bimage:L8:%dx%d/%d", imageHeader.width, imageHeader.height, samples);
        return true;
    }

    if (format == FMT_L8A8)
    {
        const int pixelCount = imageHeader.width * imageHeader.height;
        const int stride = Max(1, pixelCount / 4096);
        double sum = 0.0;
        int samples = 0;
        for (int pixelIndex = 0; pixelIndex < pixelCount; pixelIndex += stride)
        {
            sum += static_cast<double>(imageData[pixelIndex * 2]) * (1.0 / 255.0);
            ++samples;
        }
        const float average = samples > 0 ? static_cast<float>(sum / samples) : 0.0f;
        outRgb[0] = average;
        outRgb[1] = average;
        outRgb[2] = average;
        evidence = va("bimage:L8A8:%dx%d/%d", imageHeader.width, imageHeader.height, samples);
        return true;
    }

    evidence = va("bimageUnsupportedFormat:%d:%s", static_cast<int>(format), generatedName.c_str());
    return false;
}

bool RtAverageSpecularImageRgb(const RtMaterialRecord& record, float outRgb[3], idStr& evidence)
{
    const idStr& imageName = record.specularImageName;
    if (imageName.IsEmpty() || imageName.Icmp("<none>") == 0 || imageName[0] == '_')
    {
        evidence = "specularAvg:invalidName";
        return false;
    }

    const std::string cacheKey = va("%s#%d", imageName.c_str(), static_cast<int>(record.specularUsage));
    auto cached = g_specularAverageCache.find(cacheKey);
    if (cached != g_specularAverageCache.end())
    {
        outRgb[0] = cached->second.rgb[0];
        outRgb[1] = cached->second.rgb[1];
        outRgb[2] = cached->second.rgb[2];
        evidence = cached->second.evidence;
        return cached->second.valid;
    }

    RtSpecularAverageCacheEntry entry;
    idStr sourceEvidence;
    if (RtAverageSourceImageProgramRgb(imageName, entry.rgb, sourceEvidence))
    {
        entry.valid = true;
        entry.evidence = va("specularAvg:%s", sourceEvidence.c_str());
    }
    else
    {
        idStr binaryEvidence;
        if (RtAverageGeneratedImageRgb(record, entry.rgb, binaryEvidence))
        {
            entry.valid = true;
            entry.evidence = va("specularAvg:%s", binaryEvidence.c_str());
        }
        else
        {
            entry.valid = false;
            entry.evidence = va("specularAvg:%s/%s", sourceEvidence.c_str(), binaryEvidence.c_str());
        }
    }

    outRgb[0] = entry.rgb[0];
    outRgb[1] = entry.rgb[1];
    outRgb[2] = entry.rgb[2];
    evidence = entry.evidence;
    g_specularAverageCache[cacheKey] = entry;
    return entry.valid;
}

void ApplyRouteBBsdfPolicy(const idMaterial* material, RtMaterialRecord& record)
{
    if (record.route != RtMaterialBsdfRoute::LegacySpecGloss)
    {
        if (record.route == RtMaterialBsdfRoute::RealPbrRmao)
        {
            record.bsdfEvidence = "routeA:rmao";
        }
        else
        {
            record.bsdfEvidence = "routeC:classDefault";
        }
        return;
    }

    if (record.surfaceClass == RtMaterialSurfaceClass::Special && record.emissiveIntent)
    {
        record.bsdf = RtDefaultBsdfForSurfaceClass(RtMaterialSurfaceClass::Special);
        record.bsdfEvidence = "routeB:specialEmissiveDefaultNoSpecSample";
        return;
    }

    record.bsdfEvidence = "routeB:shaderPerPixelSpec";
}

uint64 ComputeRtMaterialRecordSignature(const idMaterial* material, const RtSmokeMaterialTextureInfo& info)
{
    uint64 hash = 1469598103934665603ull;
    hash = HashRtMaterialValue(hash, info.materialId);
    hash = HashRtMaterialString(hash, info.materialName);
    hash = HashRtMaterialString(hash, info.diffuseImageName);
    hash = HashRtMaterialString(hash, info.normalImageName);
    hash = HashRtMaterialString(hash, info.specularImageName);
    hash = HashRtMaterialString(hash, info.emissiveImageName);
    hash = HashRtMaterialValue(hash, material ? static_cast<uint64>(material->GetSurfaceType()) : 0u);
    hash = HashRtMaterialValue(hash, material ? static_cast<uint64>(material->GetSurfaceFlags()) : 0u);
    hash = HashRtMaterialFloat(hash, material ? material->GetSort() : 0.0f);
    hash = HashRtMaterialValue(hash, static_cast<uint64>(info.coverage));
    hash = HashRtMaterialValue(hash, static_cast<uint64>(info.diffuseUsage));
    hash = HashRtMaterialValue(hash, static_cast<uint64>(info.normalUsage));
    hash = HashRtMaterialValue(hash, static_cast<uint64>(info.specularUsage));
    hash = HashRtMaterialValue(hash, static_cast<uint64>(info.emissiveUsage));
    hash = HashRtMaterialValue(hash, static_cast<uint64>(info.diffuseColorFormat));
    hash = HashRtMaterialValue(hash, static_cast<uint64>(info.normalColorFormat));
    hash = HashRtMaterialValue(hash, static_cast<uint64>(info.specularColorFormat));
    hash = HashRtMaterialValue(hash, material ? static_cast<uint64>(material->GetNumStages()) : 0u);
    if (material)
    {
        const float* constantRegisters = material->ConstantRegisters();
        const int registerCount = material->GetNumRegisters();
        for (int stageIndex = 0; stageIndex < material->GetNumStages(); ++stageIndex)
        {
            const shaderStage_t* stage = material->GetStage(stageIndex);
            hash = HashRtMaterialValue(hash, stage ? static_cast<uint64>(stage->lighting) : 0u);
            hash = HashRtMaterialValue(hash, stage && stage->texture.image ? static_cast<uint64>(stage->texture.image->GetUsage()) : 0u);
            hash = HashRtMaterialValue(hash, stage && stage->texture.image ? static_cast<uint64>(stage->texture.image->GetOpts().colorFormat) : 0u);
            hash = HashRtMaterialValue(hash, stage ? stage->drawStateBits : 0u);
            hash = HashRtMaterialValue(hash, stage ? static_cast<uint64>(stage->texture.texgen) : 0u);
            hash = HashRtMaterialValue(hash, stage ? static_cast<uint64>(stage->texture.dynamic) : 0u);
            hash = HashRtMaterialValue(hash, stage && stage->newStage ? 1u : 0u);
            hash = HashRtMaterialValue(hash, stage && stage->texture.cinematic ? 1u : 0u);
            if (stage)
            {
                hash = HashRtMaterialValue(hash, static_cast<uint64>(stage->conditionRegister));
                if (constantRegisters && stage->conditionRegister >= 0 && stage->conditionRegister < registerCount)
                {
                    hash = HashRtMaterialFloat(hash, constantRegisters[stage->conditionRegister]);
                }
                for (int component = 0; component < 4; ++component)
                {
                    const int registerIndex = stage->color.registers[component];
                    hash = HashRtMaterialValue(hash, static_cast<uint64>(registerIndex));
                    if (constantRegisters && registerIndex >= 0 && registerIndex < registerCount)
                    {
                        hash = HashRtMaterialFloat(hash, constantRegisters[registerIndex]);
                    }
                }
            }
        }
    }
    hash = HashRtMaterialValue(hash, info.hasAlphaTest ? 1u : 0u);
    hash = HashRtMaterialValue(hash, info.emissive ? 1u : 0u);
    hash = HashRtMaterialValue(hash, static_cast<uint64>(r_pathTracingMatClassUseRmao.GetInteger() != 0 ? 1 : 0));
    hash = HashRtMaterialValue(hash, static_cast<uint64>(idMath::ClampInt(0, 2, r_pathTracingMatClassNormalDecodeMode.GetInteger())));
    hash = HashRtMaterialValue(hash, static_cast<uint64>(idMath::ClampInt(0, 1, r_pathTracingMatClassGlossRoughnessMode.GetInteger())));
    return hash;
}

RtMaterialRecord BuildRtMaterialRecord(const idMaterial* material, const RtSmokeMaterialTextureInfo& info, uint64 signature)
{
    RtMaterialRecord record;
    record.valid = true;
    record.signature = signature;
    record.materialId = info.materialId;
    record.materialName = info.materialName;
    record.diffuseImageName = info.diffuseImageName;
    record.normalImageName = info.normalImageName;
    record.specularImageName = info.specularImageName;
    record.emissiveImageName = info.emissiveImageName;
    record.rawSurfaceType = material ? static_cast<int>(material->GetSurfaceType()) : static_cast<int>(SURFTYPE_NONE);
    record.surfaceFlags = material ? material->GetSurfaceFlags() : 0;
    record.sort = material ? material->GetSort() : 0.0f;
    record.coverage = info.coverage;
    record.hasDiffuseImage = info.hasDiffuseImage;
    record.hasNormalImage = info.hasNormalImage;
    record.hasSpecularImage = info.hasSpecularImage;
    record.hasEmissiveImage = info.hasEmissiveImage;
    record.diffuseUsage = info.diffuseUsage;
    record.normalUsage = info.normalUsage;
    record.specularUsage = info.specularUsage;
    record.emissiveUsage = info.emissiveUsage;
    record.diffuseColorFormat = info.diffuseColorFormat;
    record.normalColorFormat = info.normalColorFormat;
    record.specularColorFormat = info.specularColorFormat;
    record.emissiveColorFormat = info.emissiveColorFormat;
    record.alphaTested = info.hasAlphaTest;
    record.emissiveIntent = info.emissive;
    record.stageFacts = AnalyzeRtMaterialStages(material);

    const RtMaterialClassCandidate surfaceCandidate = ResolveSurfaceClassCandidate(material, info, record.stageFacts);
    record.surfaceClass = surfaceCandidate.surfaceClass;
    record.surfaceClassConfidence = surfaceCandidate.confidence;
    record.surfaceClassReason = surfaceCandidate.reason;
    record.surfaceClassEvidence = surfaceCandidate.evidence;

    record.route = ResolveBsdfRoute(info, record.stageFacts, record.routeReason);
    record.normalDecodeMode = ResolveNormalDecodeMode(info);
    record.bsdf = RtDefaultBsdfForSurfaceClass(record.surfaceClass);
    if (record.route == RtMaterialBsdfRoute::RealPbrRmao)
    {
        record.bsdf.roughness = 0.05f;
        record.bsdf.metallic = 0.0f;
        record.bsdf.ao = 1.0f;
    }
    else if (record.route == RtMaterialBsdfRoute::LegacySpecGloss)
    {
        record.bsdf.metallic =
            record.surfaceClass == RtMaterialSurfaceClass::Metal ||
            record.surfaceClass == RtMaterialSurfaceClass::Ricochet ? 1.0f : 0.0f;
    }
    ApplyRouteBBsdfPolicy(material, record);
    return record;
}

void AccumulateRecordStats(const RtMaterialRecord& record)
{
    switch (record.route)
    {
        case RtMaterialBsdfRoute::RealPbrRmao:
            ++g_materialClassifierStats.routeRealPbr;
            break;
        case RtMaterialBsdfRoute::LegacySpecGloss:
            ++g_materialClassifierStats.routeLegacySpec;
            break;
        case RtMaterialBsdfRoute::SurfaceTypeFallback:
            ++g_materialClassifierStats.routeFallback;
            break;
        default:
            break;
    }
    switch (record.surfaceClassConfidence)
    {
        case RtMaterialClassConfidence::Authoritative:
            ++g_materialClassifierStats.confidenceAuthoritative;
            break;
        case RtMaterialClassConfidence::Flag:
            ++g_materialClassifierStats.confidenceFlag;
            break;
        case RtMaterialClassConfidence::Heuristic:
            ++g_materialClassifierStats.confidenceHeuristic;
            break;
        case RtMaterialClassConfidence::FallbackNone:
            ++g_materialClassifierStats.confidenceFallbackNone;
            break;
    }
}

const char* RtMaterialStageRouteEvidence(const shaderStage_t* stage)
{
    if (!stage || stage->lighting != SL_SPECULAR || !stage->texture.image)
    {
        return "-";
    }

    const textureUsage_t usage = stage->texture.image->GetUsage();
    if (usage == TD_SPECULAR_PBR_RMAO || usage == TD_SPECULAR_PBR_RMAOD)
    {
        return "routeA_rmao";
    }
    return "routeB_specular";
}

void MaybeDumpRecordStages(const RtMaterialRecord& record)
{
    if (r_pathTracingMatClassDebugList.GetInteger() < 3)
    {
        return;
    }

    const idMaterial* material = declManager ? declManager->FindMaterial(record.materialName.c_str(), false) : nullptr;
    if (!material)
    {
        common->Printf("MatClass: stageDump id=%u material='%s' materialDeclMissing=1\n",
            record.materialId,
            record.materialName.c_str());
        return;
    }

    for (int stageIndex = 0; stageIndex < material->GetNumStages(); ++stageIndex)
    {
        const shaderStage_t* stage = material->GetStage(stageIndex);
        if (!stage)
        {
            continue;
        }

        idImage* image = stage->texture.image;
        const bool active = RtStageConditionCanBeActive(material, stage);
        const bool additive = RtStageIsAdditiveBlend(stage);
        const bool filter = RtStageIsFilterBlend(stage);
        const bool alphaBlend = RtStageBlendUsesSourceAlpha(stage);
        const bool guiOrScreen =
            stage->texture.texgen == TG_SCREEN ||
            stage->texture.texgen == TG_SCREEN2 ||
            stage->texture.dynamic == DI_GUI_RENDER ||
            stage->texture.dynamic == DI_RENDER_TARGET;
        const bool cubeMap =
            stage->texture.texgen == TG_SKYBOX_CUBE ||
            stage->texture.texgen == TG_WOBBLESKY_CUBE ||
            stage->texture.texgen == TG_REFLECT_CUBE ||
            stage->texture.texgen == TG_REFLECT_CUBE2;
        const float conditionValue = RtStageConstantRegisterValue(material, stage->conditionRegister, 1.0f);
        const float alphaTestValue = RtStageConstantRegisterValue(material, stage->alphaTestRegister, -1.0f);
        const textureUsage_t usage = image ? image->GetUsage() : TD_DEFAULT;
        const textureColor_t colorFormat = image ? image->GetOpts().colorFormat : CFM_DEFAULT;

        common->Printf("MatClass: stage id=%u index=%d active=%d conditionReg=%d condition=%.3f lighting=%s routeEvidence=%s image='%s' usage=%s color=%s drawState=0x%llx srcBlend=%llu dstBlend=%llu additive=%d filter=%d alphaBlend=%d alphaTest=%d alphaReg=%d alphaValue=%.3f guiScreen=%d dynamic=%d cinematic=%d cube=%d customProgram=%d texgen=%d\n",
            record.materialId,
            stageIndex,
            active ? 1 : 0,
            stage->conditionRegister,
            conditionValue,
            RtStageLightingName(stage->lighting),
            RtMaterialStageRouteEvidence(stage),
            image ? image->GetName() : "<none>",
            RtTextureUsageName(usage),
            RtTextureColorFormatName(colorFormat),
            static_cast<unsigned long long>(stage->drawStateBits),
            static_cast<unsigned long long>((stage->drawStateBits & GLS_SRCBLEND_BITS) >> 0),
            static_cast<unsigned long long>((stage->drawStateBits & GLS_DSTBLEND_BITS) >> 3),
            additive ? 1 : 0,
            filter ? 1 : 0,
            alphaBlend ? 1 : 0,
            stage->hasAlphaTest ? 1 : 0,
            stage->alphaTestRegister,
            alphaTestValue,
            guiOrScreen ? 1 : 0,
            static_cast<int>(stage->texture.dynamic),
            stage->texture.cinematic ? 1 : 0,
            cubeMap ? 1 : 0,
            stage->newStage ? 1 : 0,
            static_cast<int>(stage->texture.texgen));
    }
}

void MaybeDumpRecord(const RtMaterialRecord& record)
{
    const int debugList = r_pathTracingMatClassDebugList.GetInteger();
    if (debugList <= 0)
    {
        return;
    }
    const int maxLogs = Max(0, r_pathTracingMatClassDebugMax.GetInteger());
    if (maxLogs > 0 && g_recordDebugLogs >= maxLogs)
    {
        return;
    }
    common->Printf("MatClass: record id=%u material='%s'\n",
        record.materialId,
        record.materialName.c_str());
    common->Printf("MatClass: route id=%u route=%s routeReason=%s routeStage=%d surfaceType=%d class=%s classReason=%s classEvidence='%s' confidence=%s normalDecode=%s\n",
        record.materialId,
        RtMaterialBsdfRouteName(record.route),
        RtMaterialBsdfRouteReasonName(record.routeReason),
        record.stageFacts.routeStage,
        record.rawSurfaceType,
        RtMaterialSurfaceClassName(record.surfaceClass),
        RtMaterialSurfaceClassReasonName(record.surfaceClassReason),
        record.surfaceClassEvidence.c_str(),
        RtMaterialClassConfidenceName(record.surfaceClassConfidence),
        RtMaterialNormalDecodeModeName(record.normalDecodeMode));
    common->Printf("MatClass: stages id=%u total=%d bump=%d diffuse=%d specular=%d rmao=%d legacySpec=%d ambient=%d effect=%d additive=%d filter=%d alphaBlend=%d coverage=%d guiScreen=%d dynamic=%d cinematic=%d cube=%d program=%d\n",
        record.materialId,
        record.stageFacts.stageCount,
        record.stageFacts.bumpStages,
        record.stageFacts.diffuseStages,
        record.stageFacts.specularStages,
        record.stageFacts.pbrRmaoStages,
        record.stageFacts.legacySpecStages,
        record.stageFacts.ambientStages,
        record.stageFacts.effectStages,
        record.stageFacts.additiveBlendStages,
        record.stageFacts.filterBlendStages,
        record.stageFacts.alphaBlendStages,
        record.stageFacts.coverageStages,
        record.stageFacts.guiOrScreenStages,
        record.stageFacts.dynamicImageStages,
        record.stageFacts.cinematicStages,
        record.stageFacts.cubeMapStages,
        record.stageFacts.customProgramStages);
    common->Printf("MatClass: images id=%u diffuse=%d/%s/%s normal=%d/%s/%s specular=%d/%s/%s emissive=%d/%s/%s alphaTest=%d emissiveIntent=%d\n",
        record.materialId,
        record.hasDiffuseImage ? 1 : 0,
        RtTextureUsageName(record.diffuseUsage),
        RtTextureColorFormatName(record.diffuseColorFormat),
        record.hasNormalImage ? 1 : 0,
        RtTextureUsageName(record.normalUsage),
        RtTextureColorFormatName(record.normalColorFormat),
        record.hasSpecularImage ? 1 : 0,
        RtTextureUsageName(record.specularUsage),
        RtTextureColorFormatName(record.specularColorFormat),
        record.hasEmissiveImage ? 1 : 0,
        RtTextureUsageName(record.emissiveUsage),
        RtTextureColorFormatName(record.emissiveColorFormat),
        record.alphaTested ? 1 : 0,
        record.emissiveIntent ? 1 : 0);
    common->Printf("MatClass: imageNames id=%u diffuse='%s' normal='%s' specular='%s' emissive='%s'\n",
        record.materialId,
        record.diffuseImageName.c_str(),
        record.normalImageName.c_str(),
        record.specularImageName.c_str(),
        record.emissiveImageName.c_str());
    common->Printf("MatClass: bsdf id=%u roughness=%.3f metallic=%.3f ior=%.3f transmission=%.3f f0=%.3f ao=%.3f subsurface=%d twoSided=%d bsdfEvidence='%s' specAvg=(%.3f,%.3f,%.3f) specLuma=%.3f\n",
        record.materialId,
        record.bsdf.roughness,
        record.bsdf.metallic,
        record.bsdf.ior,
        record.bsdf.transmission,
        record.bsdf.specularF0,
        record.bsdf.ao,
        static_cast<int>(record.bsdf.subsurfaceHint),
        static_cast<int>(record.bsdf.twoSidedBsdf),
        record.bsdfEvidence.c_str(),
        record.specularRepresentativeRgb[0],
        record.specularRepresentativeRgb[1],
        record.specularRepresentativeRgb[2],
        record.specularRepresentativeLuma);
    common->Printf("MatClass: packed id=%u flags=0x%08x params=0x%08x\n",
        record.materialId,
        PackPathTraceMaterialClassifierFlags(record),
        PackPathTraceMaterialClassifierParams(record));
    MaybeDumpRecordStages(record);
    ++g_recordDebugLogs;
}

const char* SurfaceTypeNameForDump(surfTypes_t surfaceType)
{
    switch (surfaceType)
    {
        case SURFTYPE_METAL:
            return "metal";
        case SURFTYPE_STONE:
            return "stone";
        case SURFTYPE_FLESH:
            return "flesh";
        case SURFTYPE_WOOD:
            return "wood";
        case SURFTYPE_CARDBOARD:
            return "cardboard";
        case SURFTYPE_LIQUID:
            return "liquid";
        case SURFTYPE_GLASS:
            return "glass";
        case SURFTYPE_PLASTIC:
            return "plastic";
        case SURFTYPE_RICOCHET:
            return "ricochet";
        case SURFTYPE_NONE:
            return "none";
        default:
            return "other";
    }
}

}

const char* RtMaterialSurfaceClassName(RtMaterialSurfaceClass surfaceClass)
{
    switch (surfaceClass)
    {
        case RtMaterialSurfaceClass::Metal:
            return "Metal";
        case RtMaterialSurfaceClass::Stone:
            return "Stone";
        case RtMaterialSurfaceClass::Flesh:
            return "Flesh";
        case RtMaterialSurfaceClass::Wood:
            return "Wood";
        case RtMaterialSurfaceClass::Cardboard:
            return "Cardboard";
        case RtMaterialSurfaceClass::Liquid:
            return "Liquid";
        case RtMaterialSurfaceClass::Glass:
            return "Glass";
        case RtMaterialSurfaceClass::Plastic:
            return "Plastic";
        case RtMaterialSurfaceClass::Ricochet:
            return "Ricochet";
        case RtMaterialSurfaceClass::Special:
            return "Special";
        default:
            return "Unknown";
    }
}

const char* RtMaterialClassConfidenceName(RtMaterialClassConfidence confidence)
{
    switch (confidence)
    {
        case RtMaterialClassConfidence::Authoritative:
            return "Authoritative";
        case RtMaterialClassConfidence::Flag:
            return "Flag";
        case RtMaterialClassConfidence::Heuristic:
            return "Heuristic";
        default:
            return "FallbackNone";
    }
}

const char* RtMaterialBsdfRouteName(RtMaterialBsdfRoute route)
{
    switch (route)
    {
        case RtMaterialBsdfRoute::RealPbrRmao:
            return "RouteA_RMAO";
        case RtMaterialBsdfRoute::LegacySpecGloss:
            return "RouteB_LegacySpec";
        case RtMaterialBsdfRoute::SurfaceTypeFallback:
            return "RouteC_SurfaceFallback";
        default:
            return "Unknown";
    }
}

const char* RtMaterialSurfaceClassReasonName(RtMaterialSurfaceClassReason reason)
{
    switch (reason)
    {
        case RtMaterialSurfaceClassReason::MaterialSort:
            return "MaterialSort";
        case RtMaterialSurfaceClassReason::SurfaceType:
            return "SurfaceType";
        case RtMaterialSurfaceClassReason::StageKind:
            return "StageKind";
        case RtMaterialSurfaceClassReason::NameToken:
            return "NameToken";
        case RtMaterialSurfaceClassReason::ImageNameToken:
            return "ImageNameToken";
        case RtMaterialSurfaceClassReason::FallbackMetal:
            return "FallbackMetal";
        case RtMaterialSurfaceClassReason::FallbackUnknown:
            return "FallbackUnknown";
        default:
            return "Unknown";
    }
}

const char* RtMaterialBsdfRouteReasonName(RtMaterialBsdfRouteReason reason)
{
    switch (reason)
    {
        case RtMaterialBsdfRouteReason::PbrRmaoStage:
            return "PbrRmaoStage";
        case RtMaterialBsdfRouteReason::LegacySpecularStage:
            return "LegacySpecularStage";
        case RtMaterialBsdfRouteReason::DiscoveredLegacySpecularImage:
            return "DiscoveredLegacySpecularImage";
        case RtMaterialBsdfRouteReason::RmaoDisabled:
            return "RmaoDisabled";
        case RtMaterialBsdfRouteReason::NoCompatibleSpecularStage:
            return "NoCompatibleSpecularStage";
        default:
            return "Unknown";
    }
}

const char* RtMaterialNormalDecodeModeName(RtMaterialNormalDecodeMode mode)
{
    switch (mode)
    {
        case RtMaterialNormalDecodeMode::Rgb8Rg:
            return "RGB8_RG";
        case RtMaterialNormalDecodeMode::CompressedWy:
            return "Compressed_WY";
        default:
            return "None";
    }
}

void BeginPathTraceMaterialClassifierFrame()
{
    g_materialClassifierStats.frameHits = 0;
    g_materialClassifierStats.frameMisses = 0;
    g_materialClassifierStats.frameRebuilds = 0;
    g_materialClassifierStats.routeRealPbr = 0;
    g_materialClassifierStats.routeLegacySpec = 0;
    g_materialClassifierStats.routeFallback = 0;
    g_materialClassifierStats.confidenceAuthoritative = 0;
    g_materialClassifierStats.confidenceFlag = 0;
    g_materialClassifierStats.confidenceHeuristic = 0;
    g_materialClassifierStats.confidenceFallbackNone = 0;
    g_recordDebugLogs = 0;
}

const RtMaterialRecord& RegisterPathTraceMaterialRecord(const idMaterial* material, const RtSmokeMaterialTextureInfo& info)
{
    const uint64 signature = ComputeRtMaterialRecordSignature(material, info);
    const RtMaterialRecordKey key = BuildRtMaterialRecordKey(info.materialId, info.materialName);
    std::pair<std::unordered_map<RtMaterialRecordKey, RtMaterialRecord, RtMaterialRecordKeyHash>::iterator, bool> insertResult =
        g_materialRecords.emplace(key, RtMaterialRecord());
    RtMaterialRecord& record = insertResult.first->second;
    if (!record.valid)
    {
        ++g_materialClassifierStats.misses;
        ++g_materialClassifierStats.frameMisses;
        record = BuildRtMaterialRecord(material, info, signature);
        ++g_materialClassifierGeneration;
        MaybeDumpRecord(record);
    }
    else if (record.signature != signature)
    {
        ++g_materialClassifierStats.rebuilds;
        ++g_materialClassifierStats.frameRebuilds;
        record = BuildRtMaterialRecord(material, info, signature);
        ++g_materialClassifierGeneration;
        MaybeDumpRecord(record);
    }
    else
    {
        ++g_materialClassifierStats.hits;
        ++g_materialClassifierStats.frameHits;
    }
    AccumulateRecordStats(record);
    return record;
}

const RtMaterialRecord* FindPathTraceMaterialRecord(uint32_t materialId)
{
    for (std::unordered_map<RtMaterialRecordKey, RtMaterialRecord, RtMaterialRecordKeyHash>::const_iterator it = g_materialRecords.begin(); it != g_materialRecords.end(); ++it)
    {
        if (it->second.materialId == materialId)
        {
            return &it->second;
        }
    }
    return nullptr;
}

RtMaterialClassifierStats GetPathTraceMaterialClassifierStats()
{
    RtMaterialClassifierStats stats = g_materialClassifierStats;
    stats.records = static_cast<int>(g_materialRecords.size());
    return stats;
}

uint32_t GetPathTraceMaterialClassifierGeneration()
{
    return g_materialClassifierGeneration;
}

uint32_t PackPathTraceMaterialClassifierFlags(const RtMaterialRecord& record)
{
    uint32_t flags = 0;
    flags |= static_cast<uint32_t>(record.surfaceClass) & 0x0fu;
    flags |= (static_cast<uint32_t>(record.surfaceClassConfidence) & 0x03u) << 4;
    flags |= (static_cast<uint32_t>(record.surfaceClassReason) & 0x0fu) << 6;
    flags |= (static_cast<uint32_t>(record.route) & 0x0fu) << 10;
    flags |= (static_cast<uint32_t>(record.routeReason) & 0x0fu) << 14;
    flags |= (static_cast<uint32_t>(record.normalDecodeMode) & 0x03u) << 18;
    flags |= record.hasDiffuseImage ? (1u << 20) : 0u;
    flags |= record.hasNormalImage ? (1u << 21) : 0u;
    flags |= record.hasSpecularImage ? (1u << 22) : 0u;
    flags |= record.hasEmissiveImage ? (1u << 23) : 0u;
    flags |= record.alphaTested ? (1u << 24) : 0u;
    flags |= record.emissiveIntent ? (1u << 25) : 0u;
    flags |= record.bsdf.twoSidedBsdf != 0 ? (1u << 26) : 0u;
    flags |= record.bsdf.subsurfaceHint != 0 ? (1u << 27) : 0u;
    flags |= record.stageFacts.additiveBlendStages > 0 ? (1u << 28) : 0u;
    flags |= record.stageFacts.filterBlendStages > 0 ? (1u << 29) : 0u;
    flags |= record.stageFacts.guiOrScreenStages > 0 ? (1u << 30) : 0u;
    flags |= record.stageFacts.effectStages > 0 ? (1u << 31) : 0u;
    return flags;
}

uint32_t PackPathTraceMaterialClassifierParams(const RtMaterialRecord& record)
{
    return QuantizeRtMaterialUnitFloat(record.bsdf.roughness) |
        (QuantizeRtMaterialUnitFloat(record.bsdf.metallic) << 8) |
        (QuantizeRtMaterialUnitFloat(record.bsdf.transmission) << 16) |
        (QuantizeRtMaterialUnitFloat(record.bsdf.specularF0) << 24);
}

void MaybeDumpPathTraceMaterialDeclSurfaceTypeDistribution()
{
    if (r_pathTracingMatClassDebugList.GetInteger() < 2 || g_dumpedDeclSurfaceDistribution)
    {
        return;
    }
    g_dumpedDeclSurfaceDistribution = true;

    int counts[SURFTYPE_15 + 2] = {};
    const int materialCount = declManager ? declManager->GetNumDecls(DECL_MATERIAL) : 0;
    for (int materialIndex = 0; materialIndex < materialCount; ++materialIndex)
    {
        const idMaterial* material = static_cast<const idMaterial*>(declManager->DeclByIndex(DECL_MATERIAL, materialIndex, false));
        const int surfaceType = material ? static_cast<int>(material->GetSurfaceType()) : static_cast<int>(SURFTYPE_NONE);
        if (surfaceType >= 0 && surfaceType <= SURFTYPE_15)
        {
            ++counts[surfaceType];
        }
        else
        {
            ++counts[SURFTYPE_15 + 1];
        }
    }

    common->Printf("MatClass: decl surfaceType distribution total=%d\n", materialCount);
    for (int surfaceType = SURFTYPE_NONE; surfaceType <= SURFTYPE_15; ++surfaceType)
    {
        common->Printf("MatClass: decl surfaceType %d (%s) count=%d\n",
            surfaceType,
            SurfaceTypeNameForDump(static_cast<surfTypes_t>(surfaceType)),
            counts[surfaceType]);
    }
    if (counts[SURFTYPE_15 + 1] > 0)
    {
        common->Printf("MatClass: decl surfaceType other count=%d\n", counts[SURFTYPE_15 + 1]);
    }
}
