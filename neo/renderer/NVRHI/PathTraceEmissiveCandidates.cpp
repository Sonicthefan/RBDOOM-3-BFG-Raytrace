#include "precompiled.h"
#pragma hdrstop

#include "PathTraceEmissiveCandidates.h"
#include "PathTraceDynamicMaterialState.h"
#include "PathTraceMaterialUniverse.h"
#include "PathTraceSceneCapture.h"
#include "PathTraceTextureRegistry.h"
#include "../RenderCommon.h"

#include <algorithm>

namespace {

uint64 HashSmokeEmissiveIdentityValue(uint64 hash, uint64 value)
{
    hash ^= value;
    hash *= 1099511628211ull;
    return hash;
}

uint64 BuildSmokeEmissiveTriangleIdentity(uint32_t materialId, uint32_t instanceId, uint32_t primitiveIndex, uint32_t materialIndex, uint32_t triangleClassAndFlags)
{
    uint64 hash = 1469598103934665603ull;
    hash = HashSmokeEmissiveIdentityValue(hash, materialId);
    hash = HashSmokeEmissiveIdentityValue(hash, instanceId);
    hash = HashSmokeEmissiveIdentityValue(hash, primitiveIndex);
    hash = HashSmokeEmissiveIdentityValue(hash, materialIndex);
    hash = HashSmokeEmissiveIdentityValue(hash, triangleClassAndFlags);
    return hash;
}

uint32_t FindSmokeMaterialTableIndexForId(const std::vector<uint32_t>& materialIds, uint32_t materialId)
{
    for (int materialIndex = 0; materialIndex < static_cast<int>(materialIds.size()); ++materialIndex)
    {
        if (materialIds[materialIndex] == materialId)
        {
            return static_cast<uint32_t>(materialIndex);
        }
    }
    return UINT32_MAX;
}

}

float SmokeMaterialEmissiveLuminance(const PathTraceSmokeMaterial& material)
{
    const float r = Max(0.0f, material.emissiveColor[0]);
    const float g = Max(0.0f, material.emissiveColor[1]);
    const float b = Max(0.0f, material.emissiveColor[2]);
    return r * 0.2126f + g * 0.7152f + b * 0.0722f;
}

