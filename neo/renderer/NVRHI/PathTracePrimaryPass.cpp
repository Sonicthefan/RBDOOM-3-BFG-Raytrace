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
#include <vector>

extern DeviceManager* deviceManager;

idCVar r_pathTracingDebugMode(
    "r_pathTracingDebugMode",
    "0",
    CVAR_RENDERER | CVAR_INTEGER | CVAR_ARCHIVE,
    "RT smoke debug output mode: 0 = hit/miss, 1 = depth, 2 = interpolated normal, 3 = surface class, 4 = UV, 5 = geometric normal, 6 = material ID, 7 = material table, 8 = sampled diffuse texture" );

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
    CVAR_RENDERER | CVAR_INTEGER | CVAR_ARCHIVE,
    "RT smoke debug mode 8 material table index to bind as the single sampled diffuse texture; -1 = first safe texture" );

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

idCVar r_pathTracingSmokeLog(
    "r_pathTracingSmokeLog",
    "1",
    CVAR_RENDERER | CVAR_INTEGER | CVAR_ARCHIVE,
    "Enable periodic RT smoke debug logging" );

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
const int RT_SMOKE_CLASS_REASON_SAMPLES = 8;
const int RT_SMOKE_MATERIAL_REASON_SAMPLES = 12;
const int RT_SMOKE_TEXTURE_PROBE_CANDIDATE_SAMPLES = 24;
const int RT_SMOKE_TEXTURE_DESCRIPTOR_CAPACITY = 1;
const int RT_SMOKE_VERTEX_STRIDE = sizeof(PathTraceSmokeVertex);
const uint32_t RT_SMOKE_TRIANGLE_CLASS_MASK = 0x0000ffffu;
const uint32_t RT_SMOKE_TRIANGLE_FORCE_GEOMETRIC_NORMAL = 0x00010000u;

struct PathTraceSmokeConstants
{
    float cameraOriginAndTMax[4];
    float cameraForwardAndTanX[4];
    float cameraLeftAndTanY[4];
    float cameraUpAndDebugMode[4];
};

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

struct RtSmokeMaterialStats
{
    int totalSurfaces = 0;
    int totalTriangles = 0;
    int uniqueMaterials = 0;
    std::vector<uint32_t> materialIds;
    RtSmokeMaterialSample samples[RT_SMOKE_MATERIAL_REASON_SAMPLES];
    int sampleCount = 0;
};

struct PathTraceSmokeMaterial
{
    float debugAlbedo[4];
    uint32_t diffuseTextureIndex = UINT32_MAX;
    uint32_t pad[3] = {};
};

struct RtSmokeMaterialTableBuild
{
    std::vector<uint32_t> materialIds;
    std::vector<PathTraceSmokeMaterial> materials;
    std::vector<uint32_t> staticMaterialIndexes;
    std::vector<uint32_t> dynamicMaterialIndexes;
    std::vector<nvrhi::TextureHandle> diffuseTextures;
    int materialsWithTextures = 0;
    int materialsMissingTextures = 0;
    int materialsRejectedTextures = 0;
    int materialsOverTextureSlotLimit = 0;
    int textureProbeRequestedIndex = -1;
    int textureProbeBoundIndex = -1;
    uint32_t textureProbeBoundMaterialId = 0;
    bool textureProbeUsedLatch = false;
};

struct RtSmokeMaterialTextureInfo
{
    uint32_t materialId = 0;
    idStr materialName;
    idStr diffuseImageName;
    idStr fallbackReason;
    idImage* diffuseImage = nullptr;
    bool hasDiffuseImage = false;
    bool hasTextureHandle = false;
    bool hasSafeTexture = false;
    int tableIndex = -1;
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
    table.materialIds.push_back(materialId);
    table.materials.push_back(material);
    return static_cast<uint32_t>(table.materials.size() - 1);
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

