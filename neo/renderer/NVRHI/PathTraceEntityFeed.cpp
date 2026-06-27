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

struct EntityFeedVisibleDrawSurfSet
{
    std::unordered_set<int> modelSurfaceIndexes;
    std::unordered_set<const srfTriangles_t*> geometries;
    std::unordered_set<const idMaterial*> materials;
};

using EntityFeedVisibleDrawSurfMap = std::unordered_map<const idRenderEntityLocal*, EntityFeedVisibleDrawSurfSet>;

EntityFeedVisibleDrawSurfMap BuildEntityFeedVisibleDrawSurfMap(const viewDef_t* viewDef)
{
    EntityFeedVisibleDrawSurfMap visibleDrawSurfs;
    if (!viewDef || !viewDef->drawSurfs)
    {
        return visibleDrawSurfs;
    }

    visibleDrawSurfs.reserve(viewDef->numDrawSurfs);
    for (int drawSurfIndex = 0; drawSurfIndex < viewDef->numDrawSurfs; ++drawSurfIndex)
    {
        const drawSurf_t* drawSurf = viewDef->drawSurfs[drawSurfIndex];
        const idRenderEntityLocal* entity =
            (drawSurf && drawSurf->space) ? drawSurf->space->entityDef : nullptr;
        if (!entity)
        {
            continue;
        }

        EntityFeedVisibleDrawSurfSet& visibleSet = visibleDrawSurfs[entity];
        if (drawSurf->modelSurfaceIndex >= 0)
        {
            visibleSet.modelSurfaceIndexes.insert(drawSurf->modelSurfaceIndex);
        }
        if (drawSurf->frontEndGeo)
        {
            visibleSet.geometries.insert(drawSurf->frontEndGeo);
        }
        if (drawSurf->material)
        {
            visibleSet.materials.insert(drawSurf->material);
        }
    }

    return visibleDrawSurfs;
}

