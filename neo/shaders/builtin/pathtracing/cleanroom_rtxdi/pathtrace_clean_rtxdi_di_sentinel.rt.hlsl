#include "../../../vulkan.hlsli"
#include "../PathTracePrimarySurface.hlsli"
#include "Rtxdi/DI/Reservoir.hlsli"

struct PathTraceCleanRtxdiPayload
{
    uint value;
};

struct PathTraceDoomAnalyticLightCandidate
{
    float4 originAndRadius;
    float4 colorAndIntensity;
    float4 doomRadiusAndArea;
    uint flags;
    uint renderLightIndex;
    uint entityNumber;
    uint padding0;
};

struct PathTraceDoomAnalyticLightCandidateIdentity
{
    uint universeIndex;
    uint flags;
    uint invalidReasonFlags;
    uint padding0;
};

VK_IMAGE_FORMAT("rgba32f") RWTexture2D<float4> SmokeOutput : register(u1);
RaytracingAccelerationStructure SmokeScene : register(t0);
StructuredBuffer<PathTraceDoomAnalyticLightCandidate> DoomAnalyticLights : register(t27);
RWStructuredBuffer<PathTracePrimarySurfaceRecord> PrimarySurfaceHistoryCurrent : register(u30);
StructuredBuffer<PathTraceDoomAnalyticLightCandidateIdentity> DoomAnalyticCurrentIdentities : register(t42);
RWStructuredBuffer<RTXDI_PackedDIReservoir> CleanRtxdiDiCurrentReservoirs : register(u69);

#define RTXDI_LIGHT_RESERVOIR_BUFFER CleanRtxdiDiCurrentReservoirs
#include "Rtxdi/DI/ReservoirStorage.hlsli"

cbuffer PathTraceCleanRtxdiDiSentinelConstants : register(b2)
{
    uint CleanRtxdiDiView;
    uint CleanRtxdiDiStatus;
    uint CleanRtxdiDiWidth;
    uint CleanRtxdiDiHeight;
    uint CleanRtxdiDiAnalyticLightCount;
    uint CleanRtxdiDiAnalyticIdentityCount;
    uint CleanRtxdiDiLightMode;
    uint CleanRtxdiDiFrameIndex;
    uint CleanRtxdiDiReservoirCount;
    uint CleanRtxdiDiCandidateCount;
    uint CleanRtxdiDiFlags;
    uint CleanRtxdiDiPadding0;
};

static const uint CLEAN_DOOM_ANALYTIC_IDENTITY_VALID = 1u << 0u;
static const uint CLEAN_DOOM_ANALYTIC_IDENTITY_SAMPLEABLE = 1u << 1u;
static const float CLEAN_RTXDI_PI = 3.14159265358979323846;
static const uint CLEAN_INITIAL_STATUS_VALID = 0u;
static const uint CLEAN_INITIAL_STATUS_NO_LIGHT_MODE = 1u;
static const uint CLEAN_INITIAL_STATUS_DEFERRED_LIGHT_MODE = 2u;
static const uint CLEAN_INITIAL_STATUS_NO_LIGHTS = 3u;
static const uint CLEAN_INITIAL_STATUS_INVALID_SURFACE = 4u;
static const uint CLEAN_INITIAL_STATUS_INVALID_IDENTITY = 5u;
static const uint CLEAN_INITIAL_STATUS_INVALID_PAYLOAD = 6u;
static const uint CLEAN_INITIAL_STATUS_ZERO_TARGET_PDF = 7u;
static const uint CLEAN_INITIAL_STATUS_BAD_SAMPLE_PDF = 8u;

struct PathTraceCleanRtxdiDiInitialResult
{
    RTXDI_DIReservoir reservoir;
    PathTracePrimarySurfaceRecord surface;
    PathTraceDoomAnalyticLightCandidate light;
    uint selectedLightIndex;
    uint status;
    float2 sampleUv;
    float3 samplePosition;
    float3 sampleRadiance;
    float solidAnglePdf;
    float targetPdf;
    float invSourcePdf;
    float visibility;
};

