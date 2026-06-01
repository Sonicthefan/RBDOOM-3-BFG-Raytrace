#include "../../../vulkan.hlsli"
#include "../PathTracePrimarySurface.hlsli"
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
    uint rayMode;
    uint ignoreInstanceId;
    uint ignorePrimitiveIndex;
    uint ignoreMaterialIndex;
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

#include "../RtxdiBridge/RAB_UnifiedLightRecord.hlsli"

VK_IMAGE_FORMAT("rgba32f") RWTexture2D<float4> SmokeOutput : register(u1);
RaytracingAccelerationStructure SmokeScene : register(t0);
StructuredBuffer<PathTraceSmokeVertex> SmokeStaticVertices : register(t3);
StructuredBuffer<uint> SmokeStaticIndices : register(t4);
StructuredBuffer<PathTraceSmokeVertex> SmokeDynamicVertices : register(t6);
StructuredBuffer<uint> SmokeDynamicIndices : register(t7);
StructuredBuffer<PathTraceSmokeMaterial> SmokeMaterials : register(t13);
Texture2D<float4> SmokeFallbackTexture : register(t14);
StructuredBuffer<PathTraceSmokeEmissiveTriangle> SmokeEmissiveTriangles : register(t16);
StructuredBuffer<PathTraceSmokeVertex> SmokeRigidRouteVertices : register(t22);
StructuredBuffer<uint> SmokeRigidRouteIndices : register(t23);
StructuredBuffer<PathTraceRigidRouteInstance> SmokeRigidRouteInstances : register(t26);
RWStructuredBuffer<PathTracePrimarySurfaceRecord> PrimarySurfaceHistoryCurrent : register(u30);
RWStructuredBuffer<RTXDI_PackedDIReservoir> CleanRtxdiDiPreviousReservoirs : register(u71);
RWStructuredBuffer<RTXDI_PackedDIReservoir> CleanRtxdiDiSpatialReservoirs : register(u72);
StructuredBuffer<PathTraceUnifiedLightRecord> CleanRtxdiDiRluCurrentLights : register(t66);
VK_BINDING(0, 1) Texture2D<float4> SmokeDiffuseTextures[] : register(t0, space1);
SamplerState SmokeMaterialSampler : register(s0);

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
};

static const uint RT_SMOKE_TRIANGLE_EMISSIVE_STAGE_OFF = 0x00040000u;
static const uint RT_SMOKE_MATERIAL_EMISSIVE = 0x00000008u;
static const uint RT_SMOKE_TEXTURE_FLAG_USE_EMISSIVE_MAPS = 0x00000020u;
static const uint RT_SMOKE_TEXTURE_FLAG_RESERVOIR_TWO_SIDED_EMISSIVES = 0x00000040u;
static const float CLEAN_RTXDI_PI = 3.14159265358979323846;
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
#define RB_RAB_CLEAN_DIAGNOSTIC_RELAX_BRDF_GATES 1
#include "../RtxdiBridge/RAB_LightTarget.hlsli"

static const float2 CLEAN_RTXDI_DI_SPATIAL_NEIGHBOR_OFFSETS[32] =
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

float3 CleanSafeNormalize(float3 value, float3 fallback)
{
    const float lengthSquared = dot(value, value);
    return lengthSquared > 1.0e-8 ? value * rsqrt(lengthSquared) : fallback;
}

float CleanLuminance(float3 value)
{
    return dot(max(value, float3(0.0, 0.0, 0.0)), float3(0.2126, 0.7152, 0.0722));
}

uint CleanReservoirBlockCount(uint dimension)
{
    return (dimension + RTXDI_RESERVOIR_BLOCK_SIZE - 1u) / RTXDI_RESERVOIR_BLOCK_SIZE;
}

