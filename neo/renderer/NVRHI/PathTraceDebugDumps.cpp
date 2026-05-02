#include "precompiled.h"
#pragma hdrstop

#include "PathTraceDebugDumps.h"
#include "PathTraceTextureRegistry.h"

extern idCVar r_pathTracingAllowGuiTextures;
extern idCVar r_pathTracingTextureBindlessEnable;
extern idCVar r_pathTracingTextureForceFallback;
extern idCVar r_pathTracingTextureProbeDumpCount;
extern idCVar r_pathTracingTextureProbeDumpStart;
extern idCVar r_pathTracingTextureSampleEnable;
extern idCVar r_pathTracingTextureSampleMethod;
extern idCVar r_pathTracingTextureTableStart;

namespace {

const int RT_SMOKE_MATERIAL_REASON_SAMPLES = 12;
const int RT_SMOKE_TEXTURE_PROBE_CANDIDATE_SAMPLES = 24;
const int RT_SMOKE_TEXTURE_PROBE_DUMP_CANDIDATES = 64;
const uint32_t RT_SMOKE_TRIANGLE_CLASS_MASK = 0x0000ffffu;

const char* SmokeTextureCoverageClassNameByIndex(int classIndex)
{
    switch (classIndex)
    {
        case 0:
            return "static";
        case 1:
            return "rigid-entity";
        case 2:
            return "skinned";
        case 3:
            return "particle/alpha";
        case 4:
            return "unknown";
        default:
            return "invalid";
    }
}

const char* SmokeTextureFallbackReason(const RtSmokeMaterialTableBuild& table, int tableIndex, const RtSmokeMaterialTextureInfo& info)
{
    if (!SmokeMaterialTableIndexIsValid(table, tableIndex))
    {
        return "invalid material index";
    }

    const PathTraceSmokeMaterial& material = table.materials[tableIndex];
    if (material.diffuseTextureIndex != UINT32_MAX)
    {
        return "bound";
    }
    if (!info.diffuseImage)
    {
        return "no diffuse image";
    }
    if (!info.hasTextureHandle)
    {
        return "no texture handle";
    }
    if (!info.hasSafeTexture)
    {
        return info.fallbackReason.c_str();
    }
    return "safe but outside active window";
}

void AccumulateSmokeTextureCoverageTriangles(
    const RtSmokeMaterialTableBuild& table,
    const std::vector<uint32_t>& triangleClassData,
    const std::vector<uint32_t>& triangleMaterialIndexes,
    RtSmokeTextureCoverageStats& stats)
{
    const int triangleCount = Min(static_cast<int>(triangleClassData.size()), static_cast<int>(triangleMaterialIndexes.size()));
    for (int triangleIndex = 0; triangleIndex < triangleCount; ++triangleIndex)
    {
        const int classIndex = idMath::ClampInt(0, RT_SMOKE_DEBUG_TEXTURE_COVERAGE_CLASS_COUNT - 1, static_cast<int>(triangleClassData[triangleIndex] & RT_SMOKE_TRIANGLE_CLASS_MASK));
        RtSmokeTextureCoverageClassStats& classStats = stats.classes[classIndex];
        ++classStats.triangles;

        const uint32_t materialIndex = triangleMaterialIndexes[triangleIndex];
        if (!SmokeMaterialTableIndexIsValid(table, static_cast<int>(materialIndex)))
        {
            ++classStats.invalidMaterialTriangles;
            ++classStats.fallbackTriangles;
            continue;
        }

        if (table.materials[materialIndex].diffuseTextureIndex != UINT32_MAX)
        {
            ++classStats.boundTriangles;
        }
        else
        {
            ++classStats.fallbackTriangles;
        }
    }
}

}

const char* SmokeCoverageName(materialCoverage_t coverage)
{
    switch (coverage)
    {
        case MC_OPAQUE:
            return "opaque";
        case MC_PERFORATED:
            return "perforated";
        case MC_TRANSLUCENT:
            return "translucent";
        default:
            return "bad";
    }
}

const char* SmokeStageLightingName(stageLighting_t lighting)
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

const char* SmokeTexgenName(texgen_t texgen)
{
    switch (texgen)
    {
        case TG_EXPLICIT:
            return "explicit";
        case TG_DIFFUSE_CUBE:
            return "diffuse-cube";
        case TG_REFLECT_CUBE:
            return "reflect-cube";
        case TG_REFLECT_CUBE2:
            return "reflect-cube2";
        case TG_SKYBOX_CUBE:
            return "skybox-cube";
        case TG_WOBBLESKY_CUBE:
            return "wobblesky-cube";
        case TG_SCREEN:
            return "screen";
        case TG_SCREEN2:
            return "screen2";
        case TG_GLASSWARP:
            return "glasswarp";
        default:
            return "unknown";
    }
}

