#include "precompiled.h"
#pragma hdrstop

#include "PathTraceGeometryUniverse.h"

namespace {

bool IsSmokeGeometryRangeValid(const RtSmokeGeometryRangeRecord& range, int vertexCount, int indexCount, int triangleCount, int materialTriangleCount)
{
    return
        range.vertices.offset >= 0 &&
        range.vertices.count >= 0 &&
        range.vertices.offset + range.vertices.count <= vertexCount &&
        range.indexes.offset >= 0 &&
        range.indexes.count >= 0 &&
        range.indexes.offset + range.indexes.count <= indexCount &&
        range.triangles.offset >= 0 &&
        range.triangles.count >= 0 &&
        range.triangles.offset + range.triangles.count <= triangleCount &&
        range.triangles.offset + range.triangles.count <= materialTriangleCount &&
        range.indexes.count == range.triangles.count * 3;
}

bool SmokeGeometryRangesMatchCounts(const RtSmokeGeometryRangeRecord& a, const RtSmokeGeometryRangeRecord& b)
{
    return
        a.vertices.count == b.vertices.count &&
        a.indexes.count == b.indexes.count &&
        a.triangles.count == b.triangles.count;
}

bool SmokeGeometryRangeHasOffsets(const RtSmokeGeometryRangeRecord& range)
{
    return
        range.vertices.offset >= 0 &&
        range.indexes.offset >= 0 &&
        range.triangles.offset >= 0;
}

bool SmokeGeometryRecordHasDuplicateKey(const std::vector<RtSmokePersistentStaticSurfaceRecord>& records, size_t recordIndex)
{
    if (recordIndex >= records.size() || !records[recordIndex].valid)
    {
        return false;
    }

    const uint64 key = records[recordIndex].key;
    for (size_t otherIndex = 0; otherIndex < records.size(); ++otherIndex)
    {
        if (otherIndex != recordIndex && records[otherIndex].valid && records[otherIndex].key == key)
        {
            return true;
        }
    }
    return false;
}

void PrintSmokeGeometryRange(const char* label, const RtSmokeGeometryRangeRecord& range)
{
    common->Printf("%s v=%d/%d i=%d/%d t=%d/%d",
        label ? label : "range",
        range.vertices.offset,
        range.vertices.count,
        range.indexes.offset,
        range.indexes.count,
        range.triangles.offset,
        range.triangles.count);
}

}

void RtSmokeGeometryUniverse::Clear()
{
    m_currentFrameIndex = 0;
    m_frameActive = false;
    m_staticSurfaceRecords.clear();
    m_staticSurfaceLookup.clear();
    m_staticSurfaceKeys.clear();
    m_staticVertexCache.clear();
    m_staticIndexCache.clear();
    m_staticTriangleClassCache.clear();
    m_staticTriangleMaterialCache.clear();
    ++m_generation;
}

void RtSmokeGeometryUniverse::ReserveStaticSurfaceRecords(size_t surfaceCount)
{
    if (surfaceCount <= m_staticSurfaceRecords.capacity())
    {
        return;
    }

    m_staticSurfaceRecords.reserve(surfaceCount);
    m_staticSurfaceLookup.reserve(surfaceCount);
    m_staticSurfaceKeys.reserve(surfaceCount);
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
            record->previousRange = record->currentRange;
        }
        record->previousRangeValid =
            consecutiveFrame &&
            SmokeGeometryRangeHasOffsets(record->previousRange) &&
            SmokeGeometryRangesMatchCounts(record->previousRange, record->currentRange);
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
    record.currentRange.vertices.offset = append.vertexOffset;
    record.currentRange.vertices.count = static_cast<int>(m_staticVertexCache.size()) - append.vertexOffset;
    record.currentRange.indexes.offset = append.indexOffset;
    record.currentRange.indexes.count = emittedIndexCount;
    record.currentRange.triangles.offset = append.triangleOffset;
    record.currentRange.triangles.count = emittedIndexCount / 3;
    record.lastSeenFrame = m_currentFrameIndex;
    record.previousSeenFrame = 0;
    record.seenThisFrame = true;
    record.newlyCreatedThisFrame = true;
    record.disappearedThisFrame = false;
    record.previousRangeValid = false;
    record.historyValid = false;
    record.dirty = true;
    record.geometryFormat = RtSmokeGeometryBufferFormat::LegacySmokeVertex;

    const size_t recordIndex = m_staticSurfaceRecords.size();
    m_staticSurfaceRecords.push_back(record);
    m_staticSurfaceLookup[append.key] = recordIndex;
    m_staticSurfaceKeys.push_back(append.key);
    ++m_generation;
}

