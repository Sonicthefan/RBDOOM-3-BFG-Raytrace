#include "../../vulkan.hlsli"

struct PathTraceSmokePayload
{
    uint value;
    float hitT;
    float3 normal;
    float3 geometricNormal;
    float2 texCoord;
    uint surfaceClass;
    uint materialId;
    uint materialIndex;
};

struct PathTraceSmokeVertex
{
    float4 position;
    float4 normal;
    float4 texCoord;
};

struct PathTraceSmokeMaterial
{
    float4 debugAlbedo;
    uint diffuseTextureIndex;
    uint3 pad;
};

RaytracingAccelerationStructure SmokeScene : register(t0);
VK_IMAGE_FORMAT("rgba32f") RWTexture2D<float4> SmokeOutput : register(u1);
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
VK_BINDING(0, 1) Texture2D<float4> SmokeDiffuseTextures[] : register(t0, space1);
SamplerState SmokeMaterialSampler : register(s0);

cbuffer PathTraceSmokeConstants : register(b2)
{
    float4 CameraOriginAndTMax;
    float4 CameraForwardAndTanX;
    float4 CameraLeftAndTanY;
    float4 CameraUpAndDebugMode;
    float4 TextureInfo;
};

static const uint RT_SMOKE_TRIANGLE_CLASS_MASK = 0x0000ffffu;
static const uint RT_SMOKE_TRIANGLE_FORCE_GEOMETRIC_NORMAL = 0x00010000u;

float3 SafeNormalize(float3 value, float3 fallback)
{
    const float lengthSquared = dot(value, value);
    return lengthSquared > 1.0e-8 ? value * rsqrt(lengthSquared) : fallback;
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

float4 SampleSmokeDiffuseTexture(uint textureIndex, float2 texCoord, float4 fallback)
{
    const uint textureCount = (uint)TextureInfo.x;
    const uint sampleMethod = (uint)TextureInfo.y;
    const bool bindlessEnabled = TextureInfo.w != 0.0;
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
        sampled = bindlessEnabled
            ? SmokeDiffuseTextures[NonUniformResourceIndex(textureIndex)].Load(int3(0, 0, 0))
            : SmokeFallbackTexture.Load(int3(0, 0, 0));
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

PathTraceSmokeMaterial LoadSmokeMaterial(uint materialIndex)
{
    PathTraceSmokeMaterial material;
    material.debugAlbedo = float4(1.0, 0.0, 1.0, 1.0);
    material.diffuseTextureIndex = 0xffffffffu;
    material.pad = uint3(0, 0, 0);

    const uint materialCount = (uint)TextureInfo.z;
    if (materialIndex < materialCount)
    {
        material = SmokeMaterials[materialIndex];
    }

    return material;
}

[shader("raygeneration")]
void RayGen()
{
    const uint2 pixel = DispatchRaysIndex().xy;
    const uint2 dimensions = DispatchRaysDimensions().xy;
    const float2 uv = (float2(pixel) + 0.5) / float2(dimensions);
    const float2 ndc = uv * 2.0 - 1.0;

    PathTraceSmokePayload payload;
    payload.value = 0;
    payload.hitT = 0.0;
    payload.normal = float3(0.0, 0.0, 0.0);
    payload.geometricNormal = float3(0.0, 0.0, 0.0);
    payload.texCoord = float2(0.0, 0.0);
    payload.surfaceClass = 4;
    payload.materialId = 0;
    payload.materialIndex = 0;

    RayDesc ray;
    ray.Origin = CameraOriginAndTMax.xyz;
    ray.Direction = normalize(
        CameraForwardAndTanX.xyz +
        CameraLeftAndTanY.xyz * (-ndc.x * CameraForwardAndTanX.w) +
        CameraUpAndDebugMode.xyz * (-ndc.y * CameraLeftAndTanY.w));
    ray.TMin = 0.1;
    ray.TMax = CameraOriginAndTMax.w;

    TraceRay(SmokeScene, RAY_FLAG_NONE, 0xff, 0, 1, 0, ray, payload);

    const uint debugMode = (uint)CameraUpAndDebugMode.w;
    if (payload.value == 0)
    {
        SmokeOutput[pixel] = float4(1.0, 0.0, 0.0, 1.0);
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
    else if (debugMode == 8)
    {
        const PathTraceSmokeMaterial material = LoadSmokeMaterial(payload.materialIndex);
        SmokeOutput[pixel] = SampleSmokeDiffuseTexture(material.diffuseTextureIndex, payload.texCoord, material.debugAlbedo);
    }
    else
    {
        SmokeOutput[pixel] = float4(0.0, 1.0, 0.0, 1.0);
    }
}

[shader("miss")]
void Miss(inout PathTraceSmokePayload payload)
{
    payload.value = 0;
}

[shader("closesthit")]
void ClosestHit(inout PathTraceSmokePayload payload, BuiltInTriangleIntersectionAttributes attributes)
{
    const uint instanceId = InstanceID();
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
    const float3 barycentrics = float3(1.0 - attributes.barycentrics.x - attributes.barycentrics.y, attributes.barycentrics.x, attributes.barycentrics.y);

    payload.value = 1;
    payload.hitT = RayTCurrent();
    const uint triangleClassAndFlags = instanceId == 0 ? SmokeStaticTriangleClasses[PrimitiveIndex()] : SmokeDynamicTriangleClasses[PrimitiveIndex()];
    const bool forceGeometricNormal = (triangleClassAndFlags & RT_SMOKE_TRIANGLE_FORCE_GEOMETRIC_NORMAL) != 0;
    payload.geometricNormal = SafeNormalize(cross(p1 - p0, p2 - p0), float3(0.0, 0.0, 1.0));
    const float3 interpolatedNormal = SafeNormalize(n0 * barycentrics.x + n1 * barycentrics.y + n2 * barycentrics.z, payload.geometricNormal);
    payload.normal = forceGeometricNormal ? payload.geometricNormal : interpolatedNormal;
    payload.texCoord = uv0 * barycentrics.x + uv1 * barycentrics.y + uv2 * barycentrics.z;
    payload.surfaceClass = triangleClassAndFlags & RT_SMOKE_TRIANGLE_CLASS_MASK;
    payload.materialId = instanceId == 0 ? SmokeStaticTriangleMaterials[PrimitiveIndex()] : SmokeDynamicTriangleMaterials[PrimitiveIndex()];
    payload.materialIndex = instanceId == 0 ? SmokeStaticTriangleMaterialIndexes[PrimitiveIndex()] : SmokeDynamicTriangleMaterialIndexes[PrimitiveIndex()];
}