void AppendSmokeEmissiveInventoryForGeometry(
    const std::vector<uint32_t>& materialIds,
    const std::vector<PathTraceSmokeMaterial>& materials,
    const std::vector<PathTraceSmokeVertex>& vertices,
    const std::vector<uint32_t>& indexes,
    const std::vector<uint32_t>& triangleClasses,
    const std::vector<uint32_t>& triangleMaterialIndexes,
    uint32_t instanceId,
    uint32_t emissiveMaterialFlag,
    uint32_t triangleClassMask,
    uint32_t skinnedSurfaceClassId,
    int maxRecords,
    std::vector<PathTraceSmokeEmissiveTriangle>& emissiveTriangles,
    RtSmokeEmissiveInventoryStats& stats)
{
    OPTICK_EVENT("PT Emissive Append Geometry");

    const int triangleCount = Min(static_cast<int>(triangleMaterialIndexes.size()), static_cast<int>(indexes.size() / 3));
    for (int primitiveIndex = 0; primitiveIndex < triangleCount; ++primitiveIndex)
    {
        const uint32_t materialIndex = triangleMaterialIndexes[primitiveIndex];
        if (materialIndex >= materials.size() || materialIndex >= materialIds.size())
        {
            ++stats.skippedInvalidMaterialTriangles;
            continue;
        }

        const uint32_t materialId = materialIds[materialIndex];
        const PathTraceSmokeMaterial& material = materials[materialIndex];
        if ((material.flags & emissiveMaterialFlag) == 0)
        {
            continue;
        }

        const uint32_t triangleClassAndFlags = primitiveIndex < static_cast<int>(triangleClasses.size()) ? triangleClasses[primitiveIndex] : 0u;
        const uint32_t surfaceClass = triangleClassAndFlags & triangleClassMask;
        if ((triangleClassAndFlags & RT_SMOKE_TRIANGLE_EMISSIVE_STAGE_OFF) != 0u)
        {
            ++stats.skippedRuntimeInactiveTriangles;
            continue;
        }
        if (surfaceClass == skinnedSurfaceClassId)
        {
            ++stats.skippedSkinnedTriangles;
            continue;
        }

        ++stats.totalTriangles;
        if (instanceId == 0)
        {
            ++stats.staticTriangles;
        }
        else
        {
            ++stats.dynamicTriangles;
        }
        if (std::find(stats.materialIndexes.begin(), stats.materialIndexes.end(), materialIndex) == stats.materialIndexes.end())
        {
            stats.materialIndexes.push_back(materialIndex);
        }

        const int indexOffset = primitiveIndex * 3;
        const uint32_t i0 = indexes[indexOffset + 0];
        const uint32_t i1 = indexes[indexOffset + 1];
        const uint32_t i2 = indexes[indexOffset + 2];
        if (i0 >= vertices.size() || i1 >= vertices.size() || i2 >= vertices.size())
        {
            ++stats.skippedInvalidMaterialTriangles;
            continue;
        }

        const idVec3 p0 = SmokeVertexPosition(vertices[i0]);
        const idVec3 p1 = SmokeVertexPosition(vertices[i1]);
        const idVec3 p2 = SmokeVertexPosition(vertices[i2]);
        const idVec2 uv0 = SmokeVertexTexCoord(vertices[i0]);
        const idVec2 uv1 = SmokeVertexTexCoord(vertices[i1]);
        const idVec2 uv2 = SmokeVertexTexCoord(vertices[i2]);
        const idVec3 edge01 = p1 - p0;
        const idVec3 edge02 = p2 - p0;
        idVec3 areaNormal = edge01.Cross(edge02);
        const float doubleArea = areaNormal.Length();
        if (doubleArea <= 1.0e-6f)
        {
            continue;
        }

        const float area = doubleArea * 0.5f;
        areaNormal *= 1.0f / doubleArea;
        const float luminance = SmokeMaterialEmissiveLuminance(material);
        const idVec3 estimatedRadiance(
            Max(0.0f, material.emissiveColor[0]),
            Max(0.0f, material.emissiveColor[1]),
            Max(0.0f, material.emissiveColor[2]));
        const float sampleWeight = area * luminance;
        stats.totalArea += area;
        stats.totalWeightedLuminance += sampleWeight;

        if (static_cast<int>(emissiveTriangles.size()) >= maxRecords)
        {
            ++stats.cappedTriangles;
            continue;
        }

        PathTraceSmokeEmissiveTriangle record = {};
        const idVec3 center = (p0 + p1 + p2) * (1.0f / 3.0f);
        record.centerAndArea[0] = center.x;
        record.centerAndArea[1] = center.y;
        record.centerAndArea[2] = center.z;
        record.centerAndArea[3] = area;
        record.normalAndLuminance[0] = areaNormal.x;
        record.normalAndLuminance[1] = areaNormal.y;
        record.normalAndLuminance[2] = areaNormal.z;
        record.normalAndLuminance[3] = luminance;
        record.uvBounds[0] = Min(uv0.x, Min(uv1.x, uv2.x));
        record.uvBounds[1] = Min(uv0.y, Min(uv1.y, uv2.y));
        record.uvBounds[2] = Max(uv0.x, Max(uv1.x, uv2.x));
        record.uvBounds[3] = Max(uv0.y, Max(uv1.y, uv2.y));
        record.centroidUvAndWeight[0] = (uv0.x + uv1.x + uv2.x) * (1.0f / 3.0f);
        record.centroidUvAndWeight[1] = (uv0.y + uv1.y + uv2.y) * (1.0f / 3.0f);
        record.centroidUvAndWeight[2] = sampleWeight;
        record.centroidUvAndWeight[3] = 0.0f;
        record.estimatedRadianceAndLuminance[0] = estimatedRadiance.x;
        record.estimatedRadianceAndLuminance[1] = estimatedRadiance.y;
        record.estimatedRadianceAndLuminance[2] = estimatedRadiance.z;
        record.estimatedRadianceAndLuminance[3] = luminance;
        record.sampleWeightAndPdf[0] = sampleWeight;
        record.sampleWeightAndPdf[1] = 0.0f;
        record.sampleWeightAndPdf[2] = area;
        record.sampleWeightAndPdf[3] = 0.0f;
        record.materialIndex = materialIndex;
        record.instanceId = instanceId;
        record.primitiveIndex = static_cast<uint32_t>(primitiveIndex);
        record.flags = material.flags;
        record.emissiveTextureIndex = material.emissiveTextureIndex;
        record.emissiveTextureWidth = material.emissiveTextureWidth;
        record.emissiveTextureHeight = material.emissiveTextureHeight;
        record.materialId = materialId;
        const RtSmokeMaterialTextureInfo info = ResolveSmokeMaterialTextureInfo(materialId, materialIndex);
        record.universeMaterialIndex = GetSmokeMaterialUniverseFacts(materialId, info).universeIndex;
        const uint64 identityHash = BuildSmokeEmissiveTriangleIdentity(materialId, instanceId, static_cast<uint32_t>(primitiveIndex), materialIndex, triangleClassAndFlags);
        record.identityHashLo = static_cast<uint32_t>(identityHash & 0xffffffffu);
        record.identityHashHi = static_cast<uint32_t>(identityHash >> 32);
        record.padding0 = triangleClassAndFlags;
        emissiveTriangles.push_back(record);
    }

    stats.capturedTriangles = static_cast<int>(emissiveTriangles.size());
    stats.uniqueMaterials = static_cast<int>(stats.materialIndexes.size());
}

void FinalizeSmokeEmissiveTriangleSamplingFields(std::vector<PathTraceSmokeEmissiveTriangle>& emissiveTriangles, const RtSmokeEmissiveInventoryStats& stats)
{
    OPTICK_EVENT("PT Emissive Sampling Fields");

    const float inverseTotalWeightedLuminance = stats.totalWeightedLuminance > 1.0e-8f ? 1.0f / stats.totalWeightedLuminance : 0.0f;
    const float inverseTotalArea = stats.totalArea > 1.0e-8f ? 1.0f / stats.totalArea : 0.0f;
    for (PathTraceSmokeEmissiveTriangle& record : emissiveTriangles)
    {
        record.sampleWeightAndPdf[1] = record.sampleWeightAndPdf[0] * inverseTotalWeightedLuminance;
        record.sampleWeightAndPdf[3] = record.centerAndArea[3] * inverseTotalArea;
        record.centroidUvAndWeight[3] = record.sampleWeightAndPdf[1];
    }
}

