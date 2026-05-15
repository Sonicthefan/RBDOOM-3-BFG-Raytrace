#include "../../vulkan.hlsli"
#ifndef __cplusplus
#ifndef uint16_t
#define uint16_t uint
#endif
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

struct PathTraceSkinnedPreviousPosition
{
    float4 previousPosition;
};

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

struct RAB_Material
{
    uint materialId;
    uint materialIndex;
    uint flags;
    float alphaCutoff;
    float3 diffuseAlbedo;
    float roughness;
    float3 specularF0;
    float opacity;
    float3 emissiveRadiance;
    uint emissiveTextureIndex;
};

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

#include "PathTracePrimarySurface.hlsli"

RaytracingAccelerationStructure SmokeScene : register(t0);
VK_IMAGE_FORMAT("rgba32f") RWTexture2D<float4> SmokeOutput : register(u1);
VK_IMAGE_FORMAT("rg16f") RWTexture2D<float2> PathTraceMotionVectors : register(u39);
VK_IMAGE_FORMAT("r32ui") RWTexture2D<uint> PathTraceMotionVectorMask : register(u40);
VK_IMAGE_FORMAT("rgba16f") RWTexture2D<float4> PathTraceRRGuideAlbedo : register(u48);
VK_IMAGE_FORMAT("rgba16f") RWTexture2D<float4> PathTraceRRGuideNormalRoughness : register(u49);
VK_IMAGE_FORMAT("r32f") RWTexture2D<float> PathTraceRRGuideDepth : register(u50);
VK_IMAGE_FORMAT("r32f") RWTexture2D<float> PathTraceRRGuideHitDistance : register(u51);
VK_IMAGE_FORMAT("r32ui") RWTexture2D<uint> PathTraceRRGuideResetMask : register(u52);
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
StructuredBuffer<PathTraceSkinnedPreviousPosition> SmokeSkinnedPreviousPositions : register(t32);
StructuredBuffer<PathTraceSkinnedSurfaceDispatchRecord> SmokeSkinnedSurfaceDispatch : register(t33);
StructuredBuffer<PathTraceSmokeVertex> SmokePreviousStaticVertices : register(t34);
StructuredBuffer<uint> SmokePreviousStaticIndices : register(t35);
StructuredBuffer<uint> SmokePreviousStaticTriangleClasses : register(t36);
StructuredBuffer<uint> SmokePreviousStaticTriangleMaterials : register(t37);
StructuredBuffer<uint> SmokePreviousStaticTriangleMaterialIndexes : register(t38);
StructuredBuffer<uint> SmokeSkinnedTriangleDispatchIndexes : register(t41);
Texture2D<float4> SmokeFallbackTexture : register(t14);
RWStructuredBuffer<PathTracePrimarySurfaceRecord> PrimarySurfaceHistoryCurrent : register(u30);
RWStructuredBuffer<PathTracePrimarySurfaceRecord> PrimarySurfaceHistoryPrevious : register(u31);
VK_BINDING(0, 1) Texture2D<float4> SmokeDiffuseTextures[] : register(t0, space1);
SamplerState SmokeMaterialSampler : register(s0);

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
static const uint RT_SMOKE_TRIANGLE_EMISSIVE_STAGE_OFF = 0x00040000u;
static const uint RT_SMOKE_TRANSLUCENT_SUBTYPE_SHIFT = 24u;
static const uint RT_SMOKE_TRANSLUCENT_SUBTYPE_MASK = 0x0f000000u;
static const uint RT_SMOKE_SURFACE_CLASS_RIGID_ENTITY = 1u;
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
static const uint PT_MOTION_VECTOR_MASK_VALID = 0x00000001u;
static const uint PT_MOTION_VECTOR_MASK_SOURCE_SHIFT = 1u;
static const uint PT_MOTION_VECTOR_MASK_INVALID_REASON_SHIFT = 5u;
static const uint PT_MOTION_VECTOR_SOURCE_UNKNOWN = 0u;
static const uint PT_MOTION_VECTOR_SOURCE_STATIC = 1u;
static const uint PT_MOTION_VECTOR_SOURCE_SKINNED = 2u;
static const uint PT_MOTION_VECTOR_SOURCE_RIGID = 3u;
static const uint PT_MOTION_VECTOR_SOURCE_OTHER_OBJECT = 4u;
static const uint RT_RR_RESET_INVALID_SURFACE = 0x00000001u;
static const uint RT_RR_RESET_MISSING_PREVIOUS = 0x00000002u;
static const uint RT_RR_RESET_REJECTED_PREVIOUS = 0x00000004u;
static const uint RT_RR_RESET_MATERIAL_MISMATCH = 0x00000008u;
static const uint RT_RR_RESET_OBJECT_MOTION_UNAVAILABLE = 0x00000010u;
static const uint RT_RR_RESET_STOCHASTIC_TRANSLUCENT = 0x00000020u;
static const uint RT_RR_RESET_OTHER_INVALID = 0x00000040u;
static const uint PT_SKINNED_DISPATCH_HAS_VALID_PREVIOUS = 0x00000001u;
static const uint PT_SKINNED_DISPATCH_RT_CPU_SKINNED = 0x00000002u;
static const uint PT_RIGID_ROUTE_HAS_PREVIOUS_TRANSFORM = 0x00000001u;
static const uint PT_RIGID_ROUTE_TRANSFORM_CONTINUOUS = 0x00000002u;
static const uint RT_SMOKE_TEXTURE_FLAG_USE_NORMAL_MAPS = 0x00000008u;
static const uint RT_SMOKE_TEXTURE_FLAG_USE_SPECULAR_MAPS = 0x00000010u;
static const uint RT_SMOKE_TEXTURE_FLAG_USE_EMISSIVE_MAPS = 0x00000020u;
static const uint RT_SMOKE_TEXTURE_FLAG_TOY_FAKE_PBR_SPECULAR = 0x00000080u;
static const uint RT_SMOKE_MATERIAL_OVERRIDE_ZERO_ROUGHNESS = 0x00000001u;
static const uint RT_PT_SAFETY_DISABLE_ANY_HIT_ALPHA = 0x00000001u;
static const uint RT_PT_SAFETY_DISABLE_PRIMARY_SURFACE_HISTORY = 0x00000040u;
static const uint RT_SMOKE_SURFACE_CLASS_SKINNED_DEFORMED = 2u;

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
uint PathTraceSkinnedPreviousPositionCount() { return (uint)max(GeometryInfo3.x, 0.0); }
uint PathTraceSkinnedSurfaceDispatchCount() { return (uint)max(GeometryInfo3.y, 0.0); }
uint PathTraceSkinnedTriangleDispatchIndexCount() { return (uint)max(GeometryInfo3.z, 0.0); }
uint PathTracePreviousStaticVertexCount() { return (uint)max(GeometryInfo4.x, 0.0); }
uint PathTracePreviousStaticIndexCount() { return (uint)max(GeometryInfo4.y, 0.0); }
uint PathTracePreviousStaticTriangleCount() { return (uint)max(GeometryInfo4.z, 0.0); }
uint PathTracePreviousStaticMaterialIndexCount() { return (uint)max(GeometryInfo4.w, 0.0); }

