#pragma once

// Doom renderer light diagnostics for future analytic PT/ReSTIR candidates.
//
// This module inspects renderer lightDefs, prints stable identity / proximity
// information, and builds a separate analytic light candidate buffer. It does
// not add light meshes to the BVH.

#include <cstdint>
#include <vector>

struct viewDef_t;

struct PathTraceDoomAnalyticLightCandidate
{
    float originAndRadius[4];
    float colorAndIntensity[4];
    float doomRadiusAndArea[4];
    uint32_t flags = 0;
    uint32_t renderLightIndex = 0;
    uint32_t entityNumber = 0;
    uint32_t padding0 = 0;
};
static_assert((sizeof(PathTraceDoomAnalyticLightCandidate) % 16) == 0, "PathTraceDoomAnalyticLightCandidate must stay 16-byte aligned for HLSL StructuredBuffer reads");

static constexpr uint32_t PATH_TRACE_DOOM_ANALYTIC_LIGHT_INVALID_INDEX = 0xffffffffu;

enum PathTraceDoomAnalyticLightIdentityFlags : uint32_t
{
    PATH_TRACE_DOOM_ANALYTIC_IDENTITY_VALID = 1u << 0,
    PATH_TRACE_DOOM_ANALYTIC_IDENTITY_SAMPLEABLE = 1u << 1,
    PATH_TRACE_DOOM_ANALYTIC_IDENTITY_REMAP_VALID = 1u << 2
};

struct PathTraceDoomAnalyticLightCandidateIdentity
{
    uint32_t universeIndex = PATH_TRACE_DOOM_ANALYTIC_LIGHT_INVALID_INDEX;
    uint32_t flags = 0;
    uint32_t invalidReasonFlags = 0;
    uint32_t padding0 = 0;
};
static_assert((sizeof(PathTraceDoomAnalyticLightCandidateIdentity) % 16) == 0, "PathTraceDoomAnalyticLightCandidateIdentity must stay 16-byte aligned for HLSL StructuredBuffer reads");

struct PathTraceDoomAnalyticLightRemap
{
    int32_t previousToCurrentCandidateIndex = -1;
    int32_t currentToPreviousCandidateIndex = -1;
    uint32_t flags = 0;
    uint32_t invalidReasonFlags = 0;
};
static_assert((sizeof(PathTraceDoomAnalyticLightRemap) % 16) == 0, "PathTraceDoomAnalyticLightRemap must stay 16-byte aligned for HLSL StructuredBuffer reads");

struct PathTraceDoomAnalyticLightGpuRemap
{
    std::vector<PathTraceDoomAnalyticLightCandidate> previousCandidates;
    std::vector<PathTraceDoomAnalyticLightCandidateIdentity> currentCandidateIdentities;
    std::vector<PathTraceDoomAnalyticLightCandidateIdentity> previousCandidateIdentities;
    std::vector<PathTraceDoomAnalyticLightRemap> universeRemap;
    int invalidRemapCount = 0;
};

std::vector<PathTraceDoomAnalyticLightCandidate> BuildPathTraceDoomAnalyticLightCandidates(const viewDef_t* viewDef, bool forceEnable = false);
const PathTraceDoomAnalyticLightGpuRemap& GetPathTraceDoomAnalyticLightGpuRemap();
void RunPathTraceDoomLightDiagnostics(const viewDef_t* viewDef);
