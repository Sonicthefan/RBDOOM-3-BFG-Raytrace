#ifndef RB_PATH_TRACING_RAB_LIGHT_TARGET_HLSLI
#define RB_PATH_TRACING_RAB_LIGHT_TARGET_HLSLI

#include "RAB_Brdf.hlsli"
#include "RAB_LightSamplingCore.hlsli"

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

#endif
