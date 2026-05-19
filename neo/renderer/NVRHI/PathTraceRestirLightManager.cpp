#include "precompiled.h"
#pragma hdrstop

#include "PathTraceRestirLightManager.h"
#include "PathTraceDoomLights.h"
#include "PathTraceEmissiveCandidates.h"

namespace {

constexpr uint32_t DOOM_LIGHT_MANAGER_INVALID_MISSING_PREVIOUS = 0x00000001u;
constexpr uint32_t DOOM_LIGHT_MANAGER_INVALID_MISSING_CURRENT = 0x00000002u;
constexpr uint32_t DOOM_LIGHT_MANAGER_INVALID_DUPLICATE_KEY = 0x00000004u;
constexpr uint32_t DOOM_LIGHT_MANAGER_INVALID_UNKNOWN_ENTITY = 0x00000008u;
constexpr uint32_t DOOM_LIGHT_MANAGER_INVALID_UNPROVEN_CONTINUITY = 0x00000010u;
constexpr uint32_t DOOM_LIGHT_MANAGER_INVALID_ZERO_RADIANCE = 0x00000020u;
constexpr uint32_t DOOM_LIGHT_MANAGER_INVALID_SUPPRESSED = 0x00000040u;
constexpr uint32_t DOOM_LIGHT_MANAGER_INVALID_OUT_OF_SELECTED_AREA = 0x00000080u;
constexpr uint32_t DOOM_LIGHT_MANAGER_INVALID_NON_POINT_OR_PARALLEL = 0x00000100u;
constexpr uint32_t DOOM_LIGHT_MANAGER_INVALID_RADIUS_INVALID = 0x00000200u;
constexpr uint32_t DOOM_LIGHT_MANAGER_INVALID_CANDIDATE_CAP_DROPPED = 0x00000400u;
constexpr uint32_t DOOM_LIGHT_MANAGER_INVALID_DISCONNECTED_OR_PORTAL = 0x00000800u;

uint64 HashLightManagerBytes(uint64 hash, const void* data, size_t size)
{
    const uint8_t* bytes = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < size; ++i)
    {
        hash ^= static_cast<uint64>(bytes[i]);
        hash *= 1099511628211ull;
    }
    return hash;
}

uint64 HashLightManagerValue(uint64 hash, uint32_t value)
{
    return HashLightManagerBytes(hash, &value, sizeof(value));
}

uint64 HashLightManagerFloatArray(uint64 hash, const float* values, size_t count)
{
    return HashLightManagerBytes(hash, values, count * sizeof(values[0]));
}

void AccumulateInvalidReasonStats(
    const uint32_t invalidReasonFlags,
    PathTraceRestirLightInvalidReasonStats& stats)
{
    if ((invalidReasonFlags & PATH_TRACE_RESTIR_LIGHT_INVALID_REASON_NEW_LIGHT) != 0u)
    {
        ++stats.newLight;
    }
    if ((invalidReasonFlags & PATH_TRACE_RESTIR_LIGHT_INVALID_REASON_MISSING_LIGHT) != 0u)
    {
        ++stats.missingLight;
    }
    if ((invalidReasonFlags & PATH_TRACE_RESTIR_LIGHT_INVALID_REASON_TEMPORARY) != 0u)
    {
        ++stats.temporary;
    }
    if ((invalidReasonFlags & PATH_TRACE_RESTIR_LIGHT_INVALID_REASON_PROJECTILE) != 0u)
    {
        ++stats.projectile;
    }
    if ((invalidReasonFlags & PATH_TRACE_RESTIR_LIGHT_INVALID_REASON_DUPLICATE) != 0u)
    {
        ++stats.duplicate;
    }
    if ((invalidReasonFlags & PATH_TRACE_RESTIR_LIGHT_INVALID_REASON_UNKNOWN_IDENTITY) != 0u)
    {
        ++stats.unknownIdentity;
    }
    if ((invalidReasonFlags & PATH_TRACE_RESTIR_LIGHT_INVALID_REASON_UNSUPPORTED_SOURCE) != 0u)
    {
        ++stats.unsupportedSource;
    }
    if ((invalidReasonFlags & PATH_TRACE_RESTIR_LIGHT_INVALID_REASON_STRUCTURAL_RESET) != 0u)
    {
        ++stats.structuralReset;
    }
    if ((invalidReasonFlags & PATH_TRACE_RESTIR_LIGHT_INVALID_REASON_UNPROVEN_CONTINUITY) != 0u)
    {
        ++stats.unprovenContinuity;
    }
    if ((invalidReasonFlags & PATH_TRACE_RESTIR_LIGHT_INVALID_REASON_ZERO_RADIANCE) != 0u)
    {
        ++stats.zeroRadiance;
    }
    if ((invalidReasonFlags & PATH_TRACE_RESTIR_LIGHT_INVALID_REASON_SUPPRESSED) != 0u)
    {
        ++stats.suppressed;
    }
    if ((invalidReasonFlags & PATH_TRACE_RESTIR_LIGHT_INVALID_REASON_OUT_OF_SELECTED_AREA) != 0u)
    {
        ++stats.outOfSelectedArea;
    }
    if ((invalidReasonFlags & PATH_TRACE_RESTIR_LIGHT_INVALID_REASON_DISCONNECTED_OR_PORTAL) != 0u)
    {
        ++stats.disconnectedOrPortal;
    }
    if ((invalidReasonFlags & PATH_TRACE_RESTIR_LIGHT_INVALID_REASON_INVALID_SHAPE) != 0u)
    {
        ++stats.invalidShape;
    }
    if ((invalidReasonFlags & PATH_TRACE_RESTIR_LIGHT_INVALID_REASON_CANDIDATE_CAP) != 0u)
    {
        ++stats.candidateCap;
    }
    if ((invalidReasonFlags & PATH_TRACE_RESTIR_LIGHT_INVALID_REASON_INCOMPATIBLE_SOURCE) != 0u)
    {
        ++stats.incompatibleSource;
    }
}

