#include "../../vulkan.hlsli"

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
    uint shadowIgnoreInstanceId;
    uint shadowIgnorePrimitiveIndex;
    uint shadowIgnoreMaterialId;
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

struct PathTraceSmokeReservoir
{
    float4 radianceAndTargetPdf;
    float4 weightSumAndSampleCount;
    uint lightCandidateIndex;
    uint emissiveTriangleIndex;
    uint flags;
    uint padding0;
};

struct PathTraceBoundsOverlayLine
{
    float4 startAndPad;
    float4 endAndPad;
    float4 color;
};

RaytracingAccelerationStructure SmokeScene : register(t0);
VK_IMAGE_FORMAT("rgba32f") RWTexture2D<float4> SmokeOutput : register(u1);
VK_IMAGE_FORMAT("rgba32f") RWTexture2D<float4> SmokeAccumulation : register(u15);
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
Texture2D<float4> SmokeFallbackTexture : register(t14);
StructuredBuffer<PathTraceSmokeEmissiveTriangle> SmokeEmissiveTriangles : register(t16);
StructuredBuffer<PathTraceSmokeLightCandidate> SmokeLightCandidates : register(t17);
RWStructuredBuffer<PathTraceSmokeReservoir> SmokeReservoirCurrent : register(u18);
RWStructuredBuffer<PathTraceSmokeReservoir> SmokeReservoirPrevious : register(u19);
RWStructuredBuffer<PathTraceSmokeReservoir> SmokeReservoirSpatialScratch : register(u20);
StructuredBuffer<PathTraceBoundsOverlayLine> SmokeBoundsOverlayLines : register(t21);
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
    float4 BoundsOverlayInfo;
    float4 DoomAnalyticLightInfo;
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
static const uint RT_SMOKE_MAX_DEBUG_LIGHTS = 32u;
static const uint RT_DOOM_ANALYTIC_LIGHT_CASTS_SHADOWS = 0x00000001u;

bool DoomAnalyticLightsEnabled()
{
    return DoomAnalyticLightInfo.w >= 0.5;
}

bool DoomAnalyticLightsReplaceSelected()
{
    return DoomAnalyticLightInfo.w >= 1.5;
}

