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

uint64 BuildPathTraceRigidMeshHash(
    const RtPathTraceMeshKey& key,
    const idRenderModel* model,
    int modelSurfaceIndex)
{
    uint64 hash = 14695981039346656037ull;
    const uintptr_t modelIdentity = reinterpret_cast<uintptr_t>(model);
    hash = HashRigidIdentityBytes(hash, &modelIdentity, sizeof(modelIdentity));
    hash = HashRigidIdentityBytes(hash, &modelSurfaceIndex, sizeof(modelSurfaceIndex));
    hash = HashRigidIdentityBytes(hash, &key.vertexBufferIdentity, sizeof(key.vertexBufferIdentity));
    hash = HashRigidIdentityBytes(hash, &key.indexBufferIdentity, sizeof(key.indexBufferIdentity));
    hash = HashRigidIdentityBytes(hash, &key.numVerts, sizeof(key.numVerts));
    hash = HashRigidIdentityBytes(hash, &key.numIndexes, sizeof(key.numIndexes));
    hash = HashRigidIdentityBytes(hash, &key.vertexFormat, sizeof(key.vertexFormat));
    hash = HashRigidIdentityBytes(hash, &key.materialId, sizeof(key.materialId));
    hash = HashRigidIdentityBytes(hash, &key.sourceKind, sizeof(key.sourceKind));
    return hash;
}

uint64 BuildPathTraceRigidInstanceId(
    uint64 meshHash,
    const PtRenderDefKey& renderDefKey,
    int entityIndex,
    int renderEntityNum,
    int modelSurfaceIndex,
    uint32_t materialId)
{
    uint64 hash = 14695981039346656037ull;
    hash = HashRigidIdentityBytes(hash, &renderDefKey.world, sizeof(renderDefKey.world));
    hash = HashRigidIdentityBytes(hash, &renderDefKey.index, sizeof(renderDefKey.index));
    hash = HashRigidIdentityBytes(hash, &renderDefKey.generation, sizeof(renderDefKey.generation));
    hash = HashRigidIdentityBytes(hash, &entityIndex, sizeof(entityIndex));
    hash = HashRigidIdentityBytes(hash, &renderEntityNum, sizeof(renderEntityNum));
    hash = HashRigidIdentityBytes(hash, &modelSurfaceIndex, sizeof(modelSurfaceIndex));
    hash = HashRigidIdentityBytes(hash, &materialId, sizeof(materialId));
    hash = HashRigidIdentityBytes(hash, &meshHash, sizeof(meshHash));
    return hash;
}
