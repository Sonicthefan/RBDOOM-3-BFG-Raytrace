#include "precompiled.h"
#pragma hdrstop

#include "PathTraceCVars.h"
#include "PathTraceDynamicMaterialState.h"
#include "PathTraceEntityFeed.h"
#include "PathTraceGeometryUniverse.h"
#include "PathTraceRigidIdentity.h"
#include "PathTraceSurfaceClassification.h"
#include "../RenderCommon.h"
#include "../RenderWorld_local.h"

#include <unordered_map>
#include <unordered_set>

namespace {

bool SurfaceUsesStaticModelWithJoints(const idRenderModel* model, int surfaceIndex)
{
    const modelSurface_t* surface = model ? model->Surface(surfaceIndex) : nullptr;
    const srfTriangles_t* tri = surface ? surface->geometry : nullptr;
    return tri && tri->staticModelWithJoints != nullptr;
}

bool EntityFeedModelHasJointData(const idRenderEntityLocal* entity, const idRenderModel* model)
{
    const renderEntity_t& renderEntity = entity->parms;
    if (renderEntity.numJoints > 0 || renderEntity.joints != nullptr || (model && model->NumJoints() > 0))
    {
        return true;
    }

    const int surfaceCount = model ? model->NumSurfaces() : 0;
    for (int surfaceIndex = 0; surfaceIndex < surfaceCount; ++surfaceIndex)
    {
        if (SurfaceUsesStaticModelWithJoints(model, surfaceIndex))
        {
            return true;
        }
    }

    return false;
}

unsigned int HashEntityFeedJoints(const renderEntity_t& renderEntity)
{
    if (!renderEntity.joints || renderEntity.numJoints <= 0)
    {
        return 0;
    }

    return MD5_BlockChecksum(renderEntity.joints, renderEntity.numJoints * sizeof(idJointMat));
}

void AccumulateEntityFeedSurfaceClass(
    RtPathTraceEntityFeedStats& stats,
    RtPtFeedClass feedClass)
{
    switch (feedClass)
    {
        case RtPtFeedClass::StaticWorld:
            ++stats.candidatesS0;
            break;
        case RtPtFeedClass::RigidEntity:
            ++stats.candidatesS1;
            break;
        case RtPtFeedClass::RigidSkinned:
            ++stats.candidatesS2;
            break;
        case RtPtFeedClass::TrueDeform:
            ++stats.candidatesS3;
            break;
        default:
            ++stats.candidatesS4;
            break;
    }
}

bool EntityFeedSurfaceUsableForRigidRoute(const modelSurface_t* surface, const srfTriangles_t* tri, const idMaterial* material)
{
    return
        surface != nullptr &&
        tri != nullptr &&
        tri->verts != nullptr &&
        tri->indexes != nullptr &&
        tri->numVerts > 0 &&
        tri->numIndexes > 0 &&
        material != nullptr &&
        material->IsDrawn();
}

}

void DumpEntityFeedStats(const RtPathTraceEntityFeedStats& s)
{
    common->Printf(
        "PathTracePrimaryPass: PT entityFeed frame=%d reachableAreas=%d candidatesS0=%d candidatesS1=%d candidatesS2=%d candidatesS3=%d candidatesS4=%d admitted=%d droppedBudget=%d\n",
        s.frameIndex,
        s.reachableAreas,
        s.candidatesS0,
        s.candidatesS1,
        s.candidatesS2,
        s.candidatesS3,
        s.candidatesS4,
        s.admitted,
        s.droppedBudget);
}