uint2 PathTraceFullOutputSize()
{
    const uint2 size = uint2((uint)max(DispatchTileInfo.z, 0.0), (uint)max(DispatchTileInfo.w, 0.0));
    return (size.x > 0u && size.y > 0u) ? size : DispatchRaysDimensions().xy;
}

uint2 PathTraceDispatchTileOffset()
{
    return uint2((uint)max(DispatchTileInfo.x, 0.0), (uint)max(DispatchTileInfo.y, 0.0));
}

bool PathTraceSafetyDisabled(uint bit)
{
    return (((uint)SafetyInfo.x) & bit) != 0u;
}

float3 SafeNormalize(float3 value, float3 fallback)
{
    const float lengthSquared = dot(value, value);
    return lengthSquared > 1.0e-8 ? value * rsqrt(lengthSquared) : fallback;
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

float SmokeLinear1(float c)
{
    return c <= 0.04045 ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4);
}

float3 SmokeLinear3(float3 c)
{
    return float3(SmokeLinear1(c.r), SmokeLinear1(c.g), SmokeLinear1(c.b));
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

bool SmokeToyFakePBRSpecularEnabled()
{
    return (((uint)TextureInfo.w) & RT_SMOKE_TEXTURE_FLAG_TOY_FAKE_PBR_SPECULAR) != 0u;
}

RAB_Material RAB_EmptyMaterial()
{
    RAB_Material material = (RAB_Material)0;
    material.roughness = 1.0;
    material.opacity = 1.0;
    return material;
}

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
    float4 sampled = sampleMethod == 2u
        ? SampleSmokeTextureLoad(textureIndex, textureWidth, textureHeight, wrappedTexCoord, bindlessEnabled, bilinearFilter)
        : (bindlessEnabled
            ? SmokeDiffuseTextures[NonUniformResourceIndex(textureIndex)].SampleLevel(SmokeMaterialSampler, wrappedTexCoord, 0.0)
            : SmokeFallbackTexture.SampleLevel(SmokeMaterialSampler, wrappedTexCoord, 0.0));
    return (!all(sampled == sampled) || any(abs(sampled) > 65504.0)) ? fallback : sampled;
}

float3 ConvertSmokeYCoCgToRGB(float4 ycocg)
{
    ycocg.z = (ycocg.z * 31.875) + 1.0;
    ycocg.z = 1.0 / ycocg.z;
    ycocg.xy *= ycocg.z;
    return saturate(float3(
        dot(ycocg, float4(1.0, -1.0, 0.0, 1.0)),
        dot(ycocg, float4(0.0, 1.0, -0.50196078, 1.0)),
        dot(ycocg, float4(-1.0, -1.0, 1.00392156, 1.0))));
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
    return (material.flags & RT_SMOKE_MATERIAL_FORCE_DEBUG_ALBEDO) != 0u
        ? material.debugAlbedo
        : SampleSmokeDecodedDiffuseTexture(material, texCoord);
}

float4 SampleSmokeSurfaceAlbedo(PathTraceSmokeMaterial material, float2 texCoord, uint surfaceClass, uint translucentSubtype, float4 vertexColor, float4 vertexColorAdd)
{
    float4 albedo = SampleSmokeDiffuseTexture(material, texCoord);
    if (surfaceClass == RT_SMOKE_SURFACE_CLASS_TRANSLUCENT && translucentSubtype == RT_SMOKE_TRANSLUCENT_SUBTYPE_GUI_SCREEN)
    {
        albedo = material.diffuseTextureIndex != 0xffffffffu
            ? float4(albedo.rgb * vertexColor.rgb, albedo.a * vertexColor.a)
            : vertexColor;
    }
    return saturate(albedo);
}

