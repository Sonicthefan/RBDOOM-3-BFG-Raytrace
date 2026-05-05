#pragma once

// Persistent RT smoke geometry records.
//
// This starts as ownership for the static-world geometry cache that used to
// live directly on PathTracePrimaryPass. Keeping it behind a small module gives
// later work a stable place to add surface records, GPU buffers, and dirty
// ranges without changing shader-visible behavior in this first step.

#include "PathTraceGeometry.h"

#include <unordered_map>
#include <unordered_set>
#include <vector>

const int RT_PT_RIGID_MESH_CANDIDATE_SAMPLES = 8;
const int RT_PT_RIGID_BLAS_PLAN_SAMPLES = 8;
const int RT_PT_RIGID_BLAS_INPUT_SAMPLES = 8;

struct RtSmokeSurfaceClassStats;
struct srfTriangles_t;

struct RtSmokeGeometryUniverseStats
{
    int staticRecords = 0;
    int staticSurfaces = 0;
    int staticVerts = 0;
    int staticIndexes = 0;
    int staticTriangles = 0;
    int staticSeenThisFrame = 0;
    int staticNewThisFrame = 0;
    int staticDisappearedThisFrame = 0;
    int staticHistoryValid = 0;
    int staticPreviousRangeValid = 0;
    int staticDirty = 0;
    int staticValidationErrors = 0;
    int staticRangeErrors = 0;
    int staticDuplicateKeys = 0;
    int staticHistoryErrors = 0;
    int staticKeyVectorMismatches = 0;
    int staticBytesKB = 0;
    uint64 frameIndex = 0;
    uint64 generation = 1;
};

enum RtPathTraceRigidMeshCandidateRejectFlags : uint32_t
{
    RT_PT_RIGID_MESH_REJECT_NOT_RIGID = 1u << 0,
    RT_PT_RIGID_MESH_REJECT_INVALID_GEOMETRY = 1u << 1,
    RT_PT_RIGID_MESH_REJECT_MISSING_MATERIAL = 1u << 2,
    RT_PT_RIGID_MESH_REJECT_NO_LOCAL_SPACE = 1u << 3,
    RT_PT_RIGID_MESH_REJECT_SKINNED_OR_DEFORMING = 1u << 4,
    RT_PT_RIGID_MESH_REJECT_PARTICLE_OR_TRANSIENT = 1u << 5,
    RT_PT_RIGID_MESH_REJECT_GUI = 1u << 6,
    RT_PT_RIGID_MESH_REJECT_CALLBACK_OR_GENERATED = 1u << 7,
    RT_PT_RIGID_MESH_REJECT_STATIC_WORLD = 1u << 8,
    RT_PT_RIGID_MESH_REJECT_STATIC_CACHE_MATCH = 1u << 9
};

struct RtPathTraceRigidMeshCandidateObservation
{
    const srfTriangles_t* tri = nullptr;
    uint64 meshHash = 0;
    uint64 instanceId = 0;
    uintptr_t vertexBufferIdentity = 0;
    uintptr_t indexBufferIdentity = 0;
    uint32_t sourceFlags = 0;
    uint32_t materialId = 0;
    uint32_t surfaceClassId = 0;
    uint32_t vertexFormat = 0;
    int drawSurfIndex = -1;
    int entityIndex = -1;
    int renderEntityNum = -1;
    int numVerts = 0;
    int numIndexes = 0;
    bool localSpaceValid = false;
    idStr materialName;
    idStr modelName;
};

struct RtPathTraceRigidMeshCandidateSample
{
    bool valid = false;
    bool eligible = false;
    uint64 meshHash = 0;
    uint64 instanceId = 0;
    uintptr_t triIdentity = 0;
    uintptr_t vertexBufferIdentity = 0;
    uintptr_t indexBufferIdentity = 0;
    uint32_t rejectFlags = 0;
    uint32_t materialId = 0;
    uint32_t vertexFormat = 0;
    int drawSurfIndex = -1;
    int entityIndex = -1;
    int renderEntityNum = -1;
    int numVerts = 0;
    int numIndexes = 0;
    int seenCount = 0;
    idStr materialName;
    idStr modelName;
};

