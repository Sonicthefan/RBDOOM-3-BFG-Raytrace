#pragma once

#include "PathTraceGeometryLifecycle.h"
#include "PathTraceInstanceUniverse.h"

class idRenderModel;

uint64 BuildPathTraceRigidMeshHash(
    const RtPathTraceMeshKey& key,
    const idRenderModel* model,
    int modelSurfaceIndex);

uint64 BuildPathTraceRigidInstanceId(
    uint64 meshHash,
    const PtRenderDefKey& renderDefKey,
    int entityIndex,
    int renderEntityNum,
    int modelSurfaceIndex,
    uint32_t materialId);
