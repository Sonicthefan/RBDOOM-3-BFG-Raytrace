#pragma once

// Persistent emissive light-candidate universe for ReSTIR/PT prep.
//
// Static emissive triangles are retained by stable-ish identity across frames.
// Rigid-entity emissives can be promoted to semi-static persistence after a
// short stability window. Other dynamic emissives remain frame-local.

#include "PathTraceEmissiveCandidates.h"

#include <unordered_map>
#include <vector>

struct viewDef_t;

const int RT_SMOKE_LIGHT_UNIVERSE_PORTAL_SWEEP_STEPS = 5;
const int RT_SMOKE_LIGHT_UNIVERSE_PORTAL_DEPTH_BINS = RT_SMOKE_LIGHT_UNIVERSE_PORTAL_SWEEP_STEPS + 3;
const int RT_SMOKE_LIGHT_UNIVERSE_OVERFLOW_SAMPLES = 4;
const int RT_SMOKE_LIGHT_UNIVERSE_DROPPED_SAMPLES = 4;
const int RT_SMOKE_LIGHT_UNIVERSE_CHURN_SAMPLES = 4;

struct RtSmokeLightUniverseCandidateSample
{
    bool valid = false;
    int areaNum = -1;
    uint32_t materialId = 0;
    uint32_t materialIndex = 0;
    float weight = 0.0f;
    float area = 0.0f;
    float distance = 0.0f;
    const char* reason = nullptr;
};

struct RtSmokeLightUniverseStats
{
    int persistentStaticTriangles = 0;
    int staticSeenThisFrame = 0;
    int staticNewThisFrame = 0;
    int staticUpdatedThisFrame = 0;
    int staticMissingThisFrame = 0;
    int persistentDynamicTriangles = 0;
    int dynamicSeenThisFrame = 0;
    int dynamicPromotedThisFrame = 0;
    int dynamicUpdatedThisFrame = 0;
    int dynamicMissingThisFrame = 0;
    int dynamicAgedOutThisFrame = 0;
    int dynamicFrameTriangles = 0;
    int dynamicFrameOnlyTriangles = 0;
    int dynamicPersistableFrameTriangles = 0;
    int dynamicUnpromotedFrameTriangles = 0;
    int dynamicPromotedFrameTriangles = 0;
    int staticMergedSeenTriangles = 0;
    int staticMergedMissingTriangles = 0;
    int injectedMissingDynamicTriangles = 0;
    int mergedTriangles = 0;
    int currentArea = -1;
    int totalAreas = 0;
    int staticAreaKnownTriangles = 0;
    int staticAreaUnknownTriangles = 0;
    int dynamicAreaKnownTriangles = 0;
    int dynamicAreaUnknownTriangles = 0;
    int mergedAreaKnownTriangles = 0;
    int mergedAreaUnknownTriangles = 0;
    int mergedCurrentAreaTriangles = 0;
    int mergedSelectedAreaTriangles = 0;
    int mergedConnectedAreaTriangles = 0;
    int mergedConnectedUnselectedAreaTriangles = 0;
    int mergedDisconnectedAreaTriangles = 0;
    int selectedPortalSteps = 0;
    int selectedAreaCount = 0;
    int selectedPortalEdges = 0;
    int selectedBlockedPortalEdges = 0;
    int portalStepSelectedAreas[RT_SMOKE_LIGHT_UNIVERSE_PORTAL_SWEEP_STEPS] = {};
    int portalStepMergedSelectedTriangles[RT_SMOKE_LIGHT_UNIVERSE_PORTAL_SWEEP_STEPS] = {};
    int mergedPortalDepthBins[RT_SMOKE_LIGHT_UNIVERSE_PORTAL_DEPTH_BINS] = {};
    int areaFilterEnabled = 0;
    int areaFilterApplied = 0;
    int areaFilterPortalSteps = 1;
    int areaFilterOverflowMax = 64;
    int areaFilterSelectedCandidates = 0;
    int areaFilterConnectedOverflowCandidates = 0;
    int areaFilterDisconnectedCandidates = 0;
    int areaFilterUnknownCandidates = 0;
    int areaFilterWouldUploadCandidates = 0;
    int areaFilterWouldDropCandidates = 0;
    float areaFilterPreArea = 0.0f;
    float areaFilterPreWeight = 0.0f;
    float areaFilterPostArea = 0.0f;
    float areaFilterPostWeight = 0.0f;
    float areaFilterDroppedArea = 0.0f;
    float areaFilterDroppedWeight = 0.0f;
    float areaFilterDroppedOverflowWeight = 0.0f;
    float areaFilterDroppedDisconnectedWeight = 0.0f;
    float areaFilterDroppedUnknownWeight = 0.0f;
    RtSmokeLightUniverseCandidateSample overflowSamples[RT_SMOKE_LIGHT_UNIVERSE_OVERFLOW_SAMPLES];
    RtSmokeLightUniverseCandidateSample droppedSamples[RT_SMOKE_LIGHT_UNIVERSE_DROPPED_SAMPLES];
    RtSmokeLightUniverseCandidateSample enteredSamples[RT_SMOKE_LIGHT_UNIVERSE_CHURN_SAMPLES];
    RtSmokeLightUniverseCandidateSample leftSamples[RT_SMOKE_LIGHT_UNIVERSE_CHURN_SAMPLES];
    int overflowSampleCount = 0;
    int droppedSampleCount = 0;
    int enteredSampleCount = 0;
    int leftSampleCount = 0;
    int activeChurnEnabled = 0;
    int activeChurnPrevious = 0;
    int activeChurnCurrent = 0;
    int activeChurnStayed = 0;
    int activeChurnEntered = 0;
    int activeChurnLeft = 0;
    float activeChurnPreviousWeight = 0.0f;
    float activeChurnCurrentWeight = 0.0f;
    float activeChurnStayedWeight = 0.0f;
    float activeChurnEnteredWeight = 0.0f;
    float activeChurnLeftWeight = 0.0f;
    uint64 generation = 1;
};

