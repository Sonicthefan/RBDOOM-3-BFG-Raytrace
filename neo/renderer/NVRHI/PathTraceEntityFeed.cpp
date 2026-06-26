#include "precompiled.h"
#pragma hdrstop

#include "PathTraceCVars.h"
#include "PathTraceEntityFeed.h"
#include "../RenderCommon.h"
#include "../RenderWorld_local.h"

#include <unordered_map>

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