bool SmokeToyFakePBRSpecularEnabled()
{
    return ((((uint)TextureInfo.w) & RT_SMOKE_TEXTURE_FLAG_TOY_FAKE_PBR_SPECULAR) != 0u);
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

    float3 decoded = float3(bump.w, bump.y, 0.0);
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
    payload.shadowIgnoreInstanceId = 0xffffffffu;
    payload.shadowIgnorePrimitiveIndex = 0xffffffffu;
    payload.shadowIgnoreMaterialId = 0xffffffffu;
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

uint LoadSmokeTriangleMaterialIndex(uint instanceId, uint primitiveIndex)
{
    const uint materialCount = (uint)TextureInfo.z;
    uint materialIndex = 0xffffffffu;
    if (instanceId == 0u)
    {
        materialIndex = SmokeStaticTriangleMaterialIndexes[primitiveIndex];
    }
    else if (instanceId >= 2u)
    {
        const uint routeInstanceIndex = instanceId - 2u;
        const PathTraceRigidRouteInstance routeInstance = SmokeRigidRouteInstances[routeInstanceIndex];
        materialIndex = SmokeRigidRouteTriangleMaterialIndexes[routeInstance.triangleOffset + primitiveIndex];
    }
    else
    {
        materialIndex = SmokeDynamicTriangleMaterialIndexes[primitiveIndex];
    }
    return materialIndex < materialCount ? materialIndex : 0xffffffffu;
}

uint LoadSmokeTriangleMaterialId(uint instanceId, uint primitiveIndex)
{
    if (instanceId == 0u)
    {
        return SmokeStaticTriangleMaterials[primitiveIndex];
    }
    if (instanceId >= 2u)
    {
        const uint routeInstanceIndex = instanceId - 2u;
        const PathTraceRigidRouteInstance routeInstance = SmokeRigidRouteInstances[routeInstanceIndex];
        return SmokeRigidRouteTriangleMaterials[routeInstance.triangleOffset + primitiveIndex];
    }
    return SmokeDynamicTriangleMaterials[primitiveIndex];
}

uint LoadSmokeTriangleClassAndFlags(uint instanceId, uint primitiveIndex)
{
    if (instanceId >= 2u)
    {
        return RT_SMOKE_SURFACE_CLASS_RIGID_ENTITY;
    }
    return instanceId == 0 ? SmokeStaticTriangleClasses[primitiveIndex] : SmokeDynamicTriangleClasses[primitiveIndex];
}

float2 InterpolateSmokeTexCoord(uint instanceId, uint primitiveIndex, float2 barycentrics)
{
    const uint indexOffset = primitiveIndex * 3;
    const uint i0 = instanceId == 0 ? SmokeStaticIndices[indexOffset + 0] : SmokeDynamicIndices[indexOffset + 0];
    const uint i1 = instanceId == 0 ? SmokeStaticIndices[indexOffset + 1] : SmokeDynamicIndices[indexOffset + 1];
    const uint i2 = instanceId == 0 ? SmokeStaticIndices[indexOffset + 2] : SmokeDynamicIndices[indexOffset + 2];
    const float2 uv0 = (instanceId == 0 ? SmokeStaticVertices[i0].texCoord : SmokeDynamicVertices[i0].texCoord).xy;
    const float2 uv1 = (instanceId == 0 ? SmokeStaticVertices[i1].texCoord : SmokeDynamicVertices[i1].texCoord).xy;
    const float2 uv2 = (instanceId == 0 ? SmokeStaticVertices[i2].texCoord : SmokeDynamicVertices[i2].texCoord).xy;
    const float3 weights = float3(1.0 - barycentrics.x - barycentrics.y, barycentrics.x, barycentrics.y);
    return uv0 * weights.x + uv1 * weights.y + uv2 * weights.z;
}

float4 InterpolateSmokeVertexColor(uint instanceId, uint primitiveIndex, float2 barycentrics)
{
    const uint indexOffset = primitiveIndex * 3;
    const uint i0 = instanceId == 0 ? SmokeStaticIndices[indexOffset + 0] : SmokeDynamicIndices[indexOffset + 0];
    const uint i1 = instanceId == 0 ? SmokeStaticIndices[indexOffset + 1] : SmokeDynamicIndices[indexOffset + 1];
    const uint i2 = instanceId == 0 ? SmokeStaticIndices[indexOffset + 2] : SmokeDynamicIndices[indexOffset + 2];
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
    PathTraceSmokePayload shadowPayload = InitSmokePayload();
    shadowPayload.value = ignoreInstanceId != 0xffffffffu ? 2u : 1u;
    shadowPayload.shadowIgnoreInstanceId = ignoreInstanceId;
    shadowPayload.shadowIgnorePrimitiveIndex = ignorePrimitiveIndex;
    shadowPayload.shadowIgnoreMaterialId = ignoreMaterialId;

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
        0,
        1,
        0,
        shadowRay,
        shadowPayload);

    return shadowPayload.value == 0 ? 1.0 : 0.0;
}

bool SmokePayloadIsGuiScreen(PathTraceSmokePayload payload);
float4 CompositeSmokeGuiLayers(float3 rayOrigin, float3 rayDirection, PathTraceSmokePayload firstPayload);

