// Clean-room Remix ReSTIR GI lane driver.
//
// RGI-01: route sentinel + GI reservoir page ABI round-trip (view 8).
// RGI-02: initial-sample producer - from each valid primary surface, BSDF-
//         sample one indirect ray, trace it, shade the secondary hit with the
//         DI lane's light machinery (analytic lights, diffuse BRDF, shadow
//         ray), and write the producer textures (views 1 and 2).
//
// Radiance factoring contract (remix_gi_contract.txt): the stored producer
// radiance is L_in / solidAnglePdf of the bounce direction. It excludes the
// primary-surface BSDF and primary albedo; the final shading pass applies the
// receiving BRDF. Alpha channel = indirect path length. Miss = zero radiance
// + invalid hit-geometry flag. The firefly clamp applies here only.
//
// io_whitelist: reads GI-I-01 (current primary surface), GI-I-04 (current
// light universe, secondary vertex only), GI-I-05 (TLAS rays), GI-I-06
// (NEE-cache query), GI-I-09 (parameters). Writes GI-O-01 (producer textures),
// GI-O-06 (debug views).
// Never reads DI reservoir pages.

#include "../../../vulkan.hlsli"
#include "../PathTracePrimarySurface.hlsli"
#include "Rtxdi/RtxdiParameters.h"
#include "Rtxdi/GI/ReSTIRGIParameters.h"
#include "Rtxdi/Utils/RandomSamplerState.hlsli"
#include "Rtxdi/Utils/Math.hlsli"

struct PathTraceCleanRestirGiPayload
{
    uint value;
    uint rayMode;
    uint ignoreInstanceId;
    uint ignorePrimitiveIndex;
    uint ignoreMaterialIndex;
    uint hitInstanceId;
    uint hitPrimitiveIndex;
    float hitT;
    float2 hitBarycentrics;
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

struct PathTraceSmokeVertex
{
    float4 position;
    float4 normal;
    float4 texCoord;
    float4 color;
    float4 color2;
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

struct PathTraceNeeCacheProviderResult
{
    uint selectedDenseRluIndex;
    uint sourceLabel;
    uint fallbackReason;
    uint cellIndex;
    uint candidateSlot;
    uint flags;
    float sourcePdf;
    float invSourcePdf;
    float mixtureProbability;
    float reserved0;
    uint reserved1;
    uint reserved2;
};

#include "../pathtrace_material_classifier.hlsli"
#include "../RtxdiBridge/RAB_UnifiedLightRecord.hlsli"

VK_IMAGE_FORMAT("rgba32f") RWTexture2D<float4> SmokeOutput : register(u1);
RaytracingAccelerationStructure SmokeScene : register(t0);
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
Texture2D<float4> SmokeFallbackTexture : register(t14);
StructuredBuffer<PathTraceSmokeEmissiveTriangle> SmokeEmissiveTriangles : register(t16);
StructuredBuffer<PathTraceSmokeVertex> SmokeRigidRouteVertices : register(t22);
StructuredBuffer<uint> SmokeRigidRouteIndices : register(t23);
StructuredBuffer<uint> SmokeRigidRouteTriangleMaterials : register(t24);
StructuredBuffer<uint> SmokeRigidRouteTriangleMaterialIndexes : register(t25);
StructuredBuffer<PathTraceRigidRouteInstance> SmokeRigidRouteInstances : register(t26);
StructuredBuffer<PathTraceDoomAnalyticLightCandidate> DoomAnalyticLights : register(t27);
StructuredBuffer<PathTraceEmissiveDistributionEntry> SmokeEmissiveDistribution : register(t46);
StructuredBuffer<PathTraceUnifiedLightRecord> CleanRestirGiRluCurrentLights : register(t66);
StructuredBuffer<PathTraceNeeCacheProviderResult> CleanRestirGiNeeCacheProviderResults : register(t74);
RWStructuredBuffer<PathTracePrimarySurfaceRecord> PrimarySurfaceHistoryCurrent : register(u30);
RWStructuredBuffer<PathTracePrimarySurfaceRecord> PrimarySurfaceHistoryPrevious : register(u31);
VK_IMAGE_FORMAT("rgba16f") RWTexture2D<float4> PathTraceMotionVectors : register(u39);
VK_IMAGE_FORMAT("r32ui") RWTexture2D<uint> PathTraceMotionVectorMask : register(u40);
RWStructuredBuffer<RTXDI_PackedGIReservoir> RemixRAB_GIReservoirs : register(u80);
VK_IMAGE_FORMAT("rgba16f") RWTexture2D<float4> CleanRestirGiProducerRadiance : register(u81);
VK_IMAGE_FORMAT("rgba32f") RWTexture2D<float4> CleanRestirGiProducerHitPosition : register(u82);
VK_IMAGE_FORMAT("rgba16f") RWTexture2D<float4> CleanRestirGiProducerHitNormal : register(u83);
VK_IMAGE_FORMAT("rgba16f") RWTexture2D<float4> CleanRestirGiIndirectDiffuse : register(u84);
VK_IMAGE_FORMAT("rgba32f") RWTexture2D<float4> PathTraceRRInputColor : register(u54);
VK_BINDING(0, 1) Texture2D<float4> SmokeDiffuseTextures[] : register(t0, space1);
SamplerState SmokeMaterialSampler : register(s0);

// The leading block mirrors PathTraceCleanRtxdiDiSentinelConstants exactly so
// shared DI-lane helper code compiles unchanged; the C++ side copies the live
// DI constants blob into it. GI-owned fields follow.
cbuffer PathTraceCleanRestirGiConstants : register(b2)
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
    uint CleanRtxdiDiTemporalAudit;
    uint CleanRtxdiDiStaticTriangleCount;
    uint CleanRtxdiDiDynamicTriangleCount;
    uint CleanRtxdiDiRigidRouteTriangleCount;
    uint CleanRtxdiDiCurrentEmissiveTriangleCount;
    uint CleanRtxdiDiPreviousEmissiveTriangleCount;
    uint CleanRtxdiDiRluDoomAnalyticRangeOffset;
    uint CleanRtxdiDiRluDoomAnalyticRangeCount;
    uint CleanRtxdiDiDoomAnalyticFullCurrentCount;
    uint CleanRtxdiDiDoomAnalyticFullPreviousCount;
    uint CleanRtxdiDiRluDomain;
    uint CleanRtxdiDiRluDoomAnalyticParityProof;
    float4 CleanRtxdiDiTextureInfo;
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
    float4 CleanRtxdiDiNeeCacheInfo0;
    float4 CleanRtxdiDiNeeCacheInfo1;
    float4 CleanRtxdiDiRluRangeInfo;
    float4 CleanRtxdiDiRluSampleInfo;
    float4 CleanRtxdiDiToyPathInfo;
    float4 CleanRtxdiDiGeometryInfo0;
    float4 CleanRtxdiDiGeometryInfo1;
    float4 CleanRtxdiDiSpatialInfo;
    float4 CleanRtxdiDiEmissiveDistributionInfo;
    // --- GI-owned fields below; offsets must match PathTraceCleanRestirGi.cpp ---
    uint CleanRestirGiView;
    uint CleanRestirGiTemporalEnabled;
    uint CleanRestirGiSpatialEnabled;
    uint CleanRestirGiBiasCorrection;
    uint CleanRestirGiJacobianEnabled;
    uint CleanRestirGiMaxHistoryLength;
    uint CleanRestirGiMaxReservoirAge;
    float CleanRestirGiFireflyThreshold;
    uint CleanRestirGiNeeCacheSeedEnabled;
    uint CleanRestirGiFrameIndex;
    uint CleanRestirGiPhase;
    uint CleanRestirGiResolveEnabled;
    RTXDI_ReservoirBufferParameters RemixRAB_GIReservoirParams;
    uint4 RemixRAB_GIReservoirPageInfo;
    float CleanRestirGiTemporalScreenValidation;
    float3 CleanRestirGiPadding0;
};

static const uint RT_SMOKE_TRIANGLE_CLASS_MASK = 0x0000ffffu;
static const uint RT_SMOKE_TRIANGLE_FORCE_GEOMETRIC_NORMAL = 0x00010000u;
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
static const uint RT_SMOKE_MATERIAL_ALPHA_FROM_DIFFUSE_MAGENTA_KEY = 0x00001000u;
static const uint RT_SMOKE_TEXTURE_FLAG_USE_NORMAL_MAPS = 0x00000008u;
static const uint RT_SMOKE_TEXTURE_FLAG_USE_SPECULAR_MAPS = 0x00000010u;
static const uint RT_SMOKE_TEXTURE_FLAG_USE_EMISSIVE_MAPS = 0x00000020u;
static const uint RT_SMOKE_TEXTURE_FLAG_RESERVOIR_TWO_SIDED_EMISSIVES = 0x00000040u;
static const uint RT_SMOKE_TEXTURE_FLAG_TOY_FAKE_PBR_SPECULAR = 0x00000080u;
static const uint RT_SMOKE_MATERIAL_OVERRIDE_ZERO_ROUGHNESS = 0x00000001u;
#define DoomAnalyticLightInfo CleanRtxdiDiDoomAnalyticLightInfo
#define MotionVectorInfo CleanRtxdiDiMotionVectorInfo
#define RestirPTSurfaceInfo CleanRtxdiDiRestirPTSurfaceInfo
#define TextureInfo CleanRtxdiDiTextureInfo
#define ToyPathInfo CleanRtxdiDiToyPathInfo

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
static const uint CLEAN_RAB_DIAGNOSTIC_DUMMY_EMISSIVE_NORMALS = 1u << 13u;
static const uint CLEAN_FLAG_NEE_CACHE_PROVIDER = 1u << 11u;
#define RB_RAB_LIGHT_SAMPLING_CORE_ONLY 1
#define RB_RAB_CLEAN_RTXDI_DI_SENTINEL 1
#include "../RtxdiBridge/RAB_UnifiedLightRecord.hlsli"
#include "../RtxdiBridge/RAB_LightTarget.hlsli"
#include "../RtxdiBridge/RAB_NeeCache.hlsli"

#define REMIX_RAB_GI_RESERVOIR_BRIDGE_EXTERNAL_BINDINGS 1
#include "../remix_bridge/RAB_GIReservoirBridge.hlsli"

static const uint PT_MOTION_VECTOR_MASK_VALID = 0x00000001u;

float3 CleanGiSafeNormalize(float3 value, float3 fallback);
float CleanGiLuminance(float3 value);
float CleanGiTraceVisibility(float3 fromPosition, float3 geometricNormal, float3 toPosition);

// ---------------------------------------------------------------------------
// Surface bridge callback (GI-I-01/GI-I-02): material RAB_Surface from the
// DI-owned primary surface history, current or previous frame.
// ---------------------------------------------------------------------------

RAB_Surface CleanGiMaterialSurfaceFromRecord(PathTracePrimarySurfaceRecord record)
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
    surface.geometryNormal = CleanGiSafeNormalize(record.geometricNormalAndRoughness.xyz, float3(0.0, 0.0, 1.0));
    surface.shadingNormal = CleanGiSafeNormalize(record.shadingNormalAndOpacity.xyz, surface.geometryNormal);
    surface.viewDir = CleanGiSafeNormalize(record.viewDirectionAndReserved.xyz, -surface.shadingNormal);
    surface.materialId = record.materialAndSurface.x;
    surface.materialIndex = record.materialAndSurface.y;
    surface.surfaceClass = record.materialAndSurface.w & 0xffu;
    surface.flags = record.header.w;
    surface.instanceId = record.instancePrimitiveObject.x;
    surface.primitiveIndex = record.instancePrimitiveObject.y;
    RAB_Material material = RAB_EmptyMaterial();
    material.materialId = surface.materialId;
    material.materialIndex = surface.materialIndex;
    material.flags = record.materialAndSurface.z;
    material.alphaCutoff = record.albedoAndAlphaCutoff.w;
    material.diffuseAlbedo = saturate(record.albedoAndAlphaCutoff.xyz);
    material.roughness = saturate(record.geometricNormalAndRoughness.w);
    material.specularF0 = max(record.specularF0AndReserved.xyz, float3(0.0, 0.0, 0.0));
    material.opacity = saturate(record.shadingNormalAndOpacity.w);
    material.emissiveRadiance = max(record.emissiveAndHeight.xyz, float3(0.0, 0.0, 0.0));
    material.emissiveTextureIndex = record.instancePrimitiveObject.w;
    surface.material = material;
    return surface;
}

#define REMIX_RAB_SURFACE_BRIDGE_EXTERNAL_CALLBACKS 1
RAB_Surface RemixRAB_LoadSurface(int2 pixel, bool previousFrame)
{
    if (any(pixel < int2(0, 0)) ||
        pixel.x >= int(CleanRtxdiDiWidth) || pixel.y >= int(CleanRtxdiDiHeight) ||
        CleanRtxdiDiWidth == 0u || CleanRtxdiDiHeight == 0u)
    {
        return RAB_EmptySurface();
    }
    const uint index = uint(pixel.y) * CleanRtxdiDiWidth + uint(pixel.x);
    PathTracePrimarySurfaceRecord record = (PathTracePrimarySurfaceRecord)0;
    if (previousFrame)
    {
        record = PrimarySurfaceHistoryPrevious[index];
    }
    else
    {
        record = PrimarySurfaceHistoryCurrent[index];
    }
    return CleanGiMaterialSurfaceFromRecord(record);
}

// ---------------------------------------------------------------------------
// Frozen GI temporal contract, driven through the REMIX_RAB_* override points
// (definitions follow the include; the bridges declare prototypes).
// ---------------------------------------------------------------------------

#define REMIX_RAB_GI_INITIAL_SAMPLE_EXTERNAL_CALLBACKS 1
#define REMIX_RAB_GI_TEMPORAL_VALIDATION_EXTERNAL_CALLBACKS 1
#include "restir_gi_temporal_reuse.rt.hlsl"

// GI target pdf (RAB_GetGISampleTargetPdfForSurface): BSDF-weighted luminance
// of the cached radiance at the receiving surface, gated on facing, matching
// the Remix RAB_GetGITargetPdfForSurface shape with the shared diffuse BRDF.
float RemixRAB_GetGISampleTargetPdfForSurface(float3 samplePosition, float3 sampleRadiance, RAB_Surface surface)
{
    if (!RAB_IsSurfaceValid(surface))
    {
        return 0.0;
    }
    const float3 toSample = samplePosition - RAB_GetSurfaceWorldPos(surface);
    const float distanceSquared = dot(toSample, toSample);
    if (distanceSquared <= 1.0e-6)
    {
        return 0.0;
    }
    const float3 sampleDir = toSample * rsqrt(distanceSquared);
    if (dot(sampleDir, RAB_GetSurfaceGeoNormal(surface)) <= 0.0 ||
        dot(sampleDir, RAB_GetSurfaceNormal(surface)) <= 0.0)
    {
        return 0.0;
    }
    const float3 brdf = RAB_EvaluateSurfaceBrdf(surface, sampleDir, RAB_GetSurfaceViewDir(surface));
    const float ndotl = saturate(dot(RAB_GetSurfaceNormal(surface), sampleDir));
    const float targetPdf = CleanGiLuminance(brdf * max(sampleRadiance, float3(0.0, 0.0, 0.0)) * ndotl);
    return clamp(targetPdf, 0.0, 1.0e4);
}