std::vector<uint32_t> BuildSmokeWorldStaticEmissiveMaterialIds(const viewDef_t* viewDef)
{
    OPTICK_EVENT("PT World Static Emissive Material Ids");

    std::vector<uint32_t> materialIds;
    if (!viewDef || !viewDef->renderWorld)
    {
        return materialIds;
    }

    idRenderWorldLocal* renderWorld = viewDef->renderWorld;
    for (int entityIndex = 0; entityIndex < renderWorld->entityDefs.Num(); ++entityIndex)
    {
        const idRenderEntityLocal* entity = renderWorld->entityDefs[entityIndex];
        const idRenderModel* model = entity ? entity->parms.hModel : nullptr;
        if (!model || !model->IsStaticWorldModel())
        {
            continue;
        }

        for (int surfaceIndex = 0; surfaceIndex < model->NumSurfaces(); ++surfaceIndex)
        {
            const modelSurface_t* surface = model->Surface(surfaceIndex);
            const idMaterial* material = surface ? surface->shader : nullptr;
            if (!material)
            {
                continue;
            }

            const uint32_t materialId = SmokeMaterialId(material);
            const RtSmokeMaterialTextureInfo info = ResolveSmokeMaterialTextureInfo(materialId, -1);
            const RtSmokeMaterialUniverseFacts& facts = GetSmokeMaterialUniverseFacts(materialId, info);
            if (!facts.emissive)
            {
                continue;
            }

            if (std::find(materialIds.begin(), materialIds.end(), materialId) == materialIds.end())
            {
                materialIds.push_back(materialId);
            }
        }
    }

    return materialIds;
}

