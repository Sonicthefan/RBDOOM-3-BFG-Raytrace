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
StructuredBuffer<PathTraceDoomAnalyticLightCandidate> DoomAnalyticLights : register(t27);
StructuredBuffer<PathTraceDoomAnalyticLightCandidateIdentity> DoomAnalyticCurrentIdentities : register(t42);
StructuredBuffer<PathTraceDoomAnalyticLightCandidateIdentity> DoomAnalyticPreviousIdentities : register(t43);
StructuredBuffer<PathTraceDoomAnalyticLightRemap> DoomAnalyticRemap : register(t44);
StructuredBuffer<PathTraceDoomAnalyticLightCandidate> DoomAnalyticPreviousLights : register(t45);
ConstantBuffer<RTXDI_PTParameters> RestirPTParams : register(b28);
RWStructuredBuffer<RTXDI_PackedPTReservoir> RestirPTReservoirs : register(u29);
RWStructuredBuffer<PathTracePrimarySurfaceRecord> PrimarySurfaceHistoryCurrent : register(u30);
RWStructuredBuffer<PathTracePrimarySurfaceRecord> PrimarySurfaceHistoryPrevious : register(u31);

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
static const uint RT_PT_SAFETY_DISABLE_SELECTED_LIGHT_LOOP = 0x00000002u;
static const uint RT_PT_SAFETY_DISABLE_ANALYTIC_LIGHT_LOOP = 0x00000004u;
static const uint RT_PT_SAFETY_DISABLE_EMISSIVE_TRIANGLE_SAMPLING = 0x00000008u;
static const uint RT_PT_SAFETY_DISABLE_RESTIR_VISIBILITY_RAY = 0x00000020u;
static const uint RT_PT_SAFETY_DISABLE_PRIMARY_SURFACE_HISTORY = 0x00000040u;
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

RTXDI_PTReservoir LoadRestirPTSpatialOutputReservoir(uint2 pixel)
{
    const uint2 reservoirPosition = RTXDI_PixelPosToReservoirPos(pixel, 0u);
    return RTXDI_LoadPTReservoir(
        RestirPTParams.reservoirBuffer,
        reservoirPosition,
        RestirPTParams.bufferIndices.spatialResamplingOutputBufferIndex);
}

RTXDI_PTReservoir LoadRestirPTInitialReservoir(uint2 pixel)
{
    pixel = PathTraceRestirPTIndirectRepresentativePixel(pixel);
    return RTXDI_LoadPTReservoir(
        RestirPTParams.reservoirBuffer,
        pixel,
        RestirPTParams.bufferIndices.initialPathTracerOutputBufferIndex);
}

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

bool RestirPTTryEvaluateSpatialDirectLighting(RAB_Surface surface, uint2 pixel, out float3 contribution)
{
    contribution = float3(0.0, 0.0, 0.0);
    const uint2 reservoirPixel = PathTraceFullPixelToRestirDirectPixel(pixel);
    const RTXDI_PTReservoir spatialReservoir = LoadRestirPTSpatialOutputReservoir(reservoirPixel);
    return RestirPTTryEvaluateNeeReservoirPreview(surface, spatialReservoir, RestirPTInfo.z >= 0.5, contribution);
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

    const uint guideDebugView = RayReconstructionGuideDebugView();
    if (guideDebugView != 0u)
    {
        SmokeOutput[pixel] = EvaluateRayReconstructionGuideDebug(pixel, guideDebugView);
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
