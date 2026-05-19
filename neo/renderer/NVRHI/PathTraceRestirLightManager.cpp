#include "precompiled.h"
#pragma hdrstop

#include "PathTraceRestirLightManager.h"
#include "PathTraceDoomLights.h"
#include "PathTraceEmissiveCandidates.h"

namespace {

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

uint64 HashLightManagerValue64(uint64 hash, uint64_t value)
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
    if ((invalidReasonFlags & PATH_TRACE_RESTIR_LIGHT_INVALID_REASON_DELETED) != 0u)
    {
        ++stats.deleted;
    }
}

uint32_t TranslateDoomAnalyticInvalidReasons(uint32_t doomInvalidReasonFlags)
{
    uint32_t result = PATH_TRACE_RESTIR_LIGHT_INVALID_REASON_NONE;
    if ((doomInvalidReasonFlags & DOOM_LIGHT_UNIVERSE_INVALID_MISSING_PREVIOUS) != 0u)
    {
        result |= PATH_TRACE_RESTIR_LIGHT_INVALID_REASON_MISSING_LIGHT;
    }
    if ((doomInvalidReasonFlags & DOOM_LIGHT_UNIVERSE_INVALID_MISSING_CURRENT) != 0u)
    {
        result |= PATH_TRACE_RESTIR_LIGHT_INVALID_REASON_MISSING_LIGHT;
    }
    if ((doomInvalidReasonFlags & DOOM_LIGHT_UNIVERSE_INVALID_DUPLICATE_KEY) != 0u)
    {
        result |= PATH_TRACE_RESTIR_LIGHT_INVALID_REASON_DUPLICATE;
    }
    if ((doomInvalidReasonFlags & DOOM_LIGHT_UNIVERSE_INVALID_UNKNOWN_ENTITY) != 0u)
    {
        result |= PATH_TRACE_RESTIR_LIGHT_INVALID_REASON_UNKNOWN_IDENTITY;
    }
    if ((doomInvalidReasonFlags & DOOM_LIGHT_UNIVERSE_INVALID_UNPROVEN_CONTINUITY) != 0u)
    {
        result |= PATH_TRACE_RESTIR_LIGHT_INVALID_REASON_UNPROVEN_CONTINUITY;
    }
    if ((doomInvalidReasonFlags & DOOM_LIGHT_UNIVERSE_INVALID_ZERO_RADIANCE) != 0u)
    {
        result |= PATH_TRACE_RESTIR_LIGHT_INVALID_REASON_ZERO_RADIANCE;
    }
    if ((doomInvalidReasonFlags & DOOM_LIGHT_UNIVERSE_INVALID_SUPPRESSED) != 0u)
    {
        result |= PATH_TRACE_RESTIR_LIGHT_INVALID_REASON_SUPPRESSED;
    }
    if ((doomInvalidReasonFlags & DOOM_LIGHT_UNIVERSE_INVALID_OUT_OF_SELECTED_AREA) != 0u)
    {
        result |= PATH_TRACE_RESTIR_LIGHT_INVALID_REASON_OUT_OF_SELECTED_AREA;
    }
    if ((doomInvalidReasonFlags & DOOM_LIGHT_UNIVERSE_INVALID_DISCONNECTED_OR_PORTAL) != 0u)
    {
        result |= PATH_TRACE_RESTIR_LIGHT_INVALID_REASON_DISCONNECTED_OR_PORTAL;
    }
    if ((doomInvalidReasonFlags & (DOOM_LIGHT_UNIVERSE_INVALID_NON_POINT_OR_PARALLEL | DOOM_LIGHT_UNIVERSE_INVALID_RADIUS_INVALID)) != 0u)
    {
        result |= PATH_TRACE_RESTIR_LIGHT_INVALID_REASON_INVALID_SHAPE;
    }
    if ((doomInvalidReasonFlags & DOOM_LIGHT_UNIVERSE_INVALID_CANDIDATE_CAP_DROPPED) != 0u)
    {
        result |= PATH_TRACE_RESTIR_LIGHT_INVALID_REASON_CANDIDATE_CAP;
    }
    return result;
}

