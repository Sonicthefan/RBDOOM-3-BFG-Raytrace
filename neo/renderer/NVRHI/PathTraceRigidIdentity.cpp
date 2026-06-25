#include "precompiled.h"
#pragma hdrstop

#include "PathTraceRigidIdentity.h"
#include "PathTraceAcceleration.h"

namespace {

uint64 HashRigidIdentityBytes(uint64 hash, const void* data, size_t size)
{
    return HashSmokeBytes(hash, data, size);
}

}

int ResolvePathTraceRigidModelSurfaceIndex(
    const idRenderModel* model,
    const srfTriangles_t* tri,
    int requestedModelSurfaceIndex)
{
    if (!model)
    {
        return -1;
    }

    if (requestedModelSurfaceIndex >= 0 && requestedModelSurfaceIndex < model->NumSurfaces())
    {
        const modelSurface_t* requestedSurface = model->Surface(requestedModelSurfaceIndex);
        if (!tri || (requestedSurface && requestedSurface->geometry == tri))
        {
            return requestedModelSurfaceIndex;
        }
    }

    if (!tri)
    {
        return -1;
    }

    for (int surfaceIndex = 0; surfaceIndex < model->NumSurfaces(); ++surfaceIndex)
    {
        const modelSurface_t* surface = model->Surface(surfaceIndex);
        if (surface && surface->geometry == tri)
        {
            return surfaceIndex;
        }
    }

    return -1;
}

uint64 BuildPathTraceRigidMeshHash(
    const RtPathTraceMeshKey& key,
    const idRenderModel* model,
    uint32_t modelEpoch,
    int modelSurfaceIndex)
{
    uint64 hash = 14695981039346656037ull;
    const uintptr_t modelIdentity = reinterpret_cast<uintptr_t>(model);
    hash = HashRigidIdentityBytes(hash, &modelIdentity, sizeof(modelIdentity));
    hash = HashRigidIdentityBytes(hash, &modelEpoch, sizeof(modelEpoch));
    hash = HashRigidIdentityBytes(hash, &modelSurfaceIndex, sizeof(modelSurfaceIndex));
    hash = HashRigidIdentityBytes(hash, &key.vertexBufferIdentity, sizeof(key.vertexBufferIdentity));
    hash = HashRigidIdentityBytes(hash, &key.indexBufferIdentity, sizeof(key.indexBufferIdentity));
    hash = HashRigidIdentityBytes(hash, &key.numVerts, sizeof(key.numVerts));
    hash = HashRigidIdentityBytes(hash, &key.numIndexes, sizeof(key.numIndexes));
    hash = HashRigidIdentityBytes(hash, &key.vertexFormat, sizeof(key.vertexFormat));
    hash = HashRigidIdentityBytes(hash, &key.materialId, sizeof(key.materialId));
    hash = HashRigidIdentityBytes(hash, &key.materialClassSignature, sizeof(key.materialClassSignature));
    hash = HashRigidIdentityBytes(hash, &key.sourceKind, sizeof(key.sourceKind));
    return hash;
}

uint64 BuildPathTraceRigidInstanceId(
    uint64 meshHash,
    const PtRenderDefKey& renderDefKey,
    uint32_t modelEpoch,
    int entityIndex,
    int renderEntityNum,
    int modelSurfaceIndex,
    uint32_t materialId)
{
    uint64 hash = 14695981039346656037ull;
    hash = HashRigidIdentityBytes(hash, &renderDefKey.world, sizeof(renderDefKey.world));
    hash = HashRigidIdentityBytes(hash, &renderDefKey.index, sizeof(renderDefKey.index));
    hash = HashRigidIdentityBytes(hash, &renderDefKey.generation, sizeof(renderDefKey.generation));
    hash = HashRigidIdentityBytes(hash, &modelEpoch, sizeof(modelEpoch));
    hash = HashRigidIdentityBytes(hash, &entityIndex, sizeof(entityIndex));
    hash = HashRigidIdentityBytes(hash, &renderEntityNum, sizeof(renderEntityNum));
    hash = HashRigidIdentityBytes(hash, &modelSurfaceIndex, sizeof(modelSurfaceIndex));
    hash = HashRigidIdentityBytes(hash, &materialId, sizeof(materialId));
    hash = HashRigidIdentityBytes(hash, &meshHash, sizeof(meshHash));
    return hash;
}

RtPathTraceRigidInstanceSnapshot BuildPathTraceRigidInstanceSnapshot(
    const RtPathTraceMeshKey& key,
    const idRenderModel* model,
    const srfTriangles_t* tri,
    const PtRenderDefKey& renderDefKey,
    uint32_t modelEpoch,
    int entityIndex,
    int renderEntityNum,
    int requestedModelSurfaceIndex,
    uint32_t sourceFlags)
{
    RtPathTraceRigidInstanceSnapshot snapshot;
    snapshot.meshKey = key;
    snapshot.model = model;
    snapshot.renderDefKey = renderDefKey;
    snapshot.modelEpoch = modelEpoch;
    snapshot.entityIndex = entityIndex;
    snapshot.renderEntityNum = renderEntityNum;
    snapshot.modelSurfaceIndex = ResolvePathTraceRigidModelSurfaceIndex(model, tri, requestedModelSurfaceIndex);
    snapshot.materialId = key.materialId;
    snapshot.materialClassSignature = key.materialClassSignature;
    snapshot.sourceFlags = sourceFlags;
    snapshot.meshHash = BuildPathTraceRigidMeshHash(snapshot.meshKey, model, modelEpoch, snapshot.modelSurfaceIndex);
    snapshot.instanceId = BuildPathTraceRigidInstanceId(
        snapshot.meshHash,
        renderDefKey,
        modelEpoch,
        entityIndex,
        renderEntityNum,
        snapshot.modelSurfaceIndex,
        snapshot.materialId);
    return snapshot;
}