float3 PathTraceCleanRoomSentinelColor(uint2 pixel)
{
    const uint checker = ((pixel.x >> 5u) ^ (pixel.y >> 5u)) & 1u;
    const float3 a = float3(0.95, 0.10, 0.85);
    const float3 b = float3(0.05, 0.85, 0.95);
    return checker != 0u ? a : b;
}

float3 PathTraceCleanRoomPrimarySurfaceStatusColor(uint2 pixel, uint2 dimensions)
{
    const uint width = CleanRtxdiDiWidth != 0u ? CleanRtxdiDiWidth : dimensions.x;
    const uint height = CleanRtxdiDiHeight != 0u ? CleanRtxdiDiHeight : dimensions.y;
    if (width == 0u || height == 0u || pixel.x >= width || pixel.y >= height)
    {
        return float3(0.05, 0.00, 0.00);
    }

    const uint index = pixel.y * width + pixel.x;
    const PathTracePrimarySurfaceRecord record = PrimarySurfaceHistoryCurrent[index];
    if (record.header.x != RT_PATH_TRACE_PRIMARY_SURFACE_RECORD_VERSION ||
        (record.header.y & RT_PRIMARY_SURFACE_VALID) == 0u)
    {
        return float3(0.95, 0.05, 0.10);
    }

    const uint surfaceClass = record.materialAndSurface.w & 0xffu;
    const bool opaqueSurface =
        surfaceClass == 0u ||
        surfaceClass == 1u ||
        surfaceClass == 2u;
    if (!opaqueSurface || record.shadingNormalAndOpacity.w < 0.99)
    {
        return float3(1.00, 0.78, 0.05);
    }

    return float3(0.05, 0.90, 0.18);
}

uint PathTraceCleanRoomHash(uint value)
{
    value ^= value >> 16u;
    value *= 0x7feb352du;
    value ^= value >> 15u;
    value *= 0x846ca68bu;
    value ^= value >> 16u;
    return value;
}

float PathTraceCleanRoomRandom01(uint seed)
{
    return (float)(PathTraceCleanRoomHash(seed) & 0x00ffffffu) * (1.0 / 16777215.0);
}

float3 PathTraceCleanRoomHashColor(uint value)
{
    const uint hashValue = PathTraceCleanRoomHash(value);
    const float3 color = float3(
        (float)((hashValue >> 0u) & 255u),
        (float)((hashValue >> 8u) & 255u),
        (float)((hashValue >> 16u) & 255u)) * (1.0 / 255.0);
    return color * 0.75 + float3(0.18, 0.18, 0.18);
}

float3 PathTraceCleanRoomSafeNormalize(float3 value, float3 fallback)
{
    const float lengthSquared = dot(value, value);
    return lengthSquared > 1.0e-8 ? value * rsqrt(lengthSquared) : fallback;
}

float3 PathTraceCleanRoomPerpendicular(float3 normal)
{
    const float3 axis = abs(normal.z) < 0.999 ? float3(0.0, 0.0, 1.0) : float3(0.0, 1.0, 0.0);
    return PathTraceCleanRoomSafeNormalize(cross(axis, normal), float3(1.0, 0.0, 0.0));
}

float3 PathTraceCleanRoomSampleCone(float3 axis, float cosThetaMax, float2 uv)
{
    const float cosTheta = lerp(1.0, cosThetaMax, saturate(uv.x));
    const float sinTheta = sqrt(max(0.0, 1.0 - cosTheta * cosTheta));
    const float phi = 2.0 * CLEAN_RTXDI_PI * saturate(uv.y);
    const float3 tangent = PathTraceCleanRoomPerpendicular(axis);
    const float3 bitangent = PathTraceCleanRoomSafeNormalize(cross(axis, tangent), float3(0.0, 1.0, 0.0));
    return PathTraceCleanRoomSafeNormalize(axis * cosTheta + tangent * (cos(phi) * sinTheta) + bitangent * (sin(phi) * sinTheta), axis);
}

