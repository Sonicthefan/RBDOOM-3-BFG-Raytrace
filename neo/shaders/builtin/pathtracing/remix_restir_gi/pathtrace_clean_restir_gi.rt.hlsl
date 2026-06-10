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
// light universe, secondary vertex only), GI-I-05 (TLAS rays), GI-I-09
// (parameters). Writes GI-O-01 (producer textures), GI-O-06 (debug views).
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

#include "../pathtrace_material_classifier.hlsli"

VK_IMAGE_FORMAT("rgba32f") RWTexture2D<float4> SmokeOutput : register(u1);
RaytracingAccelerationStructure SmokeScene : register(t0);
StructuredBuffer<PathTraceSmokeVertex> SmokeStaticVertices : register(t3);
StructuredBuffer<uint> SmokeStaticIndices : register(t4);
StructuredBuffer<PathTraceSmokeVertex> SmokeDynamicVertices : register(t6);
StructuredBuffer<uint> SmokeDynamicIndices : register(t7);
StructuredBuffer<uint> SmokeStaticTriangleMaterialIndexes : register(t11);
StructuredBuffer<uint> SmokeDynamicTriangleMaterialIndexes : register(t12);
StructuredBuffer<PathTraceSmokeMaterial> SmokeMaterials : register(t13);
Texture2D<float4> SmokeFallbackTexture : register(t14);
StructuredBuffer<PathTraceSmokeEmissiveTriangle> SmokeEmissiveTriangles : register(t16);
StructuredBuffer<PathTraceSmokeVertex> SmokeRigidRouteVertices : register(t22);
StructuredBuffer<uint> SmokeRigidRouteIndices : register(t23);
StructuredBuffer<uint> SmokeRigidRouteTriangleMaterialIndexes : register(t25);
StructuredBuffer<PathTraceRigidRouteInstance> SmokeRigidRouteInstances : register(t26);
StructuredBuffer<PathTraceDoomAnalyticLightCandidate> DoomAnalyticLights : register(t27);
RWStructuredBuffer<PathTracePrimarySurfaceRecord> PrimarySurfaceHistoryCurrent : register(u30);
RWStructuredBuffer<PathTracePrimarySurfaceRecord> PrimarySurfaceHistoryPrevious : register(u31);
VK_IMAGE_FORMAT("rgba16f") RWTexture2D<float4> PathTraceMotionVectors : register(u39);
VK_IMAGE_FORMAT("r32ui") RWTexture2D<uint> PathTraceMotionVectorMask : register(u40);
RWStructuredBuffer<RTXDI_PackedGIReservoir> RemixRAB_GIReservoirs : register(u80);
VK_IMAGE_FORMAT("rgba16f") RWTexture2D<float4> CleanRestirGiProducerRadiance : register(u81);
VK_IMAGE_FORMAT("rgba32f") RWTexture2D<float4> CleanRestirGiProducerHitPosition : register(u82);
VK_IMAGE_FORMAT("rgba16f") RWTexture2D<float4> CleanRestirGiProducerHitNormal : register(u83);
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
    uint CleanRestirGiPad0;
    uint CleanRestirGiPad1;
    RTXDI_ReservoirBufferParameters RemixRAB_GIReservoirParams;
    uint4 RemixRAB_GIReservoirPageInfo;
};

