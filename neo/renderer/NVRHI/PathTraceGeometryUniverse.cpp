#include "precompiled.h"
#pragma hdrstop

#include "PathTraceGeometryUniverse.h"

void RtSmokeGeometryUniverse::Clear()
{
    m_currentFrameIndex = 0;
    m_frameActive = false;
    m_staticSurfaceRecords.clear();
    m_staticSurfaceKeys.clear();
    m_staticVertexCache.clear();
    m_staticIndexCache.clear();
    m_staticTriangleClassCache.clear();
    m_staticTriangleMaterialCache.clear();
    ++m_generation;
}

void RtSmokeGeometryUniverse::BeginFrame(uint64 frameIndex)
{
    m_currentFrameIndex = frameIndex;
    m_frameActive = true;
    for (RtSmokePersistentStaticSurfaceRecord& record : m_staticSurfaceRecords)
    {
        record.seenThisFrame = false;
        record.newlyCreatedThisFrame = false;
        record.disappearedThisFrame = false;
    }
}

void RtSmokeGeometryUniverse::EndFrame()
{
    if (!m_frameActive)
    {
        return;
    }

    for (RtSmokePersistentStaticSurfaceRecord& record : m_staticSurfaceRecords)
    {
        if (!record.valid)
        {
            continue;
        }

        if (!record.seenThisFrame)
        {
            record.disappearedThisFrame = record.lastSeenFrame > 0 && record.lastSeenFrame + 1 == m_currentFrameIndex;
            if (record.disappearedThisFrame)
            {
                record.previousRangeValid = false;
                record.historyValid = false;
                record.dirty = true;
            }
            else
            {
                record.dirty = false;
            }
            continue;
        }

        record.dirty = record.newlyCreatedThisFrame || !record.historyValid;
    }

    m_frameActive = false;
}

void RtSmokeGeometryUniverse::NotifyStaticCacheChanged()
{
    ++m_generation;
}

bool RtSmokeGeometryUniverse::HasStaticSurface(uint64 key) const
{
    return FindStaticSurface(key) != nullptr;
}

RtSmokePersistentStaticSurfaceRecord* RtSmokeGeometryUniverse::TouchStaticSurface(uint64 key)
{
    RtSmokePersistentStaticSurfaceRecord* record = FindStaticSurfaceMutable(key);
    if (!record || !record->valid)
    {
        return nullptr;
    }

    if (!record->seenThisFrame)
    {
        record->previousSeenFrame = record->lastSeenFrame;
        record->lastSeenFrame = m_currentFrameIndex;
        record->seenThisFrame = true;
        record->newlyCreatedThisFrame = false;
        record->disappearedThisFrame = false;
        const bool consecutiveFrame =
            record->previousSeenFrame > 0 &&
            record->previousSeenFrame + 1 == m_currentFrameIndex;
        if (consecutiveFrame)
        {
            record->previousVertexOffset = record->vertexOffset;
            record->previousVertexCount = record->vertexCount;
            record->previousIndexOffset = record->indexOffset;
            record->previousIndexCount = record->indexCount;
            record->previousTriangleOffset = record->triangleOffset;
            record->previousTriangleCount = record->triangleCount;
        }
        record->previousRangeValid =
            consecutiveFrame &&
            record->previousVertexOffset >= 0 &&
            record->previousVertexCount == record->vertexCount &&
            record->previousIndexOffset >= 0 &&
            record->previousIndexCount == record->indexCount &&
            record->previousTriangleOffset >= 0 &&
            record->previousTriangleCount == record->triangleCount;
        record->historyValid = record->previousRangeValid;
    }

    return record;
}

bool RtSmokeGeometryUniverse::CanAppendStaticSurface(int vertexCount, int indexCount, int maxVertexCount, int maxIndexCount) const
{
    return static_cast<int>(m_staticVertexCache.size()) + vertexCount <= maxVertexCount &&
        static_cast<int>(m_staticIndexCache.size()) + indexCount <= maxIndexCount;
}

RtSmokeStaticSurfaceAppend RtSmokeGeometryUniverse::BeginStaticSurfaceAppend(uint64 key, uint32_t surfaceClassId, uint32_t materialId, int vertexCount, int indexCount) const
{
    RtSmokeStaticSurfaceAppend append;
    append.key = key;
    append.surfaceClassId = surfaceClassId;
    append.materialId = materialId;
    append.vertexOffset = static_cast<int>(m_staticVertexCache.size());
    append.indexOffset = static_cast<int>(m_staticIndexCache.size());
    append.triangleOffset = static_cast<int>(m_staticTriangleClassCache.size());
    append.requestedVertexCount = vertexCount;
    append.requestedIndexCount = indexCount;
    return append;
}

