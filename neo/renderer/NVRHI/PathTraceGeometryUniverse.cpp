#include "precompiled.h"
#pragma hdrstop

#include "PathTraceGeometryUniverse.h"
#include "PathTraceInstanceUniverse.h"
#include "PathTraceCVars.h"
#include "PathTraceAcceleration.h"

#include <algorithm>
#include <nvrhi/utils.h>

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

uint32_t BuildRigidMeshCandidateRejectFlags(const RtPathTraceRigidMeshCandidateObservation& observation)
{
    uint32_t rejectFlags = 0;
    if ((observation.sourceFlags & RT_PT_INSTANCE_SOURCE_RIGID) == 0)
    {
        rejectFlags |= RT_PT_RIGID_MESH_REJECT_NOT_RIGID;
    }
    if (observation.numVerts <= 0 || observation.numIndexes <= 0 || (observation.numIndexes % 3) != 0 || observation.meshHash == 0)
    {
        rejectFlags |= RT_PT_RIGID_MESH_REJECT_INVALID_GEOMETRY;
    }
    if (observation.tri == nullptr)
    {
        rejectFlags |= RT_PT_RIGID_MESH_REJECT_INVALID_GEOMETRY;
    }
    if (observation.materialId == 0 || observation.materialName.IsEmpty() || observation.materialName.Icmp("<none>") == 0)
    {
        rejectFlags |= RT_PT_RIGID_MESH_REJECT_MISSING_MATERIAL;
    }
    if (!observation.localSpaceValid)
    {
        rejectFlags |= RT_PT_RIGID_MESH_REJECT_NO_LOCAL_SPACE;
    }
    if ((observation.sourceFlags & RT_PT_INSTANCE_SOURCE_SKINNED_OR_DEFORMING) != 0)
    {
        rejectFlags |= RT_PT_RIGID_MESH_REJECT_SKINNED_OR_DEFORMING;
    }
    if ((observation.sourceFlags & RT_PT_INSTANCE_SOURCE_PARTICLE_OR_TRANSIENT) != 0)
    {
        rejectFlags |= RT_PT_RIGID_MESH_REJECT_PARTICLE_OR_TRANSIENT;
    }
    if ((observation.sourceFlags & RT_PT_INSTANCE_SOURCE_GUI) != 0)
    {
        rejectFlags |= RT_PT_RIGID_MESH_REJECT_GUI;
    }
    if ((observation.sourceFlags & RT_PT_INSTANCE_SOURCE_CALLBACK_OR_GENERATED) != 0)
    {
        rejectFlags |= RT_PT_RIGID_MESH_REJECT_CALLBACK_OR_GENERATED;
    }
    if ((observation.sourceFlags & RT_PT_INSTANCE_SOURCE_STATIC_WORLD) != 0)
    {
        rejectFlags |= RT_PT_RIGID_MESH_REJECT_STATIC_WORLD;
    }
    if ((observation.sourceFlags & RT_PT_INSTANCE_SOURCE_STATIC_CACHE_MATCH) != 0)
    {
        rejectFlags |= RT_PT_RIGID_MESH_REJECT_STATIC_CACHE_MATCH;
    }
    return rejectFlags;
}

void AccumulateRigidMeshCandidateRejectStats(RtPathTraceRigidMeshCandidateStats& stats, uint32_t rejectFlags)
{
    if ((rejectFlags & RT_PT_RIGID_MESH_REJECT_NOT_RIGID) != 0)
    {
        ++stats.rejectNotRigid;
    }
    if ((rejectFlags & RT_PT_RIGID_MESH_REJECT_INVALID_GEOMETRY) != 0)
    {
        ++stats.rejectInvalidGeometry;
    }
    if ((rejectFlags & RT_PT_RIGID_MESH_REJECT_MISSING_MATERIAL) != 0)
    {
        ++stats.rejectMissingMaterial;
    }
    if ((rejectFlags & RT_PT_RIGID_MESH_REJECT_NO_LOCAL_SPACE) != 0)
    {
        ++stats.rejectNoLocalSpace;
    }
    if ((rejectFlags & RT_PT_RIGID_MESH_REJECT_SKINNED_OR_DEFORMING) != 0)
    {
        ++stats.rejectSkinnedOrDeforming;
    }
    if ((rejectFlags & RT_PT_RIGID_MESH_REJECT_PARTICLE_OR_TRANSIENT) != 0)
    {
        ++stats.rejectParticleOrTransient;
    }
    if ((rejectFlags & RT_PT_RIGID_MESH_REJECT_GUI) != 0)
    {
        ++stats.rejectGui;
    }
    if ((rejectFlags & RT_PT_RIGID_MESH_REJECT_CALLBACK_OR_GENERATED) != 0)
    {
        ++stats.rejectCallbackOrGenerated;
    }
    if ((rejectFlags & RT_PT_RIGID_MESH_REJECT_STATIC_WORLD) != 0)
    {
        ++stats.rejectStaticWorld;
    }
    if ((rejectFlags & RT_PT_RIGID_MESH_REJECT_STATIC_CACHE_MATCH) != 0)
    {
        ++stats.rejectStaticCacheMatch;
    }
}

const char* RigidMeshCandidateRejectSummary(uint32_t rejectFlags)
{
    if ((rejectFlags & RT_PT_RIGID_MESH_REJECT_NOT_RIGID) != 0)
    {
        return "not-rigid";
    }
    if ((rejectFlags & RT_PT_RIGID_MESH_REJECT_INVALID_GEOMETRY) != 0)
    {
        return "invalid-geometry";
    }
    if ((rejectFlags & RT_PT_RIGID_MESH_REJECT_MISSING_MATERIAL) != 0)
    {
        return "missing-material";
    }
    if ((rejectFlags & RT_PT_RIGID_MESH_REJECT_NO_LOCAL_SPACE) != 0)
    {
        return "no-local-space";
    }
    if ((rejectFlags & RT_PT_RIGID_MESH_REJECT_SKINNED_OR_DEFORMING) != 0)
    {
        return "skinned-or-deforming";
    }
    if ((rejectFlags & RT_PT_RIGID_MESH_REJECT_PARTICLE_OR_TRANSIENT) != 0)
    {
        return "particle-or-transient";
    }
    if ((rejectFlags & RT_PT_RIGID_MESH_REJECT_GUI) != 0)
    {
        return "gui";
    }
    if ((rejectFlags & RT_PT_RIGID_MESH_REJECT_CALLBACK_OR_GENERATED) != 0)
    {
        return "callback-or-generated";
    }
    if ((rejectFlags & RT_PT_RIGID_MESH_REJECT_STATIC_WORLD) != 0)
    {
        return "static-world";
    }
    if ((rejectFlags & RT_PT_RIGID_MESH_REJECT_STATIC_CACHE_MATCH) != 0)
    {
        return "static-cache-match";
    }
    return "eligible";
}

size_t RigidSmokeBufferRequiredBytes(size_t byteSize, uint32_t structStride)
{
    return byteSize > structStride ? byteSize : structStride;
}

bool RigidSmokeBufferHasCapacity(nvrhi::BufferHandle buffer, size_t byteSize, uint32_t structStride)
{
    return buffer && buffer->getDesc().byteSize >= RigidSmokeBufferRequiredBytes(byteSize, structStride);
}

nvrhi::BufferHandle CreateRigidSmokeBuffer(nvrhi::IDevice* device, const char* debugName, size_t byteSize, uint32_t structStride, bool vertexBuffer, bool indexBuffer)
{
    if (!device)
    {
        return nullptr;
    }

    nvrhi::BufferDesc desc;
    desc.byteSize = RigidSmokeBufferRequiredBytes(byteSize, structStride);
    desc.debugName = debugName;
    desc.structStride = structStride;
    desc.isVertexBuffer = vertexBuffer;
    desc.isIndexBuffer = indexBuffer;
    desc.isAccelStructBuildInput = true;
    desc.initialState = nvrhi::ResourceStates::Common;
    desc.keepInitialState = true;
    return device->createBuffer(desc);
}

uint64 BuildRigidGpuUploadSignature(const RtSmokeGeometryUniverse::RigidMeshCandidateRecord& record)
{
    uint64 hash = 14695981039346656037ull;
    const uintptr_t triIdentity = reinterpret_cast<uintptr_t>(record.tri);
    hash = HashSmokeBytes(hash, &record.meshHash, sizeof(record.meshHash));
    hash = HashSmokeBytes(hash, &triIdentity, sizeof(triIdentity));
    hash = HashSmokeBytes(hash, &record.vertexBufferIdentity, sizeof(record.vertexBufferIdentity));
    hash = HashSmokeBytes(hash, &record.indexBufferIdentity, sizeof(record.indexBufferIdentity));
    hash = HashSmokeBytes(hash, &record.materialId, sizeof(record.materialId));
    hash = HashSmokeBytes(hash, &record.sourceRange.vertices.count, sizeof(record.sourceRange.vertices.count));
    hash = HashSmokeBytes(hash, &record.sourceRange.indexes.count, sizeof(record.sourceRange.indexes.count));
    return hash;
}