static const uint RT_SMOKE_TRIANGLE_EMISSIVE_STAGE_OFF = 0x00040000u;
static const uint RT_SMOKE_MATERIAL_DIFFUSE_YCOCG = 0x00000002u;
static const uint RT_SMOKE_MATERIAL_ADDITIVE_DECAL = 0x00000004u;
static const uint RT_SMOKE_MATERIAL_EMISSIVE = 0x00000008u;
static const uint RT_SMOKE_MATERIAL_FILTER_DECAL = 0x00000010u;
static const uint RT_SMOKE_MATERIAL_ALPHA_FROM_DIFFUSE_LUMA = 0x00000040u;
static const uint RT_SMOKE_MATERIAL_FORCE_DEBUG_ALBEDO = 0x00000080u;
static const uint RT_SMOKE_MATERIAL_PORTAL_WINDOW_FALLBACK = 0x00000200u;
static const uint RT_SMOKE_MATERIAL_OBJECT_GLASS_FALLBACK = 0x00000400u;
static const uint RT_SMOKE_MATERIAL_ADDITIVE_DECAL_WHITE_KEY = 0x00000800u;
static const uint RT_SMOKE_MATERIAL_ALPHA_FROM_DIFFUSE_MAGENTA_KEY = 0x00001000u;
static const uint RT_SMOKE_TEXTURE_FLAG_USE_EMISSIVE_MAPS = 0x00000020u;
static const uint RT_SMOKE_TEXTURE_FLAG_RESERVOIR_TWO_SIDED_EMISSIVES = 0x00000040u;
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
#define RB_RAB_LIGHT_SAMPLING_CORE_ONLY 1
#define RB_RAB_CLEAN_RTXDI_DI_SENTINEL 1
#include "../RtxdiBridge/RAB_UnifiedLightRecord.hlsli"
#include "../RtxdiBridge/RAB_LightTarget.hlsli"

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

