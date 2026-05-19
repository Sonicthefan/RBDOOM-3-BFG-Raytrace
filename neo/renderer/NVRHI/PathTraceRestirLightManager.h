#pragma once

// CPU-only scaffold for a future ReSTIR light-manager bridge.
//
// This file intentionally owns no GPU buffers and is not routed into RAB or
// ReSTIR yet. It only defines the rbdoom-native domains, remaps, and counters
// that later slices will populate.

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

struct PathTraceDoomAnalyticLightCandidate;
struct PathTraceDoomAnalyticLightCandidateIdentity;
struct PathTraceSmokeEmissiveTriangle;

static constexpr uint32_t PATH_TRACE_RESTIR_LIGHT_INVALID_INDEX = 0xffffffffu;

enum PathTraceRestirLightSourceType : uint32_t
{
    PATH_TRACE_RESTIR_LIGHT_SOURCE_INVALID = 0u,
    PATH_TRACE_RESTIR_LIGHT_SOURCE_EMISSIVE_TRIANGLE = 1u,
    PATH_TRACE_RESTIR_LIGHT_SOURCE_DOOM_ANALYTIC = 2u
};

enum PathTraceRestirLightRecordFlags : uint32_t
{
    PATH_TRACE_RESTIR_LIGHT_RECORD_STABLE_IDENTITY = 1u << 0,
    PATH_TRACE_RESTIR_LIGHT_RECORD_PAYLOAD_CHANGED = 1u << 1,
    PATH_TRACE_RESTIR_LIGHT_RECORD_CURRENT_ONLY = 1u << 2,
    PATH_TRACE_RESTIR_LIGHT_RECORD_PREVIOUS_ONLY = 1u << 3,
    PATH_TRACE_RESTIR_LIGHT_RECORD_REMAP_VALID = 1u << 4
};

enum PathTraceRestirLightInvalidReasonFlags : uint32_t
{
    PATH_TRACE_RESTIR_LIGHT_INVALID_REASON_NONE = 0u,
    PATH_TRACE_RESTIR_LIGHT_INVALID_REASON_NEW_LIGHT = 1u << 0,
    PATH_TRACE_RESTIR_LIGHT_INVALID_REASON_MISSING_LIGHT = 1u << 1,
    PATH_TRACE_RESTIR_LIGHT_INVALID_REASON_TEMPORARY = 1u << 2,
    PATH_TRACE_RESTIR_LIGHT_INVALID_REASON_PROJECTILE = 1u << 3,
    PATH_TRACE_RESTIR_LIGHT_INVALID_REASON_DUPLICATE = 1u << 4,
    PATH_TRACE_RESTIR_LIGHT_INVALID_REASON_UNKNOWN_IDENTITY = 1u << 5,
    PATH_TRACE_RESTIR_LIGHT_INVALID_REASON_UNSUPPORTED_SOURCE = 1u << 6,
    PATH_TRACE_RESTIR_LIGHT_INVALID_REASON_STRUCTURAL_RESET = 1u << 7
};

struct PathTraceRestirCurrentLightRecord
{
    uint32_t sourceType = PATH_TRACE_RESTIR_LIGHT_SOURCE_INVALID;
    uint32_t sourceIndex = PATH_TRACE_RESTIR_LIGHT_INVALID_INDEX;
    uint32_t stableKeyLo = 0;
    uint32_t stableKeyHi = 0;
    uint32_t payloadHashLo = 0;
    uint32_t payloadHashHi = 0;
    uint32_t flags = 0;
    uint32_t invalidReasonFlags = PATH_TRACE_RESTIR_LIGHT_INVALID_REASON_NONE;
};

struct PathTraceRestirPreviousLightRecord
{
    uint32_t sourceType = PATH_TRACE_RESTIR_LIGHT_SOURCE_INVALID;
    uint32_t sourceIndex = PATH_TRACE_RESTIR_LIGHT_INVALID_INDEX;
    uint32_t stableKeyLo = 0;
    uint32_t stableKeyHi = 0;
    uint32_t payloadHashLo = 0;
    uint32_t payloadHashHi = 0;
    uint32_t flags = 0;
    uint32_t invalidReasonFlags = PATH_TRACE_RESTIR_LIGHT_INVALID_REASON_NONE;
};