// Jacobian validation: reject reused samples whose reconnection-shift solid-
// angle ratio is degenerate or extreme. Disabling the cvar accepts everything
// (diagnostic only).
bool RemixRAB_ValidateGISampleWithJacobian(float jacobian)
{
    if (CleanRestirGiJacobianEnabled == 0u)
    {
        return true;
    }
    return jacobian > 0.0 && jacobian < 10.0 && jacobian == jacobian;
}

bool RemixRAB_GetTemporalConservativeVisibility(
    RAB_Surface currentSurface,
    RAB_Surface temporalSurface,
    float3 samplePosition)
{
    return CleanGiTraceVisibility(
        RAB_GetSurfaceWorldPos(currentSurface),
        RAB_GetSurfaceGeoNormal(currentSurface),
        samplePosition) > 0.0;
}

bool CleanGiProjectCurrentPixel(float3 worldPosition, out uint2 pixel, out float sampleDepth)
{
    pixel = uint2(0u, 0u);
    sampleDepth = 0.0;
    const float3 delta = worldPosition - CleanRtxdiDiCameraOriginAndValid.xyz;
    const float forwardDistance = dot(delta, CleanRtxdiDiCameraForwardAndTanX.xyz);
    if (forwardDistance <= 0.05)
    {
        return false;
    }

    const float ndcX = -dot(delta, CleanRtxdiDiCameraLeftAndTanY.xyz) / max(forwardDistance * CleanRtxdiDiCameraForwardAndTanX.w, 1.0e-5);
    const float ndcY = -dot(delta, CleanRtxdiDiCameraUpAndTanY.xyz) / max(forwardDistance * CleanRtxdiDiCameraLeftAndTanY.w, 1.0e-5);
    if (abs(ndcX) > 1.0 || abs(ndcY) > 1.0)
    {
        return false;
    }

    const float2 pixelFloat = (float2(ndcX, ndcY) * 0.5 + 0.5) * float2(CleanRtxdiDiWidth, CleanRtxdiDiHeight);
    if (!all(pixelFloat == pixelFloat) ||
        pixelFloat.x < 0.0 || pixelFloat.y < 0.0 ||
        pixelFloat.x >= (float)CleanRtxdiDiWidth || pixelFloat.y >= (float)CleanRtxdiDiHeight)
    {
        return false;
    }

    pixel = uint2(pixelFloat);
    sampleDepth = length(delta);
    return sampleDepth == sampleDepth;
}

bool CleanGiCurrentScreenOccludesSample(float3 samplePosition)
{
    uint2 samplePixel;
    float sampleDepth;
    if (!CleanGiProjectCurrentPixel(samplePosition, samplePixel, sampleDepth))
    {
        return false;
    }

    const uint index = samplePixel.y * CleanRtxdiDiWidth + samplePixel.x;
    const PathTracePrimarySurfaceRecord record = PrimarySurfaceHistoryCurrent[index];
    if (record.header.x != RT_PATH_TRACE_PRIMARY_SURFACE_RECORD_VERSION ||
        (record.header.y & RT_PRIMARY_SURFACE_VALID) == 0u)
    {
        return false;
    }

    const float visibleDepth = record.worldPositionAndViewDepth.w;
    const float tolerance = max(0.75, sampleDepth * 0.05);
    return visibleDepth + tolerance < sampleDepth;
}

// Screen-space stale-sample validation, shaped after Remix's gradient-depth
// validation. If a reused GI sample projects behind current-frame primary
// geometry, fall back to this frame's initial reservoir. This avoids a
// portal-invalid world visibility ray and only targets occluded reused samples.
RemixRestirGITemporalValidationResult RemixRAB_ValidateGITemporalReservoir(
    RAB_Surface currentSurface,
    RTXDI_GIReservoir inputReservoir,
    RTXDI_GIReservoir resultReservoir,
    RemixRestirGITemporalValidationDesc desc)
{
    RemixRestirGITemporalValidationResult result = (RemixRestirGITemporalValidationResult)0;
    result.reservoir = resultReservoir;
    if (desc.enableScreenSpaceValidation != 0u &&
        dot(desc.screenSpaceMotion, desc.screenSpaceMotion) > 0.25 &&
        RTXDI_IsValidGIReservoir(inputReservoir) &&
        RTXDI_IsValidGIReservoir(resultReservoir) &&
        resultReservoir.M > inputReservoir.M &&
        CleanGiCurrentScreenOccludesSample(resultReservoir.position))
    {
        result.reservoir = inputReservoir;
        result.staleSampleRejected = 1u;
    }
    return result;
}

// Raw initial sample: this thread's producer outputs (written earlier in the
// same dispatch; GI-I-07).
RemixRestirGIRawInitialSample RemixRAB_LoadRawGIInitialSample(uint2 pixel)
{
    RemixRestirGIRawInitialSample sample = (RemixRestirGIRawInitialSample)0;
    sample.portalIndex = REMIX_RESTIR_GI_INVALID_PORTAL_INDEX;
    const float4 radianceAndLength = CleanRestirGiProducerRadiance[pixel];
    const float4 hitPositionAndValid = CleanRestirGiProducerHitPosition[pixel];
    const float4 hitNormalPacked = CleanRestirGiProducerHitNormal[pixel];
    if (hitPositionAndValid.w <= 0.0)
    {
        return sample;
    }
    sample.valid = 1u;
    sample.flags = REMIX_RESTIR_GI_INITIAL_FLAG_SELECTED_SURFACE;
    sample.radiance = max(radianceAndLength.rgb, float3(0.0, 0.0, 0.0));
    sample.indirectPathLength = radianceAndLength.a;
    sample.hitPosition = hitPositionAndValid.xyz;
    sample.hitNormal = CleanGiSafeNormalize(hitNormalPacked.xyz, float3(0.0, 0.0, 1.0));
    return sample;
}

// Remix initial-reservoir recipe: single sample with embedded pdf (M=1,
// avgWeight=1), RIS update with target pdf at the receiving surface, then
// finalize with M forced to 1 (both inputs are MIS-weighted).
RTXDI_GIReservoir RemixRAB_LoadPreparedGIInitialReservoir(
    uint2 pixel,
    RAB_Surface surface,
    RemixRestirGIRawInitialSample rawSample,
    RemixRestirGIInitialSampleControls controls)
{
    RTXDI_GIReservoir reservoir = RTXDI_EmptyGIReservoir();
    if (rawSample.valid == 0u || !RAB_IsSurfaceValid(surface))
    {
        return reservoir;
    }

    // RGI-05 hook: when the NEE-cache seed is enabled, the seeded reservoir
    // pre-loaded into the INIT page joins the RIS update here.
    if (CleanRestirGiNeeCacheSeedEnabled != 0u)
    {
        reservoir = RAB_LoadGIReservoir(int2(pixel), int(RemixRAB_GetGIInitSampleReservoirIndex()));
    }

    const RTXDI_GIReservoir initialSample = RTXDI_MakeGIReservoir(
        rawSample.hitPosition,
        rawSample.hitNormal,
        rawSample.radiance,
        1.0);

    RAB_RandomSamplerState rng = RAB_InitRandomSampler(pixel, CleanRestirGiFrameIndex, 2u);
    const float targetPdf = RemixRAB_GetGISampleTargetPdfForSurface(initialSample.position, initialSample.radiance, surface);
    RTXDI_CombineGIReservoirs(reservoir, initialSample, RAB_GetNextRandom(rng), targetPdf);

    const float pNew = RemixRAB_GetGISampleTargetPdfForSurface(reservoir.position, reservoir.radiance, surface);
    reservoir.M = 1;
    RTXDI_FinalizeGIResampling(reservoir, 1.0, pNew * reservoir.M);
    return reservoir;
}

// ---------------------------------------------------------------------------
// Motion (GI-I-03): DI motion vectors with camera-reprojection fallback.
// ---------------------------------------------------------------------------

bool CleanGiProjectCameraMotion(PathTracePrimarySurfaceRecord currentSurface, uint2 pixel, uint2 dimensions, out float3 screenSpaceMotion)
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

bool CleanGiComputeLinearDepthMotionDelta(PathTracePrimarySurfaceRecord currentSurface, out float depthDelta)
{
    depthDelta = 0.0;
    if (CleanRtxdiDiPrevCameraOriginAndValid.w < 0.5)
    {
        return false;
    }

    const uint objectMotionFlags = RT_PRIMARY_SURFACE_HAS_OBJECT_MOTION | RT_PRIMARY_SURFACE_HAS_PREVIOUS_POSITION;
    const bool hasObjectPreviousPosition =
        currentSurface.header.x == RT_PATH_TRACE_PRIMARY_SURFACE_RECORD_VERSION &&
        (currentSurface.header.y & objectMotionFlags) == objectMotionFlags &&
        currentSurface.previousPositionOrMotion.w >= 0.5;

    const float3 previousPosition = hasObjectPreviousPosition
        ? currentSurface.previousPositionOrMotion.xyz
        : currentSurface.worldPositionAndViewDepth.xyz;
    const float previousDepth = length(previousPosition - CleanRtxdiDiPrevCameraOriginAndValid.xyz);
    depthDelta = previousDepth - currentSurface.worldPositionAndViewDepth.w;
    return depthDelta == depthDelta;
}

float3 CleanGiLoadScreenSpaceMotion(uint2 pixel, PathTracePrimarySurfaceRecord currentSurface, uint2 dimensions)
{
    if (pixel.x < CleanRtxdiDiWidth && pixel.y < CleanRtxdiDiHeight)
    {
        const uint motionMask = PathTraceMotionVectorMask[pixel];
        if ((motionMask & PT_MOTION_VECTOR_MASK_VALID) != 0u)
        {
            const float2 motionXY = PathTraceMotionVectors[pixel].xy;
            float depthDelta = 0.0;
            if (all(motionXY == motionXY) &&
                CleanGiComputeLinearDepthMotionDelta(currentSurface, depthDelta))
            {
                return float3(motionXY, depthDelta);
            }
        }
    }
    float3 cameraMotion;
    if (CleanGiProjectCameraMotion(currentSurface, pixel, dimensions, cameraMotion))
    {
        return cameraMotion;
    }
    return float3(0.0, 0.0, 0.0);
}

// ---------------------------------------------------------------------------
// RGI-06: spatial reuse. Runs as a second dispatch (phase 1) so every
// pixel's TEMPORAL_OUTPUT page write has completed before neighbors read it.
// Neighbor offsets reuse the DI spatial pass's disk sequence (power-of-two
// count for the RTXDI offset mask).
// ---------------------------------------------------------------------------

static const float2 CleanGiNeighborOffsets[32] =
{
    float2( 0.096,  0.071), float2(-0.154,  0.141), float2( 0.032, -0.287), float2( 0.247,  0.246),
    float2(-0.365, -0.065), float2( 0.362, -0.248), float2(-0.135,  0.492), float2(-0.282, -0.451),
    float2( 0.553,  0.087), float2(-0.502,  0.321), float2( 0.160, -0.615), float2( 0.364,  0.563),
    float2(-0.681, -0.157), float2( 0.648, -0.331), float2(-0.245,  0.718), float2(-0.390, -0.686),
    float2( 0.793,  0.201), float2(-0.762,  0.356), float2( 0.313, -0.803), float2( 0.407,  0.779),
    float2(-0.869, -0.272), float2( 0.857, -0.347), float2(-0.392,  0.849), float2(-0.393, -0.864),
    float2( 0.908,  0.358), float2(-0.929,  0.306), float2( 0.488, -0.858), float2( 0.341,  0.930),
    float2(-0.909, -0.442), float2( 0.981, -0.226), float2(-0.589,  0.829), float2(-0.242, -0.971)
};
static const uint CLEAN_RESTIR_GI_NEIGHBOR_OFFSET_MASK = 31u;
static const uint CLEAN_RESTIR_GI_SPATIAL_RNG_PASS = 0x52525812u;

int2 RAB_ClampSamplePositionIntoView(int2 pixelPosition, bool previousFrame)
{
    const int width = int(max(CleanRtxdiDiWidth, 1u));
    const int height = int(max(CleanRtxdiDiHeight, 1u));
    return int2(clamp(pixelPosition.x, 0, width - 1), clamp(pixelPosition.y, 0, height - 1));
}

#define RTXDI_NEIGHBOR_OFFSETS_BUFFER CleanGiNeighborOffsets
#include "Rtxdi/GI/SpatialResampling.hlsli"

// Spatial reuse over the TEMPORAL_OUTPUT page per the Remix policy:
// 4 neighbor samples while history-starved (M below the history cap), else 1;
// search radius alternates large/small per frame and pixel block. Relaxed
// similarity gates (depth 0.28 / normal 0.8) match the moving-camera temporal
// gates since neighbor reuse inherently crosses surface variation.
RTXDI_GIReservoir CleanGiRunSpatialReuse(uint2 pixel, RAB_Surface surface, RTXDI_GIReservoir inputReservoir)
{
    if (!RAB_IsSurfaceValid(surface))
    {
        return inputReservoir;
    }

    RTXDI_RandomSamplerState rng = RTXDI_InitRandomSampler(pixel, CleanRestirGiFrameIndex, CLEAN_RESTIR_GI_SPATIAL_RNG_PASS);

    RTXDI_RuntimeParameters params = (RTXDI_RuntimeParameters)0;
    params.activeCheckerboardField = 0u;
    params.neighborOffsetMask = CLEAN_RESTIR_GI_NEIGHBOR_OFFSET_MASK;

    const bool historyStarved = inputReservoir.M < CleanRestirGiMaxHistoryLength;
    const bool largeRadius = historyStarved ||
        ((CleanRestirGiFrameIndex + pixel.x / 16u + pixel.y / 8u) & 1u) == 0u;

    RTXDI_GISpatialResamplingParameters sparams = (RTXDI_GISpatialResamplingParameters)0;
    sparams.samplingRadius = largeRadius ? 200.0 : 85.0;
    sparams.numSamples = historyStarved ? 4u : 1u;
    sparams.depthThreshold = 0.28;
    sparams.normalThreshold = 0.8;
    sparams.biasCorrectionMode = min(CleanRestirGiBiasCorrection, uint(RTXDI_BIAS_CORRECTION_BASIC));

    return RTXDI_GISpatialResampling(
        pixel,
        surface,
        RemixRAB_GetGITemporalOutputReservoirIndex(),
        inputReservoir,
        rng,
        params,
        RemixRAB_GIReservoirParams,
        sparams);
}

static const uint CLEAN_RESTIR_GI_PRODUCER_RNG_PASS = 0x52525810u;
static const float CLEAN_RESTIR_GI_FIREFLY_FACTOR = 30.0;

float3 CleanGiSafeNormalize(float3 value, float3 fallback)
{
    const float lengthSquared = dot(value, value);
    return lengthSquared > 1.0e-8 ? value * rsqrt(lengthSquared) : fallback;
}

float CleanGiLuminance(float3 value)
{
    return dot(max(value, float3(0.0, 0.0, 0.0)), float3(0.2126, 0.7152, 0.0722));
}

bool CleanGiAllFinite3(float3 value)
{
    return all(value == value) && all(abs(value) < float3(3.402823e+38, 3.402823e+38, 3.402823e+38));
}

// ---------------------------------------------------------------------------
// Surface loading (GI-I-01; same adapters as the DI lane)
// ---------------------------------------------------------------------------

bool CleanGiLoadSurfaceRecord(uint2 pixel, uint2 dimensions, out PathTracePrimarySurfaceRecord record)
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