float4 SampleSmokeAlphaTexture(PathTraceSmokeMaterial material, float2 texCoord)
{
    return material.alphaTextureIndex != 0xffffffffu
        ? SampleSmokeTexture(material.alphaTextureIndex, material.alphaTextureWidth, material.alphaTextureHeight, texCoord, SampleSmokeDiffuseTexture(material, texCoord))
        : SampleSmokeDiffuseTexture(material, texCoord);
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

float3 DecodeSmokeNormalTexture(PathTraceSmokeMaterial material, float2 texCoord, float3 normal, float3 tangent, float3 bitangent)
{
    if ((material.normalTextureIndex == 0xffffffffu) || ((((uint)TextureInfo.w) & RT_SMOKE_TEXTURE_FLAG_USE_NORMAL_MAPS) == 0u))
    {
        return normal;
    }
    const float4 bump = SampleSmokeTexture(material.normalTextureIndex, material.normalTextureWidth, material.normalTextureHeight, texCoord, float4(0.5, 0.5, 1.0, 1.0)) * 2.0 - 1.0;
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
    return SafeNormalize(tangent * decoded.x + bitangent * decoded.y + normal * decoded.z, normal);
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

float3 SampleSmokeDirectSpecular(PathTraceSmokeMaterial material, float2 texCoord)
{
    if ((material.specularTextureIndex == 0xffffffffu) || ((((uint)TextureInfo.w) & RT_SMOKE_TEXTURE_FLAG_USE_SPECULAR_MAPS) == 0u))
    {
        return float3(0.0, 0.0, 0.0);
    }
    return saturate(SampleSmokeTexture(material.specularTextureIndex, material.specularTextureWidth, material.specularTextureHeight, texCoord, float4(0.0, 0.0, 0.0, 1.0)).rgb);
}

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
        emissive *= saturate(SampleSmokeTexture(material.emissiveTextureIndex, material.emissiveTextureWidth, material.emissiveTextureHeight, texCoord, float4(1.0, 1.0, 1.0, 1.0)).rgb);
    }

    return emissive * 1.75;
}

