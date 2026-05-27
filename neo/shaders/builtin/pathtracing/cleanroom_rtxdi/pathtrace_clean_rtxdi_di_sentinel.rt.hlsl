#include "../../../vulkan.hlsli"
#include "../PathTracePrimarySurface.hlsli"
#ifdef RTXDI_ENABLE_PRESAMPLING
#undef RTXDI_ENABLE_PRESAMPLING
#endif
#define RTXDI_ENABLE_PRESAMPLING 0
#include "Rtxdi/DI/Reservoir.hlsli"
#include "Rtxdi/RtxdiParameters.h"
#include "Rtxdi/DI/ReSTIRDIParameters.h"
#include "Rtxdi/Utils/RandomSamplerState.hlsli"
#include "Rtxdi/Utils/Math.hlsli"

#ifndef RTXDI_ALLOWED_BIAS_CORRECTION
#define RTXDI_ALLOWED_BIAS_CORRECTION RTXDI_BIAS_CORRECTION_BASIC
#endif

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

struct PathTraceDoomAnalyticLightRemap
{
    int previousToCurrentCandidateIndex;
    int currentToPreviousCandidateIndex;
    uint flags;
    uint invalidReasonFlags;
};

VK_IMAGE_FORMAT("rgba32f") RWTexture2D<float4> SmokeOutput : register(u1);
VK_IMAGE_FORMAT("rgba16f") RWTexture2D<float4> PathTraceMotionVectors : register(u39);
VK_IMAGE_FORMAT("r32ui") RWTexture2D<uint> PathTraceMotionVectorMask : register(u40);
RaytracingAccelerationStructure SmokeScene : register(t0);
StructuredBuffer<PathTraceDoomAnalyticLightCandidate> DoomAnalyticLights : register(t27);
RWStructuredBuffer<PathTracePrimarySurfaceRecord> PrimarySurfaceHistoryCurrent : register(u30);
RWStructuredBuffer<PathTracePrimarySurfaceRecord> PrimarySurfaceHistoryPrevious : register(u31);
StructuredBuffer<PathTraceDoomAnalyticLightCandidateIdentity> DoomAnalyticCurrentIdentities : register(t42);
StructuredBuffer<PathTraceDoomAnalyticLightCandidateIdentity> DoomAnalyticPreviousIdentities : register(t43);
StructuredBuffer<PathTraceDoomAnalyticLightRemap> DoomAnalyticRemap : register(t44);
StructuredBuffer<PathTraceDoomAnalyticLightCandidate> DoomAnalyticPreviousLights : register(t45);
RWStructuredBuffer<RTXDI_PackedDIReservoir> CleanRtxdiDiCurrentReservoirs : register(u69);
RWStructuredBuffer<RTXDI_PackedDIReservoir> CleanRtxdiDiTemporalReservoirs : register(u70);
RWStructuredBuffer<RTXDI_PackedDIReservoir> CleanRtxdiDiPreviousReservoirs : register(u71);

#define RTXDI_LIGHT_RESERVOIR_BUFFER CleanRtxdiDiPreviousReservoirs
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
    uint CleanRtxdiDiPreviousAnalyticLightCount;
    uint CleanRtxdiDiPreviousAnalyticIdentityCount;
    uint CleanRtxdiDiAnalyticRemapCount;
    uint CleanRtxdiDiTemporalFlags;
    uint CleanRtxdiDiHistoryResetCount;
    uint CleanRtxdiDiView8Band;
    uint CleanRtxdiDiResolveVisibilityReuse;
    uint CleanRtxdiDiResolveBrdfTarget;
    uint CleanRtxdiDiReferenceRab;
    uint CleanRtxdiDiRluCurrentLightCount;
    uint CleanRtxdiDiRluPreviousLightCount;
    uint CleanRtxdiDiRluCurrentToPreviousCount;
    uint CleanRtxdiDiRluPreviousToCurrentCount;
    float4 CleanRtxdiDiPrevCameraOriginAndValid;
    float4 CleanRtxdiDiPrevCameraForwardAndTanX;
    float4 CleanRtxdiDiPrevCameraLeftAndTanY;
    float4 CleanRtxdiDiPrevCameraUpAndTanY;
    float4 CleanRtxdiDiCameraOriginAndValid;
    float4 CleanRtxdiDiCameraForwardAndTanX;
    float4 CleanRtxdiDiCameraLeftAndTanY;
    float4 CleanRtxdiDiCameraUpAndTanY;
    float4 CleanRtxdiDiDoomAnalyticLightInfo;
    float4 CleanRtxdiDiMotionVectorInfo;
    float4 CleanRtxdiDiRestirPTSurfaceInfo;
};

#define DoomAnalyticLightInfo CleanRtxdiDiDoomAnalyticLightInfo
#define MotionVectorInfo CleanRtxdiDiMotionVectorInfo
#define RestirPTSurfaceInfo CleanRtxdiDiRestirPTSurfaceInfo
static const float4 TextureInfo = float4(0.0, 0.0, 0.0, 0.0);
static const float4 ToyPathInfo = float4(0.0, 0.0, 1.0, 0.0);
static const uint RT_SMOKE_EMISSIVE_TRIANGLE_HISTORY_DYNAMIC = 0u;
static const uint RT_SMOKE_TEXTURE_FLAG_RESERVOIR_TWO_SIDED_EMISSIVES = 0u;
bool RAB_RestirLightManagerRABEnabled()
{
    return false;
}
bool RAB_UnifiedLightSampleEnabled()
{
    return false;
}
static const uint CLEAN_RAB_DIAGNOSTIC_RELAX_BRDF_GATES = 1u << 8u;
static const uint CLEAN_RAB_DIAGNOSTIC_DOOM_TARGET_FLOOR = 1u << 9u;
#define RB_RAB_LIGHT_SAMPLING_CORE_ONLY 1
#define RB_RAB_CLEAN_DIAGNOSTIC_RELAX_BRDF_GATES 1
#define RB_RAB_CLEAN_REFERENCE_DOOM_ANALYTIC 1 // clean reference-RAB identity radiance modes live in the included helper
#include "../RtxdiBridge/RAB_UnifiedLightRecord.hlsli"
#include "../RtxdiBridge/RAB_LightTarget.hlsli"

StructuredBuffer<uint> CleanRtxdiDiRluCurrentToPrevious : register(t64);
StructuredBuffer<uint> CleanRtxdiDiRluPreviousToCurrent : register(t65);
StructuredBuffer<PathTraceUnifiedLightRecord> CleanRtxdiDiRluCurrentLights : register(t66);
StructuredBuffer<PathTraceUnifiedLightRecord> CleanRtxdiDiRluPreviousLights : register(t67);

static const uint CLEAN_DOOM_ANALYTIC_IDENTITY_VALID = 1u << 0u;
static const uint CLEAN_DOOM_ANALYTIC_IDENTITY_SAMPLEABLE = 1u << 1u;
static const uint CLEAN_DOOM_ANALYTIC_IDENTITY_REMAP_VALID = 1u << 2u;
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
static const uint CLEAN_INITIAL_STATUS_EXTERNAL_CURRENT_EMPTY = 9u;
static const uint CLEAN_INITIAL_STATUS_EXTERNAL_UNSUPPORTED_LIGHT = 10u;
static const uint CLEAN_FLAG_EXTERNAL_PDFNEE_CURRENT = 1u << 0u;
static const uint CLEAN_FLAG_REMIX_LIGHT_UNIVERSE = 1u << 10u;
static const uint CLEAN_TEMPORAL_FLAG_ENABLE = 1u << 0u;
static const uint CLEAN_TEMPORAL_FLAG_PREVIOUS_VALID = 1u << 1u;
static const uint CLEAN_TEMPORAL_DIAG_CURRENT_VALID = 1u << 0u;
static const uint CLEAN_TEMPORAL_DIAG_ENABLED = 1u << 1u;
static const uint CLEAN_TEMPORAL_DIAG_PREVIOUS_FRAME_VALID = 1u << 2u;
static const uint CLEAN_TEMPORAL_DIAG_CURRENT_SURFACE_VALID = 1u << 3u;
static const uint CLEAN_TEMPORAL_DIAG_MOTION_VALID = 1u << 4u;
static const uint CLEAN_TEMPORAL_DIAG_PREVIOUS_SURFACE_VALID = 1u << 5u;
static const uint CLEAN_TEMPORAL_DIAG_PREVIOUS_RESERVOIR_VALID = 1u << 6u;
static const uint CLEAN_TEMPORAL_DIAG_PREVIOUS_LIGHT_MAPPED = 1u << 7u;
static const uint CLEAN_TEMPORAL_DIAG_TEMPORAL_RESERVOIR_VALID = 1u << 8u;
static const uint CLEAN_TEMPORAL_DIAG_SDK_REUSED_PREVIOUS = 1u << 9u;
static const uint CLEAN_TEMPORAL_DIAG_CURRENT_CANDIDATE = 1u << 10u;
static const uint CLEAN_TEMPORAL_DIAG_CAMERA_REPROJECTED = 1u << 11u;
static const uint CLEAN_TEMPORAL_DIAG_SDK_CALLED = 1u << 12u;
static const uint CLEAN_TEMPORAL_DIAG_PREVIOUS_TARGET_AT_CURRENT = 1u << 13u;
static const uint CLEAN_TEMPORAL_DIAG_SDK_SELECTED_PREVIOUS_SAMPLE = 1u << 14u;
static const uint CLEAN_TEMPORAL_DIAG_TEMPORAL_OUTPUT_CHANGED = 1u << 15u;
static const uint PT_MOTION_VECTOR_MASK_VALID = 0x00000001u;
static const uint CLEAN_RAB_LIGHT_TYPE_DOOM_ANALYTIC_SPHERE = 1u;
static const uint CLEAN_RAB_LIGHT_TYPE_SYNTHETIC_CONSTANT = 2u;
static const float3 CLEAN_SYNTHETIC_CONSTANT_RADIANCE = float3(2.0, 2.0, 2.0);
static const uint CLEAN_SYNTHETIC_OVERLAP_LIGHT_COUNT = 4u;

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

struct PathTraceCleanRtxdiDiTemporalResult
{
    RTXDI_DIReservoir reservoir;
    uint flags;
    int2 previousPixel;
    int2 temporalSamplePixel;
    float previousM;
    float previousTargetPdf;
    float previousTargetAtCurrentPdf;
    float previousInvPdf;
    float temporalTargetPdf;
    float temporalInvPdf;
};

bool PathTraceCleanRoomSyntheticConstantMode()
{
    return CleanRtxdiDiView == 9u;
}

bool PathTraceCleanRoomSyntheticAnalyticPayloadMode()
{
    return CleanRtxdiDiView == 10u;
}

bool PathTraceCleanRoomSyntheticOverlapMode()
{
    return CleanRtxdiDiView == 11u;
}

bool PathTraceCleanRoomRealAnalyticPointProxyMode()
{
    return false;
}