uint CleanGiLoadTriangleMaterialIndex(uint instanceId, uint primitiveIndex)
{
    if (instanceId == 0u)
    {
        return primitiveIndex < CleanRtxdiDiStaticTriangleCount
            ? SmokeStaticTriangleMaterialIndexes[primitiveIndex]
            : 0xffffffffu;
    }
    if (instanceId == 1u)
    {
        return primitiveIndex < CleanRtxdiDiDynamicTriangleCount
            ? SmokeDynamicTriangleMaterialIndexes[primitiveIndex]
            : 0xffffffffu;
    }
    const uint routeInstanceIndex = instanceId - 2u;
    const uint routeInstanceCount = (uint)max(CleanRtxdiDiToyPathInfo.w, 0.0);
    if (routeInstanceIndex >= routeInstanceCount)
    {
        return 0xffffffffu;
    }
    const PathTraceRigidRouteInstance route = SmokeRigidRouteInstances[routeInstanceIndex];
    const uint routedPrimitiveIndex = route.triangleOffset + primitiveIndex;
    if (primitiveIndex >= route.triangleCount ||
        routedPrimitiveIndex >= CleanRtxdiDiRigidRouteTriangleCount)
    {
        return 0xffffffffu;
    }
    return SmokeRigidRouteTriangleMaterialIndexes[routedPrimitiveIndex];
}

uint CleanGiLoadTriangleMaterialId(uint instanceId, uint primitiveIndex)
{
    if (instanceId == 0u)
    {
        return primitiveIndex < CleanRtxdiDiStaticTriangleCount
            ? SmokeStaticTriangleMaterials[primitiveIndex]
            : 0u;
    }
    if (instanceId == 1u)
    {
        return primitiveIndex < CleanRtxdiDiDynamicTriangleCount
            ? SmokeDynamicTriangleMaterials[primitiveIndex]
            : 0u;
    }
    const uint routeInstanceIndex = instanceId - 2u;
    const uint routeInstanceCount = (uint)max(CleanRtxdiDiToyPathInfo.w, 0.0);
    if (routeInstanceIndex >= routeInstanceCount)
    {
        return 0u;
    }
    const PathTraceRigidRouteInstance route = SmokeRigidRouteInstances[routeInstanceIndex];
    const uint routedPrimitiveIndex = route.triangleOffset + primitiveIndex;
    if (primitiveIndex >= route.triangleCount ||
        routedPrimitiveIndex >= CleanRtxdiDiRigidRouteTriangleCount)
    {
        return route.materialId;
    }
    return SmokeRigidRouteTriangleMaterials[routedPrimitiveIndex];
}

uint CleanGiLoadTriangleClassAndFlags(uint instanceId, uint primitiveIndex)
{
    if (instanceId == 0u)
    {
        return primitiveIndex < CleanRtxdiDiStaticTriangleCount
            ? SmokeStaticTriangleClasses[primitiveIndex]
            : 0u;
    }
    if (instanceId == 1u)
    {
        return primitiveIndex < CleanRtxdiDiDynamicTriangleCount
            ? SmokeDynamicTriangleClasses[primitiveIndex]
            : 0u;
    }
    return RT_SMOKE_SURFACE_CLASS_RIGID_ENTITY;
}

uint CleanGiTriangleSurfaceClass(uint triangleClassAndFlags)
{
    return triangleClassAndFlags & RT_SMOKE_TRIANGLE_CLASS_MASK;
}

uint CleanGiTriangleTranslucentSubtype(uint triangleClassAndFlags)
{
    return (triangleClassAndFlags & RT_SMOKE_TRANSLUCENT_SUBTYPE_MASK) >> RT_SMOKE_TRANSLUCENT_SUBTYPE_SHIFT;
}

PathTraceSmokeMaterial CleanGiLoadSmokeMaterial(uint materialIndex)
{
    PathTraceSmokeMaterial material = (PathTraceSmokeMaterial)0;
    material.debugAlbedo = float4(0.5, 0.5, 0.5, 1.0);
    material.diffuseTextureIndex = 0xffffffffu;
    material.alphaTextureIndex = 0xffffffffu;
    material.normalTextureIndex = 0xffffffffu;
    material.specularTextureIndex = 0xffffffffu;
    material.emissiveTextureIndex = 0xffffffffu;
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
    const uint materialCount = (uint)TextureInfo.z;
    if (materialIndex < materialCount)
    {
        material = SmokeMaterials[materialIndex];
    }
    return material;
}

// ---------------------------------------------------------------------------
// Texture sampling for the secondary-hit material (same decode rules as the
// primary-surface producer)
// ---------------------------------------------------------------------------

float4 CleanGiTextureLoad(uint textureIndex, uint textureWidth, uint textureHeight, float2 wrappedTexCoord, bool bindlessEnabled, bool bilinearFilter)
{
    const uint width = max(textureWidth, 1u);
    const uint height = max(textureHeight, 1u);
    if (!bilinearFilter)
    {
        const uint2 texel = min((uint2)floor(wrappedTexCoord * float2(width, height)), uint2(width - 1u, height - 1u));
        return bindlessEnabled
            ? SmokeDiffuseTextures[NonUniformResourceIndex(textureIndex)].Load(int3(texel, 0))
            : SmokeFallbackTexture.Load(int3(0, 0, 0));
    }
    const float2 scaled = wrappedTexCoord * float2(width, height) - float2(0.5, 0.5);
    const int2 baseTexel = (int2)floor(scaled);
    const float2 fracPart = frac(scaled);
    const uint2 texel00 = uint2((baseTexel.x % (int)width + (int)width) % (int)width, (baseTexel.y % (int)height + (int)height) % (int)height);
    const uint2 texel10 = uint2((texel00.x + 1u) % width, texel00.y);
    const uint2 texel01 = uint2(texel00.x, (texel00.y + 1u) % height);
    const uint2 texel11 = uint2((texel00.x + 1u) % width, (texel00.y + 1u) % height);
    const float4 c00 = bindlessEnabled ? SmokeDiffuseTextures[NonUniformResourceIndex(textureIndex)].Load(int3(texel00, 0)) : SmokeFallbackTexture.Load(int3(0, 0, 0));
    const float4 c10 = bindlessEnabled ? SmokeDiffuseTextures[NonUniformResourceIndex(textureIndex)].Load(int3(texel10, 0)) : SmokeFallbackTexture.Load(int3(0, 0, 0));
    const float4 c01 = bindlessEnabled ? SmokeDiffuseTextures[NonUniformResourceIndex(textureIndex)].Load(int3(texel01, 0)) : SmokeFallbackTexture.Load(int3(0, 0, 0));
    const float4 c11 = bindlessEnabled ? SmokeDiffuseTextures[NonUniformResourceIndex(textureIndex)].Load(int3(texel11, 0)) : SmokeFallbackTexture.Load(int3(0, 0, 0));
    return lerp(lerp(c00, c10, fracPart.x), lerp(c01, c11, fracPart.x), fracPart.y);
}

float4 CleanGiSampleTexture(uint textureIndex, uint textureWidth, uint textureHeight, float2 texCoord, float4 fallback)
{
    const uint textureCount = (uint)TextureInfo.x;
    const uint sampleMethod = (uint)TextureInfo.y;
    const uint textureFlags = (uint)TextureInfo.w;
    const bool bindlessEnabled = (textureFlags & 1u) != 0u;
    const bool bilinearFilter = (textureFlags & 2u) != 0u;
    if (sampleMethod == 0u || textureIndex == 0xffffffffu || textureIndex >= textureCount ||
        !all(texCoord == texCoord) || any(abs(texCoord) > 65536.0))
    {
        return fallback;
    }
    const float2 wrappedTexCoord = frac(texCoord);
    float4 sampled = sampleMethod == 2u
        ? CleanGiTextureLoad(textureIndex, textureWidth, textureHeight, wrappedTexCoord, bindlessEnabled, bilinearFilter)
        : (bindlessEnabled
            ? SmokeDiffuseTextures[NonUniformResourceIndex(textureIndex)].SampleLevel(SmokeMaterialSampler, wrappedTexCoord, 0.0)
            : SmokeFallbackTexture.SampleLevel(SmokeMaterialSampler, wrappedTexCoord, 0.0));
    return (!all(sampled == sampled) || any(abs(sampled) > 65504.0)) ? fallback : sampled;
}

float3 CleanGiConvertYCoCgToRGB(float4 ycocg)
{
    ycocg.z = (ycocg.z * 31.875) + 1.0;
    ycocg.z = 1.0 / ycocg.z;
    ycocg.xy *= ycocg.z;
    return saturate(float3(
        dot(ycocg, float4(1.0, -1.0, 0.0, 1.0)),
        dot(ycocg, float4(0.0, 1.0, -0.50196078, 1.0)),
        dot(ycocg, float4(-1.0, -1.0, 1.00392156, 1.0))));
}

float4 CleanGiSampleDecodedDiffuseTexture(PathTraceSmokeMaterial material, float2 texCoord)
{
    float4 texel = CleanGiSampleTexture(material.diffuseTextureIndex, material.textureWidth, material.textureHeight, texCoord, material.debugAlbedo);
    const bool textureDecodeEnabled = (((uint)TextureInfo.w) & 4u) != 0u;
    if (textureDecodeEnabled && (material.flags & RT_SMOKE_MATERIAL_DIFFUSE_YCOCG) != 0u)
    {
        texel.rgb = CleanGiConvertYCoCgToRGB(texel);
    }
    return texel;
}

float4 CleanGiSampleDiffuseTexture(PathTraceSmokeMaterial material, float2 texCoord)
{
    return (material.flags & RT_SMOKE_MATERIAL_FORCE_DEBUG_ALBEDO) != 0u
        ? material.debugAlbedo
        : CleanGiSampleDecodedDiffuseTexture(material, texCoord);
}

float3 CleanGiSampleDiffuseAlbedo(PathTraceSmokeMaterial material, float2 texCoord)
{
    return saturate(CleanGiSampleDiffuseTexture(material, texCoord).rgb);
}

float4 CleanGiSampleSurfaceAlbedo(
    PathTraceSmokeMaterial material,
    float2 texCoord,
    uint surfaceClass,
    uint translucentSubtype,
    float4 vertexColor)
{
    float4 albedo = CleanGiSampleDiffuseTexture(material, texCoord);
    if (surfaceClass == RT_SMOKE_SURFACE_CLASS_TRANSLUCENT &&
        translucentSubtype == RT_SMOKE_TRANSLUCENT_SUBTYPE_GUI_SCREEN)
    {
        albedo = material.diffuseTextureIndex != 0xffffffffu
            ? float4(albedo.rgb * vertexColor.rgb, albedo.a * vertexColor.a)
            : vertexColor;
    }
    return saturate(albedo);
}

float4 CleanGiSampleAlphaTexture(PathTraceSmokeMaterial material, float2 texCoord)
{
    return material.alphaTextureIndex != 0xffffffffu
        ? CleanGiSampleTexture(
            material.alphaTextureIndex,
            material.alphaTextureWidth,
            material.alphaTextureHeight,
            texCoord,
            CleanGiSampleDiffuseTexture(material, texCoord))
        : CleanGiSampleDiffuseTexture(material, texCoord);
}

float CleanGiAlphaCoverage(PathTraceSmokeMaterial material, float2 texCoord)
{
    if ((material.flags & RT_SMOKE_MATERIAL_ALPHA_FROM_DIFFUSE_DARK_KEY) != 0u)
    {
        const float3 decoded = saturate(CleanGiSampleDecodedDiffuseTexture(material, texCoord).rgb);
        return 1.0 - max(max(decoded.r, decoded.g), decoded.b);
    }
    if ((material.flags & RT_SMOKE_MATERIAL_ALPHA_FROM_DIFFUSE_LUMA) != 0u)
    {
        const float3 decoded = saturate(CleanGiSampleDecodedDiffuseTexture(material, texCoord).rgb);
        return max(max(decoded.r, decoded.g), decoded.b);
    }
    if ((material.flags & RT_SMOKE_MATERIAL_ALPHA_FROM_DIFFUSE_MAGENTA_KEY) != 0u)
    {
        const float3 decoded = saturate(CleanGiSampleDecodedDiffuseTexture(material, texCoord).rgb);
        const float keyDistance = max(abs(decoded.r - 1.0), max(abs(decoded.g), abs(decoded.b - 1.0)));
        return keyDistance <= 0.08 ? 0.0 : 1.0;
    }
    return saturate(CleanGiSampleAlphaTexture(material, texCoord).a);
}

float CleanGiSmokeLinear1(float c)
{
    return c <= 0.04045 ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4);
}

float3 CleanGiSmokeLinear3(float3 c)
{
    return float3(CleanGiSmokeLinear1(c.r), CleanGiSmokeLinear1(c.g), CleanGiSmokeLinear1(c.b));
}

void CleanGiSmokePBRFromSpecmap(float3 specMap, out float3 F0, out float roughness)
{
    const float specLum = dot(float3(0.2125, 0.7154, 0.0721), specMap);
    F0 = float3(0.04, 0.04, 0.04);
    const float contrastMid = 0.214;
    const float contrastAmount = 2.0;
    float contrast = saturate((specLum - contrastMid) / (1.0 - contrastMid));
    contrast += saturate(specLum / contrastMid) - 1.0;
    contrast = exp2(contrastAmount * contrast);
    F0 *= contrast;
    const float linearBrightness = CleanGiSmokeLinear1(2.0 * specLum);
    const float specPow = max(0.0, ((8.0 * linearBrightness) / max(F0.y, 1.0e-4)) - 2.0);
    F0 *= min(1.0, linearBrightness / max(F0.y * 0.25, 1.0e-4));
    roughness = sqrt(2.0 / (specPow + 2.0));
    const float glossiness = saturate(1.0 - roughness);
    const float metallic = step(0.7, glossiness);
    const float3 glossColor = CleanGiSmokeLinear3(specMap.rgb);
    F0 = lerp(F0, glossColor, metallic);
    roughness = sqrt(roughness);
}

bool CleanGiToyFakePBRSpecularEnabled()
{
    return (((uint)TextureInfo.w) & RT_SMOKE_TEXTURE_FLAG_TOY_FAKE_PBR_SPECULAR) != 0u;
}

float3 CleanGiSampleDirectSpecular(PathTraceSmokeMaterial material, float2 texCoord)
{
    if (material.specularTextureIndex == 0xffffffffu ||
        ((((uint)TextureInfo.w) & RT_SMOKE_TEXTURE_FLAG_USE_SPECULAR_MAPS) == 0u))
    {
        return float3(0.0, 0.0, 0.0);
    }
    return saturate(CleanGiSampleTexture(
        material.specularTextureIndex,
        material.specularTextureWidth,
        material.specularTextureHeight,
        texCoord,
        float4(0.0, 0.0, 0.0, 1.0)).rgb);
}

float3 CleanGiBuildPerpendicular(float3 normal)
{
    const float3 axis = abs(normal.z) < 0.999 ? float3(0.0, 0.0, 1.0) : float3(0.0, 1.0, 0.0);
    return CleanGiSafeNormalize(cross(axis, normal), float3(1.0, 0.0, 0.0));
}

float3 CleanGiDecodeNormalTexture(
    PathTraceSmokeMaterial material,
    float2 texCoord,
    float3 normal,
    float3 tangent,
    float3 bitangent)
{
    if (material.normalTextureIndex == 0xffffffffu ||
        ((((uint)TextureInfo.w) & RT_SMOKE_TEXTURE_FLAG_USE_NORMAL_MAPS) == 0u))
    {
        return normal;
    }

    const float4 bump = CleanGiSampleTexture(
        material.normalTextureIndex,
        material.normalTextureWidth,
        material.normalTextureHeight,
        texCoord,
        float4(0.5, 0.5, 1.0, 1.0)) * 2.0 - 1.0;
    if (!all(bump == bump))
    {
        return normal;
    }

    const float2 normalXY = SmokeMatClassNormalXY(material, bump, 0.0);
    float3 decoded = float3(normalXY, 0.0);
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
    return CleanGiSafeNormalize(tangent * decoded.x + bitangent * decoded.y + normal * decoded.z, normal);
}

