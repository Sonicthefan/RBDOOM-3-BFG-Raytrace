#include "precompiled.h"
#pragma hdrstop

#include "PathTraceRemixLightManager.h"
#include "PathTraceDoomLights.h"
#include "PathTraceEmissiveCandidates.h"

#include <algorithm>
#include <cstring>
#include <map>

namespace {

enum PathTraceRemixLightContractStatus : uint32_t
{
    PATH_TRACE_REMIX_LIGHT_CONTRACT_OK = 0u,
    PATH_TRACE_REMIX_LIGHT_CONTRACT_DISABLED = 1u,
    PATH_TRACE_REMIX_LIGHT_CONTRACT_NO_CURRENT_DOMAIN = 2u,
    PATH_TRACE_REMIX_LIGHT_CONTRACT_MAP_SIZE_MISMATCH = 3u,
    PATH_TRACE_REMIX_LIGHT_CONTRACT_DUPLICATE_IDENTITY = 4u
};

struct RemixUnifiedIdentityKey
{
    uint32_t type = 0;
    uint32_t identityA = 0;
    uint32_t identityB = 0;
    uint32_t materialOrLightId = 0;
};

bool operator<(const RemixUnifiedIdentityKey& a, const RemixUnifiedIdentityKey& b)
{
    if (a.type != b.type)
    {
        return a.type < b.type;
    }
    if (a.identityA != b.identityA)
    {
        return a.identityA < b.identityA;
    }
    if (a.identityB != b.identityB)
    {
        return a.identityB < b.identityB;
    }
    return a.materialOrLightId < b.materialOrLightId;
}

bool operator==(const RemixUnifiedIdentityKey& a, const RemixUnifiedIdentityKey& b)
{
    return a.type == b.type &&
        a.identityA == b.identityA &&
        a.identityB == b.identityB &&
        a.materialOrLightId == b.materialOrLightId;
}

bool RemixUnifiedIdentityKeyValid(const RemixUnifiedIdentityKey& key)
{
    if (key.type == PATH_TRACE_UNIFIED_LIGHT_TYPE_INVALID)
    {
        return false;
    }
    if (key.type == PATH_TRACE_UNIFIED_LIGHT_TYPE_DOOM_ANALYTIC)
    {
        return key.identityA != PATH_TRACE_UNIFIED_LIGHT_INVALID_INDEX &&
            key.materialOrLightId != PATH_TRACE_UNIFIED_LIGHT_INVALID_INDEX;
    }
    return key.identityA != 0u || key.identityB != 0u || key.materialOrLightId != 0u;
}

RemixUnifiedIdentityKey MakeRemixUnifiedIdentityKey(const PathTraceUnifiedLightRecord& record)
{
    RemixUnifiedIdentityKey key;
    key.type = record.type;
    key.identityA = record.identityA;
    key.identityB = record.identityB;
    key.materialOrLightId = record.materialOrLightId;
    return key;
}

uint32_t BuildUniqueUnifiedIdentityIndex(
    const std::vector<PathTraceUnifiedLightRecord>& records,
    std::map<RemixUnifiedIdentityKey, int>& identityToIndex)
{
    identityToIndex.clear();
    uint32_t duplicateCount = 0;
    for (int index = 0; index < static_cast<int>(records.size()); ++index)
    {
        const RemixUnifiedIdentityKey key = MakeRemixUnifiedIdentityKey(records[index]);
        if (!RemixUnifiedIdentityKeyValid(key))
        {
            continue;
        }

        const auto insertResult = identityToIndex.emplace(key, index);
        if (!insertResult.second)
        {
            insertResult.first->second = -1;
            ++duplicateCount;
        }
    }
    return duplicateCount;
}

uint64_t RemixHashBytes(uint64_t hash, const void* data, size_t byteCount)
{
    const unsigned char* bytes = static_cast<const unsigned char*>(data);
    for (size_t i = 0; i < byteCount; ++i)
    {
        hash ^= static_cast<uint64_t>(bytes[i]);
        hash *= 1099511628211ull;
    }
    return hash;
}

template< typename T >
uint64_t RemixHashValue(uint64_t hash, const T& value)
{
    return RemixHashBytes(hash, &value, sizeof(T));
}

template< typename T >
uint64_t RemixHashVector(uint64_t hash, const std::vector<T>& values)
{
    const uint64_t count = static_cast<uint64_t>(values.size());
    hash = RemixHashValue(hash, count);
    if (!values.empty())
    {
        hash = RemixHashBytes(hash, values.data(), values.size() * sizeof(T));
    }
    return hash;
}

bool RemixLightMapIndexValid(uint32_t index)
{
    return index != PATH_TRACE_REMIX_LIGHT_INVALID_INDEX;
}

bool RemixDoomAnalyticIdentityMappable(const PathTraceDoomAnalyticLightCandidateIdentity& identity)
{
    return identity.universeIndex != PATH_TRACE_DOOM_ANALYTIC_LIGHT_INVALID_INDEX &&
        identity.remapIndex != PATH_TRACE_DOOM_ANALYTIC_LIGHT_INVALID_INDEX &&
        (identity.flags & PATH_TRACE_DOOM_ANALYTIC_IDENTITY_VALID) != 0u &&
        (identity.flags & PATH_TRACE_DOOM_ANALYTIC_IDENTITY_SAMPLEABLE) != 0u;
}

bool RemixDoomAnalyticRemapValid(const PathTraceDoomAnalyticLightRemap& remap)
{
    return (remap.flags & PATH_TRACE_DOOM_ANALYTIC_IDENTITY_REMAP_VALID) != 0u;
}

uint32_t RemixRrxDiRequestedInitialSampleBudget(uint32_t emissiveSampleCount, uint32_t doomAnalyticSampleCount)
{
    const uint64_t requestedTotal =
        static_cast<uint64_t>(emissiveSampleCount) + static_cast<uint64_t>(doomAnalyticSampleCount);
    if (requestedTotal == 0)
    {
        return 0;
    }

    return static_cast<uint32_t>(std::min<uint64_t>(requestedTotal, 32ull));
}

uint32_t RemixRrxDiBoundedRangeSampleCount(uint32_t rangeCount, uint32_t totalLightCount, uint32_t requestedTotal)
{
    if (rangeCount == 0 || totalLightCount == 0 || requestedTotal == 0)
    {
        return 0;
    }

    const uint64_t roundedSamples =
        (static_cast<uint64_t>(rangeCount) * static_cast<uint64_t>(requestedTotal) +
            static_cast<uint64_t>(totalLightCount / 2u)) /
        static_cast<uint64_t>(totalLightCount);
    const uint32_t nonEmptyRangeSamples = static_cast<uint32_t>(std::max<uint64_t>(1ull, roundedSamples));
    return std::min(rangeCount, nonEmptyRangeSamples);
}

}