uint32_t TranslateDoomAnalyticInvalidReasons(uint32_t doomInvalidReasonFlags)
{
    uint32_t result = PATH_TRACE_RESTIR_LIGHT_INVALID_REASON_NONE;
    if ((doomInvalidReasonFlags & DOOM_LIGHT_MANAGER_INVALID_MISSING_PREVIOUS) != 0u)
    {
        result |= PATH_TRACE_RESTIR_LIGHT_INVALID_REASON_MISSING_LIGHT;
    }
    if ((doomInvalidReasonFlags & DOOM_LIGHT_MANAGER_INVALID_MISSING_CURRENT) != 0u)
    {
        result |= PATH_TRACE_RESTIR_LIGHT_INVALID_REASON_MISSING_LIGHT;
    }
    if ((doomInvalidReasonFlags & DOOM_LIGHT_MANAGER_INVALID_DUPLICATE_KEY) != 0u)
    {
        result |= PATH_TRACE_RESTIR_LIGHT_INVALID_REASON_DUPLICATE;
    }
    if ((doomInvalidReasonFlags & DOOM_LIGHT_MANAGER_INVALID_UNKNOWN_ENTITY) != 0u)
    {
        result |= PATH_TRACE_RESTIR_LIGHT_INVALID_REASON_UNKNOWN_IDENTITY;
    }
    if ((doomInvalidReasonFlags & DOOM_LIGHT_MANAGER_INVALID_UNPROVEN_CONTINUITY) != 0u)
    {
        result |= PATH_TRACE_RESTIR_LIGHT_INVALID_REASON_UNPROVEN_CONTINUITY;
    }
    if ((doomInvalidReasonFlags & DOOM_LIGHT_MANAGER_INVALID_ZERO_RADIANCE) != 0u)
    {
        result |= PATH_TRACE_RESTIR_LIGHT_INVALID_REASON_ZERO_RADIANCE;
    }
    if ((doomInvalidReasonFlags & DOOM_LIGHT_MANAGER_INVALID_SUPPRESSED) != 0u)
    {
        result |= PATH_TRACE_RESTIR_LIGHT_INVALID_REASON_SUPPRESSED;
    }
    if ((doomInvalidReasonFlags & DOOM_LIGHT_MANAGER_INVALID_OUT_OF_SELECTED_AREA) != 0u)
    {
        result |= PATH_TRACE_RESTIR_LIGHT_INVALID_REASON_OUT_OF_SELECTED_AREA;
    }
    if ((doomInvalidReasonFlags & DOOM_LIGHT_MANAGER_INVALID_DISCONNECTED_OR_PORTAL) != 0u)
    {
        result |= PATH_TRACE_RESTIR_LIGHT_INVALID_REASON_DISCONNECTED_OR_PORTAL;
    }
    if ((doomInvalidReasonFlags & (DOOM_LIGHT_MANAGER_INVALID_NON_POINT_OR_PARALLEL | DOOM_LIGHT_MANAGER_INVALID_RADIUS_INVALID)) != 0u)
    {
        result |= PATH_TRACE_RESTIR_LIGHT_INVALID_REASON_INVALID_SHAPE;
    }
    if ((doomInvalidReasonFlags & DOOM_LIGHT_MANAGER_INVALID_CANDIDATE_CAP_DROPPED) != 0u)
    {
        result |= PATH_TRACE_RESTIR_LIGHT_INVALID_REASON_CANDIDATE_CAP;
    }
    return result;
}