// Baseline has no gradient/lighting validation; pass the resampled reservoir
// through (stale energy is bounded by maxReservoirAge).
RemixRestirGITemporalValidationResult RemixRAB_ValidateGITemporalReservoir(
    RAB_Surface currentSurface,
    RTXDI_GIReservoir inputReservoir,
    RTXDI_GIReservoir resultReservoir,
    RemixRestirGITemporalValidationDesc desc)
{
    RemixRestirGITemporalValidationResult result = (RemixRestirGITemporalValidationResult)0;
    result.reservoir = resultReservoir;
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

float3 CleanGiLoadScreenSpaceMotion(uint2 pixel, PathTracePrimarySurfaceRecord currentSurface, uint2 dimensions)
{
    if (pixel.x < CleanRtxdiDiWidth && pixel.y < CleanRtxdiDiHeight)
    {
        const uint motionMask = PathTraceMotionVectorMask[pixel];
        if ((motionMask & PT_MOTION_VECTOR_MASK_VALID) != 0u)
        {
            const float3 motion = PathTraceMotionVectors[pixel].xyz;
            if (all(motion == motion))
            {
                return motion;
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
    const uint materialCount = (uint)TextureInfo.z;
    if (materialIndex < materialCount)
    {
        material = SmokeMaterials[materialIndex];
    }
    return material;
}

bool CleanGiMaterialDoesNotOccludeVisibility(uint materialIndex)
{
    if (materialIndex >= (uint)TextureInfo.z)
    {
        return false;
    }
    const PathTraceSmokeMaterial material = CleanGiLoadSmokeMaterial(materialIndex);
    const uint transparentCardFlags =
        RT_SMOKE_MATERIAL_ADDITIVE_DECAL |
        RT_SMOKE_MATERIAL_FILTER_DECAL |
        RT_SMOKE_MATERIAL_ALPHA_FROM_DIFFUSE_LUMA |
        RT_SMOKE_MATERIAL_PORTAL_WINDOW_FALLBACK |
        RT_SMOKE_MATERIAL_OBJECT_GLASS_FALLBACK |
        RT_SMOKE_MATERIAL_ADDITIVE_DECAL_WHITE_KEY |
        RT_SMOKE_MATERIAL_ALPHA_FROM_DIFFUSE_MAGENTA_KEY;
    return (material.flags & transparentCardFlags) != 0u;
}

// ---------------------------------------------------------------------------
// Texture sampling for the secondary-hit material (same decode rules as the
// primary-surface producer)
// ---------------------------------------------------------------------------

float4 CleanGiTextureLoad(uint textureIndex, uint textureWidth, uint textureHeight, float2 wrappedTexCoord, bool bindlessEnabled, bool bilinearFilter)
{
    if (textureWidth == 0u || textureHeight == 0u)
    {
        return float4(1.0, 1.0, 1.0, 1.0);
    }
    if (!bilinearFilter)
    {
        const uint2 texel = min((uint2)floor(wrappedTexCoord * float2(textureWidth, textureHeight)), uint2(textureWidth - 1u, textureHeight - 1u));
        return bindlessEnabled
            ? SmokeDiffuseTextures[NonUniformResourceIndex(textureIndex)].Load(int3(texel, 0))
            : SmokeFallbackTexture.Load(int3(0, 0, 0));
    }
    const float2 scaled = wrappedTexCoord * float2(textureWidth, textureHeight) - float2(0.5, 0.5);
    const int2 baseTexel = (int2)floor(scaled);
    const float2 fracPart = frac(scaled);
    const uint2 texel00 = uint2((baseTexel.x % (int)textureWidth + (int)textureWidth) % (int)textureWidth, (baseTexel.y % (int)textureHeight + (int)textureHeight) % (int)textureHeight);
    const uint2 texel10 = uint2((texel00.x + 1u) % textureWidth, texel00.y);
    const uint2 texel01 = uint2(texel00.x, (texel00.y + 1u) % textureHeight);
    const uint2 texel11 = uint2((texel00.x + 1u) % textureWidth, (texel00.y + 1u) % textureHeight);
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

float3 CleanGiSampleDiffuseAlbedo(PathTraceSmokeMaterial material, float2 texCoord)
{
    if ((material.flags & RT_SMOKE_MATERIAL_FORCE_DEBUG_ALBEDO) != 0u)
    {
        return saturate(material.debugAlbedo.rgb);
    }
    float4 texel = CleanGiSampleTexture(material.diffuseTextureIndex, material.textureWidth, material.textureHeight, texCoord, material.debugAlbedo);
    const bool textureDecodeEnabled = (((uint)TextureInfo.w) & 4u) != 0u;
    if (textureDecodeEnabled && (material.flags & RT_SMOKE_MATERIAL_DIFFUSE_YCOCG) != 0u)
    {
        texel.rgb = CleanGiConvertYCoCgToRGB(texel);
    }
    return saturate(texel.rgb);
}

float3 CleanGiSampleEmissiveRadiance(PathTraceSmokeMaterial material, float2 texCoord)
{
    if ((material.flags & RT_SMOKE_MATERIAL_EMISSIVE) == 0u)
    {
        return float3(0.0, 0.0, 0.0);
    }
    const float emissiveScale = max(ToyPathInfo.z, 0.0);
    if (emissiveScale <= 0.0)
    {
        return float3(0.0, 0.0, 0.0);
    }
    const uint textureFlags = (uint)TextureInfo.w;
    const bool useEmissiveMaps = (textureFlags & RT_SMOKE_TEXTURE_FLAG_USE_EMISSIVE_MAPS) != 0u;
    float3 emissiveTexel = float3(1.0, 1.0, 1.0);
    if (useEmissiveMaps && material.emissiveTextureIndex != 0xffffffffu)
    {
        emissiveTexel = saturate(CleanGiSampleTexture(material.emissiveTextureIndex, material.emissiveTextureWidth, material.emissiveTextureHeight, texCoord, float4(1.0, 1.0, 1.0, 1.0)).rgb);
    }
    return max(material.emissiveColor.rgb, float3(0.0, 0.0, 0.0)) * emissiveTexel * (1.75 * emissiveScale);
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
};

// Shades the secondary vertex: its own emissive plus one NEE sample from the
// analytic light universe (uniform pick), using the shared diffuse BRDF.
float3 CleanGiShadeSecondaryVertex(
    RAB_Surface secondarySurface,
    float3 hitGeometricNormal,
    float3 secondaryEmissive,
    inout RAB_RandomSamplerState rng)
{
    float3 radiance = secondaryEmissive;

    const uint lightCount = CleanRtxdiDiAnalyticLightCount;
    if (lightCount == 0u)
    {
        return radiance;
    }

    const uint lightIndex = min((uint)(RAB_GetNextRandom(rng) * lightCount), lightCount - 1u);
    const float inversePickPdf = (float)lightCount;
    const RAB_LightInfo lightInfo = CleanGiBuildAnalyticLightInfo(DoomAnalyticLights[lightIndex], lightIndex);
    if (!RAB_IsLightInfoValid(lightInfo))
    {
        return radiance;
    }

    const float2 uv = float2(RAB_GetNextRandom(rng), RAB_GetNextRandom(rng));
    const RAB_LightSample lightSample = RAB_SamplePolymorphicLight(lightInfo, secondarySurface, uv);
    if (!RAB_IsReplayableLightSample(lightSample) || CleanGiLuminance(lightSample.radiance) <= 0.0)
    {
        return radiance;
    }

    float3 lightDir;
    float lightDistance;
    RAB_GetLightDirDistance(secondarySurface, lightSample, lightDir, lightDistance);
    const float ndotl = saturate(dot(RAB_GetSurfaceNormal(secondarySurface), lightDir));
    if (ndotl <= 0.0)
    {
        return radiance;
    }

    const float3 brdf = RAB_EvaluateSurfaceBrdf(secondarySurface, lightDir, RAB_GetSurfaceViewDir(secondarySurface));
    if (CleanGiLuminance(brdf) <= 0.0)
    {
        return radiance;
    }

    const float visibility = CleanGiTraceVisibility(
        RAB_GetSurfaceWorldPos(secondarySurface), hitGeometricNormal, lightSample.position);
    if (visibility <= 0.0)
    {
        return radiance;
    }

    radiance += brdf * lightSample.radiance * ndotl * visibility * inversePickPdf / max(lightSample.solidAnglePdf, 1.0e-6);
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
    float2 uv0, uv1, uv2;
    float3 hitNormal = -bounceDir;
    float2 hitTexCoord = float2(0.0, 0.0);
    if (CleanGiLoadTriangleGeometry(payload.hitInstanceId, payload.hitPrimitiveIndex, p0, p1, p2, uv0, uv1, uv2))
    {
        const float3 crossValue = cross(p1 - p0, p2 - p0);
        hitNormal = CleanGiSafeNormalize(crossValue, -bounceDir);
        if (dot(hitNormal, bounceDir) > 0.0)
        {
            hitNormal = -hitNormal;
        }
        const float b1 = saturate(payload.hitBarycentrics.x);
        const float b2 = saturate(payload.hitBarycentrics.y);
        const float b0 = saturate(1.0 - b1 - b2);
        hitTexCoord = uv0 * b0 + uv1 * b1 + uv2 * b2;
    }

    const uint hitMaterialIndex = CleanGiLoadTriangleMaterialIndex(payload.hitInstanceId, payload.hitPrimitiveIndex);
    const PathTraceSmokeMaterial hitMaterial = CleanGiLoadSmokeMaterial(hitMaterialIndex);
    const float3 hitAlbedo = CleanGiSampleDiffuseAlbedo(hitMaterial, hitTexCoord);
    const float3 hitEmissive = CleanGiSampleEmissiveRadiance(hitMaterial, hitTexCoord);

    RAB_Surface secondarySurface = RAB_EmptySurface();
    secondarySurface.valid = 1u;
    secondarySurface.worldPos = hitPosition;
    secondarySurface.linearDepth = payload.hitT;
    secondarySurface.geometryNormal = hitNormal;
    secondarySurface.shadingNormal = hitNormal;
    secondarySurface.viewDir = -bounceDir;
    secondarySurface.materialId = hitMaterial.flags;
    secondarySurface.materialIndex = hitMaterialIndex;
    secondarySurface.surfaceClass = 0u;
    secondarySurface.material = RAB_EmptyMaterial();
    secondarySurface.material.materialIndex = hitMaterialIndex;
    secondarySurface.material.diffuseAlbedo = hitAlbedo;
    secondarySurface.material.roughness = 1.0;
    secondarySurface.material.opacity = 1.0;

    const float3 outgoing = CleanGiShadeSecondaryVertex(secondarySurface, hitNormal, hitEmissive, rng);

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
    result.hitNormal = hitNormal;
    return result;
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
    else if (view == 8u)
    {
        color = PathTraceCleanRestirGiSentinelColor(pixel);
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
    const uint materialIndex = CleanGiLoadTriangleMaterialIndex(InstanceID(), PrimitiveIndex());
    if (CleanGiMaterialDoesNotOccludeVisibility(materialIndex))
    {
        IgnoreHit();
    }
}

[shader("anyhit")]
void ShadowAnyHit(inout PathTraceCleanRestirGiPayload payload, BuiltInTriangleIntersectionAttributes attributes)
{
    const uint materialIndex = CleanGiLoadTriangleMaterialIndex(InstanceID(), PrimitiveIndex());
    if (CleanGiMaterialDoesNotOccludeVisibility(materialIndex))
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