const char* SmokeTextureUsageName(textureUsage_t usage)
{
    switch (usage)
    {
        case TD_SPECULAR:
            return "TD_SPECULAR";
        case TD_DIFFUSE:
            return "TD_DIFFUSE";
        case TD_DEFAULT:
            return "TD_DEFAULT";
        case TD_BUMP:
            return "TD_BUMP";
        case TD_COVERAGE:
            return "TD_COVERAGE";
        default:
            return va("TD_%d", static_cast<int>(usage));
    }
}

const char* SmokeTextureColorFormatName(textureColor_t colorFormat)
{
    switch (colorFormat)
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
            return va("CFM_%d", static_cast<int>(colorFormat));
    }
}

const char* SmokeDeformName(deform_t deform)
{
    switch (deform)
    {
        case DFRM_NONE:
            return "none";
        case DFRM_SPRITE:
            return "sprite";
        case DFRM_TUBE:
            return "tube";
        case DFRM_FLARE:
            return "flare";
        case DFRM_EXPAND:
            return "expand";
        case DFRM_MOVE:
            return "move";
        case DFRM_EYEBALL:
            return "eyeball";
        case DFRM_PARTICLE:
            return "particle";
        case DFRM_PARTICLE2:
            return "particle2";
        case DFRM_TURB:
            return "turb";
        default:
            return "unknown";
    }
}

const char* SmokeDynamicModelName(dynamicModel_t dynamicModel)
{
    switch (dynamicModel)
    {
        case DM_STATIC:
            return "static";
        case DM_CACHED:
            return "cached";
        case DM_CONTINUOUS:
            return "continuous";
        default:
            return "unknown";
    }
}

void LogSmokeMaterialTable(const RtSmokeMaterialTableBuild& table)
{
    const int materialTableCount = Min(static_cast<int>(table.materialIds.size()), static_cast<int>(table.materials.size()));
    common->Printf("PathTracePrimaryPass: RT smoke material table entries=%d staticTriangles=%d dynamicTriangles=%d textureSlots=%d textured=%d normalTextured=%d specularTextured=%d emissive=%d emissiveTextured=%d alphaTested=%d alphaTextured=%d guiTextures=%d/%d/%d allowGui=%d additiveDecals=%d missingTextures=%d rejectedTextures=%d finalRejected=%d descriptorFallbacks=%d overTextureSlotLimit=%d probeRequest=%d probeBound=%d probeMaterial=%u latch=%d samples=",
        materialTableCount,
        static_cast<int>(table.staticMaterialIndexes.size()),
        static_cast<int>(table.dynamicMaterialIndexes.size()),
        static_cast<int>(table.diffuseTextures.size()),
        table.materialsWithTextures,
        table.materialsWithNormalTextures,
        table.materialsWithSpecularTextures,
        table.materialsEmissive,
        table.materialsWithEmissiveTextures,
        table.materialsAlphaTested,
        table.materialsWithAlphaTextures,
        table.guiTextureCandidates,
        table.guiTexturesAccepted,
        table.guiTexturesRejected,
        r_pathTracingAllowGuiTextures.GetInteger() != 0 ? 1 : 0,
        table.materialsAdditiveDecals,
        table.materialsMissingTextures,
        table.materialsRejectedTextures,
        table.materialsRejectedAtFinalCheck,
        table.descriptorsReplacedWithFallback,
        table.materialsOverTextureSlotLimit,
        table.textureProbeRequestedIndex,
        table.textureProbeBoundIndex,
        table.textureProbeBoundMaterialId,
        table.textureProbeUsedLatch ? 1 : 0);

    const int sampleCount = idMath::ClampInt(0, RT_SMOKE_MATERIAL_REASON_SAMPLES, materialTableCount);
    for (int sampleIndex = 0; sampleIndex < sampleCount; ++sampleIndex)
    {
        const PathTraceSmokeMaterial& material = table.materials[sampleIndex];
        common->Printf("%sindex=%d id=%u color=(%.2f %.2f %.2f) emissive=(%.2f %.2f %.2f) tex=%d normalTex=%d specTex=%d emissiveTex=%d alphaTex=%d alpha=%d additive=%d emissive=%d cutoff=%.2f",
            sampleIndex == 0 ? "" : ", ",
            sampleIndex,
            table.materialIds[sampleIndex],
            material.debugAlbedo[0],
            material.debugAlbedo[1],
            material.debugAlbedo[2],
            material.emissiveColor[0],
            material.emissiveColor[1],
            material.emissiveColor[2],
            material.diffuseTextureIndex == UINT32_MAX ? -1 : static_cast<int>(material.diffuseTextureIndex),
            material.normalTextureIndex == UINT32_MAX ? -1 : static_cast<int>(material.normalTextureIndex),
            material.specularTextureIndex == UINT32_MAX ? -1 : static_cast<int>(material.specularTextureIndex),
            material.emissiveTextureIndex == UINT32_MAX ? -1 : static_cast<int>(material.emissiveTextureIndex),
            material.alphaTextureIndex == UINT32_MAX ? -1 : static_cast<int>(material.alphaTextureIndex),
            (material.flags & RT_SMOKE_MATERIAL_ALPHA_TEST) != 0 ? 1 : 0,
            (material.flags & RT_SMOKE_MATERIAL_ADDITIVE_DECAL) != 0 ? 1 : 0,
            (material.flags & RT_SMOKE_MATERIAL_EMISSIVE) != 0 ? 1 : 0,
            material.alphaCutoff);
    }

    common->Printf("\n");
}