float3 CleanGiConstrainShadingNormal(float3 shadingNormal, float3 geometryNormal)
{
    const float minGeometryDot = 0.02;
    geometryNormal = CleanGiSafeNormalize(geometryNormal, float3(0.0, 0.0, 1.0));
    shadingNormal = CleanGiSafeNormalize(shadingNormal, geometryNormal);
    const float geometryDot = dot(shadingNormal, geometryNormal);
    if (geometryDot >= minGeometryDot)
    {
        return shadingNormal;
    }

    float3 tangentComponent = shadingNormal - geometryNormal * geometryDot;
    const float tangentLengthSquared = dot(tangentComponent, tangentComponent);
    if (tangentLengthSquared <= 1.0e-8)
    {
        return geometryNormal;
    }

    tangentComponent *= rsqrt(tangentLengthSquared);
    const float tangentScale = sqrt(max(1.0 - minGeometryDot * minGeometryDot, 0.0));
    return CleanGiSafeNormalize(tangentComponent * tangentScale + geometryNormal * minGeometryDot, geometryNormal);
}

bool CleanGiMaterialUsesUnlitColorFallback(PathTraceSmokeMaterial material, uint surfaceClass, uint translucentSubtype)
{
    if (surfaceClass != RT_SMOKE_SURFACE_CLASS_TRANSLUCENT)
    {
        return false;
    }
    if (translucentSubtype == RT_SMOKE_TRANSLUCENT_SUBTYPE_OBJECT_GLASS ||
        translucentSubtype == RT_SMOKE_TRANSLUCENT_SUBTYPE_PORTAL_WINDOW ||
        translucentSubtype == RT_SMOKE_TRANSLUCENT_SUBTYPE_GUI_SCREEN)
    {
        return false;
    }
    if ((material.flags & RT_SMOKE_MATERIAL_ADDITIVE_DECAL) != 0u)
    {
        return true;
    }
    return translucentSubtype == RT_SMOKE_TRANSLUCENT_SUBTYPE_SMOKE_PARTICLE &&
        material.alphaTextureIndex == 0xffffffffu;
}

float3 CleanGiSampleEmissiveRadiance(PathTraceSmokeMaterial material, float2 texCoord, uint surfaceClass, bool activeEmissiveStage)
{
    if ((material.flags & RT_SMOKE_MATERIAL_EMISSIVE) == 0u ||
        !activeEmissiveStage ||
        surfaceClass == RT_SMOKE_SURFACE_CLASS_SKINNED_DEFORMED ||
        ((((uint)TextureInfo.w) & RT_SMOKE_TEXTURE_FLAG_USE_EMISSIVE_MAPS) == 0u))
    {
        return float3(0.0, 0.0, 0.0);
    }
    const float emissiveScale = max(ToyPathInfo.z, 0.0);
    if (emissiveScale <= 0.0)
    {
        return float3(0.0, 0.0, 0.0);
    }
    float3 emissiveTexel = float3(1.0, 1.0, 1.0);
    if (material.emissiveTextureIndex != 0xffffffffu)
    {
        emissiveTexel = saturate(CleanGiSampleTexture(material.emissiveTextureIndex, material.emissiveTextureWidth, material.emissiveTextureHeight, texCoord, float4(1.0, 1.0, 1.0, 1.0)).rgb);
    }
    return max(material.emissiveColor.rgb, float3(0.0, 0.0, 0.0)) * emissiveTexel * (1.75 * emissiveScale);
}

RAB_Material CleanGiBuildMaterialFromHit(
    uint materialId,
    uint materialIndex,
    PathTraceSmokeMaterial smokeMaterial,
    float2 texCoord,
    uint surfaceClass,
    uint translucentSubtype,
    uint triangleClassAndFlags,
    float4 vertexColor)
{
    float3 materialAlbedo = CleanGiSampleSurfaceAlbedo(smokeMaterial, texCoord, surfaceClass, translucentSubtype, vertexColor).rgb;
    const float3 specularColor = CleanGiSampleDirectSpecular(smokeMaterial, texCoord);
    float3 specularF0 = specularColor;
    float roughness = 1.0;
    if (CleanGiToyFakePBRSpecularEnabled())
    {
        CleanGiSmokePBRFromSpecmap(saturate(specularColor), specularF0, roughness);
    }
    SmokeApplyMaterialClassifierBsdfWithSpecularTexel(smokeMaterial, materialAlbedo, saturate(specularColor), specularF0, roughness);
    if ((smokeMaterial.padding0 & RT_SMOKE_MATERIAL_OVERRIDE_ZERO_ROUGHNESS) != 0u)
    {
        roughness = 0.0;
        specularF0 = max(specularF0, float3(0.85, 0.85, 0.85));
    }
    const bool activeEmissiveStage = (triangleClassAndFlags & RT_SMOKE_TRIANGLE_EMISSIVE_STAGE_OFF) == 0u;

    RAB_Material material = RAB_EmptyMaterial();
    material.materialId = materialId;
    material.materialIndex = materialIndex;
    material.flags = smokeMaterial.flags;
    material.alphaCutoff = smokeMaterial.alphaCutoff;
    material.diffuseAlbedo = saturate(materialAlbedo);
    material.roughness = saturate(roughness);
    material.specularF0 = max(specularF0, float3(0.0, 0.0, 0.0));
    material.opacity = CleanGiAlphaCoverage(smokeMaterial, texCoord);
    material.emissiveRadiance = CleanGiSampleEmissiveRadiance(smokeMaterial, texCoord, surfaceClass, activeEmissiveStage);
    if (CleanGiMaterialUsesUnlitColorFallback(smokeMaterial, surfaceClass, translucentSubtype))
    {
        material.emissiveRadiance = max(material.emissiveRadiance, material.diffuseAlbedo);
    }
    material.emissiveTextureIndex = smokeMaterial.emissiveTextureIndex;
    return material;
}

// ---------------------------------------------------------------------------
// Secondary-hit geometry reconstruction
// ---------------------------------------------------------------------------

float3 CleanGiTransformRigidRoutePoint(PathTraceRigidRouteInstance routeInstance, float3 localPoint)
{
    return float3(
        dot(routeInstance.currentObjectToWorld0, float4(localPoint, 1.0)),
        dot(routeInstance.currentObjectToWorld1, float4(localPoint, 1.0)),
        dot(routeInstance.currentObjectToWorld2, float4(localPoint, 1.0)));
}

float3 CleanGiTransformRigidRouteVector(PathTraceRigidRouteInstance routeInstance, float3 localVector)
{
    return float3(
        dot(routeInstance.currentObjectToWorld0.xyz, localVector),
        dot(routeInstance.currentObjectToWorld1.xyz, localVector),
        dot(routeInstance.currentObjectToWorld2.xyz, localVector));
}

bool CleanGiLoadTriangleGeometryFull(
    uint instanceId,
    uint primitiveIndex,
    out float3 p0, out float3 p1, out float3 p2,
    out float3 n0, out float3 n1, out float3 n2,
    out float2 uv0, out float2 uv1, out float2 uv2,
    out float4 c0, out float4 c1, out float4 c2,
    out float4 c20, out float4 c21, out float4 c22)
{
    p0 = float3(0.0, 0.0, 0.0);
    p1 = float3(0.0, 0.0, 0.0);
    p2 = float3(0.0, 0.0, 0.0);
    n0 = float3(0.0, 0.0, 1.0);
    n1 = float3(0.0, 0.0, 1.0);
    n2 = float3(0.0, 0.0, 1.0);
    uv0 = float2(0.0, 0.0);
    uv1 = float2(0.0, 0.0);
    uv2 = float2(0.0, 0.0);
    c0 = float4(1.0, 1.0, 1.0, 1.0);
    c1 = float4(1.0, 1.0, 1.0, 1.0);
    c2 = float4(1.0, 1.0, 1.0, 1.0);
    c20 = float4(0.5, 0.5, 0.5, 0.5);
    c21 = float4(0.5, 0.5, 0.5, 0.5);
    c22 = float4(0.5, 0.5, 0.5, 0.5);

    if (instanceId == 0u)
    {
        const uint vertexCount = (uint)max(CleanRtxdiDiGeometryInfo0.x, 0.0);
        const uint indexCount = (uint)max(CleanRtxdiDiGeometryInfo0.y, 0.0);
        const uint triangleCount = (uint)max(CleanRtxdiDiGeometryInfo0.z, 0.0);
        const uint indexOffset = primitiveIndex * 3u;
        if (primitiveIndex >= triangleCount || indexOffset + 2u >= indexCount)
        {
            return false;
        }
        const uint i0 = SmokeStaticIndices[indexOffset + 0u];
        const uint i1 = SmokeStaticIndices[indexOffset + 1u];
        const uint i2 = SmokeStaticIndices[indexOffset + 2u];
        if (i0 >= vertexCount || i1 >= vertexCount || i2 >= vertexCount)
        {
            return false;
        }
        const PathTraceSmokeVertex v0 = SmokeStaticVertices[i0];
        const PathTraceSmokeVertex v1 = SmokeStaticVertices[i1];
        const PathTraceSmokeVertex v2 = SmokeStaticVertices[i2];
        p0 = v0.position.xyz; p1 = v1.position.xyz; p2 = v2.position.xyz;
        n0 = v0.normal.xyz; n1 = v1.normal.xyz; n2 = v2.normal.xyz;
        uv0 = v0.texCoord.xy; uv1 = v1.texCoord.xy; uv2 = v2.texCoord.xy;
        c0 = v0.color; c1 = v1.color; c2 = v2.color;
        c20 = v0.color2; c21 = v1.color2; c22 = v2.color2;
        return true;
    }
    if (instanceId == 1u)
    {
        const uint vertexCount = (uint)max(CleanRtxdiDiGeometryInfo0.w, 0.0);
        const uint indexCount = (uint)max(CleanRtxdiDiGeometryInfo1.x, 0.0);
        const uint triangleCount = (uint)max(CleanRtxdiDiGeometryInfo1.y, 0.0);
        const uint indexOffset = primitiveIndex * 3u;
        if (primitiveIndex >= triangleCount || indexOffset + 2u >= indexCount)
        {
            return false;
        }
        const uint i0 = SmokeDynamicIndices[indexOffset + 0u];
        const uint i1 = SmokeDynamicIndices[indexOffset + 1u];
        const uint i2 = SmokeDynamicIndices[indexOffset + 2u];
        if (i0 >= vertexCount || i1 >= vertexCount || i2 >= vertexCount)
        {
            return false;
        }
        const PathTraceSmokeVertex v0 = SmokeDynamicVertices[i0];
        const PathTraceSmokeVertex v1 = SmokeDynamicVertices[i1];
        const PathTraceSmokeVertex v2 = SmokeDynamicVertices[i2];
        p0 = v0.position.xyz; p1 = v1.position.xyz; p2 = v2.position.xyz;
        n0 = v0.normal.xyz; n1 = v1.normal.xyz; n2 = v2.normal.xyz;
        uv0 = v0.texCoord.xy; uv1 = v1.texCoord.xy; uv2 = v2.texCoord.xy;
        c0 = v0.color; c1 = v1.color; c2 = v2.color;
        c20 = v0.color2; c21 = v1.color2; c22 = v2.color2;
        return true;
    }

    const uint routeInstanceIndex = instanceId - 2u;
    const uint routeInstanceCount = (uint)max(CleanRtxdiDiToyPathInfo.w, 0.0);
    if (routeInstanceIndex >= routeInstanceCount)
    {
        return false;
    }
    const PathTraceRigidRouteInstance route = SmokeRigidRouteInstances[routeInstanceIndex];
    const uint vertexCount = (uint)max(CleanRtxdiDiGeometryInfo1.z, 0.0);
    const uint indexCount = (uint)max(CleanRtxdiDiGeometryInfo1.w, 0.0);
    const uint indexOffset = route.indexOffset + primitiveIndex * 3u;
    if (primitiveIndex >= route.triangleCount || indexOffset + 2u >= indexCount)
    {
        return false;
    }
    const uint i0 = SmokeRigidRouteIndices[indexOffset + 0u];
    const uint i1 = SmokeRigidRouteIndices[indexOffset + 1u];
    const uint i2 = SmokeRigidRouteIndices[indexOffset + 2u];
    if (i0 >= route.vertexCount || i1 >= route.vertexCount || i2 >= route.vertexCount ||
        route.vertexOffset + i0 >= vertexCount || route.vertexOffset + i1 >= vertexCount || route.vertexOffset + i2 >= vertexCount)
    {
        return false;
    }
    const PathTraceSmokeVertex v0 = SmokeRigidRouteVertices[route.vertexOffset + i0];
    const PathTraceSmokeVertex v1 = SmokeRigidRouteVertices[route.vertexOffset + i1];
    const PathTraceSmokeVertex v2 = SmokeRigidRouteVertices[route.vertexOffset + i2];
    p0 = CleanGiTransformRigidRoutePoint(route, v0.position.xyz);
    p1 = CleanGiTransformRigidRoutePoint(route, v1.position.xyz);
    p2 = CleanGiTransformRigidRoutePoint(route, v2.position.xyz);
    n0 = CleanGiTransformRigidRouteVector(route, v0.normal.xyz);
    n1 = CleanGiTransformRigidRouteVector(route, v1.normal.xyz);
    n2 = CleanGiTransformRigidRouteVector(route, v2.normal.xyz);
    uv0 = v0.texCoord.xy; uv1 = v1.texCoord.xy; uv2 = v2.texCoord.xy;
    c0 = v0.color; c1 = v1.color; c2 = v2.color;
    c20 = v0.color2; c21 = v1.color2; c22 = v2.color2;
    return true;
}