PathTraceSmokeMaterial LoadSmokeMaterial(uint materialIndex)
{
    PathTraceSmokeMaterial material = (PathTraceSmokeMaterial)0;
    material.debugAlbedo = float4(1.0, 0.0, 1.0, 1.0);
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

RAB_Surface BuildSurfaceFromPayload(PathTraceSmokePayload payload, float3 rayOrigin, float3 rayDirection)
{
    RAB_Surface surface = RAB_EmptySurface();
    if (payload.value == 0u)
    {
        return surface;
    }

    const PathTraceSmokeMaterial smokeMaterial = LoadSmokeMaterial(payload.materialIndex);
    const float3 baseNormal = SafeNormalize(payload.normal, payload.geometricNormal);
    float3 shadingNormal = DecodeSmokeNormalTexture(smokeMaterial, payload.texCoord, baseNormal, payload.tangent, payload.bitangent);
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

    const float4 albedo = SampleSmokeSurfaceAlbedo(smokeMaterial, payload.texCoord, payload.surfaceClass, payload.translucentSubtype, payload.vertexColor, payload.vertexColorAdd);
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
    surface.material.materialId = payload.materialId;
    surface.material.materialIndex = payload.materialIndex;
    surface.material.flags = smokeMaterial.flags;
    surface.material.alphaCutoff = smokeMaterial.alphaCutoff;
    surface.material.diffuseAlbedo = albedo.rgb;
    surface.material.roughness = roughness;
    surface.material.specularF0 = specularF0;
    surface.material.opacity = SmokeAlphaCoverage(smokeMaterial, payload.texCoord);
    surface.material.emissiveRadiance = SampleSmokeEmissive(smokeMaterial, payload.texCoord, payload.surfaceClass, activeEmissiveStage) * max(ToyPathInfo.z, 0.0);
    surface.material.emissiveTextureIndex = smokeMaterial.emissiveTextureIndex;
    return surface;
}

uint2 PathTracePrimarySurfaceStorePixel(uint2 pixel)
{
    return pixel + PathTraceDispatchTileOffset();
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

PathTracePrimarySurfaceRecord PackPrimarySurfaceRecord(RAB_Surface surface)
{
    PathTracePrimarySurfaceRecord record = (PathTracePrimarySurfaceRecord)0;
    record.header.x = RT_PATH_TRACE_PRIMARY_SURFACE_RECORD_VERSION;
    if (!RAB_IsSurfaceValid(surface))
    {
        record.header.z = RT_PRIMARY_SURFACE_DEBUG_MISSING_CURRENT;
        return record;
    }

    uint validFlags = RT_PRIMARY_SURFACE_VALID;
    if (PrevCameraOriginAndValid.w >= 0.5)
    {
        validFlags |= RT_PRIMARY_SURFACE_HAS_CAMERA_REPROJECTION;
    }

    uint debugStatus = RT_PRIMARY_SURFACE_DEBUG_NO_OBJECT_MOTION;
    float3 previousWorldPosition = float3(0.0, 0.0, 0.0);
    uint objectMotionDebugStatus = RT_PRIMARY_SURFACE_DEBUG_NO_OBJECT_MOTION;
    if (TryPathTracePrimarySurfaceObjectMotion(surface, previousWorldPosition, objectMotionDebugStatus))
    {
        validFlags |= RT_PRIMARY_SURFACE_HAS_OBJECT_MOTION | RT_PRIMARY_SURFACE_HAS_PREVIOUS_POSITION;
        debugStatus = RT_PRIMARY_SURFACE_DEBUG_OK;
    }
    else
    {
        debugStatus = objectMotionDebugStatus;
    }

    record.header = uint4(RT_PATH_TRACE_PRIMARY_SURFACE_RECORD_VERSION, validFlags, debugStatus, surface.flags);
    record.worldPositionAndViewDepth = float4(surface.worldPos, surface.linearDepth);
    record.geometricNormalAndRoughness = float4(surface.geometryNormal, surface.material.roughness);
    record.shadingNormalAndOpacity = float4(surface.shadingNormal, surface.material.opacity);
    record.viewDirectionAndReserved = float4(surface.viewDir, 0.0);
    record.albedoAndAlphaCutoff = float4(surface.material.diffuseAlbedo, surface.material.alphaCutoff);
    record.specularF0AndReserved = float4(surface.material.specularF0, 0.0);
    record.emissiveAndHeight = float4(surface.material.emissiveRadiance, 0.0);
    record.previousPositionOrMotion = (validFlags & RT_PRIMARY_SURFACE_HAS_PREVIOUS_POSITION) != 0u
        ? float4(previousWorldPosition, 1.0)
        : float4(0.0, 0.0, 0.0, 0.0);
    record.materialAndSurface = uint4(surface.materialId, surface.materialIndex, surface.material.flags, surface.surfaceClass);
    record.instancePrimitiveObject = uint4(surface.instanceId, surface.primitiveIndex, 0u, surface.material.emissiveTextureIndex);
    return record;
}

void StorePrimarySurfaceRecord(uint2 pixel, RAB_Surface surface)
{
    if (PathTraceSafetyDisabled(RT_PT_SAFETY_DISABLE_PRIMARY_SURFACE_HISTORY))
    {
        return;
    }
    pixel = PathTracePrimarySurfaceStorePixel(pixel);
    const uint2 dimensions = PathTraceFullOutputSize();
    if (pixel.x >= dimensions.x || pixel.y >= dimensions.y)
    {
        return;
    }
    const uint index = pixel.y * dimensions.x + pixel.x;
    if (index < PathTracePrimarySurfaceHistoryCount())
    {
        PrimarySurfaceHistoryCurrent[index] = PackPrimarySurfaceRecord(surface);
    }
}

void StoreRayReconstructionGuides(uint2 pixel, RAB_Surface surface)
{
    pixel = PathTracePrimarySurfaceStorePixel(pixel);
    const uint2 dimensions = PathTraceFullOutputSize();
    if (pixel.x >= dimensions.x || pixel.y >= dimensions.y)
    {
        return;
    }

    if (!RAB_IsSurfaceValid(surface))
    {
        PathTraceRRGuideAlbedo[pixel] = float4(0.0, 0.0, 0.0, 1.0);
        PathTraceRRGuideNormalRoughness[pixel] = float4(0.5, 0.5, 1.0, 1.0);
        PathTraceRRGuideDepth[pixel] = 0.0;
        PathTraceRRGuideHitDistance[pixel] = 0.0;
        return;
    }

    const float3 normal = SafeNormalize(surface.shadingNormal, surface.geometryNormal);
    PathTraceRRGuideAlbedo[pixel] = float4(saturate(surface.material.diffuseAlbedo), saturate(surface.material.opacity));
    PathTraceRRGuideNormalRoughness[pixel] = float4(normal * 0.5 + 0.5, saturate(surface.material.roughness));
    PathTraceRRGuideDepth[pixel] = max(surface.linearDepth, 0.0);
    PathTraceRRGuideHitDistance[pixel] = max(surface.linearDepth, 0.0);
}

uint PathTraceMotionVectorSourceKind(RAB_Surface surface)
{
    if (!RAB_IsSurfaceValid(surface))
    {
        return PT_MOTION_VECTOR_SOURCE_UNKNOWN;
    }
    if (surface.instanceId == 0u && surface.surfaceClass == 0u)
    {
        return PT_MOTION_VECTOR_SOURCE_STATIC;
    }
    if (surface.surfaceClass == RT_SMOKE_SURFACE_CLASS_SKINNED_DEFORMED)
    {
        return PT_MOTION_VECTOR_SOURCE_SKINNED;
    }
    if (surface.surfaceClass == RT_SMOKE_SURFACE_CLASS_RIGID_ENTITY)
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

bool ProjectPrimarySurfaceToPreviousPixelFloatAndDepth(float3 worldPosition, uint2 dimensions, out float2 previousPixel, out float previousLinearDepth)
{
    previousPixel = float2(-1.0, -1.0);
    previousLinearDepth = 0.0;
    if (PrevCameraOriginAndValid.w < 0.5)
    {
        return false;
    }

    const float3 delta = worldPosition - PrevCameraOriginAndValid.xyz;
    const float forwardDistance = dot(delta, PrevCameraForwardAndTanX.xyz);
    if (forwardDistance <= 0.05)
    {
        return false;
    }
    previousLinearDepth = length(delta);

    const float ndcX = -dot(delta, PrevCameraLeftAndTanY.xyz) / max(forwardDistance * PrevCameraForwardAndTanX.w, 1.0e-5);
    const float ndcY = -dot(delta, PrevCameraUpAndTanY.xyz) / max(forwardDistance * PrevCameraLeftAndTanY.w, 1.0e-5);
    if (abs(ndcX) > 1.0 || abs(ndcY) > 1.0)
    {
        return false;
    }

    previousPixel = (float2(ndcX, ndcY) * 0.5 + 0.5) * float2(dimensions);
    return previousPixel.x >= 0.0 && previousPixel.y >= 0.0 &&
        previousPixel.x < (float)dimensions.x && previousPixel.y < (float)dimensions.y;
}

bool ProjectPrimarySurfaceToPreviousPixel(float3 worldPosition, uint2 dimensions, out float2 previousPixel)
{
    float previousLinearDepth;
    return ProjectPrimarySurfaceToPreviousPixelFloatAndDepth(worldPosition, dimensions, previousPixel, previousLinearDepth);
}

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

bool TryPathTracePreviousStaticSnapshotMotionPixels(RAB_Surface currentSurface, uint2 pixel, out int2 previousPixel, out float2 previousPixelFloat, out float2 motionPixels, out float expectedPrevDepth, out uint debugStatus)
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

    if (!ProjectPrimarySurfaceToPreviousPixelFloatAndDepth(previousPosition, PathTraceFullOutputSize(), previousPixelFloat, expectedPrevDepth))
    {
        debugStatus = RT_PRIMARY_SURFACE_DEBUG_REJECTED_PREVIOUS;
        return false;
    }

    previousPixel = int2(floor(previousPixelFloat));
    motionPixels = previousPixelFloat - (float2(pixel) + 0.5);
    debugStatus = RT_PRIMARY_SURFACE_DEBUG_OK;
    return true;
}

bool TryPathTracePackedObjectMotionPixels(RAB_Surface currentSurface, uint2 pixel, out int2 previousPixel, out float2 previousPixelFloat, out float2 motionPixels, out float expectedPrevDepth, out uint debugStatus)
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

    const PathTracePrimarySurfaceRecord currentRecord = PackPrimarySurfaceRecord(currentSurface);
    const uint requiredFlags = RT_PRIMARY_SURFACE_HAS_OBJECT_MOTION | RT_PRIMARY_SURFACE_HAS_PREVIOUS_POSITION;
    if (currentRecord.header.x != RT_PATH_TRACE_PRIMARY_SURFACE_RECORD_VERSION ||
        (currentRecord.header.y & requiredFlags) != requiredFlags ||
        currentRecord.previousPositionOrMotion.w < 0.5)
    {
        debugStatus = currentRecord.header.z;
        return false;
    }

    if (!ProjectPrimarySurfaceToPreviousPixelFloatAndDepth(currentRecord.previousPositionOrMotion.xyz, PathTraceFullOutputSize(), previousPixelFloat, expectedPrevDepth))
    {
        debugStatus = RT_PRIMARY_SURFACE_DEBUG_REJECTED_PREVIOUS;
        return false;
    }

    previousPixel = int2(floor(previousPixelFloat));
    motionPixels = previousPixelFloat - (float2(pixel) + 0.5);
    return true;
}

bool TryPathTraceCombinedGeometryMotionPixels(RAB_Surface currentSurface, uint2 pixel, out int2 previousPixel, out float2 previousPixelFloat, out float2 motionPixels, out float expectedPrevDepth, out uint debugStatus, out uint sourceKind)
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
        return TryPathTracePreviousStaticSnapshotMotionPixels(currentSurface, pixel, previousPixel, previousPixelFloat, motionPixels, expectedPrevDepth, debugStatus);
    }

    return TryPathTracePackedObjectMotionPixels(currentSurface, pixel, previousPixel, previousPixelFloat, motionPixels, expectedPrevDepth, debugStatus);
}