void LogSmokeTextureProbe(const RtSmokeMaterialTableBuild& table)
{
    const int materialTableCount = Min(static_cast<int>(table.materialIds.size()), static_cast<int>(table.materials.size()));
    const int textureTableLimit = GetSmokeTextureTableEffectiveLimit();
    const int requestedTextureTableLimit = GetSmokeTextureTableRequestedLimit();
    const int textureTableStart = Max(0, r_pathTracingTextureTableStart.GetInteger());
    const int textureSampleMethod = r_pathTracingTextureSampleEnable.GetInteger() != 0 ? idMath::ClampInt(0, 2, r_pathTracingTextureSampleMethod.GetInteger()) : 0;
    const bool textureBindlessEnabled = r_pathTracingTextureBindlessEnable.GetInteger() != 0;
    common->Printf("PathTracePrimaryPass: RT smoke bindless texture table occupancy=%d/%d requested=%d start=%d capacity=%d activeCap=%d sampleMethod=%d bindless=%d texturedMaterials=%d normalTextured=%d specularTextured=%d emissive=%d emissiveTextured=%d alphaTested=%d alphaTextured=%d guiTextures=%d/%d/%d allowGui=%d missing=%d rejected=%d finalRejected=%d descriptorFallbacks=%d skippedOrOverLimit=%d\n",
        static_cast<int>(table.diffuseTextures.size()),
        textureTableLimit,
        requestedTextureTableLimit,
        textureTableStart,
        RT_SMOKE_TEXTURE_DESCRIPTOR_CAPACITY,
        RT_SMOKE_TEXTURE_EXPERIMENTAL_ACTIVE_CAP,
        textureSampleMethod,
        textureBindlessEnabled ? 1 : 0,
        table.materialsWithTextures,
        table.materialsWithNormalTextures,
        table.materialsWithSpecularTextures,
        table.materialsEmissive,
        table.materialsWithEmissiveTextures,
        table.materialsAlphaTested,
        table.materialsWithAlphaTextures,
        table.guiTextureCandidates,
        table.guiTexturesAccepted,
        table.guiTexturesRejected,
        r_pathTracingAllowGuiTextures.GetInteger() != 0 ? 1 : 0,
        table.materialsMissingTextures,
        table.materialsRejectedTextures,
        table.materialsRejectedAtFinalCheck,
        table.descriptorsReplacedWithFallback,
        table.materialsOverTextureSlotLimit);

    if (table.textureProbeBoundIndex < 0 || table.textureProbeBoundIndex >= materialTableCount)
    {
        common->Printf("PathTracePrimaryPass: RT smoke texture probe no bound texture request=%d tableEntries=%d\n",
            table.textureProbeRequestedIndex,
            materialTableCount);
        return;
    }

    const RtSmokeMaterialTextureInfo info = ResolveSmokeMaterialTextureInfo(table.materialIds[table.textureProbeBoundIndex], table.textureProbeBoundIndex);
    const PathTraceSmokeMaterial& material = table.materials[table.textureProbeBoundIndex];
    nvrhi::TextureHandle texture = info.diffuseImage ? info.diffuseImage->GetTextureHandle() : nullptr;
    if (!texture)
    {
        common->Printf("PathTracePrimaryPass: RT smoke texture probe missing bound texture index=%d material='%s'\n",
            table.textureProbeBoundIndex,
            info.materialName.c_str());
        return;
    }

    const nvrhi::TextureDesc& desc = texture->getDesc();
    common->Printf("PathTracePrimaryPass: RT smoke texture probe slot=%d tableIndex=%d material='%s' diffuse='%s' format=%d size=%ux%u mips=%u reason='%s'\n",
        material.diffuseTextureIndex == UINT32_MAX ? -1 : static_cast<int>(material.diffuseTextureIndex),
        table.textureProbeBoundIndex,
        info.materialName.c_str(),
        info.diffuseImageName.c_str(),
        static_cast<int>(desc.format),
        desc.width,
        desc.height,
        desc.mipLevels,
        info.fallbackReason.c_str());
}

