#include "precompiled.h"
#pragma hdrstop

#include "PathTraceUnifiedLight.h"
#include "PathTraceDoomLights.h"
#include "PathTraceEmissiveCandidates.h"

#include <algorithm>
#include <cmath>

namespace {

float Max3(const float x, const float y, const float z)
{
    return std::max(std::max(x, y), z);
}

float Luminance(const float rgb[3])
{
    return rgb[0] * 0.2126f + rgb[1] * 0.7152f + rgb[2] * 0.0722f;
}

bool DoomAnalyticIdentitySampleable(const PathTraceDoomAnalyticLightCandidateIdentity& identity)
{
    return identity.universeIndex != PATH_TRACE_DOOM_ANALYTIC_LIGHT_INVALID_INDEX &&
        (identity.flags & PATH_TRACE_DOOM_ANALYTIC_IDENTITY_VALID) != 0u &&
        (identity.flags & PATH_TRACE_DOOM_ANALYTIC_IDENTITY_SAMPLEABLE) != 0u;
}

bool DoomAnalyticRemapValid(const PathTraceDoomAnalyticLightRemap& remap)
{
    return (remap.flags & PATH_TRACE_DOOM_ANALYTIC_IDENTITY_REMAP_VALID) != 0u;
}

bool EmissiveRemapValid(const PathTraceEmissiveLightRemap& remap)
{
    return (remap.flags & RT_SMOKE_EMISSIVE_REMAP_VALID) != 0u;
}

float DoomAnalyticLightColorMagnitude(const PathTraceDoomAnalyticLightCandidate& light)
{
    return Max3(
        std::max(light.colorAndIntensity[0], 0.0f),
        std::max(light.colorAndIntensity[1], 0.0f),
        std::max(light.colorAndIntensity[2], 0.0f));
}

bool DoomAnalyticLightStateCompatible(
    const PathTraceDoomAnalyticLightCandidate& currentLight,
    const PathTraceDoomAnalyticLightCandidate& previousLight,
    float tolerance)
{
    tolerance = std::max(0.0f, std::min(tolerance, 1.0f));
    const float currentColor[3] = {
        std::max(currentLight.colorAndIntensity[0], 0.0f),
        std::max(currentLight.colorAndIntensity[1], 0.0f),
        std::max(currentLight.colorAndIntensity[2], 0.0f)
    };
    const float previousColor[3] = {
        std::max(previousLight.colorAndIntensity[0], 0.0f),
        std::max(previousLight.colorAndIntensity[1], 0.0f),
        std::max(previousLight.colorAndIntensity[2], 0.0f)
    };
    const float colorScale = std::max(std::max(DoomAnalyticLightColorMagnitude(currentLight), DoomAnalyticLightColorMagnitude(previousLight)), 1.0e-4f);
    for (int i = 0; i < 3; ++i)
    {
        if (std::fabs(currentColor[i] - previousColor[i]) > colorScale * tolerance + 1.0e-4f)
        {
            return false;
        }
    }

    const float currentIntensity = std::max(currentLight.colorAndIntensity[3], 0.0f);
    const float previousIntensity = std::max(previousLight.colorAndIntensity[3], 0.0f);
    const float intensityScale = std::max(std::max(currentIntensity, previousIntensity), 1.0e-4f);
    if (std::fabs(currentIntensity - previousIntensity) > intensityScale * tolerance + 1.0e-4f)
    {
        return false;
    }

    const float currentInfluenceRadius = std::max(currentLight.doomRadiusAndArea[0], 1.0f);
    const float previousInfluenceRadius = std::max(previousLight.doomRadiusAndArea[0], 1.0f);
    const float positionTolerance = std::max(std::max(currentInfluenceRadius, previousInfluenceRadius) * 0.005f, 0.5f);
    const float dx = currentLight.originAndRadius[0] - previousLight.originAndRadius[0];
    const float dy = currentLight.originAndRadius[1] - previousLight.originAndRadius[1];
    const float dz = currentLight.originAndRadius[2] - previousLight.originAndRadius[2];
    if (std::sqrt(dx * dx + dy * dy + dz * dz) > positionTolerance)
    {
        return false;
    }

    const float radiusScale = std::max(std::max(std::fabs(currentLight.originAndRadius[3]), std::fabs(previousLight.originAndRadius[3])), 1.0e-4f);
    if (std::fabs(currentLight.originAndRadius[3] - previousLight.originAndRadius[3]) > radiusScale * tolerance + 1.0e-4f)
    {
        return false;
    }

    return true;
}

PathTraceUnifiedLightRecord BuildUnifiedEmissiveLightRecord(
    const PathTraceSmokeEmissiveTriangle& emissiveTriangle,
    uint32_t sourceIndex)
{
    PathTraceUnifiedLightRecord record;
    record.positionAndRadius[0] = emissiveTriangle.centerAndArea[0];
    record.positionAndRadius[1] = emissiveTriangle.centerAndArea[1];
    record.positionAndRadius[2] = emissiveTriangle.centerAndArea[2];
    record.positionAndRadius[3] = 0.0f;
    record.normalAndArea[0] = emissiveTriangle.normalAndLuminance[0];
    record.normalAndArea[1] = emissiveTriangle.normalAndLuminance[1];
    record.normalAndArea[2] = emissiveTriangle.normalAndLuminance[2];
    record.normalAndArea[3] = std::max(emissiveTriangle.centerAndArea[3], 0.0f);
    record.radianceAndLuminance[0] = std::max(emissiveTriangle.estimatedRadianceAndLuminance[0], 0.0f);
    record.radianceAndLuminance[1] = std::max(emissiveTriangle.estimatedRadianceAndLuminance[1], 0.0f);
    record.radianceAndLuminance[2] = std::max(emissiveTriangle.estimatedRadianceAndLuminance[2], 0.0f);
    record.radianceAndLuminance[3] = std::max(emissiveTriangle.estimatedRadianceAndLuminance[3], Luminance(record.radianceAndLuminance));
    record.uvOrDoomParams[0] = emissiveTriangle.uvBounds[0];
    record.uvOrDoomParams[1] = emissiveTriangle.uvBounds[1];
    record.uvOrDoomParams[2] = emissiveTriangle.uvBounds[2];
    record.uvOrDoomParams[3] = emissiveTriangle.uvBounds[3];
    record.type = PATH_TRACE_UNIFIED_LIGHT_TYPE_EMISSIVE_TRIANGLE;
    record.sourceIndex = sourceIndex;
    record.flags = emissiveTriangle.flags | emissiveTriangle.padding0;
    record.materialOrLightId = emissiveTriangle.materialIndex;
    record.instanceId = emissiveTriangle.instanceId;
    record.primitiveIndex = emissiveTriangle.primitiveIndex;
    record.identityA = emissiveTriangle.identityHashLo;
    record.identityB = emissiveTriangle.identityHashHi;
    record.sourcePdf = std::max(emissiveTriangle.sampleWeightAndPdf[1], 0.0f);
    record.sourceWeight = std::max(
        std::max(emissiveTriangle.sampleWeightAndPdf[0], emissiveTriangle.estimatedRadianceAndLuminance[3]),
        emissiveTriangle.centerAndArea[3] > 1.0e-6f ? 1.0e-6f : 0.0f);
    return record;
}

PathTraceUnifiedLightRecord BuildUnifiedDoomAnalyticLightRecord(
    const PathTraceDoomAnalyticLightCandidate& analyticLight,
    const PathTraceDoomAnalyticLightCandidateIdentity* identity,
    uint32_t sourceIndex)
{
    const float radius = std::max(analyticLight.originAndRadius[3], 0.01f);
    const float influenceRadius = std::max(analyticLight.doomRadiusAndArea[0], 1.0f);

    PathTraceUnifiedLightRecord record;
    record.positionAndRadius[0] = analyticLight.originAndRadius[0];
    record.positionAndRadius[1] = analyticLight.originAndRadius[1];
    record.positionAndRadius[2] = analyticLight.originAndRadius[2];
    record.positionAndRadius[3] = radius;
    record.normalAndArea[0] = 0.0f;
    record.normalAndArea[1] = 0.0f;
    record.normalAndArea[2] = 1.0f;
    record.normalAndArea[3] = 4.0f * idMath::PI * radius * radius;
    record.radianceAndLuminance[0] = std::max(analyticLight.colorAndIntensity[0], 0.0f);
    record.radianceAndLuminance[1] = std::max(analyticLight.colorAndIntensity[1], 0.0f);
    record.radianceAndLuminance[2] = std::max(analyticLight.colorAndIntensity[2], 0.0f);
    record.radianceAndLuminance[3] = Luminance(record.radianceAndLuminance);
    record.uvOrDoomParams[0] = influenceRadius;
    record.uvOrDoomParams[1] = analyticLight.doomRadiusAndArea[1];
    record.uvOrDoomParams[2] = analyticLight.doomRadiusAndArea[2];
    record.uvOrDoomParams[3] = analyticLight.doomRadiusAndArea[3];
    record.type = PATH_TRACE_UNIFIED_LIGHT_TYPE_DOOM_ANALYTIC;
    record.sourceIndex = sourceIndex;
    record.flags = analyticLight.flags | (identity ? identity->flags : 0u);
    record.materialOrLightId = analyticLight.renderLightIndex;
    record.instanceId = 0;
    record.primitiveIndex = 0;
    record.identityA = analyticLight.renderLightIndex;
    record.identityB = analyticLight.entityNumber;
    record.sourcePdf = 0.0f;
    record.sourceWeight = Max3(record.radianceAndLuminance[0], record.radianceAndLuminance[1], record.radianceAndLuminance[2]) * record.normalAndArea[3] * influenceRadius;
    return record;
}

} // namespace