bool CleanGiLoadTriangleGeometry(
    uint instanceId,
    uint primitiveIndex,
    out float3 p0, out float3 p1, out float3 p2,
    out float2 uv0, out float2 uv1, out float2 uv2)
{
    p0 = float3(0.0, 0.0, 0.0);
    p1 = float3(0.0, 0.0, 0.0);
    p2 = float3(0.0, 0.0, 0.0);
    uv0 = float2(0.0, 0.0);
    uv1 = float2(0.0, 0.0);
    uv2 = float2(0.0, 0.0);

    if (instanceId == 0u)
    {
        const uint vertexCount = (uint)max(CleanRtxdiDiGeometryInfo0.x, 0.0);
        const uint indexCount = (uint)max(CleanRtxdiDiGeometryInfo0.y, 0.0);
        const uint triangleCount = (uint)max(CleanRtxdiDiGeometryInfo0.z, 0.0);
        const uint indexOffset = primitiveIndex * 3u;
        if (primitiveIndex >= triangleCount || indexOffset + 2u >= indexCount)
        {
            return false;
        }
        const uint i0 = SmokeStaticIndices[indexOffset + 0u];
        const uint i1 = SmokeStaticIndices[indexOffset + 1u];
        const uint i2 = SmokeStaticIndices[indexOffset + 2u];
        if (i0 >= vertexCount || i1 >= vertexCount || i2 >= vertexCount)
        {
            return false;
        }
        const PathTraceSmokeVertex v0 = SmokeStaticVertices[i0];
        const PathTraceSmokeVertex v1 = SmokeStaticVertices[i1];
        const PathTraceSmokeVertex v2 = SmokeStaticVertices[i2];
        p0 = v0.position.xyz; p1 = v1.position.xyz; p2 = v2.position.xyz;
        uv0 = v0.texCoord.xy; uv1 = v1.texCoord.xy; uv2 = v2.texCoord.xy;
        return true;
    }
    if (instanceId == 1u)
    {
        const uint vertexCount = (uint)max(CleanRtxdiDiGeometryInfo0.w, 0.0);
        const uint indexCount = (uint)max(CleanRtxdiDiGeometryInfo1.x, 0.0);
        const uint triangleCount = (uint)max(CleanRtxdiDiGeometryInfo1.y, 0.0);
        const uint indexOffset = primitiveIndex * 3u;
        if (primitiveIndex >= triangleCount || indexOffset + 2u >= indexCount)
        {
            return false;
        }
        const uint i0 = SmokeDynamicIndices[indexOffset + 0u];
        const uint i1 = SmokeDynamicIndices[indexOffset + 1u];
        const uint i2 = SmokeDynamicIndices[indexOffset + 2u];
        if (i0 >= vertexCount || i1 >= vertexCount || i2 >= vertexCount)
        {
            return false;
        }
        const PathTraceSmokeVertex v0 = SmokeDynamicVertices[i0];
        const PathTraceSmokeVertex v1 = SmokeDynamicVertices[i1];
        const PathTraceSmokeVertex v2 = SmokeDynamicVertices[i2];
        p0 = v0.position.xyz; p1 = v1.position.xyz; p2 = v2.position.xyz;
        uv0 = v0.texCoord.xy; uv1 = v1.texCoord.xy; uv2 = v2.texCoord.xy;
        return true;
    }

    const uint routeInstanceIndex = instanceId - 2u;
    const uint routeInstanceCount = (uint)max(CleanRtxdiDiToyPathInfo.w, 0.0);
    if (routeInstanceIndex >= routeInstanceCount)
    {
        return false;
    }
    const PathTraceRigidRouteInstance route = SmokeRigidRouteInstances[routeInstanceIndex];
    const uint vertexCount = (uint)max(CleanRtxdiDiGeometryInfo1.z, 0.0);
    const uint indexCount = (uint)max(CleanRtxdiDiGeometryInfo1.w, 0.0);
    const uint indexOffset = route.indexOffset + primitiveIndex * 3u;
    if (primitiveIndex >= route.triangleCount || indexOffset + 2u >= indexCount)
    {
        return false;
    }
    const uint i0 = SmokeRigidRouteIndices[indexOffset + 0u];
    const uint i1 = SmokeRigidRouteIndices[indexOffset + 1u];
    const uint i2 = SmokeRigidRouteIndices[indexOffset + 2u];
    if (i0 >= route.vertexCount || i1 >= route.vertexCount || i2 >= route.vertexCount ||
        route.vertexOffset + i0 >= vertexCount || route.vertexOffset + i1 >= vertexCount || route.vertexOffset + i2 >= vertexCount)
    {
        return false;
    }
    const PathTraceSmokeVertex v0 = SmokeRigidRouteVertices[route.vertexOffset + i0];
    const PathTraceSmokeVertex v1 = SmokeRigidRouteVertices[route.vertexOffset + i1];
    const PathTraceSmokeVertex v2 = SmokeRigidRouteVertices[route.vertexOffset + i2];
    p0 = CleanGiTransformRigidRoutePoint(route, v0.position.xyz);
    p1 = CleanGiTransformRigidRoutePoint(route, v1.position.xyz);
    p2 = CleanGiTransformRigidRoutePoint(route, v2.position.xyz);
    uv0 = v0.texCoord.xy; uv1 = v1.texCoord.xy; uv2 = v2.texCoord.xy;
    return true;
}

bool CleanGiLoadHitTexCoord(uint instanceId, uint primitiveIndex, float2 barycentrics, out float2 texCoord)
{
    texCoord = float2(0.0, 0.0);
    float3 p0, p1, p2;
    float2 uv0, uv1, uv2;
    if (!CleanGiLoadTriangleGeometry(instanceId, primitiveIndex, p0, p1, p2, uv0, uv1, uv2))
    {
        return false;
    }
    const float b1 = saturate(barycentrics.x);
    const float b2 = saturate(barycentrics.y);
    const float b0 = saturate(1.0 - b1 - b2);
    texCoord = uv0 * b0 + uv1 * b1 + uv2 * b2;
    return true;
}

float CleanGiHashToUnitFloat(uint hash)
{
    hash ^= hash >> 16;
    hash *= 2246822519u;
    hash ^= hash >> 13;
    hash *= 3266489917u;
    hash ^= hash >> 16;
    return ((hash >> 8) & 0x00ffffffu) * (1.0 / 16777215.0);
}

float CleanGiVisibilityRandom(uint instanceId, uint primitiveIndex, uint salt)
{
    const uint2 pixel = DispatchRaysIndex().xy;
    return CleanGiHashToUnitFloat(
        instanceId * 1597334677u ^
        primitiveIndex * 3812015801u ^
        pixel.x * 2798796415u ^
        pixel.y * 1979697957u ^
        CleanRestirGiFrameIndex * 3266489917u ^
        salt);
}

bool CleanGiMaterialRejectsHit(uint instanceId, uint primitiveIndex, float2 barycentrics, bool shadowRay)
{
    const uint materialIndex = CleanGiLoadTriangleMaterialIndex(instanceId, primitiveIndex);
    if (materialIndex >= (uint)TextureInfo.z)
    {
        return false;
    }

    float3 p0, p1, p2;
    float3 n0, n1, n2;
    float2 uv0, uv1, uv2;
    float4 c0, c1, c2;
    float4 c20, c21, c22;
    if (!CleanGiLoadTriangleGeometryFull(
        instanceId,
        primitiveIndex,
        p0, p1, p2,
        n0, n1, n2,
        uv0, uv1, uv2,
        c0, c1, c2,
        c20, c21, c22))
    {
        return false;
    }
    const float b1 = saturate(barycentrics.x);
    const float b2 = saturate(barycentrics.y);
    const float b0 = saturate(1.0 - b1 - b2);
    const float2 texCoord = uv0 * b0 + uv1 * b1 + uv2 * b2;
    const float4 vertexColor = saturate(c0 * b0 + c1 * b1 + c2 * b2);

    const PathTraceSmokeMaterial material = CleanGiLoadSmokeMaterial(materialIndex);
    const uint triangleClassAndFlags = CleanGiLoadTriangleClassAndFlags(instanceId, primitiveIndex);
    const uint surfaceClass = CleanGiTriangleSurfaceClass(triangleClassAndFlags);
    const uint translucentSubtype = CleanGiTriangleTranslucentSubtype(triangleClassAndFlags);
    if (surfaceClass == RT_SMOKE_SURFACE_CLASS_TRANSLUCENT &&
        translucentSubtype == RT_SMOKE_TRANSLUCENT_SUBTYPE_GUI_SCREEN &&
        vertexColor.a <= 0.03)
    {
        return true;
    }

    const float coverage = saturate(CleanGiAlphaCoverage(material, texCoord));
    if ((material.flags & RT_SMOKE_MATERIAL_ALPHA_TEST) != 0u &&
        coverage < material.alphaCutoff)
    {
        return true;
    }

    float visibilityCoverage = coverage;
    if ((material.flags & RT_SMOKE_MATERIAL_ADDITIVE_DECAL) != 0u)
    {
        const float3 albedo = saturate(CleanGiSampleDiffuseTexture(material, texCoord).rgb);
        visibilityCoverage = (material.flags & RT_SMOKE_MATERIAL_ADDITIVE_DECAL_WHITE_KEY) != 0u
            ? 1.0 - min(min(albedo.r, albedo.g), albedo.b)
            : max(max(albedo.r, albedo.g), albedo.b);
        visibilityCoverage = saturate(visibilityCoverage * 0.5);
    }
    else if ((material.flags & RT_SMOKE_MATERIAL_FILTER_DECAL) != 0u)
    {
        const float3 albedo = saturate(CleanGiSampleDiffuseTexture(material, texCoord).rgb);
        const bool blackKey = (material.flags & RT_SMOKE_MATERIAL_FILTER_DECAL_BLACK_KEY) != 0u;
        visibilityCoverage = blackKey
            ? max(max(albedo.r, albedo.g), albedo.b)
            : 1.0 - min(min(albedo.r, albedo.g), albedo.b);
        visibilityCoverage = saturate(visibilityCoverage * 0.5);
    }

    const uint alphaMaterialFlags =
        RT_SMOKE_MATERIAL_ADDITIVE_DECAL |
        RT_SMOKE_MATERIAL_FILTER_DECAL |
        RT_SMOKE_MATERIAL_ALPHA_FROM_DIFFUSE_DARK_KEY |
        RT_SMOKE_MATERIAL_ALPHA_FROM_DIFFUSE_LUMA |
        RT_SMOKE_MATERIAL_ADDITIVE_DECAL_WHITE_KEY |
        RT_SMOKE_MATERIAL_ALPHA_FROM_DIFFUSE_MAGENTA_KEY;
    const bool alphaDriven =
        material.alphaTextureIndex != 0xffffffffu ||
        (material.flags & alphaMaterialFlags) != 0u;
    if (!alphaDriven)
    {
        return false;
    }
    if (visibilityCoverage <= 0.001)
    {
        return true;
    }
    if (visibilityCoverage >= 0.999)
    {
        return false;
    }

    const uint salt = shadowRay ? 0xa8f31b2du : 0x51d734bbu;
    return CleanGiVisibilityRandom(instanceId, primitiveIndex, salt) > visibilityCoverage;
}

bool CleanGiResolveTriangleNormalAndArea(
    float3 p0,
    float3 p1,
    float3 p2,
    float3 payloadNormal,
    out float3 triangleNormal,
    out float triangleArea)
{
    triangleNormal = float3(0.0, 0.0, 1.0);
    triangleArea = 0.0;
    if (!CleanGiAllFinite3(p0) || !CleanGiAllFinite3(p1) || !CleanGiAllFinite3(p2))
    {
        return false;
    }

    const float3 crossValue = cross(p1 - p0, p2 - p0);
    const float doubleAreaSquared = dot(crossValue, crossValue);
    if (doubleAreaSquared <= 1.0e-12)
    {
        return false;
    }

    const float doubleArea = sqrt(doubleAreaSquared);
    triangleNormal = crossValue / doubleArea;
    const float3 safePayloadNormal = CleanGiSafeNormalize(payloadNormal, triangleNormal);
    if (dot(triangleNormal, safePayloadNormal) < 0.0)
    {
        triangleNormal = -triangleNormal;
    }
    triangleArea = 0.5 * doubleArea;
    return triangleArea > 1.0e-6 && triangleArea < 3.402823e+38;
}

// ---------------------------------------------------------------------------
// Analytic light universe at the secondary vertex (GI-I-04)
// ---------------------------------------------------------------------------

bool CleanGiAnalyticPayloadValid(PathTraceDoomAnalyticLightCandidate light)
{
    const float3 radiance = max(light.colorAndIntensity.rgb, float3(0.0, 0.0, 0.0));
    const float luminance = CleanGiLuminance(radiance);
    return CleanGiAllFinite3(light.originAndRadius.xyz) &&
        CleanGiAllFinite3(radiance) &&
        luminance > 0.0 &&
        light.originAndRadius.w > 0.0 &&
        light.originAndRadius.w < 3.402823e+38 &&
        light.doomRadiusAndArea.x > 0.0 &&
        light.doomRadiusAndArea.x < 3.402823e+38;
}

RAB_LightInfo CleanGiBuildAnalyticLightInfo(PathTraceDoomAnalyticLightCandidate light, uint lightIndex)
{
    RAB_LightInfo lightInfo = RAB_EmptyLightInfo();
    if (!CleanGiAnalyticPayloadValid(light))
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
    lightInfo.weight = CleanGiLuminance(lightInfo.radiance) * lightInfo.area * lightInfo.influenceRadius;
    return lightInfo;
}

RAB_LightInfo CleanGiBuildRluAnalyticLightInfo(PathTraceUnifiedLightRecord light, uint lightIndex)
{
    RAB_LightInfo lightInfo = RAB_EmptyLightInfo();
    if (light.type != PATH_TRACE_UNIFIED_LIGHT_TYPE_DOOM_ANALYTIC ||
        !CleanGiAllFinite3(light.positionAndRadius.xyz) ||
        !CleanGiAllFinite3(light.radianceAndLuminance.rgb))
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
    lightInfo.weight = CleanGiLuminance(lightInfo.radiance) * lightInfo.area * lightInfo.influenceRadius;
    return lightInfo;
}

RAB_LightInfo CleanGiBuildEmissiveLightInfo(uint sourceIndex, uint lightIndex)
{
    RAB_LightInfo lightInfo = RAB_EmptyLightInfo();
    if (sourceIndex >= CleanRtxdiDiCurrentEmissiveTriangleCount)
    {
        return lightInfo;
    }

    const PathTraceSmokeEmissiveTriangle emissiveTriangle = SmokeEmissiveTriangles[sourceIndex];
    if (emissiveTriangle.materialIndex >= (uint)TextureInfo.z ||
        (emissiveTriangle.padding0 & RT_SMOKE_TRIANGLE_EMISSIVE_STAGE_OFF) != 0u)
    {
        return lightInfo;
    }

    const PathTraceSmokeMaterial emissiveMaterial = CleanGiLoadSmokeMaterial(emissiveTriangle.materialIndex);
    if ((emissiveMaterial.flags & RT_SMOKE_MATERIAL_EMISSIVE) == 0u)
    {
        return lightInfo;
    }
    const uint triangleClassAndFlags = CleanGiLoadTriangleClassAndFlags(emissiveTriangle.instanceId, emissiveTriangle.primitiveIndex);
    const uint surfaceClass = CleanGiTriangleSurfaceClass(triangleClassAndFlags);
    const bool activeEmissiveStage =
        (emissiveTriangle.padding0 & RT_SMOKE_TRIANGLE_EMISSIVE_STAGE_OFF) == 0u &&
        (triangleClassAndFlags & RT_SMOKE_TRIANGLE_EMISSIVE_STAGE_OFF) == 0u;

    float3 p0, p1, p2;
    float2 uv0, uv1, uv2;
    if (!CleanGiLoadTriangleGeometry(emissiveTriangle.instanceId, emissiveTriangle.primitiveIndex, p0, p1, p2, uv0, uv1, uv2))
    {
        return lightInfo;
    }

    float3 triangleNormal;
    float triangleArea;
    if (!CleanGiResolveTriangleNormalAndArea(p0, p1, p2, emissiveTriangle.normalAndLuminance.xyz, triangleNormal, triangleArea))
    {
        return lightInfo;
    }

    const float3 centroidRadiance = CleanGiSampleEmissiveRadiance(emissiveMaterial, emissiveTriangle.centroidUvAndWeight.xy, surfaceClass, activeEmissiveStage);
    if (CleanGiLuminance(centroidRadiance) <= 0.0)
    {
        return lightInfo;
    }

    lightInfo.lightType = RAB_LIGHT_TYPE_EMISSIVE_TRIANGLE;
    lightInfo.lightIndex = lightIndex;
    lightInfo.unifiedLightType = PATH_TRACE_UNIFIED_LIGHT_TYPE_EMISSIVE_TRIANGLE;
    lightInfo.materialIndex = emissiveTriangle.materialIndex;
    lightInfo.flags = emissiveTriangle.flags;
    lightInfo.position = emissiveTriangle.centerAndArea.xyz;
    lightInfo.radius = 0.0;
    lightInfo.normal = triangleNormal;
    lightInfo.radiance = centroidRadiance;
    lightInfo.influenceRadius = 0.0;
    lightInfo.area = triangleArea;
    lightInfo.weight = max(emissiveTriangle.sampleWeightAndPdf.x, CleanGiLuminance(centroidRadiance) * triangleArea);
    lightInfo.sourceIndex = sourceIndex;
    lightInfo.hasTriangleGeometry = 1u;
    lightInfo.emissiveTextureIndex = emissiveMaterial.emissiveTextureIndex;
    lightInfo.emissiveTextureWidth = emissiveMaterial.emissiveTextureWidth;
    lightInfo.emissiveTextureHeight = emissiveMaterial.emissiveTextureHeight;
    lightInfo.emissiveActiveStage = 1u;
    lightInfo.emissiveColor = max(emissiveMaterial.emissiveColor.rgb, float3(0.0, 0.0, 0.0));
    lightInfo.trianglePosition0 = p0;
    lightInfo.trianglePosition1 = p1;
    lightInfo.trianglePosition2 = p2;
    lightInfo.triangleUv0 = uv0;
    lightInfo.triangleUv1 = uv1;
    lightInfo.triangleUv2 = uv2;
    return lightInfo;
}