float3 EvaluateSmokeLightSpriteProxies(float3 rayOrigin, float3 rayDirection, float sceneHitT)
{
    if (LightSpriteInfo.x <= 0.0)
    {
        return float3(0.0, 0.0, 0.0);
    }

    float3 sprites = float3(0.0, 0.0, 0.0);
    const uint lightCount = min((uint)LightInfo.x, RT_SMOKE_MAX_DEBUG_LIGHTS);
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

float3 EvaluateDoomAnalyticSphereLights(float3 albedo, float3 specularColor, float3 normal, float3 viewDir, float3 hitPosition, uint seed)
{
    if (!DoomAnalyticLightsEnabled())
    {
        return float3(0.0, 0.0, 0.0);
    }

    const uint uploadedCount = (uint)max(DoomAnalyticLightInfo.x, 0.0);
    const uint traceCap = (uint)max(DoomAnalyticLightInfo.y, 0.0);
    const uint lightCount = min(uploadedCount, traceCap);
    const float intensityScale = max(DoomAnalyticLightInfo.z, 0.0);
    if (lightCount == 0u || intensityScale <= 0.0)
    {
        return float3(0.0, 0.0, 0.0);
    }

    float3 direct = float3(0.0, 0.0, 0.0);
    [loop]
    for (uint lightIndex = 0u; lightIndex < lightCount; ++lightIndex)
    {
        const PathTraceDoomAnalyticLightCandidate light = DoomAnalyticLights[lightIndex];
        const float3 toLight = light.originAndRadius.xyz - hitPosition;
        const float distanceSquared = max(dot(toLight, toLight), 1.0);
        const float lightDistance = sqrt(distanceSquared);
        const float3 lightDir = toLight / lightDistance;
        const float ndotl = saturate(dot(normal, lightDir));
        if (ndotl <= 0.0)
        {
            continue;
        }

        const float doomRadius = max(light.doomRadiusAndArea.x, 1.0);
        if (lightDistance > doomRadius)
        {
            continue;
        }

        const float sphereRadius = clamp(light.originAndRadius.w, 0.01, doomRadius);
        const float sinThetaMax = saturate(sphereRadius / lightDistance);
        const float cosThetaMax = sqrt(max(0.0, 1.0 - sinThetaMax * sinThetaMax));
        const float solidAngle = max(6.2831853 * (1.0 - cosThetaMax), 1.0e-5);
        const uint lightSeed = seed ^ (light.renderLightIndex + 1u) * 747796405u ^ (lightIndex + 1u) * 2891336453u;
        const float3 sampledLightDir = SmokeSampleSphereSolidAngle(lightDir, cosThetaMax, lightSeed);
        const float sampledNdotL = saturate(dot(normal, sampledLightDir));
        if (sampledNdotL <= 0.0)
        {
            continue;
        }

        const float normalOffsetSign = dot(normal, sampledLightDir) >= 0.0 ? 1.0 : -1.0;
        const float3 shadowOrigin = hitPosition + normal * (normalOffsetSign * 0.75) + sampledLightDir * 0.25;
        const float sampledHitT = SmokeRaySphereHitT(shadowOrigin, sampledLightDir, light.originAndRadius.xyz, sphereRadius, lightDistance);
        const float shadowTMax = max(sampledHitT - 0.5, 0.01);
        const float visibility = (light.flags & RT_DOOM_ANALYTIC_LIGHT_CASTS_SHADOWS) != 0u
            ? TraceSmokeShadowVisibility(shadowOrigin, sampledLightDir, shadowTMax, 0xffffffffu, 0xffffffffu, 0xffffffffu)
            : 1.0;

        const float radiusFraction = saturate(lightDistance / doomRadius);
        const float doomInfluence = saturate(1.0 - radiusFraction * radiusFraction);
        const float directScale = (solidAngle * 0.318309886) * doomInfluence * intensityScale;
        const float3 lightColor = max(light.colorAndIntensity.rgb, float3(0.0, 0.0, 0.0));
        direct += albedo * lightColor * (sampledNdotL * directScale * visibility);
        direct += EvaluateSmokeSpecular(specularColor, normal, sampledLightDir, viewDir, lightColor, directScale, visibility);
    }

    return direct;
}

float3 EvaluateSmokeDirectLighting(PathTraceSmokePayload payload, float3 rayOrigin, float3 rayDirection, bool useNormalMap, bool useSpecular, bool includeEmissive, uint seed)
{
    const PathTraceSmokeMaterial material = LoadSmokeMaterial(payload.materialIndex);
    const float3 albedo = SampleSmokeSurfaceAlbedo(material, payload.texCoord, payload.surfaceClass, payload.translucentSubtype, payload.vertexColor, payload.vertexColorAdd).rgb;
    const float3 baseNormal = SafeNormalize(payload.normal, payload.geometricNormal);
    const float3 normal = useNormalMap ? DecodeSmokeNormalTexture(material, payload.texCoord, baseNormal, payload.tangent, payload.bitangent) : baseNormal;
    const float3 hitPosition = rayOrigin + rayDirection * payload.hitT;
    const float3 viewDir = SafeNormalize(rayOrigin - hitPosition, -rayDirection);
    const float3 specularColor = useSpecular ? SampleSmokeDirectSpecular(material, payload.texCoord) : float3(0.0, 0.0, 0.0);
    const float lightScale = max(ToyPathInfo.y, 0.0);
    const float emissiveScale = max(ToyPathInfo.z, 0.0);
    const bool activeEmissiveStage = (payload.triangleClassAndFlags & RT_SMOKE_TRIANGLE_EMISSIVE_STAGE_OFF) == 0u;
    const float3 emissive = includeEmissive ? SampleSmokeEmissive(material, payload.texCoord, payload.surfaceClass, activeEmissiveStage) * emissiveScale : float3(0.0, 0.0, 0.0);
    const float ambientScale = saturate(LightSpriteInfo.w);
    const float maxToyRayDistance = max(ToyPathInfo.x, 64.0);
    float3 direct = albedo * ambientScale + emissive;

    const uint lightCount = min((uint)LightInfo.x, RT_SMOKE_MAX_DEBUG_LIGHTS);
    if (lightCount == 0u)
    {
        return direct + EvaluateDoomAnalyticSphereLights(albedo, specularColor, normal, viewDir, hitPosition, seed);
    }

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
        if (lightDistance > maxToyRayDistance)
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
        const float shadowTMax = min(max(lightDistance - 0.5, 0.01), maxToyRayDistance);
        const float visibility = TraceSmokeShadowVisibility(shadowOrigin, lightDir, shadowTMax, 0xffffffffu, 0xffffffffu, 0xffffffffu);
        const float lightAttenuation = saturate(1.0 - lightDistance / max(lightOriginAndRadius.w, 1.0));
        const float directScale = 0.10 + lightAttenuation * lightAttenuation * 0.70;
        const float3 lightColor = max(LightColorAndIntensity[lightIndex].rgb, float3(0.0, 0.0, 0.0));
        direct += albedo * lightColor * (ndotl * directScale * visibility * lightScale);
        direct += EvaluateSmokeSpecular(specularColor, normal, lightDir, viewDir, lightColor, directScale * lightScale, visibility);
    }

    direct += EvaluateDoomAnalyticSphereLights(albedo, specularColor, normal, viewDir, hitPosition, seed);

    return direct;
}

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