void LogSmokeTextureProbeSwitch(const RtSmokeMaterialTableBuild& table)
{
    const int materialTableCount = Min(static_cast<int>(table.materialIds.size()), static_cast<int>(table.materials.size()));
    if (table.textureProbeBoundIndex < 0 || table.textureProbeBoundIndex >= materialTableCount)
    {
        common->Printf("PathTracePrimaryPass: RT smoke texture probe unbound request=%d latchedMaterial=%u visible=0\n",
            table.textureProbeRequestedIndex,
            table.textureProbeBoundMaterialId);
        return;
    }

    const RtSmokeMaterialTextureInfo info = ResolveSmokeMaterialTextureInfo(table.materialIds[table.textureProbeBoundIndex], table.textureProbeBoundIndex);
    const PathTraceSmokeMaterial& material = table.materials[table.textureProbeBoundIndex];
    common->Printf("PathTracePrimaryPass: RT smoke texture probe latched slot=%d tableIndex=%d materialId=%u material='%s' diffuse='%s' latch=%d\n",
        material.diffuseTextureIndex == UINT32_MAX ? -1 : static_cast<int>(material.diffuseTextureIndex),
        table.textureProbeBoundIndex,
        table.textureProbeBoundMaterialId,
        info.materialName.c_str(),
        info.diffuseImageName.c_str(),
        table.textureProbeUsedLatch ? 1 : 0);
}

void LogSmokeAlphaMaterialDump(const RtSmokeMaterialTableBuild& table)
{
    const int materialTableCount = Min(static_cast<int>(table.materialIds.size()), static_cast<int>(table.materials.size()));
    int alphaMaterialCount = 0;
    for (int tableIndex = 0; tableIndex < materialTableCount; ++tableIndex)
    {
        if ((table.materials[tableIndex].flags & RT_SMOKE_MATERIAL_ALPHA_TEST) != 0)
        {
            ++alphaMaterialCount;
        }
    }

    const int maxLogged = RT_SMOKE_TEXTURE_PROBE_DUMP_CANDIDATES;
    common->Printf("PathTracePrimaryPass: RT smoke alpha material dump entries=%d alphaTested=%d slots=%d tableLimit=%d start=%d sampleMethod=%d bindless=%d\n",
        materialTableCount,
        alphaMaterialCount,
        static_cast<int>(table.diffuseTextures.size()),
        GetSmokeTextureTableEffectiveLimit(),
        Max(0, r_pathTracingTextureTableStart.GetInteger()),
        r_pathTracingTextureSampleEnable.GetInteger() != 0 ? idMath::ClampInt(0, 2, r_pathTracingTextureSampleMethod.GetInteger()) : 0,
        r_pathTracingTextureBindlessEnable.GetInteger() != 0 ? 1 : 0);

    int logged = 0;
    for (int tableIndex = 0; tableIndex < materialTableCount && logged < maxLogged; ++tableIndex)
    {
        const PathTraceSmokeMaterial& material = table.materials[tableIndex];
        if ((material.flags & RT_SMOKE_MATERIAL_ALPHA_TEST) == 0)
        {
            continue;
        }

        const RtSmokeMaterialTextureInfo info = ResolveSmokeMaterialTextureInfo(table.materialIds[tableIndex], tableIndex);
        common->Printf("PathTracePrimaryPass: RT smoke alpha material index=%d id=%u material='%s' coverage=%s cutoff=%.3f diffuse='%s' diffuseSlot=%d diffuseSize=%ux%u alpha='%s' alphaSlot=%d alphaSize=%ux%u alphaReason='%s' diffuseReason='%s' safeDiffuse=%d safeAlpha=%d\n",
            tableIndex,
            table.materialIds[tableIndex],
            info.materialName.c_str(),
            SmokeCoverageName(info.coverage),
            material.alphaCutoff,
            info.diffuseImageName.c_str(),
            material.diffuseTextureIndex == UINT32_MAX ? -1 : static_cast<int>(material.diffuseTextureIndex),
            material.textureWidth,
            material.textureHeight,
            info.alphaImageName.c_str(),
            material.alphaTextureIndex == UINT32_MAX ? -1 : static_cast<int>(material.alphaTextureIndex),
            material.alphaTextureWidth,
            material.alphaTextureHeight,
            info.alphaReason.c_str(),
            info.fallbackReason.c_str(),
            info.hasSafeTexture ? 1 : 0,
            info.hasSafeAlphaTexture ? 1 : 0);
        ++logged;
    }

    if (logged == 0)
    {
        common->Printf("PathTracePrimaryPass: RT smoke alpha material dump found no alpha-tested materials in current capture\n");
    }
    else if (logged < alphaMaterialCount)
    {
        common->Printf("PathTracePrimaryPass: RT smoke alpha material dump truncated logged=%d total=%d\n",
            logged,
            alphaMaterialCount);
    }
}

