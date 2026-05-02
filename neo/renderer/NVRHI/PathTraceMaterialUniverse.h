#pragma once

// Persistent RT smoke material records.
//
// This layer owns stable per-material data that can outlive a single frame's
// GPU-facing material table. DynamicMaterialState still assembles the current
// frame table and texture slots; the universe provides the cached base records.

#include "PathTraceEmissiveCandidates.h"
#include "PathTraceTextureRegistry.h"

struct RtSmokePersistentMaterialRecord
{
    bool valid = false;
    uint64 signature = 0;
    PathTraceSmokeMaterial material = {};
    int additiveDecalContribution = 0;
};

struct RtSmokeMaterialUniverseStats
{
    int records = 0;
    int hits = 0;
    int misses = 0;
    int rebuilds = 0;
    int validationChecks = 0;
    int validationMismatches = 0;
};

const RtSmokePersistentMaterialRecord& GetSmokePersistentMaterialRecord(uint32_t materialId, const RtSmokeMaterialTextureInfo& info);
RtSmokeMaterialUniverseStats GetSmokeMaterialUniverseStats();
