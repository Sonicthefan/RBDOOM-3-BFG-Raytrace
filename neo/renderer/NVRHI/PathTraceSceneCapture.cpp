#include "precompiled.h"
#pragma hdrstop

// Doom draw-surface capture implementation for the RT smoke scene.
//
// The capture pass is intentionally conservative: it buckets surfaces into the
// few categories the prototype can render, records skip/classification reasons
// for diagnostics, and leaves unsupported dynamic material behavior hidden until
// a later material system can represent it safely.

#include "PathTraceCVars.h"
#include "PathTraceSceneCapture.h"
#include "PathTraceAcceleration.h"
#include "PathTraceDoomMaterialClassifier.h"
#include "PathTraceDynamicMaterialState.h"
#include "PathTraceGuiSurfaces.h"
#include "PathTraceSkinning.h"
#include "PathTraceSurfaceClassification.h"
#include "../GLMatrix.h"
#include "../Model_local.h"
#include "../RenderCommon.h"

#include <algorithm>


void TransformSurfacePointToWorld(const drawSurf_t* drawSurf, const idVec3& localPoint, idVec3& worldPoint)
{
    if (drawSurf->space)
    {
        R_LocalPointToGlobal(drawSurf->space->modelMatrix, localPoint, worldPoint);
        return;
    }

    worldPoint = localPoint;
}

void TransformSurfaceVectorToWorld(const drawSurf_t* drawSurf, const idVec3& localVector, idVec3& worldVector)
{
    if (drawSurf->space)
    {
        R_LocalVectorToGlobal(drawSurf->space->modelMatrix, localVector, worldVector);
        return;
    }

    worldVector = localVector;
}

bool ValidateSmokeDrawSurface(const viewDef_t* viewDef, const drawSurf_t* drawSurf, const srfTriangles_t*& tri, RtSmokeSurfaceSkipStats* skipStats)
{
    tri = nullptr;
    if (!drawSurf)
    {
        if (skipStats)
        {
            ++skipStats->nullSurface;
        }
        return false;
    }

    if (!drawSurf->frontEndGeo)
    {
        if (skipStats)
        {
            ++skipStats->missingGeometry;
        }
        return false;
    }

    if (!drawSurf->material)
    {
        if (skipStats)
        {
            ++skipStats->nullMaterial;
        }
        return false;
    }

    const bool guiDrawSurface = IsSmokeGuiDrawSurface(drawSurf);
    if (guiDrawSurface && r_pathTracingAllowGuiSurfaces.GetInteger() == 0)
    {
        if (skipStats)
        {
            ++skipStats->guiSurface;
        }
        return false;
    }

    if (!drawSurf->space)
    {
        if (skipStats)
        {
            ++skipStats->nullSpace;
        }
        return false;
    }

    const viewEntity_t* space = drawSurf->space;
    const idRenderEntityLocal* entityDef = space->entityDef;
    const renderEntity_t* renderEntity = entityDef ? &entityDef->parms : nullptr;
    if (!guiDrawSurface && viewDef && space != &viewDef->worldSpace && (!renderEntity || !renderEntity->hModel))
    {
        if (skipStats)
        {
            ++skipStats->nullModel;
        }
        return false;
    }

    const bool riskyCallbackSurface =
        renderEntity &&
        renderEntity->callback &&
        renderEntity->customShader != nullptr;
    if (!guiDrawSurface && riskyCallbackSurface && r_pathTracingSkipCallbackEntities.GetInteger() != 0)
    {
        if (skipStats)
        {
            ++skipStats->callbackEntity;
        }
        return false;
    }

    tri = drawSurf->frontEndGeo;
    if (!tri->verts || !tri->indexes || tri->numVerts < 3 || tri->numIndexes < 3)
    {
        if (skipStats)
        {
            ++skipStats->missingGeometry;
        }
        return false;
    }

    if ((tri->numIndexes % 3) != 0 || drawSurf->numIndexes < 3)
    {
        if (skipStats)
        {
            ++skipStats->invalidIndexCount;
        }
        return false;
    }

    const bool hasAmbientCache = tri->ambientCache != 0;
    const bool hasIndexCache = tri->indexCache != 0;
    if ((hasAmbientCache && !vertexCache.CacheIsCurrent(tri->ambientCache)) ||
        (hasIndexCache && !vertexCache.CacheIsCurrent(tri->indexCache)))
    {
        if (skipStats)
        {
            ++skipStats->nonCurrentCache;
        }
        return false;
    }

    return true;
}

bool SmokeSurfaceBoundsMayHitRay(const drawSurf_t* drawSurf, const srfTriangles_t* tri, const idVec3& rayOrigin, const idVec3& rayDirection)
{
    if (!drawSurf || !tri)
    {
        return false;
    }

    idVec3 localPoints[8];
    tri->bounds.Expand(1.0f).ToPoints(localPoints);

    idBounds worldBounds;
    worldBounds.Clear();
    for (int pointIndex = 0; pointIndex < 8; ++pointIndex)
    {
        idVec3 worldPoint;
        TransformSurfacePointToWorld(drawSurf, localPoints[pointIndex], worldPoint);
        worldBounds.AddPoint(worldPoint);
    }

    float boundsHitScale = 0.0f;
    return worldBounds.RayIntersection(rayOrigin, rayDirection, boundsHitScale) && boundsHitScale >= 0.0f;
}

bool FindCenterCameraRayAnchor(const viewDef_t* viewDef, idVec3& anchorPoint, int& anchorSurface, int& anchorTriangle, RtSmokeSceneCaptureTiming* captureTiming)
{
    const idVec3 rayOrigin = viewDef->renderView.vieworg;
    idVec3 rayDirection = viewDef->renderView.viewaxis[0];
    rayDirection.Normalize();

    bool foundHit = false;
    float closestHit = 1.0e30f;
    anchorSurface = -1;
    anchorTriangle = -1;

    for (int anchorPass = 0; anchorPass < 2 && !foundHit; ++anchorPass)
    {
        const bool useBoundsCull = anchorPass == 0;
        for (int surfaceIndex = 0; surfaceIndex < viewDef->numDrawSurfs; ++surfaceIndex)
        {
            const drawSurf_t* drawSurf = viewDef->drawSurfs[surfaceIndex];
            if (!drawSurf || !drawSurf->frontEndGeo)
            {
                continue;
            }

            const srfTriangles_t* tri = nullptr;
            if (!ValidateSmokeDrawSurface(viewDef, drawSurf, tri, nullptr))
            {
                continue;
            }

            if (captureTiming && useBoundsCull)
            {
                ++captureTiming->anchorSurfaceTests;
            }
            if (useBoundsCull && !SmokeSurfaceBoundsMayHitRay(drawSurf, tri, rayOrigin, rayDirection))
            {
                if (captureTiming)
                {
                    ++captureTiming->anchorBoundsRejects;
                }
                continue;
            }

            const idJointMat* rtCpuSkinningJoints = GetSmokeRtCpuSkinningJoints(tri);
            for (int index = 0; index + 2 < tri->numIndexes; index += 3)
            {
                if (captureTiming && useBoundsCull)
                {
                    ++captureTiming->anchorTriangleTests;
                }
                const int i0 = tri->indexes[index + 0];
                const int i1 = tri->indexes[index + 1];
                const int i2 = tri->indexes[index + 2];
                if (i0 < 0 || i1 < 0 || i2 < 0 || i0 >= tri->numVerts || i1 >= tri->numVerts || i2 >= tri->numVerts)
                {
                    continue;
                }

                idVec3 p0;
                idVec3 p1;
                idVec3 p2;
                TransformSmokeSurfaceVertexToWorld(drawSurf, tri, i0, rtCpuSkinningJoints, p0);
                TransformSmokeSurfaceVertexToWorld(drawSurf, tri, i1, rtCpuSkinningJoints, p1);
                TransformSmokeSurfaceVertexToWorld(drawSurf, tri, i2, rtCpuSkinningJoints, p2);
                if (IsZeroAreaSmokeTriangle(p0, p1, p2))
                {
                    continue;
                }

                float hitDistance = 0.0f;
                if (IntersectRayTriangle(rayOrigin, rayDirection, p0, p1, p2, hitDistance) && hitDistance < closestHit)
                {
                    closestHit = hitDistance;
                    anchorPoint = rayOrigin + rayDirection * hitDistance;
                    anchorSurface = surfaceIndex;
                    anchorTriangle = index / 3;
                    foundHit = true;
                }
            }
        }
    }

    return foundHit;
}

