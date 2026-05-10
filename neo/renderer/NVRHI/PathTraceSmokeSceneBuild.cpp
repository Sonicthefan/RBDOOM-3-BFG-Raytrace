#include "precompiled.h"
#pragma hdrstop

// Per-frame scene build orchestration for the RT smoke/path tracing path.
//
// This file keeps the top-level build order visible: capture Doom surfaces,
// build material/emissive data, create and upload buffers, submit acceleration
// structures, create bindings, commit resources, then run scene diagnostics.
// Lower-level classification, capture, resource, and diagnostic work stays in
// the narrower PathTrace* modules.

#include "PathTraceAcceleration.h"
#include "PathTraceCVars.h"
#include "PathTraceDebugDumps.h"
#include "PathTraceDoomLights.h"
#include "PathTraceDrawSurfCapture.h"
#include "PathTraceDynamicMaterialState.h"
#include "PathTraceEmissiveCandidates.h"
#include "PathTraceMaterialUniverse.h"
#include "PathTraceMaterialTextureDiscovery.h"
#include "PathTracePrimaryPass.h"
#include "PathTraceSceneCapture.h"
#include "PathTraceSceneUniverse.h"
#include "PathTraceSkinning.h"
#include "PathTraceSmokeResources.h"
#include "PathTraceSurfaceClassification.h"
#include "../RenderBackend.h"
#include "../Image.h"
#include "../Model_local.h"
#include "../Passes/CommonPasses.h"
#include "../../framework/Common_local.h"
#include "../../sys/DeviceManager.h"

#include <algorithm>
#include <vector>

extern DeviceManager* deviceManager;

namespace {

const int RT_SMOKE_SCENE_LOG_INTERVAL_FRAMES = 120;
const int RT_SMOKE_MAX_EMISSIVE_TRIANGLE_RECORDS = 65536;
const int RT_SMOKE_GEOMETRY_VALIDATION_DUMP_RECORDS = 16;
const int RT_SMOKE_GEOMETRY_RANGE_DUMP_RECORDS = 16;
const int RT_PT_RESIDENT_BOUNDS_OVERLAY_SAFE_BOXES = 64;
const float RT_SMOKE_SKINNED_TELEPORT_DISTANCE = 1024.0f;

int g_smokeLastSceneTimingLogMs = -1000000;
uint64 g_smokeLastGeometryValidationDumpGeneration = 0;
int g_smokeLastGeometryValidationDumpErrors = 0;

bool SmokeUploadElementRangeValid(int elementOffset, int elementCount, size_t elementTotal)
{
    return elementOffset >= 0 &&
        elementCount > 0 &&
        static_cast<size_t>(elementOffset) <= elementTotal &&
        static_cast<size_t>(elementCount) <= elementTotal - static_cast<size_t>(elementOffset);
}

template< typename T >
RtSmokeBufferUploadItem MakeSmokeVectorUploadItem(
    nvrhi::BufferHandle buffer,
    const std::vector<T>& data,
    nvrhi::ResourceStates finalState,
    bool skip,
    int elementOffset = -1,
    int elementCount = 0)
{
    RtSmokeBufferUploadItem item;
    item.buffer = buffer;
    item.data = data.data();
    item.byteSize = data.size() * sizeof(T);
    item.finalState = finalState;
    item.skip = skip;
    if (SmokeUploadElementRangeValid(elementOffset, elementCount, data.size()))
    {
        item.byteSize = static_cast<size_t>(elementCount) * sizeof(T);
        item.sourceOffsetBytes = static_cast<size_t>(elementOffset) * sizeof(T);
        item.destOffsetBytes = static_cast<uint64_t>(item.sourceOffsetBytes);
    }
    return item;
}

uint64_t SumSmokeUploadBytes(const RtSmokeBufferUploadItem* items, int firstItem, int itemCount)
{
    uint64_t bytes = 0;
    for (int itemIndex = firstItem; itemIndex < firstItem + itemCount; ++itemIndex)
    {
        const RtSmokeBufferUploadItem& item = items[itemIndex];
        if (!item.skip && item.buffer && item.data && item.byteSize > 0)
        {
            bytes += static_cast<uint64_t>(item.byteSize);
        }
    }
    return bytes;
}

template< typename T >
uint64 HashSmokeVectorData(uint64 hash, const std::vector<T>& data)
{
    const uint64 count = static_cast<uint64>(data.size());
    hash = HashSmokeBytes(hash, &count, sizeof(count));
    if (!data.empty())
    {
        hash = HashSmokeBytes(hash, data.data(), data.size() * sizeof(data[0]));
    }
    return hash;
}

uint64 ComputeSmokePreviousStaticSnapshotUploadSignature(
    const std::vector<PathTraceSmokeVertex>& vertices,
    const std::vector<uint32_t>& indexes,
    const std::vector<uint32_t>& triangleClasses,
    const std::vector<uint32_t>& triangleMaterials,
    const std::vector<uint32_t>& triangleMaterialIndexes)
{
    uint64 hash = 14695981039346656037ull;
    hash = HashSmokeVectorData(hash, vertices);
    hash = HashSmokeVectorData(hash, indexes);
    hash = HashSmokeVectorData(hash, triangleClasses);
    hash = HashSmokeVectorData(hash, triangleMaterials);
    hash = HashSmokeVectorData(hash, triangleMaterialIndexes);
    return hash;
}

struct RtSmokeStaticDrawSurfCounts
{
    int surfaces = 0;
    int triangles = 0;
};

struct RtSmokeSkinnedGpuScaffoldBuild
{
    std::vector<PathTraceSkinnedSourceVertex> sourceVertices;
    std::vector<PathTraceSmokeVertex> currentOutputVertices;
    std::vector<PathTraceSkinnedPreviousPosition> previousPositions;
    std::vector<PathTraceSkinnedSurfaceDispatchRecord> dispatchRecords;
    std::vector<PathTraceSkinnedJointMatrix> currentJointMatrices;
    std::vector<PathTraceSkinnedJointMatrix> previousJointMatrices;
};

bool SmokeSkinnedSurfaceKeysEqual(const RtSmokeSkinnedSurfaceKey& a, const RtSmokeSkinnedSurfaceKey& b)
{
    return a.entityIndex == b.entityIndex &&
        a.entityDef == b.entityDef &&
        a.model == b.model &&
        a.tri == b.tri &&
        a.materialId == b.materialId &&
        a.surfaceClassId == b.surfaceClassId;
}

bool SmokeSkinnedSurfaceLooseKeysEqual(const RtSmokeSkinnedSurfaceKey& a, const RtSmokeSkinnedSurfaceKey& b)
{
    return a.entityIndex == b.entityIndex &&
        a.entityDef == b.entityDef &&
        a.model == b.model &&
        a.tri == b.tri;
}

const RtSmokeSkinnedSurfaceRecord* FindSmokeSkinnedPreviousRecord(
    const std::vector<RtSmokeSkinnedSurfaceRecord>& previousRecords,
    const RtSmokeSkinnedSurfaceRecord& current)
{
    for (const RtSmokeSkinnedSurfaceRecord& previous : previousRecords)
    {
        if (SmokeSkinnedSurfaceKeysEqual(previous.key, current.key))
        {
            return &previous;
        }
    }
    return nullptr;
}

const RtSmokeSkinnedSurfaceRecord* FindSmokeSkinnedPreviousLooseRecord(
    const std::vector<RtSmokeSkinnedSurfaceRecord>& previousRecords,
    const RtSmokeSkinnedSurfaceRecord& current)
{
    for (const RtSmokeSkinnedSurfaceRecord& previous : previousRecords)
    {
        if (SmokeSkinnedSurfaceLooseKeysEqual(previous.key, current.key))
        {
            return &previous;
        }
    }
    return nullptr;
}

void AddSmokeSkinnedInvalidReasonStats(RtSmokeSkinnedPreviousFrameStats& stats, uint32_t reasons)
{
    if ((reasons & RT_SMOKE_SKINNED_INVALID_NO_PREVIOUS_FRAME) != 0u) { ++stats.noPreviousFrameCount; }
    if ((reasons & RT_SMOKE_SKINNED_INVALID_NO_PREVIOUS_SURFACE) != 0u) { ++stats.noPreviousSurfaceCount; }
    if ((reasons & RT_SMOKE_SKINNED_INVALID_VERTEX_COUNT_MISMATCH) != 0u) { ++stats.vertexCountMismatchCount; }
    if ((reasons & RT_SMOKE_SKINNED_INVALID_INDEX_COUNT_MISMATCH) != 0u) { ++stats.indexCountMismatchCount; }
    if ((reasons & RT_SMOKE_SKINNED_INVALID_TRIANGLE_COUNT_MISMATCH) != 0u) { ++stats.triangleCountMismatchCount; }
    if ((reasons & RT_SMOKE_SKINNED_INVALID_MATERIAL_CHANGED) != 0u) { ++stats.materialChangedCount; }
    if ((reasons & RT_SMOKE_SKINNED_INVALID_SURFACE_CLASS_CHANGED) != 0u) { ++stats.surfaceClassChangedCount; }
    if ((reasons & RT_SMOKE_SKINNED_INVALID_NOT_RT_CPU_SKINNED) != 0u) { ++stats.notRtCpuSkinnedCount; }
    if ((reasons & RT_SMOKE_SKINNED_INVALID_SKELETON_CHANGED) != 0u) { ++stats.skeletonChangedCount; }
    if ((reasons & RT_SMOKE_SKINNED_INVALID_TRANSFORM_DISCONTINUITY) != 0u) { ++stats.transformDiscontinuityCount; }
    if ((reasons & RT_SMOKE_SKINNED_INVALID_PREVIOUS_BUFFER_UNAVAILABLE) != 0u) { ++stats.previousBufferUnavailableCount; }
}

bool SmokeSkinnedCurrentVertexRangeValid(const RtSmokeSkinnedSurfaceRecord& record, const std::vector<PathTraceSmokeVertex>& dynamicVertexData)
{
    return record.currentVertexOffset >= 0 &&
        record.vertexCount > 0 &&
        record.currentVertexOffset <= static_cast<int>(dynamicVertexData.size()) &&
        record.vertexCount <= static_cast<int>(dynamicVertexData.size()) - record.currentVertexOffset;
}

RtSmokeSkinnedPreviousFrameStats UpdateSmokeSkinnedPreviousCpuBridge(
    std::vector<RtSmokeSkinnedSurfaceRecord>& currentRecords,
    const std::vector<RtSmokeSkinnedSurfaceRecord>& previousRecords,
    const std::vector<PathTraceSmokeVertex>& previousSkinnedVertexData,
    const std::vector<PathTraceSmokeVertex>& dynamicVertexData,
    std::vector<PathTraceSmokeVertex>& nextPreviousSkinnedVertexData)
{
    RtSmokeSkinnedPreviousFrameStats stats;
    const bool hadPreviousFrame = !previousRecords.empty() || !previousSkinnedVertexData.empty();
    const float teleportDistanceSqr = RT_SMOKE_SKINNED_TELEPORT_DISTANCE * RT_SMOKE_SKINNED_TELEPORT_DISTANCE;

    nextPreviousSkinnedVertexData.clear();
    for (RtSmokeSkinnedSurfaceRecord& current : currentRecords)
    {
        ++stats.currentSurfaceCount;
        stats.currentTriangleCount += current.triangleCount;
        if (current.rtCpuSkinned)
        {
            ++stats.currentRtCpuSkinnedSurfaceCount;
        }

        uint32_t reasons = RT_SMOKE_SKINNED_INVALID_NONE;
        uint32_t temporalFlags = 0;
        const RtSmokeSkinnedSurfaceRecord* previous = FindSmokeSkinnedPreviousRecord(previousRecords, current);
        if (!current.rtCpuSkinned)
        {
            reasons |= RT_SMOKE_SKINNED_INVALID_NOT_RT_CPU_SKINNED;
        }
        if (!hadPreviousFrame)
        {
            reasons |= RT_SMOKE_SKINNED_INVALID_NO_PREVIOUS_FRAME;
        }
        else if (!previous)
        {
            const RtSmokeSkinnedSurfaceRecord* loosePrevious = FindSmokeSkinnedPreviousLooseRecord(previousRecords, current);
            if (loosePrevious && loosePrevious->key.surfaceClassId != current.key.surfaceClassId)
            {
                reasons |= RT_SMOKE_SKINNED_INVALID_SURFACE_CLASS_CHANGED;
            }
            else if (loosePrevious && loosePrevious->key.materialId != current.key.materialId)
            {
                reasons |= RT_SMOKE_SKINNED_INVALID_MATERIAL_CHANGED;
            }
            else
            {
                reasons |= RT_SMOKE_SKINNED_INVALID_NO_PREVIOUS_SURFACE;
            }
        }
        else
        {
            if (previous->vertexCount != current.vertexCount)
            {
                reasons |= RT_SMOKE_SKINNED_INVALID_VERTEX_COUNT_MISMATCH;
            }
            if (previous->indexCount != current.indexCount)
            {
                reasons |= RT_SMOKE_SKINNED_INVALID_INDEX_COUNT_MISMATCH;
            }
            if (previous->triangleCount != current.triangleCount)
            {
                reasons |= RT_SMOKE_SKINNED_INVALID_TRIANGLE_COUNT_MISMATCH;
            }
            if (previous->jointCount != current.jointCount || previous->jointSource != current.jointSource)
            {
                reasons |= RT_SMOKE_SKINNED_INVALID_SKELETON_CHANGED;
            }
            if (previous->hasEntityOrigin && current.hasEntityOrigin && (current.entityOrigin - previous->entityOrigin).LengthSqr() > teleportDistanceSqr)
            {
                reasons |= RT_SMOKE_SKINNED_INVALID_TRANSFORM_DISCONTINUITY;
            }
            if (previous->retainedVertexOffset < 0 ||
                previous->vertexCount <= 0 ||
                previous->retainedVertexOffset > static_cast<int>(previousSkinnedVertexData.size()) ||
                previous->vertexCount > static_cast<int>(previousSkinnedVertexData.size()) - previous->retainedVertexOffset)
            {
                reasons |= RT_SMOKE_SKINNED_INVALID_PREVIOUS_BUFFER_UNAVAILABLE;
            }

            if ((reasons & (RT_SMOKE_SKINNED_INVALID_VERTEX_COUNT_MISMATCH | RT_SMOKE_SKINNED_INVALID_INDEX_COUNT_MISMATCH | RT_SMOKE_SKINNED_INVALID_TRIANGLE_COUNT_MISMATCH)) == 0u)
            {
                temporalFlags |= RT_SMOKE_SKINNED_TEMPORAL_TOPOLOGY_STABLE;
            }
            temporalFlags |= RT_SMOKE_SKINNED_TEMPORAL_LOD_STABLE;
            if ((reasons & RT_SMOKE_SKINNED_INVALID_TRANSFORM_DISCONTINUITY) == 0u)
            {
                temporalFlags |= RT_SMOKE_SKINNED_TEMPORAL_TRANSFORM_CONTINUOUS;
            }
            if ((reasons & RT_SMOKE_SKINNED_INVALID_SKELETON_CHANGED) == 0u && current.rtCpuSkinned)
            {
                temporalFlags |= RT_SMOKE_SKINNED_TEMPORAL_DEFORMATION_CONTINUOUS;
            }
            temporalFlags |= RT_SMOKE_SKINNED_TEMPORAL_MATERIAL_STABLE;
            if ((reasons & RT_SMOKE_SKINNED_INVALID_PREVIOUS_BUFFER_UNAVAILABLE) == 0u)
            {
                temporalFlags |= RT_SMOKE_SKINNED_TEMPORAL_PREVIOUS_BUFFER_VALID;
            }
        }

        if (previous && reasons == RT_SMOKE_SKINNED_INVALID_NONE)
        {
            current.previousValid = true;
            current.previousVertexOffset = previous->retainedVertexOffset;
            current.previousIndexOffset = previous->currentIndexOffset;
            current.previousTriangleOffset = previous->currentTriangleOffset;
            temporalFlags |= RT_SMOKE_SKINNED_TEMPORAL_HAS_VALID_PREVIOUS;
            ++stats.previousMatchedSurfaceCount;
        }
        else
        {
            current.previousValid = false;
            ++stats.previousInvalidSurfaceCount;
        }

        current.invalidReasonFlags = reasons;
        current.temporalStateFlags = temporalFlags;
        AddSmokeSkinnedInvalidReasonStats(stats, reasons);
        if ((temporalFlags & RT_SMOKE_SKINNED_TEMPORAL_TOPOLOGY_STABLE) != 0u) { ++stats.topologyStableCount; }
        if ((temporalFlags & RT_SMOKE_SKINNED_TEMPORAL_LOD_STABLE) != 0u) { ++stats.lodStableCount; }
        if ((temporalFlags & RT_SMOKE_SKINNED_TEMPORAL_TRANSFORM_CONTINUOUS) != 0u) { ++stats.transformContinuousCount; }
        if ((temporalFlags & RT_SMOKE_SKINNED_TEMPORAL_DEFORMATION_CONTINUOUS) != 0u) { ++stats.deformationContinuousCount; }
        if ((temporalFlags & RT_SMOKE_SKINNED_TEMPORAL_MATERIAL_STABLE) != 0u) { ++stats.materialStableCount; }
        if ((temporalFlags & RT_SMOKE_SKINNED_TEMPORAL_PREVIOUS_BUFFER_VALID) != 0u) { ++stats.previousBufferValidCount; }
    }

    nextPreviousSkinnedVertexData.reserve(dynamicVertexData.size());
    for (RtSmokeSkinnedSurfaceRecord& current : currentRecords)
    {
        current.retainedVertexOffset = -1;
        if (!current.rtCpuSkinned || !SmokeSkinnedCurrentVertexRangeValid(current, dynamicVertexData))
        {
            continue;
        }

        current.retainedVertexOffset = static_cast<int>(nextPreviousSkinnedVertexData.size());
        nextPreviousSkinnedVertexData.insert(
            nextPreviousSkinnedVertexData.end(),
            dynamicVertexData.begin() + current.currentVertexOffset,
            dynamicVertexData.begin() + current.currentVertexOffset + current.vertexCount);
    }
    stats.previousRetainedVertexCount = static_cast<int>(nextPreviousSkinnedVertexData.size());
    return stats;
}

PathTraceSkinnedSourceVertex BuildSmokeSkinnedSourceVertex(const idDrawVert& drawVert)
{
    const idVec3 normal = drawVert.GetNormal();
    const idVec3 tangent = drawVert.GetTangent();
    const idVec2 texCoord = drawVert.GetTexCoord();

    PathTraceSkinnedSourceVertex vertex = {};
    vertex.localPosition[0] = drawVert.xyz.x;
    vertex.localPosition[1] = drawVert.xyz.y;
    vertex.localPosition[2] = drawVert.xyz.z;
    vertex.localPosition[3] = 1.0f;
    vertex.localNormal[0] = normal.x;
    vertex.localNormal[1] = normal.y;
    vertex.localNormal[2] = normal.z;
    vertex.localNormal[3] = 0.0f;
    vertex.localTangent[0] = tangent.x;
    vertex.localTangent[1] = tangent.y;
    vertex.localTangent[2] = tangent.z;
    vertex.localTangent[3] = drawVert.GetBiTangentSign();
    vertex.texCoord[0] = texCoord.x;
    vertex.texCoord[1] = texCoord.y;
    vertex.texCoord[2] = 0.0f;
    vertex.texCoord[3] = 0.0f;
    for (int component = 0; component < 4; ++component)
    {
        vertex.color[component] = drawVert.color[component] * (1.0f / 255.0f);
        vertex.jointIndices[component] = static_cast<uint32_t>(drawVert.color[component]);
        vertex.jointWeights[component] = drawVert.color2[component] * (1.0f / 255.0f);
    }
    return vertex;
}

void CopySmokeObjectToWorldRows(float dst[12], const float src[12])
{
    for (int i = 0; i < 12; ++i)
    {
        dst[i] = src[i];
    }
}

void CopySmokeJointMatrixRows(PathTraceSkinnedJointMatrix& dst, const idJointMat& src)
{
    const float* rows = src.ToFloatPtr();
    for (int i = 0; i < 12; ++i)
    {
        dst.rows[i] = rows[i];
    }
}

const idJointMat* SmokeSkinnedRecordJoints(const RtSmokeSkinnedSurfaceRecord& record)
{
    const srfTriangles_t* tri = reinterpret_cast<const srfTriangles_t*>(record.key.tri);
    return GetSmokeRtCpuSkinningJoints(tri);
}

bool SmokeSkinnedJointRangeValid(const RtSmokeSkinnedSurfaceRecord& record, const std::vector<PathTraceSkinnedJointMatrix>& retainedJointMatrices)
{
    return record.retainedJointOffset >= 0 &&
        record.jointCount > 0 &&
        record.retainedJointOffset <= static_cast<int>(retainedJointMatrices.size()) &&
        record.jointCount <= static_cast<int>(retainedJointMatrices.size()) - record.retainedJointOffset;
}

bool AppendSmokeSkinnedJointMatrices(const idJointMat* joints, int jointCount, std::vector<PathTraceSkinnedJointMatrix>& jointMatrices, int& jointOffset)
{
    jointOffset = -1;
    if (!joints || jointCount <= 0)
    {
        return false;
    }

    jointOffset = static_cast<int>(jointMatrices.size());
    jointMatrices.resize(jointMatrices.size() + jointCount);
    for (int jointIndex = 0; jointIndex < jointCount; ++jointIndex)
    {
        CopySmokeJointMatrixRows(jointMatrices[jointOffset + jointIndex], joints[jointIndex]);
    }
    return true;
}

void RetainSmokeSkinnedCurrentJointMatrices(
    std::vector<RtSmokeSkinnedSurfaceRecord>& currentRecords,
    std::vector<PathTraceSkinnedJointMatrix>& nextPreviousSkinnedJointMatrices)
{
    nextPreviousSkinnedJointMatrices.clear();
    for (RtSmokeSkinnedSurfaceRecord& current : currentRecords)
    {
        current.retainedJointOffset = -1;
        if (!current.rtCpuSkinned || current.jointCount <= 0)
        {
            continue;
        }

        int jointOffset = -1;
        if (AppendSmokeSkinnedJointMatrices(
                SmokeSkinnedRecordJoints(current),
                current.jointCount,
                nextPreviousSkinnedJointMatrices,
                jointOffset))
        {
            current.retainedJointOffset = jointOffset;
        }
    }
}

RtSmokeSkinnedGpuScaffoldBuild BuildSmokeSkinnedGpuScaffold(
    int gpuSkinningMode,
    std::vector<RtSmokeSkinnedSurfaceRecord>& currentRecords,
    const std::vector<RtSmokeSkinnedSurfaceRecord>& previousRecords,
    const std::vector<PathTraceSmokeVertex>& dynamicVertexData,
    const std::vector<PathTraceSmokeVertex>& previousSkinnedVertexData,
    const std::vector<PathTraceSkinnedJointMatrix>& previousSkinnedJointMatrices)
{
    RtSmokeSkinnedGpuScaffoldBuild build;
    if (gpuSkinningMode <= 0 || currentRecords.empty())
    {
        return build;
    }

    for (int recordIndex = 0; recordIndex < static_cast<int>(currentRecords.size()); ++recordIndex)
    {
        RtSmokeSkinnedSurfaceRecord& record = currentRecords[recordIndex];
        const srfTriangles_t* tri = reinterpret_cast<const srfTriangles_t*>(record.key.tri);
        if (!record.rtCpuSkinned ||
            !tri ||
            !tri->verts ||
            record.vertexCount <= 0 ||
            record.vertexCount > tri->numVerts ||
            !SmokeSkinnedCurrentVertexRangeValid(record, dynamicVertexData))
        {
            continue;
        }

        record.gpuSourceVertexOffset = static_cast<int>(build.sourceVertices.size());
        record.gpuOutputVertexOffset = static_cast<int>(build.currentOutputVertices.size());
        record.gpuPreviousPositionOffset = -1;

        for (int vertexIndex = 0; vertexIndex < record.vertexCount; ++vertexIndex)
        {
            build.sourceVertices.push_back(BuildSmokeSkinnedSourceVertex(tri->verts[vertexIndex]));
            build.currentOutputVertices.push_back(dynamicVertexData[record.currentVertexOffset + vertexIndex]);
        }

        const bool hasPreviousPositionRange =
            record.previousValid &&
            record.previousVertexOffset >= 0 &&
            record.previousVertexOffset <= static_cast<int>(previousSkinnedVertexData.size()) &&
            record.vertexCount <= static_cast<int>(previousSkinnedVertexData.size()) - record.previousVertexOffset;
        if (hasPreviousPositionRange)
        {
            record.gpuPreviousPositionOffset = static_cast<int>(build.previousPositions.size());
            for (int vertexIndex = 0; vertexIndex < record.vertexCount; ++vertexIndex)
            {
                const PathTraceSmokeVertex& previousVertex = previousSkinnedVertexData[record.previousVertexOffset + vertexIndex];
                PathTraceSkinnedPreviousPosition previousPosition = {};
                previousPosition.previousPosition[0] = previousVertex.position[0];
                previousPosition.previousPosition[1] = previousVertex.position[1];
                previousPosition.previousPosition[2] = previousVertex.position[2];
                previousPosition.previousPosition[3] = 1.0f;
                build.previousPositions.push_back(previousPosition);
            }
        }

        PathTraceSkinnedSurfaceDispatchRecord dispatch = {};
        dispatch.sourceVertexOffset = static_cast<uint32_t>(record.gpuSourceVertexOffset);
        dispatch.outputVertexOffset = static_cast<uint32_t>(record.gpuOutputVertexOffset);
        dispatch.previousPositionOffset = record.gpuPreviousPositionOffset >= 0 ? static_cast<uint32_t>(record.gpuPreviousPositionOffset) : UINT32_MAX;
        dispatch.vertexCount = static_cast<uint32_t>(record.vertexCount);
        dispatch.currentJointOffset = UINT32_MAX;
        dispatch.previousJointOffset = UINT32_MAX;
        dispatch.surfaceRecordIndex = static_cast<uint32_t>(recordIndex);
        dispatch.flags = PT_SKINNED_DISPATCH_RT_CPU_SKINNED | PT_SKINNED_DISPATCH_SOURCE_READY;
        dispatch.dynamicVertexOffset = static_cast<uint32_t>(record.currentVertexOffset);
        dispatch.dynamicIndexOffset = static_cast<uint32_t>(record.currentIndexOffset);
        dispatch.dynamicTriangleOffset = static_cast<uint32_t>(record.currentTriangleOffset);
        dispatch.triangleCount = static_cast<uint32_t>(record.triangleCount);
        if (record.previousValid && record.gpuPreviousPositionOffset >= 0)
        {
            dispatch.flags |= PT_SKINNED_DISPATCH_HAS_VALID_PREVIOUS;
        }
        CopySmokeObjectToWorldRows(dispatch.currentObjectToWorld, record.objectToWorld);
        const RtSmokeSkinnedSurfaceRecord* previousRecord = FindSmokeSkinnedPreviousRecord(previousRecords, record);
        CopySmokeObjectToWorldRows(dispatch.previousObjectToWorld, previousRecord ? previousRecord->objectToWorld : record.objectToWorld);
        int currentJointOffset = -1;
        if (AppendSmokeSkinnedJointMatrices(
                SmokeSkinnedRecordJoints(record),
                record.jointCount,
                build.currentJointMatrices,
                currentJointOffset))
        {
            dispatch.currentJointOffset = static_cast<uint32_t>(currentJointOffset);
            dispatch.flags |= PT_SKINNED_DISPATCH_HAS_CURRENT_JOINTS;
        }
        if (record.previousValid &&
            previousRecord &&
            previousRecord->jointCount == record.jointCount &&
            SmokeSkinnedJointRangeValid(*previousRecord, previousSkinnedJointMatrices))
        {
            dispatch.previousJointOffset = static_cast<uint32_t>(build.previousJointMatrices.size());
            build.previousJointMatrices.insert(
                build.previousJointMatrices.end(),
                previousSkinnedJointMatrices.begin() + previousRecord->retainedJointOffset,
                previousSkinnedJointMatrices.begin() + previousRecord->retainedJointOffset + previousRecord->jointCount);
            dispatch.flags |= PT_SKINNED_DISPATCH_HAS_PREVIOUS_JOINTS;
        }
        build.dispatchRecords.push_back(dispatch);
    }

    return build;
}

void ApplySmokeRoutedScenePreset(int debugMode, int requestedPreset, const char* label)
{
    const int preset = idMath::ClampInt(0, 4, requestedPreset);
    const bool mode18 = debugMode == 18;
    const bool mode20 = debugMode == 20;
    if (!mode18 && !mode20)
    {
        return;
    }

    r_pathTracingDebugMode.SetInteger(debugMode);
    r_pathTracingSceneSource.SetInteger(3);
    r_pathTracingRigidBlasGpuScaffold.SetInteger(1);
    r_pathTracingRigidBlasGpuBuild.SetInteger(1);
    r_pathTracingRigidTlasRoute.SetInteger(1);
    r_pathTracingRigidRouteMode18.SetInteger(mode18 ? 1 : r_pathTracingRigidRouteMode18.GetInteger());
    r_pathTracingRigidRouteMode20.SetInteger(mode20 ? 1 : r_pathTracingRigidRouteMode20.GetInteger());
    r_pathTracingRigidRouteRemoveDynamic.SetInteger(1);
    r_pathTracingRigidRouteEmissiveCards.SetInteger(1);
    r_pathTracingRigidResidency.SetInteger(1);
    r_pathTracingStaticAreaPreload.SetInteger(1);

    const int portalSteps = 4;
    r_pathTracingRigidResidencyPortalSteps.SetInteger(portalSteps);
    r_pathTracingStaticAreaPreloadPortalSteps.SetInteger(portalSteps);
    r_pathTracingLightAreaPortalSteps.SetInteger(portalSteps);

    r_pathTracingLightAreaFilter.SetInteger(mode20 && preset >= 2 ? 1 : 0);
    r_pathTracingLightAreaFilterApply.SetInteger(mode20 && preset == 3 ? 1 : 0);
    r_pathTracingLightAreaOverflowMax.SetInteger(512);
    r_pathTracingLightUniverseChurn.SetInteger(mode20 && preset >= 2 ? 1 : 0);

    const int presetRigidRouteMaxInstances = 256;
    if (r_pathTracingRigidRouteMaxInstances.GetInteger() < presetRigidRouteMaxInstances)
    {
        r_pathTracingRigidRouteMaxInstances.SetInteger(presetRigidRouteMaxInstances);
    }

    common->Printf("PathTracePrimaryPass: applied %s preset %d source3=1 rigidRoute=1 rigidResidency=1 staticPreload=1 rigidEmissiveCards=1 portalSteps=%d lightAreaDiag=%d lightAreaApply=%d bvhValidation=%d churn=%d overflowMax=512 rigidRouteMax=%d\n",
        label ? label : "mode test",
        preset,
        portalSteps,
        mode20 && preset >= 2 ? 1 : 0,
        mode20 && preset == 3 ? 1 : 0,
        preset == 4 ? 1 : 0,
        mode20 && preset >= 2 ? 1 : 0,
        r_pathTracingRigidRouteMaxInstances.GetInteger());
}

nvrhi::ObjectType GetPathTraceCommandObjectType()
{
    if (deviceManager && deviceManager->GetGraphicsAPI() == nvrhi::GraphicsAPI::VULKAN)
    {
        return nvrhi::ObjectTypes::VK_CommandBuffer;
    }
    return nvrhi::ObjectTypes::D3D12_GraphicsCommandList;
}

RtSmokeStaticDrawSurfCounts CountCurrentStaticDrawSurfs(const viewDef_t* viewDef)
{
    RtSmokeStaticDrawSurfCounts counts;
    if (!viewDef || !viewDef->drawSurfs)
    {
        return counts;
    }

    for (int surfaceIndex = 0; surfaceIndex < viewDef->numDrawSurfs; ++surfaceIndex)
    {
        const drawSurf_t* drawSurf = viewDef->drawSurfs[surfaceIndex];
        const srfTriangles_t* tri = nullptr;
        if (!ValidateSmokeDrawSurface(viewDef, drawSurf, tri, nullptr))
        {
            continue;
        }
        if (ClassifySmokeSurface(viewDef, drawSurf, tri) != RtSmokeSurfaceClass::StaticWorld)
        {
            continue;
        }
        ++counts.surfaces;
        counts.triangles += tri->numIndexes / 3;
    }
    return counts;
}

void AppendRigidResidencyBoundsOverlayLines(
    const std::vector<RtPathTraceRigidResidencyBoundsBox>& boxes,
    std::vector<RtPathTraceBoundsOverlayLine>& lines)
{
    static const int edgeStarts[12] = { 0, 1, 2, 3, 4, 5, 6, 7, 0, 1, 2, 3 };
    static const int edgeEnds[12] = { 1, 2, 3, 0, 5, 6, 7, 4, 4, 5, 6, 7 };

    for (const RtPathTraceRigidResidencyBoundsBox& box : boxes)
    {
        if (!box.valid || lines.size() + 12 > RT_PT_BOUNDS_OVERLAY_MAX_LINES)
        {
            continue;
        }

        for (int edgeIndex = 0; edgeIndex < 12; ++edgeIndex)
        {
            RtPathTraceBoundsOverlayLine line;
            line.startAndPad = idVec4(box.corners[edgeStarts[edgeIndex]].x, box.corners[edgeStarts[edgeIndex]].y, box.corners[edgeStarts[edgeIndex]].z, 0.0f);
            line.endAndPad = idVec4(box.corners[edgeEnds[edgeIndex]].x, box.corners[edgeEnds[edgeIndex]].y, box.corners[edgeEnds[edgeIndex]].z, 0.0f);
            line.color = box.color;
            lines.push_back(line);
        }
    }
}

int CountSmokeDynamicSurfaces(const RtSmokeSurfaceClassStats& stats)
{
    return stats.rigidEntitySurfaces + stats.skinnedDeformedSurfaces + stats.particleAlphaSurfaces + stats.unknownSurfaces;
}

int CountSmokeDynamicTriangles(const RtSmokeSurfaceClassStats& stats)
{
    return stats.rigidEntityTriangles + stats.skinnedDeformedTriangles + stats.particleAlphaTriangles + stats.unknownTriangles;
}

std::vector<uint32_t> BuildSortedUniqueMaterialIds(const std::vector<uint32_t>& materialIds)
{
    std::vector<uint32_t> uniqueIds = materialIds;
    std::sort(uniqueIds.begin(), uniqueIds.end());
    uniqueIds.erase(std::unique(uniqueIds.begin(), uniqueIds.end()), uniqueIds.end());
    return uniqueIds;
}

struct RtSmokeSourceCompareMaterialDiff
{
    int oldUnique = 0;
    int source3Unique = 0;
    int missing = 0;
    int extra = 0;
    uint32_t missingSamples[8] = {};
    uint32_t extraSamples[8] = {};
    int missingSampleCount = 0;
    int extraSampleCount = 0;
};

RtSmokeSourceCompareMaterialDiff CompareSmokeDynamicMaterialIds(
    const std::vector<uint32_t>& oldMaterialIds,
    const std::vector<uint32_t>& source3MaterialIds)
{
    RtSmokeSourceCompareMaterialDiff diff;
    const std::vector<uint32_t> oldUnique = BuildSortedUniqueMaterialIds(oldMaterialIds);
    const std::vector<uint32_t> source3Unique = BuildSortedUniqueMaterialIds(source3MaterialIds);
    diff.oldUnique = static_cast<int>(oldUnique.size());
    diff.source3Unique = static_cast<int>(source3Unique.size());

    size_t oldIndex = 0;
    size_t source3Index = 0;
    while (oldIndex < oldUnique.size() || source3Index < source3Unique.size())
    {
        if (source3Index >= source3Unique.size() || (oldIndex < oldUnique.size() && oldUnique[oldIndex] < source3Unique[source3Index]))
        {
            if (diff.missingSampleCount < static_cast<int>(sizeof(diff.missingSamples) / sizeof(diff.missingSamples[0])))
            {
                diff.missingSamples[diff.missingSampleCount++] = oldUnique[oldIndex];
            }
            ++diff.missing;
            ++oldIndex;
        }
        else if (oldIndex >= oldUnique.size() || source3Unique[source3Index] < oldUnique[oldIndex])
        {
            if (diff.extraSampleCount < static_cast<int>(sizeof(diff.extraSamples) / sizeof(diff.extraSamples[0])))
            {
                diff.extraSamples[diff.extraSampleCount++] = source3Unique[source3Index];
            }
            ++diff.extra;
            ++source3Index;
        }
        else
        {
            ++oldIndex;
            ++source3Index;
        }
    }
    return diff;
}

void PrintSmokeMaterialIdSamples(const char* label, const uint32_t* samples, int sampleCount)
{
    if (sampleCount <= 0)
    {
        return;
    }

    char sampleText[256];
    sampleText[0] = '\0';
    for (int sampleIndex = 0; sampleIndex < sampleCount; ++sampleIndex)
    {
        idStr::Append(sampleText, sizeof(sampleText), va("%s%u", sampleIndex == 0 ? "" : ",", samples[sampleIndex]));
    }
    common->Printf("PathTracePrimaryPass: PT source3 compare %s materialIds=[%s]\n", label, sampleText);
}

void DumpSource3CaptureCompare(
    const viewDef_t* viewDef,
    uint64 frameIndex,
    const RtSmokeSurfaceClassStats& source3ClassStats,
    const RtSmokeSurfaceSkipStats& source3SkipStats,
    int source3Surfaces,
    int source3Verts,
    int source3Indexes,
    const std::vector<uint32_t>& source3DynamicIndexes,
    const std::vector<uint32_t>& source3DynamicTriangleMaterialIds)
{
    std::vector<PathTraceSmokeVertex> oldDynamicVertices;
    std::vector<uint32_t> oldDynamicIndexes;
    std::vector<uint32_t> oldDynamicTriangleClasses;
    std::vector<uint32_t> oldDynamicTriangleMaterialIds;
    RtSmokeGeometryUniverse oldGeometryUniverse;
    bool oldStaticCacheChanged = false;
    idVec3 oldSceneOrigin = vec3_origin;
    int oldSurfaces = 0;
    int oldVerts = 0;
    int oldIndexes = 0;
    int oldAnchorTriangle = -1;
    RtSmokeSurfaceClassStats oldClassStats;
    RtSmokeSurfaceSkipStats oldSkipStats;
    RtSmokeDynamicGeometryStats oldDynamicStats;
    RtSmokeAttributeStats oldAttributeStats;
    RtSmokeMaterialStats oldMaterialStats;
    RtSmokeBucketRanges oldBucketRanges;
    RtSmokeSceneCaptureTiming oldCaptureTiming;

    oldGeometryUniverse.BeginFrame(frameIndex);
    const bool oldCaptured = CaptureDoomSurfacesForSmokeTest(
        viewDef,
        oldDynamicVertices,
        oldDynamicIndexes,
        oldDynamicTriangleClasses,
        oldDynamicTriangleMaterialIds,
        oldGeometryUniverse,
        oldStaticCacheChanged,
        oldSceneOrigin,
        oldSurfaces,
        oldVerts,
        oldIndexes,
        oldAnchorTriangle,
        oldClassStats,
        oldSkipStats,
        oldDynamicStats,
        oldAttributeStats,
        oldMaterialStats,
        oldBucketRanges,
        oldCaptureTiming,
        nullptr,
        nullptr,
        false,
        false,
        false);
    oldGeometryUniverse.EndFrame();

    const RtSmokeGeometryUniverseStats oldGeometryStats = oldGeometryUniverse.GetStats(false);
    const RtSmokeSourceCompareMaterialDiff materialDiff = CompareSmokeDynamicMaterialIds(oldDynamicTriangleMaterialIds, source3DynamicTriangleMaterialIds);

    common->Printf("PathTracePrimaryPass: PT source3 compare capturedOld=%d totals old/source3 surfaces=%d/%d verts=%d/%d indexes=%d/%d staticTris=%d/%d dynTris=%d/%d oldStaticCache tris=%d records=%d\n",
        oldCaptured ? 1 : 0,
        oldSurfaces,
        source3Surfaces,
        oldVerts,
        source3Verts,
        oldIndexes,
        source3Indexes,
        oldClassStats.staticWorldTriangles,
        source3ClassStats.staticWorldTriangles,
        CountSmokeDynamicTriangles(oldClassStats),
        CountSmokeDynamicTriangles(source3ClassStats),
        oldGeometryStats.staticTriangles,
        oldGeometryStats.staticRecords);
    common->Printf("PathTracePrimaryPass: PT source3 compare classes old/source3 static=%d/%d rigid=%d/%d skinned=%d/%d particle=%d/%d unknown=%d/%d dynamicSurfaces=%d/%d dynamicIndexes=%d/%d\n",
        oldClassStats.staticWorldSurfaces,
        source3ClassStats.staticWorldSurfaces,
        oldClassStats.rigidEntitySurfaces,
        source3ClassStats.rigidEntitySurfaces,
        oldClassStats.skinnedDeformedSurfaces,
        source3ClassStats.skinnedDeformedSurfaces,
        oldClassStats.particleAlphaSurfaces,
        source3ClassStats.particleAlphaSurfaces,
        oldClassStats.unknownSurfaces,
        source3ClassStats.unknownSurfaces,
        CountSmokeDynamicSurfaces(oldClassStats),
        CountSmokeDynamicSurfaces(source3ClassStats),
        static_cast<int>(oldDynamicIndexes.size()),
        static_cast<int>(source3DynamicIndexes.size()));
    common->Printf("PathTracePrimaryPass: PT source3 compare dynamicMaterials unique old/source3=%d/%d missing=%d extra=%d triangleIds=%d/%d\n",
        materialDiff.oldUnique,
        materialDiff.source3Unique,
        materialDiff.missing,
        materialDiff.extra,
        static_cast<int>(oldDynamicTriangleMaterialIds.size()),
        static_cast<int>(source3DynamicTriangleMaterialIds.size()));
    PrintSmokeMaterialIdSamples("missingFromSource3", materialDiff.missingSamples, materialDiff.missingSampleCount);
    PrintSmokeMaterialIdSamples("extraInSource3", materialDiff.extraSamples, materialDiff.extraSampleCount);
    common->Printf("PathTracePrimaryPass: PT source3 compare skips old/source3 null=%d/%d geom=%d/%d material=%d/%d space=%d/%d model=%d/%d invalid=%d/%d nonCurrent=%d/%d limits=%d/%d zero=%d/%d gui=%d/%d callback=%d/%d\n",
        oldSkipStats.nullSurface,
        source3SkipStats.nullSurface,
        oldSkipStats.missingGeometry,
        source3SkipStats.missingGeometry,
        oldSkipStats.nullMaterial,
        source3SkipStats.nullMaterial,
        oldSkipStats.nullSpace,
        source3SkipStats.nullSpace,
        oldSkipStats.nullModel,
        source3SkipStats.nullModel,
        oldSkipStats.invalidIndexCount,
        source3SkipStats.invalidIndexCount,
        oldSkipStats.nonCurrentCache,
        source3SkipStats.nonCurrentCache,
        oldSkipStats.limitExceeded,
        source3SkipStats.limitExceeded,
        oldSkipStats.zeroAreaOnly,
        source3SkipStats.zeroAreaOnly,
        oldSkipStats.guiSurface,
        source3SkipStats.guiSurface,
        oldSkipStats.callbackEntity,
        source3SkipStats.callbackEntity);
}

}

