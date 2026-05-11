#include "precompiled.h"
#pragma hdrstop

#include "PathTraceAcceleration.h"
#include "PathTraceCVars.h"
#include "PathTraceDrawSurfCapture.h"
#include "PathTraceDoomMaterialClassifier.h"
#include "PathTraceDynamicMaterialState.h"
#include "PathTraceGeometryUniverse.h"
#include "PathTraceGuiSurfaces.h"
#include "PathTraceSceneCapture.h"
#include "PathTraceSceneUniverse.h"
#include "PathTraceSkinning.h"
#include "PathTraceSurfaceClassification.h"
#include "../RenderCommon.h"

#include <algorithm>
#include <unordered_set>

namespace {

uint64 PtHashBytes(uint64 hash, const void* data, size_t size)
{
    return HashSmokeBytes(hash, data, size);
}

uint64 PtMeshKeyHash(const RtPathTraceMeshKey& key)
{
    uint64 hash = 14695981039346656037ull;
    const uintptr_t triPtr = reinterpret_cast<uintptr_t>(key.tri);
    hash = PtHashBytes(hash, &triPtr, sizeof(triPtr));
    hash = PtHashBytes(hash, &key.vertexBufferIdentity, sizeof(key.vertexBufferIdentity));
    hash = PtHashBytes(hash, &key.indexBufferIdentity, sizeof(key.indexBufferIdentity));
    hash = PtHashBytes(hash, &key.numVerts, sizeof(key.numVerts));
    hash = PtHashBytes(hash, &key.numIndexes, sizeof(key.numIndexes));
    hash = PtHashBytes(hash, &key.vertexFormat, sizeof(key.vertexFormat));
    hash = PtHashBytes(hash, &key.materialId, sizeof(key.materialId));
    hash = PtHashBytes(hash, &key.sourceKind, sizeof(key.sourceKind));
    return hash;
}

uint64 PtInstanceIdHash(uint64 meshHash, int entityIndex, int renderEntityNum, uint32_t materialId, const srfTriangles_t* tri)
{
    uint64 hash = 14695981039346656037ull;
    const uintptr_t triPtr = reinterpret_cast<uintptr_t>(tri);
    hash = PtHashBytes(hash, &meshHash, sizeof(meshHash));
    hash = PtHashBytes(hash, &entityIndex, sizeof(entityIndex));
    hash = PtHashBytes(hash, &renderEntityNum, sizeof(renderEntityNum));
    hash = PtHashBytes(hash, &materialId, sizeof(materialId));
    hash = PtHashBytes(hash, &triPtr, sizeof(triPtr));
    return hash;
}

void CopyDrawSurfObjectToWorld(const drawSurf_t* drawSurf, float objectToWorld[16])
{
    if (drawSurf && drawSurf->space)
    {
        memcpy(objectToWorld, drawSurf->space->modelMatrix, sizeof(float) * 16);
        return;
    }

    memset(objectToWorld, 0, sizeof(float) * 16);
    objectToWorld[0] = 1.0f;
    objectToWorld[5] = 1.0f;
    objectToWorld[10] = 1.0f;
    objectToWorld[15] = 1.0f;
}

uint32_t PtSourceFlagsForDrawSurf(const viewDef_t* viewDef, const drawSurf_t* drawSurf, const srfTriangles_t* tri, RtSmokeSurfaceClass surfaceClass)
{
    uint32_t flags = 0;
    switch (surfaceClass)
    {
        case RtSmokeSurfaceClass::StaticWorld:
            flags |= RT_PT_INSTANCE_SOURCE_STATIC_WORLD;
            break;
        case RtSmokeSurfaceClass::RigidEntity:
            flags |= RT_PT_INSTANCE_SOURCE_RIGID;
            break;
        case RtSmokeSurfaceClass::SkinnedDeformed:
            flags |= RT_PT_INSTANCE_SOURCE_SKINNED_OR_DEFORMING;
            break;
        case RtSmokeSurfaceClass::ParticleAlpha:
            flags |= RT_PT_INSTANCE_SOURCE_PARTICLE_OR_TRANSIENT;
            break;
        default:
            break;
    }

    const viewEntity_t* space = drawSurf ? drawSurf->space : nullptr;
    const idRenderEntityLocal* entity = space ? space->entityDef : nullptr;
    const renderEntity_t* renderEntity = entity ? &entity->parms : nullptr;
    const idMaterial* material = drawSurf ? drawSurf->material : nullptr;
    const idRenderModel* model = renderEntity ? renderEntity->hModel : nullptr;

    if (IsSmokeGuiDrawSurface(drawSurf))
    {
        flags |= RT_PT_INSTANCE_SOURCE_GUI;
    }
    if (surfaceClass == RtSmokeSurfaceClass::ParticleAlpha)
    {
        flags |= RT_PT_INSTANCE_SOURCE_PARTICLE_OR_TRANSIENT;
    }
    if ((drawSurf && drawSurf->jointCache != 0) ||
        (tri && tri->staticModelWithJoints != nullptr) ||
        (renderEntity && renderEntity->joints != nullptr && renderEntity->numJoints > 0) ||
        (model && model->IsDynamicModel() != DM_STATIC))
    {
        flags |= RT_PT_INSTANCE_SOURCE_SKINNED_OR_DEFORMING;
    }
    if ((renderEntity && (renderEntity->callback != nullptr || renderEntity->forceUpdate != 0)) ||
        (entity && (entity->dynamicModel != nullptr || entity->cachedDynamicModel != nullptr)) ||
        (material && material->Deform() != DFRM_NONE))
    {
        flags |= RT_PT_INSTANCE_SOURCE_CALLBACK_OR_GENERATED;
    }
    if (renderEntity && (renderEntity->customShader != nullptr || renderEntity->customSkin != nullptr))
    {
        flags |= RT_PT_INSTANCE_SOURCE_MATERIAL_OVERRIDE;
    }
    return flags;
}

bool PtMirrorCanPromoteRigidEmissiveCard(const drawSurf_t* drawSurf, const srfTriangles_t* tri, RtSmokeSurfaceClass surfaceClass)
{
    if (r_pathTracingRigidRouteEmissiveCards.GetInteger() == 0 ||
        surfaceClass != RtSmokeSurfaceClass::ParticleAlpha ||
        !drawSurf ||
        !tri ||
        !drawSurf->space ||
        !drawSurf->material)
    {
        return false;
    }

    const viewEntity_t* space = drawSurf->space;
    const idRenderEntityLocal* entity = space->entityDef;
    const renderEntity_t* renderEntity = entity ? &entity->parms : nullptr;
    const idRenderModel* model = renderEntity ? renderEntity->hModel : nullptr;
    const idMaterial* material = drawSurf->material;
    const char* materialName = material->GetName();

    if (!entity ||
        IsSmokeGuiDrawSurface(drawSurf) ||
        drawSurf->jointCache != 0 ||
        tri->staticModelWithJoints != nullptr ||
        (renderEntity && renderEntity->joints != nullptr && renderEntity->numJoints > 0) ||
        (model && model->IsDynamicModel() != DM_STATIC) ||
        (renderEntity && (renderEntity->callback != nullptr || renderEntity->forceUpdate != 0)) ||
        (entity->dynamicModel != nullptr || entity->cachedDynamicModel != nullptr) ||
        material->Deform() != DFRM_NONE ||
        space->modelDepthHack != 0.0f)
    {
        return false;
    }

    if (materialName && idStr::FindText(materialName, "swinglight", false) >= 0)
    {
        return false;
    }

    const RtSmokeTranslucentClassifierInfo classifier = BuildSmokeTranslucentClassifierInfo(material);
    if (classifier.hasScreenTexgen ||
        classifier.hasAddDefault0200Texture ||
        classifier.nameLooksGui ||
        classifier.nameLooksParticle ||
        classifier.nameLooksGlass ||
        classifier.sortIsPostProcess ||
        classifier.sortIsGuiOrSubview)
    {
        return false;
    }

    const bool looksLikeRigidEmissiveCard =
        classifier.nameLooksSignage ||
        classifier.nameLooksGlow;
    const bool hasEmissiveCardStage =
        classifier.hasAdditiveBlend ||
        classifier.hasAmbientBlendStage ||
        (classifier.hasAmbientStage && !classifier.hasDiffuseStage);

    return looksLikeRigidEmissiveCard &&
        hasEmissiveCardStage &&
        (material->Coverage() == MC_TRANSLUCENT || classifier.hasAdditiveBlend || classifier.hasAmbientBlendStage);
}

RtSmokeSurfaceClass PtMirrorEffectiveSurfaceClass(const drawSurf_t* drawSurf, const srfTriangles_t* tri, RtSmokeSurfaceClass surfaceClass)
{
    return PtMirrorCanPromoteRigidEmissiveCard(drawSurf, tri, surfaceClass)
        ? RtSmokeSurfaceClass::RigidEntity
        : surfaceClass;
}

void BuildSceneUniverseLegacyKeySet(const RtPathTraceSceneUniverse* sceneUniverse, std::unordered_set<uint64>& keys)
{
    keys.clear();
    if (!sceneUniverse || !sceneUniverse->GetStats().valid)
    {
        return;
    }

    const std::vector<RtPathTraceSceneUniverseSurface>& surfaces = sceneUniverse->Surfaces();
    keys.reserve(surfaces.size());
    for (const RtPathTraceSceneUniverseSurface& surface : surfaces)
    {
        if (surface.legacyDrawSurfKey != 0)
        {
            keys.insert(surface.legacyDrawSurfKey);
        }
    }
}

void AddMirrorMaterialStats(RtSmokeMaterialStats& stats, const idMaterial* material, int indexes, RtSmokeSurfaceClass surfaceClass, RtSmokeTranslucentSubtype translucentSubtype)
{
    const char* materialName = material ? material->GetName() : "<none>";
    const uint32_t materialId = HashSmokeMaterialName(materialName);
    ++stats.totalSurfaces;
    stats.totalTriangles += indexes / 3;

    const bool firstMaterial = std::find(stats.materialIds.begin(), stats.materialIds.end(), materialId) == stats.materialIds.end();
    if (firstMaterial)
    {
        stats.materialIds.push_back(materialId);
        ++stats.uniqueMaterials;
    }

    bool materialSampleFound = false;
    for (int sampleIndex = 0; sampleIndex < stats.sampleCount; ++sampleIndex)
    {
        RtSmokeMaterialSample& sample = stats.samples[sampleIndex];
        if (sample.id == materialId)
        {
            ++sample.surfaces;
            sample.triangles += indexes / 3;
            materialSampleFound = true;
            break;
        }
    }
    if (!materialSampleFound && stats.sampleCount < RT_SMOKE_MATERIAL_REASON_SAMPLES)
    {
        RtSmokeMaterialSample& sample = stats.samples[stats.sampleCount++];
        sample.id = materialId;
        sample.surfaces = 1;
        sample.triangles = indexes / 3;
        sample.name = materialName;
    }

    if (surfaceClass != RtSmokeSurfaceClass::ParticleAlpha)
    {
        return;
    }

    ++stats.translucentSurfaces;
    stats.translucentTriangles += indexes / 3;
    const bool firstTranslucentMaterial = std::find(stats.translucentMaterialIds.begin(), stats.translucentMaterialIds.end(), materialId) == stats.translucentMaterialIds.end();
    if (firstTranslucentMaterial)
    {
        stats.translucentMaterialIds.push_back(materialId);
        ++stats.translucentUniqueMaterials;
    }

    const int subtypeIndex = idMath::ClampInt(0, RT_SMOKE_TRANSLUCENT_SUBTYPE_COUNT - 1, static_cast<int>(SmokeTranslucentSubtypeId(translucentSubtype)));
    ++stats.translucentSubtypeSurfaces[subtypeIndex];
    stats.translucentSubtypeTriangles[subtypeIndex] += indexes / 3;
}

void AddMirrorSurfaceClassStats(RtSmokeSurfaceClassStats& stats, RtSmokeSurfaceClass surfaceClass, int verts, int indexes)
{
    const int triangles = indexes / 3;
    switch (surfaceClass)
    {
        case RtSmokeSurfaceClass::StaticWorld:
            ++stats.staticWorldSurfaces;
            stats.staticWorldVerts += verts;
            stats.staticWorldIndexes += indexes;
            stats.staticWorldTriangles += triangles;
            break;
        case RtSmokeSurfaceClass::RigidEntity:
            ++stats.rigidEntitySurfaces;
            stats.rigidEntityVerts += verts;
            stats.rigidEntityIndexes += indexes;
            stats.rigidEntityTriangles += triangles;
            break;
        case RtSmokeSurfaceClass::SkinnedDeformed:
            ++stats.skinnedDeformedSurfaces;
            stats.skinnedDeformedVerts += verts;
            stats.skinnedDeformedIndexes += indexes;
            stats.skinnedDeformedTriangles += triangles;
            break;
        case RtSmokeSurfaceClass::ParticleAlpha:
            ++stats.particleAlphaSurfaces;
            stats.particleAlphaVerts += verts;
            stats.particleAlphaIndexes += indexes;
            stats.particleAlphaTriangles += triangles;
            break;
        default:
            ++stats.unknownSurfaces;
            stats.unknownVerts += verts;
            stats.unknownIndexes += indexes;
            stats.unknownTriangles += triangles;
            break;
    }
}

void AddMirrorDynamicGeometryStats(RtSmokeDynamicGeometryStats& stats, RtSmokeSurfaceClass surfaceClass, const drawSurf_t* drawSurf, const srfTriangles_t* tri, int indexes)
{
    switch (surfaceClass)
    {
        case RtSmokeSurfaceClass::RigidEntity:
            ++stats.rigidSurfaces;
            stats.rigidIndexes += indexes;
            break;
        case RtSmokeSurfaceClass::SkinnedDeformed:
            if (GetSmokeRtCpuSkinningJoints(tri) != nullptr)
            {
                ++stats.skinnedRtCpuSkinnedSurfaces;
                stats.skinnedRtCpuSkinnedIndexes += indexes;
            }
            else if (SmokeSkinnedSurfaceLikelyBasePose(drawSurf, tri))
            {
                ++stats.skinnedLikelyBasePoseSurfaces;
                stats.skinnedLikelyBasePoseIndexes += indexes;
            }
            else
            {
                ++stats.skinnedCpuCurrentSurfaces;
                stats.skinnedCpuCurrentIndexes += indexes;
            }
            break;
        case RtSmokeSurfaceClass::ParticleAlpha:
            ++stats.particleAlphaSurfaces;
            stats.particleAlphaIndexes += indexes;
            break;
        case RtSmokeSurfaceClass::Unknown:
            ++stats.unknownSurfaces;
            stats.unknownIndexes += indexes;
            break;
        default:
            break;
    }
}

bool DrawSurfMirrorIsStaticMatch(const RtSmokeGeometryUniverse* geometryUniverse, const std::unordered_set<uint64>& sceneUniverseLegacyKeys, uint64 legacyStaticKey)
{
    if (!sceneUniverseLegacyKeys.empty() && sceneUniverseLegacyKeys.find(legacyStaticKey) != sceneUniverseLegacyKeys.end())
    {
        return true;
    }
    return geometryUniverse && geometryUniverse->HasStaticSurface(legacyStaticKey);
}

bool PtMirrorIsEligibleRigidCandidate(
    const RtPathTraceMeshObservation& meshObservation,
    const RtPathTraceInstanceObservation& instanceObservation)
{
    if ((instanceObservation.sourceFlags & RT_PT_INSTANCE_SOURCE_RIGID) == 0)
    {
        return false;
    }
    if (meshObservation.key.tri == nullptr ||
        meshObservation.key.numVerts <= 0 ||
        meshObservation.key.numIndexes <= 0 ||
        (meshObservation.key.numIndexes % 3) != 0 ||
        meshObservation.stableHash == 0 ||
        meshObservation.key.materialId == 0 ||
        !meshObservation.localSpaceValid)
    {
        return false;
    }
    const uint32_t rejectMask =
        RT_PT_INSTANCE_SOURCE_STATIC_WORLD |
        RT_PT_INSTANCE_SOURCE_SKINNED_OR_DEFORMING |
        RT_PT_INSTANCE_SOURCE_PARTICLE_OR_TRANSIENT |
        RT_PT_INSTANCE_SOURCE_GUI |
        RT_PT_INSTANCE_SOURCE_CALLBACK_OR_GENERATED |
        RT_PT_INSTANCE_SOURCE_STATIC_CACHE_MATCH;
    return (instanceObservation.sourceFlags & rejectMask) == 0;
}

idVec4 PtMirrorBoundsColor(
    RtSmokeSurfaceClass surfaceClass,
    const RtPathTraceMeshObservation& meshObservation,
    const RtPathTraceInstanceObservation& instanceObservation)
{
    if ((instanceObservation.sourceFlags & RT_PT_INSTANCE_SOURCE_STATIC_CACHE_MATCH) != 0 ||
        (instanceObservation.sourceFlags & RT_PT_INSTANCE_SOURCE_STATIC_WORLD) != 0 ||
        surfaceClass == RtSmokeSurfaceClass::StaticWorld)
    {
        return colorGreen;
    }
    if (PtMirrorIsEligibleRigidCandidate(meshObservation, instanceObservation))
    {
        return colorDodgerBlue;
    }
    if ((instanceObservation.sourceFlags & RT_PT_INSTANCE_SOURCE_RIGID) != 0)
    {
        return colorRed;
    }
    if ((instanceObservation.sourceFlags & RT_PT_INSTANCE_SOURCE_SKINNED_OR_DEFORMING) != 0 ||
        surfaceClass == RtSmokeSurfaceClass::SkinnedDeformed)
    {
        return colorMagenta;
    }
    if ((instanceObservation.sourceFlags & RT_PT_INSTANCE_SOURCE_PARTICLE_OR_TRANSIENT) != 0 ||
        surfaceClass == RtSmokeSurfaceClass::ParticleAlpha)
    {
        return colorGray;
    }
    if ((instanceObservation.sourceFlags & RT_PT_INSTANCE_SOURCE_GUI) != 0)
    {
        return colorOrange;
    }
    return colorYellow;
}

void PtMirrorBoundsPointToWorld(const drawSurf_t* drawSurf, const idVec3& localPoint, idVec3& worldPoint)
{
    if (drawSurf && drawSurf->space)
    {
        R_LocalPointToGlobal(drawSurf->space->modelMatrix, localPoint, worldPoint);
        return;
    }
    worldPoint = localPoint;
}

int PtMirrorResolveDrawSurfArea(const viewDef_t* viewDef, const drawSurf_t* drawSurf, const srfTriangles_t* tri)
{
    idRenderWorldLocal* renderWorld = viewDef ? viewDef->renderWorld : nullptr;
    if (!renderWorld || !drawSurf || !tri)
    {
        return -1;
    }

    idVec3 worldCenter;
    PtMirrorBoundsPointToWorld(drawSurf, tri->bounds.GetCenter(), worldCenter);
    return renderWorld->PointInArea(worldCenter);
}

void PtMirrorAppendBoundsOverlayLines(
    const drawSurf_t* drawSurf,
    const srfTriangles_t* tri,
    const idVec4& color,
    std::vector<RtPathTraceBoundsOverlayLine>& lines)
{
    if (!tri || tri->bounds.IsCleared() || lines.size() + 12 > RT_PT_BOUNDS_OVERLAY_MAX_LINES)
    {
        return;
    }

    idVec3 corners[8];
    for (int cornerIndex = 0; cornerIndex < 8; ++cornerIndex)
    {
        idVec3 localPoint;
        localPoint.x = tri->bounds[(cornerIndex ^ (cornerIndex >> 1)) & 1].x;
        localPoint.y = tri->bounds[(cornerIndex >> 1) & 1].y;
        localPoint.z = tri->bounds[(cornerIndex >> 2) & 1].z;
        PtMirrorBoundsPointToWorld(drawSurf, localPoint, corners[cornerIndex]);
    }

    for (int edgeIndex = 0; edgeIndex < 4; ++edgeIndex)
    {
        const int edgeStarts[3] = { edgeIndex, 4 + edgeIndex, edgeIndex };
        const int edgeEnds[3] = { (edgeIndex + 1) & 3, 4 + ((edgeIndex + 1) & 3), 4 + edgeIndex };
        for (int edgePart = 0; edgePart < 3; ++edgePart)
        {
            RtPathTraceBoundsOverlayLine line;
            line.startAndPad.Set(corners[edgeStarts[edgePart]].x, corners[edgeStarts[edgePart]].y, corners[edgeStarts[edgePart]].z, 0.0f);
            line.endAndPad.Set(corners[edgeEnds[edgePart]].x, corners[edgeEnds[edgePart]].y, corners[edgeEnds[edgePart]].z, 0.0f);
            line.color = color;
            lines.push_back(line);
        }
    }
}

}

