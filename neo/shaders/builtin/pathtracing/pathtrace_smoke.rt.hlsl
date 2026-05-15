#include "../../vulkan.hlsli"
#ifndef __cplusplus
#ifndef uint16_t
#define uint16_t uint
#endif
#endif
#ifdef RB_PT_ENABLE_RESTIR
#include "Rtxdi/PT/ReSTIRPTParameters.h"
#endif

struct PathTraceSmokePayload
{
    uint value;
    float hitT;
    float3 normal;
    float3 geometricNormal;
    float3 tangent;
    float3 bitangent;
    float2 texCoord;
    float4 vertexColor;
    float4 vertexColorAdd;
    uint surfaceClass;
    uint translucentSubtype;
    uint triangleClassAndFlags;
    uint materialId;
    uint materialIndex;
    uint instanceId;
    uint primitiveIndex;
    uint shadowIgnoreInstanceId;
    uint shadowIgnorePrimitiveIndex;
    uint shadowIgnoreMaterialId;
    float3 debugVector;
    uint debugFlags;
};

struct PathTraceSmokeShadowPayload
{
    uint hit;
    uint rayMode;
    uint ignoreInstanceId;
    uint ignorePrimitiveIndex;
    uint ignoreMaterialId;
};

struct PathTraceSmokeVertex
{
    float4 position;
    float4 normal;
    float4 texCoord;
    float4 color;
    float4 color2;
};

struct PathTraceSmokeMaterial
{
    float4 debugAlbedo;
    float4 emissiveColor;
    uint diffuseTextureIndex;
    uint alphaTextureIndex;
    uint normalTextureIndex;
    uint specularTextureIndex;
    uint emissiveTextureIndex;
    float alphaCutoff;
    uint flags;
    uint textureWidth;
    uint textureHeight;
    uint alphaTextureWidth;
    uint alphaTextureHeight;
    uint normalTextureWidth;
    uint normalTextureHeight;
    uint specularTextureWidth;
    uint specularTextureHeight;
    uint emissiveTextureWidth;
    uint emissiveTextureHeight;
    uint padding0;
    uint padding1;
    uint padding2;
};