void RtSmokeGeometryUniverse::CompleteStaticSurfaceAppend(const RtSmokeStaticSurfaceAppend& append, int emittedIndexCount)
{
    if (emittedIndexCount <= 0)
    {
        return;
    }

    RtSmokePersistentStaticSurfaceRecord record;
    record.valid = true;
    record.key = append.key;
    record.surfaceClassId = append.surfaceClassId;
    record.materialId = append.materialId;
    record.vertexOffset = append.vertexOffset;
    record.vertexCount = static_cast<int>(m_staticVertexCache.size()) - append.vertexOffset;
    record.indexOffset = append.indexOffset;
    record.indexCount = emittedIndexCount;
    record.triangleOffset = append.triangleOffset;
    record.triangleCount = emittedIndexCount / 3;
    record.previousVertexOffset = record.vertexOffset;
    record.previousVertexCount = 0;
    record.previousIndexOffset = record.indexOffset;
    record.previousIndexCount = 0;
    record.previousTriangleOffset = record.triangleOffset;
    record.previousTriangleCount = 0;
    record.lastSeenFrame = m_currentFrameIndex;
    record.previousSeenFrame = 0;
    record.seenThisFrame = true;
    record.newlyCreatedThisFrame = true;
    record.disappearedThisFrame = false;
    record.previousRangeValid = false;
    record.historyValid = false;
    record.dirty = true;
    record.geometryFormat = RtSmokeGeometryBufferFormat::LegacySmokeVertex;

    m_staticSurfaceRecords.push_back(record);
    m_staticSurfaceKeys.push_back(append.key);
    ++m_generation;
}

const RtSmokePersistentStaticSurfaceRecord* RtSmokeGeometryUniverse::FindStaticSurface(uint64 key) const
{
    for (const RtSmokePersistentStaticSurfaceRecord& record : m_staticSurfaceRecords)
    {
        if (record.valid && record.key == key)
        {
            return &record;
        }
    }
    return nullptr;
}

RtSmokePersistentStaticSurfaceRecord* RtSmokeGeometryUniverse::FindStaticSurfaceMutable(uint64 key)
{
    for (RtSmokePersistentStaticSurfaceRecord& record : m_staticSurfaceRecords)
    {
        if (record.valid && record.key == key)
        {
            return &record;
        }
    }
    return nullptr;
}

const std::vector<RtSmokePersistentStaticSurfaceRecord>& RtSmokeGeometryUniverse::StaticSurfaceRecords() const
{
    return m_staticSurfaceRecords;
}

std::vector<uint64>& RtSmokeGeometryUniverse::StaticSurfaceKeys()
{
    return m_staticSurfaceKeys;
}

const std::vector<uint64>& RtSmokeGeometryUniverse::StaticSurfaceKeys() const
{
    return m_staticSurfaceKeys;
}

std::vector<PathTraceSmokeVertex>& RtSmokeGeometryUniverse::StaticVertices()
{
    return m_staticVertexCache;
}

const std::vector<PathTraceSmokeVertex>& RtSmokeGeometryUniverse::StaticVertices() const
{
    return m_staticVertexCache;
}

std::vector<uint32_t>& RtSmokeGeometryUniverse::StaticIndexes()
{
    return m_staticIndexCache;
}

const std::vector<uint32_t>& RtSmokeGeometryUniverse::StaticIndexes() const
{
    return m_staticIndexCache;
}

std::vector<uint32_t>& RtSmokeGeometryUniverse::StaticTriangleClasses()
{
    return m_staticTriangleClassCache;
}

const std::vector<uint32_t>& RtSmokeGeometryUniverse::StaticTriangleClasses() const
{
    return m_staticTriangleClassCache;
}

std::vector<uint32_t>& RtSmokeGeometryUniverse::StaticTriangleMaterials()
{
    return m_staticTriangleMaterialCache;
}

const std::vector<uint32_t>& RtSmokeGeometryUniverse::StaticTriangleMaterials() const
{
    return m_staticTriangleMaterialCache;
}