PathTraceSmokeVertex BuildSmokeSurfaceVertex(const drawSurf_t* drawSurf, const srfTriangles_t* tri, int vertexIndex, const idJointMat* rtCpuSkinningJoints)
{
    const idDrawVert& drawVert = tri->verts[vertexIndex];
    idVec3 localPosition = drawVert.xyz;
    idVec3 localNormal = drawVert.GetNormal();
    if (rtCpuSkinningJoints)
    {
        localPosition = TransformSmokeSkinnedVertexPosition(drawVert, rtCpuSkinningJoints);
        localNormal = TransformSmokeSkinnedVertexNormal(drawVert, rtCpuSkinningJoints);
    }

    idVec3 worldPosition;
    idVec3 worldNormal;
    TransformSurfacePointToWorld(drawSurf, localPosition, worldPosition);
    TransformSurfaceVectorToWorld(drawSurf, localNormal, worldNormal);
    worldNormal.Normalize();

    const idVec2 texCoord = drawVert.GetTexCoord();
    PathTraceSmokeVertex vertex = {};
    vertex.position[0] = worldPosition.x;
    vertex.position[1] = worldPosition.y;
    vertex.position[2] = worldPosition.z;
    vertex.position[3] = 1.0f;
    vertex.normal[0] = worldNormal.x;
    vertex.normal[1] = worldNormal.y;
    vertex.normal[2] = worldNormal.z;
    vertex.normal[3] = 0.0f;
    vertex.texCoord[0] = texCoord.x;
    vertex.texCoord[1] = texCoord.y;
    vertex.texCoord[2] = 0.0f;
    vertex.texCoord[3] = 0.0f;
    vertex.color[0] = drawVert.color[0] * (1.0f / 255.0f);
    vertex.color[1] = drawVert.color[1] * (1.0f / 255.0f);
    vertex.color[2] = drawVert.color[2] * (1.0f / 255.0f);
    vertex.color[3] = drawVert.color[3] * (1.0f / 255.0f);
    vertex.color2[0] = drawVert.color2[0] * (1.0f / 255.0f);
    vertex.color2[1] = drawVert.color2[1] * (1.0f / 255.0f);
    vertex.color2[2] = drawVert.color2[2] * (1.0f / 255.0f);
    vertex.color2[3] = drawVert.color2[3] * (1.0f / 255.0f);
    return vertex;
}

void TransformSmokeSurfaceVertexToWorld(const drawSurf_t* drawSurf, const srfTriangles_t* tri, int vertexIndex, const idJointMat* rtCpuSkinningJoints, idVec3& worldPosition)
{
    const PathTraceSmokeVertex vertex = BuildSmokeSurfaceVertex(drawSurf, tri, vertexIndex, rtCpuSkinningJoints);
    worldPosition.Set(vertex.position[0], vertex.position[1], vertex.position[2]);
}

static bool SmokeDrawSurfaceHasActiveEmissiveStage(const drawSurf_t* drawSurf)
{
    const idMaterial* material = drawSurf ? drawSurf->material : nullptr;
    if (!material)
    {
        return false;
    }

    const RtSmokeTranslucentClassifierInfo classifier = BuildSmokeTranslucentClassifierInfo(material);
    const bool nameLooksEmissive = !classifier.hasAddDefault0200Texture && (classifier.nameLooksGlow || classifier.nameLooksSignage);
    const float* regs = drawSurf->shaderRegisters ? drawSurf->shaderRegisters : material->ConstantRegisters();
    const int registerCount = material->GetNumRegisters();
    for (int stageIndex = 0; stageIndex < material->GetNumStages(); ++stageIndex)
    {
        const shaderStage_t* stage = material->GetStage(stageIndex);
        if (!stage || stage->lighting != SL_AMBIENT || !stage->texture.image)
        {
            continue;
        }

        if (regs && stage->conditionRegister >= 0 && stage->conditionRegister < registerCount && regs[stage->conditionRegister] == 0.0f)
        {
            continue;
        }

        const uint64 srcBlend = stage->drawStateBits & GLS_SRCBLEND_BITS;
        const uint64 dstBlend = stage->drawStateBits & GLS_DSTBLEND_BITS;
        const bool ambientBlendStage = dstBlend != GLS_DSTBLEND_ZERO || srcBlend == GLS_SRCBLEND_DST_COLOR || srcBlend == GLS_SRCBLEND_ONE_MINUS_DST_COLOR;
        if (!SmokeStageIsAdditiveBlend(stage) && !(nameLooksEmissive && ambientBlendStage && !classifier.nameLooksDecal))
        {
            continue;
        }

        idVec4 stageColor(1.0f, 1.0f, 1.0f, 1.0f);
        if (regs)
        {
            for (int component = 0; component < 4; ++component)
            {
                const int colorRegister = stage->color.registers[component];
                if (colorRegister >= 0 && colorRegister < registerCount)
                {
                    stageColor[component] = regs[colorRegister];
                }
            }
        }

        const float stageLuminance = Max(stageColor.x, Max(stageColor.y, stageColor.z)) * Max(stageColor.w, 0.0f);
        if (stageLuminance > 1.0e-4f)
        {
            return true;
        }
    }

    return false;
}

static bool SmokeDynamicEvalStageIsEmissiveLike(const shaderStage_t* stage)
{
    if (!stage)
    {
        return false;
    }

    const uint64 srcBlend = stage->drawStateBits & GLS_SRCBLEND_BITS;
    const uint64 dstBlend = stage->drawStateBits & GLS_DSTBLEND_BITS;
    return (stage->lighting == SL_AMBIENT && dstBlend == GLS_DSTBLEND_ONE) ||
        (srcBlend == GLS_SRCBLEND_ONE && dstBlend == GLS_DSTBLEND_ONE);
}

static float SmokeDynamicEvalColorLuminance(const float color[4])
{
    return Max(0.0f, color[0]) * 0.2126f +
        Max(0.0f, color[1]) * 0.7152f +
        Max(0.0f, color[2]) * 0.0722f;
}

static bool SmokeDynamicEvalSampleShouldReplace(const RtSmokeDynamicMaterialEvalSample& current, const RtSmokeDynamicMaterialEvalSample& candidate)
{
    if (!current.valid)
    {
        return true;
    }
    if (candidate.stagePriority != current.stagePriority)
    {
        return candidate.stagePriority > current.stagePriority;
    }
    return SmokeDynamicEvalColorLuminance(candidate.color) > SmokeDynamicEvalColorLuminance(current.color);
}

static void SmokeDynamicEvalCopySelectedStage(RtSmokeDynamicMaterialEvalSample& dst, const RtSmokeDynamicMaterialEvalSample& src)
{
    dst.valid = src.valid;
    dst.stageIndex = src.stageIndex;
    dst.stagePriority = src.stagePriority;
    dst.condition = src.condition;
    dst.alphaTest = src.alphaTest;
    for (int component = 0; component < 4; ++component)
    {
        dst.color[component] = src.color[component];
    }
    for (int row = 0; row < 2; ++row)
    {
        for (int column = 0; column < 3; ++column)
        {
            dst.texMatrix[row][column] = src.texMatrix[row][column];
        }
    }
}