void AppendSmokeWorldStaticEmissiveTriangleInventory(
    const viewDef_t* viewDef,
    const std::vector<uint32_t>& materialIds,
    const std::vector<PathTraceSmokeMaterial>& materials,
    uint32_t emissiveMaterialFlag,
    uint32_t staticSurfaceClassId,
    int maxRecords,
    std::vector<PathTraceSmokeEmissiveTriangle>& emissiveTriangles,
    RtSmokeEmissiveInventoryStats& stats)
{
    OPTICK_EVENT("PT World Static Emissive Inventory");

    if (!viewDef || !viewDef->renderWorld)
    {
        return;
    }

    idRenderWorldLocal* renderWorld = viewDef->renderWorld;
    uint32_t worldPrimitiveId = 0;
    for (int entityIndex = 0; entityIndex < renderWorld->entityDefs.Num(); ++entityIndex)
    {
        const idRenderEntityLocal* entity = renderWorld->entityDefs[entityIndex];
        const idRenderModel* model = entity ? entity->parms.hModel : nullptr;
        if (!model || !model->IsStaticWorldModel())
        {
            continue;
        }

        for (int surfaceIndex = 0; surfaceIndex < model->NumSurfaces(); ++surfaceIndex)
        {
            const modelSurface_t* surface = model->Surface(surfaceIndex);
            const idMaterial* material = surface ? surface->shader : nullptr;
            const srfTriangles_t* tri = surface ? surface->geometry : nullptr;
            if (!material || !tri || !tri->verts || !tri->indexes)
            {
                continue;
            }

            const uint32_t materialId = SmokeMaterialId(material);
            const uint32_t materialIndex = FindSmokeMaterialTableIndexForId(materialIds, materialId);
            if (materialIndex == UINT32_MAX || materialIndex >= materials.size())
            {
                stats.skippedInvalidMaterialTriangles += tri->numIndexes / 3;
                continue;
            }

            const PathTraceSmokeMaterial& smokeMaterial = materials[materialIndex];
            if ((smokeMaterial.flags & emissiveMaterialFlag) == 0)
            {
                continue;
            }

            const float luminance = SmokeMaterialEmissiveLuminance(smokeMaterial);
            const idVec3 estimatedRadiance(
                Max(0.0f, smokeMaterial.emissiveColor[0]),
                Max(0.0f, smokeMaterial.emissiveColor[1]),
                Max(0.0f, smokeMaterial.emissiveColor[2]));

            for (int indexOffset = 0; indexOffset + 2 < tri->numIndexes; indexOffset += 3)
            {
                ++worldPrimitiveId;
                const int i0 = tri->indexes[indexOffset + 0];
                const int i1 = tri->indexes[indexOffset + 1];
                const int i2 = tri->indexes[indexOffset + 2];
                if (i0 < 0 || i1 < 0 || i2 < 0 || i0 >= tri->numVerts || i1 >= tri->numVerts || i2 >= tri->numVerts)
                {
                    ++stats.skippedInvalidMaterialTriangles;
                    continue;
                }

                const idDrawVert& v0 = tri->verts[i0];
                const idDrawVert& v1 = tri->verts[i1];
                const idDrawVert& v2 = tri->verts[i2];
                const idVec3 p0 = v0.xyz;
                const idVec3 p1 = v1.xyz;
                const idVec3 p2 = v2.xyz;
                const idVec2 uv0 = v0.GetTexCoord();
                const idVec2 uv1 = v1.GetTexCoord();
                const idVec2 uv2 = v2.GetTexCoord();
                const idVec3 edge01 = p1 - p0;
                const idVec3 edge02 = p2 - p0;
                idVec3 areaNormal = edge01.Cross(edge02);
                const float doubleArea = areaNormal.Length();
                if (doubleArea <= 1.0e-6f)
                {
                    continue;
                }

                const float area = doubleArea * 0.5f;
                areaNormal *= 1.0f / doubleArea;
                const float sampleWeight = area * luminance;
                ++stats.totalTriangles;
                ++stats.staticTriangles;
                ++stats.fullLevelStaticTriangles;
                stats.totalArea += area;
                stats.totalWeightedLuminance += sampleWeight;

                if (std::find(stats.materialIndexes.begin(), stats.materialIndexes.end(), materialIndex) == stats.materialIndexes.end())
                {
                    stats.materialIndexes.push_back(materialIndex);
                }

                if (static_cast<int>(emissiveTriangles.size()) >= maxRecords)
                {
                    ++stats.cappedTriangles;
                    continue;
                }

                PathTraceSmokeEmissiveTriangle record = {};
                const idVec3 center = (p0 + p1 + p2) * (1.0f / 3.0f);
                record.centerAndArea[0] = center.x;
                record.centerAndArea[1] = center.y;
                record.centerAndArea[2] = center.z;
                record.centerAndArea[3] = area;
                record.normalAndLuminance[0] = areaNormal.x;
                record.normalAndLuminance[1] = areaNormal.y;
                record.normalAndLuminance[2] = areaNormal.z;
                record.normalAndLuminance[3] = luminance;
                record.uvBounds[0] = Min(uv0.x, Min(uv1.x, uv2.x));
                record.uvBounds[1] = Min(uv0.y, Min(uv1.y, uv2.y));
                record.uvBounds[2] = Max(uv0.x, Max(uv1.x, uv2.x));
                record.uvBounds[3] = Max(uv0.y, Max(uv1.y, uv2.y));
                record.centroidUvAndWeight[0] = (uv0.x + uv1.x + uv2.x) * (1.0f / 3.0f);
                record.centroidUvAndWeight[1] = (uv0.y + uv1.y + uv2.y) * (1.0f / 3.0f);
                record.centroidUvAndWeight[2] = sampleWeight;
                record.estimatedRadianceAndLuminance[0] = estimatedRadiance.x;
                record.estimatedRadianceAndLuminance[1] = estimatedRadiance.y;
                record.estimatedRadianceAndLuminance[2] = estimatedRadiance.z;
                record.estimatedRadianceAndLuminance[3] = luminance;
                record.sampleWeightAndPdf[0] = sampleWeight;
                record.sampleWeightAndPdf[2] = area;
                record.materialIndex = materialIndex;
                record.instanceId = 0;
                record.primitiveIndex = worldPrimitiveId;
                record.flags = smokeMaterial.flags;
                record.emissiveTextureIndex = smokeMaterial.emissiveTextureIndex;
                record.emissiveTextureWidth = smokeMaterial.emissiveTextureWidth;
                record.emissiveTextureHeight = smokeMaterial.emissiveTextureHeight;
                record.materialId = materialId;
                const RtSmokeMaterialTextureInfo info = ResolveSmokeMaterialTextureInfo(materialId, static_cast<int>(materialIndex));
                record.universeMaterialIndex = GetSmokeMaterialUniverseFacts(materialId, info).universeIndex;
                const uint32_t classAndFlags = staticSurfaceClassId;
                const uint64 identityHash = BuildSmokeEmissiveTriangleIdentity(materialId, 0, worldPrimitiveId, materialIndex, classAndFlags);
                record.identityHashLo = static_cast<uint32_t>(identityHash & 0xffffffffu);
                record.identityHashHi = static_cast<uint32_t>(identityHash >> 32);
                record.padding0 = classAndFlags;
                emissiveTriangles.push_back(record);
            }
        }
    }

    stats.capturedTriangles = static_cast<int>(emissiveTriangles.size());
    stats.uniqueMaterials = static_cast<int>(stats.materialIndexes.size());
}

