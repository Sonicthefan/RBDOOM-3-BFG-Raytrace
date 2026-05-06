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

std::vector<PathTraceDoomAnalyticLightCandidate> BuildPathTraceDoomAnalyticLightCandidates(const viewDef_t* viewDef);
void RunPathTraceDoomLightDiagnostics(const viewDef_t* viewDef);