RAB_LightInfo CleanGiLoadCurrentRluLightInfo(uint denseRluIndex)
{
    if (denseRluIndex >= CleanRtxdiDiRluCurrentLightCount)
    {
        return RAB_EmptyLightInfo();
    }

    const PathTraceUnifiedLightRecord light = CleanRestirGiRluCurrentLights[denseRluIndex];
    if (light.type == PATH_TRACE_UNIFIED_LIGHT_TYPE_EMISSIVE_TRIANGLE)
    {
        if (light.sourceIndex == PATH_TRACE_UNIFIED_LIGHT_INVALID_INDEX)
        {
            return RAB_EmptyLightInfo();
        }
        return CleanGiBuildEmissiveLightInfo(light.sourceIndex, denseRluIndex);
    }
    if (light.type == PATH_TRACE_UNIFIED_LIGHT_TYPE_DOOM_ANALYTIC)
    {
        if (light.sourceIndex != PATH_TRACE_UNIFIED_LIGHT_INVALID_INDEX &&
            light.sourceIndex < CleanRtxdiDiDoomAnalyticFullCurrentCount)
        {
            return CleanGiBuildAnalyticLightInfo(DoomAnalyticLights[light.sourceIndex], denseRluIndex);
        }
        return CleanGiBuildRluAnalyticLightInfo(light, denseRluIndex);
    }
    return RAB_EmptyLightInfo();
}

bool CleanGiAccumulateLightSample(
    inout float3 radiance,
    RAB_Surface secondarySurface,
    float3 hitGeometricNormal,
    RAB_LightInfo lightInfo,
    float sourcePdf,
    inout RAB_RandomSamplerState rng)
{
    if (!RAB_IsLightInfoValid(lightInfo) || sourcePdf <= 1.0e-8)
    {
        return false;
    }

    const float2 uv = float2(RAB_GetNextRandom(rng), RAB_GetNextRandom(rng));
    const RAB_LightSample lightSample = RAB_SamplePolymorphicLight(lightInfo, secondarySurface, uv);
    if (!RAB_IsReplayableLightSample(lightSample) ||
        lightSample.solidAnglePdf <= 1.0e-8 ||
        CleanGiLuminance(lightSample.radiance) <= 0.0)
    {
        return false;
    }

    float3 lightDir;
    float lightDistance;
    RAB_GetLightDirDistance(secondarySurface, lightSample, lightDir, lightDistance);
    const float ndotl = saturate(dot(RAB_GetSurfaceNormal(secondarySurface), lightDir));
    if (ndotl <= 0.0)
    {
        return false;
    }

    const float3 brdf = RAB_EvaluateSurfaceBrdf(secondarySurface, lightDir, RAB_GetSurfaceViewDir(secondarySurface));
    if (CleanGiLuminance(brdf) <= 0.0)
    {
        return false;
    }

    const float visibility = CleanGiTraceVisibility(
        RAB_GetSurfaceWorldPos(secondarySurface), hitGeometricNormal, lightSample.position);
    if (visibility <= 0.0)
    {
        return false;
    }

    radiance += brdf * lightSample.radiance * ndotl * visibility /
        max(sourcePdf * lightSample.solidAnglePdf, 1.0e-6);
    return true;
}

bool CleanGiSelectEmissiveDistributionSample(
    inout RAB_RandomSamplerState rng,
    out uint sourceIndex,
    out float sourcePdf)
{
    sourceIndex = 0xffffffffu;
    sourcePdf = 0.0;

    const uint distributionCount = min((uint)max(CleanRtxdiDiEmissiveDistributionInfo.x, 0.0), CleanRtxdiDiCurrentEmissiveTriangleCount);
    if (CleanRtxdiDiEmissiveDistributionInfo.y >= 0.5 && distributionCount > 0u)
    {
        const float selector = RAB_GetNextRandom(rng);
        uint low = 0u;
        uint high = distributionCount;
        [loop]
        while (low < high)
        {
            const uint mid = low + ((high - low) >> 1u);
            const PathTraceEmissiveDistributionEntry entry = SmokeEmissiveDistribution[mid];
            if (selector <= saturate(entry.cumulativePdf))
            {
                high = mid;
            }
            else
            {
                low = mid + 1u;
            }
        }
        const uint entryIndex = min(low, distributionCount - 1u);
        const PathTraceEmissiveDistributionEntry entry = SmokeEmissiveDistribution[entryIndex];
        if (entry.emissiveTriangleIndex < CleanRtxdiDiCurrentEmissiveTriangleCount)
        {
            const float previousCdf = entryIndex > 0u ? saturate(SmokeEmissiveDistribution[entryIndex - 1u].cumulativePdf) : 0.0;
            const float currentCdf = max(saturate(entry.cumulativePdf), previousCdf);
            sourceIndex = entry.emissiveTriangleIndex;
            sourcePdf = max(currentCdf - previousCdf, 1.0e-6);
            return true;
        }
    }

    const uint emissiveCount = CleanRtxdiDiCurrentEmissiveTriangleCount;
    if (emissiveCount == 0u)
    {
        return false;
    }
    sourceIndex = min((uint)(RAB_GetNextRandom(rng) * (float)emissiveCount), emissiveCount - 1u);
    sourcePdf = 1.0 / max((float)emissiveCount, 1.0);
    return true;
}

bool CleanGiNeeCacheProviderReady()
{
    return (CleanRtxdiDiFlags & CLEAN_FLAG_NEE_CACHE_PROVIDER) != 0u &&
        CleanRtxdiDiNeeCacheInfo0.x >= 0.5 &&
        CleanRtxdiDiNeeCacheInfo1.z > 0.0 &&
        CleanRtxdiDiNeeCacheInfo1.w > 0.0 &&
        CleanRtxdiDiRluCurrentLightCount > 0u;
}

bool CleanGiAccumulateNeeCacheProviderSample(
    inout float3 radiance,
    RAB_Surface secondarySurface,
    float3 hitGeometricNormal,
    inout RAB_RandomSamplerState rng)
{
    if (!CleanGiNeeCacheProviderReady())
    {
        return false;
    }

    const PathTraceNeeCacheCellDebug cell = PathTraceNeeCacheMapWorldPositionToCell(
        RAB_GetSurfaceWorldPos(secondarySurface),
        CleanRtxdiDiCameraOriginAndValid.xyz,
        max((uint)max(CleanRtxdiDiNeeCacheInfo1.x, 1.0), 1u),
        max(CleanRtxdiDiNeeCacheInfo1.y, 1.0),
        max((uint)max(CleanRtxdiDiNeeCacheInfo1.z, 1.0), 1u));
    const uint cellCount = max((uint)max(CleanRtxdiDiNeeCacheInfo1.z, 1.0), 1u);
    if (cell.valid == 0u || cell.cellIndex >= cellCount)
    {
        return false;
    }

    const PathTraceNeeCacheProviderResult providerResult = CleanRestirGiNeeCacheProviderResults[cell.cellIndex];
    if (providerResult.flags == 0u ||
        providerResult.selectedDenseRluIndex >= CleanRtxdiDiRluCurrentLightCount ||
        providerResult.sourcePdf <= 1.0e-8)
    {
        return false;
    }

    const RAB_LightInfo lightInfo = CleanGiLoadCurrentRluLightInfo(providerResult.selectedDenseRluIndex);
    return CleanGiAccumulateLightSample(
        radiance,
        secondarySurface,
        hitGeometricNormal,
        lightInfo,
        providerResult.sourcePdf,
        rng);
}

// ---------------------------------------------------------------------------
// Visibility ray from an arbitrary surface point (secondary vertex NEE)
// ---------------------------------------------------------------------------

float CleanGiTraceVisibility(float3 fromPosition, float3 geometricNormal, float3 toPosition)
{
    const float3 toSample = toPosition - fromPosition;
    const float distanceSquared = dot(toSample, toSample);
    if (distanceSquared <= 1.0e-6)
    {
        return 0.0;
    }
    const float distance = sqrt(distanceSquared);
    const float3 direction = toSample / distance;
    if (dot(geometricNormal, direction) <= 0.0)
    {
        return 0.0;
    }

    RayDesc shadowRay;
    shadowRay.Origin = fromPosition + geometricNormal * 0.75 + direction * 0.25;
    shadowRay.Direction = direction;
    shadowRay.TMin = 0.01;
    shadowRay.TMax = max(distance - 0.5, 0.01);

    PathTraceCleanRestirGiPayload shadowPayload = (PathTraceCleanRestirGiPayload)0;
    shadowPayload.rayMode = 1u;
    shadowPayload.ignoreInstanceId = 0xffffffffu;
    shadowPayload.ignorePrimitiveIndex = 0xffffffffu;
    shadowPayload.ignoreMaterialIndex = 0xffffffffu;
    const uint rayFlags = RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_FORCE_NON_OPAQUE;
    TraceRay(SmokeScene, rayFlags, 0xff, 1, 1, 1, shadowRay, shadowPayload);
    return shadowPayload.value == 0u ? 1.0 : 0.0;
}

// ---------------------------------------------------------------------------
// Producer (RGI-02)
// ---------------------------------------------------------------------------

struct CleanGiProducerResult
{
    uint valid;          // 1 when the bounce ray hit a usable surface
    float3 radiance;     // L_in / solidAnglePdf (excludes primary BSDF/albedo)
    float pathLength;    // indirect path length (hitT)
    float3 hitPosition;
    float3 hitNormal;
    float3 materialAlbedo;
    float materialOpacity;
    uint materialFlags;
    uint diffuseTextureIndex;
};

// Shades the secondary vertex: its own emissive plus one direct-light proposal.
// The NEE cache provider is the preferred proposal source when ready; otherwise
// the producer falls back to one analytic and one emissive sample.
float3 CleanGiShadeSecondaryVertex(
    RAB_Surface secondarySurface,
    float3 hitGeometricNormal,
    float3 secondaryEmissive,
    inout RAB_RandomSamplerState rng)
{
    float3 radiance = secondaryEmissive;

    if (CleanGiAccumulateNeeCacheProviderSample(radiance, secondarySurface, hitGeometricNormal, rng))
    {
        return radiance;
    }

    const uint lightCount = CleanRtxdiDiAnalyticLightCount;
    if (lightCount > 0u)
    {
        const uint lightIndex = min((uint)(RAB_GetNextRandom(rng) * lightCount), lightCount - 1u);
        const RAB_LightInfo lightInfo = CleanGiBuildAnalyticLightInfo(DoomAnalyticLights[lightIndex], lightIndex);
        CleanGiAccumulateLightSample(
            radiance,
            secondarySurface,
            hitGeometricNormal,
            lightInfo,
            1.0 / max((float)lightCount, 1.0),
            rng);
    }

    uint emissiveSourceIndex;
    float emissiveSourcePdf;
    if (CleanGiSelectEmissiveDistributionSample(rng, emissiveSourceIndex, emissiveSourcePdf))
    {
        const RAB_LightInfo emissiveInfo = CleanGiBuildEmissiveLightInfo(emissiveSourceIndex, emissiveSourceIndex);
        CleanGiAccumulateLightSample(
            radiance,
            secondarySurface,
            hitGeometricNormal,
            emissiveInfo,
            emissiveSourcePdf,
            rng);
    }

    return radiance;
}