std::vector<PathTraceSmokeMaterial> BuildSmokeEmissiveMaterialViews(const std::vector<uint32_t>& materialIds, const std::vector<PathTraceSmokeMaterial>& frameMaterials, uint32_t emissiveMaterialFlag)
{
    OPTICK_EVENT("PT Emissive Material Views");

    std::vector<PathTraceSmokeMaterial> materialViews = frameMaterials;
    const int materialCount = Min(static_cast<int>(materialIds.size()), static_cast<int>(frameMaterials.size()));
    for (int materialIndex = 0; materialIndex < materialCount; ++materialIndex)
    {
        const RtSmokeMaterialTextureInfo info = ResolveSmokeMaterialTextureInfo(materialIds[materialIndex], materialIndex);
        const RtSmokePersistentMaterialRecord& universeRecord = GetSmokePersistentMaterialRecord(materialIds[materialIndex], info);
        const RtSmokeMaterialUniverseFacts& universeFacts = universeRecord.facts;
        PathTraceSmokeMaterial material = universeRecord.material;

        // Texture descriptor slots are frame-local; stable flags/color come from the universe.
        material.diffuseTextureIndex = frameMaterials[materialIndex].diffuseTextureIndex;
        material.alphaTextureIndex = frameMaterials[materialIndex].alphaTextureIndex;
        material.normalTextureIndex = frameMaterials[materialIndex].normalTextureIndex;
        material.specularTextureIndex = frameMaterials[materialIndex].specularTextureIndex;
        material.emissiveTextureIndex = frameMaterials[materialIndex].emissiveTextureIndex;
        material.textureWidth = frameMaterials[materialIndex].textureWidth;
        material.textureHeight = frameMaterials[materialIndex].textureHeight;
        material.alphaTextureWidth = frameMaterials[materialIndex].alphaTextureWidth;
        material.alphaTextureHeight = frameMaterials[materialIndex].alphaTextureHeight;
        material.normalTextureWidth = frameMaterials[materialIndex].normalTextureWidth;
        material.normalTextureHeight = frameMaterials[materialIndex].normalTextureHeight;
        material.specularTextureWidth = frameMaterials[materialIndex].specularTextureWidth;
        material.specularTextureHeight = frameMaterials[materialIndex].specularTextureHeight;
        material.emissiveTextureWidth = frameMaterials[materialIndex].emissiveTextureWidth;
        material.emissiveTextureHeight = frameMaterials[materialIndex].emissiveTextureHeight;

        if (!universeFacts.emissive || (frameMaterials[materialIndex].flags & emissiveMaterialFlag) == 0)
        {
            material.flags &= ~emissiveMaterialFlag;
            material.emissiveColor[0] = 0.0f;
            material.emissiveColor[1] = 0.0f;
            material.emissiveColor[2] = 0.0f;
            material.emissiveColor[3] = 1.0f;
        }

        materialViews[materialIndex] = material;
    }

    return materialViews;
}

void BuildSmokeEmissiveLightCandidateSummaries(
    const std::vector<uint32_t>& materialIds,
    const std::vector<PathTraceSmokeEmissiveTriangle>& emissiveTriangles,
    RtSmokeEmissiveInventoryStats& stats)
{
    OPTICK_EVENT("PT Emissive Candidate Summaries");

    stats.lightCandidates.clear();
    stats.lightCandidates.reserve(stats.uniqueMaterials);
    stats.candidateMaterials = 0;
    stats.texturedCandidateMaterials = 0;
    stats.untexturedCandidateMaterials = 0;

    for (int triangleIndex = 0; triangleIndex < stats.capturedTriangles; ++triangleIndex)
    {
        const PathTraceSmokeEmissiveTriangle& record = emissiveTriangles[triangleIndex];
        const int materialIndex = static_cast<int>(record.materialIndex);
        if (materialIndex < 0 || materialIndex >= static_cast<int>(materialIds.size()))
        {
            continue;
        }

        const uint32_t materialId = materialIds[materialIndex];
        RtSmokeEmissiveLightCandidateSummary* candidate = nullptr;
        for (RtSmokeEmissiveLightCandidateSummary& existing : stats.lightCandidates)
        {
            if (existing.materialId == materialId)
            {
                candidate = &existing;
                break;
            }
        }

        if (!candidate)
        {
            const RtSmokeMaterialTextureInfo info = ResolveSmokeMaterialTextureInfo(materialId, materialIndex);
            const RtSmokeMaterialUniverseFacts& facts = GetSmokeMaterialUniverseFacts(materialId, info);
            RtSmokeEmissiveLightCandidateSummary newCandidate = {};
            newCandidate.materialId = materialId;
            newCandidate.universeMaterialIndex = facts.universeIndex;
            newCandidate.materialIndex = record.materialIndex;
            newCandidate.emissiveColor = facts.emissiveColor;
            newCandidate.emissiveLuminance = facts.emissiveLuminance;
            newCandidate.hasEmissiveTexture = facts.hasEmissiveImage;
            newCandidate.hasSafeEmissiveTexture = facts.hasSafeEmissiveTexture;
            newCandidate.emissiveTextureIndex = record.emissiveTextureIndex;
            newCandidate.emissiveTextureWidth = record.emissiveTextureWidth;
            newCandidate.emissiveTextureHeight = record.emissiveTextureHeight;
            stats.lightCandidates.push_back(newCandidate);
            candidate = &stats.lightCandidates.back();
        }

        ++candidate->triangles;
        if (record.instanceId == 0)
        {
            ++candidate->staticTriangles;
        }
        else
        {
            ++candidate->dynamicTriangles;
        }
        candidate->area += record.centerAndArea[3];
        candidate->weightedLuminance += record.centroidUvAndWeight[2];
        if (candidate->emissiveTextureIndex == UINT32_MAX && record.emissiveTextureIndex != UINT32_MAX)
        {
            candidate->emissiveTextureIndex = record.emissiveTextureIndex;
            candidate->emissiveTextureWidth = record.emissiveTextureWidth;
            candidate->emissiveTextureHeight = record.emissiveTextureHeight;
        }
    }

    std::sort(stats.lightCandidates.begin(), stats.lightCandidates.end(),
        [](const RtSmokeEmissiveLightCandidateSummary& lhs, const RtSmokeEmissiveLightCandidateSummary& rhs)
        {
            return lhs.weightedLuminance > rhs.weightedLuminance;
        });

    stats.candidateMaterials = static_cast<int>(stats.lightCandidates.size());
    for (const RtSmokeEmissiveLightCandidateSummary& candidate : stats.lightCandidates)
    {
        if (candidate.emissiveTextureIndex != UINT32_MAX)
        {
            ++stats.texturedCandidateMaterials;
        }
        else
        {
            ++stats.untexturedCandidateMaterials;
        }
    }
}