PathTraceRestirPreviousLightRecord MakePreviousRecord(const PathTraceRestirCurrentLightRecord& currentRecord)
{
    PathTraceRestirPreviousLightRecord previousRecord;
    previousRecord.sourceType = currentRecord.sourceType;
    previousRecord.sourceIndex = currentRecord.sourceIndex;
    previousRecord.stableKeyLo = currentRecord.stableKeyLo;
    previousRecord.stableKeyHi = currentRecord.stableKeyHi;
    previousRecord.compatibilityKey0 = currentRecord.compatibilityKey0;
    previousRecord.compatibilityKey1 = currentRecord.compatibilityKey1;
    previousRecord.compatibilityKey2 = currentRecord.compatibilityKey2;
    previousRecord.compatibilityKey3 = currentRecord.compatibilityKey3;
    previousRecord.payloadHashLo = currentRecord.payloadHashLo;
    previousRecord.payloadHashHi = currentRecord.payloadHashHi;
    previousRecord.flags = currentRecord.flags & PATH_TRACE_RESTIR_LIGHT_RECORD_STABLE_IDENTITY;
    previousRecord.invalidReasonFlags = currentRecord.invalidReasonFlags & (
        PATH_TRACE_RESTIR_LIGHT_INVALID_REASON_DUPLICATE |
        PATH_TRACE_RESTIR_LIGHT_INVALID_REASON_UNKNOWN_IDENTITY |
        PATH_TRACE_RESTIR_LIGHT_INVALID_REASON_UNSUPPORTED_SOURCE |
        PATH_TRACE_RESTIR_LIGHT_INVALID_REASON_UNPROVEN_CONTINUITY |
        PATH_TRACE_RESTIR_LIGHT_INVALID_REASON_ZERO_RADIANCE |
        PATH_TRACE_RESTIR_LIGHT_INVALID_REASON_SUPPRESSED |
        PATH_TRACE_RESTIR_LIGHT_INVALID_REASON_OUT_OF_SELECTED_AREA |
        PATH_TRACE_RESTIR_LIGHT_INVALID_REASON_DISCONNECTED_OR_PORTAL |
        PATH_TRACE_RESTIR_LIGHT_INVALID_REASON_INVALID_SHAPE |
        PATH_TRACE_RESTIR_LIGHT_INVALID_REASON_CANDIDATE_CAP |
        PATH_TRACE_RESTIR_LIGHT_INVALID_REASON_INCOMPATIBLE_SOURCE);
    return previousRecord;
}

bool HasStableKey(uint32_t keyLo, uint32_t keyHi)
{
    return keyLo != 0u || keyHi != 0u;
}

uint64 HashEmissivePayload(const PathTraceSmokeEmissiveTriangle& emissiveTriangle)
{
    uint64 hash = 1469598103934665603ull;
    hash = HashLightManagerFloatArray(hash, emissiveTriangle.centerAndArea, 4);
    hash = HashLightManagerFloatArray(hash, emissiveTriangle.normalAndLuminance, 4);
    hash = HashLightManagerFloatArray(hash, emissiveTriangle.estimatedRadianceAndLuminance, 4);
    hash = HashLightManagerFloatArray(hash, emissiveTriangle.sampleWeightAndPdf, 4);
    hash = HashLightManagerValue(hash, emissiveTriangle.materialIndex);
    hash = HashLightManagerValue(hash, emissiveTriangle.instanceId);
    hash = HashLightManagerValue(hash, emissiveTriangle.primitiveIndex);
    hash = HashLightManagerValue(hash, emissiveTriangle.flags);
    hash = HashLightManagerValue(hash, emissiveTriangle.materialId);
    return hash;
}