uint SelectSmokeWeightedEmissiveTriangle(uint emissiveTriangleCount, float randomValue)
{
    float cumulative = 0.0;
    uint fallbackIndex = 0u;
    float fallbackWeight = -1.0;
    const float target = saturate(randomValue);

    [loop]
    for (uint triangleIndex = 0u; triangleIndex < emissiveTriangleCount; ++triangleIndex)
    {
        const PathTraceSmokeEmissiveTriangle candidate = SmokeEmissiveTriangles[triangleIndex];
        const float pdf = max(candidate.sampleWeightAndPdf.y, 0.0);
        const float weight = max(candidate.sampleWeightAndPdf.x, 0.0);
        if (weight > fallbackWeight)
        {
            fallbackWeight = weight;
            fallbackIndex = triangleIndex;
        }
        cumulative += pdf;
        if (target <= cumulative && pdf > 0.0)
        {
            return triangleIndex;
        }
    }

    return fallbackIndex;
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
    const uint2 dimensions = DispatchRaysDimensions().xy;
    const uint reservoirIndex = pixel.y * dimensions.x + pixel.x;

    PathTraceSmokeReservoir reservoir = (PathTraceSmokeReservoir)0;
    reservoir.lightCandidateIndex = 0xffffffffu;
    reservoir.emissiveTriangleIndex = 0xffffffffu;

    if (SmokePayloadIsGuiScreen(payload))
    {
        SmokeReservoirCurrent[reservoirIndex] = reservoir;
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
    const uint emissiveTriangleCount = (uint)max(EmissiveInfo.x, 0.0);
    const uint lightCandidateCount = (uint)max(EmissiveInfo.w, 0.0);
    if (emissiveTriangleCount == 0u)
    {
        SmokeReservoirCurrent[reservoirIndex] = reservoir;
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

    SmokeReservoirCurrent[reservoirIndex] = reservoir;
    const float3 viewDir = SafeNormalize(rayOrigin - hitPosition, -rayDirection);
    direct += EvaluateDoomAnalyticSphereLights(albedo, float3(0.0, 0.0, 0.0), normal, viewDir, hitPosition, seed);
    const float3 debugDirect = direct / (1.0 + direct);
    return float4(saturate(albedo * (ambientScale * 0.04) + debugDirect), 1.0);
}

float4 EvaluateSmokeToyPathTrace(float3 rayOrigin, float3 rayDirection, PathTraceSmokePayload primaryPayload, uint2 pixel)
{
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
        ((uint)max(ToyPathInfo.w, 0.0)) * 104729u;
    const bool useFakePBRSpecular = SmokeToyFakePBRSpecularEnabled();
    float3 radiance = EvaluateSmokeDirectLighting(primaryPayload, rayOrigin, rayDirection, true, useFakePBRSpecular, true, bounceSeed);

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
        const PathTraceSmokeMaterial bounceMaterial = LoadSmokeMaterial(bouncePayload.materialIndex);
        const float3 bounceAlbedo = SampleSmokeSurfaceAlbedo(bounceMaterial, bouncePayload.texCoord, bouncePayload.surfaceClass, bouncePayload.translucentSubtype, bouncePayload.vertexColor, bouncePayload.vertexColorAdd).rgb;
        const float3 bounceDirect = EvaluateSmokeDirectLighting(bouncePayload, bounceRay.Origin, bounceRay.Direction, true, useFakePBRSpecular, true, bounceSeed ^ 0x9e3779b9u);
        radiance += primaryAlbedo * bounceDirect * (0.28 + 0.22 * max(max(bounceAlbedo.r, bounceAlbedo.g), bounceAlbedo.b));
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
    const uint2 pixel = DispatchRaysIndex().xy;
    const uint2 dimensions = DispatchRaysDimensions().xy;
    const float2 uv = (float2(pixel) + 0.5) / float2(dimensions);
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

    const uint debugMode = (uint)CameraUpAndDebugMode.w;
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
    TraceRay(SmokeScene, RAY_FLAG_NONE, traceMask, 0, 1, 0, ray, payload);

    if (payload.value == 0)
    {
        if (debugMode == 14)
        {
            SmokeOutput[pixel] = float4(saturate(EvaluateSmokeLightSpriteProxies(ray.Origin, ray.Direction, ray.TMax)), 1.0);
        }
        else if (debugMode == 18 || debugMode == 19 || debugMode == 20 || debugMode == 25)
        {
            SmokeOutput[pixel] = float4(0.0, 0.0, 0.0, 1.0);
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
            const uint lightCount = min((uint)LightInfo.x, RT_SMOKE_MAX_DEBUG_LIGHTS);
            if (lightCount == 0u)
            {
                if (!DoomAnalyticLightsReplaceSelected())
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
        const float4 sampleColor = EvaluateSmokeToyPathTrace(ray.Origin, ray.Direction, payload, pixel);
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

[shader("closesthit")]
void ClosestHit(inout PathTraceSmokePayload payload, BuiltInTriangleIntersectionAttributes attributes)
{
    const uint instanceId = InstanceID();
    payload.instanceId = instanceId;
    if (instanceId >= 2u)
    {
        const uint routeInstanceIndex = instanceId - 2u;
        const PathTraceRigidRouteInstance routeInstance = SmokeRigidRouteInstances[routeInstanceIndex];
        const uint routeIndexOffset = routeInstance.indexOffset + PrimitiveIndex() * 3u;
        const uint i0 = SmokeRigidRouteIndices[routeIndexOffset + 0u];
        const uint i1 = SmokeRigidRouteIndices[routeIndexOffset + 1u];
        const uint i2 = SmokeRigidRouteIndices[routeIndexOffset + 2u];
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

        payload.value = 1;
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
        payload.materialId = SmokeRigidRouteTriangleMaterials[routeInstance.triangleOffset + PrimitiveIndex()];
        payload.materialIndex = SmokeRigidRouteTriangleMaterialIndexes[routeInstance.triangleOffset + PrimitiveIndex()];
        return;
    }
    const uint indexOffset = PrimitiveIndex() * 3;
    const uint i0 = instanceId == 0 ? SmokeStaticIndices[indexOffset + 0] : SmokeDynamicIndices[indexOffset + 0];
    const uint i1 = instanceId == 0 ? SmokeStaticIndices[indexOffset + 1] : SmokeDynamicIndices[indexOffset + 1];
    const uint i2 = instanceId == 0 ? SmokeStaticIndices[indexOffset + 2] : SmokeDynamicIndices[indexOffset + 2];

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
    const uint triangleClassAndFlags = LoadSmokeTriangleClassAndFlags(instanceId, PrimitiveIndex());
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
    payload.materialId = LoadSmokeTriangleMaterialId(instanceId, PrimitiveIndex());
    payload.materialIndex = instanceId == 0 ? SmokeStaticTriangleMaterialIndexes[PrimitiveIndex()] : SmokeDynamicTriangleMaterialIndexes[PrimitiveIndex()];
}
