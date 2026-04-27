#include "precompiled.h"
#pragma hdrstop

#include "PathTracePrimaryPass.h"
#include "../RenderCommon.h"
#include "../RenderBackend.h"
#include "../GLMatrix.h"
#include "../Passes/CommonPasses.h"
#include "../../framework/Common_local.h"
#include "../../sys/DeviceManager.h"

#include <nvrhi/utils.h>
#include <algorithm>
#include <vector>

extern DeviceManager* deviceManager;

idCVar r_pathTracingDebugMode(
    "r_pathTracingDebugMode",
    "0",
    CVAR_RENDERER | CVAR_INTEGER | CVAR_ARCHIVE,
    "RT smoke debug output mode: 0 = hit/miss, 1 = depth, 2 = geometric normal, 3 = surface class" );

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
const int RT_SMOKE_CLASS_REASON_SAMPLES = 2;

struct PathTraceSmokeConstants
{
    float cameraOriginAndTMax[4];
    float cameraForwardAndTanX[4];
    float cameraLeftAndTanY[4];
    float cameraUpAndDebugMode[4];
};

struct PathTraceSmokeInstanceRange
{
    uint32_t indexOffset = 0;
    uint32_t triangleOffset = 0;
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
};

struct RtSmokeSurfaceClassReasonSamples
{
    RtSmokeSurfaceClassReason samples[RT_SMOKE_CLASS_COUNT][RT_SMOKE_CLASS_REASON_SAMPLES];
    int counts[RT_SMOKE_CLASS_COUNT] = {};
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

bool TransformSurfacePointToWorld(const drawSurf_t* drawSurf, const idVec3& localPoint, idVec3& worldPoint)
{
    if (drawSurf->space)
    {
        R_LocalPointToGlobal(drawSurf->space->modelMatrix, localPoint, worldPoint);
        return true;
    }

    worldPoint = localPoint;
    return true;
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

    if ((drawSurf && drawSurf->jointCache != 0) ||
        (tri && tri->staticModelWithJoints != nullptr) ||
        (renderEntity && renderEntity->joints != nullptr && renderEntity->numJoints > 0))
    {
        return RtSmokeSurfaceClass::SkinnedDeformed;
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
        const dynamicModel_t dynamicModel = model ? model->IsDynamicModel() : DM_STATIC;
        if (entityDef->dynamicModel ||
            entityDef->cachedDynamicModel ||
            dynamicModel != DM_STATIC ||
            (renderEntity && (renderEntity->callback || renderEntity->forceUpdate != 0 || renderEntity->weaponDepthHack || renderEntity->modelDepthHack != 0.0f)))
        {
            return RtSmokeSurfaceClass::RigidEntity;
        }

        return RtSmokeSurfaceClass::RigidEntity;
    }

    return RtSmokeSurfaceClass::Unknown;
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

    return reason;
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

            common->Printf("PathTracePrimaryPass: RT smoke class sample %s surf=%d material='%s' coverage=%s sort=%.1f deform=%s entity=%d model='%s' modelDynamic=%s joints(cache=%d staticModel=%d entity=%d) cache(staticV=%d staticI=%d) worldSpace=%d staticWorldModel=%d entityDef=%d dynModel=%d cachedDyn=%d callback=%d forceUpdate=%d weaponDepthHack=%d modelDepthHack=%.3f verts=%d indexes=%d\n",
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
    triangleGeometry.vertexStride = sizeof(float) * 3;
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

RtSmokeStaticBlasSignature ComputeSmokeStaticBlasSignature(const std::vector<float>& vertexData, const std::vector<uint32_t>& indexData, const RtSmokeBucketRange& staticRange, const idVec3& sceneOrigin)
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
        const size_t vertexFloatOffset = static_cast<size_t>(staticRange.vertexOffset) * 3;
        const size_t vertexFloatCount = static_cast<size_t>(staticRange.vertexCount) * 3;
        hash = HashSmokeBytes(hash, vertexData.data() + vertexFloatOffset, vertexFloatCount * sizeof(vertexData[0]));
    }

    if (staticRange.indexCount > 0)
    {
        hash = HashSmokeBytes(hash, indexData.data() + staticRange.indexOffset, static_cast<size_t>(staticRange.indexCount) * sizeof(indexData[0]));
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
            TransformSurfacePointToWorld(drawSurf, tri->verts[i0].xyz, p0);
            TransformSurfacePointToWorld(drawSurf, tri->verts[i1].xyz, p1);
            TransformSurfacePointToWorld(drawSurf, tri->verts[i2].xyz, p2);
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

bool CaptureDoomSurfacesForSmokeTest(const viewDef_t* viewDef, std::vector<float>& vertexData, std::vector<uint32_t>& indexData, std::vector<uint32_t>& triangleClassData, std::vector<uint64>& staticSurfaceKeys, std::vector<float>& staticVertexCache, std::vector<uint32_t>& staticIndexCache, std::vector<uint32_t>& staticTriangleClassCache, bool& staticCacheChanged, idVec3& sceneOrigin, int& sourceSurfaces, int& sourceVerts, int& sourceIndexes, int& anchorTriangle, RtSmokeSurfaceClassStats& classStats, RtSmokeSurfaceSkipStats& skipStats, RtSmokeBucketRanges& bucketRanges, RtSmokeSurfaceClassReasonSamples* reasonSamples)
{
    sourceSurfaces = 0;
    sourceVerts = 0;
    sourceIndexes = 0;
    staticCacheChanged = false;
    int anchorSurface = -1;
    anchorTriangle = -1;
    classStats = RtSmokeSurfaceClassStats();
    skipStats = RtSmokeSurfaceSkipStats();
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
    vertexData.reserve(RT_SMOKE_MAX_VERTS * 3);
    indexData.reserve(RT_SMOKE_MAX_INDEXES);
    triangleClassData.reserve(RT_SMOKE_MAX_INDEXES / 3);

    std::vector<float> bucketVertexData[RT_SMOKE_CLASS_COUNT];
    std::vector<uint32_t> bucketIndexData[RT_SMOKE_CLASS_COUNT];
    std::vector<uint32_t> bucketTriangleClassData[RT_SMOKE_CLASS_COUNT];
    for (int bucketIndex = 0; bucketIndex < RT_SMOKE_CLASS_COUNT; ++bucketIndex)
    {
        bucketVertexData[bucketIndex].reserve(RT_SMOKE_MAX_VERTS * 3 / RT_SMOKE_CLASS_COUNT);
        bucketIndexData[bucketIndex].reserve(RT_SMOKE_MAX_INDEXES / RT_SMOKE_CLASS_COUNT);
        bucketTriangleClassData[bucketIndex].reserve(RT_SMOKE_MAX_INDEXES / (3 * RT_SMOKE_CLASS_COUNT));
    }

    int dynamicVerts = 0;
    int dynamicIndexes = 0;

    for (int surfaceOffset = 0; surfaceOffset < viewDef->numDrawSurfs; ++surfaceOffset)
    {
        const int surfaceIndex = (anchorSurface + surfaceOffset) % viewDef->numDrawSurfs;
        const drawSurf_t* drawSurf = viewDef->drawSurfs[surfaceIndex];
        const srfTriangles_t* tri = nullptr;
        if (!ValidateSmokeDrawSurface(viewDef, drawSurf, tri, &skipStats))
        {
            continue;
        }

        if (sourceSurfaces >= RT_SMOKE_MAX_SURFACES ||
            sourceVerts + tri->numVerts > RT_SMOKE_MAX_VERTS ||
            sourceIndexes + tri->numIndexes > RT_SMOKE_MAX_INDEXES)
        {
            ++skipStats.limitExceeded;
            break;
        }

        const RtSmokeSurfaceClass surfaceClass = ClassifySmokeSurface(viewDef, drawSurf, tri);
        const uint32_t surfaceClassId = SmokeSurfaceClassId(surfaceClass);
        const int bucketIndex = idMath::ClampInt(0, RT_SMOKE_CLASS_COUNT - 1, static_cast<int>(surfaceClassId));
        const bool isStaticWorld = surfaceClass == RtSmokeSurfaceClass::StaticWorld;
        const uint64 staticSurfaceKey = isStaticWorld ? BuildSmokeStaticSurfaceKey(drawSurf, tri) : 0;
        const bool staticSurfaceCached = isStaticWorld && std::find(staticSurfaceKeys.begin(), staticSurfaceKeys.end(), staticSurfaceKey) != staticSurfaceKeys.end();
        if (isStaticWorld && staticSurfaceCached)
        {
            ++sourceSurfaces;
            sourceVerts += tri->numVerts;
            sourceIndexes += tri->numIndexes;
            AddSmokeSurfaceClassStats(classStats, surfaceClass, tri->numVerts, tri->numIndexes);
            ++bucketRanges.buckets[bucketIndex].surfaceCount;
            if (reasonSamples)
            {
                AddSmokeSurfaceClassReasonSample(*reasonSamples, BuildSmokeSurfaceClassReason(viewDef, drawSurf, tri, surfaceIndex, surfaceClass));
            }
            continue;
        }

        if (isStaticWorld)
        {
            const int cachedVerts = static_cast<int>(staticVertexCache.size() / 3);
            const int cachedIndexes = static_cast<int>(staticIndexCache.size());
            if (cachedVerts + tri->numVerts > RT_SMOKE_MAX_VERTS ||
                cachedIndexes + tri->numIndexes + dynamicIndexes > RT_SMOKE_MAX_INDEXES)
            {
                ++skipStats.limitExceeded;
                break;
            }
        }
        else
        {
            const int cachedVerts = static_cast<int>(staticVertexCache.size() / 3);
            const int cachedIndexes = static_cast<int>(staticIndexCache.size());
            if (cachedVerts + dynamicVerts + tri->numVerts > RT_SMOKE_MAX_VERTS ||
                cachedIndexes + dynamicIndexes + tri->numIndexes > RT_SMOKE_MAX_INDEXES)
            {
                ++skipStats.limitExceeded;
                break;
            }
        }

        std::vector<float>& bucketVertices = bucketVertexData[bucketIndex];
        std::vector<uint32_t>& bucketIndexes = bucketIndexData[bucketIndex];
        std::vector<uint32_t>& bucketClasses = bucketTriangleClassData[bucketIndex];
        const size_t vertexStart = bucketVertices.size();
        const size_t indexStart = bucketIndexes.size();
        const size_t classStart = bucketClasses.size();
        const uint32_t indexBase = static_cast<uint32_t>(bucketVertices.size() / 3);
        for (int vertexIndex = 0; vertexIndex < tri->numVerts; ++vertexIndex)
        {
            idVec3 worldPosition;
            TransformSurfacePointToWorld(drawSurf, tri->verts[vertexIndex].xyz, worldPosition);
            const idVec3 position = worldPosition;
            bucketVertices.push_back(position.x);
            bucketVertices.push_back(position.y);
            bucketVertices.push_back(position.z);
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

            idVec3 p0;
            idVec3 p1;
            idVec3 p2;
            TransformSurfacePointToWorld(drawSurf, tri->verts[i0].xyz, p0);
            TransformSurfacePointToWorld(drawSurf, tri->verts[i1].xyz, p1);
            TransformSurfacePointToWorld(drawSurf, tri->verts[i2].xyz, p2);
            if (IsZeroAreaSmokeTriangle(p0, p1, p2))
            {
                continue;
            }

            bucketIndexes.push_back(indexBase + static_cast<uint32_t>(i0));
            bucketIndexes.push_back(indexBase + static_cast<uint32_t>(i1));
            bucketIndexes.push_back(indexBase + static_cast<uint32_t>(i2));
            bucketClasses.push_back(surfaceClassId);
        }

        const int emittedIndexes = static_cast<int>(bucketIndexes.size() - indexStart);
        if (emittedIndexes <= 0 || bucketClasses.size() == classStart)
        {
            bucketVertices.resize(vertexStart);
            bucketIndexes.resize(indexStart);
            bucketClasses.resize(classStart);
            ++skipStats.zeroAreaOnly;
            continue;
        }

        ++sourceSurfaces;
        sourceVerts += tri->numVerts;
        sourceIndexes += emittedIndexes;
        AddSmokeSurfaceClassStats(classStats, surfaceClass, tri->numVerts, emittedIndexes);
        ++bucketRanges.buckets[bucketIndex].surfaceCount;
        if (isStaticWorld)
        {
            const uint32_t staticIndexBase = static_cast<uint32_t>(staticVertexCache.size() / 3);
            staticSurfaceKeys.push_back(staticSurfaceKey);
            staticVertexCache.insert(staticVertexCache.end(), bucketVertices.begin() + vertexStart, bucketVertices.end());
            for (size_t localIndex = indexStart; localIndex < bucketIndexes.size(); ++localIndex)
            {
                staticIndexCache.push_back(staticIndexBase + bucketIndexes[localIndex]);
            }
            staticTriangleClassCache.insert(staticTriangleClassCache.end(), bucketClasses.begin() + classStart, bucketClasses.end());
            bucketVertices.resize(vertexStart);
            bucketIndexes.resize(indexStart);
            bucketClasses.resize(classStart);
            staticCacheChanged = true;
        }
        else
        {
            dynamicVerts += tri->numVerts;
            dynamicIndexes += emittedIndexes;
        }
        if (reasonSamples)
        {
            AddSmokeSurfaceClassReasonSample(*reasonSamples, BuildSmokeSurfaceClassReason(viewDef, drawSurf, tri, surfaceIndex, surfaceClass));
        }
    }

    RtSmokeBucketRange& staticRange = bucketRanges.buckets[0];
    staticRange.vertexOffset = 0;
    staticRange.indexOffset = 0;
    staticRange.triangleOffset = 0;
    staticRange.vertexCount = static_cast<int>(staticVertexCache.size() / 3);
    staticRange.indexCount = static_cast<int>(staticIndexCache.size());
    staticRange.triangleCount = static_cast<int>(staticTriangleClassCache.size());
    vertexData.insert(vertexData.end(), staticVertexCache.begin(), staticVertexCache.end());
    indexData.insert(indexData.end(), staticIndexCache.begin(), staticIndexCache.end());
    triangleClassData.insert(triangleClassData.end(), staticTriangleClassCache.begin(), staticTriangleClassCache.end());

    for (int bucketIndex = 0; bucketIndex < RT_SMOKE_CLASS_COUNT; ++bucketIndex)
    {
        if (bucketIndex == 0)
        {
            continue;
        }

        RtSmokeBucketRange& range = bucketRanges.buckets[bucketIndex];
        range.vertexOffset = static_cast<int>(vertexData.size() / 3);
        range.indexOffset = static_cast<int>(indexData.size());
        range.triangleOffset = static_cast<int>(triangleClassData.size());
        range.vertexCount = static_cast<int>(bucketVertexData[bucketIndex].size() / 3);
        range.indexCount = static_cast<int>(bucketIndexData[bucketIndex].size());
        range.triangleCount = static_cast<int>(bucketTriangleClassData[bucketIndex].size());

        const uint32_t vertexOffset = static_cast<uint32_t>(range.vertexOffset);
        vertexData.insert(vertexData.end(), bucketVertexData[bucketIndex].begin(), bucketVertexData[bucketIndex].end());
        for (uint32_t localIndex : bucketIndexData[bucketIndex])
        {
            indexData.push_back(vertexOffset + localIndex);
        }
        triangleClassData.insert(triangleClassData.end(), bucketTriangleClassData[bucketIndex].begin(), bucketTriangleClassData[bucketIndex].end());
    }

    if (triangleClassData.empty())
    {
        ++skipStats.emptyClassBuffer;
    }

    return sourceSurfaces > 0 && !vertexData.empty() && !indexData.empty() && !triangleClassData.empty();
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
    bindingLayoutDesc.bindings = {
        nvrhi::BindingLayoutItem::RayTracingAccelStruct(0),
        nvrhi::BindingLayoutItem::Texture_UAV(1),
        nvrhi::BindingLayoutItem::ConstantBuffer(2),
        nvrhi::BindingLayoutItem::StructuredBuffer_SRV(3),
        nvrhi::BindingLayoutItem::StructuredBuffer_SRV(4),
        nvrhi::BindingLayoutItem::StructuredBuffer_SRV(5),
        nvrhi::BindingLayoutItem::StructuredBuffer_SRV(6)
    };
    m_smokeBindingLayout = device->createBindingLayout(bindingLayoutDesc);

    if (!m_smokeBindingLayout)
    {
        common->Printf("PathTracePrimaryPass: failed to create RT smoke binding layout\n");
        return;
    }

    nvrhi::rt::PipelineDesc pipelineDesc;
    pipelineDesc.globalBindingLayouts = { m_smokeBindingLayout };
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
    pipelineDesc.maxPayloadSize = 32;
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

    common->Printf("PathTracePrimaryPass: RT smoke output UAV initialized (%dx%d)\n", width, height);
    return true;
}

void PathTracePrimaryPass::BuildRayTracingSmokeTestScene(const viewDef_t* viewDef)
{
    m_smokeSceneBuilt = false;

    if (!m_smokeTlas || !m_smokeBindingLayout || !m_smokeOutputTexture || !m_smokeConstantsBuffer)
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

    std::vector<float> vertexData;
    std::vector<uint32_t> indexData;
    std::vector<uint32_t> triangleClassData;
    int sourceSurfaces = 0;
    int sourceVerts = 0;
    int sourceIndexes = 0;
    int anchorTriangle = -1;
    RtSmokeSurfaceClassStats classStats;
    RtSmokeSurfaceSkipStats skipStats;
    RtSmokeBucketRanges bucketRanges;
    const bool dumpClassReasons = r_pathTracingClassDump.GetInteger() != 0;
    RtSmokeSurfaceClassReasonSamples reasonSamples;
    bool staticCacheChanged = false;
    const bool usingDoomSurfaces = CaptureDoomSurfacesForSmokeTest(viewDef, vertexData, indexData, triangleClassData, m_smokeStaticSurfaceKeys, m_smokeStaticVertexCache, m_smokeStaticIndexCache, m_smokeStaticTriangleClassCache, staticCacheChanged, m_smokeSceneOrigin, sourceSurfaces, sourceVerts, sourceIndexes, anchorTriangle, classStats, skipStats, bucketRanges, dumpClassReasons ? &reasonSamples : nullptr);
    if (!usingDoomSurfaces)
    {
        if (!m_smokeWaitingForDoomSurfaceLogged)
        {
            common->Printf("PathTracePrimaryPass: waiting for center camera ray Doom surface hit to build RT smoke BLAS\n");
            m_smokeWaitingForDoomSurfaceLogged = true;
        }
        return;
    }

    nvrhi::BufferDesc vertexBufferDesc;
    vertexBufferDesc.byteSize = vertexData.size() * sizeof(vertexData[0]);
    vertexBufferDesc.debugName = "PathTraceSmokeDoomSurfaceVertices";
    vertexBufferDesc.structStride = sizeof(float) * 3;
    vertexBufferDesc.isVertexBuffer = true;
    vertexBufferDesc.isAccelStructBuildInput = true;
    vertexBufferDesc.initialState = nvrhi::ResourceStates::Common;
    vertexBufferDesc.keepInitialState = true;
    nvrhi::BufferHandle smokeVertexBuffer = device->createBuffer(vertexBufferDesc);

    nvrhi::BufferDesc indexBufferDesc;
    indexBufferDesc.byteSize = indexData.size() * sizeof(indexData[0]);
    indexBufferDesc.debugName = "PathTraceSmokeDoomSurfaceIndices";
    indexBufferDesc.structStride = sizeof(uint32_t);
    indexBufferDesc.isIndexBuffer = true;
    indexBufferDesc.isAccelStructBuildInput = true;
    indexBufferDesc.initialState = nvrhi::ResourceStates::Common;
    indexBufferDesc.keepInitialState = true;
    nvrhi::BufferHandle smokeIndexBuffer = device->createBuffer(indexBufferDesc);

    nvrhi::BufferDesc triangleClassBufferDesc;
    triangleClassBufferDesc.byteSize = triangleClassData.size() * sizeof(triangleClassData[0]);
    triangleClassBufferDesc.debugName = "PathTraceSmokeTriangleClasses";
    triangleClassBufferDesc.structStride = sizeof(uint32_t);
    triangleClassBufferDesc.initialState = nvrhi::ResourceStates::Common;
    triangleClassBufferDesc.keepInitialState = true;
    nvrhi::BufferHandle smokeTriangleClassBuffer = device->createBuffer(triangleClassBufferDesc);

    PathTraceSmokeInstanceRange instanceRanges[2];
    instanceRanges[0].indexOffset = static_cast<uint32_t>(bucketRanges.buckets[0].indexOffset);
    instanceRanges[0].triangleOffset = static_cast<uint32_t>(bucketRanges.buckets[0].triangleOffset);
    instanceRanges[1].indexOffset = static_cast<uint32_t>(bucketRanges.buckets[1].indexOffset);
    instanceRanges[1].triangleOffset = static_cast<uint32_t>(bucketRanges.buckets[1].triangleOffset);

    nvrhi::BufferDesc instanceRangeBufferDesc;
    instanceRangeBufferDesc.byteSize = sizeof(instanceRanges);
    instanceRangeBufferDesc.debugName = "PathTraceSmokeInstanceRanges";
    instanceRangeBufferDesc.structStride = sizeof(PathTraceSmokeInstanceRange);
    instanceRangeBufferDesc.initialState = nvrhi::ResourceStates::Common;
    instanceRangeBufferDesc.keepInitialState = true;
    nvrhi::BufferHandle smokeInstanceRangeBuffer = device->createBuffer(instanceRangeBufferDesc);

    if (!smokeVertexBuffer || !smokeIndexBuffer || !smokeTriangleClassBuffer || !smokeInstanceRangeBuffer)
    {
        common->Printf("PathTracePrimaryPass: failed to create RT smoke geometry buffers\n");
        return;
    }

    const int totalVertexCount = static_cast<int>(vertexData.size() / 3);
    const int staticIndexOffset = bucketRanges.buckets[0].indexOffset;
    const int staticIndexCount = bucketRanges.buckets[0].indexCount;
    const int dynamicIndexOffset = bucketRanges.buckets[1].indexOffset;
    const int dynamicIndexCount = static_cast<int>(indexData.size()) - dynamicIndexOffset;
    const bool hasStaticBlas = staticIndexCount > 0;
    const bool hasDynamicBlas = dynamicIndexCount > 0;
    const RtSmokeStaticBlasSignature staticSignature = ComputeSmokeStaticBlasSignature(vertexData, indexData, bucketRanges.buckets[0], vec3_origin);
    const bool staticBlasCacheHit = hasStaticBlas && m_smokeStaticBlasCacheValid && m_smokeStaticBlas && !staticCacheChanged;

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
            InitSmokeTriangleGeometry(staticTriangleGeometry, smokeVertexBuffer, smokeIndexBuffer, totalVertexCount, staticIndexOffset, staticIndexCount);

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
        InitSmokeTriangleGeometry(dynamicTriangleGeometry, smokeVertexBuffer, smokeIndexBuffer, totalVertexCount, dynamicIndexOffset, dynamicIndexCount);

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

    commandList->beginTrackingBufferState(smokeVertexBuffer, nvrhi::ResourceStates::Common);
    commandList->beginTrackingBufferState(smokeIndexBuffer, nvrhi::ResourceStates::Common);
    commandList->beginTrackingBufferState(smokeTriangleClassBuffer, nvrhi::ResourceStates::Common);
    commandList->beginTrackingBufferState(smokeInstanceRangeBuffer, nvrhi::ResourceStates::Common);
    commandList->writeBuffer(smokeVertexBuffer, vertexData.data(), vertexData.size() * sizeof(vertexData[0]));
    commandList->writeBuffer(smokeIndexBuffer, indexData.data(), indexData.size() * sizeof(indexData[0]));
    commandList->writeBuffer(smokeTriangleClassBuffer, triangleClassData.data(), triangleClassData.size() * sizeof(triangleClassData[0]));
    commandList->writeBuffer(smokeInstanceRangeBuffer, instanceRanges, sizeof(instanceRanges));
    commandList->setBufferState(smokeVertexBuffer, nvrhi::ResourceStates::AccelStructBuildInput);
    commandList->setBufferState(smokeIndexBuffer, nvrhi::ResourceStates::AccelStructBuildInput);
    commandList->setBufferState(smokeTriangleClassBuffer, nvrhi::ResourceStates::ShaderResource);
    commandList->setBufferState(smokeInstanceRangeBuffer, nvrhi::ResourceStates::ShaderResource);
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
        nvrhi::BindingSetItem::StructuredBuffer_SRV(3, smokeVertexBuffer),
        nvrhi::BindingSetItem::StructuredBuffer_SRV(4, smokeIndexBuffer),
        nvrhi::BindingSetItem::StructuredBuffer_SRV(5, smokeTriangleClassBuffer),
        nvrhi::BindingSetItem::StructuredBuffer_SRV(6, smokeInstanceRangeBuffer)
    };
    nvrhi::BindingSetHandle smokeBindingSet = device->createBindingSet(bindingSetDesc, m_smokeBindingLayout);
    if (!smokeBindingSet)
    {
        common->Printf("PathTracePrimaryPass: failed to create RT smoke binding set\n");
        return;
    }

    m_smokeVertexBuffer = smokeVertexBuffer;
    m_smokeIndexBuffer = smokeIndexBuffer;
    m_smokeTriangleClassBuffer = smokeTriangleClassBuffer;
    m_smokeInstanceRangeBuffer = smokeInstanceRangeBuffer;
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

    if (!m_smokeSceneRebuildLogged)
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
        common->Printf("PathTracePrimaryPass: RT smoke static BLAS cache %s signature=%llu cacheSurfaces=%d hits=%d misses=%d\n",
            staticBlasCacheHit ? "hit" : "rebuild",
            static_cast<unsigned long long>(staticSignature.hash),
            static_cast<int>(m_smokeStaticSurfaceKeys.size()),
            m_smokeStaticBlasCacheHitCount,
            m_smokeStaticBlasCacheMissCount);
        LogSmokeBucketRanges(bucketRanges);
        m_smokeSceneRebuildLogged = true;
    }

    if (dumpClassReasons)
    {
        common->Printf("PathTracePrimaryPass: RT smoke one-shot surface classification sample dump\n");
        LogSmokeSurfaceClassReasonSamples(reasonSamples);
        r_pathTracingClassDump.SetInteger(0);
    }

    if (m_smokeSceneLogCooldownFrames <= 0)
    {
        common->Printf("PathTracePrimaryPass: RT smoke scene capture %d/%d surfaces, %d/%d verts, %d/%d indexes, anchor triangle %d; skipped null=%d geo=%d material=%d space=%d model=%d indexes=%d cache=%d limits=%d zeroArea=%d emptyClass=%d; buckets static-world=%d(%dv/%di/%dt) rigid-entity=%d(%dv/%di/%dt) skinned=%d(%dv/%di/%dt) particle/alpha=%d(%dv/%di/%dt) unknown=%d(%dv/%di/%dt)\n",
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
        common->Printf("PathTracePrimaryPass: RT smoke static BLAS cache %s signature=%llu cacheSurfaces=%d hits=%d misses=%d\n",
            staticBlasCacheHit ? "hit" : "rebuild",
            static_cast<unsigned long long>(staticSignature.hash),
            static_cast<int>(m_smokeStaticSurfaceKeys.size()),
            m_smokeStaticBlasCacheHitCount,
            m_smokeStaticBlasCacheMissCount);
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
    if (!viewDef || !m_smokeSceneBuilt || !m_smokeShaderTable || !m_smokeBindingSet || !m_smokeOutputTexture || !m_smokeReadbackTexture || !m_smokeConstantsBuffer || !m_smokeTriangleClassBuffer || !m_smokeInstanceRangeBuffer)
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
    state.bindings = { m_smokeBindingSet };
    const int debugMode = idMath::ClampInt(0, 3, r_pathTracingDebugMode.GetInteger());

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
    commandList->setBufferState(m_smokeVertexBuffer, nvrhi::ResourceStates::ShaderResource);
    commandList->setBufferState(m_smokeIndexBuffer, nvrhi::ResourceStates::ShaderResource);
    commandList->setBufferState(m_smokeTriangleClassBuffer, nvrhi::ResourceStates::ShaderResource);
    commandList->setBufferState(m_smokeInstanceRangeBuffer, nvrhi::ResourceStates::ShaderResource);
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