bool PathTraceCleanRoomLoadSurfaceRecord(uint2 pixel, uint2 dimensions, out PathTracePrimarySurfaceRecord record)
{
    record = (PathTracePrimarySurfaceRecord)0;
    const uint width = CleanRtxdiDiWidth != 0u ? CleanRtxdiDiWidth : dimensions.x;
    const uint height = CleanRtxdiDiHeight != 0u ? CleanRtxdiDiHeight : dimensions.y;
    if (width == 0u || height == 0u || pixel.x >= width || pixel.y >= height)
    {
        return false;
    }

    record = PrimarySurfaceHistoryCurrent[pixel.y * width + pixel.x];
    return record.header.x == RT_PATH_TRACE_PRIMARY_SURFACE_RECORD_VERSION &&
        (record.header.y & RT_PRIMARY_SURFACE_VALID) != 0u;
}

bool PathTraceCleanRoomAnalyticPayloadValid(PathTraceDoomAnalyticLightCandidate light)
{
    const float3 radiance = max(light.colorAndIntensity.rgb, float3(0.0, 0.0, 0.0));
    const float luminance = dot(radiance, float3(0.2126, 0.7152, 0.0722));
    return all(light.originAndRadius.xyz == light.originAndRadius.xyz) &&
        all(abs(light.originAndRadius.xyz) < float3(3.402823e+38, 3.402823e+38, 3.402823e+38)) &&
        all(radiance == radiance) &&
        all(abs(radiance) < float3(3.402823e+38, 3.402823e+38, 3.402823e+38)) &&
        luminance > 0.0 &&
        light.originAndRadius.w > 0.0 &&
        light.originAndRadius.w < 3.402823e+38 &&
        light.doomRadiusAndArea.x > 0.0 &&
        light.doomRadiusAndArea.x < 3.402823e+38;
}

float PathTraceCleanRoomAnalyticTargetPdf(PathTracePrimarySurfaceRecord surface, PathTraceDoomAnalyticLightCandidate light)
{
    const float3 toLight = light.originAndRadius.xyz - surface.worldPositionAndViewDepth.xyz;
    const float distanceSquared = max(dot(toLight, toLight), 1.0e-6);
    const float3 lightDirection = toLight * rsqrt(distanceSquared);
    const float3 normal = normalize(surface.shadingNormalAndOpacity.xyz);
    const float ndotl = saturate(dot(normal, lightDirection));
    const float3 radiance = max(light.colorAndIntensity.rgb, float3(0.0, 0.0, 0.0));
    return dot(radiance * (ndotl * (1.0 / CLEAN_RTXDI_PI)), float3(0.2126, 0.7152, 0.0722));
}

float PathTraceCleanRoomAnalyticTargetPdfFromSample(PathTracePrimarySurfaceRecord surface, float3 samplePosition, float3 sampleRadiance)
{
    const float3 toSample = samplePosition - surface.worldPositionAndViewDepth.xyz;
    const float distanceSquared = max(dot(toSample, toSample), 1.0e-6);
    const float3 lightDirection = toSample * rsqrt(distanceSquared);
    const float3 normal = PathTraceCleanRoomSafeNormalize(surface.shadingNormalAndOpacity.xyz, surface.geometricNormalAndRoughness.xyz);
    const float ndotl = saturate(dot(normal, lightDirection));
    return dot(max(sampleRadiance, float3(0.0, 0.0, 0.0)) * (ndotl * (1.0 / CLEAN_RTXDI_PI)), float3(0.2126, 0.7152, 0.0722));
}

float PathTraceCleanRoomSphereHitT(float3 origin, float3 direction, float3 center, float radius, float fallbackT)
{
    const float3 originToCenter = origin - center;
    const float halfB = dot(originToCenter, direction);
    const float c = dot(originToCenter, originToCenter) - radius * radius;
    const float discriminant = halfB * halfB - c;
    if (discriminant <= 0.0)
    {
        return fallbackT;
    }

    const float t = -halfB - sqrt(discriminant);
    return t > 0.01 ? t : fallbackT;
}

