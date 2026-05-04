#include "precompiled.h"
#pragma hdrstop

#include "PathTraceLightUniverse.h"
#include "PathTraceSceneCapture.h"
#include "PathTraceSurfaceClassification.h"

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

}

void RtSmokeLightUniverse::Clear()
{
    m_generation = 1;
    m_staticRecords.clear();
    m_dynamicRecords.clear();
    m_staticLookup.clear();
    m_dynamicLookup.clear();
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

std::vector<PathTraceSmokeEmissiveTriangle> RtSmokeLightUniverse::MergeFrameCandidates(
    const std::vector<PathTraceSmokeEmissiveTriangle>& frameCandidates,
    int maxRecords,
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
            if (!persistDynamic || !IsPersistableDynamicCandidate(triangle))
            {
                dynamicFrameCandidates.push_back(triangle);
                continue;
            }

            const uint64 key = DynamicCandidateKey(triangle);
            if (key == 0)
            {
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
                record.seenThisFrame = true;
                record.seenFrames = 1;
                record.promoted = dynamicMinSeenFrames <= 1;
                m_dynamicLookup[key] = m_dynamicRecords.size();
                m_dynamicRecords.push_back(record);
                dynamicFrameCandidates.push_back(triangle);
                if (record.promoted)
                {
                    ++m_stats.dynamicPromotedThisFrame;
                }
                continue;
            }

            PersistentEmissiveRecord& record = m_dynamicRecords[found->second];
            record.triangle = triangle;
            record.lastSeenGeneration = m_generation;
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
            dynamicFrameCandidates.push_back(triangle);
            continue;
        }

        const uint64 key = CandidateKey(triangle);
        if (key == 0)
        {
            continue;
        }

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
    merged.reserve(Max(1, Min(maxRecords, static_cast<int>(m_staticRecords.size() + m_dynamicRecords.size() + dynamicFrameCandidates.size()))));
    float totalArea = 0.0f;
    float totalWeightedLuminance = 0.0f;

    for (const PersistentEmissiveRecord& record : m_staticRecords)
    {
        if (static_cast<int>(merged.size()) >= maxRecords)
        {
            break;
        }
        merged.push_back(record.triangle);
        totalArea += record.triangle.centerAndArea[3];
        totalWeightedLuminance += record.triangle.sampleWeightAndPdf[0];
        if (record.seenThisFrame)
        {
            ++m_stats.staticSeenThisFrame;
        }
        else
        {
            ++m_stats.staticMissingThisFrame;
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
        totalArea += historyTriangle.centerAndArea[3];
        totalWeightedLuminance += historyTriangle.sampleWeightAndPdf[0];
        ++m_stats.dynamicMissingThisFrame;
    }

    for (const PathTraceSmokeEmissiveTriangle& triangle : dynamicFrameCandidates)
    {
        if (static_cast<int>(merged.size()) >= maxRecords)
        {
            break;
        }
        merged.push_back(triangle);
        totalArea += triangle.centerAndArea[3];
        totalWeightedLuminance += triangle.sampleWeightAndPdf[0];
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
    return merged;
}

RtSmokeLightUniverseStats RtSmokeLightUniverse::GetStats() const
{
    return m_stats;
}