bool PathTraceCleanRoomSyntheticMode()
{
    return PathTraceCleanRoomSyntheticConstantMode() ||
        PathTraceCleanRoomSyntheticAnalyticPayloadMode() ||
        PathTraceCleanRoomSyntheticOverlapMode();
}

bool PathTraceCleanRoomSyntheticSingleLightMode()
{
    return PathTraceCleanRoomSyntheticConstantMode() ||
        (PathTraceCleanRoomSyntheticAnalyticPayloadMode() && (uint)clamp(CleanRtxdiDiRestirPTSurfaceInfo.z, 1.0, 8.0) <= 1u);
}

uint PathTraceCleanRoomSyntheticLightCount()
{
    if (PathTraceCleanRoomSyntheticOverlapMode())
    {
        return CLEAN_SYNTHETIC_OVERLAP_LIGHT_COUNT;
    }
    if (PathTraceCleanRoomSyntheticAnalyticPayloadMode())
    {
        const uint requestedCount = (uint)clamp(CleanRtxdiDiRestirPTSurfaceInfo.z, 1.0, 8.0);
        return min(requestedCount, min(CleanRtxdiDiAnalyticLightCount, CleanRtxdiDiAnalyticIdentityCount));
    }
    return 1u;
}

uint PathTraceCleanRoomInitialLightCount()
{
    if ((CleanRtxdiDiFlags & CLEAN_FLAG_REMIX_LIGHT_UNIVERSE) != 0u)
    {
        return CleanRtxdiDiRluCurrentLightCount;
    }

    const uint analyticCount = min(CleanRtxdiDiAnalyticLightCount, CleanRtxdiDiAnalyticIdentityCount);
    return analyticCount;
}

bool PathTraceCleanRoomRemixLightUniverseEnabled()
{
    return (CleanRtxdiDiFlags & CLEAN_FLAG_REMIX_LIGHT_UNIVERSE) != 0u;
}

bool PathTraceCleanRoomExternalPdfNeeCurrentEnabled()
{
    return (CleanRtxdiDiFlags & CLEAN_FLAG_EXTERNAL_PDFNEE_CURRENT) != 0u;
}

uint PathTraceCleanRoomExternalPdfNeeEmissiveOffset()
{
    return 0u;
}

uint PathTraceCleanRoomExternalPdfNeeToAnalyticIndex(uint lightIndex)
{
    const uint offset = PathTraceCleanRoomExternalPdfNeeEmissiveOffset();
    return lightIndex >= offset ? (lightIndex - offset) : 0xffffffffu;
}

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

float PathTraceCleanRoomLuminance(float3 value)
{
    return dot(max(value, float3(0.0, 0.0, 0.0)), float3(0.2126, 0.7152, 0.0722));
}

uint PathTraceCleanRoomReservoirBlockCount(uint dimension)
{
    return (dimension + RTXDI_RESERVOIR_BLOCK_SIZE - 1u) / RTXDI_RESERVOIR_BLOCK_SIZE;
}

RTXDI_ReservoirBufferParameters PathTraceCleanRoomReservoirParams(uint2 dimensions)
{
    RTXDI_ReservoirBufferParameters params = (RTXDI_ReservoirBufferParameters)0;
    const uint blockCountX = max(PathTraceCleanRoomReservoirBlockCount(dimensions.x), 1u);
    const uint blockCountY = max(PathTraceCleanRoomReservoirBlockCount(dimensions.y), 1u);
    params.reservoirBlockRowPitch = blockCountX * RTXDI_RESERVOIR_BLOCK_SIZE * RTXDI_RESERVOIR_BLOCK_SIZE;
    params.reservoirArrayPitch = params.reservoirBlockRowPitch * blockCountY;
    return params;
}