PathTraceRestirPreviousLightRecord MakePreviousRecord(const PathTraceRestirCurrentLightRecord& currentRecord)
{
    PathTraceRestirPreviousLightRecord previousRecord;
    previousRecord.sourceType = currentRecord.sourceType;
    previousRecord.payloadSourceIndex = currentRecord.payloadSourceIndex;
    previousRecord.identityKeyLo = currentRecord.identityKeyLo;
    previousRecord.identityKeyHi = currentRecord.identityKeyHi;
    previousRecord.compatibilityKey0 = currentRecord.compatibilityKey0;
    previousRecord.compatibilityKey1 = currentRecord.compatibilityKey1;
    previousRecord.compatibilityKey2 = currentRecord.compatibilityKey2;
    previousRecord.continuityClass = currentRecord.continuityClass;
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
        PATH_TRACE_RESTIR_LIGHT_INVALID_REASON_INCOMPATIBLE_SOURCE |
        PATH_TRACE_RESTIR_LIGHT_INVALID_REASON_DELETED);
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
    hash = HashLightManagerFloatArray(hash, emissiveTriangle.uvBounds, 4);
    hash = HashLightManagerFloatArray(hash, emissiveTriangle.centroidUvAndWeight, 3);
    hash = HashLightManagerValue(hash, emissiveTriangle.instanceId);
    hash = HashLightManagerValue(hash, emissiveTriangle.primitiveIndex);
    hash = HashLightManagerValue(hash, emissiveTriangle.flags);
    hash = HashLightManagerValue(hash, emissiveTriangle.materialId);
    hash = HashLightManagerValue(hash, emissiveTriangle.emissiveTextureIndex);
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

PathTraceRestirLightObservation MakeEmissiveObservation(
    const PathTraceSmokeEmissiveTriangle& emissiveTriangle,
    uint32_t payloadSourceIndex)
{
    PathTraceRestirLightObservation observation;
    observation.sourceType = PATH_TRACE_RESTIR_LIGHT_SOURCE_EMISSIVE_TRIANGLE;
    observation.payloadSourceIndex = payloadSourceIndex;
    observation.identityKeyLo = emissiveTriangle.identityHashLo;
    observation.identityKeyHi = emissiveTriangle.identityHashHi;
    observation.compatibilityKey0 = emissiveTriangle.materialId;
    observation.compatibilityKey1 = emissiveTriangle.universeMaterialIndex;
    observation.compatibilityKey2 = emissiveTriangle.emissiveTextureIndex;
    const uint64 payloadHash = HashEmissivePayload(emissiveTriangle);
    observation.payloadHashLo = static_cast<uint32_t>(payloadHash);
    observation.payloadHashHi = static_cast<uint32_t>(payloadHash >> 32);
    if (HasStableKey(observation.identityKeyLo, observation.identityKeyHi))
    {
        observation.flags |= PATH_TRACE_RESTIR_LIGHT_RECORD_STABLE_IDENTITY;
    }
    else
    {
        observation.invalidReasonFlags |= PATH_TRACE_RESTIR_LIGHT_INVALID_REASON_UNKNOWN_IDENTITY;
    }
    return observation;
}