RtSmokeGeometryUniverseStats RtSmokeGeometryUniverse::GetStats() const
{
    RtSmokeGeometryUniverseStats stats;
    stats.staticRecords = static_cast<int>(m_staticSurfaceRecords.size());
    stats.staticSurfaces = static_cast<int>(m_staticSurfaceKeys.size());
    stats.staticVerts = static_cast<int>(m_staticVertexCache.size());
    stats.staticIndexes = static_cast<int>(m_staticIndexCache.size());
    stats.staticTriangles = static_cast<int>(m_staticTriangleClassCache.size());
    if (m_staticSurfaceKeys.size() != m_staticSurfaceRecords.size())
    {
        ++stats.staticKeyVectorMismatches;
    }
    for (size_t recordIndex = 0; recordIndex < m_staticSurfaceRecords.size(); ++recordIndex)
    {
        const RtSmokePersistentStaticSurfaceRecord& record = m_staticSurfaceRecords[recordIndex];
        if (record.valid && record.historyValid)
        {
            ++stats.staticHistoryValid;
        }
        if (record.valid && record.seenThisFrame)
        {
            ++stats.staticSeenThisFrame;
        }
        if (record.valid && record.newlyCreatedThisFrame)
        {
            ++stats.staticNewThisFrame;
        }
        if (record.valid && record.disappearedThisFrame)
        {
            ++stats.staticDisappearedThisFrame;
        }
        if (record.valid && record.previousRangeValid)
        {
            ++stats.staticPreviousRangeValid;
        }
        if (record.valid && record.dirty)
        {
            ++stats.staticDirty;
        }
        if (!record.valid)
        {
            continue;
        }

        const bool vertexRangeValid =
            record.vertexOffset >= 0 &&
            record.vertexCount >= 0 &&
            record.vertexOffset + record.vertexCount <= stats.staticVerts;
        const bool indexRangeValid =
            record.indexOffset >= 0 &&
            record.indexCount >= 0 &&
            record.indexOffset + record.indexCount <= stats.staticIndexes;
        const bool triangleRangeValid =
            record.triangleOffset >= 0 &&
            record.triangleCount >= 0 &&
            record.triangleOffset + record.triangleCount <= stats.staticTriangles;
        const bool materialRangeValid =
            record.triangleOffset >= 0 &&
            record.triangleCount >= 0 &&
            record.triangleOffset + record.triangleCount <= static_cast<int>(m_staticTriangleMaterialCache.size());
        if (!vertexRangeValid || !indexRangeValid || !triangleRangeValid || !materialRangeValid ||
            record.indexCount != record.triangleCount * 3)
        {
            ++stats.staticRangeErrors;
        }
        if (record.historyValid && !record.previousRangeValid)
        {
            ++stats.staticHistoryErrors;
        }
        if (record.previousRangeValid)
        {
            const bool previousRangeValid =
                record.previousVertexOffset >= 0 &&
                record.previousVertexCount >= 0 &&
                record.previousIndexOffset >= 0 &&
                record.previousIndexCount >= 0 &&
                record.previousTriangleOffset >= 0 &&
                record.previousTriangleCount >= 0 &&
                record.previousVertexOffset + record.previousVertexCount <= stats.staticVerts &&
                record.previousIndexOffset + record.previousIndexCount <= stats.staticIndexes &&
                record.previousTriangleOffset + record.previousTriangleCount <= stats.staticTriangles &&
                record.previousTriangleOffset + record.previousTriangleCount <= static_cast<int>(m_staticTriangleMaterialCache.size()) &&
                record.previousIndexCount == record.previousTriangleCount * 3;
            if (!previousRangeValid)
            {
                ++stats.staticHistoryErrors;
            }
        }
        if (recordIndex >= m_staticSurfaceKeys.size() || m_staticSurfaceKeys[recordIndex] != record.key)
        {
            ++stats.staticKeyVectorMismatches;
        }
        for (size_t otherIndex = recordIndex + 1; otherIndex < m_staticSurfaceRecords.size(); ++otherIndex)
        {
            const RtSmokePersistentStaticSurfaceRecord& otherRecord = m_staticSurfaceRecords[otherIndex];
            if (otherRecord.valid && otherRecord.key == record.key)
            {
                ++stats.staticDuplicateKeys;
                break;
            }
        }
    }
    stats.staticValidationErrors =
        stats.staticRangeErrors +
        stats.staticDuplicateKeys +
        stats.staticHistoryErrors +
        stats.staticKeyVectorMismatches;
    const size_t staticBytes =
        m_staticSurfaceRecords.size() * sizeof(m_staticSurfaceRecords[0]) +
        m_staticSurfaceKeys.size() * sizeof(m_staticSurfaceKeys[0]) +
        m_staticVertexCache.size() * sizeof(m_staticVertexCache[0]) +
        m_staticIndexCache.size() * sizeof(m_staticIndexCache[0]) +
        m_staticTriangleClassCache.size() * sizeof(m_staticTriangleClassCache[0]) +
        m_staticTriangleMaterialCache.size() * sizeof(m_staticTriangleMaterialCache[0]);
    stats.staticBytesKB = static_cast<int>((staticBytes + 1023) / 1024);
    stats.frameIndex = m_currentFrameIndex;
    stats.generation = m_generation;
    return stats;
}