int AppendSmokeSurfaceGeometry(
    const drawSurf_t* drawSurf,
    const srfTriangles_t* tri,
    uint32_t surfaceClassId,
    uint32_t materialId,
    int classCount,
    uint32_t triangleClassMask,
    uint32_t particleAlphaClassId,
    uint32_t forceGeometricNormalFlag,
    std::vector<PathTraceSmokeVertex>& vertices,
    std::vector<uint32_t>& indexes,
    std::vector<uint32_t>& triangleClasses,
    std::vector<uint32_t>& triangleMaterials,
    RtSmokeSurfaceSkipStats& skipStats,
    RtSmokeAttributeStats& attributeStats)
{
    const size_t vertexStart = vertices.size();
    const size_t indexStart = indexes.size();
    const size_t classStart = triangleClasses.size();
    const size_t materialStart = triangleMaterials.size();
    const uint32_t indexBase = static_cast<uint32_t>(vertices.size());
    const idJointMat* rtCpuSkinningJoints = GetSmokeRtCpuSkinningJoints(tri);
    const int classIndex = idMath::ClampInt(0, classCount - 1, static_cast<int>(surfaceClassId & triangleClassMask));
    const bool activeEmissiveStage = SmokeDrawSurfaceHasActiveEmissiveStage(drawSurf);
    const uint32_t perSurfaceTriangleFlags = activeEmissiveStage ? 0u : RT_SMOKE_TRIANGLE_EMISSIVE_STAGE_OFF;

    for (int vertexIndex = 0; vertexIndex < tri->numVerts; ++vertexIndex)
    {
        PathTraceSmokeVertex vertex = BuildSmokeSurfaceVertex(drawSurf, tri, vertexIndex, rtCpuSkinningJoints);
        const idVec3 normal = SmokeVertexNormal(vertex);
        const idVec2 texCoord = SmokeVertexTexCoord(vertex);
        if (!SmokeNormalIsUsable(normal))
        {
            ++attributeStats.classes[classIndex].invalidNormalVerts;
            vertex.normal[0] = 0.0f;
            vertex.normal[1] = 0.0f;
            vertex.normal[2] = 0.0f;
        }
        if (!SmokeTexCoordIsUsable(texCoord))
        {
            ++attributeStats.classes[classIndex].invalidUvVerts;
            vertex.texCoord[0] = 0.0f;
            vertex.texCoord[1] = 0.0f;
        }
        vertices.push_back(vertex);
    }

    for (int sourceIndex = 0; sourceIndex + 2 < tri->numIndexes; sourceIndex += 3)
    {
        const int i0 = tri->indexes[sourceIndex + 0];
        const int i1 = tri->indexes[sourceIndex + 1];
        const int i2 = tri->indexes[sourceIndex + 2];
        if (i0 < 0 || i1 < 0 || i2 < 0 || i0 >= tri->numVerts || i1 >= tri->numVerts || i2 >= tri->numVerts)
        {
            ++skipStats.invalidIndexCount;
            continue;
        }

        const PathTraceSmokeVertex& v0 = vertices[indexBase + static_cast<uint32_t>(i0)];
        const PathTraceSmokeVertex& v1 = vertices[indexBase + static_cast<uint32_t>(i1)];
        const PathTraceSmokeVertex& v2 = vertices[indexBase + static_cast<uint32_t>(i2)];
        const idVec3 p0 = SmokeVertexPosition(v0);
        const idVec3 p1 = SmokeVertexPosition(v1);
        const idVec3 p2 = SmokeVertexPosition(v2);
        if (IsZeroAreaSmokeTriangle(p0, p1, p2))
        {
            continue;
        }

        indexes.push_back(indexBase + static_cast<uint32_t>(i0));
        indexes.push_back(indexBase + static_cast<uint32_t>(i1));
        indexes.push_back(indexBase + static_cast<uint32_t>(i2));
        const bool invalidNormalTriangle =
            !SmokeNormalIsUsable(SmokeVertexNormal(v0)) ||
            !SmokeNormalIsUsable(SmokeVertexNormal(v1)) ||
            !SmokeNormalIsUsable(SmokeVertexNormal(v2));
        const bool invalidUvTriangle =
            !SmokeTexCoordIsUsable(SmokeVertexTexCoord(v0)) ||
            !SmokeTexCoordIsUsable(SmokeVertexTexCoord(v1)) ||
            !SmokeTexCoordIsUsable(SmokeVertexTexCoord(v2));
        const bool preferGeometricNormal =
            invalidNormalTriangle ||
            static_cast<uint32_t>(classIndex) == particleAlphaClassId;

        if (invalidNormalTriangle)
        {
            ++attributeStats.classes[classIndex].invalidNormalTriangles;
        }
        if (invalidUvTriangle)
        {
            ++attributeStats.classes[classIndex].invalidUvTriangles;
        }
        if (preferGeometricNormal)
        {
            ++attributeStats.classes[classIndex].forcedGeometricNormalTriangles;
        }

        triangleClasses.push_back(surfaceClassId | perSurfaceTriangleFlags | (preferGeometricNormal ? forceGeometricNormalFlag : 0u));
        triangleMaterials.push_back(materialId);
    }

    const int emittedIndexes = static_cast<int>(indexes.size() - indexStart);
    if (emittedIndexes <= 0 || triangleClasses.size() == classStart || triangleMaterials.size() == materialStart)
    {
        vertices.resize(vertexStart);
        indexes.resize(indexStart);
        triangleClasses.resize(classStart);
        triangleMaterials.resize(materialStart);
        ++skipStats.zeroAreaOnly;
        return 0;
    }

    return emittedIndexes;
}

void AddSmokeSkinnedSurfaceRecord(
    std::vector<RtSmokeSkinnedSurfaceRecord>* records,
    const drawSurf_t* drawSurf,
    const srfTriangles_t* tri,
    uint32_t surfaceClassId,
    uint32_t materialId,
    int bucketIndex,
    int currentVertexOffset,
    int currentIndexOffset,
    int currentTriangleOffset,
    int vertexCount,
    int indexCount,
    int triangleCount)
{
    if (!records || !drawSurf || !tri)
    {
        return;
    }

    const viewEntity_t* space = drawSurf->space;
    const idRenderEntityLocal* entityDef = space ? space->entityDef : nullptr;
    const renderEntity_t* renderEntity = entityDef ? &entityDef->parms : nullptr;

    RtSmokeSkinnedSurfaceRecord record;
    record.key.entityIndex = entityDef ? entityDef->index : -1;
    record.key.entityDef = reinterpret_cast<uintptr_t>(entityDef);
    record.key.model = reinterpret_cast<uintptr_t>(renderEntity ? renderEntity->hModel : nullptr);
    record.key.tri = reinterpret_cast<uintptr_t>(tri);
    record.key.materialId = materialId;
    record.key.surfaceClassId = surfaceClassId;
    record.currentVertexOffset = currentVertexOffset;
    record.currentIndexOffset = currentIndexOffset;
    record.currentTriangleOffset = currentTriangleOffset;
    record.vertexCount = vertexCount;
    record.indexCount = indexCount;
    record.triangleCount = triangleCount;
    record.rtCpuSkinned = GetSmokeRtCpuSkinningJoints(tri) != nullptr;
    record.basePoseLikely = SmokeSkinnedSurfaceLikelyBasePose(drawSurf, tri);
    record.entityIndex = record.key.entityIndex;
    record.materialId = materialId;
    record.jointCount = tri->staticModelWithJoints ? tri->staticModelWithJoints->numInvertedJoints : (renderEntity ? renderEntity->numJoints : 0);
    record.jointSource = reinterpret_cast<uintptr_t>(tri->staticModelWithJoints ? static_cast<const void*>(tri->staticModelWithJoints) : static_cast<const void*>(renderEntity ? renderEntity->joints : nullptr));
    record.bucketIndex = bucketIndex;
    if (renderEntity)
    {
        record.hasEntityOrigin = true;
        record.entityOrigin = renderEntity->origin;
    }
    if (space)
    {
        record.objectToWorld[0] = space->modelMatrix[0];
        record.objectToWorld[1] = space->modelMatrix[4];
        record.objectToWorld[2] = space->modelMatrix[8];
        record.objectToWorld[3] = space->modelMatrix[12];
        record.objectToWorld[4] = space->modelMatrix[1];
        record.objectToWorld[5] = space->modelMatrix[5];
        record.objectToWorld[6] = space->modelMatrix[9];
        record.objectToWorld[7] = space->modelMatrix[13];
        record.objectToWorld[8] = space->modelMatrix[2];
        record.objectToWorld[9] = space->modelMatrix[6];
        record.objectToWorld[10] = space->modelMatrix[10];
        record.objectToWorld[11] = space->modelMatrix[14];
    }
    else
    {
        record.objectToWorld[0] = 1.0f;
        record.objectToWorld[5] = 1.0f;
        record.objectToWorld[10] = 1.0f;
    }

    records->push_back(record);
}

void FinalizeSmokeSkinnedSurfaceRecordOffsets(
    std::vector<RtSmokeSkinnedSurfaceRecord>* records,
    int bucketIndex,
    const RtSmokeBucketRange& range)
{
    if (!records)
    {
        return;
    }

    for (RtSmokeSkinnedSurfaceRecord& record : *records)
    {
        if (record.bucketIndex != bucketIndex)
        {
            continue;
        }
        record.currentVertexOffset += range.vertexOffset;
        record.currentIndexOffset += range.indexOffset;
        record.currentTriangleOffset += range.triangleOffset;
    }
}