uint32_t ValidateRigidBlasInputRecord(const RtSmokeGeometryUniverse::RigidMeshCandidateRecord& record)
{
    const uint32_t expectedVertexFormat = static_cast<uint32_t>(RtSmokeGeometryBufferFormat::LegacySmokeVertex);
    uint32_t invalidFlags = 0;
    if (record.tri == nullptr)
    {
        invalidFlags |= RT_PT_RIGID_BLAS_INPUT_INVALID_NULL_TRI;
    }
    if (record.sourceRange.vertices.count <= 0)
    {
        invalidFlags |= RT_PT_RIGID_BLAS_INPUT_INVALID_VERTEX_COUNT;
    }
    if (record.sourceRange.indexes.count <= 0 || (record.sourceRange.indexes.count % 3) != 0)
    {
        invalidFlags |= RT_PT_RIGID_BLAS_INPUT_INVALID_INDEX_COUNT;
    }
    if (record.sourceRange.triangles.count <= 0 || record.sourceRange.triangles.count * 3 != record.sourceRange.indexes.count)
    {
        invalidFlags |= RT_PT_RIGID_BLAS_INPUT_INVALID_TRIANGLE_COUNT;
    }
    if (record.vertexFormat != expectedVertexFormat)
    {
        invalidFlags |= RT_PT_RIGID_BLAS_INPUT_INVALID_VERTEX_FORMAT;
    }
    if (record.vertexBufferIdentity == 0 || record.indexBufferIdentity == 0)
    {
        invalidFlags |= RT_PT_RIGID_BLAS_INPUT_INVALID_MISSING_SOURCE_IDENTITY;
    }
    if (record.materialId == 0)
    {
        invalidFlags |= RT_PT_RIGID_BLAS_INPUT_INVALID_MATERIAL;
    }
    if (record.tri)
    {
        if (!record.tri->verts || record.tri->numVerts < record.sourceRange.vertices.count)
        {
            invalidFlags |= RT_PT_RIGID_BLAS_INPUT_INVALID_VERTEX_COUNT;
        }
        if (!record.tri->indexes || record.tri->numIndexes < record.sourceRange.indexes.count)
        {
            invalidFlags |= RT_PT_RIGID_BLAS_INPUT_INVALID_INDEX_COUNT;
        }
    }
    return invalidFlags;
}

