#include "precompiled.h"
#pragma hdrstop

#include "PathTraceAcceleration.h"
#include "PathTraceCVars.h"
#include "PathTraceDrawSurfCapture.h"
#include "PathTraceDoomMaterialClassifier.h"
#include "PathTraceDynamicMaterialState.h"
#include "PathTraceGeometryLifecycle.h"
#include "PathTraceGeometryUniverse.h"
#include "PathTraceGuiSurfaces.h"
#include "PathTraceRestirPasses.h"
#include "PathTraceSceneCapture.h"
#include "PathTraceSceneUniverse.h"
#include "PathTraceSkinning.h"
#include "PathTraceSurfaceClassification.h"
#include "PathTraceTextureRegistry.h"
#include "../RenderCommon.h"
#include "../RenderWorld_local.h"

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

uint32_t PtDynamicTriangleIdentitySeed(const drawSurf_t* drawSurf, const srfTriangles_t* tri, uint32_t materialId, uint32_t localTriangleIndex)
{
    const idRenderEntityLocal* entity = (drawSurf && drawSurf->space) ? drawSurf->space->entityDef : nullptr;
    const renderEntity_t* renderEntity = entity ? &entity->parms : nullptr;
    const int entityIndex = entity ? entity->index : -1;
    const int renderEntityNum = renderEntity ? renderEntity->entityNum : -1;
    const uintptr_t triPtr = reinterpret_cast<uintptr_t>(tri);
    uint64 hash = 14695981039346656037ull;
    hash = PtHashBytes(hash, &entityIndex, sizeof(entityIndex));
    hash = PtHashBytes(hash, &renderEntityNum, sizeof(renderEntityNum));
    hash = PtHashBytes(hash, &materialId, sizeof(materialId));
    hash = PtHashBytes(hash, &triPtr, sizeof(triPtr));
    hash = PtHashBytes(hash, &localTriangleIndex, sizeof(localTriangleIndex));
    const uint32_t folded = static_cast<uint32_t>(hash) ^ static_cast<uint32_t>(hash >> 32);
    return folded != 0u ? folded : 1u;
}

struct PtMirrorCapturedDynamicSurfaceKey
{
    int entityIndex = -1;
    const srfTriangles_t* tri = nullptr;
    uint32_t materialId = 0;
};

bool PtMirrorCapturedDynamicSurfaceMatches(const PtMirrorCapturedDynamicSurfaceKey& key, int entityIndex, const srfTriangles_t* tri, uint32_t materialId)
{
    return key.entityIndex == entityIndex && key.tri == tri && key.materialId == materialId;
}

bool PtMirrorDynamicSurfaceAlreadyCaptured(const std::vector<PtMirrorCapturedDynamicSurfaceKey>& capturedSurfaces, int entityIndex, const srfTriangles_t* tri, uint32_t materialId)
{
    for (const PtMirrorCapturedDynamicSurfaceKey& key : capturedSurfaces)
    {
        if (PtMirrorCapturedDynamicSurfaceMatches(key, entityIndex, tri, materialId))
        {
            return true;
        }
    }
    return false;
}