PathTraceRestirLightObservation MakeDoomAnalyticObservation(
    const PathTraceDoomAnalyticLightCandidate& light,
    const PathTraceDoomAnalyticLightCandidateIdentity* identity,
    uint32_t payloadSourceIndex)
{
    PathTraceRestirLightObservation observation;
    observation.sourceType = PATH_TRACE_RESTIR_LIGHT_SOURCE_DOOM_ANALYTIC;
    observation.payloadSourceIndex = payloadSourceIndex;
    if (identity &&
        identity->universeIndex != PATH_TRACE_DOOM_ANALYTIC_LIGHT_INVALID_INDEX &&
        (identity->flags & PATH_TRACE_DOOM_ANALYTIC_IDENTITY_VALID) != 0u)
    {
        observation.identityKeyLo = identity->universeIndex;
        observation.identityKeyHi = 0u;
        observation.flags |= PATH_TRACE_RESTIR_LIGHT_RECORD_STABLE_IDENTITY;
        if ((identity->flags & PATH_TRACE_DOOM_ANALYTIC_IDENTITY_REMAP_VALID) == 0u)
        {
            observation.invalidReasonFlags |= TranslateDoomAnalyticInvalidReasons(identity->invalidReasonFlags);
            if (observation.invalidReasonFlags == PATH_TRACE_RESTIR_LIGHT_INVALID_REASON_NONE)
            {
                observation.invalidReasonFlags |= PATH_TRACE_RESTIR_LIGHT_INVALID_REASON_UNSUPPORTED_SOURCE;
            }
        }
    }
    else
    {
        observation.invalidReasonFlags |= PATH_TRACE_RESTIR_LIGHT_INVALID_REASON_UNKNOWN_IDENTITY;
    }

    const uint64 payloadHash = HashDoomAnalyticPayload(light);
    observation.payloadHashLo = static_cast<uint32_t>(payloadHash);
    observation.payloadHashHi = static_cast<uint32_t>(payloadHash >> 32);
    return observation;
}

PathTraceRestirCurrentLightRecord MakeCurrentRecordFromObservation(
    const PathTraceRestirLightObservation& observation)
{
    PathTraceRestirCurrentLightRecord record;
    record.sourceType = observation.sourceType;
    record.payloadSourceIndex = observation.payloadSourceIndex;
    record.identityKeyLo = observation.identityKeyLo;
    record.identityKeyHi = observation.identityKeyHi;
    record.compatibilityKey0 = observation.compatibilityKey0;
    record.compatibilityKey1 = observation.compatibilityKey1;
    record.compatibilityKey2 = observation.compatibilityKey2;
    record.payloadHashLo = observation.payloadHashLo;
    record.payloadHashHi = observation.payloadHashHi;
    record.flags = observation.flags;
    record.invalidReasonFlags = observation.invalidReasonFlags;
    if ((record.flags & PATH_TRACE_RESTIR_LIGHT_RECORD_STABLE_IDENTITY) != 0u)
    {
        record.continuityClass = record.invalidReasonFlags != PATH_TRACE_RESTIR_LIGHT_INVALID_REASON_NONE
            ? PATH_TRACE_RESTIR_LIGHT_CONTINUITY_REMAP_INVALID
            : PATH_TRACE_RESTIR_LIGHT_CONTINUITY_STABLE;
    }
    else
    {
        record.flags |= PATH_TRACE_RESTIR_LIGHT_RECORD_CURRENT_ONLY;
        record.continuityClass = PATH_TRACE_RESTIR_LIGHT_CONTINUITY_CURRENT_ONLY;
        if (record.invalidReasonFlags == PATH_TRACE_RESTIR_LIGHT_INVALID_REASON_NONE)
        {
            record.invalidReasonFlags |= PATH_TRACE_RESTIR_LIGHT_INVALID_REASON_UNKNOWN_IDENTITY;
        }
    }
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
    previousRecord.continuityClass = PATH_TRACE_RESTIR_LIGHT_CONTINUITY_PREVIOUS_ONLY;
    previousRecord.invalidReasonFlags |= PATH_TRACE_RESTIR_LIGHT_INVALID_REASON_MISSING_LIGHT;
    return previousRecord;
}

void MarkDuplicate(PathTraceRestirCurrentLightRecord& record)
{
    record.flags &= ~PATH_TRACE_RESTIR_LIGHT_RECORD_REMAP_VALID;
    record.continuityClass = PATH_TRACE_RESTIR_LIGHT_CONTINUITY_REMAP_INVALID;
    record.invalidReasonFlags |= PATH_TRACE_RESTIR_LIGHT_INVALID_REASON_DUPLICATE;
}

void MarkIncompatible(PathTraceRestirCurrentLightRecord& record)
{
    record.flags &= ~PATH_TRACE_RESTIR_LIGHT_RECORD_REMAP_VALID;
    record.continuityClass = PATH_TRACE_RESTIR_LIGHT_CONTINUITY_REMAP_INVALID;
    record.invalidReasonFlags |= PATH_TRACE_RESTIR_LIGHT_INVALID_REASON_INCOMPATIBLE_SOURCE;
}

