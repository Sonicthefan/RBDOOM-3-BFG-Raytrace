#include "precompiled.h"
#pragma hdrstop

#include "PathTraceCVars.h"
#include "PathTraceDynamicMaterialState.h"
#include "PathTraceEntityFeed.h"
#include "PathTraceGeometryUniverse.h"
#include "PathTraceMaterialUniverse.h"
#include "PathTraceMaterialTextureDiscovery.h"
#include "PathTraceRigidIdentity.h"
#include "PathTraceSceneCapture.h"
#include "PathTraceSceneUniverse.h"
#include "PathTraceSurfaceClassification.h"
#include "PathTraceTextureRegistry.h"
#include "../RenderCommon.h"
#include "../RenderWorld_local.h"

#include <algorithm>
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

bool EntityFeedCanPromoteRigidEmissiveCard(const idRenderEntityLocal* entity, const idRenderModel* model, const srfTriangles_t* tri, const idMaterial* material)
{
    if (r_pathTracingRigidRouteEmissiveCards.GetInteger() == 0 ||
        !entity ||
        !model ||
        !tri ||
        !material ||
        model->IsStaticWorldModel() ||
        model->IsDynamicModel() != DM_STATIC)
    {
        return false;
    }

    const renderEntity_t& renderEntity = entity->parms;
    if (renderEntity.joints != nullptr ||
        renderEntity.numJoints > 0 ||
        renderEntity.callback != nullptr ||
        renderEntity.forceUpdate != 0 ||
        renderEntity.weaponDepthHack ||
        renderEntity.modelDepthHack != 0.0f ||
        entity->dynamicModel != nullptr ||
        entity->cachedDynamicModel != nullptr ||
        tri->staticModelWithJoints != nullptr)
    {
        return false;
    }

    return SmokeMaterialCanPromoteEntityFeedRigidEmissiveCard(material);
}

struct EntityFeedRigidCandidate
{
    RtPathTraceMeshKey meshKey;
    RtPathTraceRigidInstanceSnapshot rigidSnapshot;
    RtPathTraceMeshObservation meshObservation;
    RtPathTraceInstanceObservation instanceObservation;
    RtPathTraceRigidMeshCandidateObservation candidateObservation;
    float priority = 0.0f;
    float distance = 0.0f;
    bool onScreen = false;
    bool emissive = false;
};

idVec3 EntityFeedEntityOrigin(const idRenderEntityLocal* entity)
{
    return idVec3(entity->modelMatrix[12], entity->modelMatrix[13], entity->modelMatrix[14]);
}

float EntityFeedEntityDistance(const idRenderEntityLocal* entity, const idVec3& viewOrigin)
{
    return (EntityFeedEntityOrigin(entity) - viewOrigin).Length();
}

float EntityFeedProjectedSizeProxy(const idRenderEntityLocal* entity, float distance)
{
    if (!entity || entity->localReferenceBounds.IsCleared())
    {
        return 0.0f;
    }

    const idVec3 extent = entity->localReferenceBounds[1] - entity->localReferenceBounds[0];
    const float radius = extent.Length() * 0.5f;
    const float safeDistance = distance > 1.0f ? distance : 1.0f;
    return radius / safeDistance;
}

bool EntityFeedMaterialIsEmissive(uint32_t materialId)
{
    const RtSmokeMaterialTextureInfo info = ResolveSmokeMaterialTextureInfo(materialId, -1);
    const RtSmokeMaterialUniverseFacts& facts = GetSmokeMaterialUniverseFacts(materialId, info);
    return facts.emissive;
}

float EntityFeedCandidatePriority(bool onScreen, bool emissive, float projectedSize, float distance)
{
    // These weights intentionally make current-frame and emissive entities dominate, then use
    // apparent size and distance to keep the bounded offscreen set stable and nearby.
    constexpr float onScreenWeight = 1000000.0f;
    constexpr float emissiveWeight = 10000.0f;
    constexpr float projectedSizeWeight = 1000.0f;
    constexpr float distanceWeight = 0.01f;
    return
        (onScreen ? onScreenWeight : 0.0f) +
        (emissive ? emissiveWeight : 0.0f) +
        projectedSize * projectedSizeWeight -
        distance * distanceWeight;
}

