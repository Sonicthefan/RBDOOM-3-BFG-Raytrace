#pragma once

// Persistent RT smoke geometry records.
//
// This starts as ownership for the static-world geometry cache that used to
// live directly on PathTracePrimaryPass. Keeping it behind a small module gives
// later work a stable place to add surface records, GPU buffers, and dirty
// ranges without changing shader-visible behavior in this first step.

#include "PathTraceGeometry.h"

#include <vector>

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

enum class RtSmokeGeometryBufferFormat : uint32_t
{
    LegacySmokeVertex = 0
};

struct RtSmokeGeometryRangeRecord
{
    int vertexOffset = -1;
    int vertexCount = 0;
    int indexOffset = -1;
    int indexCount = 0;
    int triangleOffset = -1;
    int triangleCount = 0;
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

    RtSmokeGeometryUniverseStats GetStats() const;

private:
    RtSmokePersistentStaticSurfaceRecord* FindStaticSurfaceMutable(uint64 key);

    uint64 m_currentFrameIndex = 0;
    bool m_frameActive = false;
    uint64 m_generation = 1;
    std::vector<RtSmokePersistentStaticSurfaceRecord> m_staticSurfaceRecords;
    std::vector<uint64> m_staticSurfaceKeys;
    std::vector<PathTraceSmokeVertex> m_staticVertexCache;
    std::vector<uint32_t> m_staticIndexCache;
    std::vector<uint32_t> m_staticTriangleClassCache;
    std::vector<uint32_t> m_staticTriangleMaterialCache;
};