const RtSmokePersistentStaticSurfaceRecord* RtSmokeGeometryUniverse::FindStaticSurface(uint64 key) const
{
    const std::unordered_map<uint64, size_t>::const_iterator it = m_staticSurfaceLookup.find(key);
    if (it == m_staticSurfaceLookup.end() || it->second >= m_staticSurfaceRecords.size())
    {
        return nullptr;
    }

    const RtSmokePersistentStaticSurfaceRecord& record = m_staticSurfaceRecords[it->second];
    return record.valid && record.key == key ? &record : nullptr;
}

RtSmokePersistentStaticSurfaceRecord* RtSmokeGeometryUniverse::FindStaticSurfaceMutable(uint64 key)
{
    const std::unordered_map<uint64, size_t>::const_iterator it = m_staticSurfaceLookup.find(key);
    if (it == m_staticSurfaceLookup.end() || it->second >= m_staticSurfaceRecords.size())
    {
        return nullptr;
    }

    RtSmokePersistentStaticSurfaceRecord& record = m_staticSurfaceRecords[it->second];
    return record.valid && record.key == key ? &record : nullptr;
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

RtSmokeGeometryUniverseStats RtSmokeGeometryUniverse::GetStats(bool validateRecords) const
{
    RtSmokeGeometryUniverseStats stats;
    stats.staticRecords = static_cast<int>(m_staticSurfaceRecords.size());
    stats.staticSurfaces = static_cast<int>(m_staticSurfaceKeys.size());
    stats.staticVerts = static_cast<int>(m_staticVertexCache.size());
    stats.staticIndexes = static_cast<int>(m_staticIndexCache.size());
    stats.staticTriangles = static_cast<int>(m_staticTriangleClassCache.size());
    if (validateRecords && m_staticSurfaceKeys.size() != m_staticSurfaceRecords.size())
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

        if (validateRecords)
        {
            if (!IsSmokeGeometryRangeValid(record.currentRange, stats.staticVerts, stats.staticIndexes, stats.staticTriangles, static_cast<int>(m_staticTriangleMaterialCache.size())))
            {
                ++stats.staticRangeErrors;
            }
            if (record.historyValid && !record.previousRangeValid)
            {
                ++stats.staticHistoryErrors;
            }
            if (record.previousRangeValid)
            {
                if (!IsSmokeGeometryRangeValid(record.previousRange, stats.staticVerts, stats.staticIndexes, stats.staticTriangles, static_cast<int>(m_staticTriangleMaterialCache.size())))
                {
                    ++stats.staticHistoryErrors;
                }
            }
            if (recordIndex >= m_staticSurfaceKeys.size() || m_staticSurfaceKeys[recordIndex] != record.key)
            {
                ++stats.staticKeyVectorMismatches;
            }
            const std::unordered_map<uint64, size_t>::const_iterator lookupIt = m_staticSurfaceLookup.find(record.key);
            if (lookupIt == m_staticSurfaceLookup.end() || lookupIt->second != recordIndex)
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

void RtSmokeGeometryUniverse::LogStaticValidationFailures(int maxRecords) const
{
    const int recordLimit = maxRecords > 0 ? maxRecords : 0;
    const int vertexCount = static_cast<int>(m_staticVertexCache.size());
    const int indexCount = static_cast<int>(m_staticIndexCache.size());
    const int triangleCount = static_cast<int>(m_staticTriangleClassCache.size());
    const int materialTriangleCount = static_cast<int>(m_staticTriangleMaterialCache.size());
    const bool keyVectorSizeMismatch = m_staticSurfaceKeys.size() != m_staticSurfaceRecords.size();

    common->Printf("PathTracePrimaryPass: RT smoke geometry universe validation failure dump frame=%llu generation=%llu records=%d keys=%d verts=%d indexes=%d triangles=%d materialTriangles=%d maxRecords=%d\n",
        static_cast<unsigned long long>(m_currentFrameIndex),
        static_cast<unsigned long long>(m_generation),
        static_cast<int>(m_staticSurfaceRecords.size()),
        static_cast<int>(m_staticSurfaceKeys.size()),
        vertexCount,
        indexCount,
        triangleCount,
        materialTriangleCount,
        recordLimit);

    int logged = 0;
    int failedRecords = 0;
    for (size_t recordIndex = 0; recordIndex < m_staticSurfaceRecords.size(); ++recordIndex)
    {
        const RtSmokePersistentStaticSurfaceRecord& record = m_staticSurfaceRecords[recordIndex];
        if (!record.valid)
        {
            continue;
        }

        const bool currentRangeValid = IsSmokeGeometryRangeValid(record.currentRange, vertexCount, indexCount, triangleCount, materialTriangleCount);
        const bool previousRangeValid =
            !record.previousRangeValid ||
            IsSmokeGeometryRangeValid(record.previousRange, vertexCount, indexCount, triangleCount, materialTriangleCount);
        const bool historyValid = !record.historyValid || record.previousRangeValid;
        const bool keyVectorValid =
            recordIndex < m_staticSurfaceKeys.size() &&
            m_staticSurfaceKeys[recordIndex] == record.key;
        const std::unordered_map<uint64, size_t>::const_iterator lookupIt = m_staticSurfaceLookup.find(record.key);
        const bool lookupValid =
            lookupIt != m_staticSurfaceLookup.end() &&
            lookupIt->second == recordIndex;
        const bool duplicateKey = SmokeGeometryRecordHasDuplicateKey(m_staticSurfaceRecords, recordIndex);
        const bool failed =
            !currentRangeValid ||
            !previousRangeValid ||
            !historyValid ||
            !keyVectorValid ||
            !lookupValid ||
            duplicateKey;
        if (!failed)
        {
            continue;
        }

        ++failedRecords;
        if (logged >= recordLimit)
        {
            continue;
        }

        common->Printf("PathTracePrimaryPass: RT smoke geometry record index=%d key=%llu class=%u material=%u flags seen/new/gone/hist/prev/dirty=%d/%d/%d/%d/%d/%d valid current/history/keyVec/lookup/dup=%d/%d/%d/%d/%d frame last/prev=%llu/%llu format=%u ",
            static_cast<int>(recordIndex),
            static_cast<unsigned long long>(record.key),
            record.surfaceClassId,
            record.materialId,
            record.seenThisFrame ? 1 : 0,
            record.newlyCreatedThisFrame ? 1 : 0,
            record.disappearedThisFrame ? 1 : 0,
            record.historyValid ? 1 : 0,
            record.previousRangeValid ? 1 : 0,
            record.dirty ? 1 : 0,
            currentRangeValid ? 1 : 0,
            (previousRangeValid && historyValid) ? 1 : 0,
            keyVectorValid ? 1 : 0,
            lookupValid ? 1 : 0,
            duplicateKey ? 1 : 0,
            static_cast<unsigned long long>(record.lastSeenFrame),
            static_cast<unsigned long long>(record.previousSeenFrame),
            static_cast<uint32_t>(record.geometryFormat));
        PrintSmokeGeometryRange("current", record.currentRange);
        common->Printf(" ");
        PrintSmokeGeometryRange("previous", record.previousRange);
        common->Printf("\n");
        ++logged;
    }

    if (keyVectorSizeMismatch && logged < recordLimit)
    {
        common->Printf("PathTracePrimaryPass: RT smoke geometry key-vector size mismatch records=%d keys=%d\n",
            static_cast<int>(m_staticSurfaceRecords.size()),
            static_cast<int>(m_staticSurfaceKeys.size()));
    }

    common->Printf("PathTracePrimaryPass: RT smoke geometry universe validation failure dump logged=%d failedRecords=%d keyVectorSizeMismatch=%d\n",
        logged,
        failedRecords,
        keyVectorSizeMismatch ? 1 : 0);
}