void RecordEntityFeedRigidCandidate(
    const EntityFeedRigidCandidate& candidate,
    RtSmokeGeometryUniverse& geometryUniverse,
    RtPathTraceInstanceUniverse& instanceUniverse)
{
    instanceUniverse.RecordObservation(
        candidate.meshObservation,
        candidate.instanceObservation,
        RtSmokeSurfaceClass::RigidEntity,
        candidate.meshKey.numVerts,
        candidate.meshKey.numIndexes);
    geometryUniverse.RecordRigidMeshCandidate(candidate.candidateObservation);
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

void ProduceEntityFeedRigidEntities(const viewDef_t* viewDef, RtSmokeGeometryUniverse& geometryUniverse, RtPathTraceInstanceUniverse& instanceUniverse, RtSmokeMaterialStats& materialStats)
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

    const idVec3& viewOrigin = viewDef->renderView.vieworg;
    const float maxDistance = r_pathTracingEntityFeedMaxDistance.GetFloat();
    const int rigidRouteMaxInstances = idMath::ClampInt(1, 510, r_pathTracingRigidRouteMaxInstances.GetInteger());
    int offscreenBudget = rigidRouteMaxInstances;

    RtPathTraceEntityFeedStats stats;
    stats.frameIndex = tr.frameCount;

    std::vector<EntityFeedRigidCandidate> candidates;
    std::unordered_set<uint64> candidateInstanceIds;
    std::unordered_set<uint32_t> registeredMaterialIds;
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
                const srfTriangles_t* tri = surface ? surface->geometry : nullptr;
                const idMaterial* surfaceMaterial = surface ? surface->shader : nullptr;
                const idMaterial* material = R_RemapShaderBySkin(surfaceMaterial, renderEntity.customSkin, renderEntity.customShader);
                const RtPtFeedClass feedClass = ClassifyEntityFeedSurface(entity, model, surface);
                const bool promotedEmissiveCard =
                    feedClass == RtPtFeedClass::Transient &&
                    EntityFeedCanPromoteRigidEmissiveCard(entity, model, tri, material);
                if (promotedEmissiveCard && entity->viewCount == tr.viewCount)
                {
                    continue;
                }
                if (feedClass != RtPtFeedClass::RigidEntity && !promotedEmissiveCard)
                {
                    continue;
                }
                ++stats.candidatesS1;

                if (!EntityFeedSurfaceUsableForRigidRoute(surface, tri, material))
                {
                    continue;
                }

                const uint32_t baseMaterialId = SmokeMaterialId(material);
                const uint32_t materialId = SmokeRuntimeMaterialTableIdForEntitySurface(entity, surfaceIndex, material, baseMaterialId);
                if (registeredMaterialIds.insert(materialId).second)
                {
                    if (materialId == baseMaterialId)
                    {
                        RegisterSmokeMaterialTextureInfo(material);
                    }
                }
                const uint32_t materialClassSignature = SmokeMaterialRouteClassSignature(material, RtSmokeSurfaceClass::RigidEntity, RtSmokeTranslucentSubtype::Unknown);
                const uint32_t surfaceClassId = SmokeSurfaceClassId(RtSmokeSurfaceClass::RigidEntity);
                const uint32_t surfaceClassAndFlags = surfaceClassId |
                    (SmokeEntitySurfaceHasActiveEmissiveStage(viewDef, entity, material) ? 0u : RT_SMOKE_TRIANGLE_EMISSIVE_STAGE_OFF);
                RtPathTraceMeshKey meshKey;
                meshKey.tri = tri;
                meshKey.vertexBufferIdentity = static_cast<uintptr_t>(tri->ambientCache);
                meshKey.indexBufferIdentity = static_cast<uintptr_t>(tri->indexCache);
                meshKey.numVerts = tri->numVerts;
                meshKey.numIndexes = tri->numIndexes;
                meshKey.vertexFormat = static_cast<uint32_t>(RtSmokeGeometryBufferFormat::LegacySmokeVertex);
                meshKey.materialId = materialId;
                meshKey.materialClassSignature = materialClassSignature;
                meshKey.sourceKind = surfaceClassId;

                const PtRenderDefKey renderDefKey = PtGeometryLifecycle::MakeEntityKey(entity);
                const uint32_t modelEpoch = PtGeometryLifecycle::EntityModelEpoch(renderDefKey.world, renderDefKey.index);
                uint32_t sourceFlags = RT_PT_INSTANCE_SOURCE_RIGID | RT_PT_INSTANCE_SOURCE_ENTITY_FEED;
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
                if (!candidateInstanceIds.insert(rigidSnapshot.instanceId).second)
                {
                    continue;
                }

                const float distance = EntityFeedEntityDistance(entity, viewOrigin);
                if (maxDistance > 0.0f && distance > maxDistance)
                {
                    ++stats.droppedBudget;
                    continue;
                }

                EntityFeedRigidCandidate candidate;
                candidate.meshKey = meshKey;
                candidate.rigidSnapshot = rigidSnapshot;
                candidate.distance = distance;
                candidate.onScreen = entity->viewCount == tr.viewCount;
                candidate.emissive = EntityFeedMaterialIsEmissive(materialId);
                candidate.priority = EntityFeedCandidatePriority(
                    candidate.onScreen,
                    candidate.emissive,
                    EntityFeedProjectedSizeProxy(entity, distance),
                    distance);

                candidate.meshObservation.key = rigidSnapshot.meshKey;
                candidate.meshObservation.stableHash = rigidSnapshot.meshHash;
                candidate.meshObservation.baseMaterial = material;
                candidate.meshObservation.surfaceClassId = surfaceClassId;
                candidate.meshObservation.jointIndex = rigidSnapshot.jointIndex;
                candidate.meshObservation.materialName = material ? material->GetName() : "<none>";
                candidate.meshObservation.modelName = model ? model->Name() : "<none>";
                candidate.meshObservation.localSpaceValid = true;

                candidate.instanceObservation.meshHash = rigidSnapshot.meshHash;
                candidate.instanceObservation.entity = entity;
                candidate.instanceObservation.entityIndex = rigidSnapshot.entityIndex;
                candidate.instanceObservation.renderEntityNum = rigidSnapshot.renderEntityNum;
                candidate.instanceObservation.drawSurfIndex = -1;
                candidate.instanceObservation.modelSurfaceIndex = rigidSnapshot.modelSurfaceIndex;
                candidate.instanceObservation.jointIndex = rigidSnapshot.jointIndex;
                candidate.instanceObservation.currentArea = areaIndex;
                candidate.instanceObservation.renderDefKey = rigidSnapshot.renderDefKey;
                candidate.instanceObservation.modelEpoch = rigidSnapshot.modelEpoch;
                candidate.instanceObservation.materialOverrideId = rigidSnapshot.materialId;
                candidate.instanceObservation.surfaceClassId = surfaceClassId;
                candidate.instanceObservation.triangleClassAndFlags = surfaceClassAndFlags;
                candidate.instanceObservation.sourceFlags = rigidSnapshot.sourceFlags;
                memcpy(candidate.instanceObservation.objectToWorld, entity->modelMatrix, sizeof(candidate.instanceObservation.objectToWorld));
                candidate.instanceObservation.instanceId = rigidSnapshot.instanceId;
                candidate.instanceObservation.materialName = candidate.meshObservation.materialName;
                candidate.instanceObservation.modelName = candidate.meshObservation.modelName;

                candidate.candidateObservation.tri = tri;
                candidate.candidateObservation.meshHash = rigidSnapshot.meshHash;
                candidate.candidateObservation.instanceId = rigidSnapshot.instanceId;
                candidate.candidateObservation.vertexBufferIdentity = meshKey.vertexBufferIdentity;
                candidate.candidateObservation.indexBufferIdentity = meshKey.indexBufferIdentity;
                candidate.candidateObservation.sourceFlags = rigidSnapshot.sourceFlags;
                candidate.candidateObservation.materialId = rigidSnapshot.materialId;
                candidate.candidateObservation.materialClassSignature = rigidSnapshot.materialClassSignature;
                candidate.candidateObservation.surfaceClassId = surfaceClassId;
                candidate.candidateObservation.triangleClassAndFlags = surfaceClassAndFlags;
                candidate.candidateObservation.vertexFormat = meshKey.vertexFormat;
                candidate.candidateObservation.drawSurfIndex = -1;
                candidate.candidateObservation.entityIndex = entity->index;
                candidate.candidateObservation.renderEntityNum = renderEntity.entityNum;
                candidate.candidateObservation.modelEpoch = rigidSnapshot.modelEpoch;
                candidate.candidateObservation.jointIndex = rigidSnapshot.jointIndex;
                candidate.candidateObservation.numVerts = tri->numVerts;
                candidate.candidateObservation.numIndexes = tri->numIndexes;
                candidate.candidateObservation.localSpaceValid = candidate.meshObservation.localSpaceValid;
                candidate.candidateObservation.materialName = candidate.meshObservation.materialName;
                candidate.candidateObservation.modelName = candidate.meshObservation.modelName;
                candidates.push_back(candidate);
            }
        }
    }

    std::stable_sort(
        candidates.begin(),
        candidates.end(),
        [](const EntityFeedRigidCandidate& lhs, const EntityFeedRigidCandidate& rhs) {
            if (lhs.onScreen != rhs.onScreen)
            {
                return lhs.onScreen;
            }
            if (lhs.priority != rhs.priority)
            {
                return lhs.priority > rhs.priority;
            }
            if (lhs.distance != rhs.distance)
            {
                return lhs.distance < rhs.distance;
            }
            return lhs.rigidSnapshot.instanceId < rhs.rigidSnapshot.instanceId;
        });

    for (const EntityFeedRigidCandidate& candidate : candidates)
    {
        if (!candidate.onScreen && offscreenBudget <= 0)
        {
            ++stats.droppedBudget;
            continue;
        }

        RecordEntityFeedRigidCandidate(candidate, geometryUniverse, instanceUniverse);
        SceneUniverseAddDynamicMaterialEvalStatsForId(
            materialStats,
            viewDef,
            candidate.instanceObservation.entity,
            candidate.meshObservation.baseMaterial,
            static_cast<int>(candidate.meshKey.numIndexes),
            candidate.candidateObservation.materialId);
        ++stats.admitted;
        if (!candidate.onScreen)
        {
            --offscreenBudget;
        }
    }

    if (r_pathTracingEntityFeedDump.GetInteger() != 0)
    {
        DumpEntityFeedStats(stats);
    }
}