void PathTraceRemixLightManager::Clear()
{
    m_currentLightPayloads.clear();
    m_previousLightPayloads.clear();
    m_currentToPreviousMap.clear();
    m_previousToCurrentMap.clear();
    for (PathTraceRemixLightRange& range : m_lightRanges)
    {
        range = PathTraceRemixLightRange();
    }
    m_stats = PathTraceRemixLightManagerStats();
    m_lastStructuralSignature = 0;
    m_lastMappingSignature = 0;
    m_lastPayloadSignature = 0;
    m_haveLastSignatures = false;
    m_lightUniverseHistoryValid = false;
    m_lastPrepareWasLightUniverse = false;
}

void PathTraceRemixLightManager::PrepareDisabled(
    const PathTraceRemixFramePrepareObservationPackage& framePackage,
    uint32_t domain,
    bool strictRemixMapping)
{
    Clear();
    m_stats.frameIndex = framePackage.frameIndex;
    m_stats.enabled = 0;
    m_stats.domain = domain;
    m_stats.strictRemixMapping = strictRemixMapping ? 1u : 0u;
    m_stats.resetReasonFlags = framePackage.resetReasonFlags;
    m_stats.firstFailingContract = PATH_TRACE_REMIX_LIGHT_CONTRACT_DISABLED;
}

