#ifndef RB_PATH_TRACING_RAB_VISIBILITY_HLSLI
#define RB_PATH_TRACING_RAB_VISIBILITY_HLSLI

#include "RAB_LightSamplingCore.hlsli"

uint RAB_RestirPTVisibilityPolicy()
{
    return ((uint)clamp(floor(SafetyInfo.z + 0.5), 0.0, 255.0) >> 4u) & 0x0fu;
}

bool RAB_GetConservativeLightSampleGeometry(RAB_Surface surface, RAB_LightSample lightSample, out float3 lightDir, out float lightDistance)
{
    lightDir = float3(0.0, 0.0, 1.0);
    lightDistance = 0.0;
    if (!RAB_IsSurfaceValid(surface) || !RAB_IsReplayableLightSample(lightSample))
    {
        return false;
    }

    RAB_GetLightDirDistance(surface, lightSample, lightDir, lightDistance);

    const float3 normal = RAB_SafeNormalize(RAB_GetSurfaceNormal(surface), RAB_GetSurfaceGeoNormal(surface));
    if (dot(normal, lightDir) <= 0.0 || dot(RAB_GetSurfaceGeoNormal(surface), lightDir) <= 0.0)
    {
        return false;
    }

    return true;
}

bool RAB_GetStrictLightSampleVisibility(RAB_Surface surface, RAB_LightSample lightSample)
{
    float3 lightDir;
    float lightDistance;
    if (!RAB_GetConservativeLightSampleGeometry(surface, lightSample, lightDir, lightDistance))
    {
        return false;
    }

    uint ignoreInstanceId = surface.instanceId;
    uint ignorePrimitiveIndex = surface.primitiveIndex;
    uint ignoreMaterialId = surface.materialId;
    if (lightSample.lightType == RAB_LIGHT_TYPE_EMISSIVE_TRIANGLE && lightSample.lightIndex < RAB_GetCurrentEmissiveTriangleCount())
    {
        const PathTraceSmokeEmissiveTriangle emissiveTriangle = SmokeEmissiveTriangles[lightSample.lightIndex];
        ignoreInstanceId = emissiveTriangle.instanceId;
        ignorePrimitiveIndex = emissiveTriangle.primitiveIndex;
        ignoreMaterialId = emissiveTriangle.materialId;
    }

    const float3 normal = RAB_SafeNormalize(RAB_GetSurfaceNormal(surface), RAB_GetSurfaceGeoNormal(surface));
    const float normalOffsetSign = dot(normal, lightDir) >= 0.0 ? 1.0 : -1.0;
    const float3 shadowOrigin = surface.worldPos + normal * (normalOffsetSign * 0.75) + lightDir * 0.25;
    const float shadowTMax = max(lightDistance - 0.5, 0.01);
    return TraceSmokeShadowVisibility(shadowOrigin, lightDir, shadowTMax, ignoreInstanceId, ignorePrimitiveIndex, ignoreMaterialId) > 0.0;
}

bool RAB_GetCandidateNeeVisibility(RAB_Surface surface, RAB_LightSample lightSample)
{
    if (RAB_RestirPTVisibilityPolicy() >= 2u)
    {
        return RAB_GetStrictLightSampleVisibility(surface, lightSample);
    }

    float3 lightDir;
    float lightDistance;
    return RAB_GetConservativeLightSampleGeometry(surface, lightSample, lightDir, lightDistance);
}

bool RAB_GetSelectedNeeVisibility(RAB_Surface surface, RAB_LightSample lightSample)
{
    if (RAB_RestirPTVisibilityPolicy() >= 1u)
    {
        return RAB_GetStrictLightSampleVisibility(surface, lightSample);
    }

    float3 lightDir;
    float lightDistance;
    return RAB_GetConservativeLightSampleGeometry(surface, lightSample, lightDir, lightDistance);
}

bool RAB_GetConservativeVisibility(RAB_Surface surface, RAB_LightSample lightSample)
{
    return RAB_GetStrictLightSampleVisibility(surface, lightSample);
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
    const float3 normal = RAB_SafeNormalize(RAB_GetSurfaceNormal(surface), RAB_GetSurfaceGeoNormal(surface));
    if (dot(normal, sampleDir) <= 0.0 || dot(RAB_GetSurfaceGeoNormal(surface), sampleDir) <= 0.0)
    {
        return 0.0;
    }

    const float normalOffsetSign = dot(normal, sampleDir) >= 0.0 ? 1.0 : -1.0;
    const float3 shadowOrigin = surface.worldPos + normal * (normalOffsetSign * 0.75) + sampleDir * 0.25;
    const float shadowTMax = max(sqrt(distanceSquared) - 0.5, 0.01);
    return TraceSmokeShadowVisibility(shadowOrigin, sampleDir, shadowTMax, 0xffffffffu, 0xffffffffu, 0xffffffffu);
}

#endif