struct PathTraceRigidRouteInstance
{
    uint vertexOffset;
    uint indexOffset;
    uint triangleOffset;
    uint materialId;
    uint materialIndex;
    uint vertexCount;
    uint indexCount;
    uint triangleCount;
    uint flags;
    uint instanceIdLo;
    uint instanceIdHi;
    uint padding0;
    float4 currentObjectToWorld0;
    float4 currentObjectToWorld1;
    float4 currentObjectToWorld2;
    float4 previousObjectToWorld0;
    float4 previousObjectToWorld1;
    float4 previousObjectToWorld2;
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

struct PathTraceEmissiveDistributionEntry
{
    uint emissiveTriangleIndex;
    float cumulativePdf;
    float weight;
    float padding0;
};

struct PathTraceSmokeLightCandidate
{
    float4 emissiveColorAndLuminance;
    float4 areaAndWeightedLuminance;
    uint materialId;
    uint universeMaterialIndex;
    uint materialIndex;
    uint triangleCount;
    uint flags;
    uint staticTriangleCount;
    uint dynamicTriangleCount;
    uint emissiveTextureIndex;
    uint emissiveTextureWidth;
    uint emissiveTextureHeight;
    uint padding1;
    uint padding2;
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

struct PathTraceSmokeReservoir
{
    float4 radianceAndTargetPdf;
    float4 weightSumAndSampleCount;
    uint lightCandidateIndex;
    uint emissiveTriangleIndex;
    uint flags;
    uint padding0;
};

struct PathTraceSkinnedPreviousPosition
{
    float4 previousPosition;
};

static const uint PT_SKINNED_DISPATCH_HAS_VALID_PREVIOUS = 0x00000001u;
static const uint PT_SKINNED_DISPATCH_RT_CPU_SKINNED = 0x00000002u;
static const uint PT_RIGID_ROUTE_HAS_PREVIOUS_TRANSFORM = 0x00000001u;
static const uint PT_RIGID_ROUTE_TRANSFORM_CONTINUOUS = 0x00000002u;

struct PathTraceSkinnedSurfaceDispatchRecord
{
    uint sourceVertexOffset;
    uint outputVertexOffset;
    uint previousPositionOffset;
    uint vertexCount;
    uint currentJointOffset;
    uint previousJointOffset;
    uint surfaceRecordIndex;
    uint flags;
    uint dynamicVertexOffset;
    uint dynamicIndexOffset;
    uint dynamicTriangleOffset;
    uint triangleCount;
    float4 currentObjectToWorld0;
    float4 currentObjectToWorld1;
    float4 currentObjectToWorld2;
    float4 previousObjectToWorld0;
    float4 previousObjectToWorld1;
    float4 previousObjectToWorld2;
};

uint2 PathTracePrimarySurfaceStorePixel(uint2 pixel);
int2 PathTracePrimarySurfaceLoadPixel(int2 pixelPosition, bool previousFrame);

#include "PathTracePrimarySurface.hlsli"

struct PathTraceBoundsOverlayLine
{
    float4 startAndPad;
    float4 endAndPad;
    float4 color;
};

RaytracingAccelerationStructure SmokeScene : register(t0);
VK_IMAGE_FORMAT("rgba32f") RWTexture2D<float4> SmokeOutput : register(u1);
VK_IMAGE_FORMAT("rgba32f") RWTexture2D<float4> SmokeAccumulation : register(u15);
VK_IMAGE_FORMAT("rg16f") RWTexture2D<float2> PathTraceMotionVectors : register(u39);
VK_IMAGE_FORMAT("r32ui") RWTexture2D<uint> PathTraceMotionVectorMask : register(u40);
VK_IMAGE_FORMAT("rgba32f") RWTexture2D<float4> RestirPTReflectionOutput : register(u47);
StructuredBuffer<PathTraceSmokeVertex> SmokeStaticVertices : register(t3);
StructuredBuffer<uint> SmokeStaticIndices : register(t4);
StructuredBuffer<uint> SmokeStaticTriangleClasses : register(t5);
StructuredBuffer<PathTraceSmokeVertex> SmokeDynamicVertices : register(t6);
StructuredBuffer<uint> SmokeDynamicIndices : register(t7);
StructuredBuffer<uint> SmokeDynamicTriangleClasses : register(t8);
StructuredBuffer<uint> SmokeStaticTriangleMaterials : register(t9);
StructuredBuffer<uint> SmokeDynamicTriangleMaterials : register(t10);
StructuredBuffer<uint> SmokeStaticTriangleMaterialIndexes : register(t11);
StructuredBuffer<uint> SmokeDynamicTriangleMaterialIndexes : register(t12);
StructuredBuffer<PathTraceSmokeMaterial> SmokeMaterials : register(t13);
StructuredBuffer<PathTraceSmokeVertex> SmokeRigidRouteVertices : register(t22);
StructuredBuffer<uint> SmokeRigidRouteIndices : register(t23);
StructuredBuffer<uint> SmokeRigidRouteTriangleMaterials : register(t24);
StructuredBuffer<uint> SmokeRigidRouteTriangleMaterialIndexes : register(t25);
StructuredBuffer<PathTraceRigidRouteInstance> SmokeRigidRouteInstances : register(t26);
StructuredBuffer<PathTraceDoomAnalyticLightCandidate> DoomAnalyticLights : register(t27);
StructuredBuffer<PathTraceDoomAnalyticLightCandidateIdentity> DoomAnalyticCurrentIdentities : register(t42);
StructuredBuffer<PathTraceDoomAnalyticLightCandidateIdentity> DoomAnalyticPreviousIdentities : register(t43);
StructuredBuffer<PathTraceDoomAnalyticLightRemap> DoomAnalyticRemap : register(t44);
StructuredBuffer<PathTraceDoomAnalyticLightCandidate> DoomAnalyticPreviousLights : register(t45);
Texture2D<float4> SmokeFallbackTexture : register(t14);
StructuredBuffer<PathTraceSmokeEmissiveTriangle> SmokeEmissiveTriangles : register(t16);
StructuredBuffer<PathTraceEmissiveDistributionEntry> SmokeEmissiveDistribution : register(t46);
StructuredBuffer<PathTraceSmokeLightCandidate> SmokeLightCandidates : register(t17);
RWStructuredBuffer<PathTraceSmokeReservoir> SmokeReservoirCurrent : register(u18);
RWStructuredBuffer<PathTraceSmokeReservoir> SmokeReservoirPrevious : register(u19);
RWStructuredBuffer<PathTraceSmokeReservoir> SmokeReservoirSpatialScratch : register(u20);
StructuredBuffer<PathTraceBoundsOverlayLine> SmokeBoundsOverlayLines : register(t21);
#ifdef RB_PT_ENABLE_RESTIR
ConstantBuffer<RTXDI_PTParameters> RestirPTParams : register(b28);
RWStructuredBuffer<RTXDI_PackedPTReservoir> RestirPTReservoirs : register(u29);
#endif
RWStructuredBuffer<PathTracePrimarySurfaceRecord> PrimarySurfaceHistoryCurrent : register(u30);
RWStructuredBuffer<PathTracePrimarySurfaceRecord> PrimarySurfaceHistoryPrevious : register(u31);
StructuredBuffer<PathTraceSkinnedPreviousPosition> SmokeSkinnedPreviousPositions : register(t32);
StructuredBuffer<PathTraceSkinnedSurfaceDispatchRecord> SmokeSkinnedSurfaceDispatch : register(t33);
StructuredBuffer<PathTraceSmokeVertex> SmokePreviousStaticVertices : register(t34);
StructuredBuffer<uint> SmokePreviousStaticIndices : register(t35);
StructuredBuffer<uint> SmokePreviousStaticTriangleClasses : register(t36);
StructuredBuffer<uint> SmokePreviousStaticTriangleMaterials : register(t37);
StructuredBuffer<uint> SmokePreviousStaticTriangleMaterialIndexes : register(t38);
StructuredBuffer<uint> SmokeSkinnedTriangleDispatchIndexes : register(t41);
VK_BINDING(0, 1) Texture2D<float4> SmokeDiffuseTextures[] : register(t0, space1);
SamplerState SmokeMaterialSampler : register(s0);

#ifdef RB_PT_ENABLE_RESTIR
#define RTXDI_PT_RESERVOIR_BUFFER RestirPTReservoirs
#include "Rtxdi/PT/Reservoir.hlsli"
#endif

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

static const uint RT_SMOKE_TRIANGLE_CLASS_MASK = 0x0000ffffu;
static const uint RT_SMOKE_TRIANGLE_FORCE_GEOMETRIC_NORMAL = 0x00010000u;
static const uint RT_SMOKE_EMISSIVE_TRIANGLE_HISTORY_DYNAMIC = 0x00020000u;
static const uint RT_SMOKE_TRIANGLE_EMISSIVE_STAGE_OFF = 0x00040000u;
static const uint RT_SMOKE_TRANSLUCENT_SUBTYPE_SHIFT = 24u;
static const uint RT_SMOKE_TRANSLUCENT_SUBTYPE_MASK = 0x0f000000u;
static const uint RT_SMOKE_SURFACE_CLASS_RIGID_ENTITY = 1u;
static const uint RT_SMOKE_SURFACE_CLASS_SKINNED_DEFORMED = 2u;
static const uint RT_SMOKE_SURFACE_CLASS_TRANSLUCENT = 3u;
static const uint RT_SMOKE_TRANSLUCENT_SUBTYPE_OBJECT_GLASS = 1u;
static const uint RT_SMOKE_TRANSLUCENT_SUBTYPE_SMOKE_PARTICLE = 2u;
static const uint RT_SMOKE_TRANSLUCENT_SUBTYPE_PORTAL_WINDOW = 4u;
static const uint RT_SMOKE_TRANSLUCENT_SUBTYPE_GUI_SCREEN = 5u;
static const uint PT_MOTION_VECTOR_MASK_VALID = 0x00000001u;
static const uint PT_MOTION_VECTOR_MASK_SOURCE_SHIFT = 1u;
static const uint PT_MOTION_VECTOR_MASK_SOURCE_MASK = 0x0000001eu;
static const uint PT_MOTION_VECTOR_MASK_INVALID_REASON_SHIFT = 5u;
static const uint PT_MOTION_VECTOR_SOURCE_UNKNOWN = 0u;
static const uint PT_MOTION_VECTOR_SOURCE_STATIC = 1u;
static const uint PT_MOTION_VECTOR_SOURCE_SKINNED = 2u;
static const uint PT_MOTION_VECTOR_SOURCE_RIGID = 3u;
static const uint PT_MOTION_VECTOR_SOURCE_OTHER_OBJECT = 4u;
static const uint RT_SMOKE_MATERIAL_ALPHA_TEST = 0x00000001u;
static const uint RT_SMOKE_MATERIAL_DIFFUSE_YCOCG = 0x00000002u;
static const uint RT_SMOKE_MATERIAL_ADDITIVE_DECAL = 0x00000004u;
static const uint RT_SMOKE_MATERIAL_EMISSIVE = 0x00000008u;
static const uint RT_SMOKE_MATERIAL_FILTER_DECAL = 0x00000010u;
static const uint RT_SMOKE_MATERIAL_FILTER_DECAL_BLACK_KEY = 0x00000020u;
static const uint RT_SMOKE_MATERIAL_ALPHA_FROM_DIFFUSE_LUMA = 0x00000040u;
static const uint RT_SMOKE_MATERIAL_FORCE_DEBUG_ALBEDO = 0x00000080u;
static const uint RT_SMOKE_MATERIAL_ALPHA_FROM_DIFFUSE_DARK_KEY = 0x00000100u;
static const uint RT_SMOKE_MATERIAL_PORTAL_WINDOW_FALLBACK = 0x00000200u;
static const uint RT_SMOKE_MATERIAL_OBJECT_GLASS_FALLBACK = 0x00000400u;
static const uint RT_SMOKE_MATERIAL_ADDITIVE_DECAL_WHITE_KEY = 0x00000800u;
static const uint RT_SMOKE_TEXTURE_FLAG_USE_NORMAL_MAPS = 0x00000008u;
static const uint RT_SMOKE_TEXTURE_FLAG_USE_SPECULAR_MAPS = 0x00000010u;
static const uint RT_SMOKE_TEXTURE_FLAG_USE_EMISSIVE_MAPS = 0x00000020u;
static const uint RT_SMOKE_TEXTURE_FLAG_RESERVOIR_TWO_SIDED_EMISSIVES = 0x00000040u;
static const uint RT_SMOKE_TEXTURE_FLAG_TOY_FAKE_PBR_SPECULAR = 0x00000080u;
static const uint RT_SMOKE_MATERIAL_OVERRIDE_ZERO_ROUGHNESS = 0x00000001u;
static const uint RT_SMOKE_MAX_DEBUG_LIGHTS = 32u;
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

bool PathTraceSafetyDisabled(uint bit)
{
    return (((uint)SafetyInfo.x) & bit) != 0u;
}

uint PathTraceStaticVertexCount() { return (uint)max(GeometryInfo0.x, 0.0); }
uint PathTraceStaticIndexCount() { return (uint)max(GeometryInfo0.y, 0.0); }
uint PathTraceStaticTriangleCount() { return (uint)max(GeometryInfo0.z, 0.0); }
uint PathTraceDynamicVertexCount() { return (uint)max(GeometryInfo0.w, 0.0); }
uint PathTraceDynamicIndexCount() { return (uint)max(GeometryInfo1.x, 0.0); }
uint PathTraceDynamicTriangleCount() { return (uint)max(GeometryInfo1.y, 0.0); }
uint PathTraceRigidRouteVertexCount() { return (uint)max(GeometryInfo1.z, 0.0); }
uint PathTraceRigidRouteIndexCount() { return (uint)max(GeometryInfo1.w, 0.0); }
uint PathTraceRigidRouteTriangleCount() { return (uint)max(GeometryInfo2.x, 0.0); }
uint PathTraceRigidRouteInstanceCount() { return (uint)max(GeometryInfo2.y, 0.0); }
uint PathTracePrimarySurfaceHistoryCount() { return (uint)max(GeometryInfo2.z, 0.0); }
uint PathTraceSmokeReservoirCount() { return (uint)max(GeometryInfo2.w, 0.0); }
uint PathTraceSkinnedPreviousPositionCount() { return (uint)max(GeometryInfo3.x, 0.0); }
uint PathTraceSkinnedSurfaceDispatchCount() { return (uint)max(GeometryInfo3.y, 0.0); }
uint PathTraceSkinnedTriangleDispatchIndexCount() { return (uint)max(GeometryInfo3.z, 0.0); }
uint PathTracePreviousStaticVertexCount() { return (uint)max(GeometryInfo4.x, 0.0); }
uint PathTracePreviousStaticIndexCount() { return (uint)max(GeometryInfo4.y, 0.0); }
uint PathTracePreviousStaticTriangleCount() { return (uint)max(GeometryInfo4.z, 0.0); }
uint PathTracePreviousStaticMaterialIndexCount() { return (uint)max(GeometryInfo4.w, 0.0); }

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

bool PathTraceRestirDirectDispatchActive()
{
#if defined(RB_PT_RESTIR_SPATIAL_CONSUMES_SPATIAL_PREPASS) && !defined(RB_PT_RESTIR_SPATIAL_PRODUCER)
    return false;
#else
    const uint dispatchMode = (uint)max(RestirPTDirectInfo.z, 0.0);
    return dispatchMode == 1u || dispatchMode == 3u || dispatchMode == 5u || dispatchMode == 6u;
#endif
}

bool PathTraceRestirDirectSparseProducerDispatch()
{
    const uint dispatchMode = (uint)max(RestirPTDirectInfo.z, 0.0);
    return dispatchMode == 5u || dispatchMode == 6u;
}

bool PathTraceRestirPTPrimarySurfaceProducerDispatch()
{
    return (uint)max(RestirPTDirectInfo.z, 0.0) == 2u;
}

bool PathTraceRestirPTConsumePrimarySurfaceHistory()
{
    const uint dispatchMode = (uint)max(RestirPTDirectInfo.z, 0.0);
    return dispatchMode == 3u || dispatchMode == 4u || dispatchMode == 6u;
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

bool PathTraceRestirSparseActivePixel(uint2 pixel, bool previousFrame)
{
    if (!PathTraceRestirSparsityEnabled())
    {
        return true;
    }
    const uint rate = PathTraceRestirSparsityRate();
    const uint phase = PathTraceRestirSparsityPhase(previousFrame);
    return ((pixel.x + pixel.y * 3u + phase) % rate) == 0u;
}

uint2 PathTraceRestirSparseDispatchSize()
{
    const uint2 dimensions = max(PathTraceRestirDirectSize(), uint2(1u, 1u));
    const uint rate = PathTraceRestirSparsityRate();
    return uint2((dimensions.x + rate - 1u) / rate, dimensions.y);
}

uint2 PathTraceRestirSparseProducerPixelToDirectPixel(uint2 sparsePixel)
{
    const uint2 dimensions = max(PathTraceRestirDirectSize(), uint2(1u, 1u));
    const uint rate = PathTraceRestirSparsityRate();
    const uint phase = PathTraceRestirSparsityPhase(false);
    const uint firstX = (rate - ((sparsePixel.y * 3u + phase) % rate)) % rate;
    return uint2(firstX + sparsePixel.x * rate, sparsePixel.y);
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

uint PathTraceDebugMode()
{
#ifdef RB_PT_FORCE_DEBUG_MODE
    return RB_PT_FORCE_DEBUG_MODE;
#else
    return (uint)CameraUpAndDebugMode.w;
#endif
}

bool PathTraceRestirPTIndirectInitialMode()
{
    const uint debugMode = PathTraceDebugMode();
    return debugMode >= 53u && debugMode <= 56u;
}

bool PathTraceRestirPTIndirectProducerDispatch()
{
    return RestirPTIndirectInfo.x >= 0.5;
}

bool PathTraceRestirPTIndirectSparseProducerDispatch()
{
    return RestirPTIndirectInfo.x >= 1.5;
}

bool PathTraceRestirPTIndirectConsumesInitialPrepass()
{
    return RestirPTIndirectInfo.y >= 0.5 && !PathTraceRestirPTIndirectProducerDispatch();
}

uint PathTraceRestirPTIndirectSparsityRate()
{
    return clamp((uint)max(RestirPTIndirectInfo.z, 1.0), 1u, 8u);
}

bool PathTraceRestirPTIndirectSparsityEnabled()
{
    return RestirPTIndirectInfo.y >= 0.5 && PathTraceRestirPTIndirectSparsityRate() > 1u;
}

bool PathTraceRestirPTIndirectSparseActivePixel(uint2 pixel)
{
    if (!PathTraceRestirPTIndirectSparsityEnabled())
    {
        return true;
    }
    const uint rate = PathTraceRestirPTIndirectSparsityRate();
    const uint phase = (uint)max(RestirPTIndirectInfo.w, 0.0) % rate;
    return ((pixel.x + pixel.y * 3u + phase) % rate) == 0u;
}

uint2 PathTraceRestirPTIndirectSparseDispatchSize()
{
    const uint2 dimensions = max(PathTraceFullOutputSize(), uint2(1u, 1u));
    const uint rate = PathTraceRestirPTIndirectSparsityRate();
    return uint2((dimensions.x + rate - 1u) / rate, dimensions.y);
}

uint2 PathTraceRestirPTIndirectSparseProducerPixelToFullPixel(uint2 sparsePixel)
{
    const uint2 dimensions = max(PathTraceFullOutputSize(), uint2(1u, 1u));
    const uint rate = PathTraceRestirPTIndirectSparsityRate();
    const uint phase = (uint)max(RestirPTIndirectInfo.w, 0.0) % rate;
    const uint firstX = (rate - ((sparsePixel.y * 3u + phase) % rate)) % rate;
    return uint2(firstX + sparsePixel.x * rate, sparsePixel.y);
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
    return PathTraceRestirDirectDispatchActive() ? PathTraceRestirDirectPixelToRepresentativeFullPixel(pixel) : pixel;
}

int2 PathTracePrimarySurfaceLoadPixel(int2 pixelPosition, bool previousFrame)
{
    if (!PathTraceRestirDirectDispatchActive())
    {
        return pixelPosition;
    }
    if (pixelPosition.x < 0 || pixelPosition.y < 0)
    {
        return pixelPosition;
    }
    const uint2 directPixel = PathTraceRestirSparseRepresentativePixel(uint2(pixelPosition), previousFrame);
    return int2(PathTraceRestirDirectPixelToRepresentativeFullPixel(directPixel));
}

#ifdef RB_PT_ENABLE_RESTIR
#include "RtxdiBridge/PathTraceRtxdiBridgeCore.hlsli"
#else
#include "RtxdiBridge/RAB_Material.hlsli"

struct RAB_Surface
{
    uint valid;
    float3 worldPos;
    float linearDepth;
    float3 geometryNormal;
    uint materialId;
    float3 shadingNormal;
    uint materialIndex;
    float3 viewDir;
    uint instanceId;
    uint primitiveIndex;
    uint surfaceClass;
    uint flags;
    RAB_Material material;
};

RAB_Surface RAB_EmptySurface()
{
    RAB_Surface surface = (RAB_Surface)0;
    surface.material = RAB_EmptyMaterial();
    return surface;
}

bool RAB_IsSurfaceValid(RAB_Surface surface)
{
    return surface.valid != 0u;
}

float3 RAB_GetSurfaceNormal(RAB_Surface surface)
{
    return surface.shadingNormal;
}
#endif

bool DoomAnalyticLightsEnabled()
{
    return (((uint)max(DoomAnalyticLightInfo.w, 0.0)) & 1u) != 0u && !PathTraceSafetyDisabled(RT_PT_SAFETY_DISABLE_ANALYTIC_LIGHT_LOOP);
}

bool DoomAnalyticLightsReplaceSelected()
{
    return (((uint)max(DoomAnalyticLightInfo.w, 0.0)) & 2u) != 0u;
}

bool SmokeToyFakePBRSpecularEnabled()
{
    return ((((uint)TextureInfo.w) & RT_SMOKE_TEXTURE_FLAG_TOY_FAKE_PBR_SPECULAR) != 0u);
}

uint PathTraceIntegratorSamplesPerPixel()
{
    return clamp((uint)max(IntegratorInfo.x, 1.0), 1u, 4u);
}

uint PathTraceIntegratorMaxPathDepth()
{
    return clamp((uint)max(IntegratorInfo.y, 1.0), 1u, 4u);
}

uint PathTraceIntegratorDiffuseBounceLimit()
{
    if (PathTraceSafetyDisabled(RT_PT_SAFETY_DISABLE_DIFFUSE_SECONDARY_RAY))
    {
        return 0u;
    }
    return clamp((uint)max(IntegratorInfo.z, 0.0), 0u, 3u);
}

uint PathTraceIntegratorSpecularBounceLimit()
{
    if (PathTraceSafetyDisabled(RT_PT_SAFETY_DISABLE_REFLECTION_RAY))
    {
        return 0u;
    }
    return clamp((uint)max(IntegratorInfo.w, 0.0), 0u, 2u);
}

uint PathTraceIntegratorReflectionMode()
{
    if (PathTraceSafetyDisabled(RT_PT_SAFETY_DISABLE_REFLECTION_RAY))
    {
        return 0u;
    }
    return clamp((uint)max(IntegratorInfo2.y, 0.0), 0u, 2u);
}

uint PathTraceIntegratorRussianRouletteDepth()
{
    return clamp((uint)max(IntegratorInfo2.z, 0.0), 0u, 8u);
}

bool PathTraceIntegratorNextEventEstimationEnabled()
{
    return IntegratorInfo2.w >= 0.5;
}

uint PathTraceIntegratorSecondaryNeeMode()
{
    return clamp((uint)max(NeeInfo.x, 0.0), 0u, 2u);
}

uint PathTraceIntegratorSecondaryAnalyticNeeMode()
{
    return clamp((uint)max(NeeInfo.z, 0.0), 0u, 2u);
}

uint PathTraceIntegratorSecondaryAnalyticNeeSamples()
{
    return clamp((uint)max(NeeInfo.w, 0.0), 0u, 8u);
}

bool PathTraceIntegratorSecondaryNeeVisibilityEnabled()
{
    return NeeInfo.y >= 0.5;
}

bool ProjectSmokeOverlayPoint(float3 worldPosition, uint2 dimensions, out float2 projectedPixel)
{
    const float3 delta = worldPosition - CameraOriginAndTMax.xyz;
    const float forwardDistance = dot(delta, CameraForwardAndTanX.xyz);
    if (forwardDistance <= 0.05)
    {
        projectedPixel = float2(0.0, 0.0);
        return false;
    }

    const float ndcX = -dot(delta, CameraLeftAndTanY.xyz) / max(forwardDistance * CameraForwardAndTanX.w, 1.0e-5);
    const float ndcY = -dot(delta, CameraUpAndDebugMode.xyz) / max(forwardDistance * CameraLeftAndTanY.w, 1.0e-5);
    projectedPixel = (float2(ndcX, ndcY) * 0.5 + 0.5) * float2(dimensions);
    return all(abs(float2(ndcX, ndcY)) <= 1.35);
}

float DistanceToSmokeOverlaySegment(float2 samplePixel, float2 startPoint, float2 endPoint)
{
    const float2 segment = endPoint - startPoint;
    const float segmentLengthSquared = dot(segment, segment);
    if (segmentLengthSquared <= 1.0e-5)
    {
        return length(samplePixel - startPoint);
    }
    const float segmentT = saturate(dot(samplePixel - startPoint, segment) / segmentLengthSquared);
    return length(samplePixel - (startPoint + segment * segmentT));
}

float4 ApplySmokeBoundsOverlay(float4 baseColor, uint2 pixel, uint2 dimensions)
{
    const uint lineCount = min((uint)max(BoundsOverlayInfo.x, 0.0), 4096u);
    if (lineCount == 0u || BoundsOverlayInfo.z <= 0.0)
    {
        return baseColor;
    }

    const float2 pixelCenter = float2(pixel) + 0.5;
    const float thickness = max(BoundsOverlayInfo.y, 0.5);
    float3 overlayColor = float3(0.0, 0.0, 0.0);
    float overlayCoverage = 0.0;

    [loop]
    for (uint lineIndex = 0u; lineIndex < lineCount; ++lineIndex)
    {
        const PathTraceBoundsOverlayLine overlayLine = SmokeBoundsOverlayLines[lineIndex];
        float2 startPixel;
        float2 endPixel;
        if (!ProjectSmokeOverlayPoint(overlayLine.startAndPad.xyz, dimensions, startPixel) ||
            !ProjectSmokeOverlayPoint(overlayLine.endAndPad.xyz, dimensions, endPixel))
        {
            continue;
        }

        const float distanceToLine = DistanceToSmokeOverlaySegment(pixelCenter, startPixel, endPixel);
        const float coverage = saturate((thickness + 0.75 - distanceToLine) / 0.75) * saturate(overlayLine.color.a);
        if (coverage > overlayCoverage)
        {
            overlayCoverage = coverage;
            overlayColor = overlayLine.color.rgb;
        }
    }

    if (overlayCoverage <= 0.0)
    {
        return baseColor;
    }
    return float4(lerp(baseColor.rgb, overlayColor, saturate(overlayCoverage * 0.85)), 1.0);
}

bool SmokeRayIntersectAabb(float3 rayOrigin, float3 rayDirection, float3 boundsMin, float3 boundsMax, out float hitT)
{
    const float3 safeDirection = float3(
        abs(rayDirection.x) < 1.0e-6 ? (rayDirection.x < 0.0 ? -1.0e-6 : 1.0e-6) : rayDirection.x,
        abs(rayDirection.y) < 1.0e-6 ? (rayDirection.y < 0.0 ? -1.0e-6 : 1.0e-6) : rayDirection.y,
        abs(rayDirection.z) < 1.0e-6 ? (rayDirection.z < 0.0 ? -1.0e-6 : 1.0e-6) : rayDirection.z);
    const float3 invDirection = 1.0 / safeDirection;
    const float3 t0 = (boundsMin - rayOrigin) * invDirection;
    const float3 t1 = (boundsMax - rayOrigin) * invDirection;
    const float3 tMin = min(t0, t1);
    const float3 tMax = max(t0, t1);
    const float tEnter = max(max(tMin.x, tMin.y), max(tMin.z, 0.0));
    const float tExit = min(tMax.x, min(tMax.y, tMax.z));
    hitT = tEnter > 0.001 ? tEnter : tExit;
    return tExit >= tEnter && tExit > 0.0;
}

float4 RenderSmokeBoundsBoxes(float3 rayOrigin, float3 rayDirection)
{
    const uint lineCount = min((uint)max(BoundsOverlayInfo.x, 0.0), 4096u);
    const uint boxCount = min(lineCount / 12u, 64u);
    float closestHitT = CameraOriginAndTMax.w;
    float3 closestColor = float3(0.0, 0.0, 0.0);
    bool hitAnyBox = false;

    [loop]
    for (uint boxIndex = 0u; boxIndex < boxCount; ++boxIndex)
    {
        const uint firstLine = boxIndex * 12u;
        float3 boundsMin = float3(1.0e20, 1.0e20, 1.0e20);
        float3 boundsMax = float3(-1.0e20, -1.0e20, -1.0e20);

        [unroll]
        for (uint edgeIndex = 0u; edgeIndex < 12u; ++edgeIndex)
        {
            const PathTraceBoundsOverlayLine boxLine = SmokeBoundsOverlayLines[firstLine + edgeIndex];
            boundsMin = min(boundsMin, min(boxLine.startAndPad.xyz, boxLine.endAndPad.xyz));
            boundsMax = max(boundsMax, max(boxLine.startAndPad.xyz, boxLine.endAndPad.xyz));
        }

        const float3 extent = boundsMax - boundsMin;
        if (any(extent <= float3(0.001, 0.001, 0.001)))
        {
            continue;
        }

        float hitT;
        if (SmokeRayIntersectAabb(rayOrigin, rayDirection, boundsMin, boundsMax, hitT) && hitT < closestHitT)
        {
            const PathTraceBoundsOverlayLine firstBoxLine = SmokeBoundsOverlayLines[firstLine];
            closestHitT = hitT;
            closestColor = saturate(firstBoxLine.color.rgb);
            hitAnyBox = true;
        }
    }

    if (!hitAnyBox)
    {
        return float4(0.0, 0.0, 0.0, 1.0);
    }

    const float depthFade = 0.35 + 0.65 * saturate(1.0 - closestHitT / max(CameraOriginAndTMax.w, 1.0));
    return float4(saturate(closestColor * depthFade + closestColor * 0.25), 1.0);
}

float SmokeDistanceToSegment(float3 samplePosition, float3 startPoint, float3 endPoint)
{
    const float3 segment = endPoint - startPoint;
    const float segmentLengthSquared = dot(segment, segment);
    if (segmentLengthSquared <= 1.0e-6)
    {
        return length(samplePosition - startPoint);
    }
    const float segmentT = saturate(dot(samplePosition - startPoint, segment) / segmentLengthSquared);
    return length(samplePosition - (startPoint + segment * segmentT));
}

float4 RenderSmokeBoundsWireframeBoxes(float3 rayOrigin, float3 rayDirection)
{
    const uint lineCount = min((uint)max(BoundsOverlayInfo.x, 0.0), 4096u);
    const uint boxCount = min(lineCount / 12u, 64u);
    float closestEdgeHitT = CameraOriginAndTMax.w;
    float3 closestEdgeColor = float3(0.0, 0.0, 0.0);
    bool hitAnyEdge = false;

    [loop]
    for (uint boxIndex = 0u; boxIndex < boxCount; ++boxIndex)
    {
        const uint firstLine = boxIndex * 12u;
        float3 boundsMin = float3(1.0e20, 1.0e20, 1.0e20);
        float3 boundsMax = float3(-1.0e20, -1.0e20, -1.0e20);

        [unroll]
        for (uint edgeIndex = 0u; edgeIndex < 12u; ++edgeIndex)
        {
            const PathTraceBoundsOverlayLine boxLine = SmokeBoundsOverlayLines[firstLine + edgeIndex];
            boundsMin = min(boundsMin, min(boxLine.startAndPad.xyz, boxLine.endAndPad.xyz));
            boundsMax = max(boundsMax, max(boxLine.startAndPad.xyz, boxLine.endAndPad.xyz));
        }

        const float3 extent = boundsMax - boundsMin;
        if (any(extent <= float3(0.001, 0.001, 0.001)))
        {
            continue;
        }

        float hitT;
        if (!SmokeRayIntersectAabb(rayOrigin, rayDirection, boundsMin, boundsMax, hitT))
        {
            continue;
        }

        const float3 hitPoint = rayOrigin + rayDirection * hitT;
        float minEdgeDistance = 1.0e20;
        float3 edgeColor = float3(1.0, 1.0, 1.0);

        [unroll]
        for (uint edgeIndex = 0u; edgeIndex < 12u; ++edgeIndex)
        {
            const PathTraceBoundsOverlayLine edgeLine = SmokeBoundsOverlayLines[firstLine + edgeIndex];
            const float edgeDistance = SmokeDistanceToSegment(hitPoint, edgeLine.startAndPad.xyz, edgeLine.endAndPad.xyz);
            if (edgeDistance < minEdgeDistance)
            {
                minEdgeDistance = edgeDistance;
                edgeColor = saturate(edgeLine.color.rgb);
            }
        }

        const float edgeThickness = clamp(min(min(extent.x, extent.y), extent.z) * 0.045, 1.5, 8.0);
        if (minEdgeDistance <= edgeThickness && hitT < closestEdgeHitT)
        {
            closestEdgeHitT = hitT;
            closestEdgeColor = edgeColor;
            hitAnyEdge = true;
        }
    }

    if (!hitAnyEdge)
    {
        return float4(0.0, 0.0, 0.0, 1.0);
    }

    return float4(saturate(closestEdgeColor * 1.35 + 0.10), 1.0);
}

float3 SafeNormalize(float3 value, float3 fallback)
{
    const float lengthSquared = dot(value, value);
    return lengthSquared > 1.0e-8 ? value * rsqrt(lengthSquared) : fallback;
}

float SmokeLinear1(float c)
{
    return c <= 0.04045 ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4);
}

float3 SmokeLinear3(float3 c)
{
    return float3(SmokeLinear1(c.r), SmokeLinear1(c.g), SmokeLinear1(c.b));
}

void SmokePBRFromSpecmap(float3 specMap, out float3 F0, out float roughness)
{
    const float specLum = dot(float3(0.2125, 0.7154, 0.0721), specMap);

    F0 = float3(0.04, 0.04, 0.04);

    const float contrastMid = 0.214;
    const float contrastAmount = 2.0;
    float contrast = saturate((specLum - contrastMid) / (1.0 - contrastMid));
    contrast += saturate(specLum / contrastMid) - 1.0;
    contrast = exp2(contrastAmount * contrast);
    F0 *= contrast;

    const float linearBrightness = SmokeLinear1(2.0 * specLum);
    const float specPow = max(0.0, ((8.0 * linearBrightness) / max(F0.y, 1.0e-4)) - 2.0);
    F0 *= min(1.0, linearBrightness / max(F0.y * 0.25, 1.0e-4));

    roughness = sqrt(2.0 / (specPow + 2.0));

    const float glossiness = saturate(1.0 - roughness);
    const float metallic = step(0.7, glossiness);
    const float3 glossColor = SmokeLinear3(specMap.rgb);
    F0 = lerp(F0, glossColor, metallic);

    roughness = sqrt(roughness);
}

float3 BuildPerpendicular(float3 normal)
{
    const float3 axis = abs(normal.z) < 0.999 ? float3(0.0, 0.0, 1.0) : float3(0.0, 1.0, 0.0);
    return SafeNormalize(cross(axis, normal), float3(1.0, 0.0, 0.0));
}

float3 TransformObjectVectorToWorld(float3 objectVector)
{
    const float3x4 objectToWorld = ObjectToWorld3x4();
    return float3(
        dot(objectToWorld[0].xyz, objectVector),
        dot(objectToWorld[1].xyz, objectVector),
        dot(objectToWorld[2].xyz, objectVector));
}

float3 TransformObjectPointToWorld(float3 objectPoint)
{
    const float3x4 objectToWorld = ObjectToWorld3x4();
    return float3(
        dot(objectToWorld[0], float4(objectPoint, 1.0)),
        dot(objectToWorld[1], float4(objectPoint, 1.0)),
        dot(objectToWorld[2], float4(objectPoint, 1.0)));
}

float3 TransformObjectNormalToWorld(float3 objectNormal, float3 fallback)
{
    return SafeNormalize(TransformObjectVectorToWorld(objectNormal), fallback);
}

float3 MaterialIdToColor(uint materialId)
{
    uint hash = materialId;
    hash ^= hash >> 16;
    hash *= 2246822519u;
    hash ^= hash >> 13;
    hash *= 3266489917u;
    hash ^= hash >> 16;

    const float r = ((hash >> 0) & 255u) * (1.0 / 255.0);
    const float g = ((hash >> 8) & 255u) * (1.0 / 255.0);
    const float b = ((hash >> 16) & 255u) * (1.0 / 255.0);
    return 0.15 + float3(r, g, b) * 0.85;
}

float3 TranslucentSubtypeToColor(uint subtype)
{
    if (subtype == 0u)
    {
        return float3(0.10, 0.75, 1.00); // decal / grime
    }
    if (subtype == 1u)
    {
        return float3(0.20, 1.00, 0.65); // object glass
    }
    if (subtype == 2u)
    {
        return float3(1.00, 0.45, 0.05); // smoke / particle
    }
    if (subtype == 3u)
    {
        return float3(1.00, 0.95, 0.10); // signage / glow
    }
    if (subtype == 4u)
    {
        return float3(0.85, 0.25, 1.00); // portal / window
    }
    if (subtype == 5u)
    {
        return float3(1.00, 0.20, 0.75); // gui / screen
    }
    return float3(0.75, 0.75, 0.75);
}

float3 DebugLightSlotColor(uint lightIndex)
{
    uint hash = lightIndex + 1u;
    hash ^= hash >> 16;
    hash *= 2246822519u;
    hash ^= hash >> 13;
    hash *= 3266489917u;
    hash ^= hash >> 16;

    const float r = ((hash >> 0) & 255u) * (1.0 / 255.0);
    const float g = ((hash >> 8) & 255u) * (1.0 / 255.0);
    const float b = ((hash >> 16) & 255u) * (1.0 / 255.0);
    return 0.20 + float3(r, g, b) * 0.80;
}

float SmokeHashToUnitFloat(uint hash)
{
    hash ^= hash >> 16;
    hash *= 2246822519u;
    hash ^= hash >> 13;
    hash *= 3266489917u;
    hash ^= hash >> 16;
    return ((hash >> 8) & 0x00ffffffu) * (1.0 / 16777215.0);
}

float4 LoadSmokeTextureTexel(uint textureIndex, uint2 texel, bool bindlessEnabled)
{
    return bindlessEnabled
        ? SmokeDiffuseTextures[NonUniformResourceIndex(textureIndex)].Load(int3(texel, 0))
        : SmokeFallbackTexture.Load(int3(0, 0, 0));
}

float4 SampleSmokeTextureLoad(uint textureIndex, uint textureWidth, uint textureHeight, float2 wrappedTexCoord, bool bindlessEnabled, bool bilinearFilter)
{
    const uint width = max(textureWidth, 1u);
    const uint height = max(textureHeight, 1u);

    if (!bilinearFilter || width == 1u || height == 1u)
    {
        const uint2 texel = min(uint2(wrappedTexCoord * float2(width, height)), uint2(width - 1u, height - 1u));
        return LoadSmokeTextureTexel(textureIndex, texel, bindlessEnabled);
    }

    const float2 texelPosition = wrappedTexCoord * float2(width, height) - 0.5;
    const float2 basePosition = floor(texelPosition);
    const float2 fraction = frac(texelPosition);
    const int2 baseTexel = int2(basePosition);
    const uint2 textureSize = uint2(width, height);
    const uint2 texel00 = uint2((baseTexel + int2(0, 0) + int2(width, height)) % int2(textureSize));
    const uint2 texel10 = uint2((baseTexel + int2(1, 0) + int2(width, height)) % int2(textureSize));
    const uint2 texel01 = uint2((baseTexel + int2(0, 1) + int2(width, height)) % int2(textureSize));
    const uint2 texel11 = uint2((baseTexel + int2(1, 1) + int2(width, height)) % int2(textureSize));

    const float4 c00 = LoadSmokeTextureTexel(textureIndex, texel00, bindlessEnabled);
    const float4 c10 = LoadSmokeTextureTexel(textureIndex, texel10, bindlessEnabled);
    const float4 c01 = LoadSmokeTextureTexel(textureIndex, texel01, bindlessEnabled);
    const float4 c11 = LoadSmokeTextureTexel(textureIndex, texel11, bindlessEnabled);
    return lerp(lerp(c00, c10, fraction.x), lerp(c01, c11, fraction.x), fraction.y);
}

float4 SampleSmokeTexture(uint textureIndex, uint textureWidth, uint textureHeight, float2 texCoord, float4 fallback)
{
    const uint textureCount = (uint)TextureInfo.x;
    const uint sampleMethod = (uint)TextureInfo.y;
    const uint textureFlags = (uint)TextureInfo.w;
    const bool bindlessEnabled = (textureFlags & 1u) != 0u;
    const bool bilinearFilter = (textureFlags & 2u) != 0u;
    if (sampleMethod == 0u || textureIndex == 0xffffffffu || textureIndex >= textureCount)
    {
        return fallback;
    }

    if (!all(texCoord == texCoord) || any(abs(texCoord) > 65536.0))
    {
        return fallback;
    }

    const float2 wrappedTexCoord = frac(texCoord);
    float4 sampled = fallback;
    if (sampleMethod == 2u)
    {
        sampled = SampleSmokeTextureLoad(textureIndex, textureWidth, textureHeight, wrappedTexCoord, bindlessEnabled, bilinearFilter);
    }
    else
    {
        sampled = bindlessEnabled
            ? SmokeDiffuseTextures[NonUniformResourceIndex(textureIndex)].SampleLevel(SmokeMaterialSampler, wrappedTexCoord, 0.0)
            : SmokeFallbackTexture.SampleLevel(SmokeMaterialSampler, wrappedTexCoord, 0.0);
    }
    if (!all(sampled == sampled) || any(abs(sampled) > 65504.0))
    {
        return fallback;
    }

    return sampled;
}

float3 ConvertSmokeYCoCgToRGB(float4 ycocg)
{
    ycocg.z = (ycocg.z * 31.875) + 1.0;
    ycocg.z = 1.0 / ycocg.z;
    ycocg.xy *= ycocg.z;

    float3 rgb;
    rgb.r = dot(ycocg, float4(1.0, -1.0, 0.0, 1.0));
    rgb.g = dot(ycocg, float4(0.0, 1.0, -0.50196078, 1.0));
    rgb.b = dot(ycocg, float4(-1.0, -1.0, 1.00392156, 1.0));
    return saturate(rgb);
}

float4 SampleSmokeDecodedDiffuseTexture(PathTraceSmokeMaterial material, float2 texCoord)
{
    float4 texel = SampleSmokeTexture(material.diffuseTextureIndex, material.textureWidth, material.textureHeight, texCoord, material.debugAlbedo);
    const bool textureDecodeEnabled = (((uint)TextureInfo.w) & 4u) != 0u;
    if (textureDecodeEnabled && (material.flags & RT_SMOKE_MATERIAL_DIFFUSE_YCOCG) != 0u)
    {
        texel.rgb = ConvertSmokeYCoCgToRGB(texel);
    }
    return texel;
}

float4 SampleSmokeDiffuseTexture(PathTraceSmokeMaterial material, float2 texCoord)
{
    if ((material.flags & RT_SMOKE_MATERIAL_FORCE_DEBUG_ALBEDO) != 0u)
    {
        return material.debugAlbedo;
    }

    return SampleSmokeDecodedDiffuseTexture(material, texCoord);
}

float4 SampleSmokeSurfaceAlbedo(PathTraceSmokeMaterial material, float2 texCoord, uint surfaceClass, uint translucentSubtype, float4 vertexColor, float4 vertexColorAdd)
{
    float4 albedo = SampleSmokeDiffuseTexture(material, texCoord);
    if (surfaceClass == RT_SMOKE_SURFACE_CLASS_TRANSLUCENT && translucentSubtype == RT_SMOKE_TRANSLUCENT_SUBTYPE_GUI_SCREEN)
    {
        if (material.diffuseTextureIndex != 0xffffffffu)
        {
            albedo = float4(albedo.rgb * vertexColor.rgb, albedo.a * vertexColor.a);
        }
        else
        {
            albedo = vertexColor;
        }
    }
    return saturate(albedo);
}

float4 SampleSmokeAlphaTexture(PathTraceSmokeMaterial material, float2 texCoord)
{
    if (material.alphaTextureIndex != 0xffffffffu)
    {
        return SampleSmokeTexture(material.alphaTextureIndex, material.alphaTextureWidth, material.alphaTextureHeight, texCoord, SampleSmokeDiffuseTexture(material, texCoord));
    }

    return SampleSmokeDiffuseTexture(material, texCoord);
}

float SmokeAlphaCoverage(PathTraceSmokeMaterial material, float2 texCoord)
{
    if ((material.flags & RT_SMOKE_MATERIAL_ALPHA_FROM_DIFFUSE_DARK_KEY) != 0u)
    {
        const float3 decoded = SampleSmokeDecodedDiffuseTexture(material, texCoord).rgb;
        return 1.0 - max(max(decoded.r, decoded.g), decoded.b);
    }

    if ((material.flags & RT_SMOKE_MATERIAL_ALPHA_FROM_DIFFUSE_LUMA) != 0u)
    {
        const float3 decoded = SampleSmokeDecodedDiffuseTexture(material, texCoord).rgb;
        return max(max(decoded.r, decoded.g), decoded.b);
    }

    return SampleSmokeAlphaTexture(material, texCoord).a;
}

float4 SampleSmokeNormalTexture(PathTraceSmokeMaterial material, float2 texCoord)
{
    return SampleSmokeTexture(
        material.normalTextureIndex,
        material.normalTextureWidth,
        material.normalTextureHeight,
        texCoord,
        float4(0.5, 0.5, 1.0, 1.0));
}

float3 DecodeSmokeNormalTexture(PathTraceSmokeMaterial material, float2 texCoord, float3 normal, float3 tangent, float3 bitangent)
{
    if ((material.normalTextureIndex == 0xffffffffu) || ((((uint)TextureInfo.w) & RT_SMOKE_TEXTURE_FLAG_USE_NORMAL_MAPS) == 0u))
    {
        return normal;
    }

    const float4 bump = SampleSmokeNormalTexture(material, texCoord) * 2.0 - 1.0;
    if (!all(bump == bump))
    {
        return normal;
    }

    const float normalY = RestirPTInfo.y > 0.5 ? -bump.y : bump.y;
    float3 decoded = float3(bump.w, normalY, 0.0);
    const float xyLengthSquared = dot(decoded.xy, decoded.xy);
    if (xyLengthSquared >= 1.0)
    {
        decoded.xy *= rsqrt(xyLengthSquared);
        decoded.z = 0.0;
    }
    else
    {
        decoded.z = sqrt(1.0 - xyLengthSquared);
    }
    const float3 worldNormal = tangent * decoded.x + bitangent * decoded.y + normal * decoded.z;
    return SafeNormalize(worldNormal, normal);
}

float3 ConstrainSmokeShadingNormal(float3 shadingNormal, float3 geometryNormal)
{
    const float minGeometryDot = 0.02;
    geometryNormal = SafeNormalize(geometryNormal, float3(0.0, 0.0, 1.0));
    shadingNormal = SafeNormalize(shadingNormal, geometryNormal);

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
    return SafeNormalize(tangentComponent * tangentScale + geometryNormal * minGeometryDot, geometryNormal);
}

float4 SampleSmokeSpecularTexture(PathTraceSmokeMaterial material, float2 texCoord)
{
    return SampleSmokeTexture(
        material.specularTextureIndex,
        material.specularTextureWidth,
        material.specularTextureHeight,
        texCoord,
        float4(0.0, 0.0, 0.0, 1.0));
}

float3 SampleSmokeDirectSpecular(PathTraceSmokeMaterial material, float2 texCoord)
{
    if ((material.specularTextureIndex == 0xffffffffu) || ((((uint)TextureInfo.w) & RT_SMOKE_TEXTURE_FLAG_USE_SPECULAR_MAPS) == 0u))
    {
        return float3(0.0, 0.0, 0.0);
    }

    return saturate(SampleSmokeSpecularTexture(material, texCoord).rgb);
}

void BuildSmokeSpecularLobe(float3 specularColor, out float3 F0, out float roughness)
{
    if (SmokeToyFakePBRSpecularEnabled())
    {
        SmokePBRFromSpecmap(saturate(specularColor), F0, roughness);
    }
    else
    {
        F0 = saturate(specularColor);
        roughness = 0.28;
    }
}

bool BuildSmokeReflectionBounce(
    PathTraceSmokeMaterial material,
    PathTraceSmokePayload payload,
    float3 normal,
    float3 rayDirection,
    out float3 reflectionDir,
    out float3 reflectionWeight,
    out float roughness)
{
    reflectionDir = float3(0.0, 0.0, 0.0);
    reflectionWeight = float3(0.0, 0.0, 0.0);
    roughness = 1.0;

    if (PathTraceIntegratorReflectionMode() == 0u || PathTraceIntegratorSpecularBounceLimit() == 0u || PathTraceIntegratorMaxPathDepth() <= 1u)
    {
        return false;
    }
    if (payload.surfaceClass == RT_SMOKE_SURFACE_CLASS_TRANSLUCENT)
    {
        return false;
    }

    const float3 specularColor = SampleSmokeDirectSpecular(material, payload.texCoord);
    float3 F0;
    BuildSmokeSpecularLobe(specularColor, F0, roughness);
    if ((material.padding0 & RT_SMOKE_MATERIAL_OVERRIDE_ZERO_ROUGHNESS) != 0u)
    {
        roughness = 0.0;
        F0 = max(F0, float3(0.85, 0.85, 0.85));
    }
    const float f0Max = max(max(F0.r, F0.g), F0.b);
    const float roughnessLimit = PathTraceIntegratorReflectionMode() >= 2u ? 0.72 : 0.36;
    if (f0Max < 0.035 || roughness > roughnessLimit)
    {
        return false;
    }

    reflectionDir = SafeNormalize(reflect(rayDirection, normal), normal);
    if (dot(reflectionDir, normal) <= 0.0 || dot(reflectionDir, payload.geometricNormal) <= 0.0)
    {
        return false;
    }

    reflectionWeight = saturate(F0) * (1.0 - saturate(roughness * 0.85));
    return max(max(reflectionWeight.r, reflectionWeight.g), reflectionWeight.b) > 0.0;
}

float3 EvaluateSmokeSpecular(float3 specularColor, float3 normal, float3 lightDir, float3 viewDir, float3 lightColor, float directScale, float visibility)
{
    if (visibility <= 0.0 || max(max(specularColor.r, specularColor.g), specularColor.b) <= 0.0)
    {
        return float3(0.0, 0.0, 0.0);
    }

    const float3 halfVector = SafeNormalize(lightDir + viewDir, normal);
    if (SmokeToyFakePBRSpecularEnabled())
    {
        float3 F0;
        float roughness;
        SmokePBRFromSpecmap(saturate(specularColor), F0, roughness);

        const float ndotl = saturate(dot(normal, lightDir));
        const float hdotN = saturate(dot(normal, halfVector));
        const float ldotH = saturate(dot(lightDir, halfVector));
        const float rr = roughness * roughness;
        const float rrrr = max(rr * rr, 1.0e-4);
        const float D = max((hdotN * hdotN) * (rrrr - 1.0) + 1.0, 1.0e-4);
        const float VFapprox = max((ldotH * ldotH) * (roughness + 0.5), 1.0e-4);
        const float specularTerm = (rrrr / (4.0 * D * D * VFapprox)) * ndotl * directScale * visibility;
        return F0 * lightColor * specularTerm;
    }

    const float specularNdotH = saturate(dot(normal, halfVector));
    const float specularTerm = pow(specularNdotH, 32.0) * directScale * visibility;
    return specularColor * lightColor * specularTerm;
}

float4 SampleSmokeEmissiveTexture(PathTraceSmokeMaterial material, float2 texCoord)
{
    return SampleSmokeTexture(
        material.emissiveTextureIndex,
        material.emissiveTextureWidth,
        material.emissiveTextureHeight,
        texCoord,
        float4(1.0, 1.0, 1.0, 1.0));
}

PathTraceSmokeMaterial LoadSmokeMaterial(uint materialIndex);

float3 SampleSmokeEmissive(PathTraceSmokeMaterial material, float2 texCoord, uint surfaceClass, bool activeEmissiveStage)
{
    if ((material.flags & RT_SMOKE_MATERIAL_EMISSIVE) == 0u ||
        !activeEmissiveStage ||
        surfaceClass == RT_SMOKE_SURFACE_CLASS_SKINNED_DEFORMED ||
        ((((uint)TextureInfo.w) & RT_SMOKE_TEXTURE_FLAG_USE_EMISSIVE_MAPS) == 0u))
    {
        return float3(0.0, 0.0, 0.0);
    }

    float3 emissive = max(material.emissiveColor.rgb, float3(0.0, 0.0, 0.0));
    if (material.emissiveTextureIndex != 0xffffffffu)
    {
        emissive *= saturate(SampleSmokeEmissiveTexture(material, texCoord).rgb);
    }

    return emissive * 1.75;
}

float4 EstimateSmokeEmissiveTriangleRadiance(PathTraceSmokeEmissiveTriangle emissiveTriangle)
{
    if (emissiveTriangle.materialIndex >= (uint)TextureInfo.z)
    {
        return float4(0.0, 0.0, 0.0, 0.0);
    }

    const PathTraceSmokeMaterial material = LoadSmokeMaterial(emissiveTriangle.materialIndex);
    const float3 radiance = SampleSmokeEmissive(material, emissiveTriangle.centroidUvAndWeight.xy, 0u, (emissiveTriangle.padding0 & RT_SMOKE_TRIANGLE_EMISSIVE_STAGE_OFF) == 0u);
    const float luminance = dot(max(radiance, float3(0.0, 0.0, 0.0)), float3(0.2126, 0.7152, 0.0722));
    return float4(radiance, luminance);
}

float SmokeAdditiveDecalOpacity(float3 albedo)
{
    return max(max(albedo.r, albedo.g), albedo.b);
}

float SmokeAdditiveDecalMaterialOpacity(PathTraceSmokeMaterial material, float3 albedo)
{
    if ((material.flags & RT_SMOKE_MATERIAL_ADDITIVE_DECAL_WHITE_KEY) != 0u)
    {
        return 1.0 - min(min(albedo.r, albedo.g), albedo.b);
    }

    return SmokeAdditiveDecalOpacity(albedo);
}

bool SmokeAdditiveDecalRejectsHit(PathTraceSmokeMaterial material, float2 texCoord, bool shadowRay)
{
    if ((material.flags & RT_SMOKE_MATERIAL_ADDITIVE_DECAL) == 0u)
    {
        return false;
    }

    if (shadowRay)
    {
        return true;
    }

    const float3 albedo = SampleSmokeDiffuseTexture(material, texCoord).rgb;
    const float opacity = SmokeAdditiveDecalMaterialOpacity(material, albedo);
    return opacity < 0.035;
}

bool SmokeFilterDecalRejectsHit(PathTraceSmokeMaterial material, float2 texCoord, float2 barycentrics, uint instanceId, uint primitiveIndex, bool shadowRay)
{
    if ((material.flags & RT_SMOKE_MATERIAL_FILTER_DECAL) == 0u)
    {
        return false;
    }

    if (shadowRay)
    {
        return true;
    }

    const float3 albedo = SampleSmokeDiffuseTexture(material, texCoord).rgb;
    const bool blackKey = (material.flags & RT_SMOKE_MATERIAL_FILTER_DECAL_BLACK_KEY) != 0u;
    const float effect = blackKey
        ? max(max(albedo.r, albedo.g), albedo.b)
        : 1.0 - min(min(albedo.r, albedo.g), albedo.b);
    const float opacity = saturate(effect * 0.55);
    const float2 wrappedTexCoord = frac(abs(texCoord));
    const uint2 ditherCell = uint2(floor(wrappedTexCoord * 256.0));
    const uint2 baryCell = uint2(saturate(barycentrics) * 255.0);
    const uint hash =
        primitiveIndex * 1597334677u ^
        instanceId * 3812015801u ^
        ditherCell.x * 747796405u ^
        ditherCell.y * 2891336453u ^
        baryCell.x * 277803737u ^
        baryCell.y * 3266489917u;
    return opacity < SmokeHashToUnitFloat(hash);
}

float4 SmokeMissColor()
{
    return float4(1.0, 0.0, 0.0, 1.0);
}

PathTraceSmokePayload InitSmokePayload()
{
    PathTraceSmokePayload payload;
    payload.value = 0;
    payload.hitT = 0.0;
    payload.normal = float3(0.0, 0.0, 0.0);
    payload.geometricNormal = float3(0.0, 0.0, 0.0);
    payload.tangent = float3(1.0, 0.0, 0.0);
    payload.bitangent = float3(0.0, 1.0, 0.0);
    payload.texCoord = float2(0.0, 0.0);
    payload.vertexColor = float4(1.0, 1.0, 1.0, 1.0);
    payload.vertexColorAdd = float4(0.5, 0.5, 0.5, 0.5);
    payload.surfaceClass = 4;
    payload.translucentSubtype = 5;
    payload.triangleClassAndFlags = 4u;
    payload.materialId = 0;
    payload.materialIndex = 0;
    payload.instanceId = 0xffffffffu;
    payload.primitiveIndex = 0xffffffffu;
    payload.shadowIgnoreInstanceId = 0xffffffffu;
    payload.shadowIgnorePrimitiveIndex = 0xffffffffu;
    payload.shadowIgnoreMaterialId = 0xffffffffu;
    payload.debugVector = float3(0.0, 0.0, 0.0);
    payload.debugFlags = 0u;
    return payload;
}

PathTraceSmokeShadowPayload InitSmokeShadowPayload(uint ignoreInstanceId, uint ignorePrimitiveIndex, uint ignoreMaterialId)
{
    PathTraceSmokeShadowPayload payload;
    payload.hit = 0u;
    payload.rayMode = ignoreInstanceId != 0xffffffffu ? 2u : 1u;
    payload.ignoreInstanceId = ignoreInstanceId;
    payload.ignorePrimitiveIndex = ignorePrimitiveIndex;
    payload.ignoreMaterialId = ignoreMaterialId;
    return payload;
}

PathTraceSmokeMaterial LoadSmokeMaterial(uint materialIndex)
{
    PathTraceSmokeMaterial material;
    material.debugAlbedo = float4(1.0, 0.0, 1.0, 1.0);
    material.emissiveColor = float4(0.0, 0.0, 0.0, 1.0);
    material.diffuseTextureIndex = 0xffffffffu;
    material.alphaTextureIndex = 0xffffffffu;
    material.normalTextureIndex = 0xffffffffu;
    material.specularTextureIndex = 0xffffffffu;
    material.emissiveTextureIndex = 0xffffffffu;
    material.alphaCutoff = 0.0;
    material.flags = 0u;
    material.textureWidth = 1u;
    material.textureHeight = 1u;
    material.alphaTextureWidth = 1u;
    material.alphaTextureHeight = 1u;
    material.normalTextureWidth = 1u;
    material.normalTextureHeight = 1u;
    material.specularTextureWidth = 1u;
    material.specularTextureHeight = 1u;
    material.emissiveTextureWidth = 1u;
    material.emissiveTextureHeight = 1u;
    material.padding0 = 0u;
    material.padding1 = 0u;
    material.padding2 = 0u;

    const uint materialCount = (uint)TextureInfo.z;
    if (materialIndex < materialCount)
    {
        material = SmokeMaterials[materialIndex];
    }

    return material;
}

RAB_Material RAB_BuildMaterialFromSmokePayload(PathTraceSmokePayload payload)
{
    const PathTraceSmokeMaterial smokeMaterial = LoadSmokeMaterial(payload.materialIndex);
    const float4 albedo = SampleSmokeSurfaceAlbedo(
        smokeMaterial,
        payload.texCoord,
        payload.surfaceClass,
        payload.translucentSubtype,
        payload.vertexColor,
        payload.vertexColorAdd);
    const float3 specularColor = SampleSmokeDirectSpecular(smokeMaterial, payload.texCoord);
    float3 specularF0 = specularColor;
    float roughness = 1.0;
    if (SmokeToyFakePBRSpecularEnabled())
    {
        SmokePBRFromSpecmap(saturate(specularColor), specularF0, roughness);
    }
    if ((smokeMaterial.padding0 & RT_SMOKE_MATERIAL_OVERRIDE_ZERO_ROUGHNESS) != 0u)
    {
        roughness = 0.0;
        specularF0 = max(specularF0, float3(0.85, 0.85, 0.85));
    }

    const bool activeEmissiveStage = (payload.triangleClassAndFlags & RT_SMOKE_TRIANGLE_EMISSIVE_STAGE_OFF) == 0u;

    RAB_Material material = RAB_EmptyMaterial();
    material.materialId = payload.materialId;
    material.materialIndex = payload.materialIndex;
    material.flags = smokeMaterial.flags;
    material.alphaCutoff = smokeMaterial.alphaCutoff;
    material.diffuseAlbedo = albedo.rgb;
    material.roughness = roughness;
    material.specularF0 = specularF0;
    material.opacity = SmokeAlphaCoverage(smokeMaterial, payload.texCoord);
    material.emissiveRadiance = SampleSmokeEmissive(smokeMaterial, payload.texCoord, payload.surfaceClass, activeEmissiveStage) * max(ToyPathInfo.z, 0.0);
    material.emissiveTextureIndex = smokeMaterial.emissiveTextureIndex;
    return material;
}

RAB_Surface RAB_BuildSurfaceFromSmokePayload(PathTraceSmokePayload payload, float3 rayOrigin, float3 rayDirection, bool useNormalMap)
{
    RAB_Surface surface = RAB_EmptySurface();
    if (payload.value == 0u)
    {
        return surface;
    }

    const PathTraceSmokeMaterial smokeMaterial = LoadSmokeMaterial(payload.materialIndex);
    const float3 baseNormal = SafeNormalize(payload.normal, payload.geometricNormal);
    float3 shadingNormal = useNormalMap
        ? DecodeSmokeNormalTexture(smokeMaterial, payload.texCoord, baseNormal, payload.tangent, payload.bitangent)
        : baseNormal;
    const float3 hitPosition = rayOrigin + rayDirection * payload.hitT;
    const float3 viewDir = SafeNormalize(rayOrigin - hitPosition, -rayDirection);
    float3 geometryNormal = SafeNormalize(payload.geometricNormal, baseNormal);
    if (dot(geometryNormal, viewDir) < 0.0)
    {
        geometryNormal = -geometryNormal;
    }
    if (dot(shadingNormal, viewDir) < 0.0)
    {
        shadingNormal = -shadingNormal;
    }
    shadingNormal = ConstrainSmokeShadingNormal(shadingNormal, geometryNormal);

    surface.valid = 1u;
    surface.worldPos = hitPosition;
    surface.linearDepth = payload.hitT;
    surface.geometryNormal = geometryNormal;
    surface.materialId = payload.materialId;
    surface.shadingNormal = shadingNormal;
    surface.materialIndex = payload.materialIndex;
    surface.viewDir = viewDir;
    surface.instanceId = payload.instanceId;
    surface.primitiveIndex = payload.primitiveIndex;
    surface.surfaceClass = payload.surfaceClass;
    surface.flags = payload.triangleClassAndFlags;
    surface.material = RAB_BuildMaterialFromSmokePayload(payload);
    return surface;
}

bool ComputeSmokeTriangleBarycentrics(float3 position, float3 p0, float3 p1, float3 p2, out float3 barycentrics)
{
    barycentrics = float3(1.0, 0.0, 0.0);
    const float3 edge0 = p1 - p0;
    const float3 edge1 = p2 - p0;
    const float3 delta = position - p0;
    const float d00 = dot(edge0, edge0);
    const float d01 = dot(edge0, edge1);
    const float d11 = dot(edge1, edge1);
    const float d20 = dot(delta, edge0);
    const float d21 = dot(delta, edge1);
    const float denominator = d00 * d11 - d01 * d01;
    if (abs(denominator) <= 1.0e-10)
    {
        return false;
    }

    const float invDenominator = 1.0 / denominator;
    const float v = (d11 * d20 - d01 * d21) * invDenominator;
    const float w = (d00 * d21 - d01 * d20) * invDenominator;
    barycentrics = float3(1.0 - v - w, v, w);
    return all(barycentrics == barycentrics);
}

float3 TransformPathTraceRigidRoutePoint(float4 row0, float4 row1, float4 row2, float3 localPoint)
{
    return float3(
        dot(row0, float4(localPoint, 1.0)),
        dot(row1, float4(localPoint, 1.0)),
        dot(row2, float4(localPoint, 1.0)));
}

bool TryPathTracePrimarySurfaceSkinnedObjectMotion(RAB_Surface surface, out float3 previousWorldPosition, out uint debugStatus)
{
    previousWorldPosition = float3(0.0, 0.0, 0.0);
    debugStatus = RT_PRIMARY_SURFACE_DEBUG_NO_OBJECT_MOTION;
    if (surface.instanceId != 1u || surface.surfaceClass != RT_SMOKE_SURFACE_CLASS_SKINNED_DEFORMED)
    {
        return false;
    }

    debugStatus = RT_PRIMARY_SURFACE_DEBUG_SKINNED_MISSING_PREVIOUS;
    const uint dispatchCount = PathTraceSkinnedSurfaceDispatchCount();
    const uint dispatchIndexCount = PathTraceSkinnedTriangleDispatchIndexCount();
    if (dispatchCount == 0u || dispatchIndexCount == 0u || PathTraceSkinnedPreviousPositionCount() == 0u)
    {
        return false;
    }

    if (surface.primitiveIndex >= dispatchIndexCount)
    {
        return false;
    }

    const uint dispatchIndex = SmokeSkinnedTriangleDispatchIndexes[surface.primitiveIndex];
    if (dispatchIndex == 0xffffffffu || dispatchIndex >= dispatchCount)
    {
        return false;
    }

    const PathTraceSkinnedSurfaceDispatchRecord dispatch = SmokeSkinnedSurfaceDispatch[dispatchIndex];
    if (surface.primitiveIndex < dispatch.dynamicTriangleOffset ||
        surface.primitiveIndex >= dispatch.dynamicTriangleOffset + dispatch.triangleCount)
    {
        return false;
    }

    if ((dispatch.flags & (PT_SKINNED_DISPATCH_HAS_VALID_PREVIOUS | PT_SKINNED_DISPATCH_RT_CPU_SKINNED)) !=
        (PT_SKINNED_DISPATCH_HAS_VALID_PREVIOUS | PT_SKINNED_DISPATCH_RT_CPU_SKINNED) ||
        dispatch.previousPositionOffset == 0xffffffffu)
    {
        return false;
    }

    debugStatus = RT_PRIMARY_SURFACE_DEBUG_SKINNED_RANGE_MISMATCH;
    const uint localTriangleIndex = surface.primitiveIndex - dispatch.dynamicTriangleOffset;
    const uint indexOffset = dispatch.dynamicIndexOffset + localTriangleIndex * 3u;
    if (indexOffset + 2u >= PathTraceDynamicIndexCount() ||
        dispatch.vertexCount == 0u ||
        dispatch.triangleCount == 0u)
    {
        return false;
    }

    const uint i0 = SmokeDynamicIndices[indexOffset + 0u];
    const uint i1 = SmokeDynamicIndices[indexOffset + 1u];
    const uint i2 = SmokeDynamicIndices[indexOffset + 2u];
    const uint vertexEnd = dispatch.dynamicVertexOffset + dispatch.vertexCount;
    if (i0 < dispatch.dynamicVertexOffset || i0 >= vertexEnd ||
        i1 < dispatch.dynamicVertexOffset || i1 >= vertexEnd ||
        i2 < dispatch.dynamicVertexOffset || i2 >= vertexEnd ||
        i0 >= PathTraceDynamicVertexCount() ||
        i1 >= PathTraceDynamicVertexCount() ||
        i2 >= PathTraceDynamicVertexCount())
    {
        return false;
    }

    const float3 p0 = SmokeDynamicVertices[i0].position.xyz;
    const float3 p1 = SmokeDynamicVertices[i1].position.xyz;
    const float3 p2 = SmokeDynamicVertices[i2].position.xyz;
    float3 barycentrics;
    if (!ComputeSmokeTriangleBarycentrics(surface.worldPos, p0, p1, p2, barycentrics))
    {
        return false;
    }

    debugStatus = RT_PRIMARY_SURFACE_DEBUG_SKINNED_PREVIOUS_OUT_OF_RANGE;
    const uint previous0 = dispatch.previousPositionOffset + (i0 - dispatch.dynamicVertexOffset);
    const uint previous1 = dispatch.previousPositionOffset + (i1 - dispatch.dynamicVertexOffset);
    const uint previous2 = dispatch.previousPositionOffset + (i2 - dispatch.dynamicVertexOffset);
    if (previous0 >= PathTraceSkinnedPreviousPositionCount() ||
        previous1 >= PathTraceSkinnedPreviousPositionCount() ||
        previous2 >= PathTraceSkinnedPreviousPositionCount())
    {
        return false;
    }

    const float3 prev0 = SmokeSkinnedPreviousPositions[previous0].previousPosition.xyz;
    const float3 prev1 = SmokeSkinnedPreviousPositions[previous1].previousPosition.xyz;
    const float3 prev2 = SmokeSkinnedPreviousPositions[previous2].previousPosition.xyz;
    previousWorldPosition = prev0 * barycentrics.x + prev1 * barycentrics.y + prev2 * barycentrics.z;
    debugStatus = RT_PRIMARY_SURFACE_DEBUG_OK;
    return all(previousWorldPosition == previousWorldPosition);
}

bool TryPathTracePrimarySurfaceRigidObjectMotion(RAB_Surface surface, out float3 previousWorldPosition, out uint debugStatus)
{
    previousWorldPosition = float3(0.0, 0.0, 0.0);
    debugStatus = RT_PRIMARY_SURFACE_DEBUG_NO_OBJECT_MOTION;
    if (surface.instanceId < 2u || surface.surfaceClass != RT_SMOKE_SURFACE_CLASS_RIGID_ENTITY)
    {
        return false;
    }

    debugStatus = RT_PRIMARY_SURFACE_DEBUG_RIGID_MISSING_PREVIOUS;
    const uint routeInstanceIndex = surface.instanceId - 2u;
    if (routeInstanceIndex >= PathTraceRigidRouteInstanceCount())
    {
        return false;
    }

    const PathTraceRigidRouteInstance routeInstance = SmokeRigidRouteInstances[routeInstanceIndex];
    if ((routeInstance.flags & (PT_RIGID_ROUTE_HAS_PREVIOUS_TRANSFORM | PT_RIGID_ROUTE_TRANSFORM_CONTINUOUS)) !=
        (PT_RIGID_ROUTE_HAS_PREVIOUS_TRANSFORM | PT_RIGID_ROUTE_TRANSFORM_CONTINUOUS))
    {
        return false;
    }

    debugStatus = RT_PRIMARY_SURFACE_DEBUG_RIGID_RANGE_MISMATCH;
    if (surface.primitiveIndex >= routeInstance.triangleCount ||
        routeInstance.triangleOffset + surface.primitiveIndex >= PathTraceRigidRouteTriangleCount())
    {
        return false;
    }

    const uint indexOffset = routeInstance.indexOffset + surface.primitiveIndex * 3u;
    if (indexOffset + 2u >= PathTraceRigidRouteIndexCount())
    {
        return false;
    }

    const uint i0 = SmokeRigidRouteIndices[indexOffset + 0u];
    const uint i1 = SmokeRigidRouteIndices[indexOffset + 1u];
    const uint i2 = SmokeRigidRouteIndices[indexOffset + 2u];
    if (i0 >= routeInstance.vertexCount || i1 >= routeInstance.vertexCount || i2 >= routeInstance.vertexCount ||
        routeInstance.vertexOffset + i0 >= PathTraceRigidRouteVertexCount() ||
        routeInstance.vertexOffset + i1 >= PathTraceRigidRouteVertexCount() ||
        routeInstance.vertexOffset + i2 >= PathTraceRigidRouteVertexCount())
    {
        return false;
    }

    const float3 local0 = SmokeRigidRouteVertices[routeInstance.vertexOffset + i0].position.xyz;
    const float3 local1 = SmokeRigidRouteVertices[routeInstance.vertexOffset + i1].position.xyz;
    const float3 local2 = SmokeRigidRouteVertices[routeInstance.vertexOffset + i2].position.xyz;
    const float3 current0 = TransformPathTraceRigidRoutePoint(routeInstance.currentObjectToWorld0, routeInstance.currentObjectToWorld1, routeInstance.currentObjectToWorld2, local0);
    const float3 current1 = TransformPathTraceRigidRoutePoint(routeInstance.currentObjectToWorld0, routeInstance.currentObjectToWorld1, routeInstance.currentObjectToWorld2, local1);
    const float3 current2 = TransformPathTraceRigidRoutePoint(routeInstance.currentObjectToWorld0, routeInstance.currentObjectToWorld1, routeInstance.currentObjectToWorld2, local2);

    float3 barycentrics;
    if (!ComputeSmokeTriangleBarycentrics(surface.worldPos, current0, current1, current2, barycentrics))
    {
        return false;
    }

    const float3 prev0 = TransformPathTraceRigidRoutePoint(routeInstance.previousObjectToWorld0, routeInstance.previousObjectToWorld1, routeInstance.previousObjectToWorld2, local0);
    const float3 prev1 = TransformPathTraceRigidRoutePoint(routeInstance.previousObjectToWorld0, routeInstance.previousObjectToWorld1, routeInstance.previousObjectToWorld2, local1);
    const float3 prev2 = TransformPathTraceRigidRoutePoint(routeInstance.previousObjectToWorld0, routeInstance.previousObjectToWorld1, routeInstance.previousObjectToWorld2, local2);
    previousWorldPosition = prev0 * barycentrics.x + prev1 * barycentrics.y + prev2 * barycentrics.z;
    debugStatus = RT_PRIMARY_SURFACE_DEBUG_OK;
    return all(previousWorldPosition == previousWorldPosition);
}

bool TryPathTracePrimarySurfaceObjectMotion(RAB_Surface surface, out float3 previousWorldPosition, out uint debugStatus)
{
    if (TryPathTracePrimarySurfaceSkinnedObjectMotion(surface, previousWorldPosition, debugStatus))
    {
        return true;
    }
    if (debugStatus != RT_PRIMARY_SURFACE_DEBUG_NO_OBJECT_MOTION)
    {
        return false;
    }
    return TryPathTracePrimarySurfaceRigidObjectMotion(surface, previousWorldPosition, debugStatus);
}

#define RB_PATH_TRACE_PRIMARY_SURFACE_ENABLE_HELPERS
#define RB_PATH_TRACE_PRIMARY_SURFACE_ENABLE_OBJECT_MOTION
#include "PathTracePrimarySurface.hlsli"

bool TryPathTracePreviousStaticSnapshotPosition(RAB_Surface currentSurface, out float3 previousPosition, out uint debugStatus)
{
    previousPosition = float3(0.0, 0.0, 0.0);
    debugStatus = RT_PRIMARY_SURFACE_DEBUG_OK;
    if (!RAB_IsSurfaceValid(currentSurface))
    {
        debugStatus = RT_PRIMARY_SURFACE_DEBUG_MISSING_CURRENT;
        return false;
    }

    if (currentSurface.instanceId != 0u || currentSurface.surfaceClass != 0u)
    {
        debugStatus = RT_PRIMARY_SURFACE_DEBUG_NO_OBJECT_MOTION;
        return false;
    }

    if (PathTracePreviousStaticVertexCount() == 0u ||
        PathTracePreviousStaticIndexCount() == 0u ||
        PathTracePreviousStaticTriangleCount() == 0u ||
        PathTracePreviousStaticMaterialIndexCount() == 0u)
    {
        debugStatus = RT_PRIMARY_SURFACE_DEBUG_MISSING_PREVIOUS;
        return false;
    }

    const uint primitiveIndex = currentSurface.primitiveIndex;
    const uint indexOffset = primitiveIndex * 3u;
    if (primitiveIndex >= PathTraceStaticTriangleCount() ||
        primitiveIndex >= PathTracePreviousStaticTriangleCount() ||
        indexOffset + 2u >= PathTraceStaticIndexCount() ||
        indexOffset + 2u >= PathTracePreviousStaticIndexCount() ||
        primitiveIndex >= PathTracePreviousStaticMaterialIndexCount())
    {
        debugStatus = RT_PRIMARY_SURFACE_DEBUG_RIGID_RANGE_MISMATCH;
        return false;
    }

    const uint currentClass = SmokeStaticTriangleClasses[primitiveIndex];
    const uint previousClass = SmokePreviousStaticTriangleClasses[primitiveIndex];
    const uint currentMaterialId = SmokeStaticTriangleMaterials[primitiveIndex];
    const uint previousMaterialId = SmokePreviousStaticTriangleMaterials[primitiveIndex];
    const uint currentMaterialIndex = SmokeStaticTriangleMaterialIndexes[primitiveIndex];
    const uint previousMaterialIndex = SmokePreviousStaticTriangleMaterialIndexes[primitiveIndex];
    if ((currentClass & RT_SMOKE_TRIANGLE_CLASS_MASK) != (previousClass & RT_SMOKE_TRIANGLE_CLASS_MASK) ||
        currentMaterialId != previousMaterialId ||
        currentMaterialIndex != previousMaterialIndex)
    {
        debugStatus = RT_PRIMARY_SURFACE_DEBUG_MATERIAL_MISMATCH;
        return false;
    }

    const uint ci0 = SmokeStaticIndices[indexOffset + 0u];
    const uint ci1 = SmokeStaticIndices[indexOffset + 1u];
    const uint ci2 = SmokeStaticIndices[indexOffset + 2u];
    const uint pi0 = SmokePreviousStaticIndices[indexOffset + 0u];
    const uint pi1 = SmokePreviousStaticIndices[indexOffset + 1u];
    const uint pi2 = SmokePreviousStaticIndices[indexOffset + 2u];
    if (ci0 >= PathTraceStaticVertexCount() ||
        ci1 >= PathTraceStaticVertexCount() ||
        ci2 >= PathTraceStaticVertexCount() ||
        pi0 >= PathTracePreviousStaticVertexCount() ||
        pi1 >= PathTracePreviousStaticVertexCount() ||
        pi2 >= PathTracePreviousStaticVertexCount())
    {
        debugStatus = RT_PRIMARY_SURFACE_DEBUG_RIGID_RANGE_MISMATCH;
        return false;
    }

    const float3 c0 = SmokeStaticVertices[ci0].position.xyz;
    const float3 c1 = SmokeStaticVertices[ci1].position.xyz;
    const float3 c2 = SmokeStaticVertices[ci2].position.xyz;
    float3 barycentrics;
    if (!ComputeSmokeTriangleBarycentrics(currentSurface.worldPos, c0, c1, c2, barycentrics))
    {
        debugStatus = RT_PRIMARY_SURFACE_DEBUG_RIGID_RANGE_MISMATCH;
        return false;
    }

    const float3 p0 = SmokePreviousStaticVertices[pi0].position.xyz;
    const float3 p1 = SmokePreviousStaticVertices[pi1].position.xyz;
    const float3 p2 = SmokePreviousStaticVertices[pi2].position.xyz;
    previousPosition = p0 * barycentrics.x + p1 * barycentrics.y + p2 * barycentrics.z;
    if (!all(previousPosition == previousPosition))
    {
        debugStatus = RT_PRIMARY_SURFACE_DEBUG_NORMAL_MISMATCH;
        return false;
    }

    return true;
}

float4 EvaluatePathTracePreviousStaticSnapshotDebug(RAB_Surface currentSurface)
{
    float3 previousPosition;
    uint debugStatus;
    if (!TryPathTracePreviousStaticSnapshotPosition(currentSurface, previousPosition, debugStatus))
    {
        return PathTracePrimarySurfaceDebugColor(debugStatus, currentSurface);
    }
    if (length(previousPosition - currentSurface.worldPos) > 1.0)
    {
        return PathTracePrimarySurfaceDebugColor(RT_PRIMARY_SURFACE_DEBUG_NORMAL_MISMATCH, currentSurface);
    }
    return float4(0.03, 0.42, 0.16, 1.0);
}

float4 EvaluatePathTracePreviousStaticSnapshotReprojectionDebug(RAB_Surface currentSurface)
{
    float3 previousPosition;
    uint debugStatus;
    if (!TryPathTracePreviousStaticSnapshotPosition(currentSurface, previousPosition, debugStatus))
    {
        return PathTracePrimarySurfaceDebugColor(debugStatus, currentSurface);
    }

    int2 previousPixel;
    if (!ProjectPathTracePrimarySurfaceToPreviousPixel(previousPosition, PathTraceFullOutputSize(), previousPixel))
    {
        return PathTracePrimarySurfaceDebugColor(RT_PRIMARY_SURFACE_DEBUG_REJECTED_PREVIOUS, currentSurface);
    }

    const RAB_Surface previousSurface = LoadPathTracePrimarySurfaceRecord(previousPixel, true);
    if (!PathTracePrimarySurfacesAreSimilar(currentSurface, previousSurface, debugStatus))
    {
        return PathTracePrimarySurfaceDebugColor(debugStatus, currentSurface);
    }

    return PathTracePrimarySurfaceDebugColor(RT_PRIMARY_SURFACE_DEBUG_OK, currentSurface);
}

bool TryPathTracePreviousStaticSnapshotMotionPixels(RAB_Surface currentSurface, uint2 pixel, out int2 previousPixel, out float2 previousPixelFloat, out float2 motionPixels, out uint debugStatus);
bool TryPathTraceCombinedGeometryMotionPixels(RAB_Surface currentSurface, uint2 pixel, out int2 previousPixel, out float2 previousPixelFloat, out float2 motionPixels, out uint debugStatus, out uint sourceKind);
bool TryPathTraceCombinedGeometryMotionPixelsAndDepth(RAB_Surface currentSurface, uint2 pixel, out int2 previousPixel, out float2 previousPixelFloat, out float2 motionPixels, out float expectedPrevDepth, out uint debugStatus, out uint sourceKind);

float4 EvaluatePathTracePreviousStaticSnapshotMotionVectorDebug(RAB_Surface currentSurface, uint2 pixel)
{
    int2 previousPixel;
    float2 previousPixelFloat;
    float2 motionPixels;
    uint debugStatus;
    uint sourceKind;
    if (!TryPathTraceCombinedGeometryMotionPixels(currentSurface, pixel, previousPixel, previousPixelFloat, motionPixels, debugStatus, sourceKind))
    {
        return PathTracePrimarySurfaceDebugColor(debugStatus, currentSurface);
    }

    if (sourceKind != 1u)
    {
        return PathTracePrimarySurfaceDebugColor(RT_PRIMARY_SURFACE_DEBUG_NO_OBJECT_MOTION, currentSurface);
    }

    return PathTracePrimarySurfaceMotionVectorColor(motionPixels);
}

bool TryPathTracePreviousStaticSnapshotMotionPixelsAndDepth(RAB_Surface currentSurface, uint2 pixel, out int2 previousPixel, out float2 previousPixelFloat, out float2 motionPixels, out float expectedPrevDepth, out uint debugStatus)
{
    previousPixel = int2(-1, -1);
    previousPixelFloat = float2(-1.0, -1.0);
    motionPixels = float2(0.0, 0.0);
    expectedPrevDepth = 0.0;
    float3 previousPosition;
    if (!TryPathTracePreviousStaticSnapshotPosition(currentSurface, previousPosition, debugStatus))
    {
        return false;
    }

    if (!ProjectPathTracePrimarySurfaceToPreviousPixelFloatAndDepth(previousPosition, PathTraceFullOutputSize(), previousPixelFloat, expectedPrevDepth))
    {
        debugStatus = RT_PRIMARY_SURFACE_DEBUG_REJECTED_PREVIOUS;
        return false;
    }

    previousPixel = int2(floor(previousPixelFloat));
    motionPixels = previousPixelFloat - (float2(pixel) + 0.5);
    debugStatus = RT_PRIMARY_SURFACE_DEBUG_OK;
    return true;
}

bool TryPathTracePreviousStaticSnapshotMotionPixels(RAB_Surface currentSurface, uint2 pixel, out int2 previousPixel, out float2 previousPixelFloat, out float2 motionPixels, out uint debugStatus)
{
    float expectedPrevDepth;
    return TryPathTracePreviousStaticSnapshotMotionPixelsAndDepth(currentSurface, pixel, previousPixel, previousPixelFloat, motionPixels, expectedPrevDepth, debugStatus);
}

bool TryPathTracePackedObjectMotionPixelsAndDepth(RAB_Surface currentSurface, uint2 pixel, out int2 previousPixel, out float2 previousPixelFloat, out float2 motionPixels, out float expectedPrevDepth, out uint debugStatus)
{
    previousPixel = int2(-1, -1);
    previousPixelFloat = float2(-1.0, -1.0);
    motionPixels = float2(0.0, 0.0);
    expectedPrevDepth = 0.0;
    debugStatus = RT_PRIMARY_SURFACE_DEBUG_OK;
    if (!RAB_IsSurfaceValid(currentSurface))
    {
        debugStatus = RT_PRIMARY_SURFACE_DEBUG_MISSING_CURRENT;
        return false;
    }

    const PathTracePrimarySurfaceRecord currentRecord = PackPathTracePrimarySurfaceRecord(currentSurface);
    if (!PathTracePrimarySurfaceRecordHasObjectMotion(currentRecord))
    {
        debugStatus = currentRecord.header.z;
        return false;
    }

    if (!ProjectPathTracePrimarySurfaceToPreviousPixelFloatAndDepth(currentRecord.previousPositionOrMotion.xyz, PathTraceFullOutputSize(), previousPixelFloat, expectedPrevDepth))
    {
        debugStatus = RT_PRIMARY_SURFACE_DEBUG_REJECTED_PREVIOUS;
        return false;
    }

    previousPixel = int2(floor(previousPixelFloat));
    motionPixels = previousPixelFloat - (float2(pixel) + 0.5);
    return true;
}

uint PathTraceMotionVectorSourceKind(RAB_Surface currentSurface)
{
    if (!RAB_IsSurfaceValid(currentSurface))
    {
        return PT_MOTION_VECTOR_SOURCE_UNKNOWN;
    }
    if (currentSurface.instanceId == 0u && currentSurface.surfaceClass == 0u)
    {
        return PT_MOTION_VECTOR_SOURCE_STATIC;
    }
    if (currentSurface.surfaceClass == RT_SMOKE_SURFACE_CLASS_SKINNED_DEFORMED)
    {
        return PT_MOTION_VECTOR_SOURCE_SKINNED;
    }
    if (currentSurface.surfaceClass == RT_SMOKE_SURFACE_CLASS_RIGID_ENTITY)
    {
        return PT_MOTION_VECTOR_SOURCE_RIGID;
    }
    return PT_MOTION_VECTOR_SOURCE_OTHER_OBJECT;
}

uint PathTraceMotionVectorMaskFromStatus(bool valid, uint sourceKind, uint debugStatus)
{
    const uint sourceBits = (sourceKind & 0x0fu) << PT_MOTION_VECTOR_MASK_SOURCE_SHIFT;
    if (valid)
    {
        return PT_MOTION_VECTOR_MASK_VALID | sourceBits;
    }
    return sourceBits | ((debugStatus & 0xffu) << PT_MOTION_VECTOR_MASK_INVALID_REASON_SHIFT);
}

bool TryPathTraceCombinedGeometryMotionPixels(RAB_Surface currentSurface, uint2 pixel, out int2 previousPixel, out float2 previousPixelFloat, out float2 motionPixels, out uint debugStatus, out uint sourceKind)
{
    previousPixel = int2(-1, -1);
    previousPixelFloat = float2(-1.0, -1.0);
    motionPixels = float2(0.0, 0.0);
    debugStatus = RT_PRIMARY_SURFACE_DEBUG_NO_OBJECT_MOTION;
    sourceKind = PathTraceMotionVectorSourceKind(currentSurface);
    if (RAB_IsSurfaceValid(currentSurface) &&
        currentSurface.instanceId == 0u &&
        currentSurface.surfaceClass == 0u)
    {
        return TryPathTracePreviousStaticSnapshotMotionPixels(currentSurface, pixel, previousPixel, previousPixelFloat, motionPixels, debugStatus);
    }

    if (TryPathTracePrimarySurfacePackedObjectMotionPixels(currentSurface, pixel, PathTraceFullOutputSize(), previousPixel, motionPixels, debugStatus))
    {
        previousPixelFloat = motionPixels + (float2(pixel) + 0.5);
        return true;
    }

    return false;
}

bool TryPathTraceCombinedGeometryMotionPixelsAndDepth(RAB_Surface currentSurface, uint2 pixel, out int2 previousPixel, out float2 previousPixelFloat, out float2 motionPixels, out float expectedPrevDepth, out uint debugStatus, out uint sourceKind)
{
    previousPixel = int2(-1, -1);
    previousPixelFloat = float2(-1.0, -1.0);
    motionPixels = float2(0.0, 0.0);
    expectedPrevDepth = 0.0;
    debugStatus = RT_PRIMARY_SURFACE_DEBUG_NO_OBJECT_MOTION;
    sourceKind = PathTraceMotionVectorSourceKind(currentSurface);
    if (RAB_IsSurfaceValid(currentSurface) &&
        currentSurface.instanceId == 0u &&
        currentSurface.surfaceClass == 0u)
    {
        return TryPathTracePreviousStaticSnapshotMotionPixelsAndDepth(currentSurface, pixel, previousPixel, previousPixelFloat, motionPixels, expectedPrevDepth, debugStatus);
    }

    return TryPathTracePackedObjectMotionPixelsAndDepth(currentSurface, pixel, previousPixel, previousPixelFloat, motionPixels, expectedPrevDepth, debugStatus);
}

bool PathTraceMotionVectorExportEnabled()
{
    return MotionVectorInfo.x >= 0.5;
}

void StorePathTraceMotionVectorExport(uint2 pixel, RAB_Surface currentSurface)
{
    if (!PathTraceMotionVectorExportEnabled())
    {
        return;
    }

    int2 previousPixel;
    float2 previousPixelFloat;
    float2 motionPixels;
    uint debugStatus;
    uint sourceKind;
    if (TryPathTraceCombinedGeometryMotionPixels(currentSurface, pixel, previousPixel, previousPixelFloat, motionPixels, debugStatus, sourceKind))
    {
        PathTraceMotionVectors[pixel] = motionPixels;
        PathTraceMotionVectorMask[pixel] = PathTraceMotionVectorMaskFromStatus(true, sourceKind, debugStatus);
    }
    else
    {
        PathTraceMotionVectors[pixel] = float2(0.0, 0.0);
        PathTraceMotionVectorMask[pixel] = PathTraceMotionVectorMaskFromStatus(false, sourceKind, debugStatus);
    }
}

float4 EvaluatePathTraceCombinedGeometryMotionVectorDebug(RAB_Surface currentSurface, uint2 pixel)
{
    int2 previousPixel;
    float2 previousPixelFloat;
    float2 motionPixels;
    uint debugStatus;
    uint sourceKind;
    if (!TryPathTraceCombinedGeometryMotionPixels(currentSurface, pixel, previousPixel, previousPixelFloat, motionPixels, debugStatus, sourceKind))
    {
        return PathTracePrimarySurfaceDebugColor(debugStatus, currentSurface);
    }

    return PathTracePrimarySurfaceMotionVectorColor(motionPixels);
}

float4 EvaluatePathTraceCombinedGeometryReprojectionDebug(RAB_Surface currentSurface, uint2 pixel)
{
    int2 previousPixel;
    float2 previousPixelFloat;
    float2 motionPixels;
    uint debugStatus;
    uint sourceKind;
    if (!TryPathTraceCombinedGeometryMotionPixels(currentSurface, pixel, previousPixel, previousPixelFloat, motionPixels, debugStatus, sourceKind))
    {
        return PathTracePrimarySurfaceDebugColor(debugStatus, currentSurface);
    }

    const RAB_Surface previousSurface = LoadPathTracePrimarySurfaceRecord(previousPixel, true);
    if (!PathTracePrimarySurfacesAreSimilar(currentSurface, previousSurface, debugStatus))
    {
        return PathTracePrimarySurfaceDebugColor(debugStatus, currentSurface);
    }

    return PathTracePrimarySurfaceDebugColor(RT_PRIMARY_SURFACE_DEBUG_OK, currentSurface);
}

float4 EvaluatePathTraceCombinedGeometryMotionSourceDebug(RAB_Surface currentSurface, uint2 pixel)
{
    int2 previousPixel;
    float2 previousPixelFloat;
    float2 motionPixels;
    uint debugStatus;
    uint sourceKind;
    if (!TryPathTraceCombinedGeometryMotionPixels(currentSurface, pixel, previousPixel, previousPixelFloat, motionPixels, debugStatus, sourceKind))
    {
        return PathTracePrimarySurfaceDebugColor(debugStatus, currentSurface);
    }

    if (sourceKind == PT_MOTION_VECTOR_SOURCE_STATIC)
    {
        return float4(0.03, 0.42, 0.16, 1.0);
    }
    if (sourceKind == PT_MOTION_VECTOR_SOURCE_SKINNED)
    {
        return float4(0.48, 0.10, 0.62, 1.0);
    }
    if (sourceKind == PT_MOTION_VECTOR_SOURCE_RIGID)
    {
        return float4(0.04, 0.36, 0.48, 1.0);
    }

    return float4(0.08, 0.38, 0.18, 1.0);
}

float4 EvaluatePathTraceRigidRouteTransformParityDebug(PathTraceSmokePayload payload)
{
    if (payload.value == 0u)
    {
        return float4(0.0, 0.0, 0.0, 1.0);
    }
    if (payload.instanceId < 2u || payload.surfaceClass != RT_SMOKE_SURFACE_CLASS_RIGID_ENTITY)
    {
        return float4(0.04, 0.08, 0.32, 1.0);
    }
    if ((payload.debugFlags & 0x1u) == 0u)
    {
        return float4(0.55, 0.04, 0.04, 1.0);
    }

    const float routeError = payload.debugVector.x;
    const float tlasError = payload.debugVector.y;
    const float routeVsTlasError = payload.debugVector.z;
    if (routeVsTlasError > 0.05)
    {
        return float4(0.75, 0.0, 0.85, 1.0);
    }
    if (routeError <= 0.01 && tlasError <= 0.01)
    {
        return float4(0.02, 0.65, 0.12, 1.0);
    }
    if (routeError <= 0.05)
    {
        return float4(0.75, 0.68, 0.04, 1.0);
    }
    if (routeError <= 0.25)
    {
        return float4(0.95, 0.38, 0.02, 1.0);
    }
    return float4(0.85, 0.02, 0.02, 1.0);
}

bool SmokePayloadIsGuiScreen(PathTraceSmokePayload payload);
float4 CompositeSmokeGuiLayers(float3 rayOrigin, float3 rayDirection, PathTraceSmokePayload firstPayload);
uint SelectSmokeWeightedEmissiveTriangle(uint emissiveTriangleCount, float randomValue);

#include "pathtrace_emissive_sampling.hlsli"

#ifdef RB_PT_ENABLE_RESTIR
float TraceSmokeShadowVisibility(float3 origin, float3 direction, float tMax, uint ignoreInstanceId, uint ignorePrimitiveIndex, uint ignoreMaterialId);
#include "RtxdiBridge/PathTracer/RAB_PathTracer.hlsli"
#ifdef RB_PT_ENABLE_RESTIR_TEMPORAL
#include "RtxdiBridge/RAB_LightTarget.hlsli"
#include "RtxdiBridge/RAB_DuplicationMap.hlsli"
#include "RtxdiBridge/RAB_MISCallbacks.hlsli"
#include "Rtxdi/PT/TemporalResampling.hlsli"
#endif
#if defined(RB_PT_ENABLE_RESTIR_SPATIAL) && defined(RB_PT_ENABLE_RESTIR_TEMPORAL)
int2 RAB_ClampSamplePositionIntoView(int2 pixelPosition, bool previousFrame)
{
    const uint2 dimensions = max(PathTraceRestirDirectDispatchActive() ? PathTraceRestirDirectSize() : PathTraceFullOutputSize(), uint2(1u, 1u));
    const int2 clamped = clamp(pixelPosition, int2(0, 0), int2(dimensions) - int2(1, 1));
    if (PathTraceRestirDirectDispatchActive())
    {
        return int2(PathTraceRestirSparseRepresentativePixel(uint2(clamped), previousFrame));
    }
    return clamped;
}

static const float2 RT_PT_RESTIR_SPATIAL_NEIGHBOR_OFFSETS[32] =
{
    float2( 0.130,  0.991), float2(-0.321,  0.947), float2( 0.586,  0.810), float2(-0.724,  0.690),
    float2( 0.900,  0.436), float2(-0.963,  0.268), float2( 0.991, -0.130), float2(-0.947, -0.321),
    float2( 0.810, -0.586), float2(-0.690, -0.724), float2( 0.436, -0.900), float2(-0.268, -0.963),
    float2( 0.130, -0.991), float2(-0.130,  0.991), float2( 0.321, -0.947), float2(-0.586,  0.810),
    float2( 0.724, -0.690), float2(-0.900,  0.436), float2( 0.963, -0.268), float2(-0.991, -0.130),
    float2( 0.947,  0.321), float2(-0.810, -0.586), float2( 0.690,  0.724), float2(-0.436, -0.900),
    float2( 0.268,  0.963), float2(-0.130, -0.991), float2( 0.586, -0.810), float2(-0.724, -0.690),
    float2( 0.900, -0.436), float2(-0.963, -0.268), float2( 0.991,  0.130), float2(-0.321, -0.947)
};
#define RTXDI_NEIGHBOR_OFFSETS_BUFFER RT_PT_RESTIR_SPATIAL_NEIGHBOR_OFFSETS
#include "Rtxdi/PT/SpatialResampling.hlsli"
#undef RTXDI_NEIGHBOR_OFFSETS_BUFFER
#endif
#include "RtxdiBridge/RAB_LightTarget.hlsli"
#include "Rtxdi/PT/InitialSampling.hlsli"

void StoreRestirPTInitialReservoir(uint2 pixel, RTXDI_PTReservoir reservoir);
void StoreRestirPTTemporalOutputReservoir(uint2 pixel, RTXDI_PTReservoir reservoir);
void StoreRestirPTSpatialOutputReservoir(uint2 pixel, RTXDI_PTReservoir reservoir);
RTXDI_PTReservoir LoadRestirPTInitialReservoir(uint2 pixel);
RTXDI_PTReservoir LoadRestirPTTemporalOutputReservoir(uint2 pixel);
RTXDI_PTReservoir LoadRestirPTSpatialOutputReservoir(uint2 pixel);
RTXDI_PTReservoir GenerateRestirPTInitialReservoir(RAB_Surface surface, uint2 pixel);
float RestirPTTraceReservoirVisibility(RAB_Surface surface, RTXDI_PTReservoir reservoir);

bool RestirPTReservoirConnectsToNeeLight(RTXDI_PTReservoir reservoir)
{
    return RTXDI_IsValidPTReservoir(reservoir) && RTXDI_ConnectsToNeeLight(reservoir);
}

bool RestirPTShouldRejectPreviousNeeReservoir(RTXDI_PTReservoir reservoir)
{
    return MotionVectorInfo.y < 0.5 && RestirPTReservoirConnectsToNeeLight(reservoir);
}

bool RestirPTPreviousNeeReservoirFailsLightRemap(RTXDI_PTReservoir reservoir)
{
    if (!RestirPTReservoirConnectsToNeeLight(reservoir))
    {
        return false;
    }

    const RTXDI_SampledLightData sampledLightData = RTXDI_GetSampledLightData(reservoir);
    if (!RTXDI_SampledLightData_IsValidLightData(sampledLightData))
    {
        return true;
    }

    return RAB_TranslateLightIndex(RTXDI_SampledLightData_GetLightIndex(sampledLightData), false) < 0;
}

bool RestirPTPreviousTemporalNeighborhoodHasRejectedNeeReservoir(int2 previousPixel)
{
    const uint2 dimensions = PathTraceRestirDirectDispatchActive() ? PathTraceRestirDirectSize() : PathTraceFullOutputSize();
    [unroll]
    for (int offsetY = -1; offsetY <= 1; ++offsetY)
    {
        [unroll]
        for (int offsetX = -1; offsetX <= 1; ++offsetX)
        {
            const int2 samplePixel = previousPixel + int2(offsetX, offsetY);
            if (samplePixel.x < 0 || samplePixel.y < 0 ||
                (uint)samplePixel.x >= dimensions.x || (uint)samplePixel.y >= dimensions.y)
            {
                continue;
            }

            uint2 previousReservoirPixel = (uint2)samplePixel;
            if (PathTraceRestirDirectDispatchActive() && PathTraceRestirSparsityEnabled())
            {
                previousReservoirPixel = PathTraceRestirSparseRepresentativePixel(previousReservoirPixel, true);
            }
            const uint2 reservoirPosition = RTXDI_PixelPosToReservoirPos(previousReservoirPixel, 0u);
            const RTXDI_PTReservoir previousReservoir = RTXDI_LoadPTReservoir(
                RestirPTParams.reservoirBuffer,
                reservoirPosition,
                RestirPTParams.bufferIndices.temporalResamplingInputBufferIndex);
            if (RestirPTShouldRejectPreviousNeeReservoir(previousReservoir) ||
                RestirPTPreviousNeeReservoirFailsLightRemap(previousReservoir))
            {
                return true;
            }
        }
    }

    return false;
}

#ifdef RB_PT_ENABLE_RESTIR_TEMPORAL
RTXDI_PTReservoir GenerateRestirPTTemporalReservoir(RAB_Surface currentSurface, uint2 pixel, out float4 rejectionColor, out bool selectedPrevSample)
{
    rejectionColor = float4(0.55, 0.05, 0.04, 1.0);
    selectedPrevSample = false;

    if (PathTraceSafetyDisabled(RT_PT_SAFETY_DISABLE_RESERVOIR_WRITES))
    {
        rejectionColor = float4(0.08, 0.02, 0.02, 1.0);
        return RTXDI_EmptyPTReservoir();
    }

    if (!RAB_IsSurfaceValid(currentSurface))
    {
        const RTXDI_PTReservoir emptyReservoir = RTXDI_EmptyPTReservoir();
        StoreRestirPTInitialReservoir(pixel, emptyReservoir);
        StoreRestirPTTemporalOutputReservoir(pixel, emptyReservoir);
        return RTXDI_EmptyPTReservoir();
    }

    if (!RAB_SurfaceSupportsOpaqueDiffuseBrdf(currentSurface))
    {
        const RTXDI_PTReservoir emptyReservoir = RTXDI_EmptyPTReservoir();
        StoreRestirPTInitialReservoir(pixel, emptyReservoir);
        StoreRestirPTTemporalOutputReservoir(pixel, emptyReservoir);
        rejectionColor = float4(saturate(currentSurface.material.diffuseAlbedo * 0.025), 1.0);
        return RTXDI_EmptyPTReservoir();
    }

    const RTXDI_PTReservoir currentReservoir = GenerateRestirPTInitialReservoir(currentSurface, pixel);
    StoreRestirPTInitialReservoir(pixel, currentReservoir);

    const uint2 surfacePixel = PathTraceRestirDirectDispatchActive() ? PathTraceRestirDirectPixelToRepresentativeFullPixel(pixel) : pixel;
    int2 previousPixel;
    float2 previousPixelFloat;
    float2 motionPixels;
    float expectedPrevDepth;
    uint projectionDebugStatus;
    uint motionSourceKind;
    bool projected = TryPathTraceCombinedGeometryMotionPixelsAndDepth(
        currentSurface,
        surfacePixel,
        previousPixel,
        previousPixelFloat,
        motionPixels,
        expectedPrevDepth,
        projectionDebugStatus,
        motionSourceKind);
    if (!projected)
    {
        projected = ProjectPathTracePrimarySurfaceToPreviousPixelFloatAndDepth(
            currentSurface.worldPos,
            PathTraceFullOutputSize(),
            previousPixelFloat,
            expectedPrevDepth);
        previousPixel = int2(floor(previousPixelFloat));
        motionPixels = previousPixelFloat - (float2(surfacePixel) + 0.5);
        if (!projected)
        {
            StoreRestirPTTemporalOutputReservoir(pixel, currentReservoir);
            rejectionColor = PathTracePrimarySurfaceDebugColor(projectionDebugStatus, currentSurface);
            return currentReservoir;
        }
    }
    if (PathTraceRestirDirectDispatchActive())
    {
        const float2 previousReservoirPixelFloat = PathTraceFullPixelFloatToRestirDirectPixelFloat(previousPixelFloat);
        previousPixel = int2(floor(previousReservoirPixelFloat));
        if (PathTraceRestirSparsityEnabled() && previousPixel.x >= 0 && previousPixel.y >= 0)
        {
            previousPixel = int2(PathTraceRestirSparseRepresentativePixel(uint2(previousPixel), true));
        }
        motionPixels = previousReservoirPixelFloat - (float2(pixel) + 0.5);
    }

    const uint2 reservoirPosition = RTXDI_PixelPosToReservoirPos(pixel, 0u);

    RTXDI_PTTemporalResamplingRuntimeParameters runtimeParams = RTXDI_EmptyPTTemporalResamplingRuntimeParameters();
    runtimeParams.pixelPosition = pixel;
    runtimeParams.reservoirPosition = reservoirPosition;
    runtimeParams.motionVector = float3(motionPixels, expectedPrevDepth - currentSurface.linearDepth);
    runtimeParams.cameraPos = CameraOriginAndTMax.xyz;
    runtimeParams.prevCameraPos = PrevCameraOriginAndValid.xyz;
    runtimeParams.prevPrevCameraPos = PrevCameraOriginAndValid.xyz;

    RTXDI_RuntimeParameters rtxdiRuntimeParams = (RTXDI_RuntimeParameters)0;
    rtxdiRuntimeParams.activeCheckerboardField = 0u;
    rtxdiRuntimeParams.frameIndex = (uint)max(RestirPTInfo.x, 0.0);

    RTXDI_RandomSamplerState rng = RTXDI_InitRandomSampler(pixel, rtxdiRuntimeParams.frameIndex, 0x51ed270bu);
    RAB_PathTracerUserData ptud = RAB_EmptyPathTracerUserData();
    RTXDI_PTTemporalResamplingParameters temporalParams = RestirPTParams.temporalResampling;
    if (RestirPTPreviousTemporalNeighborhoodHasRejectedNeeReservoir(previousPixel))
    {
        temporalParams.maxReservoirAge = 0u;
    }
    RTXDI_PTReservoir temporalReservoir = RTXDI_PTTemporalResampling(
        temporalParams,
        runtimeParams,
        RestirPTParams.hybridShift,
        RestirPTParams.reconnection,
        rtxdiRuntimeParams,
        RestirPTParams.reservoirBuffer,
        rng,
        RestirPTParams.bufferIndices,
        selectedPrevSample,
        ptud);
    if (selectedPrevSample && RestirPTShouldRejectPreviousNeeReservoir(temporalReservoir))
    {
        temporalReservoir = currentReservoir;
        selectedPrevSample = false;
    }

    StoreRestirPTTemporalOutputReservoir(pixel, temporalReservoir);

    return temporalReservoir;
}

#if defined(RB_PT_ENABLE_RESTIR_SPATIAL) && defined(RB_PT_ENABLE_RESTIR_TEMPORAL)
RTXDI_PTReservoir GenerateRestirPTSpatialReservoir(RAB_Surface currentSurface, uint2 pixel, out float4 rejectionColor, out bool selectedPrevSample, out bool spatialResampled)
{
    spatialResampled = false;
    selectedPrevSample = false;
#ifdef RB_PT_RESTIR_SPATIAL_CONSUMES_TEMPORAL_PREPASS
    rejectionColor = float4(0.18, 0.18, 0.18, 1.0);
    const RTXDI_PTReservoir temporalReservoir = LoadRestirPTTemporalOutputReservoir(pixel);
#else
    const RTXDI_PTReservoir temporalReservoir = GenerateRestirPTTemporalReservoir(currentSurface, pixel, rejectionColor, selectedPrevSample);
#endif
    if (!RTXDI_IsValidPTReservoir(temporalReservoir))
    {
        StoreRestirPTSpatialOutputReservoir(pixel, temporalReservoir);
        return temporalReservoir;
    }

    RTXDI_PTSpatialResamplingRuntimeParameters runtimeParams = RTXDI_EmptyPTSpatialResamplingRuntimeParameters();
    runtimeParams.PixelPosition = pixel;
    runtimeParams.ReservoirPosition = RTXDI_PixelPosToReservoirPos(pixel, 0u);
    runtimeParams.cameraPos = CameraOriginAndTMax.xyz;
    runtimeParams.prevCameraPos = PrevCameraOriginAndValid.xyz;
    runtimeParams.prevPrevCameraPos = PrevCameraOriginAndValid.xyz;

    RTXDI_RuntimeParameters rtxdiRuntimeParams = (RTXDI_RuntimeParameters)0;
    rtxdiRuntimeParams.activeCheckerboardField = 0u;
    rtxdiRuntimeParams.frameIndex = (uint)max(RestirPTInfo.x, 0.0);
    rtxdiRuntimeParams.neighborOffsetMask = 31u;

    RTXDI_RandomSamplerState rng = RTXDI_InitRandomSampler(pixel, rtxdiRuntimeParams.frameIndex, 0x5f6a712bu);
    RAB_PathTracerUserData ptud = RAB_EmptyPathTracerUserData();
    const RTXDI_PTReservoir spatialReservoir = RTXDI_PTSpatialResampling(
        runtimeParams,
        RestirPTParams.spatialResampling,
        RestirPTParams.hybridShift,
        RestirPTParams.reconnection,
        RestirPTParams.reservoirBuffer,
        RestirPTParams.bufferIndices,
        rtxdiRuntimeParams,
        rng,
        spatialResampled,
        ptud);

    StoreRestirPTSpatialOutputReservoir(pixel, spatialReservoir);
    return spatialReservoir;
}
#endif

#ifdef RB_PT_ENABLE_RESTIR_TEMPORAL_DEBUG
float4 EvaluateRestirPTTemporalReservoirDebug(RAB_Surface currentSurface, uint2 pixel)
{
    float4 rejectionColor;
    bool selectedPrevSample;
    const RTXDI_PTReservoir temporalReservoir = GenerateRestirPTTemporalReservoir(currentSurface, pixel, rejectionColor, selectedPrevSample);
    if (!RTXDI_IsValidPTReservoir(temporalReservoir))
    {
        return rejectionColor;
    }

    const float selectedBoost = selectedPrevSample ? 0.18 : 0.0;
    const float historyTint = saturate((float)temporalReservoir.M / max((float)RestirPTParams.temporalResampling.maxHistoryLength, 1.0));
    return float4(0.02, saturate(0.36 + selectedBoost + historyTint * 0.28), 0.08 + historyTint * 0.10, 1.0);
}
#else
float4 EvaluateRestirPTTemporalReservoirDebug(RAB_Surface currentSurface, uint2 pixel)
{
    return float4(0.16, 0.04, 0.20, 1.0);
}
#endif

#if defined(RB_PT_ENABLE_RESTIR_TEMPORAL_SHADING) || defined(RB_PT_ENABLE_RESTIR_SPATIAL_SHADING) || defined(RB_PT_ENABLE_RESTIR_SPATIAL_ATTRIBUTION)
float3 RestirPTSanitizePreviewContribution(float3 contribution)
{
    contribution = max(contribution, float3(0.0, 0.0, 0.0));
    if (!all(contribution == contribution) || any(abs(contribution) > 65504.0))
    {
        return float3(0.0, 0.0, 0.0);
    }
    return contribution;
}

float3 RestirPTRoughPreviewFallback(RAB_Surface surface)
{
    if (!RAB_IsSurfaceValid(surface))
    {
        return float3(0.0, 0.0, 0.0);
    }
    return saturate(max(surface.material.diffuseAlbedo, float3(0.0, 0.0, 0.0)) * 0.008);
}

float3 RestirPTToneMapPreview(float3 contribution)
{
    const float exposure = max(RestirPTInfo.w, 0.0);
    contribution = RestirPTSanitizePreviewContribution(contribution * exposure);
    return contribution / (1.0 + contribution);
}

bool RestirPTTryEvaluateNeeReservoirPreview(RAB_Surface surface, RTXDI_PTReservoir reservoir, out float3 contribution)
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

    const uint lightIndex = RTXDI_SampledLightData_GetLightIndex(sampledLightData);
    const RAB_LightInfo lightInfo = RAB_LoadLightInfo(lightIndex, false);
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

    const float3 reflected = RAB_EvaluateSurfaceBrdf(surface, lightDir, RAB_GetSurfaceViewDir(surface)) * lightSample.radiance * ndotl;
    const float reservoirWeight = max(reservoir.WeightSum, 1.0 / max((float)reservoir.M, 1.0));
    contribution = RestirPTSanitizePreviewContribution((reflected / max(lightSample.solidAnglePdf, 1.0e-6)) * reservoirWeight);
    return RAB_Luminance(contribution) > 0.0;
}

bool RestirPTTryEvaluateSpatialDirectLighting(RAB_Surface surface, uint2 pixel, bool traceVisibility, out float3 contribution)
{
    contribution = float3(0.0, 0.0, 0.0);
    const uint2 reservoirPixel = PathTraceFullPixelToRestirDirectPixel(pixel);
    const RTXDI_PTReservoir spatialReservoir = LoadRestirPTSpatialOutputReservoir(reservoirPixel);
    if (!RestirPTTryEvaluateNeeReservoirPreview(surface, spatialReservoir, contribution))
    {
        return false;
    }
    if (traceVisibility)
    {
        contribution *= RestirPTTraceReservoirVisibility(surface, spatialReservoir);
    }
    return RAB_Luminance(contribution) > 0.0;
}

uint RestirPTMode18DebugView()
{
    return (uint)clamp(floor(SafetyInfo.w + 0.5), 0.0, 15.0) & 0x0fu;
}

bool RestirPTMode18HeavyDirectEnabled()
{
    return (((uint)clamp(floor(SafetyInfo.w + 0.5), 0.0, 255.0)) & 0x10u) != 0u;
}

float3 RestirPTMode18DebugToneMap(float3 value)
{
    value = RestirPTSanitizePreviewContribution(value);
    return value / (1.0 + value);
}
#endif

#ifdef RB_PT_ENABLE_RESTIR_TEMPORAL_SHADING
float4 EvaluateRestirPTTemporalReservoirShading(RAB_Surface currentSurface, uint2 pixel, bool traceVisibility)
{
    float4 rejectionColor;
    bool selectedPrevSample;
    const RTXDI_PTReservoir temporalReservoir = GenerateRestirPTTemporalReservoir(currentSurface, pixel, rejectionColor, selectedPrevSample);
    if (!RTXDI_IsValidPTReservoir(temporalReservoir))
    {
        return float4(RestirPTRoughPreviewFallback(currentSurface), 1.0);
    }

    float3 contribution = RestirPTSanitizePreviewContribution(temporalReservoir.TargetFunction * max(temporalReservoir.WeightSum, 0.0));
    float3 reconstructedContribution;
    if (RestirPTTryEvaluateNeeReservoirPreview(currentSurface, temporalReservoir, reconstructedContribution))
    {
        contribution = reconstructedContribution;
    }
    if (traceVisibility)
    {
        contribution *= RestirPTTraceReservoirVisibility(currentSurface, temporalReservoir);
    }

    const float3 preview = RestirPTToneMapPreview(contribution);
    return float4(saturate(RestirPTRoughPreviewFallback(currentSurface) + preview), 1.0);
}
#else
float4 EvaluateRestirPTTemporalReservoirShading(RAB_Surface currentSurface, uint2 pixel, bool traceVisibility)
{
    return float4(0.16, 0.04, 0.20, 1.0);
}
#endif

#ifdef RB_PT_ENABLE_RESTIR_SPATIAL_SHADING
float4 EvaluateRestirPTSpatialReservoirShading(RAB_Surface currentSurface, uint2 pixel, bool traceVisibility)
{
#ifdef RB_PT_RESTIR_SPATIAL_CONSUMES_SPATIAL_PREPASS
    const uint2 reservoirPixel = PathTraceFullPixelToRestirDirectPixel(pixel);
    const RTXDI_PTReservoir spatialReservoir = LoadRestirPTSpatialOutputReservoir(reservoirPixel);
    const bool spatialResampled = true;
#else
    float4 rejectionColor;
    bool selectedPrevSample;
    bool spatialResampled;
    const RTXDI_PTReservoir spatialReservoir = GenerateRestirPTSpatialReservoir(currentSurface, pixel, rejectionColor, selectedPrevSample, spatialResampled);
#endif
    if (!RTXDI_IsValidPTReservoir(spatialReservoir))
    {
        return float4(RestirPTRoughPreviewFallback(currentSurface), 1.0);
    }

    float3 contribution = RestirPTSanitizePreviewContribution(spatialReservoir.TargetFunction * max(spatialReservoir.WeightSum, 0.0));
    float3 reconstructedContribution;
    if (RestirPTTryEvaluateNeeReservoirPreview(currentSurface, spatialReservoir, reconstructedContribution))
    {
        contribution = reconstructedContribution;
    }
    if (traceVisibility)
    {
        contribution *= RestirPTTraceReservoirVisibility(currentSurface, spatialReservoir);
    }

    const float3 preview = RestirPTToneMapPreview(contribution);
    const float3 statusTint = spatialResampled ? float3(0.0, 0.002, 0.0) : float3(0.002, 0.0, 0.0);
    return float4(saturate(RestirPTRoughPreviewFallback(currentSurface) + preview + statusTint), 1.0);
}
#else
float4 EvaluateRestirPTSpatialReservoirShading(RAB_Surface currentSurface, uint2 pixel, bool traceVisibility)
{
    return float4(0.16, 0.04, 0.20, 1.0);
}
#endif

float4 RestirPTReservoirLightSourceAttributionColor(RTXDI_PTReservoir reservoir)
{
    if (!RTXDI_IsValidPTReservoir(reservoir))
    {
        return float4(0.55, 0.04, 0.04, 1.0);
    }

    const float contributionLuminance = RAB_Luminance(max(reservoir.TargetFunction, float3(0.0, 0.0, 0.0)) * max(reservoir.WeightSum, 0.0));
    const float heat = saturate(log2(1.0 + contributionLuminance) * 0.18);

    if (RTXDI_ConnectsToNeeLight(reservoir))
    {
        const RTXDI_SampledLightData sampledLightData = RTXDI_GetSampledLightData(reservoir);
        if (!RTXDI_SampledLightData_IsValidLightData(sampledLightData))
        {
            return float4(0.55, 0.04, 0.04, 1.0);
        }

        const uint lightIndex = RTXDI_SampledLightData_GetLightIndex(sampledLightData);
        const RAB_LightInfo lightInfo = RAB_LoadLightInfo(lightIndex, false);
        if (!RAB_IsLightInfoValid(lightInfo))
        {
            return float4(0.55, 0.04, 0.04, 1.0);
        }
        if (lightInfo.lightType == RAB_LIGHT_TYPE_DOOM_ANALYTIC_SPHERE)
        {
            return float4(0.0, saturate(0.35 + heat * 0.65), 1.0, 1.0);
        }
        if (lightInfo.lightType == RAB_LIGHT_TYPE_EMISSIVE_TRIANGLE)
        {
            return float4(1.0, saturate(0.35 + heat * 0.45), 0.0, 1.0);
        }

        return float4(0.55, 0.04, 0.04, 1.0);
    }

    return float4(0.08, saturate(0.55 + heat * 0.4), 0.12, 1.0);
}

uint RestirPTReservoirSourceKind(RTXDI_PTReservoir reservoir)
{
    if (!RTXDI_IsValidPTReservoir(reservoir))
    {
        return 0u;
    }
    if (!RTXDI_ConnectsToNeeLight(reservoir))
    {
        return 3u;
    }

    const RTXDI_SampledLightData sampledLightData = RTXDI_GetSampledLightData(reservoir);
    if (!RTXDI_SampledLightData_IsValidLightData(sampledLightData))
    {
        return 0u;
    }

    const RAB_LightInfo lightInfo = RAB_LoadLightInfo(RTXDI_SampledLightData_GetLightIndex(sampledLightData), false);
    if (!RAB_IsLightInfoValid(lightInfo))
    {
        return 0u;
    }
    if (lightInfo.lightType == RAB_LIGHT_TYPE_DOOM_ANALYTIC_SPHERE)
    {
        return 1u;
    }
    if (lightInfo.lightType == RAB_LIGHT_TYPE_EMISSIVE_TRIANGLE)
    {
        return 2u;
    }
    return 0u;
}

bool RestirPTReservoirSamePackedSource(RTXDI_PTReservoir a, RTXDI_PTReservoir b)
{
    const uint kindA = RestirPTReservoirSourceKind(a);
    const uint kindB = RestirPTReservoirSourceKind(b);
    if (kindA != kindB)
    {
        return false;
    }
    if (kindA == 1u || kindA == 2u)
    {
        const RTXDI_SampledLightData lightA = RTXDI_GetSampledLightData(a);
        const RTXDI_SampledLightData lightB = RTXDI_GetSampledLightData(b);
        return RTXDI_SampledLightData_IsValidLightData(lightA) &&
            RTXDI_SampledLightData_IsValidLightData(lightB) &&
            RTXDI_SampledLightData_GetLightIndex(lightA) == RTXDI_SampledLightData_GetLightIndex(lightB);
    }
    return kindA != 0u;
}

#if defined(RB_PT_ENABLE_RESTIR_SPATIAL) && defined(RB_PT_ENABLE_RESTIR_TEMPORAL)
float4 RestirPTSpatialAcceptanceDiagnosticColor(RAB_Surface currentSurface, uint2 pixel)
{
    const uint2 reservoirPixel = PathTraceFullPixelToRestirDirectPixel(pixel);
#ifdef RB_PT_RESTIR_SPATIAL_CONSUMES_SPATIAL_PREPASS
    const RTXDI_PTReservoir spatialReservoir = LoadRestirPTSpatialOutputReservoir(reservoirPixel);
    const bool spatialResampled = RTXDI_IsValidPTReservoir(spatialReservoir);
#else
    float4 rejectionColor;
    bool selectedPrevSample;
    bool spatialResampled;
    const RTXDI_PTReservoir spatialReservoir = GenerateRestirPTSpatialReservoir(currentSurface, pixel, rejectionColor, selectedPrevSample, spatialResampled);
#endif
    const RTXDI_PTReservoir temporalReservoir = LoadRestirPTTemporalOutputReservoir(reservoirPixel);

    if (!RTXDI_IsValidPTReservoir(temporalReservoir))
    {
        return float4(0.18, 0.18, 0.18, 1.0);
    }
    if (!RTXDI_IsValidPTReservoir(spatialReservoir))
    {
        return float4(0.65, 0.04, 0.04, 1.0);
    }
#ifdef RB_PT_RESTIR_SPATIAL_CONSUMES_SPATIAL_PREPASS
    const bool spatialContributionDetected = spatialReservoir.M > temporalReservoir.M + 0.5;
    if (!spatialContributionDetected)
    {
        return float4(0.05, 0.22, 0.95, 1.0);
    }
#endif
    if (!spatialResampled)
    {
        return float4(0.05, 0.22, 0.95, 1.0);
    }
    if (RestirPTReservoirSamePackedSource(temporalReservoir, spatialReservoir))
    {
        return float4(0.05, 0.75, 0.12, 1.0);
    }

    const uint spatialKind = RestirPTReservoirSourceKind(spatialReservoir);
    if (spatialKind == 1u)
    {
        return float4(0.0, 0.65, 1.0, 1.0);
    }
    if (spatialKind == 2u)
    {
        return float4(1.0, 0.62, 0.0, 1.0);
    }
    if (spatialKind == 3u)
    {
        return float4(0.78, 0.12, 0.95, 1.0);
    }
    return float4(0.95, 0.95, 0.08, 1.0);
}
#else
float4 RestirPTSpatialAcceptanceDiagnosticColor(RAB_Surface currentSurface, uint2 pixel)
{
    return float4(0.16, 0.04, 0.20, 1.0);
}
#endif

#if defined(RB_PT_ENABLE_RESTIR_SPATIAL_ATTRIBUTION) && defined(RB_PT_ENABLE_RESTIR_SPATIAL) && defined(RB_PT_ENABLE_RESTIR_TEMPORAL)
float4 RestirPTSpatialSourceCompareDiagnosticColor(RAB_Surface currentSurface, uint2 pixel)
{
    const uint2 reservoirPixel = PathTraceFullPixelToRestirDirectPixel(pixel);
    const RTXDI_PTReservoir temporalReservoir = LoadRestirPTTemporalOutputReservoir(reservoirPixel);
    const RTXDI_PTReservoir spatialReservoir = LoadRestirPTSpatialOutputReservoir(reservoirPixel);

    if (!RTXDI_IsValidPTReservoir(temporalReservoir))
    {
        return float4(0.18, 0.18, 0.18, 1.0);
    }
    if (!RTXDI_IsValidPTReservoir(spatialReservoir))
    {
        return float4(0.65, 0.04, 0.04, 1.0);
    }

    const bool spatialContributionDetected = spatialReservoir.M > temporalReservoir.M + 0.5;
    if (!spatialContributionDetected)
    {
        return float4(0.05, 0.22, 0.95, 1.0);
    }

    if (RestirPTReservoirSamePackedSource(temporalReservoir, spatialReservoir))
    {
        return float4(0.05, 0.75, 0.12, 1.0);
    }

    const uint spatialKind = RestirPTReservoirSourceKind(spatialReservoir);
    if (spatialKind == 1u || spatialKind == 2u)
    {
        float3 reconstructedContribution;
        if (!RestirPTTryEvaluateNeeReservoirPreview(currentSurface, spatialReservoir, reconstructedContribution))
        {
            return float4(0.95, 0.95, 0.08, 1.0);
        }
        return spatialKind == 1u ? float4(0.0, 0.65, 1.0, 1.0) : float4(1.0, 0.62, 0.0, 1.0);
    }
    if (spatialKind == 3u)
    {
        return float4(0.78, 0.12, 0.95, 1.0);
    }
    return float4(0.95, 0.95, 0.08, 1.0);
}
#else
float4 RestirPTSpatialSourceCompareDiagnosticColor(RAB_Surface currentSurface, uint2 pixel)
{
    return float4(0.16, 0.04, 0.20, 1.0);
}
#endif

#ifdef RB_PT_ENABLE_RESTIR_ATTRIBUTION
float4 EvaluateRestirPTTemporalLightSourceAttribution(RAB_Surface currentSurface, uint2 pixel)
{
    float4 rejectionColor;
    bool selectedPrevSample;
    const RTXDI_PTReservoir temporalReservoir = GenerateRestirPTTemporalReservoir(currentSurface, pixel, rejectionColor, selectedPrevSample);
    if (!RTXDI_IsValidPTReservoir(temporalReservoir))
    {
        return rejectionColor;
    }

    return RestirPTReservoirLightSourceAttributionColor(temporalReservoir);
}
#else
float4 EvaluateRestirPTTemporalLightSourceAttribution(RAB_Surface currentSurface, uint2 pixel)
{
    return float4(0.16, 0.04, 0.20, 1.0);
}
#endif

#ifdef RB_PT_ENABLE_RESTIR_SPATIAL_ATTRIBUTION
float4 EvaluateRestirPTSpatialLightSourceAttribution(RAB_Surface currentSurface, uint2 pixel)
{
    const uint spatialDiagnosticPacked = (uint)clamp(floor(SafetyInfo.z + 0.5), 0.0, 255.0);
    const uint spatialDiagnosticView = spatialDiagnosticPacked & 0x0fu;
    if (spatialDiagnosticView == 1u)
    {
        return RestirPTSpatialAcceptanceDiagnosticColor(currentSurface, pixel);
    }
    if (spatialDiagnosticView == 2u)
    {
        return RestirPTSpatialSourceCompareDiagnosticColor(currentSurface, pixel);
    }

#ifdef RB_PT_RESTIR_SPATIAL_CONSUMES_SPATIAL_PREPASS
    const uint2 reservoirPixel = PathTraceFullPixelToRestirDirectPixel(pixel);
    const RTXDI_PTReservoir spatialReservoir = LoadRestirPTSpatialOutputReservoir(reservoirPixel);
    const bool spatialResampled = true;
#else
    float4 rejectionColor;
    bool selectedPrevSample;
    bool spatialResampled;
    const RTXDI_PTReservoir spatialReservoir = GenerateRestirPTSpatialReservoir(currentSurface, pixel, rejectionColor, selectedPrevSample, spatialResampled);
#endif
    if (!RTXDI_IsValidPTReservoir(spatialReservoir))
    {
#ifdef RB_PT_RESTIR_SPATIAL_CONSUMES_SPATIAL_PREPASS
        return float4(0.55, 0.04, 0.04, 1.0);
#else
        return rejectionColor;
#endif
    }

    const float4 sourceColor = RestirPTReservoirLightSourceAttributionColor(spatialReservoir);
    if (sourceColor.r > 0.5 && sourceColor.g < 0.2 && sourceColor.b < 0.2)
    {
        return sourceColor;
    }
    return spatialResampled ? sourceColor : lerp(sourceColor, float4(0.18, 0.18, 0.18, 1.0), 0.2);
}
#else
float4 EvaluateRestirPTSpatialLightSourceAttribution(RAB_Surface currentSurface, uint2 pixel)
{
    return float4(0.16, 0.04, 0.20, 1.0);
}
#endif

#else
float4 EvaluateRestirPTTemporalReservoirDebug(RAB_Surface currentSurface, uint2 pixel)
{
    return float4(0.16, 0.04, 0.20, 1.0);
}

float4 EvaluateRestirPTTemporalReservoirShading(RAB_Surface currentSurface, uint2 pixel, bool traceVisibility)
{
    return float4(0.16, 0.04, 0.20, 1.0);
}

float4 EvaluateRestirPTSpatialReservoirShading(RAB_Surface currentSurface, uint2 pixel, bool traceVisibility)
{
    return float4(0.16, 0.04, 0.20, 1.0);
}

float4 EvaluateRestirPTTemporalLightSourceAttribution(RAB_Surface currentSurface, uint2 pixel)
{
    return float4(0.16, 0.04, 0.20, 1.0);
}

float4 EvaluateRestirPTSpatialLightSourceAttribution(RAB_Surface currentSurface, uint2 pixel)
{
    return float4(0.16, 0.04, 0.20, 1.0);
}
#endif

void StoreRestirPTInitialReservoir(uint2 pixel, RTXDI_PTReservoir reservoir)
{
    if (PathTraceSafetyDisabled(RT_PT_SAFETY_DISABLE_RESERVOIR_WRITES))
    {
        return;
    }
    RTXDI_StorePTReservoir(
        reservoir,
        RestirPTParams.reservoirBuffer,
        pixel,
        RestirPTParams.bufferIndices.initialPathTracerOutputBufferIndex);
}

void StoreRestirPTTemporalOutputReservoir(uint2 pixel, RTXDI_PTReservoir reservoir)
{
    if (PathTraceSafetyDisabled(RT_PT_SAFETY_DISABLE_RESERVOIR_WRITES))
    {
        return;
    }
    const uint2 reservoirPosition = RTXDI_PixelPosToReservoirPos(pixel, 0u);
    RTXDI_StorePTReservoir(
        reservoir,
        RestirPTParams.reservoirBuffer,
        reservoirPosition,
        RestirPTParams.bufferIndices.temporalResamplingOutputBufferIndex);
}

void StoreRestirPTSpatialOutputReservoir(uint2 pixel, RTXDI_PTReservoir reservoir)
{
    if (PathTraceSafetyDisabled(RT_PT_SAFETY_DISABLE_RESERVOIR_WRITES))
    {
        return;
    }
    const uint2 reservoirPosition = RTXDI_PixelPosToReservoirPos(pixel, 0u);
    RTXDI_StorePTReservoir(
        reservoir,
        RestirPTParams.reservoirBuffer,
        reservoirPosition,
        RestirPTParams.bufferIndices.spatialResamplingOutputBufferIndex);
}

RTXDI_PTReservoir LoadRestirPTInitialReservoir(uint2 pixel)
{
    if (PathTraceRestirPTIndirectConsumesInitialPrepass())
    {
        pixel = PathTraceRestirPTIndirectRepresentativePixel(pixel);
    }
    return RTXDI_LoadPTReservoir(
        RestirPTParams.reservoirBuffer,
        pixel,
        RestirPTParams.bufferIndices.initialPathTracerOutputBufferIndex);
}

RTXDI_PTReservoir LoadRestirPTTemporalOutputReservoir(uint2 pixel)
{
    const uint2 reservoirPosition = RTXDI_PixelPosToReservoirPos(pixel, 0u);
    return RTXDI_LoadPTReservoir(
        RestirPTParams.reservoirBuffer,
        reservoirPosition,
        RestirPTParams.bufferIndices.temporalResamplingOutputBufferIndex);
}

RTXDI_PTReservoir LoadRestirPTSpatialOutputReservoir(uint2 pixel)
{
    const uint2 reservoirPosition = RTXDI_PixelPosToReservoirPos(pixel, 0u);
    return RTXDI_LoadPTReservoir(
        RestirPTParams.reservoirBuffer,
        reservoirPosition,
        RestirPTParams.bufferIndices.spatialResamplingOutputBufferIndex);
}

float3 EvaluateRestirPTLocalLightCandidateDebug(RAB_Surface surface, uint2 pixel)
{
    const uint emissiveTriangleCount = PathTraceSafetyDisabled(RT_PT_SAFETY_DISABLE_EMISSIVE_TRIANGLE_SAMPLING) ? 0u : (uint)max(EmissiveInfo.x, 0.0);
    const uint uploadedAnalyticCount = PathTraceSafetyDisabled(RT_PT_SAFETY_DISABLE_ANALYTIC_LIGHT_LOOP) ? 0u : (uint)max(DoomAnalyticLightInfo.x, 0.0);
    const uint analyticTraceCap = (uint)max(DoomAnalyticLightInfo.y, 0.0);
    const uint analyticCount = DoomAnalyticLightsEnabled() ? min(uploadedAnalyticCount, analyticTraceCap) : 0u;
    const uint lightCount = emissiveTriangleCount + analyticCount;
    if (lightCount == 0u)
    {
        return float3(0.18, 0.0, 0.24);
    }

    uint validSampleCount = 0u;
    float bestScore = 0.0;
    const uint seed =
        pixel.x * 1973u ^
        pixel.y * 9277u ^
        surface.materialId * 26699u ^
        surface.materialIndex * 104729u ^
        ((uint)max(RestirPTInfo.x, 0.0)) * 668265263u;

    if (emissiveTriangleCount > 0u)
    {
        const uint trialCount = clamp((uint)max(EmissiveInfo.z, 1.0), 1u, 16u);
        [loop]
        for (uint trialIndex = 0u; trialIndex < trialCount; ++trialIndex)
        {
            const float xi = SmokeHashToUnitFloat(seed ^ (trialIndex + 1u) * 747796405u);
            const uint lightIndex = SelectSmokeWeightedEmissiveTriangle(emissiveTriangleCount, xi);
            const RAB_LightInfo lightInfo = RAB_LoadLightInfo(lightIndex, false);
            const RAB_LightSample lightSample = RAB_SamplePolymorphicLight(lightInfo, surface, float2(0.5, 0.5));
            if (lightSample.valid != 0u)
            {
                ++validSampleCount;
                bestScore = max(bestScore, RAB_GetLightSampleTargetPdfForSurface(lightSample, surface));
            }
        }
    }

    if (analyticCount > 0u)
    {
        const uint analyticTrialCount = min(analyticCount, clamp((uint)max(MotionVectorInfo.z, 1.0), 1u, 128u));
        const uint localWindow = min(analyticCount, 32u);
        [loop]
        for (uint trialIndex = 0u; trialIndex < analyticTrialCount; ++trialIndex)
        {
            const float branchXi = SmokeHashToUnitFloat(seed ^ (trialIndex + 11u) * 2246822519u);
            const float xi = SmokeHashToUnitFloat(seed ^ (trialIndex + 17u) * 1597334677u);
            const float2 uv = float2(
                SmokeHashToUnitFloat(seed ^ (trialIndex + 29u) * 3812015801u),
                SmokeHashToUnitFloat(seed ^ (trialIndex + 41u) * 3266489917u));
            const bool useLocalWindow = analyticCount > localWindow && branchXi < 0.75;
            const uint proposalDomain = useLocalWindow ? localWindow : analyticCount;
            const uint analyticIndex = min((uint)(xi * (float)proposalDomain), proposalDomain - 1u);
            const uint lightIndex = emissiveTriangleCount + analyticIndex;
            const RAB_LightInfo lightInfo = RAB_LoadLightInfo(lightIndex, false);
            const RAB_LightSample lightSample = RAB_SamplePolymorphicLight(lightInfo, surface, uv);
            if (lightSample.valid != 0u)
            {
                ++validSampleCount;
                bestScore = max(bestScore, RAB_GetLightSampleTargetPdfForSurface(lightSample, surface));
            }
        }
    }

    if (bestScore > 0.0)
    {
        const float heat = saturate(log2(1.0 + bestScore) * 0.16);
        return float3(heat, heat * 0.42, 0.0);
    }

    if (validSampleCount > 0u)
    {
        return float3(0.0, 0.12, 0.42);
    }

    return float3(0.42, 0.02, 0.02);
}

RTXDI_PTReservoir GenerateRestirPTInitialReservoir(RAB_Surface surface, uint2 pixel)
{
    RTXDI_PTInitialSamplingRuntimeParameters runtimeParams = RTXDI_EmptyPTInitialSamplingRuntimeParameters();
    runtimeParams.cameraPos = CameraOriginAndTMax.xyz;
    runtimeParams.prevCameraPos = PrevCameraOriginAndValid.w >= 0.5 ? PrevCameraOriginAndValid.xyz : CameraOriginAndTMax.xyz;
    runtimeParams.prevPrevCameraPos = CameraOriginAndTMax.xyz;

    const uint frameIndex = (uint)max(RestirPTInfo.x, 0.0);
    RTXDI_PathTracerRandomContext ptRandContext = RTXDI_InitializePathTracerRandomContext(
        pixel,
        frameIndex,
        0x9e3779b9u,
        0x85ebca6bu);
    RAB_PathTracerUserData ptud = RAB_EmptyPathTracerUserData();

    return GenerateInitialSamples(
        RestirPTParams.initialSampling,
        runtimeParams,
        RestirPTParams.reconnection,
        ptRandContext,
        surface,
        ptud);
}

void ProduceRestirPTIndirectInitialReservoirFromSurface(RAB_Surface surface, uint2 pixel)
{
    RTXDI_PTReservoir reservoir = RTXDI_EmptyPTReservoir();

    if (!RAB_SurfaceSupportsOpaqueDiffuseBrdf(surface))
    {
        StoreRestirPTInitialReservoir(pixel, reservoir);
        return;
    }

    reservoir = GenerateRestirPTInitialReservoir(surface, pixel);
    StoreRestirPTInitialReservoir(pixel, reservoir);
}

void ProduceRestirPTIndirectInitialReservoir(float3 rayOrigin, float3 rayDirection, PathTraceSmokePayload payload, uint2 pixel)
{
    RTXDI_PTReservoir reservoir = RTXDI_EmptyPTReservoir();

    if (payload.value == 0u || SmokePayloadIsGuiScreen(payload))
    {
        StoreRestirPTInitialReservoir(pixel, reservoir);
        return;
    }

    ProduceRestirPTIndirectInitialReservoirFromSurface(
        RAB_BuildSurfaceFromSmokePayload(payload, rayOrigin, rayDirection, true),
        pixel);
}

bool RestirPTReservoirHasUsefulSample(RTXDI_PTReservoir reservoir)
{
    const float targetLuminance = RTXDI_Luminance(max(reservoir.TargetFunction, float3(0.0, 0.0, 0.0)));
    return RTXDI_IsValidPTReservoir(reservoir) && reservoir.WeightSum > 0.0 && targetLuminance > 0.0;
}

float4 EvaluateRestirPTInitialReservoirDebug(float3 rayOrigin, float3 rayDirection, PathTraceSmokePayload payload, uint2 pixel)
{
    RTXDI_PTReservoir reservoir = RTXDI_EmptyPTReservoir();

    if (payload.value == 0u)
    {
        StoreRestirPTInitialReservoir(pixel, reservoir);
        return float4(0.0, 0.0, 0.0, 1.0);
    }

    if (SmokePayloadIsGuiScreen(payload))
    {
        StoreRestirPTInitialReservoir(pixel, reservoir);
        return CompositeSmokeGuiLayers(rayOrigin, rayDirection, payload);
    }

    RAB_Surface surface = RAB_BuildSurfaceFromSmokePayload(payload, rayOrigin, rayDirection, true);
    if (!RAB_SurfaceSupportsOpaqueDiffuseBrdf(surface))
    {
        StoreRestirPTInitialReservoir(pixel, reservoir);
        const float3 fallbackAlbedo = max(surface.material.diffuseAlbedo, float3(0.04, 0.04, 0.04));
        return float4(saturate(fallbackAlbedo * 0.03), 1.0);
    }

    if (PathTraceRestirPTIndirectConsumesInitialPrepass())
    {
        reservoir = LoadRestirPTInitialReservoir(pixel);
    }
    else
    {
        reservoir = GenerateRestirPTInitialReservoir(surface, pixel);
        StoreRestirPTInitialReservoir(pixel, reservoir);
    }

    if (!RestirPTReservoirHasUsefulSample(reservoir))
    {
        return float4(saturate(surface.material.diffuseAlbedo * 0.02 + EvaluateRestirPTLocalLightCandidateDebug(surface, pixel)), 1.0);
    }

    const float3 heatBase = reservoir.TargetFunction + reservoir.Radiance * max(reservoir.WeightSum, 0.0);
    const float3 heat = heatBase / (1.0 + heatBase);
    const float samplePulse = saturate((float)reservoir.M / max((float)RestirPTParams.initialSampling.numInitialSamples, 1.0));
    return float4(saturate(heat + float3(0.02, 0.12, 0.04) * samplePulse), 1.0);
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
    if (dot(normal, lightDir) <= 0.0)
    {
        return 0.0;
    }

    const float normalOffsetSign = dot(normal, lightDir) >= 0.0 ? 1.0 : -1.0;
    const float3 shadowOrigin = surface.worldPos + normal * (normalOffsetSign * 0.75) + lightDir * 0.25;
    const float shadowTMax = max(distance - 0.5, 0.01);
    return TraceSmokeShadowVisibility(shadowOrigin, lightDir, shadowTMax, ignoreInstanceId, ignorePrimitiveIndex, ignoreMaterialId);
}

float4 EvaluateRestirPTInitialReservoirShading(float3 rayOrigin, float3 rayDirection, PathTraceSmokePayload payload, uint2 pixel, bool traceVisibility)
{
    RTXDI_PTReservoir reservoir = RTXDI_EmptyPTReservoir();

    if (payload.value == 0u)
    {
        StoreRestirPTInitialReservoir(pixel, reservoir);
        return float4(0.0, 0.0, 0.0, 1.0);
    }

    if (SmokePayloadIsGuiScreen(payload))
    {
        StoreRestirPTInitialReservoir(pixel, reservoir);
        return CompositeSmokeGuiLayers(rayOrigin, rayDirection, payload);
    }

    RAB_Surface surface = RAB_BuildSurfaceFromSmokePayload(payload, rayOrigin, rayDirection, true);
    if (!RAB_SurfaceSupportsOpaqueDiffuseBrdf(surface))
    {
        StoreRestirPTInitialReservoir(pixel, reservoir);
        return float4(saturate(surface.material.diffuseAlbedo * 0.025), 1.0);
    }

    if (PathTraceRestirPTIndirectConsumesInitialPrepass())
    {
        reservoir = LoadRestirPTInitialReservoir(pixel);
    }
    else
    {
        reservoir = GenerateRestirPTInitialReservoir(surface, pixel);
        StoreRestirPTInitialReservoir(pixel, reservoir);
    }

    if (!RestirPTReservoirHasUsefulSample(reservoir))
    {
        return float4(saturate(surface.material.diffuseAlbedo * 0.025), 1.0);
    }

    float3 contribution = max(reservoir.TargetFunction, float3(0.0, 0.0, 0.0)) * max(reservoir.WeightSum, 0.0);
    if (!all(contribution == contribution) || any(abs(contribution) > 65504.0))
    {
        contribution = float3(0.0, 0.0, 0.0);
    }
    if (traceVisibility)
    {
        contribution *= RestirPTTraceReservoirVisibility(surface, reservoir);
    }

    const float3 preview = contribution / (1.0 + contribution);
    return float4(saturate(surface.material.diffuseAlbedo * 0.015 + preview), 1.0);
}

float3 RestirPTGiSanitizeContribution(float3 contribution)
{
    if (!all(contribution == contribution) || any(abs(contribution) > 65504.0))
    {
        return float3(0.0, 0.0, 0.0);
    }
    return max(contribution, float3(0.0, 0.0, 0.0));
}

float3 RestirPTGiToneMapPreview(float3 contribution)
{
    contribution = RestirPTGiSanitizeContribution(contribution * max(RestirPTInfo.w, 0.0));
    return contribution / (float3(1.0, 1.0, 1.0) + contribution);
}

float3 RestirPTGiFallback(RAB_Surface surface)
{
    if (!RAB_IsSurfaceValid(surface))
    {
        return float3(0.0, 0.0, 0.0);
    }
    return RestirPTGiToneMapPreview(surface.material.emissiveRadiance);
}

float3 RestirPTReservoirPreviewContribution(RTXDI_PTReservoir reservoir)
{
    return RestirPTGiSanitizeContribution(reservoir.TargetFunction * max(reservoir.WeightSum, 0.0));
}

float4 RestirPTGiReservoirSourceColor(RTXDI_PTReservoir reservoir)
{
    if (!RTXDI_IsValidPTReservoir(reservoir))
    {
        return float4(0.55, 0.04, 0.04, 1.0);
    }
    if (!RTXDI_ConnectsToNeeLight(reservoir))
    {
        const float heat = saturate(log2(1.0 + RAB_Luminance(RestirPTReservoirPreviewContribution(reservoir))) * 0.18);
        return heat > 0.0 ? float4(0.72 + heat * 0.20, 0.10 + heat * 0.18, 0.95, 1.0) : float4(0.08, 0.42, 0.28, 1.0);
    }

    const RTXDI_SampledLightData sampledLightData = RTXDI_GetSampledLightData(reservoir);
    if (!RTXDI_SampledLightData_IsValidLightData(sampledLightData))
    {
        return float4(0.55, 0.04, 0.04, 1.0);
    }
    const RAB_LightInfo lightInfo = RAB_LoadLightInfo(RTXDI_SampledLightData_GetLightIndex(sampledLightData), false);
    if (!RAB_IsLightInfoValid(lightInfo))
    {
        return float4(0.55, 0.04, 0.04, 1.0);
    }
    const float heat = saturate(log2(1.0 + RAB_Luminance(RestirPTReservoirPreviewContribution(reservoir))) * 0.18);
    if (lightInfo.lightType == RAB_LIGHT_TYPE_DOOM_ANALYTIC_SPHERE)
    {
        return float4(0.0, saturate(0.35 + heat * 0.65), 1.0, 1.0);
    }
    if (lightInfo.lightType == RAB_LIGHT_TYPE_EMISSIVE_TRIANGLE)
    {
        return float4(1.0, saturate(0.35 + heat * 0.45), 0.0, 1.0);
    }
    return float4(0.55, 0.04, 0.04, 1.0);
}

float4 EvaluateRestirPTIndirectReservoirDebug(float3 rayOrigin, float3 rayDirection, PathTraceSmokePayload payload, uint2 pixel)
{
    RTXDI_PTReservoir reservoir = RTXDI_EmptyPTReservoir();

    if (payload.value == 0u)
    {
        StoreRestirPTInitialReservoir(pixel, reservoir);
        return float4(0.0, 0.0, 0.0, 1.0);
    }

    if (SmokePayloadIsGuiScreen(payload))
    {
        StoreRestirPTInitialReservoir(pixel, reservoir);
        return CompositeSmokeGuiLayers(rayOrigin, rayDirection, payload);
    }

    RAB_Surface surface = RAB_BuildSurfaceFromSmokePayload(payload, rayOrigin, rayDirection, true);
    if (!RAB_SurfaceSupportsOpaqueDiffuseBrdf(surface))
    {
        StoreRestirPTInitialReservoir(pixel, reservoir);
        return float4(saturate(surface.material.diffuseAlbedo * 0.025), 1.0);
    }

    if (PathTraceRestirPTIndirectConsumesInitialPrepass())
    {
        reservoir = LoadRestirPTInitialReservoir(pixel);
    }
    else
    {
        reservoir = GenerateRestirPTInitialReservoir(surface, pixel);
        StoreRestirPTInitialReservoir(pixel, reservoir);
    }

    if (!RTXDI_IsValidPTReservoir(reservoir))
    {
        return float4(saturate(surface.material.diffuseAlbedo * 0.015 + float3(0.36, 0.02, 0.02)), 1.0);
    }

    const float heat = saturate(log2(1.0 + RAB_Luminance(RestirPTReservoirPreviewContribution(reservoir))) * 0.18);
    const float3 sourceTint = RestirPTGiReservoirSourceColor(reservoir).rgb;
    const float samplePulse = saturate((float)reservoir.M / max((float)RestirPTParams.initialSampling.numInitialSamples, 1.0));
    return float4(saturate(surface.material.diffuseAlbedo * 0.015 + sourceTint * (0.12 + heat * 0.78) + float3(0.0, 0.08, 0.03) * samplePulse), 1.0);
}

float4 EvaluateRestirPTIndirectReservoirShading(float3 rayOrigin, float3 rayDirection, PathTraceSmokePayload payload, uint2 pixel)
{
    RTXDI_PTReservoir reservoir = RTXDI_EmptyPTReservoir();

    if (payload.value == 0u)
    {
        StoreRestirPTInitialReservoir(pixel, reservoir);
        return float4(0.0, 0.0, 0.0, 1.0);
    }

    if (SmokePayloadIsGuiScreen(payload))
    {
        StoreRestirPTInitialReservoir(pixel, reservoir);
        return CompositeSmokeGuiLayers(rayOrigin, rayDirection, payload);
    }

    RAB_Surface surface = RAB_BuildSurfaceFromSmokePayload(payload, rayOrigin, rayDirection, true);
    if (!RAB_SurfaceSupportsOpaqueDiffuseBrdf(surface))
    {
        StoreRestirPTInitialReservoir(pixel, reservoir);
        return float4(saturate(surface.material.diffuseAlbedo * 0.025), 1.0);
    }

    if (PathTraceRestirPTIndirectConsumesInitialPrepass())
    {
        reservoir = LoadRestirPTInitialReservoir(pixel);
    }
    else
    {
        reservoir = GenerateRestirPTInitialReservoir(surface, pixel);
        StoreRestirPTInitialReservoir(pixel, reservoir);
    }

    if (!RTXDI_IsValidPTReservoir(reservoir))
    {
        return float4(RestirPTGiFallback(surface), 1.0);
    }

    const float3 preview = RestirPTGiToneMapPreview(RestirPTReservoirPreviewContribution(reservoir));
    return float4(saturate(RestirPTGiFallback(surface) + preview), 1.0);
}

float4 EvaluateRestirPTIndirectPathAttribution(float3 rayOrigin, float3 rayDirection, PathTraceSmokePayload payload, uint2 pixel)
{
    RTXDI_PTReservoir reservoir = RTXDI_EmptyPTReservoir();

    if (payload.value == 0u)
    {
        StoreRestirPTInitialReservoir(pixel, reservoir);
        return float4(0.0, 0.0, 0.0, 1.0);
    }

    if (SmokePayloadIsGuiScreen(payload))
    {
        StoreRestirPTInitialReservoir(pixel, reservoir);
        return CompositeSmokeGuiLayers(rayOrigin, rayDirection, payload);
    }

    RAB_Surface surface = RAB_BuildSurfaceFromSmokePayload(payload, rayOrigin, rayDirection, true);
    if (!RAB_SurfaceSupportsOpaqueDiffuseBrdf(surface))
    {
        StoreRestirPTInitialReservoir(pixel, reservoir);
        return float4(0.18, 0.18, 0.18, 1.0);
    }

    if (PathTraceRestirPTIndirectConsumesInitialPrepass())
    {
        reservoir = LoadRestirPTInitialReservoir(pixel);
    }
    else
    {
        reservoir = GenerateRestirPTInitialReservoir(surface, pixel);
        StoreRestirPTInitialReservoir(pixel, reservoir);
    }
    return RestirPTGiReservoirSourceColor(reservoir);
}

#ifdef RB_PT_RESTIR_COMBINED_SHADING
float4 EvaluateRestirPTCombinedDirectGiPreviewFromSurface(RAB_Surface surface, uint2 pixel, bool traceDirectVisibility)
{
    if (!RAB_IsSurfaceValid(surface))
    {
        return float4(0.0, 0.0, 0.0, 1.0);
    }

    if (!RAB_SurfaceSupportsOpaqueDiffuseBrdf(surface))
    {
        return float4(RestirPTGiFallback(surface), 1.0);
    }

    float3 directContribution = float3(0.0, 0.0, 0.0);
    const bool directValid = RestirPTTryEvaluateSpatialDirectLighting(surface, pixel, traceDirectVisibility, directContribution);

    const RTXDI_PTReservoir giReservoir = LoadRestirPTInitialReservoir(pixel);
    const bool giValid = RestirPTReservoirHasUsefulSample(giReservoir);
    const float3 giContribution = giValid ? RestirPTReservoirPreviewContribution(giReservoir) : float3(0.0, 0.0, 0.0);

    if (!directValid && !giValid)
    {
        return float4(RestirPTGiFallback(surface), 1.0);
    }

    const float3 combinedPreview = RestirPTGiToneMapPreview(directContribution + giContribution);
    return float4(saturate(RestirPTGiFallback(surface) + combinedPreview), 1.0);
}

float4 EvaluateRestirPTCombinedDirectGiPreview(float3 rayOrigin, float3 rayDirection, PathTraceSmokePayload payload, uint2 pixel, bool traceDirectVisibility)
{
    if (payload.value == 0u)
    {
        return float4(0.0, 0.0, 0.0, 1.0);
    }

    if (SmokePayloadIsGuiScreen(payload))
    {
        return CompositeSmokeGuiLayers(rayOrigin, rayDirection, payload);
    }

    const RAB_Surface surface = RAB_BuildSurfaceFromSmokePayload(payload, rayOrigin, rayDirection, true);
    return EvaluateRestirPTCombinedDirectGiPreviewFromSurface(surface, pixel, traceDirectVisibility);
}
#else
float4 EvaluateRestirPTCombinedDirectGiPreviewFromSurface(RAB_Surface surface, uint2 pixel, bool traceDirectVisibility)
{
    return float4(0.16, 0.04, 0.20, 1.0);
}

float4 EvaluateRestirPTCombinedDirectGiPreview(float3 rayOrigin, float3 rayDirection, PathTraceSmokePayload payload, uint2 pixel, bool traceDirectVisibility)
{
    return float4(0.16, 0.04, 0.20, 1.0);
}
#endif
#else
float4 EvaluateRestirPTInitialReservoirDebug(float3 rayOrigin, float3 rayDirection, PathTraceSmokePayload payload, uint2 pixel)
{
    return float4(0.16, 0.04, 0.20, 1.0);
}

float4 EvaluateRestirPTInitialReservoirShading(float3 rayOrigin, float3 rayDirection, PathTraceSmokePayload payload, uint2 pixel, bool traceVisibility)
{
    return float4(0.16, 0.04, 0.20, 1.0);
}

void ProduceRestirPTIndirectInitialReservoir(float3 rayOrigin, float3 rayDirection, PathTraceSmokePayload payload, uint2 pixel)
{
}

void ProduceRestirPTIndirectInitialReservoirFromSurface(RAB_Surface surface, uint2 pixel)
{
}

float4 EvaluateRestirPTTemporalReservoirDebug(RAB_Surface currentSurface, uint2 pixel)
{
    return float4(0.16, 0.04, 0.20, 1.0);
}

float4 EvaluateRestirPTTemporalReservoirShading(RAB_Surface currentSurface, uint2 pixel, bool traceVisibility)
{
    return float4(0.16, 0.04, 0.20, 1.0);
}

float4 EvaluateRestirPTSpatialReservoirShading(RAB_Surface currentSurface, uint2 pixel, bool traceVisibility)
{
    return float4(0.16, 0.04, 0.20, 1.0);
}

float4 EvaluateRestirPTTemporalLightSourceAttribution(RAB_Surface currentSurface, uint2 pixel)
{
    return float4(0.16, 0.04, 0.20, 1.0);
}

float4 EvaluateRestirPTSpatialLightSourceAttribution(RAB_Surface currentSurface, uint2 pixel)
{
    return float4(0.16, 0.04, 0.20, 1.0);
}

float4 EvaluateRestirPTIndirectReservoirDebug(float3 rayOrigin, float3 rayDirection, PathTraceSmokePayload payload, uint2 pixel)
{
    return float4(0.16, 0.04, 0.20, 1.0);
}

float4 EvaluateRestirPTIndirectReservoirShading(float3 rayOrigin, float3 rayDirection, PathTraceSmokePayload payload, uint2 pixel)
{
    return float4(0.16, 0.04, 0.20, 1.0);
}

float4 EvaluateRestirPTIndirectPathAttribution(float3 rayOrigin, float3 rayDirection, PathTraceSmokePayload payload, uint2 pixel)
{
    return float4(0.16, 0.04, 0.20, 1.0);
}

float4 EvaluateRestirPTCombinedDirectGiPreview(float3 rayOrigin, float3 rayDirection, PathTraceSmokePayload payload, uint2 pixel, bool traceDirectVisibility)
{
    return float4(0.16, 0.04, 0.20, 1.0);
}

float4 EvaluateRestirPTCombinedDirectGiPreviewFromSurface(RAB_Surface surface, uint2 pixel, bool traceDirectVisibility)
{
    return float4(0.16, 0.04, 0.20, 1.0);
}
#endif

uint LoadSmokeTriangleMaterialIndex(uint instanceId, uint primitiveIndex)
{
    const uint materialCount = (uint)TextureInfo.z;
    uint materialIndex = 0xffffffffu;
    if (instanceId == 0u)
    {
        if (primitiveIndex >= PathTraceStaticTriangleCount())
        {
            return 0xffffffffu;
        }
        materialIndex = SmokeStaticTriangleMaterialIndexes[primitiveIndex];
    }
    else if (instanceId >= 2u)
    {
        const uint routeInstanceIndex = instanceId - 2u;
        if (routeInstanceIndex >= PathTraceRigidRouteInstanceCount())
        {
            return 0xffffffffu;
        }
        const PathTraceRigidRouteInstance routeInstance = SmokeRigidRouteInstances[routeInstanceIndex];
        if (primitiveIndex >= routeInstance.triangleCount ||
            routeInstance.triangleOffset + primitiveIndex >= PathTraceRigidRouteTriangleCount())
        {
            return 0xffffffffu;
        }
        materialIndex = SmokeRigidRouteTriangleMaterialIndexes[routeInstance.triangleOffset + primitiveIndex];
    }
    else
    {
        if (primitiveIndex >= PathTraceDynamicTriangleCount())
        {
            return 0xffffffffu;
        }
        materialIndex = SmokeDynamicTriangleMaterialIndexes[primitiveIndex];
    }
    return materialIndex < materialCount ? materialIndex : 0xffffffffu;
}

uint LoadSmokeTriangleMaterialId(uint instanceId, uint primitiveIndex)
{
    if (instanceId == 0u)
    {
        if (primitiveIndex >= PathTraceStaticTriangleCount())
        {
            return 0xffffffffu;
        }
        return SmokeStaticTriangleMaterials[primitiveIndex];
    }
    if (instanceId >= 2u)
    {
        const uint routeInstanceIndex = instanceId - 2u;
        if (routeInstanceIndex >= PathTraceRigidRouteInstanceCount())
        {
            return 0xffffffffu;
        }
        const PathTraceRigidRouteInstance routeInstance = SmokeRigidRouteInstances[routeInstanceIndex];
        if (primitiveIndex >= routeInstance.triangleCount ||
            routeInstance.triangleOffset + primitiveIndex >= PathTraceRigidRouteTriangleCount())
        {
            return 0xffffffffu;
        }
        return SmokeRigidRouteTriangleMaterials[routeInstance.triangleOffset + primitiveIndex];
    }
    if (primitiveIndex >= PathTraceDynamicTriangleCount())
    {
        return 0xffffffffu;
    }
    return SmokeDynamicTriangleMaterials[primitiveIndex];
}

uint LoadSmokeTriangleClassAndFlags(uint instanceId, uint primitiveIndex)
{
    if (instanceId >= 2u)
    {
        return RT_SMOKE_SURFACE_CLASS_RIGID_ENTITY;
    }
    if (instanceId == 0u && primitiveIndex >= PathTraceStaticTriangleCount())
    {
        return 0u;
    }
    if (instanceId == 1u && primitiveIndex >= PathTraceDynamicTriangleCount())
    {
        return 0u;
    }
    return instanceId == 0 ? SmokeStaticTriangleClasses[primitiveIndex] : SmokeDynamicTriangleClasses[primitiveIndex];
}

bool SmokeTriangleIndexRangeValid(uint instanceId, uint primitiveIndex)
{
    if (instanceId == 0u)
    {
        const uint indexOffset = primitiveIndex * 3u;
        return primitiveIndex < PathTraceStaticTriangleCount() &&
            indexOffset <= PathTraceStaticIndexCount() &&
            indexOffset + 2u < PathTraceStaticIndexCount();
    }
    if (instanceId == 1u)
    {
        const uint indexOffset = primitiveIndex * 3u;
        return primitiveIndex < PathTraceDynamicTriangleCount() &&
            indexOffset <= PathTraceDynamicIndexCount() &&
            indexOffset + 2u < PathTraceDynamicIndexCount();
    }
    const uint routeInstanceIndex = instanceId - 2u;
    if (routeInstanceIndex >= PathTraceRigidRouteInstanceCount())
    {
        return false;
    }
    const PathTraceRigidRouteInstance routeInstance = SmokeRigidRouteInstances[routeInstanceIndex];
    const uint routeIndexOffset = routeInstance.indexOffset + primitiveIndex * 3u;
    return primitiveIndex < routeInstance.triangleCount &&
        routeInstance.triangleOffset + primitiveIndex < PathTraceRigidRouteTriangleCount() &&
        routeIndexOffset <= PathTraceRigidRouteIndexCount() &&
        routeIndexOffset + 2u < PathTraceRigidRouteIndexCount();
}

float2 InterpolateSmokeTexCoord(uint instanceId, uint primitiveIndex, float2 barycentrics)
{
    if (instanceId >= 2u || !SmokeTriangleIndexRangeValid(instanceId, primitiveIndex))
    {
        return float2(0.0, 0.0);
    }
    const uint indexOffset = primitiveIndex * 3;
    const uint i0 = instanceId == 0 ? SmokeStaticIndices[indexOffset + 0] : SmokeDynamicIndices[indexOffset + 0];
    const uint i1 = instanceId == 0 ? SmokeStaticIndices[indexOffset + 1] : SmokeDynamicIndices[indexOffset + 1];
    const uint i2 = instanceId == 0 ? SmokeStaticIndices[indexOffset + 2] : SmokeDynamicIndices[indexOffset + 2];
    const uint vertexCount = instanceId == 0 ? PathTraceStaticVertexCount() : PathTraceDynamicVertexCount();
    if (i0 >= vertexCount || i1 >= vertexCount || i2 >= vertexCount)
    {
        return float2(0.0, 0.0);
    }
    const float2 uv0 = (instanceId == 0 ? SmokeStaticVertices[i0].texCoord : SmokeDynamicVertices[i0].texCoord).xy;
    const float2 uv1 = (instanceId == 0 ? SmokeStaticVertices[i1].texCoord : SmokeDynamicVertices[i1].texCoord).xy;
    const float2 uv2 = (instanceId == 0 ? SmokeStaticVertices[i2].texCoord : SmokeDynamicVertices[i2].texCoord).xy;
    const float3 weights = float3(1.0 - barycentrics.x - barycentrics.y, barycentrics.x, barycentrics.y);
    return uv0 * weights.x + uv1 * weights.y + uv2 * weights.z;
}

float4 InterpolateSmokeVertexColor(uint instanceId, uint primitiveIndex, float2 barycentrics)
{
    if (instanceId >= 2u || !SmokeTriangleIndexRangeValid(instanceId, primitiveIndex))
    {
        return float4(1.0, 1.0, 1.0, 1.0);
    }
    const uint indexOffset = primitiveIndex * 3;
    const uint i0 = instanceId == 0 ? SmokeStaticIndices[indexOffset + 0] : SmokeDynamicIndices[indexOffset + 0];
    const uint i1 = instanceId == 0 ? SmokeStaticIndices[indexOffset + 1] : SmokeDynamicIndices[indexOffset + 1];
    const uint i2 = instanceId == 0 ? SmokeStaticIndices[indexOffset + 2] : SmokeDynamicIndices[indexOffset + 2];
    const uint vertexCount = instanceId == 0 ? PathTraceStaticVertexCount() : PathTraceDynamicVertexCount();
    if (i0 >= vertexCount || i1 >= vertexCount || i2 >= vertexCount)
    {
        return float4(1.0, 1.0, 1.0, 1.0);
    }
    const float4 c0 = instanceId == 0 ? SmokeStaticVertices[i0].color : SmokeDynamicVertices[i0].color;
    const float4 c1 = instanceId == 0 ? SmokeStaticVertices[i1].color : SmokeDynamicVertices[i1].color;
    const float4 c2 = instanceId == 0 ? SmokeStaticVertices[i2].color : SmokeDynamicVertices[i2].color;
    const float3 weights = float3(1.0 - barycentrics.x - barycentrics.y, barycentrics.x, barycentrics.y);
    return saturate(c0 * weights.x + c1 * weights.y + c2 * weights.z);
}

bool SmokeGuiRejectsTransparentHit(uint instanceId, uint primitiveIndex, float2 barycentrics, uint triangleClassAndFlags)
{
    const uint surfaceClass = triangleClassAndFlags & RT_SMOKE_TRIANGLE_CLASS_MASK;
    const uint translucentSubtype = (triangleClassAndFlags & RT_SMOKE_TRANSLUCENT_SUBTYPE_MASK) >> RT_SMOKE_TRANSLUCENT_SUBTYPE_SHIFT;
    if (surfaceClass != RT_SMOKE_SURFACE_CLASS_TRANSLUCENT || translucentSubtype != RT_SMOKE_TRANSLUCENT_SUBTYPE_GUI_SCREEN)
    {
        return false;
    }

    return InterpolateSmokeVertexColor(instanceId, primitiveIndex, barycentrics).a <= 0.03;
}

bool SmokeParticleDitherRejectsHit(PathTraceSmokeMaterial material, float2 texCoord, float2 barycentrics, uint instanceId, uint primitiveIndex, uint triangleClassAndFlags, bool shadowRay)
{
    const uint smokeParticleFlags = (uint)LightInfo.w;
    if ((smokeParticleFlags & 1u) == 0u)
    {
        return false;
    }

    const uint surfaceClass = triangleClassAndFlags & RT_SMOKE_TRIANGLE_CLASS_MASK;
    const uint translucentSubtype = (triangleClassAndFlags & RT_SMOKE_TRANSLUCENT_SUBTYPE_MASK) >> RT_SMOKE_TRANSLUCENT_SUBTYPE_SHIFT;
    if (surfaceClass != RT_SMOKE_SURFACE_CLASS_TRANSLUCENT || translucentSubtype != RT_SMOKE_TRANSLUCENT_SUBTYPE_SMOKE_PARTICLE)
    {
        return false;
    }

    if (shadowRay)
    {
        return true;
    }

    const float4 alphaTexel = SampleSmokeAlphaTexture(material, texCoord);
    const float textureMask = saturate(max(alphaTexel.a, max(max(alphaTexel.r, alphaTexel.g), alphaTexel.b)));
    const float2 wrappedTexCoord = frac(abs(texCoord));
    const float2 edgeDistance = min(wrappedTexCoord, 1.0 - wrappedTexCoord);
    const float edgeFade = (smokeParticleFlags & 2u) != 0u
        ? saturate(min(edgeDistance.x, edgeDistance.y) * 8.0)
        : 1.0;
    const float alpha = saturate(textureMask * edgeFade * saturate(LightInfo.z));
    const uint2 ditherCell = uint2(floor(wrappedTexCoord * 128.0));
    const uint2 baryCell = uint2(saturate(barycentrics) * 255.0);
    const uint hash =
        primitiveIndex * 747796405u ^
        instanceId * 2891336453u ^
        ditherCell.x * 277803737u ^
        ditherCell.y * 3266489917u ^
        baryCell.x * 668265263u ^
        baryCell.y * 2246822519u;
    return alpha < SmokeHashToUnitFloat(hash);
}

bool SmokeGlassFallbackRejectsHit(PathTraceSmokeMaterial material, float2 texCoord, float2 barycentrics, uint instanceId, uint primitiveIndex, uint triangleClassAndFlags, bool shadowRay)
{
    if (PortalWindowInfo.x <= 0.0)
    {
        return false;
    }

    const uint surfaceClass = triangleClassAndFlags & RT_SMOKE_TRIANGLE_CLASS_MASK;
    const uint translucentSubtype = (triangleClassAndFlags & RT_SMOKE_TRANSLUCENT_SUBTYPE_MASK) >> RT_SMOKE_TRANSLUCENT_SUBTYPE_SHIFT;
    if (surfaceClass != RT_SMOKE_SURFACE_CLASS_TRANSLUCENT)
    {
        return false;
    }

    const bool portalWindow = translucentSubtype == RT_SMOKE_TRANSLUCENT_SUBTYPE_PORTAL_WINDOW;
    const bool objectGlass =
        translucentSubtype == RT_SMOKE_TRANSLUCENT_SUBTYPE_OBJECT_GLASS &&
        (material.flags & RT_SMOKE_MATERIAL_OBJECT_GLASS_FALLBACK) != 0u;
    if (!portalWindow && !objectGlass)
    {
        return false;
    }

    const float4 alphaTexel = SampleSmokeAlphaTexture(material, texCoord);
    const float textureMask = saturate(max(alphaTexel.a, max(max(alphaTexel.r, alphaTexel.g), alphaTexel.b)));
    float opacity = max(saturate(textureMask * PortalWindowInfo.y), saturate(PortalWindowInfo.z));
    if ((material.flags & RT_SMOKE_MATERIAL_PORTAL_WINDOW_FALLBACK) != 0u)
    {
        opacity = max(opacity, 0.18);
    }
    if (objectGlass)
    {
        opacity = max(saturate(textureMask * 0.22), 0.12);
    }
    if (shadowRay)
    {
        opacity *= saturate(PortalWindowInfo.w);
    }

    const float2 wrappedTexCoord = frac(abs(texCoord));
    const uint2 ditherCell = uint2(floor(wrappedTexCoord * 192.0));
    const uint2 baryCell = uint2(saturate(barycentrics) * 255.0);
    const uint hash =
        primitiveIndex * 1597334677u ^
        instanceId * 3812015801u ^
        ditherCell.x * 747796405u ^
        ditherCell.y * 2891336453u ^
        baryCell.x * 277803737u ^
        baryCell.y * 3266489917u;
    return opacity < SmokeHashToUnitFloat(hash);
}

bool SmokeAlphaRejectsHit(uint instanceId, uint primitiveIndex, float2 barycentrics, uint rayMode)
{
    if (PathTraceSafetyDisabled(RT_PT_SAFETY_DISABLE_ANY_HIT_ALPHA) ||
        !SmokeTriangleIndexRangeValid(instanceId, primitiveIndex))
    {
        return false;
    }

    const bool shadowRay = rayMode != 0u;
    const PathTraceSmokeMaterial material = LoadSmokeMaterial(LoadSmokeTriangleMaterialIndex(instanceId, primitiveIndex));
    const float2 texCoord = InterpolateSmokeTexCoord(instanceId, primitiveIndex, barycentrics);
    const uint triangleClassAndFlags = LoadSmokeTriangleClassAndFlags(instanceId, primitiveIndex);
    if (SmokeGuiRejectsTransparentHit(instanceId, primitiveIndex, barycentrics, triangleClassAndFlags))
    {
        return true;
    }

    if (SmokeParticleDitherRejectsHit(material, texCoord, barycentrics, instanceId, primitiveIndex, triangleClassAndFlags, shadowRay))
    {
        return true;
    }

    if (SmokeGlassFallbackRejectsHit(material, texCoord, barycentrics, instanceId, primitiveIndex, triangleClassAndFlags, shadowRay))
    {
        return true;
    }

    if (SmokeAdditiveDecalRejectsHit(material, texCoord, shadowRay))
    {
        return true;
    }

    if (SmokeFilterDecalRejectsHit(material, texCoord, barycentrics, instanceId, primitiveIndex, shadowRay))
    {
        return true;
    }

    if ((material.flags & RT_SMOKE_MATERIAL_ALPHA_TEST) == 0u)
    {
        return false;
    }

    return SmokeAlphaCoverage(material, texCoord) < material.alphaCutoff;
}

float TraceSmokeShadowVisibility(float3 origin, float3 direction, float tMax, uint ignoreInstanceId, uint ignorePrimitiveIndex, uint ignoreMaterialId)
{
    PathTraceSmokeShadowPayload shadowPayload = InitSmokeShadowPayload(ignoreInstanceId, ignorePrimitiveIndex, ignoreMaterialId);

    RayDesc shadowRay;
    shadowRay.Origin = origin;
    shadowRay.Direction = direction;
    shadowRay.TMin = 0.01;
    shadowRay.TMax = tMax;

    const uint rayFlags = ignoreInstanceId != 0xffffffffu
        ? (RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER | RAY_FLAG_FORCE_NON_OPAQUE)
        : (RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER);

    TraceRay(
        SmokeScene,
        rayFlags,
        0xff,
        1,
        1,
        1,
        shadowRay,
        shadowPayload);

    return shadowPayload.hit == 0u ? 1.0 : 0.0;
}

bool SmokePayloadIsGuiScreen(PathTraceSmokePayload payload);
float4 CompositeSmokeGuiLayers(float3 rayOrigin, float3 rayDirection, PathTraceSmokePayload firstPayload);

float3 EvaluateSmokeLightSpriteProxies(float3 rayOrigin, float3 rayDirection, float sceneHitT)
{
    if (LightSpriteInfo.x <= 0.0 || PathTraceSafetyDisabled(RT_PT_SAFETY_DISABLE_SELECTED_LIGHT_LOOP))
    {
        return float3(0.0, 0.0, 0.0);
    }

    float3 sprites = float3(0.0, 0.0, 0.0);
    const uint lightCount = PathTraceSafetyDisabled(RT_PT_SAFETY_DISABLE_SELECTED_LIGHT_LOOP) ? 0u : min((uint)LightInfo.x, RT_SMOKE_MAX_DEBUG_LIGHTS);
    [loop]
    for (uint lightIndex = 0u; lightIndex < lightCount; lightIndex++)
    {
        const float4 lightOriginAndRadius = LightOriginAndRadius[lightIndex];
        const float spriteWeight = LightColorAndIntensity[lightIndex].w;
        if (spriteWeight <= 0.0)
        {
            continue;
        }

        const float3 toLight = lightOriginAndRadius.xyz - rayOrigin;
        const float t = dot(toLight, rayDirection);
        if (t <= 0.0 || t >= sceneHitT)
        {
            continue;
        }

        const float3 closest = rayOrigin + rayDirection * t;
        const float distanceToRay = length(lightOriginAndRadius.xyz - closest);
        const float proxyRadius = clamp(lightOriginAndRadius.w * LightSpriteInfo.y, 2.0, 96.0);
        const float core = saturate(1.0 - distanceToRay / proxyRadius);
        const float glow = core * core * (3.0 - 2.0 * core);
        const float distanceFade = saturate(1.0 - t / max(CameraOriginAndTMax.w, 1.0));
        const float3 lightColor = max(LightColorAndIntensity[lightIndex].rgb, float3(0.0, 0.0, 0.0));
        sprites += lightColor * glow * (0.35 + distanceFade * 0.65) * LightSpriteInfo.z * spriteWeight;
    }

    return sprites;
}

float3 SmokeCosineHemisphereDirection(float3 normal, uint seed)
{
    const float r1 = SmokeHashToUnitFloat(seed ^ 0x9e3779b9u);
    const float r2 = SmokeHashToUnitFloat(seed ^ 0x85ebca6bu);
    const float phi = 6.2831853 * r1;
    const float radius = sqrt(r2);
    const float x = cos(phi) * radius;
    const float y = sin(phi) * radius;
    const float z = sqrt(max(0.0, 1.0 - r2));
    const float3 tangent = BuildPerpendicular(normal);
    const float3 bitangent = SafeNormalize(cross(normal, tangent), float3(0.0, 1.0, 0.0));
    return SafeNormalize(tangent * x + bitangent * y + normal * z, normal);
}

float SmokeRaySphereHitT(float3 rayOrigin, float3 rayDirection, float3 sphereCenter, float sphereRadius, float fallbackT)
{
    const float3 oc = rayOrigin - sphereCenter;
    const float b = dot(oc, rayDirection);
    const float c = dot(oc, oc) - sphereRadius * sphereRadius;
    const float h = b * b - c;
    if (h <= 0.0)
    {
        return fallbackT;
    }

    const float nearT = -b - sqrt(h);
    return nearT > 0.01 ? nearT : fallbackT;
}

float3 SmokeSampleSphereSolidAngle(float3 axis, float cosThetaMax, uint seed)
{
    const float xi1 = SmokeHashToUnitFloat(seed ^ 0x27d4eb2du);
    const float xi2 = SmokeHashToUnitFloat(seed ^ 0x165667b1u);
    const float cosTheta = lerp(1.0, cosThetaMax, xi1);
    const float sinTheta = sqrt(max(0.0, 1.0 - cosTheta * cosTheta));
    const float phi = 6.2831853 * xi2;
    const float3 tangent = BuildPerpendicular(axis);
    const float3 bitangent = SafeNormalize(cross(axis, tangent), float3(0.0, 1.0, 0.0));
    return SafeNormalize(axis * cosTheta + tangent * (cos(phi) * sinTheta) + bitangent * (sin(phi) * sinTheta), axis);
}

#include "pathtrace_nee.hlsli"

#ifdef RB_PT_ENABLE_RESTIR
uint RestirPTReflectionProducerMode()
{
    if (PathTraceSafetyDisabled(RT_PT_SAFETY_DISABLE_REFLECTION_RAY))
    {
        return 0u;
    }
    return (uint)clamp(floor(GeometryInfo3.w + 0.5), 0.0, 2.0);
}

bool RestirPTSurfaceSupportsTracedReflection(RAB_Surface surface, uint reflectionMode)
{
    if (reflectionMode == 0u || !RAB_IsSurfaceValid(surface) || !RAB_SurfaceSupportsOpaqueDiffuseBrdf(surface))
    {
        return false;
    }

    const float roughnessLimit = reflectionMode == 1u ? 0.36 : 0.72;
    const float roughness = saturate(RAB_GetSurfaceRoughness(surface));
    const float specularMax = max(max(surface.material.specularF0.x, surface.material.specularF0.y), surface.material.specularF0.z);
    return roughness <= roughnessLimit && specularMax >= 0.035;
}

float4 EvaluateRestirPTTracedReflectionFromSurface(RAB_Surface surface, uint2 pixel)
{
    const uint reflectionMode = RestirPTReflectionProducerMode();
    if (!RestirPTSurfaceSupportsTracedReflection(surface, reflectionMode))
    {
        return float4(0.0, 0.0, 0.0, 1.0);
    }

    const float3 normal = RAB_SafeNormalize(RAB_GetSurfaceNormal(surface), RAB_GetSurfaceGeoNormal(surface));
    const float3 viewDir = RAB_SafeNormalize(RAB_GetSurfaceViewDir(surface), normal);
    const float3 reflectionDir = RAB_SafeNormalize(reflect(-viewDir, normal), normal);
    if (dot(reflectionDir, normal) <= 0.0 || dot(reflectionDir, RAB_GetSurfaceGeoNormal(surface)) <= -0.05)
    {
        return float4(0.0, 0.0, 0.0, 1.0);
    }

    const float roughness = saturate(RAB_GetSurfaceRoughness(surface));
    const float3 reflectionWeight = saturate(surface.material.specularF0) * (1.0 - saturate(roughness * 0.85));
    if (max(max(reflectionWeight.r, reflectionWeight.g), reflectionWeight.b) <= 0.0)
    {
        return float4(0.0, 0.0, 0.0, 1.0);
    }

    PathTraceSmokePayload reflectionPayload = InitSmokePayload();
    RayDesc reflectionRay;
    reflectionRay.Origin = surface.worldPos + normal * 0.75 + reflectionDir * 0.25;
    reflectionRay.Direction = reflectionDir;
    reflectionRay.TMin = 0.01;
    reflectionRay.TMax = min(CameraOriginAndTMax.w, max(ToyPathInfo.x, 64.0));
    TraceRay(SmokeScene, RAY_FLAG_NONE, 0xff, 0, 1, 0, reflectionRay, reflectionPayload);
    if (reflectionPayload.value == 0u || SmokePayloadIsGuiScreen(reflectionPayload))
    {
        return float4(0.0, 0.0, 0.0, 1.0);
    }

    const RAB_Surface hitSurface = RAB_BuildSurfaceFromSmokePayload(reflectionPayload, reflectionRay.Origin, reflectionRay.Direction, true);
    if (!RAB_IsSurfaceValid(hitSurface))
    {
        return float4(0.0, 0.0, 0.0, 1.0);
    }

    const uint bounceSeed =
        pixel.x * 1973u ^
        pixel.y * 9277u ^
        surface.materialId * 26699u ^
        hitSurface.materialId * 7919u ^
        ((uint)max(ToyPathInfo.w, 0.0)) * 104729u;
    const float3 direct = EvaluateSmokeMode18NeeDirectLighting(
        reflectionPayload,
        reflectionRay.Origin,
        reflectionRay.Direction,
        true,
        SmokeToyFakePBRSpecularEnabled(),
        true,
        bounceSeed ^ 0x7feb352du,
        PathTraceIntegratorSecondaryNeeMode(),
        PathTraceIntegratorSecondaryAnalyticNeeMode());
    const float3 hitPreview = RestirPTGiFallback(hitSurface) + RestirPTGiToneMapPreview(direct + hitSurface.material.emissiveRadiance);
    return float4(saturate(hitPreview * reflectionWeight), 1.0);
}
#endif

uint FindSmokeLightCandidateForTriangle(PathTraceSmokeEmissiveTriangle emissiveTriangle, uint candidateCount)
{
    const uint searchCount = min(candidateCount, 64u);
    [loop]
    for (uint candidateIndex = 0u; candidateIndex < searchCount; ++candidateIndex)
    {
        const PathTraceSmokeLightCandidate candidate = SmokeLightCandidates[candidateIndex];
        if (candidate.materialId == emissiveTriangle.materialId &&
            candidate.universeMaterialIndex == emissiveTriangle.universeMaterialIndex)
        {
            return candidateIndex;
        }
    }
    return 0xffffffffu;
}

void StoreSmokeReservoirCurrentSafe(uint reservoirIndex, PathTraceSmokeReservoir reservoir)
{
    if (!PathTraceSafetyDisabled(RT_PT_SAFETY_DISABLE_RESERVOIR_WRITES) &&
        reservoirIndex < PathTraceSmokeReservoirCount())
    {
        SmokeReservoirCurrent[reservoirIndex] = reservoir;
    }
}

float EvaluateSmokeReservoirCandidatePotential(PathTraceSmokeEmissiveTriangle emissiveTriangle, float3 hitPosition, float3 normal, float randomFallbackPdf, out float3 lightDir, out float distance, out float area, out float candidatePdf, out float lightFacing)
{
    const float3 lightCenter = emissiveTriangle.centerAndArea.xyz;
    area = max(emissiveTriangle.centerAndArea.w, 1.0e-4);
    const float3 lightNormal = SafeNormalize(emissiveTriangle.normalAndLuminance.xyz, float3(0.0, 0.0, 1.0));
    const float3 toLight = lightCenter - hitPosition;
    const float distanceSquared = max(dot(toLight, toLight), 1.0);
    distance = sqrt(distanceSquared);
    lightDir = toLight / distance;
    const float ndotl = saturate(dot(normal, lightDir));
    const bool historicalDynamicEmissive = (emissiveTriangle.padding0 & RT_SMOKE_EMISSIVE_TRIANGLE_HISTORY_DYNAMIC) != 0u;
    const bool twoSidedEmissive = !historicalDynamicEmissive && ((((uint)TextureInfo.w) & RT_SMOKE_TEXTURE_FLAG_RESERVOIR_TWO_SIDED_EMISSIVES) != 0u);
    const float lightFacingRaw = dot(lightNormal, -lightDir);
    lightFacing = twoSidedEmissive ? abs(lightFacingRaw) : saturate(lightFacingRaw);
    candidatePdf = max(emissiveTriangle.sampleWeightAndPdf.y, randomFallbackPdf);
    return area * ndotl * lightFacing / distanceSquared / max(candidatePdf, 1.0e-6);
}

float4 EvaluateSmokeReservoirDirectLighting(float3 rayOrigin, float3 rayDirection, PathTraceSmokePayload payload, uint2 pixel)
{
    const uint2 dimensions = PathTraceFullOutputSize();
    const uint reservoirIndex = pixel.y * dimensions.x + pixel.x;

    PathTraceSmokeReservoir reservoir = (PathTraceSmokeReservoir)0;
    reservoir.lightCandidateIndex = 0xffffffffu;
    reservoir.emissiveTriangleIndex = 0xffffffffu;

    if (SmokePayloadIsGuiScreen(payload))
    {
        StoreSmokeReservoirCurrentSafe(reservoirIndex, reservoir);
        return CompositeSmokeGuiLayers(rayOrigin, rayDirection, payload);
    }

    const PathTraceSmokeMaterial material = LoadSmokeMaterial(payload.materialIndex);
    const float3 albedo = SampleSmokeSurfaceAlbedo(material, payload.texCoord, payload.surfaceClass, payload.translucentSubtype, payload.vertexColor, payload.vertexColorAdd).rgb;
    const float3 baseNormal = SafeNormalize(payload.normal, payload.geometricNormal);
    const float3 normal = DecodeSmokeNormalTexture(material, payload.texCoord, baseNormal, payload.tangent, payload.bitangent);
    const float3 hitPosition = rayOrigin + rayDirection * payload.hitT;
    const float ambientScale = saturate(LightSpriteInfo.w);

    const uint seed =
        pixel.x * 1973u ^
        pixel.y * 9277u ^
        payload.materialId * 26699u ^
        ((uint)payload.hitT) * 7919u ^
        payload.materialIndex * 104729u;
    const uint emissiveTriangleCount = PathTraceSafetyDisabled(RT_PT_SAFETY_DISABLE_EMISSIVE_TRIANGLE_SAMPLING) ? 0u : (uint)max(EmissiveInfo.x, 0.0);
    const uint lightCandidateCount = PathTraceSafetyDisabled(RT_PT_SAFETY_DISABLE_EMISSIVE_TRIANGLE_SAMPLING) ? 0u : (uint)max(EmissiveInfo.w, 0.0);
    if (emissiveTriangleCount == 0u)
    {
        StoreSmokeReservoirCurrentSafe(reservoirIndex, reservoir);
        const float3 viewDir = SafeNormalize(rayOrigin - hitPosition, -rayDirection);
        const float3 analyticDirect = EvaluateDoomAnalyticSphereLights(albedo, float3(0.0, 0.0, 0.0), normal, viewDir, hitPosition, seed);
        const float3 debugAnalyticDirect = analyticDirect / (1.0 + analyticDirect);
        return float4(saturate(albedo * ambientScale + debugAnalyticDirect), 1.0);
    }

    const uint reservoirCandidateTrials = clamp((uint)max(EmissiveInfo.z, 1.0), 1u, 16u);
    uint emissiveTriangleIndex = 0u;
    float bestPotential = -1.0;
    [loop]
    for (uint trialIndex = 0u; trialIndex < reservoirCandidateTrials; ++trialIndex)
    {
        const float sampleXi = SmokeHashToUnitFloat(seed ^ (trialIndex + 1u) * 747796405u);
        const uint trialTriangleIndex = SelectSmokeWeightedEmissiveTriangle(emissiveTriangleCount, sampleXi);
        if (trialTriangleIndex >= emissiveTriangleCount)
        {
            continue;
        }
        const PathTraceSmokeEmissiveTriangle trialTriangle = SmokeEmissiveTriangles[trialTriangleIndex];
        float3 trialLightDir;
        float trialDistance;
        float trialArea;
        float trialPdf;
        float trialFacing;
        const float trialPotential = EvaluateSmokeReservoirCandidatePotential(trialTriangle, hitPosition, normal, 1.0 / max((float)emissiveTriangleCount, 1.0), trialLightDir, trialDistance, trialArea, trialPdf, trialFacing);
        if (trialPotential > bestPotential)
        {
            bestPotential = trialPotential;
            emissiveTriangleIndex = trialTriangleIndex;
        }
    }
    if (emissiveTriangleIndex >= emissiveTriangleCount)
    {
        StoreSmokeReservoirCurrentSafe(reservoirIndex, reservoir);
        return float4(saturate(albedo * (ambientScale * 0.04)), 1.0);
    }
    const PathTraceSmokeEmissiveTriangle emissiveTriangle = SmokeEmissiveTriangles[emissiveTriangleIndex];
    float3 lightDir;
    float distance;
    float area;
    float candidatePdf;
    float lightFacing;
    const float selectedPotential = EvaluateSmokeReservoirCandidatePotential(emissiveTriangle, hitPosition, normal, 1.0 / max((float)emissiveTriangleCount, 1.0), lightDir, distance, area, candidatePdf, lightFacing);
    const float distanceSquared = max(distance * distance, 1.0);
    const float ndotl = saturate(dot(normal, lightDir));
    const bool historicalDynamicEmissive = (emissiveTriangle.padding0 & RT_SMOKE_EMISSIVE_TRIANGLE_HISTORY_DYNAMIC) != 0u;
    float3 direct = float3(0.0, 0.0, 0.0);
    float visibility = 0.0;

    if (selectedPotential > 0.0 && ndotl > 0.0 && lightFacing > 0.0)
    {
        const float normalOffsetSign = dot(normal, lightDir) >= 0.0 ? 1.0 : -1.0;
        const float3 shadowOrigin = hitPosition + normal * (normalOffsetSign * 0.75) + lightDir * 0.25;
        const float shadowTMax = max(distance - 0.5, 0.01);
        const uint ignoreInstanceId = historicalDynamicEmissive ? 0xffffffffu : emissiveTriangle.instanceId;
        const uint ignorePrimitiveIndex = historicalDynamicEmissive ? 0xffffffffu : emissiveTriangle.primitiveIndex;
        visibility = TraceSmokeShadowVisibility(shadowOrigin, lightDir, shadowTMax, ignoreInstanceId, ignorePrimitiveIndex, emissiveTriangle.materialId);
        const float3 radiance = max(emissiveTriangle.estimatedRadianceAndLuminance.rgb, float3(0.0, 0.0, 0.0)) * max(ToyPathInfo.z, 0.0);
        const float sampleWeight = area * ndotl * lightFacing * visibility / distanceSquared / max(candidatePdf, 1.0e-6);
        direct = albedo * radiance * sampleWeight;

        reservoir.radianceAndTargetPdf = float4(direct, candidatePdf);
        reservoir.weightSumAndSampleCount = float4(sampleWeight, 1.0, area, distance);
        reservoir.lightCandidateIndex = FindSmokeLightCandidateForTriangle(emissiveTriangle, lightCandidateCount);
        reservoir.emissiveTriangleIndex = emissiveTriangleIndex;
        reservoir.flags = 1u;
    }

    StoreSmokeReservoirCurrentSafe(reservoirIndex, reservoir);
    const float3 viewDir = SafeNormalize(rayOrigin - hitPosition, -rayDirection);
    direct += EvaluateDoomAnalyticSphereLights(albedo, float3(0.0, 0.0, 0.0), normal, viewDir, hitPosition, seed);
    const float3 debugDirect = direct / (1.0 + direct);
    return float4(saturate(albedo * (ambientScale * 0.04) + debugDirect), 1.0);
}

static const uint RT_PT_TOY_FLAG_DIFFUSE_HIT = 0x00000001u;
static const uint RT_PT_TOY_FLAG_REFLECTION_HIT = 0x00000002u;
static const uint RT_PT_TOY_FLAG_REFLECTION_MISS = 0x00000004u;
static const uint RT_PT_TOY_FLAG_REFLECTION_GATED = 0x00000008u;
static const uint RT_PT_TOY_FLAG_MAX_DEPTH_TERMINATED = 0x00000010u;
static const uint RT_PT_TOY_FLAG_RR_CONFIGURED = 0x00000020u;

float4 EvaluateSmokeToyPathTrace(float3 rayOrigin, float3 rayDirection, PathTraceSmokePayload primaryPayload, uint2 pixel, uint sampleIndex, out uint pathDepth, out uint pathFlags)
{
    pathDepth = primaryPayload.value != 0u ? 1u : 0u;
    pathFlags = PathTraceIntegratorRussianRouletteDepth() > 0u ? RT_PT_TOY_FLAG_RR_CONFIGURED : 0u;

    if (SmokePayloadIsGuiScreen(primaryPayload))
    {
        return CompositeSmokeGuiLayers(rayOrigin, rayDirection, primaryPayload);
    }

    const PathTraceSmokeMaterial primaryMaterial = LoadSmokeMaterial(primaryPayload.materialIndex);
    const float3 primaryAlbedo = SampleSmokeSurfaceAlbedo(primaryMaterial, primaryPayload.texCoord, primaryPayload.surfaceClass, primaryPayload.translucentSubtype, primaryPayload.vertexColor, primaryPayload.vertexColorAdd).rgb;
    const float3 baseNormal = SafeNormalize(primaryPayload.normal, primaryPayload.geometricNormal);
    const float3 primaryNormal = DecodeSmokeNormalTexture(primaryMaterial, primaryPayload.texCoord, baseNormal, primaryPayload.tangent, primaryPayload.bitangent);
    const float3 primaryHit = rayOrigin + rayDirection * primaryPayload.hitT;
    const uint bounceSeed =
        pixel.x * 1973u ^
        pixel.y * 9277u ^
        primaryPayload.materialId * 26699u ^
        ((uint)primaryPayload.hitT) * 7919u ^
        sampleIndex * 374761393u ^
        ((uint)max(ToyPathInfo.w, 0.0)) * 104729u;
    const bool useFakePBRSpecular = SmokeToyFakePBRSpecularEnabled();
    const float3 nativePrimaryDirect = EvaluateSmokeMode18NeeDirectLighting(primaryPayload, rayOrigin, rayDirection, true, useFakePBRSpecular, true, bounceSeed, SMOKE_NEE_SELECTED_LIGHT_MODE_LEGACY_FULL, SMOKE_NEE_ANALYTIC_LIGHT_MODE_LEGACY_FULL);
    float3 radiance = nativePrimaryDirect;
#ifdef RB_PT_ENABLE_RESTIR_MODE18_DIRECT
    const uint restirMode18DebugView = RestirPTMode18DebugView();
    const bool restirMode18HeavyDirect = RestirPTMode18HeavyDirectEnabled();
    float3 restirPrimaryDirect = float3(0.0, 0.0, 0.0);
    bool restirPrimaryDirectValid = false;
    if (sampleIndex == 0u)
    {
        const RAB_Surface primarySurface = RAB_BuildSurfaceFromSmokePayload(primaryPayload, rayOrigin, rayDirection, true);
        restirPrimaryDirectValid = RestirPTTryEvaluateSpatialDirectLighting(primarySurface, pixel, RestirPTInfo.z >= 0.5, restirPrimaryDirect);
        if (restirMode18DebugView != 0u)
        {
            if (restirMode18DebugView == 1u)
            {
                return float4(saturate(RestirPTMode18DebugToneMap(nativePrimaryDirect)), 1.0);
            }
            if (restirMode18DebugView == 2u)
            {
                return float4(saturate(RestirPTMode18DebugToneMap(restirPrimaryDirect)), 1.0);
            }
            if (restirMode18DebugView == 3u)
            {
                const float3 delta = abs(restirPrimaryDirect - nativePrimaryDirect);
                return float4(saturate(RestirPTMode18DebugToneMap(delta)), 1.0);
            }
            if (restirMode18DebugView == 4u)
            {
                const float nativeLum = max(RAB_Luminance(nativePrimaryDirect), 1.0e-5);
                const float restirLum = max(RAB_Luminance(restirPrimaryDirect), 0.0);
                const float logRatio = clamp(log2((restirLum + 1.0e-5) / nativeLum), -2.0, 2.0);
                const float ratioT = logRatio * 0.25 + 0.5;
                return float4(saturate(float3(ratioT, 1.0 - abs(ratioT - 0.5) * 2.0, 1.0 - ratioT)), 1.0);
            }
        }
        if (restirPrimaryDirectValid)
        {
            radiance = EvaluateSmokeMode18NeeDirectLighting(
                primaryPayload,
                rayOrigin,
                rayDirection,
                true,
                useFakePBRSpecular,
                true,
                bounceSeed,
                SMOKE_NEE_SELECTED_LIGHT_MODE_OFF,
                SMOKE_NEE_ANALYTIC_LIGHT_MODE_OFF) + restirPrimaryDirect;
        }
    }
#endif

    const bool allowSecondary = PathTraceIntegratorMaxPathDepth() > 1u;
    if (allowSecondary && PathTraceIntegratorDiffuseBounceLimit() > 0u)
    {
        const float3 bounceDir = SmokeCosineHemisphereDirection(primaryNormal, bounceSeed);
        PathTraceSmokePayload bouncePayload = InitSmokePayload();
        RayDesc bounceRay;
        bounceRay.Origin = primaryHit + primaryNormal * 0.75 + bounceDir * 0.25;
        bounceRay.Direction = bounceDir;
        bounceRay.TMin = 0.01;
        bounceRay.TMax = min(CameraOriginAndTMax.w, max(ToyPathInfo.x, 64.0));
        TraceRay(SmokeScene, RAY_FLAG_NONE, 0xff, 0, 1, 0, bounceRay, bouncePayload);

        if (bouncePayload.value != 0u && !SmokePayloadIsGuiScreen(bouncePayload))
        {
            pathDepth = max(pathDepth, 2u);
            pathFlags |= RT_PT_TOY_FLAG_DIFFUSE_HIT;
            const PathTraceSmokeMaterial bounceMaterial = LoadSmokeMaterial(bouncePayload.materialIndex);
            const float3 bounceAlbedo = SampleSmokeSurfaceAlbedo(bounceMaterial, bouncePayload.texCoord, bouncePayload.surfaceClass, bouncePayload.translucentSubtype, bouncePayload.vertexColor, bouncePayload.vertexColorAdd).rgb;
            const float3 bounceDirect = EvaluateSmokeMode18NeeDirectLighting(
                bouncePayload,
                bounceRay.Origin,
                bounceRay.Direction,
                true,
                useFakePBRSpecular,
                true,
                bounceSeed ^ 0x9e3779b9u,
#ifdef RB_PT_ENABLE_RESTIR_MODE18_DIRECT
                restirMode18HeavyDirect ? SMOKE_NEE_SELECTED_LIGHT_MODE_OFF : PathTraceIntegratorSecondaryNeeMode(),
                restirMode18HeavyDirect ? SMOKE_NEE_ANALYTIC_LIGHT_MODE_OFF : PathTraceIntegratorSecondaryAnalyticNeeMode()
#else
                PathTraceIntegratorSecondaryNeeMode(),
                PathTraceIntegratorSecondaryAnalyticNeeMode()
#endif
            );
            radiance += primaryAlbedo * bounceDirect * (0.28 + 0.22 * max(max(bounceAlbedo.r, bounceAlbedo.g), bounceAlbedo.b));
        }
    }
    else if (PathTraceIntegratorDiffuseBounceLimit() > 0u)
    {
        pathFlags |= RT_PT_TOY_FLAG_MAX_DEPTH_TERMINATED;
    }

    float3 reflectionDir;
    float3 reflectionWeight;
    float reflectionRoughness;
    if (BuildSmokeReflectionBounce(primaryMaterial, primaryPayload, primaryNormal, rayDirection, reflectionDir, reflectionWeight, reflectionRoughness))
    {
        PathTraceSmokePayload reflectionPayload = InitSmokePayload();
        RayDesc reflectionRay;
        reflectionRay.Origin = primaryHit + primaryNormal * 0.75 + reflectionDir * 0.25;
        reflectionRay.Direction = reflectionDir;
        reflectionRay.TMin = 0.01;
        reflectionRay.TMax = min(CameraOriginAndTMax.w, max(ToyPathInfo.x, 64.0));
        TraceRay(SmokeScene, RAY_FLAG_NONE, 0xff, 0, 1, 0, reflectionRay, reflectionPayload);

        if (reflectionPayload.value != 0u && !SmokePayloadIsGuiScreen(reflectionPayload))
        {
            pathDepth = max(pathDepth, 2u);
            pathFlags |= RT_PT_TOY_FLAG_REFLECTION_HIT;
            const float3 reflectionDirect = EvaluateSmokeMode18NeeDirectLighting(
                reflectionPayload,
                reflectionRay.Origin,
                reflectionRay.Direction,
                true,
                useFakePBRSpecular,
                true,
                bounceSeed ^ 0x7feb352du,
#ifdef RB_PT_ENABLE_RESTIR_MODE18_DIRECT
                restirMode18HeavyDirect ? SMOKE_NEE_SELECTED_LIGHT_MODE_OFF : PathTraceIntegratorSecondaryNeeMode(),
                restirMode18HeavyDirect ? SMOKE_NEE_ANALYTIC_LIGHT_MODE_OFF : PathTraceIntegratorSecondaryAnalyticNeeMode()
#else
                PathTraceIntegratorSecondaryNeeMode(),
                PathTraceIntegratorSecondaryAnalyticNeeMode()
#endif
            );
            radiance += reflectionDirect * reflectionWeight;
        }
        else
        {
            pathFlags |= RT_PT_TOY_FLAG_REFLECTION_MISS;
        }
    }
    else if (PathTraceIntegratorReflectionMode() > 0u && PathTraceIntegratorSpecularBounceLimit() > 0u)
    {
        pathFlags |= allowSecondary ? RT_PT_TOY_FLAG_REFLECTION_GATED : RT_PT_TOY_FLAG_MAX_DEPTH_TERMINATED;
    }

    radiance += EvaluateSmokeLightSpriteProxies(rayOrigin, rayDirection, primaryPayload.hitT) * 0.35;
    return float4(saturate(radiance), 1.0);
}

bool SmokePayloadIsGuiScreen(PathTraceSmokePayload payload)
{
    return payload.value != 0u &&
        payload.surfaceClass == RT_SMOKE_SURFACE_CLASS_TRANSLUCENT &&
        payload.translucentSubtype == RT_SMOKE_TRANSLUCENT_SUBTYPE_GUI_SCREEN;
}

float4 CompositeSmokeGuiLayers(float3 rayOrigin, float3 rayDirection, PathTraceSmokePayload firstPayload)
{
    float3 color = float3(0.0, 0.0, 0.0);
    float transmittance = 1.0;
    float lastHitT = 0.0;

    PathTraceSmokePayload layerPayload = firstPayload;
    [loop]
    for (uint layer = 0u; layer < 12u; layer++)
    {
        if (!SmokePayloadIsGuiScreen(layerPayload))
        {
            break;
        }

        if (layer > 0u && abs(layerPayload.hitT - firstPayload.hitT) > 8.0)
        {
            break;
        }

        const PathTraceSmokeMaterial material = LoadSmokeMaterial(layerPayload.materialIndex);
        const float4 albedo = SampleSmokeSurfaceAlbedo(
            material,
            layerPayload.texCoord,
            layerPayload.surfaceClass,
            layerPayload.translucentSubtype,
            layerPayload.vertexColor,
            layerPayload.vertexColorAdd);
        const float alpha = saturate(albedo.a);
        color += albedo.rgb * alpha * transmittance;
        transmittance *= 1.0 - alpha;
        lastHitT = layerPayload.hitT;

        if (transmittance <= 0.02)
        {
            break;
        }

        PathTraceSmokePayload nextPayload = InitSmokePayload();
        RayDesc ray;
        ray.Origin = rayOrigin;
        ray.Direction = rayDirection;
        ray.TMin = lastHitT + 0.002;
        ray.TMax = min(CameraOriginAndTMax.w, firstPayload.hitT + 8.0);
        TraceRay(SmokeScene, RAY_FLAG_NONE, 0xff, 0, 1, 0, ray, nextPayload);
        layerPayload = nextPayload;
    }

    color += float3(0.015, 0.012, 0.008) * transmittance;
    return float4(saturate(color), 1.0);
}

[shader("raygeneration")]
void RayGen()
{
    const uint2 localPixel = DispatchRaysIndex().xy;
    uint2 pixel = localPixel + PathTraceDispatchTileOffset();
    const bool restirDirectDispatch = PathTraceRestirDirectDispatchActive();
    const bool restirDirectSparseProducerDispatch = PathTraceRestirDirectSparseProducerDispatch();
    const bool primarySurfaceProducerDispatch = PathTraceRestirPTPrimarySurfaceProducerDispatch();
    const bool consumePrimarySurfaceHistory = PathTraceRestirPTConsumePrimarySurfaceHistory();
    const bool restirIndirectProducerDispatch = PathTraceRestirPTIndirectProducerDispatch();
    const bool restirIndirectSparseProducerDispatch = PathTraceRestirPTIndirectSparseProducerDispatch();
    const uint2 dimensions = restirDirectSparseProducerDispatch
        ? PathTraceRestirSparseDispatchSize()
        : (restirIndirectSparseProducerDispatch
            ? PathTraceRestirPTIndirectSparseDispatchSize()
            : (restirDirectDispatch ? PathTraceRestirDirectSize() : PathTraceFullOutputSize()));
    const uint2 fullDimensions = PathTraceFullOutputSize();
    if (pixel.x >= dimensions.x || pixel.y >= dimensions.y)
    {
        return;
    }
    if (restirDirectSparseProducerDispatch)
    {
        pixel = PathTraceRestirSparseProducerPixelToDirectPixel(pixel);
        if (pixel.x >= PathTraceRestirDirectSize().x || pixel.y >= PathTraceRestirDirectSize().y)
        {
            return;
        }
    }
    if (restirIndirectSparseProducerDispatch)
    {
        pixel = PathTraceRestirPTIndirectSparseProducerPixelToFullPixel(pixel);
        if (pixel.x >= fullDimensions.x || pixel.y >= fullDimensions.y)
        {
            return;
        }
    }
    const uint2 surfacePixel = restirDirectDispatch ? PathTraceRestirDirectPixelToRepresentativeFullPixel(pixel) : pixel;
    const float2 uv = (float2(surfacePixel) + 0.5) / float2(fullDimensions);
    const float2 ndc = uv * 2.0 - 1.0;

    PathTraceSmokePayload payload = InitSmokePayload();

    RayDesc ray;
    ray.Origin = CameraOriginAndTMax.xyz;
    ray.Direction = normalize(
        CameraForwardAndTanX.xyz +
        CameraLeftAndTanY.xyz * (-ndc.x * CameraForwardAndTanX.w) +
        CameraUpAndDebugMode.xyz * (-ndc.y * CameraLeftAndTanY.w));
    ray.TMin = 0.1;
    ray.TMax = CameraOriginAndTMax.w;

#ifdef RB_PT_FORCE_DEBUG_MODE
    const uint debugMode = RB_PT_FORCE_DEBUG_MODE;
#else
    const uint debugMode = (uint)CameraUpAndDebugMode.w;
#endif
    if (restirDirectDispatch && !restirDirectSparseProducerDispatch && !PathTraceRestirSparseActivePixel(pixel, false))
    {
        return;
    }
    if (restirIndirectProducerDispatch && !restirIndirectSparseProducerDispatch && !PathTraceRestirPTIndirectSparseActivePixel(pixel))
    {
        return;
    }
    if (debugMode == 21u)
    {
        SmokeOutput[pixel] = RenderSmokeBoundsBoxes(ray.Origin, ray.Direction);
        return;
    }
    if (debugMode == 22u)
    {
        SmokeOutput[pixel] = RenderSmokeBoundsWireframeBoxes(ray.Origin, ray.Direction);
        return;
    }

    if (debugMode == 24u)
    {
        PathTraceSmokePayload fallbackPayload = InitSmokePayload();
        PathTraceSmokePayload rigidPayload = InitSmokePayload();
        TraceRay(SmokeScene, RAY_FLAG_NONE, 0x01u, 0, 1, 0, ray, fallbackPayload);
        TraceRay(SmokeScene, RAY_FLAG_NONE, 0x02u, 0, 1, 0, ray, rigidPayload);

        if (rigidPayload.value != 0u)
        {
            if (fallbackPayload.value != 0u)
            {
                const float distanceDelta = abs(rigidPayload.hitT - fallbackPayload.hitT);
                if (distanceDelta <= 1.5)
                {
                    const bool surfaceClassMatches = rigidPayload.surfaceClass == fallbackPayload.surfaceClass;
                    const bool materialMatches =
                        rigidPayload.materialId == fallbackPayload.materialId &&
                        rigidPayload.materialIndex == fallbackPayload.materialIndex;
                    if (!surfaceClassMatches)
                    {
                        SmokeOutput[pixel] = float4(1.0, 0.0, 1.0, 1.0);
                        return;
                    }
                    if (!materialMatches)
                    {
                        SmokeOutput[pixel] = float4(1.0, 1.0, 0.0, 1.0);
                        return;
                    }
                    SmokeOutput[pixel] = float4(0.0, 1.0, 0.0, 1.0);
                    return;
                }
                if (rigidPayload.hitT < fallbackPayload.hitT)
                {
                    SmokeOutput[pixel] = float4(0.0, 0.18, 1.0, 1.0);
                    return;
                }

                SmokeOutput[pixel] = float4(1.0, 0.45, 0.0, 1.0);
                return;
            }

            SmokeOutput[pixel] = float4(0.0, 1.0, 1.0, 1.0);
            return;
        }

        if (fallbackPayload.value != 0u)
        {
            SmokeOutput[pixel] = float4(0.18, 0.18, 0.18, 1.0);
            return;
        }

        SmokeOutput[pixel] = float4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    const uint traceMask = debugMode == 23u ? 0x02u : 0xffu;

    RAB_Surface primaryHistorySurface = RAB_EmptySurface();
    if (consumePrimarySurfaceHistory)
    {
        primaryHistorySurface = RAB_GetGBufferSurface(int2(pixel), false);
    }
    else
    {
        TraceRay(SmokeScene, RAY_FLAG_NONE, traceMask, 0, 1, 0, ray, payload);
        if (payload.value != 0u && !SmokePayloadIsGuiScreen(payload))
        {
            primaryHistorySurface = RAB_BuildSurfaceFromSmokePayload(payload, ray.Origin, ray.Direction, true);
        }
    }
#ifndef RB_PT_RESTIR_SPATIAL_PRODUCER
    if (!consumePrimarySurfaceHistory)
    {
        StoreRestirPTPrimarySurfaceHistory(pixel, primaryHistorySurface);
        if (!restirDirectDispatch)
        {
            StorePathTraceMotionVectorExport(pixel, primaryHistorySurface);
        }
        if (primarySurfaceProducerDispatch)
        {
            return;
        }
    }
#endif

#ifdef RB_PT_RESTIR_REFLECTION_PRODUCER_ONLY
    RestirPTReflectionOutput[pixel] = EvaluateRestirPTTracedReflectionFromSurface(primaryHistorySurface, pixel);
    return;
#endif

#ifdef RB_PT_RESTIR_INDIRECT_INITIAL_PRODUCER_ONLY
    ProduceRestirPTIndirectInitialReservoirFromSurface(primaryHistorySurface, pixel);
    return;
#endif

    if (restirIndirectProducerDispatch)
    {
        if (consumePrimarySurfaceHistory)
        {
            ProduceRestirPTIndirectInitialReservoirFromSurface(primaryHistorySurface, pixel);
        }
        else
        {
            ProduceRestirPTIndirectInitialReservoir(ray.Origin, ray.Direction, payload, pixel);
        }
        return;
    }

#if defined(RB_PT_ENABLE_RESTIR_TEMPORAL) && !defined(RB_PT_RESTIR_SPATIAL_PRODUCER)
#ifdef RB_PT_RESTIR_DIRECT_TEMPORAL_PRODUCER_ONLY
    {
        float4 temporalRejectionColor;
        bool temporalSelectedPrevSample;
        GenerateRestirPTTemporalReservoir(primaryHistorySurface, pixel, temporalRejectionColor, temporalSelectedPrevSample);
        return;
    }
#endif
    if (restirDirectDispatch && debugMode == 31u)
    {
        float4 temporalRejectionColor;
        bool temporalSelectedPrevSample;
        GenerateRestirPTTemporalReservoir(primaryHistorySurface, pixel, temporalRejectionColor, temporalSelectedPrevSample);
        return;
    }
#endif

#ifdef RB_PT_RESTIR_SPATIAL_PRODUCER
    float4 spatialRejectionColor;
    bool spatialSelectedPrevSample;
    bool spatialResampled;
    GenerateRestirPTSpatialReservoir(primaryHistorySurface, pixel, spatialRejectionColor, spatialSelectedPrevSample, spatialResampled);
    return;
#endif

    if (payload.value == 0)
    {
        if (debugMode == 14)
        {
            SmokeOutput[pixel] = float4(saturate(EvaluateSmokeLightSpriteProxies(ray.Origin, ray.Direction, ray.TMax)), 1.0);
        }
        else if (debugMode == 18 || debugMode == 19 || debugMode == 20 || debugMode == 25 || debugMode == 38 || debugMode == 39 || debugMode == 40 || debugMode == 41 || debugMode == 42 || debugMode == 43 || debugMode == 44 || debugMode == 45 || debugMode == 46 || debugMode == 47 || debugMode == 48 || debugMode == 49 || debugMode == 52 || (debugMode >= 34 && debugMode <= 37))
        {
            SmokeOutput[pixel] = float4(0.0, 0.0, 0.0, 1.0);
        }
        else if (debugMode == 26)
        {
            SmokeOutput[pixel] = EvaluateRestirPTInitialReservoirDebug(ray.Origin, ray.Direction, payload, pixel);
        }
        else if (debugMode == 27)
        {
            SmokeOutput[pixel] = EvaluateRestirPTInitialReservoirShading(ray.Origin, ray.Direction, payload, pixel, false);
        }
        else if (debugMode == 28)
        {
            SmokeOutput[pixel] = EvaluateRestirPTInitialReservoirShading(ray.Origin, ray.Direction, payload, pixel, true);
        }
        else if (debugMode == 29)
        {
            SmokeOutput[pixel] = EvaluateRestirPTPrimarySurfaceHistoryDebug(primaryHistorySurface, pixel);
        }
        else if (debugMode == 30)
        {
            SmokeOutput[pixel] = EvaluateRestirPTPrimarySurfaceReprojectionDebug(primaryHistorySurface);
        }
        else if (debugMode == 31)
        {
            SmokeOutput[pixel] = EvaluateRestirPTTemporalReservoirDebug(primaryHistorySurface, pixel);
        }
        else if (debugMode == 32)
        {
            SmokeOutput[pixel] = EvaluateRestirPTTemporalReservoirShading(primaryHistorySurface, pixel, RestirPTInfo.z >= 0.5);
        }
        else if (debugMode == 33)
        {
            SmokeOutput[pixel] = EvaluateRestirPTTemporalLightSourceAttribution(primaryHistorySurface, pixel);
        }
        else if (debugMode == 50)
        {
            SmokeOutput[pixel] = EvaluateRestirPTSpatialReservoirShading(primaryHistorySurface, pixel, RestirPTInfo.z >= 0.5);
        }
        else if (debugMode == 51)
        {
            SmokeOutput[pixel] = EvaluateRestirPTSpatialLightSourceAttribution(primaryHistorySurface, pixel);
        }
        else if (debugMode == 53)
        {
            SmokeOutput[pixel] = EvaluateRestirPTIndirectReservoirDebug(ray.Origin, ray.Direction, payload, pixel);
        }
        else if (debugMode == 54)
        {
            SmokeOutput[pixel] = EvaluateRestirPTIndirectReservoirShading(ray.Origin, ray.Direction, payload, pixel);
        }
        else if (debugMode == 55)
        {
            SmokeOutput[pixel] = EvaluateRestirPTIndirectPathAttribution(ray.Origin, ray.Direction, payload, pixel);
        }
        else if (debugMode == 56)
        {
            SmokeOutput[pixel] = consumePrimarySurfaceHistory
                ? EvaluateRestirPTCombinedDirectGiPreviewFromSurface(primaryHistorySurface, pixel, RestirPTInfo.z >= 0.5)
                : EvaluateRestirPTCombinedDirectGiPreview(ray.Origin, ray.Direction, payload, pixel, RestirPTInfo.z >= 0.5);
        }
        else
        {
            SmokeOutput[pixel] = SmokeMissColor();
        }
    }
    else if (debugMode == 1)
    {
        const float normalizedDepth = saturate(payload.hitT / 512.0);
        SmokeOutput[pixel] = float4(normalizedDepth, normalizedDepth, normalizedDepth, 1.0);
    }
    else if (debugMode == 2)
    {
        SmokeOutput[pixel] = float4(payload.normal * 0.5 + 0.5, 1.0);
    }
    else if (debugMode == 3)
    {
        if (payload.surfaceClass == 0)
        {
            SmokeOutput[pixel] = float4(0.0, 1.0, 0.0, 1.0);
        }
        else if (payload.surfaceClass == 1)
        {
            SmokeOutput[pixel] = float4(0.0, 0.35, 1.0, 1.0);
        }
        else if (payload.surfaceClass == 2)
        {
            SmokeOutput[pixel] = float4(1.0, 0.0, 1.0, 1.0);
        }
        else if (payload.surfaceClass == 3)
        {
            SmokeOutput[pixel] = float4(1.0, 0.75, 0.0, 1.0);
        }
        else
        {
            SmokeOutput[pixel] = float4(1.0, 1.0, 1.0, 1.0);
        }
    }
    else if (debugMode == 4)
    {
        const float2 uv = frac(abs(payload.texCoord));
        SmokeOutput[pixel] = float4(0.15 + uv.x * 0.85, 0.15 + uv.y * 0.85, 0.25, 1.0);
    }
    else if (debugMode == 5)
    {
        SmokeOutput[pixel] = float4(payload.geometricNormal * 0.5 + 0.5, 1.0);
    }
    else if (debugMode == 6)
    {
        SmokeOutput[pixel] = float4(MaterialIdToColor(payload.materialId), 1.0);
    }
    else if (debugMode == 7)
    {
        SmokeOutput[pixel] = LoadSmokeMaterial(payload.materialIndex).debugAlbedo;
    }
    else if (debugMode == 23)
    {
        const PathTraceSmokeMaterial material = LoadSmokeMaterial(payload.materialIndex);
        const float depthFade = 1.0 - saturate(payload.hitT / 1024.0);
        const float3 normalShade = payload.normal * 0.5 + 0.5;
        const float3 materialColor = max(material.debugAlbedo.rgb, float3(0.08, 0.08, 0.08));
        SmokeOutput[pixel] = float4(saturate(materialColor * (0.35 + depthFade * 0.65) * (0.45 + normalShade * 0.55)), 1.0);
    }
    else if (debugMode == 25)
    {
        const PathTraceSmokeMaterial material = LoadSmokeMaterial(payload.materialIndex);
        const float3 albedo = max(material.debugAlbedo.rgb, float3(0.06, 0.06, 0.06));
        const float3 normal = SafeNormalize(payload.normal, payload.geometricNormal);
        const float3 lightDir = normalize(float3(0.35, 0.45, 0.82));
        const float ndotl = saturate(dot(normal, lightDir));
        const float viewFacing = saturate(dot(normal, -ray.Direction));
        float3 color = albedo * (0.16 + ndotl * 0.95 + viewFacing * 0.12);
        if (payload.instanceId >= 2u)
        {
            color = saturate(color + float3(0.0, 0.08, 0.10));
        }
        SmokeOutput[pixel] = float4(saturate(color), 1.0);
    }
    else if (debugMode == 26)
    {
        SmokeOutput[pixel] = EvaluateRestirPTInitialReservoirDebug(ray.Origin, ray.Direction, payload, pixel);
    }
    else if (debugMode == 27)
    {
        SmokeOutput[pixel] = EvaluateRestirPTInitialReservoirShading(ray.Origin, ray.Direction, payload, pixel, false);
    }
    else if (debugMode == 28)
    {
        SmokeOutput[pixel] = EvaluateRestirPTInitialReservoirShading(ray.Origin, ray.Direction, payload, pixel, true);
    }
    else if (debugMode == 29)
    {
        SmokeOutput[pixel] = EvaluateRestirPTPrimarySurfaceHistoryDebug(primaryHistorySurface, pixel);
    }
    else if (debugMode == 30)
    {
        SmokeOutput[pixel] = EvaluateRestirPTPrimarySurfaceReprojectionDebug(primaryHistorySurface);
    }
    else if (debugMode == 31)
    {
        SmokeOutput[pixel] = EvaluateRestirPTTemporalReservoirDebug(primaryHistorySurface, pixel);
    }
    else if (debugMode == 32)
    {
        SmokeOutput[pixel] = EvaluateRestirPTTemporalReservoirShading(primaryHistorySurface, pixel, RestirPTInfo.z >= 0.5);
    }
    else if (debugMode == 33)
    {
        SmokeOutput[pixel] = EvaluateRestirPTTemporalLightSourceAttribution(primaryHistorySurface, pixel);
    }
    else if (debugMode == 50)
    {
        SmokeOutput[pixel] = EvaluateRestirPTSpatialReservoirShading(primaryHistorySurface, pixel, RestirPTInfo.z >= 0.5);
    }
    else if (debugMode == 51)
    {
        SmokeOutput[pixel] = EvaluateRestirPTSpatialLightSourceAttribution(primaryHistorySurface, pixel);
    }
    else if (debugMode == 53)
    {
        SmokeOutput[pixel] = EvaluateRestirPTIndirectReservoirDebug(ray.Origin, ray.Direction, payload, pixel);
    }
    else if (debugMode == 54)
    {
        SmokeOutput[pixel] = EvaluateRestirPTIndirectReservoirShading(ray.Origin, ray.Direction, payload, pixel);
    }
    else if (debugMode == 55)
    {
        SmokeOutput[pixel] = EvaluateRestirPTIndirectPathAttribution(ray.Origin, ray.Direction, payload, pixel);
    }
    else if (debugMode == 56)
    {
        SmokeOutput[pixel] = consumePrimarySurfaceHistory
            ? EvaluateRestirPTCombinedDirectGiPreviewFromSurface(primaryHistorySurface, pixel, RestirPTInfo.z >= 0.5)
            : EvaluateRestirPTCombinedDirectGiPreview(ray.Origin, ray.Direction, payload, pixel, RestirPTInfo.z >= 0.5);
    }
    else if (debugMode == 38)
    {
        SmokeOutput[pixel] = EvaluatePathTracePrimarySurfaceObjectMotionDebug(primaryHistorySurface, pixel);
    }
    else if (debugMode == 39)
    {
        SmokeOutput[pixel] = EvaluatePathTracePrimarySurfaceRigidEligibilityDebug(primaryHistorySurface);
    }
    else if (debugMode == 40)
    {
        SmokeOutput[pixel] = EvaluatePathTracePrimarySurfaceRigidObjectMotionDebug(primaryHistorySurface, pixel);
    }
    else if (debugMode == 41)
    {
        SmokeOutput[pixel] = EvaluatePathTracePrimarySurfaceCombinedObjectMotionDebug(primaryHistorySurface, pixel);
    }
    else if (debugMode == 42)
    {
        SmokeOutput[pixel] = EvaluatePathTracePrimarySurfacePackedObjectMotionDebug(primaryHistorySurface);
    }
    else if (debugMode == 43)
    {
        SmokeOutput[pixel] = EvaluatePathTracePrimarySurfaceObjectMotionReprojectionDebug(primaryHistorySurface, pixel);
    }
    else if (debugMode == 44)
    {
        SmokeOutput[pixel] = EvaluatePathTracePreviousStaticSnapshotDebug(primaryHistorySurface);
    }
    else if (debugMode == 45)
    {
        SmokeOutput[pixel] = EvaluatePathTracePreviousStaticSnapshotReprojectionDebug(primaryHistorySurface);
    }
    else if (debugMode == 46)
    {
        SmokeOutput[pixel] = EvaluatePathTracePreviousStaticSnapshotMotionVectorDebug(primaryHistorySurface, pixel);
    }
    else if (debugMode == 47)
    {
        SmokeOutput[pixel] = EvaluatePathTraceCombinedGeometryMotionVectorDebug(primaryHistorySurface, pixel);
    }
    else if (debugMode == 48)
    {
        SmokeOutput[pixel] = EvaluatePathTraceCombinedGeometryReprojectionDebug(primaryHistorySurface, pixel);
    }
    else if (debugMode == 49)
    {
        SmokeOutput[pixel] = EvaluatePathTraceCombinedGeometryMotionSourceDebug(primaryHistorySurface, pixel);
    }
    else if (debugMode == 52)
    {
        SmokeOutput[pixel] = EvaluatePathTraceRigidRouteTransformParityDebug(payload);
    }
    else if (debugMode == 8)
    {
        if (SmokePayloadIsGuiScreen(payload))
        {
            SmokeOutput[pixel] = CompositeSmokeGuiLayers(ray.Origin, ray.Direction, payload);
            return;
        }
        const PathTraceSmokeMaterial material = LoadSmokeMaterial(payload.materialIndex);
        SmokeOutput[pixel] = SampleSmokeSurfaceAlbedo(material, payload.texCoord, payload.surfaceClass, payload.translucentSubtype, payload.vertexColor, payload.vertexColorAdd);
    }
    else if (debugMode == 9)
    {
        const PathTraceSmokeMaterial material = LoadSmokeMaterial(payload.materialIndex);
        const bool alphaTested = (material.flags & RT_SMOKE_MATERIAL_ALPHA_TEST) != 0u;
        const float coverage = SmokeAlphaCoverage(material, payload.texCoord);
        if (!alphaTested)
        {
            SmokeOutput[pixel] = float4(0.15, 0.15, 0.15, 1.0);
        }
        else if (coverage < material.alphaCutoff)
        {
            SmokeOutput[pixel] = SmokeMissColor();
        }
        else
        {
            SmokeOutput[pixel] = float4(0.0, 1.0, 0.25, 1.0);
        }
    }
    else if (debugMode == 10)
    {
        if (SmokePayloadIsGuiScreen(payload))
        {
            SmokeOutput[pixel] = CompositeSmokeGuiLayers(ray.Origin, ray.Direction, payload);
            return;
        }
        const PathTraceSmokeMaterial material = LoadSmokeMaterial(payload.materialIndex);
        SmokeOutput[pixel] = SampleSmokeSurfaceAlbedo(material, payload.texCoord, payload.surfaceClass, payload.translucentSubtype, payload.vertexColor, payload.vertexColorAdd);
    }
    else if (debugMode == 16)
    {
        const PathTraceSmokeMaterial material = LoadSmokeMaterial(payload.materialIndex);
        SmokeOutput[pixel] = SampleSmokeNormalTexture(material, payload.texCoord);
    }
    else if (debugMode == 17)
    {
        const PathTraceSmokeMaterial material = LoadSmokeMaterial(payload.materialIndex);
        SmokeOutput[pixel] = SampleSmokeSpecularTexture(material, payload.texCoord);
    }
    else if (debugMode == 11)
    {
        const PathTraceSmokeMaterial material = LoadSmokeMaterial(payload.materialIndex);
        const float4 albedo = SampleSmokeSurfaceAlbedo(material, payload.texCoord, payload.surfaceClass, payload.translucentSubtype, payload.vertexColor, payload.vertexColorAdd);
        if (payload.surfaceClass == 3u)
        {
            const float3 materialColor = MaterialIdToColor(payload.materialId);
            const float stripe = frac((payload.texCoord.x + payload.texCoord.y) * 8.0) > 0.5 ? 1.0 : 0.0;
            const float3 overlayColor = lerp(float3(0.0, 1.0, 1.0), float3(1.0, 0.85, 0.0), stripe);
            SmokeOutput[pixel] = float4(lerp(overlayColor, materialColor, 0.25), 1.0);
        }
        else
        {
            SmokeOutput[pixel] = albedo;
        }
    }
    else if (debugMode == 12)
    {
        const PathTraceSmokeMaterial material = LoadSmokeMaterial(payload.materialIndex);
        const float3 albedo = SampleSmokeSurfaceAlbedo(material, payload.texCoord, payload.surfaceClass, payload.translucentSubtype, payload.vertexColor, payload.vertexColorAdd).rgb;
        if (payload.surfaceClass == 3u)
        {
            const float stripe = frac((payload.texCoord.x + payload.texCoord.y) * 12.0) > 0.5 ? 1.0 : 0.0;
            const float3 subtypeColor = TranslucentSubtypeToColor(payload.translucentSubtype);
            const float3 tintedAlbedo = lerp(albedo, subtypeColor, 0.45);
            const float3 stripedTint = lerp(tintedAlbedo, subtypeColor, stripe * 0.35);
            SmokeOutput[pixel] = float4(stripedTint, 1.0);
        }
        else
        {
            SmokeOutput[pixel] = float4(albedo * 0.35, 1.0);
        }
    }
    else if (debugMode == 13)
    {
        const PathTraceSmokeMaterial material = LoadSmokeMaterial(payload.materialIndex);
        const float3 albedo = SampleSmokeSurfaceAlbedo(material, payload.texCoord, payload.surfaceClass, payload.translucentSubtype, payload.vertexColor, payload.vertexColorAdd).rgb;
        if ((material.flags & RT_SMOKE_MATERIAL_ADDITIVE_DECAL) != 0u)
        {
            const float opacity = SmokeAdditiveDecalMaterialOpacity(material, albedo);
            const bool activeEmissiveStage = (payload.triangleClassAndFlags & RT_SMOKE_TRIANGLE_EMISSIVE_STAGE_OFF) == 0u;
            const float3 emissive = debugMode == 14 ? SampleSmokeEmissive(material, payload.texCoord, payload.surfaceClass, activeEmissiveStage) : float3(0.0, 0.0, 0.0);
            SmokeOutput[pixel] = float4(saturate(albedo * (0.35 + opacity * 1.25) + emissive), 1.0);
        }
        else
        {
            const float3 normal = SafeNormalize(payload.normal, payload.geometricNormal);
            const float3 lightDir = normalize(float3(0.35, 0.45, 0.82));
            const float ndotl = saturate(dot(normal, lightDir));
            const float3 ambient = albedo * 0.12;
            const float3 diffuse = albedo * (0.18 + ndotl * 1.15);
            SmokeOutput[pixel] = float4(saturate(ambient + diffuse), 1.0);
        }
    }
    else if (debugMode == 14 || debugMode == 15)
    {
        if (SmokePayloadIsGuiScreen(payload))
        {
            SmokeOutput[pixel] = CompositeSmokeGuiLayers(ray.Origin, ray.Direction, payload);
            return;
        }
        const PathTraceSmokeMaterial material = LoadSmokeMaterial(payload.materialIndex);
        const float3 albedo = SampleSmokeSurfaceAlbedo(material, payload.texCoord, payload.surfaceClass, payload.translucentSubtype, payload.vertexColor, payload.vertexColorAdd).rgb;
        if ((material.flags & RT_SMOKE_MATERIAL_ADDITIVE_DECAL) != 0u)
        {
            const float opacity = SmokeAdditiveDecalMaterialOpacity(material, albedo);
            const bool activeEmissiveStage = (payload.triangleClassAndFlags & RT_SMOKE_TRIANGLE_EMISSIVE_STAGE_OFF) == 0u;
            const float3 emissive = debugMode == 14 ? SampleSmokeEmissive(material, payload.texCoord, payload.surfaceClass, activeEmissiveStage) : float3(0.0, 0.0, 0.0);
            SmokeOutput[pixel] = float4(saturate(albedo * (0.35 + opacity * 1.25) + emissive), 1.0);
        }
        else
        {
            const float3 baseNormal = SafeNormalize(payload.normal, payload.geometricNormal);
            const float3 normal = debugMode == 14
                ? DecodeSmokeNormalTexture(material, payload.texCoord, baseNormal, payload.tangent, payload.bitangent)
                : baseNormal;
            const float3 hitPosition = ray.Origin + ray.Direction * payload.hitT;
            const float3 viewDir = SafeNormalize(ray.Origin - hitPosition, -ray.Direction);
            const float3 specularColor = debugMode == 14 ? SampleSmokeDirectSpecular(material, payload.texCoord) : float3(0.0, 0.0, 0.0);
            const bool activeEmissiveStage = (payload.triangleClassAndFlags & RT_SMOKE_TRIANGLE_EMISSIVE_STAGE_OFF) == 0u;
            const float3 emissive = debugMode == 14 ? SampleSmokeEmissive(material, payload.texCoord, payload.surfaceClass, activeEmissiveStage) : float3(0.0, 0.0, 0.0);
            const float3 ambient = albedo * 0.12;
            const float3 unshadowedFill = albedo * 0.18;
            float3 direct = float3(0.0, 0.0, 0.0);
            float3 dominantLightDebug = float3(0.0, 0.0, 0.0);
            float dominantLightWeight = 0.0;
            const uint lightCount = PathTraceSafetyDisabled(RT_PT_SAFETY_DISABLE_SELECTED_LIGHT_LOOP) ? 0u : min((uint)LightInfo.x, RT_SMOKE_MAX_DEBUG_LIGHTS);
            if (lightCount == 0u)
            {
                if (!DoomAnalyticLightsReplaceSelected() && !PathTraceSafetyDisabled(RT_PT_SAFETY_DISABLE_SELECTED_LIGHT_LOOP))
                {
                    const float3 lightDir = normalize(float3(0.35, 0.45, 0.82));
                    const float ndotl = saturate(dot(normal, lightDir));
                    const float normalOffsetSign = dot(normal, lightDir) >= 0.0 ? 1.0 : -1.0;
                    const float3 shadowOrigin = hitPosition + normal * (normalOffsetSign * 0.75) + lightDir * 0.25;
                    const float visibility = ndotl > 0.0 ? TraceSmokeShadowVisibility(shadowOrigin, lightDir, CameraOriginAndTMax.w, 0xffffffffu, 0xffffffffu, 0xffffffffu) : 0.0;
                    direct = albedo * (ndotl * 1.15 * visibility);
                    direct += EvaluateSmokeSpecular(specularColor, normal, lightDir, viewDir, float3(0.85, 0.85, 1.0), 1.15, visibility);
                    dominantLightDebug = float3(0.85, 0.85, 1.0) * (0.15 + ndotl * visibility);
                    dominantLightWeight = ndotl * visibility;
                }
            }
            else
            {
                [loop]
                for (uint lightIndex = 0u; lightIndex < lightCount; lightIndex++)
                {
                    const float4 lightOriginAndRadius = LightOriginAndRadius[lightIndex];
                    const float3 toLight = lightOriginAndRadius.xyz - hitPosition;
                    const float lightDistance = length(toLight);
                    if (lightDistance <= 1.0e-3)
                    {
                        continue;
                    }

                    const float3 lightDir = toLight / lightDistance;
                    const float ndotl = saturate(dot(normal, lightDir));
                    if (ndotl <= 0.0)
                    {
                        continue;
                    }

                    const float normalOffsetSign = dot(normal, lightDir) >= 0.0 ? 1.0 : -1.0;
                    const float3 shadowOrigin = hitPosition + normal * (normalOffsetSign * 0.75) + lightDir * 0.25;
                    const float shadowTMax = max(lightDistance - 0.5, 0.01);
                    const float visibility = TraceSmokeShadowVisibility(shadowOrigin, lightDir, shadowTMax, 0xffffffffu, 0xffffffffu, 0xffffffffu);
                    const float lightAttenuation = saturate(1.0 - lightDistance / max(lightOriginAndRadius.w, 1.0));
                    const float directScale = 0.12 + lightAttenuation * lightAttenuation * 0.75;
                    const float3 lightColor = max(LightColorAndIntensity[lightIndex].rgb, float3(0.0, 0.0, 0.0));
                    const float contributionWeight = ndotl * directScale * visibility * max(max(lightColor.r, lightColor.g), lightColor.b);
                    direct += albedo * lightColor * (ndotl * directScale * visibility);
                    direct += EvaluateSmokeSpecular(specularColor, normal, lightDir, viewDir, lightColor, directScale, visibility);
                    if (contributionWeight > dominantLightWeight)
                    {
                        dominantLightWeight = contributionWeight;
                        dominantLightDebug = DebugLightSlotColor(lightIndex) * (0.18 + saturate(contributionWeight * 2.0) * 0.82);
                    }
                }
            }
            const uint analyticSeed =
                pixel.x * 1973u ^
                pixel.y * 9277u ^
                payload.materialId * 26699u ^
                ((uint)payload.hitT) * 7919u ^
                payload.materialIndex * 104729u ^
                ((uint)max(ToyPathInfo.w, 0.0)) * 668265263u;
            direct += EvaluateDoomAnalyticSphereLights(albedo, specularColor, normal, viewDir, hitPosition, analyticSeed);
            if (debugMode == 15)
            {
                const float3 base = albedo * 0.08;
                SmokeOutput[pixel] = float4(saturate(base + dominantLightDebug), 1.0);
            }
            else
            {
                const float3 lightSprites = EvaluateSmokeLightSpriteProxies(ray.Origin, ray.Direction, payload.hitT);
                SmokeOutput[pixel] = float4(saturate(ambient + unshadowedFill + direct + emissive + lightSprites), 1.0);
            }
        }
    }
    else if (debugMode == 20)
    {
        const float4 sampleColor = EvaluateSmokeReservoirDirectLighting(ray.Origin, ray.Direction, payload, pixel);
        SmokeOutput[pixel] = sampleColor;
    }
    else if (debugMode >= 34 && debugMode <= 37)
    {
        uint pathDepth = 0u;
        uint pathFlags = 0u;
        EvaluateSmokeToyPathTrace(ray.Origin, ray.Direction, payload, pixel, 0u, pathDepth, pathFlags);

        if (debugMode == 34)
        {
            const float t = saturate((float)pathDepth / max((float)PathTraceIntegratorMaxPathDepth(), 1.0));
            SmokeOutput[pixel] = float4(lerp(float3(0.05, 0.10, 0.18), float3(0.05, 0.95, 0.35), t), 1.0);
        }
        else if (debugMode == 35)
        {
            if ((pathFlags & RT_PT_TOY_FLAG_REFLECTION_HIT) != 0u)
            {
                SmokeOutput[pixel] = float4(0.0, 0.85, 1.0, 1.0);
            }
            else if ((pathFlags & RT_PT_TOY_FLAG_REFLECTION_MISS) != 0u)
            {
                SmokeOutput[pixel] = float4(1.0, 0.55, 0.05, 1.0);
            }
            else if ((pathFlags & RT_PT_TOY_FLAG_REFLECTION_GATED) != 0u)
            {
                SmokeOutput[pixel] = float4(0.95, 0.05, 0.05, 1.0);
            }
            else
            {
                SmokeOutput[pixel] = float4(0.08, 0.08, 0.08, 1.0);
            }
        }
        else if (debugMode == 36)
        {
            SmokeOutput[pixel] = (pathFlags & RT_PT_TOY_FLAG_REFLECTION_GATED) != 0u
                ? float4(0.95, 0.05, 0.05, 1.0)
                : ((pathFlags & RT_PT_TOY_FLAG_REFLECTION_HIT) != 0u ? float4(0.05, 0.95, 0.20, 1.0) : float4(0.08, 0.08, 0.08, 1.0));
        }
        else
        {
            if ((pathFlags & RT_PT_TOY_FLAG_MAX_DEPTH_TERMINATED) != 0u)
            {
                SmokeOutput[pixel] = float4(1.0, 0.1, 0.0, 1.0);
            }
            else if ((pathFlags & RT_PT_TOY_FLAG_RR_CONFIGURED) != 0u)
            {
                SmokeOutput[pixel] = float4(0.75, 0.15, 1.0, 1.0);
            }
            else
            {
                SmokeOutput[pixel] = float4(0.05, 0.65, 0.2, 1.0);
            }
        }
    }
    else if (debugMode == 19)
    {
        const PathTraceSmokeMaterial material = LoadSmokeMaterial(payload.materialIndex);
        const float3 albedo = SampleSmokeSurfaceAlbedo(material, payload.texCoord, payload.surfaceClass, payload.translucentSubtype, payload.vertexColor, payload.vertexColorAdd).rgb;
        const bool activeEmissiveStage = (payload.triangleClassAndFlags & RT_SMOKE_TRIANGLE_EMISSIVE_STAGE_OFF) == 0u;
        const float3 emissive = SampleSmokeEmissive(material, payload.texCoord, payload.surfaceClass, activeEmissiveStage) * max(ToyPathInfo.z, 0.0);
        const float luminance = dot(max(emissive, float3(0.0, 0.0, 0.0)), float3(0.2126, 0.7152, 0.0722));
        if ((material.flags & RT_SMOKE_MATERIAL_EMISSIVE) == 0u ||
            payload.surfaceClass == RT_SMOKE_SURFACE_CLASS_SKINNED_DEFORMED ||
            luminance <= 0.01)
        {
            SmokeOutput[pixel] = float4(albedo * 0.04, 1.0);
        }
        else
        {
            const float heat = saturate(log2(1.0 + luminance) * 0.25);
            const float stripe = frac((payload.texCoord.x + payload.texCoord.y) * 16.0) > 0.5 ? 1.0 : 0.0;
            const float3 heatColor = lerp(float3(0.05, 0.25, 1.0), float3(1.0, 0.85, 0.08), heat);
            const float inventoryPresent = EmissiveInfo.x > 0.0 ? 1.0 : 0.0;
            SmokeOutput[pixel] = float4(saturate(heatColor * (0.35 + stripe * 0.15) + emissive * 0.65 + inventoryPresent * 0.05), 1.0);
        }
    }
    else if (debugMode == 18)
    {
        const uint samplesPerPixel = PathTraceIntegratorSamplesPerPixel();
        float4 sampleColor = float4(0.0, 0.0, 0.0, 0.0);
        [loop]
        for (uint sampleIndex = 0u; sampleIndex < samplesPerPixel; ++sampleIndex)
        {
            RayDesc sampleRay = ray;
            PathTraceSmokePayload samplePayload = payload;
            if (sampleIndex > 0u)
            {
                const float jitterX = SmokeHashToUnitFloat(pixel.x * 1973u ^ pixel.y * 9277u ^ sampleIndex * 26699u) - 0.5;
                const float jitterY = SmokeHashToUnitFloat(pixel.x * 3923u ^ pixel.y * 5801u ^ sampleIndex * 104729u) - 0.5;
                const float2 sampleUv = (float2(pixel) + 0.5 + float2(jitterX, jitterY)) / float2(dimensions);
                const float2 sampleNdc = sampleUv * 2.0 - 1.0;
                sampleRay.Direction = normalize(
                    CameraForwardAndTanX.xyz +
                    CameraLeftAndTanY.xyz * (-sampleNdc.x * CameraForwardAndTanX.w) +
                    CameraUpAndDebugMode.xyz * (-sampleNdc.y * CameraLeftAndTanY.w));
                samplePayload = InitSmokePayload();
                TraceRay(SmokeScene, RAY_FLAG_NONE, traceMask, 0, 1, 0, sampleRay, samplePayload);
            }

            if (samplePayload.value == 0u)
            {
                sampleColor += float4(0.0, 0.0, 0.0, 1.0);
            }
            else
            {
                uint pathDepth = 0u;
                uint pathFlags = 0u;
                sampleColor += EvaluateSmokeToyPathTrace(sampleRay.Origin, sampleRay.Direction, samplePayload, pixel, sampleIndex, pathDepth, pathFlags);
            }
        }
        sampleColor /= max((float)samplesPerPixel, 1.0);
        sampleColor.a = 1.0;
        const uint accumulationFrame = (uint)max(ToyPathInfo.w, 0.0);
        if (accumulationFrame == 0u)
        {
            SmokeAccumulation[pixel] = sampleColor;
            SmokeOutput[pixel] = sampleColor;
        }
        else
        {
            float4 history = SmokeAccumulation[pixel];
            if (!all(history == history) || any(abs(history) > 65504.0))
            {
                history = sampleColor;
            }
            const float weight = 1.0 / ((float)accumulationFrame + 1.0);
            float4 accumulated = lerp(history, sampleColor, weight);
            accumulated.a = 1.0;
            SmokeAccumulation[pixel] = accumulated;
            SmokeOutput[pixel] = accumulated;
        }
    }
    else
    {
        SmokeOutput[pixel] = float4(0.0, 1.0, 0.0, 1.0);
    }

    SmokeOutput[pixel] = ApplySmokeBoundsOverlay(SmokeOutput[pixel], pixel, dimensions);
}

[shader("miss")]
void Miss(inout PathTraceSmokePayload payload)
{
    payload.value = 0;
}

[shader("miss")]
void ShadowMiss(inout PathTraceSmokeShadowPayload payload)
{
    payload.hit = 0u;
}

[shader("anyhit")]
void AnyHit(inout PathTraceSmokePayload payload, BuiltInTriangleIntersectionAttributes attributes)
{
    const uint instanceId = InstanceID();
    if (payload.value == 2u &&
        instanceId == payload.shadowIgnoreInstanceId)
    {
        const uint materialId = LoadSmokeTriangleMaterialId(instanceId, PrimitiveIndex());
        if (PrimitiveIndex() == payload.shadowIgnorePrimitiveIndex || materialId == payload.shadowIgnoreMaterialId)
        {
            IgnoreHit();
            return;
        }
    }
    if (instanceId >= 2u)
    {
        return;
    }
    if (SmokeAlphaRejectsHit(instanceId, PrimitiveIndex(), attributes.barycentrics, payload.value))
    {
        IgnoreHit();
    }
}

[shader("anyhit")]
void ShadowAnyHit(inout PathTraceSmokeShadowPayload payload, BuiltInTriangleIntersectionAttributes attributes)
{
    const uint instanceId = InstanceID();
    const uint primitiveIndex = PrimitiveIndex();
    payload.hit = 1u;
    if (payload.rayMode == 2u &&
        instanceId == payload.ignoreInstanceId)
    {
        const uint materialId = LoadSmokeTriangleMaterialId(instanceId, primitiveIndex);
        if (primitiveIndex == payload.ignorePrimitiveIndex || materialId == payload.ignoreMaterialId)
        {
            payload.hit = 0u;
            IgnoreHit();
            return;
        }
    }
    if (instanceId >= 2u)
    {
        return;
    }
    if (SmokeAlphaRejectsHit(instanceId, primitiveIndex, attributes.barycentrics, payload.rayMode))
    {
        payload.hit = 0u;
        IgnoreHit();
    }
}

[shader("closesthit")]
void ShadowClosestHit(inout PathTraceSmokeShadowPayload payload, BuiltInTriangleIntersectionAttributes attributes)
{
    payload.hit = 1u;
}

[shader("closesthit")]
void ClosestHit(inout PathTraceSmokePayload payload, BuiltInTriangleIntersectionAttributes attributes)
{
    const uint instanceId = InstanceID();
    const uint primitiveIndex = PrimitiveIndex();
    payload.instanceId = instanceId;
    payload.primitiveIndex = primitiveIndex;
    if (instanceId >= 2u)
    {
        const uint routeInstanceIndex = instanceId - 2u;
        if (routeInstanceIndex >= PathTraceRigidRouteInstanceCount())
        {
            return;
        }
        const PathTraceRigidRouteInstance routeInstance = SmokeRigidRouteInstances[routeInstanceIndex];
        if (primitiveIndex >= routeInstance.triangleCount ||
            routeInstance.triangleOffset + primitiveIndex >= PathTraceRigidRouteTriangleCount())
        {
            return;
        }
        const uint routeIndexOffset = routeInstance.indexOffset + primitiveIndex * 3u;
        if (routeIndexOffset > PathTraceRigidRouteIndexCount() ||
            routeIndexOffset + 2u >= PathTraceRigidRouteIndexCount())
        {
            return;
        }
        const uint i0 = SmokeRigidRouteIndices[routeIndexOffset + 0u];
        const uint i1 = SmokeRigidRouteIndices[routeIndexOffset + 1u];
        const uint i2 = SmokeRigidRouteIndices[routeIndexOffset + 2u];
        if (i0 >= routeInstance.vertexCount || i1 >= routeInstance.vertexCount || i2 >= routeInstance.vertexCount ||
            routeInstance.vertexOffset + i0 >= PathTraceRigidRouteVertexCount() ||
            routeInstance.vertexOffset + i1 >= PathTraceRigidRouteVertexCount() ||
            routeInstance.vertexOffset + i2 >= PathTraceRigidRouteVertexCount())
        {
            return;
        }
        const PathTraceSmokeVertex v0 = SmokeRigidRouteVertices[routeInstance.vertexOffset + i0];
        const PathTraceSmokeVertex v1 = SmokeRigidRouteVertices[routeInstance.vertexOffset + i1];
        const PathTraceSmokeVertex v2 = SmokeRigidRouteVertices[routeInstance.vertexOffset + i2];
        const float3 barycentrics = float3(1.0 - attributes.barycentrics.x - attributes.barycentrics.y, attributes.barycentrics.x, attributes.barycentrics.y);
        const float3 p0 = v0.position.xyz;
        const float3 p1 = v1.position.xyz;
        const float3 p2 = v2.position.xyz;
        const float3 localHit = p0 * barycentrics.x + p1 * barycentrics.y + p2 * barycentrics.z;
        const float3 n0 = v0.normal.xyz;
        const float3 n1 = v1.normal.xyz;
        const float3 n2 = v2.normal.xyz;
        const float2 uv0 = v0.texCoord.xy;
        const float2 uv1 = v1.texCoord.xy;
        const float2 uv2 = v2.texCoord.xy;

        payload.value = 1;
        payload.hitT = RayTCurrent();
        const float3 actualWorld = WorldRayOrigin() + WorldRayDirection() * payload.hitT;
        const float3 tlasWorld = TransformObjectPointToWorld(localHit);
        const float3 routeWorld = TransformPathTraceRigidRoutePoint(routeInstance.currentObjectToWorld0, routeInstance.currentObjectToWorld1, routeInstance.currentObjectToWorld2, localHit);
        payload.debugVector = float3(length(routeWorld - actualWorld), length(tlasWorld - actualWorld), length(routeWorld - tlasWorld));
        payload.debugFlags = 0x1u;
        const float3 worldRayFallback = SafeNormalize(-WorldRayDirection(), float3(0.0, 0.0, 1.0));
        const float3 objectGeometricNormal = SafeNormalize(cross(p1 - p0, p2 - p0), worldRayFallback);
        payload.geometricNormal = TransformObjectNormalToWorld(objectGeometricNormal, worldRayFallback);
        const float3 objectInterpolatedNormal = SafeNormalize(n0 * barycentrics.x + n1 * barycentrics.y + n2 * barycentrics.z, objectGeometricNormal);
        payload.normal = TransformObjectNormalToWorld(objectInterpolatedNormal, payload.geometricNormal);
        payload.texCoord = uv0 * barycentrics.x + uv1 * barycentrics.y + uv2 * barycentrics.z;
        payload.vertexColor = saturate(v0.color * barycentrics.x + v1.color * barycentrics.y + v2.color * barycentrics.z);
        payload.vertexColorAdd = saturate(v0.color2 * barycentrics.x + v1.color2 * barycentrics.y + v2.color2 * barycentrics.z);
        const float3 tangentFallback = BuildPerpendicular(payload.normal);
        const float3 bitangentFallback = SafeNormalize(cross(payload.normal, tangentFallback), float3(0.0, 1.0, 0.0));
        const float3 dp1 = p1 - p0;
        const float3 dp2 = p2 - p0;
        const float2 duv1 = uv1 - uv0;
        const float2 duv2 = uv2 - uv0;
        const float uvDeterminant = duv1.x * duv2.y - duv1.y * duv2.x;
        if (abs(uvDeterminant) > 1.0e-8)
        {
            const float inverseDeterminant = 1.0 / uvDeterminant;
            const float3 objectRawTangent = (dp1 * duv2.y - dp2 * duv1.y) * inverseDeterminant;
            const float3 objectRawBitangent = (dp2 * duv1.x - dp1 * duv2.x) * inverseDeterminant;
            const float3 rawTangent = TransformObjectVectorToWorld(objectRawTangent);
            const float3 rawBitangent = TransformObjectVectorToWorld(objectRawBitangent);
            payload.tangent = SafeNormalize(rawTangent - payload.normal * dot(payload.normal, rawTangent), tangentFallback);
            payload.bitangent = SafeNormalize(rawBitangent - payload.normal * dot(payload.normal, rawBitangent) - payload.tangent * dot(payload.tangent, rawBitangent), bitangentFallback);
            if (dot(cross(payload.tangent, payload.bitangent), payload.normal) < 0.0)
            {
                payload.bitangent = -payload.bitangent;
            }
        }
        else
        {
            payload.tangent = tangentFallback;
            payload.bitangent = bitangentFallback;
        }
        payload.surfaceClass = 1u;
        payload.translucentSubtype = 0u;
        payload.triangleClassAndFlags = 1u;
        payload.materialId = SmokeRigidRouteTriangleMaterials[routeInstance.triangleOffset + primitiveIndex];
        payload.materialIndex = SmokeRigidRouteTriangleMaterialIndexes[routeInstance.triangleOffset + primitiveIndex];
        return;
    }
    if (!SmokeTriangleIndexRangeValid(instanceId, primitiveIndex))
    {
        return;
    }
    const uint indexOffset = primitiveIndex * 3;
    const uint i0 = instanceId == 0 ? SmokeStaticIndices[indexOffset + 0] : SmokeDynamicIndices[indexOffset + 0];
    const uint i1 = instanceId == 0 ? SmokeStaticIndices[indexOffset + 1] : SmokeDynamicIndices[indexOffset + 1];
    const uint i2 = instanceId == 0 ? SmokeStaticIndices[indexOffset + 2] : SmokeDynamicIndices[indexOffset + 2];
    const uint vertexCount = instanceId == 0 ? PathTraceStaticVertexCount() : PathTraceDynamicVertexCount();
    if (i0 >= vertexCount || i1 >= vertexCount || i2 >= vertexCount)
    {
        return;
    }

    const float3 p0 = (instanceId == 0 ? SmokeStaticVertices[i0].position : SmokeDynamicVertices[i0].position).xyz;
    const float3 p1 = (instanceId == 0 ? SmokeStaticVertices[i1].position : SmokeDynamicVertices[i1].position).xyz;
    const float3 p2 = (instanceId == 0 ? SmokeStaticVertices[i2].position : SmokeDynamicVertices[i2].position).xyz;
    const float3 n0 = (instanceId == 0 ? SmokeStaticVertices[i0].normal : SmokeDynamicVertices[i0].normal).xyz;
    const float3 n1 = (instanceId == 0 ? SmokeStaticVertices[i1].normal : SmokeDynamicVertices[i1].normal).xyz;
    const float3 n2 = (instanceId == 0 ? SmokeStaticVertices[i2].normal : SmokeDynamicVertices[i2].normal).xyz;
    const float2 uv0 = (instanceId == 0 ? SmokeStaticVertices[i0].texCoord : SmokeDynamicVertices[i0].texCoord).xy;
    const float2 uv1 = (instanceId == 0 ? SmokeStaticVertices[i1].texCoord : SmokeDynamicVertices[i1].texCoord).xy;
    const float2 uv2 = (instanceId == 0 ? SmokeStaticVertices[i2].texCoord : SmokeDynamicVertices[i2].texCoord).xy;
    const float4 c0 = instanceId == 0 ? SmokeStaticVertices[i0].color : SmokeDynamicVertices[i0].color;
    const float4 c1 = instanceId == 0 ? SmokeStaticVertices[i1].color : SmokeDynamicVertices[i1].color;
    const float4 c2 = instanceId == 0 ? SmokeStaticVertices[i2].color : SmokeDynamicVertices[i2].color;
    const float4 c20 = instanceId == 0 ? SmokeStaticVertices[i0].color2 : SmokeDynamicVertices[i0].color2;
    const float4 c21 = instanceId == 0 ? SmokeStaticVertices[i1].color2 : SmokeDynamicVertices[i1].color2;
    const float4 c22 = instanceId == 0 ? SmokeStaticVertices[i2].color2 : SmokeDynamicVertices[i2].color2;
    const float3 barycentrics = float3(1.0 - attributes.barycentrics.x - attributes.barycentrics.y, attributes.barycentrics.x, attributes.barycentrics.y);

    payload.value = 1;
    payload.hitT = RayTCurrent();
    const uint triangleClassAndFlags = LoadSmokeTriangleClassAndFlags(instanceId, primitiveIndex);
    const bool forceGeometricNormal = (triangleClassAndFlags & RT_SMOKE_TRIANGLE_FORCE_GEOMETRIC_NORMAL) != 0;
    payload.geometricNormal = SafeNormalize(cross(p1 - p0, p2 - p0), float3(0.0, 0.0, 1.0));
    const float3 interpolatedNormal = SafeNormalize(n0 * barycentrics.x + n1 * barycentrics.y + n2 * barycentrics.z, payload.geometricNormal);
    payload.normal = forceGeometricNormal ? payload.geometricNormal : interpolatedNormal;
    const float3 tangentFallback = BuildPerpendicular(payload.normal);
    const float3 bitangentFallback = SafeNormalize(cross(payload.normal, tangentFallback), float3(0.0, 1.0, 0.0));
    const float3 dp1 = p1 - p0;
    const float3 dp2 = p2 - p0;
    const float2 duv1 = uv1 - uv0;
    const float2 duv2 = uv2 - uv0;
    const float uvDeterminant = duv1.x * duv2.y - duv1.y * duv2.x;
    if (abs(uvDeterminant) > 1.0e-8)
    {
        const float inverseDeterminant = 1.0 / uvDeterminant;
        const float3 rawTangent = (dp1 * duv2.y - dp2 * duv1.y) * inverseDeterminant;
        const float3 rawBitangent = (dp2 * duv1.x - dp1 * duv2.x) * inverseDeterminant;
        payload.tangent = SafeNormalize(rawTangent - payload.normal * dot(payload.normal, rawTangent), tangentFallback);
        payload.bitangent = SafeNormalize(rawBitangent - payload.normal * dot(payload.normal, rawBitangent) - payload.tangent * dot(payload.tangent, rawBitangent), bitangentFallback);
        if (dot(cross(payload.tangent, payload.bitangent), payload.normal) < 0.0)
        {
            payload.bitangent = -payload.bitangent;
        }
    }
    else
    {
        payload.tangent = tangentFallback;
        payload.bitangent = bitangentFallback;
    }
    payload.texCoord = uv0 * barycentrics.x + uv1 * barycentrics.y + uv2 * barycentrics.z;
    payload.vertexColor = saturate(c0 * barycentrics.x + c1 * barycentrics.y + c2 * barycentrics.z);
    payload.vertexColorAdd = saturate(c20 * barycentrics.x + c21 * barycentrics.y + c22 * barycentrics.z);
    payload.surfaceClass = triangleClassAndFlags & RT_SMOKE_TRIANGLE_CLASS_MASK;
    payload.translucentSubtype = (triangleClassAndFlags & RT_SMOKE_TRANSLUCENT_SUBTYPE_MASK) >> RT_SMOKE_TRANSLUCENT_SUBTYPE_SHIFT;
    payload.triangleClassAndFlags = triangleClassAndFlags;
    payload.materialId = LoadSmokeTriangleMaterialId(instanceId, primitiveIndex);
    payload.materialIndex = instanceId == 0 ? SmokeStaticTriangleMaterialIndexes[primitiveIndex] : SmokeDynamicTriangleMaterialIndexes[primitiveIndex];
}