CleanGiProducerResult CleanGiRunProducer(uint2 pixel, PathTracePrimarySurfaceRecord record, inout RAB_RandomSamplerState rng)
{
    CleanGiProducerResult result = (CleanGiProducerResult)0;

    const float3 primaryPosition = record.worldPositionAndViewDepth.xyz;
    const float3 primaryShadingNormal = CleanGiSafeNormalize(record.shadingNormalAndOpacity.xyz, record.geometricNormalAndRoughness.xyz);
    const float3 primaryGeometricNormal = CleanGiSafeNormalize(record.geometricNormalAndRoughness.xyz, primaryShadingNormal);

    // Cosine-weighted bounce direction around the primary shading normal;
    // solidAnglePdf = ndotl / pi (the shared diffuse BRDF sampling model).
    const float2 randomValues = float2(RAB_GetNextRandom(rng), RAB_GetNextRandom(rng));
    const float3 bounceDir = RAB_CosineHemisphereDirection(primaryShadingNormal, randomValues);
    const float bounceNdotL = dot(primaryShadingNormal, bounceDir);
    if (bounceNdotL <= 0.0 || dot(primaryGeometricNormal, bounceDir) <= 0.0)
    {
        return result;
    }
    const float solidAnglePdf = bounceNdotL / RTXDI_PI;

    RayDesc bounceRay;
    bounceRay.Origin = primaryPosition + primaryGeometricNormal * 0.5 + bounceDir * 0.25;
    bounceRay.Direction = bounceDir;
    bounceRay.TMin = 0.01;
    bounceRay.TMax = 100000.0;

    PathTraceCleanRestirGiPayload payload = (PathTraceCleanRestirGiPayload)0;
    payload.rayMode = 1u;
    payload.ignoreInstanceId = 0xffffffffu;
    payload.ignorePrimitiveIndex = 0xffffffffu;
    payload.ignoreMaterialIndex = 0xffffffffu;
    TraceRay(SmokeScene, RAY_FLAG_FORCE_NON_OPAQUE, 0xff, 0, 1, 0, bounceRay, payload);
    if (payload.value == 0u)
    {
        return result; // miss: zero radiance, invalid hit geometry
    }

    const float3 hitPosition = bounceRay.Origin + bounceDir * payload.hitT;

    float3 p0, p1, p2;
    float3 n0, n1, n2;
    float2 uv0, uv1, uv2;
    float4 c0, c1, c2;
    float4 c20, c21, c22;
    float3 hitGeometricNormal = -bounceDir;
    float3 hitShadingNormal = -bounceDir;
    float2 hitTexCoord = float2(0.0, 0.0);
    float4 hitVertexColor = float4(1.0, 1.0, 1.0, 1.0);
    if (CleanGiLoadTriangleGeometryFull(
        payload.hitInstanceId,
        payload.hitPrimitiveIndex,
        p0, p1, p2,
        n0, n1, n2,
        uv0, uv1, uv2,
        c0, c1, c2,
        c20, c21, c22))
    {
        const float3 crossValue = cross(p1 - p0, p2 - p0);
        hitGeometricNormal = CleanGiSafeNormalize(crossValue, -bounceDir);
        if (dot(hitGeometricNormal, bounceDir) > 0.0)
        {
            hitGeometricNormal = -hitGeometricNormal;
        }
        const float b1 = saturate(payload.hitBarycentrics.x);
        const float b2 = saturate(payload.hitBarycentrics.y);
        const float b0 = saturate(1.0 - b1 - b2);
        hitTexCoord = uv0 * b0 + uv1 * b1 + uv2 * b2;
        hitVertexColor = saturate(c0 * b0 + c1 * b1 + c2 * b2);

        const uint hitTriangleClassAndFlags = CleanGiLoadTriangleClassAndFlags(payload.hitInstanceId, payload.hitPrimitiveIndex);
        const bool forceGeometricNormal = (hitTriangleClassAndFlags & RT_SMOKE_TRIANGLE_FORCE_GEOMETRIC_NORMAL) != 0u;
        float3 interpolatedNormal = CleanGiSafeNormalize(n0 * b0 + n1 * b1 + n2 * b2, hitGeometricNormal);
        if (dot(interpolatedNormal, hitGeometricNormal) < 0.0)
        {
            interpolatedNormal = -interpolatedNormal;
        }
        hitShadingNormal = forceGeometricNormal ? hitGeometricNormal : interpolatedNormal;

        const float3 tangentFallback = CleanGiBuildPerpendicular(hitShadingNormal);
        const float3 bitangentFallback = CleanGiSafeNormalize(cross(hitShadingNormal, tangentFallback), float3(0.0, 1.0, 0.0));
        float3 hitTangent = tangentFallback;
        float3 hitBitangent = bitangentFallback;
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
            hitTangent = CleanGiSafeNormalize(rawTangent - hitShadingNormal * dot(hitShadingNormal, rawTangent), tangentFallback);
            hitBitangent = CleanGiSafeNormalize(rawBitangent - hitShadingNormal * dot(hitShadingNormal, rawBitangent) - hitTangent * dot(hitTangent, rawBitangent), bitangentFallback);
            if (dot(cross(hitTangent, hitBitangent), hitShadingNormal) < 0.0)
            {
                hitBitangent = -hitBitangent;
            }
        }

        const uint hitMaterialIndexForNormal = CleanGiLoadTriangleMaterialIndex(payload.hitInstanceId, payload.hitPrimitiveIndex);
        const PathTraceSmokeMaterial hitMaterialForNormal = CleanGiLoadSmokeMaterial(hitMaterialIndexForNormal);
        hitShadingNormal = CleanGiConstrainShadingNormal(
            CleanGiDecodeNormalTexture(hitMaterialForNormal, hitTexCoord, hitShadingNormal, hitTangent, hitBitangent),
            hitGeometricNormal);
    }

    const uint hitMaterialIndex = CleanGiLoadTriangleMaterialIndex(payload.hitInstanceId, payload.hitPrimitiveIndex);
    const uint hitMaterialId = CleanGiLoadTriangleMaterialId(payload.hitInstanceId, payload.hitPrimitiveIndex);
    const uint hitTriangleClassAndFlags = CleanGiLoadTriangleClassAndFlags(payload.hitInstanceId, payload.hitPrimitiveIndex);
    const uint hitSurfaceClass = CleanGiTriangleSurfaceClass(hitTriangleClassAndFlags);
    const uint hitTranslucentSubtype = CleanGiTriangleTranslucentSubtype(hitTriangleClassAndFlags);
    const PathTraceSmokeMaterial hitMaterial = CleanGiLoadSmokeMaterial(hitMaterialIndex);
    const RAB_Material hitRabMaterial = CleanGiBuildMaterialFromHit(
        hitMaterialId,
        hitMaterialIndex,
        hitMaterial,
        hitTexCoord,
        hitSurfaceClass,
        hitTranslucentSubtype,
        hitTriangleClassAndFlags,
        hitVertexColor);
    const float3 hitEmissive = hitRabMaterial.emissiveRadiance;

    RAB_Surface secondarySurface = RAB_EmptySurface();
    secondarySurface.valid = 1u;
    secondarySurface.worldPos = hitPosition;
    secondarySurface.linearDepth = payload.hitT;
    secondarySurface.geometryNormal = hitGeometricNormal;
    secondarySurface.shadingNormal = hitShadingNormal;
    secondarySurface.viewDir = -bounceDir;
    secondarySurface.materialId = hitMaterialId;
    secondarySurface.materialIndex = hitMaterialIndex;
    secondarySurface.instanceId = payload.hitInstanceId;
    secondarySurface.primitiveIndex = payload.hitPrimitiveIndex;
    secondarySurface.surfaceClass = hitSurfaceClass;
    secondarySurface.flags = hitTriangleClassAndFlags;
    secondarySurface.material = hitRabMaterial;

    const float3 outgoing = CleanGiShadeSecondaryVertex(secondarySurface, hitGeometricNormal, hitEmissive, rng);

    // Producer radiance contract: incoming radiance at the primary surface
    // divided by the bounce-direction solid-angle pdf; the primary BSDF and
    // albedo are excluded (applied by final shading).
    float3 producerRadiance = outgoing / max(solidAnglePdf, 1.0e-6);

    // Firefly clamp (initial samples only). Matches the Remix shape:
    // luminance clamp at threshold * 30.
    const float fireflyThreshold = CleanRestirGiFireflyThreshold;
    if (fireflyThreshold > 0.0)
    {
        const float clampLuminance = fireflyThreshold * CLEAN_RESTIR_GI_FIREFLY_FACTOR;
        const float luminance = CleanGiLuminance(producerRadiance);
        if (luminance > clampLuminance)
        {
            producerRadiance *= clampLuminance / max(luminance, 1.0e-6);
        }
    }

    if (!CleanGiAllFinite3(producerRadiance))
    {
        return result;
    }

    result.valid = 1u;
    result.radiance = max(producerRadiance, float3(0.0, 0.0, 0.0));
    result.pathLength = payload.hitT;
    result.hitPosition = hitPosition;
    result.hitNormal = hitShadingNormal;
    result.materialAlbedo = hitRabMaterial.diffuseAlbedo;
    result.materialOpacity = hitRabMaterial.opacity;
    result.materialFlags = hitMaterial.flags;
    result.diffuseTextureIndex = hitMaterial.diffuseTextureIndex;
    return result;
}

// RGI-03/RGI-04: drives the frozen temporal contract. Builds the initial
// reservoir from the producer textures (via the REMIX_RAB_* initial-sample
// callbacks), stores it to the INIT page, then runs RTXDI GI temporal
// resampling against the TEMPORAL_INPUT page and stores the TEMPORAL_OUTPUT
// page. With temporal disabled the initial reservoir passes through.
RemixRestirGITemporalReuseResult CleanGiRunTemporalContract(
    uint2 pixel,
    uint2 dimensions,
    bool surfaceValid,
    PathTracePrimarySurfaceRecord record)
{
    RAB_Surface surface = RAB_EmptySurface();
    if (surfaceValid)
    {
        surface = CleanGiMaterialSurfaceFromRecord(record);
    }

    RemixRestirGITemporalReuseDesc desc = (RemixRestirGITemporalReuseDesc)0;
    desc.pixel = pixel;
    desc.frameIndex = CleanRestirGiFrameIndex;
    desc.screenSpaceMotion = surfaceValid
        ? CleanGiLoadScreenSpaceMotion(pixel, record, dimensions)
        : float3(0.0, 0.0, 0.0);
    desc.initSamplePage = RemixRAB_GetGIInitSampleReservoirIndex();
    desc.temporalInputPage = RemixRAB_GetGITemporalInputReservoirIndex();
    desc.temporalOutputPage = RemixRAB_GetGITemporalOutputReservoirIndex();
    desc.activeCheckerboardField = 0u;
    desc.enableTemporalReuse = CleanRestirGiTemporalEnabled;
    // Remix-shaped reprojection thresholds: depth scales with view angle,
    // normal relaxes with roughness. When the receiver is actually moving,
    // including forward/back motion near the screen-space expansion center,
    // relax the gates to depth 0.28 / normal 0.8; without that depth-only
    // activation, view 7 shows a circular low-M hole during dolly movement.
    const float viewDotNormal = surfaceValid
        ? abs(dot(CleanGiSafeNormalize(record.viewDirectionAndReserved.xyz, float3(0.0, 0.0, 1.0)),
            CleanGiSafeNormalize(record.geometricNormalAndRoughness.xyz, float3(0.0, 0.0, 1.0))))
        : 1.0;
    float depthThreshold = 0.01 / max(viewDotNormal, 0.01);
    float normalThreshold = surfaceValid
        ? lerp(0.995, 0.5, saturate(record.geometricNormalAndRoughness.w))
        : 0.995;
    const bool temporalMotionActive =
        dot(desc.screenSpaceMotion.xy, desc.screenSpaceMotion.xy) > 0.25 ||
        abs(desc.screenSpaceMotion.z) > 0.25;
    if (temporalMotionActive)
    {
        depthThreshold = max(depthThreshold, 0.28);
        normalThreshold = min(normalThreshold, 0.8);
    }
    desc.depthThreshold = depthThreshold;
    desc.normalThreshold = normalThreshold;
    desc.maxHistoryLength = CleanRestirGiMaxHistoryLength;
    desc.enableFallbackSampling = 1u;
    desc.biasCorrectionMode = CleanRestirGiBiasCorrection;
    desc.maxReservoirAge = CleanRestirGiMaxReservoirAge;
    desc.enablePermutationSampling = 0u;
    desc.uniformRandomNumber = 0u;
    desc.fireflyFilteringLuminanceThreshold = CleanRestirGiFireflyThreshold;
    desc.enableLightingValidation = CleanRestirGiTemporalScreenValidation != 0.0 ? 1u : 0u;

    return RemixRestirGIRunTemporalReuseContract(surface, desc);
}

// ---------------------------------------------------------------------------
// RGI-05: NEE cache seed. Mirrors the Remix integrate_nee ReSTIR GI feed:
// build a GI sample from the cache-selected light at the PRIMARY surface
// (position/normal from the light sample, radiance = lightSample.radiance /
// (solidAnglePdf * cacheSourcePdf), visible samples only), stream it into an
// un-finalized reservoir (M=1, weightSum = wi), and store it on the INIT page
// before the temporal contract's initial RIS update merges it (M forced to 1
// at finalize, so this adds a candidate without an energy shift).
// ---------------------------------------------------------------------------

static const uint CLEAN_RESTIR_GI_NEE_SEED_RNG_PASS = 0x52525811u;

void CleanGiSeedInitPageFromNeeCache(uint2 pixel, bool surfaceValid, PathTracePrimarySurfaceRecord record)
{
    if (CleanRestirGiNeeCacheSeedEnabled == 0u)
    {
        return;
    }

    // The INIT page is transient: always overwrite it so a stale reservoir
    // from a previous frame can never masquerade as a seed.
    RTXDI_GIReservoir seeded = RTXDI_EmptyGIReservoir();

    if (surfaceValid && CleanGiNeeCacheProviderReady())
    {
        RAB_Surface surface = CleanGiMaterialSurfaceFromRecord(record);
        const PathTraceNeeCacheCellDebug cell = PathTraceNeeCacheMapWorldPositionToCell(
            RAB_GetSurfaceWorldPos(surface),
            CleanRtxdiDiCameraOriginAndValid.xyz,
            max((uint)max(CleanRtxdiDiNeeCacheInfo1.x, 1.0), 1u),
            max(CleanRtxdiDiNeeCacheInfo1.y, 1.0),
            max((uint)max(CleanRtxdiDiNeeCacheInfo1.z, 1.0), 1u));
        const uint cellCount = max((uint)max(CleanRtxdiDiNeeCacheInfo1.z, 1.0), 1u);
        if (cell.valid != 0u && cell.cellIndex < cellCount)
        {
            const PathTraceNeeCacheProviderResult providerResult = CleanRestirGiNeeCacheProviderResults[cell.cellIndex];
            if (providerResult.flags != 0u &&
                providerResult.selectedDenseRluIndex < CleanRtxdiDiRluCurrentLightCount &&
                providerResult.sourcePdf > 1.0e-8)
            {
                const RAB_LightInfo lightInfo = CleanGiLoadCurrentRluLightInfo(providerResult.selectedDenseRluIndex);
                if (RAB_IsLightInfoValid(lightInfo))
                {
                    RAB_RandomSamplerState rng = RAB_InitRandomSampler(pixel, CleanRestirGiFrameIndex, CLEAN_RESTIR_GI_NEE_SEED_RNG_PASS);
                    const float2 uv = float2(RAB_GetNextRandom(rng), RAB_GetNextRandom(rng));
                    const RAB_LightSample lightSample = RAB_SamplePolymorphicLight(lightInfo, surface, uv);
                    if (RAB_IsReplayableLightSample(lightSample) &&
                        lightSample.solidAnglePdf > 1.0e-8 &&
                        CleanGiLuminance(lightSample.radiance) > 0.0)
                    {
                        float3 lightDir;
                        float lightDistance;
                        RAB_GetLightDirDistance(surface, lightSample, lightDir, lightDistance);
                        const float ndotl = saturate(dot(RAB_GetSurfaceNormal(surface), lightDir));
                        const float visibility = ndotl > 0.0
                            ? CleanGiTraceVisibility(RAB_GetSurfaceWorldPos(surface), RAB_GetSurfaceGeoNormal(surface), lightSample.position)
                            : 0.0;
                        if (visibility > 0.0)
                        {
                            const float3 seedRadiance = min(
                                lightSample.radiance / max(lightSample.solidAnglePdf * providerResult.sourcePdf, 1.0e-6),
                                float3(65504.0, 65504.0, 65504.0));
                            const RTXDI_GIReservoir seedSample = RTXDI_MakeGIReservoir(
                                lightSample.position,
                                CleanGiSafeNormalize(lightSample.normal, -lightDir),
                                seedRadiance,
                                1.0);
                            const float wi = RemixRAB_GetGISampleTargetPdfForSurface(seedSample.position, seedSample.radiance, surface);
                            if (wi > 0.0)
                            {
                                RTXDI_CombineGIReservoirs(seeded, seedSample, 0.5, wi);
                            }
                        }
                    }
                }
            }
        }
    }

    RAB_StoreGIReservoir(seeded, int2(pixel), int(RemixRAB_GetGIInitSampleReservoirIndex()));
}

// ---------------------------------------------------------------------------
// RGI-07: final shading + resolve. Evaluates the primary diffuse lobe toward
// reservoir.position: the dedicated GI output stores the DEMODULATED indirect
// diffuse (cos/pi * radiance * W, albedo excluded, NRD-shaped like Remix);
// the resolve step re-applies albedo when adding into the combined outputs.
// ---------------------------------------------------------------------------

