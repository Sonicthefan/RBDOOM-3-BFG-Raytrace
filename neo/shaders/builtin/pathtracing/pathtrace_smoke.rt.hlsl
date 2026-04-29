#include "../../vulkan.hlsli"

struct PathTraceSmokePayload
{
    uint value;
    float hitT;
    float3 normal;
    float3 geometricNormal;
    float2 texCoord;
    uint surfaceClass;
    uint translucentSubtype;
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
    uint alphaTextureIndex;
    float alphaCutoff;
    uint flags;
    uint textureWidth;
    uint textureHeight;
    uint alphaTextureWidth;
    uint alphaTextureHeight;
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
    float4 LightOriginAndRadius[32];
    float4 LightColorAndIntensity[32];
    float4 LightInfo;
};

static const uint RT_SMOKE_TRIANGLE_CLASS_MASK = 0x0000ffffu;
static const uint RT_SMOKE_TRIANGLE_FORCE_GEOMETRIC_NORMAL = 0x00010000u;
static const uint RT_SMOKE_TRANSLUCENT_SUBTYPE_SHIFT = 24u;
static const uint RT_SMOKE_TRANSLUCENT_SUBTYPE_MASK = 0x0f000000u;
static const uint RT_SMOKE_MATERIAL_ALPHA_TEST = 0x00000001u;
static const uint RT_SMOKE_MAX_DEBUG_LIGHTS = 32u;

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

float4 SampleSmokeTexture(uint textureIndex, uint textureWidth, uint textureHeight, float2 texCoord, float4 fallback)
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
        const uint width = max(textureWidth, 1u);
        const uint height = max(textureHeight, 1u);
        const uint2 texel = min(uint2(wrappedTexCoord * float2(width, height)), uint2(width - 1u, height - 1u));
        sampled = bindlessEnabled
            ? SmokeDiffuseTextures[NonUniformResourceIndex(textureIndex)].Load(int3(texel, 0))
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

float4 SampleSmokeDiffuseTexture(PathTraceSmokeMaterial material, float2 texCoord)
{
    return SampleSmokeTexture(material.diffuseTextureIndex, material.textureWidth, material.textureHeight, texCoord, material.debugAlbedo);
}

float4 SampleSmokeAlphaTexture(PathTraceSmokeMaterial material, float2 texCoord)
{
    if (material.alphaTextureIndex != 0xffffffffu)
    {
        return SampleSmokeTexture(material.alphaTextureIndex, material.alphaTextureWidth, material.alphaTextureHeight, texCoord, SampleSmokeDiffuseTexture(material, texCoord));
    }

    return SampleSmokeDiffuseTexture(material, texCoord);
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
    payload.texCoord = float2(0.0, 0.0);
    payload.surfaceClass = 4;
    payload.translucentSubtype = 5;
    payload.materialId = 0;
    payload.materialIndex = 0;
    return payload;
}

