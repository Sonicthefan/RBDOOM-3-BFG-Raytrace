#pragma once

// Doom render-surface capture for the RT smoke/path tracing scene.
//
// Converts the current view's draw surfaces into compact static and dynamic
// triangle buckets. This is the boundary where Doom material/surface
// classification, CPU skinning, GUI admission, and scene-origin anchoring meet.

#include "PathTraceDoomMaterialClassifier.h"
#include "PathTraceGeometry.h"
#include "PathTraceSurfaceClassification.h"

#include <vector>

struct drawSurf_t;
struct srfTriangles_t;
struct viewDef_t;
class idJointMat;

const int RT_SMOKE_MAX_SURFACES = 128;
const int RT_SMOKE_MAX_VERTS = 65536;
const int RT_SMOKE_MAX_INDEXES = 196608;
const int RT_SMOKE_CLASS_REASON_SAMPLES = 8;
const int RT_SMOKE_MATERIAL_REASON_SAMPLES = 12;
const int RT_SMOKE_TRANSLUCENT_REASON_SAMPLES = 24;
const uint32_t RT_SMOKE_TRIANGLE_CLASS_MASK = 0x0000ffffu;
const uint32_t RT_SMOKE_TRIANGLE_FORCE_GEOMETRIC_NORMAL = 0x00010000u;

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

struct RtSmokeMaterialSample
{
    uint32_t id = 0;
    int surfaces = 0;
    int triangles = 0;
    idStr name;
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

struct RtSmokeSceneCaptureTiming
{
    int anchorMs = 0;
    int anchorSurfaceTests = 0;
    int anchorBoundsRejects = 0;
    int anchorTriangleTests = 0;
    int validationMs = 0;
    int staticPassClassifyMs = 0;
    int staticCacheLookupMs = 0;
    int staticAppendMs = 0;
    int dynamicPassClassifyMs = 0;
    int dynamicAppendMs = 0;
    int rtCpuSkinningAppendMs = 0;
    int appendMs = 0;
    int bucketMergeMs = 0;
    int staticCachedSurfaces = 0;
    int staticNewSurfaces = 0;
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
    RtSmokeAttributeClassStats classes[5];
};

void TransformSurfacePointToWorld(const drawSurf_t* drawSurf, const idVec3& localPoint, idVec3& worldPoint);
void TransformSurfaceVectorToWorld(const drawSurf_t* drawSurf, const idVec3& localVector, idVec3& worldVector);
bool ValidateSmokeDrawSurface(const viewDef_t* viewDef, const drawSurf_t* drawSurf, const srfTriangles_t*& tri, RtSmokeSurfaceSkipStats* skipStats);
bool FindCenterCameraRayAnchor(const viewDef_t* viewDef, idVec3& anchorPoint, int& anchorSurface, int& anchorTriangle, RtSmokeSceneCaptureTiming* captureTiming = nullptr);
PathTraceSmokeVertex BuildSmokeSurfaceVertex(const drawSurf_t* drawSurf, const srfTriangles_t* tri, int vertexIndex, const idJointMat* rtCpuSkinningJoints);
void TransformSmokeSurfaceVertexToWorld(const drawSurf_t* drawSurf, const srfTriangles_t* tri, int vertexIndex, const idJointMat* rtCpuSkinningJoints, idVec3& worldPosition);
int AppendSmokeSurfaceGeometry(
    const drawSurf_t* drawSurf,
    const srfTriangles_t* tri,
    uint32_t surfaceClassId,
    uint32_t materialId,
    int classCount,
    uint32_t triangleClassMask,
    uint32_t particleAlphaClassId,
    uint32_t forceGeometricNormalFlag,
    std::vector<PathTraceSmokeVertex>& vertices,
    std::vector<uint32_t>& indexes,
    std::vector<uint32_t>& triangleClasses,
    std::vector<uint32_t>& triangleMaterials,
    RtSmokeSurfaceSkipStats& skipStats,
    RtSmokeAttributeStats& attributeStats);

bool CaptureDoomSurfacesForSmokeTest(
    const viewDef_t* viewDef,
    std::vector<PathTraceSmokeVertex>& vertexData,
    std::vector<uint32_t>& indexData,
    std::vector<uint32_t>& triangleClassData,
    std::vector<uint32_t>& triangleMaterialData,
    std::vector<uint64>& staticSurfaceKeys,
    std::vector<PathTraceSmokeVertex>& staticVertexCache,
    std::vector<uint32_t>& staticIndexCache,
    std::vector<uint32_t>& staticTriangleClassCache,
    std::vector<uint32_t>& staticTriangleMaterialCache,
    bool& staticCacheChanged,
    idVec3& sceneOrigin,
    int& sourceSurfaces,
    int& sourceVerts,
    int& sourceIndexes,
    int& anchorTriangle,
    RtSmokeSurfaceClassStats& classStats,
    RtSmokeSurfaceSkipStats& skipStats,
    RtSmokeDynamicGeometryStats& dynamicStats,
    RtSmokeAttributeStats& attributeStats,
    RtSmokeMaterialStats& materialStats,
    RtSmokeBucketRanges& bucketRanges,
    RtSmokeSceneCaptureTiming& captureTiming,
    RtSmokeSurfaceClassReasonSamples* reasonSamples);