struct RtPathTraceRigidMeshCandidateStats
{
    int observations = 0;
    int rigidObservations = 0;
    int eligibleInstances = 0;
    int rejectedInstances = 0;
    int eligibleUniqueMeshes = 0;
    int persistentEligibleMeshes = 0;
    int localMeshSourceRecords = 0;
    int localMeshSourceRecordsSeenThisFrame = 0;
    int localMeshSourceVerts = 0;
    int localMeshSourceIndexes = 0;
    int localMeshSourceTriangles = 0;
    int eligibleVertsThisFrame = 0;
    int eligibleIndexesThisFrame = 0;
    int eligibleTrianglesThisFrame = 0;
    int materialOverrideEligibleInstances = 0;
    int reusedEligibleMeshObservations = 0;
    int newlyEligibleMeshes = 0;
    int rejectNotRigid = 0;
    int rejectInvalidGeometry = 0;
    int rejectMissingMaterial = 0;
    int rejectNoLocalSpace = 0;
    int rejectSkinnedOrDeforming = 0;
    int rejectParticleOrTransient = 0;
    int rejectGui = 0;
    int rejectCallbackOrGenerated = 0;
    int rejectStaticWorld = 0;
    int rejectStaticCacheMatch = 0;
    uint64 frameIndex = 0;
    uint64 generation = 1;
    RtPathTraceRigidMeshCandidateSample eligibleSamples[RT_PT_RIGID_MESH_CANDIDATE_SAMPLES];
    int eligibleSampleCount = 0;
    RtPathTraceRigidMeshCandidateSample rejectedSamples[RT_PT_RIGID_MESH_CANDIDATE_SAMPLES];
    int rejectedSampleCount = 0;
};

struct RtPathTraceRigidMeshValidationStats
{
    int bakedRigidTriangles = 0;
    int bakedRigidMaterialIds = 0;
    int eligibleRigidTriangles = 0;
    int eligibleRigidMaterialIds = 0;
    int triangleDelta = 0;
    int missingMaterialIds = 0;
    int extraMaterialIds = 0;
    uint32_t missingMaterialSamples[8] = {};
    uint32_t extraMaterialSamples[8] = {};
    int missingMaterialSampleCount = 0;
    int extraMaterialSampleCount = 0;
};

struct RtPathTraceRigidBlasPlanSample
{
    bool valid = false;
    uint64 meshHash = 0;
    uintptr_t triIdentity = 0;
    uintptr_t vertexBufferIdentity = 0;
    uintptr_t indexBufferIdentity = 0;
    uint32_t materialId = 0;
    uint32_t vertexFormat = 0;
    int instanceCount = 0;
    int verts = 0;
    int indexes = 0;
    int triangles = 0;
    idStr materialName;
    idStr modelName;
};

struct RtPathTraceRigidBlasPlanStats
{
    int meshRecords = 0;
    int instances = 0;
    int localVerts = 0;
    int localIndexes = 0;
    int localTriangles = 0;
    int plannedRemoveRigidTriangles = 0;
    int bakedRigidSurfaces = 0;
    int bakedRigidTriangles = 0;
    int estimatedRemainingRigidTriangles = 0;
    int triangleDelta = 0;
    int persistentMeshRecords = 0;
    uint64 frameIndex = 0;
    uint64 generation = 1;
    RtPathTraceRigidBlasPlanSample samples[RT_PT_RIGID_BLAS_PLAN_SAMPLES];
    int sampleCount = 0;
};

enum RtPathTraceRigidBlasInputInvalidFlags : uint32_t
{
    RT_PT_RIGID_BLAS_INPUT_INVALID_NULL_TRI = 1u << 0,
    RT_PT_RIGID_BLAS_INPUT_INVALID_VERTEX_COUNT = 1u << 1,
    RT_PT_RIGID_BLAS_INPUT_INVALID_INDEX_COUNT = 1u << 2,
    RT_PT_RIGID_BLAS_INPUT_INVALID_TRIANGLE_COUNT = 1u << 3,
    RT_PT_RIGID_BLAS_INPUT_INVALID_VERTEX_FORMAT = 1u << 4,
    RT_PT_RIGID_BLAS_INPUT_INVALID_MISSING_SOURCE_IDENTITY = 1u << 5,
    RT_PT_RIGID_BLAS_INPUT_INVALID_MATERIAL = 1u << 6
};