uint64 HashDoomAnalyticPayload(const PathTraceDoomAnalyticLightCandidate& light)
{
    uint64 hash = 1469598103934665603ull;
    hash = HashLightManagerFloatArray(hash, light.originAndRadius, 4);
    hash = HashLightManagerFloatArray(hash, light.colorAndIntensity, 4);
    hash = HashLightManagerFloatArray(hash, light.doomRadiusAndArea, 4);
    hash = HashLightManagerValue(hash, light.flags);
    hash = HashLightManagerValue(hash, light.renderLightIndex);
    hash = HashLightManagerValue(hash, light.entityNumber);
    return hash;
}

PathTraceRestirCurrentLightRecord MakeEmissiveRecord(
    const PathTraceSmokeEmissiveTriangle& emissiveTriangle,
    uint32_t sourceIndex)
{
    PathTraceRestirCurrentLightRecord record;
    record.sourceType = PATH_TRACE_RESTIR_LIGHT_SOURCE_EMISSIVE_TRIANGLE;
    record.sourceIndex = sourceIndex;
    record.stableKeyLo = emissiveTriangle.identityHashLo;
    record.stableKeyHi = emissiveTriangle.identityHashHi;
    record.compatibilityKey0 = emissiveTriangle.materialId;
    record.compatibilityKey1 = emissiveTriangle.universeMaterialIndex;
    record.compatibilityKey2 = emissiveTriangle.emissiveTextureIndex;
    const uint64 payloadHash = HashEmissivePayload(emissiveTriangle);
    record.payloadHashLo = static_cast<uint32_t>(payloadHash);
    record.payloadHashHi = static_cast<uint32_t>(payloadHash >> 32);
    if (HasStableKey(record.stableKeyLo, record.stableKeyHi))
    {
        record.flags |= PATH_TRACE_RESTIR_LIGHT_RECORD_STABLE_IDENTITY;
    }
    else
    {
        record.flags |= PATH_TRACE_RESTIR_LIGHT_RECORD_CURRENT_ONLY;
        record.invalidReasonFlags |= PATH_TRACE_RESTIR_LIGHT_INVALID_REASON_UNKNOWN_IDENTITY;
    }
    return record;
}

PathTraceRestirCurrentLightRecord MakeDoomAnalyticRecord(
    const PathTraceDoomAnalyticLightCandidate& light,
    const PathTraceDoomAnalyticLightCandidateIdentity* identity,
    uint32_t sourceIndex)
{
    PathTraceRestirCurrentLightRecord record;
    record.sourceType = PATH_TRACE_RESTIR_LIGHT_SOURCE_DOOM_ANALYTIC;
    record.sourceIndex = sourceIndex;
    if (identity &&
        identity->universeIndex != PATH_TRACE_DOOM_ANALYTIC_LIGHT_INVALID_INDEX &&
        (identity->flags & PATH_TRACE_DOOM_ANALYTIC_IDENTITY_VALID) != 0u)
    {
        record.stableKeyLo = identity->universeIndex;
        record.stableKeyHi = 0u;
        record.flags |= PATH_TRACE_RESTIR_LIGHT_RECORD_STABLE_IDENTITY;
        if ((identity->flags & PATH_TRACE_DOOM_ANALYTIC_IDENTITY_REMAP_VALID) == 0u)
        {
            record.invalidReasonFlags |= TranslateDoomAnalyticInvalidReasons(identity->invalidReasonFlags);
            if (record.invalidReasonFlags == PATH_TRACE_RESTIR_LIGHT_INVALID_REASON_NONE)
            {
                record.invalidReasonFlags |= PATH_TRACE_RESTIR_LIGHT_INVALID_REASON_UNSUPPORTED_SOURCE;
            }
        }
    }
    else
    {
        record.flags |= PATH_TRACE_RESTIR_LIGHT_RECORD_CURRENT_ONLY;
        record.invalidReasonFlags |= PATH_TRACE_RESTIR_LIGHT_INVALID_REASON_UNKNOWN_IDENTITY;
    }

    const uint64 payloadHash = HashDoomAnalyticPayload(light);
    record.payloadHashLo = static_cast<uint32_t>(payloadHash);
    record.payloadHashHi = static_cast<uint32_t>(payloadHash >> 32);
    return record;
}

bool PayloadChanged(
    const PathTraceRestirCurrentLightRecord& currentRecord,
    const PathTraceRestirPreviousLightRecord& previousRecord)
{
    return currentRecord.payloadHashLo != previousRecord.payloadHashLo ||
        currentRecord.payloadHashHi != previousRecord.payloadHashHi;
}