uint RayReconstructionResetMaskFromStatus(RAB_Surface surface, bool motionValid, uint debugStatus)
{
    uint mask = 0u;
    if (!RAB_IsSurfaceValid(surface))
    {
        return RT_RR_RESET_INVALID_SURFACE;
    }
    if (surface.surfaceClass == RT_SMOKE_SURFACE_CLASS_TRANSLUCENT)
    {
        mask |= RT_RR_RESET_STOCHASTIC_TRANSLUCENT;
    }
    if (motionValid)
    {
        return mask;
    }

    if (debugStatus == RT_PRIMARY_SURFACE_DEBUG_MISSING_PREVIOUS ||
        debugStatus == RT_PRIMARY_SURFACE_DEBUG_SKINNED_MISSING_PREVIOUS ||
        debugStatus == RT_PRIMARY_SURFACE_DEBUG_RIGID_MISSING_PREVIOUS)
    {
        mask |= RT_RR_RESET_MISSING_PREVIOUS;
    }
    else if (debugStatus == RT_PRIMARY_SURFACE_DEBUG_REJECTED_PREVIOUS)
    {
        mask |= RT_RR_RESET_REJECTED_PREVIOUS;
    }
    else if (debugStatus == RT_PRIMARY_SURFACE_DEBUG_MATERIAL_MISMATCH)
    {
        mask |= RT_RR_RESET_MATERIAL_MISMATCH;
    }
    else if (debugStatus == RT_PRIMARY_SURFACE_DEBUG_NO_OBJECT_MOTION ||
        debugStatus == RT_PRIMARY_SURFACE_DEBUG_SKINNED_RANGE_MISMATCH ||
        debugStatus == RT_PRIMARY_SURFACE_DEBUG_SKINNED_PREVIOUS_OUT_OF_RANGE ||
        debugStatus == RT_PRIMARY_SURFACE_DEBUG_RIGID_RANGE_MISMATCH)
    {
        mask |= RT_RR_RESET_OBJECT_MOTION_UNAVAILABLE;
    }
    else
    {
        mask |= RT_RR_RESET_OTHER_INVALID;
    }
    return mask;
}

void StoreRayReconstructionMotionGuides(uint2 pixel, RAB_Surface surface)
{
    const bool motionVectorExportEnabled = MotionVectorInfo.x >= 0.5;
    pixel = PathTracePrimarySurfaceStorePixel(pixel);
    const uint2 dimensions = PathTraceFullOutputSize();
    if (pixel.x >= dimensions.x || pixel.y >= dimensions.y)
    {
        return;
    }

    uint sourceKind = PathTraceMotionVectorSourceKind(surface);
    int2 previousPixel;
    float2 previousPixelFloat;
    float2 motionPixels;
    float expectedPrevDepth;
    uint debugStatus;
    if (TryPathTraceCombinedGeometryMotionPixels(surface, pixel, previousPixel, previousPixelFloat, motionPixels, expectedPrevDepth, debugStatus, sourceKind))
    {
        if (motionVectorExportEnabled)
        {
            PathTraceMotionVectors[pixel] = motionPixels;
            PathTraceMotionVectorMask[pixel] = PathTraceMotionVectorMaskFromStatus(true, sourceKind, RT_PRIMARY_SURFACE_DEBUG_OK);
        }
        PathTraceRRGuideResetMask[pixel] = RayReconstructionResetMaskFromStatus(surface, true, RT_PRIMARY_SURFACE_DEBUG_OK);
    }
    else
    {
        if (motionVectorExportEnabled)
        {
            PathTraceMotionVectors[pixel] = float2(0.0, 0.0);
            PathTraceMotionVectorMask[pixel] = PathTraceMotionVectorMaskFromStatus(false, sourceKind, debugStatus);
        }
        PathTraceRRGuideResetMask[pixel] = RayReconstructionResetMaskFromStatus(surface, false, debugStatus);
    }
}