std::vector<bool> BuildEntityFeedReachableAreas(const viewDef_t* viewDef, int maxDepth, float maxDistance)
{
    std::vector<bool> reachableAreas;
    idRenderWorldLocal* renderWorld = viewDef ? viewDef->renderWorld : nullptr;
    if (!renderWorld)
    {
        return reachableAreas;
    }

    const int areaCount = renderWorld->NumAreas();
    if (areaCount <= 0)
    {
        return reachableAreas;
    }

    int seedArea = renderWorld->PointInArea(viewDef->renderView.vieworg);
    if (seedArea < 0)
    {
        seedArea = viewDef->areaNum;
    }
    if (seedArea < 0 || seedArea >= areaCount)
    {
        return reachableAreas;
    }

    maxDepth = idMath::ClampInt(0, 128, maxDepth);
    reachableAreas.assign(areaCount, false);
    std::vector<int> areaDepth(areaCount, -1);
    std::vector<int> queue;
    queue.reserve(areaCount);

    reachableAreas[seedArea] = true;
    areaDepth[seedArea] = 0;
    queue.push_back(seedArea);

    const idVec3& viewOrigin = viewDef->renderView.vieworg;
    for (size_t queueIndex = 0; queueIndex < queue.size(); ++queueIndex)
    {
        const int area = queue[queueIndex];
        const int depth = areaDepth[area];
        if (depth >= maxDepth)
        {
            continue;
        }

        const int portalCount = renderWorld->NumPortalsInArea(area);
        for (int portalIndex = 0; portalIndex < portalCount; ++portalIndex)
        {
            const exitPortal_t portal = renderWorld->GetPortal(area, portalIndex);
            int nextArea = -1;
            if (portal.areas[0] == area)
            {
                nextArea = portal.areas[1];
            }
            else if (portal.areas[1] == area)
            {
                nextArea = portal.areas[0];
            }
            if (nextArea < 0 || nextArea >= areaCount || reachableAreas[nextArea])
            {
                continue;
            }
            if (!renderWorld->AreasAreConnected(area, nextArea, PS_BLOCK_VIEW))
            {
                continue;
            }

            const portalArea_t& nextPortalArea = renderWorld->portalAreas[nextArea];
            if (maxDistance > 0.0f && !nextPortalArea.globalBounds.IsCleared())
            {
                const float areaDistanceSqr = (nextPortalArea.globalBounds.GetCenter() - viewOrigin).LengthSqr();
                if (areaDistanceSqr > maxDistance * maxDistance)
                {
                    continue;
                }
            }

            reachableAreas[nextArea] = true;
            areaDepth[nextArea] = depth + 1;
            queue.push_back(nextArea);
        }
    }

    return reachableAreas;
}

void DumpEntityFeedSingleBoneDiagnostics(const viewDef_t* viewDef)
{
    static int lastDumpFrame = -120;
    if (r_pathTracingEntityFeedDump.GetInteger() == 0)
    {
        return;
    }

    const int frameIndex = tr.frameCount;
    if (frameIndex - lastDumpFrame < 120)
    {
        return;
    }
    lastDumpFrame = frameIndex;

    idRenderWorldLocal* renderWorld = viewDef ? viewDef->renderWorld : nullptr;
    if (!renderWorld)
    {
        return;
    }

    for (int entityIndex = 0; entityIndex < renderWorld->entityDefs.Num(); ++entityIndex)
    {
        const idRenderEntityLocal* entity = renderWorld->entityDefs[entityIndex];
        idRenderModel* sourceModel = entity ? entity->parms.hModel : nullptr;
        if (!entity || !sourceModel || !EntityFeedModelHasJointData(entity, sourceModel))
        {
            continue;
        }

        idRenderModel* temporaryModel = nullptr;
        const idRenderModel* diagnosticModel = sourceModel;
        if (sourceModel->IsDynamicModel() != DM_STATIC)
        {
            temporaryModel = sourceModel->InstantiateDynamicModel(&entity->parms, viewDef, nullptr);
            diagnosticModel = temporaryModel;
        }

        if (!diagnosticModel)
        {
            continue;
        }

        const int surfaceCount = diagnosticModel->NumSurfaces();
        for (int surfaceIndex = 0; surfaceIndex < surfaceCount; ++surfaceIndex)
        {
            const modelSurface_t* surface = diagnosticModel->Surface(surfaceIndex);
            const srfTriangles_t* tri = surface ? surface->geometry : nullptr;
            common->Printf(
                "PathTracePrimaryPass: PT entityFeed md5 entity=%d model='%s' surface=%d vertCount=%d singleBone=%d\n",
                entityIndex,
                sourceModel->Name(),
                surfaceIndex,
                tri ? tri->numVerts : 0,
                IsEntityFeedSingleBoneSurface(tri) ? 1 : 0);
        }

        if (temporaryModel && temporaryModel != sourceModel)
        {
            delete temporaryModel;
        }
    }
}