bool RecordsCompatible(
    const PathTraceRestirCurrentLightRecord& currentRecord,
    const PathTraceRestirPreviousLightRecord& previousRecord)
{
    if (currentRecord.sourceType != previousRecord.sourceType)
    {
        return false;
    }
    if (currentRecord.sourceType == PATH_TRACE_RESTIR_LIGHT_SOURCE_EMISSIVE_TRIANGLE)
    {
        return currentRecord.compatibilityKey0 == previousRecord.compatibilityKey0 &&
            currentRecord.compatibilityKey1 == previousRecord.compatibilityKey1 &&
            currentRecord.compatibilityKey2 == previousRecord.compatibilityKey2;
    }
    return true;
}

PathTraceRestirPreviousLightRecord MakeMissingPreviousRecord(PathTraceRestirPreviousLightRecord previousRecord)
{
    previousRecord.flags &= ~PATH_TRACE_RESTIR_LIGHT_RECORD_REMAP_VALID;
    previousRecord.flags |= PATH_TRACE_RESTIR_LIGHT_RECORD_PREVIOUS_ONLY;
    previousRecord.invalidReasonFlags |= PATH_TRACE_RESTIR_LIGHT_INVALID_REASON_MISSING_LIGHT;
    return previousRecord;
}

void MarkDuplicate(PathTraceRestirCurrentLightRecord& record)
{
    record.flags &= ~PATH_TRACE_RESTIR_LIGHT_RECORD_REMAP_VALID;
    record.invalidReasonFlags |= PATH_TRACE_RESTIR_LIGHT_INVALID_REASON_DUPLICATE;
}

void MarkIncompatible(PathTraceRestirCurrentLightRecord& record)
{
    record.flags &= ~PATH_TRACE_RESTIR_LIGHT_RECORD_REMAP_VALID;
    record.invalidReasonFlags |= PATH_TRACE_RESTIR_LIGHT_INVALID_REASON_INCOMPATIBLE_SOURCE;
}

void MarkIncompatible(PathTraceRestirPreviousLightRecord& record)
{
    record.flags &= ~PATH_TRACE_RESTIR_LIGHT_RECORD_REMAP_VALID;
    record.invalidReasonFlags |= PATH_TRACE_RESTIR_LIGHT_INVALID_REASON_INCOMPATIBLE_SOURCE;
}

void MarkDuplicate(PathTraceRestirPreviousLightRecord& record)
{
    record.flags &= ~PATH_TRACE_RESTIR_LIGHT_RECORD_REMAP_VALID;
    record.invalidReasonFlags |= PATH_TRACE_RESTIR_LIGHT_INVALID_REASON_DUPLICATE;
}

PathTraceRestirLightManager::StableKey MakeStableKey(const PathTraceRestirCurrentLightRecord& record)
{
    PathTraceRestirLightManager::StableKey key;
    key.sourceType = record.sourceType;
    key.keyLo = record.stableKeyLo;
    key.keyHi = record.stableKeyHi;
    return key;
}

PathTraceRestirLightManager::StableKey MakeStableKey(const PathTraceRestirPreviousLightRecord& record)
{
    PathTraceRestirLightManager::StableKey key;
    key.sourceType = record.sourceType;
    key.keyLo = record.stableKeyLo;
    key.keyHi = record.stableKeyHi;
    return key;
}

bool RecordHasStableIdentity(const PathTraceRestirCurrentLightRecord& record)
{
    return (record.flags & PATH_TRACE_RESTIR_LIGHT_RECORD_STABLE_IDENTITY) != 0u;
}

bool RecordHasStableIdentity(const PathTraceRestirPreviousLightRecord& record)
{
    return (record.flags & PATH_TRACE_RESTIR_LIGHT_RECORD_STABLE_IDENTITY) != 0u;
}

bool RecordRemapBlocked(const PathTraceRestirCurrentLightRecord& record)
{
    return record.invalidReasonFlags != PATH_TRACE_RESTIR_LIGHT_INVALID_REASON_NONE;
}

bool RecordRemapBlocked(const PathTraceRestirPreviousLightRecord& record)
{
    return record.invalidReasonFlags != PATH_TRACE_RESTIR_LIGHT_INVALID_REASON_NONE;
}

} // namespace

size_t PathTraceRestirLightManager::StableKeyHash::operator()(const StableKey& key) const
{
    uint64 hash = 1469598103934665603ull;
    hash = HashLightManagerValue(hash, key.sourceType);
    hash = HashLightManagerValue(hash, key.keyLo);
    hash = HashLightManagerValue(hash, key.keyHi);
    return static_cast<size_t>(hash);
}