namespace {

void AddSmokeMaterialStats(RtSmokeMaterialStats& stats, const idMaterial* material, int indexes, RtSmokeSurfaceClass surfaceClass, RtSmokeTranslucentSubtype translucentSubtype)
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

    for (int sampleIndex = 0; sampleIndex < stats.sampleCount; ++sampleIndex)
    {
        RtSmokeMaterialSample& sample = stats.samples[sampleIndex];
        if (sample.id == materialId)
        {
            ++sample.surfaces;
            sample.triangles += indexes / 3;
            return;
        }
    }

    if (stats.sampleCount < RT_SMOKE_MATERIAL_REASON_SAMPLES)
    {
        RtSmokeMaterialSample& sample = stats.samples[stats.sampleCount];
        sample.id = materialId;
        sample.surfaces = 1;
        sample.triangles = indexes / 3;
        sample.name = materialName;
        ++stats.sampleCount;
    }

    if (surfaceClass != RtSmokeSurfaceClass::ParticleAlpha)
    {
        return;
    }

    const int subtypeIndex = idMath::ClampInt(0, RT_SMOKE_TRANSLUCENT_SUBTYPE_COUNT - 1, static_cast<int>(SmokeTranslucentSubtypeId(translucentSubtype)));
    ++stats.translucentSubtypeSurfaces[subtypeIndex];
    stats.translucentSubtypeTriangles[subtypeIndex] += indexes / 3;

    bool subtypeSampleFound = false;
    for (int sampleIndex = 0; sampleIndex < stats.translucentSubtypeSampleCounts[subtypeIndex]; ++sampleIndex)
    {
        RtSmokeMaterialSample& sample = stats.translucentSubtypeSamples[subtypeIndex][sampleIndex];
        if (sample.id == materialId)
        {
            ++sample.surfaces;
            sample.triangles += indexes / 3;
            subtypeSampleFound = true;
            break;
        }
    }

    if (!subtypeSampleFound && stats.translucentSubtypeSampleCounts[subtypeIndex] < RT_SMOKE_MATERIAL_REASON_SAMPLES)
    {
        RtSmokeMaterialSample& sample = stats.translucentSubtypeSamples[subtypeIndex][stats.translucentSubtypeSampleCounts[subtypeIndex]];
        sample.id = materialId;
        sample.surfaces = 1;
        sample.triangles = indexes / 3;
        sample.name = materialName;
        ++stats.translucentSubtypeSampleCounts[subtypeIndex];
    }

    ++stats.translucentSurfaces;
    stats.translucentTriangles += indexes / 3;
    const bool firstTranslucentMaterial = std::find(stats.translucentMaterialIds.begin(), stats.translucentMaterialIds.end(), materialId) == stats.translucentMaterialIds.end();
    if (firstTranslucentMaterial)
    {
        stats.translucentMaterialIds.push_back(materialId);
        ++stats.translucentUniqueMaterials;
    }

    for (int sampleIndex = 0; sampleIndex < stats.translucentSampleCount; ++sampleIndex)
    {
        RtSmokeMaterialSample& sample = stats.translucentSamples[sampleIndex];
        if (sample.id == materialId)
        {
            ++sample.surfaces;
            sample.triangles += indexes / 3;
            return;
        }
    }

    if (stats.translucentSampleCount < RT_SMOKE_MATERIAL_REASON_SAMPLES)
    {
        RtSmokeMaterialSample& sample = stats.translucentSamples[stats.translucentSampleCount];
        sample.id = materialId;
        sample.surfaces = 1;
        sample.triangles = indexes / 3;
        sample.name = materialName;
        ++stats.translucentSampleCount;
    }
}

bool SmokeEvalRegister(const float* regs, int registerCount, int registerIndex, float fallback, float& value)
{
    if (regs && registerIndex >= 0 && registerIndex < registerCount)
    {
        value = regs[registerIndex];
        return true;
    }
    value = fallback;
    return false;
}

bool SmokeStageUsesPerSurfaceMaterialState(const idMaterial* material, const shaderStage_t* stage)
{
    if (!material || !stage)
    {
        return false;
    }

    const bool materialUsesRuntimeRegisters = material->ConstantRegisters() == nullptr;
    if (materialUsesRuntimeRegisters && stage->conditionRegister >= 0)
    {
        return true;
    }
    if (materialUsesRuntimeRegisters && stage->hasAlphaTest && stage->alphaTestRegister >= 0)
    {
        return true;
    }
    if (materialUsesRuntimeRegisters)
    {
        for (int component = 0; component < 4; ++component)
        {
            if (stage->color.registers[component] >= 0)
            {
                return true;
            }
        }
        if (stage->texture.hasMatrix)
        {
            return true;
        }
    }
    return stage->texture.dynamic != DI_STATIC ||
        stage->texture.dynamicFrameCount > 0 ||
        stage->texture.cinematic != nullptr ||
        stage->texture.texgen == TG_SCREEN ||
        stage->texture.texgen == TG_SCREEN2 ||
        stage->newStage != nullptr;
}