void LogSmokeTextureProbeDump(const RtSmokeMaterialTableBuild& table)
{
    const int materialTableCount = Min(static_cast<int>(table.materialIds.size()), static_cast<int>(table.materials.size()));
    const int textureTableLimit = GetSmokeTextureTableEffectiveLimit();
    const int requestedTextureTableLimit = GetSmokeTextureTableRequestedLimit();
    const int textureTableStart = Max(0, r_pathTracingTextureTableStart.GetInteger());
    const int textureSampleMethod = r_pathTracingTextureSampleEnable.GetInteger() != 0 ? idMath::ClampInt(0, 2, r_pathTracingTextureSampleMethod.GetInteger()) : 0;
    const bool textureBindlessEnabled = r_pathTracingTextureBindlessEnable.GetInteger() != 0;
    common->Printf("PathTracePrimaryPass: RT smoke texture probe dump request=%d bound=%d materialId=%u slots=%d/%d requested=%d start=%d capacity=%d activeCap=%d sampleMethod=%d bindless=%d texturedMaterials=%d alphaTested=%d alphaTextured=%d missing=%d rejected=%d finalRejected=%d descriptorFallbacks=%d skippedOrOverLimit=%d\n",
        table.textureProbeRequestedIndex,
        table.textureProbeBoundIndex,
        table.textureProbeBoundMaterialId,
        static_cast<int>(table.diffuseTextures.size()),
        textureTableLimit,
        requestedTextureTableLimit,
        textureTableStart,
        RT_SMOKE_TEXTURE_DESCRIPTOR_CAPACITY,
        RT_SMOKE_TEXTURE_EXPERIMENTAL_ACTIVE_CAP,
        textureSampleMethod,
        textureBindlessEnabled ? 1 : 0,
        table.materialsWithTextures,
        table.materialsAlphaTested,
        table.materialsWithAlphaTextures,
        table.materialsMissingTextures,
        table.materialsRejectedTextures,
        table.materialsRejectedAtFinalCheck,
        table.descriptorsReplacedWithFallback,
        table.materialsOverTextureSlotLimit);

    if (table.textureProbeBoundIndex >= 0 && table.textureProbeBoundIndex < materialTableCount)
    {
        const RtSmokeMaterialTextureInfo info = ResolveSmokeMaterialTextureInfo(table.materialIds[table.textureProbeBoundIndex], table.textureProbeBoundIndex);
        const PathTraceSmokeMaterial& material = table.materials[table.textureProbeBoundIndex];
        common->Printf("PathTracePrimaryPass: RT smoke texture probe current index=%d slot=%d material='%s' diffuse='%s' safe=%d reason='%s'\n",
            table.textureProbeBoundIndex,
            material.diffuseTextureIndex == UINT32_MAX ? -1 : static_cast<int>(material.diffuseTextureIndex),
            info.materialName.c_str(),
            info.diffuseImageName.c_str(),
            info.hasSafeTexture ? 1 : 0,
            info.fallbackReason.c_str());
        common->Printf("PathTracePrimaryPass: RT smoke texture probe alpha current slot=%d alpha='%s' safe=%d reason='%s'\n",
            material.alphaTextureIndex == UINT32_MAX ? -1 : static_cast<int>(material.alphaTextureIndex),
            info.alphaImageName.c_str(),
            info.hasSafeAlphaTexture ? 1 : 0,
            info.alphaReason.c_str());
    }
    else
    {
        common->Printf("PathTracePrimaryPass: RT smoke texture probe current none\n");
    }

    common->Printf("PathTracePrimaryPass: RT smoke texture probe sampled entries=");
    int logged = 0;
    for (int tableIndex = 0; tableIndex < materialTableCount && logged < RT_SMOKE_TEXTURE_PROBE_CANDIDATE_SAMPLES; ++tableIndex)
    {
        const PathTraceSmokeMaterial& material = table.materials[tableIndex];
        if (material.diffuseTextureIndex == UINT32_MAX)
        {
            continue;
        }

        const RtSmokeMaterialTextureInfo info = ResolveSmokeMaterialTextureInfo(table.materialIds[tableIndex], tableIndex);
        common->Printf("%s%d:slot%d '%s' -> '%s'",
            logged == 0 ? "" : ", ",
            tableIndex,
            static_cast<int>(material.diffuseTextureIndex),
            info.materialName.c_str(),
            info.diffuseImageName.c_str());
        ++logged;
    }
    if (logged == 0)
    {
        common->Printf("<none>");
    }
    common->Printf("\n");

    const int dumpStart = Max(0, r_pathTracingTextureProbeDumpStart.GetInteger());
    const int dumpCount = idMath::ClampInt(1, RT_SMOKE_TEXTURE_PROBE_DUMP_CANDIDATES, r_pathTracingTextureProbeDumpCount.GetInteger());
    common->Printf("PathTracePrimaryPass: RT smoke texture probe safe candidate order page start=%d count=%d\n", dumpStart, dumpCount);
    const std::vector<int> safeMaterialIndexes = BuildSmokeSafeMaterialIndexOrder(table);
    int loggedCandidates = 0;
    for (int candidateIndex = 0; candidateIndex < static_cast<int>(safeMaterialIndexes.size()); ++candidateIndex)
    {
        if (candidateIndex < dumpStart)
        {
            continue;
        }
        if (loggedCandidates >= dumpCount)
        {
            continue;
        }

        const int tableIndex = safeMaterialIndexes[candidateIndex];
        if (!SmokeMaterialTableIndexIsValid(table, tableIndex))
        {
            continue;
        }
        const RtSmokeMaterialTextureInfo info = ResolveSmokeMaterialTextureInfo(table.materialIds[tableIndex], tableIndex);
        const PathTraceSmokeMaterial& material = table.materials[tableIndex];
        const bool sampled = material.diffuseTextureIndex != UINT32_MAX;
        common->Printf("PathTracePrimaryPass: RT smoke texture candidate %d table=%d slot=%d alphaSlot=%d sampled=%d material='%s' diffuse='%s' alpha='%s' reason='%s' alphaReason='%s'\n",
            candidateIndex,
            tableIndex,
            sampled ? static_cast<int>(material.diffuseTextureIndex) : -1,
            material.alphaTextureIndex == UINT32_MAX ? -1 : static_cast<int>(material.alphaTextureIndex),
            sampled ? 1 : 0,
            info.materialName.c_str(),
            info.diffuseImageName.c_str(),
            info.alphaImageName.c_str(),
            info.fallbackReason.c_str(),
            info.alphaReason.c_str());
        ++loggedCandidates;
    }
    if (loggedCandidates == 0)
    {
        common->Printf("PathTracePrimaryPass: RT smoke texture candidate page empty\n");
    }
    common->Printf("PathTracePrimaryPass: RT smoke texture candidate total=%d\n", static_cast<int>(safeMaterialIndexes.size()));
}

