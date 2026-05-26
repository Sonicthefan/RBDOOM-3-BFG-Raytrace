#ifndef RB_CLEAN_RTXDI_DI_RAB_DOOM_ANALYTIC_REFERENCE_HLSLI
#define RB_CLEAN_RTXDI_DI_RAB_DOOM_ANALYTIC_REFERENCE_HLSLI

bool PathTraceCleanReferenceRabEnabled()
{
    return CleanRtxdiDiReferenceRab != 0u && (CleanRtxdiDiView == 8u || CleanRtxdiDiView == 12u);
}

bool PathTraceCleanReferenceRabProveUseMode()
{
    return CleanRtxdiDiReferenceRab == 2u && (CleanRtxdiDiView == 8u || CleanRtxdiDiView == 12u);
}

bool PathTraceCleanReferenceRabProveRadianceMode()
{
    return (CleanRtxdiDiReferenceRab == 2u || CleanRtxdiDiReferenceRab == 3u) &&
        (CleanRtxdiDiView == 8u || CleanRtxdiDiView == 12u);
}

bool PathTraceCleanReferenceRabProveTargetMode()
{
    return (CleanRtxdiDiReferenceRab == 2u || CleanRtxdiDiReferenceRab == 4u) &&
        (CleanRtxdiDiView == 8u || CleanRtxdiDiView == 12u);
}

float3 PathTraceCleanReferenceRabStableKeyColor(uint key)
{
    uint hash = key * 1664525u + 1013904223u;
    hash ^= hash >> 16u;
    const float r = 0.25 + 0.75 * (float(hash & 255u) / 255.0);
    hash = hash * 1664525u + 1013904223u;
    const float g = 0.25 + 0.75 * (float(hash & 255u) / 255.0);
    hash = hash * 1664525u + 1013904223u;
    const float b = 0.25 + 0.75 * (float(hash & 255u) / 255.0);
    return float3(r, g, b);
}

uint PathTraceCleanReferenceRabPayloadKey(RAB_LightInfo lightInfo)
{
    const uint index = lightInfo.lightIndex;
    if (index < CleanRtxdiDiAnalyticLightCount)
    {
        const PathTraceDoomAnalyticLightCandidate light = DoomAnalyticLights[index];
        return (light.renderLightIndex * 16777619u) ^ light.entityNumber;
    }
    return index;
}

uint PathTraceCleanReferenceRabUniverseKey(RAB_LightInfo lightInfo)
{
    const uint index = lightInfo.lightIndex;
    if (index < CleanRtxdiDiAnalyticIdentityCount)
    {
        const PathTraceDoomAnalyticLightCandidateIdentity identity = DoomAnalyticCurrentIdentities[index];
        if (identity.universeIndex != 0xffffffffu)
        {
            return identity.universeIndex;
        }
    }
    return index;
}

float3 PathTraceCleanReferenceRabRadiance(RAB_LightInfo lightInfo)
{
    if (CleanRtxdiDiReferenceRab == 5u)
    {
        return PathTraceCleanReferenceRabStableKeyColor(lightInfo.lightIndex);
    }
    if (CleanRtxdiDiReferenceRab == 6u ||
        CleanRtxdiDiReferenceRab == 9u ||
        CleanRtxdiDiReferenceRab == 10u)
    {
        return float3(1.0, 1.0, 1.0);
    }
    if (CleanRtxdiDiReferenceRab == 7u)
    {
        return PathTraceCleanReferenceRabStableKeyColor(PathTraceCleanReferenceRabUniverseKey(lightInfo));
    }
    if (CleanRtxdiDiReferenceRab == 8u)
    {
        return PathTraceCleanReferenceRabStableKeyColor(PathTraceCleanReferenceRabPayloadKey(lightInfo));
    }

    return max(lightInfo.radiance, float3(0.0, 0.0, 0.0)) *
        (PathTraceCleanReferenceRabProveRadianceMode() ? float3(0.05, 1.50, 0.05) : float3(1.0, 1.0, 1.0));
}

bool PathTraceCleanReferenceRabFinite3(float3 value)
{
    return all(value == value) && all(abs(value) < float3(3.402823e+38, 3.402823e+38, 3.402823e+38));
}