void PathTraceRemixLightManager::PrepareSceneData(
    const PathTraceRemixFramePrepareObservationPackage& framePackage,
    const std::vector<PathTraceSmokeEmissiveTriangle>& currentEmissiveTriangles,
    const std::vector<PathTraceSmokeEmissiveTriangle>& previousEmissiveTriangles,
    const std::vector<PathTraceEmissiveLightRemap>& emissiveRemap,
    const std::vector<PathTraceDoomAnalyticLightCandidate>& currentAnalyticLights,
    const std::vector<PathTraceDoomAnalyticLightCandidate>& previousAnalyticLights,
    const std::vector<PathTraceDoomAnalyticLightCandidateIdentity>& currentAnalyticIdentities,
    const std::vector<PathTraceDoomAnalyticLightCandidateIdentity>& previousAnalyticIdentities,
    const std::vector<PathTraceDoomAnalyticLightRemap>& analyticRemap,
    uint32_t emissiveSampleCount,
    uint32_t doomAnalyticSampleCount,
    float analyticStateCompatibilityTolerance,
    uint32_t domain,
    bool strictRemixMapping,
    bool lightUniverseEnabled)
{
    std::vector<PathTraceUnifiedLightRecord> lightUniversePreviousPayloads;
    if (lightUniverseEnabled && m_lastPrepareWasLightUniverse && m_lightUniverseHistoryValid)
    {
        lightUniversePreviousPayloads = m_currentLightPayloads;
    }

    const std::vector<PathTraceSmokeEmissiveTriangle> emptyEmissiveTriangles;
    const std::vector<PathTraceEmissiveLightRemap> emptyEmissiveRemap;
    const std::vector<PathTraceDoomAnalyticLightCandidate> emptyAnalyticLights;
    const std::vector<PathTraceDoomAnalyticLightCandidateIdentity> emptyAnalyticIdentities;
    const std::vector<PathTraceDoomAnalyticLightRemap> emptyAnalyticRemap;

    PathTraceUnifiedLightBuild build = lightUniverseEnabled
        ? BuildPathTraceUnifiedLights(
            currentEmissiveTriangles,
            emptyEmissiveTriangles,
            emptyEmissiveRemap,
            currentAnalyticLights,
            emptyAnalyticLights,
            currentAnalyticIdentities,
            emptyAnalyticIdentities,
            emptyAnalyticRemap,
            analyticStateCompatibilityTolerance)
        : BuildPathTraceUnifiedLights(
            currentEmissiveTriangles,
            previousEmissiveTriangles,
            emissiveRemap,
            currentAnalyticLights,
            previousAnalyticLights,
            currentAnalyticIdentities,
            previousAnalyticIdentities,
            analyticRemap,
            analyticStateCompatibilityTolerance);

    m_currentLightPayloads.swap(build.currentLights);
    if (lightUniverseEnabled)
    {
        m_previousLightPayloads.swap(lightUniversePreviousPayloads);
    }
    else
    {
        m_previousLightPayloads.swap(build.previousLights);
    }
    m_currentToPreviousMap.swap(build.currentToPreviousRemap);
    uint32_t identityDuplicateCount = 0;
    if (lightUniverseEnabled)
    {
        identityDuplicateCount = RebuildCurrentToPreviousMapByStableIdentity();
    }
    else
    {
        const uint32_t currentEmissiveCount = static_cast<uint32_t>(currentEmissiveTriangles.size());
        const uint32_t previousEmissiveCount = static_cast<uint32_t>(previousEmissiveTriangles.size());
        for (uint32_t analyticIndex = 0; analyticIndex < currentAnalyticIdentities.size(); ++analyticIndex)
        {
            const uint32_t currentUnifiedIndex = currentEmissiveCount + analyticIndex;
            if (currentUnifiedIndex >= m_currentToPreviousMap.size())
            {
                continue;
            }

            const PathTraceDoomAnalyticLightCandidateIdentity& identity = currentAnalyticIdentities[analyticIndex];
            if (!RemixDoomAnalyticIdentityMappable(identity) || identity.remapIndex >= analyticRemap.size())
            {
                continue;
            }

            const PathTraceDoomAnalyticLightRemap& remap = analyticRemap[identity.remapIndex];
            if (!RemixDoomAnalyticRemapValid(remap) || remap.currentToPreviousCandidateIndex < 0)
            {
                continue;
            }

            const uint32_t previousAnalyticIndex = static_cast<uint32_t>(remap.currentToPreviousCandidateIndex);
            const uint32_t previousUnifiedIndex = previousEmissiveCount + previousAnalyticIndex;
            if (previousAnalyticIndex < previousAnalyticLights.size() && previousUnifiedIndex < m_previousLightPayloads.size())
            {
                m_currentToPreviousMap[currentUnifiedIndex] = previousUnifiedIndex;
                m_currentLightPayloads[currentUnifiedIndex].previousIndex = previousUnifiedIndex;
            }
        }
    }
    RebuildPreviousToCurrentMap();
    RebuildLightRanges(
        static_cast<uint32_t>(currentEmissiveTriangles.size()),
        static_cast<uint32_t>(currentAnalyticLights.size()),
        emissiveSampleCount,
        doomAnalyticSampleCount);
    RebuildSignatures();
    RebuildStats(framePackage);
    m_stats.enabled = lightUniverseEnabled ? 1u : 0u;
    m_stats.domain = domain;
    m_stats.strictRemixMapping = strictRemixMapping ? 1u : 0u;
    if (lightUniverseEnabled)
    {
        m_stats.invalidDuplicateIdentityCount = identityDuplicateCount;
        if (identityDuplicateCount != 0u)
        {
            m_stats.firstFailingContract = PATH_TRACE_REMIX_LIGHT_CONTRACT_DUPLICATE_IDENTITY;
        }
    }
    if (!lightUniverseEnabled)
    {
        m_stats.firstFailingContract = PATH_TRACE_REMIX_LIGHT_CONTRACT_DISABLED;
    }
    m_lightUniverseHistoryValid = lightUniverseEnabled;
    m_lastPrepareWasLightUniverse = lightUniverseEnabled;
}

