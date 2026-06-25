#pragma once

#include "PathTraceGeometryLifecycle.h"
#include "PathTraceInstanceUniverse.h"

class idRenderModel;

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