RTXDI_ReservoirBufferParameters CleanReservoirParams(uint2 dimensions)
{
    RTXDI_ReservoirBufferParameters params = (RTXDI_ReservoirBufferParameters)0;
    const uint blockCountX = max(CleanReservoirBlockCount(dimensions.x), 1u);
    const uint blockCountY = max(CleanReservoirBlockCount(dimensions.y), 1u);
    params.reservoirBlockRowPitch = blockCountX * RTXDI_RESERVOIR_BLOCK_SIZE * RTXDI_RESERVOIR_BLOCK_SIZE;
    params.reservoirArrayPitch = params.reservoirBlockRowPitch * blockCountY;
    return params;
}

uint CleanReservoirIndex(uint2 pixel, uint2 dimensions)
{
    return RTXDI_ReservoirPositionToPointer(CleanReservoirParams(dimensions), pixel, 0u);
}

bool CleanLoadSurface(uint2 pixel, uint2 dimensions, out PathTracePrimarySurfaceRecord record)
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

float3 CleanSurfaceNormal(PathTracePrimarySurfaceRecord record)
{
    return CleanSafeNormalize(record.shadingNormalAndOpacity.xyz, record.geometricNormalAndRoughness.xyz);
}

float3 CleanSurfaceViewDir(PathTracePrimarySurfaceRecord record)
{
    return CleanSafeNormalize(record.viewDirectionAndReserved.xyz, -CleanSurfaceNormal(record));
}

float3 CleanTransformRigidRoutePoint(PathTraceRigidRouteInstance routeInstance, float3 localPoint)
{
    return float3(
        dot(routeInstance.currentObjectToWorld0, float4(localPoint, 1.0)),
        dot(routeInstance.currentObjectToWorld1, float4(localPoint, 1.0)),
        dot(routeInstance.currentObjectToWorld2, float4(localPoint, 1.0)));
}

bool CleanLoadEmissiveTriangleGeometry(PathTraceSmokeEmissiveTriangle tri, out float3 p0, out float3 p1, out float3 p2, out float2 uv0, out float2 uv1, out float2 uv2)
{
    p0 = tri.centerAndArea.xyz;
    p1 = tri.centerAndArea.xyz;
    p2 = tri.centerAndArea.xyz;
    uv0 = tri.centroidUvAndWeight.xy;
    uv1 = tri.centroidUvAndWeight.xy;
    uv2 = tri.centroidUvAndWeight.xy;

    const uint primitiveIndex = tri.primitiveIndex;
    if (tri.instanceId == 0u)
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
        p0 = v0.position.xyz;
        p1 = v1.position.xyz;
        p2 = v2.position.xyz;
        uv0 = v0.texCoord.xy;
        uv1 = v1.texCoord.xy;
        uv2 = v2.texCoord.xy;
        return true;
    }

    if (tri.instanceId == 1u)
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
        p0 = v0.position.xyz;
        p1 = v1.position.xyz;
        p2 = v2.position.xyz;
        uv0 = v0.texCoord.xy;
        uv1 = v1.texCoord.xy;
        uv2 = v2.texCoord.xy;
        return true;
    }

    const uint routeInstanceIndex = tri.instanceId - 2u;
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
    p0 = CleanTransformRigidRoutePoint(route, v0.position.xyz);
    p1 = CleanTransformRigidRoutePoint(route, v1.position.xyz);
    p2 = CleanTransformRigidRoutePoint(route, v2.position.xyz);
    uv0 = v0.texCoord.xy;
    uv1 = v1.texCoord.xy;
    uv2 = v2.texCoord.xy;
    return true;
}

bool CleanTriangleNormalArea(float3 p0, float3 p1, float3 p2, float3 payloadNormal, out float3 normal, out float area)
{
    const float3 crossValue = cross(p1 - p0, p2 - p0);
    const float doubleAreaSquared = dot(crossValue, crossValue);
    normal = CleanSafeNormalize(payloadNormal, float3(0.0, 0.0, 1.0));
    area = 0.0;
    if (doubleAreaSquared <= 1.0e-12)
    {
        return false;
    }
    const float doubleArea = sqrt(doubleAreaSquared);
    normal = crossValue / doubleArea;
    const float3 safePayloadNormal = CleanSafeNormalize(payloadNormal, normal);
    if (dot(normal, safePayloadNormal) < 0.0)
    {
        normal = -normal;
    }
    area = 0.5 * doubleArea;
    return area > 1.0e-6;
}