bool EntityFeedSurfaceHasVisibleDrawSurf(
    const EntityFeedVisibleDrawSurfMap& visibleDrawSurfs,
    const idRenderEntityLocal* entity,
    int modelSurfaceIndex,
    const srfTriangles_t* tri,
    const idMaterial* material)
{
    if (!entity)
    {
        return false;
    }

    const auto visibleIt = visibleDrawSurfs.find(entity);
    if (visibleIt == visibleDrawSurfs.end())
    {
        return false;
    }

    const EntityFeedVisibleDrawSurfSet& visibleSet = visibleIt->second;
    if (modelSurfaceIndex >= 0 &&
        visibleSet.modelSurfaceIndexes.find(modelSurfaceIndex) != visibleSet.modelSurfaceIndexes.end())
    {
        return true;
    }
    if (tri && visibleSet.geometries.find(tri) != visibleSet.geometries.end())
    {
        return true;
    }
    if (material && visibleSet.materials.find(material) != visibleSet.materials.end())
    {
        return true;
    }

    return false;
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

struct EntityFeedCapturedRigidSurface
{
    RtPathTraceMeshKey meshKey;
    RtPathTraceRigidInstanceSnapshot rigidSnapshot;
    const idRenderEntityLocal* entity = nullptr;
    const idMaterial* baseMaterial = nullptr;
    uint32_t surfaceClassId = 0;
    uint32_t surfaceClassAndFlags = 0;
    int currentArea = -1;
    float objectToWorld[16] = {};
    float distance = 0.0f;
    float projectedSize = 0.0f;
    bool onScreen = false;
    bool emissive = false;
    idStr materialName;
    idStr modelName;
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

EntityFeedRigidCandidate BuildEntityFeedRigidCandidate(const EntityFeedCapturedRigidSurface& captured)
{
    EntityFeedRigidCandidate candidate;
    candidate.meshKey = captured.meshKey;
    candidate.rigidSnapshot = captured.rigidSnapshot;
    candidate.distance = captured.distance;
    candidate.onScreen = captured.onScreen;
    candidate.emissive = captured.emissive;
    candidate.priority = EntityFeedCandidatePriority(
        captured.onScreen,
        captured.emissive,
        captured.projectedSize,
        captured.distance);

    candidate.meshObservation.key = captured.rigidSnapshot.meshKey;
    candidate.meshObservation.stableHash = captured.rigidSnapshot.meshHash;
    candidate.meshObservation.baseMaterial = captured.baseMaterial;
    candidate.meshObservation.surfaceClassId = captured.surfaceClassId;
    candidate.meshObservation.jointIndex = captured.rigidSnapshot.jointIndex;
    candidate.meshObservation.materialName = captured.materialName;
    candidate.meshObservation.modelName = captured.modelName;
    candidate.meshObservation.localSpaceValid = true;

    candidate.instanceObservation.meshHash = captured.rigidSnapshot.meshHash;
    candidate.instanceObservation.entity = captured.entity;
    candidate.instanceObservation.entityIndex = captured.rigidSnapshot.entityIndex;
    candidate.instanceObservation.renderEntityNum = captured.rigidSnapshot.renderEntityNum;
    candidate.instanceObservation.drawSurfIndex = -1;
    candidate.instanceObservation.modelSurfaceIndex = captured.rigidSnapshot.modelSurfaceIndex;
    candidate.instanceObservation.jointIndex = captured.rigidSnapshot.jointIndex;
    candidate.instanceObservation.currentArea = captured.currentArea;
    candidate.instanceObservation.renderDefKey = captured.rigidSnapshot.renderDefKey;
    candidate.instanceObservation.modelEpoch = captured.rigidSnapshot.modelEpoch;
    candidate.instanceObservation.materialOverrideId = captured.rigidSnapshot.materialId;
    candidate.instanceObservation.surfaceClassId = captured.surfaceClassId;
    candidate.instanceObservation.triangleClassAndFlags = captured.surfaceClassAndFlags;
    candidate.instanceObservation.sourceFlags = captured.rigidSnapshot.sourceFlags;
    memcpy(candidate.instanceObservation.objectToWorld, captured.objectToWorld, sizeof(candidate.instanceObservation.objectToWorld));
    candidate.instanceObservation.instanceId = captured.rigidSnapshot.instanceId;
    candidate.instanceObservation.materialName = captured.materialName;
    candidate.instanceObservation.modelName = captured.modelName;

    candidate.candidateObservation.tri = captured.meshKey.tri;
    candidate.candidateObservation.meshHash = captured.rigidSnapshot.meshHash;
    candidate.candidateObservation.instanceId = captured.rigidSnapshot.instanceId;
    candidate.candidateObservation.vertexBufferIdentity = captured.meshKey.vertexBufferIdentity;
    candidate.candidateObservation.indexBufferIdentity = captured.meshKey.indexBufferIdentity;
    candidate.candidateObservation.sourceFlags = captured.rigidSnapshot.sourceFlags;
    candidate.candidateObservation.materialId = captured.rigidSnapshot.materialId;
    candidate.candidateObservation.materialClassSignature = captured.rigidSnapshot.materialClassSignature;
    candidate.candidateObservation.surfaceClassId = captured.surfaceClassId;
    candidate.candidateObservation.triangleClassAndFlags = captured.surfaceClassAndFlags;
    candidate.candidateObservation.vertexFormat = captured.meshKey.vertexFormat;
    candidate.candidateObservation.drawSurfIndex = -1;
    candidate.candidateObservation.entityIndex = captured.rigidSnapshot.entityIndex;
    candidate.candidateObservation.renderEntityNum = captured.rigidSnapshot.renderEntityNum;
    candidate.candidateObservation.modelEpoch = captured.rigidSnapshot.modelEpoch;
    candidate.candidateObservation.jointIndex = captured.rigidSnapshot.jointIndex;
    candidate.candidateObservation.numVerts = captured.meshKey.numVerts;
    candidate.candidateObservation.numIndexes = captured.meshKey.numIndexes;
    candidate.candidateObservation.localSpaceValid = candidate.meshObservation.localSpaceValid;
    candidate.candidateObservation.materialName = captured.materialName;
    candidate.candidateObservation.modelName = captured.modelName;
    return candidate;
}

std::vector<EntityFeedRigidCandidate> ProcessEntityFeedCapturedRigidSurfaces(
    const std::vector<EntityFeedCapturedRigidSurface>& capturedSurfaces,
    int rigidRouteMaxInstances)
{
    OPTICK_EVENT("PT EntityFeed Process Captured Candidates");

    std::vector<EntityFeedRigidCandidate> candidates;
    candidates.reserve(rigidRouteMaxInstances);

    for (const EntityFeedCapturedRigidSurface& captured : capturedSurfaces)
    {
        candidates.push_back(BuildEntityFeedRigidCandidate(captured));
    }

    {
        OPTICK_EVENT("PT EntityFeed Sort Candidates");
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
    }

    return candidates;
}

std::vector<EntityFeedCapturedRigidSurface> CaptureEntityFeedRigidSurfaces(
    const viewDef_t* viewDef,
    idRenderWorldLocal* renderWorld,
    const std::vector<bool>& reachableAreas,
    const EntityFeedVisibleDrawSurfMap& visibleDrawSurfs,
    const idVec3& viewOrigin,
    int rigidRouteMaxInstances,
    float maxDistance,
    RtPathTraceEntityFeedStats& stats)
{
    OPTICK_EVENT("PT EntityFeed Capture Candidates");

    std::vector<EntityFeedCapturedRigidSurface> capturedSurfaces;
    std::unordered_set<uint64> candidateInstanceIds;
    std::unordered_set<uint32_t> registeredMaterialIds;
    std::unordered_set<const idRenderEntityLocal*> scannedEntities;
    capturedSurfaces.reserve(rigidRouteMaxInstances);
    candidateInstanceIds.reserve(rigidRouteMaxInstances * 2);
    registeredMaterialIds.reserve(128);
    scannedEntities.reserve(renderWorld ? renderWorld->entityDefs.Num() : 0);

    if (!renderWorld)
    {
        return capturedSurfaces;
    }

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
            if (!scannedEntities.insert(entity).second)
            {
                continue;
            }

            const renderEntity_t& renderEntity = entity->parms;
            for (int surfaceIndex = 0; surfaceIndex < model->NumSurfaces(); ++surfaceIndex)
            {
                const modelSurface_t* surface = model->Surface(surfaceIndex);
                const srfTriangles_t* tri = surface ? surface->geometry : nullptr;
                const idMaterial* surfaceMaterial = surface ? surface->shader : nullptr;
                const idMaterial* material = nullptr;
                {
                    OPTICK_EVENT("PT EntityFeed Capture Material Remap");
                    material = R_RemapShaderBySkin(surfaceMaterial, renderEntity.customSkin, renderEntity.customShader);
                }

                RtPtFeedClass feedClass = RtPtFeedClass::Transient;
                {
                    OPTICK_EVENT("PT EntityFeed Capture Classify");
                    feedClass = ClassifyEntityFeedSurface(entity, model, surface);
                }

                bool promotedEmissiveCard = false;
                if (feedClass == RtPtFeedClass::Transient)
                {
                    OPTICK_EVENT("PT EntityFeed Capture Emissive Card Test");
                    promotedEmissiveCard = EntityFeedCanPromoteRigidEmissiveCard(entity, model, tri, material);
                }

                bool visibleDrawSurf = false;
                {
                    OPTICK_EVENT("PT EntityFeed Capture Visible Test");
                    visibleDrawSurf = EntityFeedSurfaceHasVisibleDrawSurf(visibleDrawSurfs, entity, surfaceIndex, tri, material);
                }
                if (promotedEmissiveCard && visibleDrawSurf)
                {
                    continue;
                }
                if (feedClass == RtPtFeedClass::RigidEntity && visibleDrawSurf)
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

                uint32_t baseMaterialId = 0;
                uint32_t materialId = 0;
                {
                    OPTICK_EVENT("PT EntityFeed Capture Material Id");
                    baseMaterialId = SmokeMaterialId(material);
                    materialId = SmokeRuntimeMaterialTableIdForEntitySurface(entity, surfaceIndex, material, baseMaterialId);
                }
                if (registeredMaterialIds.insert(materialId).second)
                {
                    if (materialId == baseMaterialId)
                    {
                        OPTICK_EVENT("PT EntityFeed Capture Register Material");
                        RegisterSmokeMaterialTextureInfo(material);
                    }
                }
                uint32_t materialClassSignature = 0;
                {
                    OPTICK_EVENT("PT EntityFeed Capture Material Class Signature");
                    materialClassSignature = SmokeMaterialRouteClassSignature(material, RtSmokeSurfaceClass::RigidEntity, RtSmokeTranslucentSubtype::Unknown);
                }
                const uint32_t surfaceClassId = SmokeSurfaceClassId(RtSmokeSurfaceClass::RigidEntity);
                bool activeEmissiveStage = false;
                {
                    OPTICK_EVENT("PT EntityFeed Capture Active Emissive Stage");
                    activeEmissiveStage = SmokeEntitySurfaceHasActiveEmissiveStage(viewDef, entity, material);
                }
                const uint32_t surfaceClassAndFlags = surfaceClassId |
                    (activeEmissiveStage ? 0u : RT_SMOKE_TRIANGLE_EMISSIVE_STAGE_OFF);
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

                RtPathTraceRigidInstanceSnapshot rigidSnapshot;
                {
                    OPTICK_EVENT("PT EntityFeed Capture Rigid Identity");
                    rigidSnapshot = BuildPathTraceRigidInstanceSnapshot(
                        meshKey,
                        model,
                        tri,
                        renderDefKey,
                        modelEpoch,
                        entity->index,
                        renderEntity.entityNum,
                        surfaceIndex,
                        sourceFlags);
                }
                if (!candidateInstanceIds.insert(rigidSnapshot.instanceId).second)
                {
                    continue;
                }

                float distance = 0.0f;
                {
                    OPTICK_EVENT("PT EntityFeed Capture Distance");
                    distance = EntityFeedEntityDistance(entity, viewOrigin);
                }
                if (maxDistance > 0.0f && distance > maxDistance)
                {
                    ++stats.droppedBudget;
                    continue;
                }

                {
                    OPTICK_EVENT("PT EntityFeed Capture Surface Snapshot");
                    EntityFeedCapturedRigidSurface captured;
                    captured.meshKey = meshKey;
                    captured.rigidSnapshot = rigidSnapshot;
                    captured.entity = entity;
                    captured.baseMaterial = material;
                    captured.surfaceClassId = surfaceClassId;
                    captured.surfaceClassAndFlags = surfaceClassAndFlags;
                    captured.currentArea = areaIndex;
                    memcpy(captured.objectToWorld, entity->modelMatrix, sizeof(captured.objectToWorld));
                    captured.distance = distance;
                    captured.projectedSize = EntityFeedProjectedSizeProxy(entity, distance);
                    captured.onScreen = entity->viewCount == tr.viewCount;
                    {
                        OPTICK_EVENT("PT EntityFeed Capture Material Facts");
                        captured.emissive = EntityFeedMaterialIsEmissive(materialId);
                    }
                    captured.materialName = material ? material->GetName() : "<none>";
                    captured.modelName = model ? model->Name() : "<none>";
                    capturedSurfaces.push_back(captured);
                }
            }
        }
    }

    return capturedSurfaces;
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
    OPTICK_EVENT("PT EntityFeed Produce");
    if (r_pathTracingEntityFeed.GetInteger() == 0)
    {
        return;
    }

    idRenderWorldLocal* renderWorld = viewDef ? viewDef->renderWorld : nullptr;
    if (!renderWorld)
    {
        return;
    }

    std::vector<bool> reachableAreas;
    {
        OPTICK_EVENT("PT EntityFeed Reachable Areas");
        reachableAreas = BuildEntityFeedReachableAreas(
            viewDef,
            r_pathTracingEntityFeedMaxDepth.GetInteger(),
            r_pathTracingEntityFeedMaxDistance.GetFloat());
    }
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

    EntityFeedVisibleDrawSurfMap visibleDrawSurfs;
    {
        OPTICK_EVENT("PT EntityFeed Visible DrawSurf Map");
        visibleDrawSurfs = BuildEntityFeedVisibleDrawSurfMap(viewDef);
    }

    const std::vector<EntityFeedCapturedRigidSurface> capturedSurfaces = CaptureEntityFeedRigidSurfaces(
        viewDef,
        renderWorld,
        reachableAreas,
        visibleDrawSurfs,
        viewOrigin,
        rigidRouteMaxInstances,
        maxDistance,
        stats);
    const std::vector<EntityFeedRigidCandidate> candidates = ProcessEntityFeedCapturedRigidSurfaces(
        capturedSurfaces,
        rigidRouteMaxInstances);

    {
        OPTICK_EVENT("PT EntityFeed Admit Candidates");
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
    }

    if (r_pathTracingEntityFeedDump.GetInteger() != 0)
    {
        DumpEntityFeedStats(stats);
    }
}