const std::vector<PathTraceUnifiedLightRecord>& PathTraceRemixLightManager::GetCurrentLightPayloads() const
{
    return m_currentLightPayloads;
}

const std::vector<PathTraceUnifiedLightRecord>& PathTraceRemixLightManager::GetPreviousLightPayloads() const
{
    return m_previousLightPayloads;
}

const std::vector<uint32_t>& PathTraceRemixLightManager::GetCurrentToPreviousMap() const
{
    return m_currentToPreviousMap;
}

const std::vector<uint32_t>& PathTraceRemixLightManager::GetPreviousToCurrentMap() const
{
    return m_previousToCurrentMap;
}

const PathTraceRemixLightRange* PathTraceRemixLightManager::GetLightRanges() const
{
    return m_lightRanges;
}

const PathTraceRemixLightManagerStats& PathTraceRemixLightManager::GetStats() const
{
    return m_stats;
}

void PathTraceRemixLightManager::RebuildPreviousToCurrentMap()
{
    m_previousToCurrentMap.assign(m_previousLightPayloads.size(), PATH_TRACE_REMIX_LIGHT_INVALID_INDEX);
    for (uint32_t currentIndex = 0; currentIndex < m_currentToPreviousMap.size(); ++currentIndex)
    {
        const uint32_t previousIndex = m_currentToPreviousMap[currentIndex];
        if (previousIndex >= m_previousToCurrentMap.size())
        {
            continue;
        }
        if (!RemixLightMapIndexValid(m_previousToCurrentMap[previousIndex]))
        {
            m_previousToCurrentMap[previousIndex] = currentIndex;
        }
    }
}

