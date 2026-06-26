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
    if (requestedModelSurfaceIndex >= 0)
    {
        return requestedModelSurfaceIndex;
    }

    if (!model)
    {
        return -1;
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
    int modelSurfaceIndex,
    int jointIndex)
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
    if (jointIndex >= 0)
    {
        hash = HashRigidIdentityBytes(hash, &jointIndex, sizeof(jointIndex));
    }
    return hash;
}

uint64 BuildPathTraceRigidInstanceId(
    uint64 meshHash,
    const PtRenderDefKey& renderDefKey,
    uint32_t modelEpoch,
    int entityIndex,
    int renderEntityNum,
    int modelSurfaceIndex,
    uint32_t materialId,
    int jointIndex)
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
    if (jointIndex >= 0)
    {
        hash = HashRigidIdentityBytes(hash, &jointIndex, sizeof(jointIndex));
    }
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
    uint32_t sourceFlags,
    int jointIndex)
{
    RtPathTraceRigidInstanceSnapshot snapshot;
    snapshot.meshKey = key;
    snapshot.model = model;
    snapshot.renderDefKey = renderDefKey;
    snapshot.modelEpoch = modelEpoch;
    snapshot.entityIndex = entityIndex;
    snapshot.renderEntityNum = renderEntityNum;
    snapshot.jointIndex = jointIndex;
    snapshot.modelSurfaceIndex = (requestedModelSurfaceIndex >= 0 || RtPathTraceSourceFlagsAreDurableRigid(sourceFlags))
        ? ResolvePathTraceRigidModelSurfaceIndex(model, tri, requestedModelSurfaceIndex)
        : -1;
    snapshot.materialId = key.materialId;
    snapshot.materialClassSignature = key.materialClassSignature;
    snapshot.sourceFlags = sourceFlags;
    snapshot.meshHash = BuildPathTraceRigidMeshHash(snapshot.meshKey, model, modelEpoch, snapshot.modelSurfaceIndex, snapshot.jointIndex);
    snapshot.instanceId = BuildPathTraceRigidInstanceId(
        snapshot.meshHash,
        renderDefKey,
        modelEpoch,
        entityIndex,
        renderEntityNum,
        snapshot.modelSurfaceIndex,
        snapshot.materialId,
        snapshot.jointIndex);
    return snapshot;
}