void LogSmokeTextureActiveWindow(const RtSmokeMaterialTableBuild& table)
{
    const int textureTableLimit = GetSmokeTextureTableEffectiveLimit();
    if (textureTableLimit <= 0)
    {
        return;
    }

    const int textureTableStart = Max(0, r_pathTracingTextureTableStart.GetInteger());
    const bool forceFallback = r_pathTracingTextureForceFallback.GetInteger() != 0;
    const int textureSampleMethod = r_pathTracingTextureSampleEnable.GetInteger() != 0 ? idMath::ClampInt(0, 2, r_pathTracingTextureSampleMethod.GetInteger()) : 0;
    const bool textureBindlessEnabled = r_pathTracingTextureBindlessEnable.GetInteger() != 0;
    uint32_t activeHash = 2166136261u;
    int sampledCount = 0;
    const int materialTableCount = Min(static_cast<int>(table.materialIds.size()), static_cast<int>(table.materials.size()));
    for (int tableIndex = 0; tableIndex < materialTableCount; ++tableIndex)
    {
        const PathTraceSmokeMaterial& material = table.materials[tableIndex];
        if (material.diffuseTextureIndex == UINT32_MAX)
        {
            continue;
        }
        activeHash = (activeHash ^ table.materialIds[tableIndex]) * 16777619u;
        activeHash = (activeHash ^ material.diffuseTextureIndex) * 16777619u;
        ++sampledCount;
    }

    static int lastLoggedStart = -1;
    static int lastLoggedLimit = -1;
    static int lastLoggedSampledCount = -1;
    static int lastLoggedForceFallback = -1;
    static int lastLoggedSampleMethod = -1;
    static int lastLoggedBindlessEnabled = -1;
    static uint32_t lastLoggedHash = 0;
    if (lastLoggedStart == textureTableStart &&
        lastLoggedLimit == textureTableLimit &&
        lastLoggedSampledCount == sampledCount &&
        lastLoggedForceFallback == (forceFallback ? 1 : 0) &&
        lastLoggedSampleMethod == textureSampleMethod &&
        lastLoggedBindlessEnabled == (textureBindlessEnabled ? 1 : 0) &&
        lastLoggedHash == activeHash)
    {
        return;
    }

    lastLoggedStart = textureTableStart;
    lastLoggedLimit = textureTableLimit;
    lastLoggedSampledCount = sampledCount;
    lastLoggedForceFallback = forceFallback ? 1 : 0;
    lastLoggedSampleMethod = textureSampleMethod;
    lastLoggedBindlessEnabled = textureBindlessEnabled ? 1 : 0;
    lastLoggedHash = activeHash;

    common->Printf("PathTracePrimaryPass: RT smoke active texture window start=%d limit=%d requested=%d activeCap=%d slots=%d sampledMaterials=%d forceFallback=%d sampleMethod=%d bindless=%d\n",
        textureTableStart,
        textureTableLimit,
        GetSmokeTextureTableRequestedLimit(),
        RT_SMOKE_TEXTURE_EXPERIMENTAL_ACTIVE_CAP,
        static_cast<int>(table.diffuseTextures.size()),
        sampledCount,
        forceFallback ? 1 : 0,
        textureSampleMethod,
        textureBindlessEnabled ? 1 : 0);

    const std::vector<int> safeMaterialIndexes = BuildSmokeSafeMaterialIndexOrder(table);
    int logged = 0;
    for (int candidateIndex = 0; candidateIndex < static_cast<int>(safeMaterialIndexes.size()) && logged < RT_SMOKE_TEXTURE_PROBE_CANDIDATE_SAMPLES; ++candidateIndex)
    {
        const int tableIndex = safeMaterialIndexes[candidateIndex];
        if (!SmokeMaterialTableIndexIsValid(table, tableIndex))
        {
            continue;
        }
        const PathTraceSmokeMaterial& material = table.materials[tableIndex];
        if (material.diffuseTextureIndex == UINT32_MAX)
        {
            continue;
        }

        const RtSmokeMaterialTextureInfo info = ResolveSmokeMaterialTextureInfo(table.materialIds[tableIndex], tableIndex);
        common->Printf("PathTracePrimaryPass: RT smoke active texture candidate %d table=%d slot=%d material='%s' diffuse='%s' reason='%s'\n",
            candidateIndex,
            tableIndex,
            static_cast<int>(material.diffuseTextureIndex),
            info.materialName.c_str(),
            info.diffuseImageName.c_str(),
            info.fallbackReason.c_str());
        ++logged;
    }
}