uint32_t PathTraceRemixLightManager::RebuildCurrentToPreviousMapByStableIdentity()
{
    m_currentToPreviousMap.assign(m_currentLightPayloads.size(), PATH_TRACE_REMIX_LIGHT_INVALID_INDEX);
    for (PathTraceUnifiedLightRecord& record : m_currentLightPayloads)
    {
        record.previousIndex = PATH_TRACE_UNIFIED_LIGHT_INVALID_INDEX;
    }

    std::map<RemixUnifiedIdentityKey, int> currentIdentityToIndex;
    std::map<RemixUnifiedIdentityKey, int> previousIdentityToIndex;
    const uint32_t duplicateCount =
        BuildUniqueUnifiedIdentityIndex(m_currentLightPayloads, currentIdentityToIndex) +
        BuildUniqueUnifiedIdentityIndex(m_previousLightPayloads, previousIdentityToIndex);

    for (uint32_t currentIndex = 0; currentIndex < m_currentLightPayloads.size(); ++currentIndex)
    {
        const RemixUnifiedIdentityKey key = MakeRemixUnifiedIdentityKey(m_currentLightPayloads[currentIndex]);
        if (!RemixUnifiedIdentityKeyValid(key))
        {
            continue;
        }

        const auto currentIt = currentIdentityToIndex.find(key);
        if (currentIt == currentIdentityToIndex.end() || currentIt->second != static_cast<int>(currentIndex))
        {
            continue;
        }

        const auto previousIt = previousIdentityToIndex.find(key);
        if (previousIt == previousIdentityToIndex.end() || previousIt->second < 0)
        {
            continue;
        }

        const uint32_t previousIndex = static_cast<uint32_t>(previousIt->second);
        if (previousIndex < m_previousLightPayloads.size())
        {
            m_currentToPreviousMap[currentIndex] = previousIndex;
            m_currentLightPayloads[currentIndex].previousIndex = previousIndex;
        }
    }
    return duplicateCount;
}

void PathTraceRemixLightManager::RebuildLightRanges(
    uint32_t currentEmissiveCount,
    uint32_t currentAnalyticCount,
    uint32_t emissiveSampleCount,
    uint32_t doomAnalyticSampleCount)
{
    for (PathTraceRemixLightRange& range : m_lightRanges)
    {
        range = PathTraceRemixLightRange();
    }

    m_lightRanges[PATH_TRACE_REMIX_LIGHT_TYPE_EMISSIVE_TRIANGLE].firstLightIndex = 0;
    m_lightRanges[PATH_TRACE_REMIX_LIGHT_TYPE_EMISSIVE_TRIANGLE].lightCount =
        std::min(currentEmissiveCount, static_cast<uint32_t>(m_currentLightPayloads.size()));

    m_lightRanges[PATH_TRACE_REMIX_LIGHT_TYPE_DOOM_ANALYTIC].firstLightIndex =
        m_lightRanges[PATH_TRACE_REMIX_LIGHT_TYPE_EMISSIVE_TRIANGLE].lightCount;
    m_lightRanges[PATH_TRACE_REMIX_LIGHT_TYPE_DOOM_ANALYTIC].lightCount =
        std::min(currentAnalyticCount, static_cast<uint32_t>(m_currentLightPayloads.size()) -
            m_lightRanges[PATH_TRACE_REMIX_LIGHT_TYPE_DOOM_ANALYTIC].firstLightIndex);

    const uint32_t totalLightCount =
        m_lightRanges[PATH_TRACE_REMIX_LIGHT_TYPE_EMISSIVE_TRIANGLE].lightCount +
        m_lightRanges[PATH_TRACE_REMIX_LIGHT_TYPE_DOOM_ANALYTIC].lightCount;
    const uint32_t requestedTotal = RemixRrxDiRequestedInitialSampleBudget(
        emissiveSampleCount,
        doomAnalyticSampleCount);

    m_lightRanges[PATH_TRACE_REMIX_LIGHT_TYPE_EMISSIVE_TRIANGLE].sampleCount =
        RemixRrxDiBoundedRangeSampleCount(
            m_lightRanges[PATH_TRACE_REMIX_LIGHT_TYPE_EMISSIVE_TRIANGLE].lightCount,
            totalLightCount,
            requestedTotal);
    m_lightRanges[PATH_TRACE_REMIX_LIGHT_TYPE_DOOM_ANALYTIC].sampleCount =
        RemixRrxDiBoundedRangeSampleCount(
            m_lightRanges[PATH_TRACE_REMIX_LIGHT_TYPE_DOOM_ANALYTIC].lightCount,
            totalLightCount,
            requestedTotal);
}

