#ifndef RB_PATH_TRACING_REMIX_RAB_LIGHT_SAMPLING_BRIDGE_HLSLI
#define RB_PATH_TRACING_REMIX_RAB_LIGHT_SAMPLING_BRIDGE_HLSLI

// Remix-owned RAB light sampling policy for inactive RTXDI contracts.
//
// Keep this bridge independent from the legacy runtime sampling headers. It
// depends only on the core record/sample types and rbdoom surface BRDF helpers.

#include "../RtxdiBridge/RAB_SurfaceCore.hlsli"
#include "../RtxdiBridge/RAB_LightInfoCore.hlsli"
#include "../RtxdiBridge/RAB_LightSample.hlsli"
#include "../RtxdiBridge/RAB_Brdf.hlsli"

float RemixRAB_Luminance(float3 radiance)
{
    return dot(max(radiance, float3(0.0, 0.0, 0.0)), float3(0.2126, 0.7152, 0.0722));
}

float RAB_Luminance(float3 radiance)
{
    return RemixRAB_Luminance(radiance);
}

void RemixRAB_GetLightDirDistance(RAB_Surface surface, RAB_LightSample lightSample, out float3 lightDir, out float lightDistance)
{
    const float3 toLight = lightSample.position - RAB_GetSurfaceWorldPos(surface);
    const float distanceSquared = max(dot(toLight, toLight), 1.0e-6);
    lightDistance = sqrt(distanceSquared);
    lightDir = toLight / lightDistance;
}

RAB_LightSample RemixRAB_SampleEmissiveTriangleLight(RAB_LightInfo lightInfo, RAB_Surface surface)
{
    RAB_LightSample lightSample = RAB_EmptyLightSample();
    if (!RAB_IsLightInfoValid(lightInfo) || !RAB_IsSurfaceValid(surface))
    {
        return lightSample;
    }

    const float3 toLight = lightInfo.position - RAB_GetSurfaceWorldPos(surface);
    const float lightDistance = sqrt(max(dot(toLight, toLight), 1.0e-6));
    const float3 lightDir = toLight / lightDistance;
    const float lightFacing = saturate(dot(lightInfo.normal, -lightDir));
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

float3 RemixRAB_SampleDirectionCone(float3 axis, float cosThetaMax, float2 uv)
{
    const float cosTheta = lerp(1.0, cosThetaMax, saturate(uv.x));
    const float sinTheta = sqrt(max(0.0, 1.0 - cosTheta * cosTheta));
    const float phi = 2.0 * RTXDI_PI * saturate(uv.y);
    const float3 tangent = RAB_BuildPerpendicular(axis);
    const float3 bitangent = RAB_SafeNormalize(cross(axis, tangent), float3(0.0, 1.0, 0.0));
    return RAB_SafeNormalize(axis * cosTheta + tangent * (cos(phi) * sinTheta) + bitangent * (sin(phi) * sinTheta), axis);
}

float RemixRAB_RaySphereHitT(float3 rayOrigin, float3 rayDirection, float3 sphereCenter, float sphereRadius, float fallbackT)
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

RAB_LightSample RemixRAB_SampleDoomAnalyticSphereLight(RAB_LightInfo lightInfo, RAB_Surface surface, float2 uv)
{
    RAB_LightSample lightSample = RAB_EmptyLightSample();
    if (!RAB_IsLightInfoValid(lightInfo) || !RAB_IsSurfaceValid(surface))
    {
        return lightSample;
    }

    const float3 toCenter = lightInfo.position - RAB_GetSurfaceWorldPos(surface);
    const float centerDistanceSquared = max(dot(toCenter, toCenter), 1.0e-4);
    const float centerDistance = sqrt(centerDistanceSquared);
    const float3 centerDir = toCenter / centerDistance;
    const float doomRadius = max(lightInfo.influenceRadius, 1.0);

    const float sphereRadius = clamp(lightInfo.radius, 0.01, doomRadius);
    const float sinThetaMax = saturate(sphereRadius / centerDistance);
    const float cosThetaMax = sqrt(max(0.0, 1.0 - sinThetaMax * sinThetaMax));
    const float solidAngle = max(2.0 * RTXDI_PI * (1.0 - cosThetaMax), 1.0e-5);
    const float3 sampledDir = RemixRAB_SampleDirectionCone(centerDir, cosThetaMax, uv);
    const float hitT = RemixRAB_RaySphereHitT(RAB_GetSurfaceWorldPos(surface), sampledDir, lightInfo.position, sphereRadius, centerDistance);
    const float3 samplePosition = RAB_GetSurfaceWorldPos(surface) + sampledDir * hitT;
    const float3 sampleNormal = RAB_SafeNormalize(samplePosition - lightInfo.position, -sampledDir);

    lightSample.valid = 1u;
    lightSample.lightType = lightInfo.lightType;
    lightSample.lightIndex = lightInfo.lightIndex;
    lightSample.flags = lightInfo.flags;
    lightSample.position = samplePosition;
    lightSample.normal = sampleNormal;
    lightSample.distance = hitT;
    lightSample.radiance = lightInfo.radiance;
    lightSample.areaPdf = 1.0 / max(lightInfo.area, 1.0e-4);
    lightSample.solidAnglePdf = 1.0 / solidAngle;
    return lightSample;
}

RAB_LightSample RAB_SamplePolymorphicLight(RAB_LightInfo lightInfo, RAB_Surface surface, float2 uv)
{
    if (lightInfo.lightType == RAB_LIGHT_TYPE_EMISSIVE_TRIANGLE)
    {
        return RemixRAB_SampleEmissiveTriangleLight(lightInfo, surface);
    }
    if (lightInfo.lightType == RAB_LIGHT_TYPE_DOOM_ANALYTIC_SPHERE)
    {
        return RemixRAB_SampleDoomAnalyticSphereLight(lightInfo, surface, uv);
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
    RemixRAB_GetLightDirDistance(surface, lightSample, lightDir, lightDistance);
    const float3 brdf = RAB_EvaluateSurfaceBrdf(surface, lightDir, RAB_GetSurfaceViewDir(surface));
    const float ndotl = saturate(dot(RAB_GetSurfaceNormal(surface), lightDir));
    const float3 reflected = brdf * lightSample.radiance * ndotl;
    return RemixRAB_Luminance(reflected) / max(lightSample.solidAnglePdf, 1.0e-6);
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
    return RAB_IsLightInfoValid(lightInfo) ? lightInfo.weight : 0.0;
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
    return RemixRAB_Luminance(brdf * max(radiance, float3(0.0, 0.0, 0.0)) * ndotl);
}

#endif