void PathTraceRestirLightManager::Clear()
{
    m_currentLightRecords.clear();
    m_previousLightRecords.clear();
    m_currentToPreviousRemap.clear();
    m_previousToCurrentRemap.clear();
    m_previousStableLookup.clear();
    m_stats = {};
}

void PathTraceRestirLightManager::BeginFrame()
{
    m_previousLightRecords.clear();
    m_previousLightRecords.reserve(m_currentLightRecords.size());
    for (const PathTraceRestirCurrentLightRecord& currentRecord : m_currentLightRecords)
    {
        m_previousLightRecords.push_back(MakePreviousRecord(currentRecord));
    }

    m_currentLightRecords.clear();
    m_currentToPreviousRemap.clear();
    m_previousToCurrentRemap.clear();
    RebuildPreviousLookup();
    m_stats = {};
    m_stats.previousLightCount = static_cast<uint32_t>(m_previousLightRecords.size());
}

void PathTraceRestirLightManager::UpdateFromObservations(
    const std::vector<PathTraceSmokeEmissiveTriangle>& emissiveTriangles,
    const std::vector<PathTraceDoomAnalyticLightCandidate>& doomAnalyticLights,
    const std::vector<PathTraceDoomAnalyticLightCandidateIdentity>& doomAnalyticIdentities)
{
    m_currentLightRecords.clear();
    m_currentLightRecords.reserve(emissiveTriangles.size() + doomAnalyticLights.size());

    std::unordered_map<StableKey, uint32_t, StableKeyHash> currentStableLookup;

    auto addCurrentRecord = [&](PathTraceRestirCurrentLightRecord record) {
        if (RecordHasStableIdentity(record))
        {
            const StableKey stableKey = MakeStableKey(record);
            auto currentIt = currentStableLookup.find(stableKey);
            if (currentIt != currentStableLookup.end())
            {
                MarkDuplicate(m_currentLightRecords[currentIt->second]);
                MarkDuplicate(record);
                auto previousIt = m_previousStableLookup.find(stableKey);
                if (previousIt != m_previousStableLookup.end())
                {
                    MarkDuplicate(m_previousLightRecords[previousIt->second]);
                }
            }
            else
            {
                currentStableLookup[stableKey] = static_cast<uint32_t>(m_currentLightRecords.size());
            }
        }

        m_currentLightRecords.push_back(record);
    };

    for (size_t i = 0; i < emissiveTriangles.size(); ++i)
    {
        addCurrentRecord(MakeEmissiveRecord(emissiveTriangles[i], static_cast<uint32_t>(i)));
    }

    for (size_t i = 0; i < doomAnalyticLights.size(); ++i)
    {
        const PathTraceDoomAnalyticLightCandidateIdentity* identity =
            i < doomAnalyticIdentities.size() ? &doomAnalyticIdentities[i] : nullptr;
        addCurrentRecord(MakeDoomAnalyticRecord(doomAnalyticLights[i], identity, static_cast<uint32_t>(i)));
    }

    RebuildCpuRemaps();
}

void PathTraceRestirLightManager::EndFrame()
{
    RebuildStats();
}

PathTraceRestirLightManagerStats PathTraceRestirLightManager::GetStats() const
{
    return m_stats;
}

const std::vector<PathTraceRestirCurrentLightRecord>& PathTraceRestirLightManager::GetCurrentLightRecords() const
{
    return m_currentLightRecords;
}

const std::vector<PathTraceRestirPreviousLightRecord>& PathTraceRestirLightManager::GetPreviousLightRecords() const
{
    return m_previousLightRecords;
}

const std::vector<uint32_t>& PathTraceRestirLightManager::GetCurrentToPreviousRemap() const
{
    return m_currentToPreviousRemap;
}

const std::vector<uint32_t>& PathTraceRestirLightManager::GetPreviousToCurrentRemap() const
{
    return m_previousToCurrentRemap;
}