void DumpEntityFeedJointAdvanceProbe(const viewDef_t* viewDef)
{
    if (r_pathTracingEntityFeedDump.GetInteger() == 0)
    {
        return;
    }

    idRenderWorldLocal* renderWorld = viewDef ? viewDef->renderWorld : nullptr;
    if (!renderWorld)
    {
        return;
    }

    static std::unordered_map<int, unsigned int> previousJointHashes;
    int printed = 0;
    for (int entityIndex = 0; entityIndex < renderWorld->entityDefs.Num() && printed < 4; ++entityIndex)
    {
        const idRenderEntityLocal* entity = renderWorld->entityDefs[entityIndex];
        const idRenderModel* model = entity ? entity->parms.hModel : nullptr;
        if (!entity || !model || !EntityFeedModelHasJointData(entity, model))
        {
            continue;
        }

        const unsigned int jointHash = HashEntityFeedJoints(entity->parms);
        const auto previousHash = previousJointHashes.find(entityIndex);
        const bool jointHashChanged = previousHash != previousJointHashes.end() && previousHash->second != jointHash;
        previousJointHashes[entityIndex] = jointHash;

        common->Printf(
            "PathTracePrimaryPass: PT entityFeed jointProbe entity=%d model='%s' jointHashChangedThisFrame=%d onScreenThisFrame=%d\n",
            entityIndex,
            model->Name(),
            jointHashChanged ? 1 : 0,
            entity->viewCount == tr.viewCount ? 1 : 0);
        ++printed;
    }
}

void DumpEntityFeedReachableCandidateStats(const viewDef_t* viewDef)
{
    if (r_pathTracingEntityFeedDump.GetInteger() == 0)
    {
        return;
    }

    idRenderWorldLocal* renderWorld = viewDef ? viewDef->renderWorld : nullptr;
    if (!renderWorld)
    {
        return;
    }

    const std::vector<bool> reachableAreas = BuildEntityFeedReachableAreas(
        viewDef,
        r_pathTracingEntityFeedMaxDepth.GetInteger(),
        r_pathTracingEntityFeedMaxDistance.GetFloat());
    if (reachableAreas.empty())
    {
        return;
    }

    RtPathTraceEntityFeedStats stats;
    stats.frameIndex = tr.frameCount;
    std::unordered_set<int> visitedEntities;
    for (int areaIndex = 0; areaIndex < static_cast<int>(reachableAreas.size()); ++areaIndex)
    {
        if (!reachableAreas[areaIndex])
        {
            continue;
        }
        ++stats.reachableAreas;
        if (areaIndex < 0 || areaIndex >= renderWorld->numPortalAreas)
        {
            continue;
        }

        portalArea_t* area = &renderWorld->portalAreas[areaIndex];
        for (areaReference_t* ref = area->entityRefs.areaNext; ref != &area->entityRefs; ref = ref->areaNext)
        {
            idRenderEntityLocal* entity = ref ? ref->entity : nullptr;
            const int entityIndex = entity ? entity->index : -1;
            if (!entity || entityIndex < 0 || !visitedEntities.insert(entityIndex).second)
            {
                continue;
            }

            idRenderModel* sourceModel = entity->parms.hModel;
            if (!sourceModel)
            {
                ++stats.candidatesS4;
                continue;
            }

            idRenderModel* temporaryModel = nullptr;
            const idRenderModel* diagnosticModel = sourceModel;
            if (sourceModel->IsDynamicModel() != DM_STATIC && EntityFeedModelHasJointData(entity, sourceModel))
            {
                temporaryModel = sourceModel->InstantiateDynamicModel(&entity->parms, viewDef, nullptr);
                diagnosticModel = temporaryModel;
            }

            const int surfaceCount = diagnosticModel ? diagnosticModel->NumSurfaces() : 0;
            if (surfaceCount <= 0)
            {
                AccumulateEntityFeedSurfaceClass(stats, ClassifyEntityFeedSurface(entity, sourceModel, nullptr));
            }
            for (int surfaceIndex = 0; surfaceIndex < surfaceCount; ++surfaceIndex)
            {
                const modelSurface_t* surface = diagnosticModel->Surface(surfaceIndex);
                AccumulateEntityFeedSurfaceClass(stats, ClassifyEntityFeedSurface(entity, sourceModel, surface));
            }

            if (temporaryModel && temporaryModel != sourceModel)
            {
                delete temporaryModel;
            }
        }
    }

    DumpEntityFeedStats(stats);
}

