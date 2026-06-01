#ifndef RB_PATH_TRACING_RAB_LIGHT_SAMPLING_CORE_HLSLI
#define RB_PATH_TRACING_RAB_LIGHT_SAMPLING_CORE_HLSLI

#include "RAB_SurfaceCore.hlsli"
#include "RAB_LightSample.hlsli"
#ifdef RB_RAB_LIGHT_SAMPLING_CORE_ONLY
#include "RAB_LightInfoCore.hlsli"
#else
#include "RAB_LightInfo.hlsli"
#endif

float RAB_Luminance(float3 radiance)
{
    return dot(max(radiance, float3(0.0, 0.0, 0.0)), float3(0.2126, 0.7152, 0.0722));
}

void RAB_GetLightDirDistance(RAB_Surface surface, RAB_LightSample lightSample, out float3 lightDir, out float lightDistance)
{
    const float3 toLight = lightSample.position - surface.worldPos;
    const float distanceSquared = max(dot(toLight, toLight), 1.0e-6);
    lightDistance = sqrt(distanceSquared);
    lightDir = toLight / lightDistance;
}

float3 RAB_SampleDirectionCone(float3 axis, float cosThetaMax, float2 uv)
{
    const float cosTheta = lerp(1.0, cosThetaMax, saturate(uv.x));
    const float sinTheta = sqrt(max(0.0, 1.0 - cosTheta * cosTheta));
    const float phi = 2.0 * RTXDI_PI * saturate(uv.y);
    const float3 tangent = RAB_BuildPerpendicular(axis);
    const float3 bitangent = RAB_SafeNormalize(cross(axis, tangent), float3(0.0, 1.0, 0.0));
    return RAB_SafeNormalize(axis * cosTheta + tangent * (cos(phi) * sinTheta) + bitangent * (sin(phi) * sinTheta), axis);
}

float RAB_RaySphereHitT(float3 rayOrigin, float3 rayDirection, float3 sphereCenter, float sphereRadius, float fallbackT)
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

#ifdef RB_RAB_CLEAN_REFERENCE_DOOM_ANALYTIC
#include "../cleanroom_rtxdi/RAB_DoomAnalyticReference.hlsli"
#endif

float RAB_DoomAnalyticInfluence(float centerDistance, float influenceRadius)
{
    if (influenceRadius <= 0.0 || centerDistance > influenceRadius)
    {
        return 0.0;
    }

    const float radiusFraction = saturate(centerDistance / influenceRadius);
    return saturate(1.0 - radiusFraction * radiusFraction);
}

bool RAB_DoomAnalyticHardInfluenceCutoffEnabled()
{
    return (((uint)max(DoomAnalyticLightInfo.w, 0.0)) & 4u) != 0u;
}

#ifdef RB_RAB_CLEAN_DOOM_ANALYTIC_POINT_PROXY
bool RAB_CleanDoomAnalyticPointProxyEnabled()
{
    return CleanRtxdiDiView == 12u;
}

RAB_LightSample RAB_SampleCleanDoomAnalyticPointProxyLight(
    RAB_LightInfo lightInfo,
    RAB_Surface surface,
    float3 centerDir,
    float centerDistance,
    float doomInfluence)
{
    RAB_LightSample lightSample = RAB_EmptyLightSample();
    lightSample.lightType = lightInfo.lightType;
    lightSample.lightIndex = lightInfo.lightIndex;
    lightSample.flags = lightInfo.flags;

    const float ndotl = saturate(dot(RAB_GetSurfaceNormal(surface), centerDir));
    if (ndotl <= 0.0)
    {
        return lightSample;
    }

    const float minEffectiveDistance = 64.0;
    const float maxProxyLuminance = 0.06;
    const float boundedDistanceSquared = max(centerDistance * centerDistance, minEffectiveDistance * minEffectiveDistance);
    const float boundedFalloff = (minEffectiveDistance * minEffectiveDistance) / boundedDistanceSquared;
    const float3 rawIncidentRadiance = max(lightInfo.radiance, float3(0.0, 0.0, 0.0)) * doomInfluence * boundedFalloff;
    const float incidentLuminance = RAB_Luminance(rawIncidentRadiance);
    const float incidentScale = incidentLuminance > maxProxyLuminance
        ? maxProxyLuminance / max(incidentLuminance, 1.0e-6)
        : 1.0;
    const float3 incidentRadiance = rawIncidentRadiance * incidentScale;

    lightSample.valid = 1u;
    lightSample.lightType = lightInfo.lightType;
    lightSample.lightIndex = lightInfo.lightIndex;
    lightSample.flags = lightInfo.flags;
    lightSample.position = lightInfo.position;
    lightSample.normal = -centerDir;
    lightSample.distance = max(centerDistance, 1.0e-3);
    lightSample.radiance = incidentRadiance;
    lightSample.areaPdf = 1.0;
    lightSample.solidAnglePdf = 1.0;
    return lightSample;
}
#endif

