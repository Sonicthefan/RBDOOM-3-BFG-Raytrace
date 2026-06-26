#include "precompiled.h"
#pragma hdrstop

#include "PathTraceCVars.h"
#include "PathTraceEntityFeed.h"
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

bool IsSingleBoneSurface(const srfTriangles_t* tri)
{
    if (!tri || !tri->verts || tri->numVerts <= 0)
    {
        return false;
    }

    int surfaceJoint = -1;
    for (int vertIndex = 0; vertIndex < tri->numVerts; ++vertIndex)
    {
        const idDrawVert& vert = tri->verts[vertIndex];
        int weightedComponent = -1;
        for (int component = 0; component < 4; ++component)
        {
            if (vert.color2[component] == 0)
            {
                continue;
            }
            if (vert.color2[component] != 255 || weightedComponent >= 0)
            {
                return false;
            }
            weightedComponent = component;
        }

        if (weightedComponent < 0)
        {
            return false;
        }

        const int jointIndex = vert.color[weightedComponent];
        if (surfaceJoint < 0)
        {
            surfaceJoint = jointIndex;
        }
        else if (surfaceJoint != jointIndex)
        {
            return false;
        }
    }

    return true;
}

unsigned int HashEntityFeedJoints(const renderEntity_t& renderEntity)
{
    if (!renderEntity.joints || renderEntity.numJoints <= 0)
    {
        return 0;
    }

    return MD5_BlockChecksum(renderEntity.joints, renderEntity.numJoints * sizeof(idJointMat));
}

bool EntityFeedRigidEntityEligible(const idRenderEntityLocal* entity, const idRenderModel* model)
{
    const renderEntity_t* renderEntity = entity ? &entity->parms : nullptr;
    if (!entity || !renderEntity || !model || model->IsStaticWorldModel())
    {
        return false;
    }
    if (model->IsDynamicModel() != DM_STATIC)
    {
        return false;
    }
    if (renderEntity->joints != nullptr || renderEntity->numJoints > 0)
    {
        return false;
    }
    if (renderEntity->callback != nullptr || renderEntity->forceUpdate != 0)
    {
        return false;
    }
    if (renderEntity->weaponDepthHack || renderEntity->modelDepthHack != 0.0f)
    {
        return false;
    }
    return true;
}

void AccumulateEntityFeedSurfaceClass(
    RtPathTraceEntityFeedStats& stats,
    const idRenderEntityLocal* entity,
    const idRenderModel* sourceModel,
    const srfTriangles_t* tri)
{
    if (!sourceModel)
    {
        ++stats.candidatesS4;
        return;
    }

    if (sourceModel->IsStaticWorldModel())
    {
        ++stats.candidatesS0;
        return;
    }

    if (EntityFeedRigidEntityEligible(entity, sourceModel))
    {
        ++stats.candidatesS1;
        return;
    }

    if (EntityFeedModelHasJointData(entity, sourceModel))
    {
        if (IsSingleBoneSurface(tri))
        {
            ++stats.candidatesS2;
        }
        else
        {
            ++stats.candidatesS3;
        }
        return;
    }

    ++stats.candidatesS4;
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
                IsSingleBoneSurface(tri) ? 1 : 0);
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
                AccumulateEntityFeedSurfaceClass(stats, entity, sourceModel, nullptr);
            }
            for (int surfaceIndex = 0; surfaceIndex < surfaceCount; ++surfaceIndex)
            {
                const modelSurface_t* surface = diagnosticModel->Surface(surfaceIndex);
                const srfTriangles_t* tri = surface ? surface->geometry : nullptr;
                AccumulateEntityFeedSurfaceClass(stats, entity, sourceModel, tri);
            }

            if (temporaryModel && temporaryModel != sourceModel)
            {
                delete temporaryModel;
            }
        }
    }

    DumpEntityFeedStats(stats);
}