struct PathTraceRestirLightInvalidReasonStats
{
    uint32_t newLight = 0;
    uint32_t missingLight = 0;
    uint32_t temporary = 0;
    uint32_t projectile = 0;
    uint32_t duplicate = 0;
    uint32_t unknownIdentity = 0;
    uint32_t unsupportedSource = 0;
    uint32_t structuralReset = 0;
};

struct PathTraceRestirLightManagerStats
{
    uint32_t currentLightCount = 0;
    uint32_t previousLightCount = 0;
    uint32_t currentToPreviousCount = 0;
    uint32_t previousToCurrentCount = 0;
    uint32_t currentMappedCount = 0;
    uint32_t currentInvalidCount = 0;
    uint32_t previousMappedCount = 0;
    uint32_t previousInvalidCount = 0;
    uint32_t stableIdentityCount = 0;
    uint32_t payloadChangedCount = 0;
    uint32_t currentOnlyCount = 0;
    uint32_t previousOnlyCount = 0;
    uint32_t remapValidCount = 0;
    uint32_t remapInvalidCount = 0;
    uint32_t mapSizeMismatchCount = 0;
    PathTraceRestirLightInvalidReasonStats invalidReasons;
};

struct PathTraceRestirLightObservationStats
{
    uint32_t emissiveObservationCount = 0;
    uint32_t emissiveStableIdentityCount = 0;
    uint32_t emissiveUnknownIdentityCount = 0;
    uint32_t doomAnalyticObservationCount = 0;
    uint32_t doomAnalyticStableIdentityCount = 0;
    uint32_t doomAnalyticUnknownIdentityCount = 0;
    uint32_t totalObservationCount = 0;
    uint32_t stableIdentityCount = 0;
    uint32_t unknownIdentityCount = 0;
};

class PathTraceRestirLightManager
{
public:
    void Clear();
    void BeginFrame();
    void UpdateFromObservations(
        const std::vector<PathTraceSmokeEmissiveTriangle>& emissiveTriangles,
        const std::vector<PathTraceDoomAnalyticLightCandidate>& doomAnalyticLights,
        const std::vector<PathTraceDoomAnalyticLightCandidateIdentity>& doomAnalyticIdentities);
    void EndFrame();
    PathTraceRestirLightManagerStats GetStats() const;

    const std::vector<PathTraceRestirCurrentLightRecord>& GetCurrentLightRecords() const;
    const std::vector<PathTraceRestirPreviousLightRecord>& GetPreviousLightRecords() const;
    const std::vector<uint32_t>& GetCurrentToPreviousRemap() const;
    const std::vector<uint32_t>& GetPreviousToCurrentRemap() const;

    struct StableKey
    {
        uint32_t sourceType = PATH_TRACE_RESTIR_LIGHT_SOURCE_INVALID;
        uint32_t keyLo = 0;
        uint32_t keyHi = 0;

        bool operator==(const StableKey& other) const
        {
            return sourceType == other.sourceType && keyLo == other.keyLo && keyHi == other.keyHi;
        }
    };

    struct StableKeyHash
    {
        size_t operator()(const StableKey& key) const;
    };

private:
    void RebuildStats();
    void RebuildCpuRemaps();
    void RebuildPreviousLookup();

    std::vector<PathTraceRestirCurrentLightRecord> m_currentLightRecords;
    std::vector<PathTraceRestirPreviousLightRecord> m_previousLightRecords;
    std::vector<uint32_t> m_currentToPreviousRemap;
    std::vector<uint32_t> m_previousToCurrentRemap;
    std::unordered_map<StableKey, uint32_t, StableKeyHash> m_previousStableLookup;
    PathTraceRestirLightManagerStats m_stats;
};

PathTraceRestirLightObservationStats BuildPathTraceRestirLightManagerDebugObservations(
    const std::vector<PathTraceSmokeEmissiveTriangle>& emissiveTriangles,
    const std::vector<PathTraceDoomAnalyticLightCandidate>& doomAnalyticLights,
    const std::vector<PathTraceDoomAnalyticLightCandidateIdentity>& doomAnalyticIdentities);