float PathTraceCleanRoomDoomLightInfluence(float centerDistance, PathTraceDoomAnalyticLightCandidate light)
{
    const float influenceRadius = max(light.doomRadiusAndArea.x, light.originAndRadius.w);
    if (influenceRadius <= 0.0 || centerDistance >= influenceRadius)
    {
        return 0.0;
    }

    const float normalizedDistance = saturate(centerDistance / influenceRadius);
    return saturate(1.0 - normalizedDistance * normalizedDistance);
}

float PathTraceCleanRoomTraceVisibility(PathTracePrimarySurfaceRecord surface, float3 samplePosition)
{
    const float3 surfacePosition = surface.worldPositionAndViewDepth.xyz;
    const float3 toSample = samplePosition - surfacePosition;
    const float distanceSquared = dot(toSample, toSample);
    if (distanceSquared <= 1.0e-6)
    {
        return 0.0;
    }

    const float distance = sqrt(distanceSquared);
    const float3 direction = toSample / distance;
    const float3 shadingNormal = PathTraceCleanRoomSafeNormalize(surface.shadingNormalAndOpacity.xyz, surface.geometricNormalAndRoughness.xyz);
    const float3 geometricNormal = PathTraceCleanRoomSafeNormalize(surface.geometricNormalAndRoughness.xyz, shadingNormal);
    if (dot(shadingNormal, direction) <= 0.0 || dot(geometricNormal, direction) <= 0.0)
    {
        return 0.0;
    }

    const float normalSign = dot(geometricNormal, shadingNormal) >= 0.0 ? 1.0 : -1.0;
    RayDesc shadowRay;
    shadowRay.Origin = surfacePosition + geometricNormal * (normalSign * 0.75) + direction * 0.25;
    shadowRay.Direction = direction;
    shadowRay.TMin = 0.01;
    shadowRay.TMax = max(distance - 0.5, 0.01);

    PathTraceCleanRtxdiPayload shadowPayload;
    shadowPayload.value = 0u;
    TraceRay(
        SmokeScene,
        RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH,
        0xff,
        1,
        1,
        1,
        shadowRay,
        shadowPayload);

    return shadowPayload.value == 0u ? 1.0 : 0.0;
}

uint PathTraceCleanRoomReservoirIndex(uint2 pixel, uint2 dimensions)
{
    const uint width = CleanRtxdiDiWidth != 0u ? CleanRtxdiDiWidth : dimensions.x;
    return pixel.y * width + pixel.x;
}

float3 PathTraceCleanRoomStatusColor(uint status)
{
    if (status == CLEAN_INITIAL_STATUS_NO_LIGHT_MODE)
    {
        return float3(1.0, 0.0, 1.0);
    }
    if (status == CLEAN_INITIAL_STATUS_DEFERRED_LIGHT_MODE)
    {
        return float3(1.0, 0.35, 0.0);
    }
    if (status == CLEAN_INITIAL_STATUS_NO_LIGHTS)
    {
        return float3(0.95, 0.0, 0.12);
    }
    if (status == CLEAN_INITIAL_STATUS_INVALID_SURFACE)
    {
        return float3(0.18, 0.0, 0.22);
    }
    if (status == CLEAN_INITIAL_STATUS_INVALID_IDENTITY)
    {
        return float3(1.0, 0.82, 0.0);
    }
    if (status == CLEAN_INITIAL_STATUS_INVALID_PAYLOAD)
    {
        return float3(1.0, 0.18, 0.0);
    }
    if (status == CLEAN_INITIAL_STATUS_ZERO_TARGET_PDF)
    {
        return float3(0.08, 0.10, 1.0);
    }
    if (status == CLEAN_INITIAL_STATUS_BAD_SAMPLE_PDF)
    {
        return float3(0.0, 0.95, 1.0);
    }
    return float3(0.05, 0.90, 0.18);
}