PathTraceUnifiedLightBuild BuildPathTraceUnifiedLights(
    const std::vector<PathTraceSmokeEmissiveTriangle>& currentEmissiveTriangles,
    const std::vector<PathTraceSmokeEmissiveTriangle>& previousEmissiveTriangles,
    const std::vector<PathTraceEmissiveLightRemap>& emissiveRemap,
    const std::vector<PathTraceDoomAnalyticLightCandidate>& currentAnalyticLights,
    const std::vector<PathTraceDoomAnalyticLightCandidate>& previousAnalyticLights,
    const std::vector<PathTraceDoomAnalyticLightCandidateIdentity>& currentAnalyticIdentities,
    const std::vector<PathTraceDoomAnalyticLightCandidateIdentity>& previousAnalyticIdentities,
    const std::vector<PathTraceDoomAnalyticLightRemap>& analyticRemap,
    float analyticStateCompatibilityTolerance)
{
    PathTraceUnifiedLightBuild build;
    build.currentLights.reserve(currentEmissiveTriangles.size() + currentAnalyticLights.size());
    build.previousLights.reserve(previousEmissiveTriangles.size() + previousAnalyticLights.size());

    for (uint32_t emissiveIndex = 0; emissiveIndex < currentEmissiveTriangles.size(); ++emissiveIndex)
    {
        build.currentLights.push_back(BuildUnifiedEmissiveLightRecord(currentEmissiveTriangles[emissiveIndex], emissiveIndex));
    }
    for (uint32_t analyticIndex = 0; analyticIndex < currentAnalyticLights.size(); ++analyticIndex)
    {
        const PathTraceDoomAnalyticLightCandidateIdentity* identity =
            analyticIndex < currentAnalyticIdentities.size() ? &currentAnalyticIdentities[analyticIndex] : nullptr;
        build.currentLights.push_back(BuildUnifiedDoomAnalyticLightRecord(currentAnalyticLights[analyticIndex], identity, analyticIndex));
    }

    for (uint32_t emissiveIndex = 0; emissiveIndex < previousEmissiveTriangles.size(); ++emissiveIndex)
    {
        build.previousLights.push_back(BuildUnifiedEmissiveLightRecord(previousEmissiveTriangles[emissiveIndex], emissiveIndex));
    }
    for (uint32_t analyticIndex = 0; analyticIndex < previousAnalyticLights.size(); ++analyticIndex)
    {
        const PathTraceDoomAnalyticLightCandidateIdentity* identity =
            analyticIndex < previousAnalyticIdentities.size() ? &previousAnalyticIdentities[analyticIndex] : nullptr;
        build.previousLights.push_back(BuildUnifiedDoomAnalyticLightRecord(previousAnalyticLights[analyticIndex], identity, analyticIndex));
    }

    build.currentToPreviousRemap.assign(build.currentLights.size(), PATH_TRACE_UNIFIED_LIGHT_INVALID_INDEX);
    const uint32_t previousEmissiveCount = static_cast<uint32_t>(previousEmissiveTriangles.size());
    const uint32_t currentEmissiveCount = static_cast<uint32_t>(currentEmissiveTriangles.size());
    for (uint32_t emissiveIndex = 0; emissiveIndex < currentEmissiveTriangles.size(); ++emissiveIndex)
    {
        if (emissiveIndex >= emissiveRemap.size() || !EmissiveRemapValid(emissiveRemap[emissiveIndex]) || emissiveRemap[emissiveIndex].currentToPreviousIndex < 0)
        {
            continue;
        }

        const uint32_t previousIndex = static_cast<uint32_t>(emissiveRemap[emissiveIndex].currentToPreviousIndex);
        if (previousIndex < previousEmissiveCount)
        {
            build.currentToPreviousRemap[emissiveIndex] = previousIndex;
            build.currentLights[emissiveIndex].previousIndex = previousIndex;
        }
    }

    for (uint32_t analyticIndex = 0; analyticIndex < currentAnalyticLights.size(); ++analyticIndex)
    {
        if (analyticIndex >= currentAnalyticIdentities.size())
        {
            continue;
        }

        const PathTraceDoomAnalyticLightCandidateIdentity& identity = currentAnalyticIdentities[analyticIndex];
        if (!DoomAnalyticIdentitySampleable(identity) || identity.universeIndex >= analyticRemap.size())
        {
            continue;
        }

        const PathTraceDoomAnalyticLightRemap& remap = analyticRemap[identity.universeIndex];
        if (!DoomAnalyticRemapValid(remap) || remap.currentToPreviousCandidateIndex < 0)
        {
            continue;
        }

        const uint32_t previousAnalyticIndex = static_cast<uint32_t>(remap.currentToPreviousCandidateIndex);
        if (previousAnalyticIndex >= previousAnalyticLights.size())
        {
            continue;
        }
        if (!DoomAnalyticLightStateCompatible(currentAnalyticLights[analyticIndex], previousAnalyticLights[previousAnalyticIndex], analyticStateCompatibilityTolerance))
        {
            continue;
        }

        const uint32_t unifiedIndex = currentEmissiveCount + analyticIndex;
        const uint32_t previousUnifiedIndex = previousEmissiveCount + previousAnalyticIndex;
        if (unifiedIndex < build.currentToPreviousRemap.size() && previousUnifiedIndex < build.previousLights.size())
        {
            build.currentToPreviousRemap[unifiedIndex] = previousUnifiedIndex;
            build.currentLights[unifiedIndex].previousIndex = previousUnifiedIndex;
        }
    }

    return build;
}