    reason = "no fast-path or diffuse-stage image";
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

void RegisterSmokeMaterialTextureInfo(const idMaterial* material)
{
    const char* materialName = material ? material->GetName() : "<none>";
    const uint32_t materialId = HashSmokeMaterialName(materialName);

    RtSmokeMaterialTextureInfo* info = nullptr;
    for (RtSmokeMaterialTextureInfo& existing : g_smokeMaterialTextureRegistry)
    {
        if (existing.materialId == materialId)
        {
            info = &existing;
            break;
        }
    }

    if (!info)
    {
        RtSmokeMaterialTextureInfo newInfo;
        newInfo.materialId = materialId;
        newInfo.materialName = materialName;
        g_smokeMaterialTextureRegistry.push_back(newInfo);
        info = &g_smokeMaterialTextureRegistry.back();
    }

    idStr reason;
    idImage* diffuseImage = FindSmokeDiffuseImage(material, reason);
    info->materialName = materialName;
    info->fallbackReason = reason;
    info->diffuseImage = diffuseImage;
    info->hasDiffuseImage = diffuseImage != nullptr;
    info->hasTextureHandle = diffuseImage && diffuseImage->GetTextureHandle();
    info->hasSafeTexture = info->hasTextureHandle && IsSmokeDiffuseTextureSafeForRayTracing(diffuseImage->GetTextureHandle());
    info->diffuseImageName = diffuseImage ? diffuseImage->GetName() : "<none>";
}

RtSmokeMaterialTextureInfo ResolveSmokeMaterialTextureInfo(uint32_t materialId, int tableIndex)
{
    for (const RtSmokeMaterialTextureInfo& existing : g_smokeMaterialTextureRegistry)
    {
        if (existing.materialId == materialId)
        {
            RtSmokeMaterialTextureInfo resolved = existing;
            resolved.tableIndex = tableIndex;
            return resolved;
        }
    }

    RtSmokeMaterialTextureInfo missing;
    missing.materialId = materialId;
    missing.tableIndex = tableIndex;
    missing.materialName = "<unseen material>";
    missing.diffuseImageName = "<none>";
    missing.fallbackReason = "material metadata not seen this session";
    return missing;
}

void PopulateSmokeMaterialTextureSlots(RtSmokeMaterialTableBuild& table, uint32_t& latchedMaterialId, int& latchedRequestedIndex)
{
    table.diffuseTextures.clear();
    table.materialsWithTextures = 0;
    table.materialsMissingTextures = 0;
    table.materialsRejectedTextures = 0;
    table.materialsOverTextureSlotLimit = 0;
    table.textureProbeRequestedIndex = r_pathTracingTextureProbeIndex.GetInteger();
    table.textureProbeBoundIndex = -1;
    table.textureProbeBoundMaterialId = 0;
    table.textureProbeUsedLatch = false;

    if (r_pathTracingTextureProbeReset.GetInteger() != 0 || latchedRequestedIndex != table.textureProbeRequestedIndex)
    {
        latchedMaterialId = 0;
        latchedRequestedIndex = table.textureProbeRequestedIndex;
        r_pathTracingTextureProbeReset.SetInteger(0);
    }

    std::vector<int> safeMaterialIndexes;
    safeMaterialIndexes.reserve(table.materialIds.size());

    for (int tableIndex = 0; tableIndex < static_cast<int>(table.materialIds.size()); ++tableIndex)
    {
        PathTraceSmokeMaterial& material = table.materials[tableIndex];
        material.diffuseTextureIndex = UINT32_MAX;

        const RtSmokeMaterialTextureInfo info = ResolveSmokeMaterialTextureInfo(table.materialIds[tableIndex], tableIndex);
        if (!info.diffuseImage || !info.hasTextureHandle)
        {
            ++table.materialsMissingTextures;
            continue;
        }
        if (!info.hasSafeTexture)
        {
            ++table.materialsRejectedTextures;
            continue;
        }

        safeMaterialIndexes.push_back(tableIndex);
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
    else if (!safeMaterialIndexes.empty())
    {
        selectedMaterialIndex = safeMaterialIndexes.front();
    }

    table.materialsOverTextureSlotLimit = static_cast<int>(safeMaterialIndexes.size()) > RT_SMOKE_TEXTURE_DESCRIPTOR_CAPACITY
        ? static_cast<int>(safeMaterialIndexes.size()) - RT_SMOKE_TEXTURE_DESCRIPTOR_CAPACITY
        : 0;
    if (selectedMaterialIndex < 0)
    {
        return;
    }

    const RtSmokeMaterialTextureInfo selectedInfo = ResolveSmokeMaterialTextureInfo(table.materialIds[selectedMaterialIndex], selectedMaterialIndex);
    if (!selectedInfo.diffuseImage || !selectedInfo.hasSafeTexture)
    {
        return;
    }

    table.diffuseTextures.push_back(selectedInfo.diffuseImage->GetTextureHandle());
    table.materials[selectedMaterialIndex].diffuseTextureIndex = 0;
    table.materialsWithTextures = 1;
    table.textureProbeBoundIndex = selectedMaterialIndex;
    table.textureProbeBoundMaterialId = table.materialIds[selectedMaterialIndex];
    if (latchedMaterialId != table.textureProbeBoundMaterialId)
    {
        latchedMaterialId = table.textureProbeBoundMaterialId;
    }
}

void BuildSmokeMaterialTable(RtSmokeMaterialTableBuild& table, const std::vector<uint32_t>& staticMaterialIds, const std::vector<uint32_t>& dynamicMaterialIds, uint32_t& latchedTextureProbeMaterialId, int& latchedTextureProbeRequestedIndex)
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

    PopulateSmokeMaterialTextureSlots(table, latchedTextureProbeMaterialId, latchedTextureProbeRequestedIndex);
}

void AddSmokeMaterialStats(RtSmokeMaterialStats& stats, const idMaterial* material, int indexes)
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
    if (viewDef && space != &viewDef->worldSpace && (!renderEntity || !renderEntity->hModel))
    {
        if (skipStats)
        {
            ++skipStats->nullModel;
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
        if (material->Coverage() == MC_TRANSLUCENT ||
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
    common->Printf("PathTracePrimaryPass: RT smoke materials unique=%d surfaces=%d triangles=%d samples=",
        stats.uniqueMaterials,
        stats.totalSurfaces,
        stats.totalTriangles);

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
}

void LogSmokeMaterialTable(const RtSmokeMaterialTableBuild& table)
{
    common->Printf("PathTracePrimaryPass: RT smoke material table entries=%d staticTriangles=%d dynamicTriangles=%d textureSlots=%d textured=%d missingTextures=%d rejectedTextures=%d overTextureSlotLimit=%d probeRequest=%d probeBound=%d probeMaterial=%u latch=%d samples=",
        static_cast<int>(table.materials.size()),
        static_cast<int>(table.staticMaterialIndexes.size()),
        static_cast<int>(table.dynamicMaterialIndexes.size()),
        static_cast<int>(table.diffuseTextures.size()),
        table.materialsWithTextures,
        table.materialsMissingTextures,
        table.materialsRejectedTextures,
        table.materialsOverTextureSlotLimit,
        table.textureProbeRequestedIndex,
        table.textureProbeBoundIndex,
        table.textureProbeBoundMaterialId,
        table.textureProbeUsedLatch ? 1 : 0);

    const int sampleCount = idMath::ClampInt(0, RT_SMOKE_MATERIAL_REASON_SAMPLES, static_cast<int>(table.materialIds.size()));
    for (int sampleIndex = 0; sampleIndex < sampleCount; ++sampleIndex)
    {
        const PathTraceSmokeMaterial& material = table.materials[sampleIndex];
        common->Printf("%sindex=%d id=%u color=(%.2f %.2f %.2f) tex=%d",
            sampleIndex == 0 ? "" : ", ",
            sampleIndex,
            table.materialIds[sampleIndex],
            material.debugAlbedo[0],
            material.debugAlbedo[1],
            material.debugAlbedo[2],
            material.diffuseTextureIndex == UINT32_MAX ? -1 : static_cast<int>(material.diffuseTextureIndex));
    }

    common->Printf("\n");
}

void LogSmokeTextureProbe(const RtSmokeMaterialTableBuild& table)
{
    if (table.textureProbeBoundIndex < 0 || table.textureProbeBoundIndex >= static_cast<int>(table.materialIds.size()))
    {
        common->Printf("PathTracePrimaryPass: RT smoke texture probe no bound texture request=%d tableEntries=%d\n",
            table.textureProbeRequestedIndex,
            static_cast<int>(table.materialIds.size()));
        return;
    }

    const RtSmokeMaterialTextureInfo info = ResolveSmokeMaterialTextureInfo(table.materialIds[table.textureProbeBoundIndex], table.textureProbeBoundIndex);
    nvrhi::TextureHandle texture = info.diffuseImage ? info.diffuseImage->GetTextureHandle() : nullptr;
    if (!texture)
    {
        common->Printf("PathTracePrimaryPass: RT smoke texture probe missing bound texture index=%d material='%s'\n",
            table.textureProbeBoundIndex,
            info.materialName.c_str());
        return;
    }

    const nvrhi::TextureDesc& desc = texture->getDesc();
    common->Printf("PathTracePrimaryPass: RT smoke texture probe slot=0 tableIndex=%d material='%s' diffuse='%s' format=%d size=%ux%u mips=%u reason='%s'\n",
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
    if (table.textureProbeBoundIndex < 0 || table.textureProbeBoundIndex >= static_cast<int>(table.materialIds.size()))
    {
        common->Printf("PathTracePrimaryPass: RT smoke texture probe unbound request=%d latchedMaterial=%u visible=0\n",
            table.textureProbeRequestedIndex,
            table.textureProbeBoundMaterialId);
        return;
    }

    const RtSmokeMaterialTextureInfo info = ResolveSmokeMaterialTextureInfo(table.materialIds[table.textureProbeBoundIndex], table.textureProbeBoundIndex);
    common->Printf("PathTracePrimaryPass: RT smoke texture probe latched slot=0 tableIndex=%d materialId=%u material='%s' diffuse='%s' latch=%d\n",
        table.textureProbeBoundIndex,
        table.textureProbeBoundMaterialId,
        info.materialName.c_str(),
        info.diffuseImageName.c_str(),
        table.textureProbeUsedLatch ? 1 : 0);
}

void LogSmokeTextureProbeDump(const RtSmokeMaterialTableBuild& table)
{
    common->Printf("PathTracePrimaryPass: RT smoke texture probe dump request=%d bound=%d materialId=%u slots=%d\n",
        table.textureProbeRequestedIndex,
        table.textureProbeBoundIndex,
        table.textureProbeBoundMaterialId,
        static_cast<int>(table.diffuseTextures.size()));

    if (table.textureProbeBoundIndex >= 0 && table.textureProbeBoundIndex < static_cast<int>(table.materialIds.size()))
    {
        const RtSmokeMaterialTextureInfo info = ResolveSmokeMaterialTextureInfo(table.materialIds[table.textureProbeBoundIndex], table.textureProbeBoundIndex);
        common->Printf("PathTracePrimaryPass: RT smoke texture probe current index=%d material='%s' diffuse='%s' safe=%d reason='%s'\n",
            table.textureProbeBoundIndex,
            info.materialName.c_str(),
            info.diffuseImageName.c_str(),
            info.hasSafeTexture ? 1 : 0,
            info.fallbackReason.c_str());
    }
    else
    {
        common->Printf("PathTracePrimaryPass: RT smoke texture probe current none\n");
    }

    common->Printf("PathTracePrimaryPass: RT smoke texture probe safe candidates=");
    int logged = 0;
    for (int tableIndex = 0; tableIndex < static_cast<int>(table.materialIds.size()) && logged < RT_SMOKE_TEXTURE_PROBE_CANDIDATE_SAMPLES; ++tableIndex)
    {
        const RtSmokeMaterialTextureInfo info = ResolveSmokeMaterialTextureInfo(table.materialIds[tableIndex], tableIndex);
        if (!info.hasSafeTexture)
        {
            continue;
        }

        common->Printf("%s%d:'%s' -> '%s'",
            logged == 0 ? "" : ", ",
            tableIndex,
            info.materialName.c_str(),
            info.diffuseImageName.c_str());
        ++logged;
    }
    if (logged == 0)
    {
        common->Printf("<none>");
    }
    common->Printf("\n");
}

void LogSmokeMaterialTextureDiscovery(const RtSmokeMaterialTableBuild& table)
{
    int diffuseImages = 0;
    int textureHandles = 0;
    int safeTextures = 0;
    int missingTextureHandles = 0;
    for (int tableIndex = 0; tableIndex < static_cast<int>(table.materialIds.size()); ++tableIndex)
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
        static_cast<int>(table.materialIds.size()),
        diffuseImages,
        textureHandles,
        safeTextures,
        missingTextureHandles);

    const int sampleCount = idMath::ClampInt(0, RT_SMOKE_MATERIAL_REASON_SAMPLES, static_cast<int>(table.materialIds.size()));
    for (int sampleIndex = 0; sampleIndex < sampleCount; ++sampleIndex)
    {
        const RtSmokeMaterialTextureInfo info = ResolveSmokeMaterialTextureInfo(table.materialIds[sampleIndex], sampleIndex);
        common->Printf("%sindex=%d material='%s' diffuse='%s' image=%d handle=%d safe=%d reason='%s'",
            sampleIndex == 0 ? "" : ", ",
            info.tableIndex,
            info.materialName.c_str(),
            info.diffuseImageName.c_str(),
            info.hasDiffuseImage ? 1 : 0,
            info.hasTextureHandle ? 1 : 0,
            info.hasSafeTexture ? 1 : 0,
            info.fallbackReason.c_str());
    }

    common->Printf("\n");

    if (missingTextureHandles > 0)
    {
        common->Printf("PathTracePrimaryPass: RT smoke material missing texture handles samples=");
        int loggedMissing = 0;
        for (int tableIndex = 0; tableIndex < static_cast<int>(table.materialIds.size()) && loggedMissing < RT_SMOKE_MATERIAL_REASON_SAMPLES; ++tableIndex)
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

        const uint32_t surfaceClassId = SmokeSurfaceClassId(surfaceClass);
        const uint32_t materialId = SmokeMaterialId(drawSurf->material);
        RegisterSmokeMaterialTextureInfo(drawSurf->material);
        const uint64 staticSurfaceKey = BuildSmokeStaticSurfaceKey(drawSurf, tri);
        const bool staticSurfaceCached = std::find(staticSurfaceKeys.begin(), staticSurfaceKeys.end(), staticSurfaceKey) != staticSurfaceKeys.end();
        ++sourceSurfaces;
        ++bucketRanges.buckets[0].surfaceCount;
        AddSmokeMaterialStats(materialStats, drawSurf->material, tri->numIndexes);

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
        const uint32_t surfaceClassId = SmokeSurfaceClassId(surfaceClass);
        const uint32_t materialId = SmokeMaterialId(drawSurf->material);
        RegisterSmokeMaterialTextureInfo(drawSurf->material);
        const int bucketIndex = idMath::ClampInt(0, RT_SMOKE_CLASS_COUNT - 1, static_cast<int>(surfaceClassId));
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

        AddSmokeMaterialStats(materialStats, drawSurf->material, emittedIndexes);
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
        .addRegisterSpace(nvrhi::BindingLayoutItem::Texture_SRV(0));
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
            nullptr,
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

    common->Printf("PathTracePrimaryPass: RT smoke output UAV initialized (%dx%d)\n", width, height);
    return true;
}

void PathTracePrimaryPass::BuildRayTracingSmokeTestScene(const viewDef_t* viewDef)
{
    m_smokeSceneBuilt = false;

    if (!m_smokeTlas || !m_smokeBindingLayout || !m_smokeTextureDescriptorTable || !m_smokeOutputTexture || !m_smokeConstantsBuffer)
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
    const bool usingDoomSurfaces = CaptureDoomSurfacesForSmokeTest(viewDef, dynamicVertexData, dynamicIndexData, dynamicTriangleClassData, dynamicTriangleMaterialData, m_smokeStaticSurfaceKeys, m_smokeStaticVertexCache, m_smokeStaticIndexCache, m_smokeStaticTriangleClassCache, m_smokeStaticTriangleMaterialCache, staticCacheChanged, m_smokeSceneOrigin, sourceSurfaces, sourceVerts, sourceIndexes, anchorTriangle, classStats, skipStats, dynamicStats, attributeStats, materialStats, bucketRanges, dumpClassReasons ? &reasonSamples : nullptr);
    if (!usingDoomSurfaces)
    {
        if (!m_smokeWaitingForDoomSurfaceLogged)
        {
            common->Printf("PathTracePrimaryPass: waiting for center camera ray Doom surface hit to build RT smoke BLAS\n");
            m_smokeWaitingForDoomSurfaceLogged = true;
        }
        return;
    }

    RtSmokeMaterialTableBuild materialTable;
    BuildSmokeMaterialTable(materialTable, m_smokeStaticTriangleMaterialCache, dynamicTriangleMaterialData, m_smokeTextureProbeMaterialId, m_smokeTextureProbeRequestedIndex);
    static uint32_t lastLoggedTextureProbeMaterialId = 0;
    if (r_pathTracingSmokeLog.GetInteger() != 0 && materialTable.textureProbeBoundMaterialId != lastLoggedTextureProbeMaterialId)
    {
        LogSmokeTextureProbeSwitch(materialTable);
        lastLoggedTextureProbeMaterialId = materialTable.textureProbeBoundMaterialId;
    }
    if (r_pathTracingTextureProbeDump.GetInteger() != 0)
    {
        LogSmokeTextureProbeDump(materialTable);
        r_pathTracingTextureProbeDump.SetInteger(0);
    }

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
            staticGeometryDesc.setFlags(nvrhi::rt::GeometryFlags::Opaque);

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
        dynamicGeometryDesc.setFlags(nvrhi::rt::GeometryFlags::Opaque);

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

    const nvrhi::TextureHandle probeTexture =
        !materialTable.diffuseTextures.empty() && materialTable.diffuseTextures[0]
        ? materialTable.diffuseTextures[0]
        : fallbackTexture;
    if (!device->writeDescriptorTable(m_smokeTextureDescriptorTable, nvrhi::BindingSetItem::Texture_SRV(0, probeTexture)))
    {
        common->Printf("PathTracePrimaryPass: failed to write RT smoke bindless texture descriptor\n");
        return;
    }

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
    m_smokeSceneBuilt = true;

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
        LogSmokeMaterialStats(materialStats);
        LogSmokeMaterialTable(materialTable);
        LogSmokeTextureProbe(materialTable);
        LogSmokeMaterialTextureDiscovery(materialTable);
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
        common->Printf("PathTracePrimaryPass: RT smoke scene capture %d surfaces (dynamic cap %d), %d/%d verts, %d/%d indexes, anchor triangle %d; skipped null=%d geo=%d material=%d space=%d model=%d indexes=%d cache=%d limits=%d zeroArea=%d emptyClass=%d; buckets static-world=%d(%dv/%di/%dt) rigid-entity=%d(%dv/%di/%dt) skinned=%d(%dv/%di/%dt) particle/alpha=%d(%dv/%di/%dt) unknown=%d(%dv/%di/%dt)\n",
            sourceSurfaces, RT_SMOKE_MAX_SURFACES,
            sourceVerts, RT_SMOKE_MAX_VERTS,
            sourceIndexes, RT_SMOKE_MAX_INDEXES,
            anchorTriangle,
            skipStats.nullSurface,
            skipStats.missingGeometry,
            skipStats.nullMaterial,
            skipStats.nullSpace,
            skipStats.nullModel,
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
        LogSmokeMaterialStats(materialStats);
        LogSmokeMaterialTable(materialTable);
        LogSmokeTextureProbe(materialTable);
        LogSmokeMaterialTextureDiscovery(materialTable);
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
    const int debugMode = idMath::ClampInt(0, 8, r_pathTracingDebugMode.GetInteger());

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
    commandList->setTextureState(m_smokeOutputTexture, nvrhi::AllSubresources, nvrhi::ResourceStates::UnorderedAccess);
    commandList->commitBarriers();
    commandList->clearTextureFloat(m_smokeOutputTexture, nvrhi::AllSubresources, nvrhi::Color(0.25f, 0.50f, 0.75f, 1.0f));
    commandList->setRayTracingState(state);

    nvrhi::rt::DispatchRaysArguments args;
    args.width = m_smokeOutputWidth;
    args.height = m_smokeOutputHeight;
    args.depth = 1;
    commandList->dispatchRays(args);

    if (!m_smokeReadbackQueued && m_smokeReadbackCooldownFrames <= 0)
    {
        commandList->setTextureState(m_smokeOutputTexture, nvrhi::AllSubresources, nvrhi::ResourceStates::CopySource);
        commandList->commitBarriers();
        commandList->copyTexture(m_smokeReadbackTexture, nvrhi::TextureSlice(), m_smokeOutputTexture, nvrhi::TextureSlice());
        m_smokeReadbackQueued = true;
        m_smokeReadbackDelayFrames = 2;
        common->Printf("PathTracePrimaryPass: queued RT smoke UAV readback\n");
    }

    if (!m_smokeTestDispatched)
    {
        common->Printf("PathTracePrimaryPass: dispatched RT smoke camera raygen (%dx%d, debugMode=%d)\n", m_smokeOutputWidth, m_smokeOutputHeight, debugMode);
    }
    m_smokeTestDispatched = true;
}

void PathTracePrimaryPass::ReadBackRayTracingSmokeTest()
{
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

    device->waitForIdle();

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

    common->Printf("PathTracePrimaryPass: RT smoke UAV readback %dx%d center rgba=(%.3f, %.3f, %.3f, %.3f), hits=%d, misses=%d, rowPitch=%u\n",
        m_smokeOutputWidth, m_smokeOutputHeight,
        centerRgba[0], centerRgba[1], centerRgba[2], centerRgba[3],
        greenHits, redMisses, static_cast<unsigned int>(rowPitch));

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