struct RtPathTraceRigidBlasInputSample
{
    bool valid = false;
    uint64 meshHash = 0;
    uintptr_t triIdentity = 0;
    uintptr_t vertexBufferIdentity = 0;
    uintptr_t indexBufferIdentity = 0;
    uint32_t materialId = 0;
    uint32_t invalidFlags = 0;
    int vertexCount = 0;
    int indexCount = 0;
    int triangleCount = 0;
    int vertexStride = 0;
    int vertexOffsetBytes = 0;
    int indexOffsetBytes = 0;
    int instanceCount = 0;
    idStr materialName;
    idStr modelName;
};

struct RtPathTraceRigidBlasInputStats
{
    int descriptors = 0;
    int validDescriptors = 0;
    int invalidDescriptors = 0;
    int instances = 0;
    int geometryDescs = 0;
    int vertexCount = 0;
    int indexCount = 0;
    int triangleCount = 0;
    int vertexBytes = 0;
    int indexBytes = 0;
    int nullTri = 0;
    int invalidVertexCount = 0;
    int invalidIndexCount = 0;
    int invalidTriangleCount = 0;
    int invalidVertexFormat = 0;
    int missingSourceIdentity = 0;
    int invalidMaterial = 0;
    uint64 frameIndex = 0;
    uint64 generation = 1;
    RtPathTraceRigidBlasInputSample samples[RT_PT_RIGID_BLAS_INPUT_SAMPLES];
    int sampleCount = 0;
};

enum class RtSmokeGeometryBufferFormat : uint32_t
{
    LegacySmokeVertex = 0
};

struct RtSmokeGeometryElementRange
{
    int offset = -1;
    int count = 0;
};

struct RtSmokeGeometryRangeRecord
{
    RtSmokeGeometryElementRange vertices;
    RtSmokeGeometryElementRange indexes;
    RtSmokeGeometryElementRange triangles;
};

struct RtSmokePersistentStaticSurfaceRecord
{
    bool valid = false;
    uint64 key = 0;
    uint32_t surfaceClassId = 0;
    uint32_t materialId = 0;
    RtSmokeGeometryRangeRecord currentRange;
    RtSmokeGeometryRangeRecord previousRange;
    uint64 lastSeenFrame = 0;
    uint64 previousSeenFrame = 0;
    bool seenThisFrame = false;
    bool newlyCreatedThisFrame = false;
    bool disappearedThisFrame = false;
    bool previousRangeValid = false;
    bool historyValid = false;
    bool dirty = true;
    RtSmokeGeometryBufferFormat geometryFormat = RtSmokeGeometryBufferFormat::LegacySmokeVertex;
};

struct RtSmokeStaticSurfaceAppend
{
    uint64 key = 0;
    uint32_t surfaceClassId = 0;
    uint32_t materialId = 0;
    int vertexOffset = 0;
    int indexOffset = 0;
    int triangleOffset = 0;
    int requestedVertexCount = 0;
    int requestedIndexCount = 0;
};

class RtSmokeGeometryUniverse
{
public:
    void Clear();
    void BeginFrame(uint64 frameIndex);
    void EndFrame();
    void NotifyStaticCacheChanged();
    void ReserveStaticSurfaceRecords(size_t surfaceCount);
    bool HasStaticSurface(uint64 key) const;
    RtSmokePersistentStaticSurfaceRecord* TouchStaticSurface(uint64 key);
    bool CanAppendStaticSurface(int vertexCount, int indexCount, int maxVertexCount, int maxIndexCount) const;
    RtSmokeStaticSurfaceAppend BeginStaticSurfaceAppend(uint64 key, uint32_t surfaceClassId, uint32_t materialId, int vertexCount, int indexCount) const;
    void CompleteStaticSurfaceAppend(const RtSmokeStaticSurfaceAppend& append, int emittedIndexCount);

    const RtSmokePersistentStaticSurfaceRecord* FindStaticSurface(uint64 key) const;
    const std::vector<RtSmokePersistentStaticSurfaceRecord>& StaticSurfaceRecords() const;

    std::vector<uint64>& StaticSurfaceKeys();
    const std::vector<uint64>& StaticSurfaceKeys() const;