void PathTraceRemixLightManager::RebuildStats(const PathTraceRemixFramePrepareObservationPackage& framePackage)
{
    const uint64_t structuralSignature = m_stats.structuralSignature;
    const uint64_t mappingSignature = m_stats.mappingSignature;
    const uint64_t payloadSignature = m_stats.payloadSignature;
    const uint32_t structuralChanged = !m_haveLastSignatures || structuralSignature != m_lastStructuralSignature ? 1u : 0u;
    const uint32_t mappingChanged = !m_haveLastSignatures || mappingSignature != m_lastMappingSignature ? 1u : 0u;
    const uint32_t payloadChanged = !m_haveLastSignatures || payloadSignature != m_lastPayloadSignature ? 1u : 0u;

    m_stats.frameIndex = framePackage.frameIndex;
    m_stats.currentLightCount = static_cast<uint32_t>(m_currentLightPayloads.size());
    m_stats.previousLightCount = static_cast<uint32_t>(m_previousLightPayloads.size());
    m_stats.currentToPreviousCount = static_cast<uint32_t>(m_currentToPreviousMap.size());
    m_stats.previousToCurrentCount = static_cast<uint32_t>(m_previousToCurrentMap.size());
    m_stats.currentMappedCount = 0;
    m_stats.currentInvalidCount = 0;
    for (uint32_t previousIndex : m_currentToPreviousMap)
    {
        if (previousIndex < m_previousLightPayloads.size())
        {
            ++m_stats.currentMappedCount;
        }
        else
        {
            ++m_stats.currentInvalidCount;
        }
    }
    m_stats.currentOnlyCount = m_stats.currentInvalidCount;
    m_stats.previousMappedCount = 0;
    m_stats.previousInvalidCount = 0;
    for (uint32_t currentIndex : m_previousToCurrentMap)
    {
        if (currentIndex < m_currentLightPayloads.size())
        {
            ++m_stats.previousMappedCount;
        }
        else
        {
            ++m_stats.previousInvalidCount;
        }
    }
    m_stats.previousOnlyCount = m_stats.previousInvalidCount;
    std::map<RemixUnifiedIdentityKey, int> currentIdentityToIndex;
    std::map<RemixUnifiedIdentityKey, int> previousIdentityToIndex;
    m_stats.invalidDuplicateIdentityCount =
        BuildUniqueUnifiedIdentityIndex(m_currentLightPayloads, currentIdentityToIndex) +
        BuildUniqueUnifiedIdentityIndex(m_previousLightPayloads, previousIdentityToIndex);
    m_stats.emissiveRangeOffset = m_lightRanges[PATH_TRACE_REMIX_LIGHT_TYPE_EMISSIVE_TRIANGLE].firstLightIndex;
    m_stats.emissiveRangeCount = m_lightRanges[PATH_TRACE_REMIX_LIGHT_TYPE_EMISSIVE_TRIANGLE].lightCount;
    m_stats.doomAnalyticRangeOffset = m_lightRanges[PATH_TRACE_REMIX_LIGHT_TYPE_DOOM_ANALYTIC].firstLightIndex;
    m_stats.doomAnalyticRangeCount = m_lightRanges[PATH_TRACE_REMIX_LIGHT_TYPE_DOOM_ANALYTIC].lightCount;
    m_stats.emissiveSampleCount = m_lightRanges[PATH_TRACE_REMIX_LIGHT_TYPE_EMISSIVE_TRIANGLE].sampleCount;
    m_stats.doomAnalyticSampleCount = m_lightRanges[PATH_TRACE_REMIX_LIGHT_TYPE_DOOM_ANALYTIC].sampleCount;
    m_stats.totalSampleCount = m_stats.emissiveSampleCount + m_stats.doomAnalyticSampleCount;
    m_stats.nonEmptyRangeCount =
        (m_stats.emissiveRangeCount > 0 ? 1u : 0u) +
        (m_stats.doomAnalyticRangeCount > 0 ? 1u : 0u);
    m_stats.structuralSignatureChanged = structuralChanged;
    m_stats.mappingSignatureChanged = mappingChanged;
    m_stats.payloadSignatureChanged = payloadChanged;
    m_stats.payloadOnlyChange = structuralChanged == 0u && mappingChanged == 0u && payloadChanged != 0u ? 1u : 0u;
    m_stats.oldSmokeReservoirSignatureConsulted = 0;
    m_stats.resourceAllocationCount = 0;
    m_stats.shaderRouteCount = 0;
    m_stats.resetReasonFlags = framePackage.resetReasonFlags;
    m_stats.firstFailingContract = PATH_TRACE_REMIX_LIGHT_CONTRACT_OK;
    if (m_currentToPreviousMap.size() != m_currentLightPayloads.size() ||
        m_previousToCurrentMap.size() != m_previousLightPayloads.size())
    {
        m_stats.firstFailingContract = PATH_TRACE_REMIX_LIGHT_CONTRACT_MAP_SIZE_MISMATCH;
    }
    else if (m_stats.invalidDuplicateIdentityCount != 0u)
    {
        m_stats.firstFailingContract = PATH_TRACE_REMIX_LIGHT_CONTRACT_DUPLICATE_IDENTITY;
    }
    else if (m_stats.currentLightCount == 0u)
    {
        m_stats.firstFailingContract = PATH_TRACE_REMIX_LIGHT_CONTRACT_NO_CURRENT_DOMAIN;
    }

    m_lastStructuralSignature = structuralSignature;
    m_lastMappingSignature = mappingSignature;
    m_lastPayloadSignature = payloadSignature;
    m_haveLastSignatures = true;
}