void PathTraceRestirLightManager::RebuildCpuRemaps()
{
    m_currentToPreviousRemap.assign(m_currentLightRecords.size(), PATH_TRACE_RESTIR_LIGHT_INVALID_INDEX);
    m_previousToCurrentRemap.assign(m_previousLightRecords.size(), PATH_TRACE_RESTIR_LIGHT_INVALID_INDEX);
    std::vector<bool> previousMatched(m_previousLightRecords.size(), false);

    for (size_t currentIndex = 0; currentIndex < m_currentLightRecords.size(); ++currentIndex)
    {
        PathTraceRestirCurrentLightRecord& currentRecord = m_currentLightRecords[currentIndex];
        if (!RecordHasStableIdentity(currentRecord))
        {
            continue;
        }

        const StableKey stableKey = MakeStableKey(currentRecord);
        auto previousIt = m_previousStableLookup.find(stableKey);
        if (previousIt == m_previousStableLookup.end())
        {
            if (!RecordRemapBlocked(currentRecord))
            {
                currentRecord.flags |= PATH_TRACE_RESTIR_LIGHT_RECORD_CURRENT_ONLY;
                currentRecord.invalidReasonFlags |= PATH_TRACE_RESTIR_LIGHT_INVALID_REASON_NEW_LIGHT;
            }
            continue;
        }

        const uint32_t previousIndex = previousIt->second;
        PathTraceRestirPreviousLightRecord& previousRecord = m_previousLightRecords[previousIndex];
        previousMatched[previousIndex] = true;
        if (RecordRemapBlocked(currentRecord) || RecordRemapBlocked(previousRecord))
        {
            continue;
        }
        if (!RecordsCompatible(currentRecord, previousRecord))
        {
            MarkIncompatible(currentRecord);
            MarkIncompatible(previousRecord);
            continue;
        }

        currentRecord.flags |= PATH_TRACE_RESTIR_LIGHT_RECORD_REMAP_VALID;
        previousRecord.flags |= PATH_TRACE_RESTIR_LIGHT_RECORD_REMAP_VALID;
        m_currentToPreviousRemap[currentIndex] = previousIndex;
        m_previousToCurrentRemap[previousIndex] = static_cast<uint32_t>(currentIndex);
        if (PayloadChanged(currentRecord, previousRecord))
        {
            currentRecord.flags |= PATH_TRACE_RESTIR_LIGHT_RECORD_PAYLOAD_CHANGED;
        }
    }

    for (size_t i = 0; i < m_previousLightRecords.size(); ++i)
    {
        PathTraceRestirPreviousLightRecord& previousRecord = m_previousLightRecords[i];
        if (RecordHasStableIdentity(previousRecord) && !previousMatched[i] && !RecordRemapBlocked(previousRecord))
        {
            previousRecord = MakeMissingPreviousRecord(previousRecord);
        }
    }
}

void PathTraceRestirLightManager::RebuildStats()
{
    m_stats.currentLightCount = static_cast<uint32_t>(m_currentLightRecords.size());
    m_stats.previousLightCount = static_cast<uint32_t>(m_previousLightRecords.size());
    m_stats.currentToPreviousCount = static_cast<uint32_t>(m_currentToPreviousRemap.size());
    m_stats.previousToCurrentCount = static_cast<uint32_t>(m_previousToCurrentRemap.size());
    m_stats.currentMappedCount = 0;
    m_stats.currentInvalidCount = 0;
    m_stats.previousMappedCount = 0;
    m_stats.previousInvalidCount = 0;
    m_stats.stableIdentityCount = 0;
    m_stats.payloadChangedCount = 0;
    m_stats.currentOnlyCount = 0;
    m_stats.previousOnlyCount = 0;
    m_stats.remapValidCount = 0;
    m_stats.remapInvalidCount = 0;
    m_stats.mapSizeMismatchCount =
        (m_currentToPreviousRemap.size() == m_currentLightRecords.size() ? 0u : 1u) +
        (m_previousToCurrentRemap.size() == m_previousLightRecords.size() ? 0u : 1u);
    m_stats.invalidReasons = {};

    for (const PathTraceRestirCurrentLightRecord& record : m_currentLightRecords)
    {
        if ((record.flags & PATH_TRACE_RESTIR_LIGHT_RECORD_STABLE_IDENTITY) != 0u)
        {
            ++m_stats.stableIdentityCount;
        }
        if ((record.flags & PATH_TRACE_RESTIR_LIGHT_RECORD_PAYLOAD_CHANGED) != 0u)
        {
            ++m_stats.payloadChangedCount;
        }
        if ((record.flags & PATH_TRACE_RESTIR_LIGHT_RECORD_CURRENT_ONLY) != 0u)
        {
            ++m_stats.currentOnlyCount;
        }
        if ((record.flags & PATH_TRACE_RESTIR_LIGHT_RECORD_REMAP_VALID) != 0u)
        {
            ++m_stats.remapValidCount;
        }
        else
        {
            ++m_stats.remapInvalidCount;
        }
        AccumulateInvalidReasonStats(record.invalidReasonFlags, m_stats.invalidReasons);
    }

    for (const PathTraceRestirPreviousLightRecord& record : m_previousLightRecords)
    {
        if ((record.flags & PATH_TRACE_RESTIR_LIGHT_RECORD_PREVIOUS_ONLY) != 0u)
        {
            ++m_stats.previousOnlyCount;
        }
        if ((record.flags & PATH_TRACE_RESTIR_LIGHT_RECORD_REMAP_VALID) == 0u)
        {
            ++m_stats.remapInvalidCount;
        }
        AccumulateInvalidReasonStats(record.invalidReasonFlags, m_stats.invalidReasons);
    }

    for (const uint32_t remapIndex : m_currentToPreviousRemap)
    {
        if (remapIndex == PATH_TRACE_RESTIR_LIGHT_INVALID_INDEX)
        {
            ++m_stats.currentInvalidCount;
        }
        else
        {
            ++m_stats.currentMappedCount;
        }
    }

    for (const uint32_t remapIndex : m_previousToCurrentRemap)
    {
        if (remapIndex == PATH_TRACE_RESTIR_LIGHT_INVALID_INDEX)
        {
            ++m_stats.previousInvalidCount;
        }
        else
        {
            ++m_stats.previousMappedCount;
        }
    }
}