PathTraceCleanRtxdiDiInitialResult PathTraceCleanRoomRunInitialProducer(uint2 pixel, uint2 dimensions)
{
    PathTraceCleanRtxdiDiInitialResult result = (PathTraceCleanRtxdiDiInitialResult)0;
    result.reservoir = RTXDI_EmptyDIReservoir();
    result.selectedLightIndex = 0xffffffffu;
    result.status = CLEAN_INITIAL_STATUS_VALID;

    if (CleanRtxdiDiLightMode == 0u)
    {
        result.status = CLEAN_INITIAL_STATUS_NO_LIGHT_MODE;
        return result;
    }
    if (CleanRtxdiDiLightMode != 1u)
    {
        result.status = CLEAN_INITIAL_STATUS_DEFERRED_LIGHT_MODE;
        return result;
    }

    const uint lightCount = min(CleanRtxdiDiAnalyticLightCount, CleanRtxdiDiAnalyticIdentityCount);
    if (lightCount == 0u)
    {
        result.status = CLEAN_INITIAL_STATUS_NO_LIGHTS;
        return result;
    }

    if (!PathTraceCleanRoomLoadSurfaceRecord(pixel, dimensions, result.surface))
    {
        result.status = CLEAN_INITIAL_STATUS_INVALID_SURFACE;
        return result;
    }

    const uint seed = pixel.x * 1973u ^ pixel.y * 9277u ^ CleanRtxdiDiFrameIndex * 26699u ^ 0x4d534449u;
    const uint selectedIndex = PathTraceCleanRoomHash(seed) % lightCount;
    result.selectedLightIndex = selectedIndex;
    const PathTraceDoomAnalyticLightCandidateIdentity identity = DoomAnalyticCurrentIdentities[selectedIndex];
    const uint identityRequiredFlags = CLEAN_DOOM_ANALYTIC_IDENTITY_VALID | CLEAN_DOOM_ANALYTIC_IDENTITY_SAMPLEABLE;
    if ((identity.flags & identityRequiredFlags) != identityRequiredFlags)
    {
        result.status = CLEAN_INITIAL_STATUS_INVALID_IDENTITY;
        return result;
    }

    const PathTraceDoomAnalyticLightCandidate light = DoomAnalyticLights[selectedIndex];
    result.light = light;
    if (!PathTraceCleanRoomAnalyticPayloadValid(light))
    {
        result.status = CLEAN_INITIAL_STATUS_INVALID_PAYLOAD;
        return result;
    }

    result.sampleUv = float2(
        PathTraceCleanRoomRandom01(seed ^ 0x68bc21ebu),
        PathTraceCleanRoomRandom01(seed ^ 0x02e5be93u));
    const float3 toCenter = light.originAndRadius.xyz - result.surface.worldPositionAndViewDepth.xyz;
    const float centerDistance = sqrt(max(dot(toCenter, toCenter), 1.0e-6));
    const float3 centerDirection = toCenter / centerDistance;
    const float sphereRadius = clamp(light.originAndRadius.w, 0.01, max(light.doomRadiusAndArea.x, 0.01));
    const float sinThetaMax = saturate(sphereRadius / centerDistance);
    const float cosThetaMax = sqrt(max(0.0, 1.0 - sinThetaMax * sinThetaMax));
    const float solidAngle = max(2.0 * CLEAN_RTXDI_PI * (1.0 - cosThetaMax), 1.0e-5);
    const float3 sampleDirection = PathTraceCleanRoomSampleCone(centerDirection, cosThetaMax, result.sampleUv);
    const float3 normal = PathTraceCleanRoomSafeNormalize(result.surface.shadingNormalAndOpacity.xyz, result.surface.geometricNormalAndRoughness.xyz);
    if (saturate(dot(normal, sampleDirection)) <= 0.0)
    {
        result.status = CLEAN_INITIAL_STATUS_ZERO_TARGET_PDF;
        return result;
    }

    const float influence = PathTraceCleanRoomDoomLightInfluence(centerDistance, light);
    result.samplePosition = result.surface.worldPositionAndViewDepth.xyz + sampleDirection * PathTraceCleanRoomSphereHitT(result.surface.worldPositionAndViewDepth.xyz, sampleDirection, light.originAndRadius.xyz, sphereRadius, centerDistance);
    result.sampleRadiance = max(light.colorAndIntensity.rgb, float3(0.0, 0.0, 0.0)) * influence;
    result.solidAnglePdf = 1.0 / solidAngle;
    result.targetPdf = PathTraceCleanRoomAnalyticTargetPdfFromSample(result.surface, result.samplePosition, result.sampleRadiance);
    if (result.targetPdf <= 1.0e-8 || !(result.targetPdf == result.targetPdf))
    {
        result.status = CLEAN_INITIAL_STATUS_ZERO_TARGET_PDF;
        return result;
    }
    if (result.solidAnglePdf <= 1.0e-8 || !(result.solidAnglePdf == result.solidAnglePdf))
    {
        result.status = CLEAN_INITIAL_STATUS_BAD_SAMPLE_PDF;
        return result;
    }

    result.invSourcePdf = (float)lightCount / result.solidAnglePdf;
    RTXDI_StreamSample(
        result.reservoir,
        selectedIndex,
        result.sampleUv,
        PathTraceCleanRoomRandom01(seed ^ 0x9e3779b9u),
        result.targetPdf,
        result.invSourcePdf);
    RTXDI_FinalizeResampling(result.reservoir, 1.0, max(result.reservoir.M, 1.0));
    result.status = RTXDI_IsValidDIReservoir(result.reservoir) ? CLEAN_INITIAL_STATUS_VALID : CLEAN_INITIAL_STATUS_ZERO_TARGET_PDF;
    if (result.status == CLEAN_INITIAL_STATUS_VALID)
    {
        result.visibility = PathTraceCleanRoomTraceVisibility(result.surface, result.samplePosition);
        RTXDI_StoreVisibilityInDIReservoir(result.reservoir, result.visibility.xxx, false);
    }
    return result;
}