RAB_Surface CleanSurfaceFromRecord(PathTracePrimarySurfaceRecord record)
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
    surface.geometryNormal = CleanSafeNormalize(record.geometricNormalAndRoughness.xyz, float3(0.0, 0.0, 1.0));
    surface.shadingNormal = CleanSafeNormalize(record.shadingNormalAndOpacity.xyz, surface.geometryNormal);
    surface.viewDir = CleanSafeNormalize(record.viewDirectionAndReserved.xyz, -surface.shadingNormal);
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

PathTraceSmokeMaterial CleanLoadSmokeMaterial(uint materialIndex)
{
    PathTraceSmokeMaterial material = (PathTraceSmokeMaterial)0;
    material.emissiveTextureIndex = 0xffffffffu;
    material.emissiveColor = float4(0.0, 0.0, 0.0, 1.0);
    material.emissiveTextureWidth = 1u;
    material.emissiveTextureHeight = 1u;
    if (materialIndex < (uint)CleanRtxdiDiTextureInfo.z)
    {
        material = SmokeMaterials[materialIndex];
    }
    return material;
}

RAB_LightInfo CleanLoadRluLightInfo(uint lightIndex)
{
    RAB_LightInfo lightInfo = RAB_EmptyLightInfo();
    if (lightIndex >= CleanRtxdiDiRluCurrentLightCount)
    {
        return lightInfo;
    }

    const PathTraceUnifiedLightRecord light = CleanRtxdiDiRluCurrentLights[lightIndex];
    if (light.type == PATH_TRACE_UNIFIED_LIGHT_TYPE_DOOM_ANALYTIC)
    {
        const float3 radiance = max(light.radianceAndLuminance.rgb, float3(0.0, 0.0, 0.0)) * max(CleanRtxdiDiDoomAnalyticLightInfo.z, 0.0);
        const float luminance = CleanLuminance(radiance);
        if (light.sourceWeight <= 0.0 || luminance <= 0.0 || light.positionAndRadius.w <= 0.0 || light.uvOrDoomParams.x <= 0.0)
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
        lightInfo.influenceRadius = max(light.uvOrDoomParams.x, lightInfo.radius);
        lightInfo.normal = float3(0.0, 0.0, 1.0);
        lightInfo.area = max(light.uvOrDoomParams.y, 1.0e-4);
        lightInfo.radiance = radiance;
        lightInfo.weight = luminance * lightInfo.area * lightInfo.influenceRadius;
        return lightInfo;
    }

    if (light.type != PATH_TRACE_UNIFIED_LIGHT_TYPE_EMISSIVE_TRIANGLE ||
        light.sourceIndex == PATH_TRACE_UNIFIED_LIGHT_INVALID_INDEX ||
        light.sourceIndex >= CleanRtxdiDiCurrentEmissiveTriangleCount ||
        light.sourceWeight <= 0.0)
    {
        return lightInfo;
    }

    const PathTraceSmokeEmissiveTriangle tri = SmokeEmissiveTriangles[light.sourceIndex];
    if (tri.materialIndex >= (uint)CleanRtxdiDiTextureInfo.z ||
        (tri.padding0 & RT_SMOKE_TRIANGLE_EMISSIVE_STAGE_OFF) != 0u)
    {
        return lightInfo;
    }

    const PathTraceSmokeMaterial material = CleanLoadSmokeMaterial(tri.materialIndex);
    if ((material.flags & RT_SMOKE_MATERIAL_EMISSIVE) == 0u)
    {
        return lightInfo;
    }

    float3 p0;
    float3 p1;
    float3 p2;
    float2 uv0;
    float2 uv1;
    float2 uv2;
    if (!CleanLoadEmissiveTriangleGeometry(tri, p0, p1, p2, uv0, uv1, uv2))
    {
        return lightInfo;
    }
    float3 normal;
    float area;
    if (!CleanTriangleNormalArea(p0, p1, p2, light.normalAndArea.xyz, normal, area))
    {
        return lightInfo;
    }

    const float emissiveScale = max(CleanRtxdiDiToyPathInfo.z, 0.0);
    const float3 radiance = max(light.radianceAndLuminance.rgb, float3(0.0, 0.0, 0.0)) * emissiveScale;
    if (CleanLuminance(radiance) <= 0.0)
    {
        return lightInfo;
    }

    lightInfo.lightType = RAB_LIGHT_TYPE_EMISSIVE_TRIANGLE;
    lightInfo.lightIndex = lightIndex;
    lightInfo.unifiedLightType = PATH_TRACE_UNIFIED_LIGHT_TYPE_EMISSIVE_TRIANGLE;
    lightInfo.materialIndex = light.materialOrLightId;
    lightInfo.flags = light.flags;
    lightInfo.position = light.positionAndRadius.xyz;
    lightInfo.radius = 0.0;
    lightInfo.influenceRadius = 0.0;
    lightInfo.normal = normal;
    lightInfo.area = area;
    lightInfo.radiance = radiance;
    lightInfo.weight = light.sourceWeight;
    lightInfo.sourceIndex = light.sourceIndex;
    lightInfo.hasTriangleGeometry = 1u;
    lightInfo.emissiveTextureIndex = material.emissiveTextureIndex;
    lightInfo.emissiveTextureWidth = material.emissiveTextureWidth;
    lightInfo.emissiveTextureHeight = material.emissiveTextureHeight;
    lightInfo.emissiveActiveStage = 1u;
    lightInfo.emissiveColor = max(material.emissiveColor.rgb, float3(0.0, 0.0, 0.0));
    lightInfo.trianglePosition0 = p0;
    lightInfo.trianglePosition1 = p1;
    lightInfo.trianglePosition2 = p2;
    lightInfo.triangleUv0 = uv0;
    lightInfo.triangleUv1 = uv1;
    lightInfo.triangleUv2 = uv2;
    return lightInfo;
}

