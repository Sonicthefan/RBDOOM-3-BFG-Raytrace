#pragma once

// Diagnostics-only PT scene universe scaffold.
//
// This walks renderer world/model data directly so we can inspect static-world
// geometry and area membership without depending on the raster drawSurf list.
// It deliberately does not feed BLAS/TLAS or shader buffers yet.

#include "../BoundsTrack.h"
#include "PathTraceSceneCapture.h"

#include <unordered_map>
#include <vector>

class idRenderWorldLocal;
class RtSmokeGeometryUniverse;
struct viewDef_t;

struct RtPathTraceSceneUniverseSurface
{
    uint64 key = 0;
    uint64 legacyDrawSurfKey = 0;
    int entityIndex = -1;
    int surfaceIndex = -1;
    uint32_t materialId = 0;
    int triangles = 0;
    idBounds bounds;
    int centerArea = -1;
    int offCenterArea = -1;
    int areas[8] = {};
    int areaCount = 0;
    int portalCount = 0;
    bool emissiveCapable = false;
    bool areaBoundsSkipped = false;
    idStr modelName;
    idStr materialName;
};

struct RtPathTraceSceneUniverseAreaStats
{
    int area = -1;
    int surfaces = 0;
    int triangles = 0;
    int emissiveCapableSurfaces = 0;
    int portals = 0;
};

struct RtPathTraceSceneUniverseStats
{
    bool valid = false;
    uint64 generation = 0;
    int entityDefs = 0;
    int staticWorldEntities = 0;
    int staticSurfaces = 0;
    int staticTriangles = 0;
    int uniqueMaterials = 0;
    int emissiveCapableSurfaces = 0;
    int emissiveCapableTriangles = 0;
    int emissiveCapableMaterials = 0;
    int assignedSurfaces = 0;
    int unassignedSurfaces = 0;
    int multiAreaSurfaces = 0;
    int boundsAreaSkippedSurfaces = 0;
    int centerAreaSurfaces = 0;
    int offCenterAreaSurfaces = 0;
    int distinctAreas = 0;
    int totalAreaRefs = 0;
    int totalPortalsReferenced = 0;
    idBounds bounds;
    idStr mapName;
};

struct RtPathTraceSceneUniverseMaterialStats
{
    uint32_t materialId = 0;
    int surfaces = 0;
    int triangles = 0;
    bool emissiveCapable = false;
    idStr materialName;
};

struct RtPathTraceSceneUniverseSelectionStats
{
    bool valid = false;
    int currentArea = -1;
    int portalSteps = 0;
    int selectedAreas = 0;
    int selectedSurfaces = 0;
    int selectedTriangles = 0;
    int selectedEmissiveCapableSurfaces = 0;
    int selectedEmissiveCapableTriangles = 0;
    int selectedMaterials = 0;
    int selectedEmissiveCapableMaterials = 0;
    int selectedCachedStaticSurfaces = 0;
    int selectedCachedStaticTriangles = 0;
    int selectedMissingStaticSurfaces = 0;
    int selectedMissingStaticTriangles = 0;
    int selectedAreaList[16] = {};
    int selectedAreaListCount = 0;
    int portalEdgesWalked = 0;
    int blockedPortalEdges = 0;
    int overflowAreas = 0;
    std::vector<RtPathTraceSceneUniverseMaterialStats> selectedEmissiveMaterials;
};

struct RtPathTraceSceneUniverseBuildStats
{
    bool built = false;
    bool cacheHit = false;
    int surfaces = 0;
    int vertices = 0;
    int indexes = 0;
    int triangles = 0;
    int skippedInvalid = 0;
    int skippedLimits = 0;
    int skippedZeroArea = 0;
    int emissiveCapableSurfaces = 0;
    int rigidEntitySurfaces = 0;
    int rigidEntityTriangles = 0;
};

class RtPathTraceSceneUniverse
{
public:
    void Clear();
    bool EnsureBuilt(const viewDef_t* viewDef);
    void RunDiagnostics(const viewDef_t* viewDef, const RtSmokeGeometryUniverse* geometryUniverse, int sceneSource, bool dumpRequested, int drawSurfStaticSurfaces, int drawSurfStaticTriangles);
    RtPathTraceSceneUniverseBuildStats BuildFullStaticGeometry(const viewDef_t* viewDef, RtSmokeGeometryUniverse& geometryUniverse, RtSmokeSurfaceClassStats& classStats, RtSmokeSurfaceSkipStats& skipStats, RtSmokeAttributeStats& attributeStats, RtSmokeMaterialStats& materialStats, RtSmokeBucketRanges& bucketRanges);

    const RtPathTraceSceneUniverseStats& GetStats() const;
    const std::vector<RtPathTraceSceneUniverseSurface>& Surfaces() const;

private:
    bool Build(const viewDef_t* viewDef);
    RtPathTraceSceneUniverseSelectionStats BuildSelectionStats(const viewDef_t* viewDef, const RtSmokeGeometryUniverse* geometryUniverse, int portalSteps) const;
    void LogSummary(int sceneSource, const RtPathTraceSceneUniverseSelectionStats& selection, int drawSurfStaticSurfaces, int drawSurfStaticTriangles) const;
    void LogDump(int sceneSource, const RtPathTraceSceneUniverseSelectionStats& selection, int drawSurfStaticSurfaces, int drawSurfStaticTriangles) const;

    const idRenderWorldLocal* m_renderWorld = nullptr;
    idStr m_renderWorldMapName;
    ID_TIME_T m_renderWorldMapTimeStamp = 0;
    uint64 m_generation = 0;
    uint64 m_fullStaticGeometryGeneration = 0;
    int m_fullStaticGeometryRigidMode = 0;
    RtPathTraceSceneUniverseStats m_stats;
    std::vector<RtPathTraceSceneUniverseSurface> m_surfaces;
    std::vector<RtPathTraceSceneUniverseAreaStats> m_areaStats;
    bool m_loggedForCurrentWorld = false;
};