RtSmokeTextureCoverageStats BuildSmokeTextureCoverageStats(
    const RtSmokeMaterialTableBuild& table,
    const std::vector<uint32_t>& staticTriangleClassData,
    const std::vector<uint32_t>& staticTriangleMaterialIndexes,
    const std::vector<uint32_t>& dynamicTriangleClassData,
    const std::vector<uint32_t>& dynamicTriangleMaterialIndexes)
{
    RtSmokeTextureCoverageStats stats;
    const int materialTableCount = Min(static_cast<int>(table.materialIds.size()), static_cast<int>(table.materials.size()));
    stats.materials = materialTableCount;
    for (int tableIndex = 0; tableIndex < materialTableCount; ++tableIndex)
    {
        if (table.materials[tableIndex].diffuseTextureIndex != UINT32_MAX)
        {
            ++stats.boundMaterials;
        }
        else
        {
            ++stats.fallbackMaterials;
        }
    }

    AccumulateSmokeTextureCoverageTriangles(table, staticTriangleClassData, staticTriangleMaterialIndexes, stats);
    AccumulateSmokeTextureCoverageTriangles(table, dynamicTriangleClassData, dynamicTriangleMaterialIndexes, stats);
    return stats;
}

void LogSmokeTextureCoverage(const RtSmokeTextureCoverageStats& stats)
{
    common->Printf("PathTracePrimaryPass: RT smoke texture coverage materials bound=%d fallback=%d total=%d",
        stats.boundMaterials,
        stats.fallbackMaterials,
        stats.materials);
    for (int classIndex = 0; classIndex < RT_SMOKE_DEBUG_TEXTURE_COVERAGE_CLASS_COUNT; ++classIndex)
    {
        const RtSmokeTextureCoverageClassStats& classStats = stats.classes[classIndex];
        common->Printf("; %s tris=%d bound=%d fallback=%d invalidMat=%d",
            SmokeTextureCoverageClassNameByIndex(classIndex),
            classStats.triangles,
            classStats.boundTriangles,
            classStats.fallbackTriangles,
            classStats.invalidMaterialTriangles);
    }
    common->Printf("\n");
}