void PathTracePrimaryPass::BuildRayTracingSmokeTestScene(const viewDef_t* viewDef)
{
    OPTICK_EVENT("PT Build Scene");

    const int sceneStartMs = Sys_Milliseconds();
    const int mode18Preset = r_pathTracingMode18TestPreset.GetInteger();
    if (mode18Preset != 0)
    {
        ApplySmokeRoutedScenePreset(18, mode18Preset, "mode18 test");
        r_pathTracingMode18TestPreset.SetInteger(0);
    }
    const int mode20Preset = r_pathTracingMode20TestPreset.GetInteger();
    if (mode20Preset != 0)
    {
        ApplySmokeRoutedScenePreset(20, mode20Preset, "mode20 test");
        r_pathTracingMode20TestPreset.SetInteger(0);
    }
    m_smokeSceneBuilt = false;
    m_smokeBoundsOverlayLines.clear();
    m_smokeBoundsOverlayLineCount = 0;
    m_smokeBoundsOverlayViewValid = false;
    const int requestedDebugMode = idMath::ClampInt(0, 43, r_pathTracingDebugMode.GetInteger());
    const bool restirPTDebugMode = requestedDebugMode >= 26 && requestedDebugMode <= 33;
    const bool integratorDebugMode = requestedDebugMode >= 34 && requestedDebugMode <= 37;
    const bool enableTextureProbe = (requestedDebugMode >= 8 && requestedDebugMode <= 20) || restirPTDebugMode || integratorDebugMode || requestedDebugMode == 38 || requestedDebugMode == 39 || requestedDebugMode == 40 || requestedDebugMode == 41 || requestedDebugMode == 42 || requestedDebugMode == 43;

    if (!m_smokeTlas || !m_smokeBindingLayout || !m_smokeTextureBindlessLayout || !m_smokeTextureDescriptorTable || !m_frameResources.outputTexture || !m_frameResources.accumulationTexture || !m_smokeConstantsBuffer || !m_smokeBoundsOverlayLineBuffer)
    {
        return;
    }
    idRenderWorldLocal* renderWorld = viewDef ? viewDef->renderWorld : nullptr;
    if (renderWorld)
    {
        const bool renderWorldChanged = m_smokeSceneRenderWorld != renderWorld;
        const bool mapChanged = m_smokeSceneMapName.Icmp(renderWorld->mapName) != 0 || m_smokeSceneMapTimeStamp != renderWorld->mapTimeStamp;
        if (renderWorldChanged || mapChanged)
        {
            if (m_smokeSceneRenderWorld || m_smokeSceneMapName.Length() > 0)
            {
                common->Printf("PathTracePrimaryPass: PT render world map changed '%s' -> '%s'; clearing scene caches\n",
                    m_smokeSceneMapName.c_str(),
                    renderWorld->mapName.c_str());
            }
            m_frameResources.smokeAccumulationSignature = 0;
            m_frameResources.smokeAccumulationFrameCount = 0;
            m_smokeBindingSet = nullptr;
            m_smokeSceneBuilt = false;
            m_smokeTestDispatched = false;
            m_frameResources.readbackQueued = false;
            m_frameResources.readbackDelayFrames = 0;
            m_frameResources.readbackCooldownFrames = 0;
            m_smokeStaticBlasCacheValid = false;
            m_smokeStaticBlasSignature = 0;
            m_smokeSceneUniverseStaticBuildGeneration = 0;
            m_smokeSceneRebuildLogged = false;
            m_smokeGeometryUniverse.Clear();
            m_smokeSkinnedSurfaceRecords.clear();
            m_smokePreviousSkinnedSurfaceRecords.clear();
            m_smokePreviousSkinnedVertexData.clear();
            m_smokePreviousSkinnedJointMatrices.clear();
            m_smokeSkinnedPreviousStats = RtSmokeSkinnedPreviousFrameStats();
            m_sceneUniverse.Clear();
            m_instanceUniverse.Clear();
            m_smokeLightUniverse.Clear();
            m_smokeLightUniverseRenderWorld = nullptr;
            m_smokeStaticBlas = nullptr;
            m_smokeDynamicBlas = nullptr;
            m_smokeStaticVertexBuffer = nullptr;
            m_smokeStaticIndexBuffer = nullptr;
            m_smokeStaticTriangleClassBuffer = nullptr;
            m_smokeStaticTriangleMaterialBuffer = nullptr;
            m_smokeStaticTriangleMaterialIndexBuffer = nullptr;
            m_smokePreviousStaticVertexBuffer = nullptr;
            m_smokePreviousStaticIndexBuffer = nullptr;
            m_smokePreviousStaticTriangleClassBuffer = nullptr;
            m_smokePreviousStaticTriangleMaterialBuffer = nullptr;
            m_smokePreviousStaticTriangleMaterialIndexBuffer = nullptr;
            m_smokePreviousStaticTriangleMaterialIndexes.clear();
            m_smokePreviousStaticSnapshotUploadSignature = 0;
            m_smokeDynamicVertexBuffer = nullptr;
            m_smokeDynamicIndexBuffer = nullptr;
            m_smokeDynamicTriangleClassBuffer = nullptr;
            m_smokeDynamicTriangleMaterialBuffer = nullptr;
            m_smokeDynamicTriangleMaterialIndexBuffer = nullptr;
            m_smokeMaterialTableBuffer = nullptr;
            m_smokeEmissiveTriangleBuffer = nullptr;
            m_smokeLightCandidateBuffer = nullptr;
            m_smokeDoomAnalyticLightBuffer = nullptr;
            m_smokeRigidRouteVertexBuffer = nullptr;
            m_smokeRigidRouteIndexBuffer = nullptr;
            m_smokeRigidRouteTriangleMaterialBuffer = nullptr;
            m_smokeRigidRouteTriangleMaterialIndexBuffer = nullptr;
            m_smokeRigidRouteInstanceBuffer = nullptr;
            m_smokeSkinnedSourceVertexBuffer = nullptr;
            m_smokeSkinnedCurrentOutputVertexBuffer = nullptr;
            m_smokeSkinnedPreviousPositionBuffer = nullptr;
            m_smokeSkinnedSurfaceDispatchBuffer = nullptr;
            m_smokeSkinnedCurrentJointMatrixBuffer = nullptr;
            m_smokeSkinnedPreviousJointMatrixBuffer = nullptr;
            m_smokeActiveTextureTable.clear();
            m_smokeMaterialTableEntryCount = 0;
            m_smokeEmissiveTriangleCount = 0;
            m_smokeEmissiveStaticTriangleCount = 0;
            m_smokeEmissiveDynamicTriangleCount = 0;
            m_smokeLightCandidateCount = 0;
            m_smokeTexturedLightCandidateCount = 0;
            m_smokeLightCandidateBytes = 0;
            m_smokeDoomAnalyticLightCount = 0;
            m_smokeDoomAnalyticLightBytes = 0;
            m_frameResources.smokeReservoirSceneSignature = 0;
            m_frameResources.smokeReservoirDispatchSignature = 0;
            m_frameResources.smokeReservoirNeedsClear = true;
            m_frameResources.smokeReservoirResetCount = 0;
            m_frameResources.smokeReservoirClearCount = 0;
            m_frameResources.MarkResetReason(RT_FRAME_RESET_SCENE_RESOURCES | RT_FRAME_RESET_RESERVOIR_SCENE_SIGNATURE);
            m_smokeSceneRenderWorld = renderWorld;
            m_smokeSceneMapName = renderWorld->mapName;
            m_smokeSceneMapTimeStamp = renderWorld->mapTimeStamp;
        }
    }
    if (viewDef && m_smokeLightUniverseRenderWorld != viewDef->renderWorld)
    {
        m_smokeLightUniverse.Clear();
        m_smokeLightUniverseRenderWorld = viewDef->renderWorld;
    }

    nvrhi::IDevice* device = deviceManager ? deviceManager->GetDevice() : nullptr;
    if (!device)
    {
        return;
    }

    if (viewDef)
    {
        m_smokeBoundsOverlayCameraOrigin = viewDef->renderView.vieworg;
        m_smokeBoundsOverlayCameraForward = viewDef->renderView.viewaxis[0];
        m_smokeBoundsOverlayCameraLeft = viewDef->renderView.viewaxis[1];
        m_smokeBoundsOverlayCameraUp = viewDef->renderView.viewaxis[2];
        m_smokeBoundsOverlayCameraForward.Normalize();
        m_smokeBoundsOverlayCameraLeft.Normalize();
        m_smokeBoundsOverlayCameraUp.Normalize();
        m_smokeBoundsOverlayTanX = idMath::Tan(DEG2RAD(viewDef->renderView.fov_x * 0.5f));
        m_smokeBoundsOverlayTanY = idMath::Tan(DEG2RAD(viewDef->renderView.fov_y * 0.5f));
        for (int matrixElement = 0; matrixElement < 16; ++matrixElement)
        {
            m_smokeBoundsOverlayModelViewMatrix[matrixElement] = viewDef->worldSpace.modelViewMatrix[matrixElement];
            m_smokeBoundsOverlayProjectionMatrix[matrixElement] = viewDef->projectionMatrix[matrixElement];
        }
        m_smokeBoundsOverlayViewValid = true;
    }

    nvrhi::ICommandList* commandList = m_backend ? m_backend->GL_GetCommandList() : nullptr;
    if (!commandList)
    {
        return;
    }
    const bool optickGpuMarkers = r_pathTracingOptickGpuMarkers.GetInteger() != 0;
    if (optickGpuMarkers)
    {
        OPTICK_GPU_CONTEXT((void*)commandList->getNativeObject(GetPathTraceCommandObjectType()));
    }

    std::vector<PathTraceSmokeVertex> dynamicVertexData;
    std::vector<uint32_t> dynamicIndexData;
    std::vector<uint32_t> dynamicTriangleClassData;
    std::vector<uint32_t> dynamicTriangleMaterialData;
    std::vector<RtSmokeSkinnedSurfaceRecord> currentSkinnedSurfaceRecords;
    RtSmokeSkinnedGpuScaffoldBuild skinnedGpuScaffold;
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
    RtSmokeSceneCaptureTiming captureTiming;
    std::vector<PathTraceSmokeVertex>& staticVertexCache = m_smokeGeometryUniverse.StaticVertices();
    std::vector<uint32_t>& staticIndexCache = m_smokeGeometryUniverse.StaticIndexes();
    std::vector<uint32_t>& staticTriangleClassCache = m_smokeGeometryUniverse.StaticTriangleClasses();
    std::vector<uint32_t>& staticTriangleMaterialCache = m_smokeGeometryUniverse.StaticTriangleMaterials();
    const std::vector<PathTraceSmokeVertex>& previousStaticVertexCache = m_smokeGeometryUniverse.PreviousStaticVertices();
    const std::vector<uint32_t>& previousStaticIndexCache = m_smokeGeometryUniverse.PreviousStaticIndexes();
    const std::vector<uint32_t>& previousStaticTriangleClassCache = m_smokeGeometryUniverse.PreviousStaticTriangleClasses();
    const std::vector<uint32_t>& previousStaticTriangleMaterialCache = m_smokeGeometryUniverse.PreviousStaticTriangleMaterials();
    const std::vector<uint32_t>& previousStaticTriangleMaterialIndexCache = m_smokePreviousStaticTriangleMaterialIndexes;
    const int captureStartMs = Sys_Milliseconds();
    bool usingDoomSurfaces = false;
    const int sceneSource = idMath::ClampInt(0, 3, r_pathTracingSceneSource.GetInteger());
    const bool useSceneUniverseStaticGeometry = sceneSource == 2;
    const bool useDrawSurfMirrorDynamicFrame = sceneSource == 3;
    const bool enableRigidRouteForMode =
        useDrawSurfMirrorDynamicFrame &&
        r_pathTracingRigidTlasRoute.GetInteger() != 0 &&
        r_pathTracingRigidBlasGpuScaffold.GetInteger() != 0 &&
        r_pathTracingRigidBlasGpuBuild.GetInteger() != 0 &&
        (requestedDebugMode == 23 || requestedDebugMode == 24 || requestedDebugMode == 25 ||
            requestedDebugMode == 39 ||
            requestedDebugMode == 40 ||
            requestedDebugMode == 41 ||
            requestedDebugMode == 42 ||
            requestedDebugMode == 43 ||
            restirPTDebugMode ||
            integratorDebugMode ||
            (requestedDebugMode == 18 && r_pathTracingRigidRouteMode18.GetInteger() != 0) ||
            (requestedDebugMode == 20 && r_pathTracingRigidRouteMode20.GetInteger() != 0));
    const bool rigidResidencyBoundsDebug = requestedDebugMode == 21 || requestedDebugMode == 22;
    const bool rigidResidencyEnabled =
        r_pathTracingRigidResidency.GetInteger() != 0 &&
        (enableRigidRouteForMode || rigidResidencyBoundsDebug);
    const int source2RigidEntities = sceneSource == 2 ? idMath::ClampInt(0, 2, r_pathTracingSceneSource2RigidEntities.GetInteger()) : 0;
    const bool dumpSceneUniverse = r_pathTracingSceneUniverseDump.GetInteger() != 0;
    const bool dumpInstanceUniverse = r_pathTracingInstanceUniverseDump.GetInteger() != 0;
    const bool dumpRigidMeshUniverse = r_pathTracingRigidMeshUniverseDump.GetInteger() != 0;
    const RtSmokeStaticDrawSurfCounts currentStaticDrawSurfs = useSceneUniverseStaticGeometry ? CountCurrentStaticDrawSurfs(viewDef) : RtSmokeStaticDrawSurfCounts();
    if (sceneSource != m_smokeSceneSourceLast || (useSceneUniverseStaticGeometry && source2RigidEntities != m_smokeSceneSource2RigidEntitiesLast))
    {
        common->Printf("PathTracePrimaryPass: PT scene source changed %d/%d -> %d/%d; clearing static geometry cache\n",
            m_smokeSceneSourceLast,
            m_smokeSceneSource2RigidEntitiesLast,
            sceneSource,
            source2RigidEntities);
        m_smokeGeometryUniverse.Clear();
        m_smokeSkinnedSurfaceRecords.clear();
        m_smokePreviousSkinnedSurfaceRecords.clear();
        m_smokePreviousSkinnedVertexData.clear();
        m_smokePreviousSkinnedJointMatrices.clear();
        m_smokePreviousStaticTriangleMaterialIndexes.clear();
        m_smokePreviousStaticSnapshotUploadSignature = 0;
        m_smokeSkinnedPreviousStats = RtSmokeSkinnedPreviousFrameStats();
        m_smokeStaticBlasCacheValid = false;
        m_smokeStaticBlasSignature = 0;
        m_smokeSceneUniverseStaticBuildGeneration = 0;
        m_smokeSceneRebuildLogged = false;
        m_smokeSceneSourceLast = sceneSource;
        m_smokeSceneSource2RigidEntitiesLast = source2RigidEntities;
    }
    uint64 sceneUniverseGeneration = 0;
    if (useSceneUniverseStaticGeometry && m_sceneUniverse.EnsureBuilt(viewDef))
    {
        sceneUniverseGeneration = m_sceneUniverse.GetStats().generation;
        if (m_smokeSceneUniverseStaticBuildGeneration != 0 && m_smokeSceneUniverseStaticBuildGeneration != sceneUniverseGeneration)
        {
            common->Printf("PathTracePrimaryPass: PT scene universe generation changed %llu -> %llu; clearing source-2 static geometry cache\n",
                static_cast<unsigned long long>(m_smokeSceneUniverseStaticBuildGeneration),
                static_cast<unsigned long long>(sceneUniverseGeneration));
            m_smokeGeometryUniverse.Clear();
            m_smokeSkinnedSurfaceRecords.clear();
            m_smokePreviousSkinnedSurfaceRecords.clear();
            m_smokePreviousSkinnedVertexData.clear();
            m_smokePreviousSkinnedJointMatrices.clear();
            m_smokePreviousStaticTriangleMaterialIndexes.clear();
            m_smokePreviousStaticSnapshotUploadSignature = 0;
            m_smokeSkinnedPreviousStats = RtSmokeSkinnedPreviousFrameStats();
            m_smokeStaticBlasCacheValid = false;
            m_smokeStaticBlasSignature = 0;
            m_smokeSceneUniverseStaticBuildGeneration = 0;
            m_smokeSceneRebuildLogged = false;
        }
    }
    if (useSceneUniverseStaticGeometry && source2RigidEntities != 0)
    {
        m_smokeGeometryUniverse.Clear();
        m_smokeSkinnedSurfaceRecords.clear();
        m_smokePreviousSkinnedSurfaceRecords.clear();
        m_smokePreviousSkinnedVertexData.clear();
        m_smokePreviousSkinnedJointMatrices.clear();
        m_smokePreviousStaticTriangleMaterialIndexes.clear();
        m_smokePreviousStaticSnapshotUploadSignature = 0;
        m_smokeSkinnedPreviousStats = RtSmokeSkinnedPreviousFrameStats();
        m_smokeStaticBlasCacheValid = false;
        m_smokeStaticBlasSignature = 0;
        m_smokeSceneUniverseStaticBuildGeneration = 0;
    }
    RtPathTraceSceneUniverseBuildStats sceneUniverseStaticBuildStats;
    {
        OPTICK_EVENT("PT Capture Doom Surfaces");
        m_smokeGeometryUniverse.BeginFrame(++m_smokeGeometryFrameIndex);
        if (useSceneUniverseStaticGeometry)
        {
            OPTICK_EVENT("PT Build Scene Universe Static Geometry");
            RtSmokeSurfaceClassStats sceneUniverseClassStats;
            RtSmokeSurfaceSkipStats sceneUniverseSkipStats;
            RtSmokeAttributeStats sceneUniverseAttributeStats;
            RtSmokeMaterialStats sceneUniverseMaterialStats;
            RtSmokeBucketRanges sceneUniverseBucketRanges;
            sceneUniverseStaticBuildStats = m_sceneUniverse.BuildFullStaticGeometry(viewDef, m_smokeGeometryUniverse, sceneUniverseClassStats, sceneUniverseSkipStats, sceneUniverseAttributeStats, sceneUniverseMaterialStats, sceneUniverseBucketRanges);
            skipStats.invalidIndexCount += sceneUniverseSkipStats.invalidIndexCount;
            skipStats.limitExceeded += sceneUniverseSkipStats.limitExceeded;
            skipStats.zeroAreaOnly += sceneUniverseSkipStats.zeroAreaOnly;
        }
        if (useDrawSurfMirrorDynamicFrame)
        {
            usingDoomSurfaces = CaptureDoomSurfacesForSmokeTest(viewDef, dynamicVertexData, dynamicIndexData, dynamicTriangleClassData, dynamicTriangleMaterialData, m_smokeGeometryUniverse, staticCacheChanged, m_smokeSceneOrigin, sourceSurfaces, sourceVerts, sourceIndexes, anchorTriangle, classStats, skipStats, dynamicStats, attributeStats, materialStats, bucketRanges, captureTiming, dumpClassReasons ? &reasonSamples : nullptr, &currentSkinnedSurfaceRecords, false, false, true);
            if (r_pathTracingStaticAreaPreload.GetInteger() != 0)
            {
                const int staticRecordsBefore = static_cast<int>(m_smokeGeometryUniverse.StaticSurfaceRecords().size());
                const int staticVertsBefore = static_cast<int>(m_smokeGeometryUniverse.StaticVertices().size());
                const RtPathTraceSceneUniverseBuildStats staticAreaPreloadStats = m_sceneUniverse.BuildSelectedStaticGeometry(
                    viewDef,
                    m_smokeGeometryUniverse,
                    classStats,
                    skipStats,
                    attributeStats,
                    materialStats,
                    bucketRanges,
                    idMath::ClampInt(0, 8, r_pathTracingStaticAreaPreloadPortalSteps.GetInteger()),
                    r_pathTracingStaticAreaPreloadDump.GetInteger() != 0);
                if (staticAreaPreloadStats.built)
                {
                    usingDoomSurfaces = true;
                    sourceSurfaces += staticAreaPreloadStats.surfaces;
                    sourceVerts += staticAreaPreloadStats.vertices;
                    sourceIndexes += staticAreaPreloadStats.indexes;
                    if (static_cast<int>(m_smokeGeometryUniverse.StaticSurfaceRecords().size()) != staticRecordsBefore ||
                        static_cast<int>(m_smokeGeometryUniverse.StaticVertices().size()) != staticVertsBefore)
                    {
                        staticCacheChanged = true;
                    }
                }
                if (r_pathTracingStaticAreaPreloadDump.GetInteger() != 0)
                {
                    r_pathTracingStaticAreaPreloadDump.SetInteger(0);
                }
            }
            const RtSmokeSurfaceClassStats staticClassStats = classStats;
            const RtSmokeBucketRange staticBucketRange = bucketRanges.buckets[0];
            const int staticSourceSurfaces = classStats.staticWorldSurfaces;
            const int staticSourceVerts = classStats.staticWorldVerts;
            const int staticSourceIndexes = classStats.staticWorldIndexes;

            RtSmokeSurfaceClassStats mirrorClassStats;
            RtSmokeSurfaceSkipStats mirrorSkipStats;
            RtSmokeDynamicGeometryStats mirrorDynamicStats;
            RtSmokeAttributeStats mirrorAttributeStats;
            RtSmokeMaterialStats mirrorMaterialStats;
            RtSmokeBucketRanges mirrorBucketRanges;
            RtSmokeSceneCaptureTiming mirrorCaptureTiming;
            RtSmokeSurfaceClassReasonSamples mirrorReasonSamples;
            int mirrorSourceSurfaces = 0;
            int mirrorSourceVerts = 0;
            int mirrorSourceIndexes = 0;
            const bool usingMirrorDynamicFrame = CapturePathTraceDynamicFrameFromDrawSurfMirror(viewDef, nullptr, &m_smokeGeometryUniverse, dynamicVertexData, dynamicIndexData, dynamicTriangleClassData, dynamicTriangleMaterialData, mirrorSourceSurfaces, mirrorSourceVerts, mirrorSourceIndexes, mirrorClassStats, mirrorSkipStats, mirrorDynamicStats, mirrorAttributeStats, mirrorMaterialStats, mirrorBucketRanges, mirrorCaptureTiming, dumpClassReasons ? &mirrorReasonSamples : nullptr, &currentSkinnedSurfaceRecords);

            classStats = RtSmokeSurfaceClassStats();
            classStats.staticWorldSurfaces = staticClassStats.staticWorldSurfaces;
            classStats.staticWorldVerts = staticClassStats.staticWorldVerts;
            classStats.staticWorldIndexes = staticClassStats.staticWorldIndexes;
            classStats.staticWorldTriangles = staticClassStats.staticWorldTriangles;
            classStats.rigidEntitySurfaces = mirrorClassStats.rigidEntitySurfaces;
            classStats.rigidEntityVerts = mirrorClassStats.rigidEntityVerts;
            classStats.rigidEntityIndexes = mirrorClassStats.rigidEntityIndexes;
            classStats.rigidEntityTriangles = mirrorClassStats.rigidEntityTriangles;
            classStats.skinnedDeformedSurfaces = mirrorClassStats.skinnedDeformedSurfaces;
            classStats.skinnedDeformedVerts = mirrorClassStats.skinnedDeformedVerts;
            classStats.skinnedDeformedIndexes = mirrorClassStats.skinnedDeformedIndexes;
            classStats.skinnedDeformedTriangles = mirrorClassStats.skinnedDeformedTriangles;
            classStats.particleAlphaSurfaces = mirrorClassStats.particleAlphaSurfaces;
            classStats.particleAlphaVerts = mirrorClassStats.particleAlphaVerts;
            classStats.particleAlphaIndexes = mirrorClassStats.particleAlphaIndexes;
            classStats.particleAlphaTriangles = mirrorClassStats.particleAlphaTriangles;
            classStats.unknownSurfaces = mirrorClassStats.unknownSurfaces;
            classStats.unknownVerts = mirrorClassStats.unknownVerts;
            classStats.unknownIndexes = mirrorClassStats.unknownIndexes;
            classStats.unknownTriangles = mirrorClassStats.unknownTriangles;
            skipStats = mirrorSkipStats;
            dynamicStats = mirrorDynamicStats;
            attributeStats = mirrorAttributeStats;
            materialStats = mirrorMaterialStats;
            bucketRanges = mirrorBucketRanges;
            bucketRanges.buckets[0] = staticBucketRange;
            captureTiming.dynamicPassClassifyMs += mirrorCaptureTiming.dynamicPassClassifyMs;
            captureTiming.dynamicAppendMs += mirrorCaptureTiming.dynamicAppendMs;
            captureTiming.rtCpuSkinningAppendMs += mirrorCaptureTiming.rtCpuSkinningAppendMs;
            captureTiming.bucketMergeMs += mirrorCaptureTiming.bucketMergeMs;
            captureTiming.appendMs += mirrorCaptureTiming.appendMs;
            captureTiming.validationMs += mirrorCaptureTiming.validationMs;
            sourceSurfaces = staticSourceSurfaces + mirrorSourceSurfaces;
            sourceVerts = staticSourceVerts + mirrorSourceVerts;
            sourceIndexes = staticSourceIndexes + mirrorSourceIndexes;
            usingDoomSurfaces = usingDoomSurfaces || usingMirrorDynamicFrame;
        }
        else
        {
            usingDoomSurfaces = CaptureDoomSurfacesForSmokeTest(viewDef, dynamicVertexData, dynamicIndexData, dynamicTriangleClassData, dynamicTriangleMaterialData, m_smokeGeometryUniverse, staticCacheChanged, m_smokeSceneOrigin, sourceSurfaces, sourceVerts, sourceIndexes, anchorTriangle, classStats, skipStats, dynamicStats, attributeStats, materialStats, bucketRanges, captureTiming, dumpClassReasons ? &reasonSamples : nullptr, &currentSkinnedSurfaceRecords, useSceneUniverseStaticGeometry, source2RigidEntities != 0);
        }
        {
            OPTICK_EVENT("PT DrawSurf Mirror");
            m_instanceUniverse.BeginFrame(m_smokeGeometryFrameIndex, viewDef);
            CapturePathTraceDrawSurfMirror(viewDef, useSceneUniverseStaticGeometry ? &m_sceneUniverse : nullptr, &m_smokeGeometryUniverse, m_instanceUniverse, &m_smokeBoundsOverlayLines);
            if (rigidResidencyEnabled)
            {
                m_smokeGeometryUniverse.RefreshRigidResidencyAreaWalk(
                    viewDef,
                    m_instanceUniverse,
                    idMath::ClampInt(0, 8, r_pathTracingRigidResidencyPortalSteps.GetInteger()));
            }
            m_smokeBoundsOverlayLineCount = static_cast<int>(m_smokeBoundsOverlayLines.size());
        }
        if (useSceneUniverseStaticGeometry)
        {
            if (sceneUniverseStaticBuildStats.built)
            {
                staticCacheChanged = staticCacheChanged || !sceneUniverseStaticBuildStats.cacheHit;
                sourceSurfaces += sceneUniverseStaticBuildStats.surfaces;
                sourceVerts += sceneUniverseStaticBuildStats.vertices;
                sourceIndexes += sceneUniverseStaticBuildStats.indexes;
                const int staticSceneUniverseSurfaces = Max(0, sceneUniverseStaticBuildStats.surfaces - sceneUniverseStaticBuildStats.rigidEntitySurfaces);
                const int staticSceneUniverseTriangles = Max(0, sceneUniverseStaticBuildStats.triangles - sceneUniverseStaticBuildStats.rigidEntityTriangles);
                classStats.staticWorldSurfaces += staticSceneUniverseSurfaces;
                classStats.staticWorldIndexes += staticSceneUniverseTriangles * 3;
                classStats.staticWorldTriangles += staticSceneUniverseTriangles;
                classStats.rigidEntitySurfaces += sceneUniverseStaticBuildStats.rigidEntitySurfaces;
                classStats.rigidEntityIndexes += sceneUniverseStaticBuildStats.rigidEntityTriangles * 3;
                classStats.rigidEntityTriangles += sceneUniverseStaticBuildStats.rigidEntityTriangles;
                bucketRanges.buckets[0].surfaceCount += sceneUniverseStaticBuildStats.surfaces;
                usingDoomSurfaces = true;
                sceneUniverseGeneration = m_sceneUniverse.GetStats().generation;
                m_smokeSceneUniverseStaticBuildGeneration = sceneUniverseGeneration;
            }
        }
        m_smokeGeometryUniverse.EndFrame();
    }
    std::vector<PathTraceSmokeVertex> nextPreviousSkinnedVertexData;
    std::vector<PathTraceSkinnedJointMatrix> nextPreviousSkinnedJointMatrices;
    m_smokeSkinnedPreviousStats = UpdateSmokeSkinnedPreviousCpuBridge(
        currentSkinnedSurfaceRecords,
        m_smokePreviousSkinnedSurfaceRecords,
        m_smokePreviousSkinnedVertexData,
        dynamicVertexData,
        nextPreviousSkinnedVertexData);
    const int gpuSkinningMode = idMath::ClampInt(0, 2, r_pathTracingGpuSkinning.GetInteger());
    skinnedGpuScaffold = BuildSmokeSkinnedGpuScaffold(
        gpuSkinningMode,
        currentSkinnedSurfaceRecords,
        m_smokePreviousSkinnedSurfaceRecords,
        dynamicVertexData,
        m_smokePreviousSkinnedVertexData,
        m_smokePreviousSkinnedJointMatrices);
    RetainSmokeSkinnedCurrentJointMatrices(
        currentSkinnedSurfaceRecords,
        nextPreviousSkinnedJointMatrices);
    m_smokeSkinnedSurfaceRecords = currentSkinnedSurfaceRecords;
    m_smokePreviousSkinnedSurfaceRecords = m_smokeSkinnedSurfaceRecords;
    m_smokePreviousSkinnedVertexData.swap(nextPreviousSkinnedVertexData);
    m_smokePreviousSkinnedJointMatrices.swap(nextPreviousSkinnedJointMatrices);

    if (useDrawSurfMirrorDynamicFrame && r_pathTracingSceneSourceCompare.GetInteger() != 0)
    {
        DumpSource3CaptureCompare(
            viewDef,
            m_smokeGeometryFrameIndex,
            classStats,
            skipStats,
            sourceSurfaces,
            sourceVerts,
            sourceIndexes,
            dynamicIndexData,
            dynamicTriangleMaterialData);
        r_pathTracingSceneSourceCompare.SetInteger(0);
    }
    if (useDrawSurfMirrorDynamicFrame && r_pathTracingRigidMeshValidate.GetInteger() != 0)
    {
        const RtPathTraceRigidMeshValidationStats rigidMeshValidationStats =
            m_smokeGeometryUniverse.ValidateRigidMeshCandidatesAgainstDynamicPayload(
                dynamicTriangleClassData,
                dynamicTriangleMaterialData,
                RT_SMOKE_TRIANGLE_CLASS_MASK,
                SmokeSurfaceClassId(RtSmokeSurfaceClass::RigidEntity));
        m_smokeGeometryUniverse.DumpRigidMeshValidationStats(rigidMeshValidationStats, sceneSource);
        r_pathTracingRigidMeshValidate.SetInteger(0);
    }
    if (useDrawSurfMirrorDynamicFrame && r_pathTracingRigidBlasPlanDump.GetInteger() != 0)
    {
        const RtPathTraceRigidBlasPlanStats rigidBlasPlanStats = m_smokeGeometryUniverse.BuildRigidBlasPlanStats(&classStats);
        m_smokeGeometryUniverse.DumpRigidBlasPlanStats(rigidBlasPlanStats, sceneSource);
        r_pathTracingRigidBlasPlanDump.SetInteger(0);
    }
    if (useDrawSurfMirrorDynamicFrame && r_pathTracingRigidBlasInputDump.GetInteger() != 0)
    {
        const RtPathTraceRigidBlasInputStats rigidBlasInputStats = m_smokeGeometryUniverse.BuildRigidBlasInputStats();
        m_smokeGeometryUniverse.DumpRigidBlasInputStats(rigidBlasInputStats, sceneSource);
        r_pathTracingRigidBlasInputDump.SetInteger(0);
    }
    if (useDrawSurfMirrorDynamicFrame)
    {
        const bool rigidBlasGpuScaffold = r_pathTracingRigidBlasGpuScaffold.GetInteger() != 0;
        const bool rigidBlasGpuBuild = rigidBlasGpuScaffold && r_pathTracingRigidBlasGpuBuild.GetInteger() != 0;
        const bool dumpRigidBlasGpu = r_pathTracingRigidBlasGpuDump.GetInteger() != 0;
        if (rigidBlasGpuScaffold)
        {
            const RtPathTraceRigidBlasGpuStats rigidBlasGpuStats = m_smokeGeometryUniverse.UpdateRigidBlasGpuScaffold(device, commandList, rigidBlasGpuBuild);
            if (dumpRigidBlasGpu)
            {
                m_smokeGeometryUniverse.DumpRigidBlasGpuStats(rigidBlasGpuStats, sceneSource, true, rigidBlasGpuBuild);
                r_pathTracingRigidBlasGpuDump.SetInteger(0);
            }
        }
        else
        {
            m_smokeGeometryUniverse.ReleaseRigidBlasGpuScaffold();
            if (dumpRigidBlasGpu)
            {
                RtPathTraceRigidBlasGpuStats rigidBlasGpuStats;
                rigidBlasGpuStats.frameIndex = m_smokeGeometryFrameIndex;
                m_smokeGeometryUniverse.DumpRigidBlasGpuStats(rigidBlasGpuStats, sceneSource, false, false);
                r_pathTracingRigidBlasGpuDump.SetInteger(0);
            }
        }
    }
    RtPathTraceRigidResidencyStats rigidResidencyStats;
    if (useDrawSurfMirrorDynamicFrame)
    {
        rigidResidencyStats = m_smokeGeometryUniverse.UpdateRigidResidency(
            viewDef,
            m_instanceUniverse,
            rigidResidencyEnabled,
            idMath::ClampInt(0, 8, r_pathTracingRigidResidencyPortalSteps.GetInteger()));
        const int boundsOverlayMode = r_pathTracingSceneBoundsOverlay.GetInteger();
        const bool appendRigidResidencyBounds =
            rigidResidencyEnabled &&
            (boundsOverlayMode == 3 || boundsOverlayMode == 5 || (rigidResidencyBoundsDebug && boundsOverlayMode < 3));
        const bool appendStaticCacheBounds =
            boundsOverlayMode == 4 || boundsOverlayMode == 5 || boundsOverlayMode == 6;
        if ((appendRigidResidencyBounds || appendStaticCacheBounds) &&
            (rigidResidencyBoundsDebug || r_pathTracingSceneBoundsOverlay.GetInteger() != 0) &&
            static_cast<int>(m_smokeBoundsOverlayLines.size()) < RT_PT_BOUNDS_OVERLAY_MAX_LINES)
        {
            const int remainingLines = RT_PT_BOUNDS_OVERLAY_MAX_LINES - static_cast<int>(m_smokeBoundsOverlayLines.size());
            const int requestedResidentBoxes = Max(0, r_pathTracingSceneBoundsOverlayMax.GetInteger());
            int remainingBoxes = Min(Min(requestedResidentBoxes, RT_PT_RESIDENT_BOUNDS_OVERLAY_SAFE_BOXES), remainingLines / 12);
            if (appendStaticCacheBounds && remainingBoxes > 0)
            {
                std::vector<RtPathTraceRigidResidencyBoundsBox> staticBoundsBoxes;
                staticBoundsBoxes.reserve(remainingBoxes);
                m_smokeGeometryUniverse.CollectStaticSurfaceBoundsBoxes(staticBoundsBoxes, remainingBoxes, boundsOverlayMode == 6);
                AppendRigidResidencyBoundsOverlayLines(staticBoundsBoxes, m_smokeBoundsOverlayLines);
                remainingBoxes -= static_cast<int>(staticBoundsBoxes.size());
            }
            if (appendRigidResidencyBounds && remainingBoxes > 0)
            {
                std::vector<RtPathTraceRigidResidencyBoundsBox> residentBoundsBoxes;
                residentBoundsBoxes.reserve(remainingBoxes);
                m_smokeGeometryUniverse.CollectRigidResidencyBoundsBoxes(residentBoundsBoxes, remainingBoxes);
                AppendRigidResidencyBoundsOverlayLines(residentBoundsBoxes, m_smokeBoundsOverlayLines);
            }
            m_smokeBoundsOverlayLineCount = static_cast<int>(m_smokeBoundsOverlayLines.size());
        }
        if (r_pathTracingRigidResidencyDump.GetInteger() != 0)
        {
            m_smokeGeometryUniverse.DumpRigidResidencyStats(rigidResidencyStats, sceneSource);
            r_pathTracingRigidResidencyDump.SetInteger(0);
        }
    }
    if (useDrawSurfMirrorDynamicFrame && r_pathTracingRigidTlasPlanDump.GetInteger() != 0)
    {
        const RtPathTraceRigidTlasPlanStats rigidTlasPlanStats = m_smokeGeometryUniverse.BuildRigidTlasPlanStats(m_instanceUniverse, &classStats);
        m_smokeGeometryUniverse.DumpRigidTlasPlanStats(rigidTlasPlanStats, sceneSource);
        r_pathTracingRigidTlasPlanDump.SetInteger(0);
    }
    const int captureMs = Sys_Milliseconds() - captureStartMs;
    if (dumpInstanceUniverse || r_pathTracingSmokeLog.GetInteger() != 0)
    {
        RtPathTraceInstanceUniverseDiagnosticDesc instanceDiagnosticDesc;
        instanceDiagnosticDesc.dumpRequested = dumpInstanceUniverse;
        instanceDiagnosticDesc.sceneSource = sceneSource;
        instanceDiagnosticDesc.legacySourceSurfaces = sourceSurfaces;
        instanceDiagnosticDesc.legacyClassStats = &classStats;
        instanceDiagnosticDesc.legacySkipStats = &skipStats;
        m_instanceUniverse.RunDiagnostics(instanceDiagnosticDesc);
    }
    if (dumpRigidMeshUniverse || r_pathTracingSmokeLog.GetInteger() != 0)
    {
        m_smokeGeometryUniverse.RunRigidMeshCandidateDiagnostics(dumpRigidMeshUniverse, sceneSource, &classStats);
    }
    if (sceneSource > 0 || dumpSceneUniverse)
    {
        const int drawSurfStaticSurfaces = useSceneUniverseStaticGeometry ? currentStaticDrawSurfs.surfaces : classStats.staticWorldSurfaces;
        const int drawSurfStaticTriangles = useSceneUniverseStaticGeometry ? currentStaticDrawSurfs.triangles : classStats.staticWorldTriangles;
        m_sceneUniverse.RunDiagnostics(viewDef, &m_smokeGeometryUniverse, sceneSource, dumpSceneUniverse, drawSurfStaticSurfaces, drawSurfStaticTriangles);
        if (dumpSceneUniverse && useSceneUniverseStaticGeometry)
        {
            common->Printf("PathTracePrimaryPass: PT scene source2 staticBuild built=%d cacheHit=%d surfaces=%d triangles=%d verts=%d indexes=%d emissiveSurfaces=%d rigidEntities=%d/%d skipped invalid/limits/zero=%d/%d/%d sourceTotals=%d/%d/%d\n",
                sceneUniverseStaticBuildStats.built ? 1 : 0,
                sceneUniverseStaticBuildStats.cacheHit ? 1 : 0,
                sceneUniverseStaticBuildStats.surfaces,
                sceneUniverseStaticBuildStats.triangles,
                sceneUniverseStaticBuildStats.vertices,
                sceneUniverseStaticBuildStats.indexes,
                sceneUniverseStaticBuildStats.emissiveCapableSurfaces,
                sceneUniverseStaticBuildStats.rigidEntitySurfaces,
                sceneUniverseStaticBuildStats.rigidEntityTriangles,
                sceneUniverseStaticBuildStats.skippedInvalid,
                sceneUniverseStaticBuildStats.skippedLimits,
                sceneUniverseStaticBuildStats.skippedZeroArea,
                sourceSurfaces,
                sourceVerts,
                sourceIndexes);
        }
    }
    if (!usingDoomSurfaces)
    {
        if (!m_smokeWaitingForDoomSurfaceLogged)
        {
            common->Printf("PathTracePrimaryPass: waiting for center camera ray Doom surface hit to build RT smoke BLAS\n");
            m_smokeWaitingForDoomSurfaceLogged = true;
        }
        return;
    }

    RtSmokeMaterialMetadataRegistrationTiming metadataTiming;
    {
        OPTICK_EVENT("PT Register Material Metadata");
        metadataTiming = RegisterSmokeMaterialTextureInfoForFrame(viewDef, enableTextureProbe);
        if (r_pathTracingWorldStaticEmissives.GetInteger() != 0 || useSceneUniverseStaticGeometry)
        {
            const RtSmokeMaterialMetadataRegistrationTiming worldStaticMetadataTiming = RegisterSmokeWorldStaticMaterialTextureInfo(viewDef, enableTextureProbe);
            metadataTiming.metadataMs += worldStaticMetadataTiming.metadataMs;
            metadataTiming.registrationMs += worldStaticMetadataTiming.registrationMs;
        }
    }
    const int metadataMs = metadataTiming.metadataMs;
    const int metadataValidationMs = metadataTiming.validationMs;
    const int metadataRegistrationMs = metadataTiming.registrationMs;

    const int materialStartMs = Sys_Milliseconds();
    RtSmokeMaterialTableBuild materialTable;
    const int rigidRouteMaxInstances = idMath::ClampInt(1, 512, r_pathTracingRigidRouteMaxInstances.GetInteger());
    std::vector<uint32_t> fullLevelStaticEmissiveMaterialIds;
    if (r_pathTracingWorldStaticEmissives.GetInteger() != 0)
    {
        fullLevelStaticEmissiveMaterialIds = BuildSmokeWorldStaticEmissiveMaterialIds(viewDef);
    }
    std::vector<uint32_t> materialTableStaticIds = staticTriangleMaterialCache;
    materialTableStaticIds.insert(materialTableStaticIds.end(), fullLevelStaticEmissiveMaterialIds.begin(), fullLevelStaticEmissiveMaterialIds.end());
    if (enableRigidRouteForMode)
    {
        const std::vector<uint32_t> rigidRouteMaterialIds = m_smokeGeometryUniverse.CollectRigidRouteMaterialIds(m_instanceUniverse, rigidRouteMaxInstances);
        materialTableStaticIds.insert(materialTableStaticIds.end(), rigidRouteMaterialIds.begin(), rigidRouteMaterialIds.end());
    }
    uint64 materialTableSignature = 0;
    bool materialTableCacheHit = false;
    RtSmokeMaterialTableCompareStats materialUniverseTableCompareStats;
    const bool useMaterialUniverseTable = r_pathTracingMaterialUniverseTable.GetInteger() != 0;
    const bool validateMaterialUniverseTable = r_pathTracingMaterialUniverseTableValidate.GetInteger() != 0;
    const char* materialTablePath = useMaterialUniverseTable ? "universe" : "legacy";
    BeginSmokeMaterialUniverseFrame();
    if (useMaterialUniverseTable)
    {
        if (validateMaterialUniverseTable)
        {
            RtSmokeMaterialTableBuild legacyMaterialTable;
            uint32_t legacyLatchedTextureProbeMaterialId = m_smokeTextureProbeMaterialId;
            int legacyLatchedTextureProbeRequestedIndex = m_smokeTextureProbeRequestedIndex;
            BuildSmokeMaterialTableCached(legacyMaterialTable, materialTableStaticIds, dynamicTriangleMaterialData, legacyLatchedTextureProbeMaterialId, legacyLatchedTextureProbeRequestedIndex, enableTextureProbe, materialTableSignature, materialTableCacheHit);
            BuildSmokeMaterialTableFromUniverseCached(materialTable, materialTableStaticIds, dynamicTriangleMaterialData, m_smokeTextureProbeMaterialId, m_smokeTextureProbeRequestedIndex, enableTextureProbe, materialTableSignature, materialTableCacheHit);
            materialUniverseTableCompareStats = CompareSmokeMaterialTables(legacyMaterialTable, materialTable);
            if (materialUniverseTableCompareStats.mismatches > 0)
            {
                common->Printf("PathTracePrimaryPass: RT smoke material universe table mismatch, falling back to legacy table for this frame (mismatches=%d material=%d/%d/%d indexes=%d/%d textures=%d/%d)\n",
                    materialUniverseTableCompareStats.mismatches,
                    materialUniverseTableCompareStats.materialCountMismatches,
                    materialUniverseTableCompareStats.materialIdMismatches,
                    materialUniverseTableCompareStats.materialRecordMismatches,
                    materialUniverseTableCompareStats.staticIndexMismatches,
                    materialUniverseTableCompareStats.dynamicIndexMismatches,
                    materialUniverseTableCompareStats.textureCountMismatches,
                    materialUniverseTableCompareStats.textureHandleMismatches);
                materialTable = legacyMaterialTable;
                m_smokeTextureProbeMaterialId = legacyLatchedTextureProbeMaterialId;
                m_smokeTextureProbeRequestedIndex = legacyLatchedTextureProbeRequestedIndex;
                materialTablePath = "legacyFallback";
            }
        }
        else
        {
            BuildSmokeMaterialTableFromUniverseCached(materialTable, materialTableStaticIds, dynamicTriangleMaterialData, m_smokeTextureProbeMaterialId, m_smokeTextureProbeRequestedIndex, enableTextureProbe, materialTableSignature, materialTableCacheHit);
        }
    }
    else
    {
        BuildSmokeMaterialTableCached(materialTable, materialTableStaticIds, dynamicTriangleMaterialData, m_smokeTextureProbeMaterialId, m_smokeTextureProbeRequestedIndex, enableTextureProbe, materialTableSignature, materialTableCacheHit);
        if (validateMaterialUniverseTable)
        {
            RtSmokeMaterialTableBuild universeMaterialTable;
            uint32_t universeLatchedTextureProbeMaterialId = m_smokeTextureProbeMaterialId;
            int universeLatchedTextureProbeRequestedIndex = m_smokeTextureProbeRequestedIndex;
            BuildSmokeMaterialTableFromUniverse(universeMaterialTable, materialTableStaticIds, dynamicTriangleMaterialData, universeLatchedTextureProbeMaterialId, universeLatchedTextureProbeRequestedIndex, enableTextureProbe);
            materialUniverseTableCompareStats = CompareSmokeMaterialTables(materialTable, universeMaterialTable);
        }
    }
    const RtSmokeMaterialTableCacheStats materialTableCacheStats = GetSmokeMaterialTableCacheStats();
    const RtSmokeMaterialTableBuildStats materialTableBuildStats = GetSmokeMaterialTableBuildStats();
    const RtSmokeMaterialUniverseStats materialUniverseStats = GetSmokeMaterialUniverseStats();
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
            staticTriangleClassCache,
            materialTable.staticMaterialIndexes,
            dynamicTriangleClassData,
            materialTable.dynamicMaterialIndexes);
    }
    const int materialMs = Sys_Milliseconds() - materialStartMs;
    RtSmokeMaterialDiagnosticTriggerDesc materialDiagnosticDesc;
    materialDiagnosticDesc.viewDef = viewDef;
    materialDiagnosticDesc.materialTable = &materialTable;
    materialDiagnosticDesc.materialStats = &materialStats;
    materialDiagnosticDesc.enableTextureProbe = enableTextureProbe;
    RunSmokeMaterialDiagnosticTriggers(materialDiagnosticDesc);

    const bool buildRigidRouteBuffers = enableRigidRouteForMode;
    RtPathTraceRigidRouteBuild rigidRouteBuild;
    if (buildRigidRouteBuffers)
    {
        rigidRouteBuild = m_smokeGeometryUniverse.BuildRigidRouteBuffers(m_instanceUniverse, materialTable.materialIds, rigidRouteMaxInstances);
        if (r_pathTracingSmokeLog.GetInteger() != 0 && (m_smokeGeometryFrameIndex % 120ull) == 1ull)
        {
            common->Printf("PathTracePrimaryPass: PT rigid route buffers instances=%d max=%d seen/cache=%d/%d prevXform/continuous=%d/%d verts/indexes/tris=%d/%d/%d skipped nonRigid/missingMesh/missingBlas=%d/%d/%d missingMaterialIndex=%d\n",
                rigidRouteBuild.stats.emittedInstances,
                rigidRouteMaxInstances,
                rigidRouteBuild.stats.emittedSeenThisFrame,
                rigidRouteBuild.stats.emittedFromCache,
                rigidRouteBuild.stats.previousTransformInstances,
                rigidRouteBuild.stats.transformContinuousInstances,
                rigidRouteBuild.stats.vertices,
                rigidRouteBuild.stats.indexes,
                rigidRouteBuild.stats.triangles,
                rigidRouteBuild.stats.skippedNonRigid,
                rigidRouteBuild.stats.skippedMissingMesh,
                rigidRouteBuild.stats.skippedMissingBlas,
                rigidRouteBuild.stats.missingMaterialTableIndex);
        }
    }

    RtSmokeEmissiveInventoryStats emissiveInventoryStats;
    const int emissiveStartMs = Sys_Milliseconds();
    std::vector<PathTraceSmokeEmissiveTriangle> emissiveTriangles;
    std::vector<PathTraceSmokeLightCandidate> lightCandidates;
    std::vector<PathTraceDoomAnalyticLightCandidate> doomAnalyticLights;
    const int maxEmissiveRecords = idMath::ClampInt(1, RT_SMOKE_MAX_EMISSIVE_TRIANGLE_RECORDS, r_pathTracingEmissiveInventoryMaxTriangles.GetInteger());
    {
        OPTICK_EVENT("PT Emissive Inventory");
        emissiveTriangles = BuildSmokeEmissiveTriangleInventory(
            materialTable.materialIds,
            materialTable.materials,
            staticVertexCache,
            staticIndexCache,
            staticTriangleClassCache,
            materialTable.staticMaterialIndexes,
            dynamicVertexData,
            dynamicIndexData,
            dynamicTriangleClassData,
            materialTable.dynamicMaterialIndexes,
            RT_SMOKE_MATERIAL_EMISSIVE,
            RT_SMOKE_TRIANGLE_CLASS_MASK,
            static_cast<uint32_t>(RtSmokeSurfaceClass::SkinnedDeformed),
            maxEmissiveRecords,
            emissiveInventoryStats);
        if (enableRigidRouteForMode && (requestedDebugMode == 20 || restirPTDebugMode || integratorDebugMode))
        {
            AppendSmokeRigidRouteEmissiveTriangleInventory(
                materialTable.materialIds,
                materialTable.materials,
                rigidRouteBuild,
                RT_SMOKE_MATERIAL_EMISSIVE,
                maxEmissiveRecords,
                emissiveTriangles,
                emissiveInventoryStats);
        }
        if (r_pathTracingWorldStaticEmissives.GetInteger() != 0)
        {
            const int fullLevelStaticSupplementCap = idMath::ClampInt(0, maxEmissiveRecords, r_pathTracingWorldStaticEmissiveMaxTriangles.GetInteger());
            const int fullLevelStaticSupplementLimit = Min(maxEmissiveRecords, static_cast<int>(emissiveTriangles.size()) + fullLevelStaticSupplementCap);
            AppendSmokeWorldStaticEmissiveTriangleInventory(
                viewDef,
                materialTable.materialIds,
                materialTable.materials,
                RT_SMOKE_MATERIAL_EMISSIVE,
                SmokeSurfaceClassId(RtSmokeSurfaceClass::StaticWorld),
                fullLevelStaticSupplementLimit,
                emissiveTriangles,
                emissiveInventoryStats);
        }
        const int fullLevelStaticEmissiveTriangles = emissiveInventoryStats.fullLevelStaticTriangles;
        const int routedRigidEmissiveTriangles = emissiveInventoryStats.routedRigidTriangles;
        const int routedRigidInstances = emissiveInventoryStats.routedRigidInstances;
        const int routedRigidSeenInstances = emissiveInventoryStats.routedRigidSeenInstances;
        const int routedRigidCacheInstances = emissiveInventoryStats.routedRigidCacheInstances;
        const int routedRigidEmissiveInstances = emissiveInventoryStats.routedRigidEmissiveInstances;
        const int routedRigidEmissiveSeenInstances = emissiveInventoryStats.routedRigidEmissiveSeenInstances;
        const int routedRigidEmissiveCacheInstances = emissiveInventoryStats.routedRigidEmissiveCacheInstances;
        const int routedRigidCapturedTriangles = emissiveInventoryStats.routedRigidCapturedTriangles;
        const int routedRigidCappedTriangles = emissiveInventoryStats.routedRigidCappedTriangles;
        const int routedRigidInvalidTriangles = emissiveInventoryStats.routedRigidInvalidTriangles;
        const int routedRigidNonEmissiveTriangles = emissiveInventoryStats.routedRigidNonEmissiveTriangles;
        const float routedRigidArea = emissiveInventoryStats.routedRigidArea;
        const float routedRigidWeightedLuminance = emissiveInventoryStats.routedRigidWeightedLuminance;
        const int runtimeInactiveEmissiveTrianglesBeforeStatsRebuild = emissiveInventoryStats.skippedRuntimeInactiveTriangles;
        FinalizeSmokeEmissiveTriangleSamplingFields(emissiveTriangles, emissiveInventoryStats);
        emissiveInventoryStats = BuildSmokeEmissiveInventoryStatsForRecords(materialTable.materialIds, emissiveTriangles);
        emissiveInventoryStats.fullLevelStaticTriangles = fullLevelStaticEmissiveTriangles;
        emissiveInventoryStats.routedRigidTriangles = routedRigidEmissiveTriangles;
        emissiveInventoryStats.routedRigidInstances = routedRigidInstances;
        emissiveInventoryStats.routedRigidSeenInstances = routedRigidSeenInstances;
        emissiveInventoryStats.routedRigidCacheInstances = routedRigidCacheInstances;
        emissiveInventoryStats.routedRigidEmissiveInstances = routedRigidEmissiveInstances;
        emissiveInventoryStats.routedRigidEmissiveSeenInstances = routedRigidEmissiveSeenInstances;
        emissiveInventoryStats.routedRigidEmissiveCacheInstances = routedRigidEmissiveCacheInstances;
        emissiveInventoryStats.routedRigidCapturedTriangles = routedRigidCapturedTriangles;
        emissiveInventoryStats.routedRigidCappedTriangles = routedRigidCappedTriangles;
        emissiveInventoryStats.routedRigidInvalidTriangles = routedRigidInvalidTriangles;
        emissiveInventoryStats.routedRigidNonEmissiveTriangles = routedRigidNonEmissiveTriangles;
        emissiveInventoryStats.routedRigidArea = routedRigidArea;
        emissiveInventoryStats.routedRigidWeightedLuminance = routedRigidWeightedLuminance;
        emissiveInventoryStats.skippedRuntimeInactiveTriangles = runtimeInactiveEmissiveTrianglesBeforeStatsRebuild;
        lightCandidates = BuildSmokeLightCandidateBufferRecords(emissiveInventoryStats);
    }
    const bool restirPTAnalyticLightCandidates = restirPTDebugMode && r_pathTracingRestirPTAnalyticLightCandidates.GetInteger() != 0;
    const bool enableDoomAnalyticLightCandidates = r_pathTracingAnalyticLightCandidates.GetInteger() != 0 || restirPTAnalyticLightCandidates;
    doomAnalyticLights = BuildPathTraceDoomAnalyticLightCandidates(viewDef, restirPTAnalyticLightCandidates);
    if (r_pathTracingSmokeLog.GetInteger() != 0 && enableDoomAnalyticLightCandidates && (m_smokeGeometryFrameIndex % 120ull) == 1ull)
    {
        common->Printf("PathTracePrimaryPass: Doom analytic lights gpu=%d bytes=%d intensityScale=%.3f\n",
            static_cast<int>(doomAnalyticLights.size()),
            static_cast<int>(doomAnalyticLights.size() * sizeof(PathTraceDoomAnalyticLightCandidate)),
            idMath::ClampFloat(0.0f, 16.0f, r_pathTracingAnalyticLightIntensityScale.GetFloat()));
    }
    const int emissiveMs = Sys_Milliseconds() - emissiveStartMs;
    RtSmokeEmissiveInventoryDiagnosticTriggerDesc emissiveInventoryDiagnosticDesc;
    emissiveInventoryDiagnosticDesc.materialTable = &materialTable;
    emissiveInventoryDiagnosticDesc.emissiveTriangles = &emissiveTriangles;
    emissiveInventoryDiagnosticDesc.emissiveInventoryStats = &emissiveInventoryStats;
    RunSmokeEmissiveInventoryDiagnosticTriggers(emissiveInventoryDiagnosticDesc);
    const int runtimeInactiveEmissiveTriangles = emissiveInventoryStats.skippedRuntimeInactiveTriangles;
    {
        OPTICK_EVENT("PT Light Universe");
        emissiveTriangles = m_smokeLightUniverse.MergeFrameCandidates(
            viewDef,
            emissiveTriangles,
            maxEmissiveRecords,
            idMath::ClampInt(0, 8, r_pathTracingLightAreaPortalSteps.GetInteger()),
            r_pathTracingLightAreaFilter.GetInteger() != 0,
            r_pathTracingLightAreaFilterApply.GetInteger() != 0,
            idMath::ClampInt(0, maxEmissiveRecords, r_pathTracingLightAreaOverflowMax.GetInteger()),
            r_pathTracingLightUniverseChurn.GetInteger() != 0,
            r_pathTracingLightUniversePersistDynamic.GetInteger() != 0,
            r_pathTracingLightUniverseInjectMissingDynamic.GetInteger() != 0,
            idMath::ClampInt(1, 120, r_pathTracingLightUniverseDynamicMinSeenFrames.GetInteger()),
            idMath::ClampInt(1, 3600, r_pathTracingLightUniverseDynamicMaxMissingFrames.GetInteger()));
        emissiveInventoryStats = BuildSmokeEmissiveInventoryStatsForRecords(materialTable.materialIds, emissiveTriangles);
        lightCandidates = BuildSmokeLightCandidateBufferRecords(emissiveInventoryStats);
    }
    const RtSmokeLightUniverseStats lightUniverseStats = m_smokeLightUniverse.GetStats();
    if (r_pathTracingLightUniverseDump.GetInteger() != 0)
    {
        common->Printf("PathTracePrimaryPass: RT smoke light universe static=%d seen=%d new=%d updated=%d missing=%d semiStatic=%d dynSeen=%d dynPromoted=%d dynUpdated=%d dynMissing=%d dynAged=%d dynamicFrame=%d merged=%d area current/total=%d/%d selected steps/areas/edges/blocked=%d/%d/%d/%d staticKnown/unknown=%d/%d dynamicKnown/unknown=%d/%d mergedKnown/unknown=%d/%d mergedCurrent/selected/connected/disconnected=%d/%d/%d/%d connectedUnselected=%d portalSweep areas=%d/%d/%d/%d/%d merged=%d/%d/%d/%d/%d portalDepthBins depth0/1/2/3/4/>4/disconnected/unknown=%d/%d/%d/%d/%d/%d/%d/%d areaFilter enabled/applied=%d/%d steps=%d overflowMax=%d selected=%d connectedOverflow=%d disconnected=%d unknown=%d wouldUpload=%d wouldDrop=%d area %.2f/%.2f drop=%.2f weight %.3f/%.3f drop=%.3f dropWeight overflow/disconnected/unknown=%.3f/%.3f/%.3f persistDynamic=%d injectMissingDynamic=%d minSeen=%d maxMissing=%d generation=%llu\n",
            lightUniverseStats.persistentStaticTriangles,
            lightUniverseStats.staticSeenThisFrame,
            lightUniverseStats.staticNewThisFrame,
            lightUniverseStats.staticUpdatedThisFrame,
            lightUniverseStats.staticMissingThisFrame,
            lightUniverseStats.persistentDynamicTriangles,
            lightUniverseStats.dynamicSeenThisFrame,
            lightUniverseStats.dynamicPromotedThisFrame,
            lightUniverseStats.dynamicUpdatedThisFrame,
            lightUniverseStats.dynamicMissingThisFrame,
            lightUniverseStats.dynamicAgedOutThisFrame,
            lightUniverseStats.dynamicFrameTriangles,
            lightUniverseStats.mergedTriangles,
            lightUniverseStats.currentArea,
            lightUniverseStats.totalAreas,
            lightUniverseStats.selectedPortalSteps,
            lightUniverseStats.selectedAreaCount,
            lightUniverseStats.selectedPortalEdges,
            lightUniverseStats.selectedBlockedPortalEdges,
            lightUniverseStats.staticAreaKnownTriangles,
            lightUniverseStats.staticAreaUnknownTriangles,
            lightUniverseStats.dynamicAreaKnownTriangles,
            lightUniverseStats.dynamicAreaUnknownTriangles,
            lightUniverseStats.mergedAreaKnownTriangles,
            lightUniverseStats.mergedAreaUnknownTriangles,
            lightUniverseStats.mergedCurrentAreaTriangles,
            lightUniverseStats.mergedSelectedAreaTriangles,
            lightUniverseStats.mergedConnectedAreaTriangles,
            lightUniverseStats.mergedDisconnectedAreaTriangles,
            lightUniverseStats.mergedConnectedUnselectedAreaTriangles,
            lightUniverseStats.portalStepSelectedAreas[0],
            lightUniverseStats.portalStepSelectedAreas[1],
            lightUniverseStats.portalStepSelectedAreas[2],
            lightUniverseStats.portalStepSelectedAreas[3],
            lightUniverseStats.portalStepSelectedAreas[4],
            lightUniverseStats.portalStepMergedSelectedTriangles[0],
            lightUniverseStats.portalStepMergedSelectedTriangles[1],
            lightUniverseStats.portalStepMergedSelectedTriangles[2],
            lightUniverseStats.portalStepMergedSelectedTriangles[3],
            lightUniverseStats.portalStepMergedSelectedTriangles[4],
            lightUniverseStats.mergedPortalDepthBins[0],
            lightUniverseStats.mergedPortalDepthBins[1],
            lightUniverseStats.mergedPortalDepthBins[2],
            lightUniverseStats.mergedPortalDepthBins[3],
            lightUniverseStats.mergedPortalDepthBins[4],
            lightUniverseStats.mergedPortalDepthBins[5],
            lightUniverseStats.mergedPortalDepthBins[6],
            lightUniverseStats.mergedPortalDepthBins[7],
            lightUniverseStats.areaFilterEnabled,
            lightUniverseStats.areaFilterApplied,
            lightUniverseStats.areaFilterPortalSteps,
            lightUniverseStats.areaFilterOverflowMax,
            lightUniverseStats.areaFilterSelectedCandidates,
            lightUniverseStats.areaFilterConnectedOverflowCandidates,
            lightUniverseStats.areaFilterDisconnectedCandidates,
            lightUniverseStats.areaFilterUnknownCandidates,
            lightUniverseStats.areaFilterWouldUploadCandidates,
            lightUniverseStats.areaFilterWouldDropCandidates,
            lightUniverseStats.areaFilterPreArea,
            lightUniverseStats.areaFilterPostArea,
            lightUniverseStats.areaFilterDroppedArea,
            lightUniverseStats.areaFilterPreWeight,
            lightUniverseStats.areaFilterPostWeight,
            lightUniverseStats.areaFilterDroppedWeight,
            lightUniverseStats.areaFilterDroppedOverflowWeight,
            lightUniverseStats.areaFilterDroppedDisconnectedWeight,
            lightUniverseStats.areaFilterDroppedUnknownWeight,
            r_pathTracingLightUniversePersistDynamic.GetInteger() != 0 ? 1 : 0,
            r_pathTracingLightUniverseInjectMissingDynamic.GetInteger() != 0 ? 1 : 0,
            idMath::ClampInt(1, 120, r_pathTracingLightUniverseDynamicMinSeenFrames.GetInteger()),
            idMath::ClampInt(1, 3600, r_pathTracingLightUniverseDynamicMaxMissingFrames.GetInteger()),
            static_cast<unsigned long long>(lightUniverseStats.generation));
        if (lightUniverseStats.overflowSampleCount > 0)
        {
            for (int sampleIndex = 0; sampleIndex < lightUniverseStats.overflowSampleCount; ++sampleIndex)
            {
                const RtSmokeLightUniverseCandidateSample& sample = lightUniverseStats.overflowSamples[sampleIndex];
                if (!sample.valid)
                {
                    continue;
                }
                common->Printf("PathTracePrimaryPass: RT smoke light area overflow sample %d area=%d material=%u materialIndex=%u triArea=%.2f weight=%.3f distance=%.2f\n",
                    sampleIndex,
                    sample.areaNum,
                    sample.materialId,
                    sample.materialIndex,
                    sample.area,
                    sample.weight,
                    sample.distance);
            }
        }
        if (lightUniverseStats.droppedSampleCount > 0)
        {
            for (int sampleIndex = 0; sampleIndex < lightUniverseStats.droppedSampleCount; ++sampleIndex)
            {
                const RtSmokeLightUniverseCandidateSample& sample = lightUniverseStats.droppedSamples[sampleIndex];
                if (!sample.valid)
                {
                    continue;
                }
                common->Printf("PathTracePrimaryPass: RT smoke light area dropped sample %d reason=%s area=%d material=%u materialIndex=%u triArea=%.2f weight=%.3f distance=%.2f\n",
                    sampleIndex,
                    sample.reason ? sample.reason : "<unknown>",
                    sample.areaNum,
                    sample.materialId,
                    sample.materialIndex,
                    sample.area,
                    sample.weight,
                    sample.distance);
            }
        }
        common->Printf("PathTracePrimaryPass: RT smoke light origins persistentStatic=%d/%d currentDynamic=%d frameOnlyDynamic=%d persistableDynamic=%d promotedDynamic=%d unpromotedDynamic=%d injectedMissingDynamic=%d runtimeActive=%d runtimeInactiveSkipped=%d spatialMembership=unassigned temporalReuse=off\n",
            lightUniverseStats.staticMergedSeenTriangles,
            lightUniverseStats.staticMergedMissingTriangles,
            lightUniverseStats.dynamicFrameTriangles,
            lightUniverseStats.dynamicFrameOnlyTriangles,
            lightUniverseStats.dynamicPersistableFrameTriangles,
            lightUniverseStats.dynamicPromotedFrameTriangles,
            lightUniverseStats.dynamicUnpromotedFrameTriangles,
            lightUniverseStats.injectedMissingDynamicTriangles,
            emissiveInventoryStats.capturedTriangles,
            runtimeInactiveEmissiveTriangles);
        common->Printf("PathTracePrimaryPass: RT smoke light churn enabled=%d previous/current/stayed/entered/left=%d/%d/%d/%d/%d weight previous/current/stayed/entered/left=%.3f/%.3f/%.3f/%.3f/%.3f\n",
            lightUniverseStats.activeChurnEnabled,
            lightUniverseStats.activeChurnPrevious,
            lightUniverseStats.activeChurnCurrent,
            lightUniverseStats.activeChurnStayed,
            lightUniverseStats.activeChurnEntered,
            lightUniverseStats.activeChurnLeft,
            lightUniverseStats.activeChurnPreviousWeight,
            lightUniverseStats.activeChurnCurrentWeight,
            lightUniverseStats.activeChurnStayedWeight,
            lightUniverseStats.activeChurnEnteredWeight,
            lightUniverseStats.activeChurnLeftWeight);
        if (lightUniverseStats.enteredSampleCount > 0)
        {
            for (int sampleIndex = 0; sampleIndex < lightUniverseStats.enteredSampleCount; ++sampleIndex)
            {
                const RtSmokeLightUniverseCandidateSample& sample = lightUniverseStats.enteredSamples[sampleIndex];
                if (!sample.valid)
                {
                    continue;
                }
                common->Printf("PathTracePrimaryPass: RT smoke light churn entered sample %d area=%d material=%u materialIndex=%u triArea=%.2f weight=%.3f distance=%.2f\n",
                    sampleIndex,
                    sample.areaNum,
                    sample.materialId,
                    sample.materialIndex,
                    sample.area,
                    sample.weight,
                    sample.distance);
            }
        }
        if (lightUniverseStats.leftSampleCount > 0)
        {
            for (int sampleIndex = 0; sampleIndex < lightUniverseStats.leftSampleCount; ++sampleIndex)
            {
                const RtSmokeLightUniverseCandidateSample& sample = lightUniverseStats.leftSamples[sampleIndex];
                if (!sample.valid)
                {
                    continue;
                }
                common->Printf("PathTracePrimaryPass: RT smoke light churn left sample %d area=%d material=%u materialIndex=%u triArea=%.2f weight=%.3f distance=%.2f\n",
                    sampleIndex,
                    sample.areaNum,
                    sample.materialId,
                    sample.materialIndex,
                    sample.area,
                    sample.weight,
                    sample.distance);
            }
        }
        r_pathTracingLightUniverseDump.SetInteger(0);
    }

    const int bufferCreateStartMs = Sys_Milliseconds();
    RtSmokeSceneBufferCreateDesc bufferCreateDesc;
    bufferCreateDesc.device = device;
    bufferCreateDesc.existingBuffers.staticVertexBuffer = m_smokeStaticVertexBuffer;
    bufferCreateDesc.existingBuffers.staticIndexBuffer = m_smokeStaticIndexBuffer;
    bufferCreateDesc.existingBuffers.staticTriangleClassBuffer = m_smokeStaticTriangleClassBuffer;
    bufferCreateDesc.existingBuffers.staticTriangleMaterialBuffer = m_smokeStaticTriangleMaterialBuffer;
    bufferCreateDesc.existingBuffers.staticTriangleMaterialIndexBuffer = m_smokeStaticTriangleMaterialIndexBuffer;
    bufferCreateDesc.existingBuffers.previousStaticVertexBuffer = m_smokePreviousStaticVertexBuffer;
    bufferCreateDesc.existingBuffers.previousStaticIndexBuffer = m_smokePreviousStaticIndexBuffer;
    bufferCreateDesc.existingBuffers.previousStaticTriangleClassBuffer = m_smokePreviousStaticTriangleClassBuffer;
    bufferCreateDesc.existingBuffers.previousStaticTriangleMaterialBuffer = m_smokePreviousStaticTriangleMaterialBuffer;
    bufferCreateDesc.existingBuffers.previousStaticTriangleMaterialIndexBuffer = m_smokePreviousStaticTriangleMaterialIndexBuffer;
    bufferCreateDesc.existingBuffers.dynamicVertexBuffer = m_smokeDynamicVertexBuffer;
    bufferCreateDesc.existingBuffers.dynamicIndexBuffer = m_smokeDynamicIndexBuffer;
    bufferCreateDesc.existingBuffers.dynamicTriangleClassBuffer = m_smokeDynamicTriangleClassBuffer;
    bufferCreateDesc.existingBuffers.dynamicTriangleMaterialBuffer = m_smokeDynamicTriangleMaterialBuffer;
    bufferCreateDesc.existingBuffers.dynamicTriangleMaterialIndexBuffer = m_smokeDynamicTriangleMaterialIndexBuffer;
    bufferCreateDesc.existingBuffers.materialTableBuffer = m_smokeMaterialTableBuffer;
    bufferCreateDesc.existingBuffers.emissiveTriangleBuffer = m_smokeEmissiveTriangleBuffer;
    bufferCreateDesc.existingBuffers.lightCandidateBuffer = m_smokeLightCandidateBuffer;
    bufferCreateDesc.existingBuffers.doomAnalyticLightBuffer = m_smokeDoomAnalyticLightBuffer;
    bufferCreateDesc.existingBuffers.rigidRouteVertexBuffer = m_smokeRigidRouteVertexBuffer;
    bufferCreateDesc.existingBuffers.rigidRouteIndexBuffer = m_smokeRigidRouteIndexBuffer;
    bufferCreateDesc.existingBuffers.rigidRouteTriangleMaterialBuffer = m_smokeRigidRouteTriangleMaterialBuffer;
    bufferCreateDesc.existingBuffers.rigidRouteTriangleMaterialIndexBuffer = m_smokeRigidRouteTriangleMaterialIndexBuffer;
    bufferCreateDesc.existingBuffers.rigidRouteInstanceBuffer = m_smokeRigidRouteInstanceBuffer;
    bufferCreateDesc.existingBuffers.skinnedSourceVertexBuffer = m_smokeSkinnedSourceVertexBuffer;
    bufferCreateDesc.existingBuffers.skinnedCurrentOutputVertexBuffer = m_smokeSkinnedCurrentOutputVertexBuffer;
    bufferCreateDesc.existingBuffers.skinnedPreviousPositionBuffer = m_smokeSkinnedPreviousPositionBuffer;
    bufferCreateDesc.existingBuffers.skinnedSurfaceDispatchBuffer = m_smokeSkinnedSurfaceDispatchBuffer;
    bufferCreateDesc.existingBuffers.skinnedCurrentJointMatrixBuffer = m_smokeSkinnedCurrentJointMatrixBuffer;
    bufferCreateDesc.existingBuffers.skinnedPreviousJointMatrixBuffer = m_smokeSkinnedPreviousJointMatrixBuffer;
    bufferCreateDesc.staticVertexBytes = staticVertexCache.size() * sizeof(staticVertexCache[0]);
    bufferCreateDesc.staticIndexBytes = staticIndexCache.size() * sizeof(staticIndexCache[0]);
    bufferCreateDesc.staticTriangleClassBytes = staticTriangleClassCache.size() * sizeof(staticTriangleClassCache[0]);
    bufferCreateDesc.staticTriangleMaterialBytes = staticTriangleMaterialCache.size() * sizeof(staticTriangleMaterialCache[0]);
    bufferCreateDesc.staticTriangleMaterialIndexBytes = materialTable.staticMaterialIndexes.size() * sizeof(materialTable.staticMaterialIndexes[0]);
    bufferCreateDesc.previousStaticVertexBytes = previousStaticVertexCache.size() * sizeof(previousStaticVertexCache[0]);
    bufferCreateDesc.previousStaticIndexBytes = previousStaticIndexCache.size() * sizeof(previousStaticIndexCache[0]);
    bufferCreateDesc.previousStaticTriangleClassBytes = previousStaticTriangleClassCache.size() * sizeof(previousStaticTriangleClassCache[0]);
    bufferCreateDesc.previousStaticTriangleMaterialBytes = previousStaticTriangleMaterialCache.size() * sizeof(previousStaticTriangleMaterialCache[0]);
    bufferCreateDesc.previousStaticTriangleMaterialIndexBytes = previousStaticTriangleMaterialIndexCache.size() * sizeof(previousStaticTriangleMaterialIndexCache[0]);
    bufferCreateDesc.dynamicVertexBytes = dynamicVertexData.size() * sizeof(dynamicVertexData[0]);
    bufferCreateDesc.dynamicIndexBytes = dynamicIndexData.size() * sizeof(dynamicIndexData[0]);
    bufferCreateDesc.dynamicTriangleClassBytes = dynamicTriangleClassData.size() * sizeof(dynamicTriangleClassData[0]);
    bufferCreateDesc.dynamicTriangleMaterialBytes = dynamicTriangleMaterialData.size() * sizeof(dynamicTriangleMaterialData[0]);
    bufferCreateDesc.dynamicTriangleMaterialIndexBytes = materialTable.dynamicMaterialIndexes.size() * sizeof(materialTable.dynamicMaterialIndexes[0]);
    bufferCreateDesc.materialTableBytes = materialTable.materials.size() * sizeof(materialTable.materials[0]);
    bufferCreateDesc.emissiveTriangleBytes = emissiveTriangles.size() * sizeof(emissiveTriangles[0]);
    bufferCreateDesc.lightCandidateBytes = lightCandidates.size() * sizeof(lightCandidates[0]);
    bufferCreateDesc.doomAnalyticLightBytes = doomAnalyticLights.size() * sizeof(PathTraceDoomAnalyticLightCandidate);
    bufferCreateDesc.rigidRouteVertexBytes = rigidRouteBuild.vertices.size() * sizeof(PathTraceSmokeVertex);
    bufferCreateDesc.rigidRouteIndexBytes = rigidRouteBuild.indexes.size() * sizeof(uint32_t);
    bufferCreateDesc.rigidRouteTriangleMaterialBytes = rigidRouteBuild.triangleMaterials.size() * sizeof(uint32_t);
    bufferCreateDesc.rigidRouteTriangleMaterialIndexBytes = rigidRouteBuild.triangleMaterialIndexes.size() * sizeof(uint32_t);
    bufferCreateDesc.rigidRouteInstanceBytes = rigidRouteBuild.instances.size() * sizeof(PathTraceRigidRouteInstance);
    bufferCreateDesc.skinnedSourceVertexBytes = skinnedGpuScaffold.sourceVertices.size() * sizeof(PathTraceSkinnedSourceVertex);
    bufferCreateDesc.skinnedCurrentOutputVertexBytes = skinnedGpuScaffold.currentOutputVertices.size() * sizeof(PathTraceSmokeVertex);
    bufferCreateDesc.skinnedPreviousPositionBytes = skinnedGpuScaffold.previousPositions.size() * sizeof(PathTraceSkinnedPreviousPosition);
    bufferCreateDesc.skinnedSurfaceDispatchBytes = skinnedGpuScaffold.dispatchRecords.size() * sizeof(PathTraceSkinnedSurfaceDispatchRecord);
    bufferCreateDesc.skinnedCurrentJointMatrixBytes = skinnedGpuScaffold.currentJointMatrices.size() * sizeof(PathTraceSkinnedJointMatrix);
    bufferCreateDesc.skinnedPreviousJointMatrixBytes = skinnedGpuScaffold.previousJointMatrices.size() * sizeof(PathTraceSkinnedJointMatrix);
    RtSmokeSceneBufferCreateResult bufferCreateResult;
    {
        OPTICK_EVENT("PT Create Scene Buffers");
        bufferCreateResult = CreateSmokeSceneBuffers(bufferCreateDesc);
    }
    if (!bufferCreateResult.Succeeded())
    {
        common->Printf("PathTracePrimaryPass: %s\n", bufferCreateResult.errorMessage ? bufferCreateResult.errorMessage : "failed to create RT smoke geometry buffers");
        return;
    }
    RtSmokeSceneBufferHandles smokeBuffers = bufferCreateResult.buffers;
    nvrhi::BufferHandle smokeStaticVertexBuffer = smokeBuffers.staticVertexBuffer;
    nvrhi::BufferHandle smokeStaticIndexBuffer = smokeBuffers.staticIndexBuffer;
    nvrhi::BufferHandle smokeStaticTriangleClassBuffer = smokeBuffers.staticTriangleClassBuffer;
    nvrhi::BufferHandle smokeStaticTriangleMaterialBuffer = smokeBuffers.staticTriangleMaterialBuffer;
    nvrhi::BufferHandle smokeStaticTriangleMaterialIndexBuffer = smokeBuffers.staticTriangleMaterialIndexBuffer;
    nvrhi::BufferHandle smokePreviousStaticVertexBuffer = smokeBuffers.previousStaticVertexBuffer;
    nvrhi::BufferHandle smokePreviousStaticIndexBuffer = smokeBuffers.previousStaticIndexBuffer;
    nvrhi::BufferHandle smokePreviousStaticTriangleClassBuffer = smokeBuffers.previousStaticTriangleClassBuffer;
    nvrhi::BufferHandle smokePreviousStaticTriangleMaterialBuffer = smokeBuffers.previousStaticTriangleMaterialBuffer;
    nvrhi::BufferHandle smokePreviousStaticTriangleMaterialIndexBuffer = smokeBuffers.previousStaticTriangleMaterialIndexBuffer;
    nvrhi::BufferHandle smokeDynamicVertexBuffer = smokeBuffers.dynamicVertexBuffer;
    nvrhi::BufferHandle smokeDynamicIndexBuffer = smokeBuffers.dynamicIndexBuffer;
    nvrhi::BufferHandle smokeDynamicTriangleClassBuffer = smokeBuffers.dynamicTriangleClassBuffer;
    nvrhi::BufferHandle smokeDynamicTriangleMaterialBuffer = smokeBuffers.dynamicTriangleMaterialBuffer;
    nvrhi::BufferHandle smokeDynamicTriangleMaterialIndexBuffer = smokeBuffers.dynamicTriangleMaterialIndexBuffer;
    nvrhi::BufferHandle smokeMaterialTableBuffer = smokeBuffers.materialTableBuffer;
    nvrhi::BufferHandle smokeEmissiveTriangleBuffer = smokeBuffers.emissiveTriangleBuffer;
    nvrhi::BufferHandle smokeLightCandidateBuffer = smokeBuffers.lightCandidateBuffer;
    nvrhi::BufferHandle smokeDoomAnalyticLightBuffer = smokeBuffers.doomAnalyticLightBuffer;
    nvrhi::BufferHandle smokeRigidRouteVertexBuffer = smokeBuffers.rigidRouteVertexBuffer;
    nvrhi::BufferHandle smokeRigidRouteIndexBuffer = smokeBuffers.rigidRouteIndexBuffer;
    nvrhi::BufferHandle smokeRigidRouteTriangleMaterialBuffer = smokeBuffers.rigidRouteTriangleMaterialBuffer;
    nvrhi::BufferHandle smokeRigidRouteTriangleMaterialIndexBuffer = smokeBuffers.rigidRouteTriangleMaterialIndexBuffer;
    nvrhi::BufferHandle smokeRigidRouteInstanceBuffer = smokeBuffers.rigidRouteInstanceBuffer;
    nvrhi::BufferHandle smokeSkinnedSourceVertexBuffer = smokeBuffers.skinnedSourceVertexBuffer;
    nvrhi::BufferHandle smokeSkinnedCurrentOutputVertexBuffer = smokeBuffers.skinnedCurrentOutputVertexBuffer;
    nvrhi::BufferHandle smokeSkinnedPreviousPositionBuffer = smokeBuffers.skinnedPreviousPositionBuffer;
    nvrhi::BufferHandle smokeSkinnedSurfaceDispatchBuffer = smokeBuffers.skinnedSurfaceDispatchBuffer;
    nvrhi::BufferHandle smokeSkinnedCurrentJointMatrixBuffer = smokeBuffers.skinnedCurrentJointMatrixBuffer;
    nvrhi::BufferHandle smokeSkinnedPreviousJointMatrixBuffer = smokeBuffers.skinnedPreviousJointMatrixBuffer;
    const int bufferCreateMs = Sys_Milliseconds() - bufferCreateStartMs;

    const int staticVertexCount = static_cast<int>(staticVertexCache.size());
    const int dynamicVertexCount = static_cast<int>(dynamicVertexData.size());
    const int staticIndexCount = bucketRanges.buckets[0].indexCount;
    const int dynamicIndexCount =
        bucketRanges.buckets[1].indexCount +
        bucketRanges.buckets[2].indexCount +
        bucketRanges.buckets[3].indexCount +
        bucketRanges.buckets[4].indexCount;
    const bool hasStaticBlas = staticIndexCount > 0;
    const bool hasDynamicBlas = dynamicIndexCount > 0;
    const RtSmokeBucketRange& staticBucketRange = bucketRanges.buckets[0];
    RtSmokeGeometryRange staticGeometryRange;
    staticGeometryRange.vertexOffset = staticBucketRange.vertexOffset;
    staticGeometryRange.vertexCount = staticBucketRange.vertexCount;
    staticGeometryRange.indexOffset = staticBucketRange.indexOffset;
    staticGeometryRange.indexCount = staticBucketRange.indexCount;
    staticGeometryRange.triangleOffset = staticBucketRange.triangleOffset;
    staticGeometryRange.triangleCount = staticBucketRange.triangleCount;
    const bool validateGeometryUniverse = r_pathTracingGeometryUniverseValidate.GetInteger() != 0;
    const RtSmokeGeometryUniverseStats geometryUniverseStats = m_smokeGeometryUniverse.GetStats(validateGeometryUniverse);
    if (r_pathTracingGeometryUniverseRangeDump.GetInteger() != 0)
    {
        m_smokeGeometryUniverse.LogStaticRangeHistory(RT_SMOKE_GEOMETRY_RANGE_DUMP_RECORDS);
        r_pathTracingGeometryUniverseRangeDump.SetInteger(0);
    }
    if (validateGeometryUniverse && geometryUniverseStats.staticValidationErrors > 0)
    {
        if (g_smokeLastGeometryValidationDumpGeneration != geometryUniverseStats.generation ||
            g_smokeLastGeometryValidationDumpErrors != geometryUniverseStats.staticValidationErrors)
        {
            m_smokeGeometryUniverse.LogStaticValidationFailures(RT_SMOKE_GEOMETRY_VALIDATION_DUMP_RECORDS);
            g_smokeLastGeometryValidationDumpGeneration = geometryUniverseStats.generation;
            g_smokeLastGeometryValidationDumpErrors = geometryUniverseStats.staticValidationErrors;
        }
    }
    else if (geometryUniverseStats.staticValidationErrors == 0)
    {
        g_smokeLastGeometryValidationDumpErrors = 0;
    }
    const int staticVertexCacheCount = geometryUniverseStats.staticVerts;
    const int staticIndexCacheCount = geometryUniverseStats.staticIndexes;
    const int staticTriangleCacheCount = geometryUniverseStats.staticTriangles;
    const int staticCacheBytesKB = geometryUniverseStats.staticBytesKB;
    RtSmokeStaticBlasSignature staticSignature;
    staticSignature.vertexCount = staticGeometryRange.vertexCount;
    staticSignature.indexCount = staticGeometryRange.indexCount;
    staticSignature.triangleCount = staticGeometryRange.triangleCount;
    const int staticSignatureStartMs = Sys_Milliseconds();
    bool staticBlasSignatureReused = false;
    if (!staticCacheChanged && m_smokeStaticBlasCacheValid && m_smokeStaticBlasSignature != 0)
    {
        staticSignature.hash = m_smokeStaticBlasSignature;
        staticBlasSignatureReused = true;
    }
    else
    {
        {
            OPTICK_EVENT("PT Static BLAS Signature");
            staticSignature = ComputeSmokeStaticBlasSignature(staticVertexCache, staticIndexCache, staticTriangleClassCache, staticTriangleMaterialCache, staticGeometryRange, vec3_origin);
        }
    }
    const int staticBlasSignatureMs = Sys_Milliseconds() - staticSignatureStartMs;
    const uint64 reservoirSceneSignature = ComputeSmokeReservoirSceneSignature(
        materialTableSignature,
        staticSignature.hash,
        emissiveTriangles,
        lightCandidates);
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
        smokeBuffers.staticVertexBuffer = smokeStaticVertexBuffer;
        smokeBuffers.staticIndexBuffer = smokeStaticIndexBuffer;
        smokeBuffers.staticTriangleClassBuffer = smokeStaticTriangleClassBuffer;
        smokeBuffers.staticTriangleMaterialBuffer = smokeStaticTriangleMaterialBuffer;
        smokeBuffers.staticTriangleMaterialIndexBuffer = smokeStaticTriangleMaterialIndexBuffer;
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
            RtSmokeBlasCreateDesc staticBlasCreateDesc;
            staticBlasCreateDesc.device = device;
            staticBlasCreateDesc.vertexBuffer = smokeStaticVertexBuffer;
            staticBlasCreateDesc.indexBuffer = smokeStaticIndexBuffer;
            staticBlasCreateDesc.vertexCount = staticVertexCount;
            staticBlasCreateDesc.indexCount = staticIndexCount;
            staticBlasCreateDesc.debugName = "PathTraceSmokeStaticWorldBLAS";
            RtSmokeBlasCreateResult staticBlasCreateResult;
            {
                OPTICK_EVENT("PT Create Static BLAS");
                staticBlasCreateResult = CreateSmokeBlas(staticBlasCreateDesc);
            }
            if (!staticBlasCreateResult.Succeeded())
            {
                common->Printf("PathTracePrimaryPass: failed to create RT smoke static BLAS\n");
                return;
            }
            smokeStaticBlasDesc = staticBlasCreateResult.accelStructDesc;
            smokeStaticBlas = staticBlasCreateResult.accelStruct;
            ++m_smokeStaticBlasCacheMissCount;
        }
    }

    nvrhi::rt::AccelStructDesc smokeDynamicBlasDesc;
    nvrhi::rt::AccelStructHandle smokeDynamicBlas;
    if (hasDynamicBlas)
    {
        RtSmokeBlasCreateDesc dynamicBlasCreateDesc;
        dynamicBlasCreateDesc.device = device;
        dynamicBlasCreateDesc.vertexBuffer = smokeDynamicVertexBuffer;
        dynamicBlasCreateDesc.indexBuffer = smokeDynamicIndexBuffer;
        dynamicBlasCreateDesc.vertexCount = dynamicVertexCount;
        dynamicBlasCreateDesc.indexCount = dynamicIndexCount;
        dynamicBlasCreateDesc.debugName = "PathTraceSmokeDynamicCandidateBLAS";
        RtSmokeBlasCreateResult dynamicBlasCreateResult;
        {
            OPTICK_EVENT("PT Create Dynamic BLAS");
            dynamicBlasCreateResult = CreateSmokeBlas(dynamicBlasCreateDesc);
        }
        if (!dynamicBlasCreateResult.Succeeded())
        {
            common->Printf("PathTracePrimaryPass: failed to create RT smoke dynamic BLAS\n");
            return;
        }
        smokeDynamicBlasDesc = dynamicBlasCreateResult.accelStructDesc;
        smokeDynamicBlas = dynamicBlasCreateResult.accelStruct;
    }

    const bool staticGeometryBuffersReused =
        smokeStaticVertexBuffer && smokeStaticVertexBuffer == m_smokeStaticVertexBuffer &&
        smokeStaticIndexBuffer && smokeStaticIndexBuffer == m_smokeStaticIndexBuffer &&
        smokeStaticTriangleClassBuffer && smokeStaticTriangleClassBuffer == m_smokeStaticTriangleClassBuffer &&
        smokeStaticTriangleMaterialBuffer && smokeStaticTriangleMaterialBuffer == m_smokeStaticTriangleMaterialBuffer;
    const bool staticDirtyRangesValid =
        SmokeUploadElementRangeValid(geometryUniverseStats.staticDirtyVertexOffset, geometryUniverseStats.staticDirtyVertexCount, staticVertexCache.size()) &&
        SmokeUploadElementRangeValid(geometryUniverseStats.staticDirtyIndexOffset, geometryUniverseStats.staticDirtyIndexCount, staticIndexCache.size()) &&
        SmokeUploadElementRangeValid(geometryUniverseStats.staticDirtyTriangleOffset, geometryUniverseStats.staticDirtyTriangleCount, staticTriangleClassCache.size()) &&
        SmokeUploadElementRangeValid(geometryUniverseStats.staticDirtyTriangleOffset, geometryUniverseStats.staticDirtyTriangleCount, staticTriangleMaterialCache.size());
    const bool useStaticDirtyRangeUploads =
        !staticBlasCacheHit &&
        staticCacheChanged &&
        geometryUniverseStats.staticDirty > 0 &&
        staticGeometryBuffersReused &&
        staticDirtyRangesValid;
    const bool previousStaticSnapshotBuffersReused =
        smokePreviousStaticVertexBuffer && smokePreviousStaticVertexBuffer == m_smokePreviousStaticVertexBuffer &&
        smokePreviousStaticIndexBuffer && smokePreviousStaticIndexBuffer == m_smokePreviousStaticIndexBuffer &&
        smokePreviousStaticTriangleClassBuffer && smokePreviousStaticTriangleClassBuffer == m_smokePreviousStaticTriangleClassBuffer &&
        smokePreviousStaticTriangleMaterialBuffer && smokePreviousStaticTriangleMaterialBuffer == m_smokePreviousStaticTriangleMaterialBuffer &&
        smokePreviousStaticTriangleMaterialIndexBuffer && smokePreviousStaticTriangleMaterialIndexBuffer == m_smokePreviousStaticTriangleMaterialIndexBuffer;
    const bool previousStaticSnapshotDataAvailable =
        !previousStaticVertexCache.empty() &&
        !previousStaticIndexCache.empty() &&
        !previousStaticTriangleClassCache.empty() &&
        !previousStaticTriangleMaterialCache.empty() &&
        !previousStaticTriangleMaterialIndexCache.empty();
    const uint64 previousStaticSnapshotUploadSignature = previousStaticSnapshotDataAvailable
        ? ComputeSmokePreviousStaticSnapshotUploadSignature(
            previousStaticVertexCache,
            previousStaticIndexCache,
            previousStaticTriangleClassCache,
            previousStaticTriangleMaterialCache,
            previousStaticTriangleMaterialIndexCache)
        : 0;
    const bool skipPreviousStaticSnapshotUpload =
        previousStaticSnapshotDataAvailable &&
        previousStaticSnapshotBuffersReused &&
        m_smokePreviousStaticSnapshotUploadSignature != 0 &&
        m_smokePreviousStaticSnapshotUploadSignature == previousStaticSnapshotUploadSignature;

    const RtSmokeBufferUploadItem uploadItems[] = {
        MakeSmokeVectorUploadItem(smokeStaticVertexBuffer, staticVertexCache, nvrhi::ResourceStates::AccelStructBuildInput, staticBlasCacheHit, useStaticDirtyRangeUploads ? geometryUniverseStats.staticDirtyVertexOffset : -1, geometryUniverseStats.staticDirtyVertexCount),
        MakeSmokeVectorUploadItem(smokeStaticIndexBuffer, staticIndexCache, nvrhi::ResourceStates::AccelStructBuildInput, staticBlasCacheHit, useStaticDirtyRangeUploads ? geometryUniverseStats.staticDirtyIndexOffset : -1, geometryUniverseStats.staticDirtyIndexCount),
        MakeSmokeVectorUploadItem(smokeStaticTriangleClassBuffer, staticTriangleClassCache, nvrhi::ResourceStates::ShaderResource, staticBlasCacheHit, useStaticDirtyRangeUploads ? geometryUniverseStats.staticDirtyTriangleOffset : -1, geometryUniverseStats.staticDirtyTriangleCount),
        MakeSmokeVectorUploadItem(smokeStaticTriangleMaterialBuffer, staticTriangleMaterialCache, nvrhi::ResourceStates::ShaderResource, staticBlasCacheHit, useStaticDirtyRangeUploads ? geometryUniverseStats.staticDirtyTriangleOffset : -1, geometryUniverseStats.staticDirtyTriangleCount),
        MakeSmokeVectorUploadItem(smokeStaticTriangleMaterialIndexBuffer, materialTable.staticMaterialIndexes, nvrhi::ResourceStates::ShaderResource, staticBlasCacheHit),
        MakeSmokeVectorUploadItem(smokePreviousStaticVertexBuffer, previousStaticVertexCache, nvrhi::ResourceStates::ShaderResource, skipPreviousStaticSnapshotUpload),
        MakeSmokeVectorUploadItem(smokePreviousStaticIndexBuffer, previousStaticIndexCache, nvrhi::ResourceStates::ShaderResource, skipPreviousStaticSnapshotUpload),
        MakeSmokeVectorUploadItem(smokePreviousStaticTriangleClassBuffer, previousStaticTriangleClassCache, nvrhi::ResourceStates::ShaderResource, skipPreviousStaticSnapshotUpload),
        MakeSmokeVectorUploadItem(smokePreviousStaticTriangleMaterialBuffer, previousStaticTriangleMaterialCache, nvrhi::ResourceStates::ShaderResource, skipPreviousStaticSnapshotUpload),
        MakeSmokeVectorUploadItem(smokePreviousStaticTriangleMaterialIndexBuffer, previousStaticTriangleMaterialIndexCache, nvrhi::ResourceStates::ShaderResource, skipPreviousStaticSnapshotUpload),
        MakeSmokeVectorUploadItem(smokeDynamicVertexBuffer, dynamicVertexData, nvrhi::ResourceStates::AccelStructBuildInput, false),
        MakeSmokeVectorUploadItem(smokeDynamicIndexBuffer, dynamicIndexData, nvrhi::ResourceStates::AccelStructBuildInput, false),
        MakeSmokeVectorUploadItem(smokeDynamicTriangleClassBuffer, dynamicTriangleClassData, nvrhi::ResourceStates::ShaderResource, false),
        MakeSmokeVectorUploadItem(smokeDynamicTriangleMaterialBuffer, dynamicTriangleMaterialData, nvrhi::ResourceStates::ShaderResource, false),
        MakeSmokeVectorUploadItem(smokeDynamicTriangleMaterialIndexBuffer, materialTable.dynamicMaterialIndexes, nvrhi::ResourceStates::ShaderResource, false),
        MakeSmokeVectorUploadItem(smokeMaterialTableBuffer, materialTable.materials, nvrhi::ResourceStates::ShaderResource, false),
        MakeSmokeVectorUploadItem(smokeEmissiveTriangleBuffer, emissiveTriangles, nvrhi::ResourceStates::ShaderResource, false),
        MakeSmokeVectorUploadItem(smokeLightCandidateBuffer, lightCandidates, nvrhi::ResourceStates::ShaderResource, false),
        MakeSmokeVectorUploadItem(smokeDoomAnalyticLightBuffer, doomAnalyticLights, nvrhi::ResourceStates::ShaderResource, false),
        MakeSmokeVectorUploadItem(smokeRigidRouteVertexBuffer, rigidRouteBuild.vertices, nvrhi::ResourceStates::ShaderResource, false),
        MakeSmokeVectorUploadItem(smokeRigidRouteIndexBuffer, rigidRouteBuild.indexes, nvrhi::ResourceStates::ShaderResource, false),
        MakeSmokeVectorUploadItem(smokeRigidRouteTriangleMaterialBuffer, rigidRouteBuild.triangleMaterials, nvrhi::ResourceStates::ShaderResource, false),
        MakeSmokeVectorUploadItem(smokeRigidRouteTriangleMaterialIndexBuffer, rigidRouteBuild.triangleMaterialIndexes, nvrhi::ResourceStates::ShaderResource, false),
        MakeSmokeVectorUploadItem(smokeRigidRouteInstanceBuffer, rigidRouteBuild.instances, nvrhi::ResourceStates::ShaderResource, false),
        MakeSmokeVectorUploadItem(smokeSkinnedSourceVertexBuffer, skinnedGpuScaffold.sourceVertices, nvrhi::ResourceStates::ShaderResource, false),
        MakeSmokeVectorUploadItem(smokeSkinnedCurrentOutputVertexBuffer, skinnedGpuScaffold.currentOutputVertices, nvrhi::ResourceStates::ShaderResource, false),
        MakeSmokeVectorUploadItem(smokeSkinnedPreviousPositionBuffer, skinnedGpuScaffold.previousPositions, nvrhi::ResourceStates::ShaderResource, false),
        MakeSmokeVectorUploadItem(smokeSkinnedSurfaceDispatchBuffer, skinnedGpuScaffold.dispatchRecords, nvrhi::ResourceStates::ShaderResource, false),
        MakeSmokeVectorUploadItem(smokeSkinnedCurrentJointMatrixBuffer, skinnedGpuScaffold.currentJointMatrices, nvrhi::ResourceStates::ShaderResource, false),
        MakeSmokeVectorUploadItem(smokeSkinnedPreviousJointMatrixBuffer, skinnedGpuScaffold.previousJointMatrices, nvrhi::ResourceStates::ShaderResource, false)
    };
    RtSmokeBufferUploadBatchDesc uploadBatchDesc;
    uploadBatchDesc.commandList = commandList;
    uploadBatchDesc.items = uploadItems;
    uploadBatchDesc.itemCount = static_cast<int>(sizeof(uploadItems) / sizeof(uploadItems[0]));
    int bufferUploadMs = 0;
    if (optickGpuMarkers)
    {
        OPTICK_GPU_EVENT("PT GPU Upload Scene Buffers");
        bufferUploadMs = UploadSmokeAccelerationBuffers(uploadBatchDesc);
    }
    else
    {
        bufferUploadMs = UploadSmokeAccelerationBuffers(uploadBatchDesc);
    }

    RtSmokeAccelSubmitDesc accelSubmitDesc;
    std::vector<nvrhi::rt::InstanceDesc> rigidTlasRouteInstances;
    const bool routeRigidTlasInstances = enableRigidRouteForMode;
    if (routeRigidTlasInstances)
    {
        const int routedRigidInstances = m_smokeGeometryUniverse.BuildRigidTlasInstanceDescs(
            m_instanceUniverse,
            rigidTlasRouteInstances,
            2,
            0x02,
            rigidRouteMaxInstances);
        if (r_pathTracingSmokeLog.GetInteger() != 0 && routedRigidInstances > 0 && (m_smokeGeometryFrameIndex % 120ull) == 1ull)
        {
            common->Printf("PathTracePrimaryPass: PT rigid TLAS route debug mode active mode=%d routedInstances=%d renderPath=dynamicFallback traceMask=%s\n",
                requestedDebugMode,
                routedRigidInstances,
                requestedDebugMode == 23 ? "rigidOnly" : (requestedDebugMode == 24 ? "fallbackAndRigidValidation" : (requestedDebugMode == 25 ? "fallbackAndRigidLighting" : (requestedDebugMode == 29 ? "mode29RestirPTPrimaryHistory" : (requestedDebugMode == 28 ? "mode28RestirPTInitialVisibility" : (requestedDebugMode == 27 ? "mode27RestirPTInitialShading" : (requestedDebugMode == 26 ? "mode26RestirPTInitial" : (requestedDebugMode == 20 ? "mode20Integration" : "mode18Integration"))))))));
        }
    }
    accelSubmitDesc.commandList = commandList;
    accelSubmitDesc.tlas = m_smokeTlas;
    accelSubmitDesc.staticBlas = smokeStaticBlas;
    accelSubmitDesc.dynamicBlas = smokeDynamicBlas;
    accelSubmitDesc.staticBlasDesc = smokeStaticBlasDesc;
    accelSubmitDesc.dynamicBlasDesc = smokeDynamicBlasDesc;
    accelSubmitDesc.extraTlasInstances = routeRigidTlasInstances ? &rigidTlasRouteInstances : nullptr;
    accelSubmitDesc.hasStaticBlas = hasStaticBlas;
    accelSubmitDesc.hasDynamicBlas = hasDynamicBlas;
    accelSubmitDesc.staticBlasCacheHit = staticBlasCacheHit;
    RtSmokeAccelSubmitTiming accelSubmitTiming;
    bool accelSubmitSucceeded = false;
    if (optickGpuMarkers)
    {
        OPTICK_GPU_EVENT("PT GPU Submit Acceleration Builds");
        accelSubmitSucceeded = SubmitSmokeAccelerationBuilds(accelSubmitDesc, accelSubmitTiming);
    }
    else
    {
        accelSubmitSucceeded = SubmitSmokeAccelerationBuilds(accelSubmitDesc, accelSubmitTiming);
    }
    if (!accelSubmitSucceeded)
    {
        common->Printf("PathTracePrimaryPass: failed to submit RT smoke acceleration structures\n");
        return;
    }
    const int blasSubmitMs = accelSubmitTiming.blasSubmitMs;
    const int tlasSubmitMs = accelSubmitTiming.tlasSubmitMs;
    const int accelSubmitMs = accelSubmitTiming.accelSubmitMs;
    const int instanceCount = accelSubmitTiming.instanceCount;

    const nvrhi::TextureHandle fallbackTexture = globalImages && globalImages->whiteImage ? globalImages->whiteImage->GetTextureHandle() : nullptr;
    if (!fallbackTexture)
    {
        common->Printf("PathTracePrimaryPass: failed to find RT smoke fallback material texture\n");
        return;
    }

    RtSmokeBindingBuildDesc bindingBuildDesc;
    bindingBuildDesc.device = device;
    bindingBuildDesc.tlas = m_smokeTlas;
    bindingBuildDesc.outputTexture = m_frameResources.outputTexture;
    bindingBuildDesc.accumulationTexture = m_frameResources.accumulationTexture;
    bindingBuildDesc.fallbackTexture = fallbackTexture;
    bindingBuildDesc.constantsBuffer = m_smokeConstantsBuffer;
    bindingBuildDesc.restirPTConstantsBuffer = m_restirPTConstantsBuffer;
    bindingBuildDesc.boundsOverlayLineBuffer = m_smokeBoundsOverlayLineBuffer;
    bindingBuildDesc.bindingLayout = m_smokeBindingLayout;
    bindingBuildDesc.textureBindlessLayout = m_smokeTextureBindlessLayout;
    const int sceneRetireFrames = idMath::ClampInt(0, 32, r_pathTracingSceneRetireFrames.GetInteger());
    bindingBuildDesc.existingTextureDescriptorTable = sceneRetireFrames > 0 ? nullptr : m_smokeTextureDescriptorTable;
    bindingBuildDesc.sampler = m_backend->GetCommonPasses().m_AnisotropicWrapSampler;
    bindingBuildDesc.buffers = smokeBuffers;
    bindingBuildDesc.reservoirBuffers = m_frameResources.smokeReservoirBuffers;
    bindingBuildDesc.restirPTReservoirBuffers = m_frameResources.restirPTReservoirBuffers;
    bindingBuildDesc.primarySurfaceHistoryBuffers = m_frameResources.primarySurfaceHistoryBuffers;
    bindingBuildDesc.enableTextureProbe = enableTextureProbe;
    bindingBuildDesc.forceFallbackTexture = r_pathTracingTextureForceFallback.GetInteger() != 0;
    bindingBuildDesc.maxActiveTextures = RT_SMOKE_TEXTURE_EXPERIMENTAL_ACTIVE_CAP;

    RtSmokeBindingBuildResult bindingBuildResult;
    {
        OPTICK_EVENT("PT Create Binding Resources");
        bindingBuildResult = CreateSmokeBindingResources(bindingBuildDesc, materialTable);
    }
    if (!bindingBuildResult.Succeeded())
    {
        if (bindingBuildResult.failedTextureSlot >= 0)
        {
            common->Printf("PathTracePrimaryPass: %s %d\n", bindingBuildResult.errorMessage, bindingBuildResult.failedTextureSlot);
        }
        else
        {
            common->Printf("PathTracePrimaryPass: %s\n", bindingBuildResult.errorMessage ? bindingBuildResult.errorMessage : "failed to create RT smoke binding resources");
        }
        return;
    }

    uint64 sceneInputCameraSignature = 1469598103934665603ull;
    if (viewDef)
    {
        sceneInputCameraSignature = HashSmokeBytes(sceneInputCameraSignature, &viewDef->renderView.vieworg, sizeof(viewDef->renderView.vieworg));
        sceneInputCameraSignature = HashSmokeBytes(sceneInputCameraSignature, &viewDef->renderView.viewaxis, sizeof(viewDef->renderView.viewaxis));
        sceneInputCameraSignature = HashSmokeBytes(sceneInputCameraSignature, &viewDef->renderView.fov_x, sizeof(viewDef->renderView.fov_x));
        sceneInputCameraSignature = HashSmokeBytes(sceneInputCameraSignature, &viewDef->renderView.fov_y, sizeof(viewDef->renderView.fov_y));
    }

    uint64 sceneInputLightSignature = 1469598103934665603ull;
    sceneInputLightSignature = HashSmokeBytes(sceneInputLightSignature, &lightUniverseStats.generation, sizeof(lightUniverseStats.generation));
    sceneInputLightSignature = HashSmokeBytes(sceneInputLightSignature, &emissiveInventoryStats.capturedTriangles, sizeof(emissiveInventoryStats.capturedTriangles));
    sceneInputLightSignature = HashSmokeBytes(sceneInputLightSignature, &emissiveInventoryStats.candidateMaterials, sizeof(emissiveInventoryStats.candidateMaterials));
    const int doomAnalyticLightCountForSignature = static_cast<int>(doomAnalyticLights.size());
    sceneInputLightSignature = HashSmokeBytes(sceneInputLightSignature, &doomAnalyticLightCountForSignature, sizeof(doomAnalyticLightCountForSignature));

    const uint64_t staticUploadBytes = SumSmokeUploadBytes(uploadItems, 0, 5);
    const uint64_t previousStaticUploadBytes = SumSmokeUploadBytes(uploadItems, 5, 5);
    const uint64_t dynamicUploadBytes = SumSmokeUploadBytes(uploadItems, 10, 5);
    const uint64_t materialUploadBytes = SumSmokeUploadBytes(uploadItems, 15, 1);
    const uint64_t lightUploadBytes = SumSmokeUploadBytes(uploadItems, 16, 3);
    const uint64_t rigidRouteUploadBytes = SumSmokeUploadBytes(uploadItems, 19, 5);

    RtPathTraceSceneInputs sceneInputs;
    sceneInputs.valid = true;
    sceneInputs.sceneSource = sceneSource;
    sceneInputs.debugMode = requestedDebugMode;
    sceneInputs.outputWidth = m_frameResources.width;
    sceneInputs.outputHeight = m_frameResources.height;
    sceneInputs.capabilityFlags = RT_SCENE_INPUT_MATERIAL_STOPGAP_CLASSIFIER |
        RT_SCENE_INPUT_MATERIAL_IDTECH4_SEMANTICS_RESERVED |
        RT_SCENE_INPUT_MATERIAL_PBR_ROLES_RESERVED |
        RT_SCENE_INPUT_GEOMETRY_PREVIOUS_TRANSFORM_RESERVED |
        RT_SCENE_INPUT_GEOMETRY_PREVIOUS_VERTEX_RESERVED |
        RT_SCENE_INPUT_SKINNED_SOURCE_GEOMETRY_RESERVED |
        RT_SCENE_INPUT_SKINNED_GPU_SKINNING_RESERVED |
        RT_SCENE_INPUT_LIGHT_PREVIOUS_IDENTITY_RESERVED;
    if (sceneSource == 3)
    {
        sceneInputs.capabilityFlags |= RT_SCENE_INPUT_SOURCE3_BASELINE | RT_SCENE_INPUT_PORTAL_AREA_RESIDENCY | RT_SCENE_INPUT_PORTAL_BLOCK_VIEW_REPORTED;
    }
    if (sceneSource == 0)
    {
        sceneInputs.capabilityFlags |= RT_SCENE_INPUT_SOURCE0_EMERGENCY_FALLBACK;
    }
    sceneInputs.signatures.geometryMembership = staticSignature.hash;
    sceneInputs.signatures.materialTable = materialTableSignature;
    sceneInputs.signatures.lightMembership = sceneInputLightSignature;
    sceneInputs.signatures.outputResolution = (static_cast<uint64>(m_frameResources.width) << 32) | static_cast<uint32_t>(m_frameResources.height);
    sceneInputs.signatures.cameraProjection = sceneInputCameraSignature;
    sceneInputs.signatures.debugFeaturePolicy = static_cast<uint64>(requestedDebugMode);
    sceneInputs.signatures.cpuUploadGeneration = m_smokeGeometryFrameIndex;
    sceneInputs.signatures.reservoirScene = reservoirSceneSignature;

    sceneInputs.portalPolicy.sceneSource = sceneSource;
    sceneInputs.portalPolicy.viewArea = viewDef ? viewDef->areaNum : -1;
    sceneInputs.portalPolicy.currentArea = lightUniverseStats.currentArea >= 0 ? lightUniverseStats.currentArea : (viewDef ? viewDef->areaNum : -1);
    sceneInputs.portalPolicy.totalAreas = lightUniverseStats.totalAreas;
    sceneInputs.portalPolicy.staticAreaPreloadSteps = idMath::ClampInt(0, 8, r_pathTracingStaticAreaPreloadPortalSteps.GetInteger());
    sceneInputs.portalPolicy.rigidResidencySteps = idMath::ClampInt(0, 8, r_pathTracingRigidResidencyPortalSteps.GetInteger());
    sceneInputs.portalPolicy.lightAreaSteps = idMath::ClampInt(0, 8, r_pathTracingLightAreaPortalSteps.GetInteger());
    sceneInputs.portalPolicy.sceneUniverseSteps = idMath::ClampInt(0, 8, r_pathTracingScenePortalSteps.GetInteger());
    sceneInputs.portalPolicy.selectedAreaCount = lightUniverseStats.selectedAreaCount;
    sceneInputs.portalPolicy.portalEdges = lightUniverseStats.selectedPortalEdges;
    sceneInputs.portalPolicy.blockedPortalEdges = lightUniverseStats.selectedBlockedPortalEdges;
    sceneInputs.portalPolicy.rigidSelectedAreaCount = rigidResidencyStats.selectedAreas;
    sceneInputs.portalPolicy.rigidPortalEdges = rigidResidencyStats.portalEdges;
    sceneInputs.portalPolicy.rigidBlockedPortalEdges = rigidResidencyStats.blockedPortalEdges;
    sceneInputs.portalPolicy.defaultPolicyEquivalent =
        sceneInputs.portalPolicy.staticAreaPreloadSteps == sceneInputs.portalPolicy.rigidResidencySteps &&
        sceneInputs.portalPolicy.rigidResidencySteps == sceneInputs.portalPolicy.lightAreaSteps;

    sceneInputs.geometry.tlas = m_smokeTlas;
    sceneInputs.geometry.staticBlas = smokeStaticBlas;
    sceneInputs.geometry.dynamicBlas = smokeDynamicBlas;
    sceneInputs.geometry.staticVertexBuffer = smokeStaticVertexBuffer;
    sceneInputs.geometry.staticIndexBuffer = smokeStaticIndexBuffer;
    sceneInputs.geometry.staticTriangleClassBuffer = smokeStaticTriangleClassBuffer;
    sceneInputs.geometry.staticTriangleMaterialBuffer = smokeStaticTriangleMaterialBuffer;
    sceneInputs.geometry.staticTriangleMaterialIndexBuffer = smokeStaticTriangleMaterialIndexBuffer;
    sceneInputs.geometry.previousStaticVertexBuffer = smokePreviousStaticVertexBuffer;
    sceneInputs.geometry.previousStaticIndexBuffer = smokePreviousStaticIndexBuffer;
    sceneInputs.geometry.previousStaticTriangleClassBuffer = smokePreviousStaticTriangleClassBuffer;
    sceneInputs.geometry.previousStaticTriangleMaterialBuffer = smokePreviousStaticTriangleMaterialBuffer;
    sceneInputs.geometry.previousStaticTriangleMaterialIndexBuffer = smokePreviousStaticTriangleMaterialIndexBuffer;
    sceneInputs.geometry.dynamicVertexBuffer = smokeDynamicVertexBuffer;
    sceneInputs.geometry.dynamicIndexBuffer = smokeDynamicIndexBuffer;
    sceneInputs.geometry.dynamicTriangleClassBuffer = smokeDynamicTriangleClassBuffer;
    sceneInputs.geometry.dynamicTriangleMaterialBuffer = smokeDynamicTriangleMaterialBuffer;
    sceneInputs.geometry.dynamicTriangleMaterialIndexBuffer = smokeDynamicTriangleMaterialIndexBuffer;
    sceneInputs.geometry.rigidRouteVertexBuffer = smokeRigidRouteVertexBuffer;
    sceneInputs.geometry.rigidRouteIndexBuffer = smokeRigidRouteIndexBuffer;
    sceneInputs.geometry.rigidRouteTriangleMaterialBuffer = smokeRigidRouteTriangleMaterialBuffer;
    sceneInputs.geometry.rigidRouteTriangleMaterialIndexBuffer = smokeRigidRouteTriangleMaterialIndexBuffer;
    sceneInputs.geometry.rigidRouteInstanceBuffer = smokeRigidRouteInstanceBuffer;
    sceneInputs.geometry.skinnedSourceVertexBuffer = smokeSkinnedSourceVertexBuffer;
    sceneInputs.geometry.skinnedCurrentOutputVertexBuffer = smokeSkinnedCurrentOutputVertexBuffer;
    sceneInputs.geometry.skinnedPreviousPositionBuffer = smokeSkinnedPreviousPositionBuffer;
    sceneInputs.geometry.skinnedSurfaceDispatchBuffer = smokeSkinnedSurfaceDispatchBuffer;
    sceneInputs.geometry.skinnedCurrentJointMatrixBuffer = smokeSkinnedCurrentJointMatrixBuffer;
    sceneInputs.geometry.skinnedPreviousJointMatrixBuffer = smokeSkinnedPreviousJointMatrixBuffer;
    sceneInputs.geometry.staticVertexCount = staticVertexCacheCount;
    sceneInputs.geometry.staticIndexCount = staticIndexCacheCount;
    sceneInputs.geometry.staticTriangleCount = staticTriangleCacheCount;
    sceneInputs.geometry.staticMaterialIndexCount = static_cast<int>(materialTable.staticMaterialIndexes.size());
    sceneInputs.geometry.previousStaticVertexCount = m_sceneInputs.geometry.staticVertexCount;
    sceneInputs.geometry.previousStaticIndexCount = m_sceneInputs.geometry.staticIndexCount;
    sceneInputs.geometry.previousStaticTriangleCount = m_sceneInputs.geometry.staticTriangleCount;
    sceneInputs.geometry.previousStaticMaterialIndexCount = m_sceneInputs.geometry.staticMaterialIndexCount;
    sceneInputs.geometry.previousStaticCpuVertexCount = geometryUniverseStats.previousStaticVerts;
    sceneInputs.geometry.previousStaticCpuIndexCount = geometryUniverseStats.previousStaticIndexes;
    sceneInputs.geometry.previousStaticCpuTriangleCount = geometryUniverseStats.previousStaticTriangles;
    sceneInputs.geometry.previousStaticCpuMaterialIndexCount = static_cast<int>(previousStaticTriangleMaterialIndexCache.size());
    sceneInputs.geometry.previousStaticCpuBytesKB = geometryUniverseStats.previousStaticBytesKB;
    sceneInputs.geometry.staticSeenSurfaceCount = geometryUniverseStats.staticSeenThisFrame;
    sceneInputs.geometry.staticNewSurfaceCount = geometryUniverseStats.staticNewThisFrame;
    sceneInputs.geometry.staticGoneSurfaceCount = geometryUniverseStats.staticDisappearedThisFrame;
    sceneInputs.geometry.staticHistoryValidSurfaceCount = geometryUniverseStats.staticHistoryValid;
    sceneInputs.geometry.staticPreviousRangeValidSurfaceCount = geometryUniverseStats.staticPreviousRangeValid;
    sceneInputs.geometry.staticDirtySurfaceCount = geometryUniverseStats.staticDirty;
    sceneInputs.geometry.staticDirtyVertexOffset = geometryUniverseStats.staticDirtyVertexOffset;
    sceneInputs.geometry.staticDirtyVertexCount = geometryUniverseStats.staticDirtyVertexCount;
    sceneInputs.geometry.staticDirtyIndexOffset = geometryUniverseStats.staticDirtyIndexOffset;
    sceneInputs.geometry.staticDirtyIndexCount = geometryUniverseStats.staticDirtyIndexCount;
    sceneInputs.geometry.staticDirtyTriangleOffset = geometryUniverseStats.staticDirtyTriangleOffset;
    sceneInputs.geometry.staticDirtyTriangleCount = geometryUniverseStats.staticDirtyTriangleCount;
    sceneInputs.geometry.staticDirtyRangeUploadUsed = useStaticDirtyRangeUploads;
    sceneInputs.geometry.staticPreviousCountsMatch =
        m_sceneInputs.valid &&
        m_sceneInputs.geometry.staticVertexCount == staticVertexCacheCount &&
        m_sceneInputs.geometry.staticIndexCount == staticIndexCacheCount &&
        m_sceneInputs.geometry.staticTriangleCount == staticTriangleCacheCount;
    const bool staticPreviousMaterialIndexCountsMatch =
        sceneInputs.geometry.previousStaticMaterialIndexCount > 0 &&
        sceneInputs.geometry.previousStaticCpuMaterialIndexCount == sceneInputs.geometry.previousStaticMaterialIndexCount;
    sceneInputs.geometry.staticPreviousRangesComplete =
        sceneInputs.geometry.staticSeenSurfaceCount > 0 &&
        sceneInputs.geometry.staticPreviousRangeValidSurfaceCount == sceneInputs.geometry.staticSeenSurfaceCount;
    sceneInputs.geometry.staticPreviousBuffersAvailable =
        sceneInputs.geometry.staticPreviousCountsMatch &&
        sceneInputs.geometry.staticPreviousRangesComplete &&
        sceneInputs.geometry.previousStaticVertexBuffer &&
        sceneInputs.geometry.previousStaticIndexBuffer &&
        sceneInputs.geometry.previousStaticTriangleClassBuffer &&
        sceneInputs.geometry.previousStaticTriangleMaterialBuffer &&
        sceneInputs.geometry.previousStaticTriangleMaterialIndexBuffer &&
        staticPreviousMaterialIndexCountsMatch;
    sceneInputs.geometry.staticPreviousMaterialIndexBufferAvailable =
        sceneInputs.geometry.staticPreviousBuffersAvailable &&
        sceneInputs.geometry.previousStaticTriangleMaterialIndexBuffer;
    sceneInputs.geometry.staticPreviousGpuSnapshotAvailable =
        sceneInputs.geometry.staticPreviousBuffersAvailable &&
        sceneInputs.geometry.previousStaticVertexBuffer &&
        sceneInputs.geometry.previousStaticIndexBuffer &&
        sceneInputs.geometry.previousStaticTriangleClassBuffer &&
        sceneInputs.geometry.previousStaticTriangleMaterialBuffer &&
        sceneInputs.geometry.previousStaticTriangleMaterialIndexBuffer;
    sceneInputs.geometry.staticPreviousGpuSnapshotUploadUsed =
        previousStaticSnapshotDataAvailable &&
        sceneInputs.geometry.staticPreviousGpuSnapshotAvailable &&
        !skipPreviousStaticSnapshotUpload;
    sceneInputs.geometry.staticPreviousBuffersAliasCurrent =
        sceneInputs.geometry.staticPreviousBuffersAvailable &&
        sceneInputs.geometry.previousStaticVertexBuffer == sceneInputs.geometry.staticVertexBuffer &&
        sceneInputs.geometry.previousStaticIndexBuffer == sceneInputs.geometry.staticIndexBuffer &&
        sceneInputs.geometry.previousStaticTriangleClassBuffer == sceneInputs.geometry.staticTriangleClassBuffer &&
        sceneInputs.geometry.previousStaticTriangleMaterialBuffer == sceneInputs.geometry.staticTriangleMaterialBuffer &&
        sceneInputs.geometry.previousStaticTriangleMaterialIndexBuffer == sceneInputs.geometry.staticTriangleMaterialIndexBuffer;
    sceneInputs.geometry.staticPreviousCpuSnapshotAvailable = geometryUniverseStats.previousStaticCpuSnapshotAvailable;
    sceneInputs.geometry.dynamicVertexCount = dynamicVertexCount;
    sceneInputs.geometry.dynamicIndexCount = dynamicIndexCount;
    sceneInputs.geometry.dynamicTriangleCount = dynamicIndexCount / 3;
    sceneInputs.geometry.rigidRouteVertexCount = rigidRouteBuild.stats.vertices;
    sceneInputs.geometry.rigidRouteIndexCount = rigidRouteBuild.stats.indexes;
    sceneInputs.geometry.rigidRouteTriangleCount = rigidRouteBuild.stats.triangles;
    sceneInputs.geometry.rigidRouteInstanceCount = rigidRouteBuild.stats.emittedInstances;
    sceneInputs.geometry.rigidRoutePreviousTransformCount = rigidRouteBuild.stats.previousTransformInstances;
    sceneInputs.geometry.rigidRouteTransformContinuousCount = rigidRouteBuild.stats.transformContinuousInstances;
    sceneInputs.geometry.skinnedSurfaceCount = classStats.skinnedDeformedSurfaces;
    sceneInputs.geometry.skinnedTriangleCount = classStats.skinnedDeformedTriangles;
    sceneInputs.geometry.skinnedRtCpuSurfaceCount = m_smokeSkinnedPreviousStats.currentRtCpuSkinnedSurfaceCount;
    sceneInputs.geometry.skinnedPreviousMatchedSurfaceCount = m_smokeSkinnedPreviousStats.previousMatchedSurfaceCount;
    sceneInputs.geometry.skinnedPreviousInvalidSurfaceCount = m_smokeSkinnedPreviousStats.previousInvalidSurfaceCount;
    sceneInputs.geometry.skinnedPreviousRetainedVertexCount = m_smokeSkinnedPreviousStats.previousRetainedVertexCount;
    sceneInputs.geometry.skinnedPreviousNoFrameCount = m_smokeSkinnedPreviousStats.noPreviousFrameCount;
    sceneInputs.geometry.skinnedPreviousNoSurfaceCount = m_smokeSkinnedPreviousStats.noPreviousSurfaceCount;
    sceneInputs.geometry.skinnedPreviousCountMismatchCount =
        m_smokeSkinnedPreviousStats.vertexCountMismatchCount +
        m_smokeSkinnedPreviousStats.indexCountMismatchCount +
        m_smokeSkinnedPreviousStats.triangleCountMismatchCount;
    sceneInputs.geometry.skinnedPreviousMaterialChangedCount = m_smokeSkinnedPreviousStats.materialChangedCount;
    sceneInputs.geometry.skinnedPreviousSurfaceClassChangedCount = m_smokeSkinnedPreviousStats.surfaceClassChangedCount;
    sceneInputs.geometry.skinnedPreviousNotRtCpuSkinnedCount = m_smokeSkinnedPreviousStats.notRtCpuSkinnedCount;
    sceneInputs.geometry.skinnedPreviousSkeletonChangedCount = m_smokeSkinnedPreviousStats.skeletonChangedCount;
    sceneInputs.geometry.skinnedPreviousTransformDiscontinuityCount = m_smokeSkinnedPreviousStats.transformDiscontinuityCount;
    sceneInputs.geometry.skinnedPreviousBufferUnavailableCount = m_smokeSkinnedPreviousStats.previousBufferUnavailableCount;
    sceneInputs.geometry.skinnedTemporalTopologyStableCount = m_smokeSkinnedPreviousStats.topologyStableCount;
    sceneInputs.geometry.skinnedTemporalLodStableCount = m_smokeSkinnedPreviousStats.lodStableCount;
    sceneInputs.geometry.skinnedTemporalTransformContinuousCount = m_smokeSkinnedPreviousStats.transformContinuousCount;
    sceneInputs.geometry.skinnedTemporalDeformationContinuousCount = m_smokeSkinnedPreviousStats.deformationContinuousCount;
    sceneInputs.geometry.skinnedTemporalMaterialStableCount = m_smokeSkinnedPreviousStats.materialStableCount;
    sceneInputs.geometry.skinnedTemporalPreviousBufferValidCount = m_smokeSkinnedPreviousStats.previousBufferValidCount;
    sceneInputs.geometry.skinnedGpuSkinningMode = gpuSkinningMode;
    sceneInputs.geometry.skinnedSourceVertexCount = static_cast<int>(skinnedGpuScaffold.sourceVertices.size());
    sceneInputs.geometry.skinnedCurrentOutputVertexCount = static_cast<int>(skinnedGpuScaffold.currentOutputVertices.size());
    sceneInputs.geometry.skinnedPreviousPositionCount = static_cast<int>(skinnedGpuScaffold.previousPositions.size());
    sceneInputs.geometry.skinnedSurfaceDispatchCount = static_cast<int>(skinnedGpuScaffold.dispatchRecords.size());
    for (const PathTraceSkinnedSurfaceDispatchRecord& dispatch : skinnedGpuScaffold.dispatchRecords)
    {
        if ((dispatch.flags & PT_SKINNED_DISPATCH_HAS_VALID_PREVIOUS) == 0u || dispatch.previousPositionOffset == UINT32_MAX)
        {
            continue;
        }

        const uint64 previousEnd = static_cast<uint64>(dispatch.previousPositionOffset) + static_cast<uint64>(dispatch.vertexCount);
        sceneInputs.geometry.skinnedPreviousDispatchMaxEnd = Max(sceneInputs.geometry.skinnedPreviousDispatchMaxEnd, static_cast<int>(Min<uint64>(previousEnd, static_cast<uint64>(INT_MAX))));
        if (previousEnd <= static_cast<uint64>(skinnedGpuScaffold.previousPositions.size()))
        {
            ++sceneInputs.geometry.skinnedPreviousDispatchValidCount;
        }
        else
        {
            ++sceneInputs.geometry.skinnedPreviousDispatchOutOfRangeCount;
        }
    }
    sceneInputs.geometry.skinnedCurrentJointMatrixCount = static_cast<int>(skinnedGpuScaffold.currentJointMatrices.size());
    sceneInputs.geometry.skinnedPreviousJointMatrixCount = static_cast<int>(skinnedGpuScaffold.previousJointMatrices.size());
    sceneInputs.geometry.currentGeometryValid = hasStaticBlas || hasDynamicBlas;
    sceneInputs.geometry.previousTransformAvailable = rigidRouteBuild.stats.previousTransformInstances > 0;
    sceneInputs.geometry.skinnedPreviousCpuVertexDataRetained = m_smokeSkinnedPreviousStats.previousRetainedVertexCount > 0;
    sceneInputs.geometry.skinnedSourceGeometryAvailable =
        smokeSkinnedSourceVertexBuffer &&
        smokeSkinnedCurrentOutputVertexBuffer &&
        smokeSkinnedSurfaceDispatchBuffer &&
        !skinnedGpuScaffold.sourceVertices.empty() &&
        !skinnedGpuScaffold.currentOutputVertices.empty() &&
        !skinnedGpuScaffold.dispatchRecords.empty();
    sceneInputs.geometry.skinnedPreviousPositionBufferAvailable =
        smokeSkinnedPreviousPositionBuffer &&
        !skinnedGpuScaffold.previousPositions.empty();
    sceneInputs.geometry.skinnedGpuSkinningAvailable = false;
    sceneInputs.geometry.capabilityFlags =
        RT_SCENE_INPUT_GEOMETRY_PREVIOUS_TRANSFORM_RESERVED |
        RT_SCENE_INPUT_GEOMETRY_PREVIOUS_VERTEX_RESERVED |
        RT_SCENE_INPUT_SKINNED_SOURCE_GEOMETRY_RESERVED |
        RT_SCENE_INPUT_SKINNED_GPU_SKINNING_RESERVED;

    sceneInputs.materials.materialTableBuffer = smokeMaterialTableBuffer;
    sceneInputs.materials.textureDescriptorTable = bindingBuildResult.textureDescriptorTable;
    sceneInputs.materials.materialTableEntryCount = static_cast<int>(materialTable.materials.size());
    sceneInputs.materials.activeTextureCount = static_cast<int>(bindingBuildResult.activeTextureTable.size());
    sceneInputs.materials.materialTablePath = materialTablePath;
    sceneInputs.materials.capabilityFlags = RT_SCENE_INPUT_MATERIAL_STOPGAP_CLASSIFIER | RT_SCENE_INPUT_MATERIAL_IDTECH4_SEMANTICS_RESERVED | RT_SCENE_INPUT_MATERIAL_PBR_ROLES_RESERVED;

    sceneInputs.lights.emissiveTriangleBuffer = smokeEmissiveTriangleBuffer;
    sceneInputs.lights.lightCandidateBuffer = smokeLightCandidateBuffer;
    sceneInputs.lights.doomAnalyticLightBuffer = smokeDoomAnalyticLightBuffer;
    sceneInputs.lights.emissiveTriangleCount = emissiveInventoryStats.capturedTriangles;
    sceneInputs.lights.emissiveStaticTriangleCount = emissiveInventoryStats.staticTriangles;
    sceneInputs.lights.emissiveDynamicTriangleCount = emissiveInventoryStats.dynamicTriangles;
    sceneInputs.lights.lightCandidateCount = emissiveInventoryStats.candidateMaterials;
    sceneInputs.lights.texturedLightCandidateCount = emissiveInventoryStats.texturedCandidateMaterials;
    sceneInputs.lights.doomAnalyticLightCount = static_cast<int>(doomAnalyticLights.size());
    sceneInputs.lights.lightUniverseGeneration = lightUniverseStats.generation;
    sceneInputs.lights.capabilityFlags = RT_SCENE_INPUT_LIGHT_PREVIOUS_IDENTITY_RESERVED;

    sceneInputs.diagnostics.geometryUploadBytes = staticUploadBytes + previousStaticUploadBytes + dynamicUploadBytes + rigidRouteUploadBytes;
    sceneInputs.diagnostics.materialUploadBytes = materialUploadBytes;
    sceneInputs.diagnostics.lightUploadBytes = lightUploadBytes;
    sceneInputs.diagnostics.sceneBuildMs = Sys_Milliseconds() - sceneStartMs;
    sceneInputs.diagnostics.captureMs = captureMs;
    sceneInputs.diagnostics.materialMs = materialMs;
    sceneInputs.diagnostics.emissiveMs = emissiveMs;
    sceneInputs.diagnostics.bufferCreateMs = bufferCreateMs;
    sceneInputs.diagnostics.bufferUploadMs = bufferUploadMs;
    sceneInputs.diagnostics.accelSubmitMs = accelSubmitMs;

    RtSmokeSceneResourceCommitBuildDesc resourceCommitBuildDesc;
    resourceCommitBuildDesc.sceneInputs = sceneInputs;
    resourceCommitBuildDesc.buffers = smokeBuffers;
    resourceCommitBuildDesc.staticBlasDesc = smokeStaticBlasDesc;
    resourceCommitBuildDesc.dynamicBlasDesc = smokeDynamicBlasDesc;
    resourceCommitBuildDesc.staticBlas = smokeStaticBlas;
    resourceCommitBuildDesc.dynamicBlas = smokeDynamicBlas;
    resourceCommitBuildDesc.tlas = m_smokeTlas;
    resourceCommitBuildDesc.hasStaticBlas = hasStaticBlas;
    resourceCommitBuildDesc.staticBlasSignature = staticSignature.hash;
    resourceCommitBuildDesc.bindingSet = bindingBuildResult.bindingSet;
    resourceCommitBuildDesc.textureDescriptorTable = bindingBuildResult.textureDescriptorTable;
    resourceCommitBuildDesc.activeTextureTable = &bindingBuildResult.activeTextureTable;
    resourceCommitBuildDesc.textureDescriptorTableCreated = bindingBuildResult.textureDescriptorTableCreated;
    resourceCommitBuildDesc.textureDescriptorTableWritten = bindingBuildResult.textureDescriptorTableWritten;
    resourceCommitBuildDesc.materialTableEntryCount = static_cast<int>(materialTable.materials.size());
    resourceCommitBuildDesc.emissiveTriangleCount = emissiveInventoryStats.capturedTriangles;
    resourceCommitBuildDesc.emissiveStaticTriangleCount = emissiveInventoryStats.staticTriangles;
    resourceCommitBuildDesc.emissiveDynamicTriangleCount = emissiveInventoryStats.dynamicTriangles;
    resourceCommitBuildDesc.lightCandidateCount = emissiveInventoryStats.candidateMaterials;
    resourceCommitBuildDesc.texturedLightCandidateCount = emissiveInventoryStats.texturedCandidateMaterials;
    resourceCommitBuildDesc.lightCandidateBytes = static_cast<int>(lightCandidates.size() * sizeof(lightCandidates[0]));
    resourceCommitBuildDesc.doomAnalyticLightCount = static_cast<int>(doomAnalyticLights.size());
    resourceCommitBuildDesc.doomAnalyticLightBytes = static_cast<int>(doomAnalyticLights.size() * sizeof(PathTraceDoomAnalyticLightCandidate));
    resourceCommitBuildDesc.reservoirSceneSignature = reservoirSceneSignature;
    const RtSmokeSceneResourceCommitDesc resourceCommitDesc = CreateSmokeSceneResourceCommitDesc(resourceCommitBuildDesc);
    {
        OPTICK_EVENT("PT Commit Scene Resources");
        CommitRayTracingSmokeSceneResources(resourceCommitDesc);
    }
    m_smokePreviousStaticTriangleMaterialIndexes = materialTable.staticMaterialIndexes;
    m_smokePreviousStaticSnapshotUploadSignature = previousStaticSnapshotUploadSignature;

    const int sceneMs = Sys_Milliseconds() - sceneStartMs;
    RtSmokeSceneBuildDiagnosticLogDesc sceneLogDesc;
    sceneLogDesc.sceneMs = sceneMs;
    sceneLogDesc.captureMs = captureMs;
    sceneLogDesc.metadataMs = metadataMs;
    sceneLogDesc.metadataValidationMs = metadataValidationMs;
    sceneLogDesc.metadataRegistrationMs = metadataRegistrationMs;
    sceneLogDesc.materialMs = materialMs;
    sceneLogDesc.emissiveMs = emissiveMs;
    sceneLogDesc.bufferCreateMs = bufferCreateMs;
    sceneLogDesc.bufferUploadMs = bufferUploadMs;
    sceneLogDesc.accelSubmitMs = accelSubmitMs;
    sceneLogDesc.blasSubmitMs = blasSubmitMs;
    sceneLogDesc.tlasSubmitMs = tlasSubmitMs;
    sceneLogDesc.sourceSurfaces = sourceSurfaces;
    sceneLogDesc.sourceVerts = sourceVerts;
    sceneLogDesc.sourceIndexes = sourceIndexes;
    sceneLogDesc.anchorTriangle = anchorTriangle;
    sceneLogDesc.staticIndexCount = staticIndexCount;
    sceneLogDesc.dynamicIndexCount = dynamicIndexCount;
    sceneLogDesc.instanceCount = instanceCount;
    sceneLogDesc.requestedDebugMode = requestedDebugMode;
    sceneLogDesc.staticSurfaceCacheSize = geometryUniverseStats.staticSurfaces;
    sceneLogDesc.staticVertexCacheCount = staticVertexCacheCount;
    sceneLogDesc.staticIndexCacheCount = staticIndexCacheCount;
    sceneLogDesc.staticTriangleCacheCount = staticTriangleCacheCount;
    sceneLogDesc.staticSeenThisFrame = geometryUniverseStats.staticSeenThisFrame;
    sceneLogDesc.staticNewThisFrame = geometryUniverseStats.staticNewThisFrame;
    sceneLogDesc.staticDisappearedThisFrame = geometryUniverseStats.staticDisappearedThisFrame;
    sceneLogDesc.staticHistoryValid = geometryUniverseStats.staticHistoryValid;
    sceneLogDesc.staticPreviousRangeValid = geometryUniverseStats.staticPreviousRangeValid;
    sceneLogDesc.staticDirty = geometryUniverseStats.staticDirty;
    sceneLogDesc.staticValidationErrors = geometryUniverseStats.staticValidationErrors;
    sceneLogDesc.staticRangeErrors = geometryUniverseStats.staticRangeErrors;
    sceneLogDesc.staticDuplicateKeys = geometryUniverseStats.staticDuplicateKeys;
    sceneLogDesc.staticHistoryErrors = geometryUniverseStats.staticHistoryErrors;
    sceneLogDesc.staticKeyVectorMismatches = geometryUniverseStats.staticKeyVectorMismatches;
    sceneLogDesc.staticCacheBytesKB = staticCacheBytesKB;
    sceneLogDesc.staticBlasSignatureReused = staticBlasSignatureReused;
    sceneLogDesc.staticBlasSignatureMs = staticBlasSignatureMs;
    sceneLogDesc.staticBlasCacheHitCount = m_smokeStaticBlasCacheHitCount;
    sceneLogDesc.staticBlasCacheMissCount = m_smokeStaticBlasCacheMissCount;
    sceneLogDesc.sceneCaptureLogIntervalFrames = RT_SMOKE_SCENE_LOG_INTERVAL_FRAMES;
    sceneLogDesc.staticBlasCacheHit = staticBlasCacheHit;
    sceneLogDesc.materialTableCacheHit = materialTableCacheHit;
    sceneLogDesc.enableTextureProbe = enableTextureProbe;
    sceneLogDesc.dumpClassReasons = dumpClassReasons;
    sceneLogDesc.staticBlasSignature = staticSignature.hash;
    sceneLogDesc.materialTableSignature = materialTableSignature;
    sceneLogDesc.materialTablePath = materialTablePath;
    sceneLogDesc.captureTiming = captureTiming;
    sceneLogDesc.classStats = &classStats;
    sceneLogDesc.skipStats = &skipStats;
    sceneLogDesc.dynamicStats = &dynamicStats;
    sceneLogDesc.attributeStats = &attributeStats;
    sceneLogDesc.materialStats = &materialStats;
    sceneLogDesc.bucketRanges = &bucketRanges;
    sceneLogDesc.materialTable = &materialTable;
    sceneLogDesc.emissiveInventoryStats = &emissiveInventoryStats;
    sceneLogDesc.lightCandidateBytes = static_cast<int>(lightCandidates.size() * sizeof(lightCandidates[0]));
    sceneLogDesc.materialTableCacheStats = &materialTableCacheStats;
    sceneLogDesc.materialTableBuildStats = &materialTableBuildStats;
    sceneLogDesc.materialUniverseStats = &materialUniverseStats;
    sceneLogDesc.materialUniverseTableCompareStats = &materialUniverseTableCompareStats;
    sceneLogDesc.textureCoverageStats = &textureCoverageStats;
    sceneLogDesc.reasonSamples = &reasonSamples;
    sceneLogDesc.lastSceneTimingLogMs = &g_smokeLastSceneTimingLogMs;
    sceneLogDesc.sceneRebuildLogged = &m_smokeSceneRebuildLogged;
    sceneLogDesc.sceneLogCooldownFrames = &m_smokeSceneLogCooldownFrames;
    RunSmokeSceneBuildDiagnosticLogs(sceneLogDesc);
}