void AddSmokeDynamicMaterialEvalStats(RtSmokeMaterialStats& stats, const drawSurf_t* drawSurf, int indexes)
{
    const idMaterial* material = drawSurf ? drawSurf->material : nullptr;
    if (!material)
    {
        return;
    }

    const float* regs = drawSurf->shaderRegisters ? drawSurf->shaderRegisters : material->ConstantRegisters();
    if (!regs)
    {
        return;
    }

    const int registerCount = material->GetNumRegisters();
    RtSmokeDynamicMaterialEvalSample surfaceSample;
    surfaceSample.valid = false;
    surfaceSample.id = SmokeMaterialId(material);
    surfaceSample.name = material->GetName();

    for (int stageIndex = 0; stageIndex < material->GetNumStages(); ++stageIndex)
    {
        const shaderStage_t* stage = material->GetStage(stageIndex);
        if (!SmokeStageUsesPerSurfaceMaterialState(material, stage))
        {
            continue;
        }

        float condition = 1.0f;
        SmokeEvalRegister(regs, registerCount, stage->conditionRegister, 1.0f, condition);
        const bool enabled = condition != 0.0f;
        ++surfaceSample.enabledStages;
        if (!enabled)
        {
            --surfaceSample.enabledStages;
            ++surfaceSample.disabledStages;
        }

        bool hasColor = false;
        float color[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
        for (int component = 0; component < 4; ++component)
        {
            hasColor = SmokeEvalRegister(regs, registerCount, stage->color.registers[component], 1.0f, color[component]) || hasColor;
        }
        if (hasColor)
        {
            ++surfaceSample.colorStages;
        }
        if (hasColor && idMath::Fabs(color[3] - 1.0f) > 1.0e-4f)
        {
            ++surfaceSample.alphaStages;
        }

        float alphaTest = 0.0f;
        const bool hasAlphaTest = stage->hasAlphaTest &&
            SmokeEvalRegister(regs, registerCount, stage->alphaTestRegister, 0.0f, alphaTest);
        if (hasAlphaTest)
        {
            ++surfaceSample.alphaTestStages;
        }

        bool hasTexMatrix = false;
        float texMatrix[2][3] = { { 1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f } };
        if (stage->texture.hasMatrix)
        {
            hasTexMatrix = true;
            ++surfaceSample.texMatrixStages;
            for (int row = 0; row < 2; ++row)
            {
                for (int column = 0; column < 3; ++column)
                {
                    const float fallback = row == column ? 1.0f : 0.0f;
                    SmokeEvalRegister(regs, registerCount, stage->texture.matrix[row][column], fallback, texMatrix[row][column]);
                }
            }
        }
        if (stage->texture.dynamic != DI_STATIC || stage->texture.dynamicFrameCount > 0)
        {
            ++surfaceSample.dynamicImageStages;
        }
        if (stage->texture.cinematic != nullptr)
        {
            ++surfaceSample.cinematicStages;
        }
        if (stage->texture.texgen == TG_SCREEN ||
            stage->texture.texgen == TG_SCREEN2 ||
            stage->texture.dynamic == DI_GUI_RENDER ||
            stage->texture.dynamic == DI_RENDER_TARGET)
        {
            ++surfaceSample.guiRenderTargetStages;
        }
        if (stage->newStage != nullptr)
        {
            ++surfaceSample.programStages;
        }

        const int stagePriority = (enabled ? 2 : 0) + (SmokeDynamicEvalStageIsEmissiveLike(stage) ? 1 : 0);
        RtSmokeDynamicMaterialEvalSample stageSample;
        stageSample.valid = true;
        stageSample.stageIndex = stageIndex;
        stageSample.stagePriority = stagePriority;
        stageSample.condition = condition;
        stageSample.alphaTest = alphaTest;
        for (int component = 0; component < 4; ++component)
        {
            stageSample.color[component] = color[component];
        }
        if (hasTexMatrix)
        {
            for (int row = 0; row < 2; ++row)
            {
                for (int column = 0; column < 3; ++column)
                {
                    stageSample.texMatrix[row][column] = texMatrix[row][column];
                }
            }
        }
        if (SmokeDynamicEvalSampleShouldReplace(surfaceSample, stageSample))
        {
            SmokeDynamicEvalCopySelectedStage(surfaceSample, stageSample);
        }
    }

    if (!surfaceSample.valid)
    {
        return;
    }

    ++stats.dynamicEvalSurfaces;
    stats.dynamicEvalTriangles += indexes / 3;
    stats.dynamicEvalStages += surfaceSample.enabledStages + surfaceSample.disabledStages;
    stats.dynamicEvalEnabledStages += surfaceSample.enabledStages;
    stats.dynamicEvalDisabledStages += surfaceSample.disabledStages;
    stats.dynamicEvalColorStages += surfaceSample.colorStages;
    stats.dynamicEvalAlphaStages += surfaceSample.alphaStages;
    stats.dynamicEvalAlphaTestStages += surfaceSample.alphaTestStages;
    stats.dynamicEvalTexMatrixStages += surfaceSample.texMatrixStages;
    stats.dynamicEvalDynamicImageStages += surfaceSample.dynamicImageStages;
    stats.dynamicEvalCinematicStages += surfaceSample.cinematicStages;
    stats.dynamicEvalGuiRenderTargetStages += surfaceSample.guiRenderTargetStages;
    stats.dynamicEvalProgramStages += surfaceSample.programStages;

    for (int sampleIndex = 0; sampleIndex < stats.dynamicEvalSampleCount; ++sampleIndex)
    {
        RtSmokeDynamicMaterialEvalSample& sample = stats.dynamicEvalSamples[sampleIndex];
        if (sample.id == surfaceSample.id)
        {
            ++sample.surfaces;
            sample.triangles += indexes / 3;
            sample.enabledStages += surfaceSample.enabledStages;
            sample.disabledStages += surfaceSample.disabledStages;
            sample.colorStages += surfaceSample.colorStages;
            sample.alphaStages += surfaceSample.alphaStages;
            sample.alphaTestStages += surfaceSample.alphaTestStages;
            sample.texMatrixStages += surfaceSample.texMatrixStages;
            sample.dynamicImageStages += surfaceSample.dynamicImageStages;
            sample.cinematicStages += surfaceSample.cinematicStages;
            sample.guiRenderTargetStages += surfaceSample.guiRenderTargetStages;
            sample.programStages += surfaceSample.programStages;
            if (SmokeDynamicEvalSampleShouldReplace(sample, surfaceSample))
            {
                SmokeDynamicEvalCopySelectedStage(sample, surfaceSample);
            }
            return;
        }
    }

    if (stats.dynamicEvalSampleCount < RT_SMOKE_DYNAMIC_MATERIAL_REASON_SAMPLES)
    {
        RtSmokeDynamicMaterialEvalSample& sample = stats.dynamicEvalSamples[stats.dynamicEvalSampleCount++];
        sample = surfaceSample;
        sample.surfaces = 1;
        sample.triangles = indexes / 3;
    }
}

void AddSmokeTranslucentDebugSample(RtSmokeMaterialStats& stats, const drawSurf_t* drawSurf, const srfTriangles_t* tri, int surfaceIndex, RtSmokeTranslucentSubtype subtype)
{
    if (stats.translucentDebugSampleCount >= RT_SMOKE_TRANSLUCENT_REASON_SAMPLES)
    {
        return;
    }

    const idMaterial* material = drawSurf ? drawSurf->material : nullptr;
    RtSmokeTranslucentSubtypeDebugSample& sample = stats.translucentDebugSamples[stats.translucentDebugSampleCount++];
    sample.valid = true;
    sample.subtype = subtype;
    sample.surfaceIndex = surfaceIndex;
    sample.verts = tri ? tri->numVerts : 0;
    sample.indexes = tri ? tri->numIndexes : 0;
    sample.materialName = material ? material->GetName() : "<none>";
    sample.coverage = material ? material->Coverage() : MC_BAD;
    sample.sort = material ? material->GetSort() : SS_BAD;
    sample.deform = material ? material->Deform() : DFRM_NONE;
    sample.info = BuildSmokeTranslucentClassifierInfo(material);
}

RtSmokeSurfaceClassReason BuildSmokeSurfaceClassReason(const viewDef_t* viewDef, const drawSurf_t* drawSurf, const srfTriangles_t* tri, int surfaceIndex, RtSmokeSurfaceClass surfaceClass)
{
    RtSmokeSurfaceClassReason reason;
    reason.valid = true;
    reason.finalClass = surfaceClass;
    reason.surfaceIndex = surfaceIndex;
    reason.verts = tri ? tri->numVerts : 0;
    reason.indexes = tri ? tri->numIndexes : 0;

    const viewEntity_t* space = drawSurf ? drawSurf->space : nullptr;
    const idRenderEntityLocal* entityDef = space ? space->entityDef : nullptr;
    const renderEntity_t* renderEntity = entityDef ? &entityDef->parms : nullptr;
    const idMaterial* material = drawSurf ? drawSurf->material : nullptr;
    const idRenderModel* model = renderEntity ? renderEntity->hModel : nullptr;

    reason.hasEntityDef = entityDef != nullptr;
    reason.isWorldSpace = viewDef && space == &viewDef->worldSpace;
    reason.materialName = material ? material->GetName() : "<none>";
    reason.coverage = material ? material->Coverage() : MC_BAD;
    reason.sort = material ? material->GetSort() : SS_BAD;
    reason.deform = material ? material->Deform() : DFRM_NONE;
    reason.entityNum = renderEntity ? renderEntity->entityNum : -1;
    reason.modelName = model ? model->Name() : "<none>";
    reason.dynamicModel = model ? model->IsDynamicModel() : DM_STATIC;
    reason.hasJointCache = drawSurf && drawSurf->jointCache != 0;
    reason.hasStaticModelWithJoints = tri && tri->staticModelWithJoints != nullptr;
    reason.hasRenderEntityJoints = renderEntity && renderEntity->joints != nullptr && renderEntity->numJoints > 0;
    reason.ambientCacheStatic = drawSurf && idVertexCache::CacheIsStatic(drawSurf->ambientCache);
    reason.indexCacheStatic = drawSurf && idVertexCache::CacheIsStatic(drawSurf->indexCache);
    reason.isStaticWorldModel = model && model->IsStaticWorldModel();
    reason.hasDynamicModel = entityDef && entityDef->dynamicModel != nullptr;
    reason.hasCachedDynamicModel = entityDef && entityDef->cachedDynamicModel != nullptr;
    reason.hasCallback = renderEntity && renderEntity->callback != nullptr;
    reason.forceUpdate = renderEntity && renderEntity->forceUpdate != 0;
    reason.weaponDepthHack = renderEntity && renderEntity->weaponDepthHack;
    reason.modelDepthHack = renderEntity ? renderEntity->modelDepthHack : (space ? space->modelDepthHack : 0.0f);
    reason.cpuVertsAvailable = tri && tri->verts != nullptr;
    reason.cpuVertexCacheCurrent = tri && tri->ambientCache != 0 && vertexCache.CacheIsCurrent(tri->ambientCache);
    reason.cpuIndexCacheCurrent = tri && tri->indexCache != 0 && vertexCache.CacheIsCurrent(tri->indexCache);
    reason.skinnedLikelyBasePose = surfaceClass == RtSmokeSurfaceClass::SkinnedDeformed && SmokeSkinnedSurfaceLikelyBasePose(drawSurf, tri);
    reason.rtCpuSkinned = surfaceClass == RtSmokeSurfaceClass::SkinnedDeformed && GetSmokeRtCpuSkinningJoints(tri) != nullptr;
    if (renderEntity)
    {
        reason.entityOrigin = renderEntity->origin;
        reason.entityAxis = renderEntity->axis;
        reason.entityBounds = renderEntity->bounds;
        reason.hasEntityBounds = true;
    }
    if (tri)
    {
        reason.surfaceBounds = tri->bounds;
        reason.hasSurfaceBounds = true;
    }
    if (entityDef)
    {
        reason.localReferenceBounds = entityDef->localReferenceBounds;
        reason.globalReferenceBounds = entityDef->globalReferenceBounds;
        reason.hasReferenceBounds = true;
    }

    return reason;
}

void AddSmokeDynamicGeometryStats(RtSmokeDynamicGeometryStats& stats, RtSmokeSurfaceClass surfaceClass, const drawSurf_t* drawSurf, const srfTriangles_t* tri, int indexes)
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

void AddSmokeSurfaceClassStats(RtSmokeSurfaceClassStats& stats, RtSmokeSurfaceClass surfaceClass, int verts, int indexes)
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

void AddSmokeSurfaceClassReasonSample(RtSmokeSurfaceClassReasonSamples& samples, const RtSmokeSurfaceClassReason& reason)
{
    if (reason.finalClass == RtSmokeSurfaceClass::SkinnedDeformed && samples.skinnedCount < RT_SMOKE_CLASS_REASON_SAMPLES)
    {
        samples.skinnedSamples[samples.skinnedCount] = reason;
        ++samples.skinnedCount;
    }

    const int classIndex = static_cast<int>(SmokeSurfaceClassId(reason.finalClass));
    if (classIndex < 0 || classIndex >= RT_SMOKE_CLASS_COUNT)
    {
        return;
    }

    if (samples.counts[classIndex] >= RT_SMOKE_CLASS_REASON_SAMPLES)
    {
        return;
    }

    samples.samples[classIndex][samples.counts[classIndex]] = reason;
    ++samples.counts[classIndex];
}

uint64 BuildSmokeStaticSurfaceKey(const drawSurf_t* drawSurf, const srfTriangles_t* tri)
{
    uint64 hash = 14695981039346656037ull;
    const uintptr_t triPtr = reinterpret_cast<uintptr_t>(tri);
    const uintptr_t materialPtr = reinterpret_cast<uintptr_t>(drawSurf ? drawSurf->material : nullptr);
    hash = HashSmokeBytes(hash, &triPtr, sizeof(triPtr));
    hash = HashSmokeBytes(hash, &materialPtr, sizeof(materialPtr));
    if (tri)
    {
        hash = HashSmokeBytes(hash, &tri->numVerts, sizeof(tri->numVerts));
        hash = HashSmokeBytes(hash, &tri->numIndexes, sizeof(tri->numIndexes));
        hash = HashSmokeBytes(hash, &tri->ambientCache, sizeof(tri->ambientCache));
        hash = HashSmokeBytes(hash, &tri->indexCache, sizeof(tri->indexCache));
    }
    if (drawSurf && drawSurf->space)
    {
        hash = HashSmokeBytes(hash, drawSurf->space->modelMatrix, sizeof(drawSurf->space->modelMatrix));
    }
    return hash;
}

struct RtSmokeCapturedDynamicSurfaceKey
{
    int entityIndex = -1;
    const srfTriangles_t* tri = nullptr;
    uint32_t materialId = 0;
};

bool SmokeCapturedDynamicSurfaceMatches(const RtSmokeCapturedDynamicSurfaceKey& key, int entityIndex, const srfTriangles_t* tri, uint32_t materialId)
{
    return key.entityIndex == entityIndex && key.tri == tri && key.materialId == materialId;
}

bool SmokeDynamicSurfaceAlreadyCaptured(const std::vector<RtSmokeCapturedDynamicSurfaceKey>& capturedSurfaces, int entityIndex, const srfTriangles_t* tri, uint32_t materialId)
{
    for (const RtSmokeCapturedDynamicSurfaceKey& key : capturedSurfaces)
    {
        if (SmokeCapturedDynamicSurfaceMatches(key, entityIndex, tri, materialId))
        {
            return true;
        }
    }
    return false;
}

bool SmokeBoundsWithinRadius(const idBounds& bounds, const idVec3& origin, float radius)
{
    if (radius <= 0.0f || bounds.IsCleared())
    {
        return false;
    }

    const idVec3 center = bounds.GetCenter();
    const float expandedRadius = radius + bounds.GetRadius(center);
    return (center - origin).LengthSqr() <= expandedRadius * expandedRadius;
}

const idMaterial* SmokeResolveEntitySurfaceMaterial(const idRenderEntityLocal* entityDef, const modelSurface_t* surface)
{
    const idMaterial* shader = surface ? surface->shader : nullptr;
    if (!entityDef || !shader)
    {
        return shader;
    }

    if (entityDef->parms.customShader != nullptr)
    {
        if (shader->Deform())
        {
            return nullptr;
        }
        return entityDef->parms.customShader;
    }

    if (entityDef->parms.customSkin)
    {
        shader = entityDef->parms.customSkin->RemapShaderBySkin(shader);
    }

    return shader;
}

}