void CapturePathTraceDrawSurfMirror(
    const viewDef_t* viewDef,
    const RtPathTraceSceneUniverse* sceneUniverse,
    RtSmokeGeometryUniverse* geometryUniverse,
    RtPathTraceInstanceUniverse& instanceUniverse,
    std::vector<RtPathTraceBoundsOverlayLine>* boundsOverlayLines)
{
    if (!viewDef || !viewDef->drawSurfs)
    {
        instanceUniverse.EndFrame();
        return;
    }

    std::unordered_set<uint64> sceneUniverseLegacyKeys;
    BuildSceneUniverseLegacyKeySet(sceneUniverse, sceneUniverseLegacyKeys);

    instanceUniverse.SetObservedDrawSurfCount(viewDef->numDrawSurfs);
    const int boundsOverlayMode = (r_pathTracingDebugMode.GetInteger() == 21 || r_pathTracingDebugMode.GetInteger() == 22)
        ? Max(1, r_pathTracingSceneBoundsOverlay.GetInteger())
        : r_pathTracingSceneBoundsOverlay.GetInteger();
    const int boundsOverlayMax = Max(0, r_pathTracingSceneBoundsOverlayMax.GetInteger());
    int boundsOverlayDrawn = 0;

    for (int surfaceIndex = 0; surfaceIndex < viewDef->numDrawSurfs; ++surfaceIndex)
    {
        const drawSurf_t* drawSurf = viewDef->drawSurfs[surfaceIndex];
        const srfTriangles_t* tri = nullptr;
        RtSmokeSurfaceSkipStats skipStats;
        if (!ValidateSmokeDrawSurface(viewDef, drawSurf, tri, &skipStats))
        {
            instanceUniverse.RecordSkippedDrawSurf(skipStats);
            continue;
        }

        const RtSmokeSurfaceClass classifiedSurfaceClass = ClassifySmokeSurface(viewDef, drawSurf, tri);
        const RtSmokeSurfaceClass surfaceClass = PtMirrorEffectiveSurfaceClass(drawSurf, tri, classifiedSurfaceClass);
        const idMaterial* material = drawSurf ? drawSurf->material : nullptr;
        const viewEntity_t* space = drawSurf ? drawSurf->space : nullptr;
        const idRenderEntityLocal* entity = space ? space->entityDef : nullptr;
        const renderEntity_t* renderEntity = entity ? &entity->parms : nullptr;
        const idRenderModel* model = renderEntity ? renderEntity->hModel : nullptr;
        const uint32_t materialId = SmokeMaterialId(material);
        const uint32_t sourceKind = SmokeSurfaceClassId(surfaceClass);
        const uint64 legacyStaticKey = BuildSmokeStaticSurfaceKeyForDiagnostics(drawSurf, tri);

        RtPathTraceMeshObservation meshObservation;
        meshObservation.key.tri = tri;
        meshObservation.key.vertexBufferIdentity = static_cast<uintptr_t>(tri ? tri->ambientCache : 0);
        meshObservation.key.indexBufferIdentity = static_cast<uintptr_t>(tri ? tri->indexCache : 0);
        meshObservation.key.numVerts = tri ? tri->numVerts : 0;
        meshObservation.key.numIndexes = tri ? tri->numIndexes : 0;
        meshObservation.key.vertexFormat = static_cast<uint32_t>(RtSmokeGeometryBufferFormat::LegacySmokeVertex);
        meshObservation.key.materialId = materialId;
        meshObservation.key.sourceKind = sourceKind;
        meshObservation.stableHash = PtMeshKeyHash(meshObservation.key);
        meshObservation.baseMaterial = material;
        meshObservation.materialName = material ? material->GetName() : "<none>";
        meshObservation.modelName = model ? model->Name() : "<none>";
        meshObservation.localSpaceValid = true;

        RtPathTraceInstanceObservation instanceObservation;
        instanceObservation.meshHash = meshObservation.stableHash;
        instanceObservation.entity = entity;
        instanceObservation.entityIndex = entity ? entity->index : -1;
        instanceObservation.renderEntityNum = renderEntity ? renderEntity->entityNum : -1;
        instanceObservation.drawSurfIndex = surfaceIndex;
        instanceObservation.currentArea = PtMirrorResolveDrawSurfArea(viewDef, drawSurf, tri);
        instanceObservation.materialOverrideId = materialId;
        instanceObservation.sourceFlags = PtSourceFlagsForDrawSurf(viewDef, drawSurf, tri, surfaceClass);
        if (!sceneUniverseLegacyKeys.empty() && sceneUniverseLegacyKeys.find(legacyStaticKey) != sceneUniverseLegacyKeys.end())
        {
            instanceObservation.sourceFlags |= RT_PT_INSTANCE_SOURCE_STATIC_UNIVERSE_MATCH;
        }
        if (geometryUniverse && geometryUniverse->HasStaticSurface(legacyStaticKey))
        {
            instanceObservation.sourceFlags |= RT_PT_INSTANCE_SOURCE_STATIC_CACHE_MATCH;
        }
        CopyDrawSurfObjectToWorld(drawSurf, instanceObservation.objectToWorld);
        instanceObservation.instanceId = PtInstanceIdHash(meshObservation.stableHash, instanceObservation.entityIndex, instanceObservation.renderEntityNum, materialId, tri);
        instanceObservation.materialName = meshObservation.materialName;
        instanceObservation.modelName = meshObservation.modelName;

        instanceUniverse.RecordObservation(meshObservation, instanceObservation, surfaceClass, tri->numVerts, tri->numIndexes);
        if ((boundsOverlayMode == 1 || boundsOverlayMode == 2) && boundsOverlayDrawn < boundsOverlayMax)
        {
            const bool eligibleRigid = PtMirrorIsEligibleRigidCandidate(meshObservation, instanceObservation);
            if (boundsOverlayLines && (boundsOverlayMode >= 2 || eligibleRigid))
            {
                PtMirrorAppendBoundsOverlayLines(drawSurf, tri, PtMirrorBoundsColor(surfaceClass, meshObservation, instanceObservation), *boundsOverlayLines);
                ++boundsOverlayDrawn;
            }
        }
        if (geometryUniverse)
        {
            RtPathTraceRigidMeshCandidateObservation candidateObservation;
            candidateObservation.tri = tri;
            candidateObservation.meshHash = meshObservation.stableHash;
            candidateObservation.instanceId = instanceObservation.instanceId;
            candidateObservation.vertexBufferIdentity = meshObservation.key.vertexBufferIdentity;
            candidateObservation.indexBufferIdentity = meshObservation.key.indexBufferIdentity;
            candidateObservation.sourceFlags = instanceObservation.sourceFlags;
            candidateObservation.materialId = materialId;
            candidateObservation.surfaceClassId = SmokeSurfaceClassId(surfaceClass);
            candidateObservation.vertexFormat = meshObservation.key.vertexFormat;
            candidateObservation.drawSurfIndex = surfaceIndex;
            candidateObservation.entityIndex = instanceObservation.entityIndex;
            candidateObservation.renderEntityNum = instanceObservation.renderEntityNum;
            candidateObservation.numVerts = tri->numVerts;
            candidateObservation.numIndexes = tri->numIndexes;
            candidateObservation.localSpaceValid = meshObservation.localSpaceValid;
            candidateObservation.materialName = meshObservation.materialName;
            candidateObservation.modelName = meshObservation.modelName;
            geometryUniverse->RecordRigidMeshCandidate(candidateObservation);
        }
    }

    instanceUniverse.EndFrame();
}

