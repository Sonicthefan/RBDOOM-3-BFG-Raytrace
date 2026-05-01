#include "precompiled.h"
#pragma hdrstop

#include "PathTracePrimaryPass.h"
#include "../RenderCommon.h"
#include "../RenderBackend.h"
#include "../GLMatrix.h"
#include "../Image.h"
#include "../Model_local.h"
#include "../Passes/CommonPasses.h"
#include "../../framework/Common_local.h"
#include "../../sys/DeviceManager.h"

#include <nvrhi/utils.h>
#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <vector>

extern DeviceManager* deviceManager;

idCVar r_pathTracingDebugMode(
    "r_pathTracingDebugMode",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "RT smoke debug output mode: 0 = hit/miss, 1 = depth, 2 = interpolated normal, 3 = surface class, 4 = UV, 5 = geometric normal, 6 = material ID, 7 = material table, 8 = sampled diffuse texture, 9 = alpha test preview, 10 = albedo, 11 = translucent overlay inspection, 12 = translucent subtype, 13 = fixed Lambert lighting, 14 = selected point-light shadows, 15 = selected light influence, 16 = normal map, 17 = specular map" );

idCVar r_pathTracingClassDump(
    "r_pathTracingClassDump",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "Set to 1 to dump sampled RT smoke surface classification reasons once" );

idCVar r_pathTracingClassSummary(
    "r_pathTracingClassSummary",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "Set to 1 to include RT smoke class counts in the throttled scene-capture summary" );

idCVar r_pathTracingDebugWidth(
    "r_pathTracingDebugWidth",
    "320",
    CVAR_RENDERER | CVAR_INTEGER | CVAR_ARCHIVE,
    "RT smoke debug output width" );

idCVar r_pathTracingDebugHeight(
    "r_pathTracingDebugHeight",
    "180",
    CVAR_RENDERER | CVAR_INTEGER | CVAR_ARCHIVE,
    "RT smoke debug output height" );

idCVar r_pathTracingTextureProbeIndex(
    "r_pathTracingTextureProbeIndex",
    "-1",
    CVAR_RENDERER | CVAR_INTEGER,
    "RT smoke debug mode 8 material table index to focus in texture probe logging; -1 = first safe texture" );

idCVar r_pathTracingTextureProbeReset(
    "r_pathTracingTextureProbeReset",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "Set to 1 to release the latched RT smoke texture probe and select a new one" );

idCVar r_pathTracingTextureProbeDump(
    "r_pathTracingTextureProbeDump",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "Set to 1 to dump the current RT smoke texture probe and sampled candidate list once" );

idCVar r_pathTracingAlphaDump(
    "r_pathTracingAlphaDump",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "Set to 1 to dump the current RT smoke alpha-test material table once" );

idCVar r_pathTracingTextureFallbackDump(
    "r_pathTracingTextureFallbackDump",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "Set to 1 to dump current RT smoke albedo fallback materials once" );

idCVar r_pathTracingTranslucentDump(
    "r_pathTracingTranslucentDump",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "Set to 1 to dump current RT smoke translucent subtype classifier samples once" );

idCVar r_pathTracingCrosshairMaterialDump(
    "r_pathTracingCrosshairMaterialDump",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "Set to 1 to dump detailed RT smoke material/stage info for the surface under the center crosshair once" );

idCVar r_pathTracingGuiDump(
    "r_pathTracingGuiDump",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "Set to 1 to dump captured RT smoke in-world GUI draw surfaces once" );

idCVar r_pathTracingLightDump(
    "r_pathTracingLightDump",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "Set to 1 to dump the current RT smoke selected debug light once" );

idCVar r_pathTracingLightCount(
    "r_pathTracingLightCount",
    "4",
    CVAR_RENDERER | CVAR_INTEGER,
    "Maximum selected visible point lights for RT smoke debug modes 14/15; 0 = fixed directional fallback, max 32" );

idCVar r_pathTracingLightSelection(
    "r_pathTracingLightSelection",
    "1",
    CVAR_RENDERER | CVAR_INTEGER,
    "RT smoke point-light selection: 0 = nearest camera lights, 1 = strongest estimated camera influence" );

idCVar r_pathTracingLightSpriteProxies(
    "r_pathTracingLightSpriteProxies",
    "1",
    CVAR_RENDERER | CVAR_INTEGER,
    "Draw debug-only emissive sprite proxies for selected RT smoke lights in mode 14" );

idCVar r_pathTracingLightSpriteRadiusScale(
    "r_pathTracingLightSpriteRadiusScale",
    "0.04",
    CVAR_RENDERER | CVAR_FLOAT,
    "World-space radius scale for RT smoke selected-light sprite proxies" );

idCVar r_pathTracingLightSpriteIntensity(
    "r_pathTracingLightSpriteIntensity",
    "2.5",
    CVAR_RENDERER | CVAR_FLOAT,
    "Intensity multiplier for RT smoke selected-light sprite proxies" );

idCVar r_pathTracingTextureProbeDumpStart(
    "r_pathTracingTextureProbeDumpStart",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "First safe RT smoke texture candidate to print in the one-shot probe dump" );

idCVar r_pathTracingTextureProbeDumpCount(
    "r_pathTracingTextureProbeDumpCount",
    "12",
    CVAR_RENDERER | CVAR_INTEGER,
    "Number of safe RT smoke texture candidates to print in the one-shot probe dump" );

idCVar r_pathTracingTextureTableLimit(
    "r_pathTracingTextureTableLimit",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "Maximum safe captured material textures to bind for RT smoke texture debug modes; 0 = discovery/logging only, max 2048" );

idCVar r_pathTracingTextureTableStart(
    "r_pathTracingTextureTableStart",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "First safe captured diffuse texture candidate to bind for RT smoke debug mode 8" );

idCVar r_pathTracingTextureForceFallback(
    "r_pathTracingTextureForceFallback",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "RT smoke debug mode 8: bind fallback white texture into active bindless slots instead of captured diffuse textures" );

idCVar r_pathTracingTextureSampleEnable(
    "r_pathTracingTextureSampleEnable",
    "1",
    CVAR_RENDERER | CVAR_INTEGER,
    "RT smoke debug mode 8: enable actual bindless texture sampling; 0 keeps the descriptor table active but shades from material fallback colors" );

idCVar r_pathTracingTextureBindlessEnable(
    "r_pathTracingTextureBindlessEnable",
    "1",
    CVAR_RENDERER | CVAR_INTEGER,
    "RT smoke debug mode 8: sample from the bindless texture table; 0 samples a regular fallback texture SRV for diagnostics" );

idCVar r_pathTracingTextureSampleMethod(
    "r_pathTracingTextureSampleMethod",
    "2",
    CVAR_RENDERER | CVAR_INTEGER,
    "RT smoke debug mode 8 texture fetch method: 0 = disabled/fallback color, 1 = SampleLevel diagnostic, 2 = Texture.Load stable default" );

idCVar r_pathTracingTextureFilter(
    "r_pathTracingTextureFilter",
    "1",
    CVAR_RENDERER | CVAR_INTEGER,
    "RT smoke Texture.Load filtering: 0 = point, 1 = manual bilinear" );

idCVar r_pathTracingTextureDecode(
    "r_pathTracingTextureDecode",
    "1",
    CVAR_RENDERER | CVAR_INTEGER,
    "Decode RT smoke material texture encodings such as Doom diffuse YCoCg" );

idCVar r_pathTracingUseNormalMaps(
    "r_pathTracingUseNormalMaps",
    "1",
    CVAR_RENDERER | CVAR_INTEGER,
    "Use sampled normal maps for RT smoke debug mode 14 direct lighting" );

idCVar r_pathTracingUseSpecularMaps(
    "r_pathTracingUseSpecularMaps",
    "1",
    CVAR_RENDERER | CVAR_INTEGER,
    "Use sampled specular maps for RT smoke debug mode 14 direct lighting" );

idCVar r_pathTracingUseEmissiveMaps(
    "r_pathTracingUseEmissiveMaps",
    "1",
    CVAR_RENDERER | CVAR_INTEGER,
    "Use sampled emissive/glow material stages for RT smoke debug mode 14 direct lighting" );

idCVar r_pathTracingSmokeParticleDither(
    "r_pathTracingSmokeParticleDither",
    "1",
    CVAR_RENDERER | CVAR_INTEGER,
    "Use stable alpha dithering for RT smoke particle cards and ignore them for debug shadow rays" );

idCVar r_pathTracingSmokeParticleAlphaScale(
    "r_pathTracingSmokeParticleAlphaScale",
    "0.25",
    CVAR_RENDERER | CVAR_FLOAT,
    "Opacity scale for RT smoke particle-card alpha dithering" );

idCVar r_pathTracingSmokeParticleEdgeFade(
    "r_pathTracingSmokeParticleEdgeFade",
    "1",
    CVAR_RENDERER | CVAR_INTEGER,
    "Fade RT smoke particle-card dither opacity near card UV edges" );

idCVar r_pathTracingPortalWindowStochastic(
    "r_pathTracingPortalWindowStochastic",
    "1",
    CVAR_RENDERER | CVAR_INTEGER,
    "Use stable stochastic transparency for RT smoke portal/window surfaces" );

idCVar r_pathTracingPortalWindowAlphaScale(
    "r_pathTracingPortalWindowAlphaScale",
    "0.35",
    CVAR_RENDERER | CVAR_FLOAT,
    "Opacity scale for RT smoke portal/window stochastic transparency" );

idCVar r_pathTracingPortalWindowMinOpacity(
    "r_pathTracingPortalWindowMinOpacity",
    "0.05",
    CVAR_RENDERER | CVAR_FLOAT,
    "Minimum primary-ray opacity for RT smoke portal/window stochastic transparency" );

idCVar r_pathTracingPortalWindowShadowOpacity(
    "r_pathTracingPortalWindowShadowOpacity",
    "0.05",
    CVAR_RENDERER | CVAR_FLOAT,
    "Shadow-ray opacity multiplier for RT smoke portal/window stochastic transparency" );

idCVar r_pathTracingAdditiveDecalKey(
    "r_pathTracingAdditiveDecalKey",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "Treat additive translucent decal/signage materials as RGB-keyed RT overlays" );

idCVar r_pathTracingAllowGuiTextures(
    "r_pathTracingAllowGuiTextures",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "Allow strictly validated GUI/SWF-like material textures into RT smoke bindless texture diagnostics" );

idCVar r_pathTracingAllowGuiSurfaces(
    "r_pathTracingAllowGuiSurfaces",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "Allow GUI/SWF draw-surface geometry cards into RT smoke capture diagnostics" );

idCVar r_pathTracingSkipCallbackEntities(
    "r_pathTracingSkipCallbackEntities",
    "1",
    CVAR_RENDERER | CVAR_INTEGER,
    "Skip deferred callback render entities in RT smoke capture to avoid item/pickup lifetime hazards" );

idCVar r_pathTracingMaterialMetadataCache(
    "r_pathTracingMaterialMetadataCache",
    "1",
    CVAR_RENDERER | CVAR_INTEGER,
    "Cache RT smoke per-material texture metadata; frame-local material tables are still rebuilt" );

idCVar r_pathTracingSmokeLog(
    "r_pathTracingSmokeLog",
    "1",
    CVAR_RENDERER | CVAR_INTEGER,
    "Enable periodic RT smoke debug logging" );

idCVar r_pathTracingTimingLog(
    "r_pathTracingTimingLog",
    "1",
    CVAR_RENDERER | CVAR_INTEGER,
    "Enable RT smoke slow-frame CPU timing logs" );

idCVar r_pathTracingTimingThreshold(
    "r_pathTracingTimingThreshold",
    "40",
    CVAR_RENDERER | CVAR_INTEGER,
    "RT smoke CPU timing log threshold in milliseconds" );

idCVar r_pathTracingTimingLogInterval(
    "r_pathTracingTimingLogInterval",
    "1000",
    CVAR_RENDERER | CVAR_INTEGER,
    "Minimum milliseconds between repeated RT smoke timing log lines; 0 logs every threshold hit" );

idCVar r_pathTracingReadbackEnable(
    "r_pathTracingReadbackEnable",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "Enable RT smoke UAV readback diagnostics; can stall the GPU/CPU while profiling" );

idCVar r_pathTracingMaterialCache(
    "r_pathTracingMaterialCache",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "Experimental RT smoke material-table cache; currently ignored while material-index stability is validated" );

namespace {

const int RT_SMOKE_MAX_SURFACES = 128;
const int RT_SMOKE_MAX_VERTS = 65536;
const int RT_SMOKE_MAX_INDEXES = 196608;
const int RT_SMOKE_MIN_OUTPUT_WIDTH = 16;
const int RT_SMOKE_MIN_OUTPUT_HEIGHT = 16;
const int RT_SMOKE_MAX_OUTPUT_WIDTH = 3840;
const int RT_SMOKE_MAX_OUTPUT_HEIGHT = 2160;
const int RT_SMOKE_READBACK_INTERVAL_FRAMES = 120;
const int RT_SMOKE_SCENE_LOG_INTERVAL_FRAMES = 120;
const int RT_SMOKE_CLASS_COUNT = 5;
const int RT_SMOKE_TRANSLUCENT_SUBTYPE_COUNT = 7;
const int RT_SMOKE_CLASS_REASON_SAMPLES = 8;
const int RT_SMOKE_MATERIAL_REASON_SAMPLES = 12;
const int RT_SMOKE_TRANSLUCENT_REASON_SAMPLES = 24;
const int RT_SMOKE_TEXTURE_PROBE_CANDIDATE_SAMPLES = 24;
const int RT_SMOKE_TEXTURE_PROBE_DUMP_CANDIDATES = 64;
const int RT_SMOKE_TEXTURE_DESCRIPTOR_CAPACITY = 2048;
const int RT_SMOKE_TEXTURE_EXPERIMENTAL_ACTIVE_CAP = 2048;
const int RT_SMOKE_MAX_DEBUG_LIGHTS = 32;
const int RT_SMOKE_VERTEX_STRIDE = sizeof(PathTraceSmokeVertex);
const uint32_t RT_SMOKE_TRIANGLE_CLASS_MASK = 0x0000ffffu;
const uint32_t RT_SMOKE_TRIANGLE_FORCE_GEOMETRIC_NORMAL = 0x00010000u;
const uint32_t RT_SMOKE_TRANSLUCENT_SUBTYPE_SHIFT = 24u;
const uint32_t RT_SMOKE_TRANSLUCENT_SUBTYPE_MASK = 0x0f000000u;
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

struct PathTraceSmokeConstants
{
    float cameraOriginAndTMax[4];
    float cameraForwardAndTanX[4];
    float cameraLeftAndTanY[4];
    float cameraUpAndDebugMode[4];
    float textureInfo[4];
    float lightOriginAndRadius[RT_SMOKE_MAX_DEBUG_LIGHTS][4];
    float lightColorAndIntensity[RT_SMOKE_MAX_DEBUG_LIGHTS][4];
    float lightInfo[4];
    float portalWindowInfo[4];
    float lightSpriteInfo[4];
};

struct RtSmokeSelectedLight
{
    idVec3 origin = vec3_zero;
    float radius = 0.0f;
    idVec4 color = idVec4(1.0f, 1.0f, 1.0f, 1.0f);
    bool spriteProxy = false;
    int index = -1;
    float distanceSquared = idMath::INFINITUM;
    float score = 0.0f;
    idStr shaderName;
};

bool SmokeNameContainsAny(const idStr& name, const char* const* tokens, int tokenCount);

idVec4 EvaluateSmokeLightColor(const viewLight_t* vLight)
{
    if (!vLight || !vLight->lightShader || !vLight->shaderRegisters)
    {
        return idVec4(1.0f, 1.0f, 1.0f, 1.0f);
    }

    const float lightScale = r_lightScale.GetFloat();
    const idMaterial* lightShader = vLight->lightShader;
    const float* lightRegs = vLight->shaderRegisters;
    for (int lightStageNum = 0; lightStageNum < lightShader->GetNumStages(); lightStageNum++)
    {
        const shaderStage_t* lightStage = lightShader->GetStage(lightStageNum);
        if (!lightStage || !lightRegs[lightStage->conditionRegister])
        {
            continue;
        }

        const int* registers = lightStage->color.registers;
        idVec4 color(
            lightScale * lightRegs[registers[0]],
            lightScale * lightRegs[registers[1]],
            lightScale * lightRegs[registers[2]],
            lightRegs[registers[3]]);
        if (color.x > 0.0f || color.y > 0.0f || color.z > 0.0f)
        {
            color.w = Max(color.x, Max(color.y, color.z));
            return color;
        }
    }

    return idVec4(1.0f, 1.0f, 1.0f, 1.0f);
}

float ScoreSmokePointLight(const idVec3& cameraOrigin, const idVec3& lightOrigin, float radius, const idVec4& color, int selectionMode)
{
    const float distanceSquared = (lightOrigin - cameraOrigin).LengthSqr();
    if (selectionMode <= 0)
    {
        return 1.0f / Max(distanceSquared, 1.0f);
    }

    const float distance = idMath::Sqrt(distanceSquared);
    const float normalizedDistance = distance / Max(radius, 1.0f);
    const float attenuation = idMath::ClampFloat(0.0f, 1.0f, 1.0f - normalizedDistance);
    const float intensity = Max(0.0f, color.w);
    return intensity * (0.001f + attenuation * attenuation);
}

bool IsSmokeLightSpriteProxyCandidate(const idMaterial* lightShader)
{
    if (!lightShader)
    {
        return false;
    }

    idStr shaderName = lightShader->GetName();
    static const char* spriteTokens[] = { "strobe", "beacon", "blink", "warning", "alarm", "flare", "sprite", "glow" };
    return SmokeNameContainsAny(shaderName, spriteTokens, sizeof(spriteTokens) / sizeof(spriteTokens[0]));
}

int CollectSelectedSmokePointLights(const viewDef_t* viewDef, const idVec3& cameraOrigin, RtSmokeSelectedLight* selectedLights, int maxSelectedLights, int selectionMode)
{
    if (!viewDef || !selectedLights || maxSelectedLights <= 0)
    {
        return 0;
    }

    int selectedLightCount = 0;
    for (const viewLight_t* vLight = viewDef->viewLights; vLight != NULL; vLight = vLight->next)
    {
        if (!vLight->pointLight || vLight->parallel || vLight->removeFromList)
        {
            continue;
        }

        float radius = 512.0f;
        if (vLight->lightDef)
        {
            const idVec3& lightRadiusVec = vLight->lightDef->parms.lightRadius;
            radius = Max(lightRadiusVec.x, Max(lightRadiusVec.y, lightRadiusVec.z));
        }

        if (radius <= 1.0f)
        {
            continue;
        }

        const idVec4 color = EvaluateSmokeLightColor(vLight);
        const float distanceSquared = (vLight->globalLightOrigin - cameraOrigin).LengthSqr();
        const float score = ScoreSmokePointLight(cameraOrigin, vLight->globalLightOrigin, radius, color, selectionMode);
        if (score <= 0.0f)
        {
            continue;
        }

        int insertIndex = selectedLightCount;
        if (selectedLightCount == maxSelectedLights)
        {
            insertIndex = -1;
            float worstScore = score;
            for (int i = 0; i < selectedLightCount; i++)
            {
                if (selectedLights[i].score < worstScore)
                {
                    worstScore = selectedLights[i].score;
                    insertIndex = i;
                }
            }

            if (insertIndex < 0)
            {
                continue;
            }
        }
        else
        {
            selectedLightCount++;
        }

        selectedLights[insertIndex].origin = vLight->globalLightOrigin;
        selectedLights[insertIndex].radius = radius;
        selectedLights[insertIndex].color = color;
        selectedLights[insertIndex].index = vLight->lightDef ? vLight->lightDef->index : -1;
        selectedLights[insertIndex].distanceSquared = distanceSquared;
        selectedLights[insertIndex].score = score;
        selectedLights[insertIndex].shaderName = vLight->lightShader ? vLight->lightShader->GetName() : "<none>";
        selectedLights[insertIndex].spriteProxy = IsSmokeLightSpriteProxyCandidate(vLight->lightShader);
    }

    std::sort(selectedLights, selectedLights + selectedLightCount, [](const RtSmokeSelectedLight& a, const RtSmokeSelectedLight& b) {
        if (a.score != b.score)
        {
            return a.score > b.score;
        }
        return a.distanceSquared < b.distanceSquared;
    });
    return selectedLightCount;
}

struct RtSmokeSurfaceClassStats
{
    int staticWorldSurfaces = 0;
    int rigidEntitySurfaces = 0;
    int skinnedDeformedSurfaces = 0;
    int particleAlphaSurfaces = 0;
    int unknownSurfaces = 0;
    int staticWorldVerts = 0;
    int rigidEntityVerts = 0;
    int skinnedDeformedVerts = 0;
    int particleAlphaVerts = 0;
    int unknownVerts = 0;
    int staticWorldIndexes = 0;
    int rigidEntityIndexes = 0;
    int skinnedDeformedIndexes = 0;
    int particleAlphaIndexes = 0;
    int unknownIndexes = 0;
    int staticWorldTriangles = 0;
    int rigidEntityTriangles = 0;
    int skinnedDeformedTriangles = 0;
    int particleAlphaTriangles = 0;
    int unknownTriangles = 0;
};

struct RtSmokeSurfaceSkipStats
{
    int nullSurface = 0;
    int missingGeometry = 0;
    int nullMaterial = 0;
    int nullSpace = 0;
    int nullModel = 0;
    int invalidIndexCount = 0;
    int nonCurrentCache = 0;
    int limitExceeded = 0;
    int zeroAreaOnly = 0;
    int emptyClassBuffer = 0;
    int guiSurface = 0;
    int callbackEntity = 0;
};

struct RtSmokeDynamicGeometryStats
{
    int rigidSurfaces = 0;
    int skinnedCpuCurrentSurfaces = 0;
    int skinnedLikelyBasePoseSurfaces = 0;
    int skinnedRtCpuSkinnedSurfaces = 0;
    int particleAlphaSurfaces = 0;
    int unknownSurfaces = 0;
    int rigidIndexes = 0;
    int skinnedCpuCurrentIndexes = 0;
    int skinnedLikelyBasePoseIndexes = 0;
    int skinnedRtCpuSkinnedIndexes = 0;
    int particleAlphaIndexes = 0;
    int unknownIndexes = 0;
};

struct RtSmokeAttributeClassStats
{
    int invalidNormalVerts = 0;
    int invalidNormalTriangles = 0;
    int invalidUvVerts = 0;
    int invalidUvTriangles = 0;
    int forcedGeometricNormalTriangles = 0;
};

struct RtSmokeAttributeStats
{
    RtSmokeAttributeClassStats classes[RT_SMOKE_CLASS_COUNT];
};

struct RtSmokeMaterialSample
{
    uint32_t id = 0;
    int surfaces = 0;
    int triangles = 0;
    idStr name;
};

struct RtSmokeTranslucentClassifierInfo
{
    bool sortIsGuiOrSubview = false;
    bool sortIsDecal = false;
    bool sortIsPostProcess = false;
    bool polygonOffsetDecal = false;
    bool hasScreenTexgen = false;
    bool hasAdditiveBlend = false;
    bool hasAmbientStage = false;
    bool hasAmbientBlendStage = false;
    bool hasDiffuseStage = false;
    bool nameLooksGui = false;
    bool nameLooksParticle = false;
    bool nameLooksDecal = false;
    bool nameLooksGlass = false;
    bool nameLooksGlow = false;
    bool nameLooksSignage = false;
};

enum class RtSmokeTranslucentSubtype
{
    DecalGrime,
    ObjectGlass,
    SmokeParticle,
    SignageGlow,
    PortalWindow,
    GuiScreen,
    Unknown
};

struct RtSmokeTranslucentSubtypeDebugSample
{
    bool valid = false;
    RtSmokeTranslucentSubtype subtype = RtSmokeTranslucentSubtype::Unknown;
    int surfaceIndex = -1;
    int verts = 0;
    int indexes = 0;
    idStr materialName;
    materialCoverage_t coverage = MC_BAD;
    float sort = SS_BAD;
    deform_t deform = DFRM_NONE;
    RtSmokeTranslucentClassifierInfo info;
};

struct RtSmokeMaterialStats
{
    int totalSurfaces = 0;
    int totalTriangles = 0;
    int uniqueMaterials = 0;
    int translucentSurfaces = 0;
    int translucentTriangles = 0;
    int translucentUniqueMaterials = 0;
    std::vector<uint32_t> materialIds;
    std::vector<uint32_t> translucentMaterialIds;
    RtSmokeMaterialSample samples[RT_SMOKE_MATERIAL_REASON_SAMPLES];
    RtSmokeMaterialSample translucentSamples[RT_SMOKE_MATERIAL_REASON_SAMPLES];
    int sampleCount = 0;
    int translucentSampleCount = 0;
    int translucentSubtypeSurfaces[RT_SMOKE_TRANSLUCENT_SUBTYPE_COUNT] = {};
    int translucentSubtypeTriangles[RT_SMOKE_TRANSLUCENT_SUBTYPE_COUNT] = {};
    RtSmokeMaterialSample translucentSubtypeSamples[RT_SMOKE_TRANSLUCENT_SUBTYPE_COUNT][RT_SMOKE_MATERIAL_REASON_SAMPLES];
    int translucentSubtypeSampleCounts[RT_SMOKE_TRANSLUCENT_SUBTYPE_COUNT] = {};
    RtSmokeTranslucentSubtypeDebugSample translucentDebugSamples[RT_SMOKE_TRANSLUCENT_REASON_SAMPLES];
    int translucentDebugSampleCount = 0;
};

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

struct RtSmokeMaterialTableCache
{
    bool valid = false;
    uint64 signature = 0;
    RtSmokeMaterialTableBuild table;
    int hits = 0;
    int misses = 0;
};

struct RtSmokeMaterialTextureInfo
{
    uint32_t materialId = 0;
    idStr materialName;
    idStr diffuseImageName;
    idStr alphaImageName;
    idStr normalImageName;
    idStr specularImageName;
    idStr emissiveImageName;
    idStr fallbackReason;
    idStr alphaReason;
    idStr normalReason;
    idStr specularReason;
    idStr emissiveReason;
    idImage* diffuseImage = nullptr;
    idImage* alphaImage = nullptr;
    idImage* normalImage = nullptr;
    idImage* specularImage = nullptr;
    idImage* emissiveImage = nullptr;
    bool hasDiffuseImage = false;
    bool hasAlphaImage = false;
    bool hasNormalImage = false;
    bool hasSpecularImage = false;
    bool hasEmissiveImage = false;
    bool hasTextureHandle = false;
    bool hasAlphaTextureHandle = false;
    bool hasNormalTextureHandle = false;
    bool hasSpecularTextureHandle = false;
    bool hasEmissiveTextureHandle = false;
    bool hasSafeTexture = false;
    bool hasSafeAlphaTexture = false;
    bool hasSafeNormalTexture = false;
    bool hasSafeSpecularTexture = false;
    bool hasSafeEmissiveTexture = false;
    bool hasAlphaTest = false;
    bool additiveDecal = false;
    bool filterDecal = false;
    bool filterDecalBlackKey = false;
    bool alphaFromDiffuseLuma = false;
    bool forceFallbackAlbedo = false;
    bool alphaFromDiffuseDarkKey = false;
    bool portalWindowFallback = false;
    bool objectGlassFallback = false;
    bool emissive = false;
    float alphaCutoff = 0.0f;
    idVec4 emissiveColor = idVec4(0.0f, 0.0f, 0.0f, 1.0f);
    idVec4 fallbackAlbedo = idVec4(0.0f, 0.0f, 0.0f, 1.0f);
    bool hasFallbackAlbedo = false;
    textureColor_t diffuseColorFormat = CFM_DEFAULT;
    textureColor_t alphaColorFormat = CFM_DEFAULT;
    textureColor_t normalColorFormat = CFM_DEFAULT;
    textureColor_t specularColorFormat = CFM_DEFAULT;
    textureColor_t emissiveColorFormat = CFM_DEFAULT;
    materialCoverage_t coverage = MC_BAD;
    int tableIndex = -1;
};

struct RtSmokeMaterialMetadataFrameStats
{
    int registrations = 0;
    int cacheRefreshes = 0;
    int fullDiscovers = 0;
    int newEntries = 0;
};

struct RtSmokeBucketRange
{
    int vertexOffset = 0;
    int vertexCount = 0;
    int indexOffset = 0;
    int indexCount = 0;
    int triangleOffset = 0;
    int triangleCount = 0;
    int surfaceCount = 0;
};

struct RtSmokeBucketRanges
{
    RtSmokeBucketRange buckets[RT_SMOKE_CLASS_COUNT];
};

struct RtSmokeTextureCoverageClassStats
{
    int triangles = 0;
    int boundTriangles = 0;
    int fallbackTriangles = 0;
    int invalidMaterialTriangles = 0;
};

struct RtSmokeTextureCoverageStats
{
    RtSmokeTextureCoverageClassStats classes[RT_SMOKE_CLASS_COUNT];
    int materials = 0;
    int boundMaterials = 0;
    int fallbackMaterials = 0;
};

struct RtSmokeStaticBlasSignature
{
    uint64 hash = 0;
    int vertexCount = 0;
    int indexCount = 0;
    int triangleCount = 0;
};

enum class RtSmokeSurfaceClass
{
    StaticWorld,
    RigidEntity,
    SkinnedDeformed,
    ParticleAlpha,
    Unknown
};

std::vector<RtSmokeMaterialTextureInfo> g_smokeMaterialTextureRegistry;
std::unordered_map<uint32_t, int> g_smokeMaterialTextureRegistryLookup;
RtSmokeMaterialTableCache g_smokeMaterialTableCache;
RtSmokeMaterialMetadataFrameStats g_smokeMaterialMetadataFrameStats;
int g_smokeLastSceneTimingLogMs = -1000000;
int g_smokeLastDispatchTimingLogMs = -1000000;
int g_smokeLastReadbackTimingLogMs = -1000000;

bool ShouldLogSmokeTiming(int elapsedMs, int nowMs, int& lastLogMs)
{
    if (r_pathTracingTimingLog.GetInteger() == 0)
    {
        return false;
    }

    if (elapsedMs < Max(0, r_pathTracingTimingThreshold.GetInteger()))
    {
        return false;
    }

    const int intervalMs = Max(0, r_pathTracingTimingLogInterval.GetInteger());
    if (intervalMs > 0 && nowMs - lastLogMs < intervalMs)
    {
        return false;
    }

    lastLogMs = nowMs;
    return true;
}

int GetSmokeTextureTableRequestedLimit()
{
    return idMath::ClampInt(0, RT_SMOKE_TEXTURE_DESCRIPTOR_CAPACITY, r_pathTracingTextureTableLimit.GetInteger());
}

int GetSmokeTextureTableEffectiveLimit()
{
    return Min(GetSmokeTextureTableRequestedLimit(), RT_SMOKE_TEXTURE_EXPERIMENTAL_ACTIVE_CAP);
}

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
    hash = HashSmokeMaterialCacheValue(hash, static_cast<uint64>(g_smokeMaterialTextureRegistry.size()));
    return hash;
}