float3 PathTraceCleanRoomFlatDiffuseResolve(PathTraceCleanRtxdiDiInitialResult result)
{
    if (result.status == CLEAN_INITIAL_STATUS_ZERO_TARGET_PDF)
    {
        return float3(0.0, 0.0, 0.0);
    }
    if (result.status != CLEAN_INITIAL_STATUS_VALID)
    {
        return PathTraceCleanRoomStatusColor(result.status);
    }

    const float3 normal = PathTraceCleanRoomSafeNormalize(result.surface.shadingNormalAndOpacity.xyz, result.surface.geometricNormalAndRoughness.xyz);
    const float3 toSample = result.samplePosition - result.surface.worldPositionAndViewDepth.xyz;
    const float3 lightDirection = PathTraceCleanRoomSafeNormalize(toSample, normal);
    const float ndotl = saturate(dot(normal, lightDirection));
    const float3 flatDiffuse = float3(0.5, 0.5, 0.5) * (1.0 / CLEAN_RTXDI_PI);
    return max(result.sampleRadiance, float3(0.0, 0.0, 0.0)) * flatDiffuse * ndotl * max(RTXDI_GetDIReservoirInvPdf(result.reservoir), 0.0) * result.visibility;
}

float3 PathTraceCleanRoomReservoirIdentityColor(PathTraceCleanRtxdiDiInitialResult result, uint2 pixel)
{
    if (result.status != CLEAN_INITIAL_STATUS_VALID)
    {
        return PathTraceCleanRoomStatusColor(result.status);
    }
    const float3 identityColor = PathTraceCleanRoomHashColor(result.selectedLightIndex ^ (CleanRtxdiDiFrameIndex * 747796405u));
    const uint band = (pixel.y / 64u) % 3u;
    if (band == 1u)
    {
        return identityColor;
    }
    if (band == 2u)
    {
        return result.visibility > 0.0 ? float3(saturate(result.reservoir.M / 4.0), 0.95, 0.25) : float3(0.95, 0.05, 0.12);
    }
    return lerp(float3(0.0, 0.85, 0.18), identityColor, 0.60);
}

