#include "precompiled.h"
#pragma hdrstop

#include "PathTraceEmissiveCandidates.h"
#include "PathTraceTextureRegistry.h"

#include <algorithm>

float SmokeMaterialEmissiveLuminance(const PathTraceSmokeMaterial& material)
{
    const float r = Max(0.0f, material.emissiveColor[0]);
    const float g = Max(0.0f, material.emissiveColor[1]);
    const float b = Max(0.0f, material.emissiveColor[2]);
    return r * 0.2126f + g * 0.7152f + b * 0.0722f;
}

void AppendSmokeEmissiveInventoryForGeometry(
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
    const int triangleCount = Min(static_cast<int>(triangleMaterialIndexes.size()), static_cast<int>(indexes.size() / 3));
    for (int primitiveIndex = 0; primitiveIndex < triangleCount; ++primitiveIndex)
    {
        const uint32_t materialIndex = triangleMaterialIndexes[primitiveIndex];
        if (materialIndex >= materials.size())
        {
            ++stats.skippedInvalidMaterialTriangles;
            continue;
        }

        const PathTraceSmokeMaterial& material = materials[materialIndex];
        if ((material.flags & emissiveMaterialFlag) == 0)
        {
            continue;
        }

        const uint32_t triangleClassAndFlags = primitiveIndex < static_cast<int>(triangleClasses.size()) ? triangleClasses[primitiveIndex] : 0u;
        const uint32_t surfaceClass = triangleClassAndFlags & triangleClassMask;
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
        stats.totalArea += area;
        stats.totalWeightedLuminance += area * luminance;

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
        record.centroidUvAndWeight[2] = area * luminance;
        record.centroidUvAndWeight[3] = 0.0f;
        record.estimatedRadianceAndLuminance[0] = estimatedRadiance.x;
        record.estimatedRadianceAndLuminance[1] = estimatedRadiance.y;
        record.estimatedRadianceAndLuminance[2] = estimatedRadiance.z;
        record.estimatedRadianceAndLuminance[3] = luminance;
        record.materialIndex = materialIndex;
        record.instanceId = instanceId;
        record.primitiveIndex = static_cast<uint32_t>(primitiveIndex);
        record.flags = material.flags;
        record.emissiveTextureIndex = material.emissiveTextureIndex;
        record.emissiveTextureWidth = material.emissiveTextureWidth;
        record.emissiveTextureHeight = material.emissiveTextureHeight;
        emissiveTriangles.push_back(record);
    }

    stats.capturedTriangles = static_cast<int>(emissiveTriangles.size());
    stats.uniqueMaterials = static_cast<int>(stats.materialIndexes.size());
}

std::vector<PathTraceSmokeEmissiveTriangle> BuildSmokeEmissiveTriangleInventory(
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
    stats = RtSmokeEmissiveInventoryStats();
    std::vector<PathTraceSmokeEmissiveTriangle> emissiveTriangles;
    maxRecords = Max(1, maxRecords);
    emissiveTriangles.reserve(Min(maxRecords, 1024));
    AppendSmokeEmissiveInventoryForGeometry(materials, staticVertices, staticIndexes, staticTriangleClasses, staticTriangleMaterialIndexes, 0, emissiveMaterialFlag, triangleClassMask, skinnedSurfaceClassId, maxRecords, emissiveTriangles, stats);
    AppendSmokeEmissiveInventoryForGeometry(materials, dynamicVertices, dynamicIndexes, dynamicTriangleClasses, dynamicTriangleMaterialIndexes, 1, emissiveMaterialFlag, triangleClassMask, skinnedSurfaceClassId, maxRecords, emissiveTriangles, stats);
    stats.capturedTriangles = static_cast<int>(emissiveTriangles.size());
    stats.uniqueMaterials = static_cast<int>(stats.materialIndexes.size());
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
    common->Printf("PathTracePrimaryPass: RT smoke emissive inventory triangles=%d captured=%d static=%d dynamic=%d uniqueMaterials=%d capped=%d skippedSkinned=%d skippedInvalid=%d totalArea=%.2f areaWeightedLum=%.3f\n",
        stats.totalTriangles,
        stats.capturedTriangles,
        stats.staticTriangles,
        stats.dynamicTriangles,
        stats.uniqueMaterials,
        stats.cappedTriangles,
        stats.skippedSkinnedTriangles,
        stats.skippedInvalidMaterialTriangles,
        stats.totalArea,
        stats.totalWeightedLuminance);

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
        common->Printf("  emissive[%d]: instance=%u primitive=%u materialIndex=%u materialId=%u area=%.2f lum=%.3f weight=%.3f center=(%.2f %.2f %.2f) centroidUV=(%.3f %.3f) uvBounds=(%.3f %.3f %.3f %.3f) estRadiance=(%.3f %.3f %.3f) emissiveSlot=%d emissiveSize=%ux%u material='%s' emissive='%s'\n",
            sampleIndex,
            record.instanceId,
            record.primitiveIndex,
            record.materialIndex,
            materialId,
            record.centerAndArea[3],
            record.normalAndLuminance[3],
            record.centroidUvAndWeight[2],
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