std::vector<PathTraceSmokeLightCandidate> BuildSmokeLightCandidateBufferRecords(const RtSmokeEmissiveInventoryStats& stats)
{
    OPTICK_EVENT("PT Emissive Candidate Buffer Records");

    std::vector<PathTraceSmokeLightCandidate> candidates;
    candidates.reserve(Max(1, stats.candidateMaterials));
    for (const RtSmokeEmissiveLightCandidateSummary& summary : stats.lightCandidates)
    {
        PathTraceSmokeLightCandidate candidate = {};
        candidate.emissiveColorAndLuminance[0] = summary.emissiveColor.x;
        candidate.emissiveColorAndLuminance[1] = summary.emissiveColor.y;
        candidate.emissiveColorAndLuminance[2] = summary.emissiveColor.z;
        candidate.emissiveColorAndLuminance[3] = summary.emissiveLuminance;
        candidate.areaAndWeightedLuminance[0] = summary.area;
        candidate.areaAndWeightedLuminance[1] = summary.weightedLuminance;
        candidate.areaAndWeightedLuminance[2] = 0.0f;
        candidate.areaAndWeightedLuminance[3] = 0.0f;
        candidate.materialId = summary.materialId;
        candidate.universeMaterialIndex = summary.universeMaterialIndex;
        candidate.materialIndex = summary.materialIndex;
        candidate.triangleCount = static_cast<uint32_t>(Max(0, summary.triangles));
        candidate.staticTriangleCount = static_cast<uint32_t>(Max(0, summary.staticTriangles));
        candidate.dynamicTriangleCount = static_cast<uint32_t>(Max(0, summary.dynamicTriangles));
        candidate.emissiveTextureIndex = summary.emissiveTextureIndex;
        candidate.emissiveTextureWidth = summary.emissiveTextureWidth;
        candidate.emissiveTextureHeight = summary.emissiveTextureHeight;
        if (summary.hasEmissiveTexture)
        {
            candidate.flags |= RT_SMOKE_LIGHT_CANDIDATE_TEXTURED;
        }
        if (summary.hasSafeEmissiveTexture)
        {
            candidate.flags |= RT_SMOKE_LIGHT_CANDIDATE_SAFE_TEXTURE;
        }
        if (summary.staticTriangles > 0)
        {
            candidate.flags |= RT_SMOKE_LIGHT_CANDIDATE_HAS_STATIC_TRIANGLES;
        }
        if (summary.dynamicTriangles > 0)
        {
            candidate.flags |= RT_SMOKE_LIGHT_CANDIDATE_HAS_DYNAMIC_TRIANGLES;
        }
        candidates.push_back(candidate);
    }

    if (candidates.empty())
    {
        candidates.resize(1);
    }
    return candidates;
}

RtSmokeEmissiveInventoryStats BuildSmokeEmissiveInventoryStatsForRecords(
    const std::vector<uint32_t>& materialIds,
    const std::vector<PathTraceSmokeEmissiveTriangle>& emissiveTriangles)
{
    OPTICK_EVENT("PT Emissive Stats From Records");

    RtSmokeEmissiveInventoryStats stats;
    stats.capturedTriangles = static_cast<int>(emissiveTriangles.size());
    for (const PathTraceSmokeEmissiveTriangle& record : emissiveTriangles)
    {
        if (record.centerAndArea[3] <= 0.0f)
        {
            continue;
        }

        ++stats.totalTriangles;
        if (record.instanceId == 0)
        {
            ++stats.staticTriangles;
        }
        else
        {
            ++stats.dynamicTriangles;
        }
        stats.totalArea += record.centerAndArea[3];
        stats.totalWeightedLuminance += record.sampleWeightAndPdf[0];

        if (record.materialIndex < materialIds.size() &&
            std::find(stats.materialIndexes.begin(), stats.materialIndexes.end(), record.materialIndex) == stats.materialIndexes.end())
        {
            stats.materialIndexes.push_back(record.materialIndex);
        }
    }

    stats.uniqueMaterials = static_cast<int>(stats.materialIndexes.size());
    BuildSmokeEmissiveLightCandidateSummaries(materialIds, emissiveTriangles, stats);
    return stats;
}