PathTraceSmokePayload InitSmokePayload()
{
    PathTraceSmokePayload payload = (PathTraceSmokePayload)0;
    payload.shadowIgnoreInstanceId = 0xffffffffu;
    payload.shadowIgnorePrimitiveIndex = 0xffffffffu;
    payload.shadowIgnoreMaterialId = 0xffffffffu;
    return payload;
}

bool SmokeTriangleIndexRangeValid(uint instanceId, uint primitiveIndex)
{
    if (instanceId == 0u)
    {
        return primitiveIndex < PathTraceStaticTriangleCount() &&
            primitiveIndex * 3u + 2u < PathTraceStaticIndexCount();
    }
    if (instanceId == 1u)
    {
        return primitiveIndex < PathTraceDynamicTriangleCount() &&
            primitiveIndex * 3u + 2u < PathTraceDynamicIndexCount();
    }
    return false;
}

uint LoadSmokeTriangleMaterialId(uint instanceId, uint primitiveIndex)
{
    if (instanceId == 0u && primitiveIndex < PathTraceStaticTriangleCount())
    {
        return SmokeStaticTriangleMaterials[primitiveIndex];
    }
    if (instanceId == 1u && primitiveIndex < PathTraceDynamicTriangleCount())
    {
        return SmokeDynamicTriangleMaterials[primitiveIndex];
    }
    if (instanceId >= 2u)
    {
        const uint routeInstanceIndex = instanceId - 2u;
        if (routeInstanceIndex < PathTraceRigidRouteInstanceCount())
        {
            const PathTraceRigidRouteInstance routeInstance = SmokeRigidRouteInstances[routeInstanceIndex];
            if (primitiveIndex < routeInstance.triangleCount &&
                routeInstance.triangleOffset + primitiveIndex < PathTraceRigidRouteTriangleCount())
            {
                return SmokeRigidRouteTriangleMaterials[routeInstance.triangleOffset + primitiveIndex];
            }
        }
    }
    return 0xffffffffu;
}

uint LoadSmokeTriangleClassAndFlags(uint instanceId, uint primitiveIndex)
{
    if (instanceId == 0u && primitiveIndex < PathTraceStaticTriangleCount())
    {
        return SmokeStaticTriangleClasses[primitiveIndex];
    }
    if (instanceId == 1u && primitiveIndex < PathTraceDynamicTriangleCount())
    {
        return SmokeDynamicTriangleClasses[primitiveIndex];
    }
    return RT_SMOKE_SURFACE_CLASS_RIGID_ENTITY;
}

uint LoadSmokeTriangleMaterialIndex(uint instanceId, uint primitiveIndex)
{
    if (instanceId == 0u && primitiveIndex < PathTraceStaticTriangleCount())
    {
        return SmokeStaticTriangleMaterialIndexes[primitiveIndex];
    }
    if (instanceId == 1u && primitiveIndex < PathTraceDynamicTriangleCount())
    {
        return SmokeDynamicTriangleMaterialIndexes[primitiveIndex];
    }
    return 0xffffffffu;
}

float2 InterpolateSmokeTexCoord(uint instanceId, uint primitiveIndex, float2 barycentrics)
{
    const uint indexOffset = primitiveIndex * 3u;
    const uint i0 = instanceId == 0u ? SmokeStaticIndices[indexOffset + 0u] : SmokeDynamicIndices[indexOffset + 0u];
    const uint i1 = instanceId == 0u ? SmokeStaticIndices[indexOffset + 1u] : SmokeDynamicIndices[indexOffset + 1u];
    const uint i2 = instanceId == 0u ? SmokeStaticIndices[indexOffset + 2u] : SmokeDynamicIndices[indexOffset + 2u];
    const uint vertexCount = instanceId == 0u ? PathTraceStaticVertexCount() : PathTraceDynamicVertexCount();
    if (i0 >= vertexCount || i1 >= vertexCount || i2 >= vertexCount)
    {
        return float2(0.0, 0.0);
    }
    const float2 uv0 = (instanceId == 0u ? SmokeStaticVertices[i0].texCoord : SmokeDynamicVertices[i0].texCoord).xy;
    const float2 uv1 = (instanceId == 0u ? SmokeStaticVertices[i1].texCoord : SmokeDynamicVertices[i1].texCoord).xy;
    const float2 uv2 = (instanceId == 0u ? SmokeStaticVertices[i2].texCoord : SmokeDynamicVertices[i2].texCoord).xy;
    const float3 weights = float3(1.0 - barycentrics.x - barycentrics.y, barycentrics.x, barycentrics.y);
    return uv0 * weights.x + uv1 * weights.y + uv2 * weights.z;
}