    std::vector<PathTraceSmokeVertex>& StaticVertices();
    const std::vector<PathTraceSmokeVertex>& StaticVertices() const;

    std::vector<uint32_t>& StaticIndexes();
    const std::vector<uint32_t>& StaticIndexes() const;

    std::vector<uint32_t>& StaticTriangleClasses();
    const std::vector<uint32_t>& StaticTriangleClasses() const;

    std::vector<uint32_t>& StaticTriangleMaterials();
    const std::vector<uint32_t>& StaticTriangleMaterials() const;

    RtSmokeGeometryUniverseStats GetStats(bool validateRecords) const;
    void LogStaticValidationFailures(int maxRecords) const;
    void RecordRigidMeshCandidate(const RtPathTraceRigidMeshCandidateObservation& observation);
    const RtPathTraceRigidMeshCandidateStats& GetRigidMeshCandidateStats() const;
    void RunRigidMeshCandidateDiagnostics(bool dumpRequested, int sceneSource, const RtSmokeSurfaceClassStats* sourceClassStats = nullptr);
    RtPathTraceRigidMeshValidationStats ValidateRigidMeshCandidatesAgainstDynamicPayload(
        const std::vector<uint32_t>& dynamicTriangleClassData,
        const std::vector<uint32_t>& dynamicTriangleMaterialData,
        uint32_t triangleClassMask,
        uint32_t rigidClassId) const;
    void DumpRigidMeshValidationStats(const RtPathTraceRigidMeshValidationStats& stats, int sceneSource) const;
    RtPathTraceRigidBlasPlanStats BuildRigidBlasPlanStats(const RtSmokeSurfaceClassStats* sourceClassStats = nullptr) const;
    void DumpRigidBlasPlanStats(const RtPathTraceRigidBlasPlanStats& stats, int sceneSource) const;
    RtPathTraceRigidBlasInputStats BuildRigidBlasInputStats() const;
    void DumpRigidBlasInputStats(const RtPathTraceRigidBlasInputStats& stats, int sceneSource) const;

private:
    struct RigidMeshCandidateRecord
    {
        bool valid = false;
        const srfTriangles_t* tri = nullptr;
        uint64 meshHash = 0;
        uintptr_t vertexBufferIdentity = 0;
        uintptr_t indexBufferIdentity = 0;
        uint32_t materialId = 0;
        uint32_t surfaceClassId = 0;
        uint32_t vertexFormat = 0;
        RtSmokeGeometryRangeRecord sourceRange;
        int firstSeenFrame = 0;
        int lastSeenFrame = 0;
        int seenCount = 0;
        int instanceCountThisFrame = 0;
        bool seenThisFrame = false;
        bool newlyCreatedThisFrame = false;
        idStr materialName;
        idStr modelName;
    };

    RtSmokePersistentStaticSurfaceRecord* FindStaticSurfaceMutable(uint64 key);
    RigidMeshCandidateRecord* FindOrCreateRigidMeshCandidate(const RtPathTraceRigidMeshCandidateObservation& observation, bool& cacheHit);
    void ResetRigidMeshCandidateFrameStats();
    void AddRigidMeshCandidateSample(const RtPathTraceRigidMeshCandidateObservation& observation, bool eligible, uint32_t rejectFlags, int seenCount);

    uint64 m_currentFrameIndex = 0;
    bool m_frameActive = false;
    uint64 m_generation = 1;
    std::vector<RtSmokePersistentStaticSurfaceRecord> m_staticSurfaceRecords;
    std::unordered_map<uint64, size_t> m_staticSurfaceLookup;
    std::vector<uint64> m_staticSurfaceKeys;
    std::vector<PathTraceSmokeVertex> m_staticVertexCache;
    std::vector<uint32_t> m_staticIndexCache;
    std::vector<uint32_t> m_staticTriangleClassCache;
    std::vector<uint32_t> m_staticTriangleMaterialCache;
    std::vector<RigidMeshCandidateRecord> m_rigidMeshCandidateRecords;
    std::unordered_map<uint64, size_t> m_rigidMeshCandidateLookup;
    std::unordered_set<uint64> m_frameRigidMeshCandidateHashes;
    RtPathTraceRigidMeshCandidateStats m_rigidMeshCandidateFrameStats;
};