uint64 BuildSmokeStaticSurfaceKeyForDiagnostics(const drawSurf_t* drawSurf, const srfTriangles_t* tri)
{
    return BuildSmokeStaticSurfaceKey(drawSurf, tri);
}

bool CaptureDoomSurfacesForSmokeTest(const viewDef_t* viewDef, std::vector<PathTraceSmokeVertex>& vertexData, std::vector<uint32_t>& indexData, std::vector<uint32_t>& triangleClassData, std::vector<uint32_t>& triangleMaterialData, RtSmokeGeometryUniverse& geometryUniverse, bool& staticCacheChanged, idVec3& sceneOrigin, int& sourceSurfaces, int& sourceVerts, int& sourceIndexes, int& anchorTriangle, RtSmokeSurfaceClassStats& classStats, RtSmokeSurfaceSkipStats& skipStats, RtSmokeDynamicGeometryStats& dynamicStats, RtSmokeAttributeStats& attributeStats, RtSmokeMaterialStats& materialStats, RtSmokeBucketRanges& bucketRanges, RtSmokeSceneCaptureTiming& captureTiming, RtSmokeSurfaceClassReasonSamples* reasonSamples, std::vector<RtSmokeSkinnedSurfaceRecord>* skinnedSurfaceRecords, bool skipStaticWorldCapture, bool skipPromotedStaticSurfaceCapture, bool skipDynamicCapture)
{
    OPTICK_EVENT("PT Capture Doom Surfaces Detail");

    sourceSurfaces = 0;
    sourceVerts = 0;
    sourceIndexes = 0;
    staticCacheChanged = false;
    int anchorSurface = -1;
    anchorTriangle = -1;
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

    if (!viewDef || !viewDef->drawSurfs)
    {
        return false;
    }

    const int anchorStartMs = Sys_Milliseconds();
    {
        OPTICK_EVENT("PT Capture Anchor");
        if (r_pathTracingAnchorRaycast.GetInteger() == 0)
        {
            sceneOrigin = viewDef->renderView.vieworg;
            anchorSurface = 0;
        }
        else if (!FindCenterCameraRayAnchor(viewDef, sceneOrigin, anchorSurface, anchorTriangle, &captureTiming))
        {
            captureTiming.anchorMs = Sys_Milliseconds() - anchorStartMs;
            return false;
        }
    }
    captureTiming.anchorMs = Sys_Milliseconds() - anchorStartMs;

    {
        OPTICK_EVENT("PT Capture Reserve Buffers");
        vertexData.clear();
        indexData.clear();
        triangleClassData.clear();
        triangleMaterialData.clear();
        vertexData.reserve(RT_SMOKE_MAX_VERTS);
        indexData.reserve(RT_SMOKE_MAX_INDEXES);
        triangleClassData.reserve(RT_SMOKE_MAX_INDEXES / 3);
        triangleMaterialData.reserve(RT_SMOKE_MAX_INDEXES / 3);
    }
    std::vector<PathTraceSmokeVertex>& staticVertexCache = geometryUniverse.StaticVertices();
    std::vector<uint32_t>& staticIndexCache = geometryUniverse.StaticIndexes();
    std::vector<uint32_t>& staticTriangleClassCache = geometryUniverse.StaticTriangleClasses();
    std::vector<uint32_t>& staticTriangleMaterialCache = geometryUniverse.StaticTriangleMaterials();
    geometryUniverse.ReserveStaticSurfaceRecords(geometryUniverse.StaticSurfaceRecords().size() + static_cast<size_t>(viewDef->numDrawSurfs));

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
    std::vector<RtSmokeCapturedDynamicSurfaceKey> capturedDynamicSurfaces;
    capturedDynamicSurfaces.reserve(static_cast<size_t>(viewDef->numDrawSurfs));

    {
        OPTICK_EVENT("PT Capture Static Pass");
        for (int surfaceIndex = 0; !skipStaticWorldCapture && surfaceIndex < viewDef->numDrawSurfs; ++surfaceIndex)
        {
            const drawSurf_t* drawSurf = viewDef->drawSurfs[surfaceIndex];
            const srfTriangles_t* tri = nullptr;
            const int validationStartMs = Sys_Milliseconds();
            if (!ValidateSmokeDrawSurface(viewDef, drawSurf, tri, nullptr))
            {
                captureTiming.validationMs += Sys_Milliseconds() - validationStartMs;
                continue;
            }
            captureTiming.validationMs += Sys_Milliseconds() - validationStartMs;

            const int classifyStartMs = Sys_Milliseconds();
            const RtSmokeSurfaceClass surfaceClass = ClassifySmokeSurface(viewDef, drawSurf, tri);
            captureTiming.staticPassClassifyMs += Sys_Milliseconds() - classifyStartMs;
            if (surfaceClass != RtSmokeSurfaceClass::StaticWorld)
            {
                continue;
            }

            const RtSmokeTranslucentSubtype translucentSubtype = RtSmokeTranslucentSubtype::Unknown;
            const uint32_t surfaceClassId = SmokeSurfaceClassAndSubtypeId(surfaceClass, translucentSubtype);
            const uint32_t materialId = SmokeMaterialId(drawSurf->material);
            const uint64 staticSurfaceKey = BuildSmokeStaticSurfaceKey(drawSurf, tri);
            const int cacheLookupStartMs = Sys_Milliseconds();
            RtSmokePersistentStaticSurfaceRecord* staticSurfaceRecord = geometryUniverse.TouchStaticSurface(staticSurfaceKey);
            captureTiming.staticCacheLookupMs += Sys_Milliseconds() - cacheLookupStartMs;
            ++sourceSurfaces;
            ++bucketRanges.buckets[0].surfaceCount;
            AddSmokeMaterialStats(materialStats, drawSurf->material, tri->numIndexes, surfaceClass, translucentSubtype);
            AddSmokeDynamicMaterialEvalStats(materialStats, drawSurf, tri->numIndexes);

            if (staticSurfaceRecord)
            {
                ++captureTiming.staticCachedSurfaces;
                sourceVerts += tri->numVerts;
                sourceIndexes += tri->numIndexes;
                AddSmokeSurfaceClassStats(classStats, surfaceClass, tri->numVerts, tri->numIndexes);
                if (reasonSamples)
                {
                    AddSmokeSurfaceClassReasonSample(*reasonSamples, BuildSmokeSurfaceClassReason(viewDef, drawSurf, tri, surfaceIndex, surfaceClass));
                }
                continue;
            }

            if (!geometryUniverse.CanAppendStaticSurface(tri->numVerts, tri->numIndexes, RT_SMOKE_MAX_VERTS, RT_SMOKE_MAX_INDEXES))
            {
                ++skipStats.limitExceeded;
                continue;
            }

            const RtSmokeStaticSurfaceAppend staticAppend = geometryUniverse.BeginStaticSurfaceAppend(staticSurfaceKey, surfaceClassId, materialId, tri->numVerts, tri->numIndexes);
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
                staticVertexCache,
                staticIndexCache,
                staticTriangleClassCache,
                staticTriangleMaterialCache,
                skipStats,
                attributeStats);
            const int appendMs = Sys_Milliseconds() - appendStartMs;
            captureTiming.staticAppendMs += appendMs;
            captureTiming.appendMs += appendMs;
            if (emittedIndexes <= 0)
            {
                continue;
            }

            ++captureTiming.staticNewSurfaces;
            geometryUniverse.CompleteStaticSurfaceAppend(staticAppend, emittedIndexes);
            staticCacheChanged = true;
            sourceVerts += tri->numVerts;
            sourceIndexes += emittedIndexes;
            AddSmokeSurfaceClassStats(classStats, surfaceClass, tri->numVerts, emittedIndexes);
            if (reasonSamples)
            {
                AddSmokeSurfaceClassReasonSample(*reasonSamples, BuildSmokeSurfaceClassReason(viewDef, drawSurf, tri, surfaceIndex, surfaceClass));
            }
        }
    }

    if (!skipDynamicCapture)
    {
        OPTICK_EVENT("PT Capture Dynamic Pass");
        for (int surfaceOffset = 0; surfaceOffset < viewDef->numDrawSurfs; ++surfaceOffset)
        {
            const int surfaceIndex = (anchorSurface + surfaceOffset) % viewDef->numDrawSurfs;
            const drawSurf_t* drawSurf = viewDef->drawSurfs[surfaceIndex];
            const srfTriangles_t* tri = nullptr;
            const int validationStartMs = Sys_Milliseconds();
            if (!ValidateSmokeDrawSurface(viewDef, drawSurf, tri, &skipStats))
            {
                captureTiming.validationMs += Sys_Milliseconds() - validationStartMs;
                continue;
            }
            captureTiming.validationMs += Sys_Milliseconds() - validationStartMs;

            if (dynamicSurfaces >= RT_SMOKE_MAX_SURFACES)
            {
                ++skipStats.limitExceeded;
                break;
            }

            const int classifyStartMs = Sys_Milliseconds();
            const RtSmokeSurfaceClass surfaceClass = ClassifySmokeSurface(viewDef, drawSurf, tri);
            captureTiming.dynamicPassClassifyMs += Sys_Milliseconds() - classifyStartMs;
            const RtSmokeTranslucentSubtype translucentSubtype = surfaceClass == RtSmokeSurfaceClass::ParticleAlpha ? ClassifySmokeTranslucentSubtype(drawSurf) : RtSmokeTranslucentSubtype::Unknown;
            const uint32_t surfaceClassId = SmokeSurfaceClassAndSubtypeId(surfaceClass, translucentSubtype);
            const uint32_t materialId = SmokeMaterialId(drawSurf->material);
            const int entityIndex = (drawSurf->space && drawSurf->space->entityDef) ? drawSurf->space->entityDef->index : -1;
            const int bucketIndex = idMath::ClampInt(0, RT_SMOKE_CLASS_COUNT - 1, static_cast<int>(surfaceClassId & RT_SMOKE_TRIANGLE_CLASS_MASK));
            const bool isStaticWorld = surfaceClass == RtSmokeSurfaceClass::StaticWorld;
            if (isStaticWorld)
            {
                continue;
            }
            if (skipPromotedStaticSurfaceCapture && geometryUniverse.HasStaticSurface(BuildSmokeStaticSurfaceKey(drawSurf, tri)))
            {
                continue;
            }

            if (dynamicVerts + tri->numVerts > RT_SMOKE_MAX_VERTS ||
                dynamicIndexes + tri->numIndexes > RT_SMOKE_MAX_INDEXES)
            {
                ++skipStats.limitExceeded;
                continue;
            }

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

            AddSmokeMaterialStats(materialStats, drawSurf->material, emittedIndexes, surfaceClass, translucentSubtype);
            AddSmokeDynamicMaterialEvalStats(materialStats, drawSurf, emittedIndexes);
            if (surfaceClass == RtSmokeSurfaceClass::ParticleAlpha)
            {
                AddSmokeTranslucentDebugSample(materialStats, drawSurf, tri, surfaceIndex, translucentSubtype);
            }
            ++sourceSurfaces;
            ++dynamicSurfaces;
            sourceVerts += tri->numVerts;
            sourceIndexes += emittedIndexes;
            AddSmokeSurfaceClassStats(classStats, surfaceClass, tri->numVerts, emittedIndexes);
            AddSmokeDynamicGeometryStats(dynamicStats, surfaceClass, drawSurf, tri, emittedIndexes);
            ++bucketRanges.buckets[bucketIndex].surfaceCount;
            if (entityIndex >= 0)
            {
                RtSmokeCapturedDynamicSurfaceKey capturedKey;
                capturedKey.entityIndex = entityIndex;
                capturedKey.tri = tri;
                capturedKey.materialId = materialId;
                capturedDynamicSurfaces.push_back(capturedKey);
            }
            dynamicVerts += tri->numVerts;
            dynamicIndexes += emittedIndexes;
            if (reasonSamples)
            {
                AddSmokeSurfaceClassReasonSample(*reasonSamples, BuildSmokeSurfaceClassReason(viewDef, drawSurf, tri, surfaceIndex, surfaceClass));
            }
        }
    }

    if (!skipDynamicCapture)
    {
        OPTICK_EVENT("PT Capture Nearby Dynamic Occluders");
        const int retentionRadius = idMath::ClampInt(0, 8192, r_pathTracingDynamicOccluderRadius.GetInteger());
        const int retainedSurfaceLimit = idMath::ClampInt(0, RT_SMOKE_MAX_SURFACES, r_pathTracingDynamicOccluderMaxSurfaces.GetInteger());
        int retainedSurfaces = 0;
        if (retentionRadius > 0 && retainedSurfaceLimit > 0 && viewDef->renderWorld)
        {
            const float retentionRadiusFloat = static_cast<float>(retentionRadius);
            const float retentionRadiusSqr = retentionRadiusFloat * retentionRadiusFloat;
            idRenderWorldLocal* renderWorld = viewDef->renderWorld;
            for (int entityIndex = 0; entityIndex < renderWorld->entityDefs.Num(); ++entityIndex)
            {
                idRenderEntityLocal* entityDef = renderWorld->entityDefs[entityIndex];
                const renderEntity_t* renderEntity = entityDef ? &entityDef->parms : nullptr;
                idRenderModel* model = renderEntity ? renderEntity->hModel : nullptr;
                if (!entityDef || !renderEntity || !model || model->IsStaticWorldModel())
                {
                    continue;
                }

                // This pass is deliberately limited to rigid entities backed by ordinary model surfaces.
                // Skinned/callback/continuous effects need identity and motion handling before retention is trustworthy.
                if (model->IsDynamicModel() != DM_STATIC)
                {
                    continue;
                }
                if (renderEntity->callback && renderEntity->customShader != nullptr && r_pathTracingSkipCallbackEntities.GetInteger() != 0)
                {
                    continue;
                }

                if (!SmokeBoundsWithinRadius(entityDef->globalReferenceBounds, viewDef->renderView.vieworg, retentionRadiusFloat) &&
                    (renderEntity->origin - viewDef->renderView.vieworg).LengthSqr() > retentionRadiusSqr)
                {
                    continue;
                }

                viewEntity_t retainedSpace = {};
                retainedSpace.entityDef = entityDef;
                retainedSpace.weaponDepthHack = renderEntity->weaponDepthHack;
                retainedSpace.modelDepthHack = renderEntity->modelDepthHack;
                memcpy(retainedSpace.modelMatrix, entityDef->modelMatrix, sizeof(retainedSpace.modelMatrix));
                R_MatrixMultiply(entityDef->modelMatrix, viewDef->worldSpace.modelViewMatrix, retainedSpace.modelViewMatrix);

                for (int surfaceIndex = 0; surfaceIndex < model->NumSurfaces(); ++surfaceIndex)
                {
                    if (retainedSurfaces >= retainedSurfaceLimit || dynamicSurfaces >= RT_SMOKE_MAX_SURFACES)
                    {
                        break;
                    }

                    const modelSurface_t* surface = model->Surface(surfaceIndex);
                    const srfTriangles_t* tri = surface ? surface->geometry : nullptr;
                    const idMaterial* shader = SmokeResolveEntitySurfaceMaterial(entityDef, surface);
                    if (!tri || !tri->verts || !tri->indexes || tri->numVerts < 3 || tri->numIndexes < 3 || !shader || !shader->IsDrawn())
                    {
                        continue;
                    }
                    if ((tri->numIndexes % 3) != 0)
                    {
                        ++skipStats.invalidIndexCount;
                        continue;
                    }

                    const uint32_t materialId = SmokeMaterialId(shader);
                    if (SmokeDynamicSurfaceAlreadyCaptured(capturedDynamicSurfaces, entityIndex, tri, materialId))
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

                    const RtSmokeSurfaceClass surfaceClass = ClassifySmokeSurface(viewDef, &retainedDrawSurf, tri);
                    if (surfaceClass == RtSmokeSurfaceClass::StaticWorld || surfaceClass == RtSmokeSurfaceClass::SkinnedDeformed)
                    {
                        continue;
                    }
                    const RtSmokeTranslucentSubtype translucentSubtype = surfaceClass == RtSmokeSurfaceClass::ParticleAlpha ? ClassifySmokeTranslucentSubtype(&retainedDrawSurf) : RtSmokeTranslucentSubtype::Unknown;
                    const uint32_t surfaceClassId = SmokeSurfaceClassAndSubtypeId(surfaceClass, translucentSubtype);
                    const int bucketIndex = idMath::ClampInt(0, RT_SMOKE_CLASS_COUNT - 1, static_cast<int>(surfaceClassId & RT_SMOKE_TRIANGLE_CLASS_MASK));

                    if (dynamicVerts + tri->numVerts > RT_SMOKE_MAX_VERTS ||
                        dynamicIndexes + tri->numIndexes > RT_SMOKE_MAX_INDEXES)
                    {
                        ++skipStats.limitExceeded;
                        break;
                    }

                    std::vector<PathTraceSmokeVertex>& bucketVertices = bucketVertexData[bucketIndex];
                    std::vector<uint32_t>& bucketIndexes = bucketIndexData[bucketIndex];
                    std::vector<uint32_t>& bucketClasses = bucketTriangleClassData[bucketIndex];
                    std::vector<uint32_t>& bucketMaterials = bucketTriangleMaterialData[bucketIndex];
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
                    if (emittedIndexes <= 0)
                    {
                        continue;
                    }

                    AddSmokeMaterialStats(materialStats, shader, emittedIndexes, surfaceClass, translucentSubtype);
                    AddSmokeDynamicMaterialEvalStats(materialStats, &retainedDrawSurf, emittedIndexes);
                    ++sourceSurfaces;
                    ++dynamicSurfaces;
                    ++retainedSurfaces;
                    ++dynamicStats.retainedOccluderSurfaces;
                    dynamicStats.retainedOccluderIndexes += emittedIndexes;
                    sourceVerts += tri->numVerts;
                    sourceIndexes += emittedIndexes;
                    AddSmokeSurfaceClassStats(classStats, surfaceClass, tri->numVerts, emittedIndexes);
                    AddSmokeDynamicGeometryStats(dynamicStats, surfaceClass, &retainedDrawSurf, tri, emittedIndexes);
                    ++bucketRanges.buckets[bucketIndex].surfaceCount;
                    dynamicVerts += tri->numVerts;
                    dynamicIndexes += emittedIndexes;

                    RtSmokeCapturedDynamicSurfaceKey capturedKey;
                    capturedKey.entityIndex = entityIndex;
                    capturedKey.tri = tri;
                    capturedKey.materialId = materialId;
                    capturedDynamicSurfaces.push_back(capturedKey);
                }
            }
        }
    }

    const int bucketMergeStartMs = Sys_Milliseconds();
    {
        OPTICK_EVENT("PT Capture Bucket Merge");
        RtSmokeBucketRange& staticRange = bucketRanges.buckets[0];
        staticRange.vertexOffset = 0;
        staticRange.indexOffset = 0;
        staticRange.triangleOffset = 0;
        staticRange.vertexCount = static_cast<int>(staticVertexCache.size());
        staticRange.indexCount = static_cast<int>(staticIndexCache.size());
        staticRange.triangleCount = static_cast<int>(staticTriangleClassCache.size());

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

    if ((staticTriangleClassCache.empty() && triangleClassData.empty()) ||
        (staticTriangleMaterialCache.empty() && triangleMaterialData.empty()))
    {
        ++skipStats.emptyClassBuffer;
    }

    const bool hasStaticGeometry = !staticVertexCache.empty() && !staticIndexCache.empty() && !staticTriangleClassCache.empty() && !staticTriangleMaterialCache.empty();
    const bool hasDynamicGeometry = !vertexData.empty() && !indexData.empty() && !triangleClassData.empty() && !triangleMaterialData.empty();
    return sourceSurfaces > 0 && (hasStaticGeometry || hasDynamicGeometry);
}