float3 PathTraceCleanRoomReservoirWeightColor(PathTraceCleanRtxdiDiInitialResult result, uint2 pixel)
{
    if (result.status != CLEAN_INITIAL_STATUS_VALID)
    {
        return PathTraceCleanRoomStatusColor(result.status);
    }
    const uint band = (pixel.y / 64u) % 3u;
    if (band == 0u)
    {
        return float3(saturate(log2(1.0 + result.targetPdf) / 8.0), 0.15, 0.85);
    }
    if (band == 1u)
    {
        return float3(0.15, saturate(log2(1.0 + RTXDI_GetDIReservoirInvPdf(result.reservoir)) / 12.0), 0.85);
    }
    return result.visibility > 0.0 ? float3(0.0, 0.95, 0.72) : float3(0.95, 0.05, 0.12);
}

float3 PathTraceCleanRoomInitialReservoirOutput(uint2 pixel, uint2 dimensions, uint view)
{
    const uint reservoirIndex = PathTraceCleanRoomReservoirIndex(pixel, dimensions);
    PathTraceCleanRtxdiDiInitialResult result = PathTraceCleanRoomRunInitialProducer(pixel, dimensions);
    if (reservoirIndex < CleanRtxdiDiReservoirCount)
    {
        CleanRtxdiDiCurrentReservoirs[reservoirIndex] = RTXDI_PackDIReservoir(result.reservoir);
    }

    if (view == 4u)
    {
        return PathTraceCleanRoomFlatDiffuseResolve(result);
    }
    if (view == 7u)
    {
        return PathTraceCleanRoomReservoirIdentityColor(result, pixel);
    }
    if (view == 8u)
    {
        return PathTraceCleanRoomReservoirWeightColor(result, pixel);
    }
    return PathTraceCleanRoomStatusColor(result.status);
}

float3 PathTraceCleanRoomAnalyticLightStatusColor(uint2 pixel, uint2 dimensions)
{
    if (CleanRtxdiDiLightMode == 0u)
    {
        return float3(1.0, 0.0, 1.0);
    }
    if (CleanRtxdiDiLightMode != 1u)
    {
        return float3(1.0, 0.35, 0.0);
    }

    const uint lightCount = min(CleanRtxdiDiAnalyticLightCount, CleanRtxdiDiAnalyticIdentityCount);
    if (lightCount == 0u)
    {
        return float3(0.95, 0.0, 0.12);
    }

    const uint selectedIndex = PathTraceCleanRoomHash(pixel.x + pixel.y * 4099u) % lightCount;
    const PathTraceDoomAnalyticLightCandidateIdentity identity = DoomAnalyticCurrentIdentities[selectedIndex];
    const float3 identityColor = PathTraceCleanRoomHashColor(identity.universeIndex ^ (selectedIndex * 1664525u));
    const uint identityRequiredFlags = CLEAN_DOOM_ANALYTIC_IDENTITY_VALID | CLEAN_DOOM_ANALYTIC_IDENTITY_SAMPLEABLE;
    if ((identity.flags & identityRequiredFlags) != identityRequiredFlags)
    {
        return lerp(float3(1.0, 0.82, 0.0), identityColor, 0.25);
    }

    const PathTraceDoomAnalyticLightCandidate light = DoomAnalyticLights[selectedIndex];
    if (!PathTraceCleanRoomAnalyticPayloadValid(light))
    {
        return lerp(float3(1.0, 0.18, 0.0), identityColor, 0.25);
    }

    PathTracePrimarySurfaceRecord surface;
    if (!PathTraceCleanRoomLoadSurfaceRecord(pixel, dimensions, surface))
    {
        return float3(0.18, 0.0, 0.22);
    }

    const float targetPdf = PathTraceCleanRoomAnalyticTargetPdf(surface, light);
    if (targetPdf <= 1.0e-8 || !(targetPdf == targetPdf))
    {
        return lerp(float3(0.08, 0.10, 1.0), identityColor, 0.20);
    }

    const uint band = (pixel.y / 64u) % 3u;
    if (band == 1u)
    {
        return identityColor;
    }
    if (band == 2u)
    {
        return float3(0.0, 0.95, 0.72);
    }
    return lerp(float3(0.0, 0.85, 0.18), identityColor, 0.55);
}

