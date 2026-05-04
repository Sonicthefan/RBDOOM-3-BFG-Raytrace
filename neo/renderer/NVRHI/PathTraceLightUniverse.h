#pragma once

// Persistent emissive light-candidate universe for ReSTIR/PT prep.
//
// Static emissive triangles are retained by stable-ish identity across frames.
// Rigid-entity emissives can be promoted to semi-static persistence after a
// short stability window. Other dynamic emissives remain frame-local.

#include "PathTraceEmissiveCandidates.h"

#include <unordered_map>
#include <vector>

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
    uint64 generation = 1;
};

class RtSmokeLightUniverse
{
public:
    void Clear();
    std::vector<PathTraceSmokeEmissiveTriangle> MergeFrameCandidates(
        const std::vector<PathTraceSmokeEmissiveTriangle>& frameCandidates,
        int maxRecords,
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
        bool seenThisFrame = false;
        int seenFrames = 0;
        bool promoted = false;
    };

    static uint64 CandidateKey(const PathTraceSmokeEmissiveTriangle& triangle);
    static uint64 DynamicCandidateKey(const PathTraceSmokeEmissiveTriangle& triangle);
    static bool IsPersistableDynamicCandidate(const PathTraceSmokeEmissiveTriangle& triangle);

    uint64 m_generation = 1;
    std::vector<PersistentEmissiveRecord> m_staticRecords;
    std::vector<PersistentEmissiveRecord> m_dynamicRecords;
    std::unordered_map<uint64, size_t> m_staticLookup;
    std::unordered_map<uint64, size_t> m_dynamicLookup;
    RtSmokeLightUniverseStats m_stats;
};