void PathTraceRemixLightManager::RebuildSignatures()
{
    uint64_t structuralHash = 1469598103934665603ull;
    const uint64_t structuralVersion = 2;
    structuralHash = RemixHashValue(structuralHash, structuralVersion);
    structuralHash = RemixHashValue(structuralHash, static_cast<uint32_t>(PATH_TRACE_REMIX_LIGHT_TYPE_COUNT));
    structuralHash = RemixHashValue(structuralHash, static_cast<uint32_t>(PATH_TRACE_REMIX_LIGHT_TYPE_EMISSIVE_TRIANGLE));
    structuralHash = RemixHashValue(structuralHash, static_cast<uint32_t>(PATH_TRACE_REMIX_LIGHT_TYPE_DOOM_ANALYTIC));
    structuralHash = RemixHashValue(structuralHash, static_cast<uint32_t>(PATH_TRACE_REMIX_LIGHT_INVALID_INDEX));
    structuralHash = RemixHashValue(structuralHash, static_cast<uint32_t>(sizeof(PathTraceUnifiedLightRecord)));
    structuralHash = RemixHashValue(structuralHash, static_cast<uint32_t>(sizeof(PathTraceRemixLightRange)));

    uint64_t mappingHash = 1469598103934665603ull;
    const uint64_t mappingVersion = 1;
    mappingHash = RemixHashValue(mappingHash, mappingVersion);
    mappingHash = RemixHashVector(mappingHash, m_currentToPreviousMap);
    mappingHash = RemixHashVector(mappingHash, m_previousToCurrentMap);

    uint64_t payloadHash = 1469598103934665603ull;
    const uint64_t payloadVersion = 1;
    payloadHash = RemixHashValue(payloadHash, payloadVersion);
    payloadHash = RemixHashVector(payloadHash, m_currentLightPayloads);
    payloadHash = RemixHashVector(payloadHash, m_previousLightPayloads);
    payloadHash = RemixHashBytes(payloadHash, m_lightRanges, sizeof(m_lightRanges));

    m_stats.structuralSignature = structuralHash;
    m_stats.mappingSignature = mappingHash;
    m_stats.payloadSignature = payloadHash;
}