float3 PathTraceCleanReferenceRabReflectedRadiance(float3 samplePosition, float3 sampleRadiance, RAB_Surface surface)
{
    if (!RAB_IsSurfaceValid(surface) || surface.surfaceClass == 3u || surface.material.opacity <= 0.0)
    {
        return float3(0.0, 0.0, 0.0);
    }

    const float3 toSample = samplePosition - RAB_GetSurfaceWorldPos(surface);
    const float distanceSquared = max(dot(toSample, toSample), 1.0e-6);
    const float3 lightDir = toSample * rsqrt(distanceSquared);
    const float3 normal = RAB_SafeNormalize(RAB_GetSurfaceNormal(surface), RAB_GetSurfaceGeoNormal(surface));
    const float3 geoNormal = RAB_SafeNormalize(RAB_GetSurfaceGeoNormal(surface), normal);
    const float ndotl = saturate(dot(normal, lightDir));
    if (ndotl <= 0.0 || dot(geoNormal, lightDir) <= 0.0)
    {
        return float3(0.0, 0.0, 0.0);
    }

    const float3 diffuseBrdf = GetDiffuseAlbedo(surface.material) * (1.0 / RTXDI_PI);
    return diffuseBrdf * max(sampleRadiance, float3(0.0, 0.0, 0.0)) * ndotl;
}

float PathTraceCleanReferenceRabTargetPdf(RAB_LightSample lightSample, RAB_Surface surface)
{
    if (!RAB_IsReplayableLightSample(lightSample) || lightSample.solidAnglePdf <= 0.0)
    {
        return 0.0;
    }

    const float3 reflectedRadiance = PathTraceCleanReferenceRabReflectedRadiance(lightSample.position, lightSample.radiance, surface);
    const float targetPdf = RAB_Luminance(reflectedRadiance) / max(lightSample.solidAnglePdf, 1.0e-6);
    return PathTraceCleanReferenceRabProveTargetMode() ? targetPdf * 0.03125 : targetPdf;
}

RAB_LightSample PathTraceCleanReferenceRabSampleDoomAnalyticSphereLight(RAB_LightInfo lightInfo, RAB_Surface surface, float2 uv)
{
    RAB_LightSample lightSample = RAB_EmptyLightSample();
    lightSample.lightType = lightInfo.lightType;
    lightSample.lightIndex = lightInfo.lightIndex;
    lightSample.flags = lightInfo.flags;

    if (!RAB_IsLightInfoValid(lightInfo) || !RAB_IsSurfaceValid(surface) ||
        !PathTraceCleanReferenceRabFinite3(lightInfo.position) ||
        !PathTraceCleanReferenceRabFinite3(lightInfo.radiance))
    {
        return lightSample;
    }

    const float sphereRadius = max(lightInfo.radius, 0.01);
    if (sphereRadius >= 3.402823e+38)
    {
        return lightSample;
    }

    const float3 toCenter = lightInfo.position - RAB_GetSurfaceWorldPos(surface);
    const float centerDistanceSquared = max(dot(toCenter, toCenter), 1.0e-6);
    const float centerDistance = sqrt(centerDistanceSquared);
    const float3 centerDir = toCenter / centerDistance;

    float solidAngle = 4.0 * RTXDI_PI;
    float3 sampledDir = centerDir;
    if (centerDistance > sphereRadius * 1.0001)
    {
        const float sinThetaMax = saturate(sphereRadius / centerDistance);
        const float cosThetaMax = sqrt(max(0.0, 1.0 - sinThetaMax * sinThetaMax));
        solidAngle = max(2.0 * RTXDI_PI * (1.0 - cosThetaMax), 1.0e-6);
        sampledDir = RAB_SampleDirectionCone(centerDir, cosThetaMax, uv);
    }

    const float hitT = RAB_RaySphereHitT(RAB_GetSurfaceWorldPos(surface), sampledDir, lightInfo.position, sphereRadius, centerDistance);
    const float3 samplePosition = RAB_GetSurfaceWorldPos(surface) + sampledDir * hitT;

    lightSample.valid = 1u;
    lightSample.position = samplePosition;
    lightSample.normal = RAB_SafeNormalize(samplePosition - lightInfo.position, -sampledDir);
    lightSample.distance = max(hitT, 1.0e-3);
    lightSample.radiance = PathTraceCleanReferenceRabRadiance(lightInfo);
    lightSample.areaPdf = 1.0 / max(4.0 * RTXDI_PI * sphereRadius * sphereRadius, 1.0e-4);
    lightSample.solidAnglePdf = PathTraceCleanReferenceRabProveTargetMode() ? 1.0 : 1.0 / solidAngle;
    return lightSample;
}

#endif