void ProduceEntityFeedRigidEntities(const viewDef_t* viewDef, RtSmokeGeometryUniverse& geometryUniverse, RtPathTraceInstanceUniverse& instanceUniverse)
{
    if (r_pathTracingEntityFeed.GetInteger() == 0)
    {
        return;
    }

    idRenderWorldLocal* renderWorld = viewDef ? viewDef->renderWorld : nullptr;
    if (!renderWorld)
    {
        return;
    }

    const std::vector<bool> reachableAreas = BuildEntityFeedReachableAreas(
        viewDef,
        r_pathTracingEntityFeedMaxDepth.GetInteger(),
        r_pathTracingEntityFeedMaxDistance.GetFloat());
    if (reachableAreas.empty())
    {
        return;
    }

    RtPathTraceEntityFeedStats stats;
    stats.frameIndex = tr.frameCount;

    for (int areaIndex = 0; areaIndex < static_cast<int>(reachableAreas.size()); ++areaIndex)
    {
        if (!reachableAreas[areaIndex])
        {
            continue;
        }
        ++stats.reachableAreas;
        if (areaIndex < 0 || areaIndex >= renderWorld->numPortalAreas)
        {
            continue;
        }

        portalArea_t* area = &renderWorld->portalAreas[areaIndex];
        for (areaReference_t* ref = area->entityRefs.areaNext; ref != &area->entityRefs; ref = ref->areaNext)
        {
            idRenderEntityLocal* entity = ref ? ref->entity : nullptr;
            idRenderModel* model = entity ? entity->parms.hModel : nullptr;
            if (!entity || !model)
            {
                continue;
            }

            const renderEntity_t& renderEntity = entity->parms;
            for (int surfaceIndex = 0; surfaceIndex < model->NumSurfaces(); ++surfaceIndex)
            {
                const modelSurface_t* surface = model->Surface(surfaceIndex);
                if (ClassifyEntityFeedSurface(entity, model, surface) != RtPtFeedClass::RigidEntity)
                {
                    continue;
                }
                ++stats.candidatesS1;

                const srfTriangles_t* tri = surface ? surface->geometry : nullptr;
                const idMaterial* surfaceMaterial = surface ? surface->shader : nullptr;
                const idMaterial* material = R_RemapShaderBySkin(surfaceMaterial, renderEntity.customSkin, renderEntity.customShader);
                if (!EntityFeedSurfaceUsableForRigidRoute(surface, tri, material))
                {
                    continue;
                }

                const uint32_t materialId = SmokeMaterialId(material);
                const uint32_t materialClassSignature = SmokeMaterialRouteClassSignature(material, RtSmokeSurfaceClass::RigidEntity, RtSmokeTranslucentSubtype::Unknown);
                RtPathTraceMeshKey meshKey;
                meshKey.tri = tri;
                meshKey.vertexBufferIdentity = static_cast<uintptr_t>(tri->ambientCache);
                meshKey.indexBufferIdentity = static_cast<uintptr_t>(tri->indexCache);
                meshKey.numVerts = tri->numVerts;
                meshKey.numIndexes = tri->numIndexes;
                meshKey.vertexFormat = static_cast<uint32_t>(RtSmokeGeometryBufferFormat::LegacySmokeVertex);
                meshKey.materialId = materialId;
                meshKey.materialClassSignature = materialClassSignature;
                meshKey.sourceKind = SmokeSurfaceClassId(RtSmokeSurfaceClass::RigidEntity);

                const PtRenderDefKey renderDefKey = PtGeometryLifecycle::MakeEntityKey(entity);
                const uint32_t modelEpoch = PtGeometryLifecycle::EntityModelEpoch(renderDefKey.world, renderDefKey.index);
                uint32_t sourceFlags = RT_PT_INSTANCE_SOURCE_RIGID;
                if (renderEntity.customShader != nullptr || renderEntity.customSkin != nullptr)
                {
                    sourceFlags |= RT_PT_INSTANCE_SOURCE_MATERIAL_OVERRIDE;
                }

                const RtPathTraceRigidInstanceSnapshot rigidSnapshot = BuildPathTraceRigidInstanceSnapshot(
                    meshKey,
                    model,
                    tri,
                    renderDefKey,
                    modelEpoch,
                    entity->index,
                    renderEntity.entityNum,
                    surfaceIndex,
                    sourceFlags);
                if (instanceUniverse.HasFrameInstance(rigidSnapshot.instanceId))
                {
                    continue;
                }

                RtPathTraceMeshObservation meshObservation;
                meshObservation.key = rigidSnapshot.meshKey;
                meshObservation.stableHash = rigidSnapshot.meshHash;
                meshObservation.baseMaterial = material;
                meshObservation.materialName = material ? material->GetName() : "<none>";
                meshObservation.modelName = model ? model->Name() : "<none>";
                meshObservation.localSpaceValid = true;

                RtPathTraceInstanceObservation instanceObservation;
                instanceObservation.meshHash = rigidSnapshot.meshHash;
                instanceObservation.entity = entity;
                instanceObservation.entityIndex = rigidSnapshot.entityIndex;
                instanceObservation.renderEntityNum = rigidSnapshot.renderEntityNum;
                instanceObservation.drawSurfIndex = -1;
                instanceObservation.modelSurfaceIndex = rigidSnapshot.modelSurfaceIndex;
                instanceObservation.currentArea = areaIndex;
                instanceObservation.renderDefKey = rigidSnapshot.renderDefKey;
                instanceObservation.modelEpoch = rigidSnapshot.modelEpoch;
                instanceObservation.materialOverrideId = rigidSnapshot.materialId;
                instanceObservation.sourceFlags = rigidSnapshot.sourceFlags;
                memcpy(instanceObservation.objectToWorld, entity->modelMatrix, sizeof(instanceObservation.objectToWorld));
                instanceObservation.instanceId = rigidSnapshot.instanceId;
                instanceObservation.materialName = meshObservation.materialName;
                instanceObservation.modelName = meshObservation.modelName;

                instanceUniverse.RecordObservation(meshObservation, instanceObservation, RtSmokeSurfaceClass::RigidEntity, tri->numVerts, tri->numIndexes);

                RtPathTraceRigidMeshCandidateObservation candidateObservation;
                candidateObservation.tri = tri;
                candidateObservation.meshHash = rigidSnapshot.meshHash;
                candidateObservation.instanceId = rigidSnapshot.instanceId;
                candidateObservation.vertexBufferIdentity = meshKey.vertexBufferIdentity;
                candidateObservation.indexBufferIdentity = meshKey.indexBufferIdentity;
                candidateObservation.sourceFlags = rigidSnapshot.sourceFlags;
                candidateObservation.materialId = rigidSnapshot.materialId;
                candidateObservation.materialClassSignature = rigidSnapshot.materialClassSignature;
                candidateObservation.surfaceClassId = SmokeSurfaceClassId(RtSmokeSurfaceClass::RigidEntity);
                candidateObservation.vertexFormat = meshKey.vertexFormat;
                candidateObservation.drawSurfIndex = -1;
                candidateObservation.entityIndex = entity->index;
                candidateObservation.renderEntityNum = renderEntity.entityNum;
                candidateObservation.modelEpoch = rigidSnapshot.modelEpoch;
                candidateObservation.numVerts = tri->numVerts;
                candidateObservation.numIndexes = tri->numIndexes;
                candidateObservation.localSpaceValid = meshObservation.localSpaceValid;
                candidateObservation.materialName = meshObservation.materialName;
                candidateObservation.modelName = meshObservation.modelName;
                geometryUniverse.RecordRigidMeshCandidate(candidateObservation);

                ++stats.admitted;
            }
        }
    }

    if (r_pathTracingEntityFeedDump.GetInteger() != 0)
    {
        DumpEntityFeedStats(stats);
    }
}
