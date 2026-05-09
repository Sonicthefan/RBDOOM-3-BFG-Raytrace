#ifndef RB_PATH_TRACING_RAB_LIGHT_SAMPLING_HLSLI
#define RB_PATH_TRACING_RAB_LIGHT_SAMPLING_HLSLI

#include "RAB_Surface.hlsli"
#include "RAB_LightInfo.hlsli"

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

RAB_LightSample RAB_SampleEmissiveTriangleLight(RAB_LightInfo lightInfo, RAB_Surface surface)
{
    RAB_LightSample lightSample = RAB_EmptyLightSample();
    if (!RAB_IsLightInfoValid(lightInfo) || !RAB_IsSurfaceValid(surface))
    {
        return lightSample;
    }

    const float3 toLight = lightInfo.position - surface.worldPos;
    const float lightDistance = sqrt(max(dot(toLight, toLight), 1.0e-6));
    const float3 lightDir = toLight / lightDistance;

    const bool historicalDynamicEmissive = (lightInfo.flags & RT_SMOKE_EMISSIVE_TRIANGLE_HISTORY_DYNAMIC) != 0u;
    const bool twoSidedEmissive = !historicalDynamicEmissive && ((((uint)TextureInfo.w) & RT_SMOKE_TEXTURE_FLAG_RESERVOIR_TWO_SIDED_EMISSIVES) != 0u);
    const float lightFacingRaw = dot(lightInfo.normal, -lightDir);
    const float lightFacing = twoSidedEmissive ? abs(lightFacingRaw) : saturate(lightFacingRaw);
    if (lightFacing <= 0.0)
    {
        return lightSample;
    }

    lightSample.valid = 1u;
    lightSample.lightType = lightInfo.lightType;
    lightSample.lightIndex = lightInfo.lightIndex;
    lightSample.flags = lightInfo.flags;
    lightSample.position = lightInfo.position;
    lightSample.normal = lightInfo.normal;
    lightSample.distance = lightDistance;
    lightSample.radiance = lightInfo.radiance;
    lightSample.areaPdf = 1.0 / max(lightInfo.area, 1.0e-4);
    lightSample.solidAnglePdf = max(lightDistance * lightDistance, 1.0e-4) / max(lightInfo.area * lightFacing, 1.0e-4);
    return lightSample;
}

