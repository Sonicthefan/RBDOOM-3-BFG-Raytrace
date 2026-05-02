#include "precompiled.h"
#pragma hdrstop

#include "PathTraceEmissiveCandidates.h"

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
