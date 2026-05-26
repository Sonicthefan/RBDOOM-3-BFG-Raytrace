// Purpose:
//     Supplies rbdoom data for RTXDI/RAB light identity and remap predicates.
//
// NVIDIA reference:
//     E:\prog\references\RTXDI-main\Samples\FullSample\Shaders\LightingPasses\RtxdiApplicationBridge\RAB_LightInfo.hlsli
//     E:\prog\references\RTXDI-main\Samples\FullSample\Shaders\LightingPasses\RtxdiApplicationBridge\RAB_Buffers.hlsli
//     E:\prog\references\RTXDI-main\Libraries\Rtxdi\Include\Rtxdi\PT\HybridShift.hlsli
//
// Current rbdoom supplier:
//     Doom analytic current/previous identity buffers, Doom analytic remap
//     records, smoke emissive remap records, and MotionVectorInfo.w tolerance.
//
// Current deviation:
//     rbdoom adapts local emissive and Doom analytic domains into RTXDI's
//     cross-frame light identity contract.
//
// Extraction rule:
//     This file must preserve existing behavior until a later correction task.

#ifndef RB_PATH_TRACING_RAB_LIGHT_IDENTITY_REMAP_HLSLI
#define RB_PATH_TRACING_RAB_LIGHT_IDENTITY_REMAP_HLSLI

static const uint RAB_DOOM_ANALYTIC_IDENTITY_VALID = 0x00000001u;
static const uint RAB_DOOM_ANALYTIC_IDENTITY_SAMPLEABLE = 0x00000002u;
static const uint RAB_DOOM_ANALYTIC_IDENTITY_REMAP_VALID = 0x00000004u;
static const uint RAB_DOOM_ANALYTIC_INVALID_INDEX = 0xffffffffu;
static const uint RAB_SMOKE_EMISSIVE_REMAP_VALID = 0x00000001u;
static const uint RAB_SMOKE_EMISSIVE_REMAP_CURRENT_ZERO_IDENTITY = 0x00000010u;
static const uint RAB_SMOKE_EMISSIVE_REMAP_PREVIOUS_ZERO_IDENTITY = 0x00000020u;
static const uint RAB_SMOKE_EMISSIVE_REMAP_CURRENT_DUPLICATE = 0x00000040u;
static const uint RAB_SMOKE_EMISSIVE_REMAP_PREVIOUS_DUPLICATE = 0x00000080u;
static const uint RAB_SMOKE_EMISSIVE_REMAP_CURRENT_MISSING = 0x00000100u;
static const uint RAB_SMOKE_EMISSIVE_REMAP_PREVIOUS_MISSING = 0x00000200u;
static const uint RAB_SMOKE_EMISSIVE_REMAP_INCOMPATIBLE = 0x00000400u;

bool RAB_DoomAnalyticIdentityValid(PathTraceDoomAnalyticLightCandidateIdentity identity)
{
    return identity.universeIndex != RAB_DOOM_ANALYTIC_INVALID_INDEX &&
        (identity.flags & RAB_DOOM_ANALYTIC_IDENTITY_VALID) != 0u;
}

uint RAB_DoomAnalyticIdentityRemapIndex(PathTraceDoomAnalyticLightCandidateIdentity identity)
{
    return identity.padding0;
}

bool RAB_DoomAnalyticIdentitySampleable(PathTraceDoomAnalyticLightCandidateIdentity identity)
{
    return RAB_DoomAnalyticIdentityValid(identity) &&
        (identity.flags & RAB_DOOM_ANALYTIC_IDENTITY_SAMPLEABLE) != 0u;
}

bool RAB_DoomAnalyticRemapValid(PathTraceDoomAnalyticLightRemap remap)
{
    return (remap.flags & RAB_DOOM_ANALYTIC_IDENTITY_REMAP_VALID) != 0u;
}

bool RAB_SmokeEmissiveRemapValid(PathTraceEmissiveLightRemap remap)
{
    return (remap.flags & RAB_SMOKE_EMISSIVE_REMAP_VALID) != 0u;
}

float RAB_DoomAnalyticLightColorMagnitude(PathTraceDoomAnalyticLightCandidate light)
{
    const float3 color = max(light.colorAndIntensity.rgb, float3(0.0, 0.0, 0.0));
    return max(max(color.r, color.g), color.b);
}

bool RAB_DoomAnalyticLightStateCompatible(
    PathTraceDoomAnalyticLightCandidate currentLight,
    PathTraceDoomAnalyticLightCandidate previousLight)
{
    const float tolerance = clamp(MotionVectorInfo.w, 0.0, 1.0);
    const float3 currentColor = max(currentLight.colorAndIntensity.rgb, float3(0.0, 0.0, 0.0));
    const float3 previousColor = max(previousLight.colorAndIntensity.rgb, float3(0.0, 0.0, 0.0));
    const float colorScale = max(max(RAB_DoomAnalyticLightColorMagnitude(currentLight), RAB_DoomAnalyticLightColorMagnitude(previousLight)), 1.0e-4);
    if (any(abs(currentColor - previousColor) > colorScale * tolerance + 1.0e-4))
    {
        return false;
    }

    const float currentIntensity = max(currentLight.colorAndIntensity.w, 0.0);
    const float previousIntensity = max(previousLight.colorAndIntensity.w, 0.0);
    const float intensityScale = max(max(currentIntensity, previousIntensity), 1.0e-4);
    if (abs(currentIntensity - previousIntensity) > intensityScale * tolerance + 1.0e-4)
    {
        return false;
    }

    const float currentInfluenceRadius = max(currentLight.doomRadiusAndArea.x, 1.0);
    const float previousInfluenceRadius = max(previousLight.doomRadiusAndArea.x, 1.0);
    const float positionTolerance = max(max(currentInfluenceRadius, previousInfluenceRadius) * 0.005, 0.5);
    if (length(currentLight.originAndRadius.xyz - previousLight.originAndRadius.xyz) > positionTolerance)
    {
        return false;
    }

    const float radiusScale = max(max(abs(currentLight.originAndRadius.w), abs(previousLight.originAndRadius.w)), 1.0e-4);
    if (abs(currentLight.originAndRadius.w - previousLight.originAndRadius.w) > radiusScale * tolerance + 1.0e-4)
    {
        return false;
    }

    return true;
}

#endif
