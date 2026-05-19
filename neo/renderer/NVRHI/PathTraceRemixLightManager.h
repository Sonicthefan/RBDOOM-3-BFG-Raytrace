#pragma once

// CPU-owned RTX Remix-shaped light manager contract.
//
// This owns the current/previous light payload domain, bidirectional remaps,
// light type ranges, sample-count metadata, and signatures. It does not own GPU
// buffers, RAB routing, RTXDI dispatch, or reservoir clear policy.

#include "PathTraceRemixFramePrepare.h"
#include "PathTraceUnifiedLight.h"

#include <cstdint>
#include <vector>

struct PathTraceDoomAnalyticLightCandidate;
struct PathTraceDoomAnalyticLightCandidateIdentity;
struct PathTraceDoomAnalyticLightRemap;
struct PathTraceEmissiveLightRemap;
struct PathTraceSmokeEmissiveTriangle;

static constexpr uint32_t PATH_TRACE_REMIX_LIGHT_INVALID_INDEX = 0xffffffffu;

enum PathTraceRemixLightType : uint32_t
{
    PATH_TRACE_REMIX_LIGHT_TYPE_EMISSIVE_TRIANGLE = 0u,
    PATH_TRACE_REMIX_LIGHT_TYPE_DOOM_ANALYTIC = 1u,
    PATH_TRACE_REMIX_LIGHT_TYPE_COUNT = 2u
};

struct PathTraceRemixLightRange
{
    uint32_t firstLightIndex = 0;
    uint32_t lightCount = 0;
    uint32_t sampleCount = 0;
    uint32_t padding0 = 0;
};

struct PathTraceRemixLightManagerStats
{
    uint64_t frameIndex = 0;
    uint32_t currentLightCount = 0;
    uint32_t previousLightCount = 0;
    uint32_t currentToPreviousCount = 0;
    uint32_t previousToCurrentCount = 0;
    uint32_t currentMappedCount = 0;
    uint32_t currentInvalidCount = 0;
    uint32_t previousMappedCount = 0;
    uint32_t previousInvalidCount = 0;
    uint32_t emissiveRangeOffset = 0;
    uint32_t emissiveRangeCount = 0;
    uint32_t doomAnalyticRangeOffset = 0;
    uint32_t doomAnalyticRangeCount = 0;
    uint32_t emissiveSampleCount = 0;
    uint32_t doomAnalyticSampleCount = 0;
    uint32_t totalSampleCount = 0;
    uint32_t nonEmptyRangeCount = 0;
    uint32_t payloadOnlyChange = 0;
    uint32_t structuralSignatureChanged = 0;
    uint32_t mappingSignatureChanged = 0;
    uint32_t payloadSignatureChanged = 0;
    uint32_t oldSmokeReservoirSignatureConsulted = 0;
    uint32_t resourceAllocationCount = 0;
    uint32_t shaderRouteCount = 0;
    uint64_t structuralSignature = 0;
    uint64_t mappingSignature = 0;
    uint64_t payloadSignature = 0;
};

class PathTraceRemixLightManager
{
public:
    void Clear();
    void PrepareSceneData(
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
        float analyticStateCompatibilityTolerance);

    const std::vector<PathTraceUnifiedLightRecord>& GetCurrentLightPayloads() const;
    const std::vector<PathTraceUnifiedLightRecord>& GetPreviousLightPayloads() const;
    const std::vector<uint32_t>& GetCurrentToPreviousMap() const;
    const std::vector<uint32_t>& GetPreviousToCurrentMap() const;
    const PathTraceRemixLightRange* GetLightRanges() const;
    const PathTraceRemixLightManagerStats& GetStats() const;

private:
    void RebuildPreviousToCurrentMap();
    void RebuildLightRanges(
        uint32_t currentEmissiveCount,
        uint32_t currentAnalyticCount,
        uint32_t emissiveSampleCount,
        uint32_t doomAnalyticSampleCount);
    void RebuildStats(const PathTraceRemixFramePrepareObservationPackage& framePackage);
    void RebuildSignatures();

    std::vector<PathTraceUnifiedLightRecord> m_currentLightPayloads;
    std::vector<PathTraceUnifiedLightRecord> m_previousLightPayloads;
    std::vector<uint32_t> m_currentToPreviousMap;
    std::vector<uint32_t> m_previousToCurrentMap;
    PathTraceRemixLightRange m_lightRanges[PATH_TRACE_REMIX_LIGHT_TYPE_COUNT];
    PathTraceRemixLightManagerStats m_stats;
    uint64_t m_lastStructuralSignature = 0;
    uint64_t m_lastMappingSignature = 0;
    uint64_t m_lastPayloadSignature = 0;
    bool m_haveLastSignatures = false;
};