PathTraceSmokeVertex BuildRigidLocalSmokeVertex(const idDrawVert& drawVert)
{
    idVec3 localNormal = drawVert.GetNormal();
    if (localNormal.Normalize() == 0.0f)
    {
        localNormal.Set(0.0f, 0.0f, 1.0f);
    }

    const idVec2 texCoord = drawVert.GetTexCoord();
    PathTraceSmokeVertex vertex = {};
    vertex.position[0] = drawVert.xyz.x;
    vertex.position[1] = drawVert.xyz.y;
    vertex.position[2] = drawVert.xyz.z;
    vertex.position[3] = 1.0f;
    vertex.normal[0] = localNormal.x;
    vertex.normal[1] = localNormal.y;
    vertex.normal[2] = localNormal.z;
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

bool BuildRigidLocalMeshData(const RtSmokeGeometryUniverse::RigidMeshCandidateRecord& record, std::vector<PathTraceSmokeVertex>& vertices, std::vector<uint32_t>& indexes)
{
    if (ValidateRigidBlasInputRecord(record) != 0)
    {
        return false;
    }

    vertices.resize(record.sourceRange.vertices.count);
    for (int vertexIndex = 0; vertexIndex < record.sourceRange.vertices.count; ++vertexIndex)
    {
        vertices[vertexIndex] = BuildRigidLocalSmokeVertex(record.tri->verts[vertexIndex]);
    }

    indexes.resize(record.sourceRange.indexes.count);
    for (int indexIndex = 0; indexIndex < record.sourceRange.indexes.count; ++indexIndex)
    {
        const int sourceIndex = static_cast<int>(record.tri->indexes[indexIndex]);
        if (sourceIndex < 0 || sourceIndex >= record.sourceRange.vertices.count)
        {
            return false;
        }
        indexes[indexIndex] = static_cast<uint32_t>(sourceIndex);
    }
    return true;
}

uint32_t FindRigidRouteMaterialTableIndex(const std::vector<uint32_t>& materialTableIds, uint32_t materialId, int& missingCount)
{
    for (size_t materialIndex = 0; materialIndex < materialTableIds.size(); ++materialIndex)
    {
        if (materialTableIds[materialIndex] == materialId)
        {
            return static_cast<uint32_t>(materialIndex);
        }
    }
    ++missingCount;
    return 0;
}

void BuildRigidTlasAffineTransform(const float objectToWorld[16], nvrhi::rt::AffineTransform& transform)
{
    transform[0] = objectToWorld[0];
    transform[1] = objectToWorld[4];
    transform[2] = objectToWorld[8];
    transform[3] = objectToWorld[12];
    transform[4] = objectToWorld[1];
    transform[5] = objectToWorld[5];
    transform[6] = objectToWorld[9];
    transform[7] = objectToWorld[13];
    transform[8] = objectToWorld[2];
    transform[9] = objectToWorld[6];
    transform[10] = objectToWorld[10];
    transform[11] = objectToWorld[14];
}

std::vector<uint32_t> UniqueSortedMaterialIds(std::vector<uint32_t> materialIds)
{
    std::sort(materialIds.begin(), materialIds.end());
    materialIds.erase(std::unique(materialIds.begin(), materialIds.end()), materialIds.end());
    return materialIds;
}

void AddMaterialSample(uint32_t* samples, int& sampleCount, uint32_t materialId)
{
    if (sampleCount >= 8)
    {
        return;
    }
    samples[sampleCount++] = materialId;
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
    m_rigidMeshCandidateRecords.clear();
    m_rigidMeshCandidateLookup.clear();
    m_frameRigidMeshCandidateHashes.clear();
    ResetRigidMeshCandidateFrameStats();
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
    ResetRigidMeshCandidateFrameStats();
    m_rigidMeshCandidateFrameStats.frameIndex = frameIndex;
    m_rigidMeshCandidateFrameStats.generation = m_generation;
    m_frameRigidMeshCandidateHashes.clear();
    for (RtSmokePersistentStaticSurfaceRecord& record : m_staticSurfaceRecords)
    {
        record.seenThisFrame = false;
        record.newlyCreatedThisFrame = false;
        record.disappearedThisFrame = false;
    }
    for (RigidMeshCandidateRecord& record : m_rigidMeshCandidateRecords)
    {
        record.seenThisFrame = false;
        record.newlyCreatedThisFrame = false;
        record.instanceCountThisFrame = 0;
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

    m_rigidMeshCandidateFrameStats.eligibleUniqueMeshes = static_cast<int>(m_frameRigidMeshCandidateHashes.size());
    m_rigidMeshCandidateFrameStats.persistentEligibleMeshes = static_cast<int>(m_rigidMeshCandidateRecords.size());
    m_rigidMeshCandidateFrameStats.localMeshSourceRecords = static_cast<int>(m_rigidMeshCandidateRecords.size());
    for (const RigidMeshCandidateRecord& record : m_rigidMeshCandidateRecords)
    {
        if (!record.valid)
        {
            continue;
        }
        m_rigidMeshCandidateFrameStats.localMeshSourceVerts += record.sourceRange.vertices.count;
        m_rigidMeshCandidateFrameStats.localMeshSourceIndexes += record.sourceRange.indexes.count;
        m_rigidMeshCandidateFrameStats.localMeshSourceTriangles += record.sourceRange.triangles.count;
        if (record.seenThisFrame)
        {
            ++m_rigidMeshCandidateFrameStats.localMeshSourceRecordsSeenThisFrame;
        }
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

void RtSmokeGeometryUniverse::RecordRigidMeshCandidate(const RtPathTraceRigidMeshCandidateObservation& observation)
{
    ++m_rigidMeshCandidateFrameStats.observations;
    if ((observation.sourceFlags & RT_PT_INSTANCE_SOURCE_RIGID) != 0)
    {
        ++m_rigidMeshCandidateFrameStats.rigidObservations;
    }

    const uint32_t rejectFlags = BuildRigidMeshCandidateRejectFlags(observation);
    if (rejectFlags != 0)
    {
        ++m_rigidMeshCandidateFrameStats.rejectedInstances;
        AccumulateRigidMeshCandidateRejectStats(m_rigidMeshCandidateFrameStats, rejectFlags);
        AddRigidMeshCandidateSample(observation, false, rejectFlags, 0);
        return;
    }

    bool cacheHit = false;
    RigidMeshCandidateRecord* record = FindOrCreateRigidMeshCandidate(observation, cacheHit);
    int seenCount = 0;
    if (record)
    {
        if (!record->seenThisFrame)
        {
            record->seenThisFrame = true;
            record->lastSeenFrame = static_cast<int>(m_currentFrameIndex);
            record->newlyCreatedThisFrame = !cacheHit;
        }
        ++record->seenCount;
        ++record->instanceCountThisFrame;
        seenCount = record->seenCount;
    }

    ++m_rigidMeshCandidateFrameStats.eligibleInstances;
    m_rigidMeshCandidateFrameStats.eligibleVertsThisFrame += observation.numVerts;
    m_rigidMeshCandidateFrameStats.eligibleIndexesThisFrame += observation.numIndexes;
    m_rigidMeshCandidateFrameStats.eligibleTrianglesThisFrame += observation.numIndexes / 3;
    if ((observation.sourceFlags & RT_PT_INSTANCE_SOURCE_MATERIAL_OVERRIDE) != 0)
    {
        ++m_rigidMeshCandidateFrameStats.materialOverrideEligibleInstances;
    }
    if (cacheHit)
    {
        ++m_rigidMeshCandidateFrameStats.reusedEligibleMeshObservations;
    }
    else
    {
        ++m_rigidMeshCandidateFrameStats.newlyEligibleMeshes;
    }
    m_frameRigidMeshCandidateHashes.insert(observation.meshHash);
    AddRigidMeshCandidateSample(observation, true, 0, seenCount);
}

const RtPathTraceRigidMeshCandidateStats& RtSmokeGeometryUniverse::GetRigidMeshCandidateStats() const
{
    return m_rigidMeshCandidateFrameStats;
}

void RtSmokeGeometryUniverse::RunRigidMeshCandidateDiagnostics(bool dumpRequested, int sceneSource, const RtSmokeSurfaceClassStats* sourceClassStats)
{
    const int sourceRigidTriangles = sourceClassStats ? sourceClassStats->rigidEntityTriangles : 0;
    const int sourceRigidSurfaces = sourceClassStats ? sourceClassStats->rigidEntitySurfaces : 0;
    const int estimatedRemainingRigidTriangles = sourceClassStats ? Max(0, sourceRigidTriangles - m_rigidMeshCandidateFrameStats.eligibleTrianglesThisFrame) : 0;
    if (r_pathTracingSmokeLog.GetInteger() != 0 && (m_currentFrameIndex % 120ull) == 1ull)
    {
        common->Printf("PathTracePrimaryPass: PT rigid mesh candidates source=%d observed=%d rigid=%d eligibleInstances=%d uniqueMeshes=%d localRecords=%d/%d eligibleTris=%d bakedRigid=%d/%d remainingAfterPromotion=%d rejected=%d reused=%d newMeshes=%d overrides=%d rejects nonRigid/geom/material/local/skinned/transient/gui/callback/static/cache=%d/%d/%d/%d/%d/%d/%d/%d/%d/%d\n",
            sceneSource,
            m_rigidMeshCandidateFrameStats.observations,
            m_rigidMeshCandidateFrameStats.rigidObservations,
            m_rigidMeshCandidateFrameStats.eligibleInstances,
            m_rigidMeshCandidateFrameStats.eligibleUniqueMeshes,
            m_rigidMeshCandidateFrameStats.localMeshSourceRecordsSeenThisFrame,
            m_rigidMeshCandidateFrameStats.localMeshSourceRecords,
            m_rigidMeshCandidateFrameStats.eligibleTrianglesThisFrame,
            sourceRigidSurfaces,
            sourceRigidTriangles,
            estimatedRemainingRigidTriangles,
            m_rigidMeshCandidateFrameStats.rejectedInstances,
            m_rigidMeshCandidateFrameStats.reusedEligibleMeshObservations,
            m_rigidMeshCandidateFrameStats.newlyEligibleMeshes,
            m_rigidMeshCandidateFrameStats.materialOverrideEligibleInstances,
            m_rigidMeshCandidateFrameStats.rejectNotRigid,
            m_rigidMeshCandidateFrameStats.rejectInvalidGeometry,
            m_rigidMeshCandidateFrameStats.rejectMissingMaterial,
            m_rigidMeshCandidateFrameStats.rejectNoLocalSpace,
            m_rigidMeshCandidateFrameStats.rejectSkinnedOrDeforming,
            m_rigidMeshCandidateFrameStats.rejectParticleOrTransient,
            m_rigidMeshCandidateFrameStats.rejectGui,
            m_rigidMeshCandidateFrameStats.rejectCallbackOrGenerated,
            m_rigidMeshCandidateFrameStats.rejectStaticWorld,
            m_rigidMeshCandidateFrameStats.rejectStaticCacheMatch);
    }

    if (!dumpRequested)
    {
        return;
    }

    common->Printf("PathTracePrimaryPass: PT rigid mesh universe dump source=%d frame=%llu generation=%llu observed=%d rigid=%d eligibleInstances=%d eligibleUniqueMeshes=%d persistentMeshes=%d localRecords(seen/total)=%d/%d rejected=%d reused=%d newMeshes=%d overrides=%d\n",
        sceneSource,
        static_cast<unsigned long long>(m_rigidMeshCandidateFrameStats.frameIndex),
        static_cast<unsigned long long>(m_rigidMeshCandidateFrameStats.generation),
        m_rigidMeshCandidateFrameStats.observations,
        m_rigidMeshCandidateFrameStats.rigidObservations,
        m_rigidMeshCandidateFrameStats.eligibleInstances,
        m_rigidMeshCandidateFrameStats.eligibleUniqueMeshes,
        m_rigidMeshCandidateFrameStats.persistentEligibleMeshes,
        m_rigidMeshCandidateFrameStats.localMeshSourceRecordsSeenThisFrame,
        m_rigidMeshCandidateFrameStats.localMeshSourceRecords,
        m_rigidMeshCandidateFrameStats.rejectedInstances,
        m_rigidMeshCandidateFrameStats.reusedEligibleMeshObservations,
        m_rigidMeshCandidateFrameStats.newlyEligibleMeshes,
        m_rigidMeshCandidateFrameStats.materialOverrideEligibleInstances);
    common->Printf("PathTracePrimaryPass: PT rigid mesh universe localSource frameVerts/indexes/tris=%d/%d/%d persistentVerts/indexes/tris=%d/%d/%d bakedRigidSurfaces/tris=%d/%d estimatedRigidTrisAfterPromotion=%d renderPath=dynamicFallback\n",
        m_rigidMeshCandidateFrameStats.eligibleVertsThisFrame,
        m_rigidMeshCandidateFrameStats.eligibleIndexesThisFrame,
        m_rigidMeshCandidateFrameStats.eligibleTrianglesThisFrame,
        m_rigidMeshCandidateFrameStats.localMeshSourceVerts,
        m_rigidMeshCandidateFrameStats.localMeshSourceIndexes,
        m_rigidMeshCandidateFrameStats.localMeshSourceTriangles,
        sourceRigidSurfaces,
        sourceRigidTriangles,
        estimatedRemainingRigidTriangles);
    common->Printf("PathTracePrimaryPass: PT rigid mesh universe rejects nonRigid/geom/material/local/skinned/transient/gui/callback/static/cache=%d/%d/%d/%d/%d/%d/%d/%d/%d/%d\n",
        m_rigidMeshCandidateFrameStats.rejectNotRigid,
        m_rigidMeshCandidateFrameStats.rejectInvalidGeometry,
        m_rigidMeshCandidateFrameStats.rejectMissingMaterial,
        m_rigidMeshCandidateFrameStats.rejectNoLocalSpace,
        m_rigidMeshCandidateFrameStats.rejectSkinnedOrDeforming,
        m_rigidMeshCandidateFrameStats.rejectParticleOrTransient,
        m_rigidMeshCandidateFrameStats.rejectGui,
        m_rigidMeshCandidateFrameStats.rejectCallbackOrGenerated,
        m_rigidMeshCandidateFrameStats.rejectStaticWorld,
        m_rigidMeshCandidateFrameStats.rejectStaticCacheMatch);

    for (int sampleIndex = 0; sampleIndex < m_rigidMeshCandidateFrameStats.eligibleSampleCount; ++sampleIndex)
    {
        const RtPathTraceRigidMeshCandidateSample& sample = m_rigidMeshCandidateFrameStats.eligibleSamples[sampleIndex];
        if (!sample.valid)
        {
            continue;
        }
        common->Printf("PathTracePrimaryPass: PT rigid mesh eligible sample %d surf=%d entity=%d renderEntity=%d mesh=%llu instance=%llu tri=%llu vb=%llu ib=%llu format=%u seen=%d verts=%d indexes=%d tris=%d material=%u '%s' model='%s'\n",
            sampleIndex,
            sample.drawSurfIndex,
            sample.entityIndex,
            sample.renderEntityNum,
            static_cast<unsigned long long>(sample.meshHash),
            static_cast<unsigned long long>(sample.instanceId),
            static_cast<unsigned long long>(sample.triIdentity),
            static_cast<unsigned long long>(sample.vertexBufferIdentity),
            static_cast<unsigned long long>(sample.indexBufferIdentity),
            sample.vertexFormat,
            sample.seenCount,
            sample.numVerts,
            sample.numIndexes,
            sample.numIndexes / 3,
            sample.materialId,
            sample.materialName.c_str(),
            sample.modelName.c_str());
    }

    for (int sampleIndex = 0; sampleIndex < m_rigidMeshCandidateFrameStats.rejectedSampleCount; ++sampleIndex)
    {
        const RtPathTraceRigidMeshCandidateSample& sample = m_rigidMeshCandidateFrameStats.rejectedSamples[sampleIndex];
        if (!sample.valid)
        {
            continue;
        }
        common->Printf("PathTracePrimaryPass: PT rigid mesh rejected sample %d reason=%s flags=0x%x surf=%d entity=%d renderEntity=%d mesh=%llu instance=%llu verts=%d indexes=%d material=%u '%s' model='%s'\n",
            sampleIndex,
            RigidMeshCandidateRejectSummary(sample.rejectFlags),
            sample.rejectFlags,
            sample.drawSurfIndex,
            sample.entityIndex,
            sample.renderEntityNum,
            static_cast<unsigned long long>(sample.meshHash),
            static_cast<unsigned long long>(sample.instanceId),
            sample.numVerts,
            sample.numIndexes,
            sample.materialId,
            sample.materialName.c_str(),
            sample.modelName.c_str());
    }

    r_pathTracingRigidMeshUniverseDump.SetInteger(0);
}

RtSmokeGeometryUniverse::RigidMeshCandidateRecord* RtSmokeGeometryUniverse::FindOrCreateRigidMeshCandidate(const RtPathTraceRigidMeshCandidateObservation& observation, bool& cacheHit)
{
    cacheHit = false;
    const std::unordered_map<uint64, size_t>::iterator it = m_rigidMeshCandidateLookup.find(observation.meshHash);
    if (it != m_rigidMeshCandidateLookup.end() && it->second < m_rigidMeshCandidateRecords.size())
    {
        cacheHit = true;
        return &m_rigidMeshCandidateRecords[it->second];
    }

    RigidMeshCandidateRecord record;
    record.valid = true;
    record.tri = observation.tri;
    record.meshHash = observation.meshHash;
    record.vertexBufferIdentity = observation.vertexBufferIdentity;
    record.indexBufferIdentity = observation.indexBufferIdentity;
    record.materialId = observation.materialId;
    record.surfaceClassId = observation.surfaceClassId;
    record.vertexFormat = observation.vertexFormat;
    record.sourceRange.vertices.offset = 0;
    record.sourceRange.vertices.count = observation.numVerts;
    record.sourceRange.indexes.offset = 0;
    record.sourceRange.indexes.count = observation.numIndexes;
    record.sourceRange.triangles.offset = 0;
    record.sourceRange.triangles.count = observation.numIndexes / 3;
    record.firstSeenFrame = static_cast<int>(m_currentFrameIndex);
    record.lastSeenFrame = static_cast<int>(m_currentFrameIndex);
    record.materialName = observation.materialName;
    record.modelName = observation.modelName;
    const size_t recordIndex = m_rigidMeshCandidateRecords.size();
    m_rigidMeshCandidateRecords.push_back(record);
    m_rigidMeshCandidateLookup[observation.meshHash] = recordIndex;
    ++m_generation;
    m_rigidMeshCandidateFrameStats.generation = m_generation;
    return &m_rigidMeshCandidateRecords.back();
}

void RtSmokeGeometryUniverse::ResetRigidMeshCandidateFrameStats()
{
    m_rigidMeshCandidateFrameStats = RtPathTraceRigidMeshCandidateStats();
}

void RtSmokeGeometryUniverse::AddRigidMeshCandidateSample(const RtPathTraceRigidMeshCandidateObservation& observation, bool eligible, uint32_t rejectFlags, int seenCount)
{
    RtPathTraceRigidMeshCandidateSample* sample = nullptr;
    if (eligible)
    {
        if (m_rigidMeshCandidateFrameStats.eligibleSampleCount >= RT_PT_RIGID_MESH_CANDIDATE_SAMPLES)
        {
            return;
        }
        sample = &m_rigidMeshCandidateFrameStats.eligibleSamples[m_rigidMeshCandidateFrameStats.eligibleSampleCount++];
    }
    else
    {
        if (m_rigidMeshCandidateFrameStats.rejectedSampleCount >= RT_PT_RIGID_MESH_CANDIDATE_SAMPLES)
        {
            return;
        }
        sample = &m_rigidMeshCandidateFrameStats.rejectedSamples[m_rigidMeshCandidateFrameStats.rejectedSampleCount++];
    }

    sample->valid = true;
    sample->eligible = eligible;
    sample->meshHash = observation.meshHash;
    sample->instanceId = observation.instanceId;
    sample->triIdentity = reinterpret_cast<uintptr_t>(observation.tri);
    sample->vertexBufferIdentity = observation.vertexBufferIdentity;
    sample->indexBufferIdentity = observation.indexBufferIdentity;
    sample->rejectFlags = rejectFlags;
    sample->materialId = observation.materialId;
    sample->vertexFormat = observation.vertexFormat;
    sample->drawSurfIndex = observation.drawSurfIndex;
    sample->entityIndex = observation.entityIndex;
    sample->renderEntityNum = observation.renderEntityNum;
    sample->numVerts = observation.numVerts;
    sample->numIndexes = observation.numIndexes;
    sample->seenCount = seenCount;
    sample->materialName = observation.materialName;
    sample->modelName = observation.modelName;
}

RtPathTraceRigidMeshValidationStats RtSmokeGeometryUniverse::ValidateRigidMeshCandidatesAgainstDynamicPayload(
    const std::vector<uint32_t>& dynamicTriangleClassData,
    const std::vector<uint32_t>& dynamicTriangleMaterialData,
    uint32_t triangleClassMask,
    uint32_t rigidClassId) const
{
    RtPathTraceRigidMeshValidationStats stats;
    std::vector<uint32_t> bakedRigidMaterialIds;
    std::vector<uint32_t> eligibleRigidMaterialIds;

    const size_t triangleCount = Min(dynamicTriangleClassData.size(), dynamicTriangleMaterialData.size());
    bakedRigidMaterialIds.reserve(triangleCount);
    for (size_t triangleIndex = 0; triangleIndex < triangleCount; ++triangleIndex)
    {
        if ((dynamicTriangleClassData[triangleIndex] & triangleClassMask) != rigidClassId)
        {
            continue;
        }
        ++stats.bakedRigidTriangles;
        bakedRigidMaterialIds.push_back(dynamicTriangleMaterialData[triangleIndex]);
    }

    for (const RigidMeshCandidateRecord& record : m_rigidMeshCandidateRecords)
    {
        if (!record.valid || !record.seenThisFrame)
        {
            continue;
        }
        stats.eligibleRigidTriangles += record.sourceRange.triangles.count * Max(1, record.instanceCountThisFrame);
        for (int instanceIndex = 0; instanceIndex < Max(1, record.instanceCountThisFrame); ++instanceIndex)
        {
            for (int triangleIndex = 0; triangleIndex < record.sourceRange.triangles.count; ++triangleIndex)
            {
                eligibleRigidMaterialIds.push_back(record.materialId);
            }
        }
    }

    stats.triangleDelta = stats.eligibleRigidTriangles - stats.bakedRigidTriangles;
    const std::vector<uint32_t> bakedUnique = UniqueSortedMaterialIds(bakedRigidMaterialIds);
    const std::vector<uint32_t> eligibleUnique = UniqueSortedMaterialIds(eligibleRigidMaterialIds);
    stats.bakedRigidMaterialIds = static_cast<int>(bakedUnique.size());
    stats.eligibleRigidMaterialIds = static_cast<int>(eligibleUnique.size());

    size_t bakedIndex = 0;
    size_t eligibleIndex = 0;
    while (bakedIndex < bakedUnique.size() || eligibleIndex < eligibleUnique.size())
    {
        if (eligibleIndex >= eligibleUnique.size() || (bakedIndex < bakedUnique.size() && bakedUnique[bakedIndex] < eligibleUnique[eligibleIndex]))
        {
            ++stats.missingMaterialIds;
            AddMaterialSample(stats.missingMaterialSamples, stats.missingMaterialSampleCount, bakedUnique[bakedIndex]);
            ++bakedIndex;
        }
        else if (bakedIndex >= bakedUnique.size() || eligibleUnique[eligibleIndex] < bakedUnique[bakedIndex])
        {
            ++stats.extraMaterialIds;
            AddMaterialSample(stats.extraMaterialSamples, stats.extraMaterialSampleCount, eligibleUnique[eligibleIndex]);
            ++eligibleIndex;
        }
        else
        {
            ++bakedIndex;
            ++eligibleIndex;
        }
    }

    return stats;
}

void RtSmokeGeometryUniverse::DumpRigidMeshValidationStats(const RtPathTraceRigidMeshValidationStats& stats, int sceneSource) const
{
    common->Printf("PathTracePrimaryPass: PT rigid mesh validation source=%d bakedRigidTris=%d eligibleLocalTris=%d triangleDelta=%d materialIds baked/eligible=%d/%d missing=%d extra=%d renderPath=dynamicFallback\n",
        sceneSource,
        stats.bakedRigidTriangles,
        stats.eligibleRigidTriangles,
        stats.triangleDelta,
        stats.bakedRigidMaterialIds,
        stats.eligibleRigidMaterialIds,
        stats.missingMaterialIds,
        stats.extraMaterialIds);

    if (stats.missingMaterialSampleCount > 0)
    {
        char sampleText[256];
        sampleText[0] = '\0';
        for (int sampleIndex = 0; sampleIndex < stats.missingMaterialSampleCount; ++sampleIndex)
        {
            idStr::Append(sampleText, sizeof(sampleText), va("%s%u", sampleIndex == 0 ? "" : ",", stats.missingMaterialSamples[sampleIndex]));
        }
        common->Printf("PathTracePrimaryPass: PT rigid mesh validation missingMaterialIds=[%s]\n", sampleText);
    }
    if (stats.extraMaterialSampleCount > 0)
    {
        char sampleText[256];
        sampleText[0] = '\0';
        for (int sampleIndex = 0; sampleIndex < stats.extraMaterialSampleCount; ++sampleIndex)
        {
            idStr::Append(sampleText, sizeof(sampleText), va("%s%u", sampleIndex == 0 ? "" : ",", stats.extraMaterialSamples[sampleIndex]));
        }
        common->Printf("PathTracePrimaryPass: PT rigid mesh validation extraMaterialIds=[%s]\n", sampleText);
    }
}

RtPathTraceRigidBlasPlanStats RtSmokeGeometryUniverse::BuildRigidBlasPlanStats(const RtSmokeSurfaceClassStats* sourceClassStats) const
{
    RtPathTraceRigidBlasPlanStats stats;
    stats.frameIndex = m_currentFrameIndex;
    stats.generation = m_generation;
    stats.persistentMeshRecords = static_cast<int>(m_rigidMeshCandidateRecords.size());
    stats.bakedRigidSurfaces = sourceClassStats ? sourceClassStats->rigidEntitySurfaces : 0;
    stats.bakedRigidTriangles = sourceClassStats ? sourceClassStats->rigidEntityTriangles : 0;

    for (const RigidMeshCandidateRecord& record : m_rigidMeshCandidateRecords)
    {
        if (!record.valid || !record.seenThisFrame)
        {
            continue;
        }

        const int instanceCount = Max(1, record.instanceCountThisFrame);
        ++stats.meshRecords;
        stats.instances += instanceCount;
        stats.localVerts += record.sourceRange.vertices.count;
        stats.localIndexes += record.sourceRange.indexes.count;
        stats.localTriangles += record.sourceRange.triangles.count;
        stats.plannedRemoveRigidTriangles += record.sourceRange.triangles.count * instanceCount;

        if (stats.sampleCount < RT_PT_RIGID_BLAS_PLAN_SAMPLES)
        {
            RtPathTraceRigidBlasPlanSample& sample = stats.samples[stats.sampleCount++];
            sample.valid = true;
            sample.meshHash = record.meshHash;
            sample.triIdentity = reinterpret_cast<uintptr_t>(record.tri);
            sample.vertexBufferIdentity = record.vertexBufferIdentity;
            sample.indexBufferIdentity = record.indexBufferIdentity;
            sample.materialId = record.materialId;
            sample.vertexFormat = record.vertexFormat;
            sample.instanceCount = instanceCount;
            sample.verts = record.sourceRange.vertices.count;
            sample.indexes = record.sourceRange.indexes.count;
            sample.triangles = record.sourceRange.triangles.count;
            sample.materialName = record.materialName;
            sample.modelName = record.modelName;
        }
    }

    stats.estimatedRemainingRigidTriangles = Max(0, stats.bakedRigidTriangles - stats.plannedRemoveRigidTriangles);
    stats.triangleDelta = stats.plannedRemoveRigidTriangles - stats.bakedRigidTriangles;
    return stats;
}

void RtSmokeGeometryUniverse::DumpRigidBlasPlanStats(const RtPathTraceRigidBlasPlanStats& stats, int sceneSource) const
{
    common->Printf("PathTracePrimaryPass: PT rigid BLAS plan source=%d frame=%llu generation=%llu meshRecords=%d instances=%d localVerts/indexes/tris=%d/%d/%d plannedRemoveRigidTris=%d bakedRigidSurfaces/tris=%d/%d remainingDynamicRigidTris=%d triangleDelta=%d persistentMeshRecords=%d gpuBuild=0 renderPath=dynamicFallback\n",
        sceneSource,
        static_cast<unsigned long long>(stats.frameIndex),
        static_cast<unsigned long long>(stats.generation),
        stats.meshRecords,
        stats.instances,
        stats.localVerts,
        stats.localIndexes,
        stats.localTriangles,
        stats.plannedRemoveRigidTriangles,
        stats.bakedRigidSurfaces,
        stats.bakedRigidTriangles,
        stats.estimatedRemainingRigidTriangles,
        stats.triangleDelta,
        stats.persistentMeshRecords);

    for (int sampleIndex = 0; sampleIndex < stats.sampleCount; ++sampleIndex)
    {
        const RtPathTraceRigidBlasPlanSample& sample = stats.samples[sampleIndex];
        if (!sample.valid)
        {
            continue;
        }
        common->Printf("PathTracePrimaryPass: PT rigid BLAS plan sample %d mesh=%llu instances=%d tri=%llu vb=%llu ib=%llu format=%u verts=%d indexes=%d tris=%d material=%u '%s' model='%s'\n",
            sampleIndex,
            static_cast<unsigned long long>(sample.meshHash),
            sample.instanceCount,
            static_cast<unsigned long long>(sample.triIdentity),
            static_cast<unsigned long long>(sample.vertexBufferIdentity),
            static_cast<unsigned long long>(sample.indexBufferIdentity),
            sample.vertexFormat,
            sample.verts,
            sample.indexes,
            sample.triangles,
            sample.materialId,
            sample.materialName.c_str(),
            sample.modelName.c_str());
    }
}

RtPathTraceRigidBlasInputStats RtSmokeGeometryUniverse::BuildRigidBlasInputStats() const
{
    RtPathTraceRigidBlasInputStats stats;
    stats.frameIndex = m_currentFrameIndex;
    stats.generation = m_generation;

    const uint32_t expectedVertexFormat = static_cast<uint32_t>(RtSmokeGeometryBufferFormat::LegacySmokeVertex);
    for (const RigidMeshCandidateRecord& record : m_rigidMeshCandidateRecords)
    {
        if (!record.valid || !record.seenThisFrame)
        {
            continue;
        }

        const int instanceCount = Max(1, record.instanceCountThisFrame);
        uint32_t invalidFlags = 0;
        if (record.tri == nullptr)
        {
            invalidFlags |= RT_PT_RIGID_BLAS_INPUT_INVALID_NULL_TRI;
        }
        if (record.sourceRange.vertices.count <= 0)
        {
            invalidFlags |= RT_PT_RIGID_BLAS_INPUT_INVALID_VERTEX_COUNT;
        }
        if (record.sourceRange.indexes.count <= 0 || (record.sourceRange.indexes.count % 3) != 0)
        {
            invalidFlags |= RT_PT_RIGID_BLAS_INPUT_INVALID_INDEX_COUNT;
        }
        if (record.sourceRange.triangles.count <= 0 || record.sourceRange.triangles.count * 3 != record.sourceRange.indexes.count)
        {
            invalidFlags |= RT_PT_RIGID_BLAS_INPUT_INVALID_TRIANGLE_COUNT;
        }
        if (record.vertexFormat != expectedVertexFormat)
        {
            invalidFlags |= RT_PT_RIGID_BLAS_INPUT_INVALID_VERTEX_FORMAT;
        }
        if (record.vertexBufferIdentity == 0 || record.indexBufferIdentity == 0)
        {
            invalidFlags |= RT_PT_RIGID_BLAS_INPUT_INVALID_MISSING_SOURCE_IDENTITY;
        }
        if (record.materialId == 0)
        {
            invalidFlags |= RT_PT_RIGID_BLAS_INPUT_INVALID_MATERIAL;
        }

        ++stats.descriptors;
        ++stats.geometryDescs;
        stats.instances += instanceCount;
        stats.vertexCount += record.sourceRange.vertices.count;
        stats.indexCount += record.sourceRange.indexes.count;
        stats.triangleCount += record.sourceRange.triangles.count;
        stats.vertexBytes += record.sourceRange.vertices.count * static_cast<int>(sizeof(PathTraceSmokeVertex));
        stats.indexBytes += record.sourceRange.indexes.count * static_cast<int>(sizeof(uint32_t));
        if (invalidFlags == 0)
        {
            ++stats.validDescriptors;
        }
        else
        {
            ++stats.invalidDescriptors;
            if ((invalidFlags & RT_PT_RIGID_BLAS_INPUT_INVALID_NULL_TRI) != 0)
            {
                ++stats.nullTri;
            }
            if ((invalidFlags & RT_PT_RIGID_BLAS_INPUT_INVALID_VERTEX_COUNT) != 0)
            {
                ++stats.invalidVertexCount;
            }
            if ((invalidFlags & RT_PT_RIGID_BLAS_INPUT_INVALID_INDEX_COUNT) != 0)
            {
                ++stats.invalidIndexCount;
            }
            if ((invalidFlags & RT_PT_RIGID_BLAS_INPUT_INVALID_TRIANGLE_COUNT) != 0)
            {
                ++stats.invalidTriangleCount;
            }
            if ((invalidFlags & RT_PT_RIGID_BLAS_INPUT_INVALID_VERTEX_FORMAT) != 0)
            {
                ++stats.invalidVertexFormat;
            }
            if ((invalidFlags & RT_PT_RIGID_BLAS_INPUT_INVALID_MISSING_SOURCE_IDENTITY) != 0)
            {
                ++stats.missingSourceIdentity;
            }
            if ((invalidFlags & RT_PT_RIGID_BLAS_INPUT_INVALID_MATERIAL) != 0)
            {
                ++stats.invalidMaterial;
            }
        }

        if (stats.sampleCount < RT_PT_RIGID_BLAS_INPUT_SAMPLES)
        {
            RtPathTraceRigidBlasInputSample& sample = stats.samples[stats.sampleCount++];
            sample.valid = true;
            sample.meshHash = record.meshHash;
            sample.triIdentity = reinterpret_cast<uintptr_t>(record.tri);
            sample.vertexBufferIdentity = record.vertexBufferIdentity;
            sample.indexBufferIdentity = record.indexBufferIdentity;
            sample.materialId = record.materialId;
            sample.invalidFlags = invalidFlags;
            sample.vertexCount = record.sourceRange.vertices.count;
            sample.indexCount = record.sourceRange.indexes.count;
            sample.triangleCount = record.sourceRange.triangles.count;
            sample.vertexStride = static_cast<int>(sizeof(PathTraceSmokeVertex));
            sample.vertexOffsetBytes = record.sourceRange.vertices.offset * static_cast<int>(sizeof(PathTraceSmokeVertex));
            sample.indexOffsetBytes = record.sourceRange.indexes.offset * static_cast<int>(sizeof(uint32_t));
            sample.instanceCount = instanceCount;
            sample.materialName = record.materialName;
            sample.modelName = record.modelName;
        }
    }

    return stats;
}

void RtSmokeGeometryUniverse::DumpRigidBlasInputStats(const RtPathTraceRigidBlasInputStats& stats, int sceneSource) const
{
    common->Printf("PathTracePrimaryPass: PT rigid BLAS inputs source=%d frame=%llu generation=%llu descriptors=%d valid=%d invalid=%d geometryDescs=%d instances=%d verts/indexes/tris=%d/%d/%d bytes(v/i)=%d/%d vertexFormat=RGB32_FLOAT indexFormat=R32_UINT vertexStride=%d cpuDescriptorsOnly=1 gpuBuild=0 renderPath=dynamicFallback\n",
        sceneSource,
        static_cast<unsigned long long>(stats.frameIndex),
        static_cast<unsigned long long>(stats.generation),
        stats.descriptors,
        stats.validDescriptors,
        stats.invalidDescriptors,
        stats.geometryDescs,
        stats.instances,
        stats.vertexCount,
        stats.indexCount,
        stats.triangleCount,
        stats.vertexBytes,
        stats.indexBytes,
        static_cast<int>(sizeof(PathTraceSmokeVertex)));
    common->Printf("PathTracePrimaryPass: PT rigid BLAS input invalids nullTri/verts/indexes/tris/format/source/material=%d/%d/%d/%d/%d/%d/%d\n",
        stats.nullTri,
        stats.invalidVertexCount,
        stats.invalidIndexCount,
        stats.invalidTriangleCount,
        stats.invalidVertexFormat,
        stats.missingSourceIdentity,
        stats.invalidMaterial);

    for (int sampleIndex = 0; sampleIndex < stats.sampleCount; ++sampleIndex)
    {
        const RtPathTraceRigidBlasInputSample& sample = stats.samples[sampleIndex];
        if (!sample.valid)
        {
            continue;
        }
        common->Printf("PathTracePrimaryPass: PT rigid BLAS input sample %d mesh=%llu instances=%d tri=%llu vb=%llu ib=%llu invalidFlags=0x%x vertexOffsetBytes=%d indexOffsetBytes=%d vertexStride=%d verts=%d indexes=%d tris=%d material=%u '%s' model='%s'\n",
            sampleIndex,
            static_cast<unsigned long long>(sample.meshHash),
            sample.instanceCount,
            static_cast<unsigned long long>(sample.triIdentity),
            static_cast<unsigned long long>(sample.vertexBufferIdentity),
            static_cast<unsigned long long>(sample.indexBufferIdentity),
            sample.invalidFlags,
            sample.vertexOffsetBytes,
            sample.indexOffsetBytes,
            sample.vertexStride,
            sample.vertexCount,
            sample.indexCount,
            sample.triangleCount,
            sample.materialId,
            sample.materialName.c_str(),
            sample.modelName.c_str());
    }
}

RtPathTraceRigidBlasGpuStats RtSmokeGeometryUniverse::UpdateRigidBlasGpuScaffold(nvrhi::IDevice* device, nvrhi::ICommandList* commandList, bool submitBuilds)
{
    RtPathTraceRigidBlasGpuStats stats;
    stats.frameIndex = m_currentFrameIndex;
    stats.generation = m_generation;

    if (!device)
    {
        ++stats.skippedNoDevice;
        return stats;
    }
    if (!commandList)
    {
        ++stats.skippedNoCommandList;
        return stats;
    }

    std::vector<PathTraceSmokeVertex> localVertices;
    std::vector<uint32_t> localIndexes;

    for (RigidMeshCandidateRecord& record : m_rigidMeshCandidateRecords)
    {
        if (!record.valid || !record.seenThisFrame)
        {
            continue;
        }

        ++stats.meshRecords;
        const int instanceCount = Max(1, record.instanceCountThisFrame);
        stats.instances += instanceCount;
        stats.vertexCount += record.sourceRange.vertices.count;
        stats.indexCount += record.sourceRange.indexes.count;
        stats.triangleCount += record.sourceRange.triangles.count;
        const int vertexBytes = record.sourceRange.vertices.count * static_cast<int>(sizeof(PathTraceSmokeVertex));
        const int indexBytes = record.sourceRange.indexes.count * static_cast<int>(sizeof(uint32_t));
        stats.vertexBytes += vertexBytes;
        stats.indexBytes += indexBytes;

        uint32_t invalidFlags = ValidateRigidBlasInputRecord(record);
        if (invalidFlags != 0)
        {
            ++stats.invalidInputs;
            ++stats.skippedInvalid;
            continue;
        }

        localVertices.clear();
        localIndexes.clear();
        if (!BuildRigidLocalMeshData(record, localVertices, localIndexes))
        {
            invalidFlags |= RT_PT_RIGID_BLAS_INPUT_INVALID_INDEX_COUNT;
            ++stats.invalidInputs;
            ++stats.skippedInvalid;
            continue;
        }

        ++stats.validInputs;
        const size_t requiredVertexBytes = localVertices.size() * sizeof(PathTraceSmokeVertex);
        const size_t requiredIndexBytes = localIndexes.size() * sizeof(uint32_t);
        bool createdVertexBuffer = false;
        bool createdIndexBuffer = false;
        if (RigidSmokeBufferHasCapacity(record.rigidVertexBuffer, requiredVertexBytes, sizeof(PathTraceSmokeVertex)))
        {
            ++stats.vertexBuffersReused;
        }
        else
        {
            record.rigidVertexBuffer = CreateRigidSmokeBuffer(device, "PathTraceRigidMeshLocalVertices", requiredVertexBytes, sizeof(PathTraceSmokeVertex), true, false);
            record.gpuBuffersUploaded = false;
            createdVertexBuffer = record.rigidVertexBuffer != nullptr;
            if (createdVertexBuffer)
            {
                ++stats.vertexBuffersCreated;
            }
        }

        if (RigidSmokeBufferHasCapacity(record.rigidIndexBuffer, requiredIndexBytes, sizeof(uint32_t)))
        {
            ++stats.indexBuffersReused;
        }
        else
        {
            record.rigidIndexBuffer = CreateRigidSmokeBuffer(device, "PathTraceRigidMeshLocalIndices", requiredIndexBytes, sizeof(uint32_t), false, true);
            record.gpuBuffersUploaded = false;
            createdIndexBuffer = record.rigidIndexBuffer != nullptr;
            if (createdIndexBuffer)
            {
                ++stats.indexBuffersCreated;
            }
        }

        if (!record.rigidVertexBuffer || !record.rigidIndexBuffer)
        {
            ++stats.skippedInvalid;
            continue;
        }

        const uint64 uploadSignature = BuildRigidGpuUploadSignature(record);
        const bool uploadRequired = createdVertexBuffer || createdIndexBuffer || !record.gpuBuffersUploaded || record.gpuUploadSignature != uploadSignature;
        if (uploadRequired)
        {
            commandList->beginTrackingBufferState(record.rigidVertexBuffer, nvrhi::ResourceStates::Common);
            commandList->beginTrackingBufferState(record.rigidIndexBuffer, nvrhi::ResourceStates::Common);
            commandList->writeBuffer(record.rigidVertexBuffer, localVertices.data(), requiredVertexBytes);
            commandList->writeBuffer(record.rigidIndexBuffer, localIndexes.data(), requiredIndexBytes);
            commandList->setBufferState(record.rigidVertexBuffer, nvrhi::ResourceStates::AccelStructBuildInput);
            commandList->setBufferState(record.rigidIndexBuffer, nvrhi::ResourceStates::AccelStructBuildInput);
            commandList->commitBarriers();
            record.gpuUploadSignature = uploadSignature;
            record.gpuBuffersUploaded = true;
            ++stats.vertexUploads;
            ++stats.indexUploads;
            stats.uploadBytes += static_cast<int>(requiredVertexBytes + requiredIndexBytes);
        }

        bool builtThisFrame = false;
        if (submitBuilds)
        {
            if (!record.rigidBlas)
            {
                RtSmokeBlasCreateDesc blasCreateDesc;
                blasCreateDesc.device = device;
                blasCreateDesc.vertexBuffer = record.rigidVertexBuffer;
                blasCreateDesc.indexBuffer = record.rigidIndexBuffer;
                blasCreateDesc.vertexCount = static_cast<int>(localVertices.size());
                blasCreateDesc.indexCount = static_cast<int>(localIndexes.size());
                blasCreateDesc.debugName = "PathTraceRigidMeshLocalBLAS";
                const RtSmokeBlasCreateResult blasCreateResult = CreateSmokeBlas(blasCreateDesc);
                if (blasCreateResult.Succeeded())
                {
                    record.rigidBlasDesc = blasCreateResult.accelStructDesc;
                    record.rigidBlas = blasCreateResult.accelStruct;
                    record.gpuBlasCreated = true;
                    ++stats.blasHandlesCreated;
                }
                else
                {
                    ++stats.blasBuildsSkipped;
                }
            }
            else
            {
                ++stats.blasHandlesReused;
            }

            if (record.rigidBlas)
            {
                nvrhi::utils::BuildBottomLevelAccelStruct(commandList, record.rigidBlas, record.rigidBlasDesc);
                ++stats.blasBuildsSubmitted;
                builtThisFrame = true;
            }
        }
        else
        {
            ++stats.buildGateOff;
            ++stats.blasBuildsSkipped;
        }

        if (stats.sampleCount < RT_PT_RIGID_BLAS_GPU_SAMPLES)
        {
            RtPathTraceRigidBlasGpuSample& sample = stats.samples[stats.sampleCount++];
            sample.valid = true;
            sample.meshHash = record.meshHash;
            sample.triIdentity = reinterpret_cast<uintptr_t>(record.tri);
            sample.vertexBufferIdentity = record.vertexBufferIdentity;
            sample.indexBufferIdentity = record.indexBufferIdentity;
            sample.materialId = record.materialId;
            sample.invalidFlags = invalidFlags;
            sample.vertexCount = record.sourceRange.vertices.count;
            sample.indexCount = record.sourceRange.indexes.count;
            sample.triangleCount = record.sourceRange.triangles.count;
            sample.vertexBytes = vertexBytes;
            sample.indexBytes = indexBytes;
            sample.instanceCount = instanceCount;
            sample.vertexBufferValid = record.rigidVertexBuffer != nullptr;
            sample.indexBufferValid = record.rigidIndexBuffer != nullptr;
            sample.blasValid = record.rigidBlas != nullptr;
            sample.uploadedThisFrame = uploadRequired;
            sample.builtThisFrame = builtThisFrame;
            sample.materialName = record.materialName;
            sample.modelName = record.modelName;
        }
    }

    return stats;
}

void RtSmokeGeometryUniverse::ReleaseRigidBlasGpuScaffold()
{
    for (RigidMeshCandidateRecord& record : m_rigidMeshCandidateRecords)
    {
        record.rigidVertexBuffer = nullptr;
        record.rigidIndexBuffer = nullptr;
        record.rigidBlas = nullptr;
        record.rigidBlasDesc = nvrhi::rt::AccelStructDesc();
        record.gpuUploadSignature = 0;
        record.gpuBuffersUploaded = false;
        record.gpuBlasCreated = false;
    }
}

void RtSmokeGeometryUniverse::DumpRigidBlasGpuStats(const RtPathTraceRigidBlasGpuStats& stats, int sceneSource, bool scaffoldEnabled, bool submitBuilds) const
{
    common->Printf("PathTracePrimaryPass: PT rigid BLAS GPU scaffold source=%d frame=%llu generation=%llu scaffold=%d build=%d meshRecords=%d valid=%d invalid=%d instances=%d verts/indexes/tris=%d/%d/%d bytes(v/i/upload)=%d/%d/%d buffers(v create/reuse uploads i create/reuse uploads)=%d/%d/%d %d/%d/%d blas(handles create/reuse builds/skips)=%d/%d/%d/%d skips noDevice/noCmd/invalid=%d/%d/%d renderPath=dynamicFallback tlasRoute=oldStaticPlusDynamic\n",
        sceneSource,
        static_cast<unsigned long long>(stats.frameIndex),
        static_cast<unsigned long long>(stats.generation),
        scaffoldEnabled ? 1 : 0,
        submitBuilds ? 1 : 0,
        stats.meshRecords,
        stats.validInputs,
        stats.invalidInputs,
        stats.instances,
        stats.vertexCount,
        stats.indexCount,
        stats.triangleCount,
        stats.vertexBytes,
        stats.indexBytes,
        stats.uploadBytes,
        stats.vertexBuffersCreated,
        stats.vertexBuffersReused,
        stats.vertexUploads,
        stats.indexBuffersCreated,
        stats.indexBuffersReused,
        stats.indexUploads,
        stats.blasHandlesCreated,
        stats.blasHandlesReused,
        stats.blasBuildsSubmitted,
        stats.blasBuildsSkipped,
        stats.skippedNoDevice,
        stats.skippedNoCommandList,
        stats.skippedInvalid);

    for (int sampleIndex = 0; sampleIndex < stats.sampleCount; ++sampleIndex)
    {
        const RtPathTraceRigidBlasGpuSample& sample = stats.samples[sampleIndex];
        if (!sample.valid)
        {
            continue;
        }
        common->Printf("PathTracePrimaryPass: PT rigid BLAS GPU sample %d mesh=%llu instances=%d tri=%llu vb=%llu ib=%llu invalidFlags=0x%x verts=%d indexes=%d tris=%d bytes=%d/%d gpu(v/i/blas)=%d/%d/%d uploaded=%d built=%d material=%u '%s' model='%s'\n",
            sampleIndex,
            static_cast<unsigned long long>(sample.meshHash),
            sample.instanceCount,
            static_cast<unsigned long long>(sample.triIdentity),
            static_cast<unsigned long long>(sample.vertexBufferIdentity),
            static_cast<unsigned long long>(sample.indexBufferIdentity),
            sample.invalidFlags,
            sample.vertexCount,
            sample.indexCount,
            sample.triangleCount,
            sample.vertexBytes,
            sample.indexBytes,
            sample.vertexBufferValid ? 1 : 0,
            sample.indexBufferValid ? 1 : 0,
            sample.blasValid ? 1 : 0,
            sample.uploadedThisFrame ? 1 : 0,
            sample.builtThisFrame ? 1 : 0,
            sample.materialId,
            sample.materialName.c_str(),
            sample.modelName.c_str());
    }
}

RtPathTraceRigidTlasPlanStats RtSmokeGeometryUniverse::BuildRigidTlasPlanStats(const RtPathTraceInstanceUniverse& instanceUniverse, const RtSmokeSurfaceClassStats* sourceClassStats) const
{
    RtPathTraceRigidTlasPlanStats stats;
    stats.frameIndex = m_currentFrameIndex;
    stats.generation = m_generation;
    stats.bakedRigidSurfaces = sourceClassStats ? sourceClassStats->rigidEntitySurfaces : 0;
    stats.bakedRigidTriangles = sourceClassStats ? sourceClassStats->rigidEntityTriangles : 0;

    std::unordered_set<uint64> plannedMeshHashes;
    const std::vector<RtPathTraceInstanceObservation>& instances = instanceUniverse.FrameInstances();
    stats.visibleInstances = static_cast<int>(instances.size());
    for (const RtPathTraceInstanceObservation& instance : instances)
    {
        if ((instance.sourceFlags & RT_PT_INSTANCE_SOURCE_RIGID) == 0)
        {
            continue;
        }

        ++stats.rigidInstances;
        if ((instance.sourceFlags & RT_PT_INSTANCE_SOURCE_MATERIAL_OVERRIDE) != 0)
        {
            ++stats.materialOverrideInstances;
        }

        const RigidMeshCandidateRecord* record = nullptr;
        const std::unordered_map<uint64, size_t>::const_iterator it = m_rigidMeshCandidateLookup.find(instance.meshHash);
        if (it != m_rigidMeshCandidateLookup.end() && it->second < m_rigidMeshCandidateRecords.size())
        {
            record = &m_rigidMeshCandidateRecords[it->second];
        }

        bool hasMeshRecord = record && record->valid;
        bool meshSeenThisFrame = hasMeshRecord && record->seenThisFrame;
        bool hasGpuBuffers = hasMeshRecord && record->rigidVertexBuffer && record->rigidIndexBuffer;
        bool hasBlas = hasMeshRecord && record->rigidBlas;
        int triangleCount = 0;
        int meshInstanceCount = 0;

        if (!hasMeshRecord)
        {
            ++stats.missingMeshRecord;
        }
        else if (!meshSeenThisFrame)
        {
            ++stats.staleMeshRecord;
        }
        else
        {
            ++stats.plannedInstances;
            plannedMeshHashes.insert(instance.meshHash);
            triangleCount = record->sourceRange.triangles.count;
            meshInstanceCount = Max(1, record->instanceCountThisFrame);
            stats.plannedRigidTriangles += triangleCount;
            if (hasGpuBuffers)
            {
                ++stats.instancesWithGpuBuffers;
            }
            else
            {
                ++stats.missingGpuBuffers;
            }
            if (hasBlas)
            {
                ++stats.instancesWithBlas;
            }
            else
            {
                ++stats.missingBlas;
            }
        }

        if (stats.sampleCount < RT_PT_RIGID_TLAS_PLAN_SAMPLES)
        {
            RtPathTraceRigidTlasPlanSample& sample = stats.samples[stats.sampleCount++];
            sample.valid = true;
            sample.meshHash = instance.meshHash;
            sample.instanceId = instance.instanceId;
            sample.triIdentity = hasMeshRecord ? reinterpret_cast<uintptr_t>(record->tri) : 0;
            sample.materialId = hasMeshRecord ? record->materialId : instance.materialOverrideId;
            sample.sourceFlags = instance.sourceFlags;
            sample.drawSurfIndex = instance.drawSurfIndex;
            sample.entityIndex = instance.entityIndex;
            sample.renderEntityNum = instance.renderEntityNum;
            sample.triangles = triangleCount;
            sample.instanceCountForMesh = meshInstanceCount;
            sample.hasMeshRecord = hasMeshRecord;
            sample.meshSeenThisFrame = meshSeenThisFrame;
            sample.hasGpuBuffers = hasGpuBuffers;
            sample.hasBlas = hasBlas;
            sample.origin.Set(instance.objectToWorld[12], instance.objectToWorld[13], instance.objectToWorld[14]);
            sample.materialName = hasMeshRecord ? record->materialName : instance.materialName;
            sample.modelName = hasMeshRecord ? record->modelName : instance.modelName;
        }
    }

    stats.uniqueMeshes = static_cast<int>(plannedMeshHashes.size());
    stats.estimatedRemainingRigidTriangles = Max(0, stats.bakedRigidTriangles - stats.plannedRigidTriangles);
    stats.triangleDelta = stats.plannedRigidTriangles - stats.bakedRigidTriangles;
    return stats;
}

void RtSmokeGeometryUniverse::DumpRigidTlasPlanStats(const RtPathTraceRigidTlasPlanStats& stats, int sceneSource) const
{
    common->Printf("PathTracePrimaryPass: PT rigid TLAS plan source=%d frame=%llu generation=%llu visibleInstances=%d rigidInstances=%d plannedInstances=%d uniqueMeshes=%d gpuBuffers=%d blas=%d missing(mesh/stale/buffers/blas)=%d/%d/%d/%d overrides=%d plannedRigidTris=%d bakedRigidSurfaces/tris=%d/%d remainingDynamicRigidTris=%d triangleDelta=%d renderPath=dynamicFallback tlasRoute=oldStaticPlusDynamic\n",
        sceneSource,
        static_cast<unsigned long long>(stats.frameIndex),
        static_cast<unsigned long long>(stats.generation),
        stats.visibleInstances,
        stats.rigidInstances,
        stats.plannedInstances,
        stats.uniqueMeshes,
        stats.instancesWithGpuBuffers,
        stats.instancesWithBlas,
        stats.missingMeshRecord,
        stats.staleMeshRecord,
        stats.missingGpuBuffers,
        stats.missingBlas,
        stats.materialOverrideInstances,
        stats.plannedRigidTriangles,
        stats.bakedRigidSurfaces,
        stats.bakedRigidTriangles,
        stats.estimatedRemainingRigidTriangles,
        stats.triangleDelta);

    for (int sampleIndex = 0; sampleIndex < stats.sampleCount; ++sampleIndex)
    {
        const RtPathTraceRigidTlasPlanSample& sample = stats.samples[sampleIndex];
        if (!sample.valid)
        {
            continue;
        }

        common->Printf("PathTracePrimaryPass: PT rigid TLAS plan sample %d mesh=%llu instance=%llu surf=%d entity=%d renderEntity=%d tri=%llu tris=%d meshInstances=%d origin=(%.2f %.2f %.2f) sourceFlags=0x%x ready(mesh/seen/buffers/blas)=%d/%d/%d/%d material=%u '%s' model='%s'\n",
            sampleIndex,
            static_cast<unsigned long long>(sample.meshHash),
            static_cast<unsigned long long>(sample.instanceId),
            sample.drawSurfIndex,
            sample.entityIndex,
            sample.renderEntityNum,
            static_cast<unsigned long long>(sample.triIdentity),
            sample.triangles,
            sample.instanceCountForMesh,
            sample.origin.x,
            sample.origin.y,
            sample.origin.z,
            sample.sourceFlags,
            sample.hasMeshRecord ? 1 : 0,
            sample.meshSeenThisFrame ? 1 : 0,
            sample.hasGpuBuffers ? 1 : 0,
            sample.hasBlas ? 1 : 0,
            sample.materialId,
            sample.materialName.c_str(),
            sample.modelName.c_str());
    }
}

bool RtSmokeGeometryUniverse::IsRigidRouteReady(uint64 meshHash) const
{
    const std::unordered_map<uint64, size_t>::const_iterator it = m_rigidMeshCandidateLookup.find(meshHash);
    if (it == m_rigidMeshCandidateLookup.end() || it->second >= m_rigidMeshCandidateRecords.size())
    {
        return false;
    }

    const RigidMeshCandidateRecord& record = m_rigidMeshCandidateRecords[it->second];
    return record.valid && record.rigidVertexBuffer && record.rigidIndexBuffer && record.rigidBlas;
}

std::vector<uint32_t> RtSmokeGeometryUniverse::CollectRigidRouteMaterialIds(const RtPathTraceInstanceUniverse& instanceUniverse, int maxInstances) const
{
    std::vector<uint32_t> materialIds;
    int emittedInstances = 0;
    const std::vector<RtPathTraceInstanceObservation>& instances = instanceUniverse.FrameInstances();
    for (const RtPathTraceInstanceObservation& instance : instances)
    {
        if (maxInstances > 0 && emittedInstances >= maxInstances)
        {
            break;
        }
        if ((instance.sourceFlags & RT_PT_INSTANCE_SOURCE_RIGID) == 0)
        {
            continue;
        }

        const std::unordered_map<uint64, size_t>::const_iterator it = m_rigidMeshCandidateLookup.find(instance.meshHash);
        if (it == m_rigidMeshCandidateLookup.end() || it->second >= m_rigidMeshCandidateRecords.size())
        {
            continue;
        }

        const RigidMeshCandidateRecord& record = m_rigidMeshCandidateRecords[it->second];
        if (!record.valid || !record.rigidBlas)
        {
            continue;
        }
        if (std::find(materialIds.begin(), materialIds.end(), record.materialId) == materialIds.end())
        {
            materialIds.push_back(record.materialId);
        }
        ++emittedInstances;
    }
    return materialIds;
}

int RtSmokeGeometryUniverse::BuildRigidTlasInstanceDescs(
    const RtPathTraceInstanceUniverse& instanceUniverse,
    std::vector<nvrhi::rt::InstanceDesc>& instanceDescs,
    uint32_t firstInstanceId,
    uint32_t instanceMask,
    int maxInstances) const
{
    int emittedInstances = 0;
    const std::vector<RtPathTraceInstanceObservation>& instances = instanceUniverse.FrameInstances();
    for (const RtPathTraceInstanceObservation& instance : instances)
    {
        if (maxInstances > 0 && emittedInstances >= maxInstances)
        {
            break;
        }
        if ((instance.sourceFlags & RT_PT_INSTANCE_SOURCE_RIGID) == 0)
        {
            continue;
        }

        const std::unordered_map<uint64, size_t>::const_iterator it = m_rigidMeshCandidateLookup.find(instance.meshHash);
        if (it == m_rigidMeshCandidateLookup.end() || it->second >= m_rigidMeshCandidateRecords.size())
        {
            continue;
        }

        const RigidMeshCandidateRecord& record = m_rigidMeshCandidateRecords[it->second];
        if (!record.valid || !record.seenThisFrame || !record.rigidBlas)
        {
            continue;
        }

        nvrhi::rt::AffineTransform transform;
        BuildRigidTlasAffineTransform(instance.objectToWorld, transform);

        nvrhi::rt::InstanceDesc instanceDesc;
        instanceDesc
            .setInstanceID(firstInstanceId + static_cast<uint32_t>(emittedInstances))
            .setInstanceMask(instanceMask)
            .setInstanceContributionToHitGroupIndex(0)
            .setFlags(nvrhi::rt::InstanceFlags::TriangleCullDisable)
            .setTransform(transform)
            .setBLAS(record.rigidBlas);
        instanceDescs.push_back(instanceDesc);
        ++emittedInstances;
    }

    return emittedInstances;
}

RtPathTraceRigidRouteBuild RtSmokeGeometryUniverse::BuildRigidRouteBuffers(
    const RtPathTraceInstanceUniverse& instanceUniverse,
    const std::vector<uint32_t>& materialTableIds,
    int maxInstances) const
{
    RtPathTraceRigidRouteBuild build;
    std::vector<PathTraceSmokeVertex> localVertices;
    std::vector<uint32_t> localIndexes;

    const std::vector<RtPathTraceInstanceObservation>& instances = instanceUniverse.FrameInstances();
    build.stats.visibleInstances = static_cast<int>(instances.size());
    for (const RtPathTraceInstanceObservation& instance : instances)
    {
        if (maxInstances > 0 && build.stats.emittedInstances >= maxInstances)
        {
            break;
        }
        if ((instance.sourceFlags & RT_PT_INSTANCE_SOURCE_RIGID) == 0)
        {
            ++build.stats.skippedNonRigid;
            continue;
        }

        const std::unordered_map<uint64, size_t>::const_iterator it = m_rigidMeshCandidateLookup.find(instance.meshHash);
        if (it == m_rigidMeshCandidateLookup.end() || it->second >= m_rigidMeshCandidateRecords.size())
        {
            ++build.stats.skippedMissingMesh;
            continue;
        }

        const RigidMeshCandidateRecord& record = m_rigidMeshCandidateRecords[it->second];
        if (!record.valid || !record.seenThisFrame)
        {
            ++build.stats.skippedMissingMesh;
            continue;
        }
        if (!record.rigidBlas)
        {
            ++build.stats.skippedMissingBlas;
            continue;
        }

        localVertices.clear();
        localIndexes.clear();
        if (!BuildRigidLocalMeshData(record, localVertices, localIndexes))
        {
            ++build.stats.skippedMissingMesh;
            continue;
        }

        const uint32_t vertexOffset = static_cast<uint32_t>(build.vertices.size());
        const uint32_t indexOffset = static_cast<uint32_t>(build.indexes.size());
        const uint32_t triangleOffset = static_cast<uint32_t>(build.triangleMaterials.size());
        build.vertices.insert(build.vertices.end(), localVertices.begin(), localVertices.end());
        build.indexes.insert(build.indexes.end(), localIndexes.begin(), localIndexes.end());

        const uint32_t materialIndex = FindRigidRouteMaterialTableIndex(materialTableIds, record.materialId, build.stats.missingMaterialTableIndex);
        const int triangleCount = static_cast<int>(localIndexes.size() / 3);
        for (int triangleIndex = 0; triangleIndex < triangleCount; ++triangleIndex)
        {
            build.triangleMaterials.push_back(record.materialId);
            build.triangleMaterialIndexes.push_back(materialIndex);
        }

        PathTraceRigidRouteInstance routeInstance;
        routeInstance.vertexOffset = vertexOffset;
        routeInstance.indexOffset = indexOffset;
        routeInstance.triangleOffset = triangleOffset;
        routeInstance.materialId = record.materialId;
        routeInstance.materialIndex = materialIndex;
        routeInstance.vertexCount = static_cast<uint32_t>(localVertices.size());
        routeInstance.indexCount = static_cast<uint32_t>(localIndexes.size());
        routeInstance.triangleCount = static_cast<uint32_t>(triangleCount);
        build.instances.push_back(routeInstance);
        std::array<float, 16> objectToWorld = {};
        for (int elementIndex = 0; elementIndex < 16; ++elementIndex)
        {
            objectToWorld[elementIndex] = instance.objectToWorld[elementIndex];
        }
        build.instanceObjectToWorld.push_back(objectToWorld);

        ++build.stats.emittedInstances;
        build.stats.vertices += static_cast<int>(localVertices.size());
        build.stats.indexes += static_cast<int>(localIndexes.size());
        build.stats.triangles += triangleCount;
    }

    return build;
}