std::vector<PathTraceSmokeEmissiveTriangle> BuildSmokeEmissiveTriangleInventory(
    const std::vector<uint32_t>& materialIds,
    const std::vector<PathTraceSmokeMaterial>& materials,
    const std::vector<PathTraceSmokeVertex>& staticVertices,
    const std::vector<uint32_t>& staticIndexes,
    const std::vector<uint32_t>& staticTriangleClasses,
    const std::vector<uint32_t>& staticTriangleMaterialIndexes,
    const std::vector<PathTraceSmokeVertex>& dynamicVertices,
    const std::vector<uint32_t>& dynamicIndexes,
    const std::vector<uint32_t>& dynamicTriangleClasses,
    const std::vector<uint32_t>& dynamicTriangleMaterialIndexes,
    uint32_t emissiveMaterialFlag,
    uint32_t triangleClassMask,
    uint32_t skinnedSurfaceClassId,
    int maxRecords,
    RtSmokeEmissiveInventoryStats& stats)
{
    OPTICK_EVENT("PT Emissive Triangle Inventory Detail");

    stats = RtSmokeEmissiveInventoryStats();
    std::vector<PathTraceSmokeEmissiveTriangle> emissiveTriangles;
    maxRecords = Max(1, maxRecords);
    emissiveTriangles.reserve(Min(maxRecords, 1024));
    const std::vector<PathTraceSmokeMaterial> materialViews = BuildSmokeEmissiveMaterialViews(materialIds, materials, emissiveMaterialFlag);
    AppendSmokeEmissiveInventoryForGeometry(materialIds, materialViews, staticVertices, staticIndexes, staticTriangleClasses, staticTriangleMaterialIndexes, 0, emissiveMaterialFlag, triangleClassMask, skinnedSurfaceClassId, maxRecords, emissiveTriangles, stats);
    AppendSmokeEmissiveInventoryForGeometry(materialIds, materialViews, dynamicVertices, dynamicIndexes, dynamicTriangleClasses, dynamicTriangleMaterialIndexes, 1, emissiveMaterialFlag, triangleClassMask, skinnedSurfaceClassId, maxRecords, emissiveTriangles, stats);
    stats.capturedTriangles = static_cast<int>(emissiveTriangles.size());
    stats.uniqueMaterials = static_cast<int>(stats.materialIndexes.size());
    FinalizeSmokeEmissiveTriangleSamplingFields(emissiveTriangles, stats);
    BuildSmokeEmissiveLightCandidateSummaries(materialIds, emissiveTriangles, stats);
    if (emissiveTriangles.empty())
    {
        emissiveTriangles.resize(1);
    }
    return emissiveTriangles;
}