bool RAB_AllFinite3(float3 value)
{
    return all(value == value) && all(abs(value) < float3(3.402823e+38, 3.402823e+38, 3.402823e+38));
}

#ifdef RB_RAB_CLEAN_RTXDI_DI_SENTINEL
float4 RAB_CleanSampleTextureLoad(uint textureIndex, uint textureWidth, uint textureHeight, float2 wrappedTexCoord, bool bindlessEnabled, bool bilinearFilter)
{
    if (textureWidth == 0u || textureHeight == 0u)
    {
        return float4(1.0, 1.0, 1.0, 1.0);
    }

    const float2 scaled = wrappedTexCoord * float2(textureWidth, textureHeight) - (bilinearFilter ? float2(0.5, 0.5) : float2(0.0, 0.0));
    if (!bilinearFilter)
    {
        const uint2 texel = min((uint2)floor(wrappedTexCoord * float2(textureWidth, textureHeight)), uint2(textureWidth - 1u, textureHeight - 1u));
        return bindlessEnabled
            ? SmokeDiffuseTextures[NonUniformResourceIndex(textureIndex)].Load(int3(texel, 0))
            : SmokeFallbackTexture.Load(int3(0, 0, 0));
    }

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

float3 RAB_CleanSampleEmissiveRadianceAtUv(RAB_LightInfo lightInfo, float2 texCoord)
{
    const float emissiveScale = max(ToyPathInfo.z, 0.0);
    if (emissiveScale <= 0.0 || lightInfo.emissiveActiveStage == 0u)
    {
        return float3(0.0, 0.0, 0.0);
    }

    const uint textureFlags = (uint)TextureInfo.w;
    const bool useEmissiveMaps = (textureFlags & RT_SMOKE_TEXTURE_FLAG_USE_EMISSIVE_MAPS) != 0u;
    if (!useEmissiveMaps || lightInfo.emissiveTextureIndex == 0xffffffffu)
    {
        return max(lightInfo.radiance, float3(0.0, 0.0, 0.0));
    }

    const uint textureCount = (uint)TextureInfo.x;
    const uint sampleMethod = (uint)TextureInfo.y;
    if (sampleMethod == 0u || lightInfo.emissiveTextureIndex >= textureCount || !all(texCoord == texCoord) || any(abs(texCoord) > 65536.0))
    {
        return max(lightInfo.radiance, float3(0.0, 0.0, 0.0));
    }

    const bool bindlessEnabled = (textureFlags & 1u) != 0u;
    const bool bilinearFilter = (textureFlags & 2u) != 0u;
    const float2 wrappedTexCoord = frac(texCoord);
    const float4 sampled = sampleMethod == 2u
        ? RAB_CleanSampleTextureLoad(lightInfo.emissiveTextureIndex, lightInfo.emissiveTextureWidth, lightInfo.emissiveTextureHeight, wrappedTexCoord, bindlessEnabled, bilinearFilter)
        : (bindlessEnabled
            ? SmokeDiffuseTextures[NonUniformResourceIndex(lightInfo.emissiveTextureIndex)].SampleLevel(SmokeMaterialSampler, wrappedTexCoord, 0.0)
            : SmokeFallbackTexture.SampleLevel(SmokeMaterialSampler, wrappedTexCoord, 0.0));
    if (!all(sampled == sampled) || any(abs(sampled) > 65504.0))
    {
        return max(lightInfo.radiance, float3(0.0, 0.0, 0.0));
    }

    return max(lightInfo.emissiveColor, float3(0.0, 0.0, 0.0)) * saturate(sampled.rgb) * (1.75 * emissiveScale);
}
#endif

RAB_LightSample RAB_SampleEmissiveTriangleLight(RAB_LightInfo lightInfo, RAB_Surface surface, float2 uv)
{
    RAB_LightSample lightSample = RAB_EmptyLightSample();
    if (!RAB_IsLightInfoValid(lightInfo) || !RAB_IsSurfaceValid(surface))
    {
        return lightSample;
    }

    float3 samplePosition = lightInfo.position;
#ifdef RB_RAB_CLEAN_RTXDI_DI_SENTINEL
    float2 sampleTexCoord = lightInfo.triangleUv0;
#endif
#ifdef RB_RAB_CLEAN_RTXDI_DI_SENTINEL
    if (lightInfo.hasTriangleGeometry != 0u)
    {
        const float su = sqrt(saturate(uv.x));
        const float b0 = 1.0 - su;
        const float b1 = saturate(uv.y) * su;
        const float b2 = 1.0 - b0 - b1;
        samplePosition =
            lightInfo.trianglePosition0 * b0 +
            lightInfo.trianglePosition1 * b1 +
            lightInfo.trianglePosition2 * b2;
        sampleTexCoord =
            lightInfo.triangleUv0 * b0 +
            lightInfo.triangleUv1 * b1 +
            lightInfo.triangleUv2 * b2;
    }
#endif

    const float3 toLight = samplePosition - surface.worldPos;
    const float lightDistance = sqrt(max(dot(toLight, toLight), 1.0e-6));
    const float3 lightDir = toLight / lightDistance;

    const bool reservoirTwoSidedEmissive = ((((uint)TextureInfo.w) & RT_SMOKE_TEXTURE_FLAG_RESERVOIR_TWO_SIDED_EMISSIVES) != 0u);
#if defined(RB_PT_RESTIR_PDF_NEE_RLU_CURRENT_PRODUCER_ONLY) || defined(RB_RAB_CLEAN_RTXDI_DI_SENTINEL)
    const bool twoSidedEmissive = reservoirTwoSidedEmissive;
#else
    const bool historicalDynamicEmissive = (lightInfo.flags & RT_SMOKE_EMISSIVE_TRIANGLE_HISTORY_DYNAMIC) != 0u;
    const bool twoSidedEmissive = !historicalDynamicEmissive && reservoirTwoSidedEmissive;
#endif
    const float lightFacingRaw = dot(lightInfo.normal, -lightDir);
#ifdef RB_RAB_CLEAN_RTXDI_DI_SENTINEL
    const bool dummyEmissiveNormals = (CleanRtxdiDiFlags & CLEAN_RAB_DIAGNOSTIC_DUMMY_EMISSIVE_NORMALS) != 0u;
    const float lightFacing = dummyEmissiveNormals ? 1.0 : (twoSidedEmissive ? abs(lightFacingRaw) : saturate(lightFacingRaw));
#else
    const float lightFacing = twoSidedEmissive ? abs(lightFacingRaw) : saturate(lightFacingRaw);
#endif
    if (lightFacing <= 0.0)
    {
        return lightSample;
    }

    lightSample.valid = 1u;
    lightSample.lightType = lightInfo.lightType;
    lightSample.lightIndex = lightInfo.lightIndex;
    lightSample.flags = lightInfo.flags;
    lightSample.position = samplePosition;
    lightSample.normal = lightInfo.normal;
    lightSample.distance = lightDistance;
#ifdef RB_RAB_CLEAN_RTXDI_DI_SENTINEL
    lightSample.radiance = RAB_CleanSampleEmissiveRadianceAtUv(lightInfo, sampleTexCoord);
#else
    lightSample.radiance = lightInfo.radiance;
#endif
    lightSample.areaPdf = 1.0 / max(lightInfo.area, 1.0e-4);
    lightSample.solidAnglePdf = max(lightDistance * lightDistance, 1.0e-4) / max(lightInfo.area * lightFacing, 1.0e-4);
    return lightSample;
}

RAB_LightSample RAB_SampleDoomAnalyticSphereLight(RAB_LightInfo lightInfo, RAB_Surface surface, float2 uv)
{
    RAB_LightSample lightSample = RAB_EmptyLightSample();
    lightSample.lightType = lightInfo.lightType;
    lightSample.lightIndex = lightInfo.lightIndex;
    lightSample.flags = lightInfo.flags;
    if (!RAB_IsLightInfoValid(lightInfo) || !RAB_IsSurfaceValid(surface))
    {
        return lightSample;
    }

    if (!RAB_AllFinite3(lightInfo.position) ||
        !RAB_AllFinite3(lightInfo.radiance) ||
        lightInfo.radius <= 0.0 ||
        lightInfo.radius >= 3.402823e+38 ||
        lightInfo.influenceRadius <= 0.0 ||
        lightInfo.influenceRadius >= 3.402823e+38)
    {
        lightSample.flags |= RAB_LIGHT_SAMPLE_FLAG_DOOM_NONFINITE_PAYLOAD;
        return lightSample;
    }

    const float3 toCenter = lightInfo.position - surface.worldPos;
    const float centerDistanceSquared = max(dot(toCenter, toCenter), 1.0e-4);
    const float centerDistance = sqrt(centerDistanceSquared);
    const float3 centerDir = toCenter / centerDistance;
    const float doomRadius = lightInfo.influenceRadius;
    const bool hardInfluenceCutoff = RAB_DoomAnalyticHardInfluenceCutoffEnabled();
    const float doomInfluence = hardInfluenceCutoff ? RAB_DoomAnalyticInfluence(centerDistance, doomRadius) : 1.0;
    if (hardInfluenceCutoff && doomInfluence <= 0.0)
    {
        lightSample.flags |= RAB_LIGHT_SAMPLE_FLAG_DOOM_OUTSIDE_INFLUENCE;
        return lightSample;
    }

    const float centerNdotL = saturate(dot(RAB_GetSurfaceNormal(surface), centerDir));
    if (centerNdotL <= 0.0)
    {
        return lightSample;
    }

#ifdef RB_RAB_CLEAN_DOOM_ANALYTIC_POINT_PROXY
    if (RAB_CleanDoomAnalyticPointProxyEnabled())
    {
        return RAB_SampleCleanDoomAnalyticPointProxyLight(lightInfo, surface, centerDir, centerDistance, doomInfluence);
    }
#endif

    const float sphereRadius = clamp(lightInfo.radius, 0.01, doomRadius);
    const float sinThetaMax = saturate(sphereRadius / centerDistance);
    const float cosThetaMax = sqrt(max(0.0, 1.0 - sinThetaMax * sinThetaMax));
    const float solidAngle = max(2.0 * RTXDI_PI * (1.0 - cosThetaMax), 1.0e-5);
    const float3 sampledDir = RAB_SampleDirectionCone(centerDir, cosThetaMax, uv);
    const float sampledNdotL = saturate(dot(RAB_GetSurfaceNormal(surface), sampledDir));
    if (sampledNdotL <= 0.0)
    {
        return lightSample;
    }

    const float hitT = RAB_RaySphereHitT(surface.worldPos, sampledDir, lightInfo.position, sphereRadius, centerDistance);
    const float3 samplePosition = surface.worldPos + sampledDir * hitT;
    const float3 sampleNormal = RAB_SafeNormalize(samplePosition - lightInfo.position, -sampledDir);

    lightSample.valid = 1u;
    lightSample.lightType = lightInfo.lightType;
    lightSample.lightIndex = lightInfo.lightIndex;
    lightSample.flags = lightInfo.flags;
    lightSample.position = samplePosition;
    lightSample.normal = sampleNormal;
    lightSample.distance = hitT;
    lightSample.radiance = lightInfo.radiance * doomInfluence;
    lightSample.areaPdf = 1.0 / max(lightInfo.area, 1.0e-4);
    lightSample.solidAnglePdf = 1.0 / solidAngle;
    return lightSample;
}

RAB_LightSample RAB_SampleSplitPolymorphicLight(RAB_LightInfo lightInfo, RAB_Surface surface, float2 uv)
{
    if (lightInfo.lightType == RAB_LIGHT_TYPE_EMISSIVE_TRIANGLE)
    {
        return RAB_SampleEmissiveTriangleLight(lightInfo, surface, uv);
    }
    if (lightInfo.lightType == RAB_LIGHT_TYPE_DOOM_ANALYTIC_SPHERE)
    {
        return RAB_SampleDoomAnalyticSphereLight(lightInfo, surface, uv);
    }
    return RAB_EmptyLightSample();
}

RAB_LightSample RAB_SampleUnifiedPolymorphicLight(RAB_LightInfo lightInfo, RAB_Surface surface, float2 uv)
{
    if (lightInfo.unifiedLightType == PATH_TRACE_UNIFIED_LIGHT_TYPE_EMISSIVE_TRIANGLE)
    {
        return RAB_SampleEmissiveTriangleLight(lightInfo, surface, uv);
    }
    if (lightInfo.unifiedLightType == PATH_TRACE_UNIFIED_LIGHT_TYPE_DOOM_ANALYTIC)
    {
        return RAB_SampleDoomAnalyticSphereLight(lightInfo, surface, uv);
    }
    return RAB_EmptyLightSample();
}

RAB_LightSample RAB_SamplePolymorphicLight(RAB_LightInfo lightInfo, RAB_Surface surface, float2 uv)
{
#ifdef RB_RAB_CLEAN_REFERENCE_DOOM_ANALYTIC
    if (PathTraceCleanReferenceRabEnabled() && lightInfo.lightType == RAB_LIGHT_TYPE_DOOM_ANALYTIC_SPHERE)
    {
        return PathTraceCleanReferenceRabSampleDoomAnalyticSphereLight(lightInfo, surface, uv);
    }
#endif
    if (RAB_RestirLightManagerRABEnabled() || RAB_UnifiedLightSampleEnabled())
    {
        return RAB_SampleUnifiedPolymorphicLight(lightInfo, surface, uv);
    }
    return RAB_SampleSplitPolymorphicLight(lightInfo, surface, uv);
}

RAB_LightSample RAB_SampleActiveRrxPolymorphicLight(RAB_LightInfo lightInfo, RAB_Surface surface, float2 uv)
{
    if (!RAB_RestirLightManagerRABEnabled())
    {
        return RAB_EmptyLightSample();
    }
    return RAB_SampleUnifiedPolymorphicLight(lightInfo, surface, uv);
}

#endif