float CleanTargetPdf(uint lightIndex, float2 sampleUv, RAB_Surface surface)
{
    const RAB_LightInfo lightInfo = CleanLoadRluLightInfo(lightIndex);
    const RAB_LightSample sample = RAB_SamplePolymorphicLight(lightInfo, surface, sampleUv);
    return max(RAB_GetLightSampleTargetPdfForSurface(sample, surface), 0.0);
}

float CleanTraceVisibility(RAB_Surface surface, RAB_LightInfo lightInfo, RAB_LightSample sample)
{
    const float3 surfacePosition = RAB_GetSurfaceWorldPos(surface);
    const float3 toSample = sample.position - surfacePosition;
    const float distanceSquared = dot(toSample, toSample);
    if (distanceSquared <= 1.0e-6)
    {
        return 0.0;
    }

    const float distance = sqrt(distanceSquared);
    const float3 direction = toSample / distance;
    const float3 shadingNormal = RAB_GetSurfaceNormal(surface);
    const float3 geometricNormal = RAB_GetSurfaceGeoNormal(surface);
    if (dot(shadingNormal, direction) <= 0.0 || dot(geometricNormal, direction) <= 0.0)
    {
        return 0.0;
    }

    RayDesc ray;
    ray.Origin = surfacePosition + geometricNormal * 0.75 + direction * 0.25;
    ray.Direction = direction;
    ray.TMin = 0.01;
    ray.TMax = max(distance - 0.5, 0.01);

    PathTraceCleanRtxdiPayload payload;
    payload.value = 0u;
    payload.rayMode = lightInfo.lightType == RAB_LIGHT_TYPE_EMISSIVE_TRIANGLE ? 2u : 1u;
    payload.ignoreInstanceId = 0xffffffffu;
    payload.ignorePrimitiveIndex = 0xffffffffu;
    payload.ignoreMaterialIndex = 0xffffffffu;
    if (lightInfo.lightType == RAB_LIGHT_TYPE_EMISSIVE_TRIANGLE &&
        lightInfo.sourceIndex < CleanRtxdiDiCurrentEmissiveTriangleCount)
    {
        const PathTraceSmokeEmissiveTriangle tri = SmokeEmissiveTriangles[lightInfo.sourceIndex];
        payload.ignoreInstanceId = tri.instanceId;
        payload.ignorePrimitiveIndex = tri.primitiveIndex;
        payload.ignoreMaterialIndex = tri.materialIndex;
    }

    TraceRay(SmokeScene, RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH, 0xff, 1, 1, 1, ray, payload);
    return payload.value == 0u ? 1.0 : 0.0;
}