void LogSmokeEmissiveInventoryDump(
    const std::vector<uint32_t>& materialIds,
    const std::vector<PathTraceSmokeEmissiveTriangle>& emissiveTriangles,
    const RtSmokeEmissiveInventoryStats& stats)
{
    common->Printf("PathTracePrimaryPass: RT smoke emissive inventory triangles=%d captured=%d static=%d fullLevelStatic=%d dynamic=%d uniqueMaterials=%d capped=%d skippedSkinned=%d skippedRuntimeInactive=%d skippedInvalid=%d totalArea=%.2f areaWeightedLum=%.3f\n",
        stats.totalTriangles,
        stats.capturedTriangles,
        stats.staticTriangles,
        stats.fullLevelStaticTriangles,
        stats.dynamicTriangles,
        stats.uniqueMaterials,
        stats.cappedTriangles,
        stats.skippedSkinnedTriangles,
        stats.skippedRuntimeInactiveTriangles,
        stats.skippedInvalidMaterialTriangles,
        stats.totalArea,
        stats.totalWeightedLuminance);

    common->Printf("PathTracePrimaryPass: RT smoke emissive candidates materials=%d textured=%d untextured=%d triangles=%d area=%.2f areaWeightedLum=%.3f\n",
        stats.candidateMaterials,
        stats.texturedCandidateMaterials,
        stats.untexturedCandidateMaterials,
        stats.capturedTriangles,
        stats.totalArea,
        stats.totalWeightedLuminance);

    const int maxCandidateLogged = Min(8, static_cast<int>(stats.lightCandidates.size()));
    for (int candidateIndex = 0; candidateIndex < maxCandidateLogged; ++candidateIndex)
    {
        const RtSmokeEmissiveLightCandidateSummary& candidate = stats.lightCandidates[candidateIndex];
        const RtSmokeMaterialTextureInfo info = ResolveSmokeMaterialTextureInfo(candidate.materialId, static_cast<int>(candidate.materialIndex));
        common->Printf("  candidate[%d]: materialId=%u universeIndex=%u materialIndex=%u triangles=%d static=%d dynamic=%d area=%.2f areaLum=%.3f lum=%.3f emissiveSlot=%d safeEmissive=%d material='%s' emissive='%s'\n",
            candidateIndex,
            candidate.materialId,
            candidate.universeMaterialIndex,
            candidate.materialIndex,
            candidate.triangles,
            candidate.staticTriangles,
            candidate.dynamicTriangles,
            candidate.area,
            candidate.weightedLuminance,
            candidate.emissiveLuminance,
            candidate.emissiveTextureIndex == UINT32_MAX ? -1 : static_cast<int>(candidate.emissiveTextureIndex),
            candidate.hasSafeEmissiveTexture ? 1 : 0,
            info.materialName.c_str(),
            info.emissiveImageName.c_str());
    }

    std::vector<RtSmokeEmissiveInventoryMaterialSummary> materialSummaries;
    materialSummaries.reserve(stats.uniqueMaterials);
    for (int triangleIndex = 0; triangleIndex < stats.capturedTriangles; ++triangleIndex)
    {
        const PathTraceSmokeEmissiveTriangle& record = emissiveTriangles[triangleIndex];
        RtSmokeEmissiveInventoryMaterialSummary* summary = nullptr;
        for (RtSmokeEmissiveInventoryMaterialSummary& candidate : materialSummaries)
        {
            if (candidate.materialIndex == record.materialIndex)
            {
                summary = &candidate;
                break;
            }
        }
        if (!summary)
        {
            RtSmokeEmissiveInventoryMaterialSummary newSummary = {};
            newSummary.materialIndex = record.materialIndex;
            newSummary.emissiveTextureIndex = record.emissiveTextureIndex;
            newSummary.emissiveTextureWidth = record.emissiveTextureWidth;
            newSummary.emissiveTextureHeight = record.emissiveTextureHeight;
            materialSummaries.push_back(newSummary);
            summary = &materialSummaries.back();
        }

        ++summary->triangles;
        if (record.instanceId == 0)
        {
            ++summary->staticTriangles;
        }
        else
        {
            ++summary->dynamicTriangles;
        }
        summary->area += record.centerAndArea[3];
        summary->weightedLuminance += record.centerAndArea[3] * record.normalAndLuminance[3];
    }

    std::sort(materialSummaries.begin(), materialSummaries.end(),
        [](const RtSmokeEmissiveInventoryMaterialSummary& lhs, const RtSmokeEmissiveInventoryMaterialSummary& rhs)
        {
            return lhs.weightedLuminance > rhs.weightedLuminance;
        });

    const int maxSummaryLogged = Min(16, static_cast<int>(materialSummaries.size()));
    for (int summaryIndex = 0; summaryIndex < maxSummaryLogged; ++summaryIndex)
    {
        const RtSmokeEmissiveInventoryMaterialSummary& summary = materialSummaries[summaryIndex];
        const int materialIndex = static_cast<int>(summary.materialIndex);
        const uint32_t materialId = materialIndex >= 0 && materialIndex < static_cast<int>(materialIds.size()) ? materialIds[materialIndex] : 0u;
        const RtSmokeMaterialTextureInfo info = ResolveSmokeMaterialTextureInfo(materialId, materialIndex);
        common->Printf("  material[%d]: materialIndex=%u materialId=%u triangles=%d static=%d dynamic=%d area=%.2f areaLum=%.3f emissiveSlot=%d emissiveSize=%ux%u material='%s' emissive='%s'\n",
            summaryIndex,
            summary.materialIndex,
            materialId,
            summary.triangles,
            summary.staticTriangles,
            summary.dynamicTriangles,
            summary.area,
            summary.weightedLuminance,
            summary.emissiveTextureIndex == UINT32_MAX ? -1 : static_cast<int>(summary.emissiveTextureIndex),
            summary.emissiveTextureWidth,
            summary.emissiveTextureHeight,
            info.materialName.c_str(),
            info.emissiveImageName.c_str());
    }

    const int maxLogged = Min(16, stats.capturedTriangles);
    for (int sampleIndex = 0; sampleIndex < maxLogged; ++sampleIndex)
    {
        const PathTraceSmokeEmissiveTriangle& record = emissiveTriangles[sampleIndex];
        const int materialIndex = static_cast<int>(record.materialIndex);
        const uint32_t materialId = materialIndex >= 0 && materialIndex < static_cast<int>(materialIds.size()) ? materialIds[materialIndex] : 0u;
        const RtSmokeMaterialTextureInfo info = ResolveSmokeMaterialTextureInfo(materialId, materialIndex);
        common->Printf("  emissive[%d]: instance=%u primitive=%u materialIndex=%u materialId=%u universeIndex=%u identity=%08x%08x area=%.2f lum=%.3f weight=%.3f pdf=%.6f areaPdf=%.6f center=(%.2f %.2f %.2f) centroidUV=(%.3f %.3f) uvBounds=(%.3f %.3f %.3f %.3f) estRadiance=(%.3f %.3f %.3f) emissiveSlot=%d emissiveSize=%ux%u material='%s' emissive='%s'\n",
            sampleIndex,
            record.instanceId,
            record.primitiveIndex,
            record.materialIndex,
            materialId,
            record.universeMaterialIndex,
            record.identityHashHi,
            record.identityHashLo,
            record.centerAndArea[3],
            record.normalAndLuminance[3],
            record.centroidUvAndWeight[2],
            record.sampleWeightAndPdf[1],
            record.sampleWeightAndPdf[3],
            record.centerAndArea[0],
            record.centerAndArea[1],
            record.centerAndArea[2],
            record.centroidUvAndWeight[0],
            record.centroidUvAndWeight[1],
            record.uvBounds[0],
            record.uvBounds[1],
            record.uvBounds[2],
            record.uvBounds[3],
            record.estimatedRadianceAndLuminance[0],
            record.estimatedRadianceAndLuminance[1],
            record.estimatedRadianceAndLuminance[2],
            record.emissiveTextureIndex == UINT32_MAX ? -1 : static_cast<int>(record.emissiveTextureIndex),
            record.emissiveTextureWidth,
            record.emissiveTextureHeight,
            info.materialName.c_str(),
            info.emissiveImageName.c_str());
    }
}