float3 CleanGiFinalShadeIndirectDiffuse(RAB_Surface surface, RTXDI_GIReservoir reservoir)
{
    if (!RAB_IsSurfaceValid(surface) || !RTXDI_IsValidGIReservoir(reservoir))
    {
        return float3(0.0, 0.0, 0.0);
    }
    const float weight = max(reservoir.weightSum, 0.0);
    if (weight <= 0.0 || !CleanGiAllFinite3(reservoir.radiance) || weight != weight)
    {
        return float3(0.0, 0.0, 0.0);
    }
    const float3 toSample = reservoir.position - RAB_GetSurfaceWorldPos(surface);
    const float distanceSquared = dot(toSample, toSample);
    if (distanceSquared <= 1.0e-6)
    {
        return float3(0.0, 0.0, 0.0);
    }
    const float3 sampleDir = toSample * rsqrt(distanceSquared);
    if (dot(sampleDir, RAB_GetSurfaceGeoNormal(surface)) <= 0.0)
    {
        return float3(0.0, 0.0, 0.0);
    }
    const float ndotl = saturate(dot(RAB_GetSurfaceNormal(surface), sampleDir));
    // Diffuse lobe sans albedo: brdf * cos = (albedo/pi) * ndotl, demodulated.
    const float3 indirect = max(reservoir.radiance, float3(0.0, 0.0, 0.0)) * weight * (ndotl / RTXDI_PI);
    return CleanGiAllFinite3(indirect) ? indirect : float3(0.0, 0.0, 0.0);
}

// Writes the GI-O-05 output. The boiling-filter compute pass consumes it,
// clamps outliers, and performs the resolve add into the combined outputs
// (so the beauty image receives the FILTERED contribution).
void CleanGiFinalShadingAndResolve(uint2 pixel, RAB_Surface surface, RTXDI_GIReservoir reservoir)
{
    const float3 indirectDiffuse = CleanGiFinalShadeIndirectDiffuse(surface, reservoir);
    CleanRestirGiIndirectDiffuse[pixel] = float4(indirectDiffuse, 1.0);
}

// ---------------------------------------------------------------------------
// Debug views
// ---------------------------------------------------------------------------

float3 CleanGiToneMap(float3 radiance)
{
    const float3 safeRadiance = max(radiance, float3(0.0, 0.0, 0.0));
    return safeRadiance / (float3(1.0, 1.0, 1.0) + safeRadiance);
}

// RGI-01 sentinel (view 8): GI lane state only; green pulse proves a packed-
// reservoir store/load round-trip through the INIT page; red = ABI failure.
float3 PathTraceCleanRestirGiSentinelColor(uint2 pixel)
{
    RTXDI_GIReservoir empty = RTXDI_EmptyGIReservoir();
    RAB_StoreGIReservoir(empty, int2(pixel), int(RemixRAB_GetGIInitSampleReservoirIndex()));
    RTXDI_GIReservoir loaded = RAB_LoadGIReservoir(int2(pixel), int(RemixRAB_GetGIInitSampleReservoirIndex()));
    const bool roundTripOk = !RTXDI_IsValidGIReservoir(loaded) && loaded.M == 0u && loaded.weightSum == 0.0;

    const uint band = ((pixel.x + pixel.y + CleanRestirGiFrameIndex) / 32u) & 1u;
    const float pulse = band != 0u ? 0.85 : 0.25;
    return roundTripOk ? float3(0.05, pulse, 0.25) : float3(1.0, 0.0, 0.0);
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

    const uint view = CleanRestirGiView;

    // ---- Phase 1: spatial reuse (separate dispatch; every pixel's
    // TEMPORAL_OUTPUT write from phase 0 has completed) ----
    if (CleanRestirGiPhase == 1u)
    {
        PathTracePrimarySurfaceRecord spatialRecord;
        const bool spatialSurfaceValid = CleanGiLoadSurfaceRecord(pixel, dimensions, spatialRecord);
        RAB_Surface spatialSurface = RAB_EmptySurface();
        if (spatialSurfaceValid)
        {
            spatialSurface = CleanGiMaterialSurfaceFromRecord(spatialRecord);
        }

        RTXDI_GIReservoir spatialInput = RAB_LoadGIReservoir(
            int2(pixel), int(RemixRAB_GetGITemporalOutputReservoirIndex()));
        RTXDI_GIReservoir spatialReservoir = spatialInput;
        if (CleanRestirGiSpatialEnabled != 0u)
        {
            spatialReservoir = CleanGiRunSpatialReuse(pixel, spatialSurface, spatialInput);
        }
        RAB_StoreGIReservoir(spatialReservoir, int2(pixel), int(RemixRAB_GetGISpatialOutputReservoirIndex()));

        // RGI-07: final shading + optional resolve from the spatial output.
        CleanGiFinalShadingAndResolve(pixel, spatialSurface, spatialReservoir);

        if (view == 6u)
        {
            // Isolated indirect diffuse (demodulated, no albedo, no DI).
            const float3 indirect = CleanGiFinalShadeIndirectDiffuse(spatialSurface, spatialReservoir);
            SmokeOutput[pixel] = float4(spatialSurfaceValid ? CleanGiToneMap(indirect) : float3(0.08, 0.08, 0.08), 1.0);
        }
        else if (view == 5u)
        {
            float3 spatialColor = float3(1.0, 0.0, 1.0);
            if (!spatialSurfaceValid)
            {
                spatialColor = float3(0.08, 0.08, 0.08);
            }
            else if (!RTXDI_IsValidGIReservoir(spatialReservoir))
            {
                spatialColor = float3(0.0, 0.0, 0.0);
            }
            else if (!CleanGiAllFinite3(spatialReservoir.radiance) ||
                spatialReservoir.weightSum != spatialReservoir.weightSum)
            {
                spatialColor = float3(1.0, 1.0, 0.0);
            }
            else
            {
                spatialColor = CleanGiToneMap(spatialReservoir.radiance * max(spatialReservoir.weightSum, 0.0));
            }
            SmokeOutput[pixel] = float4(spatialColor, 1.0);
        }
        return;
    }

    // ---- Producer stage (always runs while the lane is active) ----
    PathTracePrimarySurfaceRecord record;
    const bool surfaceValid = CleanGiLoadSurfaceRecord(pixel, dimensions, record);

    CleanGiProducerResult producer = (CleanGiProducerResult)0;
    if (surfaceValid)
    {
        RAB_RandomSamplerState rng = RAB_InitRandomSampler(pixel, CleanRestirGiFrameIndex, CLEAN_RESTIR_GI_PRODUCER_RNG_PASS);
        producer = CleanGiRunProducer(pixel, record, rng);
    }

    CleanRestirGiProducerRadiance[pixel] = float4(producer.radiance, producer.pathLength);
    CleanRestirGiProducerHitPosition[pixel] = float4(producer.hitPosition, producer.valid != 0u ? 1.0 : 0.0);
    CleanRestirGiProducerHitNormal[pixel] = float4(producer.hitNormal, 0.0);

    // ---- RGI-05 NEE cache seed (writes the INIT page pre-merge) ----
    CleanGiSeedInitPageFromNeeCache(pixel, surfaceValid, record);

    // ---- RGI-03/04 initial reservoir + temporal reuse (frozen contract) ----
    const RemixRestirGITemporalReuseResult temporalResult =
        CleanGiRunTemporalContract(pixel, dimensions, surfaceValid, record);
    const RTXDI_GIReservoir initialReservoir = temporalResult.initialReservoir;
    const RTXDI_GIReservoir temporalReservoir = temporalResult.temporalReservoir;

    // Spatial disabled: pass the temporal output through to the spatial page
    // here so the SPATIAL_OUTPUT page is always this frame's data and view 5
    // reproduces view 4 exactly. With spatial enabled, phase 1 writes it and
    // also owns final shading + resolve (RGI-07).
    if (CleanRestirGiSpatialEnabled == 0u)
    {
        RAB_StoreGIReservoir(temporalReservoir, int2(pixel), int(RemixRAB_GetGISpatialOutputReservoirIndex()));
        RAB_Surface resolveSurface = RAB_EmptySurface();
        if (surfaceValid)
        {
            resolveSurface = CleanGiMaterialSurfaceFromRecord(record);
        }
        CleanGiFinalShadingAndResolve(pixel, resolveSurface, temporalReservoir);
    }

    // ---- Debug views (GI lane resources only) ----
    if (view == 0u)
    {
        return;
    }

    float3 color = float3(1.0, 0.0, 1.0); // loud magenta: unimplemented view
    if (view == 1u)
    {
        // Producer radiance. Invalid primary surface = dark gray; miss =
        // black; NaN guard = loud yellow (validation matrix RGI-02).
        if (!surfaceValid)
        {
            color = float3(0.08, 0.08, 0.08);
        }
        else if (producer.valid == 0u)
        {
            color = float3(0.0, 0.0, 0.0);
        }
        else if (!CleanGiAllFinite3(producer.radiance))
        {
            color = float3(1.0, 1.0, 0.0);
        }
        else
        {
            color = CleanGiToneMap(producer.radiance);
        }
    }
    else if (view == 2u)
    {
        // Producer hit geometry bands: alternating world-position and normal
        // bands; invalid hit = dark red, invalid surface = dark gray.
        if (!surfaceValid)
        {
            color = float3(0.08, 0.08, 0.08);
        }
        else if (producer.valid == 0u)
        {
            color = float3(0.25, 0.0, 0.0);
        }
        else
        {
            const uint band = ((pixel.x / 64u) + (pixel.y / 64u)) & 1u;
            color = band != 0u
                ? saturate(producer.hitNormal * 0.5 + 0.5)
                : frac(producer.hitPosition / 128.0);
        }
    }
    else if (view == 3u)
    {
        // Initial-reservoir radiance after the single-sample RIS update.
        // This should match view 1 structurally; W only changes intensity.
        if (!surfaceValid)
        {
            color = float3(0.08, 0.08, 0.08);
        }
        else if (!RTXDI_IsValidGIReservoir(initialReservoir))
        {
            color = float3(0.0, 0.0, 0.0);
        }
        else if (!CleanGiAllFinite3(initialReservoir.radiance) ||
            initialReservoir.weightSum != initialReservoir.weightSum)
        {
            color = float3(1.0, 1.0, 0.0);
        }
        else
        {
            color = CleanGiToneMap(initialReservoir.radiance * max(initialReservoir.weightSum, 0.0));
        }
    }
    else if (view == 4u)
    {
        // Temporal output radiance * W. Static camera: converges over frames
        // vs view 3; temporal cvar 0 must reproduce view 3 exactly.
        if (!surfaceValid)
        {
            color = float3(0.08, 0.08, 0.08);
        }
        else if (!RTXDI_IsValidGIReservoir(temporalReservoir))
        {
            color = float3(0.0, 0.0, 0.0);
        }
        else if (!CleanGiAllFinite3(temporalReservoir.radiance) ||
            temporalReservoir.weightSum != temporalReservoir.weightSum)
        {
            color = float3(1.0, 1.0, 0.0);
        }
        else
        {
            color = CleanGiToneMap(temporalReservoir.radiance * max(temporalReservoir.weightSum, 0.0));
        }
    }
    else if (view == 5u)
    {
        if (CleanRestirGiSpatialEnabled != 0u)
        {
            // Phase 1 owns the view-5 output when spatial actually runs.
            return;
        }
        // Pass-through proof: identical to view 4 by construction.
        if (!surfaceValid)
        {
            color = float3(0.08, 0.08, 0.08);
        }
        else if (!RTXDI_IsValidGIReservoir(temporalReservoir))
        {
            color = float3(0.0, 0.0, 0.0);
        }
        else
        {
            color = CleanGiToneMap(temporalReservoir.radiance * max(temporalReservoir.weightSum, 0.0));
        }
    }
    else if (view == 6u)
    {
        if (CleanRestirGiSpatialEnabled != 0u)
        {
            // Phase 1 owns the view-6 output when spatial actually runs.
            return;
        }
        if (!surfaceValid)
        {
            color = float3(0.08, 0.08, 0.08);
        }
        else
        {
            RAB_Surface viewSurface = CleanGiMaterialSurfaceFromRecord(record);
            color = CleanGiToneMap(CleanGiFinalShadeIndirectDiffuse(viewSurface, temporalReservoir));
        }
    }
    else if (view == 7u)
    {
        // Reservoir M / age diagnostics: green ramp = M / maxHistoryLength,
        // red ramp = age / maxReservoirAge, blue marks invalid reservoirs.
        if (!surfaceValid)
        {
            color = float3(0.08, 0.08, 0.08);
        }
        else if (!RTXDI_IsValidGIReservoir(temporalReservoir))
        {
            color = float3(0.0, 0.0, 0.35);
        }
        else
        {
            const float mRamp = saturate((float)temporalReservoir.M / max((float)CleanRestirGiMaxHistoryLength, 1.0));
            const float ageRamp = saturate((float)temporalReservoir.age / max((float)CleanRestirGiMaxReservoirAge, 1.0));
            color = float3(ageRamp, mRamp, 0.0);
        }
    }
    else if (view == 8u)
    {
        color = PathTraceCleanRestirGiSentinelColor(pixel);
    }
    else if (view == 9u)
    {
        // Secondary-hit material proof: sampled diffuse/classifier albedo.
        // Invalid primary surface = dark gray; bounce miss = dark red.
        if (!surfaceValid)
        {
            color = float3(0.08, 0.08, 0.08);
        }
        else if (producer.valid == 0u)
        {
            color = float3(0.25, 0.0, 0.0);
        }
        else
        {
            color = saturate(producer.materialAlbedo);
        }
    }
    else if (view == 10u)
    {
        // Secondary-hit material source: green = has diffuse texture slot,
        // red = diffuse fallback/debug albedo, blue = forced debug albedo flag.
        if (!surfaceValid)
        {
            color = float3(0.08, 0.08, 0.08);
        }
        else if (producer.valid == 0u)
        {
            color = float3(0.25, 0.0, 0.0);
        }
        else
        {
            const bool hasDiffuseTexture = producer.diffuseTextureIndex != 0xffffffffu;
            const bool forceDebugAlbedo = (producer.materialFlags & RT_SMOKE_MATERIAL_FORCE_DEBUG_ALBEDO) != 0u;
            color = float3(hasDiffuseTexture ? 0.0 : 1.0, hasDiffuseTexture ? 1.0 : 0.0, forceDebugAlbedo ? 1.0 : 0.0);
        }
    }
    SmokeOutput[pixel] = float4(color, 1.0);
}

[shader("miss")]
void Miss(inout PathTraceCleanRestirGiPayload payload)
{
    payload.value = 0u;
}

[shader("miss")]
void ShadowMiss(inout PathTraceCleanRestirGiPayload payload)
{
    payload.value = 0u;
}

[shader("anyhit")]
void AnyHit(inout PathTraceCleanRestirGiPayload payload, BuiltInTriangleIntersectionAttributes attributes)
{
    if (CleanGiMaterialRejectsHit(InstanceID(), PrimitiveIndex(), attributes.barycentrics, false))
    {
        IgnoreHit();
    }
}

[shader("anyhit")]
void ShadowAnyHit(inout PathTraceCleanRestirGiPayload payload, BuiltInTriangleIntersectionAttributes attributes)
{
    if (CleanGiMaterialRejectsHit(InstanceID(), PrimitiveIndex(), attributes.barycentrics, true))
    {
        IgnoreHit();
    }
}

[shader("closesthit")]
void ClosestHit(inout PathTraceCleanRestirGiPayload payload, BuiltInTriangleIntersectionAttributes attributes)
{
    payload.value = 1u;
    payload.hitInstanceId = InstanceID();
    payload.hitPrimitiveIndex = PrimitiveIndex();
    payload.hitT = RayTCurrent();
    payload.hitBarycentrics = attributes.barycentrics;
}

[shader("closesthit")]
void ShadowClosestHit(inout PathTraceCleanRestirGiPayload payload, BuiltInTriangleIntersectionAttributes attributes)
{
    payload.value = 1u;
}