bool CapturePathTraceDynamicFrameFromDrawSurfMirror(
    const viewDef_t* viewDef,
    const RtPathTraceSceneUniverse* sceneUniverse,
    const RtSmokeGeometryUniverse* geometryUniverse,
    std::vector<PathTraceSmokeVertex>& vertexData,
    std::vector<uint32_t>& indexData,
    std::vector<uint32_t>& triangleClassData,
    std::vector<uint32_t>& triangleMaterialData,
    int& sourceSurfaces,
    int& sourceVerts,
    int& sourceIndexes,
    RtSmokeSurfaceClassStats& classStats,
    RtSmokeSurfaceSkipStats& skipStats,
    RtSmokeDynamicGeometryStats& dynamicStats,
    RtSmokeAttributeStats& attributeStats,
    RtSmokeMaterialStats& materialStats,
    RtSmokeBucketRanges& bucketRanges,
    RtSmokeSceneCaptureTiming& captureTiming,
    RtSmokeSurfaceClassReasonSamples* reasonSamples,
    std::vector<RtSmokeSkinnedSurfaceRecord>* skinnedSurfaceRecords)
{
    OPTICK_EVENT("PT Capture Dynamic Frame From DrawSurf Mirror");

    sourceSurfaces = 0;
    sourceVerts = 0;
    sourceIndexes = 0;
    classStats = RtSmokeSurfaceClassStats();
    skipStats = RtSmokeSurfaceSkipStats();
    dynamicStats = RtSmokeDynamicGeometryStats();
    attributeStats = RtSmokeAttributeStats();
    materialStats = RtSmokeMaterialStats();
    bucketRanges = RtSmokeBucketRanges();
    captureTiming = RtSmokeSceneCaptureTiming();
    if (reasonSamples)
    {
        *reasonSamples = RtSmokeSurfaceClassReasonSamples();
    }

    vertexData.clear();
    indexData.clear();
    triangleClassData.clear();
    triangleMaterialData.clear();
    vertexData.reserve(RT_SMOKE_MAX_VERTS);
    indexData.reserve(RT_SMOKE_MAX_INDEXES);
    triangleClassData.reserve(RT_SMOKE_MAX_INDEXES / 3);
    triangleMaterialData.reserve(RT_SMOKE_MAX_INDEXES / 3);

    if (!viewDef || !viewDef->drawSurfs)
    {
        return false;
    }

    std::unordered_set<uint64> sceneUniverseLegacyKeys;
    BuildSceneUniverseLegacyKeySet(sceneUniverse, sceneUniverseLegacyKeys);

    std::vector<PathTraceSmokeVertex> bucketVertexData[RT_SMOKE_CLASS_COUNT];
    std::vector<uint32_t> bucketIndexData[RT_SMOKE_CLASS_COUNT];
    std::vector<uint32_t> bucketTriangleClassData[RT_SMOKE_CLASS_COUNT];
    std::vector<uint32_t> bucketTriangleMaterialData[RT_SMOKE_CLASS_COUNT];
    for (int bucketIndex = 0; bucketIndex < RT_SMOKE_CLASS_COUNT; ++bucketIndex)
    {
        bucketVertexData[bucketIndex].reserve(RT_SMOKE_MAX_VERTS / RT_SMOKE_CLASS_COUNT);
        bucketIndexData[bucketIndex].reserve(RT_SMOKE_MAX_INDEXES / RT_SMOKE_CLASS_COUNT);
        bucketTriangleClassData[bucketIndex].reserve(RT_SMOKE_MAX_INDEXES / (3 * RT_SMOKE_CLASS_COUNT));
        bucketTriangleMaterialData[bucketIndex].reserve(RT_SMOKE_MAX_INDEXES / (3 * RT_SMOKE_CLASS_COUNT));
    }

    int dynamicVerts = 0;
    int dynamicIndexes = 0;
    int dynamicSurfaces = 0;
    int skippedRoutedRigidDynamicSurfaces = 0;
    int skippedRoutedRigidDynamicIndexes = 0;
    const int requestedDebugMode = r_pathTracingDebugMode.GetInteger();
    const bool routeMode18 = requestedDebugMode == 18 && r_pathTracingRigidRouteMode18.GetInteger() != 0;
    const bool routeMode20 = requestedDebugMode == 20 && r_pathTracingRigidRouteMode20.GetInteger() != 0;
    const bool removeRoutedRigidDynamic =
        (requestedDebugMode == 24 || requestedDebugMode == 25 || requestedDebugMode == 39 || requestedDebugMode == 40 || requestedDebugMode == 41 || requestedDebugMode == 47 || requestedDebugMode == 48 || requestedDebugMode == 42 || requestedDebugMode == 43 || routeMode18 || routeMode20) &&
        r_pathTracingRigidRouteRemoveDynamic.GetInteger() != 0 &&
        r_pathTracingRigidTlasRoute.GetInteger() != 0 &&
        r_pathTracingRigidBlasGpuScaffold.GetInteger() != 0 &&
        r_pathTracingRigidBlasGpuBuild.GetInteger() != 0;

    for (int surfaceIndex = 0; surfaceIndex < viewDef->numDrawSurfs; ++surfaceIndex)
    {
        const drawSurf_t* drawSurf = viewDef->drawSurfs[surfaceIndex];
        const srfTriangles_t* tri = nullptr;
        const int validationStartMs = Sys_Milliseconds();
        if (!ValidateSmokeDrawSurface(viewDef, drawSurf, tri, &skipStats))
        {
            captureTiming.validationMs += Sys_Milliseconds() - validationStartMs;
            continue;
        }
        captureTiming.validationMs += Sys_Milliseconds() - validationStartMs;

        const int classifyStartMs = Sys_Milliseconds();
        const RtSmokeSurfaceClass classifiedSurfaceClass = ClassifySmokeSurface(viewDef, drawSurf, tri);
        const RtSmokeSurfaceClass surfaceClass = PtMirrorEffectiveSurfaceClass(drawSurf, tri, classifiedSurfaceClass);
        captureTiming.dynamicPassClassifyMs += Sys_Milliseconds() - classifyStartMs;
        if (surfaceClass == RtSmokeSurfaceClass::StaticWorld)
        {
            continue;
        }

        const uint64 legacyStaticKey = BuildSmokeStaticSurfaceKeyForDiagnostics(drawSurf, tri);
        if (DrawSurfMirrorIsStaticMatch(geometryUniverse, sceneUniverseLegacyKeys, legacyStaticKey))
        {
            continue;
        }
        if (removeRoutedRigidDynamic && surfaceClass == RtSmokeSurfaceClass::RigidEntity && geometryUniverse)
        {
            RtPathTraceMeshKey meshKey;
            meshKey.tri = tri;
            meshKey.vertexBufferIdentity = static_cast<uintptr_t>(tri ? tri->ambientCache : 0);
            meshKey.indexBufferIdentity = static_cast<uintptr_t>(tri ? tri->indexCache : 0);
            meshKey.numVerts = tri ? tri->numVerts : 0;
            meshKey.numIndexes = tri ? tri->numIndexes : 0;
            meshKey.vertexFormat = static_cast<uint32_t>(RtSmokeGeometryBufferFormat::LegacySmokeVertex);
            meshKey.materialId = SmokeMaterialId(drawSurf->material);
            meshKey.sourceKind = SmokeSurfaceClassId(surfaceClass);
            if (geometryUniverse->IsRigidRouteReady(PtMeshKeyHash(meshKey)))
            {
                ++skippedRoutedRigidDynamicSurfaces;
                skippedRoutedRigidDynamicIndexes += tri->numIndexes;
                continue;
            }
        }

        if (dynamicSurfaces >= RT_SMOKE_MAX_SURFACES ||
            dynamicVerts + tri->numVerts > RT_SMOKE_MAX_VERTS ||
            dynamicIndexes + tri->numIndexes > RT_SMOKE_MAX_INDEXES)
        {
            ++skipStats.limitExceeded;
            continue;
        }

        const RtSmokeTranslucentSubtype translucentSubtype = surfaceClass == RtSmokeSurfaceClass::ParticleAlpha ? ClassifySmokeTranslucentSubtype(drawSurf) : RtSmokeTranslucentSubtype::Unknown;
        const uint32_t surfaceClassId = SmokeSurfaceClassAndSubtypeId(surfaceClass, translucentSubtype);
        const uint32_t materialId = SmokeMaterialId(drawSurf->material);
        const int bucketIndex = idMath::ClampInt(0, RT_SMOKE_CLASS_COUNT - 1, static_cast<int>(surfaceClassId & RT_SMOKE_TRIANGLE_CLASS_MASK));

        std::vector<PathTraceSmokeVertex>& bucketVertices = bucketVertexData[bucketIndex];
        std::vector<uint32_t>& bucketIndexes = bucketIndexData[bucketIndex];
        std::vector<uint32_t>& bucketClasses = bucketTriangleClassData[bucketIndex];
        std::vector<uint32_t>& bucketMaterials = bucketTriangleMaterialData[bucketIndex];
        const bool usesRtCpuSkinning = GetSmokeRtCpuSkinningJoints(tri) != nullptr;
        const int bucketVertexStart = static_cast<int>(bucketVertices.size());
        const int bucketIndexStart = static_cast<int>(bucketIndexes.size());
        const int bucketTriangleStart = static_cast<int>(bucketClasses.size());
        const int appendStartMs = Sys_Milliseconds();
        const int emittedIndexes = AppendSmokeSurfaceGeometry(
            drawSurf,
            tri,
            surfaceClassId,
            materialId,
            RT_SMOKE_CLASS_COUNT,
            RT_SMOKE_TRIANGLE_CLASS_MASK,
            static_cast<uint32_t>(RtSmokeSurfaceClass::ParticleAlpha),
            RT_SMOKE_TRIANGLE_FORCE_GEOMETRIC_NORMAL,
            bucketVertices,
            bucketIndexes,
            bucketClasses,
            bucketMaterials,
            skipStats,
            attributeStats);
        const int appendMs = Sys_Milliseconds() - appendStartMs;
        captureTiming.dynamicAppendMs += appendMs;
        captureTiming.appendMs += appendMs;
        if (usesRtCpuSkinning)
        {
            captureTiming.rtCpuSkinningAppendMs += appendMs;
        }
        if (emittedIndexes <= 0)
        {
            continue;
        }
        if (surfaceClass == RtSmokeSurfaceClass::SkinnedDeformed)
        {
            AddSmokeSkinnedSurfaceRecord(
                skinnedSurfaceRecords,
                drawSurf,
                tri,
                surfaceClassId,
                materialId,
                bucketIndex,
                bucketVertexStart,
                bucketIndexStart,
                bucketTriangleStart,
                static_cast<int>(bucketVertices.size()) - bucketVertexStart,
                emittedIndexes,
                emittedIndexes / 3);
        }

        AddMirrorMaterialStats(materialStats, drawSurf->material, emittedIndexes, surfaceClass, translucentSubtype);
        ++sourceSurfaces;
        ++dynamicSurfaces;
        sourceVerts += tri->numVerts;
        sourceIndexes += emittedIndexes;
        AddMirrorSurfaceClassStats(classStats, surfaceClass, tri->numVerts, emittedIndexes);
        AddMirrorDynamicGeometryStats(dynamicStats, surfaceClass, drawSurf, tri, emittedIndexes);
        ++bucketRanges.buckets[bucketIndex].surfaceCount;
        dynamicVerts += tri->numVerts;
        dynamicIndexes += emittedIndexes;
    }

    const int bucketMergeStartMs = Sys_Milliseconds();
    {
        OPTICK_EVENT("PT Dynamic Frame Bucket Merge");
        for (int bucketIndex = 0; bucketIndex < RT_SMOKE_CLASS_COUNT; ++bucketIndex)
        {
            if (bucketIndex == 0)
            {
                continue;
            }

            RtSmokeBucketRange& range = bucketRanges.buckets[bucketIndex];
            range.vertexOffset = static_cast<int>(vertexData.size());
            range.indexOffset = static_cast<int>(indexData.size());
            range.triangleOffset = static_cast<int>(triangleClassData.size());
            range.vertexCount = static_cast<int>(bucketVertexData[bucketIndex].size());
            range.indexCount = static_cast<int>(bucketIndexData[bucketIndex].size());
            range.triangleCount = static_cast<int>(bucketTriangleClassData[bucketIndex].size());
            FinalizeSmokeSkinnedSurfaceRecordOffsets(skinnedSurfaceRecords, bucketIndex, range);

            const uint32_t vertexOffset = static_cast<uint32_t>(range.vertexOffset);
            vertexData.insert(vertexData.end(), bucketVertexData[bucketIndex].begin(), bucketVertexData[bucketIndex].end());
            for (uint32_t localIndex : bucketIndexData[bucketIndex])
            {
                indexData.push_back(vertexOffset + localIndex);
            }
            triangleClassData.insert(triangleClassData.end(), bucketTriangleClassData[bucketIndex].begin(), bucketTriangleClassData[bucketIndex].end());
            triangleMaterialData.insert(triangleMaterialData.end(), bucketTriangleMaterialData[bucketIndex].begin(), bucketTriangleMaterialData[bucketIndex].end());
        }
    }
    captureTiming.bucketMergeMs = Sys_Milliseconds() - bucketMergeStartMs;

    if (triangleClassData.empty() || triangleMaterialData.empty())
    {
        ++skipStats.emptyClassBuffer;
    }
    if (skippedRoutedRigidDynamicSurfaces > 0 && (r_pathTracingSmokeLog.GetInteger() != 0 || r_pathTracingRigidRouteOverlapDump.GetInteger() != 0))
    {
        common->Printf("PathTracePrimaryPass: PT rigid route dynamic removal mode=%d removedSurfaces=%d removedIndexes=%d renderPath=routedRigidPlusDynamicFallback\n",
            requestedDebugMode,
            skippedRoutedRigidDynamicSurfaces,
            skippedRoutedRigidDynamicIndexes);
    }

    const bool hasDynamicGeometry = !vertexData.empty() && !indexData.empty() && !triangleClassData.empty() && !triangleMaterialData.empty();
    return sourceSurfaces > 0 && hasDynamicGeometry;
}
