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

enum PathTraceRestirLightContinuityClass : uint32_t
{
    PATH_TRACE_RESTIR_LIGHT_CONTINUITY_REMAP_INVALID = 0u,
    PATH_TRACE_RESTIR_LIGHT_CONTINUITY_STABLE = 1u,
    PATH_TRACE_RESTIR_LIGHT_CONTINUITY_CURRENT_ONLY = 2u,
    PATH_TRACE_RESTIR_LIGHT_CONTINUITY_PREVIOUS_ONLY = 3u
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
    PATH_TRACE_RESTIR_LIGHT_INVALID_REASON_STRUCTURAL_RESET = 1u << 7,
    PATH_TRACE_RESTIR_LIGHT_INVALID_REASON_UNPROVEN_CONTINUITY = 1u << 8,
    PATH_TRACE_RESTIR_LIGHT_INVALID_REASON_ZERO_RADIANCE = 1u << 9,
    PATH_TRACE_RESTIR_LIGHT_INVALID_REASON_SUPPRESSED = 1u << 10,
    PATH_TRACE_RESTIR_LIGHT_INVALID_REASON_OUT_OF_SELECTED_AREA = 1u << 11,
    PATH_TRACE_RESTIR_LIGHT_INVALID_REASON_DISCONNECTED_OR_PORTAL = 1u << 12,
    PATH_TRACE_RESTIR_LIGHT_INVALID_REASON_INVALID_SHAPE = 1u << 13,
    PATH_TRACE_RESTIR_LIGHT_INVALID_REASON_CANDIDATE_CAP = 1u << 14,
    PATH_TRACE_RESTIR_LIGHT_INVALID_REASON_INCOMPATIBLE_SOURCE = 1u << 15,
    PATH_TRACE_RESTIR_LIGHT_INVALID_REASON_DELETED = 1u << 16
};

struct PathTraceRestirCurrentLightRecord
{
    uint32_t sourceType = PATH_TRACE_RESTIR_LIGHT_SOURCE_INVALID;
    uint32_t payloadSourceIndex = PATH_TRACE_RESTIR_LIGHT_INVALID_INDEX;
    uint32_t identityKeyLo = 0;
    uint32_t identityKeyHi = 0;
    uint32_t compatibilityKey0 = 0;
    uint32_t compatibilityKey1 = 0;
    uint32_t compatibilityKey2 = 0;
    uint32_t continuityClass = PATH_TRACE_RESTIR_LIGHT_CONTINUITY_REMAP_INVALID;
    uint32_t payloadHashLo = 0;
    uint32_t payloadHashHi = 0;
    uint32_t flags = 0;
    uint32_t invalidReasonFlags = PATH_TRACE_RESTIR_LIGHT_INVALID_REASON_NONE;
};

static_assert(sizeof(PathTraceRestirCurrentLightRecord) == 48, "PathTraceRestirCurrentLightRecord must match HLSL layout");
static_assert((sizeof(PathTraceRestirCurrentLightRecord) % 16) == 0, "PathTraceRestirCurrentLightRecord must stay 16-byte aligned for HLSL StructuredBuffer reads");

struct PathTraceRestirPreviousLightRecord
{
    uint32_t sourceType = PATH_TRACE_RESTIR_LIGHT_SOURCE_INVALID;
    uint32_t payloadSourceIndex = PATH_TRACE_RESTIR_LIGHT_INVALID_INDEX;
    uint32_t identityKeyLo = 0;
    uint32_t identityKeyHi = 0;
    uint32_t compatibilityKey0 = 0;
    uint32_t compatibilityKey1 = 0;
    uint32_t compatibilityKey2 = 0;
    uint32_t continuityClass = PATH_TRACE_RESTIR_LIGHT_CONTINUITY_REMAP_INVALID;
    uint32_t payloadHashLo = 0;
    uint32_t payloadHashHi = 0;
    uint32_t flags = 0;
    uint32_t invalidReasonFlags = PATH_TRACE_RESTIR_LIGHT_INVALID_REASON_NONE;
};

static_assert(sizeof(PathTraceRestirPreviousLightRecord) == 48, "PathTraceRestirPreviousLightRecord must match HLSL layout");
static_assert((sizeof(PathTraceRestirPreviousLightRecord) % 16) == 0, "PathTraceRestirPreviousLightRecord must stay 16-byte aligned for HLSL StructuredBuffer reads");

struct PathTraceRestirLightObservation
{
    uint32_t sourceType = PATH_TRACE_RESTIR_LIGHT_SOURCE_INVALID;
    uint32_t payloadSourceIndex = PATH_TRACE_RESTIR_LIGHT_INVALID_INDEX;
    uint32_t identityKeyLo = 0;
    uint32_t identityKeyHi = 0;
    uint32_t compatibilityKey0 = 0;
    uint32_t compatibilityKey1 = 0;
    uint32_t compatibilityKey2 = 0;
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
    uint32_t unprovenContinuity = 0;
    uint32_t zeroRadiance = 0;
    uint32_t suppressed = 0;
    uint32_t outOfSelectedArea = 0;
    uint32_t disconnectedOrPortal = 0;
    uint32_t invalidShape = 0;
    uint32_t candidateCap = 0;
    uint32_t incompatibleSource = 0;
    uint32_t deleted = 0;
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
    uint32_t stableMappedCount = 0;
    uint32_t payloadChangedMappedCount = 0;
    uint32_t currentOnlyCount = 0;
    uint32_t previousOnlyCount = 0;
    uint32_t remapValidCount = 0;
    uint32_t remapInvalidCount = 0;
    uint32_t mapSizeMismatchCount = 0;
    uint32_t structuralSignatureChanged = 0;
    uint32_t mappingIdentitySignatureChanged = 0;
    uint32_t animatedPayloadSignatureChanged = 0;
    uint64_t structuralSignature = 0;
    uint64_t mappingIdentitySignature = 0;
    uint64_t animatedPayloadSignature = 0;
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
    uint32_t stableMappedReadyCount = 0;
    uint32_t remapInvalidObservationCount = 0;
    uint32_t unsupportedObservationCount = 0;
    uint32_t payloadSourceValidCount = 0;
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
        const std::vector<PathTraceRestirLightObservation>& observations);
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
    void RebuildSignatures();

    std::vector<PathTraceRestirCurrentLightRecord> m_currentLightRecords;
    std::vector<PathTraceRestirPreviousLightRecord> m_previousLightRecords;
    std::vector<uint32_t> m_currentToPreviousRemap;
    std::vector<uint32_t> m_previousToCurrentRemap;
    std::unordered_map<StableKey, uint32_t, StableKeyHash> m_previousStableLookup;
    PathTraceRestirLightManagerStats m_stats;
    uint64_t m_lastStructuralSignature = 0;
    uint64_t m_lastMappingIdentitySignature = 0;
    uint64_t m_lastAnimatedPayloadSignature = 0;
    bool m_haveLastSignatures = false;
};

std::vector<PathTraceRestirLightObservation> BuildPathTraceRestirLightManagerObservations(
    const std::vector<PathTraceSmokeEmissiveTriangle>& emissiveTriangles,
    const std::vector<PathTraceDoomAnalyticLightCandidate>& doomAnalyticLights,
    const std::vector<PathTraceDoomAnalyticLightCandidateIdentity>& doomAnalyticIdentities);

PathTraceRestirLightObservationStats BuildPathTraceRestirLightManagerDebugObservations(
    const std::vector<PathTraceRestirLightObservation>& observations);
