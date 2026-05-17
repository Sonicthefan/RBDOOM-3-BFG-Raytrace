#include "../../vulkan.hlsli"
#ifndef __cplusplus
#ifndef uint16_t
#define uint16_t uint
#endif
#endif

#define RB_PT_ENABLE_RESTIR 1
#include "Rtxdi/PT/ReSTIRPTParameters.h"

struct PathTraceSmokePayload
{
    uint value;
};

struct PathTraceSmokeShadowPayload
{
    uint hit;
    uint rayMode;
    uint ignoreInstanceId;
    uint ignorePrimitiveIndex;
    uint ignoreMaterialId;
};

struct PathTraceSmokeEmissiveTriangle
{
    float4 centerAndArea;
    float4 normalAndLuminance;
    float4 uvBounds;
    float4 centroidUvAndWeight;
    float4 estimatedRadianceAndLuminance;
    float4 sampleWeightAndPdf;
    uint materialIndex;
    uint instanceId;
    uint primitiveIndex;
    uint flags;
    uint emissiveTextureIndex;
    uint emissiveTextureWidth;
    uint emissiveTextureHeight;
    uint materialId;
    uint universeMaterialIndex;
    uint identityHashLo;
    uint identityHashHi;
    uint padding0;
};

struct PathTraceEmissiveLightRemap
{
    int previousToCurrentIndex;
    int currentToPreviousIndex;
    uint flags;
    uint padding0;
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

#include "PathTracePrimarySurface.hlsli"

VK_IMAGE_FORMAT("rgba32f") RWTexture2D<float4> SmokeOutput : register(u1);
VK_IMAGE_FORMAT("rgba16f") RWTexture2D<float4> PathTraceMotionVectors : register(u39);
VK_IMAGE_FORMAT("r32ui") RWTexture2D<uint> PathTraceMotionVectorMask : register(u40);
VK_IMAGE_FORMAT("rgba32f") RWTexture2D<float4> RestirPTReflectionOutput : register(u47);
VK_IMAGE_FORMAT("rgba16f") RWTexture2D<float4> PathTraceRRGuideAlbedo : register(u48);
VK_IMAGE_FORMAT("rgba16f") RWTexture2D<float4> PathTraceRRGuideNormalRoughness : register(u49);
VK_IMAGE_FORMAT("r32f") RWTexture2D<float> PathTraceRRGuideDepth : register(u50);
VK_IMAGE_FORMAT("r32f") RWTexture2D<float> PathTraceRRGuideHitDistance : register(u51);
VK_IMAGE_FORMAT("r32ui") RWTexture2D<uint> PathTraceRRGuideResetMask : register(u52);
VK_IMAGE_FORMAT("rgba16f") RWTexture2D<float4> PathTraceRRGuideSpecularAlbedo : register(u53);
VK_IMAGE_FORMAT("rgba32f") RWTexture2D<float4> PathTraceRRInputColor : register(u54);
RaytracingAccelerationStructure SmokeScene : register(t0);
StructuredBuffer<PathTraceSmokeEmissiveTriangle> SmokeEmissiveTriangles : register(t16);
StructuredBuffer<PathTraceSmokeEmissiveTriangle> SmokePreviousEmissiveTriangles : register(t57);
StructuredBuffer<PathTraceEmissiveLightRemap> SmokeEmissiveRemap : register(t58);
StructuredBuffer<PathTraceDoomAnalyticLightCandidate> DoomAnalyticLights : register(t27);
StructuredBuffer<PathTraceDoomAnalyticLightCandidateIdentity> DoomAnalyticCurrentIdentities : register(t42);
StructuredBuffer<PathTraceDoomAnalyticLightCandidateIdentity> DoomAnalyticPreviousIdentities : register(t43);
StructuredBuffer<PathTraceDoomAnalyticLightRemap> DoomAnalyticRemap : register(t44);
StructuredBuffer<PathTraceDoomAnalyticLightCandidate> DoomAnalyticPreviousLights : register(t45);
ConstantBuffer<RTXDI_PTParameters> RestirPTParams : register(b28);
RWStructuredBuffer<RTXDI_PackedPTReservoir> RestirPTReservoirs : register(u29);
RWStructuredBuffer<PathTracePrimarySurfaceRecord> PrimarySurfaceHistoryCurrent : register(u30);
RWStructuredBuffer<PathTracePrimarySurfaceRecord> PrimarySurfaceHistoryPrevious : register(u31);
RWStructuredBuffer<RTXDI_PackedPTReservoir> RestirPTGiReservoirs : register(u55);
RWStructuredBuffer<RTXDI_PackedPTReservoir> RestirPTDiReservoirs : register(u56);

#define RTXDI_PT_RESERVOIR_BUFFER RestirPTReservoirs
#include "Rtxdi/PT/Reservoir.hlsli"

cbuffer PathTraceSmokeConstants : register(b2)
{
    float4 CameraOriginAndTMax;
    float4 CameraForwardAndTanX;
    float4 CameraLeftAndTanY;
    float4 CameraUpAndDebugMode;
    float4 TextureInfo;
    float4 LightOriginAndRadius[32];
    float4 LightColorAndIntensity[32];
    float4 LightInfo;
    float4 PortalWindowInfo;
    float4 LightSpriteInfo;
    float4 ToyPathInfo;
    float4 EmissiveInfo;
    float4 EmissiveDistributionInfo;
    float4 BoundsOverlayInfo;
    float4 DoomAnalyticLightInfo;
    float4 DoomAnalyticLightRemapInfo;
    float4 RestirPTInfo;
    float4 IntegratorInfo;
    float4 IntegratorInfo2;
    float4 PrevCameraOriginAndValid;
    float4 PrevCameraForwardAndTanX;
    float4 PrevCameraLeftAndTanY;
    float4 PrevCameraUpAndTanY;
    float4 SafetyInfo;
    float4 GeometryInfo0;
    float4 GeometryInfo1;
    float4 GeometryInfo2;
    float4 GeometryInfo3;
    float4 GeometryInfo4;
    float4 DispatchTileInfo;
    float4 NeeInfo;
    float4 MotionVectorInfo;
    float4 RestirPTDirectInfo;
    float4 RestirPTSparsityInfo;
    float4 RestirPTIndirectInfo;
    float4 RayReconstructionInfo;
    float4 RestirPTDiDebugInfo;
    float4 RestirPTGiDebugInfo;
};

static const uint RT_SMOKE_SURFACE_CLASS_RIGID_ENTITY = 1u;
static const uint RT_SMOKE_SURFACE_CLASS_SKINNED_DEFORMED = 2u;
static const uint RT_SMOKE_SURFACE_CLASS_TRANSLUCENT = 3u;
static const uint RT_SMOKE_TRANSLUCENT_SUBTYPE_SHIFT = 24u;
static const uint RT_SMOKE_TRANSLUCENT_SUBTYPE_MASK = 0x0f000000u;
static const uint RT_SMOKE_TRANSLUCENT_SUBTYPE_OBJECT_GLASS = 1u;
static const uint RT_SMOKE_TRANSLUCENT_SUBTYPE_SMOKE_PARTICLE = 2u;
static const uint RT_SMOKE_TRANSLUCENT_SUBTYPE_PORTAL_WINDOW = 4u;
static const uint RT_SMOKE_TRANSLUCENT_SUBTYPE_GUI_SCREEN = 5u;
static const uint RT_SMOKE_EMISSIVE_TRIANGLE_HISTORY_DYNAMIC = 0x00020000u;
static const uint RT_SMOKE_TEXTURE_FLAG_RESERVOIR_TWO_SIDED_EMISSIVES = 0x00000040u;
static const uint RT_DOOM_ANALYTIC_LIGHT_CASTS_SHADOWS = 0x00000001u;
static const uint RT_PT_SAFETY_DISABLE_ANY_HIT_ALPHA = 0x00000001u;
static const uint RT_PT_SAFETY_DISABLE_SELECTED_LIGHT_LOOP = 0x00000002u;
static const uint RT_PT_SAFETY_DISABLE_ANALYTIC_LIGHT_LOOP = 0x00000004u;
static const uint RT_PT_SAFETY_DISABLE_EMISSIVE_TRIANGLE_SAMPLING = 0x00000008u;
static const uint RT_PT_SAFETY_DISABLE_DIFFUSE_SECONDARY_RAY = 0x00000010u;
static const uint RT_PT_SAFETY_DISABLE_REFLECTION_RAY = 0x00000020u;
static const uint RT_PT_SAFETY_DISABLE_PRIMARY_SURFACE_HISTORY = 0x00000040u;
static const uint RT_PT_SAFETY_DISABLE_RESERVOIR_WRITES = 0x00000080u;
static const uint RT_PT_SAFETY_DISABLE_RESTIR_VISIBILITY_RAY = 0x00000100u;
static const uint PT_MOTION_VECTOR_MASK_VALID = 0x00000001u;
static const uint PT_MOTION_VECTOR_MASK_SOURCE_SHIFT = 1u;
static const uint PT_MOTION_VECTOR_MASK_SOURCE_MASK = 0x0000001eu;
static const uint PT_MOTION_VECTOR_SOURCE_STATIC = 1u;
static const uint PT_MOTION_VECTOR_SOURCE_SKINNED = 2u;
static const uint PT_MOTION_VECTOR_SOURCE_RIGID = 3u;
static const uint RT_RR_RESET_INVALID_SURFACE = 0x00000001u;
static const uint RT_RR_RESET_MISSING_PREVIOUS = 0x00000002u;
static const uint RT_RR_RESET_REJECTED_PREVIOUS = 0x00000004u;
static const uint RT_RR_RESET_MATERIAL_MISMATCH = 0x00000008u;
static const uint RT_RR_RESET_OBJECT_MOTION_UNAVAILABLE = 0x00000010u;
static const uint RT_RR_RESET_STOCHASTIC_TRANSLUCENT = 0x00000020u;
static const uint RT_RR_RESET_OTHER_INVALID = 0x00000040u;

bool PathTraceSafetyDisabled(uint bit)
{
    return (((uint)SafetyInfo.x) & bit) != 0u;
}

uint PathTracePrimarySurfaceHistoryCount()
{
    return (uint)max(GeometryInfo2.z, 0.0);
}

uint2 PathTraceDispatchTileOffset()
{
    return uint2((uint)max(DispatchTileInfo.x, 0.0), (uint)max(DispatchTileInfo.y, 0.0));
}

uint2 PathTraceFullOutputSize()
{
    const uint2 size = uint2((uint)max(DispatchTileInfo.z, 0.0), (uint)max(DispatchTileInfo.w, 0.0));
    return (size.x > 0u && size.y > 0u) ? size : DispatchRaysDimensions().xy;
}

uint2 PathTraceRestirDirectSize()
{
    const uint2 size = uint2((uint)max(RestirPTDirectInfo.x, 0.0), (uint)max(RestirPTDirectInfo.y, 0.0));
    return (size.x > 0u && size.y > 0u) ? size : PathTraceFullOutputSize();
}

uint PathTraceRestirSparsityRate()
{
    return clamp((uint)max(RestirPTSparsityInfo.x, 1.0), 1u, 8u);
}

bool PathTraceRestirSparsityEnabled()
{
    return RestirPTSparsityInfo.z >= 0.5 && PathTraceRestirSparsityRate() > 1u;
}

uint PathTraceRestirSparsityPhase(bool previousFrame)
{
    const uint rate = PathTraceRestirSparsityRate();
    const float phaseValue = previousFrame ? RestirPTSparsityInfo.w : RestirPTSparsityInfo.y;
    return rate > 1u ? (uint)max(phaseValue, 0.0) % rate : 0u;
}

uint2 PathTraceRestirSparseRepresentativePixel(uint2 pixel, bool previousFrame)
{
    const uint2 dimensions = max(PathTraceRestirDirectSize(), uint2(1u, 1u));
    pixel = min(pixel, dimensions - uint2(1u, 1u));
    if (!PathTraceRestirSparsityEnabled())
    {
        return pixel;
    }

    const uint rate = PathTraceRestirSparsityRate();
    const uint phase = PathTraceRestirSparsityPhase(previousFrame);
    const uint residue = (pixel.x + pixel.y * 3u + phase) % rate;
    const int dxBack = -int(residue);
    const int dxForward = int((rate - residue) % rate);
    const int xBack = int(pixel.x) + dxBack;
    const int xForward = int(pixel.x) + dxForward;
    const bool backValid = xBack >= 0;
    const bool forwardValid = xForward < int(dimensions.x);
    int chosenX = int(pixel.x);
    if (backValid && forwardValid)
    {
        chosenX = abs(dxBack) <= abs(dxForward) ? xBack : xForward;
    }
    else if (backValid)
    {
        chosenX = xBack;
    }
    else if (forwardValid)
    {
        chosenX = xForward;
    }
    return uint2(uint(clamp(chosenX, 0, int(dimensions.x) - 1)), pixel.y);
}

uint PathTraceRestirPTIndirectSparsityRate()
{
    return clamp((uint)max(RestirPTIndirectInfo.z, 1.0), 1u, 8u);
}

bool PathTraceRestirPTIndirectSparsityEnabled()
{
    return RestirPTIndirectInfo.y >= 0.5 && PathTraceRestirPTIndirectSparsityRate() > 1u;
}

uint2 PathTraceRestirPTIndirectRepresentativePixel(uint2 pixel)
{
    const uint2 dimensions = max(PathTraceFullOutputSize(), uint2(1u, 1u));
    pixel = min(pixel, dimensions - uint2(1u, 1u));
    if (!PathTraceRestirPTIndirectSparsityEnabled())
    {
        return pixel;
    }

    const uint rate = PathTraceRestirPTIndirectSparsityRate();
    const uint phase = (uint)max(RestirPTIndirectInfo.w, 0.0) % rate;
    const uint residue = (pixel.x + pixel.y * 3u + phase) % rate;
    const int dxBack = -int(residue);
    const int dxForward = int((rate - residue) % rate);
    const int xBack = int(pixel.x) + dxBack;
    const int xForward = int(pixel.x) + dxForward;
    const bool backValid = xBack >= 0;
    const bool forwardValid = xForward < int(dimensions.x);
    int chosenX = int(pixel.x);
    if (backValid && forwardValid)
    {
        chosenX = abs(dxBack) <= abs(dxForward) ? xBack : xForward;
    }
    else if (backValid)
    {
        chosenX = xBack;
    }
    else if (forwardValid)
    {
        chosenX = xForward;
    }
    return uint2(uint(clamp(chosenX, 0, int(dimensions.x) - 1)), pixel.y);
}

uint2 PathTraceFullPixelToRestirDirectPixel(uint2 fullPixel)
{
    const uint2 fullSize = max(PathTraceFullOutputSize(), uint2(1u, 1u));
    const uint2 directSize = max(PathTraceRestirDirectSize(), uint2(1u, 1u));
    const float2 directPixel = floor(((float2(fullPixel) + 0.5) * float2(directSize)) / float2(fullSize));
    return PathTraceRestirSparseRepresentativePixel(min(uint2(directPixel), directSize - uint2(1u, 1u)), false);
}

float2 PathTraceFullPixelFloatToRestirDirectPixelFloat(float2 fullPixelFloat)
{
    const uint2 fullSize = max(PathTraceFullOutputSize(), uint2(1u, 1u));
    const uint2 directSize = max(PathTraceRestirDirectSize(), uint2(1u, 1u));
    return ((fullPixelFloat + 0.5) * float2(directSize)) / float2(fullSize) - 0.5;
}

uint2 PathTraceRestirDirectPixelToRepresentativeFullPixel(uint2 directPixel)
{
    const uint2 fullSize = max(PathTraceFullOutputSize(), uint2(1u, 1u));
    const uint2 directSize = max(PathTraceRestirDirectSize(), uint2(1u, 1u));
    const float2 fullPixel = floor(((float2(directPixel) + 0.5) * float2(fullSize)) / float2(directSize));
    return min(uint2(fullPixel), fullSize - uint2(1u, 1u));
}

uint2 PathTracePrimarySurfaceStorePixel(uint2 pixel)
{
    return pixel;
}

int2 PathTracePrimarySurfaceLoadPixel(int2 pixelPosition, bool previousFrame)
{
    return pixelPosition;
}

float TraceSmokeShadowVisibility(float3 origin, float3 direction, float tMax, uint ignoreInstanceId, uint ignorePrimitiveIndex, uint ignoreMaterialId);
#include "RtxdiBridge/PathTraceRtxdiBridge.hlsli"

float3 SafeNormalize(float3 value, float3 fallback)
{
    return RAB_SafeNormalize(value, fallback);
}

float3 ConstrainSmokeShadingNormal(float3 shadingNormal, float3 geometryNormal)
{
    const float minGeometryDot = 0.02;
    geometryNormal = RAB_SafeNormalize(geometryNormal, float3(0.0, 0.0, 1.0));
    shadingNormal = RAB_SafeNormalize(shadingNormal, geometryNormal);
    const float geometryDot = dot(shadingNormal, geometryNormal);
    if (geometryDot >= minGeometryDot)
    {
        return shadingNormal;
    }
    float3 tangentComponent = shadingNormal - geometryNormal * geometryDot;
    const float tangentLengthSquared = dot(tangentComponent, tangentComponent);
    if (tangentLengthSquared <= 1e-8)
    {
        return geometryNormal;
    }
    tangentComponent *= rsqrt(tangentLengthSquared);
    const float tangentScale = sqrt(max(1.0 - minGeometryDot * minGeometryDot, 0.0));
    return RAB_SafeNormalize(tangentComponent * tangentScale + geometryNormal * minGeometryDot, geometryNormal);
}

#define RB_PATH_TRACE_PRIMARY_SURFACE_ENABLE_HELPERS
#include "PathTracePrimarySurface.hlsli"

float3 RestirPTToneMapPreview(float3 contribution);
float3 RestirPTReferenceFinalShadingContribution(RTXDI_PTReservoir reservoir);
bool RestirPTReferenceFinalShadingHasUsefulSample(RTXDI_PTReservoir reservoir);
float4 RestirPTReferenceFinalShadingStateColor(RTXDI_PTReservoir reservoir);
#include "pathtrace_restir_combined_resolve_reservoir_pages.hlsli"

float3 RestirPTSanitizePreviewContribution(float3 contribution)
{
    if (!all(contribution == contribution) || any(abs(contribution) > 65504.0))
    {
        return float3(0.0, 0.0, 0.0);
    }
    return max(contribution, float3(0.0, 0.0, 0.0));
}

float3 RestirPTToneMapPreview(float3 contribution)
{
    contribution = RestirPTSanitizePreviewContribution(contribution * max(RestirPTInfo.w, 0.0));
    return contribution / (float3(1.0, 1.0, 1.0) + contribution);
}

float3 RestirPTSanitizeHdrRadiance(float3 radiance)
{
    return RestirPTSanitizePreviewContribution(radiance);
}

struct RestirPTCombinedLighting
{
    float3 preview;
    float3 hdrRadiance;
};

float3 RestirPTVisibleSurfaceBase(RAB_Surface surface)
{
    if (!RAB_IsSurfaceValid(surface))
    {
        return float3(0.0, 0.0, 0.0);
    }
    const float3 visibleEmissive = RestirPTToneMapPreview(surface.material.emissiveRadiance);
    if (surface.surfaceClass == RT_SMOKE_SURFACE_CLASS_TRANSLUCENT &&
        ((surface.flags & RT_SMOKE_TRANSLUCENT_SUBTYPE_MASK) >> RT_SMOKE_TRANSLUCENT_SUBTYPE_SHIFT) == RT_SMOKE_TRANSLUCENT_SUBTYPE_GUI_SCREEN)
    {
        const float3 albedo = saturate(max(surface.material.diffuseAlbedo, float3(0.0, 0.0, 0.0)));
        return saturate(albedo + visibleEmissive);
    }
    return visibleEmissive;
}

float3 RestirPTReservoirPreviewContribution(RTXDI_PTReservoir reservoir)
{
    return RestirPTSanitizePreviewContribution(reservoir.TargetFunction * max(reservoir.WeightSum, 0.0));
}

bool RestirPTReservoirHasUsefulSample(RTXDI_PTReservoir reservoir)
{
    const float targetLuminance = RTXDI_Luminance(max(reservoir.TargetFunction, float3(0.0, 0.0, 0.0)));
    return RTXDI_IsValidPTReservoir(reservoir) && reservoir.WeightSum > 0.0 && targetLuminance > 0.0;
}

uint RestirPTGiDebugView()
{
    return clamp((uint)max(RestirPTGiDebugInfo.x, 0.0), 0u, 4u);
}

uint RestirPTDiDebugView()
{
    return clamp((uint)max(RestirPTDiDebugInfo.x, 0.0), 0u, 35u);
}

bool RestirPTDiTemporalPrepassEnabled()
{
    return RestirPTDiDebugInfo.y >= 0.5;
}

uint RestirPTGiTemporalCurrentBufferIndex()
{
    return ((uint)max(RestirPTInfo.x, 0.0)) & 1u;
}

uint RestirPTGiTemporalPreviousBufferIndex()
{
    return 1u - RestirPTGiTemporalCurrentBufferIndex();
}

float RestirPTGiHash01(uint2 pixel, uint salt)
{
    uint h = pixel.x * 1664525u + pixel.y * 1013904223u + ((uint)max(RestirPTInfo.x, 0.0)) * 747796405u + salt;
    h ^= h >> 16;
    h *= 2246822519u;
    h ^= h >> 13;
    h *= 3266489917u;
    h ^= h >> 16;
    return ((float)(h & 0x00ffffffu) + 0.5) / 16777216.0;
}

uint RestirPTGiReservoirPointer(uint2 pixel, uint reservoirArrayIndex)
{
    const uint2 reservoirPosition = RTXDI_PixelPosToReservoirPos(pixel, 0u);
    return RTXDI_ReservoirPositionToPointer(RestirPTParams.reservoirBuffer, reservoirPosition, reservoirArrayIndex);
}

RTXDI_PTReservoir LoadRestirPTGiTemporalReservoir(uint2 pixel, uint reservoirArrayIndex)
{
    return RTXDI_UnpackPTReservoir(RestirPTGiReservoirs[RestirPTGiReservoirPointer(pixel, reservoirArrayIndex)]);
}

void StoreRestirPTGiTemporalReservoir(uint2 pixel, uint reservoirArrayIndex, RTXDI_PTReservoir reservoir)
{
    RestirPTGiReservoirs[RestirPTGiReservoirPointer(pixel, reservoirArrayIndex)] = RTXDI_PackPTReservoir(reservoir);
}

RTXDI_PTReservoir LoadRestirPTDiTemporalReservoir(uint2 pixel, uint reservoirArrayIndex)
{
    return RTXDI_UnpackPTReservoir(RestirPTDiReservoirs[RestirPTGiReservoirPointer(pixel, reservoirArrayIndex)]);
}

void StoreRestirPTDiTemporalReservoir(uint2 pixel, uint reservoirArrayIndex, RTXDI_PTReservoir reservoir)
{
    RestirPTDiReservoirs[RestirPTGiReservoirPointer(pixel, reservoirArrayIndex)] = RTXDI_PackPTReservoir(reservoir);
}

static const uint RESTIR_PT_TEMPORAL_UNSAFE_RESET_MASK =
    RT_RR_RESET_INVALID_SURFACE |
    RT_RR_RESET_MISSING_PREVIOUS |
    RT_RR_RESET_REJECTED_PREVIOUS |
    RT_RR_RESET_MATERIAL_MISMATCH |
    RT_RR_RESET_OBJECT_MOTION_UNAVAILABLE |
    RT_RR_RESET_STOCHASTIC_TRANSLUCENT |
    RT_RR_RESET_OTHER_INVALID;

bool RestirPTTryProjectMotionVectorToPreviousPixel(uint2 pixel, out int2 previousPixel, out float2 motionPixels)
{
    previousPixel = int2(-1, -1);
    motionPixels = float2(0.0, 0.0);

    const uint motionMask = PathTraceMotionVectorMask[pixel];
    if ((motionMask & PT_MOTION_VECTOR_MASK_VALID) == 0u)
    {
        return false;
    }

    const uint resetMask = PathTraceRRGuideResetMask[pixel];
    if ((resetMask & RESTIR_PT_TEMPORAL_UNSAFE_RESET_MASK) != 0u)
    {
        return false;
    }

    motionPixels = PathTraceMotionVectors[pixel].xy;
    const float2 previousPixelFloat = float2(pixel) + 0.5 + motionPixels;
    previousPixel = int2(floor(previousPixelFloat));

    const uint2 dimensions = PathTraceFullOutputSize();
    return previousPixel.x >= 0 && previousPixel.y >= 0 &&
        (uint)previousPixel.x < dimensions.x && (uint)previousPixel.y < dimensions.y;
}

bool RestirPTTemporalSamePixelFallbackAllowed(RAB_Surface surface, uint2 pixel, bool projected, float2 motionPixels)
{
    bool candidate = projected && dot(motionPixels, motionPixels) <= 0.25;
    if (!candidate)
    {
        const uint resetMask = PathTraceRRGuideResetMask[pixel];
        const uint hardResetMask = RESTIR_PT_TEMPORAL_UNSAFE_RESET_MASK & ~RT_RR_RESET_OBJECT_MOTION_UNAVAILABLE;
        candidate =
            (resetMask & hardResetMask) == 0u &&
            (resetMask & RT_RR_RESET_OBJECT_MOTION_UNAVAILABLE) != 0u;
    }

    if (!candidate)
    {
        return false;
    }

    const RAB_Surface previousSurface = LoadPathTracePrimarySurfaceRecord(int2(pixel), true);
    uint similarityStatus;
    return PathTracePrimarySurfacesAreSimilar(surface, previousSurface, similarityStatus);
}

RTXDI_PTReservoir GenerateRestirPTGiTemporalReservoir(RAB_Surface surface, uint2 pixel, out uint status)
{
    status = 0u;
    const uint currentIndex = RestirPTGiTemporalCurrentBufferIndex();
    const RTXDI_PTReservoir currentReservoir = LoadRestirPTInitialReservoir(pixel);
    if (!RAB_IsSurfaceValid(surface) || !RAB_SurfaceSupportsOpaqueDiffuseBrdf(surface) || !RestirPTReservoirHasUsefulSample(currentReservoir))
    {
        StoreRestirPTGiTemporalReservoir(pixel, currentIndex, RTXDI_EmptyPTReservoir());
        status = 1u;
        return RTXDI_EmptyPTReservoir();
    }

    RTXDI_PTReservoir temporalReservoir = RTXDI_EmptyPTReservoir();
    float3 selectedTargetFunction = float3(0.0, 0.0, 0.0);
    if (CombineReservoirs(temporalReservoir, currentReservoir, RestirPTGiHash01(pixel, 0x314159u), currentReservoir.TargetFunction))
    {
        selectedTargetFunction = currentReservoir.TargetFunction;
    }

    float piSum = RTXDI_Luminance(currentReservoir.TargetFunction) * max((float)currentReservoir.M, 1.0);
    int2 previousPixel;
    float2 motionPixels;
    const bool projected = RestirPTTryProjectMotionVectorToPreviousPixel(pixel, previousPixel, motionPixels);
    const bool samePixelFallbackAllowed = RestirPTTemporalSamePixelFallbackAllowed(surface, pixel, projected, motionPixels);
    bool acceptedPrevious = false;
    if (projected)
    {
        const RAB_Surface previousSurface = LoadPathTracePrimarySurfaceRecord(previousPixel, true);
        uint similarityStatus;
        if (PathTracePrimarySurfacesAreSimilar(surface, previousSurface, similarityStatus))
        {
            const RTXDI_PTReservoir previousReservoir = LoadRestirPTGiTemporalReservoir(uint2(previousPixel), RestirPTGiTemporalPreviousBufferIndex());
            if (RestirPTReservoirHasUsefulSample(previousReservoir))
            {
                acceptedPrevious = true;
                const uint cappedPreviousM = min(previousReservoir.M, RestirPTParams.temporalResampling.maxHistoryLength);
                RTXDI_PTReservoir previousCandidate = previousReservoir;
                previousCandidate.M = max(cappedPreviousM, 1u);
                if (CombineReservoirs(temporalReservoir, previousCandidate, RestirPTGiHash01(pixel, 0x271828u), previousCandidate.TargetFunction))
                {
                    selectedTargetFunction = previousCandidate.TargetFunction;
                }
                piSum += RTXDI_Luminance(previousCandidate.TargetFunction) * max((float)previousCandidate.M, 1.0);
            }
        }
    }
    if (!acceptedPrevious && samePixelFallbackAllowed)
    {
        const RTXDI_PTReservoir previousReservoir = LoadRestirPTGiTemporalReservoir(pixel, RestirPTGiTemporalPreviousBufferIndex());
        if (RestirPTReservoirHasUsefulSample(previousReservoir))
        {
            acceptedPrevious = true;
            const uint cappedPreviousM = min(previousReservoir.M, RestirPTParams.temporalResampling.maxHistoryLength);
            RTXDI_PTReservoir previousCandidate = previousReservoir;
            previousCandidate.M = max(cappedPreviousM, 1u);
            if (CombineReservoirs(temporalReservoir, previousCandidate, RestirPTGiHash01(pixel, 0x618033u), previousCandidate.TargetFunction))
            {
                selectedTargetFunction = previousCandidate.TargetFunction;
            }
            piSum += RTXDI_Luminance(previousCandidate.TargetFunction) * max((float)previousCandidate.M, 1.0);
        }
    }

    const float pi = RTXDI_Luminance(selectedTargetFunction);
    RTXDI_FinalizeResampling(temporalReservoir, pi, piSum * max(pi, 1.0e-6));
    temporalReservoir.M = min(max(temporalReservoir.M, currentReservoir.M), RestirPTParams.temporalResampling.maxHistoryLength);
    StoreRestirPTGiTemporalReservoir(pixel, currentIndex, temporalReservoir);
    status = acceptedPrevious ? 3u : 2u;
    return temporalReservoir;
}

float4 EvaluateRestirPTGiDebugView(RAB_Surface surface, uint2 pixel, uint view)
{
    if (view == 3u)
    {
        return float4(0.0, 0.0, 0.0, 1.0);
    }

    if (!RAB_IsSurfaceValid(surface) || !RAB_SurfaceSupportsOpaqueDiffuseBrdf(surface))
    {
        return view == 4u ? float4(0.20, 0.0, 0.0, 1.0) : float4(0.0, 0.0, 0.0, 1.0);
    }

    const RTXDI_PTReservoir reservoir = LoadRestirPTInitialReservoir(pixel);
    const bool valid = RestirPTReservoirHasUsefulSample(reservoir);

    if (view == 1u)
    {
        const float3 contribution = valid ? RestirPTReservoirPreviewContribution(reservoir) : float3(0.0, 0.0, 0.0);
        return float4(RestirPTToneMapPreview(contribution), 1.0);
    }

    if (view == 2u)
    {
        uint temporalStatus;
        const RTXDI_PTReservoir temporalReservoir = GenerateRestirPTGiTemporalReservoir(surface, pixel, temporalStatus);
        const bool temporalValid = RestirPTReservoirHasUsefulSample(temporalReservoir);
        const float3 contribution = temporalValid ? RestirPTReservoirPreviewContribution(temporalReservoir) : float3(0.0, 0.0, 0.0);
        return float4(RestirPTToneMapPreview(contribution), 1.0);
    }

    if (view == 4u)
    {
        const float mNorm = saturate((float)reservoir.M / max((float)RestirPTParams.initialSampling.numInitialSamples, 1.0));
        const float weightHeat = saturate(max(reservoir.WeightSum, 0.0) / (1.0 + max(reservoir.WeightSum, 0.0)));
        const float targetHeat = saturate(RTXDI_Luminance(max(reservoir.TargetFunction, float3(0.0, 0.0, 0.0))));
        return valid ? float4(targetHeat, mNorm, weightHeat, 1.0) : float4(0.35, mNorm * 0.25, weightHeat * 0.25, 1.0);
    }

    return float4(0.0, 0.0, 0.0, 1.0);
}

float TraceSmokeShadowVisibility(float3 origin, float3 direction, float tMax, uint ignoreInstanceId, uint ignorePrimitiveIndex, uint ignoreMaterialId)
{
    PathTraceSmokeShadowPayload shadowPayload;
    shadowPayload.hit = 0u;
    shadowPayload.rayMode = ignoreInstanceId != 0xffffffffu ? 2u : 1u;
    shadowPayload.ignoreInstanceId = ignoreInstanceId;
    shadowPayload.ignorePrimitiveIndex = ignorePrimitiveIndex;
    shadowPayload.ignoreMaterialId = ignoreMaterialId;

    RayDesc shadowRay;
    shadowRay.Origin = origin;
    shadowRay.Direction = direction;
    shadowRay.TMin = 0.01;
    shadowRay.TMax = tMax;

    TraceRay(
        SmokeScene,
        RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH,
        0xff,
        1,
        1,
        1,
        shadowRay,
        shadowPayload);

    return shadowPayload.hit == 0u ? 1.0 : 0.0;
}

float RestirPTTraceReservoirVisibility(RAB_Surface surface, RTXDI_PTReservoir reservoir)
{
    if (PathTraceSafetyDisabled(RT_PT_SAFETY_DISABLE_RESTIR_VISIBILITY_RAY))
    {
        return 1.0;
    }
    if (!RAB_IsSurfaceValid(surface) || !RTXDI_IsValidPTReservoir(reservoir))
    {
        return 0.0;
    }

    float3 targetPosition = reservoir.TranslatedWorldPosition;
    uint ignoreInstanceId = 0xffffffffu;
    uint ignorePrimitiveIndex = 0xffffffffu;
    uint ignoreMaterialId = 0xffffffffu;

    if (RTXDI_ConnectsToNeeLight(reservoir))
    {
        const RTXDI_SampledLightData sampledLightData = RTXDI_GetSampledLightData(reservoir);
        if (!RTXDI_SampledLightData_IsValidLightData(sampledLightData))
        {
            return 0.0;
        }

        const uint lightIndex = RTXDI_SampledLightData_GetLightIndex(sampledLightData);
        const RAB_LightInfo lightInfo = RAB_LoadLightInfo(lightIndex, false);
        if (!RAB_IsLightInfoValid(lightInfo))
        {
            return 0.0;
        }

        const RAB_LightSample lightSample = RAB_SamplePolymorphicLight(
            lightInfo,
            surface,
            RTXDI_SampledLightData_GetUVDataFloat2(sampledLightData));
        if (lightSample.valid == 0u)
        {
            return 0.0;
        }

        targetPosition = lightSample.position;
        if (lightSample.lightType == RAB_LIGHT_TYPE_DOOM_ANALYTIC_SPHERE &&
            (lightSample.flags & RT_DOOM_ANALYTIC_LIGHT_CASTS_SHADOWS) == 0u)
        {
            return 1.0;
        }

        if (lightSample.lightType == RAB_LIGHT_TYPE_EMISSIVE_TRIANGLE)
        {
            const uint emissiveTriangleCount = PathTraceSafetyDisabled(RT_PT_SAFETY_DISABLE_EMISSIVE_TRIANGLE_SAMPLING) ? 0u : (uint)max(EmissiveInfo.x, 0.0);
            if (lightSample.lightIndex < emissiveTriangleCount)
            {
                const PathTraceSmokeEmissiveTriangle emissiveTriangle = SmokeEmissiveTriangles[lightSample.lightIndex];
                const bool historicalDynamicEmissive = (emissiveTriangle.padding0 & RT_SMOKE_EMISSIVE_TRIANGLE_HISTORY_DYNAMIC) != 0u;
                ignoreInstanceId = historicalDynamicEmissive ? 0xffffffffu : emissiveTriangle.instanceId;
                ignorePrimitiveIndex = historicalDynamicEmissive ? 0xffffffffu : emissiveTriangle.primitiveIndex;
                ignoreMaterialId = emissiveTriangle.materialId;
            }
        }
    }

    if (!all(targetPosition == targetPosition))
    {
        return 0.0;
    }

    const float3 toTarget = targetPosition - surface.worldPos;
    const float distanceSquared = dot(toTarget, toTarget);
    if (distanceSquared <= 1.0e-6)
    {
        return 0.0;
    }

    const float distance = sqrt(distanceSquared);
    const float3 lightDir = toTarget / distance;
    const float3 normal = RAB_SafeNormalize(RAB_GetSurfaceNormal(surface), RAB_GetSurfaceGeoNormal(surface));
    if (dot(normal, lightDir) <= 0.0 || dot(RAB_GetSurfaceGeoNormal(surface), lightDir) <= 0.0)
    {
        return 0.0;
    }

    const float normalOffsetSign = dot(normal, lightDir) >= 0.0 ? 1.0 : -1.0;
    const float3 shadowOrigin = surface.worldPos + normal * (normalOffsetSign * 0.75) + lightDir * 0.25;
    const float shadowTMax = max(distance - 0.5, 0.01);
    return TraceSmokeShadowVisibility(shadowOrigin, lightDir, shadowTMax, ignoreInstanceId, ignorePrimitiveIndex, ignoreMaterialId);
}

bool RestirPTTryEvaluateNeeReservoirPreview(RAB_Surface surface, RTXDI_PTReservoir reservoir, bool traceVisibility, out float3 contribution)
{
    contribution = float3(0.0, 0.0, 0.0);
    if (!RAB_IsSurfaceValid(surface) || !RTXDI_IsValidPTReservoir(reservoir) || !RTXDI_ConnectsToNeeLight(reservoir))
    {
        return false;
    }

    const RTXDI_SampledLightData sampledLightData = RTXDI_GetSampledLightData(reservoir);
    if (!RTXDI_SampledLightData_IsValidLightData(sampledLightData))
    {
        return false;
    }

    const RAB_LightInfo lightInfo = RAB_LoadLightInfo(RTXDI_SampledLightData_GetLightIndex(sampledLightData), false);
    if (!RAB_IsLightInfoValid(lightInfo))
    {
        return false;
    }

    const RAB_LightSample lightSample = RAB_SamplePolymorphicLight(
        lightInfo,
        surface,
        RTXDI_SampledLightData_GetUVDataFloat2(sampledLightData));
    if (lightSample.valid == 0u)
    {
        return false;
    }

    float3 lightDir;
    float lightDistance;
    RAB_GetLightDirDistance(surface, lightSample, lightDir, lightDistance);
    const float3 normal = RAB_SafeNormalize(RAB_GetSurfaceNormal(surface), RAB_GetSurfaceGeoNormal(surface));
    const float ndotl = saturate(dot(normal, lightDir));
    if (ndotl <= 0.0 || lightSample.solidAnglePdf <= 1.0e-6)
    {
        return false;
    }

    if (traceVisibility)
    {
        const float normalOffsetSign = dot(normal, lightDir) >= 0.0 ? 1.0 : -1.0;
        const float3 shadowOrigin = surface.worldPos + normal * (normalOffsetSign * 0.75) + lightDir * 0.25;
        const float shadowTMax = max(lightDistance - 0.5, 0.01);
        if (TraceSmokeShadowVisibility(shadowOrigin, lightDir, shadowTMax, 0xffffffffu, 0xffffffffu, 0xffffffffu) <= 0.0)
        {
            return false;
        }
    }

    const float3 reflected = RAB_EvaluateSurfaceBrdf(surface, lightDir, RAB_GetSurfaceViewDir(surface)) * lightSample.radiance * ndotl;
    const float reservoirWeight = max(reservoir.WeightSum, 1.0 / max((float)reservoir.M, 1.0));
    contribution = RestirPTSanitizePreviewContribution((reflected / max(lightSample.solidAnglePdf, 1.0e-6)) * reservoirWeight);
    return RAB_Luminance(contribution) > 0.0;
}

bool RestirPTTryEvaluateNeeReservoirTargetFunction(RAB_Surface surface, RTXDI_PTReservoir reservoir, bool traceVisibility, out float3 targetFunction)
{
    targetFunction = float3(0.0, 0.0, 0.0);
    if (!RAB_IsSurfaceValid(surface) || !RTXDI_IsValidPTReservoir(reservoir) || !RTXDI_ConnectsToNeeLight(reservoir))
    {
        return false;
    }

    const RTXDI_SampledLightData sampledLightData = RTXDI_GetSampledLightData(reservoir);
    if (!RTXDI_SampledLightData_IsValidLightData(sampledLightData))
    {
        return false;
    }

    const RAB_LightInfo lightInfo = RAB_LoadLightInfo(RTXDI_SampledLightData_GetLightIndex(sampledLightData), false);
    if (!RAB_IsLightInfoValid(lightInfo))
    {
        return false;
    }

    const RAB_LightSample lightSample = RAB_SamplePolymorphicLight(
        lightInfo,
        surface,
        RTXDI_SampledLightData_GetUVDataFloat2(sampledLightData));
    if (lightSample.valid == 0u)
    {
        return false;
    }

    float3 lightDir;
    float lightDistance;
    RAB_GetLightDirDistance(surface, lightSample, lightDir, lightDistance);
    const float3 normal = RAB_SafeNormalize(RAB_GetSurfaceNormal(surface), RAB_GetSurfaceGeoNormal(surface));
    const float ndotl = saturate(dot(normal, lightDir));
    if (ndotl <= 0.0 || lightSample.solidAnglePdf <= 1.0e-6)
    {
        return false;
    }

    if (traceVisibility)
    {
        const float normalOffsetSign = dot(normal, lightDir) >= 0.0 ? 1.0 : -1.0;
        const float3 shadowOrigin = surface.worldPos + normal * (normalOffsetSign * 0.75) + lightDir * 0.25;
        const float shadowTMax = max(lightDistance - 0.5, 0.01);
        if (TraceSmokeShadowVisibility(shadowOrigin, lightDir, shadowTMax, 0xffffffffu, 0xffffffffu, 0xffffffffu) <= 0.0)
        {
            return false;
        }
    }

    const float3 reflected = RAB_EvaluateSurfaceBrdf(surface, lightDir, RAB_GetSurfaceViewDir(surface)) * lightSample.radiance * ndotl;
    targetFunction = RestirPTSanitizePreviewContribution(reflected / max(lightSample.solidAnglePdf, 1.0e-6));
    return RAB_Luminance(targetFunction) > 0.0;
}

bool RestirPTTryEvaluateSpatialDirectLighting(RAB_Surface surface, uint2 pixel, out float3 contribution)
{
    contribution = float3(0.0, 0.0, 0.0);
    const uint2 reservoirPixel = PathTraceFullPixelToRestirDirectPixel(pixel);
    const RTXDI_PTReservoir spatialReservoir = LoadRestirPTSpatialOutputReservoir(reservoirPixel);
    return RestirPTTryEvaluateNeeReservoirPreview(surface, spatialReservoir, RestirPTInfo.z >= 0.5, contribution);
}

RTXDI_PTReservoir LoadRestirPTRawDirectReservoir(uint2 pixel)
{
    const uint2 reservoirPixel = PathTraceFullPixelToRestirDirectPixel(pixel);
    return LoadRestirPTSpatialOutputReservoir(reservoirPixel);
}

bool RestirPTDirectReservoirHasUsefulSample(RTXDI_PTReservoir reservoir)
{
    const float targetLuminance = RTXDI_Luminance(max(reservoir.TargetFunction, float3(0.0, 0.0, 0.0)));
    return RTXDI_IsValidPTReservoir(reservoir) &&
        RTXDI_ConnectsToNeeLight(reservoir) &&
        reservoir.WeightSum > 0.0 &&
        targetLuminance > 0.0;
}

float3 RestirPTReferenceFinalShadingContribution(RTXDI_PTReservoir reservoir)
{
    return RestirPTSanitizePreviewContribution(reservoir.TargetFunction * max(reservoir.WeightSum, 0.0));
}

bool RestirPTReferenceFinalShadingHasUsefulSample(RTXDI_PTReservoir reservoir)
{
    return RestirPTReservoirHasUsefulSample(reservoir) &&
        RTXDI_Luminance(RestirPTReferenceFinalShadingContribution(reservoir)) > 0.0;
}

float4 RestirPTReferenceFinalShadingStateColor(RTXDI_PTReservoir reservoir)
{
    const bool valid = RTXDI_IsValidPTReservoir(reservoir);
    const float targetHeat = saturate(RTXDI_Luminance(max(reservoir.TargetFunction, float3(0.0, 0.0, 0.0))));
    const float weightHeat = saturate(max(reservoir.WeightSum, 0.0) / (1.0 + max(reservoir.WeightSum, 0.0)));
    const float mNorm = saturate((float)reservoir.M / max((float)RestirPTParams.temporalResampling.maxHistoryLength, 1.0));
    return valid ? float4(targetHeat, weightHeat, mNorm, 1.0) : float4(0.35, 0.0, 0.0, 1.0);
}

float4 RestirPTReferenceFinalShadingContractColor(RAB_Surface surface, RTXDI_PTReservoir reservoir)
{
    if (!RAB_IsSurfaceValid(surface))
    {
        return float4(0.0, 0.0, 0.0, 1.0);
    }
    if (!RTXDI_IsValidPTReservoir(reservoir))
    {
        return float4(0.95, 0.05, 0.05, 1.0);
    }
    if (reservoir.WeightSum != reservoir.WeightSum || abs(reservoir.WeightSum) > 65504.0)
    {
        return float4(1.0, 0.0, 1.0, 1.0);
    }
    if (reservoir.WeightSum < 0.0)
    {
        return float4(1.0, 0.35, 0.0, 1.0);
    }
    if (reservoir.WeightSum == 0.0)
    {
        return float4(1.0, 0.9, 0.0, 1.0);
    }
    if (!all(reservoir.TargetFunction == reservoir.TargetFunction) || any(abs(reservoir.TargetFunction) > 65504.0))
    {
        return float4(0.85, 0.0, 1.0, 1.0);
    }
    if (RTXDI_Luminance(max(reservoir.TargetFunction, float3(0.0, 0.0, 0.0))) <= 0.0)
    {
        return float4(0.0, 0.25, 1.0, 1.0);
    }
    const float3 contribution = reservoir.TargetFunction * reservoir.WeightSum;
    if (!all(contribution == contribution))
    {
        return float4(0.55, 0.0, 1.0, 1.0);
    }
    if (any(abs(contribution) > 65504.0))
    {
        return float4(1.0, 1.0, 1.0, 1.0);
    }
    return float4(0.0, 0.85, 0.2, 1.0);
}

float4 RestirPTReferenceTargetReplayColor(RAB_Surface surface, RTXDI_PTReservoir reservoir)
{
    if (!RAB_IsSurfaceValid(surface))
    {
        return float4(0.0, 0.0, 0.0, 1.0);
    }
    if (!RTXDI_IsValidPTReservoir(reservoir))
    {
        return float4(0.95, 0.05, 0.05, 1.0);
    }
    if (!RTXDI_ConnectsToNeeLight(reservoir))
    {
        return float4(0.05, 0.18, 0.75, 1.0);
    }
    if (!all(reservoir.TargetFunction == reservoir.TargetFunction) ||
        RTXDI_Luminance(max(reservoir.TargetFunction, float3(0.0, 0.0, 0.0))) <= 0.0)
    {
        return float4(1.0, 0.0, 1.0, 1.0);
    }

    float3 replayTargetFunction = float3(0.0, 0.0, 0.0);
    if (!RestirPTTryEvaluateNeeReservoirTargetFunction(surface, reservoir, RestirPTInfo.z >= 0.5, replayTargetFunction))
    {
        return float4(1.0, 0.45, 0.0, 1.0);
    }

    const float storedLum = RTXDI_Luminance(max(reservoir.TargetFunction, float3(0.0, 0.0, 0.0)));
    const float replayLum = RTXDI_Luminance(max(replayTargetFunction, float3(0.0, 0.0, 0.0)));
    if (replayLum <= 0.0)
    {
        return float4(0.0, 0.25, 1.0, 1.0);
    }

    const float ratio = replayLum / max(storedLum, 1.0e-6);
    if (ratio < 0.5 || ratio > 2.0)
    {
        return float4(0.95, 0.85, 0.05, 1.0);
    }
    if (ratio < 0.8 || ratio > 1.25)
    {
        return float4(0.05, 0.85, 0.85, 1.0);
    }
    return float4(0.0, 0.85, 0.2, 1.0);
}

float4 EvaluateRestirPTEnvironmentMisContractView(RAB_Surface surface, uint2 pixel)
{
    const uint2 dimensions = max(PathTraceFullOutputSize(), uint2(1u, 1u));
    const uint band = min((pixel.x * 3u) / dimensions.x, 2u);
    const float3 fallbackDir = RAB_SafeNormalize(CameraForwardAndTanX.xyz, float3(0.0, 0.0, 1.0));
    const float3 envDirection = RAB_IsSurfaceValid(surface)
        ? RAB_SafeNormalize(RAB_GetSurfaceNormal(surface), fallbackDir)
        : fallbackDir;

    if (band == 0u)
    {
        const float2 envUv = RAB_GetEnvironmentMapRandXYFromDir(envDirection);
        return all(envUv == envUv) && all(envUv >= 0.0) && all(envUv <= 1.0)
            ? float4(0.0, 0.85, 0.2, 1.0)
            : float4(1.0, 0.0, 1.0, 1.0);
    }

    if (band == 1u)
    {
        const float envPdf = RAB_EvaluateEnvironmentMapSamplingPdf(envDirection);
        if (envPdf != envPdf || abs(envPdf) > 65504.0)
        {
            return float4(1.0, 0.0, 1.0, 1.0);
        }
        return envPdf > 0.0
            ? float4(0.0, 0.85, 0.2, 1.0)
            : float4(0.95, 0.05, 0.05, 1.0);
    }

    return float4(1.0, 0.45, 0.0, 1.0);
}

float4 RestirPTReferenceResetReasonColor(uint resetMask)
{
    if ((resetMask & RT_RR_RESET_STOCHASTIC_TRANSLUCENT) != 0u)
    {
        return float4(0.75, 0.15, 0.85, 1.0);
    }
    if ((resetMask & RT_RR_RESET_MATERIAL_MISMATCH) != 0u)
    {
        return float4(1.00, 0.60, 0.05, 1.0);
    }
    if ((resetMask & RT_RR_RESET_REJECTED_PREVIOUS) != 0u)
    {
        return float4(0.95, 0.05, 0.05, 1.0);
    }
    if ((resetMask & RT_RR_RESET_MISSING_PREVIOUS) != 0u)
    {
        return float4(0.60, 0.10, 0.10, 1.0);
    }
    if ((resetMask & RT_RR_RESET_OBJECT_MOTION_UNAVAILABLE) != 0u)
    {
        return float4(0.80, 0.80, 0.15, 1.0);
    }
    if ((resetMask & RT_RR_RESET_INVALID_SURFACE) != 0u)
    {
        return float4(0.02, 0.02, 0.02, 1.0);
    }
    return float4(0.80, 0.30, 0.10, 1.0);
}

float4 EvaluateRestirPTReferenceTemporalReadinessView(RAB_Surface surface, uint2 pixel)
{
    if (!RAB_IsSurfaceValid(surface) || !RAB_SurfaceSupportsOpaqueDiffuseBrdf(surface))
    {
        return PathTracePrimarySurfaceDebugColor(RT_PRIMARY_SURFACE_DEBUG_MISSING_CURRENT, surface);
    }

    const uint resetMask = PathTraceRRGuideResetMask[pixel];
    if ((resetMask & RESTIR_PT_TEMPORAL_UNSAFE_RESET_MASK) != 0u)
    {
        return RestirPTReferenceResetReasonColor(resetMask);
    }

    int2 previousPixel;
    float2 motionPixels;
    if (!RestirPTTryProjectMotionVectorToPreviousPixel(pixel, previousPixel, motionPixels))
    {
        return float4(0.95, 0.05, 0.05, 1.0);
    }

    const RAB_Surface previousSurface = LoadPathTracePrimarySurfaceRecord(previousPixel, true);
    uint similarityStatus;
    if (!PathTracePrimarySurfacesAreSimilar(surface, previousSurface, similarityStatus))
    {
        return PathTracePrimarySurfaceDebugColor(similarityStatus, surface);
    }

    const uint2 reservoirPixel = PathTraceFullPixelToRestirDirectPixel(pixel);
    const RTXDI_PTReservoir initialReservoir = LoadRestirPTInitialDirectReservoir(reservoirPixel);
    const RTXDI_PTReservoir temporalReservoir = LoadRestirPTTemporalOutputReservoir(reservoirPixel);
    if (!RTXDI_IsValidPTReservoir(temporalReservoir))
    {
        return float4(0.05, 0.12, 0.55, 1.0);
    }

    const float initialM = RTXDI_IsValidPTReservoir(initialReservoir) ? (float)initialReservoir.M : 0.0;
    const float temporalM = (float)temporalReservoir.M;
    const float history = saturate(temporalM / max((float)RestirPTParams.temporalResampling.maxHistoryLength, 1.0));
    if (temporalM <= initialM + 0.5)
    {
        return float4(0.05, 0.26 + 0.35 * history, 0.34 + 0.35 * history, 1.0);
    }

    return float4(0.02, 0.34 + 0.45 * history, 0.08 + 0.70 * history, 1.0);
}

float4 EvaluateRestirPTReferenceTemporalPageFlowView(uint2 pixel)
{
    const uint2 reservoirPixel = PathTraceFullPixelToRestirDirectPixel(pixel);
    const RTXDI_PTReservoir initialReservoir = LoadRestirPTInitialDirectReservoir(reservoirPixel);
    const RTXDI_PTReservoir temporalInputReservoir = LoadRestirPTTemporalInputReservoir(reservoirPixel);
    const RTXDI_PTReservoir temporalOutputReservoir = LoadRestirPTTemporalOutputReservoir(reservoirPixel);

    const bool initialValid = RTXDI_IsValidPTReservoir(initialReservoir);
    const bool temporalInputValid = RTXDI_IsValidPTReservoir(temporalInputReservoir);
    const bool temporalOutputValid = RTXDI_IsValidPTReservoir(temporalOutputReservoir);
    if (!initialValid)
    {
        return float4(0.55, 0.05, 0.04, 1.0);
    }
    if (!temporalInputValid)
    {
        return float4(0.05, 0.12, 0.75, 1.0);
    }
    if (!temporalOutputValid)
    {
        return float4(0.62, 0.08, 0.65, 1.0);
    }

    const float inputM = (float)temporalInputReservoir.M;
    const float initialM = (float)initialReservoir.M;
    const float outputM = (float)temporalOutputReservoir.M;
    const float history = saturate(outputM / max((float)RestirPTParams.temporalResampling.maxHistoryLength, 1.0));
    if (outputM <= initialM + 0.5)
    {
        return float4(0.05, 0.65, 0.65, 1.0);
    }
    if (outputM <= max(inputM, initialM) + 0.5)
    {
        return float4(0.75, 0.45, 0.95, 1.0);
    }
    return float4(0.02, 0.30 + 0.45 * history, 0.08 + 0.75 * history, 1.0);
}

int2 RestirPTReferenceTemporalOffset(int sampleIdx, int radius)
{
    sampleIdx &= 7;
    if (sampleIdx == 0) return int2(-radius, -radius);
    if (sampleIdx == 1) return int2( radius,  radius);
    if (sampleIdx == 2) return int2(-radius,  radius);
    if (sampleIdx == 3) return int2( radius, -radius);
    if (sampleIdx == 4) return int2(-radius,  0);
    if (sampleIdx == 5) return int2( radius,  0);
    if (sampleIdx == 6) return int2( 0, -radius);
    return int2(0, radius);
}

bool RestirPTReferenceSurfaceMaterialSimilar(RAB_Surface surface, RAB_Surface temporalSurface)
{
    return surface.materialId == temporalSurface.materialId &&
        surface.materialIndex == temporalSurface.materialIndex &&
        surface.surfaceClass == temporalSurface.surfaceClass &&
        abs(GetRoughness(surface.material) - GetRoughness(temporalSurface.material)) <= 0.20;
}

uint RestirPTReferenceSurfaceMaterialMismatchReason(RAB_Surface surface, RAB_Surface temporalSurface)
{
    if (surface.materialId != temporalSurface.materialId)
    {
        return 23u;
    }
    if (surface.materialIndex != temporalSurface.materialIndex)
    {
        return 24u;
    }
    if (surface.surfaceClass != temporalSurface.surfaceClass)
    {
        return 25u;
    }
    if (abs(GetRoughness(surface.material) - GetRoughness(temporalSurface.material)) > 0.20)
    {
        return 26u;
    }
    return 0u;
}

bool RestirPTReferenceReservoirConnectsToNeeLight(RTXDI_PTReservoir reservoir)
{
    return RTXDI_IsValidPTReservoir(reservoir) && RTXDI_ConnectsToNeeLight(reservoir);
}

bool RestirPTReferenceDuplicationMapRequested()
{
    return RestirPTParams.temporalResampling.duplicationBasedHistoryReduction != 0u &&
        RestirPTParams.temporalResampling.historyReductionStrength > 0.0;
}

bool RestirPTReferenceSmokeEmissiveIdentityNonZero(PathTraceSmokeEmissiveTriangle emissiveTriangle)
{
    return emissiveTriangle.identityHashLo != 0u || emissiveTriangle.identityHashHi != 0u;
}

bool RestirPTReferenceSmokeEmissiveRecordsCompatible(
    PathTraceSmokeEmissiveTriangle currentTriangle,
    PathTraceSmokeEmissiveTriangle previousTriangle)
{
    return currentTriangle.identityHashLo == previousTriangle.identityHashLo &&
        currentTriangle.identityHashHi == previousTriangle.identityHashHi &&
        currentTriangle.materialId == previousTriangle.materialId &&
        currentTriangle.universeMaterialIndex == previousTriangle.universeMaterialIndex &&
        currentTriangle.emissiveTextureIndex == previousTriangle.emissiveTextureIndex;
}

uint RestirPTReferencePreviousNeeLightRemapFailureReason(RTXDI_PTReservoir reservoir)
{
    if (!RestirPTReferenceReservoirConnectsToNeeLight(reservoir))
    {
        return 0u;
    }

    const RTXDI_SampledLightData sampledLightData = RTXDI_GetSampledLightData(reservoir);
    if (!RTXDI_SampledLightData_IsValidLightData(sampledLightData))
    {
        return 10u;
    }

    const uint lightIndex = RTXDI_SampledLightData_GetLightIndex(sampledLightData);
    const uint previousEmissiveTriangleCount = RAB_GetPreviousEmissiveTriangleCount();
    const uint analyticCount = RAB_GetCurrentDoomAnalyticLightCount();
    const uint currentIdentityCount = (uint)max(DoomAnalyticLightRemapInfo.x, 0.0);
    const uint previousIdentityCount = (uint)max(DoomAnalyticLightRemapInfo.y, 0.0);
    const uint remapCount = (uint)max(DoomAnalyticLightRemapInfo.z, 0.0);

    if (lightIndex < previousEmissiveTriangleCount)
    {
        const PathTraceSmokeEmissiveTriangle previousTriangle = SmokePreviousEmissiveTriangles[lightIndex];
        if (!RestirPTReferenceSmokeEmissiveIdentityNonZero(previousTriangle))
        {
            return 17u;
        }

        const PathTraceEmissiveLightRemap remap = SmokeEmissiveRemap[lightIndex];
        if (!RAB_SmokeEmissiveRemapValid(remap) || remap.previousToCurrentIndex < 0)
        {
            if ((remap.flags & RAB_SMOKE_EMISSIVE_REMAP_PREVIOUS_DUPLICATE) != 0u)
            {
                return 18u;
            }
            if ((remap.flags & RAB_SMOKE_EMISSIVE_REMAP_CURRENT_DUPLICATE) != 0u)
            {
                return 19u;
            }
            if ((remap.flags & RAB_SMOKE_EMISSIVE_REMAP_CURRENT_MISSING) != 0u)
            {
                return 20u;
            }
            if ((remap.flags & RAB_SMOKE_EMISSIVE_REMAP_INCOMPATIBLE) != 0u)
            {
                return 21u;
            }
            return 11u;
        }

        const uint currentEmissiveIndex = (uint)remap.previousToCurrentIndex;
        if (currentEmissiveIndex >= RAB_GetCurrentEmissiveTriangleCount())
        {
            return 22u;
        }

        const PathTraceSmokeEmissiveTriangle currentTriangle = SmokeEmissiveTriangles[currentEmissiveIndex];
        return RestirPTReferenceSmokeEmissiveRecordsCompatible(currentTriangle, previousTriangle) ? 0u : 21u;
    }

    const uint previousAnalyticIndex = lightIndex - previousEmissiveTriangleCount;
    if (previousAnalyticIndex >= previousIdentityCount)
    {
        return 12u;
    }

    const PathTraceDoomAnalyticLightCandidateIdentity previousIdentity = DoomAnalyticPreviousIdentities[previousAnalyticIndex];
    if (!RAB_DoomAnalyticIdentityValid(previousIdentity) || previousIdentity.universeIndex >= remapCount)
    {
        return 13u;
    }

    const PathTraceDoomAnalyticLightRemap remap = DoomAnalyticRemap[previousIdentity.universeIndex];
    if (!RAB_DoomAnalyticRemapValid(remap) || remap.previousToCurrentCandidateIndex < 0)
    {
        return 14u;
    }

    const uint currentAnalyticIndex = (uint)remap.previousToCurrentCandidateIndex;
    if (currentAnalyticIndex >= analyticCount || currentAnalyticIndex >= currentIdentityCount)
    {
        return 15u;
    }
    if (!RAB_DoomAnalyticLightStateCompatible(DoomAnalyticLights[currentAnalyticIndex], DoomAnalyticPreviousLights[previousAnalyticIndex]))
    {
        return 16u;
    }

    return 0u;
}

RAB_Surface RestirPTReferenceLoadDirectDomainSurface(int2 directPixel, bool previousFrame)
{
    const uint2 directSize = max(PathTraceRestirDirectSize(), uint2(1u, 1u));
    if (directPixel.x < 0 || directPixel.y < 0 ||
        (uint)directPixel.x >= directSize.x || (uint)directPixel.y >= directSize.y)
    {
        return RAB_EmptySurface();
    }

    const uint2 representativeFullPixel = PathTraceRestirDirectPixelToRepresentativeFullPixel(uint2(directPixel));
    return RAB_GetGBufferSurface(int2(representativeFullPixel), previousFrame);
}

bool RestirPTReferencePreviousNeeReservoirFailsLightRemap(RTXDI_PTReservoir reservoir)
{
    return RestirPTReferencePreviousNeeLightRemapFailureReason(reservoir) != 0u;
}

uint RestirPTReferenceClassifyTemporalCandidate(
    RAB_Surface surface,
    int2 temporalSurfacePos,
    bool isFallbackSample,
    float expectedPrevLinearDepth,
    out RTXDI_PTReservoir previousReservoir)
{
    previousReservoir = RTXDI_EmptyPTReservoir();

    const uint2 dimensions = PathTraceRestirDirectSize();
    if (temporalSurfacePos.x < 0 || temporalSurfacePos.y < 0 ||
        (uint)temporalSurfacePos.x >= dimensions.x || (uint)temporalSurfacePos.y >= dimensions.y)
    {
        return 2u;
    }

    const RAB_Surface temporalSurface = RestirPTReferenceLoadDirectDomainSurface(temporalSurfacePos, true);
    if (!RAB_IsSurfaceValid(temporalSurface))
    {
        return 2u;
    }

    if (!isFallbackSample && !RTXDI_IsValidNeighbor(
        RAB_GetSurfaceNormal(surface), RAB_GetSurfaceNormal(temporalSurface),
        expectedPrevLinearDepth, RAB_GetSurfaceLinearDepth(temporalSurface),
        RestirPTParams.temporalResampling.normalThreshold,
        RestirPTParams.temporalResampling.depthThreshold))
    {
        return 3u;
    }

    const uint materialMismatchReason = RestirPTReferenceSurfaceMaterialMismatchReason(surface, temporalSurface);
    if (materialMismatchReason != 0u)
    {
        return materialMismatchReason;
    }

    previousReservoir = LoadRestirPTTemporalInputReservoir(uint2(temporalSurfacePos));
    if (!RTXDI_IsValidPTReservoir(previousReservoir))
    {
        return 5u;
    }

    const uint nextAge = previousReservoir.Age + 1u;
    if (nextAge > min(RTXDI_PTRESERVOIR_AGE_MAX, RestirPTParams.temporalResampling.maxReservoirAge))
    {
        return 6u;
    }

    return 7u;
}

float4 RestirPTReferenceTemporalNeighborColor(uint status, float history)
{
    if (status == 1u) return float4(0.90, 0.04, 0.04, 1.0);
    if (status == 2u) return float4(0.04, 0.08, 0.42, 1.0);
    if (status == 3u) return float4(0.58, 0.08, 0.78, 1.0);
    if (status == 4u) return float4(0.80, 0.60, 0.04, 1.0);
    if (status == 5u) return float4(0.06, 0.24, 0.95, 1.0);
    if (status == 6u) return float4(0.95, 0.36, 0.04, 1.0);
    if (status == 8u) return float4(0.05, 0.65, 0.65, 1.0);
    if (status == 9u) return float4(1.00, 0.55, 0.00, 1.0);
    if (status == 10u) return float4(1.00, 1.00, 1.00, 1.0);
    if (status == 11u) return float4(0.00, 1.00, 0.25, 1.0);
    if (status == 12u) return float4(0.25, 0.45, 1.00, 1.0);
    if (status == 13u) return float4(0.55, 0.25, 1.00, 1.0);
    if (status == 14u) return float4(1.00, 0.00, 1.00, 1.0);
    if (status == 15u) return float4(1.00, 0.35, 0.65, 1.0);
    if (status == 16u) return float4(0.45, 0.45, 0.45, 1.0);
    if (status == 17u) return float4(0.00, 0.45, 0.15, 1.0);
    if (status == 18u) return float4(0.20, 1.00, 0.10, 1.0);
    if (status == 19u) return float4(0.55, 1.00, 0.15, 1.0);
    if (status == 20u) return float4(0.00, 0.85, 0.65, 1.0);
    if (status == 21u) return float4(0.95, 0.95, 0.10, 1.0);
    if (status == 22u) return float4(0.00, 0.25, 0.18, 1.0);
    if (status == 23u) return float4(0.95, 0.75, 0.05, 1.0);
    if (status == 24u) return float4(0.95, 0.45, 0.05, 1.0);
    if (status == 25u) return float4(0.65, 0.55, 0.00, 1.0);
    if (status == 26u) return float4(0.95, 0.90, 0.45, 1.0);
    if (status == 27u) return float4(1.00, 0.12, 0.00, 1.0);
    return float4(0.02, 0.30 + 0.45 * history, 0.08 + 0.75 * history, 1.0);
}

float4 EvaluateRestirPTReferenceTemporalNeighborView(RAB_Surface surface, uint2 pixel)
{
    const uint2 reservoirPixel = PathTraceFullPixelToRestirDirectPixel(pixel);
    surface = RestirPTReferenceLoadDirectDomainSurface(int2(reservoirPixel), false);
    if (!RAB_IsSurfaceValid(surface) || !RAB_SurfaceSupportsOpaqueDiffuseBrdf(surface))
    {
        return float4(0.0, 0.0, 0.0, 1.0);
    }

    const RTXDI_PTReservoir initialReservoir = LoadRestirPTInitialDirectReservoir(reservoirPixel);
    const RTXDI_PTReservoir temporalOutputReservoir = LoadRestirPTTemporalOutputReservoir(reservoirPixel);
    if (!RTXDI_IsValidPTReservoir(initialReservoir))
    {
        return float4(0.55, 0.05, 0.04, 1.0);
    }
    if (!RTXDI_IsValidPTReservoir(temporalOutputReservoir))
    {
        return float4(0.62, 0.08, 0.65, 1.0);
    }

    const uint2 representativeFullPixel = PathTraceRestirDirectPixelToRepresentativeFullPixel(reservoirPixel);
    const uint motionMask = PathTraceMotionVectorMask[representativeFullPixel];
    float2 previousFullPixelFloat = float2(representativeFullPixel) + 0.5;
    float expectedPrevLinearDepth = surface.linearDepth;
    if ((motionMask & PT_MOTION_VECTOR_MASK_VALID) != 0u)
    {
        const float3 motionVector = PathTraceMotionVectors[representativeFullPixel].xyz;
        previousFullPixelFloat += motionVector.xy;
        expectedPrevLinearDepth = surface.linearDepth + motionVector.z;
    }
    else if (!ProjectPathTracePrimarySurfaceToPreviousPixelFloatAndDepth(
        surface.worldPos, PathTraceFullOutputSize(), previousFullPixelFloat, expectedPrevLinearDepth))
    {
        return RestirPTReferenceTemporalNeighborColor(1u, 0.0);
    }

    const float2 previousPixelFloat = PathTraceFullPixelFloatToRestirDirectPixelFloat(previousFullPixelFloat);
    int2 previousPixel = int2(floor(previousPixelFloat));
    if (PathTraceRestirSparsityEnabled() && previousPixel.x >= 0 && previousPixel.y >= 0)
    {
        previousPixel = int2(PathTraceRestirSparseRepresentativePixel(uint2(previousPixel), true));
    }

    uint bestFailure = 2u;
    RTXDI_PTReservoir acceptedReservoir = RTXDI_EmptyPTReservoir();
    const int temporalSampleCount = 5;
    const int sampleCount = temporalSampleCount + (RestirPTParams.temporalResampling.enableFallbackSampling != 0u ? 1 : 0);
    const int radius = 1;
    RTXDI_RuntimeParameters rtxdiRuntimeParams = (RTXDI_RuntimeParameters)0;
    rtxdiRuntimeParams.frameIndex = (uint)max(RestirPTInfo.x, 0.0);
    RTXDI_RandomSamplerState rng = RTXDI_InitRandomSampler(reservoirPixel, rtxdiRuntimeParams.frameIndex, 0x51ed270bu);
    const int temporalSampleStartIdx = int(RTXDI_GetNextRandom(rng) * 8.0);
    for (int i = 0; i < sampleCount; ++i)
    {
        const bool isFallbackSample = i == temporalSampleCount;
        int2 candidatePixel = isFallbackSample ? int2(reservoirPixel) : previousPixel;
        if (!isFallbackSample && i != 0)
        {
            candidatePixel += RestirPTReferenceTemporalOffset(temporalSampleStartIdx + i, radius);
        }

        RTXDI_PTReservoir candidateReservoir;
        const uint candidateStatus = RestirPTReferenceClassifyTemporalCandidate(
            surface, candidatePixel, isFallbackSample, expectedPrevLinearDepth, candidateReservoir);
        if (candidateStatus == 7u)
        {
            acceptedReservoir = candidateReservoir;
            break;
        }
        bestFailure = max(bestFailure, candidateStatus);
    }

    const float initialM = (float)initialReservoir.M;
    const float outputM = (float)temporalOutputReservoir.M;
    const float history = saturate(outputM / max((float)RestirPTParams.temporalResampling.maxHistoryLength, 1.0));
    if (!RTXDI_IsValidPTReservoir(acceptedReservoir))
    {
        return RestirPTReferenceTemporalNeighborColor(bestFailure, history);
    }
    if (RestirPTReferenceDuplicationMapRequested())
    {
        return RestirPTReferenceTemporalNeighborColor(27u, history);
    }
    if (outputM <= initialM + 0.5)
    {
        if (MotionVectorInfo.y < 0.5 && RestirPTReferenceReservoirConnectsToNeeLight(acceptedReservoir))
        {
            return RestirPTReferenceTemporalNeighborColor(9u, history);
        }
        const uint acceptedRemapFailureReason = RestirPTReferencePreviousNeeLightRemapFailureReason(acceptedReservoir);
        if (acceptedRemapFailureReason != 0u)
        {
            return RestirPTReferenceTemporalNeighborColor(acceptedRemapFailureReason, history);
        }
        return RestirPTReferenceTemporalNeighborColor(8u, history);
    }
    return RestirPTReferenceTemporalNeighborColor(7u, history);
}

float4 EvaluateRestirPTReferenceBrdfContractView(RAB_Surface surface)
{
    if (!RAB_IsSurfaceValid(surface))
    {
        return float4(0.0, 0.0, 0.0, 1.0);
    }
    if (!RAB_SurfaceSupportsOpaqueDiffuseBrdf(surface))
    {
        return float4(0.45, 0.05, 0.75, 1.0);
    }

    const float3 specularF0 = GetSpecularF0(surface.material);
    const float specularMax = max(max(specularF0.r, specularF0.g), specularF0.b);
    const float roughness = saturate(GetRoughness(surface.material));
    if (specularMax <= 0.02)
    {
        return float4(0.02, 0.75, 0.12, 1.0);
    }
    if (roughness <= 0.08)
    {
        return float4(1.00, 0.05, 0.05, 1.0);
    }
    if (roughness <= 0.35)
    {
        return float4(1.00, 0.45, 0.00, 1.0);
    }
    return float4(0.95, 0.85, 0.05, 1.0);
}

float4 RestirPTReferenceNeeSampleContractColor(RAB_Surface surface, RTXDI_PTReservoir reservoir)
{
    if (!RAB_IsSurfaceValid(surface))
    {
        return float4(0.0, 0.0, 0.0, 1.0);
    }

    if (!RTXDI_IsValidPTReservoir(reservoir))
    {
        return float4(0.04, 0.04, 0.04, 1.0);
    }
    if (!RTXDI_ConnectsToNeeLight(reservoir))
    {
        return float4(0.05, 0.18, 0.75, 1.0);
    }
    if (reservoir.WeightSum <= 0.0 ||
        !all(reservoir.TargetFunction == reservoir.TargetFunction) ||
        RTXDI_Luminance(max(reservoir.TargetFunction, float3(0.0, 0.0, 0.0))) <= 0.0)
    {
        return float4(0.75, 0.02, 0.02, 1.0);
    }

    const RTXDI_SampledLightData sampledLightData = RTXDI_GetSampledLightData(reservoir);
    if (!RTXDI_SampledLightData_IsValidLightData(sampledLightData))
    {
        return float4(1.0, 1.0, 1.0, 1.0);
    }

    const uint lightIndex = RTXDI_SampledLightData_GetLightIndex(sampledLightData);
    if (lightIndex >= RAB_GetCurrentLightCount())
    {
        return float4(1.0, 0.0, 0.75, 1.0);
    }

    const RAB_LightInfo lightInfo = RAB_LoadLightInfo(lightIndex, false);
    if (!RAB_IsLightInfoValid(lightInfo))
    {
        return float4(0.55, 0.05, 0.80, 1.0);
    }

    const RAB_LightSample lightSample = RAB_SamplePolymorphicLight(
        lightInfo,
        surface,
        RTXDI_SampledLightData_GetUVDataFloat2(sampledLightData));
    if (lightSample.valid == 0u)
    {
        return float4(1.0, 0.45, 0.0, 1.0);
    }
    if (lightSample.solidAnglePdf <= 0.0 ||
        !all(lightSample.position == lightSample.position) ||
        !all(lightSample.radiance == lightSample.radiance))
    {
        return float4(0.95, 0.85, 0.05, 1.0);
    }

    if (lightSample.lightType == RAB_LIGHT_TYPE_EMISSIVE_TRIANGLE)
    {
        return float4(0.90, 0.35, 0.05, 1.0);
    }
    if (lightSample.lightType == RAB_LIGHT_TYPE_DOOM_ANALYTIC_SPHERE)
    {
        return float4(0.05, 0.80, 0.95, 1.0);
    }
    return float4(0.05, 0.75, 0.12, 1.0);
}

float4 EvaluateRestirPTReferenceNeeSampleContractView(RAB_Surface surface, uint2 pixel)
{
    const uint2 reservoirPixel = PathTraceFullPixelToRestirDirectPixel(pixel);
    const RTXDI_PTReservoir reservoir = LoadRestirPTInitialDirectReservoir(reservoirPixel);
    return RestirPTReferenceNeeSampleContractColor(surface, reservoir);
}

float4 RestirPTReferencePathTraceMetadataColor(RTXDI_PTReservoir reservoir)
{
    if (!RTXDI_IsValidPTReservoir(reservoir))
    {
        return float4(0.0, 0.0, 0.0, 1.0);
    }

    const bool connectsToNee = RTXDI_ConnectsToNeeLight(reservoir);
    if (!connectsToNee)
    {
        return float4(0.05, 0.18, 0.75, 1.0);
    }

    const RTXDI_SampledLightData sampledLightData = RTXDI_GetSampledLightData(reservoir);
    if (!RTXDI_SampledLightData_IsValidLightData(sampledLightData))
    {
        return float4(1.0, 1.0, 1.0, 1.0);
    }
    if (reservoir.PathLength == 0u)
    {
        return float4(0.85, 0.02, 0.02, 1.0);
    }

    if (reservoir.RcVertexLength == reservoir.PathLength && reservoir.PartialJacobian <= 0.0)
    {
        return float4(1.0, 0.0, 0.75, 1.0);
    }
    if (reservoir.RcVertexLength + 1u == reservoir.PathLength && reservoir.RcWiPdf <= 0.0)
    {
        return float4(0.95, 0.85, 0.05, 1.0);
    }
    if (reservoir.RcVertexLength > reservoir.PathLength)
    {
        return float4(0.05, 0.62, 0.18, 1.0);
    }
    if (reservoir.PathLength <= 3u)
    {
        return float4(0.90, 0.35, 0.05, 1.0);
    }
    return float4(0.05, 0.80, 0.95, 1.0);
}

float4 EvaluateRestirPTReferencePathTraceMetadataView(uint2 pixel)
{
    const uint2 reservoirPixel = PathTraceFullPixelToRestirDirectPixel(pixel);
    const RTXDI_PTReservoir reservoir = LoadRestirPTInitialDirectReservoir(reservoirPixel);
    return RestirPTReferencePathTraceMetadataColor(reservoir);
}

float4 RestirPTReferenceNeeRecordScalarColor(RTXDI_PTReservoir reservoir)
{
    if (!RTXDI_IsValidPTReservoir(reservoir))
    {
        return float4(0.0, 0.0, 0.0, 1.0);
    }
    if (!RTXDI_ConnectsToNeeLight(reservoir))
    {
        return float4(0.05, 0.18, 0.75, 1.0);
    }

    const RTXDI_SampledLightData sampledLightData = RTXDI_GetSampledLightData(reservoir);
    if (!RTXDI_SampledLightData_IsValidLightData(sampledLightData))
    {
        return float4(1.0, 1.0, 1.0, 1.0);
    }
    if (reservoir.WeightSum <= 0.0 || reservoir.WeightSum != reservoir.WeightSum || abs(reservoir.WeightSum) > 65504.0)
    {
        return float4(0.85, 0.02, 0.02, 1.0);
    }
    if (reservoir.PathLength == 0u || reservoir.RcVertexLength > reservoir.PathLength + 1u)
    {
        return float4(1.0, 0.45, 0.0, 1.0);
    }

    const float targetLuminance = RAB_Luminance(max(reservoir.TargetFunction, float3(0.0, 0.0, 0.0)));
    if (!all(reservoir.TargetFunction == reservoir.TargetFunction) || targetLuminance <= 0.0)
    {
        return float4(0.55, 0.05, 0.80, 1.0);
    }

    const bool connectsToLightVertex = reservoir.RcVertexLength == reservoir.PathLength;
    const bool connectsToPreLightVertex = reservoir.RcVertexLength + 1u == reservoir.PathLength;
    const bool packedLightData = isinf(reservoir.Radiance.x);
    if (connectsToLightVertex && !packedLightData)
    {
        return float4(1.0, 0.0, 0.75, 1.0);
    }
    if (!connectsToLightVertex && packedLightData)
    {
        return float4(0.55, 0.22, 0.75, 1.0);
    }
    if (connectsToLightVertex && (!RAB_IsFinitePositive(reservoir.PartialJacobian)))
    {
        return float4(0.95, 0.85, 0.05, 1.0);
    }
    if (connectsToPreLightVertex && (!RAB_IsFinitePositive(reservoir.RcWiPdf) || !RAB_IsFinitePositive(reservoir.PartialJacobian)))
    {
        return float4(0.05, 0.80, 0.95, 1.0);
    }
    if (!packedLightData && (!all(reservoir.Radiance == reservoir.Radiance) || RAB_Luminance(max(reservoir.Radiance, float3(0.0, 0.0, 0.0))) <= 0.0))
    {
        return float4(0.95, 0.22, 0.04, 1.0);
    }

    if (connectsToLightVertex)
    {
        return float4(0.05, 0.75, 0.12, 1.0);
    }
    if (connectsToPreLightVertex)
    {
        return float4(0.00, 0.35, 0.55, 1.0);
    }
    return float4(0.05, 0.62, 0.18, 1.0);
}

float4 RestirPTReferencePathTracerEntryColor(RAB_Surface surface, uint2 pixel)
{
    const uint2 dimensions = max(PathTraceFullOutputSize(), uint2(1u, 1u));
    const uint band = min((pixel.x * 6u) / dimensions.x, 5u);
    const bool validSurface = RAB_IsSurfaceValid(surface);
    const bool diffuseSurface = validSurface && RAB_SurfaceSupportsOpaqueDiffuseBrdf(surface);
    const bool neeEnabled = IntegratorInfo2.w >= 0.5;
    const bool secondaryRayDisabled = PathTraceSafetyDisabled(RT_PT_SAFETY_DISABLE_DIFFUSE_SECONDARY_RAY);
    const uint maxPathDepth = clamp((uint)max(IntegratorInfo.y, 1.0), 1u, 4u);
    const uint diffuseBounceLimit = clamp((uint)max(IntegratorInfo.z, 0.0), 0u, 3u);
    const uint configuredSecondaryBounces = min(
        diffuseBounceLimit,
        maxPathDepth > 0u ? maxPathDepth - 1u : 0u);
    const uint debugMode = (uint)CameraUpAndDebugMode.w;
    const bool indirectInitialMode = debugMode >= 53u && debugMode <= 56u;
    const uint maxSecondaryBounces = indirectInitialMode ? max(configuredSecondaryBounces, 1u) : configuredSecondaryBounces;
    const bool secondaryConfigured = maxSecondaryBounces > 0u && !secondaryRayDisabled;
    const uint currentLightCount = RAB_GetCurrentLightCount();

    if (band == 0u)
    {
        if (!validSurface)
        {
            return float4(0.0, 0.0, 0.0, 1.0);
        }
        return diffuseSurface ? float4(0.05, 0.75, 0.12, 1.0) : float4(0.55, 0.05, 0.80, 1.0);
    }
    if (band == 1u)
    {
        if (!diffuseSurface)
        {
            return validSurface ? float4(0.55, 0.05, 0.80, 1.0) : float4(0.0, 0.0, 0.0, 1.0);
        }
        if (!neeEnabled)
        {
            return float4(0.85, 0.02, 0.02, 1.0);
        }
        if (currentLightCount == 0u)
        {
            return float4(1.0, 0.45, 0.0, 1.0);
        }
        return float4(0.05, 0.75, 0.12, 1.0);
    }
    if (band == 2u)
    {
        if (!diffuseSurface)
        {
            return validSurface ? float4(0.55, 0.05, 0.80, 1.0) : float4(0.0, 0.0, 0.0, 1.0);
        }
        return secondaryConfigured ? float4(0.05, 0.75, 0.12, 1.0) : float4(0.05, 0.18, 0.75, 1.0);
    }
    if (band == 3u)
    {
        return secondaryConfigured ? float4(0.85, 0.02, 0.02, 1.0) : float4(0.05, 0.62, 0.18, 1.0);
    }
    if (band == 4u)
    {
        return float4(1.0, 0.0, 0.75, 1.0);
    }

    if (configuredSecondaryBounces > 1u)
    {
        return float4(1.0, 0.45, 0.0, 1.0);
    }
    if (indirectInitialMode && configuredSecondaryBounces == 0u)
    {
        return float4(0.00, 0.35, 0.55, 1.0);
    }
    return float4(0.05, 0.75, 0.12, 1.0);
}

float4 RestirPTReferenceRandomReplayMetadataColor(RTXDI_PTReservoir reservoir)
{
    if (!RTXDI_IsValidPTReservoir(reservoir))
    {
        return float4(0.0, 0.0, 0.0, 1.0);
    }
    if (reservoir.PathLength == 0u)
    {
        return float4(0.85, 0.02, 0.02, 1.0);
    }
    if (reservoir.RcVertexLength > reservoir.PathLength)
    {
        return float4(1.0, 0.0, 0.75, 1.0);
    }
    if (reservoir.RcVertexLength <= 2u)
    {
        return float4(0.05, 0.18, 0.75, 1.0);
    }
    if (reservoir.RandomSeed == 0u && reservoir.RandomIndex == 0u)
    {
        return float4(0.95, 0.85, 0.05, 1.0);
    }
    if (reservoir.RandomSeed == 0u)
    {
        return float4(1.0, 0.45, 0.0, 1.0);
    }
    if (reservoir.RandomIndex == 0xffu)
    {
        return float4(0.55, 0.05, 0.80, 1.0);
    }
    return float4(0.05, 0.75, 0.12, 1.0);
}

float4 RestirPTReferenceRandomSamplerBridgeColor(uint2 pixel)
{
    const uint frameIndex = (uint)max(RestirPTInfo.x, 0.0);
    const uint passSeed = 0x7a53d1u;
    RTXDI_RandomSamplerState rtxdiPixelRng = RTXDI_InitRandomSampler(pixel, frameIndex, passSeed);
    RAB_RandomSamplerState rabPixelRng = RAB_InitRandomSampler(pixel, frameIndex, passSeed);
    if (rtxdiPixelRng.seed != rabPixelRng.seed || rtxdiPixelRng.index != rabPixelRng.index)
    {
        return float4(0.85, 0.02, 0.02, 1.0);
    }

    RTXDI_RandomSamplerState rtxdiDirectRng = RTXDI_CreateRandomSamplerFromDirectSeed(0x12345678u, 37u);
    RAB_RandomSamplerState rabDirectRng = RAB_CreateRandomSamplerFromDirectSeed(0x12345678u, 37u);
    if (rtxdiDirectRng.seed != rabDirectRng.seed || rtxdiDirectRng.index != rabDirectRng.index)
    {
        return float4(1.0, 0.0, 0.75, 1.0);
    }

    [unroll]
    for (uint i = 0u; i < 4u; ++i)
    {
        const float rtxdiValue = RTXDI_GetNextRandom(rtxdiPixelRng);
        const float rabValue = RAB_GetNextRandom(rabPixelRng);
        if (rtxdiValue != rabValue)
        {
            return float4(0.95, 0.85, 0.05, 1.0);
        }
        if (rtxdiPixelRng.seed != rabPixelRng.seed || rtxdiPixelRng.index != rabPixelRng.index)
        {
            return float4(1.0, 0.45, 0.0, 1.0);
        }
    }

    const float rtxdiDirectValue = RTXDI_GetNextRandom(rtxdiDirectRng);
    const float rabDirectValue = RAB_GetNextRandom(rabDirectRng);
    if (rtxdiDirectValue != rabDirectValue ||
        rtxdiDirectRng.seed != rabDirectRng.seed ||
        rtxdiDirectRng.index != rabDirectRng.index)
    {
        return float4(0.55, 0.05, 0.80, 1.0);
    }

    return float4(0.05, 0.75, 0.12, 1.0);
}

bool RestirPTReferenceCheckPathTracerUserDataPathType(RTXDI_PTPathTraceInvocationType pathType, uint expectedFlag)
{
    RAB_PathTracerUserData ptud = RAB_EmptyPathTracerUserData();
    ptud.flags = RAB_PTUD_FLAG_ENVIRONMENT_MAP_MISS_UNBRIDGED;
    RAB_PathTracerUserDataSetPathType(ptud, pathType);
    const uint expectedFlags =
        RAB_PTUD_FLAG_ENVIRONMENT_MAP_MISS_UNBRIDGED |
        RAB_PTUD_FLAG_PATH_TYPE_SET |
        expectedFlag;
    return ptud.pathType == pathType && ptud.flags == expectedFlags;
}

float4 RestirPTReferencePathTracerUserDataBridgeColor()
{
    const RAB_PathTracerUserData emptyPtud = RAB_EmptyPathTracerUserData();
    if (emptyPtud.flags != 0u ||
        emptyPtud.pathType != 0u ||
        emptyPtud.reconnectionDenoiserHitDistance != 0.0 ||
        emptyPtud.lastBounceDenoiserHitDistance != 0.0)
    {
        return float4(0.85, 0.02, 0.02, 1.0);
    }

    if (!RestirPTReferenceCheckPathTracerUserDataPathType(RTXDI_PTPathTraceInvocationType_Initial, RAB_PTUD_FLAG_PATH_TYPE_INITIAL) ||
        !RestirPTReferenceCheckPathTracerUserDataPathType(RTXDI_PTPathTraceInvocationType_Temporal, RAB_PTUD_FLAG_PATH_TYPE_TEMPORAL) ||
        !RestirPTReferenceCheckPathTracerUserDataPathType(RTXDI_PTPathTraceInvocationType_TemporalInverse, RAB_PTUD_FLAG_PATH_TYPE_TEMPORAL_INVERSE) ||
        !RestirPTReferenceCheckPathTracerUserDataPathType(RTXDI_PTPathTraceInvocationType_Spatial, RAB_PTUD_FLAG_PATH_TYPE_SPATIAL) ||
        !RestirPTReferenceCheckPathTracerUserDataPathType(RTXDI_PTPathTraceInvocationType_SpatialInverse, RAB_PTUD_FLAG_PATH_TYPE_SPATIAL_INVERSE) ||
        !RestirPTReferenceCheckPathTracerUserDataPathType(RTXDI_PTPathTraceInvocationType_DebugTemporalRetrace, RAB_PTUD_FLAG_PATH_TYPE_DEBUG_TEMPORAL_RETRACE) ||
        !RestirPTReferenceCheckPathTracerUserDataPathType(RTXDI_PTPathTraceInvocationType_DebugSpatialRetrace, RAB_PTUD_FLAG_PATH_TYPE_DEBUG_SPATIAL_RETRACE))
    {
        return float4(1.0, 0.0, 0.75, 1.0);
    }

    RAB_PathTracerUserData callbackPtud = RAB_EmptyPathTracerUserData();
    RAB_Surface surface = RAB_EmptySurface();
    RAB_SetSurfaceWorldPos(surface, float3(1.0, 2.0, 3.0));

    RTXDI_PTReservoir neighborSample = RTXDI_EmptyPTReservoir();
    neighborSample.TranslatedWorldPosition = float3(1.0, 2.0, 8.0);
    RAB_ReconnectionDenoiserCallback(neighborSample, surface, callbackPtud);
    if ((callbackPtud.flags & RAB_PTUD_FLAG_RECONNECTION_DENOISER_CALLBACK) == 0u ||
        abs(callbackPtud.reconnectionDenoiserHitDistance - 5.0) > 0.0001)
    {
        return float4(0.55, 0.05, 0.80, 1.0);
    }

    RAB_LastBounceDenoiserCallback(float3(4.0, 6.0, 3.0), surface, callbackPtud);
    if ((callbackPtud.flags & RAB_PTUD_FLAG_LAST_BOUNCE_DENOISER_CALLBACK) == 0u ||
        abs(callbackPtud.lastBounceDenoiserHitDistance - 5.0) > 0.0001)
    {
        return float4(0.95, 0.85, 0.05, 1.0);
    }

    RAB_NoteEnvironmentMapMissUnbridged(callbackPtud);
    if ((callbackPtud.flags & RAB_PTUD_FLAG_ENVIRONMENT_MAP_MISS_UNBRIDGED) == 0u)
    {
        return float4(1.0, 1.0, 1.0, 1.0);
    }

    return float4(0.05, 0.75, 0.12, 1.0);
}

float4 RestirPTReferencePsrDenoiserPayloadColor(uint2 pixel)
{
    const uint2 dimensions = max(PathTraceFullOutputSize(), uint2(1u, 1u));
    const uint band = min((pixel.x * 6u) / dimensions.x, 5u);

    if (band == 0u)
    {
        const RAB_PathTracerUserData emptyPtud = RAB_EmptyPathTracerUserData();
        if (emptyPtud.flags != 0u ||
            emptyPtud.pathType != 0u ||
            emptyPtud.reconnectionDenoiserHitDistance != 0.0 ||
            emptyPtud.lastBounceDenoiserHitDistance != 0.0)
        {
            return float4(0.85, 0.02, 0.02, 1.0);
        }
        return float4(1.0, 0.0, 0.75, 1.0);
    }

    if (band == 1u)
    {
        return RestirPTReferencePathTracerUserDataBridgeColor();
    }

    RAB_PathTracerUserData callbackPtud = RAB_EmptyPathTracerUserData();
    RAB_Surface surface = RAB_EmptySurface();
    RAB_SetSurfaceWorldPos(surface, float3(1.0, 2.0, 3.0));

    RTXDI_PTReservoir neighborSample = RTXDI_EmptyPTReservoir();
    neighborSample.TranslatedWorldPosition = float3(1.0, 2.0, 8.0);
    RAB_ReconnectionDenoiserCallback(neighborSample, surface, callbackPtud);
    const bool reconnectionScalarOk =
        ((callbackPtud.flags & RAB_PTUD_FLAG_RECONNECTION_DENOISER_CALLBACK) != 0u) &&
        abs(callbackPtud.reconnectionDenoiserHitDistance - 5.0) <= 0.0001;

    RAB_LastBounceDenoiserCallback(float3(4.0, 6.0, 3.0), surface, callbackPtud);
    const bool lastBounceScalarOk =
        ((callbackPtud.flags & RAB_PTUD_FLAG_LAST_BOUNCE_DENOISER_CALLBACK) != 0u) &&
        abs(callbackPtud.lastBounceDenoiserHitDistance - 5.0) <= 0.0001;

    if (band == 2u)
    {
        return reconnectionScalarOk ? float4(0.95, 0.85, 0.05, 1.0) : float4(0.85, 0.02, 0.02, 1.0);
    }
    if (band == 3u)
    {
        return lastBounceScalarOk ? float4(0.95, 0.85, 0.05, 1.0) : float4(0.85, 0.02, 0.02, 1.0);
    }
    if (band == 4u)
    {
        return float4(1.0, 0.45, 0.0, 1.0);
    }

    RAB_NoteEnvironmentMapMissUnbridged(callbackPtud);
    return ((callbackPtud.flags & RAB_PTUD_FLAG_ENVIRONMENT_MAP_MISS_UNBRIDGED) != 0u)
        ? float4(0.05, 0.62, 0.18, 1.0)
        : float4(0.85, 0.02, 0.02, 1.0);
}

float4 RestirPTReferencePathTracerContinuationPolicyColor(uint2 pixel)
{
    const uint2 dimensions = max(PathTraceFullOutputSize(), uint2(1u, 1u));
    const uint band = min((pixel.x * 6u) / dimensions.x, 5u);
    const uint rrDepth = clamp((uint)max(IntegratorInfo2.z, 0.0), 0u, 8u);
    const uint maxPathDepth = clamp((uint)max(IntegratorInfo.y, 1.0), 1u, 4u);
    const uint diffuseBounceLimit = clamp((uint)max(IntegratorInfo.z, 0.0), 0u, 3u);
    const bool secondaryConfigured = min(diffuseBounceLimit, maxPathDepth > 0u ? maxPathDepth - 1u : 0u) > 0u &&
        !PathTraceSafetyDisabled(RT_PT_SAFETY_DISABLE_DIFFUSE_SECONDARY_RAY);
    const bool rrConfigured = rrDepth > 0u;

    if (band == 0u)
    {
        return rrConfigured ? float4(0.05, 0.75, 0.12, 1.0) : float4(0.05, 0.18, 0.75, 1.0);
    }
    if (band == 1u)
    {
        return rrConfigured ? float4(1.0, 0.45, 0.0, 1.0) : float4(0.05, 0.62, 0.18, 1.0);
    }
    if (band == 2u)
    {
        return rrConfigured && secondaryConfigured ? float4(0.85, 0.02, 0.02, 1.0) : float4(0.05, 0.62, 0.18, 1.0);
    }
    if (band == 3u)
    {
        return secondaryConfigured ? float4(1.0, 0.0, 0.75, 1.0) : float4(0.05, 0.62, 0.18, 1.0);
    }
    if (band == 4u)
    {
        return float4(0.55, 0.22, 0.75, 1.0);
    }

    return rrConfigured && secondaryConfigured
        ? float4(0.95, 0.85, 0.05, 1.0)
        : float4(0.05, 0.75, 0.12, 1.0);
}

float4 RestirPTReferenceFinalShadingOutputColor(RAB_Surface surface, uint2 pixel)
{
    const uint2 dimensions = max(PathTraceFullOutputSize(), uint2(1u, 1u));
    const uint band = min((pixel.x * 6u) / dimensions.x, 5u);
    const uint2 reservoirPixel = PathTraceFullPixelToRestirDirectPixel(pixel);
    const RTXDI_PTReservoir reservoir = LoadRestirPTFinalShadingInputReservoir(reservoirPixel);

    if (band == 0u)
    {
        if (!RAB_IsSurfaceValid(surface))
        {
            return float4(0.0, 0.0, 0.0, 1.0);
        }
        if (!RTXDI_IsValidPTReservoir(reservoir))
        {
            return float4(0.85, 0.02, 0.02, 1.0);
        }
        return RestirPTReferenceFinalShadingHasUsefulSample(reservoir)
            ? float4(0.05, 0.75, 0.12, 1.0)
            : float4(0.05, 0.18, 0.75, 1.0);
    }
    if (band == 1u)
    {
        if (!RTXDI_IsValidPTReservoir(reservoir))
        {
            return float4(0.85, 0.02, 0.02, 1.0);
        }
        const float3 contribution = RestirPTReferenceFinalShadingContribution(reservoir);
        if (!all(contribution == contribution) || any(abs(contribution) > 65504.0))
        {
            return float4(1.0, 0.0, 0.75, 1.0);
        }
        return RTXDI_Luminance(max(contribution, float3(0.0, 0.0, 0.0))) > 0.0
            ? float4(0.05, 0.75, 0.12, 1.0)
            : float4(0.05, 0.18, 0.75, 1.0);
    }
    if (band == 2u)
    {
        return float4(1.0, 0.45, 0.0, 1.0);
    }
    if (band == 3u)
    {
        return float4(1.0, 0.0, 0.75, 1.0);
    }
    if (band == 4u)
    {
        return float4(0.55, 0.22, 0.75, 1.0);
    }

    return RAB_IsSurfaceValid(surface) ? float4(0.05, 0.62, 0.18, 1.0) : float4(0.0, 0.0, 0.0, 1.0);
}

float4 RestirPTReferenceRemainingSupplierGapColor(uint2 pixel)
{
    const uint2 dimensions = max(PathTraceFullOutputSize(), uint2(1u, 1u));
    const uint band = min((pixel.x * 6u) / dimensions.x, 5u);

    if (band == 0u)
    {
        return float4(1.0, 0.45, 0.0, 1.0);
    }
    if (band == 1u)
    {
        return float4(0.95, 0.85, 0.05, 1.0);
    }
    if (band == 2u)
    {
        return float4(1.0, 0.45, 0.0, 1.0);
    }
    if (band == 3u)
    {
        return float4(1.0, 0.0, 0.75, 1.0);
    }
    if (band == 4u)
    {
        return float4(0.55, 0.22, 0.75, 1.0);
    }
    return float4(0.55, 0.05, 0.80, 1.0);
}

float4 RestirPTReferenceVisibilityPolicyColor(RAB_Surface surface, RTXDI_PTReservoir reservoir)
{
    if (!RAB_IsSurfaceValid(surface))
    {
        return float4(0.0, 0.0, 0.0, 1.0);
    }
    if (!RTXDI_IsValidPTReservoir(reservoir))
    {
        return float4(0.85, 0.02, 0.02, 1.0);
    }
    if (!RTXDI_ConnectsToNeeLight(reservoir))
    {
        return float4(0.05, 0.18, 0.75, 1.0);
    }

    const RTXDI_SampledLightData sampledLightData = RTXDI_GetSampledLightData(reservoir);
    if (!RTXDI_SampledLightData_IsValidLightData(sampledLightData))
    {
        return float4(1.0, 1.0, 1.0, 1.0);
    }

    const uint lightIndex = RTXDI_SampledLightData_GetLightIndex(sampledLightData);
    if (lightIndex >= RAB_GetCurrentLightCount())
    {
        return float4(1.0, 0.0, 0.75, 1.0);
    }

    const RAB_LightInfo lightInfo = RAB_LoadLightInfo(lightIndex, false);
    if (!RAB_IsLightInfoValid(lightInfo))
    {
        return float4(0.55, 0.05, 0.80, 1.0);
    }

    const RAB_LightSample lightSample = RAB_SamplePolymorphicLight(
        lightInfo,
        surface,
        RTXDI_SampledLightData_GetUVDataFloat2(sampledLightData));
    if (lightSample.valid == 0u)
    {
        return float4(1.0, 0.45, 0.0, 1.0);
    }

    float3 lightDir;
    float lightDistance;
    const bool geometryVisible = RAB_GetConservativeLightSampleGeometry(surface, lightSample, lightDir, lightDistance);
    if (!geometryVisible)
    {
        return float4(0.45, 0.05, 0.75, 1.0);
    }

    const bool candidateVisible = RAB_GetCandidateNeeVisibility(surface, lightSample);
    const bool selectedVisible = RAB_GetSelectedNeeVisibility(surface, lightSample);
    const bool strictVisible = RAB_GetConservativeVisibility(surface, lightSample);
    if (strictVisible && candidateVisible && selectedVisible)
    {
        return float4(0.05, 0.75, 0.12, 1.0);
    }
    if (!strictVisible && candidateVisible && selectedVisible)
    {
        return float4(0.05, 0.45, 0.95, 1.0);
    }
    if (!strictVisible && candidateVisible && !selectedVisible)
    {
        return float4(0.95, 0.85, 0.05, 1.0);
    }
    if (!strictVisible && !candidateVisible && !selectedVisible)
    {
        return float4(0.95, 0.22, 0.04, 1.0);
    }
    return float4(1.0, 0.0, 0.75, 1.0);
}

float4 RestirPTReferenceLightSampleNumericColor(RAB_Surface surface, RTXDI_PTReservoir reservoir)
{
    if (!RAB_IsSurfaceValid(surface))
    {
        return float4(0.0, 0.0, 0.0, 1.0);
    }
    if (!RTXDI_IsValidPTReservoir(reservoir))
    {
        return float4(0.85, 0.02, 0.02, 1.0);
    }
    if (!RTXDI_ConnectsToNeeLight(reservoir))
    {
        return float4(0.05, 0.18, 0.75, 1.0);
    }

    const RTXDI_SampledLightData sampledLightData = RTXDI_GetSampledLightData(reservoir);
    if (!RTXDI_SampledLightData_IsValidLightData(sampledLightData))
    {
        return float4(1.0, 1.0, 1.0, 1.0);
    }

    const uint lightIndex = RTXDI_SampledLightData_GetLightIndex(sampledLightData);
    if (lightIndex >= RAB_GetCurrentLightCount())
    {
        return float4(1.0, 0.0, 0.75, 1.0);
    }

    const RAB_LightInfo lightInfo = RAB_LoadLightInfo(lightIndex, false);
    if (!RAB_IsLightInfoValid(lightInfo))
    {
        return float4(0.55, 0.05, 0.80, 1.0);
    }

    const RAB_LightSample lightSample = RAB_SamplePolymorphicLight(
        lightInfo,
        surface,
        RTXDI_SampledLightData_GetUVDataFloat2(sampledLightData));
    if (lightSample.valid == 0u)
    {
        return float4(1.0, 0.45, 0.0, 1.0);
    }
    if (!all(lightSample.position == lightSample.position) || any(abs(lightSample.position) > 1.0e20))
    {
        return float4(0.95, 0.85, 0.05, 1.0);
    }
    if (!all(lightSample.radiance == lightSample.radiance) || any(abs(lightSample.radiance) > 65504.0))
    {
        return float4(1.0, 1.0, 1.0, 1.0);
    }
    if (!RAB_IsFinitePositive(lightSample.solidAnglePdf))
    {
        return float4(0.85, 0.02, 0.02, 1.0);
    }
    if (!RAB_IsFinitePositive(lightSample.distance))
    {
        return float4(0.0, 0.75, 0.95, 1.0);
    }

    float3 lightDir;
    float lightDistance;
    RAB_GetLightDirDistance(surface, lightSample, lightDir, lightDistance);
    if (!all(lightDir == lightDir) || !RAB_IsFinitePositive(lightDistance))
    {
        return float4(0.45, 0.05, 0.75, 1.0);
    }

    const float targetPdf = RAB_GetLightSampleTargetPdfForSurface(lightSample, surface);
    if (targetPdf != targetPdf || abs(targetPdf) > 65504.0)
    {
        return float4(0.95, 0.22, 0.04, 1.0);
    }
    if (targetPdf <= 0.0)
    {
        return float4(0.05, 0.45, 0.95, 1.0);
    }

    return lightSample.lightType == RAB_LIGHT_TYPE_DOOM_ANALYTIC_SPHERE
        ? float4(0.05, 0.80, 0.95, 1.0)
        : float4(0.05, 0.75, 0.12, 1.0);
}

float4 RestirPTReferenceEmissiveHitMapColor(RAB_Surface surface)
{
    if (!RAB_IsSurfaceValid(surface))
    {
        return float4(0.0, 0.0, 0.0, 1.0);
    }

    const float visibleEmissive = RAB_Luminance(max(surface.material.emissiveRadiance, float3(0.0, 0.0, 0.0)));
    if (visibleEmissive <= 0.0)
    {
        return float4(0.05, 0.18, 0.75, 1.0);
    }

    const uint emissiveTriangleCount = RAB_GetCurrentEmissiveTriangleCount();
    if (emissiveTriangleCount == 0u)
    {
        return float4(0.85, 0.02, 0.02, 1.0);
    }

    uint matchCount = 0u;
    uint firstMatch = RAB_INVALID_LIGHT_INDEX;
    bool dynamicMatch = false;
    [loop]
    for (uint lightIndex = 0u; lightIndex < emissiveTriangleCount; ++lightIndex)
    {
        const PathTraceSmokeEmissiveTriangle candidate = SmokeEmissiveTriangles[lightIndex];
        const bool identityMatch =
            candidate.instanceId == surface.instanceId &&
            candidate.primitiveIndex == surface.primitiveIndex &&
            candidate.materialId == surface.materialId;
        if (identityMatch)
        {
            if (firstMatch == RAB_INVALID_LIGHT_INDEX)
            {
                firstMatch = lightIndex;
            }
            dynamicMatch = dynamicMatch || ((candidate.padding0 & RT_SMOKE_EMISSIVE_TRIANGLE_HISTORY_DYNAMIC) != 0u);
            ++matchCount;
        }
    }

    if (matchCount == 0u)
    {
        return float4(1.0, 0.45, 0.0, 1.0);
    }
    if (matchCount > 1u)
    {
        return float4(0.95, 0.85, 0.05, 1.0);
    }

    const RAB_LightInfo lightInfo = RAB_LoadLightInfo(firstMatch, false);
    if (!RAB_IsLightInfoValid(lightInfo))
    {
        return float4(0.55, 0.05, 0.80, 1.0);
    }
    if (!all(lightInfo.position == lightInfo.position) ||
        !all(lightInfo.radiance == lightInfo.radiance) ||
        lightInfo.area <= 0.0)
    {
        return float4(1.0, 0.0, 0.75, 1.0);
    }
    if (RAB_Luminance(max(lightInfo.radiance, float3(0.0, 0.0, 0.0))) <= 0.0)
    {
        return float4(1.0, 1.0, 1.0, 1.0);
    }

    return dynamicMatch ? float4(0.05, 0.80, 0.95, 1.0) : float4(0.05, 0.75, 0.12, 1.0);
}

float4 RestirPTReferenceCurrentToPreviousLightFailureColor(uint lightIndex)
{
    const uint currentEmissiveTriangleCount = RAB_GetCurrentEmissiveTriangleCount();
    if (lightIndex < currentEmissiveTriangleCount)
    {
        const PathTraceEmissiveLightRemap remap = SmokeEmissiveRemap[lightIndex];
        if (!RAB_SmokeEmissiveRemapValid(remap))
        {
            return float4(0.55, 0.05, 0.80, 1.0);
        }
        if (remap.currentToPreviousIndex < 0)
        {
            return float4(1.0, 0.45, 0.0, 1.0);
        }
        return float4(0.95, 0.22, 0.04, 1.0);
    }

    const uint analyticCount = RAB_GetCurrentDoomAnalyticLightCount();
    const uint currentIdentityCount = (uint)max(DoomAnalyticLightRemapInfo.x, 0.0);
    const uint previousIdentityCount = (uint)max(DoomAnalyticLightRemapInfo.y, 0.0);
    const uint remapCount = (uint)max(DoomAnalyticLightRemapInfo.z, 0.0);
    const uint currentAnalyticIndex = lightIndex - currentEmissiveTriangleCount;
    if (currentAnalyticIndex >= analyticCount || currentAnalyticIndex >= currentIdentityCount)
    {
        return float4(0.85, 0.02, 0.02, 1.0);
    }

    const PathTraceDoomAnalyticLightCandidateIdentity currentIdentity = DoomAnalyticCurrentIdentities[currentAnalyticIndex];
    if (!RAB_DoomAnalyticIdentitySampleable(currentIdentity))
    {
        return float4(1.0, 1.0, 1.0, 1.0);
    }
    if (currentIdentity.universeIndex >= remapCount)
    {
        return float4(1.0, 0.0, 0.75, 1.0);
    }

    const PathTraceDoomAnalyticLightRemap remap = DoomAnalyticRemap[currentIdentity.universeIndex];
    if (!RAB_DoomAnalyticRemapValid(remap) || remap.currentToPreviousCandidateIndex < 0)
    {
        const uint reason = currentIdentity.invalidReasonFlags | remap.invalidReasonFlags;
        if ((reason & 0x00000001u) != 0u)
        {
            return float4(1.0, 0.45, 0.0, 1.0);
        }
        if ((reason & 0x00000004u) != 0u)
        {
            return float4(0.95, 0.85, 0.05, 1.0);
        }
        if ((reason & 0x00000010u) != 0u)
        {
            return float4(0.15, 0.25, 1.0, 1.0);
        }
        if ((reason & 0x00000400u) != 0u)
        {
            return float4(0.05, 0.45, 0.95, 1.0);
        }
        return float4(0.55, 0.05, 0.80, 1.0);
    }

    const uint previousAnalyticIndex = (uint)remap.currentToPreviousCandidateIndex;
    if (previousAnalyticIndex >= previousIdentityCount)
    {
        return float4(1.0, 0.0, 0.75, 1.0);
    }
    if (!RAB_DoomAnalyticLightStateCompatible(DoomAnalyticLights[currentAnalyticIndex], DoomAnalyticPreviousLights[previousAnalyticIndex]))
    {
        return float4(0.05, 0.45, 0.95, 1.0);
    }

    return float4(0.55, 0.05, 0.80, 1.0);
}

float4 RestirPTReferenceLightDomainLoadColor(uint2 pixel)
{
    const uint2 dimensions = PathTraceFullOutputSize();
    const uint currentLightCount = RAB_GetCurrentLightCount();
    if (currentLightCount == 0u || dimensions.x == 0u || dimensions.y == 0u)
    {
        return float4(0.0, 0.0, 0.0, 1.0);
    }

    const bool previousFrame = pixel.y >= (dimensions.y >> 1u);
    const uint halfHeight = max(dimensions.y >> 1u, 1u);
    const uint localY = previousFrame ? pixel.y - halfHeight : pixel.y;
    const uint cellSize = 12u;
    const uint tileX = pixel.x / cellSize;
    const uint tileY = min(localY, halfHeight - 1u) / cellSize;
    const uint tilesX = max((dimensions.x + cellSize - 1u) / cellSize, 1u);
    const uint lightIndex = tileY * tilesX + tileX;
    if (lightIndex >= currentLightCount)
    {
        return float4(0.0, 0.0, 0.0, 1.0);
    }

    const uint emissiveCount = RAB_GetCurrentEmissiveTriangleCount();
    const RAB_LightInfo lightInfo = RAB_LoadLightInfo(lightIndex, previousFrame);
    if (!RAB_IsLightInfoValid(lightInfo))
    {
        if (previousFrame)
        {
            return RestirPTReferenceCurrentToPreviousLightFailureColor(lightIndex);
        }
        return float4(0.55, 0.05, 0.80, 1.0);
    }
    if (!all(lightInfo.position == lightInfo.position) || !all(lightInfo.radiance == lightInfo.radiance))
    {
        return float4(1.0, 0.0, 0.75, 1.0);
    }
    if (lightInfo.area <= 0.0 || lightInfo.weight <= 0.0)
    {
        return float4(0.95, 0.85, 0.05, 1.0);
    }
    if (RAB_Luminance(max(lightInfo.radiance, float3(0.0, 0.0, 0.0))) <= 0.0)
    {
        return float4(1.0, 1.0, 1.0, 1.0);
    }
    return lightIndex < emissiveCount ? float4(0.05, 0.75, 0.12, 1.0) : float4(0.05, 0.80, 0.95, 1.0);
}

float4 RestirPTReferenceLightDomainSampleColor(RAB_Surface surface, uint2 pixel)
{
    if (!RAB_IsSurfaceValid(surface))
    {
        return float4(0.0, 0.0, 0.0, 1.0);
    }

    const uint2 dimensions = PathTraceFullOutputSize();
    const uint currentLightCount = RAB_GetCurrentLightCount();
    if (currentLightCount == 0u || dimensions.x == 0u || dimensions.y == 0u)
    {
        return float4(0.0, 0.0, 0.0, 1.0);
    }

    const uint cellSize = 12u;
    const uint tileX = pixel.x / cellSize;
    const uint tileY = pixel.y / cellSize;
    const uint tilesX = max((dimensions.x + cellSize - 1u) / cellSize, 1u);
    const uint lightIndex = tileY * tilesX + tileX;
    if (lightIndex >= currentLightCount)
    {
        return float4(0.0, 0.0, 0.0, 1.0);
    }

    const uint emissiveCount = RAB_GetCurrentEmissiveTriangleCount();
    const RAB_LightInfo lightInfo = RAB_LoadLightInfo(lightIndex, false);
    if (!RAB_IsLightInfoValid(lightInfo))
    {
        return float4(0.55, 0.05, 0.80, 1.0);
    }

    const RAB_LightSample lightSample = RAB_SamplePolymorphicLight(lightInfo, surface, float2(0.37, 0.61));
    if (lightSample.valid == 0u)
    {
        return float4(1.0, 0.45, 0.0, 1.0);
    }
    if (!all(lightSample.position == lightSample.position) ||
        !all(lightSample.radiance == lightSample.radiance))
    {
        return float4(1.0, 0.0, 0.75, 1.0);
    }
    if (!RAB_IsFinitePositive(lightSample.solidAnglePdf) ||
        !RAB_IsFinitePositive(lightSample.distance))
    {
        return float4(0.95, 0.85, 0.05, 1.0);
    }
    if (RAB_Luminance(max(lightSample.radiance, float3(0.0, 0.0, 0.0))) <= 0.0)
    {
        return float4(1.0, 1.0, 1.0, 1.0);
    }

    float3 lightDir;
    float lightDistance;
    RAB_GetLightDirDistance(surface, lightSample, lightDir, lightDistance);
    if (!all(lightDir == lightDir) || !RAB_IsFinitePositive(lightDistance))
    {
        return float4(0.85, 0.02, 0.02, 1.0);
    }

    const float targetPdf = RAB_GetLightSampleTargetPdfForSurface(lightSample, surface);
    if (targetPdf != targetPdf || abs(targetPdf) > 65504.0)
    {
        return float4(0.05, 0.45, 0.95, 1.0);
    }
    if (targetPdf <= 0.0)
    {
        if (!RAB_SurfaceSupportsOpaqueDiffuseBrdf(surface))
        {
            return float4(0.35, 0.05, 0.75, 1.0);
        }

        const float3 surfaceNormal = RAB_SafeNormalize(RAB_GetSurfaceNormal(surface), RAB_GetSurfaceGeoNormal(surface));
        const float3 surfaceGeoNormal = RAB_GetSurfaceGeoNormal(surface);
        const float3 surfaceViewDir = RAB_GetSurfaceViewDir(surface);
        if (dot(surfaceNormal, lightDir) <= 0.0)
        {
            return float4(0.02, 0.08, 0.45, 1.0);
        }
        if (dot(surfaceGeoNormal, lightDir) <= 0.0)
        {
            return float4(0.00, 0.35, 0.55, 1.0);
        }
        if (dot(surfaceNormal, surfaceViewDir) <= 0.0 || dot(surfaceGeoNormal, surfaceViewDir) <= 0.0)
        {
            return float4(0.55, 0.22, 0.75, 1.0);
        }
        return float4(0.15, 0.25, 1.0, 1.0);
    }

    return lightIndex < emissiveCount ? float4(0.05, 0.75, 0.12, 1.0) : float4(0.05, 0.80, 0.95, 1.0);
}

float4 RestirPTReferenceLightDomainVisibilityColor(RAB_Surface surface, uint2 pixel)
{
    if (!RAB_IsSurfaceValid(surface))
    {
        return float4(0.0, 0.0, 0.0, 1.0);
    }

    const uint2 dimensions = PathTraceFullOutputSize();
    const uint currentLightCount = RAB_GetCurrentLightCount();
    if (currentLightCount == 0u || dimensions.x == 0u || dimensions.y == 0u)
    {
        return float4(0.0, 0.0, 0.0, 1.0);
    }

    const uint cellSize = 12u;
    const uint tileX = pixel.x / cellSize;
    const uint tileY = pixel.y / cellSize;
    const uint tilesX = max((dimensions.x + cellSize - 1u) / cellSize, 1u);
    const uint lightIndex = tileY * tilesX + tileX;
    if (lightIndex >= currentLightCount)
    {
        return float4(0.0, 0.0, 0.0, 1.0);
    }

    const RAB_LightInfo lightInfo = RAB_LoadLightInfo(lightIndex, false);
    if (!RAB_IsLightInfoValid(lightInfo))
    {
        return float4(0.55, 0.05, 0.80, 1.0);
    }

    const RAB_LightSample lightSample = RAB_SamplePolymorphicLight(lightInfo, surface, float2(0.37, 0.61));
    if (lightSample.valid == 0u)
    {
        return float4(1.0, 0.45, 0.0, 1.0);
    }

    float3 lightDir;
    float lightDistance;
    const bool geometryVisible = RAB_GetConservativeLightSampleGeometry(surface, lightSample, lightDir, lightDistance);
    if (!geometryVisible)
    {
        return float4(0.45, 0.05, 0.75, 1.0);
    }

    const bool candidateVisible = RAB_GetCandidateNeeVisibility(surface, lightSample);
    const bool selectedVisible = RAB_GetSelectedNeeVisibility(surface, lightSample);
    const bool strictVisible = RAB_GetConservativeVisibility(surface, lightSample);
    if (strictVisible && candidateVisible && selectedVisible)
    {
        return float4(0.05, 0.75, 0.12, 1.0);
    }
    if (!strictVisible && candidateVisible && selectedVisible)
    {
        return float4(0.05, 0.45, 0.95, 1.0);
    }
    if (!strictVisible && candidateVisible && !selectedVisible)
    {
        return float4(0.95, 0.85, 0.05, 1.0);
    }
    if (!strictVisible && !candidateVisible && !selectedVisible)
    {
        return float4(0.95, 0.22, 0.04, 1.0);
    }
    if (strictVisible && !candidateVisible && !selectedVisible)
    {
        return float4(0.85, 0.02, 0.02, 1.0);
    }
    if (strictVisible && !candidateVisible && selectedVisible)
    {
        return float4(1.0, 0.0, 0.75, 1.0);
    }
    if (strictVisible && candidateVisible && !selectedVisible)
    {
        return float4(0.55, 0.22, 0.75, 1.0);
    }
    if (!strictVisible && !candidateVisible && selectedVisible)
    {
        return float4(1.0, 1.0, 1.0, 1.0);
    }

    return float4(1.0, 0.0, 0.75, 1.0);
}

RTXDI_PTReservoir GenerateRestirPTDiTemporalReservoir(RAB_Surface surface, uint2 pixel, out uint status)
{
    status = 0u;
    const uint currentIndex = RestirPTGiTemporalCurrentBufferIndex();
    const RTXDI_PTReservoir currentReservoir = LoadRestirPTRawDirectReservoir(pixel);
    if (!RAB_IsSurfaceValid(surface) || !RAB_SurfaceSupportsOpaqueDiffuseBrdf(surface))
    {
        StoreRestirPTDiTemporalReservoir(pixel, currentIndex, RTXDI_EmptyPTReservoir());
        status = 1u;
        return RTXDI_EmptyPTReservoir();
    }

    RTXDI_PTReservoir temporalReservoir = RTXDI_EmptyPTReservoir();
    float3 selectedTargetFunction = float3(0.0, 0.0, 0.0);
    const bool currentValid = RestirPTDirectReservoirHasUsefulSample(currentReservoir);
    if (currentValid && CombineReservoirs(temporalReservoir, currentReservoir, RestirPTGiHash01(pixel, 0x444901u), currentReservoir.TargetFunction))
    {
        selectedTargetFunction = currentReservoir.TargetFunction;
    }

    float piSum = currentValid ? RTXDI_Luminance(currentReservoir.TargetFunction) * max((float)currentReservoir.M, 1.0) : 0.0;
    int2 previousPixel;
    float2 motionPixels;
    const bool projected = RestirPTTryProjectMotionVectorToPreviousPixel(pixel, previousPixel, motionPixels);
    const bool samePixelFallbackAllowed = RestirPTTemporalSamePixelFallbackAllowed(surface, pixel, projected, motionPixels);
    bool acceptedPrevious = false;
    uint acceptedPreviousStatus = 0u;
    if (projected)
    {
        const RAB_Surface previousSurface = LoadPathTracePrimarySurfaceRecord(previousPixel, true);
        uint similarityStatus;
        if (PathTracePrimarySurfacesAreSimilar(surface, previousSurface, similarityStatus))
        {
            const RTXDI_PTReservoir previousReservoir = LoadRestirPTDiTemporalReservoir(uint2(previousPixel), RestirPTGiTemporalPreviousBufferIndex());
            if (RestirPTDirectReservoirHasUsefulSample(previousReservoir))
            {
                acceptedPrevious = true;
                acceptedPreviousStatus = 3u;
                const uint cappedPreviousM = min(previousReservoir.M, RestirPTParams.temporalResampling.maxHistoryLength);
                RTXDI_PTReservoir previousCandidate = previousReservoir;
                previousCandidate.M = max(cappedPreviousM, 1u);
                if (CombineReservoirs(temporalReservoir, previousCandidate, RestirPTGiHash01(pixel, 0x554411u), previousCandidate.TargetFunction))
                {
                    selectedTargetFunction = previousCandidate.TargetFunction;
                }
                piSum += RTXDI_Luminance(previousCandidate.TargetFunction) * max((float)previousCandidate.M, 1.0);
            }
        }
    }
    if (!acceptedPrevious && samePixelFallbackAllowed)
    {
        const RTXDI_PTReservoir previousReservoir = LoadRestirPTDiTemporalReservoir(pixel, RestirPTGiTemporalPreviousBufferIndex());
        if (RestirPTDirectReservoirHasUsefulSample(previousReservoir))
        {
            acceptedPrevious = true;
            acceptedPreviousStatus = 4u;
            const uint cappedPreviousM = min(previousReservoir.M, RestirPTParams.temporalResampling.maxHistoryLength);
            RTXDI_PTReservoir previousCandidate = previousReservoir;
            previousCandidate.M = max(cappedPreviousM, 1u);
            if (CombineReservoirs(temporalReservoir, previousCandidate, RestirPTGiHash01(pixel, 0x884221u), previousCandidate.TargetFunction))
            {
                selectedTargetFunction = previousCandidate.TargetFunction;
            }
            piSum += RTXDI_Luminance(previousCandidate.TargetFunction) * max((float)previousCandidate.M, 1.0);
        }
    }

    const float pi = RTXDI_Luminance(selectedTargetFunction);
    RTXDI_FinalizeResampling(temporalReservoir, pi, piSum * max(pi, 1.0e-6));
    if (!RTXDI_IsValidPTReservoir(temporalReservoir))
    {
        StoreRestirPTDiTemporalReservoir(pixel, currentIndex, RTXDI_EmptyPTReservoir());
        status = 1u;
        return RTXDI_EmptyPTReservoir();
    }

    const float currentM = currentValid ? currentReservoir.M : 0.0;
    temporalReservoir.M = min(max(temporalReservoir.M, currentM), RestirPTParams.temporalResampling.maxHistoryLength);
    StoreRestirPTDiTemporalReservoir(pixel, currentIndex, temporalReservoir);
    status = acceptedPrevious ? acceptedPreviousStatus : 2u;
    return temporalReservoir;
}

float4 RestirPTDiTemporalStatusColor(RTXDI_PTReservoir reservoir, uint status)
{
    const float history = saturate((float)reservoir.M / max((float)RestirPTParams.temporalResampling.maxHistoryLength, 1.0));
    if (status == 1u)
    {
        return float4(0.45, 0.02, 0.02, 1.0);
    }
    if (status == 3u)
    {
        return float4(0.05, 0.25 + 0.75 * history, 0.08, 1.0);
    }
    if (status == 4u)
    {
        return float4(0.85, 0.55 + 0.35 * history, 0.05, 1.0);
    }
    return float4(0.04, 0.10 + 0.35 * history, 0.75, 1.0);
}

float4 RestirPTDiSpatialStatusColor(RTXDI_PTReservoir reservoir, uint status)
{
    const float history = saturate((float)reservoir.M / max((float)RestirPTParams.temporalResampling.maxHistoryLength, 1.0));
    if (status == 1u)
    {
        return float4(0.45, 0.02, 0.02, 1.0);
    }
    if (status == 3u)
    {
        return float4(0.05, 0.65 + 0.35 * history, 0.75, 1.0);
    }
    return float4(0.04, 0.10 + 0.35 * history, 0.75, 1.0);
}

RTXDI_PTReservoir GenerateRestirPTDiSpatialReservoir(RAB_Surface surface, uint2 pixel, out uint status)
{
    status = 0u;
    const RTXDI_PTReservoir temporalReservoir = LoadRestirPTDiTemporalReservoir(pixel, RestirPTGiTemporalCurrentBufferIndex());
    if (!RestirPTDirectReservoirHasUsefulSample(temporalReservoir))
    {
        status = 1u;
        return RTXDI_EmptyPTReservoir();
    }

    RTXDI_PTReservoir spatialReservoir = RTXDI_EmptyPTReservoir();
    float3 selectedTargetFunction = float3(0.0, 0.0, 0.0);
    float3 currentTargetFunction;
    if (!RestirPTTryEvaluateNeeReservoirTargetFunction(surface, temporalReservoir, RestirPTInfo.z >= 0.5, currentTargetFunction))
    {
        currentTargetFunction = temporalReservoir.TargetFunction;
    }
    if (CombineReservoirs(spatialReservoir, temporalReservoir, RestirPTGiHash01(pixel, 0x392211u), currentTargetFunction))
    {
        selectedTargetFunction = currentTargetFunction;
    }

    float piSum = RTXDI_Luminance(currentTargetFunction) * max((float)temporalReservoir.M, 1.0);
    uint acceptedNeighbors = 0u;
    const uint2 dimensions = PathTraceFullOutputSize();
    const uint sampleCount = clamp(RestirPTParams.spatialResampling.numSpatialSamples, 1u, 8u);
    const float sampleRadius = max(RestirPTParams.spatialResampling.samplingRadius, 1.0);

    [loop]
    for (uint sampleIndex = 0u; sampleIndex < sampleCount; ++sampleIndex)
    {
        const float angle = RestirPTGiHash01(pixel, 0x6123a1u + sampleIndex * 17u) * 6.28318530718;
        const float radius = max(1.0, sqrt(RestirPTGiHash01(pixel, 0x9427c3u + sampleIndex * 31u)) * sampleRadius);
        int2 neighborPixel = int2(pixel) + int2(round(float2(cos(angle), sin(angle)) * radius));
        neighborPixel = clamp(neighborPixel, int2(0, 0), int2(dimensions) - int2(1, 1));
        if (all(neighborPixel == int2(pixel)))
        {
            continue;
        }

        const RAB_Surface neighborSurface = LoadPathTracePrimarySurfaceRecord(neighborPixel, false);
        uint similarityStatus;
        if (!PathTracePrimarySurfacesAreSimilar(surface, neighborSurface, similarityStatus))
        {
            continue;
        }

        const RTXDI_PTReservoir neighborReservoir = LoadRestirPTDiTemporalReservoir(uint2(neighborPixel), RestirPTGiTemporalCurrentBufferIndex());
        if (!RestirPTDirectReservoirHasUsefulSample(neighborReservoir))
        {
            continue;
        }

        float3 neighborTargetFunction;
        if (!RestirPTTryEvaluateNeeReservoirTargetFunction(surface, neighborReservoir, RestirPTInfo.z >= 0.5, neighborTargetFunction))
        {
            continue;
        }

        if (CombineReservoirs(spatialReservoir, neighborReservoir, RestirPTGiHash01(pixel, 0x827331u + sampleIndex * 43u), neighborTargetFunction))
        {
            selectedTargetFunction = neighborTargetFunction;
        }
        piSum += RTXDI_Luminance(neighborTargetFunction) * max((float)neighborReservoir.M, 1.0);
        ++acceptedNeighbors;
    }

    const float pi = RTXDI_Luminance(selectedTargetFunction);
    RTXDI_FinalizeResampling(spatialReservoir, pi, piSum * max(pi, 1.0e-6));
    spatialReservoir.M = min(max(spatialReservoir.M, temporalReservoir.M), RestirPTParams.temporalResampling.maxHistoryLength);
    status = acceptedNeighbors > 0u ? 3u : 2u;
    return spatialReservoir;
}

float4 EvaluateRestirPTDiDebugView(RAB_Surface surface, uint2 pixel, uint view)
{
    if (view == 13u)
    {
        return EvaluateRestirPTReferenceBrdfContractView(surface);
    }
    if (view == 14u)
    {
        return EvaluateRestirPTReferenceNeeSampleContractView(surface, pixel);
    }
    if (view == 15u)
    {
        return EvaluateRestirPTReferencePathTraceMetadataView(pixel);
    }
    if (view == 16u)
    {
        const uint2 reservoirPixel = PathTraceFullPixelToRestirDirectPixel(pixel);
        const RTXDI_PTReservoir reservoir = LoadRestirPTFinalShadingInputReservoir(reservoirPixel);
        return RestirPTReferenceFinalShadingContractColor(surface, reservoir);
    }
    if (view == 17u)
    {
        const uint2 reservoirPixel = PathTraceFullPixelToRestirDirectPixel(pixel);
        const RTXDI_PTReservoir reservoir = LoadRestirPTFinalShadingInputReservoir(reservoirPixel);
        return RestirPTReferenceTargetReplayColor(surface, reservoir);
    }
    if (view == 18u)
    {
        return EvaluateRestirPTEnvironmentMisContractView(surface, pixel);
    }
    if (view == 19u)
    {
        const uint2 reservoirPixel = PathTraceFullPixelToRestirDirectPixel(pixel);
        const RTXDI_PTReservoir reservoir = LoadRestirPTFinalShadingInputReservoir(reservoirPixel);
        return RestirPTReferenceNeeSampleContractColor(surface, reservoir);
    }
    if (view == 20u)
    {
        const uint2 reservoirPixel = PathTraceFullPixelToRestirDirectPixel(pixel);
        const RTXDI_PTReservoir reservoir = LoadRestirPTFinalShadingInputReservoir(reservoirPixel);
        return RestirPTReferencePathTraceMetadataColor(reservoir);
    }
    if (view == 21u)
    {
        const uint2 reservoirPixel = PathTraceFullPixelToRestirDirectPixel(pixel);
        const RTXDI_PTReservoir reservoir = LoadRestirPTFinalShadingInputReservoir(reservoirPixel);
        return RestirPTReferenceRandomReplayMetadataColor(reservoir);
    }
    if (view == 22u)
    {
        return RestirPTReferenceRandomSamplerBridgeColor(pixel);
    }
    if (view == 23u)
    {
        return RestirPTReferencePathTracerUserDataBridgeColor();
    }
    if (view == 24u)
    {
        const uint2 reservoirPixel = PathTraceFullPixelToRestirDirectPixel(pixel);
        const RTXDI_PTReservoir reservoir = LoadRestirPTFinalShadingInputReservoir(reservoirPixel);
        return RestirPTReferenceVisibilityPolicyColor(surface, reservoir);
    }
    if (view == 25u)
    {
        const uint2 reservoirPixel = PathTraceFullPixelToRestirDirectPixel(pixel);
        const RTXDI_PTReservoir reservoir = LoadRestirPTFinalShadingInputReservoir(reservoirPixel);
        return RestirPTReferenceLightSampleNumericColor(surface, reservoir);
    }
    if (view == 26u)
    {
        return RestirPTReferenceEmissiveHitMapColor(surface);
    }
    if (view == 27u)
    {
        return RestirPTReferenceLightDomainLoadColor(pixel);
    }
    if (view == 28u)
    {
        return RestirPTReferenceLightDomainSampleColor(surface, pixel);
    }
    if (view == 29u)
    {
        return RestirPTReferenceLightDomainVisibilityColor(surface, pixel);
    }
    if (view == 30u)
    {
        const uint2 reservoirPixel = PathTraceFullPixelToRestirDirectPixel(pixel);
        const RTXDI_PTReservoir reservoir = LoadRestirPTInitialDirectReservoir(reservoirPixel);
        return RestirPTReferenceNeeRecordScalarColor(reservoir);
    }
    if (view == 31u)
    {
        return RestirPTReferencePathTracerEntryColor(surface, pixel);
    }
    if (view == 32u)
    {
        return RestirPTReferencePsrDenoiserPayloadColor(pixel);
    }
    if (view == 33u)
    {
        return RestirPTReferencePathTracerContinuationPolicyColor(pixel);
    }
    if (view == 34u)
    {
        return RestirPTReferenceFinalShadingOutputColor(surface, pixel);
    }
    if (view == 35u)
    {
        return RestirPTReferenceRemainingSupplierGapColor(pixel);
    }

    if (!RAB_IsSurfaceValid(surface) || !RAB_SurfaceSupportsOpaqueDiffuseBrdf(surface))
    {
        return view == 4u ? float4(0.20, 0.0, 0.0, 1.0) : float4(0.0, 0.0, 0.0, 1.0);
    }

    const RTXDI_PTReservoir reservoir = LoadRestirPTRawDirectReservoir(pixel);
    const bool valid = RestirPTDirectReservoirHasUsefulSample(reservoir);

    if (view == 1u)
    {
        float3 contribution = float3(0.0, 0.0, 0.0);
        const bool evaluated = valid && RestirPTTryEvaluateNeeReservoirPreview(surface, reservoir, RestirPTInfo.z >= 0.5, contribution);
        return float4(RestirPTToneMapPreview(evaluated ? contribution : float3(0.0, 0.0, 0.0)), 1.0);
    }

    if (view == 2u)
    {
        uint temporalStatus;
        const RTXDI_PTReservoir temporalReservoir = GenerateRestirPTDiTemporalReservoir(surface, pixel, temporalStatus);
        const bool temporalValid = RestirPTDirectReservoirHasUsefulSample(temporalReservoir);
        float3 contribution = float3(0.0, 0.0, 0.0);
        const bool evaluated = temporalValid &&
            RestirPTTryEvaluateNeeReservoirPreview(surface, temporalReservoir, RestirPTInfo.z >= 0.5, contribution);
        if (!evaluated && temporalValid)
        {
            contribution = RestirPTReservoirPreviewContribution(temporalReservoir);
        }
        return float4(RestirPTToneMapPreview(contribution), 1.0);
    }

    if (view == 3u)
    {
        uint spatialStatus;
        const RTXDI_PTReservoir spatialReservoir = GenerateRestirPTDiSpatialReservoir(surface, pixel, spatialStatus);
        const bool spatialValid = RestirPTDirectReservoirHasUsefulSample(spatialReservoir);
        float3 contribution = float3(0.0, 0.0, 0.0);
        const bool evaluated = spatialValid &&
            RestirPTTryEvaluateNeeReservoirPreview(surface, spatialReservoir, RestirPTInfo.z >= 0.5, contribution);
        if (!evaluated && spatialValid)
        {
            contribution = RestirPTReservoirPreviewContribution(spatialReservoir);
        }
        return float4(RestirPTToneMapPreview(contribution), 1.0);
    }

    if (view == 5u)
    {
        const RTXDI_PTReservoir temporalReservoir = LoadRestirPTDiTemporalReservoir(pixel, RestirPTGiTemporalCurrentBufferIndex());
        const uint temporalStatus = RestirPTDirectReservoirHasUsefulSample(temporalReservoir) ? 2u : 1u;
        uint spatialStatus;
        const RTXDI_PTReservoir spatialReservoir = GenerateRestirPTDiSpatialReservoir(surface, pixel, spatialStatus);
        return RestirPTDirectReservoirHasUsefulSample(spatialReservoir)
            ? RestirPTDiSpatialStatusColor(spatialReservoir, spatialStatus)
            : RestirPTDiTemporalStatusColor(temporalReservoir, temporalStatus);
    }

    if (view == 6u)
    {
        const uint2 reservoirPixel = PathTraceFullPixelToRestirDirectPixel(pixel);
        const RTXDI_PTReservoir reservoir = LoadRestirPTFinalShadingInputReservoir(reservoirPixel);
        const float3 contribution = RestirPTReferenceFinalShadingHasUsefulSample(reservoir)
            ? RestirPTReferenceFinalShadingContribution(reservoir)
            : float3(0.0, 0.0, 0.0);
        return float4(RestirPTToneMapPreview(contribution), 1.0);
    }

    if (view == 7u)
    {
        const uint2 reservoirPixel = PathTraceFullPixelToRestirDirectPixel(pixel);
        const RTXDI_PTReservoir reservoir = LoadRestirPTFinalShadingInputReservoir(reservoirPixel);
        return RestirPTReferenceFinalShadingStateColor(reservoir);
    }

    if (view == 8u)
    {
        return EvaluateRestirPTReferencePageStateView(pixel);
    }

    if (view == 9u)
    {
        return EvaluateRestirPTReferencePageContributionView(pixel);
    }

    if (view == 10u)
    {
        return EvaluateRestirPTReferenceTemporalReadinessView(surface, pixel);
    }

    if (view == 11u)
    {
        return EvaluateRestirPTReferenceTemporalPageFlowView(pixel);
    }

    if (view == 12u)
    {
        return EvaluateRestirPTReferenceTemporalNeighborView(surface, pixel);
    }

    if (view == 4u)
    {
        const float mNorm = saturate((float)reservoir.M / max((float)RestirPTParams.initialSampling.numInitialSamples, 1.0));
        const float weightHeat = saturate(max(reservoir.WeightSum, 0.0) / (1.0 + max(reservoir.WeightSum, 0.0)));
        const float targetHeat = saturate(RTXDI_Luminance(max(reservoir.TargetFunction, float3(0.0, 0.0, 0.0))));
        return valid ? float4(targetHeat, mNorm, weightHeat, 1.0) : float4(0.35, mNorm * 0.25, weightHeat * 0.25, 1.0);
    }

    return float4(0.0, 0.0, 0.0, 1.0);
}

uint RestirPTReflectionPreviewMode()
{
    return (uint)clamp(floor(GeometryInfo3.w + 0.5), 0.0, 2.0);
}

bool RestirPTSurfaceSupportsReflectionPreview(RAB_Surface surface, uint reflectionMode)
{
    if (reflectionMode == 0u || !RAB_IsSurfaceValid(surface) || !RAB_SurfaceSupportsOpaqueDiffuseBrdf(surface))
    {
        return false;
    }

    const float roughnessLimit = reflectionMode == 1u ? 0.35 : 0.70;
    const float roughness = saturate(RAB_GetSurfaceRoughness(surface));
    const float specularMax = max(max(surface.material.specularF0.x, surface.material.specularF0.y), surface.material.specularF0.z);
    return roughness <= roughnessLimit && specularMax >= 0.035;
}

bool RestirPTProjectWorldToCurrentPixel(float3 worldPosition, uint2 dimensions, out float2 pixelFloat, out float forwardDistance)
{
    pixelFloat = float2(-1.0, -1.0);
    forwardDistance = 0.0;
    const float3 delta = worldPosition - CameraOriginAndTMax.xyz;
    forwardDistance = dot(delta, CameraForwardAndTanX.xyz);
    if (forwardDistance <= 0.05)
    {
        return false;
    }

    const float ndcX = -dot(delta, CameraLeftAndTanY.xyz) / max(forwardDistance * CameraForwardAndTanX.w, 1.0e-5);
    const float ndcY = -dot(delta, CameraUpAndDebugMode.xyz) / max(forwardDistance * CameraLeftAndTanY.w, 1.0e-5);
    if (abs(ndcX) > 1.0 || abs(ndcY) > 1.0)
    {
        return false;
    }

    pixelFloat = (float2(ndcX, ndcY) * 0.5 + 0.5) * float2(dimensions);
    return pixelFloat.x >= 0.0 && pixelFloat.y >= 0.0 &&
        pixelFloat.x < (float)dimensions.x && pixelFloat.y < (float)dimensions.y;
}

RestirPTCombinedLighting RestirPTEvaluateCombinedLightingNoReflection(RAB_Surface surface, uint2 pixel)
{
    RestirPTCombinedLighting result;
    result.preview = float3(0.0, 0.0, 0.0);
    result.hdrRadiance = float3(0.0, 0.0, 0.0);

    if (!RAB_IsSurfaceValid(surface))
    {
        return result;
    }

    const float3 emissiveRadiance = RestirPTSanitizeHdrRadiance(surface.material.emissiveRadiance);
    if (!RAB_SurfaceSupportsOpaqueDiffuseBrdf(surface))
    {
        result.preview = RestirPTVisibleSurfaceBase(surface);
        result.hdrRadiance = emissiveRadiance;
        return result;
    }

    const float3 surfaceBase = RestirPTVisibleSurfaceBase(surface);
    float3 directContribution = float3(0.0, 0.0, 0.0);
    const bool directValid = RestirPTTryEvaluateSpatialDirectLighting(surface, pixel, directContribution);
    const RTXDI_PTReservoir giReservoir = LoadRestirPTInitialReservoir(pixel);
    const bool giValid = RestirPTReservoirHasUsefulSample(giReservoir);
    const float giVisibility = (RestirPTInfo.z >= 0.5 && giValid) ? RestirPTTraceReservoirVisibility(surface, giReservoir) : 1.0;
    const bool giVisible = giValid && giVisibility > 0.0;
    const float3 giContribution = giVisible ? RestirPTReservoirPreviewContribution(giReservoir) * giVisibility : float3(0.0, 0.0, 0.0);
    const float3 lightingRadiance =
        (directValid ? directContribution : float3(0.0, 0.0, 0.0)) +
        (giVisible ? giContribution : float3(0.0, 0.0, 0.0));
    result.hdrRadiance = RestirPTSanitizeHdrRadiance(emissiveRadiance + lightingRadiance);
    if (!directValid && !giVisible)
    {
        result.preview = surfaceBase;
        return result;
    }

    result.preview = saturate(surfaceBase + RestirPTToneMapPreview(lightingRadiance));
    return result;
}

float3 RestirPTCombinedLightingNoReflection(RAB_Surface surface, uint2 pixel)
{
    return RestirPTEvaluateCombinedLightingNoReflection(surface, pixel).preview;
}

bool RestirPTTryFindScreenSpaceReflectionHit(RAB_Surface surface, uint2 pixel, uint reflectionMode, out RAB_Surface hitSurface, out uint2 hitPixel)
{
    hitSurface = RAB_EmptySurface();
    hitPixel = pixel;
    if (!RestirPTSurfaceSupportsReflectionPreview(surface, reflectionMode))
    {
        return false;
    }

    const uint2 dimensions = PathTraceFullOutputSize();
    const float3 normal = RAB_SafeNormalize(RAB_GetSurfaceNormal(surface), RAB_GetSurfaceGeoNormal(surface));
    const float3 viewDir = RAB_SafeNormalize(RAB_GetSurfaceViewDir(surface), -normal);
    const float3 reflectionDir = RAB_SafeNormalize(reflect(-viewDir, normal), normal);
    if (dot(reflectionDir, normal) <= 0.01)
    {
        return false;
    }

    const float roughness = saturate(RAB_GetSurfaceRoughness(surface));
    const float3 rayOrigin = surface.worldPos + normal * 0.75 + reflectionDir * 0.25;
    const float maxDistance = min(max(CameraOriginAndTMax.w, 1.0), max(ToyPathInfo.x, 256.0));
    float rayT = 2.0;
    const uint stepCount = reflectionMode == 1u ? 40u : 28u;

    [loop]
    for (uint step = 0u; step < stepCount && rayT < maxDistance; ++step)
    {
        const float3 probePosition = rayOrigin + reflectionDir * rayT;
        float2 probePixelFloat;
        float probeForwardDistance;
        if (RestirPTProjectWorldToCurrentPixel(probePosition, dimensions, probePixelFloat, probeForwardDistance))
        {
            const int2 candidatePixelInt = int2(floor(probePixelFloat));
            if (candidatePixelInt.x >= 0 && candidatePixelInt.y >= 0 &&
                (uint)candidatePixelInt.x < dimensions.x && (uint)candidatePixelInt.y < dimensions.y)
            {
                const RAB_Surface candidate = LoadPathTracePrimarySurfaceRecord(candidatePixelInt, false);
                const bool samePrimitive =
                    candidate.instanceId == surface.instanceId &&
                    candidate.primitiveIndex == surface.primitiveIndex &&
                    candidate.materialId == surface.materialId;
                if (RAB_IsSurfaceValid(candidate) && !samePrimitive)
                {
                    const float3 toCandidate = candidate.worldPos - rayOrigin;
                    const float alongRay = dot(toCandidate, reflectionDir);
                    const float3 closest = rayOrigin + reflectionDir * alongRay;
                    const float missDistance = length(candidate.worldPos - closest);
                    const float thickness = max(2.0, alongRay * (reflectionMode == 1u ? 0.018 : 0.035) + roughness * 24.0);
                    const float normalFacing = dot(RAB_GetSurfaceGeoNormal(candidate), -reflectionDir);
                    if (alongRay > 0.0 && missDistance <= thickness && normalFacing > -0.35)
                    {
                        hitSurface = candidate;
                        hitPixel = uint2(candidatePixelInt);
                        return true;
                    }
                }
            }
        }

        rayT += max(3.0, rayT * (reflectionMode == 1u ? 0.10 : 0.15));
    }

    return false;
}

float3 RestirPTScreenSpaceReflectionPreview(RAB_Surface surface, uint2 pixel)
{
    const uint reflectionMode = RestirPTReflectionPreviewMode();
    RAB_Surface hitSurface;
    uint2 hitPixel;
    if (!RestirPTTryFindScreenSpaceReflectionHit(surface, pixel, reflectionMode, hitSurface, hitPixel))
    {
        return float3(0.0, 0.0, 0.0);
    }

    const float roughness = saturate(RAB_GetSurfaceRoughness(surface));
    const float specularMax = max(max(surface.material.specularF0.x, surface.material.specularF0.y), surface.material.specularF0.z);
    const float roughnessFade = reflectionMode == 1u
        ? saturate((0.38 - roughness) / 0.38)
        : saturate((0.75 - roughness) / 0.75);
    const float reflectionWeight = saturate(specularMax) * roughnessFade;
    return RestirPTCombinedLightingNoReflection(hitSurface, hitPixel) * reflectionWeight;
}

float4 EvaluateCombinedResolve(RAB_Surface surface, uint2 pixel)
{
    const RestirPTCombinedLighting lighting = RestirPTEvaluateCombinedLightingNoReflection(surface, pixel);
    if (!RestirPTSurfaceSupportsReflectionPreview(surface, RestirPTReflectionPreviewMode()))
    {
        PathTraceRRInputColor[pixel] = float4(lighting.hdrRadiance, 1.0);
        return float4(lighting.preview, 1.0);
    }

    const float3 reflectionPreview = RestirPTSanitizeHdrRadiance(RestirPTReflectionOutput[pixel].rgb);
    PathTraceRRInputColor[pixel] = float4(RestirPTSanitizeHdrRadiance(lighting.hdrRadiance + reflectionPreview), 1.0);
    return float4(saturate(lighting.preview + reflectionPreview), 1.0);
}

uint RayReconstructionGuideDebugView()
{
    return clamp((uint)max(RayReconstructionInfo.x, 0.0), 0u, 9u);
}

float4 RayReconstructionMotionMaskDebugColor(uint motionMask)
{
    const bool valid = (motionMask & PT_MOTION_VECTOR_MASK_VALID) != 0u;
    const uint sourceKind = (motionMask & PT_MOTION_VECTOR_MASK_SOURCE_MASK) >> PT_MOTION_VECTOR_MASK_SOURCE_SHIFT;
    if (!valid)
    {
        return sourceKind == 0u ? float4(0.02, 0.02, 0.02, 1.0) : float4(0.65, 0.08, 0.08, 1.0);
    }
    if (sourceKind == PT_MOTION_VECTOR_SOURCE_STATIC)
    {
        return float4(0.10, 0.75, 0.20, 1.0);
    }
    if (sourceKind == PT_MOTION_VECTOR_SOURCE_SKINNED)
    {
        return float4(0.15, 0.45, 1.00, 1.0);
    }
    if (sourceKind == PT_MOTION_VECTOR_SOURCE_RIGID)
    {
        return float4(0.10, 0.85, 0.85, 1.0);
    }
    return float4(0.85, 0.75, 0.15, 1.0);
}

float4 RayReconstructionResetMaskDebugColor(uint resetMask)
{
    if (resetMask == 0u)
    {
        return float4(0.03, 0.45, 0.12, 1.0);
    }
    if ((resetMask & RT_RR_RESET_STOCHASTIC_TRANSLUCENT) != 0u)
    {
        return float4(0.75, 0.15, 0.85, 1.0);
    }
    if ((resetMask & RT_RR_RESET_MATERIAL_MISMATCH) != 0u)
    {
        return float4(1.00, 0.60, 0.05, 1.0);
    }
    if ((resetMask & RT_RR_RESET_REJECTED_PREVIOUS) != 0u)
    {
        return float4(0.95, 0.05, 0.05, 1.0);
    }
    if ((resetMask & RT_RR_RESET_MISSING_PREVIOUS) != 0u)
    {
        return float4(0.60, 0.10, 0.10, 1.0);
    }
    if ((resetMask & RT_RR_RESET_OBJECT_MOTION_UNAVAILABLE) != 0u)
    {
        return float4(0.80, 0.80, 0.15, 1.0);
    }
    if ((resetMask & RT_RR_RESET_INVALID_SURFACE) != 0u)
    {
        return float4(0.02, 0.02, 0.02, 1.0);
    }
    return float4(0.80, 0.30, 0.10, 1.0);
}

float4 EvaluateRayReconstructionGuideDebug(uint2 pixel, uint view)
{
    const RAB_Surface surface = LoadPathTracePrimarySurfaceRecord(int2(pixel), false);
    if (view == 1u)
    {
        const float3 guideAlbedo = PathTraceRRGuideAlbedo[pixel].rgb;
        const float3 historyAlbedo = RAB_IsSurfaceValid(surface) ? surface.material.diffuseAlbedo : float3(0.0, 0.0, 0.0);
        const float3 albedo = RAB_Luminance(guideAlbedo) > 0.0 ? guideAlbedo : historyAlbedo;
        return float4(saturate(albedo), 1.0);
    }
    if (view == 2u)
    {
        const float3 guideNormal = PathTraceRRGuideNormalRoughness[pixel].rgb;
        const float3 historyNormal = RAB_IsSurfaceValid(surface) ? surface.shadingNormal * 0.5 + 0.5 : float3(0.5, 0.5, 1.0);
        const float normalHasGuide = any(abs(guideNormal - float3(0.5, 0.5, 1.0)) > float3(0.001, 0.001, 0.001)) ? 1.0 : 0.0;
        return float4(saturate(lerp(historyNormal, guideNormal, normalHasGuide)), 1.0);
    }
    if (view == 8u)
    {
        const float3 guideSpecular = PathTraceRRGuideSpecularAlbedo[pixel].rgb;
        const float3 historySpecular = RAB_IsSurfaceValid(surface) ? surface.material.specularF0 : float3(0.0, 0.0, 0.0);
        const float3 specularAlbedo = max(max(guideSpecular.r, guideSpecular.g), guideSpecular.b) > 0.0 ? guideSpecular : historySpecular;
        return float4(saturate(specularAlbedo), 1.0);
    }
    if (view == 3u)
    {
        float roughness = saturate(PathTraceRRGuideNormalRoughness[pixel].a);
        if (roughness >= 0.999 && RAB_IsSurfaceValid(surface))
        {
            roughness = saturate(surface.material.roughness);
        }
        return float4(roughness, roughness, roughness, 1.0);
    }
    if (view == 4u)
    {
        float depth = PathTraceRRGuideDepth[pixel];
        if (depth <= 0.0 && RAB_IsSurfaceValid(surface))
        {
            depth = surface.linearDepth;
        }
        const float normalizedDepth = saturate(depth / 4096.0);
        return float4(normalizedDepth, normalizedDepth, normalizedDepth, 1.0);
    }
    if (view == 5u)
    {
        float hitDistance = PathTraceRRGuideHitDistance[pixel];
        if (hitDistance <= 0.0 && RAB_IsSurfaceValid(surface))
        {
            hitDistance = surface.linearDepth;
        }
        const float normalizedHitDistance = saturate(hitDistance / 4096.0);
        return float4(normalizedHitDistance, normalizedHitDistance, normalizedHitDistance, 1.0);
    }

    if (view == 6u)
    {
        return RayReconstructionMotionMaskDebugColor(PathTraceMotionVectorMask[pixel]);
    }

    if (view == 7u)
    {
        return RayReconstructionResetMaskDebugColor(PathTraceRRGuideResetMask[pixel]);
    }

    if (view == 9u)
    {
        const RestirPTCombinedLighting lighting = RestirPTEvaluateCombinedLightingNoReflection(surface, pixel);
        return float4(RestirPTToneMapPreview(lighting.hdrRadiance), 1.0);
    }

    return float4(0.0, 0.0, 0.0, 1.0);
}

[shader("raygeneration")]
void RayGen()
{
    const uint2 pixel = DispatchRaysIndex().xy + PathTraceDispatchTileOffset();
    const uint2 dimensions = PathTraceFullOutputSize();
    if (pixel.x >= dimensions.x || pixel.y >= dimensions.y)
    {
        return;
    }

    if (RestirPTDiTemporalPrepassEnabled())
    {
        const RAB_Surface surface = LoadPathTracePrimarySurfaceRecord(int2(pixel), false);
        uint temporalStatus;
        GenerateRestirPTDiTemporalReservoir(surface, pixel, temporalStatus);
        return;
    }

    const uint guideDebugView = RayReconstructionGuideDebugView();
    if (guideDebugView != 0u)
    {
        SmokeOutput[pixel] = EvaluateRayReconstructionGuideDebug(pixel, guideDebugView);
        return;
    }

    const uint diDebugView = RestirPTDiDebugView();
    if (diDebugView != 0u)
    {
        const RAB_Surface surface = LoadPathTracePrimarySurfaceRecord(int2(pixel), false);
        SmokeOutput[pixel] = EvaluateRestirPTDiDebugView(surface, pixel, diDebugView);
        return;
    }

    const uint giDebugView = RestirPTGiDebugView();
    if (giDebugView != 0u)
    {
        const RAB_Surface surface = LoadPathTracePrimarySurfaceRecord(int2(pixel), false);
        SmokeOutput[pixel] = EvaluateRestirPTGiDebugView(surface, pixel, giDebugView);
        return;
    }

    const RAB_Surface surface = LoadPathTracePrimarySurfaceRecord(int2(pixel), false);
    SmokeOutput[pixel] = EvaluateCombinedResolve(surface, pixel);
}

[shader("miss")]
void Miss(inout PathTraceSmokePayload payload)
{
    payload.value = 0u;
}

[shader("miss")]
void ShadowMiss(inout PathTraceSmokeShadowPayload payload)
{
    payload.hit = 0u;
}

[shader("anyhit")]
void AnyHit(inout PathTraceSmokePayload payload, BuiltInTriangleIntersectionAttributes attributes)
{
}

[shader("anyhit")]
void ShadowAnyHit(inout PathTraceSmokeShadowPayload payload, BuiltInTriangleIntersectionAttributes attributes)
{
    if (payload.rayMode == 2u &&
        InstanceID() == payload.ignoreInstanceId &&
        PrimitiveIndex() == payload.ignorePrimitiveIndex)
    {
        payload.hit = 0u;
        IgnoreHit();
        return;
    }
    payload.hit = 1u;
}

[shader("closesthit")]
void ClosestHit(inout PathTraceSmokePayload payload, BuiltInTriangleIntersectionAttributes attributes)
{
}

[shader("closesthit")]
void ShadowClosestHit(inout PathTraceSmokeShadowPayload payload, BuiltInTriangleIntersectionAttributes attributes)
{
    payload.hit = 1u;
}