class RtSmokeLightUniverse
{
public:
    void Clear();
    std::vector<PathTraceSmokeEmissiveTriangle> MergeFrameCandidates(
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
        int dynamicMaxMissingFrames);
    RtSmokeLightUniverseStats GetStats() const;

private:
    struct PersistentEmissiveRecord
    {
        PathTraceSmokeEmissiveTriangle triangle = {};
        uint64 key = 0;
        uint64 lastSeenGeneration = 0;
        int areaNum = -1;
        bool seenThisFrame = false;
        int seenFrames = 0;
        bool promoted = false;
    };

    struct ActiveEmissiveRecord
    {
        RtSmokeLightUniverseCandidateSample sample = {};
    };

    static uint64 CandidateKey(const PathTraceSmokeEmissiveTriangle& triangle);
    static uint64 DynamicCandidateKey(const PathTraceSmokeEmissiveTriangle& triangle);
    static uint64 ActiveCandidateKey(const PathTraceSmokeEmissiveTriangle& triangle);
    static bool IsPersistableDynamicCandidate(const PathTraceSmokeEmissiveTriangle& triangle);
    void UpdateActiveChurn(
        const viewDef_t* viewDef,
        const std::vector<PathTraceSmokeEmissiveTriangle>& activeCandidates,
        bool activeChurnEnabled);

    uint64 m_generation = 1;
    std::vector<PersistentEmissiveRecord> m_staticRecords;
    std::vector<PersistentEmissiveRecord> m_dynamicRecords;
    std::unordered_map<uint64, size_t> m_staticLookup;
    std::unordered_map<uint64, size_t> m_dynamicLookup;
    std::unordered_map<uint64, ActiveEmissiveRecord> m_activeLookup;
    RtSmokeLightUniverseStats m_stats;
};