RAB_LightSample RAB_SampleDoomAnalyticSphereLight(RAB_LightInfo lightInfo, RAB_Surface surface, float2 uv)
{
    RAB_LightSample lightSample = RAB_EmptyLightSample();
    if (!RAB_IsLightInfoValid(lightInfo) || !RAB_IsSurfaceValid(surface))
    {
        return lightSample;
    }

    const float3 toCenter = lightInfo.position - surface.worldPos;
    const float centerDistanceSquared = max(dot(toCenter, toCenter), 1.0e-4);
    const float centerDistance = sqrt(centerDistanceSquared);
    const float3 centerDir = toCenter / centerDistance;
    const float doomRadius = max(lightInfo.influenceRadius, 1.0);
    if (centerDistance > doomRadius)
    {
        return lightSample;
    }

    const float sphereRadius = clamp(lightInfo.radius, 0.01, doomRadius);
    const float sinThetaMax = saturate(sphereRadius / centerDistance);
    const float cosThetaMax = sqrt(max(0.0, 1.0 - sinThetaMax * sinThetaMax));
    const float solidAngle = max(2.0 * RTXDI_PI * (1.0 - cosThetaMax), 1.0e-5);
    const float3 sampledDir = RAB_SampleDirectionCone(centerDir, cosThetaMax, uv);
    const float hitT = RAB_RaySphereHitT(surface.worldPos, sampledDir, lightInfo.position, sphereRadius, centerDistance);
    const float3 samplePosition = surface.worldPos + sampledDir * hitT;
    const float3 sampleNormal = RAB_SafeNormalize(samplePosition - lightInfo.position, -sampledDir);
    const float radiusFraction = saturate(centerDistance / doomRadius);
    const float doomInfluence = saturate(1.0 - radiusFraction * radiusFraction);
    if (doomInfluence <= 0.0)
    {
        return lightSample;
    }

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

RAB_LightSample RAB_SamplePolymorphicLight(RAB_LightInfo lightInfo, RAB_Surface surface, float2 uv)
{
    if (lightInfo.lightType == RAB_LIGHT_TYPE_EMISSIVE_TRIANGLE)
    {
        return RAB_SampleEmissiveTriangleLight(lightInfo, surface);
    }
    if (lightInfo.lightType == RAB_LIGHT_TYPE_DOOM_ANALYTIC_SPHERE)
    {
        return RAB_SampleDoomAnalyticSphereLight(lightInfo, surface, uv);
    }
    return RAB_EmptyLightSample();
}

float RAB_GetLightSampleTargetPdfForSurface(RAB_LightSample lightSample, RAB_Surface surface)
{
    if (!RAB_IsReplayableLightSample(lightSample) || !RAB_IsSurfaceValid(surface))
    {
        return 0.0;
    }

    float3 lightDir;
    float lightDistance;
    RAB_GetLightDirDistance(surface, lightSample, lightDir, lightDistance);
    const float3 brdf = RAB_EvaluateSurfaceBrdf(surface, lightDir, RAB_GetSurfaceViewDir(surface));
    const float ndotl = saturate(dot(RAB_GetSurfaceNormal(surface), lightDir));
    const float3 reflected = brdf * lightSample.radiance * ndotl;
    return RAB_Luminance(reflected) / max(lightSample.solidAnglePdf, 1.0e-6);
}

float3 RAB_GetReflectedBsdfRadianceForSurface(float3 incomingRadianceLocation, float3 incomingRadiance, RAB_Surface surface)
{
    if (!RAB_IsSurfaceValid(surface))
    {
        return float3(0.0, 0.0, 0.0);
    }

    const float3 toLight = incomingRadianceLocation - RAB_GetSurfaceWorldPos(surface);
    const float distanceSquared = max(dot(toLight, toLight), 1.0e-6);
    const float3 lightDir = toLight * rsqrt(distanceSquared);
    const float ndotl = saturate(dot(RAB_GetSurfaceNormal(surface), lightDir));
    return RAB_EvaluateSurfaceBrdf(surface, lightDir, RAB_GetSurfaceViewDir(surface)) * max(incomingRadiance, float3(0.0, 0.0, 0.0)) * ndotl;
}

float RAB_GetLightTargetPdfForVolume(RAB_LightInfo lightInfo, float3 volumeCenter, float volumeRadius)
{
    if (!RAB_IsLightInfoValid(lightInfo))
    {
        return 0.0;
    }

    const float distance = length(lightInfo.position - volumeCenter);
    const float reach = max(volumeRadius + max(lightInfo.radius, lightInfo.influenceRadius), 1.0);
    const float attenuation = saturate(1.0 - distance / reach);
    return lightInfo.weight * max(attenuation, 0.01);
}

float RAB_GetPTSampleTargetPdfForSurface(float3 samplePosition, float3 radiance, RAB_Surface surface)
{
    if (!RAB_IsSurfaceValid(surface))
    {
        return 0.0;
    }

    const float3 toSample = samplePosition - RAB_GetSurfaceWorldPos(surface);
    const float distanceSquared = max(dot(toSample, toSample), 1.0e-6);
    const float3 sampleDir = toSample * rsqrt(distanceSquared);
    const float3 brdf = RAB_EvaluateSurfaceBrdf(surface, sampleDir, RAB_GetSurfaceViewDir(surface));
    const float ndotl = saturate(dot(RAB_GetSurfaceNormal(surface), sampleDir));
    return RAB_Luminance(brdf * max(radiance, float3(0.0, 0.0, 0.0)) * ndotl);
}

bool RAB_GetConservativeVisibility(RAB_Surface surface, RAB_LightSample lightSample)
{
    return RAB_IsSurfaceValid(surface) && RAB_IsReplayableLightSample(lightSample);
}

float RAB_GetConservativeVisibility(RAB_Surface surface, float3 samplePosition)
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
    return dot(RAB_GetSurfaceGeoNormal(surface), sampleDir) > 0.0 ? 1.0 : 0.0;
}

uint RAB_GetDuplicationMapCount(int2 pixelPosition)
{
    return 0u;
}

#endif