void PathTraceRestirLightManager::RebuildPreviousLookup()
{
    m_previousStableLookup.clear();
    for (size_t i = 0; i < m_previousLightRecords.size(); ++i)
    {
        PathTraceRestirPreviousLightRecord& record = m_previousLightRecords[i];
        if (!RecordHasStableIdentity(record))
        {
            continue;
        }

        const StableKey stableKey = MakeStableKey(record);
        auto previousIt = m_previousStableLookup.find(stableKey);
        if (previousIt != m_previousStableLookup.end())
        {
            MarkDuplicate(m_previousLightRecords[previousIt->second]);
            MarkDuplicate(record);
            continue;
        }
        m_previousStableLookup[stableKey] = static_cast<uint32_t>(i);
    }
}

PathTraceRestirLightObservationStats BuildPathTraceRestirLightManagerDebugObservations(
    const std::vector<PathTraceSmokeEmissiveTriangle>& emissiveTriangles,
    const std::vector<PathTraceDoomAnalyticLightCandidate>& doomAnalyticLights,
    const std::vector<PathTraceDoomAnalyticLightCandidateIdentity>& doomAnalyticIdentities)
{
    PathTraceRestirLightObservationStats stats;
    stats.emissiveObservationCount = static_cast<uint32_t>(emissiveTriangles.size());
    stats.doomAnalyticObservationCount = static_cast<uint32_t>(doomAnalyticLights.size());

    for (const PathTraceSmokeEmissiveTriangle& emissiveTriangle : emissiveTriangles)
    {
        if (emissiveTriangle.identityHashLo != 0u || emissiveTriangle.identityHashHi != 0u)
        {
            ++stats.emissiveStableIdentityCount;
        }
        else
        {
            ++stats.emissiveUnknownIdentityCount;
        }
    }

    for (size_t lightIndex = 0; lightIndex < doomAnalyticLights.size(); ++lightIndex)
    {
        const bool hasIdentityRecord = lightIndex < doomAnalyticIdentities.size();
        const PathTraceDoomAnalyticLightCandidateIdentity* identity = hasIdentityRecord ? &doomAnalyticIdentities[lightIndex] : nullptr;
        const bool stableIdentity =
            identity &&
            identity->universeIndex != PATH_TRACE_DOOM_ANALYTIC_LIGHT_INVALID_INDEX &&
            (identity->flags & PATH_TRACE_DOOM_ANALYTIC_IDENTITY_VALID) != 0u;
        if (stableIdentity)
        {
            ++stats.doomAnalyticStableIdentityCount;
        }
        else
        {
            ++stats.doomAnalyticUnknownIdentityCount;
        }
    }

    stats.totalObservationCount = stats.emissiveObservationCount + stats.doomAnalyticObservationCount;
    stats.stableIdentityCount = stats.emissiveStableIdentityCount + stats.doomAnalyticStableIdentityCount;
    stats.unknownIdentityCount = stats.emissiveUnknownIdentityCount + stats.doomAnalyticUnknownIdentityCount;
    return stats;
}