uint PathTraceCleanRoomReservoirPointer(uint2 pixel, uint2 dimensions)
{
    return RTXDI_ReservoirPositionToPointer(PathTraceCleanRoomReservoirParams(dimensions), pixel, 0u);
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

bool PathTraceCleanRoomLoadSurfaceRecordSigned(int2 pixel, uint2 dimensions, bool previousFrame, out PathTracePrimarySurfaceRecord record)
{
    record = (PathTracePrimarySurfaceRecord)0;
    const uint width = CleanRtxdiDiWidth != 0u ? CleanRtxdiDiWidth : dimensions.x;
    const uint height = CleanRtxdiDiHeight != 0u ? CleanRtxdiDiHeight : dimensions.y;
    if (width == 0u || height == 0u || pixel.x < 0 || pixel.y < 0 || (uint)pixel.x >= width || (uint)pixel.y >= height)
    {
        return false;
    }

    const uint index = (uint)pixel.y * width + (uint)pixel.x;
    if (previousFrame)
    {
        record = PrimarySurfaceHistoryPrevious[index];
    }
    else
    {
        record = PrimarySurfaceHistoryCurrent[index];
    }
    return record.header.x == RT_PATH_TRACE_PRIMARY_SURFACE_RECORD_VERSION &&
        (record.header.y & RT_PRIMARY_SURFACE_VALID) != 0u;
}

RAB_Surface PathTraceCleanRoomSurfaceFromRecord(PathTracePrimarySurfaceRecord record)
{
    RAB_Surface surface = RAB_EmptySurface();
    if (record.header.x != RT_PATH_TRACE_PRIMARY_SURFACE_RECORD_VERSION ||
        (record.header.y & RT_PRIMARY_SURFACE_VALID) == 0u)
    {
        return surface;
    }

    surface.valid = 1u;
    surface.worldPos = record.worldPositionAndViewDepth.xyz;
    surface.linearDepth = record.worldPositionAndViewDepth.w;
    surface.geometryNormal = PathTraceCleanRoomSafeNormalize(record.geometricNormalAndRoughness.xyz, float3(0.0, 0.0, 1.0));
    surface.shadingNormal = PathTraceCleanRoomSafeNormalize(record.shadingNormalAndOpacity.xyz, surface.geometryNormal);
    surface.viewDir = PathTraceCleanRoomSafeNormalize(record.viewDirectionAndReserved.xyz, -surface.shadingNormal);
    surface.materialId = record.materialAndSurface.x;
    surface.materialIndex = record.materialAndSurface.y;
    surface.surfaceClass = record.materialAndSurface.w;
    surface.material = RAB_EmptyMaterial();
    surface.material.materialId = surface.materialId;
    surface.material.materialIndex = surface.materialIndex;
    surface.material.diffuseAlbedo = float3(0.5, 0.5, 0.5);
    surface.material.roughness = 1.0;
    surface.material.opacity = 1.0;
    return surface;
}

RAB_Surface RAB_GetGBufferSurface(int2 pixel, bool previousFrame)
{
    const uint2 dimensions = uint2(
        CleanRtxdiDiWidth != 0u ? CleanRtxdiDiWidth : DispatchRaysDimensions().x,
        CleanRtxdiDiHeight != 0u ? CleanRtxdiDiHeight : DispatchRaysDimensions().y);
    PathTracePrimarySurfaceRecord record;
    if (!PathTraceCleanRoomLoadSurfaceRecordSigned(pixel, dimensions, previousFrame, record))
    {
        return RAB_EmptySurface();
    }
    return PathTraceCleanRoomSurfaceFromRecord(record);
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

bool PathTraceCleanRoomAnalyticIdentityValid(PathTraceDoomAnalyticLightCandidateIdentity identity)
{
    return (identity.flags & CLEAN_DOOM_ANALYTIC_IDENTITY_VALID) != 0u;
}

bool PathTraceCleanRoomAnalyticIdentitySampleable(PathTraceDoomAnalyticLightCandidateIdentity identity)
{
    const uint identityRequiredFlags = CLEAN_DOOM_ANALYTIC_IDENTITY_VALID | CLEAN_DOOM_ANALYTIC_IDENTITY_SAMPLEABLE;
    return (identity.flags & identityRequiredFlags) == identityRequiredFlags;
}

uint PathTraceCleanRoomAnalyticIdentityRemapIndex(PathTraceDoomAnalyticLightCandidateIdentity identity)
{
    return identity.padding0;
}

bool PathTraceCleanRoomAnalyticRemapValid(PathTraceDoomAnalyticLightRemap remap)
{
    return (remap.flags & CLEAN_DOOM_ANALYTIC_IDENTITY_REMAP_VALID) != 0u;
}

RAB_LightInfo PathTraceCleanRoomBuildAnalyticLightInfo(PathTraceDoomAnalyticLightCandidate light, uint lightIndex)
{
    RAB_LightInfo lightInfo = RAB_EmptyLightInfo();
    if (!PathTraceCleanRoomAnalyticPayloadValid(light))
    {
        return lightInfo;
    }

    lightInfo.lightType = RAB_LIGHT_TYPE_DOOM_ANALYTIC_SPHERE;
    lightInfo.lightIndex = lightIndex;
    lightInfo.unifiedLightType = PATH_TRACE_UNIFIED_LIGHT_TYPE_DOOM_ANALYTIC;
    lightInfo.materialIndex = RAB_INVALID_LIGHT_INDEX;
    lightInfo.flags = light.flags;
    lightInfo.position = light.originAndRadius.xyz;
    lightInfo.radius = max(light.originAndRadius.w, 0.01);
    lightInfo.normal = float3(0.0, 0.0, 1.0);
    lightInfo.radiance = max(light.colorAndIntensity.rgb, float3(0.0, 0.0, 0.0)) * max(CleanRtxdiDiDoomAnalyticLightInfo.z, 0.0);
    lightInfo.influenceRadius = max(light.doomRadiusAndArea.x, lightInfo.radius);
    lightInfo.area = max(light.doomRadiusAndArea.y, 1.0e-4);
    lightInfo.weight = PathTraceCleanRoomLuminance(lightInfo.radiance) * lightInfo.area * lightInfo.influenceRadius;
    return lightInfo;
}

bool PathTraceCleanRoomRluPayloadValid(PathTraceUnifiedLightRecord light)
{
    const float3 radiance = max(light.radianceAndLuminance.rgb, float3(0.0, 0.0, 0.0));
    const float luminance = dot(radiance, float3(0.2126, 0.7152, 0.0722));
    return light.type == PATH_TRACE_UNIFIED_LIGHT_TYPE_DOOM_ANALYTIC &&
        all(light.positionAndRadius.xyz == light.positionAndRadius.xyz) &&
        all(abs(light.positionAndRadius.xyz) < float3(3.402823e+38, 3.402823e+38, 3.402823e+38)) &&
        all(radiance == radiance) &&
        all(abs(radiance) < float3(3.402823e+38, 3.402823e+38, 3.402823e+38)) &&
        luminance > 0.0 &&
        light.positionAndRadius.w > 0.0 &&
        light.positionAndRadius.w < 3.402823e+38 &&
        light.uvOrDoomParams.x > 0.0 &&
        light.uvOrDoomParams.x < 3.402823e+38;
}

RAB_LightInfo PathTraceCleanRoomBuildRluLightInfo(PathTraceUnifiedLightRecord light, uint lightIndex)
{
    RAB_LightInfo lightInfo = RAB_EmptyLightInfo();
    if (!PathTraceCleanRoomRluPayloadValid(light))
    {
        return lightInfo;
    }

    lightInfo.lightType = RAB_LIGHT_TYPE_DOOM_ANALYTIC_SPHERE;
    lightInfo.lightIndex = lightIndex;
    lightInfo.unifiedLightType = PATH_TRACE_UNIFIED_LIGHT_TYPE_DOOM_ANALYTIC;
    lightInfo.materialIndex = RAB_INVALID_LIGHT_INDEX;
    lightInfo.flags = light.flags;
    lightInfo.position = light.positionAndRadius.xyz;
    lightInfo.radius = max(light.positionAndRadius.w, 0.01);
    lightInfo.normal = float3(0.0, 0.0, 1.0);
    lightInfo.radiance = max(light.radianceAndLuminance.rgb, float3(0.0, 0.0, 0.0)) * max(CleanRtxdiDiDoomAnalyticLightInfo.z, 0.0);
    lightInfo.influenceRadius = max(light.uvOrDoomParams.x, lightInfo.radius);
    lightInfo.area = max(light.uvOrDoomParams.y, 1.0e-4);
    lightInfo.weight = PathTraceCleanRoomLuminance(lightInfo.radiance) * lightInfo.area * lightInfo.influenceRadius;
    return lightInfo;
}

RAB_LightInfo PathTraceCleanRoomBuildSyntheticConstantLightInfo()
{
    RAB_LightInfo lightInfo = RAB_EmptyLightInfo();
    lightInfo.lightType = RAB_LIGHT_TYPE_DOOM_ANALYTIC_SPHERE;
    lightInfo.lightIndex = 0u;
    lightInfo.unifiedLightType = PATH_TRACE_UNIFIED_LIGHT_TYPE_DOOM_ANALYTIC;
    lightInfo.materialIndex = RAB_INVALID_LIGHT_INDEX;
    const float3 forward = PathTraceCleanRoomSafeNormalize(CleanRtxdiDiCameraForwardAndTanX.xyz, float3(1.0, 0.0, 0.0));
    lightInfo.position = CleanRtxdiDiCameraOriginAndValid.xyz + forward * 96.0;
    lightInfo.radiance = CLEAN_SYNTHETIC_CONSTANT_RADIANCE;
    lightInfo.normal = float3(0.0, 0.0, 1.0);
    lightInfo.radius = 12.0;
    lightInfo.influenceRadius = 220.0;
    lightInfo.area = 4.0 * CLEAN_RTXDI_PI * lightInfo.radius * lightInfo.radius;
    lightInfo.weight = PathTraceCleanRoomLuminance(lightInfo.radiance) * lightInfo.area * lightInfo.influenceRadius;
    return lightInfo;
}

uint PathTraceCleanRoomFindFixedAnalyticLightIndex(uint requestedSampleableIndex)
{
    const uint lightCount = min(CleanRtxdiDiAnalyticLightCount, CleanRtxdiDiAnalyticIdentityCount);
    const uint firstSampleableIndex = (uint)clamp(CleanRtxdiDiRestirPTSurfaceInfo.x, 0.0, 64.0);
    uint sampleableIndex = 0u;
    for (uint index = 0u; index < lightCount; ++index)
    {
        if (PathTraceCleanRoomAnalyticIdentitySampleable(DoomAnalyticCurrentIdentities[index]) &&
            PathTraceCleanRoomAnalyticPayloadValid(DoomAnalyticLights[index]))
        {
            if (sampleableIndex == firstSampleableIndex + requestedSampleableIndex)
            {
                return index;
            }
            ++sampleableIndex;
        }
    }
    return 0xffffffffu;
}

RAB_LightInfo PathTraceCleanRoomBuildSyntheticAnalyticPayloadLightInfo(uint lightIndex)
{
    const uint sourceIndex = PathTraceCleanRoomFindFixedAnalyticLightIndex(lightIndex);
    if (sourceIndex == 0xffffffffu)
    {
        return RAB_EmptyLightInfo();
    }

    const PathTraceDoomAnalyticLightCandidate light = DoomAnalyticLights[sourceIndex];
    RAB_LightInfo lightInfo = RAB_EmptyLightInfo();
    lightInfo.lightType = RAB_LIGHT_TYPE_DOOM_ANALYTIC_SPHERE;
    lightInfo.lightIndex = lightIndex;
    lightInfo.unifiedLightType = PATH_TRACE_UNIFIED_LIGHT_TYPE_DOOM_ANALYTIC;
    lightInfo.materialIndex = RAB_INVALID_LIGHT_INDEX;
    lightInfo.flags = light.flags;
    lightInfo.position = light.originAndRadius.xyz;
    lightInfo.radius = max(light.originAndRadius.w, 0.01);
    lightInfo.normal = float3(0.0, 0.0, 1.0);
    lightInfo.radiance = max(light.colorAndIntensity.rgb, float3(0.0, 0.0, 0.0)) * max(CleanRtxdiDiDoomAnalyticLightInfo.z, 0.0);
    lightInfo.influenceRadius = max(light.doomRadiusAndArea.x, lightInfo.radius);
    lightInfo.area = max(light.doomRadiusAndArea.y, 1.0e-4);
    lightInfo.weight = PathTraceCleanRoomLuminance(lightInfo.radiance) * lightInfo.area * lightInfo.influenceRadius;
    return lightInfo;
}

RAB_LightInfo PathTraceCleanRoomBuildSyntheticOverlapLightInfo(uint lightIndex)
{
    if (lightIndex >= CLEAN_SYNTHETIC_OVERLAP_LIGHT_COUNT ||
        CleanRtxdiDiCameraOriginAndValid.w < 0.5)
    {
        return RAB_EmptyLightInfo();
    }

    static const float3 overlapRadiance[CLEAN_SYNTHETIC_OVERLAP_LIGHT_COUNT] =
    {
        float3(0.55, 0.48, 0.40),
        float3(0.40, 0.52, 0.60),
        float3(0.58, 0.42, 0.52),
        float3(0.46, 0.56, 0.44)
    };
    static const float3 overlapOffsets[CLEAN_SYNTHETIC_OVERLAP_LIGHT_COUNT] =
    {
        float3(0.0, 0.0, 0.0),
        float3(18.0, 4.0, 5.0),
        float3(-14.0, 13.0, -3.0),
        float3(7.0, -15.0, 8.0)
    };

    const float3 forward = PathTraceCleanRoomSafeNormalize(CleanRtxdiDiCameraForwardAndTanX.xyz, float3(1.0, 0.0, 0.0));
    const float3 left = PathTraceCleanRoomSafeNormalize(CleanRtxdiDiCameraLeftAndTanY.xyz, float3(0.0, 1.0, 0.0));
    const float3 up = PathTraceCleanRoomSafeNormalize(CleanRtxdiDiCameraUpAndTanY.xyz, float3(0.0, 0.0, 1.0));
    const float3 offset = overlapOffsets[lightIndex];

    RAB_LightInfo lightInfo = RAB_EmptyLightInfo();
    lightInfo.lightType = RAB_LIGHT_TYPE_DOOM_ANALYTIC_SPHERE;
    lightInfo.lightIndex = lightIndex;
    lightInfo.unifiedLightType = PATH_TRACE_UNIFIED_LIGHT_TYPE_DOOM_ANALYTIC;
    lightInfo.materialIndex = RAB_INVALID_LIGHT_INDEX;
    lightInfo.position = CleanRtxdiDiCameraOriginAndValid.xyz +
        forward * (92.0 + offset.z) +
        left * offset.x +
        up * offset.y;
    lightInfo.radiance = overlapRadiance[lightIndex];
    lightInfo.normal = float3(0.0, 0.0, 1.0);
    lightInfo.radius = 12.0;
    lightInfo.influenceRadius = 220.0;
    lightInfo.area = 4.0 * CLEAN_RTXDI_PI * lightInfo.radius * lightInfo.radius;
    lightInfo.weight = PathTraceCleanRoomLuminance(lightInfo.radiance) * lightInfo.area * lightInfo.influenceRadius;
    return lightInfo;
}

int RAB_TranslateLightIndex(uint lightIndex, bool currentToPrevious)
{
    if (PathTraceCleanRoomSyntheticMode())
    {
        return lightIndex < PathTraceCleanRoomSyntheticLightCount() ? (int)lightIndex : -1;
    }

    if (PathTraceCleanRoomRemixLightUniverseEnabled())
    {
        if (currentToPrevious)
        {
            if (lightIndex >= CleanRtxdiDiRluCurrentLightCount || lightIndex >= CleanRtxdiDiRluCurrentToPreviousCount)
            {
                return -1;
            }

            const uint previousIndex = CleanRtxdiDiRluCurrentToPrevious[lightIndex];
            return previousIndex < CleanRtxdiDiRluPreviousLightCount ? (int)previousIndex : -1;
        }

        if (lightIndex >= CleanRtxdiDiRluPreviousLightCount || lightIndex >= CleanRtxdiDiRluPreviousToCurrentCount)
        {
            return -1;
        }

        const uint currentIndex = CleanRtxdiDiRluPreviousToCurrent[lightIndex];
        return currentIndex < CleanRtxdiDiRluCurrentLightCount ? (int)currentIndex : -1;
    }

    if (PathTraceCleanRoomExternalPdfNeeCurrentEnabled())
    {
        const uint emissiveOffset = PathTraceCleanRoomExternalPdfNeeEmissiveOffset();
        const uint analyticIndex = PathTraceCleanRoomExternalPdfNeeToAnalyticIndex(lightIndex);
        if (analyticIndex == 0xffffffffu)
        {
            return -1;
        }

        if (currentToPrevious)
        {
            if (analyticIndex >= CleanRtxdiDiAnalyticLightCount || analyticIndex >= CleanRtxdiDiAnalyticIdentityCount)
            {
                return -1;
            }

            const PathTraceDoomAnalyticLightCandidateIdentity currentIdentity = DoomAnalyticCurrentIdentities[analyticIndex];
            const uint remapIndex = PathTraceCleanRoomAnalyticIdentityRemapIndex(currentIdentity);
            if (!PathTraceCleanRoomAnalyticIdentityValid(currentIdentity) || remapIndex >= CleanRtxdiDiAnalyticRemapCount)
            {
                return -1;
            }

            const PathTraceDoomAnalyticLightRemap remap = DoomAnalyticRemap[remapIndex];
            if (!PathTraceCleanRoomAnalyticRemapValid(remap) || remap.currentToPreviousCandidateIndex < 0)
            {
                return -1;
            }

            const uint previousIndex = (uint)remap.currentToPreviousCandidateIndex;
            return previousIndex < CleanRtxdiDiPreviousAnalyticIdentityCount ? (int)(emissiveOffset + previousIndex) : -1;
        }

        if (analyticIndex >= CleanRtxdiDiPreviousAnalyticLightCount || analyticIndex >= CleanRtxdiDiPreviousAnalyticIdentityCount)
        {
            return -1;
        }

        const PathTraceDoomAnalyticLightCandidateIdentity previousIdentity = DoomAnalyticPreviousIdentities[analyticIndex];
        const uint remapIndex = PathTraceCleanRoomAnalyticIdentityRemapIndex(previousIdentity);
        if (!PathTraceCleanRoomAnalyticIdentityValid(previousIdentity) || remapIndex >= CleanRtxdiDiAnalyticRemapCount)
        {
            return -1;
        }

        const PathTraceDoomAnalyticLightRemap remap = DoomAnalyticRemap[remapIndex];
        if (!PathTraceCleanRoomAnalyticRemapValid(remap) || remap.previousToCurrentCandidateIndex < 0)
        {
            return -1;
        }

        const uint currentIndex = (uint)remap.previousToCurrentCandidateIndex;
        return currentIndex < CleanRtxdiDiAnalyticLightCount && currentIndex < CleanRtxdiDiAnalyticIdentityCount ? (int)(emissiveOffset + currentIndex) : -1;
    }

    if (currentToPrevious)
    {
        if (lightIndex >= CleanRtxdiDiAnalyticLightCount || lightIndex >= CleanRtxdiDiAnalyticIdentityCount)
        {
            return -1;
        }

        const PathTraceDoomAnalyticLightCandidateIdentity currentIdentity = DoomAnalyticCurrentIdentities[lightIndex];
        const uint remapIndex = PathTraceCleanRoomAnalyticIdentityRemapIndex(currentIdentity);
        if (!PathTraceCleanRoomAnalyticIdentityValid(currentIdentity) || remapIndex >= CleanRtxdiDiAnalyticRemapCount)
        {
            return -1;
        }

        const PathTraceDoomAnalyticLightRemap remap = DoomAnalyticRemap[remapIndex];
        if (!PathTraceCleanRoomAnalyticRemapValid(remap) || remap.currentToPreviousCandidateIndex < 0)
        {
            return -1;
        }

        const uint previousIndex = (uint)remap.currentToPreviousCandidateIndex;
        return previousIndex < CleanRtxdiDiPreviousAnalyticIdentityCount ? (int)previousIndex : -1;
    }

    if (lightIndex >= CleanRtxdiDiPreviousAnalyticLightCount || lightIndex >= CleanRtxdiDiPreviousAnalyticIdentityCount)
    {
        return -1;
    }

    const PathTraceDoomAnalyticLightCandidateIdentity previousIdentity = DoomAnalyticPreviousIdentities[lightIndex];
    const uint remapIndex = PathTraceCleanRoomAnalyticIdentityRemapIndex(previousIdentity);
    if (!PathTraceCleanRoomAnalyticIdentityValid(previousIdentity) || remapIndex >= CleanRtxdiDiAnalyticRemapCount)
    {
        return -1;
    }

    const PathTraceDoomAnalyticLightRemap remap = DoomAnalyticRemap[remapIndex];
    if (!PathTraceCleanRoomAnalyticRemapValid(remap) || remap.previousToCurrentCandidateIndex < 0)
    {
        return -1;
    }

    const uint currentIndex = (uint)remap.previousToCurrentCandidateIndex;
    return currentIndex < CleanRtxdiDiAnalyticLightCount && currentIndex < CleanRtxdiDiAnalyticIdentityCount ? (int)currentIndex : -1;
}

RAB_LightInfo RAB_LoadLightInfo(uint lightIndex, bool previousFrame)
{
    if (PathTraceCleanRoomSyntheticConstantMode())
    {
        if (lightIndex == 0u)
        {
            return PathTraceCleanRoomBuildSyntheticConstantLightInfo();
        }
        return RAB_EmptyLightInfo();
    }
    if (PathTraceCleanRoomSyntheticAnalyticPayloadMode())
    {
        if (lightIndex < PathTraceCleanRoomSyntheticLightCount())
        {
            return PathTraceCleanRoomBuildSyntheticAnalyticPayloadLightInfo(lightIndex);
        }
        return RAB_EmptyLightInfo();
    }
    if (PathTraceCleanRoomSyntheticOverlapMode())
    {
        return PathTraceCleanRoomBuildSyntheticOverlapLightInfo(lightIndex);
    }

    if (PathTraceCleanRoomRemixLightUniverseEnabled())
    {
        if (previousFrame)
        {
            if (lightIndex >= CleanRtxdiDiRluPreviousLightCount)
            {
                return RAB_EmptyLightInfo();
            }
            return PathTraceCleanRoomBuildRluLightInfo(CleanRtxdiDiRluPreviousLights[lightIndex], lightIndex);
        }

        if (lightIndex >= CleanRtxdiDiRluCurrentLightCount)
        {
            return RAB_EmptyLightInfo();
        }
        return PathTraceCleanRoomBuildRluLightInfo(CleanRtxdiDiRluCurrentLights[lightIndex], lightIndex);
    }

    if (PathTraceCleanRoomExternalPdfNeeCurrentEnabled())
    {
        const uint analyticIndex = PathTraceCleanRoomExternalPdfNeeToAnalyticIndex(lightIndex);
        if (analyticIndex == 0xffffffffu)
        {
            return RAB_EmptyLightInfo();
        }

        if (previousFrame)
        {
            if (analyticIndex >= CleanRtxdiDiPreviousAnalyticLightCount)
            {
                return RAB_EmptyLightInfo();
            }
            return PathTraceCleanRoomBuildAnalyticLightInfo(DoomAnalyticPreviousLights[analyticIndex], lightIndex);
        }

        if (analyticIndex >= CleanRtxdiDiAnalyticLightCount)
        {
            return RAB_EmptyLightInfo();
        }
        return PathTraceCleanRoomBuildAnalyticLightInfo(DoomAnalyticLights[analyticIndex], lightIndex);
    }

    if (previousFrame)
    {
        if (lightIndex >= CleanRtxdiDiPreviousAnalyticLightCount)
        {
            return RAB_EmptyLightInfo();
        }
        return PathTraceCleanRoomBuildAnalyticLightInfo(DoomAnalyticPreviousLights[lightIndex], lightIndex);
    }

    if (lightIndex >= CleanRtxdiDiAnalyticLightCount)
    {
        return RAB_EmptyLightInfo();
    }
    return PathTraceCleanRoomBuildAnalyticLightInfo(DoomAnalyticLights[lightIndex], lightIndex);
}

float PathTraceCleanRoomTraceVisibility(PathTracePrimarySurfaceRecord surface, float3 samplePosition);

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
    const float3 flatDiffuse = float3(0.5, 0.5, 0.5) * (1.0 / CLEAN_RTXDI_PI);
    return PathTraceCleanRoomLuminance(flatDiffuse * max(sampleRadiance, float3(0.0, 0.0, 0.0)) * ndotl);
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

bool RAB_GetConservativeVisibility(RAB_Surface surface, RAB_LightSample lightSample)
{
    PathTracePrimarySurfaceRecord record = (PathTracePrimarySurfaceRecord)0;
    record.header = uint4(RT_PATH_TRACE_PRIMARY_SURFACE_RECORD_VERSION, RT_PRIMARY_SURFACE_VALID, 0u, 0u);
    record.worldPositionAndViewDepth = float4(surface.worldPos, surface.linearDepth);
    record.geometricNormalAndRoughness = float4(surface.geometryNormal, 0.5);
    record.shadingNormalAndOpacity = float4(surface.shadingNormal, 1.0);
    record.viewDirectionAndReserved = float4(surface.viewDir, 0.0);
    return PathTraceCleanRoomTraceVisibility(record, lightSample.position) > 0.0;
}

bool RAB_TraceRayForLocalLight(float3 rayOrigin, float3 rayDirection, float tMin, float tMax, out uint lightIndex, out float2 randXY)
{
    lightIndex = RTXDI_InvalidLightIndex;
    randXY = float2(0.0, 0.0);
    return false;
}

float RAB_EvaluateLocalLightSourcePdf(uint lightIndex)
{
    return 0.0;
}

float2 RAB_GetEnvironmentMapRandXYFromDir(float3 sampleDir)
{
    return float2(0.0, 0.0);
}

float RAB_EvaluateEnvironmentMapSamplingPdf(float3 sampleDir)
{
    return 0.0;
}

bool RAB_GetTemporalConservativeVisibility(RAB_Surface surface, RAB_Surface previousSurface, RAB_LightSample lightSample)
{
    return RAB_GetConservativeVisibility(surface, lightSample);
}

#include "Rtxdi/DI/InitialSampling.hlsli"
#include "Rtxdi/DI/TemporalResampling.hlsli"

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
    return PathTraceCleanRoomReservoirPointer(pixel, dimensions);
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
    if (status == CLEAN_INITIAL_STATUS_EXTERNAL_CURRENT_EMPTY)
    {
        return float3(0.0, 0.0, 0.0);
    }
    if (status == CLEAN_INITIAL_STATUS_EXTERNAL_UNSUPPORTED_LIGHT)
    {
        return float3(0.65, 0.0, 1.0);
    }
    return float3(0.05, 0.90, 0.18);
}

PathTraceCleanRtxdiDiInitialResult PathTraceCleanRoomRunInitialProducer(uint2 pixel, uint2 dimensions)
{
    PathTraceCleanRtxdiDiInitialResult result = (PathTraceCleanRtxdiDiInitialResult)0;
    result.reservoir = RTXDI_EmptyDIReservoir();
    result.selectedLightIndex = 0xffffffffu;
    result.status = CLEAN_INITIAL_STATUS_VALID;

    const bool syntheticMode = PathTraceCleanRoomSyntheticMode();
    if (!syntheticMode && CleanRtxdiDiLightMode == 0u)
    {
        result.status = CLEAN_INITIAL_STATUS_NO_LIGHT_MODE;
        return result;
    }
    if (!syntheticMode && CleanRtxdiDiLightMode != 1u)
    {
        result.status = CLEAN_INITIAL_STATUS_DEFERRED_LIGHT_MODE;
        return result;
    }

    const uint lightCount = syntheticMode ? PathTraceCleanRoomSyntheticLightCount() : PathTraceCleanRoomInitialLightCount();
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

    const RAB_Surface surface = PathTraceCleanRoomSurfaceFromRecord(result.surface);
    if (!RAB_IsSurfaceValid(surface))
    {
        result.status = CLEAN_INITIAL_STATUS_INVALID_SURFACE;
        return result;
    }

    RTXDI_DIInitialSamplingParameters sampleParams = (RTXDI_DIInitialSamplingParameters)0;
    sampleParams.numLocalLightSamples = syntheticMode
        ? 1u
        : max(CleanRtxdiDiCandidateCount, 1u);
    sampleParams.localLightSamplingMode = ReSTIRDI_LocalLightSamplingMode_UNIFORM;
    sampleParams.enableInitialVisibility = 0u;

    RTXDI_LightBufferRegion localLightRegion = (RTXDI_LightBufferRegion)0;
    localLightRegion.firstLightIndex = 0u;
    localLightRegion.numLights = lightCount;

    RTXDI_InitialSamplingMisData misData = RTXDI_ComputeInitialSamplingMisData(sampleParams);
    RTXDI_RandomSamplerState rng = RTXDI_InitRandomSampler(pixel, CleanRtxdiDiFrameIndex, 0x4d534449u);
    RTXDI_RandomSamplerState coherentRng = RTXDI_InitRandomSampler(pixel / RTXDI_TILE_SIZE_IN_PIXELS, CleanRtxdiDiFrameIndex, 0x4d534449u);
    RAB_LightSample selectedSample = RAB_EmptyLightSample();
    result.reservoir = RTXDI_SampleLocalLights(
        rng,
        coherentRng,
        surface,
        sampleParams,
        misData,
        ReSTIRDI_LocalLightSamplingMode_UNIFORM,
        localLightRegion,
        selectedSample);

    result.status = RTXDI_IsValidDIReservoir(result.reservoir) ? CLEAN_INITIAL_STATUS_VALID : CLEAN_INITIAL_STATUS_ZERO_TARGET_PDF;
    if (result.status != CLEAN_INITIAL_STATUS_VALID)
    {
        return result;
    }

    result.selectedLightIndex = RTXDI_GetDIReservoirLightIndex(result.reservoir);
    result.sampleUv = RTXDI_GetDIReservoirSampleUV(result.reservoir);
    result.samplePosition = selectedSample.position;
    result.sampleRadiance = selectedSample.radiance;
    result.solidAnglePdf = selectedSample.solidAnglePdf;
    result.targetPdf = result.reservoir.targetPdf;
    result.invSourcePdf = RTXDI_GetDIReservoirInvPdf(result.reservoir);
    if ((!syntheticMode && result.selectedLightIndex >= lightCount) ||
        selectedSample.valid == 0u || selectedSample.solidAnglePdf <= 1.0e-8)
    {
        result.status = CLEAN_INITIAL_STATUS_BAD_SAMPLE_PDF;
        result.reservoir = RTXDI_EmptyDIReservoir();
        return result;
    }

    if (!syntheticMode && !PathTraceCleanRoomRemixLightUniverseEnabled())
    {
        result.light = DoomAnalyticLights[result.selectedLightIndex];
    }
    result.visibility = PathTraceCleanRoomSyntheticOverlapMode()
        ? PathTraceCleanRoomTraceVisibility(result.surface, result.samplePosition)
        : (syntheticMode ? 1.0 : PathTraceCleanRoomTraceVisibility(result.surface, result.samplePosition));
    RTXDI_StoreVisibilityInDIReservoir(result.reservoir, result.visibility.xxx, false);
    return result;
}

PathTraceCleanRtxdiDiInitialResult PathTraceCleanRoomRunExternalPdfNeeCurrentProducer(uint2 pixel, uint2 dimensions)
{
    PathTraceCleanRtxdiDiInitialResult result = (PathTraceCleanRtxdiDiInitialResult)0;
    result.reservoir = RTXDI_EmptyDIReservoir();
    result.selectedLightIndex = 0xffffffffu;
    result.status = CLEAN_INITIAL_STATUS_VALID;

    if (CleanRtxdiDiLightMode != 1u)
    {
        result.status = CLEAN_INITIAL_STATUS_DEFERRED_LIGHT_MODE;
        return result;
    }

    if (!PathTraceCleanRoomLoadSurfaceRecord(pixel, dimensions, result.surface))
    {
        result.status = CLEAN_INITIAL_STATUS_INVALID_SURFACE;
        return result;
    }

    const RAB_Surface surface = PathTraceCleanRoomSurfaceFromRecord(result.surface);
    if (!RAB_IsSurfaceValid(surface))
    {
        result.status = CLEAN_INITIAL_STATUS_INVALID_SURFACE;
        return result;
    }

    const uint reservoirIndex = PathTraceCleanRoomReservoirIndex(pixel, dimensions);
    if (reservoirIndex >= CleanRtxdiDiReservoirCount)
    {
        result.status = CLEAN_INITIAL_STATUS_EXTERNAL_CURRENT_EMPTY;
        return result;
    }

    result.reservoir = RTXDI_UnpackDIReservoir(CleanRtxdiDiCurrentReservoirs[reservoirIndex]);
    if (!RTXDI_IsValidDIReservoir(result.reservoir) || result.reservoir.M <= 0.0)
    {
        result.reservoir = RTXDI_EmptyDIReservoir();
        result.status = CLEAN_INITIAL_STATUS_EXTERNAL_CURRENT_EMPTY;
        return result;
    }

    result.selectedLightIndex = RTXDI_GetDIReservoirLightIndex(result.reservoir);
    if (PathTraceCleanRoomExternalPdfNeeToAnalyticIndex(result.selectedLightIndex) == 0xffffffffu)
    {
        result.reservoir = RTXDI_EmptyDIReservoir();
        result.status = CLEAN_INITIAL_STATUS_EXTERNAL_UNSUPPORTED_LIGHT;
        return result;
    }

    const RAB_LightInfo lightInfo = RAB_LoadLightInfo(result.selectedLightIndex, false);
    if (!RAB_IsLightInfoValid(lightInfo))
    {
        result.reservoir = RTXDI_EmptyDIReservoir();
        result.status = CLEAN_INITIAL_STATUS_EXTERNAL_UNSUPPORTED_LIGHT;
        return result;
    }

    result.sampleUv = RTXDI_GetDIReservoirSampleUV(result.reservoir);
    const RAB_LightSample selectedSample = RAB_SamplePolymorphicLight(lightInfo, surface, result.sampleUv);
    if (!RAB_IsReplayableLightSample(selectedSample))
    {
        result.reservoir = RTXDI_EmptyDIReservoir();
        result.status = CLEAN_INITIAL_STATUS_BAD_SAMPLE_PDF;
        return result;
    }

    result.samplePosition = selectedSample.position;
    result.sampleRadiance = selectedSample.radiance;
    result.solidAnglePdf = selectedSample.solidAnglePdf;
    result.targetPdf = result.reservoir.targetPdf;
    result.invSourcePdf = RTXDI_GetDIReservoirInvPdf(result.reservoir);
    result.visibility = PathTraceCleanRoomTraceVisibility(result.surface, result.samplePosition);
    RTXDI_StoreVisibilityInDIReservoir(result.reservoir, result.visibility.xxx, false);
    return result;
}

PathTraceCleanRtxdiDiInitialResult PathTraceCleanRoomRunSelectedInitialProducer(uint2 pixel, uint2 dimensions)
{
    if (PathTraceCleanRoomExternalPdfNeeCurrentEnabled())
    {
        return PathTraceCleanRoomRunExternalPdfNeeCurrentProducer(pixel, dimensions);
    }
    return PathTraceCleanRoomRunInitialProducer(pixel, dimensions);
}

float3 PathTraceCleanRoomFlatDiffuseResolveReservoir(PathTracePrimarySurfaceRecord surfaceRecord, RTXDI_DIReservoir reservoir)
{
    if (!RTXDI_IsValidDIReservoir(reservoir))
    {
        return float3(0.0, 0.0, 0.0);
    }

    const uint lightIndex = RTXDI_GetDIReservoirLightIndex(reservoir);
    if (!PathTraceCleanRoomSyntheticMode() &&
        !PathTraceCleanRoomExternalPdfNeeCurrentEnabled() &&
        lightIndex >= PathTraceCleanRoomInitialLightCount())
    {
        return float3(0.0, 0.0, 0.0);
    }

    const RAB_Surface surface = PathTraceCleanRoomSurfaceFromRecord(surfaceRecord);
    const RAB_LightInfo lightInfo = RAB_LoadLightInfo(lightIndex, false);
    const RAB_LightSample lightSample = RAB_SamplePolymorphicLight(lightInfo, surface, RTXDI_GetDIReservoirSampleUV(reservoir));
    if (!RAB_IsReplayableLightSample(lightSample))
    {
        return float3(0.0, 0.0, 0.0);
    }
    const bool referenceDoomAnalytic = PathTraceCleanReferenceRabEnabled() &&
        lightSample.lightType == RAB_LIGHT_TYPE_DOOM_ANALYTIC_SPHERE;

    const float3 toSample = lightSample.position - surfaceRecord.worldPositionAndViewDepth.xyz;
    const float3 lightDirection = PathTraceCleanRoomSafeNormalize(toSample, PathTraceCleanRoomSafeNormalize(surfaceRecord.shadingNormalAndOpacity.xyz, surfaceRecord.geometricNormalAndRoughness.xyz));
    const float ndotl = saturate(dot(PathTraceCleanRoomSafeNormalize(surfaceRecord.shadingNormalAndOpacity.xyz, surfaceRecord.geometricNormalAndRoughness.xyz), lightDirection));
    const float3 flatDiffuse = float3(0.5, 0.5, 0.5) * (1.0 / CLEAN_RTXDI_PI);
    float visibility = PathTraceCleanRoomSyntheticSingleLightMode()
        ? 1.0
        : PathTraceCleanRoomTraceVisibility(surfaceRecord, lightSample.position);
    if (!PathTraceCleanRoomSyntheticSingleLightMode() && CleanRtxdiDiResolveVisibilityReuse != 0u)
    {
        RTXDI_VisibilityReuseParameters visibilityReuseParams = (RTXDI_VisibilityReuseParameters)0;
        visibilityReuseParams.maxAge = 16u;
        visibilityReuseParams.maxDistance = 4.0;
        float3 reusedVisibility = float3(0.0, 0.0, 0.0);
        if (RTXDI_GetDIReservoirVisibility(reservoir, visibilityReuseParams, reusedVisibility))
        {
            visibility = saturate(dot(reusedVisibility, float3(0.33333334, 0.33333334, 0.33333334)));
        }
    }
    if (referenceDoomAnalytic && (CleanRtxdiDiReferenceRab == 9u || CleanRtxdiDiReferenceRab == 10u))
    {
        visibility = 1.0;
    }
    const float3 reflectedRadiance = referenceDoomAnalytic
        ? PathTraceCleanReferenceRabReflectedRadiance(lightSample.position, lightSample.radiance, surface)
        : CleanRtxdiDiResolveBrdfTarget != 0u
        ? RAB_GetReflectedBsdfRadianceForSurface(lightSample.position, lightSample.radiance, surface)
        : max(lightSample.radiance, float3(0.0, 0.0, 0.0)) * flatDiffuse * ndotl;
    const float reservoirThroughput = referenceDoomAnalytic && CleanRtxdiDiReferenceRab == 10u
        ? 1.0
        : max(RTXDI_GetDIReservoirInvPdf(reservoir), 0.0) / max(lightSample.solidAnglePdf, 1.0e-6);
    return reflectedRadiance *
        reservoirThroughput *
        visibility;
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

    return PathTraceCleanRoomFlatDiffuseResolveReservoir(result.surface, result.reservoir);
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

float PathTraceCleanRoomLog01(float value, float scale)
{
    return saturate(log2(1.0 + max(value, 0.0)) / max(scale, 1.0e-3));
}

float3 PathTraceCleanRoomTargetInvPdfColor(float targetPdf, float invPdf)
{
    return float3(
        PathTraceCleanRoomLog01(targetPdf, 14.0),
        PathTraceCleanRoomLog01(invPdf, 16.0),
        0.15);
}

float3 PathTraceCleanRoomRealAnalyticOneSampleDiagnosticColor(uint2 pixel, uint2 dimensions)
{
    PathTraceCleanRtxdiDiInitialResult result = PathTraceCleanRoomRunSelectedInitialProducer(pixel, dimensions);
    if (result.status != CLEAN_INITIAL_STATUS_VALID)
    {
        return PathTraceCleanRoomStatusColor(result.status);
    }

    const float sampledDistance = distance(result.surface.worldPositionAndViewDepth.xyz, result.samplePosition);
    const float resolvedLuminance = RAB_Luminance(PathTraceCleanRoomFlatDiffuseResolveReservoir(result.surface, result.reservoir));
    const uint band = min((pixel.y * 5u) / max(dimensions.y, 1u), 4u);
    if (band == 0u)
    {
        const float v = PathTraceCleanRoomLog01(sampledDistance, 8.0);
        return float3(v, v, v);
    }
    if (band == 1u)
    {
        const float v = PathTraceCleanRoomLog01(result.solidAnglePdf, 8.0);
        return float3(0.0, v, 0.0);
    }
    if (band == 2u)
    {
        const float v = PathTraceCleanRoomLog01(result.targetPdf, 6.0);
        return float3(v, 0.0, 0.0);
    }
    if (band == 3u)
    {
        const float v = PathTraceCleanRoomLog01(resolvedLuminance, 6.0);
        return float3(v, v * 0.75, 0.05);
    }

    return PathTraceCleanRoomHashColor(result.selectedLightIndex);
}

float3 PathTraceCleanRoomRealAnalyticTargetFactorDiagnosticColor(uint2 pixel, uint2 dimensions)
{
    const uint2 samplePixel = min(dimensions - 1u, dimensions >> 1u);
    PathTracePrimarySurfaceRecord surfaceRecord;
    if (!PathTraceCleanRoomLoadSurfaceRecord(samplePixel, dimensions, surfaceRecord))
    {
        return PathTraceCleanRoomStatusColor(CLEAN_INITIAL_STATUS_INVALID_SURFACE);
    }

    const RAB_Surface surface = PathTraceCleanRoomSurfaceFromRecord(surfaceRecord);
    const uint lightCount = PathTraceCleanRoomInitialLightCount();
    if (!RAB_IsSurfaceValid(surface) || lightCount == 0u || CleanRtxdiDiLightMode != 1u)
    {
        return PathTraceCleanRoomStatusColor(lightCount == 0u ? CLEAN_INITIAL_STATUS_NO_LIGHTS : CLEAN_INITIAL_STATUS_INVALID_SURFACE);
    }

    uint lightIndex = 0u;
    RAB_LightInfo lightInfo = RAB_EmptyLightInfo();
    RAB_LightSample lightSample = RAB_EmptyLightSample();
    const uint scanCount = min(lightCount, 64u);
    [loop]
    for (uint candidateIndex = 0u; candidateIndex < scanCount; ++candidateIndex)
    {
        RAB_LightInfo candidateInfo = RAB_LoadLightInfo(candidateIndex, false);
        RAB_LightSample candidateSample = RAB_SamplePolymorphicLight(candidateInfo, surface, float2(0.5, 0.5));
        if (RAB_IsReplayableLightSample(candidateSample) && RAB_Luminance(candidateSample.radiance) > 1.0e-8)
        {
            lightIndex = candidateIndex;
            lightInfo = candidateInfo;
            lightSample = candidateSample;
            break;
        }
    }
    const float2 sampleUv = float2(0.5, 0.5);
    if (!RAB_IsReplayableLightSample(lightSample))
    {
        return PathTraceCleanRoomHashColor(lightIndex) * float3(0.25, 0.05, 0.05);
    }

    float3 lightDir;
    float lightDistance;
    RAB_GetLightDirDistance(surface, lightSample, lightDir, lightDistance);
    const float shadeNdotL = saturate(dot(RAB_GetSurfaceNormal(surface), lightDir));
    const float geoNdotL = saturate(dot(RAB_GetSurfaceGeoNormal(surface), lightDir));
    const float shadeNdotV = saturate(dot(RAB_GetSurfaceNormal(surface), RAB_GetSurfaceViewDir(surface)));
    const float geoNdotV = saturate(dot(RAB_GetSurfaceGeoNormal(surface), RAB_GetSurfaceViewDir(surface)));
    const float3 brdf = RAB_EvaluateSurfaceBrdf(surface, lightDir, RAB_GetSurfaceViewDir(surface));
    const float radianceLum = RAB_Luminance(lightSample.radiance);
    const float brdfLum = RAB_Luminance(brdf);
    const float targetPdf = RAB_GetLightSampleTargetPdfForSurface(lightSample, surface);
    const float reflectedLum = RAB_Luminance(brdf * lightSample.radiance * shadeNdotL);

    const uint band = min((pixel.y * 8u) / max(dimensions.y, 1u), 7u);
    if (band == 0u)
    {
        const float v = PathTraceCleanRoomLog01(lightDistance, 8.0);
        return float3(v, v, v);
    }
    if (band == 1u)
    {
        const float v = PathTraceCleanRoomLog01(lightSample.solidAnglePdf, 8.0);
        return float3(0.0, v, 0.0);
    }
    if (band == 2u)
    {
        const float v = PathTraceCleanRoomLog01(radianceLum, 6.0);
        return float3(v, v * 0.5, 0.0);
    }
    if (band == 3u)
    {
        return pixel.x < (dimensions.x >> 1u)
            ? float3(0.0, shadeNdotL, 0.0)
            : float3(0.0, 0.0, geoNdotL);
    }
    if (band == 4u)
    {
        return pixel.x < (dimensions.x >> 1u)
            ? float3(shadeNdotV, shadeNdotV, 0.0)
            : float3(geoNdotV, 0.0, geoNdotV);
    }
    if (band == 5u)
    {
        const float v = PathTraceCleanRoomLog01(brdfLum, 4.0);
        return float3(v, 0.0, v);
    }
    if (band == 6u)
    {
        const float target = PathTraceCleanRoomLog01(targetPdf, 6.0);
        const float reflected = PathTraceCleanRoomLog01(reflectedLum, 6.0);
        return pixel.x < (dimensions.x >> 1u)
            ? float3(target, 0.0, 0.0)
            : float3(reflected, reflected * 0.75, 0.0);
    }

    return PathTraceCleanRoomHashColor(lightIndex);
}

float3 PathTraceCleanRoomBinaryGateColor(bool pass)
{
    return pass ? float3(0.0, 0.90, 0.15) : float3(0.90, 0.04, 0.04);
}

float3 PathTraceCleanRoomRealAnalyticBinaryGateDiagnosticColor(uint2 pixel, uint2 dimensions)
{
    const uint2 samplePixel = min(dimensions - 1u, dimensions >> 1u);
    PathTracePrimarySurfaceRecord surfaceRecord;
    const bool surfaceRecordValid = PathTraceCleanRoomLoadSurfaceRecord(samplePixel, dimensions, surfaceRecord);
    RAB_Surface surface = RAB_EmptySurface();
    if (surfaceRecordValid)
    {
        surface = PathTraceCleanRoomSurfaceFromRecord(surfaceRecord);
    }
    const uint lightCount = PathTraceCleanRoomInitialLightCount();
    const bool lightDomainValid = CleanRtxdiDiLightMode == 1u && lightCount > 0u;
    const bool surfaceValid = surfaceRecordValid && RAB_IsSurfaceValid(surface);
    RAB_LightInfo lightInfo = RAB_EmptyLightInfo();
    RAB_LightSample lightSample = RAB_EmptyLightSample();
    if (surfaceValid && lightDomainValid)
    {
        const uint scanCount = min(lightCount, 64u);
        [loop]
        for (uint candidateIndex = 0u; candidateIndex < scanCount; ++candidateIndex)
        {
            RAB_LightInfo candidateInfo = RAB_LoadLightInfo(candidateIndex, false);
            RAB_LightSample candidateSample = RAB_SamplePolymorphicLight(candidateInfo, surface, float2(0.5, 0.5));
            if (RAB_IsReplayableLightSample(candidateSample) && RAB_Luminance(candidateSample.radiance) > 1.0e-8)
            {
                lightInfo = candidateInfo;
                lightSample = candidateSample;
                break;
            }
        }
    }

    float3 lightDir = float3(0.0, 0.0, 1.0);
    float lightDistance = 0.0;
    if (RAB_IsReplayableLightSample(lightSample))
    {
        RAB_GetLightDirDistance(surface, lightSample, lightDir, lightDistance);
    }

    const float shadeNdotL = dot(RAB_GetSurfaceNormal(surface), lightDir);
    const float geoNdotL = dot(RAB_GetSurfaceGeoNormal(surface), lightDir);
    const float shadeNdotV = dot(RAB_GetSurfaceNormal(surface), RAB_GetSurfaceViewDir(surface));
    const float geoNdotV = dot(RAB_GetSurfaceGeoNormal(surface), RAB_GetSurfaceViewDir(surface));
    const float radianceLum = RAB_Luminance(lightSample.radiance);
    const bool brdfSupported = RAB_SurfaceSupportsOpaqueDiffuseBrdf(surface);
    const float3 brdf = RAB_EvaluateSurfaceBrdf(surface, lightDir, RAB_GetSurfaceViewDir(surface));
    const float brdfLum = RAB_Luminance(brdf);
    const float targetPdf = RAB_GetLightSampleTargetPdfForSurface(lightSample, surface);
    const float reflectedLum = RAB_Luminance(brdf * lightSample.radiance * saturate(shadeNdotL));

    const uint band = min((pixel.y * 8u) / max(dimensions.y, 1u), 7u);
    if (band == 0u)
    {
        return PathTraceCleanRoomBinaryGateColor(surfaceValid && lightDomainValid);
    }
    if (band == 1u)
    {
        return PathTraceCleanRoomBinaryGateColor(brdfSupported);
    }
    if (band == 2u)
    {
        return PathTraceCleanRoomBinaryGateColor(RAB_IsReplayableLightSample(lightSample));
    }
    if (band == 3u)
    {
        return PathTraceCleanRoomBinaryGateColor(radianceLum > 1.0e-8);
    }
    if (band == 4u)
    {
        return PathTraceCleanRoomBinaryGateColor(shadeNdotL > 0.0 && geoNdotL > 0.0);
    }
    if (band == 5u)
    {
        return PathTraceCleanRoomBinaryGateColor(shadeNdotV > 0.0 && geoNdotV > 0.0);
    }
    if (band == 6u)
    {
        return PathTraceCleanRoomBinaryGateColor(brdfLum > 1.0e-8);
    }

    return pixel.x < (dimensions.x >> 1u)
        ? PathTraceCleanRoomBinaryGateColor(targetPdf > 1.0e-8)
        : PathTraceCleanRoomBinaryGateColor(reflectedLum > 1.0e-8);
}

bool PathTraceCleanRoomTemporalReuseEnabled()
{
    return (CleanRtxdiDiTemporalFlags & CLEAN_TEMPORAL_FLAG_ENABLE) != 0u &&
        (CleanRtxdiDiTemporalFlags & CLEAN_TEMPORAL_FLAG_PREVIOUS_VALID) != 0u;
}

bool PathTraceCleanRoomProjectCameraMotion(PathTracePrimarySurfaceRecord currentSurface, uint2 pixel, uint2 dimensions, out float3 screenSpaceMotion)
{
    screenSpaceMotion = float3(0.0, 0.0, 0.0);
    if (CleanRtxdiDiPrevCameraOriginAndValid.w < 0.5)
    {
        return false;
    }

    const float3 delta = currentSurface.worldPositionAndViewDepth.xyz - CleanRtxdiDiPrevCameraOriginAndValid.xyz;
    const float forwardDistance = dot(delta, CleanRtxdiDiPrevCameraForwardAndTanX.xyz);
    if (forwardDistance <= 0.05)
    {
        return false;
    }

    const float ndcX = -dot(delta, CleanRtxdiDiPrevCameraLeftAndTanY.xyz) / max(forwardDistance * CleanRtxdiDiPrevCameraForwardAndTanX.w, 1.0e-5);
    const float ndcY = -dot(delta, CleanRtxdiDiPrevCameraUpAndTanY.xyz) / max(forwardDistance * CleanRtxdiDiPrevCameraLeftAndTanY.w, 1.0e-5);
    if (abs(ndcX) > 1.0 || abs(ndcY) > 1.0)
    {
        return false;
    }

    const float2 previousPixelFloat = (float2(ndcX, ndcY) * 0.5 + 0.5) * float2(dimensions);
    if (!all(previousPixelFloat == previousPixelFloat) ||
        previousPixelFloat.x < 0.0 || previousPixelFloat.y < 0.0 ||
        previousPixelFloat.x >= (float)dimensions.x || previousPixelFloat.y >= (float)dimensions.y)
    {
        return false;
    }

    const float previousLinearDepth = length(delta);
    screenSpaceMotion = float3(previousPixelFloat - (float2(pixel) + 0.5), previousLinearDepth - currentSurface.worldPositionAndViewDepth.w);
    return all(screenSpaceMotion == screenSpaceMotion);
}

bool PathTraceCleanRoomLoadMotion(uint2 pixel, PathTracePrimarySurfaceRecord currentSurface, uint2 dimensions, out float3 screenSpaceMotion, out bool cameraReprojected)
{
    screenSpaceMotion = float3(0.0, 0.0, 0.0);
    cameraReprojected = false;
    if (pixel.x >= CleanRtxdiDiWidth || pixel.y >= CleanRtxdiDiHeight)
    {
        return false;
    }

    const uint motionMask = PathTraceMotionVectorMask[pixel];
    if ((motionMask & PT_MOTION_VECTOR_MASK_VALID) != 0u)
    {
        screenSpaceMotion = PathTraceMotionVectors[pixel].xyz;
        return all(screenSpaceMotion == screenSpaceMotion);
    }

    cameraReprojected = PathTraceCleanRoomProjectCameraMotion(currentSurface, pixel, dimensions, screenSpaceMotion);
    return cameraReprojected;
}

PathTraceCleanRtxdiDiTemporalResult PathTraceCleanRoomRunTemporalProducer(uint2 pixel, uint2 dimensions, RTXDI_DIReservoir currentReservoir, PathTracePrimarySurfaceRecord currentSurface)
{
    PathTraceCleanRtxdiDiTemporalResult result = (PathTraceCleanRtxdiDiTemporalResult)0;
    result.reservoir = currentReservoir;
    result.previousPixel = int2(-1, -1);
    result.temporalSamplePixel = int2(-1, -1);

    if (RTXDI_IsValidDIReservoir(currentReservoir))
    {
        result.flags |= CLEAN_TEMPORAL_DIAG_CURRENT_VALID;
    }
    if (currentReservoir.M > 0.0)
    {
        result.flags |= CLEAN_TEMPORAL_DIAG_CURRENT_CANDIDATE;
    }
    if ((CleanRtxdiDiTemporalFlags & CLEAN_TEMPORAL_FLAG_ENABLE) != 0u)
    {
        result.flags |= CLEAN_TEMPORAL_DIAG_ENABLED;
    }
    if ((CleanRtxdiDiTemporalFlags & CLEAN_TEMPORAL_FLAG_PREVIOUS_VALID) != 0u)
    {
        result.flags |= CLEAN_TEMPORAL_DIAG_PREVIOUS_FRAME_VALID;
    }
    if (currentReservoir.M <= 0.0 || !PathTraceCleanRoomTemporalReuseEnabled())
    {
        return result;
    }

    const RAB_Surface surface = PathTraceCleanRoomSurfaceFromRecord(currentSurface);
    if (RAB_IsSurfaceValid(surface))
    {
        result.flags |= CLEAN_TEMPORAL_DIAG_CURRENT_SURFACE_VALID;
    }
    else
    {
        return result;
    }

    float3 screenSpaceMotion;
    bool cameraReprojected = false;
    if (!PathTraceCleanRoomLoadMotion(pixel, currentSurface, dimensions, screenSpaceMotion, cameraReprojected))
    {
        return result;
    }
    result.flags |= CLEAN_TEMPORAL_DIAG_MOTION_VALID;
    if (cameraReprojected)
    {
        result.flags |= CLEAN_TEMPORAL_DIAG_CAMERA_REPROJECTED;
    }

    const int2 previousPixel = int2(round(float2(pixel) + screenSpaceMotion.xy));
    result.previousPixel = previousPixel;
    PathTracePrimarySurfaceRecord previousSurfaceRecord;
    if (PathTraceCleanRoomLoadSurfaceRecordSigned(previousPixel, dimensions, true, previousSurfaceRecord))
    {
        result.flags |= CLEAN_TEMPORAL_DIAG_PREVIOUS_SURFACE_VALID;
    }

    if (previousPixel.x >= 0 && previousPixel.y >= 0 && (uint)previousPixel.x < dimensions.x && (uint)previousPixel.y < dimensions.y)
    {
        const RTXDI_DIReservoir previousReservoir = RTXDI_LoadDIReservoir(PathTraceCleanRoomReservoirParams(dimensions), (uint2)previousPixel, 0u);
        if (RTXDI_IsValidDIReservoir(previousReservoir))
        {
            result.flags |= CLEAN_TEMPORAL_DIAG_PREVIOUS_RESERVOIR_VALID;
            result.previousM = previousReservoir.M;
            result.previousTargetPdf = previousReservoir.targetPdf;
            result.previousInvPdf = RTXDI_GetDIReservoirInvPdf(previousReservoir);
            const int mappedPreviousLightIndex = RAB_TranslateLightIndex(RTXDI_GetDIReservoirLightIndex(previousReservoir), false);
            if (mappedPreviousLightIndex >= 0)
            {
                result.flags |= CLEAN_TEMPORAL_DIAG_PREVIOUS_LIGHT_MAPPED;

                const RAB_LightInfo mappedPreviousLight = RAB_LoadLightInfo((uint)mappedPreviousLightIndex, false);
                const RAB_LightSample mappedPreviousSample = RAB_SamplePolymorphicLight(
                    mappedPreviousLight, surface, RTXDI_GetDIReservoirSampleUV(previousReservoir));
                result.previousTargetAtCurrentPdf = RAB_GetLightSampleTargetPdfForSurface(mappedPreviousSample, surface);
                if (result.previousTargetAtCurrentPdf > 1.0e-8)
                {
                    result.flags |= CLEAN_TEMPORAL_DIAG_PREVIOUS_TARGET_AT_CURRENT;
                }
            }
        }
    }

    RTXDI_RuntimeParameters runtimeParams = (RTXDI_RuntimeParameters)0;
    runtimeParams.activeCheckerboardField = 0u;
    runtimeParams.frameIndex = CleanRtxdiDiFrameIndex;

    RTXDI_DITemporalResamplingParameters temporalParams = (RTXDI_DITemporalResamplingParameters)0;
    temporalParams.maxHistoryLength = (uint)clamp(CleanRtxdiDiRestirPTSurfaceInfo.y, 0.0, 64.0);
    temporalParams.biasCorrectionMode = (uint)clamp(CleanRtxdiDiRestirPTSurfaceInfo.w, 0.0, 1.0);
    temporalParams.depthThreshold = 0.08;
    temporalParams.normalThreshold = 0.80;
    temporalParams.enableVisibilityShortcut = 0u;
    temporalParams.enablePermutationSampling = 0u;
    temporalParams.uniformRandomNumber = PathTraceCleanRoomHash(CleanRtxdiDiFrameIndex ^ 0x711ad151u);
    temporalParams.permutationSamplingThreshold = 0.0;

    RTXDI_RandomSamplerState rng = RTXDI_InitRandomSampler(pixel, CleanRtxdiDiFrameIndex, 0x52525805u);
    int2 temporalSamplePixel = int2(-1, -1);
    RAB_LightSample selectedLightSample = RAB_EmptyLightSample();
    result.flags |= CLEAN_TEMPORAL_DIAG_SDK_CALLED;
    const RTXDI_DIReservoir temporalReservoir = RTXDI_DITemporalResampling(
        pixel,
        surface,
        currentReservoir,
        rng,
        runtimeParams,
        PathTraceCleanRoomReservoirParams(dimensions),
        screenSpaceMotion,
        0u,
        temporalParams,
        temporalSamplePixel,
        selectedLightSample);

    result.temporalSamplePixel = temporalSamplePixel;
    if (RTXDI_IsValidDIReservoir(temporalReservoir))
    {
        result.flags |= CLEAN_TEMPORAL_DIAG_TEMPORAL_RESERVOIR_VALID;
        result.reservoir = temporalReservoir;
        result.temporalTargetPdf = temporalReservoir.targetPdf;
        result.temporalInvPdf = RTXDI_GetDIReservoirInvPdf(temporalReservoir);
        if (RAB_IsReplayableLightSample(selectedLightSample))
        {
            result.flags |= CLEAN_TEMPORAL_DIAG_SDK_SELECTED_PREVIOUS_SAMPLE;
        }
        if (RTXDI_GetDIReservoirLightIndex(temporalReservoir) != RTXDI_GetDIReservoirLightIndex(currentReservoir) ||
            any(abs(RTXDI_GetDIReservoirSampleUV(temporalReservoir) - RTXDI_GetDIReservoirSampleUV(currentReservoir)) > 1.0e-6))
        {
            result.flags |= CLEAN_TEMPORAL_DIAG_TEMPORAL_OUTPUT_CHANGED;
        }
        if (temporalReservoir.M > currentReservoir.M)
        {
            result.flags |= CLEAN_TEMPORAL_DIAG_SDK_REUSED_PREVIOUS;
        }
    }
    return result;
}

float3 PathTraceCleanRoomTemporalGateColor(uint flags, uint flag)
{
    return (flags & flag) == flag ? float3(0.0, 0.90, 0.20) : float3(0.95, 0.04, 0.10);
}

float3 PathTraceCleanRoomTemporalDiagnosticColor(PathTraceCleanRtxdiDiInitialResult initial, PathTraceCleanRtxdiDiTemporalResult temporal, uint2 pixel, uint2 dimensions)
{
    if (initial.status != CLEAN_INITIAL_STATUS_VALID && initial.status != CLEAN_INITIAL_STATUS_ZERO_TARGET_PDF)
    {
        return PathTraceCleanRoomStatusColor(initial.status);
    }

    const uint bandCount = 8u;
    const bool forcedBand = CleanRtxdiDiView8Band < bandCount;
    const uint band = forcedBand ? CleanRtxdiDiView8Band : min((pixel.y * bandCount) / max(dimensions.y, 1u), bandCount - 1u);
    if (band == 0u)
    {
        float3 gate = PathTraceCleanRoomTemporalGateColor(temporal.flags, CLEAN_TEMPORAL_DIAG_ENABLED | CLEAN_TEMPORAL_DIAG_PREVIOUS_FRAME_VALID);
        gate.b = PathTraceCleanRoomLog01((float)CleanRtxdiDiHistoryResetCount, 8.0);
        if (PathTraceCleanReferenceRabEnabled())
        {
            gate.r = 0.85;
        }
        return gate;
    }
    if (band == 1u)
    {
        return PathTraceCleanRoomTemporalGateColor(temporal.flags, CLEAN_TEMPORAL_DIAG_CURRENT_CANDIDATE | CLEAN_TEMPORAL_DIAG_CURRENT_SURFACE_VALID);
    }
    if (band == 2u)
    {
        return PathTraceCleanRoomTemporalGateColor(temporal.flags, CLEAN_TEMPORAL_DIAG_MOTION_VALID);
    }
    if (band == 3u)
    {
        return PathTraceCleanRoomTemporalGateColor(temporal.flags, CLEAN_TEMPORAL_DIAG_PREVIOUS_SURFACE_VALID);
    }
    if (band == 4u)
    {
        float3 gate = PathTraceCleanRoomTemporalGateColor(temporal.flags, CLEAN_TEMPORAL_DIAG_PREVIOUS_RESERVOIR_VALID);
        gate.b = PathTraceCleanRoomLog01(temporal.previousM, 5.0);
        return gate;
    }
    if (band == 5u)
    {
        if (pixel.x < (dimensions.x >> 1u))
        {
            return PathTraceCleanRoomTemporalGateColor(temporal.flags, CLEAN_TEMPORAL_DIAG_PREVIOUS_LIGHT_MAPPED);
        }

        float3 gate = PathTraceCleanRoomTemporalGateColor(temporal.flags, CLEAN_TEMPORAL_DIAG_PREVIOUS_TARGET_AT_CURRENT);
        gate.b = PathTraceCleanRoomLog01(temporal.previousTargetAtCurrentPdf, 6.0);
        return gate;
    }
    if (band == 6u)
    {
        return pixel.x < (dimensions.x >> 1u)
            ? PathTraceCleanRoomTargetInvPdfColor(initial.targetPdf, RTXDI_GetDIReservoirInvPdf(initial.reservoir))
            : PathTraceCleanRoomTargetInvPdfColor(temporal.temporalTargetPdf, temporal.temporalInvPdf);
    }

    if (pixel.x < (dimensions.x >> 1u))
    {
        const float currentLum = RAB_Luminance(PathTraceCleanRoomFlatDiffuseResolveReservoir(initial.surface, initial.reservoir));
        return float3(PathTraceCleanRoomLog01(currentLum, 12.0), PathTraceCleanRoomLog01(initial.reservoir.M, 5.0), 0.05);
    }

    // SDK's out selectedLightSample is populated by RTXDI when the previous reservoir sample wins the temporal combine.
    float3 gate = PathTraceCleanRoomTemporalGateColor(temporal.flags, CLEAN_TEMPORAL_DIAG_SDK_SELECTED_PREVIOUS_SAMPLE);
    gate.b = PathTraceCleanRoomLog01(max(temporal.reservoir.M - initial.reservoir.M, 0.0), 5.0);
    return gate;
}

float3 PathTraceCleanRoomInitialReservoirOutput(uint2 pixel, uint2 dimensions, uint view)
{
    const uint reservoirIndex = PathTraceCleanRoomReservoirIndex(pixel, dimensions);
    PathTraceCleanRtxdiDiInitialResult result = PathTraceCleanRoomRunSelectedInitialProducer(pixel, dimensions);
    if (reservoirIndex < CleanRtxdiDiReservoirCount)
    {
        if (!PathTraceCleanRoomExternalPdfNeeCurrentEnabled())
        {
            CleanRtxdiDiCurrentReservoirs[reservoirIndex] = RTXDI_PackDIReservoir(result.reservoir);
        }
        CleanRtxdiDiTemporalReservoirs[reservoirIndex] = RTXDI_PackDIReservoir(result.reservoir);
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

float3 PathTraceCleanRoomTemporalReservoirOutput(uint2 pixel, uint2 dimensions, uint view)
{
    const uint reservoirIndex = PathTraceCleanRoomReservoirIndex(pixel, dimensions);
    PathTraceCleanRtxdiDiInitialResult initial = PathTraceCleanRoomRunSelectedInitialProducer(pixel, dimensions);
    if (reservoirIndex < CleanRtxdiDiReservoirCount)
    {
        if (!PathTraceCleanRoomExternalPdfNeeCurrentEnabled())
        {
            CleanRtxdiDiCurrentReservoirs[reservoirIndex] = RTXDI_PackDIReservoir(initial.reservoir);
        }
    }

    const bool temporalRequested = view == 5u || view == 6u || view == 8u || view == 9u || view == 10u || view == 11u || view == 12u;
    const bool temporalAllowed = temporalRequested && (CleanRtxdiDiTemporalFlags & CLEAN_TEMPORAL_FLAG_ENABLE) != 0u;
    RTXDI_DIReservoir temporalReservoir = initial.reservoir;
    PathTraceCleanRtxdiDiTemporalResult temporal = (PathTraceCleanRtxdiDiTemporalResult)0;
    temporal.reservoir = initial.reservoir;
    temporal.previousPixel = int2(-1, -1);
    temporal.temporalSamplePixel = int2(-1, -1);
    if (temporalAllowed)
    {
        temporal = PathTraceCleanRoomRunTemporalProducer(pixel, dimensions, initial.reservoir, initial.surface);
        temporalReservoir = temporal.reservoir;
    }

    if (reservoirIndex < CleanRtxdiDiReservoirCount)
    {
        CleanRtxdiDiTemporalReservoirs[reservoirIndex] = RTXDI_PackDIReservoir(temporalReservoir);
    }

    if (view == 6u)
    {
        const float3 currentColor = PathTraceCleanRoomFlatDiffuseResolve(initial);
        const bool temporalSdkCalled = !temporalAllowed || (temporal.flags & CLEAN_TEMPORAL_DIAG_SDK_CALLED) != 0u;
        const bool temporalAcceptedPrevious = !temporalAllowed || (temporal.flags & CLEAN_TEMPORAL_DIAG_SDK_REUSED_PREVIOUS) != 0u;
        const bool temporalRenderable = (initial.status == CLEAN_INITIAL_STATUS_VALID || initial.status == CLEAN_INITIAL_STATUS_ZERO_TARGET_PDF) &&
            RTXDI_IsValidDIReservoir(temporalReservoir);
        const float3 temporalColor = temporalSdkCalled && temporalAcceptedPrevious
            ? (temporalRenderable
                ? PathTraceCleanRoomFlatDiffuseResolveReservoir(initial.surface, temporalReservoir)
                : (initial.status == CLEAN_INITIAL_STATUS_ZERO_TARGET_PDF ? float3(0.0, 0.0, 0.0) : PathTraceCleanRoomStatusColor(initial.status)))
            : float3(0.95, 0.04, 0.10);
        return pixel.x < (dimensions.x >> 1u) ? currentColor : temporalColor;
    }

    if (view == 8u)
    {
        return PathTraceCleanRoomTemporalDiagnosticColor(initial, temporal, pixel, dimensions);
    }

    if (view == 9u || view == 10u || view == 11u || view == 12u)
    {
        if (!RTXDI_IsValidDIReservoir(temporalReservoir))
        {
            return float3(0.0, 0.0, 0.0);
        }
        return PathTraceCleanRoomFlatDiffuseResolveReservoir(initial.surface, temporalReservoir);
    }

    if (view == 5u)
    {
        if (temporalAllowed && (temporal.flags & CLEAN_TEMPORAL_DIAG_SDK_CALLED) == 0u)
        {
            return float3(0.95, 0.04, 0.10);
        }
        if (temporalAllowed && (temporal.flags & CLEAN_TEMPORAL_DIAG_SDK_REUSED_PREVIOUS) == 0u)
        {
            return float3(0.95, 0.04, 0.10);
        }
        if (initial.status != CLEAN_INITIAL_STATUS_VALID &&
            !(initial.status == CLEAN_INITIAL_STATUS_ZERO_TARGET_PDF && RTXDI_IsValidDIReservoir(temporalReservoir)))
        {
            return initial.status == CLEAN_INITIAL_STATUS_ZERO_TARGET_PDF ? float3(0.0, 0.0, 0.0) : PathTraceCleanRoomStatusColor(initial.status);
        }
        return PathTraceCleanRoomFlatDiffuseResolveReservoir(initial.surface, temporalReservoir);
    }

    const bool reusedPrevious = (temporal.flags & CLEAN_TEMPORAL_DIAG_SDK_REUSED_PREVIOUS) != 0u;
    const float3 base = reusedPrevious ? float3(0.0, 0.85, 0.18) : float3(0.20, 0.28, 0.95);
    return RTXDI_IsValidDIReservoir(temporalReservoir)
        ? lerp(base, float3(saturate(temporalReservoir.M / 16.0), 0.95, 0.25), 0.45)
        : float3(0.95, 0.05, 0.12);
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

    const uint view = CleanRtxdiDiView;
    if (view < 1u || view > 15u)
    {
        SmokeOutput[pixel] = float4(1.0, 0.0, 1.0, 1.0);
        return;
    }

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
    else if (view == 4u || view == 7u || (view == 8u && (CleanRtxdiDiTemporalFlags & CLEAN_TEMPORAL_FLAG_ENABLE) == 0u))
    {
        color = PathTraceCleanRoomInitialReservoirOutput(pixel, dimensions, view);
    }
    else if (view == 13u)
    {
        color = PathTraceCleanRoomRealAnalyticOneSampleDiagnosticColor(pixel, dimensions);
    }
    else if (view == 14u)
    {
        color = PathTraceCleanRoomRealAnalyticTargetFactorDiagnosticColor(pixel, dimensions);
    }
    else if (view == 15u)
    {
        color = PathTraceCleanRoomRealAnalyticBinaryGateDiagnosticColor(pixel, dimensions);
    }
    else if (view == 5u || view == 6u || view == 8u || view == 9u || view == 10u || view == 11u || view == 12u)
    {
        color = PathTraceCleanRoomTemporalReservoirOutput(pixel, dimensions, view);
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
