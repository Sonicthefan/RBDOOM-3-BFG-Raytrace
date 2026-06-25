#pragma once

#include "PathTraceGeometryLifecycle.h"
#include "PathTraceInstanceUniverse.h"

class idRenderModel;
struct srfTriangles_t;

struct RtPathTraceRigidInstanceSnapshot
{
    RtPathTraceMeshKey meshKey;
    const idRenderModel* model = nullptr;
    PtRenderDefKey renderDefKey;
    uint32_t modelEpoch = 0;
    int entityIndex = -1;
    int renderEntityNum = -1;
    int modelSurfaceIndex = -1;
    uint32_t materialId = 0;
    uint32_t materialClassSignature = 0;
    uint32_t sourceFlags = 0;
    uint64 meshHash = 0;
    uint64 instanceId = 0;
};

int ResolvePathTraceRigidModelSurfaceIndex(
    const idRenderModel* model,
    const srfTriangles_t* tri,
    int requestedModelSurfaceIndex);

uint64 BuildPathTraceRigidMeshHash(
    const RtPathTraceMeshKey& key,
    const idRenderModel* model,
    uint32_t modelEpoch,
    int modelSurfaceIndex);

uint64 BuildPathTraceRigidInstanceId(
    uint64 meshHash,
    const PtRenderDefKey& renderDefKey,
    uint32_t modelEpoch,
    int entityIndex,
    int renderEntityNum,
    int modelSurfaceIndex,
    uint32_t materialId);

RtPathTraceRigidInstanceSnapshot BuildPathTraceRigidInstanceSnapshot(
    const RtPathTraceMeshKey& key,
    const idRenderModel* model,
    const srfTriangles_t* tri,
    const PtRenderDefKey& renderDefKey,
    uint32_t modelEpoch,
    int entityIndex,
    int renderEntityNum,
    int requestedModelSurfaceIndex,
    uint32_t sourceFlags);
