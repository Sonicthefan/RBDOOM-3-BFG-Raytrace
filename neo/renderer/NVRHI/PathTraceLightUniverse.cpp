#include "precompiled.h"
#pragma hdrstop

#include "PathTraceLightUniverse.h"
#include "PathTraceSceneCapture.h"
#include "PathTraceSurfaceClassification.h"
#include "../RenderCommon.h"

#include <algorithm>

namespace {

bool IsStaticEmissiveCandidate(const PathTraceSmokeEmissiveTriangle& triangle)
{
    return triangle.instanceId == 0;
}

uint32_t TriangleSurfaceClass(const PathTraceSmokeEmissiveTriangle& triangle)
{
    return triangle.padding0 & RT_SMOKE_TRIANGLE_CLASS_MASK;
}

uint32_t TriangleTranslucentSubtype(const PathTraceSmokeEmissiveTriangle& triangle)
{
    return (triangle.padding0 & RT_SMOKE_TRANSLUCENT_SUBTYPE_MASK) >> RT_SMOKE_TRANSLUCENT_SUBTYPE_SHIFT;
}

uint64 HashLightUniverseValue(uint64 hash, uint32 value)
{
    hash ^= static_cast<uint64>(value);
    hash *= 1099511628211ull;
    return hash;
}

uint32 QuantizeLightUniverseFloat(float value, float scale)
{
    const int quantized = idMath::Ftoi(value * scale);
    return static_cast<uint32>(quantized);
}

int ResolveCurrentLightUniverseArea(const viewDef_t* viewDef)
{
    idRenderWorldLocal* renderWorld = viewDef ? viewDef->renderWorld : nullptr;
    if (!renderWorld)
    {
        return -1;
    }

    int area = viewDef->areaNum;
    if (area < 0)
    {
        area = renderWorld->PointInArea(viewDef->initialViewAreaOrigin);
    }
    if (area < 0)
    {
        area = renderWorld->PointInArea(viewDef->renderView.vieworg);
    }
    return area;
}

std::vector<int> ResolveLightUniverseSeedAreas(const viewDef_t* viewDef)
{
    std::vector<int> seedAreas;
    idRenderWorldLocal* renderWorld = viewDef ? viewDef->renderWorld : nullptr;
    if (!renderWorld)
    {
        return seedAreas;
    }

    const int areaCount = renderWorld->NumAreas();
    auto addSeedArea = [&](const int area) {
        if (area < 0 || area >= areaCount)
        {
            return;
        }
        if (std::find(seedAreas.begin(), seedAreas.end(), area) == seedAreas.end())
        {
            seedAreas.push_back(area);
        }
    };

    addSeedArea(viewDef->areaNum);
    addSeedArea(renderWorld->PointInArea(viewDef->initialViewAreaOrigin));
    addSeedArea(renderWorld->PointInArea(viewDef->renderView.vieworg));

    const idVec3& viewOrigin = viewDef->renderView.vieworg;
    const float probeDistance = 8.0f;
    addSeedArea(renderWorld->PointInArea(viewOrigin + viewDef->renderView.viewaxis[0] * probeDistance));
    addSeedArea(renderWorld->PointInArea(viewOrigin - viewDef->renderView.viewaxis[0] * probeDistance));
    addSeedArea(renderWorld->PointInArea(viewOrigin + viewDef->renderView.viewaxis[1] * probeDistance));
    addSeedArea(renderWorld->PointInArea(viewOrigin - viewDef->renderView.viewaxis[1] * probeDistance));
    addSeedArea(renderWorld->PointInArea(viewOrigin + viewDef->renderView.viewaxis[2] * probeDistance));
    addSeedArea(renderWorld->PointInArea(viewOrigin - viewDef->renderView.viewaxis[2] * probeDistance));
    return seedAreas;
}

int ResolveLightUniverseTriangleArea(const viewDef_t* viewDef, const PathTraceSmokeEmissiveTriangle& triangle)
{
    idRenderWorldLocal* renderWorld = viewDef ? viewDef->renderWorld : nullptr;
    if (!renderWorld)
    {
        return -1;
    }

    const idVec3 center(triangle.centerAndArea[0], triangle.centerAndArea[1], triangle.centerAndArea[2]);
    int area = renderWorld->PointInArea(center);
    if (area >= 0)
    {
        return area;
    }

    const idVec3 normal(triangle.normalAndLuminance[0], triangle.normalAndLuminance[1], triangle.normalAndLuminance[2]);
    if (normal.LengthSqr() > 1.0e-8f)
    {
        idVec3 offsetNormal = normal;
        offsetNormal.Normalize();
        area = renderWorld->PointInArea(center + offsetNormal * 2.0f);
        if (area >= 0)
        {
            return area;
        }
        area = renderWorld->PointInArea(center - offsetNormal * 2.0f);
    }
    return area;
}

void AccumulateLightUniverseAreaStats(
    const viewDef_t* viewDef,
    int areaNum,
    bool staticCandidate,
    bool mergedCandidate,
    const std::vector<bool>* selectedAreas,
    RtSmokeLightUniverseStats& stats)
{
    if (!mergedCandidate)
    {
        if (areaNum >= 0)
        {
            if (staticCandidate)
            {
                ++stats.staticAreaKnownTriangles;
            }
            else
            {
                ++stats.dynamicAreaKnownTriangles;
            }
        }
        else
        {
            if (staticCandidate)
            {
                ++stats.staticAreaUnknownTriangles;
            }
            else
            {
                ++stats.dynamicAreaUnknownTriangles;
            }
        }
    }

    if (!mergedCandidate)
    {
        return;
    }

    if (areaNum < 0)
    {
        ++stats.mergedAreaUnknownTriangles;
        return;
    }

    ++stats.mergedAreaKnownTriangles;
    if (areaNum == stats.currentArea)
    {
        ++stats.mergedCurrentAreaTriangles;
    }
    if (selectedAreas && areaNum < static_cast<int>(selectedAreas->size()) && (*selectedAreas)[areaNum])
    {
        ++stats.mergedSelectedAreaTriangles;
    }
    else if (viewDef && viewDef->connectedAreas && areaNum < stats.totalAreas && viewDef->connectedAreas[areaNum])
    {
        ++stats.mergedConnectedAreaTriangles;
        ++stats.mergedConnectedUnselectedAreaTriangles;
    }
    else
    {
        ++stats.mergedDisconnectedAreaTriangles;
    }
}

std::vector<bool> BuildLightUniverseSelectedAreas(const viewDef_t* viewDef, int portalSteps, int* portalEdges, int* blockedPortalEdges)
{
    std::vector<bool> selectedAreas;
    idRenderWorldLocal* renderWorld = viewDef ? viewDef->renderWorld : nullptr;
    const std::vector<int> seedAreas = ResolveLightUniverseSeedAreas(viewDef);
    if (!renderWorld || seedAreas.empty())
    {
        return selectedAreas;
    }

    const int areaCount = renderWorld->NumAreas();

    portalSteps = idMath::ClampInt(0, 8, portalSteps);
    selectedAreas.assign(areaCount, false);
    std::vector<int> selectedDepth(areaCount, -1);
    std::vector<int> queue;
    queue.reserve(areaCount);

    for (int seedArea : seedAreas)
    {
        selectedAreas[seedArea] = true;
        selectedDepth[seedArea] = 0;
        queue.push_back(seedArea);
    }

    for (size_t queueIndex = 0; queueIndex < queue.size(); ++queueIndex)
    {
        const int area = queue[queueIndex];
        const int depth = selectedDepth[area];
        if (depth >= portalSteps)
        {
            continue;
        }

        const int portalCount = renderWorld->NumPortalsInArea(area);
        for (int portalIndex = 0; portalIndex < portalCount; ++portalIndex)
        {
            const exitPortal_t portal = renderWorld->GetPortal(area, portalIndex);
            if ((portal.blockingBits & PS_BLOCK_VIEW) != 0)
            {
                if (blockedPortalEdges)
                {
                    ++(*blockedPortalEdges);
                }
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

            if (portalEdges)
            {
                ++(*portalEdges);
            }
            if (!selectedAreas[nextArea])
            {
                selectedAreas[nextArea] = true;
                selectedDepth[nextArea] = depth + 1;
                queue.push_back(nextArea);
            }
        }
    }

    return selectedAreas;
}

int CountLightUniverseSelectedAreas(const std::vector<bool>& selectedAreas)
{
    int count = 0;
    for (bool selected : selectedAreas)
    {
        if (selected)
        {
            ++count;
        }
    }
    return count;
}

int CountLightUniverseMergedSelectedTriangles(const std::vector<int>& mergedAreas, const std::vector<bool>& selectedAreas)
{
    int count = 0;
    for (int areaNum : mergedAreas)
    {
        if (areaNum >= 0 && areaNum < static_cast<int>(selectedAreas.size()) && selectedAreas[areaNum])
        {
            ++count;
        }
    }
    return count;
}

void BuildLightUniversePortalDepthBins(const viewDef_t* viewDef, const std::vector<int>& mergedAreas, RtSmokeLightUniverseStats& stats)
{
    std::vector<bool> sweepAreas[RT_SMOKE_LIGHT_UNIVERSE_PORTAL_SWEEP_STEPS];
    for (int step = 0; step < RT_SMOKE_LIGHT_UNIVERSE_PORTAL_SWEEP_STEPS; ++step)
    {
        sweepAreas[step] = BuildLightUniverseSelectedAreas(viewDef, step, nullptr, nullptr);
    }

    for (int areaNum : mergedAreas)
    {
        if (areaNum < 0)
        {
            ++stats.mergedPortalDepthBins[RT_SMOKE_LIGHT_UNIVERSE_PORTAL_SWEEP_STEPS + 2];
            continue;
        }

        bool assigned = false;
        for (int step = 0; step < RT_SMOKE_LIGHT_UNIVERSE_PORTAL_SWEEP_STEPS; ++step)
        {
            if (areaNum < static_cast<int>(sweepAreas[step].size()) && sweepAreas[step][areaNum])
            {
                ++stats.mergedPortalDepthBins[step];
                assigned = true;
                break;
            }
        }
        if (assigned)
        {
            continue;
        }

        if (viewDef && viewDef->connectedAreas && areaNum < stats.totalAreas && viewDef->connectedAreas[areaNum])
        {
            ++stats.mergedPortalDepthBins[RT_SMOKE_LIGHT_UNIVERSE_PORTAL_SWEEP_STEPS];
        }
        else
        {
            ++stats.mergedPortalDepthBins[RT_SMOKE_LIGHT_UNIVERSE_PORTAL_SWEEP_STEPS + 1];
        }
    }
}

float LightUniverseCandidateDistance(const viewDef_t* viewDef, const PathTraceSmokeEmissiveTriangle& triangle)
{
    if (!viewDef)
    {
        return 0.0f;
    }
    const idVec3 center(triangle.centerAndArea[0], triangle.centerAndArea[1], triangle.centerAndArea[2]);
    return (center - viewDef->renderView.vieworg).Length();
}

RtSmokeLightUniverseCandidateSample MakeLightUniverseCandidateSample(
    const viewDef_t* viewDef,
    const PathTraceSmokeEmissiveTriangle& triangle,
    int areaNum,
    const char* reason)
{
    RtSmokeLightUniverseCandidateSample sample;
    sample.valid = true;
    sample.areaNum = areaNum;
    sample.materialId = triangle.materialId;
    sample.materialIndex = triangle.materialIndex;
    sample.weight = triangle.sampleWeightAndPdf[0];
    sample.area = triangle.centerAndArea[3];
    sample.distance = LightUniverseCandidateDistance(viewDef, triangle);
    sample.reason = reason;
    return sample;
}

void AddLightUniverseRankedSample(
    RtSmokeLightUniverseCandidateSample* samples,
    int maxSamples,
    int& sampleCount,
    const RtSmokeLightUniverseCandidateSample& sample)
{
    int insertIndex = -1;
    for (int sampleIndex = 0; sampleIndex < maxSamples; ++sampleIndex)
    {
        if (!samples[sampleIndex].valid || sample.weight > samples[sampleIndex].weight)
        {
            insertIndex = sampleIndex;
            break;
        }
    }
    if (insertIndex < 0)
    {
        return;
    }

    for (int sampleIndex = maxSamples - 1; sampleIndex > insertIndex; --sampleIndex)
    {
        samples[sampleIndex] = samples[sampleIndex - 1];
    }
    samples[insertIndex] = sample;
    sampleCount = Min(maxSamples, sampleCount + 1);
}

void AddLightUniverseOverflowSample(
    const viewDef_t* viewDef,
    const PathTraceSmokeEmissiveTriangle& triangle,
    int areaNum,
    RtSmokeLightUniverseStats& stats)
{
    const RtSmokeLightUniverseCandidateSample sample = MakeLightUniverseCandidateSample(viewDef, triangle, areaNum, "connectedOverflow");
    AddLightUniverseRankedSample(stats.overflowSamples, RT_SMOKE_LIGHT_UNIVERSE_OVERFLOW_SAMPLES, stats.overflowSampleCount, sample);
}

void AddLightUniverseDroppedSample(
    const viewDef_t* viewDef,
    const PathTraceSmokeEmissiveTriangle& triangle,
    int areaNum,
    const char* reason,
    RtSmokeLightUniverseStats& stats)
{
    const RtSmokeLightUniverseCandidateSample sample = MakeLightUniverseCandidateSample(viewDef, triangle, areaNum, reason);
    AddLightUniverseRankedSample(stats.droppedSamples, RT_SMOKE_LIGHT_UNIVERSE_DROPPED_SAMPLES, stats.droppedSampleCount, sample);
}

void BuildLightUniverseAreaFilterDiagnostics(
    const viewDef_t* viewDef,
    const std::vector<PathTraceSmokeEmissiveTriangle>& merged,
    const std::vector<int>& mergedAreas,
    const std::vector<bool>& selectedAreas,
    bool areaFilterEnabled,
    int overflowMax,
    RtSmokeLightUniverseStats& stats)
{
    stats.areaFilterEnabled = areaFilterEnabled ? 1 : 0;
    stats.areaFilterPortalSteps = stats.selectedPortalSteps;
    stats.areaFilterOverflowMax = Max(0, overflowMax);

    const int candidateCount = Min(static_cast<int>(merged.size()), static_cast<int>(mergedAreas.size()));
    struct OverflowCandidate
    {
        int index = -1;
        float weight = 0.0f;
        float distance = 0.0f;
    };
    std::vector<OverflowCandidate> overflowCandidates;
    overflowCandidates.reserve(candidateCount);
    for (int candidateIndex = 0; candidateIndex < candidateCount; ++candidateIndex)
    {
        stats.areaFilterPreArea += merged[candidateIndex].centerAndArea[3];
        stats.areaFilterPreWeight += merged[candidateIndex].sampleWeightAndPdf[0];
        const int areaNum = mergedAreas[candidateIndex];
        if (areaNum < 0)
        {
            ++stats.areaFilterUnknownCandidates;
            continue;
        }

        if (areaNum < static_cast<int>(selectedAreas.size()) && selectedAreas[areaNum])
        {
            ++stats.areaFilterSelectedCandidates;
            stats.areaFilterPostArea += merged[candidateIndex].centerAndArea[3];
            stats.areaFilterPostWeight += merged[candidateIndex].sampleWeightAndPdf[0];
            continue;
        }

        if (viewDef && viewDef->connectedAreas && areaNum < stats.totalAreas && viewDef->connectedAreas[areaNum])
        {
            ++stats.areaFilterConnectedOverflowCandidates;
            AddLightUniverseOverflowSample(viewDef, merged[candidateIndex], areaNum, stats);
            OverflowCandidate candidate;
            candidate.index = candidateIndex;
            candidate.weight = merged[candidateIndex].sampleWeightAndPdf[0];
            candidate.distance = LightUniverseCandidateDistance(viewDef, merged[candidateIndex]);
            overflowCandidates.push_back(candidate);
        }
        else
        {
            ++stats.areaFilterDisconnectedCandidates;
            stats.areaFilterDroppedDisconnectedWeight += merged[candidateIndex].sampleWeightAndPdf[0];
            AddLightUniverseDroppedSample(viewDef, merged[candidateIndex], areaNum, "disconnected", stats);
        }
    }

    std::sort(overflowCandidates.begin(), overflowCandidates.end(),
        [](const OverflowCandidate& lhs, const OverflowCandidate& rhs)
        {
            if (lhs.weight != rhs.weight)
            {
                return lhs.weight > rhs.weight;
            }
            return lhs.distance < rhs.distance;
        });

    const int cappedOverflow = Min(stats.areaFilterConnectedOverflowCandidates, stats.areaFilterOverflowMax);
    for (int overflowIndex = 0; overflowIndex < static_cast<int>(overflowCandidates.size()); ++overflowIndex)
    {
        const int candidateIndex = overflowCandidates[overflowIndex].index;
        if (candidateIndex < 0 || candidateIndex >= candidateCount)
        {
            continue;
        }
        if (overflowIndex < cappedOverflow)
        {
            stats.areaFilterPostArea += merged[candidateIndex].centerAndArea[3];
            stats.areaFilterPostWeight += merged[candidateIndex].sampleWeightAndPdf[0];
        }
        else
        {
            stats.areaFilterDroppedOverflowWeight += merged[candidateIndex].sampleWeightAndPdf[0];
            AddLightUniverseDroppedSample(viewDef, merged[candidateIndex], mergedAreas[candidateIndex], "overflow", stats);
        }
    }
    for (int candidateIndex = 0; candidateIndex < candidateCount; ++candidateIndex)
    {
        if (mergedAreas[candidateIndex] < 0)
        {
            stats.areaFilterDroppedUnknownWeight += merged[candidateIndex].sampleWeightAndPdf[0];
            AddLightUniverseDroppedSample(viewDef, merged[candidateIndex], mergedAreas[candidateIndex], "unknown", stats);
        }
    }
    stats.areaFilterDroppedArea = Max(0.0f, stats.areaFilterPreArea - stats.areaFilterPostArea);
    stats.areaFilterDroppedWeight = Max(0.0f, stats.areaFilterPreWeight - stats.areaFilterPostWeight);
    stats.areaFilterWouldUploadCandidates = stats.areaFilterSelectedCandidates + cappedOverflow;
    stats.areaFilterWouldDropCandidates = Max(0, candidateCount - stats.areaFilterWouldUploadCandidates);
}

std::vector<PathTraceSmokeEmissiveTriangle> ApplyLightUniverseAreaFilter(
    const viewDef_t* viewDef,
    const std::vector<PathTraceSmokeEmissiveTriangle>& merged,
    const std::vector<int>& mergedAreas,
    const std::vector<bool>& selectedAreas,
    int overflowMax)
{
    struct OverflowCandidate
    {
        int index = -1;
        float weight = 0.0f;
        float distance = 0.0f;
    };

    std::vector<PathTraceSmokeEmissiveTriangle> filtered;
    std::vector<OverflowCandidate> overflowCandidates;
    const int candidateCount = Min(static_cast<int>(merged.size()), static_cast<int>(mergedAreas.size()));
    filtered.reserve(candidateCount);
    overflowCandidates.reserve(candidateCount);
    overflowMax = Max(0, overflowMax);

    for (int candidateIndex = 0; candidateIndex < candidateCount; ++candidateIndex)
    {
        const int areaNum = mergedAreas[candidateIndex];
        if (areaNum < 0)
        {
            continue;
        }

        if (areaNum < static_cast<int>(selectedAreas.size()) && selectedAreas[areaNum])
        {
            filtered.push_back(merged[candidateIndex]);
            continue;
        }

        if (viewDef && viewDef->connectedAreas && areaNum < static_cast<int>(selectedAreas.size()) && viewDef->connectedAreas[areaNum])
        {
            OverflowCandidate candidate;
            candidate.index = candidateIndex;
            candidate.weight = merged[candidateIndex].sampleWeightAndPdf[0];
            candidate.distance = LightUniverseCandidateDistance(viewDef, merged[candidateIndex]);
            overflowCandidates.push_back(candidate);
        }
    }

    std::sort(overflowCandidates.begin(), overflowCandidates.end(),
        [](const OverflowCandidate& lhs, const OverflowCandidate& rhs)
        {
            if (lhs.weight != rhs.weight)
            {
                return lhs.weight > rhs.weight;
            }
            return lhs.distance < rhs.distance;
        });

    const int overflowCount = Min(overflowMax, static_cast<int>(overflowCandidates.size()));
    for (int overflowIndex = 0; overflowIndex < overflowCount; ++overflowIndex)
    {
        const int candidateIndex = overflowCandidates[overflowIndex].index;
        if (candidateIndex >= 0 && candidateIndex < static_cast<int>(merged.size()))
        {
            filtered.push_back(merged[candidateIndex]);
        }
    }

    if (filtered.empty())
    {
        filtered.resize(1);
    }
    return filtered;
}

}

void RtSmokeLightUniverse::Clear()
{
    m_generation = 1;
    m_staticRecords.clear();
    m_dynamicRecords.clear();
    m_staticLookup.clear();
    m_dynamicLookup.clear();
    m_activeLookup.clear();
    m_stats = RtSmokeLightUniverseStats();
}

uint64 RtSmokeLightUniverse::CandidateKey(const PathTraceSmokeEmissiveTriangle& triangle)
{
    const uint64 lo = static_cast<uint64>(triangle.identityHashLo);
    const uint64 hi = static_cast<uint64>(triangle.identityHashHi);
    return lo | (hi << 32);
}

uint64 RtSmokeLightUniverse::DynamicCandidateKey(const PathTraceSmokeEmissiveTriangle& triangle)
{
    uint64 hash = 1469598103934665603ull;
    hash = HashLightUniverseValue(hash, triangle.materialId);
    hash = HashLightUniverseValue(hash, triangle.universeMaterialIndex);
    hash = HashLightUniverseValue(hash, triangle.emissiveTextureIndex);
    hash = HashLightUniverseValue(hash, TriangleSurfaceClass(triangle));
    hash = HashLightUniverseValue(hash, TriangleTranslucentSubtype(triangle));
    hash = HashLightUniverseValue(hash, QuantizeLightUniverseFloat(triangle.centerAndArea[0], 16.0f));
    hash = HashLightUniverseValue(hash, QuantizeLightUniverseFloat(triangle.centerAndArea[1], 16.0f));
    hash = HashLightUniverseValue(hash, QuantizeLightUniverseFloat(triangle.centerAndArea[2], 16.0f));
    hash = HashLightUniverseValue(hash, QuantizeLightUniverseFloat(triangle.centerAndArea[3], 64.0f));
    hash = HashLightUniverseValue(hash, QuantizeLightUniverseFloat(triangle.normalAndLuminance[0], 1024.0f));
    hash = HashLightUniverseValue(hash, QuantizeLightUniverseFloat(triangle.normalAndLuminance[1], 1024.0f));
    hash = HashLightUniverseValue(hash, QuantizeLightUniverseFloat(triangle.normalAndLuminance[2], 1024.0f));
    return hash;
}

uint64 RtSmokeLightUniverse::ActiveCandidateKey(const PathTraceSmokeEmissiveTriangle& triangle)
{
    const uint64 stableKey = CandidateKey(triangle);
    if (stableKey != 0)
    {
        return stableKey;
    }
    return DynamicCandidateKey(triangle);
}

bool RtSmokeLightUniverse::IsPersistableDynamicCandidate(const PathTraceSmokeEmissiveTriangle& triangle)
{
    if (IsStaticEmissiveCandidate(triangle))
    {
        return false;
    }
    const uint32_t surfaceClass = TriangleSurfaceClass(triangle);
    if (surfaceClass == SmokeSurfaceClassId(RtSmokeSurfaceClass::RigidEntity))
    {
        return true;
    }

    if (surfaceClass != SmokeSurfaceClassId(RtSmokeSurfaceClass::ParticleAlpha))
    {
        return false;
    }

    const uint32_t subtype = TriangleTranslucentSubtype(triangle);
    return subtype == SmokeTranslucentSubtypeId(RtSmokeTranslucentSubtype::ObjectGlass) ||
        subtype == SmokeTranslucentSubtypeId(RtSmokeTranslucentSubtype::SignageGlow) ||
        subtype == SmokeTranslucentSubtypeId(RtSmokeTranslucentSubtype::GuiScreen);
}

void RtSmokeLightUniverse::UpdateActiveChurn(
    const viewDef_t* viewDef,
    const std::vector<PathTraceSmokeEmissiveTriangle>& activeCandidates,
    bool activeChurnEnabled)
{
    m_stats.activeChurnEnabled = activeChurnEnabled ? 1 : 0;
    if (!activeChurnEnabled)
    {
        m_activeLookup.clear();
        return;
    }

    std::unordered_map<uint64, ActiveEmissiveRecord> currentLookup;
    currentLookup.reserve(activeCandidates.size());

    for (const PathTraceSmokeEmissiveTriangle& triangle : activeCandidates)
    {
        if (triangle.sampleWeightAndPdf[0] <= 0.0f && triangle.centerAndArea[3] <= 0.0f)
        {
            continue;
        }
        const uint64 key = ActiveCandidateKey(triangle);
        if (key == 0)
        {
            continue;
        }

        ActiveEmissiveRecord record;
        const int areaNum = ResolveLightUniverseTriangleArea(viewDef, triangle);
        record.sample = MakeLightUniverseCandidateSample(viewDef, triangle, areaNum, nullptr);
        currentLookup[key] = record;
    }

    m_stats.activeChurnPrevious = static_cast<int>(m_activeLookup.size());
    m_stats.activeChurnCurrent = static_cast<int>(currentLookup.size());

    for (const auto& currentPair : currentLookup)
    {
        const RtSmokeLightUniverseCandidateSample& sample = currentPair.second.sample;
        m_stats.activeChurnCurrentWeight += sample.weight;
        const auto previousFound = m_activeLookup.find(currentPair.first);
        if (previousFound != m_activeLookup.end())
        {
            ++m_stats.activeChurnStayed;
            m_stats.activeChurnStayedWeight += sample.weight;
        }
        else
        {
            ++m_stats.activeChurnEntered;
            m_stats.activeChurnEnteredWeight += sample.weight;
            RtSmokeLightUniverseCandidateSample enteredSample = sample;
            enteredSample.reason = "entered";
            AddLightUniverseRankedSample(m_stats.enteredSamples, RT_SMOKE_LIGHT_UNIVERSE_CHURN_SAMPLES, m_stats.enteredSampleCount, enteredSample);
        }
    }

    for (const auto& previousPair : m_activeLookup)
    {
        const RtSmokeLightUniverseCandidateSample& sample = previousPair.second.sample;
        m_stats.activeChurnPreviousWeight += sample.weight;
        if (currentLookup.find(previousPair.first) != currentLookup.end())
        {
            continue;
        }
        ++m_stats.activeChurnLeft;
        m_stats.activeChurnLeftWeight += sample.weight;
        RtSmokeLightUniverseCandidateSample leftSample = sample;
        leftSample.reason = "left";
        AddLightUniverseRankedSample(m_stats.leftSamples, RT_SMOKE_LIGHT_UNIVERSE_CHURN_SAMPLES, m_stats.leftSampleCount, leftSample);
    }

    m_activeLookup.swap(currentLookup);
}

std::vector<PathTraceSmokeEmissiveTriangle> RtSmokeLightUniverse::MergeFrameCandidates(
    const viewDef_t* viewDef,
    const std::vector<PathTraceSmokeEmissiveTriangle>& frameCandidates,
    int maxRecords,
    int selectedPortalSteps,
    bool areaFilterEnabled,
    bool areaFilterApply,
    int areaFilterOverflowMax,
    bool activeChurnEnabled,
    bool persistDynamic,
    bool injectMissingDynamic,
    int dynamicMinSeenFrames,
    int dynamicMaxMissingFrames)
{
    OPTICK_EVENT("PT Light Universe Merge");

    ++m_generation;
    maxRecords = Max(1, maxRecords);
    dynamicMinSeenFrames = Max(1, dynamicMinSeenFrames);
    dynamicMaxMissingFrames = Max(1, dynamicMaxMissingFrames);
    m_stats = RtSmokeLightUniverseStats();
    m_stats.generation = m_generation;
    idRenderWorldLocal* renderWorld = viewDef ? viewDef->renderWorld : nullptr;
    m_stats.currentArea = ResolveCurrentLightUniverseArea(viewDef);
    m_stats.totalAreas = renderWorld ? renderWorld->NumAreas() : 0;
    m_stats.selectedPortalSteps = idMath::ClampInt(0, 8, selectedPortalSteps);
    const std::vector<bool> selectedAreas = BuildLightUniverseSelectedAreas(viewDef, m_stats.selectedPortalSteps, &m_stats.selectedPortalEdges, &m_stats.selectedBlockedPortalEdges);
    m_stats.selectedAreaCount = CountLightUniverseSelectedAreas(selectedAreas);

    for (PersistentEmissiveRecord& record : m_staticRecords)
    {
        record.seenThisFrame = false;
    }
    for (PersistentEmissiveRecord& record : m_dynamicRecords)
    {
        record.seenThisFrame = false;
    }

    std::vector<PathTraceSmokeEmissiveTriangle> dynamicFrameCandidates;
    dynamicFrameCandidates.reserve(frameCandidates.size());

    for (const PathTraceSmokeEmissiveTriangle& triangle : frameCandidates)
    {
        if (triangle.sampleWeightAndPdf[0] <= 0.0f && triangle.centerAndArea[3] <= 0.0f)
        {
            continue;
        }

        if (!IsStaticEmissiveCandidate(triangle))
        {
            const int areaNum = ResolveLightUniverseTriangleArea(viewDef, triangle);
            AccumulateLightUniverseAreaStats(viewDef, areaNum, false, false, &selectedAreas, m_stats);
            if (!persistDynamic || !IsPersistableDynamicCandidate(triangle))
            {
                ++m_stats.dynamicFrameOnlyTriangles;
                dynamicFrameCandidates.push_back(triangle);
                continue;
            }

            ++m_stats.dynamicPersistableFrameTriangles;
            const uint64 key = DynamicCandidateKey(triangle);
            if (key == 0)
            {
                ++m_stats.dynamicFrameOnlyTriangles;
                dynamicFrameCandidates.push_back(triangle);
                continue;
            }

            const auto found = m_dynamicLookup.find(key);
            if (found == m_dynamicLookup.end())
            {
                PersistentEmissiveRecord record;
                record.triangle = triangle;
                record.key = key;
                record.lastSeenGeneration = m_generation;
                record.areaNum = areaNum;
                record.seenThisFrame = true;
                record.seenFrames = 1;
                record.promoted = dynamicMinSeenFrames <= 1;
                m_dynamicLookup[key] = m_dynamicRecords.size();
                m_dynamicRecords.push_back(record);
                dynamicFrameCandidates.push_back(triangle);
                if (record.promoted)
                {
                    ++m_stats.dynamicPromotedThisFrame;
                    ++m_stats.dynamicPromotedFrameTriangles;
                }
                else
                {
                    ++m_stats.dynamicUnpromotedFrameTriangles;
                }
                continue;
            }

            PersistentEmissiveRecord& record = m_dynamicRecords[found->second];
            record.triangle = triangle;
            record.lastSeenGeneration = m_generation;
            record.areaNum = areaNum;
            record.seenThisFrame = true;
            ++record.seenFrames;
            if (!record.promoted && record.seenFrames >= dynamicMinSeenFrames)
            {
                record.promoted = true;
                ++m_stats.dynamicPromotedThisFrame;
            }
            else
            {
                ++m_stats.dynamicUpdatedThisFrame;
            }
            if (record.promoted)
            {
                ++m_stats.dynamicPromotedFrameTriangles;
            }
            else
            {
                ++m_stats.dynamicUnpromotedFrameTriangles;
            }
            dynamicFrameCandidates.push_back(triangle);
            continue;
        }

        const uint64 key = CandidateKey(triangle);
        if (key == 0)
        {
            continue;
        }
        const int areaNum = ResolveLightUniverseTriangleArea(viewDef, triangle);
        AccumulateLightUniverseAreaStats(viewDef, areaNum, true, false, &selectedAreas, m_stats);

        const auto found = m_staticLookup.find(key);
        if (found == m_staticLookup.end())
        {
            if (static_cast<int>(m_staticRecords.size()) >= maxRecords)
            {
                continue;
            }
            PersistentEmissiveRecord record;
            record.triangle = triangle;
            record.key = key;
            record.lastSeenGeneration = m_generation;
            record.areaNum = areaNum;
            record.seenThisFrame = true;
            record.seenFrames = 1;
            record.promoted = true;
            m_staticLookup[key] = m_staticRecords.size();
            m_staticRecords.push_back(record);
            ++m_stats.staticNewThisFrame;
        }
        else
        {
            PersistentEmissiveRecord& record = m_staticRecords[found->second];
            record.triangle = triangle;
            record.lastSeenGeneration = m_generation;
            record.areaNum = areaNum;
            record.seenThisFrame = true;
            ++record.seenFrames;
            ++m_stats.staticUpdatedThisFrame;
        }
    }

    if (!m_dynamicRecords.empty())
    {
        std::vector<PersistentEmissiveRecord> keptDynamicRecords;
        keptDynamicRecords.reserve(m_dynamicRecords.size());
        m_dynamicLookup.clear();
        for (PersistentEmissiveRecord& record : m_dynamicRecords)
        {
            const uint64 missingFrames = m_generation > record.lastSeenGeneration ? m_generation - record.lastSeenGeneration : 0;
            if (missingFrames > static_cast<uint64>(dynamicMaxMissingFrames))
            {
                ++m_stats.dynamicAgedOutThisFrame;
                continue;
            }
            m_dynamicLookup[record.key] = keptDynamicRecords.size();
            keptDynamicRecords.push_back(record);
        }
        m_dynamicRecords.swap(keptDynamicRecords);
    }

    std::vector<PathTraceSmokeEmissiveTriangle> merged;
    std::vector<int> mergedAreas;
    merged.reserve(Max(1, Min(maxRecords, static_cast<int>(m_staticRecords.size() + m_dynamicRecords.size() + dynamicFrameCandidates.size()))));
    mergedAreas.reserve(Max(1, Min(maxRecords, static_cast<int>(m_staticRecords.size() + m_dynamicRecords.size() + dynamicFrameCandidates.size()))));
    float totalArea = 0.0f;
    float totalWeightedLuminance = 0.0f;

    for (const PathTraceSmokeEmissiveTriangle& triangle : dynamicFrameCandidates)
    {
        if (static_cast<int>(merged.size()) >= maxRecords)
        {
            break;
        }
        const int areaNum = ResolveLightUniverseTriangleArea(viewDef, triangle);
        merged.push_back(triangle);
        mergedAreas.push_back(areaNum);
        AccumulateLightUniverseAreaStats(viewDef, areaNum, !IsStaticEmissiveCandidate(triangle) ? false : true, true, &selectedAreas, m_stats);
        totalArea += triangle.centerAndArea[3];
        totalWeightedLuminance += triangle.sampleWeightAndPdf[0];
    }

    for (const PersistentEmissiveRecord& record : m_staticRecords)
    {
        if (static_cast<int>(merged.size()) >= maxRecords)
        {
            if (record.seenThisFrame)
            {
                ++m_stats.staticSeenThisFrame;
            }
            else
            {
                ++m_stats.staticMissingThisFrame;
            }
            continue;
        }
        merged.push_back(record.triangle);
        mergedAreas.push_back(record.areaNum);
        AccumulateLightUniverseAreaStats(viewDef, record.areaNum, true, true, &selectedAreas, m_stats);
        totalArea += record.triangle.centerAndArea[3];
        totalWeightedLuminance += record.triangle.sampleWeightAndPdf[0];
        if (record.seenThisFrame)
        {
            ++m_stats.staticSeenThisFrame;
            ++m_stats.staticMergedSeenTriangles;
        }
        else
        {
            ++m_stats.staticMissingThisFrame;
            ++m_stats.staticMergedMissingTriangles;
        }
    }

    for (const PersistentEmissiveRecord& record : m_dynamicRecords)
    {
        if (!record.promoted)
        {
            continue;
        }
        if (record.seenThisFrame)
        {
            ++m_stats.dynamicSeenThisFrame;
            continue;
        }
        if (static_cast<int>(merged.size()) >= maxRecords)
        {
            break;
        }
        if (!injectMissingDynamic)
        {
            ++m_stats.dynamicMissingThisFrame;
            continue;
        }
        PathTraceSmokeEmissiveTriangle historyTriangle = record.triangle;
        historyTriangle.padding0 |= RT_SMOKE_EMISSIVE_TRIANGLE_HISTORY_DYNAMIC;
        merged.push_back(historyTriangle);
        mergedAreas.push_back(record.areaNum);
        AccumulateLightUniverseAreaStats(viewDef, record.areaNum, false, true, &selectedAreas, m_stats);
        totalArea += historyTriangle.centerAndArea[3];
        totalWeightedLuminance += historyTriangle.sampleWeightAndPdf[0];
        ++m_stats.dynamicMissingThisFrame;
        ++m_stats.injectedMissingDynamicTriangles;
    }

    const float inverseTotalWeightedLuminance = totalWeightedLuminance > 1.0e-8f ? 1.0f / totalWeightedLuminance : 0.0f;
    const float inverseTotalArea = totalArea > 1.0e-8f ? 1.0f / totalArea : 0.0f;
    for (PathTraceSmokeEmissiveTriangle& triangle : merged)
    {
        triangle.sampleWeightAndPdf[1] = triangle.sampleWeightAndPdf[0] * inverseTotalWeightedLuminance;
        triangle.sampleWeightAndPdf[3] = triangle.centerAndArea[3] * inverseTotalArea;
        triangle.centroidUvAndWeight[3] = triangle.sampleWeightAndPdf[1];
    }

    if (merged.empty())
    {
        merged.resize(1);
    }

    m_stats.persistentStaticTriangles = static_cast<int>(m_staticRecords.size());
    for (const PersistentEmissiveRecord& record : m_dynamicRecords)
    {
        if (record.promoted)
        {
            ++m_stats.persistentDynamicTriangles;
        }
    }
    m_stats.dynamicFrameTriangles = static_cast<int>(dynamicFrameCandidates.size());
    m_stats.mergedTriangles = static_cast<int>(merged.size());
    for (int step = 0; step < RT_SMOKE_LIGHT_UNIVERSE_PORTAL_SWEEP_STEPS; ++step)
    {
        std::vector<bool> sweepAreas = BuildLightUniverseSelectedAreas(viewDef, step, nullptr, nullptr);
        m_stats.portalStepSelectedAreas[step] = CountLightUniverseSelectedAreas(sweepAreas);
        m_stats.portalStepMergedSelectedTriangles[step] = CountLightUniverseMergedSelectedTriangles(mergedAreas, sweepAreas);
    }
    BuildLightUniversePortalDepthBins(viewDef, mergedAreas, m_stats);
    BuildLightUniverseAreaFilterDiagnostics(viewDef, merged, mergedAreas, selectedAreas, areaFilterEnabled, areaFilterOverflowMax, m_stats);
    if (areaFilterApply)
    {
        merged = ApplyLightUniverseAreaFilter(viewDef, merged, mergedAreas, selectedAreas, areaFilterOverflowMax);
        m_stats.areaFilterApplied = 1;
        float filteredArea = 0.0f;
        float filteredWeightedLuminance = 0.0f;
        for (const PathTraceSmokeEmissiveTriangle& triangle : merged)
        {
            filteredArea += triangle.centerAndArea[3];
            filteredWeightedLuminance += triangle.sampleWeightAndPdf[0];
        }
        const float inverseFilteredWeightedLuminance = filteredWeightedLuminance > 1.0e-8f ? 1.0f / filteredWeightedLuminance : 0.0f;
        const float inverseFilteredArea = filteredArea > 1.0e-8f ? 1.0f / filteredArea : 0.0f;
        for (PathTraceSmokeEmissiveTriangle& triangle : merged)
        {
            triangle.sampleWeightAndPdf[1] = triangle.sampleWeightAndPdf[0] * inverseFilteredWeightedLuminance;
            triangle.sampleWeightAndPdf[3] = triangle.centerAndArea[3] * inverseFilteredArea;
            triangle.centroidUvAndWeight[3] = triangle.sampleWeightAndPdf[1];
        }
    }
    UpdateActiveChurn(viewDef, merged, activeChurnEnabled);
    return merged;
}

RtSmokeLightUniverseStats RtSmokeLightUniverse::GetStats() const
{
    return m_stats;
}