float3 PathTraceCleanRoomDeferredColor(uint2 pixel, uint view)
{
    const uint stripe = ((pixel.x >> 5u) + (pixel.y >> 4u) + view) & 1u;
    const uint band = ((pixel.y / 48u) + view) % 3u;
    float3 a = float3(0.35, 0.35, 0.35);
    float3 b = float3(0.75, 0.75, 0.75);

    if (view == 2u)
    {
        a = float3(0.55, 0.05, 0.10);
        b = float3(1.00, 0.25, 0.30);
    }
    else if (view == 3u)
    {
        a = float3(0.55, 0.32, 0.00);
        b = float3(1.00, 0.75, 0.05);
    }
    else if (view == 4u)
    {
        a = float3(0.05, 0.20, 0.65);
        b = float3(0.25, 0.55, 1.00);
    }
    else if (view == 5u)
    {
        a = float3(0.05, 0.45, 0.18);
        b = float3(0.30, 1.00, 0.48);
    }
    else if (view == 6u)
    {
        a = float3(0.35, 0.08, 0.60);
        b = float3(0.85, 0.35, 1.00);
    }
    else if (view == 7u)
    {
        a = float3(0.08, 0.48, 0.50);
        b = float3(0.35, 1.00, 0.95);
    }
    else if (view == 8u)
    {
        a = float3(0.55, 0.18, 0.02);
        b = float3(1.00, 0.48, 0.12);
    }

    float3 color = stripe != 0u ? a : b;
    if (band == 1u)
    {
        color *= 0.55;
    }
    else if (band == 2u)
    {
        color = color * 0.75 + float3(0.08, 0.08, 0.08);
    }
    return color;
}

[shader("raygeneration")]
void RayGen()
{
    const uint2 pixel = DispatchRaysIndex().xy;
    const uint2 dimensions = DispatchRaysDimensions().xy;
    if (pixel.x >= dimensions.x || pixel.y >= dimensions.y)
    {
        return;
    }

    const uint view = clamp(CleanRtxdiDiView, 1u, 8u);
    float3 color = PathTraceCleanRoomDeferredColor(pixel, view);
    if (view == 1u)
    {
        color = PathTraceCleanRoomSentinelColor(pixel);
    }
    else if (view == 2u)
    {
        color = PathTraceCleanRoomPrimarySurfaceStatusColor(pixel, dimensions);
    }
    else if (view == 3u)
    {
        color = PathTraceCleanRoomAnalyticLightStatusColor(pixel, dimensions);
    }
    else if (view == 4u || view == 7u || view == 8u)
    {
        color = PathTraceCleanRoomInitialReservoirOutput(pixel, dimensions, view);
    }
    SmokeOutput[pixel] = float4(color, 1.0);
}

[shader("miss")]
void Miss(inout PathTraceCleanRtxdiPayload payload)
{
    payload.value = 0u;
}

[shader("miss")]
void ShadowMiss(inout PathTraceCleanRtxdiPayload payload)
{
    payload.value = 0u;
}

[shader("anyhit")]
void AnyHit(inout PathTraceCleanRtxdiPayload payload, BuiltInTriangleIntersectionAttributes attributes)
{
}

[shader("anyhit")]
void ShadowAnyHit(inout PathTraceCleanRtxdiPayload payload, BuiltInTriangleIntersectionAttributes attributes)
{
}

[shader("closesthit")]
void ClosestHit(inout PathTraceCleanRtxdiPayload payload, BuiltInTriangleIntersectionAttributes attributes)
{
    payload.value = 1u;
}

[shader("closesthit")]
void ShadowClosestHit(inout PathTraceCleanRtxdiPayload payload, BuiltInTriangleIntersectionAttributes attributes)
{
    payload.value = 1u;
}