void LogSmokeTextureFallbackDump(const RtSmokeMaterialTableBuild& table)
{
    const int materialTableCount = Min(static_cast<int>(table.materialIds.size()), static_cast<int>(table.materials.size()));
    common->Printf("PathTracePrimaryPass: RT smoke texture fallback dump entries=%d textureSlots=%d/%d tableLimit=%d requested=%d activeCap=%d start=%d\n",
        materialTableCount,
        static_cast<int>(table.diffuseTextures.size()),
        RT_SMOKE_TEXTURE_DESCRIPTOR_CAPACITY,
        GetSmokeTextureTableEffectiveLimit(),
        GetSmokeTextureTableRequestedLimit(),
        RT_SMOKE_TEXTURE_EXPERIMENTAL_ACTIVE_CAP,
        Max(0, r_pathTracingTextureTableStart.GetInteger()));

    int logged = 0;
    int fallbackCount = 0;
    for (int tableIndex = 0; tableIndex < materialTableCount; ++tableIndex)
    {
        if (table.materials[tableIndex].diffuseTextureIndex != UINT32_MAX)
        {
            continue;
        }

        ++fallbackCount;
        if (logged >= RT_SMOKE_TEXTURE_PROBE_DUMP_CANDIDATES)
        {
            continue;
        }

        const RtSmokeMaterialTextureInfo info = ResolveSmokeMaterialTextureInfo(table.materialIds[tableIndex], tableIndex);
        common->Printf("PathTracePrimaryPass: RT smoke texture fallback index=%d id=%u material='%s' diffuse='%s' reason='%s' fallback='%s' image=%d handle=%d safe=%d coverage=%s\n",
            tableIndex,
            table.materialIds[tableIndex],
            info.materialName.c_str(),
            info.diffuseImageName.c_str(),
            info.fallbackReason.c_str(),
            SmokeTextureFallbackReason(table, tableIndex, info),
            info.hasDiffuseImage ? 1 : 0,
            info.hasTextureHandle ? 1 : 0,
            info.hasSafeTexture ? 1 : 0,
            SmokeCoverageName(info.coverage));
        ++logged;
    }

    common->Printf("PathTracePrimaryPass: RT smoke texture fallback dump logged=%d total=%d\n",
        logged,
        fallbackCount);
}

void LogSmokeMaterialTextureDiscovery(const RtSmokeMaterialTableBuild& table)
{
    const int materialTableCount = Min(static_cast<int>(table.materialIds.size()), static_cast<int>(table.materials.size()));
    int diffuseImages = 0;
    int textureHandles = 0;
    int safeTextures = 0;
    int missingTextureHandles = 0;
    for (int tableIndex = 0; tableIndex < materialTableCount; ++tableIndex)
    {
        const RtSmokeMaterialTextureInfo info = ResolveSmokeMaterialTextureInfo(table.materialIds[tableIndex], tableIndex);
        if (info.hasDiffuseImage)
        {
            ++diffuseImages;
        }
        if (info.hasTextureHandle)
        {
            ++textureHandles;
        }
        if (info.hasSafeTexture)
        {
            ++safeTextures;
        }
        else
        {
            ++missingTextureHandles;
        }
    }

    common->Printf("PathTracePrimaryPass: RT smoke material texture discovery tableEntries=%d diffuseImages=%d textureHandles=%d safeTextures=%d missingOrRejected=%d samples=",
        materialTableCount,
        diffuseImages,
        textureHandles,
        safeTextures,
        missingTextureHandles);

    const int sampleCount = idMath::ClampInt(0, RT_SMOKE_MATERIAL_REASON_SAMPLES, materialTableCount);
    for (int sampleIndex = 0; sampleIndex < sampleCount; ++sampleIndex)
    {
        const RtSmokeMaterialTextureInfo info = ResolveSmokeMaterialTextureInfo(table.materialIds[sampleIndex], sampleIndex);
        common->Printf("%sindex=%d material='%s' diffuse='%s' image=%d handle=%d safe=%d guiLike=%d reason='%s'",
            sampleIndex == 0 ? "" : ", ",
            info.tableIndex,
            info.materialName.c_str(),
            info.diffuseImageName.c_str(),
            info.hasDiffuseImage ? 1 : 0,
            info.hasTextureHandle ? 1 : 0,
            info.hasSafeTexture ? 1 : 0,
            IsSmokeImageNameGuiLike(info.diffuseImageName.c_str()) ? 1 : 0,
            info.fallbackReason.c_str());
    }

    common->Printf("\n");

    if (missingTextureHandles > 0)
    {
        common->Printf("PathTracePrimaryPass: RT smoke material missing texture handles samples=");
        int loggedMissing = 0;
        for (int tableIndex = 0; tableIndex < materialTableCount && loggedMissing < RT_SMOKE_MATERIAL_REASON_SAMPLES; ++tableIndex)
        {
            const RtSmokeMaterialTextureInfo info = ResolveSmokeMaterialTextureInfo(table.materialIds[tableIndex], tableIndex);
            if (info.hasTextureHandle)
            {
                continue;
            }

            common->Printf("%sindex=%d material='%s' diffuse='%s' image=%d reason='%s'",
                loggedMissing == 0 ? "" : ", ",
                info.tableIndex,
                info.materialName.c_str(),
                info.diffuseImageName.c_str(),
                info.hasDiffuseImage ? 1 : 0,
                info.fallbackReason.c_str());
            ++loggedMissing;
        }

        common->Printf("\n");
    }
}