const idMaterial* PtMirrorResolveEntitySurfaceMaterial(const idRenderEntityLocal* entityDef, const modelSurface_t* surface)
{
    const idMaterial* shader = surface ? surface->shader : nullptr;
    if (!entityDef || !shader)
    {
        return shader;
    }

    return R_RemapShaderBySkin(shader, entityDef->parms.customSkin, entityDef->parms.customShader);
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
        (renderEntity && renderEntity->joints != nullptr && renderEntity->numJoints > 0))
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
    const idMaterial* material = drawSurf->material;
    const char* materialName = material->GetName();

    if (!entity ||
        IsSmokeGuiDrawSurface(drawSurf) ||
        drawSurf->jointCache != 0 ||
        tri->staticModelWithJoints != nullptr ||
        (renderEntity && renderEntity->joints != nullptr && renderEntity->numJoints > 0) ||
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

    const uint32_t baseMaterialId = SmokeMaterialId(material);
    if (SmokeRuntimeMaterialVariantIdForDrawSurf(drawSurf, baseMaterialId) != baseMaterialId)
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

std::vector<bool> PtMirrorBuildSelectedAreas(const viewDef_t* viewDef, int portalSteps)
{
    idRenderWorldLocal* renderWorld = viewDef ? viewDef->renderWorld : nullptr;
    const int areaCount = renderWorld ? renderWorld->NumAreas() : 0;
    if (!renderWorld || areaCount <= 0)
    {
        return std::vector<bool>();
    }

    std::vector<bool> selectedAreas(static_cast<size_t>(areaCount), false);
    std::vector<int> selectedDepth(static_cast<size_t>(areaCount), -1);
    std::vector<int> queue;
    queue.reserve(static_cast<size_t>(areaCount));

    int currentArea = viewDef->areaNum;
    if (currentArea < 0)
    {
        currentArea = renderWorld->PointInArea(viewDef->initialViewAreaOrigin);
    }
    if (currentArea < 0)
    {
        currentArea = renderWorld->PointInArea(viewDef->renderView.vieworg);
    }
    if (currentArea < 0 || currentArea >= areaCount)
    {
        return selectedAreas;
    }

    selectedAreas[static_cast<size_t>(currentArea)] = true;
    selectedDepth[static_cast<size_t>(currentArea)] = 0;
    queue.push_back(currentArea);

    const int maxDepth = idMath::ClampInt(0, 8, portalSteps);
    for (size_t queueIndex = 0; queueIndex < queue.size(); ++queueIndex)
    {
        const int area = queue[queueIndex];
        const int depth = selectedDepth[static_cast<size_t>(area)];
        if (depth >= maxDepth)
        {
            continue;
        }

        const int portalCount = renderWorld->NumPortalsInArea(area);
        for (int portalIndex = 0; portalIndex < portalCount; ++portalIndex)
        {
            const exitPortal_t portal = renderWorld->GetPortal(area, portalIndex);
            if (portal.blockingBits != PS_BLOCK_NONE)
            {
                continue;
            }

            int nextArea = -1;
            if (portal.areas[0] == area)
            {
                nextArea = portal.areas[1];
            }
            else if (portal.areas[1] == area)
            {
                nextArea = portal.areas[0];
            }
            if (nextArea < 0 || nextArea >= areaCount)
            {
                continue;
            }
            if (!selectedAreas[static_cast<size_t>(nextArea)])
            {
                selectedAreas[static_cast<size_t>(nextArea)] = true;
                selectedDepth[static_cast<size_t>(nextArea)] = depth + 1;
                queue.push_back(nextArea);
            }
        }
    }

    return selectedAreas;
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
        const char* modelName = renderEntity && renderEntity->hModel ? "<entity-model>" : "<none>";
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
        meshObservation.modelName = modelName;
        meshObservation.localSpaceValid = true;

        RtPathTraceInstanceObservation instanceObservation;
        instanceObservation.meshHash = meshObservation.stableHash;
        instanceObservation.entity = entity;
        instanceObservation.entityIndex = entity ? entity->index : -1;
        instanceObservation.renderEntityNum = renderEntity ? renderEntity->entityNum : -1;
        instanceObservation.drawSurfIndex = surfaceIndex;
        instanceObservation.currentArea = PtMirrorResolveDrawSurfArea(viewDef, drawSurf, tri);
        instanceObservation.renderDefKey = PtGeometryLifecycle::MakeEntityKey(entity);
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
            const uint32_t candidateBaseMaterialId = SmokeMaterialId(material);
            if (SmokeRuntimeMaterialVariantIdForDrawSurf(drawSurf, candidateBaseMaterialId) != candidateBaseMaterialId)
            {
                continue;
            }

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
    std::vector<uint32_t>* triangleInstanceData,
    std::vector<uint32_t>* triangleIdentityData,
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
    if (triangleInstanceData)
    {
        triangleInstanceData->clear();
    }
    if (triangleIdentityData)
    {
        triangleIdentityData->clear();
    }
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
    std::vector<uint32_t> bucketTriangleInstanceData[RT_SMOKE_CLASS_COUNT];
    std::vector<uint32_t> bucketTriangleIdentityData[RT_SMOKE_CLASS_COUNT];
    for (int bucketIndex = 0; bucketIndex < RT_SMOKE_CLASS_COUNT; ++bucketIndex)
    {
        bucketVertexData[bucketIndex].reserve(RT_SMOKE_MAX_VERTS / RT_SMOKE_CLASS_COUNT);
        bucketIndexData[bucketIndex].reserve(RT_SMOKE_MAX_INDEXES / RT_SMOKE_CLASS_COUNT);
        bucketTriangleClassData[bucketIndex].reserve(RT_SMOKE_MAX_INDEXES / (3 * RT_SMOKE_CLASS_COUNT));
        bucketTriangleMaterialData[bucketIndex].reserve(RT_SMOKE_MAX_INDEXES / (3 * RT_SMOKE_CLASS_COUNT));
        bucketTriangleInstanceData[bucketIndex].reserve(RT_SMOKE_MAX_INDEXES / (3 * RT_SMOKE_CLASS_COUNT));
        bucketTriangleIdentityData[bucketIndex].reserve(RT_SMOKE_MAX_INDEXES / (3 * RT_SMOKE_CLASS_COUNT));
    }

    int dynamicVerts = 0;
    int dynamicIndexes = 0;
    int dynamicSurfaces = 0;
    std::vector<PtMirrorCapturedDynamicSurfaceKey> capturedDynamicSurfaces;
    capturedDynamicSurfaces.reserve(static_cast<size_t>(viewDef->numDrawSurfs));
    int skippedRoutedRigidDynamicSurfaces = 0;
    int skippedRoutedRigidDynamicIndexes = 0;
    const int requestedDebugMode = r_pathTracingDebugMode.GetInteger();
    const bool routeMode18 = requestedDebugMode == 18 && r_pathTracingRigidRouteMode18.GetInteger() != 0;
    const bool routeMode20 = requestedDebugMode == 20 && r_pathTracingRigidRouteMode20.GetInteger() != 0;
    const bool routeRestirPTMode = IsPathTraceRestirPTDebugMode(requestedDebugMode);
    const bool routeIntegratorDebugMode = requestedDebugMode >= 34 && requestedDebugMode <= 37;
    const bool routeLifecycleMode = r_pathTracingGeometryLifecycle.GetInteger() != 0;
    const bool removeRoutedRigidDynamic =
        (routeLifecycleMode || requestedDebugMode == 24 || requestedDebugMode == 25 || requestedDebugMode == 39 || requestedDebugMode == 40 || requestedDebugMode == 41 || requestedDebugMode == 47 || requestedDebugMode == 48 || requestedDebugMode == 49 || requestedDebugMode == 52 || requestedDebugMode == 42 || requestedDebugMode == 43 || routeMode18 || routeMode20 || routeRestirPTMode || routeIntegratorDebugMode) &&
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
        const uint32_t baseMaterialId = SmokeMaterialId(drawSurf->material);
        const uint32_t materialId = SmokeRuntimeMaterialTableIdForDrawSurf(drawSurf, baseMaterialId);
        const int bucketIndex = idMath::ClampInt(0, RT_SMOKE_CLASS_COUNT - 1, static_cast<int>(surfaceClassId & RT_SMOKE_TRIANGLE_CLASS_MASK));

        std::vector<PathTraceSmokeVertex>& bucketVertices = bucketVertexData[bucketIndex];
        std::vector<uint32_t>& bucketIndexes = bucketIndexData[bucketIndex];
        std::vector<uint32_t>& bucketClasses = bucketTriangleClassData[bucketIndex];
        std::vector<uint32_t>& bucketMaterials = bucketTriangleMaterialData[bucketIndex];
        std::vector<uint32_t>& bucketInstances = bucketTriangleInstanceData[bucketIndex];
        std::vector<uint32_t>& bucketIdentities = bucketTriangleIdentityData[bucketIndex];
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
        const int entityIndex = (drawSurf->space && drawSurf->space->entityDef) ? drawSurf->space->entityDef->index : -1;
        const uint32_t dynamicInstanceId = static_cast<uint32_t>(Max(1, entityIndex + 1));
        const int emittedTriangles = emittedIndexes / 3;
        bucketInstances.insert(bucketInstances.end(), emittedTriangles, dynamicInstanceId);
        for (int localTriangleIndex = 0; localTriangleIndex < emittedTriangles; ++localTriangleIndex)
        {
            bucketIdentities.push_back(PtDynamicTriangleIdentitySeed(drawSurf, tri, baseMaterialId, static_cast<uint32_t>(localTriangleIndex)));
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
        AddSmokeDynamicMaterialEvalStatsForMaterialId(materialStats, drawSurf, emittedIndexes, materialId);
        ++sourceSurfaces;
        ++dynamicSurfaces;
        sourceVerts += tri->numVerts;
        sourceIndexes += emittedIndexes;
        AddMirrorSurfaceClassStats(classStats, surfaceClass, tri->numVerts, emittedIndexes);
        AddMirrorDynamicGeometryStats(dynamicStats, surfaceClass, drawSurf, tri, emittedIndexes);
        ++bucketRanges.buckets[bucketIndex].surfaceCount;
        if (entityIndex >= 0)
        {
            PtMirrorCapturedDynamicSurfaceKey capturedKey;
            capturedKey.entityIndex = entityIndex;
            capturedKey.tri = tri;
            capturedKey.materialId = baseMaterialId;
            capturedDynamicSurfaces.push_back(capturedKey);
        }
        dynamicVerts += tri->numVerts;
        dynamicIndexes += emittedIndexes;
    }

    if (routeLifecycleMode)
    {
        OPTICK_EVENT("PT Capture Lifecycle Deforming Area Feed");
        idRenderWorldLocal* renderWorld = viewDef->renderWorld;
        const std::vector<bool> selectedAreas = PtMirrorBuildSelectedAreas(viewDef, r_pathTracingRigidResidencyPortalSteps.GetInteger());
        std::unordered_set<int> retainedEntities;
        retainedEntities.reserve(128);

        for (int areaIndex = 0; renderWorld && areaIndex < static_cast<int>(selectedAreas.size()); ++areaIndex)
        {
            if (!selectedAreas[static_cast<size_t>(areaIndex)])
            {
                continue;
            }

            portalArea_t* area = &renderWorld->portalAreas[areaIndex];
            for (areaReference_t* ref = area->entityRefs.areaNext; ref != &area->entityRefs; ref = ref->areaNext)
            {
                idRenderEntityLocal* entityDef = ref ? ref->entity : nullptr;
                const renderEntity_t* renderEntity = entityDef ? &entityDef->parms : nullptr;
                idRenderModel* model = renderEntity ? renderEntity->hModel : nullptr;
                if (!entityDef || !renderEntity || !model || model->IsStaticWorldModel())
                {
                    continue;
                }
                if (!PtGeometryLifecycle::IsEntityKeyAlive(PtGeometryLifecycle::MakeEntityKey(entityDef)))
                {
                    continue;
                }
                if (!retainedEntities.insert(entityDef->index).second)
                {
                    continue;
                }
                if (model->IsDynamicModel() != DM_CACHED)
                {
                    continue;
                }
                if (renderEntity->callback != nullptr && r_pathTracingSkipCallbackEntities.GetInteger() != 0)
                {
                    continue;
                }

                idRenderModel* dynamicModel = R_EntityDefDynamicModel(entityDef);
                if (!dynamicModel || dynamicModel->IsStaticWorldModel())
                {
                    continue;
                }

                viewEntity_t retainedSpace = {};
                retainedSpace.entityDef = entityDef;
                retainedSpace.weaponDepthHack = renderEntity->weaponDepthHack;
                retainedSpace.modelDepthHack = renderEntity->modelDepthHack;
                memcpy(retainedSpace.modelMatrix, entityDef->modelMatrix, sizeof(retainedSpace.modelMatrix));
                R_MatrixMultiply(entityDef->modelMatrix, viewDef->worldSpace.modelViewMatrix, retainedSpace.modelViewMatrix);

                for (int surfaceIndex = 0; surfaceIndex < dynamicModel->NumSurfaces(); ++surfaceIndex)
                {
                    const modelSurface_t* surface = dynamicModel->Surface(surfaceIndex);
                    const srfTriangles_t* tri = surface ? surface->geometry : nullptr;
                    const idMaterial* shader = PtMirrorResolveEntitySurfaceMaterial(entityDef, surface);
                    if (!tri || !tri->verts || !tri->indexes || tri->numVerts < 3 || tri->numIndexes < 3 || !shader || !shader->IsDrawn())
                    {
                        continue;
                    }
                    if ((tri->numIndexes % 3) != 0)
                    {
                        ++skipStats.invalidIndexCount;
                        continue;
                    }

                    const uint32_t baseMaterialId = SmokeMaterialId(shader);
                    if (PtMirrorDynamicSurfaceAlreadyCaptured(capturedDynamicSurfaces, entityDef->index, tri, baseMaterialId))
                    {
                        continue;
                    }

                    drawSurf_t retainedDrawSurf = {};
                    retainedDrawSurf.frontEndGeo = tri;
                    retainedDrawSurf.numIndexes = tri->numIndexes;
                    retainedDrawSurf.indexCache = tri->indexCache;
                    retainedDrawSurf.ambientCache = tri->ambientCache;
                    retainedDrawSurf.jointCache = 0;
                    retainedDrawSurf.space = &retainedSpace;
                    retainedDrawSurf.extraGLState = 0;
                    R_SetupDrawSurfShader(&retainedDrawSurf, shader, renderEntity);

                    const int classifyStartMs = Sys_Milliseconds();
                    const RtSmokeSurfaceClass surfaceClass = ClassifySmokeSurface(viewDef, &retainedDrawSurf, tri);
                    captureTiming.dynamicPassClassifyMs += Sys_Milliseconds() - classifyStartMs;
                    if (surfaceClass != RtSmokeSurfaceClass::SkinnedDeformed)
                    {
                        continue;
                    }

                    if (dynamicSurfaces >= RT_SMOKE_MAX_SURFACES ||
                        dynamicVerts + tri->numVerts > RT_SMOKE_MAX_VERTS ||
                        dynamicIndexes + tri->numIndexes > RT_SMOKE_MAX_INDEXES)
                    {
                        ++skipStats.limitExceeded;
                        break;
                    }

                    const RtSmokeTranslucentSubtype translucentSubtype = RtSmokeTranslucentSubtype::Unknown;
                    const uint32_t surfaceClassId = SmokeSurfaceClassAndSubtypeId(surfaceClass, translucentSubtype);
                    const uint32_t materialId = SmokeRuntimeMaterialTableIdForDrawSurf(&retainedDrawSurf, baseMaterialId);
                    const int bucketIndex = idMath::ClampInt(0, RT_SMOKE_CLASS_COUNT - 1, static_cast<int>(surfaceClassId & RT_SMOKE_TRIANGLE_CLASS_MASK));

                    std::vector<PathTraceSmokeVertex>& bucketVertices = bucketVertexData[bucketIndex];
                    std::vector<uint32_t>& bucketIndexes = bucketIndexData[bucketIndex];
                    std::vector<uint32_t>& bucketClasses = bucketTriangleClassData[bucketIndex];
                    std::vector<uint32_t>& bucketMaterials = bucketTriangleMaterialData[bucketIndex];
                    std::vector<uint32_t>& bucketInstances = bucketTriangleInstanceData[bucketIndex];
                    std::vector<uint32_t>& bucketIdentities = bucketTriangleIdentityData[bucketIndex];
                    const bool usesRtCpuSkinning = GetSmokeRtCpuSkinningJoints(tri) != nullptr;
                    const int bucketVertexStart = static_cast<int>(bucketVertices.size());
                    const int bucketIndexStart = static_cast<int>(bucketIndexes.size());
                    const int bucketTriangleStart = static_cast<int>(bucketClasses.size());
                    const int appendStartMs = Sys_Milliseconds();
                    const int emittedIndexes = AppendSmokeSurfaceGeometry(
                        &retainedDrawSurf,
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

                    const uint32_t dynamicInstanceId = static_cast<uint32_t>(Max(1, entityDef->index + 1));
                    const int emittedTriangles = emittedIndexes / 3;
                    bucketInstances.insert(bucketInstances.end(), emittedTriangles, dynamicInstanceId);
                    for (int localTriangleIndex = 0; localTriangleIndex < emittedTriangles; ++localTriangleIndex)
                    {
                        bucketIdentities.push_back(PtDynamicTriangleIdentitySeed(&retainedDrawSurf, tri, baseMaterialId, static_cast<uint32_t>(localTriangleIndex)));
                    }
                    AddSmokeSkinnedSurfaceRecord(
                        skinnedSurfaceRecords,
                        &retainedDrawSurf,
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

                    AddMirrorMaterialStats(materialStats, shader, emittedIndexes, surfaceClass, translucentSubtype);
                    AddSmokeDynamicMaterialEvalStatsForMaterialId(materialStats, &retainedDrawSurf, emittedIndexes, materialId);
                    ++sourceSurfaces;
                    ++dynamicSurfaces;
                    ++dynamicStats.retainedOccluderSurfaces;
                    dynamicStats.retainedOccluderIndexes += emittedIndexes;
                    sourceVerts += tri->numVerts;
                    sourceIndexes += emittedIndexes;
                    AddMirrorSurfaceClassStats(classStats, surfaceClass, tri->numVerts, emittedIndexes);
                    AddMirrorDynamicGeometryStats(dynamicStats, surfaceClass, &retainedDrawSurf, tri, emittedIndexes);
                    ++bucketRanges.buckets[bucketIndex].surfaceCount;
                    dynamicVerts += tri->numVerts;
                    dynamicIndexes += emittedIndexes;

                    PtMirrorCapturedDynamicSurfaceKey capturedKey;
                    capturedKey.entityIndex = entityDef->index;
                    capturedKey.tri = tri;
                    capturedKey.materialId = baseMaterialId;
                    capturedDynamicSurfaces.push_back(capturedKey);
                }
            }
        }
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
            if (triangleInstanceData)
            {
                triangleInstanceData->insert(triangleInstanceData->end(), bucketTriangleInstanceData[bucketIndex].begin(), bucketTriangleInstanceData[bucketIndex].end());
            }
            if (triangleIdentityData)
            {
                triangleIdentityData->insert(triangleIdentityData->end(), bucketTriangleIdentityData[bucketIndex].begin(), bucketTriangleIdentityData[bucketIndex].end());
            }
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