void MarkIncompatible(PathTraceRestirPreviousLightRecord& record)
{
    record.flags &= ~PATH_TRACE_RESTIR_LIGHT_RECORD_REMAP_VALID;
    record.continuityClass = PATH_TRACE_RESTIR_LIGHT_CONTINUITY_REMAP_INVALID;
    record.invalidReasonFlags |= PATH_TRACE_RESTIR_LIGHT_INVALID_REASON_INCOMPATIBLE_SOURCE;
}

void MarkDuplicate(PathTraceRestirPreviousLightRecord& record)
{
    record.flags &= ~PATH_TRACE_RESTIR_LIGHT_RECORD_REMAP_VALID;
    record.continuityClass = PATH_TRACE_RESTIR_LIGHT_CONTINUITY_REMAP_INVALID;
    record.invalidReasonFlags |= PATH_TRACE_RESTIR_LIGHT_INVALID_REASON_DUPLICATE;
}

PathTraceRestirLightManager::StableKey MakeStableKey(const PathTraceRestirCurrentLightRecord& record)
{
    PathTraceRestirLightManager::StableKey key;
    key.sourceType = record.sourceType;
    key.keyLo = record.identityKeyLo;
    key.keyHi = record.identityKeyHi;
    return key;
}

PathTraceRestirLightManager::StableKey MakeStableKey(const PathTraceRestirPreviousLightRecord& record)
{
    PathTraceRestirLightManager::StableKey key;
    key.sourceType = record.sourceType;
    key.keyLo = record.identityKeyLo;
    key.keyHi = record.identityKeyHi;
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
    m_lastStructuralSignature = 0;
    m_lastMappingIdentitySignature = 0;
    m_lastAnimatedPayloadSignature = 0;
    m_haveLastSignatures = false;
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
    const std::vector<PathTraceRestirLightObservation>& observations)
{
    m_currentLightRecords.clear();
    m_currentLightRecords.reserve(observations.size());

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

    for (const PathTraceRestirLightObservation& observation : observations)
    {
        addCurrentRecord(MakeCurrentRecordFromObservation(observation));
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
                currentRecord.continuityClass = PATH_TRACE_RESTIR_LIGHT_CONTINUITY_CURRENT_ONLY;
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
        currentRecord.continuityClass = PATH_TRACE_RESTIR_LIGHT_CONTINUITY_STABLE;
        previousRecord.continuityClass = PATH_TRACE_RESTIR_LIGHT_CONTINUITY_STABLE;
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
    RebuildSignatures();

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
    m_stats.stableMappedCount = 0;
    m_stats.payloadChangedMappedCount = 0;
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
        if ((record.flags & PATH_TRACE_RESTIR_LIGHT_RECORD_REMAP_VALID) != 0u &&
            record.continuityClass == PATH_TRACE_RESTIR_LIGHT_CONTINUITY_STABLE)
        {
            ++m_stats.stableMappedCount;
            if ((record.flags & PATH_TRACE_RESTIR_LIGHT_RECORD_PAYLOAD_CHANGED) != 0u)
            {
                ++m_stats.payloadChangedMappedCount;
            }
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

void PathTraceRestirLightManager::RebuildSignatures()
{
    const uint64_t structuralVersion = 1;
    uint64_t structuralHash = 1469598103934665603ull;
    structuralHash = HashLightManagerValue64(structuralHash, structuralVersion);
    structuralHash = HashLightManagerValue(structuralHash, m_currentToPreviousRemap.size() == m_currentLightRecords.size() ? 0u : 1u);
    structuralHash = HashLightManagerValue(structuralHash, m_previousToCurrentRemap.size() == m_previousLightRecords.size() ? 0u : 1u);

    uint64_t mappingHash = 1469598103934665603ull;
    const uint64_t currentCount = static_cast<uint64_t>(m_currentLightRecords.size());
    const uint64_t previousCount = static_cast<uint64_t>(m_previousLightRecords.size());
    mappingHash = HashLightManagerValue64(mappingHash, currentCount);
    for (const PathTraceRestirCurrentLightRecord& record : m_currentLightRecords)
    {
        mappingHash = HashLightManagerValue(mappingHash, record.sourceType);
        mappingHash = HashLightManagerValue(mappingHash, record.identityKeyLo);
        mappingHash = HashLightManagerValue(mappingHash, record.identityKeyHi);
        mappingHash = HashLightManagerValue(mappingHash, record.compatibilityKey0);
        mappingHash = HashLightManagerValue(mappingHash, record.compatibilityKey1);
        mappingHash = HashLightManagerValue(mappingHash, record.compatibilityKey2);
        mappingHash = HashLightManagerValue(mappingHash, record.continuityClass);
        mappingHash = HashLightManagerValue(mappingHash, record.flags & ~PATH_TRACE_RESTIR_LIGHT_RECORD_PAYLOAD_CHANGED);
        mappingHash = HashLightManagerValue(mappingHash, record.invalidReasonFlags);
    }
    mappingHash = HashLightManagerValue64(mappingHash, previousCount);
    for (const PathTraceRestirPreviousLightRecord& record : m_previousLightRecords)
    {
        mappingHash = HashLightManagerValue(mappingHash, record.sourceType);
        mappingHash = HashLightManagerValue(mappingHash, record.identityKeyLo);
        mappingHash = HashLightManagerValue(mappingHash, record.identityKeyHi);
        mappingHash = HashLightManagerValue(mappingHash, record.compatibilityKey0);
        mappingHash = HashLightManagerValue(mappingHash, record.compatibilityKey1);
        mappingHash = HashLightManagerValue(mappingHash, record.compatibilityKey2);
        mappingHash = HashLightManagerValue(mappingHash, record.continuityClass);
        mappingHash = HashLightManagerValue(mappingHash, record.flags & ~PATH_TRACE_RESTIR_LIGHT_RECORD_PAYLOAD_CHANGED);
        mappingHash = HashLightManagerValue(mappingHash, record.invalidReasonFlags);
    }
    mappingHash = HashLightManagerValue64(mappingHash, static_cast<uint64_t>(m_currentToPreviousRemap.size()));
    for (const uint32_t remapIndex : m_currentToPreviousRemap)
    {
        mappingHash = HashLightManagerValue(mappingHash, remapIndex);
    }
    mappingHash = HashLightManagerValue64(mappingHash, static_cast<uint64_t>(m_previousToCurrentRemap.size()));
    for (const uint32_t remapIndex : m_previousToCurrentRemap)
    {
        mappingHash = HashLightManagerValue(mappingHash, remapIndex);
    }

    uint64_t payloadHash = 1469598103934665603ull;
    payloadHash = HashLightManagerValue64(payloadHash, currentCount);
    for (const PathTraceRestirCurrentLightRecord& record : m_currentLightRecords)
    {
        payloadHash = HashLightManagerValue(payloadHash, record.sourceType);
        payloadHash = HashLightManagerValue(payloadHash, record.identityKeyLo);
        payloadHash = HashLightManagerValue(payloadHash, record.identityKeyHi);
        payloadHash = HashLightManagerValue(payloadHash, record.payloadHashLo);
        payloadHash = HashLightManagerValue(payloadHash, record.payloadHashHi);
    }

    m_stats.structuralSignature = structuralHash;
    m_stats.mappingIdentitySignature = mappingHash;
    m_stats.animatedPayloadSignature = payloadHash;
    m_stats.structuralSignatureChanged = !m_haveLastSignatures || structuralHash != m_lastStructuralSignature ? 1u : 0u;
    m_stats.mappingIdentitySignatureChanged = !m_haveLastSignatures || mappingHash != m_lastMappingIdentitySignature ? 1u : 0u;
    m_stats.animatedPayloadSignatureChanged = !m_haveLastSignatures || payloadHash != m_lastAnimatedPayloadSignature ? 1u : 0u;
    m_lastStructuralSignature = structuralHash;
    m_lastMappingIdentitySignature = mappingHash;
    m_lastAnimatedPayloadSignature = payloadHash;
    m_haveLastSignatures = true;
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

std::vector<PathTraceRestirLightObservation> BuildPathTraceRestirLightManagerObservations(
    const std::vector<PathTraceSmokeEmissiveTriangle>& emissiveTriangles,
    const std::vector<PathTraceDoomAnalyticLightCandidate>& doomAnalyticLights,
    const std::vector<PathTraceDoomAnalyticLightCandidateIdentity>& doomAnalyticIdentities)
{
    std::vector<PathTraceRestirLightObservation> observations;
    observations.reserve(emissiveTriangles.size() + doomAnalyticLights.size());

    for (size_t i = 0; i < emissiveTriangles.size(); ++i)
    {
        observations.push_back(MakeEmissiveObservation(emissiveTriangles[i], static_cast<uint32_t>(i)));
    }

    for (size_t i = 0; i < doomAnalyticLights.size(); ++i)
    {
        const PathTraceDoomAnalyticLightCandidateIdentity* identity =
            i < doomAnalyticIdentities.size() ? &doomAnalyticIdentities[i] : nullptr;
        observations.push_back(MakeDoomAnalyticObservation(doomAnalyticLights[i], identity, static_cast<uint32_t>(i)));
    }

    return observations;
}

PathTraceRestirLightObservationStats BuildPathTraceRestirLightManagerDebugObservations(
    const std::vector<PathTraceRestirLightObservation>& observations)
{
    PathTraceRestirLightObservationStats stats;

    for (const PathTraceRestirLightObservation& observation : observations)
    {
        if (observation.sourceType == PATH_TRACE_RESTIR_LIGHT_SOURCE_EMISSIVE_TRIANGLE)
        {
            ++stats.emissiveObservationCount;
            if ((observation.flags & PATH_TRACE_RESTIR_LIGHT_RECORD_STABLE_IDENTITY) != 0u)
            {
                ++stats.emissiveStableIdentityCount;
            }
            else if ((observation.invalidReasonFlags & PATH_TRACE_RESTIR_LIGHT_INVALID_REASON_UNKNOWN_IDENTITY) != 0u)
            {
                ++stats.emissiveUnknownIdentityCount;
            }
        }
        else if (observation.sourceType == PATH_TRACE_RESTIR_LIGHT_SOURCE_DOOM_ANALYTIC)
        {
            ++stats.doomAnalyticObservationCount;
            if ((observation.flags & PATH_TRACE_RESTIR_LIGHT_RECORD_STABLE_IDENTITY) != 0u)
            {
                ++stats.doomAnalyticStableIdentityCount;
            }
            else if ((observation.invalidReasonFlags & PATH_TRACE_RESTIR_LIGHT_INVALID_REASON_UNKNOWN_IDENTITY) != 0u)
            {
                ++stats.doomAnalyticUnknownIdentityCount;
            }
        }

        if ((observation.flags & PATH_TRACE_RESTIR_LIGHT_RECORD_STABLE_IDENTITY) != 0u &&
            observation.invalidReasonFlags == PATH_TRACE_RESTIR_LIGHT_INVALID_REASON_NONE)
        {
            ++stats.stableMappedReadyCount;
        }
        if (observation.invalidReasonFlags != PATH_TRACE_RESTIR_LIGHT_INVALID_REASON_NONE)
        {
            ++stats.remapInvalidObservationCount;
        }
        if ((observation.invalidReasonFlags & PATH_TRACE_RESTIR_LIGHT_INVALID_REASON_UNSUPPORTED_SOURCE) != 0u)
        {
            ++stats.unsupportedObservationCount;
        }
        if (observation.payloadSourceIndex != PATH_TRACE_RESTIR_LIGHT_INVALID_INDEX)
        {
            ++stats.payloadSourceValidCount;
        }
    }

    stats.totalObservationCount = stats.emissiveObservationCount + stats.doomAnalyticObservationCount;
    stats.stableIdentityCount = stats.emissiveStableIdentityCount + stats.doomAnalyticStableIdentityCount;
    stats.unknownIdentityCount = stats.emissiveUnknownIdentityCount + stats.doomAnalyticUnknownIdentityCount;
    return stats;
}