float3 CleanResolve(uint lightIndex, float2 sampleUv, RAB_Surface surface, RTXDI_DIReservoir reservoir)
{
    if (!RTXDI_IsValidDIReservoir(reservoir))
    {
        return float3(0.0, 0.0, 0.0);
    }

    const RAB_LightInfo lightInfo = CleanLoadRluLightInfo(lightIndex);
    const RAB_LightSample sample = RAB_SamplePolymorphicLight(lightInfo, surface, sampleUv);
    if (!RAB_IsReplayableLightSample(sample) || sample.solidAnglePdf <= 0.0)
    {
        return float3(0.0, 0.0, 0.0);
    }

    const float3 reflected = RAB_GetReflectedBsdfRadianceForSurface(sample.position, sample.radiance, surface);
    const float visibility = CleanTraceVisibility(surface, lightInfo, sample);
    return reflected * visibility * RTXDI_GetDIReservoirInvPdf(reservoir) / max(sample.solidAnglePdf, 1.0e-6);
}

int2 CleanClampPixel(int2 pixel, uint2 dimensions)
{
    return clamp(pixel, int2(0, 0), int2(max(dimensions.x, 1u) - 1u, max(dimensions.y, 1u) - 1u));
}

RTXDI_DIReservoir CleanSpatialReuse(uint2 pixel, uint2 dimensions, PathTracePrimarySurfaceRecord surfaceRecord, RAB_Surface surface, RTXDI_DIReservoir centerReservoir)
{
    if (!RTXDI_IsValidDIReservoir(centerReservoir))
    {
        return centerReservoir;
    }

    RTXDI_RandomSamplerState rng = RTXDI_InitRandomSampler(pixel, CleanRtxdiDiFrameIndex, 0x52525807u);
    RTXDI_DIReservoir result = RTXDI_EmptyDIReservoir();
    RTXDI_CombineDIReservoirs(result, centerReservoir, 0.5, centerReservoir.targetPdf);

    const uint requestedSamples = clamp((uint)max(CleanRtxdiDiSpatialInfo.x, 1.0), 1u, 16u);
    const uint disocclusionSamples = clamp((uint)max(CleanRtxdiDiSpatialInfo.y, 1.0), 1u, 16u);
    const uint targetHistoryLength = (uint)clamp(CleanRtxdiDiRestirPTSurfaceInfo.y, 1.0, 64.0);
    const uint spatialSamples = min(centerReservoir.M < targetHistoryLength ? max(requestedSamples, disocclusionSamples) : requestedSamples, 16u);
    const float radius = max(CleanRtxdiDiSpatialInfo.z, 1.0);
    float denominator = centerReservoir.targetPdf * centerReservoir.M;
    float selectedPdf = centerReservoir.targetPdf;
    for (uint i = 0u; i < spatialSamples; ++i)
    {
        const uint offsetIndex = (uint)(RTXDI_GetNextRandom(rng) * 32.0) & 31u;
        const int2 neighborPixel = CleanClampPixel(int2(pixel) + int2(CLEAN_RTXDI_DI_SPATIAL_NEIGHBOR_OFFSETS[offsetIndex] * radius), dimensions);
        PathTracePrimarySurfaceRecord neighborSurface;
        if (!CleanLoadSurface((uint2)neighborPixel, dimensions, neighborSurface))
        {
            continue;
        }
        if (!RTXDI_IsValidNeighbor(
            CleanSurfaceNormal(surfaceRecord),
            CleanSurfaceNormal(neighborSurface),
            surfaceRecord.worldPositionAndViewDepth.w,
            neighborSurface.worldPositionAndViewDepth.w,
            0.35,
            0.10))
        {
            continue;
        }

        RTXDI_DIReservoir neighborReservoir = RTXDI_LoadDIReservoir(CleanReservoirParams(dimensions), (uint2)neighborPixel, 0u);
        if (!RTXDI_IsValidDIReservoir(neighborReservoir))
        {
            continue;
        }

        const float targetPdf = CleanTargetPdf(
            RTXDI_GetDIReservoirLightIndex(neighborReservoir),
            RTXDI_GetDIReservoirSampleUV(neighborReservoir),
            surface);
        denominator += targetPdf * neighborReservoir.M;
        neighborReservoir.spatialDistance += neighborPixel - int2(pixel);
        if (RTXDI_CombineDIReservoirs(result, neighborReservoir, RTXDI_GetNextRandom(rng), targetPdf))
        {
            selectedPdf = targetPdf;
        }
    }

    if (RTXDI_IsValidDIReservoir(result))
    {
        RTXDI_FinalizeResampling(result, selectedPdf, max(denominator, 1.0e-8));
    }
    return result;
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

    PathTracePrimarySurfaceRecord surface;
    if (!CleanLoadSurface(pixel, dimensions, surface))
    {
        SmokeOutput[pixel] = float4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    const uint reservoirIndex = CleanReservoirIndex(pixel, dimensions);
    if (reservoirIndex >= CleanRtxdiDiReservoirCount)
    {
        SmokeOutput[pixel] = float4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    const RTXDI_DIReservoir centerReservoir = RTXDI_LoadDIReservoir(CleanReservoirParams(dimensions), pixel, 0u);
    const RAB_Surface rabSurface = CleanSurfaceFromRecord(surface);
    const RTXDI_DIReservoir spatialReservoir = CleanSpatialReuse(pixel, dimensions, surface, rabSurface, centerReservoir);
    CleanRtxdiDiSpatialReservoirs[reservoirIndex] = RTXDI_PackDIReservoir(spatialReservoir);

    if (CleanRtxdiDiView == 8u && CleanRtxdiDiView8Band == 16u)
    {
        const bool acceptedNeighbor = spatialReservoir.M > centerReservoir.M;
        SmokeOutput[pixel] = float4(acceptedNeighbor ? float3(0.0, 0.90, 0.20) : float3(0.20, 0.28, 0.95), 1.0);
        return;
    }

    SmokeOutput[pixel] = float4(CleanResolve(
        RTXDI_GetDIReservoirLightIndex(spatialReservoir),
        RTXDI_GetDIReservoirSampleUV(spatialReservoir),
        rabSurface,
        spatialReservoir), 1.0);
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
    if (payload.rayMode == 2u &&
        InstanceID() == payload.ignoreInstanceId &&
        PrimitiveIndex() == payload.ignorePrimitiveIndex)
    {
        IgnoreHit();
    }
}

[shader("anyhit")]
void ShadowAnyHit(inout PathTraceCleanRtxdiPayload payload, BuiltInTriangleIntersectionAttributes attributes)
{
    if (payload.rayMode == 2u &&
        InstanceID() == payload.ignoreInstanceId &&
        PrimitiveIndex() == payload.ignorePrimitiveIndex)
    {
        IgnoreHit();
    }
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