float4 InterpolateSmokeVertexColor(uint instanceId, uint primitiveIndex, float2 barycentrics)
{
    const uint indexOffset = primitiveIndex * 3u;
    const uint i0 = instanceId == 0u ? SmokeStaticIndices[indexOffset + 0u] : SmokeDynamicIndices[indexOffset + 0u];
    const uint i1 = instanceId == 0u ? SmokeStaticIndices[indexOffset + 1u] : SmokeDynamicIndices[indexOffset + 1u];
    const uint i2 = instanceId == 0u ? SmokeStaticIndices[indexOffset + 2u] : SmokeDynamicIndices[indexOffset + 2u];
    const uint vertexCount = instanceId == 0u ? PathTraceStaticVertexCount() : PathTraceDynamicVertexCount();
    if (i0 >= vertexCount || i1 >= vertexCount || i2 >= vertexCount)
    {
        return float4(1.0, 1.0, 1.0, 1.0);
    }
    const float4 c0 = instanceId == 0u ? SmokeStaticVertices[i0].color : SmokeDynamicVertices[i0].color;
    const float4 c1 = instanceId == 0u ? SmokeStaticVertices[i1].color : SmokeDynamicVertices[i1].color;
    const float4 c2 = instanceId == 0u ? SmokeStaticVertices[i2].color : SmokeDynamicVertices[i2].color;
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

float SmokeAdditiveDecalMaterialOpacity(PathTraceSmokeMaterial material, float3 albedo)
{
    if ((material.flags & RT_SMOKE_MATERIAL_ADDITIVE_DECAL_WHITE_KEY) != 0u)
    {
        return 1.0 - min(min(albedo.r, albedo.g), albedo.b);
    }
    return max(max(albedo.r, albedo.g), albedo.b);
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
    const float opacity = SmokeAdditiveDecalMaterialOpacity(material, SampleSmokeDiffuseTexture(material, texCoord).rgb);
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
    const float key = blackKey ? max(max(albedo.r, albedo.g), albedo.b) : 1.0 - min(min(albedo.r, albedo.g), albedo.b);
    const uint2 ditherCell = uint2(floor(frac(abs(texCoord)) * 128.0));
    const uint2 baryCell = uint2(saturate(barycentrics) * 255.0);
    const uint hash =
        primitiveIndex * 668265263u ^
        instanceId * 2246822519u ^
        ditherCell.x * 3266489917u ^
        ditherCell.y * 668265263u ^
        baryCell.x * 747796405u ^
        baryCell.y * 2891336453u;
    return saturate(key) < SmokeHashToUnitFloat(hash);
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

bool SmokeAlphaRejectsHit(uint instanceId, uint primitiveIndex, float2 hitBarycentrics, uint rayMode)
{
    if (PathTraceSafetyDisabled(RT_PT_SAFETY_DISABLE_ANY_HIT_ALPHA) ||
        instanceId >= 2u ||
        !SmokeTriangleIndexRangeValid(instanceId, primitiveIndex))
    {
        return false;
    }
    const uint materialIndex = LoadSmokeTriangleMaterialIndex(instanceId, primitiveIndex);
    const PathTraceSmokeMaterial material = LoadSmokeMaterial(materialIndex);
    const float2 texCoord = InterpolateSmokeTexCoord(instanceId, primitiveIndex, hitBarycentrics);
    const uint triangleClassAndFlags = LoadSmokeTriangleClassAndFlags(instanceId, primitiveIndex);
    const bool shadowRay = rayMode != 0u;
    if (SmokeGuiRejectsTransparentHit(instanceId, primitiveIndex, hitBarycentrics, triangleClassAndFlags))
    {
        return true;
    }
    if (SmokeParticleDitherRejectsHit(material, texCoord, hitBarycentrics, instanceId, primitiveIndex, triangleClassAndFlags, shadowRay))
    {
        return true;
    }
    if (SmokeGlassFallbackRejectsHit(material, texCoord, hitBarycentrics, instanceId, primitiveIndex, triangleClassAndFlags, shadowRay))
    {
        return true;
    }
    if (SmokeAdditiveDecalRejectsHit(material, texCoord, shadowRay))
    {
        return true;
    }
    if (SmokeFilterDecalRejectsHit(material, texCoord, hitBarycentrics, instanceId, primitiveIndex, shadowRay))
    {
        return true;
    }
    if ((material.flags & RT_SMOKE_MATERIAL_ALPHA_TEST) == 0u)
    {
        return false;
    }
    return SmokeAlphaCoverage(material, texCoord) < material.alphaCutoff;
}

[shader("raygeneration")]
void RayGen()
{
    const uint2 pixel = DispatchRaysIndex().xy;
    const uint2 dimensions = DispatchRaysDimensions().xy;
    const uint2 outputPixel = pixel + PathTraceDispatchTileOffset();
    const uint2 fullDimensions = PathTraceFullOutputSize();
    if (outputPixel.x >= fullDimensions.x || outputPixel.y >= fullDimensions.y)
    {
        return;
    }

    const float2 uv = (float2(outputPixel) + 0.5) / float2(max(fullDimensions, uint2(1u, 1u)));
    const float2 ndc = uv * 2.0 - 1.0;
    RayDesc ray;
    ray.Origin = CameraOriginAndTMax.xyz;
    ray.Direction = normalize(
        CameraForwardAndTanX.xyz +
        CameraLeftAndTanY.xyz * (-ndc.x * CameraForwardAndTanX.w) +
        CameraUpAndDebugMode.xyz * (-ndc.y * CameraLeftAndTanY.w));
    ray.TMin = 0.01;
    ray.TMax = max(CameraOriginAndTMax.w, 0.1);

    PathTraceSmokePayload payload = InitSmokePayload();
    TraceRay(SmokeScene, RAY_FLAG_NONE, 0xff, 0, 1, 0, ray, payload);
    const RAB_Surface surface = BuildSurfaceFromPayload(payload, ray.Origin, ray.Direction);
    StorePrimarySurfaceRecord(pixel, surface);
    StoreRayReconstructionGuides(pixel, surface);
    StoreRayReconstructionMotionGuides(pixel, surface);
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
    if (SmokeAlphaRejectsHit(InstanceID(), PrimitiveIndex(), attributes.barycentrics, payload.value))
    {
        IgnoreHit();
    }
}

[shader("anyhit")]
void ShadowAnyHit(inout PathTraceSmokeShadowPayload payload, BuiltInTriangleIntersectionAttributes attributes)
{
    payload.hit = 1u;
    if (SmokeAlphaRejectsHit(InstanceID(), PrimitiveIndex(), attributes.barycentrics, payload.rayMode))
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
        if (routeIndexOffset + 2u >= PathTraceRigidRouteIndexCount())
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
        const float3 n0 = v0.normal.xyz;
        const float3 n1 = v1.normal.xyz;
        const float3 n2 = v2.normal.xyz;
        const float2 uv0 = v0.texCoord.xy;
        const float2 uv1 = v1.texCoord.xy;
        const float2 uv2 = v2.texCoord.xy;

        payload.value = 1u;
        payload.hitT = RayTCurrent();
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
            const float3 rawTangent = TransformObjectVectorToWorld((dp1 * duv2.y - dp2 * duv1.y) * inverseDeterminant);
            const float3 rawBitangent = TransformObjectVectorToWorld((dp2 * duv1.x - dp1 * duv2.x) * inverseDeterminant);
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
        payload.surfaceClass = RT_SMOKE_SURFACE_CLASS_RIGID_ENTITY;
        payload.translucentSubtype = 0u;
        payload.triangleClassAndFlags = RT_SMOKE_SURFACE_CLASS_RIGID_ENTITY;
        payload.materialId = SmokeRigidRouteTriangleMaterials[routeInstance.triangleOffset + primitiveIndex];
        payload.materialIndex = SmokeRigidRouteTriangleMaterialIndexes[routeInstance.triangleOffset + primitiveIndex];
        return;
    }

    if (!SmokeTriangleIndexRangeValid(instanceId, primitiveIndex))
    {
        return;
    }
    const uint indexOffset = primitiveIndex * 3u;
    const uint i0 = instanceId == 0u ? SmokeStaticIndices[indexOffset + 0u] : SmokeDynamicIndices[indexOffset + 0u];
    const uint i1 = instanceId == 0u ? SmokeStaticIndices[indexOffset + 1u] : SmokeDynamicIndices[indexOffset + 1u];
    const uint i2 = instanceId == 0u ? SmokeStaticIndices[indexOffset + 2u] : SmokeDynamicIndices[indexOffset + 2u];
    const uint vertexCount = instanceId == 0u ? PathTraceStaticVertexCount() : PathTraceDynamicVertexCount();
    if (i0 >= vertexCount || i1 >= vertexCount || i2 >= vertexCount)
    {
        return;
    }

    const float3 p0 = (instanceId == 0u ? SmokeStaticVertices[i0].position : SmokeDynamicVertices[i0].position).xyz;
    const float3 p1 = (instanceId == 0u ? SmokeStaticVertices[i1].position : SmokeDynamicVertices[i1].position).xyz;
    const float3 p2 = (instanceId == 0u ? SmokeStaticVertices[i2].position : SmokeDynamicVertices[i2].position).xyz;
    const float3 n0 = (instanceId == 0u ? SmokeStaticVertices[i0].normal : SmokeDynamicVertices[i0].normal).xyz;
    const float3 n1 = (instanceId == 0u ? SmokeStaticVertices[i1].normal : SmokeDynamicVertices[i1].normal).xyz;
    const float3 n2 = (instanceId == 0u ? SmokeStaticVertices[i2].normal : SmokeDynamicVertices[i2].normal).xyz;
    const float2 uv0 = (instanceId == 0u ? SmokeStaticVertices[i0].texCoord : SmokeDynamicVertices[i0].texCoord).xy;
    const float2 uv1 = (instanceId == 0u ? SmokeStaticVertices[i1].texCoord : SmokeDynamicVertices[i1].texCoord).xy;
    const float2 uv2 = (instanceId == 0u ? SmokeStaticVertices[i2].texCoord : SmokeDynamicVertices[i2].texCoord).xy;
    const float4 c0 = instanceId == 0u ? SmokeStaticVertices[i0].color : SmokeDynamicVertices[i0].color;
    const float4 c1 = instanceId == 0u ? SmokeStaticVertices[i1].color : SmokeDynamicVertices[i1].color;
    const float4 c2 = instanceId == 0u ? SmokeStaticVertices[i2].color : SmokeDynamicVertices[i2].color;
    const float4 c20 = instanceId == 0u ? SmokeStaticVertices[i0].color2 : SmokeDynamicVertices[i0].color2;
    const float4 c21 = instanceId == 0u ? SmokeStaticVertices[i1].color2 : SmokeDynamicVertices[i1].color2;
    const float4 c22 = instanceId == 0u ? SmokeStaticVertices[i2].color2 : SmokeDynamicVertices[i2].color2;
    const float3 barycentrics = float3(1.0 - attributes.barycentrics.x - attributes.barycentrics.y, attributes.barycentrics.x, attributes.barycentrics.y);
    const uint triangleClassAndFlags = LoadSmokeTriangleClassAndFlags(instanceId, primitiveIndex);
    const bool forceGeometricNormal = (triangleClassAndFlags & RT_SMOKE_TRIANGLE_FORCE_GEOMETRIC_NORMAL) != 0u;

    payload.value = 1u;
    payload.hitT = RayTCurrent();
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
    payload.materialIndex = LoadSmokeTriangleMaterialIndex(instanceId, primitiveIndex);
}