struct RtSmokeSurfaceClassReason
{
    bool valid = false;
    RtSmokeSurfaceClass finalClass = RtSmokeSurfaceClass::Unknown;
    int surfaceIndex = -1;
    int verts = 0;
    int indexes = 0;
    idStr materialName = "<none>";
    materialCoverage_t coverage = MC_BAD;
    float sort = SS_BAD;
    deform_t deform = DFRM_NONE;
    int entityNum = -1;
    idStr modelName = "<none>";
    dynamicModel_t dynamicModel = DM_STATIC;
    bool hasJointCache = false;
    bool hasStaticModelWithJoints = false;
    bool hasRenderEntityJoints = false;
    bool ambientCacheStatic = false;
    bool indexCacheStatic = false;
    bool isWorldSpace = false;
    bool isStaticWorldModel = false;
    bool hasEntityDef = false;
    bool hasDynamicModel = false;
    bool hasCachedDynamicModel = false;
    bool hasCallback = false;
    bool forceUpdate = false;
    bool weaponDepthHack = false;
    float modelDepthHack = 0.0f;
    bool cpuVertsAvailable = false;
    bool cpuVertexCacheCurrent = false;
    bool cpuIndexCacheCurrent = false;
    bool skinnedLikelyBasePose = false;
    bool rtCpuSkinned = false;
    idVec3 entityOrigin = vec3_origin;
    idMat3 entityAxis = mat3_identity;
    idBounds entityBounds;
    idBounds surfaceBounds;
    idBounds localReferenceBounds;
    idBounds globalReferenceBounds;
    bool hasEntityBounds = false;
    bool hasSurfaceBounds = false;
    bool hasReferenceBounds = false;
};

struct RtSmokeSurfaceClassReasonSamples
{
    RtSmokeSurfaceClassReason samples[RT_SMOKE_CLASS_COUNT][RT_SMOKE_CLASS_REASON_SAMPLES];
    int counts[RT_SMOKE_CLASS_COUNT] = {};
    RtSmokeSurfaceClassReason skinnedSamples[RT_SMOKE_CLASS_REASON_SAMPLES];
    int skinnedCount = 0;
};

uint32_t SmokeSurfaceClassId(RtSmokeSurfaceClass surfaceClass)
{
    switch (surfaceClass)
    {
        case RtSmokeSurfaceClass::StaticWorld:
            return 0;
        case RtSmokeSurfaceClass::RigidEntity:
            return 1;
        case RtSmokeSurfaceClass::SkinnedDeformed:
            return 2;
        case RtSmokeSurfaceClass::ParticleAlpha:
            return 3;
        default:
            return 4;
    }
}

