#pragma once

// Unified local-light record shape for the ReSTIR-friendly light-universe
// rebuild. This is a layout contract only; upload/binding comes later.

#include <cstdint>
#include <vector>

struct PathTraceSmokeEmissiveTriangle;
struct PathTraceEmissiveLightRemap;
struct PathTraceDoomAnalyticLightCandidate;
struct PathTraceDoomAnalyticLightCandidateIdentity;
struct PathTraceDoomAnalyticLightRemap;

static constexpr uint32_t PATH_TRACE_UNIFIED_LIGHT_TYPE_INVALID = 0u;
static constexpr uint32_t PATH_TRACE_UNIFIED_LIGHT_TYPE_EMISSIVE_TRIANGLE = 1u;
static constexpr uint32_t PATH_TRACE_UNIFIED_LIGHT_TYPE_DOOM_ANALYTIC = 2u;
static constexpr uint32_t PATH_TRACE_UNIFIED_LIGHT_INVALID_INDEX = 0xffffffffu;

struct PathTraceUnifiedLightRecord
{
    float positionAndRadius[4] = {};
    float normalAndArea[4] = {};
    float radianceAndLuminance[4] = {};
    float uvOrDoomParams[4] = {};

    uint32_t type = PATH_TRACE_UNIFIED_LIGHT_TYPE_INVALID;
    uint32_t sourceIndex = PATH_TRACE_UNIFIED_LIGHT_INVALID_INDEX;
    uint32_t flags = 0;
    uint32_t materialOrLightId = 0;

    uint32_t instanceId = 0;
    uint32_t primitiveIndex = 0;
    uint32_t identityA = 0;
    uint32_t identityB = 0;

    float sourcePdf = 0.0f;
    float sourceWeight = 0.0f;
    uint32_t previousIndex = PATH_TRACE_UNIFIED_LIGHT_INVALID_INDEX;
    uint32_t padding0 = 0;
};
static_assert(sizeof(PathTraceUnifiedLightRecord) == 112, "PathTraceUnifiedLightRecord must match HLSL layout");
static_assert((sizeof(PathTraceUnifiedLightRecord) % 16) == 0, "PathTraceUnifiedLightRecord must stay 16-byte aligned for HLSL StructuredBuffer reads");

struct PathTraceUnifiedLightBuild
{
    std::vector<PathTraceUnifiedLightRecord> currentLights;
    std::vector<PathTraceUnifiedLightRecord> previousLights;
    std::vector<uint32_t> currentToPreviousRemap;
};

PathTraceUnifiedLightBuild BuildPathTraceUnifiedLights(
    const std::vector<PathTraceSmokeEmissiveTriangle>& currentEmissiveTriangles,
    const std::vector<PathTraceSmokeEmissiveTriangle>& previousEmissiveTriangles,
    const std::vector<PathTraceEmissiveLightRemap>& emissiveRemap,
    const std::vector<PathTraceDoomAnalyticLightCandidate>& currentAnalyticLights,
    const std::vector<PathTraceDoomAnalyticLightCandidate>& previousAnalyticLights,
    const std::vector<PathTraceDoomAnalyticLightCandidateIdentity>& currentAnalyticIdentities,
    const std::vector<PathTraceDoomAnalyticLightCandidateIdentity>& previousAnalyticIdentities,
    const std::vector<PathTraceDoomAnalyticLightRemap>& analyticRemap,
    float analyticStateCompatibilityTolerance);