PathTraceSmokeMaterial LoadSmokeMaterial(uint materialIndex)
{
    PathTraceSmokeMaterial material;
    material.debugAlbedo = float4(1.0, 0.0, 1.0, 1.0);
    material.diffuseTextureIndex = 0xffffffffu;
    material.alphaTextureIndex = 0xffffffffu;
    material.alphaCutoff = 0.0;
    material.flags = 0u;
    material.textureWidth = 1u;
    material.textureHeight = 1u;
    material.alphaTextureWidth = 1u;
    material.alphaTextureHeight = 1u;

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
    const uint materialIndex = instanceId == 0 ? SmokeStaticTriangleMaterialIndexes[primitiveIndex] : SmokeDynamicTriangleMaterialIndexes[primitiveIndex];
    return materialIndex < materialCount ? materialIndex : 0xffffffffu;
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

bool SmokeAlphaRejectsHit(uint instanceId, uint primitiveIndex, float2 barycentrics)
{
    const PathTraceSmokeMaterial material = LoadSmokeMaterial(LoadSmokeTriangleMaterialIndex(instanceId, primitiveIndex));
    if ((material.flags & RT_SMOKE_MATERIAL_ALPHA_TEST) == 0u)
    {
        return false;
    }

    const float2 texCoord = InterpolateSmokeTexCoord(instanceId, primitiveIndex, barycentrics);
    return SampleSmokeAlphaTexture(material, texCoord).a < material.alphaCutoff;
}

float TraceSmokeShadowVisibility(float3 origin, float3 direction, float tMax)
{
    PathTraceSmokePayload shadowPayload = InitSmokePayload();
    shadowPayload.value = 1;

    RayDesc shadowRay;
    shadowRay.Origin = origin;
    shadowRay.Direction = direction;
    shadowRay.TMin = 0.01;
    shadowRay.TMax = tMax;

    TraceRay(
        SmokeScene,
        RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER,
        0xff,
        0,
        1,
        0,
        shadowRay,
        shadowPayload);

    return shadowPayload.value == 0 ? 1.0 : 0.0;
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

    TraceRay(SmokeScene, RAY_FLAG_NONE, 0xff, 0, 1, 0, ray, payload);

    const uint debugMode = (uint)CameraUpAndDebugMode.w;
    if (payload.value == 0)
    {
        SmokeOutput[pixel] = SmokeMissColor();
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
        SmokeOutput[pixel] = SampleSmokeDiffuseTexture(material, payload.texCoord);
    }
    else if (debugMode == 9)
    {
        const PathTraceSmokeMaterial material = LoadSmokeMaterial(payload.materialIndex);
        const float4 texel = SampleSmokeAlphaTexture(material, payload.texCoord);
        const bool alphaTested = (material.flags & RT_SMOKE_MATERIAL_ALPHA_TEST) != 0u;
        if (!alphaTested)
        {
            SmokeOutput[pixel] = float4(0.15, 0.15, 0.15, 1.0);
        }
        else if (texel.a < material.alphaCutoff)
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
        const PathTraceSmokeMaterial material = LoadSmokeMaterial(payload.materialIndex);
        SmokeOutput[pixel] = SampleSmokeDiffuseTexture(material, payload.texCoord);
    }
    else if (debugMode == 11)
    {
        const PathTraceSmokeMaterial material = LoadSmokeMaterial(payload.materialIndex);
        const float4 albedo = SampleSmokeDiffuseTexture(material, payload.texCoord);
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
        const float3 albedo = SampleSmokeDiffuseTexture(material, payload.texCoord).rgb;
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
        const float3 albedo = SampleSmokeDiffuseTexture(material, payload.texCoord).rgb;
        const float3 normal = SafeNormalize(payload.normal, payload.geometricNormal);
        const float3 lightDir = normalize(float3(0.35, 0.45, 0.82));
        const float ndotl = saturate(dot(normal, lightDir));
        const float3 ambient = albedo * 0.12;
        const float3 diffuse = albedo * (0.18 + ndotl * 1.15);
        SmokeOutput[pixel] = float4(saturate(ambient + diffuse), 1.0);
    }
    else if (debugMode == 14 || debugMode == 15)
    {
        const PathTraceSmokeMaterial material = LoadSmokeMaterial(payload.materialIndex);
        const float3 albedo = SampleSmokeDiffuseTexture(material, payload.texCoord).rgb;
        const float3 normal = SafeNormalize(payload.normal, payload.geometricNormal);
        const float3 hitPosition = ray.Origin + ray.Direction * payload.hitT;
        const float3 ambient = albedo * 0.12;
        const float3 unshadowedFill = albedo * 0.18;
        float3 direct = float3(0.0, 0.0, 0.0);
        float3 dominantLightDebug = float3(0.0, 0.0, 0.0);
        float dominantLightWeight = 0.0;
        const uint lightCount = min((uint)LightInfo.x, RT_SMOKE_MAX_DEBUG_LIGHTS);
        if (lightCount == 0u)
        {
            const float3 lightDir = normalize(float3(0.35, 0.45, 0.82));
            const float ndotl = saturate(dot(normal, lightDir));
            const float normalOffsetSign = dot(normal, lightDir) >= 0.0 ? 1.0 : -1.0;
            const float3 shadowOrigin = hitPosition + normal * (normalOffsetSign * 0.75) + lightDir * 0.25;
            const float visibility = ndotl > 0.0 ? TraceSmokeShadowVisibility(shadowOrigin, lightDir, CameraOriginAndTMax.w) : 0.0;
            direct = albedo * (ndotl * 1.15 * visibility);
            dominantLightDebug = float3(0.85, 0.85, 1.0) * (0.15 + ndotl * visibility);
            dominantLightWeight = ndotl * visibility;
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
                const float visibility = TraceSmokeShadowVisibility(shadowOrigin, lightDir, shadowTMax);
                const float lightAttenuation = saturate(1.0 - lightDistance / max(lightOriginAndRadius.w, 1.0));
                const float directScale = 0.12 + lightAttenuation * lightAttenuation * 0.75;
                const float3 lightColor = max(LightColorAndIntensity[lightIndex].rgb, float3(0.0, 0.0, 0.0));
                const float contributionWeight = ndotl * directScale * visibility * max(max(lightColor.r, lightColor.g), lightColor.b);
                direct += albedo * lightColor * (ndotl * directScale * visibility);
                if (contributionWeight > dominantLightWeight)
                {
                    dominantLightWeight = contributionWeight;
                    dominantLightDebug = DebugLightSlotColor(lightIndex) * (0.18 + saturate(contributionWeight * 2.0) * 0.82);
                }
            }
        }
        if (debugMode == 15)
        {
            const float3 base = albedo * 0.08;
            SmokeOutput[pixel] = float4(saturate(base + dominantLightDebug), 1.0);
        }
        else
        {
            SmokeOutput[pixel] = float4(saturate(ambient + unshadowedFill + direct), 1.0);
        }
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

[shader("anyhit")]
void AnyHit(inout PathTraceSmokePayload payload, BuiltInTriangleIntersectionAttributes attributes)
{
    if (SmokeAlphaRejectsHit(InstanceID(), PrimitiveIndex(), attributes.barycentrics))
    {
        IgnoreHit();
    }
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
    payload.translucentSubtype = (triangleClassAndFlags & RT_SMOKE_TRANSLUCENT_SUBTYPE_MASK) >> RT_SMOKE_TRANSLUCENT_SUBTYPE_SHIFT;
    payload.materialId = instanceId == 0 ? SmokeStaticTriangleMaterials[PrimitiveIndex()] : SmokeDynamicTriangleMaterials[PrimitiveIndex()];
    payload.materialIndex = instanceId == 0 ? SmokeStaticTriangleMaterialIndexes[PrimitiveIndex()] : SmokeDynamicTriangleMaterialIndexes[PrimitiveIndex()];
}