const char* SmokeSurfaceClassName(RtSmokeSurfaceClass surfaceClass)
{
    switch (surfaceClass)
    {
        case RtSmokeSurfaceClass::StaticWorld:
            return "static";
        case RtSmokeSurfaceClass::RigidEntity:
            return "rigid-entity";
        case RtSmokeSurfaceClass::SkinnedDeformed:
            return "skinned";
        case RtSmokeSurfaceClass::ParticleAlpha:
            return "particle/alpha";
        default:
            return "unknown";
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

const char* SmokeSurfaceClassNameByIndex(int classIndex)
{
    if (classIndex < 0 || classIndex >= RT_SMOKE_CLASS_COUNT)
    {
        return "invalid";
    }

    return SmokeSurfaceClassName(static_cast<RtSmokeSurfaceClass>(classIndex));
}

uint32_t SmokeTranslucentSubtypeId(RtSmokeTranslucentSubtype subtype)
{
    switch (subtype)
    {
        case RtSmokeTranslucentSubtype::DecalGrime:
            return 0;
        case RtSmokeTranslucentSubtype::ObjectGlass:
            return 1;
        case RtSmokeTranslucentSubtype::SmokeParticle:
            return 2;
        case RtSmokeTranslucentSubtype::SignageGlow:
            return 3;
        case RtSmokeTranslucentSubtype::GuiScreen:
            return 5;
        case RtSmokeTranslucentSubtype::PortalWindow:
            return 4;
        default:
            return 6;
    }
}

const char* SmokeTranslucentSubtypeName(RtSmokeTranslucentSubtype subtype)
{
    switch (subtype)
    {
        case RtSmokeTranslucentSubtype::DecalGrime:
            return "decal/grime";
        case RtSmokeTranslucentSubtype::ObjectGlass:
            return "object-glass";
        case RtSmokeTranslucentSubtype::SmokeParticle:
            return "smoke/particle";
        case RtSmokeTranslucentSubtype::SignageGlow:
            return "signage/glow";
        case RtSmokeTranslucentSubtype::PortalWindow:
            return "portal/window";
        case RtSmokeTranslucentSubtype::GuiScreen:
            return "gui/screen";
        default:
            return "unknown";
    }
}

const char* SmokeTranslucentSubtypeNameByIndex(int subtypeIndex)
{
    if (subtypeIndex < 0 || subtypeIndex >= RT_SMOKE_TRANSLUCENT_SUBTYPE_COUNT)
    {
        return "invalid";
    }

    return SmokeTranslucentSubtypeName(static_cast<RtSmokeTranslucentSubtype>(subtypeIndex));
}

bool SmokeNameContainsAny(const idStr& name, const char* const* tokens, int tokenCount)
{
    for (int tokenIndex = 0; tokenIndex < tokenCount; ++tokenIndex)
    {
        if (name.Find(tokens[tokenIndex], false) >= 0)
        {
            return true;
        }
    }
    return false;
}

RtSmokeTranslucentClassifierInfo BuildSmokeTranslucentClassifierInfo(const idMaterial* material)
{
    RtSmokeTranslucentClassifierInfo info;
    if (!material)
    {
        return info;
    }

    idStr materialName = material->GetName();
    const float sort = material->GetSort();
    info.sortIsGuiOrSubview = sort <= SS_GUI;
    info.sortIsDecal = sort >= SS_DECAL && sort < SS_FAR;
    info.sortIsPostProcess = sort >= SS_POST_PROCESS;
    info.polygonOffsetDecal = material->TestMaterialFlag(MF_POLYGONOFFSET);

    static const char* guiTokens[] = { "gui", "guis/", "video", "cinematic", "terminal", "console", "pda", "cursor" };
    static const char* particleTokens[] = { "particle", "smoke", "dust", "steam", "fog", "muzzle", "spark", "bloodcloud" };
    static const char* decalTokens[] = { "decal", "stain", "grime", "dirt", "scorch", "burn", "bullet", "mud", "blood", "splat", "mark" };
    static const char* glassTokens[] = { "glass", "window", "visor", "transparent" };
    static const char* glowTokens[] = { "glow", "light", "lamp", "beam", "flare", "strip", "striplight", "tube", "neon", "emissive", "emit", "bulb", "fluoro", "flouro" };
    static const char* signageTokens[] = { "logo", "sign", "label", "snack", "soda", "cola", "add", "screen", "monitor" };
    info.nameLooksGui = SmokeNameContainsAny(materialName, guiTokens, sizeof(guiTokens) / sizeof(guiTokens[0]));
    info.nameLooksParticle = SmokeNameContainsAny(materialName, particleTokens, sizeof(particleTokens) / sizeof(particleTokens[0]));
    info.nameLooksDecal = SmokeNameContainsAny(materialName, decalTokens, sizeof(decalTokens) / sizeof(decalTokens[0]));
    info.nameLooksGlass = SmokeNameContainsAny(materialName, glassTokens, sizeof(glassTokens) / sizeof(glassTokens[0]));
    info.nameLooksGlow = SmokeNameContainsAny(materialName, glowTokens, sizeof(glowTokens) / sizeof(glowTokens[0]));
    info.nameLooksSignage = SmokeNameContainsAny(materialName, signageTokens, sizeof(signageTokens) / sizeof(signageTokens[0]));

    for (int stageIndex = 0; stageIndex < material->GetNumStages(); ++stageIndex)
    {
        const shaderStage_t* stage = material->GetStage(stageIndex);
        if (!stage)
        {
            continue;
        }

        if (stage->texture.texgen == TG_SCREEN || stage->texture.texgen == TG_SCREEN2)
        {
            info.hasScreenTexgen = true;
        }
        if (stage->lighting == SL_AMBIENT)
        {
            info.hasAmbientStage = true;
        }
        else if (stage->lighting == SL_DIFFUSE)
        {
            info.hasDiffuseStage = true;
        }

        const uint64 srcBlend = stage->drawStateBits & GLS_SRCBLEND_BITS;
        const uint64 dstBlend = stage->drawStateBits & GLS_DSTBLEND_BITS;
        if ((srcBlend == GLS_SRCBLEND_ONE || srcBlend == GLS_SRCBLEND_SRC_ALPHA) && dstBlend == GLS_DSTBLEND_ONE)
        {
            info.hasAdditiveBlend = true;
        }
        if (stage->lighting == SL_AMBIENT && (dstBlend != GLS_DSTBLEND_ZERO || srcBlend == GLS_SRCBLEND_DST_COLOR || srcBlend == GLS_SRCBLEND_ONE_MINUS_DST_COLOR))
        {
            info.hasAmbientBlendStage = true;
        }
    }

    return info;
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

void ResolveSmokeMaterialAlphaInfo(const idMaterial* material, bool& hasAlphaTest, float& alphaCutoff)
{
    hasAlphaTest = false;
    alphaCutoff = 0.0f;
    if (!material)
    {
        return;
    }

    const RtSmokeTranslucentClassifierInfo classifier = BuildSmokeTranslucentClassifierInfo(material);
    const bool allowTranslucentCutout =
        material->Coverage() == MC_TRANSLUCENT &&
        !classifier.hasScreenTexgen &&
        !classifier.nameLooksGui &&
        !classifier.nameLooksParticle &&
        (classifier.nameLooksGlass || classifier.nameLooksSignage || classifier.nameLooksGlow);
    if (material->Coverage() != MC_PERFORATED && !allowTranslucentCutout)
    {
        return;
    }

    const float* constantRegisters = material->ConstantRegisters();
    const int registerCount = material->GetNumRegisters();
    for (int stageIndex = 0; stageIndex < material->GetNumStages(); ++stageIndex)
    {
        const shaderStage_t* stage = material->GetStage(stageIndex);
        if (!stage || !stage->hasAlphaTest || stage->ignoreAlphaTest)
        {
            continue;
        }

        hasAlphaTest = true;
        alphaCutoff = 0.5f;
        if (constantRegisters && stage->alphaTestRegister >= 0 && stage->alphaTestRegister < registerCount)
        {
            alphaCutoff = idMath::ClampFloat(0.0f, 1.0f, constantRegisters[stage->alphaTestRegister]);
        }
        return;
    }

    if (material->Coverage() == MC_PERFORATED)
    {
        hasAlphaTest = true;
        alphaCutoff = 0.5f;
    }
}

RtSmokeMaterialTextureInfo ResolveSmokeMaterialTextureInfo(uint32_t materialId, int tableIndex);
bool IsSmokePortalWindowFallbackMaterial(const idMaterial* material);
bool IsSmokeObjectGlassFallbackMaterial(const idMaterial* material);
bool IsSmokeTranslucentOverlayCardMaterial(const idMaterial* material, const RtSmokeTranslucentClassifierInfo& classifier);

bool IsSmokeAdditiveDecalMaterial(const idMaterial* material)
{
    if (!material)
    {
        return false;
    }

    const RtSmokeTranslucentClassifierInfo info = BuildSmokeTranslucentClassifierInfo(material);
    if (!info.hasAdditiveBlend)
    {
        return false;
    }

    if (info.hasScreenTexgen || info.nameLooksGui || info.nameLooksParticle || info.nameLooksGlass)
    {
        return false;
    }

    return material->Coverage() == MC_TRANSLUCENT ||
        info.hasAmbientStage ||
        info.sortIsDecal ||
        info.polygonOffsetDecal ||
        info.nameLooksDecal ||
        info.nameLooksSignage ||
        info.nameLooksGlow;
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
    if (info.additiveDecal && r_pathTracingAdditiveDecalKey.GetInteger() != 0)
    {
        material.flags |= RT_SMOKE_MATERIAL_ADDITIVE_DECAL;
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
    if (!info.hasDiffuseImage)
    {
        return "unsupported/no diffuse image";
    }
    if (!info.hasTextureHandle)
    {
        return "missing texture handle";
    }
    if (!info.hasSafeTexture)
    {
        return "unsafe/rejected image";
    }

    return "texture table limit/window";
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
            albedo = idVec4(0.18f, 0.26f, 0.36f, 1.0f);
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

    const RtSmokeTranslucentClassifierInfo classifier = BuildSmokeTranslucentClassifierInfo(material);
    if (classifier.hasScreenTexgen ||
        classifier.sortIsGuiOrSubview ||
        classifier.sortIsPostProcess ||
        classifier.nameLooksGui ||
        classifier.nameLooksParticle ||
        classifier.nameLooksGlass)
    {
        reason = "rejected gui/particle/glass/view-dependent material";
        return nullptr;
    }

    const bool nameLooksEmissive = classifier.nameLooksGlow || classifier.nameLooksSignage;
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
        const bool stageLooksEmissive = additiveStage || (nameLooksEmissive && classifier.hasAmbientBlendStage && !classifier.nameLooksDecal);
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
        !classifier.nameLooksGui &&
        !classifier.nameLooksParticle &&
        (classifier.nameLooksGlass || classifier.nameLooksSignage || classifier.nameLooksGlow);
    if (material->Coverage() != MC_PERFORATED && !allowTranslucentCutout)
    {
        reason = "not perforated or translucent cutout";
        return nullptr;
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

    if (allowTranslucentCutout)
    {
        for (int stageIndex = 0; stageIndex < material->GetNumStages(); ++stageIndex)
        {
            const shaderStage_t* stage = material->GetStage(stageIndex);
            if (!stage || !stage->hasAlphaTest || stage->ignoreAlphaTest || !stage->texture.image)
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

void RegisterSmokeMaterialTextureInfo(const idMaterial* material)
{
    ++g_smokeMaterialMetadataFrameStats.registrations;

    const char* materialName = material ? material->GetName() : "<none>";
    const uint32_t materialId = HashSmokeMaterialName(materialName);

    RtSmokeMaterialTextureInfo* info = FindSmokeMaterialTextureInfo(materialId);
    if (info && r_pathTracingMaterialMetadataCache.GetInteger() != 0)
    {
        const RtSmokeTranslucentClassifierInfo classifier = BuildSmokeTranslucentClassifierInfo(material);
        const bool rediscoverGuiDiffuse =
            r_pathTracingAllowGuiTextures.GetInteger() != 0 &&
            (material && (material->HasGui() || classifier.nameLooksGui || classifier.sortIsGuiOrSubview)) &&
            !info->hasDiffuseImage;
        if (!rediscoverGuiDiffuse)
        {
            ++g_smokeMaterialMetadataFrameStats.cacheRefreshes;
            RefreshSmokeMaterialTextureHandleState(*info);
            return;
        }
    }

    if (!info)
    {
        ++g_smokeMaterialMetadataFrameStats.newEntries;
        RtSmokeMaterialTextureInfo newInfo;
        newInfo.materialId = materialId;
        newInfo.materialName = materialName;
        g_smokeMaterialTextureRegistry.push_back(newInfo);
        g_smokeMaterialTextureRegistryLookup[materialId] = static_cast<int>(g_smokeMaterialTextureRegistry.size() - 1);
        info = &g_smokeMaterialTextureRegistry.back();
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
    info->diffuseColorFormat = diffuseImage ? diffuseImage->GetOpts().colorFormat : CFM_DEFAULT;
    info->alphaColorFormat = alphaImage ? alphaImage->GetOpts().colorFormat : CFM_DEFAULT;
    info->normalColorFormat = normalImage ? normalImage->GetOpts().colorFormat : CFM_DEFAULT;
    info->specularColorFormat = specularImage ? specularImage->GetOpts().colorFormat : CFM_DEFAULT;
    info->emissiveColorFormat = emissiveImage ? emissiveImage->GetOpts().colorFormat : CFM_DEFAULT;
    ResolveSmokeMaterialAlphaInfo(material, info->hasAlphaTest, info->alphaCutoff);
    info->additiveDecal = IsSmokeAdditiveDecalMaterial(material);
    const RtSmokeTranslucentClassifierInfo classifier = BuildSmokeTranslucentClassifierInfo(material);
    info->filterDecal = IsSmokeTranslucentOverlayCardMaterial(material, classifier);
    if (info->filterDecal)
    {
        for (int stageIndex = 0; stageIndex < material->GetNumStages(); ++stageIndex)
        {
            const shaderStage_t* stage = material->GetStage(stageIndex);
            if (!stage || stage->lighting != SL_AMBIENT || !stage->texture.image)
            {
                continue;
            }

            const uint64_t srcBlend = stage->drawStateBits & GLS_SRCBLEND_BITS;
            const uint64_t dstBlend = stage->drawStateBits & GLS_DSTBLEND_BITS;
            info->filterDecalBlackKey =
                srcBlend == GLS_SRCBLEND_ZERO &&
                dstBlend == GLS_DSTBLEND_ONE_MINUS_SRC_COLOR;
            break;
        }
    }
    info->alphaFromDiffuseLuma =
        info->hasAlphaTest &&
        diffuseImage != nullptr &&
        alphaImage == diffuseImage &&
        diffuseImage->GetOpts().colorFormat == CFM_YCOCG_DXT5;
    info->forceFallbackAlbedo = IsSmokeReflectiveEyewearMaterial(material);
    info->alphaFromDiffuseDarkKey = info->alphaFromDiffuseLuma && info->forceFallbackAlbedo;
    info->portalWindowFallback = IsSmokePortalWindowFallbackMaterial(material);
    info->objectGlassFallback = IsSmokeObjectGlassFallbackMaterial(material);
    info->emissiveColor = emissiveColor;
    info->emissive = info->hasEmissiveImage || emissiveColor.x > 0.0f || emissiveColor.y > 0.0f || emissiveColor.z > 0.0f;
    info->fallbackAlbedo = fallbackAlbedo;
    info->hasFallbackAlbedo = hasFallbackAlbedo;
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

void AddSmokeMaterialStats(RtSmokeMaterialStats& stats, const idMaterial* material, int indexes, RtSmokeSurfaceClass surfaceClass, RtSmokeTranslucentSubtype translucentSubtype)
{
    const char* materialName = material ? material->GetName() : "<none>";
    const uint32_t materialId = HashSmokeMaterialName(materialName);
    ++stats.totalSurfaces;
    stats.totalTriangles += indexes / 3;

    const bool firstMaterial = std::find(stats.materialIds.begin(), stats.materialIds.end(), materialId) == stats.materialIds.end();
    if (firstMaterial)
    {
        stats.materialIds.push_back(materialId);
        ++stats.uniqueMaterials;
    }

    for (int sampleIndex = 0; sampleIndex < stats.sampleCount; ++sampleIndex)
    {
        RtSmokeMaterialSample& sample = stats.samples[sampleIndex];
        if (sample.id == materialId)
        {
            ++sample.surfaces;
            sample.triangles += indexes / 3;
            return;
        }
    }

    if (stats.sampleCount < RT_SMOKE_MATERIAL_REASON_SAMPLES)
    {
        RtSmokeMaterialSample& sample = stats.samples[stats.sampleCount];
        sample.id = materialId;
        sample.surfaces = 1;
        sample.triangles = indexes / 3;
        sample.name = materialName;
        ++stats.sampleCount;
    }

    if (surfaceClass != RtSmokeSurfaceClass::ParticleAlpha)
    {
        return;
    }

    const int subtypeIndex = idMath::ClampInt(0, RT_SMOKE_TRANSLUCENT_SUBTYPE_COUNT - 1, static_cast<int>(SmokeTranslucentSubtypeId(translucentSubtype)));
    ++stats.translucentSubtypeSurfaces[subtypeIndex];
    stats.translucentSubtypeTriangles[subtypeIndex] += indexes / 3;

    bool subtypeSampleFound = false;
    for (int sampleIndex = 0; sampleIndex < stats.translucentSubtypeSampleCounts[subtypeIndex]; ++sampleIndex)
    {
        RtSmokeMaterialSample& sample = stats.translucentSubtypeSamples[subtypeIndex][sampleIndex];
        if (sample.id == materialId)
        {
            ++sample.surfaces;
            sample.triangles += indexes / 3;
            subtypeSampleFound = true;
            break;
        }
    }

    if (!subtypeSampleFound && stats.translucentSubtypeSampleCounts[subtypeIndex] < RT_SMOKE_MATERIAL_REASON_SAMPLES)
    {
        RtSmokeMaterialSample& sample = stats.translucentSubtypeSamples[subtypeIndex][stats.translucentSubtypeSampleCounts[subtypeIndex]];
        sample.id = materialId;
        sample.surfaces = 1;
        sample.triangles = indexes / 3;
        sample.name = materialName;
        ++stats.translucentSubtypeSampleCounts[subtypeIndex];
    }

    ++stats.translucentSurfaces;
    stats.translucentTriangles += indexes / 3;
    const bool firstTranslucentMaterial = std::find(stats.translucentMaterialIds.begin(), stats.translucentMaterialIds.end(), materialId) == stats.translucentMaterialIds.end();
    if (firstTranslucentMaterial)
    {
        stats.translucentMaterialIds.push_back(materialId);
        ++stats.translucentUniqueMaterials;
    }

    for (int sampleIndex = 0; sampleIndex < stats.translucentSampleCount; ++sampleIndex)
    {
        RtSmokeMaterialSample& sample = stats.translucentSamples[sampleIndex];
        if (sample.id == materialId)
        {
            ++sample.surfaces;
            sample.triangles += indexes / 3;
            return;
        }
    }

    if (stats.translucentSampleCount < RT_SMOKE_MATERIAL_REASON_SAMPLES)
    {
        RtSmokeMaterialSample& sample = stats.translucentSamples[stats.translucentSampleCount];
        sample.id = materialId;
        sample.surfaces = 1;
        sample.triangles = indexes / 3;
        sample.name = materialName;
        ++stats.translucentSampleCount;
    }
}

void AddSmokeTranslucentDebugSample(RtSmokeMaterialStats& stats, const drawSurf_t* drawSurf, const srfTriangles_t* tri, int surfaceIndex, RtSmokeTranslucentSubtype subtype)
{
    if (stats.translucentDebugSampleCount >= RT_SMOKE_TRANSLUCENT_REASON_SAMPLES)
    {
        return;
    }

    const idMaterial* material = drawSurf ? drawSurf->material : nullptr;
    RtSmokeTranslucentSubtypeDebugSample& sample = stats.translucentDebugSamples[stats.translucentDebugSampleCount++];
    sample.valid = true;
    sample.subtype = subtype;
    sample.surfaceIndex = surfaceIndex;
    sample.verts = tri ? tri->numVerts : 0;
    sample.indexes = tri ? tri->numIndexes : 0;
    sample.materialName = material ? material->GetName() : "<none>";
    sample.coverage = material ? material->Coverage() : MC_BAD;
    sample.sort = material ? material->GetSort() : SS_BAD;
    sample.deform = material ? material->Deform() : DFRM_NONE;
    sample.info = BuildSmokeTranslucentClassifierInfo(material);
}

bool SmokeFloatIsFinite(float value)
{
    return std::isfinite(value) != 0;
}

bool SmokeVec2IsFinite(const idVec2& value)
{
    return SmokeFloatIsFinite(value.x) && SmokeFloatIsFinite(value.y);
}

bool SmokeVec3IsFinite(const idVec3& value)
{
    return SmokeFloatIsFinite(value.x) && SmokeFloatIsFinite(value.y) && SmokeFloatIsFinite(value.z);
}

idVec3 SmokeVertexPosition(const PathTraceSmokeVertex& vertex)
{
    return idVec3(vertex.position[0], vertex.position[1], vertex.position[2]);
}

idVec3 SmokeVertexNormal(const PathTraceSmokeVertex& vertex)
{
    return idVec3(vertex.normal[0], vertex.normal[1], vertex.normal[2]);
}

idVec2 SmokeVertexTexCoord(const PathTraceSmokeVertex& vertex)
{
    return idVec2(vertex.texCoord[0], vertex.texCoord[1]);
}

bool SmokeNormalIsUsable(const idVec3& normal)
{
    return SmokeVec3IsFinite(normal) && normal.LengthSqr() > 1.0e-8f;
}

bool SmokeTexCoordIsUsable(const idVec2& texCoord)
{
    return SmokeVec2IsFinite(texCoord) && idMath::Fabs(texCoord.x) < 1.0e6f && idMath::Fabs(texCoord.y) < 1.0e6f;
}

void TransformSurfacePointToWorld(const drawSurf_t* drawSurf, const idVec3& localPoint, idVec3& worldPoint)
{
    if (drawSurf->space)
    {
        R_LocalPointToGlobal(drawSurf->space->modelMatrix, localPoint, worldPoint);
        return;
    }

    worldPoint = localPoint;
}

void TransformSurfaceVectorToWorld(const drawSurf_t* drawSurf, const idVec3& localVector, idVec3& worldVector)
{
    if (drawSurf->space)
    {
        R_LocalVectorToGlobal(drawSurf->space->modelMatrix, localVector, worldVector);
        return;
    }

    worldVector = localVector;
}

const idJointMat* GetSmokeRtCpuSkinningJoints(const srfTriangles_t* tri)
{
    if (!r_useGPUSkinning.GetBool() || !tri || !tri->staticModelWithJoints || !tri->staticModelWithJoints->jointsInverted)
    {
        return nullptr;
    }

    return tri->staticModelWithJoints->jointsInverted;
}

idVec3 TransformSmokeSkinnedVertexPosition(const idDrawVert& base, const idJointMat* joints)
{
    const idJointMat& j0 = joints[base.color[0]];
    const idJointMat& j1 = joints[base.color[1]];
    const idJointMat& j2 = joints[base.color[2]];
    const idJointMat& j3 = joints[base.color[3]];

    const float w0 = base.color2[0] * (1.0f / 255.0f);
    const float w1 = base.color2[1] * (1.0f / 255.0f);
    const float w2 = base.color2[2] * (1.0f / 255.0f);
    const float w3 = base.color2[3] * (1.0f / 255.0f);

    idJointMat accum;
    idJointMat::Mul(accum, j0, w0);
    idJointMat::Mad(accum, j1, w1);
    idJointMat::Mad(accum, j2, w2);
    idJointMat::Mad(accum, j3, w3);

    return accum * idVec4(base.xyz.x, base.xyz.y, base.xyz.z, 1.0f);
}

idVec3 TransformSmokeSkinnedVertexNormal(const idDrawVert& base, const idJointMat* joints)
{
    const idJointMat& j0 = joints[base.color[0]];
    const idJointMat& j1 = joints[base.color[1]];
    const idJointMat& j2 = joints[base.color[2]];
    const idJointMat& j3 = joints[base.color[3]];

    const float w0 = base.color2[0] * (1.0f / 255.0f);
    const float w1 = base.color2[1] * (1.0f / 255.0f);
    const float w2 = base.color2[2] * (1.0f / 255.0f);
    const float w3 = base.color2[3] * (1.0f / 255.0f);

    idJointMat accum;
    idJointMat::Mul(accum, j0, w0);
    idJointMat::Mad(accum, j1, w1);
    idJointMat::Mad(accum, j2, w2);
    idJointMat::Mad(accum, j3, w3);

    idVec3 normal = accum * base.GetNormal();
    normal.Normalize();
    return normal;
}

bool IsZeroAreaSmokeTriangle(const idVec3& p0, const idVec3& p1, const idVec3& p2)
{
    const idVec3 edge1 = p1 - p0;
    const idVec3 edge2 = p2 - p0;
    const float areaSqr = edge1.Cross(edge2).LengthSqr();
    return areaSqr <= 1.0e-8f;
}

bool IsSmokeGuiDrawSurface(const drawSurf_t* drawSurf)
{
    if (!drawSurf)
    {
        return false;
    }

    if (drawSurf->space && drawSurf->space->isGuiSurface)
    {
        return true;
    }

    const idMaterial* material = drawSurf->material;
    return material && material->HasGui();
}

bool ValidateSmokeDrawSurface(const viewDef_t* viewDef, const drawSurf_t* drawSurf, const srfTriangles_t*& tri, RtSmokeSurfaceSkipStats* skipStats)
{
    tri = nullptr;
    if (!drawSurf)
    {
        if (skipStats)
        {
            ++skipStats->nullSurface;
        }
        return false;
    }

    if (!drawSurf->frontEndGeo)
    {
        if (skipStats)
        {
            ++skipStats->missingGeometry;
        }
        return false;
    }

    if (!drawSurf->material)
    {
        if (skipStats)
        {
            ++skipStats->nullMaterial;
        }
        return false;
    }

    const bool guiDrawSurface = IsSmokeGuiDrawSurface(drawSurf);
    if (guiDrawSurface && r_pathTracingAllowGuiSurfaces.GetInteger() == 0)
    {
        if (skipStats)
        {
            ++skipStats->guiSurface;
        }
        return false;
    }

    if (!drawSurf->space)
    {
        if (skipStats)
        {
            ++skipStats->nullSpace;
        }
        return false;
    }

    const viewEntity_t* space = drawSurf->space;
    const idRenderEntityLocal* entityDef = space->entityDef;
    const renderEntity_t* renderEntity = entityDef ? &entityDef->parms : nullptr;
    if (!guiDrawSurface && viewDef && space != &viewDef->worldSpace && (!renderEntity || !renderEntity->hModel))
    {
        if (skipStats)
        {
            ++skipStats->nullModel;
        }
        return false;
    }

    if (!guiDrawSurface && renderEntity && renderEntity->callback && r_pathTracingSkipCallbackEntities.GetInteger() != 0)
    {
        if (skipStats)
        {
            ++skipStats->callbackEntity;
        }
        return false;
    }

    tri = drawSurf->frontEndGeo;
    if (!tri->verts || !tri->indexes || tri->numVerts < 3 || tri->numIndexes < 3)
    {
        if (skipStats)
        {
            ++skipStats->missingGeometry;
        }
        return false;
    }

    if ((tri->numIndexes % 3) != 0 || drawSurf->numIndexes < 3)
    {
        if (skipStats)
        {
            ++skipStats->invalidIndexCount;
        }
        return false;
    }

    const bool hasAmbientCache = tri->ambientCache != 0;
    const bool hasIndexCache = tri->indexCache != 0;
    if ((hasAmbientCache && !vertexCache.CacheIsCurrent(tri->ambientCache)) ||
        (hasIndexCache && !vertexCache.CacheIsCurrent(tri->indexCache)))
    {
        if (skipStats)
        {
            ++skipStats->nonCurrentCache;
        }
        return false;
    }

    return true;
}

RtSmokeSurfaceClass ClassifySmokeSurface(const viewDef_t* viewDef, const drawSurf_t* drawSurf, const srfTriangles_t* tri)
{
    const viewEntity_t* space = drawSurf ? drawSurf->space : nullptr;
    const idRenderEntityLocal* entityDef = space ? space->entityDef : nullptr;
    const renderEntity_t* renderEntity = entityDef ? &entityDef->parms : nullptr;
    const idMaterial* material = drawSurf ? drawSurf->material : nullptr;

    if ((drawSurf && drawSurf->jointCache != 0) ||
        (tri && tri->staticModelWithJoints != nullptr) ||
        (renderEntity && renderEntity->joints != nullptr && renderEntity->numJoints > 0))
    {
        return RtSmokeSurfaceClass::SkinnedDeformed;
    }

    if (material)
    {
        const deform_t deform = material->Deform();
        if (IsSmokeGuiDrawSurface(drawSurf) ||
            material->Coverage() == MC_TRANSLUCENT ||
            deform == DFRM_SPRITE ||
            deform == DFRM_TUBE ||
            deform == DFRM_FLARE ||
            deform == DFRM_PARTICLE ||
            deform == DFRM_PARTICLE2 ||
            material->GetSort() >= SS_MEDIUM ||
            (space && space->modelDepthHack != 0.0f))
        {
            return RtSmokeSurfaceClass::ParticleAlpha;
        }
    }

    const idRenderModel* model = renderEntity ? renderEntity->hModel : nullptr;
    if ((viewDef && space == &viewDef->worldSpace) ||
        !entityDef ||
        (model && model->IsStaticWorldModel()) ||
        (drawSurf && idVertexCache::CacheIsStatic(drawSurf->ambientCache) && idVertexCache::CacheIsStatic(drawSurf->indexCache) && !entityDef))
    {
        return RtSmokeSurfaceClass::StaticWorld;
    }

    if (entityDef)
    {
        return RtSmokeSurfaceClass::RigidEntity;
    }

    return RtSmokeSurfaceClass::Unknown;
}

RtSmokeTranslucentSubtype ClassifySmokeTranslucentSubtype(const drawSurf_t* drawSurf)
{
    const idMaterial* material = drawSurf ? drawSurf->material : nullptr;
    if (!material)
    {
        return RtSmokeTranslucentSubtype::Unknown;
    }

    const deform_t deform = material->Deform();
    const float sort = material->GetSort();
    const RtSmokeTranslucentClassifierInfo info = BuildSmokeTranslucentClassifierInfo(material);

    if (IsSmokeGuiDrawSurface(drawSurf) || info.hasScreenTexgen || info.nameLooksGui)
    {
        return RtSmokeTranslucentSubtype::GuiScreen;
    }

    if (info.sortIsPostProcess || info.sortIsGuiOrSubview)
    {
        return RtSmokeTranslucentSubtype::PortalWindow;
    }

    if (deform == DFRM_PARTICLE ||
        deform == DFRM_PARTICLE2 ||
        deform == DFRM_SPRITE ||
        deform == DFRM_TUBE ||
        deform == DFRM_FLARE ||
        sort >= SS_ALMOST_NEAREST ||
        info.nameLooksParticle)
    {
        return RtSmokeTranslucentSubtype::SmokeParticle;
    }

    if (info.nameLooksGlass)
    {
        return RtSmokeTranslucentSubtype::ObjectGlass;
    }

    if (info.hasAdditiveBlend ||
        (info.hasAmbientStage && !info.hasDiffuseStage && info.nameLooksGlow) ||
        (info.hasAmbientBlendStage && info.nameLooksGlow) ||
        (info.nameLooksGlow && !info.nameLooksDecal) ||
        info.nameLooksSignage)
    {
        return RtSmokeTranslucentSubtype::SignageGlow;
    }

    if (info.sortIsDecal || info.polygonOffsetDecal || info.nameLooksDecal)
    {
        return RtSmokeTranslucentSubtype::DecalGrime;
    }

    return RtSmokeTranslucentSubtype::Unknown;
}

uint32_t SmokeSurfaceClassAndSubtypeId(RtSmokeSurfaceClass surfaceClass, RtSmokeTranslucentSubtype subtype)
{
    uint32_t id = SmokeSurfaceClassId(surfaceClass);
    if (surfaceClass == RtSmokeSurfaceClass::ParticleAlpha)
    {
        id |= (SmokeTranslucentSubtypeId(subtype) << RT_SMOKE_TRANSLUCENT_SUBTYPE_SHIFT) & RT_SMOKE_TRANSLUCENT_SUBTYPE_MASK;
    }
    return id;
}

bool SmokeSkinnedSurfaceLikelyBasePose(const drawSurf_t* drawSurf, const srfTriangles_t* tri)
{
    const viewEntity_t* space = drawSurf ? drawSurf->space : nullptr;
    const idRenderEntityLocal* entityDef = space ? space->entityDef : nullptr;
    const renderEntity_t* renderEntity = entityDef ? &entityDef->parms : nullptr;
    return (drawSurf && drawSurf->jointCache != 0) ||
        (tri && tri->staticModelWithJoints != nullptr) ||
        (renderEntity && renderEntity->joints != nullptr && renderEntity->numJoints > 0);
}

RtSmokeSurfaceClassReason BuildSmokeSurfaceClassReason(const viewDef_t* viewDef, const drawSurf_t* drawSurf, const srfTriangles_t* tri, int surfaceIndex, RtSmokeSurfaceClass surfaceClass)
{
    RtSmokeSurfaceClassReason reason;
    reason.valid = true;
    reason.finalClass = surfaceClass;
    reason.surfaceIndex = surfaceIndex;
    reason.verts = tri ? tri->numVerts : 0;
    reason.indexes = tri ? tri->numIndexes : 0;

    const viewEntity_t* space = drawSurf ? drawSurf->space : nullptr;
    const idRenderEntityLocal* entityDef = space ? space->entityDef : nullptr;
    const renderEntity_t* renderEntity = entityDef ? &entityDef->parms : nullptr;
    const idMaterial* material = drawSurf ? drawSurf->material : nullptr;
    const idRenderModel* model = renderEntity ? renderEntity->hModel : nullptr;

    reason.hasEntityDef = entityDef != nullptr;
    reason.isWorldSpace = viewDef && space == &viewDef->worldSpace;
    reason.materialName = material ? material->GetName() : "<none>";
    reason.coverage = material ? material->Coverage() : MC_BAD;
    reason.sort = material ? material->GetSort() : SS_BAD;
    reason.deform = material ? material->Deform() : DFRM_NONE;
    reason.entityNum = renderEntity ? renderEntity->entityNum : -1;
    reason.modelName = model ? model->Name() : "<none>";
    reason.dynamicModel = model ? model->IsDynamicModel() : DM_STATIC;
    reason.hasJointCache = drawSurf && drawSurf->jointCache != 0;
    reason.hasStaticModelWithJoints = tri && tri->staticModelWithJoints != nullptr;
    reason.hasRenderEntityJoints = renderEntity && renderEntity->joints != nullptr && renderEntity->numJoints > 0;
    reason.ambientCacheStatic = drawSurf && idVertexCache::CacheIsStatic(drawSurf->ambientCache);
    reason.indexCacheStatic = drawSurf && idVertexCache::CacheIsStatic(drawSurf->indexCache);
    reason.isStaticWorldModel = model && model->IsStaticWorldModel();
    reason.hasDynamicModel = entityDef && entityDef->dynamicModel != nullptr;
    reason.hasCachedDynamicModel = entityDef && entityDef->cachedDynamicModel != nullptr;
    reason.hasCallback = renderEntity && renderEntity->callback != nullptr;
    reason.forceUpdate = renderEntity && renderEntity->forceUpdate != 0;
    reason.weaponDepthHack = renderEntity && renderEntity->weaponDepthHack;
    reason.modelDepthHack = renderEntity ? renderEntity->modelDepthHack : (space ? space->modelDepthHack : 0.0f);
    reason.cpuVertsAvailable = tri && tri->verts != nullptr;
    reason.cpuVertexCacheCurrent = tri && tri->ambientCache != 0 && vertexCache.CacheIsCurrent(tri->ambientCache);
    reason.cpuIndexCacheCurrent = tri && tri->indexCache != 0 && vertexCache.CacheIsCurrent(tri->indexCache);
    reason.skinnedLikelyBasePose = surfaceClass == RtSmokeSurfaceClass::SkinnedDeformed && SmokeSkinnedSurfaceLikelyBasePose(drawSurf, tri);
    reason.rtCpuSkinned = surfaceClass == RtSmokeSurfaceClass::SkinnedDeformed && GetSmokeRtCpuSkinningJoints(tri) != nullptr;
    if (renderEntity)
    {
        reason.entityOrigin = renderEntity->origin;
        reason.entityAxis = renderEntity->axis;
        reason.entityBounds = renderEntity->bounds;
        reason.hasEntityBounds = true;
    }
    if (tri)
    {
        reason.surfaceBounds = tri->bounds;
        reason.hasSurfaceBounds = true;
    }
    if (entityDef)
    {
        reason.localReferenceBounds = entityDef->localReferenceBounds;
        reason.globalReferenceBounds = entityDef->globalReferenceBounds;
        reason.hasReferenceBounds = true;
    }

    return reason;
}

void AddSmokeDynamicGeometryStats(RtSmokeDynamicGeometryStats& stats, RtSmokeSurfaceClass surfaceClass, const drawSurf_t* drawSurf, const srfTriangles_t* tri, int indexes)
{
    switch (surfaceClass)
    {
        case RtSmokeSurfaceClass::RigidEntity:
            ++stats.rigidSurfaces;
            stats.rigidIndexes += indexes;
            break;
        case RtSmokeSurfaceClass::SkinnedDeformed:
            if (GetSmokeRtCpuSkinningJoints(tri) != nullptr)
            {
                ++stats.skinnedRtCpuSkinnedSurfaces;
                stats.skinnedRtCpuSkinnedIndexes += indexes;
            }
            else if (SmokeSkinnedSurfaceLikelyBasePose(drawSurf, tri))
            {
                ++stats.skinnedLikelyBasePoseSurfaces;
                stats.skinnedLikelyBasePoseIndexes += indexes;
            }
            else
            {
                ++stats.skinnedCpuCurrentSurfaces;
                stats.skinnedCpuCurrentIndexes += indexes;
            }
            break;
        case RtSmokeSurfaceClass::ParticleAlpha:
            ++stats.particleAlphaSurfaces;
            stats.particleAlphaIndexes += indexes;
            break;
        case RtSmokeSurfaceClass::Unknown:
            ++stats.unknownSurfaces;
            stats.unknownIndexes += indexes;
            break;
        default:
            break;
    }
}

void AddSmokeSurfaceClassStats(RtSmokeSurfaceClassStats& stats, RtSmokeSurfaceClass surfaceClass, int verts, int indexes)
{
    const int triangles = indexes / 3;
    switch (surfaceClass)
    {
        case RtSmokeSurfaceClass::StaticWorld:
            ++stats.staticWorldSurfaces;
            stats.staticWorldVerts += verts;
            stats.staticWorldIndexes += indexes;
            stats.staticWorldTriangles += triangles;
            break;
        case RtSmokeSurfaceClass::RigidEntity:
            ++stats.rigidEntitySurfaces;
            stats.rigidEntityVerts += verts;
            stats.rigidEntityIndexes += indexes;
            stats.rigidEntityTriangles += triangles;
            break;
        case RtSmokeSurfaceClass::SkinnedDeformed:
            ++stats.skinnedDeformedSurfaces;
            stats.skinnedDeformedVerts += verts;
            stats.skinnedDeformedIndexes += indexes;
            stats.skinnedDeformedTriangles += triangles;
            break;
        case RtSmokeSurfaceClass::ParticleAlpha:
            ++stats.particleAlphaSurfaces;
            stats.particleAlphaVerts += verts;
            stats.particleAlphaIndexes += indexes;
            stats.particleAlphaTriangles += triangles;
            break;
        default:
            ++stats.unknownSurfaces;
            stats.unknownVerts += verts;
            stats.unknownIndexes += indexes;
            stats.unknownTriangles += triangles;
            break;
    }
}

void AddSmokeSurfaceClassReasonSample(RtSmokeSurfaceClassReasonSamples& samples, const RtSmokeSurfaceClassReason& reason)
{
    if (reason.finalClass == RtSmokeSurfaceClass::SkinnedDeformed && samples.skinnedCount < RT_SMOKE_CLASS_REASON_SAMPLES)
    {
        samples.skinnedSamples[samples.skinnedCount] = reason;
        ++samples.skinnedCount;
    }

    const int classIndex = static_cast<int>(SmokeSurfaceClassId(reason.finalClass));
    if (classIndex < 0 || classIndex >= RT_SMOKE_CLASS_COUNT)
    {
        return;
    }

    if (samples.counts[classIndex] >= RT_SMOKE_CLASS_REASON_SAMPLES)
    {
        return;
    }

    samples.samples[classIndex][samples.counts[classIndex]] = reason;
    ++samples.counts[classIndex];
}

void LogSmokeSurfaceClassReasonSamples(const RtSmokeSurfaceClassReasonSamples& samples)
{
    for (int classIndex = 0; classIndex < RT_SMOKE_CLASS_COUNT; ++classIndex)
    {
        for (int sampleIndex = 0; sampleIndex < samples.counts[classIndex]; ++sampleIndex)
        {
            const RtSmokeSurfaceClassReason& reason = samples.samples[classIndex][sampleIndex];
            if (!reason.valid)
            {
                continue;
            }

            common->Printf("PathTracePrimaryPass: RT smoke class sample %s surf=%d material='%s' coverage=%s sort=%.1f deform=%s entity=%d model='%s' modelDynamic=%s joints(cache=%d staticModel=%d entity=%d) cache(staticV=%d staticI=%d currentV=%d currentI=%d) cpuVerts=%d rtCpuSkin=%d skinnedBasePose=%d worldSpace=%d staticWorldModel=%d entityDef=%d dynModel=%d cachedDyn=%d callback=%d forceUpdate=%d weaponDepthHack=%d modelDepthHack=%.3f verts=%d indexes=%d\n",
                SmokeSurfaceClassName(reason.finalClass),
                reason.surfaceIndex,
                reason.materialName.c_str(),
                SmokeCoverageName(reason.coverage),
                reason.sort,
                SmokeDeformName(reason.deform),
                reason.entityNum,
                reason.modelName.c_str(),
                SmokeDynamicModelName(reason.dynamicModel),
                reason.hasJointCache ? 1 : 0,
                reason.hasStaticModelWithJoints ? 1 : 0,
                reason.hasRenderEntityJoints ? 1 : 0,
                reason.ambientCacheStatic ? 1 : 0,
                reason.indexCacheStatic ? 1 : 0,
                reason.cpuVertexCacheCurrent ? 1 : 0,
                reason.cpuIndexCacheCurrent ? 1 : 0,
                reason.cpuVertsAvailable ? 1 : 0,
                reason.rtCpuSkinned ? 1 : 0,
                reason.skinnedLikelyBasePose ? 1 : 0,
                reason.isWorldSpace ? 1 : 0,
                reason.isStaticWorldModel ? 1 : 0,
                reason.hasEntityDef ? 1 : 0,
                reason.hasDynamicModel ? 1 : 0,
                reason.hasCachedDynamicModel ? 1 : 0,
                reason.hasCallback ? 1 : 0,
                reason.forceUpdate ? 1 : 0,
                reason.weaponDepthHack ? 1 : 0,
                reason.modelDepthHack,
                reason.verts,
                reason.indexes);

            if (reason.finalClass == RtSmokeSurfaceClass::SkinnedDeformed)
            {
                common->Printf("PathTracePrimaryPass: RT smoke skinned detail entity=%d model='%s' origin=(%.2f %.2f %.2f) axis0=(%.3f %.3f %.3f) axis1=(%.3f %.3f %.3f) axis2=(%.3f %.3f %.3f) joints(cache=%d staticModel=%d entityCount=%d) cpuVerts=%d cacheCurrent=(v%d i%d) rtCpuSkin=%d likelyBasePose=%d\n",
                    reason.entityNum,
                    reason.modelName.c_str(),
                    reason.entityOrigin.x, reason.entityOrigin.y, reason.entityOrigin.z,
                    reason.entityAxis[0].x, reason.entityAxis[0].y, reason.entityAxis[0].z,
                    reason.entityAxis[1].x, reason.entityAxis[1].y, reason.entityAxis[1].z,
                    reason.entityAxis[2].x, reason.entityAxis[2].y, reason.entityAxis[2].z,
                    reason.hasJointCache ? 1 : 0,
                    reason.hasStaticModelWithJoints ? 1 : 0,
                    reason.hasRenderEntityJoints ? 1 : 0,
                    reason.cpuVertsAvailable ? 1 : 0,
                    reason.cpuVertexCacheCurrent ? 1 : 0,
                    reason.cpuIndexCacheCurrent ? 1 : 0,
                    reason.rtCpuSkinned ? 1 : 0,
                    reason.skinnedLikelyBasePose ? 1 : 0);
                if (reason.hasEntityBounds)
                {
                    common->Printf("PathTracePrimaryPass: RT smoke skinned bounds entityLocal=[(%.2f %.2f %.2f)..(%.2f %.2f %.2f)]\n",
                        reason.entityBounds[0].x, reason.entityBounds[0].y, reason.entityBounds[0].z,
                        reason.entityBounds[1].x, reason.entityBounds[1].y, reason.entityBounds[1].z);
                }
                if (reason.hasSurfaceBounds)
                {
                    common->Printf("PathTracePrimaryPass: RT smoke skinned bounds surfaceLocal=[(%.2f %.2f %.2f)..(%.2f %.2f %.2f)]\n",
                        reason.surfaceBounds[0].x, reason.surfaceBounds[0].y, reason.surfaceBounds[0].z,
                        reason.surfaceBounds[1].x, reason.surfaceBounds[1].y, reason.surfaceBounds[1].z);
                }
                if (reason.hasReferenceBounds)
                {
                    common->Printf("PathTracePrimaryPass: RT smoke skinned bounds refLocal=[(%.2f %.2f %.2f)..(%.2f %.2f %.2f)] refGlobal=[(%.2f %.2f %.2f)..(%.2f %.2f %.2f)]\n",
                        reason.localReferenceBounds[0].x, reason.localReferenceBounds[0].y, reason.localReferenceBounds[0].z,
                        reason.localReferenceBounds[1].x, reason.localReferenceBounds[1].y, reason.localReferenceBounds[1].z,
                        reason.globalReferenceBounds[0].x, reason.globalReferenceBounds[0].y, reason.globalReferenceBounds[0].z,
                        reason.globalReferenceBounds[1].x, reason.globalReferenceBounds[1].y, reason.globalReferenceBounds[1].z);
                }
            }
        }
    }

    common->Printf("PathTracePrimaryPass: RT smoke focused skinned sample dump count=%d\n", samples.skinnedCount);
    for (int sampleIndex = 0; sampleIndex < samples.skinnedCount; ++sampleIndex)
    {
        const RtSmokeSurfaceClassReason& reason = samples.skinnedSamples[sampleIndex];
        if (!reason.valid)
        {
            continue;
        }

        common->Printf("PathTracePrimaryPass: RT smoke skinned focused surf=%d entity=%d model='%s' material='%s' origin=(%.2f %.2f %.2f) axis0=(%.3f %.3f %.3f) axis1=(%.3f %.3f %.3f) axis2=(%.3f %.3f %.3f) joints(cache=%d staticModel=%d entityCount=%d) cpuVerts=%d cacheCurrent=(v%d i%d) rtCpuSkin=%d likelyBasePose=%d verts=%d indexes=%d\n",
            reason.surfaceIndex,
            reason.entityNum,
            reason.modelName.c_str(),
            reason.materialName.c_str(),
            reason.entityOrigin.x, reason.entityOrigin.y, reason.entityOrigin.z,
            reason.entityAxis[0].x, reason.entityAxis[0].y, reason.entityAxis[0].z,
            reason.entityAxis[1].x, reason.entityAxis[1].y, reason.entityAxis[1].z,
            reason.entityAxis[2].x, reason.entityAxis[2].y, reason.entityAxis[2].z,
            reason.hasJointCache ? 1 : 0,
            reason.hasStaticModelWithJoints ? 1 : 0,
            reason.hasRenderEntityJoints ? 1 : 0,
            reason.cpuVertsAvailable ? 1 : 0,
            reason.cpuVertexCacheCurrent ? 1 : 0,
            reason.cpuIndexCacheCurrent ? 1 : 0,
            reason.rtCpuSkinned ? 1 : 0,
            reason.skinnedLikelyBasePose ? 1 : 0,
            reason.verts,
            reason.indexes);
        if (reason.hasReferenceBounds)
        {
            common->Printf("PathTracePrimaryPass: RT smoke skinned focused bounds refLocal=[(%.2f %.2f %.2f)..(%.2f %.2f %.2f)] refGlobal=[(%.2f %.2f %.2f)..(%.2f %.2f %.2f)]\n",
                reason.localReferenceBounds[0].x, reason.localReferenceBounds[0].y, reason.localReferenceBounds[0].z,
                reason.localReferenceBounds[1].x, reason.localReferenceBounds[1].y, reason.localReferenceBounds[1].z,
                reason.globalReferenceBounds[0].x, reason.globalReferenceBounds[0].y, reason.globalReferenceBounds[0].z,
                reason.globalReferenceBounds[1].x, reason.globalReferenceBounds[1].y, reason.globalReferenceBounds[1].z);
        }
    }
}

void LogSmokeBucketRanges(const RtSmokeBucketRanges& ranges)
{
    common->Printf("PathTracePrimaryPass: RT smoke bucket ranges static-world v[%d+%d] i[%d+%d] t[%d+%d]; rigid-entity v[%d+%d] i[%d+%d] t[%d+%d]; skinned v[%d+%d] i[%d+%d] t[%d+%d]; particle/alpha v[%d+%d] i[%d+%d] t[%d+%d]; unknown v[%d+%d] i[%d+%d] t[%d+%d]\n",
        ranges.buckets[0].vertexOffset, ranges.buckets[0].vertexCount,
        ranges.buckets[0].indexOffset, ranges.buckets[0].indexCount,
        ranges.buckets[0].triangleOffset, ranges.buckets[0].triangleCount,
        ranges.buckets[1].vertexOffset, ranges.buckets[1].vertexCount,
        ranges.buckets[1].indexOffset, ranges.buckets[1].indexCount,
        ranges.buckets[1].triangleOffset, ranges.buckets[1].triangleCount,
        ranges.buckets[2].vertexOffset, ranges.buckets[2].vertexCount,
        ranges.buckets[2].indexOffset, ranges.buckets[2].indexCount,
        ranges.buckets[2].triangleOffset, ranges.buckets[2].triangleCount,
        ranges.buckets[3].vertexOffset, ranges.buckets[3].vertexCount,
        ranges.buckets[3].indexOffset, ranges.buckets[3].indexCount,
        ranges.buckets[3].triangleOffset, ranges.buckets[3].triangleCount,
        ranges.buckets[4].vertexOffset, ranges.buckets[4].vertexCount,
        ranges.buckets[4].indexOffset, ranges.buckets[4].indexCount,
        ranges.buckets[4].triangleOffset, ranges.buckets[4].triangleCount);
}

void LogSmokeAttributeStats(const RtSmokeAttributeStats& stats)
{
    common->Printf("PathTracePrimaryPass: RT smoke attribute validation invalidNormals static=%dv/%dt rigid=%dv/%dt skinned=%dv/%dt particle/alpha=%dv/%dt unknown=%dv/%dt; invalidUVs static=%dv/%dt rigid=%dv/%dt skinned=%dv/%dt particle/alpha=%dv/%dt unknown=%dv/%dt; geomNormalFallback static=%dt rigid=%dt skinned=%dt particle/alpha=%dt unknown=%dt\n",
        stats.classes[0].invalidNormalVerts, stats.classes[0].invalidNormalTriangles,
        stats.classes[1].invalidNormalVerts, stats.classes[1].invalidNormalTriangles,
        stats.classes[2].invalidNormalVerts, stats.classes[2].invalidNormalTriangles,
        stats.classes[3].invalidNormalVerts, stats.classes[3].invalidNormalTriangles,
        stats.classes[4].invalidNormalVerts, stats.classes[4].invalidNormalTriangles,
        stats.classes[0].invalidUvVerts, stats.classes[0].invalidUvTriangles,
        stats.classes[1].invalidUvVerts, stats.classes[1].invalidUvTriangles,
        stats.classes[2].invalidUvVerts, stats.classes[2].invalidUvTriangles,
        stats.classes[3].invalidUvVerts, stats.classes[3].invalidUvTriangles,
        stats.classes[4].invalidUvVerts, stats.classes[4].invalidUvTriangles,
        stats.classes[0].forcedGeometricNormalTriangles,
        stats.classes[1].forcedGeometricNormalTriangles,
        stats.classes[2].forcedGeometricNormalTriangles,
        stats.classes[3].forcedGeometricNormalTriangles,
        stats.classes[4].forcedGeometricNormalTriangles);
}

void LogSmokeMaterialStats(const RtSmokeMaterialStats& stats)
{
    common->Printf("PathTracePrimaryPass: RT smoke materials unique=%d surfaces=%d triangles=%d translucentUnique=%d translucentSurfaces=%d translucentTriangles=%d samples=",
        stats.uniqueMaterials,
        stats.totalSurfaces,
        stats.totalTriangles,
        stats.translucentUniqueMaterials,
        stats.translucentSurfaces,
        stats.translucentTriangles);

    for (int sampleIndex = 0; sampleIndex < stats.sampleCount; ++sampleIndex)
    {
        const RtSmokeMaterialSample& sample = stats.samples[sampleIndex];
        common->Printf("%s%s(id=%u surfaces=%d triangles=%d)",
            sampleIndex == 0 ? "" : ", ",
            sample.name.c_str(),
            sample.id,
            sample.surfaces,
            sample.triangles);
    }

    common->Printf("\n");

    if (stats.translucentSampleCount <= 0)
    {
        return;
    }

    common->Printf("PathTracePrimaryPass: RT smoke translucent material samples=");
    for (int sampleIndex = 0; sampleIndex < stats.translucentSampleCount; ++sampleIndex)
    {
        const RtSmokeMaterialSample& sample = stats.translucentSamples[sampleIndex];
        common->Printf("%s%s(id=%u surfaces=%d triangles=%d)",
            sampleIndex == 0 ? "" : ", ",
            sample.name.c_str(),
            sample.id,
            sample.surfaces,
            sample.triangles);
    }
    common->Printf("\n");

    common->Printf("PathTracePrimaryPass: RT smoke translucent subtype counts");
    for (int subtypeIndex = 0; subtypeIndex < RT_SMOKE_TRANSLUCENT_SUBTYPE_COUNT; ++subtypeIndex)
    {
        common->Printf(" %s=%d(%dt)",
            SmokeTranslucentSubtypeNameByIndex(subtypeIndex),
            stats.translucentSubtypeSurfaces[subtypeIndex],
            stats.translucentSubtypeTriangles[subtypeIndex]);
    }
    common->Printf("\n");

    for (int subtypeIndex = 0; subtypeIndex < RT_SMOKE_TRANSLUCENT_SUBTYPE_COUNT; ++subtypeIndex)
    {
        const int sampleCount = stats.translucentSubtypeSampleCounts[subtypeIndex];
        if (sampleCount <= 0)
        {
            continue;
        }

        common->Printf("PathTracePrimaryPass: RT smoke translucent subtype %s samples=",
            SmokeTranslucentSubtypeNameByIndex(subtypeIndex));
        for (int sampleIndex = 0; sampleIndex < sampleCount; ++sampleIndex)
        {
            const RtSmokeMaterialSample& sample = stats.translucentSubtypeSamples[subtypeIndex][sampleIndex];
            common->Printf("%s%s(id=%u surfaces=%d triangles=%d)",
                sampleIndex == 0 ? "" : ", ",
                sample.name.c_str(),
                sample.id,
                sample.surfaces,
                sample.triangles);
        }
        common->Printf("\n");
    }
}

void LogSmokeTranslucentSubtypeDump(const RtSmokeMaterialStats& stats)
{
    common->Printf("PathTracePrimaryPass: RT smoke translucent subtype dump samples=%d\n",
        stats.translucentDebugSampleCount);

    for (int sampleIndex = 0; sampleIndex < stats.translucentDebugSampleCount; ++sampleIndex)
    {
        const RtSmokeTranslucentSubtypeDebugSample& sample = stats.translucentDebugSamples[sampleIndex];
        if (!sample.valid)
        {
            continue;
        }

        const RtSmokeTranslucentClassifierInfo& info = sample.info;
        common->Printf("PathTracePrimaryPass: RT smoke translucent sample surf=%d subtype=%s material='%s' coverage=%s sort=%.2f deform=%s verts=%d indexes=%d flags guiSort=%d decalSort=%d postSort=%d polyOffset=%d screenTex=%d addBlend=%d ambient=%d ambientBlend=%d diffuse=%d nameGui=%d nameParticle=%d nameDecal=%d nameGlass=%d nameGlow=%d nameSignage=%d\n",
            sample.surfaceIndex,
            SmokeTranslucentSubtypeName(sample.subtype),
            sample.materialName.c_str(),
            SmokeCoverageName(sample.coverage),
            sample.sort,
            SmokeDeformName(sample.deform),
            sample.verts,
            sample.indexes,
            info.sortIsGuiOrSubview ? 1 : 0,
            info.sortIsDecal ? 1 : 0,
            info.sortIsPostProcess ? 1 : 0,
            info.polygonOffsetDecal ? 1 : 0,
            info.hasScreenTexgen ? 1 : 0,
            info.hasAdditiveBlend ? 1 : 0,
            info.hasAmbientStage ? 1 : 0,
            info.hasAmbientBlendStage ? 1 : 0,
            info.hasDiffuseStage ? 1 : 0,
            info.nameLooksGui ? 1 : 0,
            info.nameLooksParticle ? 1 : 0,
            info.nameLooksDecal ? 1 : 0,
            info.nameLooksGlass ? 1 : 0,
            info.nameLooksGlow ? 1 : 0,
            info.nameLooksSignage ? 1 : 0);
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

void AccumulateSmokeTextureCoverageTriangles(
    const RtSmokeMaterialTableBuild& table,
    const std::vector<uint32_t>& triangleClassData,
    const std::vector<uint32_t>& triangleMaterialIndexes,
    RtSmokeTextureCoverageStats& stats)
{
    const int triangleCount = Min(static_cast<int>(triangleClassData.size()), static_cast<int>(triangleMaterialIndexes.size()));
    for (int triangleIndex = 0; triangleIndex < triangleCount; ++triangleIndex)
    {
        const int classIndex = idMath::ClampInt(0, RT_SMOKE_CLASS_COUNT - 1, static_cast<int>(triangleClassData[triangleIndex] & RT_SMOKE_TRIANGLE_CLASS_MASK));
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
    for (int classIndex = 0; classIndex < RT_SMOKE_CLASS_COUNT; ++classIndex)
    {
        const RtSmokeTextureCoverageClassStats& classStats = stats.classes[classIndex];
        common->Printf("; %s tris=%d bound=%d fallback=%d invalidMat=%d",
            SmokeSurfaceClassNameByIndex(classIndex),
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

void InitSmokeTriangleGeometry(nvrhi::rt::GeometryTriangles& triangleGeometry, nvrhi::IBuffer* vertexBuffer, nvrhi::IBuffer* indexBuffer, int totalVertexCount, int indexOffset, int indexCount)
{
    triangleGeometry.indexBuffer = indexBuffer;
    triangleGeometry.vertexBuffer = vertexBuffer;
    triangleGeometry.indexFormat = nvrhi::Format::R32_UINT;
    triangleGeometry.vertexFormat = nvrhi::Format::RGB32_FLOAT;
    triangleGeometry.indexOffset = static_cast<uint64_t>(indexOffset) * sizeof(uint32_t);
    triangleGeometry.vertexOffset = 0;
    triangleGeometry.indexCount = static_cast<uint32_t>(indexCount);
    triangleGeometry.vertexCount = static_cast<uint32_t>(totalVertexCount);
    triangleGeometry.vertexStride = RT_SMOKE_VERTEX_STRIDE;
}

nvrhi::BufferHandle CreateSmokeGeometryBuffer(nvrhi::IDevice* device, const char* debugName, size_t byteSize, uint32_t structStride, bool vertexBuffer, bool indexBuffer, bool accelStructInput)
{
    nvrhi::BufferDesc desc;
    desc.byteSize = byteSize > structStride ? byteSize : structStride;
    desc.debugName = debugName;
    desc.structStride = structStride;
    desc.isVertexBuffer = vertexBuffer;
    desc.isIndexBuffer = indexBuffer;
    desc.isAccelStructBuildInput = accelStructInput;
    desc.initialState = nvrhi::ResourceStates::Common;
    desc.keepInitialState = true;
    return device->createBuffer(desc);
}

uint64 HashSmokeBytes(uint64 hash, const void* data, size_t size)
{
    const byte* bytes = static_cast<const byte*>(data);
    for (size_t index = 0; index < size; ++index)
    {
        hash ^= static_cast<uint64>(bytes[index]);
        hash *= 1099511628211ull;
    }
    return hash;
}

RtSmokeStaticBlasSignature ComputeSmokeStaticBlasSignature(const std::vector<PathTraceSmokeVertex>& vertexData, const std::vector<uint32_t>& indexData, const std::vector<uint32_t>& triangleClassData, const std::vector<uint32_t>& triangleMaterialData, const RtSmokeBucketRange& staticRange, const idVec3& sceneOrigin)
{
    RtSmokeStaticBlasSignature signature;
    signature.vertexCount = staticRange.vertexCount;
    signature.indexCount = staticRange.indexCount;
    signature.triangleCount = staticRange.triangleCount;

    uint64 hash = 14695981039346656037ull;
    hash = HashSmokeBytes(hash, &sceneOrigin.x, sizeof(sceneOrigin.x));
    hash = HashSmokeBytes(hash, &sceneOrigin.y, sizeof(sceneOrigin.y));
    hash = HashSmokeBytes(hash, &sceneOrigin.z, sizeof(sceneOrigin.z));
    hash = HashSmokeBytes(hash, &signature.vertexCount, sizeof(signature.vertexCount));
    hash = HashSmokeBytes(hash, &signature.indexCount, sizeof(signature.indexCount));
    hash = HashSmokeBytes(hash, &signature.triangleCount, sizeof(signature.triangleCount));

    if (staticRange.vertexCount > 0)
    {
        hash = HashSmokeBytes(hash, vertexData.data() + staticRange.vertexOffset, static_cast<size_t>(staticRange.vertexCount) * sizeof(vertexData[0]));
    }

    if (staticRange.indexCount > 0)
    {
        hash = HashSmokeBytes(hash, indexData.data() + staticRange.indexOffset, static_cast<size_t>(staticRange.indexCount) * sizeof(indexData[0]));
    }

    if (staticRange.triangleCount > 0)
    {
        hash = HashSmokeBytes(hash, triangleClassData.data() + staticRange.triangleOffset, static_cast<size_t>(staticRange.triangleCount) * sizeof(triangleClassData[0]));
        hash = HashSmokeBytes(hash, triangleMaterialData.data() + staticRange.triangleOffset, static_cast<size_t>(staticRange.triangleCount) * sizeof(triangleMaterialData[0]));
    }

    signature.hash = hash;
    return signature;
}

uint64 BuildSmokeStaticSurfaceKey(const drawSurf_t* drawSurf, const srfTriangles_t* tri)
{
    uint64 hash = 14695981039346656037ull;
    const uintptr_t triPtr = reinterpret_cast<uintptr_t>(tri);
    const uintptr_t materialPtr = reinterpret_cast<uintptr_t>(drawSurf ? drawSurf->material : nullptr);
    hash = HashSmokeBytes(hash, &triPtr, sizeof(triPtr));
    hash = HashSmokeBytes(hash, &materialPtr, sizeof(materialPtr));
    if (tri)
    {
        hash = HashSmokeBytes(hash, &tri->numVerts, sizeof(tri->numVerts));
        hash = HashSmokeBytes(hash, &tri->numIndexes, sizeof(tri->numIndexes));
        hash = HashSmokeBytes(hash, &tri->ambientCache, sizeof(tri->ambientCache));
        hash = HashSmokeBytes(hash, &tri->indexCache, sizeof(tri->indexCache));
    }
    if (drawSurf && drawSurf->space)
    {
        hash = HashSmokeBytes(hash, drawSurf->space->modelMatrix, sizeof(drawSurf->space->modelMatrix));
    }
    return hash;
}

bool IntersectRayTriangle(const idVec3& rayOrigin, const idVec3& rayDirection, const idVec3& p0, const idVec3& p1, const idVec3& p2, float& hitDistance)
{
    const idVec3 edge1 = p1 - p0;
    const idVec3 edge2 = p2 - p0;
    const idVec3 pvec = rayDirection.Cross(edge2);
    const float det = edge1 * pvec;
    if (idMath::Fabs(det) < 1.0e-8f)
    {
        return false;
    }

    const float invDet = 1.0f / det;
    const idVec3 tvec = rayOrigin - p0;
    const float u = (tvec * pvec) * invDet;
    if (u < 0.0f || u > 1.0f)
    {
        return false;
    }

    const idVec3 qvec = tvec.Cross(edge1);
    const float v = (rayDirection * qvec) * invDet;
    if (v < 0.0f || u + v > 1.0f)
    {
        return false;
    }

    const float t = (edge2 * qvec) * invDet;
    if (t <= 0.1f)
    {
        return false;
    }

    hitDistance = t;
    return true;
}

void TransformSmokeSurfaceVertexToWorld(const drawSurf_t* drawSurf, const srfTriangles_t* tri, int vertexIndex, const idJointMat* rtCpuSkinningJoints, idVec3& worldPosition);
PathTraceSmokeVertex BuildSmokeSurfaceVertex(const drawSurf_t* drawSurf, const srfTriangles_t* tri, int vertexIndex, const idJointMat* rtCpuSkinningJoints);

bool FindCenterCameraRayAnchor(const viewDef_t* viewDef, idVec3& anchorPoint, int& anchorSurface, int& anchorTriangle)
{
    const idVec3 rayOrigin = viewDef->renderView.vieworg;
    idVec3 rayDirection = viewDef->renderView.viewaxis[0];
    rayDirection.Normalize();

    bool foundHit = false;
    float closestHit = 1.0e30f;
    anchorSurface = -1;
    anchorTriangle = -1;

    for (int surfaceIndex = 0; surfaceIndex < viewDef->numDrawSurfs; ++surfaceIndex)
    {
        const drawSurf_t* drawSurf = viewDef->drawSurfs[surfaceIndex];
        if (!drawSurf || !drawSurf->frontEndGeo)
        {
            continue;
        }

        const srfTriangles_t* tri = nullptr;
        if (!ValidateSmokeDrawSurface(viewDef, drawSurf, tri, nullptr))
        {
            continue;
        }

        const idJointMat* rtCpuSkinningJoints = GetSmokeRtCpuSkinningJoints(tri);
        for (int index = 0; index + 2 < tri->numIndexes; index += 3)
        {
            const int i0 = tri->indexes[index + 0];
            const int i1 = tri->indexes[index + 1];
            const int i2 = tri->indexes[index + 2];
            if (i0 < 0 || i1 < 0 || i2 < 0 || i0 >= tri->numVerts || i1 >= tri->numVerts || i2 >= tri->numVerts)
            {
                continue;
            }

            idVec3 p0;
            idVec3 p1;
            idVec3 p2;
            TransformSmokeSurfaceVertexToWorld(drawSurf, tri, i0, rtCpuSkinningJoints, p0);
            TransformSmokeSurfaceVertexToWorld(drawSurf, tri, i1, rtCpuSkinningJoints, p1);
            TransformSmokeSurfaceVertexToWorld(drawSurf, tri, i2, rtCpuSkinningJoints, p2);
            if (IsZeroAreaSmokeTriangle(p0, p1, p2))
            {
                continue;
            }

            float hitDistance = 0.0f;
            if (IntersectRayTriangle(rayOrigin, rayDirection, p0, p1, p2, hitDistance) && hitDistance < closestHit)
            {
                closestHit = hitDistance;
                anchorPoint = rayOrigin + rayDirection * hitDistance;
                anchorSurface = surfaceIndex;
                anchorTriangle = index / 3;
                foundHit = true;
            }
        }
    }

    return foundHit;
}

void LogSmokeCrosshairMaterialDump(const viewDef_t* viewDef, const RtSmokeMaterialTableBuild& table)
{
    idVec3 hitPoint = vec3_origin;
    int surfaceIndex = -1;
    int triangleIndex = -1;
    if (!FindCenterCameraRayAnchor(viewDef, hitPoint, surfaceIndex, triangleIndex))
    {
        common->Printf("PathTracePrimaryPass: RT smoke crosshair material dump found no center-ray hit\n");
        return;
    }

    if (!viewDef || surfaceIndex < 0 || surfaceIndex >= viewDef->numDrawSurfs)
    {
        common->Printf("PathTracePrimaryPass: RT smoke crosshair material dump invalid hit surface=%d triangle=%d\n", surfaceIndex, triangleIndex);
        return;
    }

    const drawSurf_t* drawSurf = viewDef->drawSurfs[surfaceIndex];
    const srfTriangles_t* tri = nullptr;
    if (!ValidateSmokeDrawSurface(viewDef, drawSurf, tri, nullptr) || !drawSurf || !drawSurf->material || !tri)
    {
        common->Printf("PathTracePrimaryPass: RT smoke crosshair material dump failed validation surface=%d triangle=%d\n", surfaceIndex, triangleIndex);
        return;
    }

    const idMaterial* material = drawSurf->material;
    const RtSmokeSurfaceClass surfaceClass = ClassifySmokeSurface(viewDef, drawSurf, tri);
    const RtSmokeTranslucentSubtype translucentSubtype = surfaceClass == RtSmokeSurfaceClass::ParticleAlpha ? ClassifySmokeTranslucentSubtype(drawSurf) : RtSmokeTranslucentSubtype::Unknown;
    const RtSmokeTranslucentClassifierInfo classifier = BuildSmokeTranslucentClassifierInfo(material);
    const uint32_t materialId = SmokeMaterialId(material);
    const RtSmokeMaterialTextureInfo info = ResolveSmokeMaterialTextureInfo(materialId, -1);

    int tableIndex = -1;
    for (int index = 0; index < static_cast<int>(table.materialIds.size()); ++index)
    {
        if (table.materialIds[index] == materialId)
        {
            tableIndex = index;
            break;
        }
    }

    common->Printf("PathTracePrimaryPass: RT smoke crosshair material hit surface=%d triangle=%d point=(%.2f %.2f %.2f) material='%s' id=%u tableIndex=%d class=%s subtype=%s coverage=%s sort=%.2f deform=%s cull=%d stages=%d guiSurface=%d\n",
        surfaceIndex,
        triangleIndex,
        hitPoint.x,
        hitPoint.y,
        hitPoint.z,
        material->GetName(),
        materialId,
        tableIndex,
        SmokeSurfaceClassName(surfaceClass),
        SmokeTranslucentSubtypeName(translucentSubtype),
        SmokeCoverageName(material->Coverage()),
        material->GetSort(),
        SmokeDeformName(material->Deform()),
        static_cast<int>(material->GetCullType()),
        material->GetNumStages(),
        IsSmokeGuiDrawSurface(drawSurf) ? 1 : 0);

    common->Printf("PathTracePrimaryPass: RT smoke crosshair classifiers guiSort=%d decalSort=%d postSort=%d polyOffset=%d screenTex=%d addBlend=%d ambient=%d ambientBlend=%d diffuse=%d nameGui=%d nameParticle=%d nameDecal=%d nameGlass=%d nameGlow=%d nameSignage=%d\n",
        classifier.sortIsGuiOrSubview ? 1 : 0,
        classifier.sortIsDecal ? 1 : 0,
        classifier.sortIsPostProcess ? 1 : 0,
        classifier.polygonOffsetDecal ? 1 : 0,
        classifier.hasScreenTexgen ? 1 : 0,
        classifier.hasAdditiveBlend ? 1 : 0,
        classifier.hasAmbientStage ? 1 : 0,
        classifier.hasAmbientBlendStage ? 1 : 0,
        classifier.hasDiffuseStage ? 1 : 0,
        classifier.nameLooksGui ? 1 : 0,
        classifier.nameLooksParticle ? 1 : 0,
        classifier.nameLooksDecal ? 1 : 0,
        classifier.nameLooksGlass ? 1 : 0,
        classifier.nameLooksGlow ? 1 : 0,
        classifier.nameLooksSignage ? 1 : 0);

    common->Printf("PathTracePrimaryPass: RT smoke crosshair RT metadata diffuse='%s' image=%d handle=%d safe=%d reason='%s' alpha='%s' image=%d handle=%d safe=%d reason='%s' hasAlphaTest=%d cutoff=%.3f alphaFromLuma=%d alphaDarkKey=%d normal='%s' safe=%d specular='%s' safe=%d emissive='%s' safe=%d emissive=%d filterDecal=%d blackKey=%d forceAlbedo=%d portalFallback=%d objectGlassFallback=%d fallbackAlbedo=%d(%.2f %.2f %.2f)\n",
        info.diffuseImageName.c_str(),
        info.hasDiffuseImage ? 1 : 0,
        info.hasTextureHandle ? 1 : 0,
        info.hasSafeTexture ? 1 : 0,
        info.fallbackReason.c_str(),
        info.alphaImageName.c_str(),
        info.hasAlphaImage ? 1 : 0,
        info.hasAlphaTextureHandle ? 1 : 0,
        info.hasSafeAlphaTexture ? 1 : 0,
        info.alphaReason.c_str(),
        info.hasAlphaTest ? 1 : 0,
        info.alphaCutoff,
        info.alphaFromDiffuseLuma ? 1 : 0,
        info.alphaFromDiffuseDarkKey ? 1 : 0,
        info.normalImageName.c_str(),
        info.hasSafeNormalTexture ? 1 : 0,
        info.specularImageName.c_str(),
        info.hasSafeSpecularTexture ? 1 : 0,
        info.emissiveImageName.c_str(),
        info.hasSafeEmissiveTexture ? 1 : 0,
        info.emissive ? 1 : 0,
        info.filterDecal ? 1 : 0,
        info.filterDecalBlackKey ? 1 : 0,
        info.forceFallbackAlbedo ? 1 : 0,
        info.portalWindowFallback ? 1 : 0,
        info.objectGlassFallback ? 1 : 0,
        info.hasFallbackAlbedo ? 1 : 0,
        info.fallbackAlbedo.x,
        info.fallbackAlbedo.y,
        info.fallbackAlbedo.z);

    if (tableIndex >= 0 && tableIndex < static_cast<int>(table.materials.size()))
    {
        const PathTraceSmokeMaterial& rtMaterial = table.materials[tableIndex];
        common->Printf("PathTracePrimaryPass: RT smoke crosshair RT material debugAlbedo=(%.2f %.2f %.2f %.2f) flags=0x%08x diffuseSlot=%d alphaSlot=%d normalSlot=%d specSlot=%d emissiveSlot=%d alphaCutoff=%.3f\n",
            rtMaterial.debugAlbedo[0],
            rtMaterial.debugAlbedo[1],
            rtMaterial.debugAlbedo[2],
            rtMaterial.debugAlbedo[3],
            rtMaterial.flags,
            rtMaterial.diffuseTextureIndex == UINT32_MAX ? -1 : static_cast<int>(rtMaterial.diffuseTextureIndex),
            rtMaterial.alphaTextureIndex == UINT32_MAX ? -1 : static_cast<int>(rtMaterial.alphaTextureIndex),
            rtMaterial.normalTextureIndex == UINT32_MAX ? -1 : static_cast<int>(rtMaterial.normalTextureIndex),
            rtMaterial.specularTextureIndex == UINT32_MAX ? -1 : static_cast<int>(rtMaterial.specularTextureIndex),
            rtMaterial.emissiveTextureIndex == UINT32_MAX ? -1 : static_cast<int>(rtMaterial.emissiveTextureIndex),
            rtMaterial.alphaCutoff);
    }

    const int indexBase = triangleIndex * 3;
    if (indexBase >= 0 && indexBase + 2 < tri->numIndexes)
    {
        const int i0 = tri->indexes[indexBase + 0];
        const int i1 = tri->indexes[indexBase + 1];
        const int i2 = tri->indexes[indexBase + 2];
        if (i0 >= 0 && i1 >= 0 && i2 >= 0 && i0 < tri->numVerts && i1 < tri->numVerts && i2 < tri->numVerts)
        {
            common->Printf("PathTracePrimaryPass: RT smoke crosshair triangle indexes=%d/%d/%d vertexColors=(%u %u %u %u),(%u %u %u %u),(%u %u %u %u)\n",
                i0, i1, i2,
                tri->verts[i0].color[0], tri->verts[i0].color[1], tri->verts[i0].color[2], tri->verts[i0].color[3],
                tri->verts[i1].color[0], tri->verts[i1].color[1], tri->verts[i1].color[2], tri->verts[i1].color[3],
                tri->verts[i2].color[0], tri->verts[i2].color[1], tri->verts[i2].color[2], tri->verts[i2].color[3]);
        }
    }

    const float* regs = drawSurf->shaderRegisters ? drawSurf->shaderRegisters : material->ConstantRegisters();
    const int registerCount = material->GetNumRegisters();
    for (int stageIndex = 0; stageIndex < material->GetNumStages(); ++stageIndex)
    {
        const shaderStage_t* stage = material->GetStage(stageIndex);
        if (!stage)
        {
            continue;
        }

        idVec4 stageColor(1.0f, 1.0f, 1.0f, 1.0f);
        if (regs)
        {
            for (int component = 0; component < 4; ++component)
            {
                const int colorRegister = stage->color.registers[component];
                if (colorRegister >= 0 && colorRegister < registerCount)
                {
                    stageColor[component] = regs[colorRegister];
                }
            }
        }

        const float condition = regs && stage->conditionRegister >= 0 && stage->conditionRegister < registerCount ? regs[stage->conditionRegister] : 1.0f;
        const float alphaTest = regs && stage->alphaTestRegister >= 0 && stage->alphaTestRegister < registerCount ? regs[stage->alphaTestRegister] : -1.0f;
        idImage* image = stage->texture.image;
        const bool imageSafe = image && IsSmokeDiffuseImageSafeForRayTracing(image);
        common->Printf("PathTracePrimaryPass: RT smoke crosshair stage[%d] lighting=%s condition=%.3f color=(%.3f %.3f %.3f %.3f) drawState=0x%llx srcBlend=%llu dstBlend=%llu alphaTest=%d alphaReg=%d alphaValue=%.3f ignoreAlpha=%d texgen=%s dynamic=%d cinematic=%d image='%s' safe=%d\n",
            stageIndex,
            SmokeStageLightingName(stage->lighting),
            condition,
            stageColor.x,
            stageColor.y,
            stageColor.z,
            stageColor.w,
            static_cast<unsigned long long>(stage->drawStateBits),
            static_cast<unsigned long long>((stage->drawStateBits & GLS_SRCBLEND_BITS) >> 0),
            static_cast<unsigned long long>((stage->drawStateBits & GLS_DSTBLEND_BITS) >> 3),
            stage->hasAlphaTest ? 1 : 0,
            stage->alphaTestRegister,
            alphaTest,
            stage->ignoreAlphaTest ? 1 : 0,
            SmokeTexgenName(stage->texture.texgen),
            static_cast<int>(stage->texture.dynamic),
            stage->texture.cinematic ? 1 : 0,
            image ? image->GetName() : "<none>",
            imageSafe ? 1 : 0);
    }
}

void LogSmokeGuiSurfaceDump(const viewDef_t* viewDef, const RtSmokeMaterialTableBuild& table)
{
    if (!viewDef)
    {
        common->Printf("PathTracePrimaryPass: RT smoke GUI dump no viewDef\n");
        return;
    }

    const int maxLogged = 24;
    int guiSurfaces = 0;
    int capturedGuiSurfaces = 0;
    int logged = 0;
    common->Printf("PathTracePrimaryPass: RT smoke GUI dump drawSurfs=%d allowGuiSurfaces=%d allowGuiTextures=%d\n",
        viewDef->numDrawSurfs,
        r_pathTracingAllowGuiSurfaces.GetInteger() != 0 ? 1 : 0,
        r_pathTracingAllowGuiTextures.GetInteger() != 0 ? 1 : 0);

    for (int surfaceIndex = 0; surfaceIndex < viewDef->numDrawSurfs; ++surfaceIndex)
    {
        const drawSurf_t* drawSurf = viewDef->drawSurfs[surfaceIndex];
        if (!IsSmokeGuiDrawSurface(drawSurf))
        {
            continue;
        }

        ++guiSurfaces;
        const srfTriangles_t* tri = nullptr;
        const bool captured = ValidateSmokeDrawSurface(viewDef, drawSurf, tri, nullptr);
        if (captured)
        {
            ++capturedGuiSurfaces;
        }

        if (logged >= maxLogged)
        {
            continue;
        }

        const idMaterial* material = drawSurf ? drawSurf->material : nullptr;
        const char* materialName = material ? material->GetName() : "<none>";
        const uint32_t materialId = HashSmokeMaterialName(materialName);
        int tableIndex = -1;
        std::vector<uint32_t>::const_iterator tableIt = std::find(table.materialIds.begin(), table.materialIds.end(), materialId);
        if (tableIt != table.materialIds.end())
        {
            tableIndex = static_cast<int>(tableIt - table.materialIds.begin());
        }

        idVec4 colorMin(1.0f, 1.0f, 1.0f, 1.0f);
        idVec4 colorMax(0.0f, 0.0f, 0.0f, 0.0f);
        idVec2 uvMin(1.0e20f, 1.0e20f);
        idVec2 uvMax(-1.0e20f, -1.0e20f);
        if (tri && tri->verts)
        {
            for (int vertIndex = 0; vertIndex < tri->numVerts; ++vertIndex)
            {
                const idDrawVert& vert = tri->verts[vertIndex];
                for (int component = 0; component < 4; ++component)
                {
                    const float c = vert.color[component] * (1.0f / 255.0f);
                    colorMin[component] = Min(colorMin[component], c);
                    colorMax[component] = Max(colorMax[component], c);
                }
                const idVec2 uv = vert.GetTexCoord();
                uvMin.x = Min(uvMin.x, uv.x);
                uvMin.y = Min(uvMin.y, uv.y);
                uvMax.x = Max(uvMax.x, uv.x);
                uvMax.y = Max(uvMax.y, uv.y);
            }
        }

        const RtSmokeMaterialTextureInfo info = ResolveSmokeMaterialTextureInfo(materialId, tableIndex);
        const PathTraceSmokeMaterial* rtMaterial = tableIndex >= 0 && tableIndex < static_cast<int>(table.materials.size()) ? &table.materials[tableIndex] : nullptr;
        common->Printf("PathTracePrimaryPass: RT smoke GUI surface[%d] captured=%d table=%d id=%u material='%s' verts=%d indexes=%d colorMin=(%.2f %.2f %.2f %.2f) colorMax=(%.2f %.2f %.2f %.2f) uvMin=(%.2f %.2f) uvMax=(%.2f %.2f) diffuse='%s' safe=%d handle=%d slot=%d reason='%s'\n",
            surfaceIndex,
            captured ? 1 : 0,
            tableIndex,
            materialId,
            materialName,
            tri ? tri->numVerts : 0,
            tri ? tri->numIndexes : 0,
            colorMin.x, colorMin.y, colorMin.z, colorMin.w,
            colorMax.x, colorMax.y, colorMax.z, colorMax.w,
            uvMin.x, uvMin.y,
            uvMax.x, uvMax.y,
            info.diffuseImageName.c_str(),
            info.hasSafeTexture ? 1 : 0,
            info.hasTextureHandle ? 1 : 0,
            rtMaterial && rtMaterial->diffuseTextureIndex != UINT32_MAX ? static_cast<int>(rtMaterial->diffuseTextureIndex) : -1,
            info.fallbackReason.c_str());

        if (material)
        {
            const float* regs = drawSurf && drawSurf->shaderRegisters ? drawSurf->shaderRegisters : material->ConstantRegisters();
            const int registerCount = material->GetNumRegisters();
            for (int stageIndex = 0; stageIndex < material->GetNumStages(); ++stageIndex)
            {
                const shaderStage_t* stage = material->GetStage(stageIndex);
                if (!stage)
                {
                    continue;
                }

                idVec4 stageColor(1.0f, 1.0f, 1.0f, 1.0f);
                if (regs)
                {
                    for (int component = 0; component < 4; ++component)
                    {
                        const int colorRegister = stage->color.registers[component];
                        if (colorRegister >= 0 && colorRegister < registerCount)
                        {
                            stageColor[component] = regs[colorRegister];
                        }
                    }
                }
                common->Printf("PathTracePrimaryPass: RT smoke GUI surface[%d] stage[%d] lighting=%s color=(%.3f %.3f %.3f %.3f) texgen=%s dynamic=%d image='%s' safe=%d\n",
                    surfaceIndex,
                    stageIndex,
                    SmokeStageLightingName(stage->lighting),
                    stageColor.x, stageColor.y, stageColor.z, stageColor.w,
                    SmokeTexgenName(stage->texture.texgen),
                    static_cast<int>(stage->texture.dynamic),
                    stage->texture.image ? stage->texture.image->GetName() : "<none>",
                    stage->texture.image && IsSmokeDiffuseImageSafeForRayTracing(stage->texture.image) ? 1 : 0);
            }
        }

        ++logged;
    }

    common->Printf("PathTracePrimaryPass: RT smoke GUI dump summary guiSurfaces=%d captured=%d logged=%d\n",
        guiSurfaces,
        capturedGuiSurfaces,
        logged);
}

PathTraceSmokeVertex BuildSmokeSurfaceVertex(const drawSurf_t* drawSurf, const srfTriangles_t* tri, int vertexIndex, const idJointMat* rtCpuSkinningJoints)
{
    const idDrawVert& drawVert = tri->verts[vertexIndex];
    idVec3 localPosition = drawVert.xyz;
    idVec3 localNormal = drawVert.GetNormal();
    if (rtCpuSkinningJoints)
    {
        localPosition = TransformSmokeSkinnedVertexPosition(drawVert, rtCpuSkinningJoints);
        localNormal = TransformSmokeSkinnedVertexNormal(drawVert, rtCpuSkinningJoints);
    }

    idVec3 worldPosition;
    idVec3 worldNormal;
    TransformSurfacePointToWorld(drawSurf, localPosition, worldPosition);
    TransformSurfaceVectorToWorld(drawSurf, localNormal, worldNormal);
    worldNormal.Normalize();

    const idVec2 texCoord = drawVert.GetTexCoord();
    PathTraceSmokeVertex vertex = {};
    vertex.position[0] = worldPosition.x;
    vertex.position[1] = worldPosition.y;
    vertex.position[2] = worldPosition.z;
    vertex.position[3] = 1.0f;
    vertex.normal[0] = worldNormal.x;
    vertex.normal[1] = worldNormal.y;
    vertex.normal[2] = worldNormal.z;
    vertex.normal[3] = 0.0f;
    vertex.texCoord[0] = texCoord.x;
    vertex.texCoord[1] = texCoord.y;
    vertex.texCoord[2] = 0.0f;
    vertex.texCoord[3] = 0.0f;
    vertex.color[0] = drawVert.color[0] * (1.0f / 255.0f);
    vertex.color[1] = drawVert.color[1] * (1.0f / 255.0f);
    vertex.color[2] = drawVert.color[2] * (1.0f / 255.0f);
    vertex.color[3] = drawVert.color[3] * (1.0f / 255.0f);
    vertex.color2[0] = drawVert.color2[0] * (1.0f / 255.0f);
    vertex.color2[1] = drawVert.color2[1] * (1.0f / 255.0f);
    vertex.color2[2] = drawVert.color2[2] * (1.0f / 255.0f);
    vertex.color2[3] = drawVert.color2[3] * (1.0f / 255.0f);
    return vertex;
}

void TransformSmokeSurfaceVertexToWorld(const drawSurf_t* drawSurf, const srfTriangles_t* tri, int vertexIndex, const idJointMat* rtCpuSkinningJoints, idVec3& worldPosition)
{
    const PathTraceSmokeVertex vertex = BuildSmokeSurfaceVertex(drawSurf, tri, vertexIndex, rtCpuSkinningJoints);
    worldPosition.Set(vertex.position[0], vertex.position[1], vertex.position[2]);
}

int AppendSmokeSurfaceGeometry(const drawSurf_t* drawSurf, const srfTriangles_t* tri, uint32_t surfaceClassId, uint32_t materialId, std::vector<PathTraceSmokeVertex>& vertices, std::vector<uint32_t>& indexes, std::vector<uint32_t>& triangleClasses, std::vector<uint32_t>& triangleMaterials, RtSmokeSurfaceSkipStats& skipStats, RtSmokeAttributeStats& attributeStats)
{
    const size_t vertexStart = vertices.size();
    const size_t indexStart = indexes.size();
    const size_t classStart = triangleClasses.size();
    const size_t materialStart = triangleMaterials.size();
    const uint32_t indexBase = static_cast<uint32_t>(vertices.size());
    const idJointMat* rtCpuSkinningJoints = GetSmokeRtCpuSkinningJoints(tri);
    const int classIndex = idMath::ClampInt(0, RT_SMOKE_CLASS_COUNT - 1, static_cast<int>(surfaceClassId & RT_SMOKE_TRIANGLE_CLASS_MASK));

    for (int vertexIndex = 0; vertexIndex < tri->numVerts; ++vertexIndex)
    {
        PathTraceSmokeVertex vertex = BuildSmokeSurfaceVertex(drawSurf, tri, vertexIndex, rtCpuSkinningJoints);
        const idVec3 normal = SmokeVertexNormal(vertex);
        const idVec2 texCoord = SmokeVertexTexCoord(vertex);
        if (!SmokeNormalIsUsable(normal))
        {
            ++attributeStats.classes[classIndex].invalidNormalVerts;
            vertex.normal[0] = 0.0f;
            vertex.normal[1] = 0.0f;
            vertex.normal[2] = 0.0f;
        }
        if (!SmokeTexCoordIsUsable(texCoord))
        {
            ++attributeStats.classes[classIndex].invalidUvVerts;
            vertex.texCoord[0] = 0.0f;
            vertex.texCoord[1] = 0.0f;
        }
        vertices.push_back(vertex);
    }

    for (int sourceIndex = 0; sourceIndex + 2 < tri->numIndexes; sourceIndex += 3)
    {
        const int i0 = tri->indexes[sourceIndex + 0];
        const int i1 = tri->indexes[sourceIndex + 1];
        const int i2 = tri->indexes[sourceIndex + 2];
        if (i0 < 0 || i1 < 0 || i2 < 0 || i0 >= tri->numVerts || i1 >= tri->numVerts || i2 >= tri->numVerts)
        {
            ++skipStats.invalidIndexCount;
            continue;
        }

        const PathTraceSmokeVertex& v0 = vertices[indexBase + static_cast<uint32_t>(i0)];
        const PathTraceSmokeVertex& v1 = vertices[indexBase + static_cast<uint32_t>(i1)];
        const PathTraceSmokeVertex& v2 = vertices[indexBase + static_cast<uint32_t>(i2)];
        const idVec3 p0 = SmokeVertexPosition(v0);
        const idVec3 p1 = SmokeVertexPosition(v1);
        const idVec3 p2 = SmokeVertexPosition(v2);
        if (IsZeroAreaSmokeTriangle(p0, p1, p2))
        {
            continue;
        }

        indexes.push_back(indexBase + static_cast<uint32_t>(i0));
        indexes.push_back(indexBase + static_cast<uint32_t>(i1));
        indexes.push_back(indexBase + static_cast<uint32_t>(i2));
        const bool invalidNormalTriangle =
            !SmokeNormalIsUsable(SmokeVertexNormal(v0)) ||
            !SmokeNormalIsUsable(SmokeVertexNormal(v1)) ||
            !SmokeNormalIsUsable(SmokeVertexNormal(v2));
        const bool invalidUvTriangle =
            !SmokeTexCoordIsUsable(SmokeVertexTexCoord(v0)) ||
            !SmokeTexCoordIsUsable(SmokeVertexTexCoord(v1)) ||
            !SmokeTexCoordIsUsable(SmokeVertexTexCoord(v2));
        const bool preferGeometricNormal =
            invalidNormalTriangle ||
            static_cast<RtSmokeSurfaceClass>(classIndex) == RtSmokeSurfaceClass::ParticleAlpha;

        if (invalidNormalTriangle)
        {
            ++attributeStats.classes[classIndex].invalidNormalTriangles;
        }
        if (invalidUvTriangle)
        {
            ++attributeStats.classes[classIndex].invalidUvTriangles;
        }
        if (preferGeometricNormal)
        {
            ++attributeStats.classes[classIndex].forcedGeometricNormalTriangles;
        }

        triangleClasses.push_back(surfaceClassId | (preferGeometricNormal ? RT_SMOKE_TRIANGLE_FORCE_GEOMETRIC_NORMAL : 0u));
        triangleMaterials.push_back(materialId);
    }

    const int emittedIndexes = static_cast<int>(indexes.size() - indexStart);
    if (emittedIndexes <= 0 || triangleClasses.size() == classStart || triangleMaterials.size() == materialStart)
    {
        vertices.resize(vertexStart);
        indexes.resize(indexStart);
        triangleClasses.resize(classStart);
        triangleMaterials.resize(materialStart);
        ++skipStats.zeroAreaOnly;
        return 0;
    }

    return emittedIndexes;
}

bool CaptureDoomSurfacesForSmokeTest(const viewDef_t* viewDef, std::vector<PathTraceSmokeVertex>& vertexData, std::vector<uint32_t>& indexData, std::vector<uint32_t>& triangleClassData, std::vector<uint32_t>& triangleMaterialData, std::vector<uint64>& staticSurfaceKeys, std::vector<PathTraceSmokeVertex>& staticVertexCache, std::vector<uint32_t>& staticIndexCache, std::vector<uint32_t>& staticTriangleClassCache, std::vector<uint32_t>& staticTriangleMaterialCache, bool& staticCacheChanged, idVec3& sceneOrigin, int& sourceSurfaces, int& sourceVerts, int& sourceIndexes, int& anchorTriangle, RtSmokeSurfaceClassStats& classStats, RtSmokeSurfaceSkipStats& skipStats, RtSmokeDynamicGeometryStats& dynamicStats, RtSmokeAttributeStats& attributeStats, RtSmokeMaterialStats& materialStats, RtSmokeBucketRanges& bucketRanges, RtSmokeSurfaceClassReasonSamples* reasonSamples)
{
    sourceSurfaces = 0;
    sourceVerts = 0;
    sourceIndexes = 0;
    staticCacheChanged = false;
    int anchorSurface = -1;
    anchorTriangle = -1;
    classStats = RtSmokeSurfaceClassStats();
    skipStats = RtSmokeSurfaceSkipStats();
    dynamicStats = RtSmokeDynamicGeometryStats();
    attributeStats = RtSmokeAttributeStats();
    materialStats = RtSmokeMaterialStats();
    bucketRanges = RtSmokeBucketRanges();
    if (reasonSamples)
    {
        *reasonSamples = RtSmokeSurfaceClassReasonSamples();
    }

    if (!viewDef || !viewDef->drawSurfs)
    {
        return false;
    }

    if (!FindCenterCameraRayAnchor(viewDef, sceneOrigin, anchorSurface, anchorTriangle))
    {
        return false;
    }

    vertexData.clear();
    indexData.clear();
    triangleClassData.clear();
    triangleMaterialData.clear();
    vertexData.reserve(RT_SMOKE_MAX_VERTS);
    indexData.reserve(RT_SMOKE_MAX_INDEXES);
    triangleClassData.reserve(RT_SMOKE_MAX_INDEXES / 3);
    triangleMaterialData.reserve(RT_SMOKE_MAX_INDEXES / 3);

    std::vector<PathTraceSmokeVertex> bucketVertexData[RT_SMOKE_CLASS_COUNT];
    std::vector<uint32_t> bucketIndexData[RT_SMOKE_CLASS_COUNT];
    std::vector<uint32_t> bucketTriangleClassData[RT_SMOKE_CLASS_COUNT];
    std::vector<uint32_t> bucketTriangleMaterialData[RT_SMOKE_CLASS_COUNT];
    for (int bucketIndex = 0; bucketIndex < RT_SMOKE_CLASS_COUNT; ++bucketIndex)
    {
        bucketVertexData[bucketIndex].reserve(RT_SMOKE_MAX_VERTS / RT_SMOKE_CLASS_COUNT);
        bucketIndexData[bucketIndex].reserve(RT_SMOKE_MAX_INDEXES / RT_SMOKE_CLASS_COUNT);
        bucketTriangleClassData[bucketIndex].reserve(RT_SMOKE_MAX_INDEXES / (3 * RT_SMOKE_CLASS_COUNT));
        bucketTriangleMaterialData[bucketIndex].reserve(RT_SMOKE_MAX_INDEXES / (3 * RT_SMOKE_CLASS_COUNT));
    }

    int dynamicVerts = 0;
    int dynamicIndexes = 0;
    int dynamicSurfaces = 0;

    for (int surfaceIndex = 0; surfaceIndex < viewDef->numDrawSurfs; ++surfaceIndex)
    {
        const drawSurf_t* drawSurf = viewDef->drawSurfs[surfaceIndex];
        const srfTriangles_t* tri = nullptr;
        if (!ValidateSmokeDrawSurface(viewDef, drawSurf, tri, nullptr))
        {
            continue;
        }

        const RtSmokeSurfaceClass surfaceClass = ClassifySmokeSurface(viewDef, drawSurf, tri);
        if (surfaceClass != RtSmokeSurfaceClass::StaticWorld)
        {
            continue;
        }

        const RtSmokeTranslucentSubtype translucentSubtype = RtSmokeTranslucentSubtype::Unknown;
        const uint32_t surfaceClassId = SmokeSurfaceClassAndSubtypeId(surfaceClass, translucentSubtype);
        const uint32_t materialId = SmokeMaterialId(drawSurf->material);
        const uint64 staticSurfaceKey = BuildSmokeStaticSurfaceKey(drawSurf, tri);
        const bool staticSurfaceCached = std::find(staticSurfaceKeys.begin(), staticSurfaceKeys.end(), staticSurfaceKey) != staticSurfaceKeys.end();
        ++sourceSurfaces;
        ++bucketRanges.buckets[0].surfaceCount;
        AddSmokeMaterialStats(materialStats, drawSurf->material, tri->numIndexes, surfaceClass, translucentSubtype);

        if (staticSurfaceCached)
        {
            sourceVerts += tri->numVerts;
            sourceIndexes += tri->numIndexes;
            AddSmokeSurfaceClassStats(classStats, surfaceClass, tri->numVerts, tri->numIndexes);
            if (reasonSamples)
            {
                AddSmokeSurfaceClassReasonSample(*reasonSamples, BuildSmokeSurfaceClassReason(viewDef, drawSurf, tri, surfaceIndex, surfaceClass));
            }
            continue;
        }

        const int cachedVerts = static_cast<int>(staticVertexCache.size());
        const int cachedIndexes = static_cast<int>(staticIndexCache.size());
        if (cachedVerts + tri->numVerts > RT_SMOKE_MAX_VERTS ||
            cachedIndexes + tri->numIndexes > RT_SMOKE_MAX_INDEXES)
        {
            ++skipStats.limitExceeded;
            continue;
        }

        const int emittedIndexes = AppendSmokeSurfaceGeometry(drawSurf, tri, surfaceClassId, materialId, staticVertexCache, staticIndexCache, staticTriangleClassCache, staticTriangleMaterialCache, skipStats, attributeStats);
        if (emittedIndexes <= 0)
        {
            continue;
        }

        staticSurfaceKeys.push_back(staticSurfaceKey);
        staticCacheChanged = true;
        sourceVerts += tri->numVerts;
        sourceIndexes += emittedIndexes;
        AddSmokeSurfaceClassStats(classStats, surfaceClass, tri->numVerts, emittedIndexes);
        if (reasonSamples)
        {
            AddSmokeSurfaceClassReasonSample(*reasonSamples, BuildSmokeSurfaceClassReason(viewDef, drawSurf, tri, surfaceIndex, surfaceClass));
        }
    }

    for (int surfaceOffset = 0; surfaceOffset < viewDef->numDrawSurfs; ++surfaceOffset)
    {
        const int surfaceIndex = (anchorSurface + surfaceOffset) % viewDef->numDrawSurfs;
        const drawSurf_t* drawSurf = viewDef->drawSurfs[surfaceIndex];
        const srfTriangles_t* tri = nullptr;
        if (!ValidateSmokeDrawSurface(viewDef, drawSurf, tri, &skipStats))
        {
            continue;
        }

        if (dynamicSurfaces >= RT_SMOKE_MAX_SURFACES)
        {
            ++skipStats.limitExceeded;
            break;
        }

        const RtSmokeSurfaceClass surfaceClass = ClassifySmokeSurface(viewDef, drawSurf, tri);
        const RtSmokeTranslucentSubtype translucentSubtype = surfaceClass == RtSmokeSurfaceClass::ParticleAlpha ? ClassifySmokeTranslucentSubtype(drawSurf) : RtSmokeTranslucentSubtype::Unknown;
        const uint32_t surfaceClassId = SmokeSurfaceClassAndSubtypeId(surfaceClass, translucentSubtype);
        const uint32_t materialId = SmokeMaterialId(drawSurf->material);
        const int bucketIndex = idMath::ClampInt(0, RT_SMOKE_CLASS_COUNT - 1, static_cast<int>(surfaceClassId & RT_SMOKE_TRIANGLE_CLASS_MASK));
        const bool isStaticWorld = surfaceClass == RtSmokeSurfaceClass::StaticWorld;
        if (isStaticWorld)
        {
            continue;
        }

        const int cachedVerts = static_cast<int>(staticVertexCache.size());
        const int cachedIndexes = static_cast<int>(staticIndexCache.size());
        if (cachedVerts + dynamicVerts + tri->numVerts > RT_SMOKE_MAX_VERTS ||
            cachedIndexes + dynamicIndexes + tri->numIndexes > RT_SMOKE_MAX_INDEXES)
        {
            ++skipStats.limitExceeded;
            continue;
        }

        std::vector<PathTraceSmokeVertex>& bucketVertices = bucketVertexData[bucketIndex];
        std::vector<uint32_t>& bucketIndexes = bucketIndexData[bucketIndex];
        std::vector<uint32_t>& bucketClasses = bucketTriangleClassData[bucketIndex];
        std::vector<uint32_t>& bucketMaterials = bucketTriangleMaterialData[bucketIndex];
        const int emittedIndexes = AppendSmokeSurfaceGeometry(drawSurf, tri, surfaceClassId, materialId, bucketVertices, bucketIndexes, bucketClasses, bucketMaterials, skipStats, attributeStats);
        if (emittedIndexes <= 0)
        {
            continue;
        }

        AddSmokeMaterialStats(materialStats, drawSurf->material, emittedIndexes, surfaceClass, translucentSubtype);
        if (surfaceClass == RtSmokeSurfaceClass::ParticleAlpha)
        {
            AddSmokeTranslucentDebugSample(materialStats, drawSurf, tri, surfaceIndex, translucentSubtype);
        }
        ++sourceSurfaces;
        ++dynamicSurfaces;
        sourceVerts += tri->numVerts;
        sourceIndexes += emittedIndexes;
        AddSmokeSurfaceClassStats(classStats, surfaceClass, tri->numVerts, emittedIndexes);
        AddSmokeDynamicGeometryStats(dynamicStats, surfaceClass, drawSurf, tri, emittedIndexes);
        ++bucketRanges.buckets[bucketIndex].surfaceCount;
        dynamicVerts += tri->numVerts;
        dynamicIndexes += emittedIndexes;
        if (reasonSamples)
        {
            AddSmokeSurfaceClassReasonSample(*reasonSamples, BuildSmokeSurfaceClassReason(viewDef, drawSurf, tri, surfaceIndex, surfaceClass));
        }
    }

    RtSmokeBucketRange& staticRange = bucketRanges.buckets[0];
    staticRange.vertexOffset = 0;
    staticRange.indexOffset = 0;
    staticRange.triangleOffset = 0;
    staticRange.vertexCount = static_cast<int>(staticVertexCache.size());
    staticRange.indexCount = static_cast<int>(staticIndexCache.size());
    staticRange.triangleCount = static_cast<int>(staticTriangleClassCache.size());

    for (int bucketIndex = 0; bucketIndex < RT_SMOKE_CLASS_COUNT; ++bucketIndex)
    {
        if (bucketIndex == 0)
        {
            continue;
        }

        RtSmokeBucketRange& range = bucketRanges.buckets[bucketIndex];
        range.vertexOffset = static_cast<int>(vertexData.size());
        range.indexOffset = static_cast<int>(indexData.size());
        range.triangleOffset = static_cast<int>(triangleClassData.size());
        range.vertexCount = static_cast<int>(bucketVertexData[bucketIndex].size());
        range.indexCount = static_cast<int>(bucketIndexData[bucketIndex].size());
        range.triangleCount = static_cast<int>(bucketTriangleClassData[bucketIndex].size());

        const uint32_t vertexOffset = static_cast<uint32_t>(range.vertexOffset);
        vertexData.insert(vertexData.end(), bucketVertexData[bucketIndex].begin(), bucketVertexData[bucketIndex].end());
        for (uint32_t localIndex : bucketIndexData[bucketIndex])
        {
            indexData.push_back(vertexOffset + localIndex);
        }
        triangleClassData.insert(triangleClassData.end(), bucketTriangleClassData[bucketIndex].begin(), bucketTriangleClassData[bucketIndex].end());
        triangleMaterialData.insert(triangleMaterialData.end(), bucketTriangleMaterialData[bucketIndex].begin(), bucketTriangleMaterialData[bucketIndex].end());
    }

    if ((staticTriangleClassCache.empty() && triangleClassData.empty()) ||
        (staticTriangleMaterialCache.empty() && triangleMaterialData.empty()))
    {
        ++skipStats.emptyClassBuffer;
    }

    const bool hasStaticGeometry = !staticVertexCache.empty() && !staticIndexCache.empty() && !staticTriangleClassCache.empty() && !staticTriangleMaterialCache.empty();
    const bool hasDynamicGeometry = !vertexData.empty() && !indexData.empty() && !triangleClassData.empty() && !triangleMaterialData.empty();
    return sourceSurfaces > 0 && (hasStaticGeometry || hasDynamicGeometry);
}

}

PathTracePrimaryPass::PathTracePrimaryPass(idRenderBackend* backend)
    : m_backend(backend)
    , m_reportedMode(false)
    , m_rayTracingSupported(false)
    , m_smokeTestInitialized(false)
    , m_smokeSceneBuilt(false)
    , m_smokeSceneRebuildLogged(false)
    , m_smokeTestDispatched(false)
    , m_smokeWaitingForDoomSurfaceLogged(false)
    , m_smokeReadbackQueued(false)
    , m_smokeReadbackLogged(false)
    , m_smokeReadbackDelayFrames(0)
    , m_smokeReadbackCooldownFrames(0)
    , m_smokeSceneLogCooldownFrames(0)
    , m_smokeOutputWidth(0)
    , m_smokeOutputHeight(0)
    , m_smokeStaticBlasCacheValid(false)
    , m_smokeStaticBlasSignature(0)
    , m_smokeStaticBlasCacheHitCount(0)
    , m_smokeStaticBlasCacheMissCount(0)
    , m_smokeTextureProbeMaterialId(0)
    , m_smokeTextureProbeRequestedIndex(-1)
    , m_smokeSceneOrigin(vec3_origin)
{
    nvrhi::IDevice* device = deviceManager ? deviceManager->GetDevice() : nullptr;
    if (device)
    {
        m_rayTracingSupported =
            device->queryFeatureSupport(nvrhi::Feature::RayTracingAccelStruct) &&
            device->queryFeatureSupport(nvrhi::Feature::RayTracingPipeline);
    }

    common->Printf("PathTracePrimaryPass: initialized, ray tracing %s\n",
        m_rayTracingSupported ? "available" : "unavailable");
}

PathTracePrimaryPass::~PathTracePrimaryPass()
{
}

void PathTracePrimaryPass::Execute(const viewDef_t* viewDef)
{
    const int mode = r_pathTracing.GetInteger();

    if (!m_reportedMode)
    {
        common->Printf("PathTracePrimaryPass: mode %d (%s)\n",
            mode, mode == 2 ? "pure primary rays" : "hybrid");

        if (!m_rayTracingSupported)
        {
            common->Printf("PathTracePrimaryPass: RT device features are not available; restart with r_pathTracing enabled before device creation\n");
        }

        m_reportedMode = true;
    }

    if (!m_rayTracingSupported)
    {
        return;
    }

    InitRayTracingSmokeTest();
    const int outputWidth = idMath::ClampInt(RT_SMOKE_MIN_OUTPUT_WIDTH, RT_SMOKE_MAX_OUTPUT_WIDTH, r_pathTracingDebugWidth.GetInteger());
    const int outputHeight = idMath::ClampInt(RT_SMOKE_MIN_OUTPUT_HEIGHT, RT_SMOKE_MAX_OUTPUT_HEIGHT, r_pathTracingDebugHeight.GetInteger());
    if (!ResizeRayTracingSmokeOutput(outputWidth, outputHeight))
    {
        return;
    }

    BuildRayTracingSmokeTestScene(viewDef);
    ExecuteRayTracingSmokeTest(viewDef);
    ReadBackRayTracingSmokeTest();
}

void PathTracePrimaryPass::InitRayTracingSmokeTest()
{
    if (m_smokeTestInitialized)
    {
        return;
    }

    m_smokeTestInitialized = true;

    nvrhi::IDevice* device = deviceManager ? deviceManager->GetDevice() : nullptr;
    if (!device)
    {
        common->Printf("PathTracePrimaryPass: cannot initialize RT smoke test without an NVRHI device\n");
        return;
    }

    const char* shaderPath = nullptr;
    if (deviceManager->GetGraphicsAPI() == nvrhi::GraphicsAPI::D3D12)
    {
        shaderPath = "renderprogs2/dxil/builtin/pathtracing/pathtrace_smoke.rt.bin";
    }
    else if (deviceManager->GetGraphicsAPI() == nvrhi::GraphicsAPI::VULKAN)
    {
        shaderPath = "renderprogs2/spirv/builtin/pathtracing/pathtrace_smoke.rt.bin";
    }
    else
    {
        common->Printf("PathTracePrimaryPass: RT smoke test does not support this graphics API\n");
        return;
    }

    void* shaderData = nullptr;
    ID_TIME_T shaderTimestamp = 0;
    const int shaderSize = fileSystem->ReadFile(shaderPath, &shaderData, &shaderTimestamp);
    if (shaderSize <= 0 || !shaderData)
    {
        common->Printf("PathTracePrimaryPass: couldn't read %s\n", shaderPath);
        return;
    }

    common->Printf("PathTracePrimaryPass: loaded RT smoke shader %s (%d bytes, timestamp %u)\n",
        shaderPath, shaderSize, static_cast<unsigned int>(shaderTimestamp));

    m_smokeShaderLibrary = device->createShaderLibrary(shaderData, shaderSize);
    Mem_Free(shaderData);

    if (!m_smokeShaderLibrary)
    {
        common->Printf("PathTracePrimaryPass: failed to create RT smoke shader library\n");
        return;
    }

    m_smokeTlas = device->createAccelStruct(nvrhi::rt::AccelStructDesc()
        .setTopLevelMaxInstances(2)
        .setBuildFlags(nvrhi::rt::AccelStructBuildFlags::PreferFastTrace)
        .setDebugName("PathTraceSmokeTLAS"));

    if (!m_smokeTlas)
    {
        common->Printf("PathTracePrimaryPass: failed to create RT smoke TLAS\n");
        return;
    }

    nvrhi::BufferDesc constantsDesc;
    constantsDesc.byteSize = sizeof(PathTraceSmokeConstants);
    constantsDesc.debugName = "PathTraceSmokeConstants";
    constantsDesc.isConstantBuffer = true;
    constantsDesc.initialState = nvrhi::ResourceStates::ConstantBuffer;
    constantsDesc.keepInitialState = true;
    m_smokeConstantsBuffer = device->createBuffer(constantsDesc);

    if (!m_smokeConstantsBuffer)
    {
        common->Printf("PathTracePrimaryPass: failed to create RT smoke constants buffer\n");
        return;
    }

    nvrhi::BindingLayoutDesc bindingLayoutDesc;
    bindingLayoutDesc.visibility = nvrhi::ShaderType::AllRayTracing;
    bindingLayoutDesc.bindingOffsets = nvrhi::VulkanBindingOffsets()
        .setShaderResourceOffset(0)
        .setConstantBufferOffset(0)
        .setUnorderedAccessViewOffset(0);
    bindingLayoutDesc.addItem(nvrhi::BindingLayoutItem::RayTracingAccelStruct(0));
    bindingLayoutDesc.addItem(nvrhi::BindingLayoutItem::Texture_UAV(1));
    bindingLayoutDesc.addItem(nvrhi::BindingLayoutItem::ConstantBuffer(2));
    bindingLayoutDesc.addItem(nvrhi::BindingLayoutItem::StructuredBuffer_SRV(3));
    bindingLayoutDesc.addItem(nvrhi::BindingLayoutItem::StructuredBuffer_SRV(4));
    bindingLayoutDesc.addItem(nvrhi::BindingLayoutItem::StructuredBuffer_SRV(5));
    bindingLayoutDesc.addItem(nvrhi::BindingLayoutItem::StructuredBuffer_SRV(6));
    bindingLayoutDesc.addItem(nvrhi::BindingLayoutItem::StructuredBuffer_SRV(7));
    bindingLayoutDesc.addItem(nvrhi::BindingLayoutItem::StructuredBuffer_SRV(8));
    bindingLayoutDesc.addItem(nvrhi::BindingLayoutItem::StructuredBuffer_SRV(9));
    bindingLayoutDesc.addItem(nvrhi::BindingLayoutItem::StructuredBuffer_SRV(10));
    bindingLayoutDesc.addItem(nvrhi::BindingLayoutItem::StructuredBuffer_SRV(11));
    bindingLayoutDesc.addItem(nvrhi::BindingLayoutItem::StructuredBuffer_SRV(12));
    bindingLayoutDesc.addItem(nvrhi::BindingLayoutItem::StructuredBuffer_SRV(13));
    bindingLayoutDesc.addItem(nvrhi::BindingLayoutItem::Texture_SRV(14));
    bindingLayoutDesc.addItem(nvrhi::BindingLayoutItem::Sampler(0));
    m_smokeBindingLayout = device->createBindingLayout(bindingLayoutDesc);

    if (!m_smokeBindingLayout)
    {
        common->Printf("PathTracePrimaryPass: failed to create RT smoke binding layout\n");
        return;
    }

    nvrhi::BindlessLayoutDesc textureBindlessLayoutDesc;
    textureBindlessLayoutDesc
        .setVisibility(nvrhi::ShaderType::AllRayTracing)
        .setFirstSlot(0)
        .setMaxCapacity(RT_SMOKE_TEXTURE_DESCRIPTOR_CAPACITY)
        .addRegisterSpace(nvrhi::BindingLayoutItem::Texture_SRV(1));
    m_smokeTextureBindlessLayout = device->createBindlessLayout(textureBindlessLayoutDesc);
    if (!m_smokeTextureBindlessLayout)
    {
        common->Printf("PathTracePrimaryPass: failed to create RT smoke bindless texture layout\n");
        return;
    }

    m_smokeTextureDescriptorTable = device->createDescriptorTable(m_smokeTextureBindlessLayout);
    if (!m_smokeTextureDescriptorTable)
    {
        common->Printf("PathTracePrimaryPass: failed to create RT smoke texture descriptor table\n");
        return;
    }

    nvrhi::rt::PipelineDesc pipelineDesc;
    pipelineDesc.globalBindingLayouts = { m_smokeBindingLayout, m_smokeTextureBindlessLayout };
    pipelineDesc.shaders = {
        { "", m_smokeShaderLibrary->getShader("RayGen", nvrhi::ShaderType::RayGeneration), nullptr },
        { "", m_smokeShaderLibrary->getShader("Miss", nvrhi::ShaderType::Miss), nullptr }
    };
    pipelineDesc.hitGroups = {
        {
            "HitGroup",
            m_smokeShaderLibrary->getShader("ClosestHit", nvrhi::ShaderType::ClosestHit),
            m_smokeShaderLibrary->getShader("AnyHit", nvrhi::ShaderType::AnyHit),
            nullptr,
            nullptr,
            false
        }
    };
    pipelineDesc.maxPayloadSize = 64;
    pipelineDesc.maxAttributeSize = 8;
    pipelineDesc.maxRecursionDepth = 1;

    m_smokePipeline = device->createRayTracingPipeline(pipelineDesc);
    if (!m_smokePipeline)
    {
        common->Printf("PathTracePrimaryPass: failed to create RT smoke pipeline\n");
        return;
    }

    m_smokeShaderTable = m_smokePipeline->createShaderTable();
    if (!m_smokeShaderTable)
    {
        common->Printf("PathTracePrimaryPass: failed to create RT smoke shader table\n");
        return;
    }

    m_smokeShaderTable->setRayGenerationShader("RayGen");
    m_smokeShaderTable->addMissShader("Miss");
    m_smokeShaderTable->addHitGroup("HitGroup");

    common->Printf("PathTracePrimaryPass: RT smoke pipeline initialized\n");
}

bool PathTracePrimaryPass::ResizeRayTracingSmokeOutput(int width, int height)
{
    if (!m_smokeTestInitialized || !m_smokeShaderTable)
    {
        return false;
    }

    if (m_smokeOutputTexture && m_smokeReadbackTexture && width == m_smokeOutputWidth && height == m_smokeOutputHeight)
    {
        return true;
    }

    nvrhi::IDevice* device = deviceManager ? deviceManager->GetDevice() : nullptr;
    if (!device)
    {
        return false;
    }

    nvrhi::TextureDesc outputDesc;
    outputDesc.width = width;
    outputDesc.height = height;
    outputDesc.mipLevels = 1;
    outputDesc.arraySize = 1;
    outputDesc.format = nvrhi::Format::RGBA32_FLOAT;
    outputDesc.dimension = nvrhi::TextureDimension::Texture2D;
    outputDesc.isUAV = true;
    outputDesc.initialState = nvrhi::ResourceStates::UnorderedAccess;
    outputDesc.keepInitialState = true;
    outputDesc.debugName = "PathTraceSmokeOutput";
    nvrhi::TextureHandle outputTexture = device->createTexture(outputDesc);

    if (!outputTexture)
    {
        common->Printf("PathTracePrimaryPass: failed to create RT smoke output UAV (%dx%d)\n", width, height);
        return false;
    }

    nvrhi::TextureDesc readbackDesc = outputDesc;
    readbackDesc.isShaderResource = false;
    readbackDesc.isUAV = false;
    readbackDesc.initialState = nvrhi::ResourceStates::Unknown;
    readbackDesc.keepInitialState = false;
    readbackDesc.debugName = "PathTraceSmokeReadback";
    nvrhi::StagingTextureHandle readbackTexture = device->createStagingTexture(readbackDesc, nvrhi::CpuAccessMode::Read);

    if (!readbackTexture)
    {
        common->Printf("PathTracePrimaryPass: failed to create RT smoke readback texture (%dx%d)\n", width, height);
        return false;
    }

    m_smokeOutputTexture = outputTexture;
    m_smokeReadbackTexture = readbackTexture;
    m_smokeOutputWidth = width;
    m_smokeOutputHeight = height;
    m_smokeBindingSet = nullptr;
    m_smokeSceneBuilt = false;
    m_smokeTestDispatched = false;
    m_smokeReadbackQueued = false;
    m_smokeReadbackDelayFrames = 0;
    m_smokeReadbackCooldownFrames = 0;
    m_smokeStaticBlasCacheValid = false;
    m_smokeStaticBlas = nullptr;
    m_smokeDynamicBlas = nullptr;
    m_smokeStaticVertexBuffer = nullptr;
    m_smokeStaticIndexBuffer = nullptr;
    m_smokeStaticTriangleClassBuffer = nullptr;
    m_smokeStaticTriangleMaterialBuffer = nullptr;
    m_smokeStaticTriangleMaterialIndexBuffer = nullptr;
    m_smokeDynamicVertexBuffer = nullptr;
    m_smokeDynamicIndexBuffer = nullptr;
    m_smokeDynamicTriangleClassBuffer = nullptr;
    m_smokeDynamicTriangleMaterialBuffer = nullptr;
    m_smokeDynamicTriangleMaterialIndexBuffer = nullptr;
    m_smokeMaterialTableBuffer = nullptr;
    m_smokeActiveTextureTable.clear();
    m_smokeMaterialTableEntryCount = 0;

    common->Printf("PathTracePrimaryPass: RT smoke output UAV initialized (%dx%d)\n", width, height);
    return true;
}

void PathTracePrimaryPass::BuildRayTracingSmokeTestScene(const viewDef_t* viewDef)
{
    const int sceneStartMs = Sys_Milliseconds();
    m_smokeSceneBuilt = false;
    const int requestedDebugMode = idMath::ClampInt(0, 17, r_pathTracingDebugMode.GetInteger());
    const bool enableTextureProbe = requestedDebugMode >= 8 && requestedDebugMode <= 17;

    if (!m_smokeTlas || !m_smokeBindingLayout || !m_smokeTextureBindlessLayout || !m_smokeTextureDescriptorTable || !m_smokeOutputTexture || !m_smokeConstantsBuffer)
    {
        return;
    }

    nvrhi::IDevice* device = deviceManager ? deviceManager->GetDevice() : nullptr;
    if (!device)
    {
        return;
    }

    nvrhi::ICommandList* commandList = m_backend ? m_backend->GL_GetCommandList() : nullptr;
    if (!commandList)
    {
        return;
    }

    std::vector<PathTraceSmokeVertex> dynamicVertexData;
    std::vector<uint32_t> dynamicIndexData;
    std::vector<uint32_t> dynamicTriangleClassData;
    std::vector<uint32_t> dynamicTriangleMaterialData;
    int sourceSurfaces = 0;
    int sourceVerts = 0;
    int sourceIndexes = 0;
    int anchorTriangle = -1;
    RtSmokeSurfaceClassStats classStats;
    RtSmokeSurfaceSkipStats skipStats;
    RtSmokeDynamicGeometryStats dynamicStats;
    RtSmokeAttributeStats attributeStats;
    RtSmokeMaterialStats materialStats;
    RtSmokeBucketRanges bucketRanges;
    const bool dumpClassReasons = r_pathTracingClassDump.GetInteger() != 0;
    RtSmokeSurfaceClassReasonSamples reasonSamples;
    bool staticCacheChanged = false;
    const int captureStartMs = Sys_Milliseconds();
    const bool usingDoomSurfaces = CaptureDoomSurfacesForSmokeTest(viewDef, dynamicVertexData, dynamicIndexData, dynamicTriangleClassData, dynamicTriangleMaterialData, m_smokeStaticSurfaceKeys, m_smokeStaticVertexCache, m_smokeStaticIndexCache, m_smokeStaticTriangleClassCache, m_smokeStaticTriangleMaterialCache, staticCacheChanged, m_smokeSceneOrigin, sourceSurfaces, sourceVerts, sourceIndexes, anchorTriangle, classStats, skipStats, dynamicStats, attributeStats, materialStats, bucketRanges, dumpClassReasons ? &reasonSamples : nullptr);
    const int captureMs = Sys_Milliseconds() - captureStartMs;
    if (!usingDoomSurfaces)
    {
        if (!m_smokeWaitingForDoomSurfaceLogged)
        {
            common->Printf("PathTracePrimaryPass: waiting for center camera ray Doom surface hit to build RT smoke BLAS\n");
            m_smokeWaitingForDoomSurfaceLogged = true;
        }
        return;
    }

    g_smokeMaterialMetadataFrameStats = RtSmokeMaterialMetadataFrameStats();
    const int metadataStartMs = Sys_Milliseconds();
    if (enableTextureProbe)
    {
        for (int surfaceIndex = 0; surfaceIndex < viewDef->numDrawSurfs; ++surfaceIndex)
        {
            const drawSurf_t* drawSurf = viewDef->drawSurfs[surfaceIndex];
            const srfTriangles_t* tri = nullptr;
            if (!ValidateSmokeDrawSurface(viewDef, drawSurf, tri, nullptr))
            {
                continue;
            }

            RegisterSmokeMaterialTextureInfo(drawSurf->material);
        }
    }
    const int metadataMs = Sys_Milliseconds() - metadataStartMs;

    const int materialStartMs = Sys_Milliseconds();
    RtSmokeMaterialTableBuild materialTable;
    uint64 materialTableSignature = 0;
    bool materialTableCacheHit = false;
    BuildSmokeMaterialTableCached(materialTable, m_smokeStaticTriangleMaterialCache, dynamicTriangleMaterialData, m_smokeTextureProbeMaterialId, m_smokeTextureProbeRequestedIndex, enableTextureProbe, materialTableSignature, materialTableCacheHit);
    if (!ValidateSmokeMaterialIndexes(materialTable))
    {
        common->Printf("PathTracePrimaryPass: invalid RT smoke material table, skipping scene build\n");
        return;
    }
    RtSmokeTextureCoverageStats textureCoverageStats;
    const bool needTextureCoverageStats = enableTextureProbe && r_pathTracingSmokeLog.GetInteger() != 0;
    if (needTextureCoverageStats)
    {
        textureCoverageStats = BuildSmokeTextureCoverageStats(
            materialTable,
            m_smokeStaticTriangleClassCache,
            materialTable.staticMaterialIndexes,
            dynamicTriangleClassData,
            materialTable.dynamicMaterialIndexes);
    }
    const int materialMs = Sys_Milliseconds() - materialStartMs;
    static uint32_t lastLoggedTextureProbeMaterialId = 0;
    if (enableTextureProbe && r_pathTracingSmokeLog.GetInteger() != 0 && materialTable.textureProbeBoundMaterialId != lastLoggedTextureProbeMaterialId)
    {
        LogSmokeTextureProbeSwitch(materialTable);
        lastLoggedTextureProbeMaterialId = materialTable.textureProbeBoundMaterialId;
    }
    if (enableTextureProbe && r_pathTracingTextureProbeDump.GetInteger() != 0)
    {
        LogSmokeTextureProbeDump(materialTable);
        r_pathTracingTextureProbeDump.SetInteger(0);
    }
    if (enableTextureProbe && r_pathTracingAlphaDump.GetInteger() != 0)
    {
        LogSmokeAlphaMaterialDump(materialTable);
        r_pathTracingAlphaDump.SetInteger(0);
    }
    if (enableTextureProbe && r_pathTracingTextureFallbackDump.GetInteger() != 0)
    {
        LogSmokeTextureFallbackDump(materialTable);
        r_pathTracingTextureFallbackDump.SetInteger(0);
    }
    if (r_pathTracingTranslucentDump.GetInteger() != 0)
    {
        LogSmokeTranslucentSubtypeDump(materialStats);
        r_pathTracingTranslucentDump.SetInteger(0);
    }
    if (r_pathTracingCrosshairMaterialDump.GetInteger() != 0)
    {
        LogSmokeCrosshairMaterialDump(viewDef, materialTable);
        r_pathTracingCrosshairMaterialDump.SetInteger(0);
    }
    if (r_pathTracingGuiDump.GetInteger() != 0)
    {
        LogSmokeGuiSurfaceDump(viewDef, materialTable);
        r_pathTracingGuiDump.SetInteger(0);
    }
    if (enableTextureProbe && r_pathTracingSmokeLog.GetInteger() != 0)
    {
        LogSmokeTextureActiveWindow(materialTable);
    }

    const int bufferCreateStartMs = Sys_Milliseconds();
    nvrhi::BufferHandle smokeStaticVertexBuffer = CreateSmokeGeometryBuffer(device, "PathTraceSmokeStaticWorldVertices", m_smokeStaticVertexCache.size() * sizeof(m_smokeStaticVertexCache[0]), RT_SMOKE_VERTEX_STRIDE, true, false, true);
    nvrhi::BufferHandle smokeStaticIndexBuffer = CreateSmokeGeometryBuffer(device, "PathTraceSmokeStaticWorldIndices", m_smokeStaticIndexCache.size() * sizeof(m_smokeStaticIndexCache[0]), sizeof(uint32_t), false, true, true);
    nvrhi::BufferHandle smokeStaticTriangleClassBuffer = CreateSmokeGeometryBuffer(device, "PathTraceSmokeStaticWorldTriangleClasses", m_smokeStaticTriangleClassCache.size() * sizeof(m_smokeStaticTriangleClassCache[0]), sizeof(uint32_t), false, false, false);
    nvrhi::BufferHandle smokeStaticTriangleMaterialBuffer = CreateSmokeGeometryBuffer(device, "PathTraceSmokeStaticWorldTriangleMaterials", m_smokeStaticTriangleMaterialCache.size() * sizeof(m_smokeStaticTriangleMaterialCache[0]), sizeof(uint32_t), false, false, false);
    nvrhi::BufferHandle smokeStaticTriangleMaterialIndexBuffer = CreateSmokeGeometryBuffer(device, "PathTraceSmokeStaticWorldTriangleMaterialIndexes", materialTable.staticMaterialIndexes.size() * sizeof(materialTable.staticMaterialIndexes[0]), sizeof(uint32_t), false, false, false);
    nvrhi::BufferHandle smokeDynamicVertexBuffer = CreateSmokeGeometryBuffer(device, "PathTraceSmokeDynamicCandidateVertices", dynamicVertexData.size() * sizeof(dynamicVertexData[0]), RT_SMOKE_VERTEX_STRIDE, true, false, true);
    nvrhi::BufferHandle smokeDynamicIndexBuffer = CreateSmokeGeometryBuffer(device, "PathTraceSmokeDynamicCandidateIndices", dynamicIndexData.size() * sizeof(dynamicIndexData[0]), sizeof(uint32_t), false, true, true);
    nvrhi::BufferHandle smokeDynamicTriangleClassBuffer = CreateSmokeGeometryBuffer(device, "PathTraceSmokeDynamicCandidateTriangleClasses", dynamicTriangleClassData.size() * sizeof(dynamicTriangleClassData[0]), sizeof(uint32_t), false, false, false);
    nvrhi::BufferHandle smokeDynamicTriangleMaterialBuffer = CreateSmokeGeometryBuffer(device, "PathTraceSmokeDynamicCandidateTriangleMaterials", dynamicTriangleMaterialData.size() * sizeof(dynamicTriangleMaterialData[0]), sizeof(uint32_t), false, false, false);
    nvrhi::BufferHandle smokeDynamicTriangleMaterialIndexBuffer = CreateSmokeGeometryBuffer(device, "PathTraceSmokeDynamicCandidateTriangleMaterialIndexes", materialTable.dynamicMaterialIndexes.size() * sizeof(materialTable.dynamicMaterialIndexes[0]), sizeof(uint32_t), false, false, false);
    nvrhi::BufferHandle smokeMaterialTableBuffer = CreateSmokeGeometryBuffer(device, "PathTraceSmokeMaterialTable", materialTable.materials.size() * sizeof(materialTable.materials[0]), sizeof(PathTraceSmokeMaterial), false, false, false);

    if (!smokeStaticVertexBuffer || !smokeStaticIndexBuffer || !smokeStaticTriangleClassBuffer || !smokeStaticTriangleMaterialBuffer || !smokeStaticTriangleMaterialIndexBuffer || !smokeDynamicVertexBuffer || !smokeDynamicIndexBuffer || !smokeDynamicTriangleClassBuffer || !smokeDynamicTriangleMaterialBuffer || !smokeDynamicTriangleMaterialIndexBuffer || !smokeMaterialTableBuffer)
    {
        common->Printf("PathTracePrimaryPass: failed to create RT smoke geometry buffers\n");
        return;
    }
    const int bufferCreateMs = Sys_Milliseconds() - bufferCreateStartMs;

    const int staticVertexCount = static_cast<int>(m_smokeStaticVertexCache.size());
    const int dynamicVertexCount = static_cast<int>(dynamicVertexData.size());
    const int staticIndexCount = bucketRanges.buckets[0].indexCount;
    const int dynamicIndexCount =
        bucketRanges.buckets[1].indexCount +
        bucketRanges.buckets[2].indexCount +
        bucketRanges.buckets[3].indexCount +
        bucketRanges.buckets[4].indexCount;
    const bool hasStaticBlas = staticIndexCount > 0;
    const bool hasDynamicBlas = dynamicIndexCount > 0;
    const RtSmokeStaticBlasSignature staticSignature = ComputeSmokeStaticBlasSignature(m_smokeStaticVertexCache, m_smokeStaticIndexCache, m_smokeStaticTriangleClassCache, m_smokeStaticTriangleMaterialCache, bucketRanges.buckets[0], vec3_origin);
    const bool staticBlasCacheHit = hasStaticBlas && m_smokeStaticBlasCacheValid && m_smokeStaticBlas &&
        m_smokeStaticVertexBuffer && m_smokeStaticIndexBuffer && m_smokeStaticTriangleClassBuffer && m_smokeStaticTriangleMaterialBuffer && m_smokeStaticTriangleMaterialIndexBuffer &&
        !staticCacheChanged && m_smokeStaticBlasSignature == staticSignature.hash;
    if (staticBlasCacheHit)
    {
        smokeStaticVertexBuffer = m_smokeStaticVertexBuffer;
        smokeStaticIndexBuffer = m_smokeStaticIndexBuffer;
        smokeStaticTriangleClassBuffer = m_smokeStaticTriangleClassBuffer;
        smokeStaticTriangleMaterialBuffer = m_smokeStaticTriangleMaterialBuffer;
        smokeStaticTriangleMaterialIndexBuffer = m_smokeStaticTriangleMaterialIndexBuffer;
    }

    if (!hasStaticBlas && !hasDynamicBlas)
    {
        common->Printf("PathTracePrimaryPass: no RT smoke BLAS ranges to build\n");
        return;
    }

    nvrhi::rt::AccelStructDesc smokeStaticBlasDesc;
    nvrhi::rt::AccelStructHandle smokeStaticBlas;
    if (hasStaticBlas)
    {
        if (staticBlasCacheHit)
        {
            smokeStaticBlas = m_smokeStaticBlas;
            smokeStaticBlasDesc = m_smokeStaticBlasDesc;
            ++m_smokeStaticBlasCacheHitCount;
        }
        else
        {
            nvrhi::rt::GeometryTriangles staticTriangleGeometry;
            InitSmokeTriangleGeometry(staticTriangleGeometry, smokeStaticVertexBuffer, smokeStaticIndexBuffer, staticVertexCount, 0, staticIndexCount);

            nvrhi::rt::GeometryDesc staticGeometryDesc;
            staticGeometryDesc.setTriangles(staticTriangleGeometry);

            smokeStaticBlasDesc = nvrhi::rt::AccelStructDesc()
                .addBottomLevelGeometry(staticGeometryDesc)
                .setBuildFlags(nvrhi::rt::AccelStructBuildFlags::PreferFastTrace)
                .setDebugName("PathTraceSmokeStaticWorldBLAS");
            smokeStaticBlas = device->createAccelStruct(smokeStaticBlasDesc);
            if (!smokeStaticBlas)
            {
                common->Printf("PathTracePrimaryPass: failed to create RT smoke static BLAS\n");
                return;
            }
            ++m_smokeStaticBlasCacheMissCount;
        }
    }

    nvrhi::rt::AccelStructDesc smokeDynamicBlasDesc;
    nvrhi::rt::AccelStructHandle smokeDynamicBlas;
    if (hasDynamicBlas)
    {
        nvrhi::rt::GeometryTriangles dynamicTriangleGeometry;
        InitSmokeTriangleGeometry(dynamicTriangleGeometry, smokeDynamicVertexBuffer, smokeDynamicIndexBuffer, dynamicVertexCount, 0, dynamicIndexCount);

        nvrhi::rt::GeometryDesc dynamicGeometryDesc;
        dynamicGeometryDesc.setTriangles(dynamicTriangleGeometry);

        smokeDynamicBlasDesc = nvrhi::rt::AccelStructDesc()
            .addBottomLevelGeometry(dynamicGeometryDesc)
            .setBuildFlags(nvrhi::rt::AccelStructBuildFlags::PreferFastTrace)
            .setDebugName("PathTraceSmokeDynamicCandidateBLAS");
        smokeDynamicBlas = device->createAccelStruct(smokeDynamicBlasDesc);
        if (!smokeDynamicBlas)
        {
            common->Printf("PathTracePrimaryPass: failed to create RT smoke dynamic BLAS\n");
            return;
        }
    }

    if (!staticBlasCacheHit)
    {
        commandList->beginTrackingBufferState(smokeStaticVertexBuffer, nvrhi::ResourceStates::Common);
        commandList->beginTrackingBufferState(smokeStaticIndexBuffer, nvrhi::ResourceStates::Common);
        commandList->beginTrackingBufferState(smokeStaticTriangleClassBuffer, nvrhi::ResourceStates::Common);
        commandList->beginTrackingBufferState(smokeStaticTriangleMaterialBuffer, nvrhi::ResourceStates::Common);
        commandList->beginTrackingBufferState(smokeStaticTriangleMaterialIndexBuffer, nvrhi::ResourceStates::Common);
    }
    commandList->beginTrackingBufferState(smokeDynamicVertexBuffer, nvrhi::ResourceStates::Common);
    commandList->beginTrackingBufferState(smokeDynamicIndexBuffer, nvrhi::ResourceStates::Common);
    commandList->beginTrackingBufferState(smokeDynamicTriangleClassBuffer, nvrhi::ResourceStates::Common);
    commandList->beginTrackingBufferState(smokeDynamicTriangleMaterialBuffer, nvrhi::ResourceStates::Common);
    commandList->beginTrackingBufferState(smokeDynamicTriangleMaterialIndexBuffer, nvrhi::ResourceStates::Common);
    commandList->beginTrackingBufferState(smokeMaterialTableBuffer, nvrhi::ResourceStates::Common);
    const int bufferUploadStartMs = Sys_Milliseconds();
    if (!staticBlasCacheHit && !m_smokeStaticVertexCache.empty())
    {
        commandList->writeBuffer(smokeStaticVertexBuffer, m_smokeStaticVertexCache.data(), m_smokeStaticVertexCache.size() * sizeof(m_smokeStaticVertexCache[0]));
    }
    if (!staticBlasCacheHit && !m_smokeStaticIndexCache.empty())
    {
        commandList->writeBuffer(smokeStaticIndexBuffer, m_smokeStaticIndexCache.data(), m_smokeStaticIndexCache.size() * sizeof(m_smokeStaticIndexCache[0]));
    }
    if (!staticBlasCacheHit && !m_smokeStaticTriangleClassCache.empty())
    {
        commandList->writeBuffer(smokeStaticTriangleClassBuffer, m_smokeStaticTriangleClassCache.data(), m_smokeStaticTriangleClassCache.size() * sizeof(m_smokeStaticTriangleClassCache[0]));
    }
    if (!staticBlasCacheHit && !m_smokeStaticTriangleMaterialCache.empty())
    {
        commandList->writeBuffer(smokeStaticTriangleMaterialBuffer, m_smokeStaticTriangleMaterialCache.data(), m_smokeStaticTriangleMaterialCache.size() * sizeof(m_smokeStaticTriangleMaterialCache[0]));
    }
    if (!staticBlasCacheHit && !materialTable.staticMaterialIndexes.empty())
    {
        commandList->writeBuffer(smokeStaticTriangleMaterialIndexBuffer, materialTable.staticMaterialIndexes.data(), materialTable.staticMaterialIndexes.size() * sizeof(materialTable.staticMaterialIndexes[0]));
    }
    if (!dynamicVertexData.empty())
    {
        commandList->writeBuffer(smokeDynamicVertexBuffer, dynamicVertexData.data(), dynamicVertexData.size() * sizeof(dynamicVertexData[0]));
    }
    if (!dynamicIndexData.empty())
    {
        commandList->writeBuffer(smokeDynamicIndexBuffer, dynamicIndexData.data(), dynamicIndexData.size() * sizeof(dynamicIndexData[0]));
    }
    if (!dynamicTriangleClassData.empty())
    {
        commandList->writeBuffer(smokeDynamicTriangleClassBuffer, dynamicTriangleClassData.data(), dynamicTriangleClassData.size() * sizeof(dynamicTriangleClassData[0]));
    }
    if (!dynamicTriangleMaterialData.empty())
    {
        commandList->writeBuffer(smokeDynamicTriangleMaterialBuffer, dynamicTriangleMaterialData.data(), dynamicTriangleMaterialData.size() * sizeof(dynamicTriangleMaterialData[0]));
    }
    if (!materialTable.dynamicMaterialIndexes.empty())
    {
        commandList->writeBuffer(smokeDynamicTriangleMaterialIndexBuffer, materialTable.dynamicMaterialIndexes.data(), materialTable.dynamicMaterialIndexes.size() * sizeof(materialTable.dynamicMaterialIndexes[0]));
    }
    if (!materialTable.materials.empty())
    {
        commandList->writeBuffer(smokeMaterialTableBuffer, materialTable.materials.data(), materialTable.materials.size() * sizeof(materialTable.materials[0]));
    }
    if (!staticBlasCacheHit)
    {
        commandList->setBufferState(smokeStaticVertexBuffer, nvrhi::ResourceStates::AccelStructBuildInput);
        commandList->setBufferState(smokeStaticIndexBuffer, nvrhi::ResourceStates::AccelStructBuildInput);
        commandList->setBufferState(smokeStaticTriangleClassBuffer, nvrhi::ResourceStates::ShaderResource);
        commandList->setBufferState(smokeStaticTriangleMaterialBuffer, nvrhi::ResourceStates::ShaderResource);
        commandList->setBufferState(smokeStaticTriangleMaterialIndexBuffer, nvrhi::ResourceStates::ShaderResource);
    }
    commandList->setBufferState(smokeDynamicVertexBuffer, nvrhi::ResourceStates::AccelStructBuildInput);
    commandList->setBufferState(smokeDynamicIndexBuffer, nvrhi::ResourceStates::AccelStructBuildInput);
    commandList->setBufferState(smokeDynamicTriangleClassBuffer, nvrhi::ResourceStates::ShaderResource);
    commandList->setBufferState(smokeDynamicTriangleMaterialBuffer, nvrhi::ResourceStates::ShaderResource);
    commandList->setBufferState(smokeDynamicTriangleMaterialIndexBuffer, nvrhi::ResourceStates::ShaderResource);
    commandList->setBufferState(smokeMaterialTableBuffer, nvrhi::ResourceStates::ShaderResource);
    commandList->commitBarriers();
    const int bufferUploadMs = Sys_Milliseconds() - bufferUploadStartMs;

    const int accelSubmitStartMs = Sys_Milliseconds();
    if (hasStaticBlas && !staticBlasCacheHit)
    {
        nvrhi::utils::BuildBottomLevelAccelStruct(commandList, smokeStaticBlas, smokeStaticBlasDesc);
    }

    if (hasDynamicBlas)
    {
        nvrhi::utils::BuildBottomLevelAccelStruct(commandList, smokeDynamicBlas, smokeDynamicBlasDesc);
    }

    nvrhi::rt::InstanceDesc instanceDescs[2];
    int instanceCount = 0;
    if (hasStaticBlas)
    {
        instanceDescs[instanceCount]
            .setInstanceID(0)
            .setInstanceMask(0xff)
            .setInstanceContributionToHitGroupIndex(0)
            .setFlags(nvrhi::rt::InstanceFlags::TriangleCullDisable)
            .setBLAS(smokeStaticBlas);
        ++instanceCount;
    }
    if (hasDynamicBlas)
    {
        instanceDescs[instanceCount]
            .setInstanceID(1)
            .setInstanceMask(0xff)
            .setInstanceContributionToHitGroupIndex(0)
            .setFlags(nvrhi::rt::InstanceFlags::TriangleCullDisable)
            .setBLAS(smokeDynamicBlas);
        ++instanceCount;
    }

    commandList->buildTopLevelAccelStruct(m_smokeTlas, instanceDescs, instanceCount, nvrhi::rt::AccelStructBuildFlags::PreferFastTrace);
    const int accelSubmitMs = Sys_Milliseconds() - accelSubmitStartMs;

    nvrhi::BindingSetDesc bindingSetDesc;
    bindingSetDesc.bindings = {
        nvrhi::BindingSetItem::RayTracingAccelStruct(0, m_smokeTlas),
        nvrhi::BindingSetItem::Texture_UAV(1, m_smokeOutputTexture),
        nvrhi::BindingSetItem::ConstantBuffer(2, m_smokeConstantsBuffer),
        nvrhi::BindingSetItem::StructuredBuffer_SRV(3, smokeStaticVertexBuffer),
        nvrhi::BindingSetItem::StructuredBuffer_SRV(4, smokeStaticIndexBuffer),
        nvrhi::BindingSetItem::StructuredBuffer_SRV(5, smokeStaticTriangleClassBuffer),
        nvrhi::BindingSetItem::StructuredBuffer_SRV(6, smokeDynamicVertexBuffer),
        nvrhi::BindingSetItem::StructuredBuffer_SRV(7, smokeDynamicIndexBuffer),
        nvrhi::BindingSetItem::StructuredBuffer_SRV(8, smokeDynamicTriangleClassBuffer),
        nvrhi::BindingSetItem::StructuredBuffer_SRV(9, smokeStaticTriangleMaterialBuffer),
        nvrhi::BindingSetItem::StructuredBuffer_SRV(10, smokeDynamicTriangleMaterialBuffer),
        nvrhi::BindingSetItem::StructuredBuffer_SRV(11, smokeStaticTriangleMaterialIndexBuffer),
        nvrhi::BindingSetItem::StructuredBuffer_SRV(12, smokeDynamicTriangleMaterialIndexBuffer),
        nvrhi::BindingSetItem::StructuredBuffer_SRV(13, smokeMaterialTableBuffer)
    };
    const nvrhi::TextureHandle fallbackTexture = globalImages && globalImages->whiteImage ? globalImages->whiteImage->GetTextureHandle() : nullptr;
    if (!fallbackTexture)
    {
        common->Printf("PathTracePrimaryPass: failed to find RT smoke fallback material texture\n");
        return;
    }

    nvrhi::DescriptorTableHandle smokeTextureDescriptorTable = m_smokeTextureDescriptorTable;
    std::vector<nvrhi::TextureHandle> smokeActiveTextureTable;
    smokeActiveTextureTable.push_back(fallbackTexture);
    if (enableTextureProbe)
    {
        const bool forceFallbackTexture = r_pathTracingTextureForceFallback.GetInteger() != 0;
        if (!smokeTextureDescriptorTable)
        {
            smokeTextureDescriptorTable = device->createDescriptorTable(m_smokeTextureBindlessLayout);
        }
        if (!smokeTextureDescriptorTable)
        {
            common->Printf("PathTracePrimaryPass: failed to create RT smoke texture descriptor table\n");
            return;
        }

        const int textureSlotCount = idMath::ClampInt(1, RT_SMOKE_TEXTURE_EXPERIMENTAL_ACTIVE_CAP, Max(static_cast<int>(materialTable.diffuseTextures.size()), 1));
        smokeActiveTextureTable.reserve(textureSlotCount + 1);
        for (int textureSlot = 0; textureSlot < textureSlotCount; ++textureSlot)
        {
            nvrhi::TextureHandle texture = fallbackTexture;
            if (!forceFallbackTexture && textureSlot >= 0 && textureSlot < static_cast<int>(materialTable.diffuseTextures.size()))
            {
                const nvrhi::TextureHandle candidateTexture = materialTable.diffuseTextures[textureSlot];
                if (candidateTexture)
                {
                    texture = candidateTexture;
                }
            }
            if (!IsSmokeTextureHandleSafeForDescriptor(texture))
            {
                ++materialTable.descriptorsReplacedWithFallback;
                texture = fallbackTexture;
            }

            smokeActiveTextureTable.push_back(texture);
        }

        for (int textureSlot = 0; textureSlot < textureSlotCount; ++textureSlot)
        {
            nvrhi::TextureHandle texture = smokeActiveTextureTable[textureSlot + 1];
            if (!device->writeDescriptorTable(smokeTextureDescriptorTable, nvrhi::BindingSetItem::Texture_SRV(textureSlot, texture)))
            {
                common->Printf("PathTracePrimaryPass: failed to write RT smoke bindless texture descriptor slot %d\n", textureSlot);
                return;
            }
        }
    }

    bindingSetDesc.addItem(nvrhi::BindingSetItem::Texture_SRV(14, fallbackTexture));
    bindingSetDesc.addItem(nvrhi::BindingSetItem::Sampler(0, m_backend->GetCommonPasses().m_AnisotropicWrapSampler));
    nvrhi::BindingSetHandle smokeBindingSet = device->createBindingSet(bindingSetDesc, m_smokeBindingLayout);
    if (!smokeBindingSet)
    {
        common->Printf("PathTracePrimaryPass: failed to create RT smoke binding set\n");
        return;
    }

    m_smokeStaticVertexBuffer = smokeStaticVertexBuffer;
    m_smokeStaticIndexBuffer = smokeStaticIndexBuffer;
    m_smokeStaticTriangleClassBuffer = smokeStaticTriangleClassBuffer;
    m_smokeStaticTriangleMaterialBuffer = smokeStaticTriangleMaterialBuffer;
    m_smokeStaticTriangleMaterialIndexBuffer = smokeStaticTriangleMaterialIndexBuffer;
    m_smokeDynamicVertexBuffer = smokeDynamicVertexBuffer;
    m_smokeDynamicIndexBuffer = smokeDynamicIndexBuffer;
    m_smokeDynamicTriangleClassBuffer = smokeDynamicTriangleClassBuffer;
    m_smokeDynamicTriangleMaterialBuffer = smokeDynamicTriangleMaterialBuffer;
    m_smokeDynamicTriangleMaterialIndexBuffer = smokeDynamicTriangleMaterialIndexBuffer;
    m_smokeMaterialTableBuffer = smokeMaterialTableBuffer;
    m_smokeStaticBlasDesc = smokeStaticBlasDesc;
    m_smokeDynamicBlasDesc = smokeDynamicBlasDesc;
    m_smokeStaticBlas = smokeStaticBlas;
    m_smokeDynamicBlas = smokeDynamicBlas;
    if (hasStaticBlas)
    {
        m_smokeStaticBlasCacheValid = true;
        m_smokeStaticBlasSignature = staticSignature.hash;
    }
    m_smokeBindingSet = smokeBindingSet;
    m_smokeTextureDescriptorTable = smokeTextureDescriptorTable;
    m_smokeActiveTextureTable = smokeActiveTextureTable;
    m_smokeMaterialTableEntryCount = static_cast<int>(materialTable.materials.size());
    m_smokeSceneBuilt = true;

    const int sceneMs = Sys_Milliseconds() - sceneStartMs;
    if (ShouldLogSmokeTiming(sceneMs, Sys_Milliseconds(), g_smokeLastSceneTimingLogMs))
    {
        common->Printf("PathTracePrimaryPass: RT smoke slow scene build %d ms (capture=%d metadata=%d material=%d bufferCreate=%d bufferSubmit=%d accelSubmit=%d) surfaces=%d verts=%d indexes=%d dynamicIndexes=%d skinnedRtCpu=%d(%di) staticCacheHit=%d materialCacheHit=%d materialCache=%d/%d metadataCache=%d metadataFrame=%d/%d/%d/%d metadataRegistry=%d guiTextures=%d/%d/%d additiveDecals=%d lightCount=%d debugMode=%d\n",
            sceneMs,
            captureMs,
            metadataMs,
            materialMs,
            bufferCreateMs,
            bufferUploadMs,
            accelSubmitMs,
            sourceSurfaces,
            sourceVerts,
            sourceIndexes,
            dynamicIndexCount,
            dynamicStats.skinnedRtCpuSkinnedSurfaces,
            dynamicStats.skinnedRtCpuSkinnedIndexes,
            staticBlasCacheHit ? 1 : 0,
            materialTableCacheHit ? 1 : 0,
            g_smokeMaterialTableCache.hits,
            g_smokeMaterialTableCache.misses,
            r_pathTracingMaterialMetadataCache.GetInteger() != 0 ? 1 : 0,
            g_smokeMaterialMetadataFrameStats.cacheRefreshes,
            g_smokeMaterialMetadataFrameStats.fullDiscovers,
            g_smokeMaterialMetadataFrameStats.newEntries,
            g_smokeMaterialMetadataFrameStats.registrations,
            static_cast<int>(g_smokeMaterialTextureRegistry.size()),
            materialTable.guiTextureCandidates,
            materialTable.guiTexturesAccepted,
            materialTable.guiTexturesRejected,
            materialTable.materialsAdditiveDecals,
            r_pathTracingLightCount.GetInteger(),
            requestedDebugMode);
    }

    if (r_pathTracingSmokeLog.GetInteger() != 0 && !m_smokeSceneRebuildLogged)
    {
        common->Printf("PathTracePrimaryPass: rebuilding RT smoke BLAS/TLAS every frame from current visible Doom surfaces (first frame: %d surfaces, %d verts, %d indexes, anchor triangle %d)\n",
            sourceSurfaces, sourceVerts, sourceIndexes, anchorTriangle);
        common->Printf("PathTracePrimaryPass: RT smoke capture buckets static-world=%d rigid-entity=%d skinned=%d particle/alpha=%d unknown=%d\n",
            classStats.staticWorldSurfaces,
            classStats.rigidEntitySurfaces,
            classStats.skinnedDeformedSurfaces,
            classStats.particleAlphaSurfaces,
            classStats.unknownSurfaces);
        common->Printf("PathTracePrimaryPass: RT smoke BLAS split static-world=%d indexes, dynamic-candidate=%d indexes, TLAS instances=%d\n",
            staticIndexCount, dynamicIndexCount, instanceCount);
        common->Printf("PathTracePrimaryPass: RT smoke dynamic geometry rigid=%d(%di) skinnedCpu=%d(%di) skinnedRtCpu=%d(%di) skinnedLikelyBasePose=%d(%di) particle/alpha=%d(%di) unknown=%d(%di)\n",
            dynamicStats.rigidSurfaces, dynamicStats.rigidIndexes,
            dynamicStats.skinnedCpuCurrentSurfaces, dynamicStats.skinnedCpuCurrentIndexes,
            dynamicStats.skinnedRtCpuSkinnedSurfaces, dynamicStats.skinnedRtCpuSkinnedIndexes,
            dynamicStats.skinnedLikelyBasePoseSurfaces, dynamicStats.skinnedLikelyBasePoseIndexes,
            dynamicStats.particleAlphaSurfaces, dynamicStats.particleAlphaIndexes,
            dynamicStats.unknownSurfaces, dynamicStats.unknownIndexes);
        common->Printf("PathTracePrimaryPass: RT smoke static BLAS cache %s signature=%llu cacheSurfaces=%d hits=%d misses=%d\n",
            staticBlasCacheHit ? "hit" : "rebuild",
            static_cast<unsigned long long>(staticSignature.hash),
            static_cast<int>(m_smokeStaticSurfaceKeys.size()),
            m_smokeStaticBlasCacheHitCount,
            m_smokeStaticBlasCacheMissCount);
        common->Printf("PathTracePrimaryPass: RT smoke material table cache %s signature=%llu hits=%d misses=%d materials=%d textures=%d\n",
            materialTableCacheHit ? "hit" : "rebuild",
            static_cast<unsigned long long>(materialTableSignature),
            g_smokeMaterialTableCache.hits,
            g_smokeMaterialTableCache.misses,
            static_cast<int>(materialTable.materials.size()),
            static_cast<int>(materialTable.diffuseTextures.size()));
        LogSmokeMaterialStats(materialStats);
        LogSmokeMaterialTable(materialTable);
        if (enableTextureProbe)
        {
            LogSmokeTextureProbe(materialTable);
            LogSmokeTextureCoverage(textureCoverageStats);
            LogSmokeMaterialTextureDiscovery(materialTable);
        }
        LogSmokeAttributeStats(attributeStats);
        LogSmokeBucketRanges(bucketRanges);
        m_smokeSceneRebuildLogged = true;
    }

    if (dumpClassReasons)
    {
        common->Printf("PathTracePrimaryPass: RT smoke one-shot surface classification sample dump\n");
        LogSmokeSurfaceClassReasonSamples(reasonSamples);
        r_pathTracingClassDump.SetInteger(0);
    }

    if (r_pathTracingSmokeLog.GetInteger() != 0 && m_smokeSceneLogCooldownFrames <= 0)
    {
        common->Printf("PathTracePrimaryPass: RT smoke scene capture %d surfaces (dynamic cap %d), %d/%d verts, %d/%d indexes, anchor triangle %d; skipped null=%d geo=%d material=%d gui=%d allowGuiSurfaces=%d space=%d model=%d callback=%d skipCallbacks=%d indexes=%d cache=%d limits=%d zeroArea=%d emptyClass=%d; buckets static-world=%d(%dv/%di/%dt) rigid-entity=%d(%dv/%di/%dt) skinned=%d(%dv/%di/%dt) particle/alpha=%d(%dv/%di/%dt) unknown=%d(%dv/%di/%dt)\n",
            sourceSurfaces, RT_SMOKE_MAX_SURFACES,
            sourceVerts, RT_SMOKE_MAX_VERTS,
            sourceIndexes, RT_SMOKE_MAX_INDEXES,
            anchorTriangle,
            skipStats.nullSurface,
            skipStats.missingGeometry,
            skipStats.nullMaterial,
            skipStats.guiSurface,
            r_pathTracingAllowGuiSurfaces.GetInteger() != 0 ? 1 : 0,
            skipStats.nullSpace,
            skipStats.nullModel,
            skipStats.callbackEntity,
            r_pathTracingSkipCallbackEntities.GetInteger() != 0 ? 1 : 0,
            skipStats.invalidIndexCount,
            skipStats.nonCurrentCache,
            skipStats.limitExceeded,
            skipStats.zeroAreaOnly,
            skipStats.emptyClassBuffer,
            classStats.staticWorldSurfaces, classStats.staticWorldVerts, classStats.staticWorldIndexes, classStats.staticWorldTriangles,
            classStats.rigidEntitySurfaces, classStats.rigidEntityVerts, classStats.rigidEntityIndexes, classStats.rigidEntityTriangles,
            classStats.skinnedDeformedSurfaces, classStats.skinnedDeformedVerts, classStats.skinnedDeformedIndexes, classStats.skinnedDeformedTriangles,
            classStats.particleAlphaSurfaces, classStats.particleAlphaVerts, classStats.particleAlphaIndexes, classStats.particleAlphaTriangles,
            classStats.unknownSurfaces, classStats.unknownVerts, classStats.unknownIndexes, classStats.unknownTriangles);
        common->Printf("PathTracePrimaryPass: RT smoke BLAS split static-world=%d indexes, dynamic-candidate=%d indexes, TLAS instances=%d\n",
            staticIndexCount, dynamicIndexCount, instanceCount);
        common->Printf("PathTracePrimaryPass: RT smoke dynamic geometry rigid=%d(%di) skinnedCpu=%d(%di) skinnedRtCpu=%d(%di) skinnedLikelyBasePose=%d(%di) particle/alpha=%d(%di) unknown=%d(%di)\n",
            dynamicStats.rigidSurfaces, dynamicStats.rigidIndexes,
            dynamicStats.skinnedCpuCurrentSurfaces, dynamicStats.skinnedCpuCurrentIndexes,
            dynamicStats.skinnedRtCpuSkinnedSurfaces, dynamicStats.skinnedRtCpuSkinnedIndexes,
            dynamicStats.skinnedLikelyBasePoseSurfaces, dynamicStats.skinnedLikelyBasePoseIndexes,
            dynamicStats.particleAlphaSurfaces, dynamicStats.particleAlphaIndexes,
            dynamicStats.unknownSurfaces, dynamicStats.unknownIndexes);
        common->Printf("PathTracePrimaryPass: RT smoke static BLAS cache %s signature=%llu cacheSurfaces=%d hits=%d misses=%d\n",
            staticBlasCacheHit ? "hit" : "rebuild",
            static_cast<unsigned long long>(staticSignature.hash),
            static_cast<int>(m_smokeStaticSurfaceKeys.size()),
            m_smokeStaticBlasCacheHitCount,
            m_smokeStaticBlasCacheMissCount);
        common->Printf("PathTracePrimaryPass: RT smoke material table cache %s signature=%llu hits=%d misses=%d materials=%d textures=%d\n",
            materialTableCacheHit ? "hit" : "rebuild",
            static_cast<unsigned long long>(materialTableSignature),
            g_smokeMaterialTableCache.hits,
            g_smokeMaterialTableCache.misses,
            static_cast<int>(materialTable.materials.size()),
            static_cast<int>(materialTable.diffuseTextures.size()));
        LogSmokeMaterialStats(materialStats);
        LogSmokeMaterialTable(materialTable);
        if (enableTextureProbe)
        {
            LogSmokeTextureProbe(materialTable);
            LogSmokeTextureCoverage(textureCoverageStats);
            LogSmokeMaterialTextureDiscovery(materialTable);
        }
        LogSmokeAttributeStats(attributeStats);
        LogSmokeBucketRanges(bucketRanges);
        m_smokeSceneLogCooldownFrames = RT_SMOKE_SCENE_LOG_INTERVAL_FRAMES;
    }
    else
    {
        --m_smokeSceneLogCooldownFrames;
    }
}

void PathTracePrimaryPass::ExecuteRayTracingSmokeTest(const viewDef_t* viewDef)
{
    const int executeStartMs = Sys_Milliseconds();
    if (!viewDef || !m_smokeSceneBuilt || !m_smokeShaderTable || !m_smokeBindingSet || !m_smokeTextureDescriptorTable || !m_smokeOutputTexture || !m_smokeReadbackTexture || !m_smokeConstantsBuffer ||
        !m_smokeStaticVertexBuffer || !m_smokeStaticIndexBuffer || !m_smokeStaticTriangleClassBuffer || !m_smokeStaticTriangleMaterialBuffer || !m_smokeStaticTriangleMaterialIndexBuffer ||
        !m_smokeDynamicVertexBuffer || !m_smokeDynamicIndexBuffer || !m_smokeDynamicTriangleClassBuffer || !m_smokeDynamicTriangleMaterialBuffer || !m_smokeDynamicTriangleMaterialIndexBuffer || !m_smokeMaterialTableBuffer)
    {
        return;
    }

    nvrhi::ICommandList* commandList = m_backend ? m_backend->GL_GetCommandList() : nullptr;
    if (!commandList)
    {
        return;
    }

    nvrhi::rt::State state;
    state.shaderTable = m_smokeShaderTable;
    state.bindings = { m_smokeBindingSet, m_smokeTextureDescriptorTable };
    int debugMode = idMath::ClampInt(0, 17, r_pathTracingDebugMode.GetInteger());
    if ((debugMode == 8 || debugMode == 9 || debugMode == 10 || debugMode == 11 || debugMode == 12 || debugMode == 13 || debugMode == 14 || debugMode == 15) && r_pathTracingTextureTableLimit.GetInteger() <= 0)
    {
        debugMode = 7;
    }

    idVec3 cameraOrigin = viewDef->renderView.vieworg;
    idVec3 cameraForward = viewDef->renderView.viewaxis[0];
    idVec3 cameraLeft = viewDef->renderView.viewaxis[1];
    idVec3 cameraUp = viewDef->renderView.viewaxis[2];
    cameraForward.Normalize();
    cameraLeft.Normalize();
    cameraUp.Normalize();

    PathTraceSmokeConstants constants = {};
    constants.cameraOriginAndTMax[0] = cameraOrigin.x;
    constants.cameraOriginAndTMax[1] = cameraOrigin.y;
    constants.cameraOriginAndTMax[2] = cameraOrigin.z;
    constants.cameraOriginAndTMax[3] = 100000.0f;
    constants.cameraForwardAndTanX[0] = cameraForward.x;
    constants.cameraForwardAndTanX[1] = cameraForward.y;
    constants.cameraForwardAndTanX[2] = cameraForward.z;
    constants.cameraForwardAndTanX[3] = idMath::Tan(DEG2RAD(viewDef->renderView.fov_x * 0.5f));
    constants.cameraLeftAndTanY[0] = cameraLeft.x;
    constants.cameraLeftAndTanY[1] = cameraLeft.y;
    constants.cameraLeftAndTanY[2] = cameraLeft.z;
    constants.cameraLeftAndTanY[3] = idMath::Tan(DEG2RAD(viewDef->renderView.fov_y * 0.5f));
    constants.cameraUpAndDebugMode[0] = cameraUp.x;
    constants.cameraUpAndDebugMode[1] = cameraUp.y;
    constants.cameraUpAndDebugMode[2] = cameraUp.z;
    constants.cameraUpAndDebugMode[3] = static_cast<float>(debugMode);
    constants.textureInfo[0] = static_cast<float>(Max(0, static_cast<int>(m_smokeActiveTextureTable.size()) - 1));
    const int textureSampleMethod = r_pathTracingTextureSampleEnable.GetInteger() != 0
        ? idMath::ClampInt(0, 2, r_pathTracingTextureSampleMethod.GetInteger())
        : 0;
    constants.textureInfo[1] = static_cast<float>(textureSampleMethod);
    constants.textureInfo[2] = static_cast<float>(Max(0, m_smokeMaterialTableEntryCount));
    const uint32_t textureFlags =
        (r_pathTracingTextureBindlessEnable.GetInteger() != 0 ? 1u : 0u) |
        (r_pathTracingTextureFilter.GetInteger() != 0 ? 2u : 0u) |
        (r_pathTracingTextureDecode.GetInteger() != 0 ? 4u : 0u) |
        (r_pathTracingUseNormalMaps.GetInteger() != 0 && debugMode == 14 ? 8u : 0u) |
        (r_pathTracingUseSpecularMaps.GetInteger() != 0 && debugMode == 14 ? 16u : 0u) |
        (r_pathTracingUseEmissiveMaps.GetInteger() != 0 && debugMode == 14 ? 32u : 0u);
    constants.textureInfo[3] = static_cast<float>(textureFlags);
    RtSmokeSelectedLight selectedLights[RT_SMOKE_MAX_DEBUG_LIGHTS];
    const int requestedLightCount = idMath::ClampInt(0, RT_SMOKE_MAX_DEBUG_LIGHTS, r_pathTracingLightCount.GetInteger());
    const int lightSelectionMode = idMath::ClampInt(0, 1, r_pathTracingLightSelection.GetInteger());
    const int selectedLightCount = (debugMode == 14 || debugMode == 15)
        ? CollectSelectedSmokePointLights(viewDef, cameraOrigin, selectedLights, requestedLightCount, lightSelectionMode)
        : 0;
    constants.lightInfo[0] = static_cast<float>(selectedLightCount);
    constants.lightInfo[1] = static_cast<float>(lightSelectionMode);
    constants.lightInfo[2] = idMath::ClampFloat(0.0f, 1.0f, r_pathTracingSmokeParticleAlphaScale.GetFloat());
    constants.lightInfo[3] =
        (r_pathTracingSmokeParticleDither.GetInteger() != 0 ? 1.0f : 0.0f) +
        (r_pathTracingSmokeParticleEdgeFade.GetInteger() != 0 ? 2.0f : 0.0f);
    constants.portalWindowInfo[0] = r_pathTracingPortalWindowStochastic.GetInteger() != 0 ? 1.0f : 0.0f;
    constants.portalWindowInfo[1] = idMath::ClampFloat(0.0f, 1.0f, r_pathTracingPortalWindowAlphaScale.GetFloat());
    constants.portalWindowInfo[2] = idMath::ClampFloat(0.0f, 1.0f, r_pathTracingPortalWindowMinOpacity.GetFloat());
    constants.portalWindowInfo[3] = idMath::ClampFloat(0.0f, 1.0f, r_pathTracingPortalWindowShadowOpacity.GetFloat());
    constants.lightSpriteInfo[0] = r_pathTracingLightSpriteProxies.GetInteger() != 0 ? 1.0f : 0.0f;
    constants.lightSpriteInfo[1] = idMath::ClampFloat(0.001f, 0.25f, r_pathTracingLightSpriteRadiusScale.GetFloat());
    constants.lightSpriteInfo[2] = idMath::ClampFloat(0.0f, 16.0f, r_pathTracingLightSpriteIntensity.GetFloat());
    constants.lightSpriteInfo[3] = 0.0f;
    for (int i = 0; i < selectedLightCount; i++)
    {
        constants.lightOriginAndRadius[i][0] = selectedLights[i].origin.x;
        constants.lightOriginAndRadius[i][1] = selectedLights[i].origin.y;
        constants.lightOriginAndRadius[i][2] = selectedLights[i].origin.z;
        constants.lightOriginAndRadius[i][3] = selectedLights[i].radius;
        constants.lightColorAndIntensity[i][0] = selectedLights[i].color.x;
        constants.lightColorAndIntensity[i][1] = selectedLights[i].color.y;
        constants.lightColorAndIntensity[i][2] = selectedLights[i].color.z;
        constants.lightColorAndIntensity[i][3] = selectedLights[i].spriteProxy ? 1.0f : 0.0f;
    }
    if ((debugMode == 14 || debugMode == 15) && r_pathTracingLightDump.GetInteger() != 0)
    {
        common->Printf("PathTracePrimaryPass: RT smoke selected %d debug point lights selection=%s\n",
            selectedLightCount,
            lightSelectionMode == 0 ? "nearest" : "cameraInfluence");
        for (int i = 0; i < selectedLightCount; i++)
        {
            common->Printf("  light[%d]: index=%d origin=(%.2f %.2f %.2f) radius=%.2f distance=%.2f score=%.6f color=(%.3f %.3f %.3f) intensity=%.3f sprite=%d shader='%s'\n",
                i,
                selectedLights[i].index,
                selectedLights[i].origin.x,
                selectedLights[i].origin.y,
                selectedLights[i].origin.z,
                selectedLights[i].radius,
                idMath::Sqrt(selectedLights[i].distanceSquared),
                selectedLights[i].score,
                selectedLights[i].color.x,
                selectedLights[i].color.y,
                selectedLights[i].color.z,
                selectedLights[i].color.w,
                selectedLights[i].spriteProxy ? 1 : 0,
                selectedLights[i].shaderName.c_str());
        }
        r_pathTracingLightDump.SetInteger(0);
    }

    commandList->writeBuffer(m_smokeConstantsBuffer, &constants, sizeof(constants));
    commandList->setBufferState(m_smokeStaticVertexBuffer, nvrhi::ResourceStates::ShaderResource);
    commandList->setBufferState(m_smokeStaticIndexBuffer, nvrhi::ResourceStates::ShaderResource);
    commandList->setBufferState(m_smokeStaticTriangleClassBuffer, nvrhi::ResourceStates::ShaderResource);
    commandList->setBufferState(m_smokeStaticTriangleMaterialBuffer, nvrhi::ResourceStates::ShaderResource);
    commandList->setBufferState(m_smokeStaticTriangleMaterialIndexBuffer, nvrhi::ResourceStates::ShaderResource);
    commandList->setBufferState(m_smokeDynamicVertexBuffer, nvrhi::ResourceStates::ShaderResource);
    commandList->setBufferState(m_smokeDynamicIndexBuffer, nvrhi::ResourceStates::ShaderResource);
    commandList->setBufferState(m_smokeDynamicTriangleClassBuffer, nvrhi::ResourceStates::ShaderResource);
    commandList->setBufferState(m_smokeDynamicTriangleMaterialBuffer, nvrhi::ResourceStates::ShaderResource);
    commandList->setBufferState(m_smokeDynamicTriangleMaterialIndexBuffer, nvrhi::ResourceStates::ShaderResource);
    commandList->setBufferState(m_smokeMaterialTableBuffer, nvrhi::ResourceStates::ShaderResource);
    for (nvrhi::TextureHandle texture : m_smokeActiveTextureTable)
    {
        if (texture)
        {
            commandList->setTextureState(texture, nvrhi::AllSubresources, nvrhi::ResourceStates::ShaderResource);
        }
    }
    commandList->setTextureState(m_smokeOutputTexture, nvrhi::AllSubresources, nvrhi::ResourceStates::UnorderedAccess);
    commandList->commitBarriers();
    commandList->clearTextureFloat(m_smokeOutputTexture, nvrhi::AllSubresources, nvrhi::Color(0.25f, 0.50f, 0.75f, 1.0f));
    commandList->setRayTracingState(state);

    nvrhi::rt::DispatchRaysArguments args;
    args.width = m_smokeOutputWidth;
    args.height = m_smokeOutputHeight;
    args.depth = 1;
    commandList->dispatchRays(args);
    const int dispatchSubmitMs = Sys_Milliseconds() - executeStartMs;
    if (ShouldLogSmokeTiming(dispatchSubmitMs, Sys_Milliseconds(), g_smokeLastDispatchTimingLogMs))
    {
        common->Printf("PathTracePrimaryPass: RT smoke slow dispatch submit %d ms output=%dx%d debugMode=%d lightCount=%d\n",
            dispatchSubmitMs,
            m_smokeOutputWidth,
            m_smokeOutputHeight,
            debugMode,
            requestedLightCount);
    }

    if (r_pathTracingReadbackEnable.GetInteger() != 0 && !m_smokeReadbackQueued && m_smokeReadbackCooldownFrames <= 0)
    {
        commandList->setTextureState(m_smokeOutputTexture, nvrhi::AllSubresources, nvrhi::ResourceStates::CopySource);
        commandList->commitBarriers();
        commandList->copyTexture(m_smokeReadbackTexture, nvrhi::TextureSlice(), m_smokeOutputTexture, nvrhi::TextureSlice());
        m_smokeReadbackQueued = true;
        m_smokeReadbackDelayFrames = 2;
        if (r_pathTracingSmokeLog.GetInteger() != 0)
        {
            common->Printf("PathTracePrimaryPass: queued RT smoke UAV readback\n");
        }
    }

    if (!m_smokeTestDispatched)
    {
        common->Printf("PathTracePrimaryPass: dispatched RT smoke camera raygen (%dx%d, debugMode=%d)\n", m_smokeOutputWidth, m_smokeOutputHeight, debugMode);
    }
    m_smokeTestDispatched = true;
}

void PathTracePrimaryPass::ReadBackRayTracingSmokeTest()
{
    if (r_pathTracingReadbackEnable.GetInteger() == 0)
    {
        m_smokeReadbackQueued = false;
        m_smokeReadbackDelayFrames = 0;
        m_smokeReadbackCooldownFrames = RT_SMOKE_READBACK_INTERVAL_FRAMES;
        return;
    }

    if (!m_smokeReadbackQueued || !m_smokeReadbackTexture)
    {
        if (m_smokeReadbackCooldownFrames > 0)
        {
            --m_smokeReadbackCooldownFrames;
        }
        return;
    }

    if (m_smokeReadbackDelayFrames > 0)
    {
        --m_smokeReadbackDelayFrames;
        return;
    }

    nvrhi::IDevice* device = deviceManager ? deviceManager->GetDevice() : nullptr;
    if (!device)
    {
        return;
    }

    const int readbackStartMs = Sys_Milliseconds();
    device->waitForIdle();
    const int waitForIdleMs = Sys_Milliseconds() - readbackStartMs;

    size_t rowPitch = 0;
    void* readbackData = device->mapStagingTexture(m_smokeReadbackTexture, nvrhi::TextureSlice(), nvrhi::CpuAccessMode::Read, &rowPitch);
    if (!readbackData)
    {
        common->Printf("PathTracePrimaryPass: RT smoke UAV readback map failed\n");
        m_smokeReadbackQueued = false;
        m_smokeReadbackCooldownFrames = RT_SMOKE_READBACK_INTERVAL_FRAMES;
        return;
    }

    const int sampleX = m_smokeOutputWidth / 2;
    const int sampleY = m_smokeOutputHeight / 2;
    const byte* readbackBytes = static_cast<const byte*>(readbackData);
    const float* centerRgba = reinterpret_cast<const float*>(readbackBytes + rowPitch * sampleY + sizeof(float) * 4 * sampleX);

    int greenHits = 0;
    int redMisses = 0;
    for (int y = 0; y < m_smokeOutputHeight; ++y)
    {
        const float* row = reinterpret_cast<const float*>(readbackBytes + rowPitch * y);
        for (int x = 0; x < m_smokeOutputWidth; ++x)
        {
            const float* rgba = row + x * 4;
            if (rgba[1] > 0.5f)
            {
                ++greenHits;
            }
            else if (rgba[0] > 0.5f)
            {
                ++redMisses;
            }
        }
    }

    const int readbackMs = Sys_Milliseconds() - readbackStartMs;
    if (r_pathTracingSmokeLog.GetInteger() != 0 || ShouldLogSmokeTiming(readbackMs, Sys_Milliseconds(), g_smokeLastReadbackTimingLogMs))
    {
        common->Printf("PathTracePrimaryPass: RT smoke UAV readback %dx%d center rgba=(%.3f, %.3f, %.3f, %.3f), hits=%d, misses=%d, rowPitch=%u, total=%d ms, waitForIdle=%d ms\n",
            m_smokeOutputWidth, m_smokeOutputHeight,
            centerRgba[0], centerRgba[1], centerRgba[2], centerRgba[3],
            greenHits, redMisses, static_cast<unsigned int>(rowPitch),
            readbackMs, waitForIdleMs);
    }

    device->unmapStagingTexture(m_smokeReadbackTexture);
    m_smokeReadbackLogged = true;
    m_smokeReadbackQueued = false;
    m_smokeReadbackCooldownFrames = RT_SMOKE_READBACK_INTERVAL_FRAMES;
}

void PathTracePrimaryPass::PresentDebugOutput()
{
    if (!deviceManager)
    {
        return;
    }

    nvrhi::IFramebuffer* targetFramebuffer = deviceManager->GetCurrentFramebuffer();
    if (!targetFramebuffer)
    {
        return;
    }

    BlitDebugOutput(targetFramebuffer, nvrhi::Viewport(renderSystem->GetNativeWidth(), renderSystem->GetNativeHeight()));
}

void PathTracePrimaryPass::BlitDebugOutput(nvrhi::IFramebuffer* targetFramebuffer, const nvrhi::Viewport& targetViewport)
{
    if (!m_smokeTestDispatched || !m_smokeOutputTexture || !m_backend || !targetFramebuffer)
    {
        return;
    }

    nvrhi::ICommandList* commandList = m_backend->GL_GetCommandList();
    if (!commandList)
    {
        return;
    }

    commandList->setTextureState(m_smokeOutputTexture, nvrhi::AllSubresources, nvrhi::ResourceStates::ShaderResource);
    commandList->commitBarriers();

    BlitParameters blitParms;
    blitParms.sourceTexture = m_smokeOutputTexture;
    blitParms.targetFramebuffer = targetFramebuffer;
    blitParms.targetViewport = targetViewport;
    blitParms.sampler = BlitSampler::Point;
    m_backend->GetCommonPasses().BlitTexture(commandList, blitParms, nullptr);
}
