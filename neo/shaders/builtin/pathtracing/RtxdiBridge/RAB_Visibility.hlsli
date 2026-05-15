#ifndef RB_PATH_TRACING_RAB_VISIBILITY_HLSLI
#define RB_PATH_TRACING_RAB_VISIBILITY_HLSLI

#include "RAB_LightSamplingCore.hlsli"

bool RAB_GetConservativeVisibility(RAB_Surface surface, RAB_LightSample lightSample)
{
    if (!RAB_IsSurfaceValid(surface) || !RAB_IsReplayableLightSample(lightSample))
    {
        return false;
    }

    float3 lightDir;
    float lightDistance;
    RAB_GetLightDirDistance(surface, lightSample, lightDir, lightDistance);

    const float3 normal = RAB_SafeNormalize(RAB_GetSurfaceNormal(surface), RAB_GetSurfaceGeoNormal(surface));
    if (dot(normal, lightDir) <= 0.0 || dot(RAB_GetSurfaceGeoNormal(surface), lightDir) <= 0.0)
    {
        return false;
    }

    uint ignoreInstanceId = 0xffffffffu;
    uint ignorePrimitiveIndex = 0xffffffffu;
    uint ignoreMaterialId = 0xffffffffu;
    if (lightSample.lightType == RAB_LIGHT_TYPE_EMISSIVE_TRIANGLE && lightSample.lightIndex < RAB_GetCurrentEmissiveTriangleCount())
    {
        const PathTraceSmokeEmissiveTriangle emissiveTriangle = SmokeEmissiveTriangles[lightSample.lightIndex];
        ignoreInstanceId = emissiveTriangle.instanceId;
        ignorePrimitiveIndex = emissiveTriangle.primitiveIndex;
        ignoreMaterialId = emissiveTriangle.materialId;
    }

    const float normalOffsetSign = dot(normal, lightDir) >= 0.0 ? 1.0 : -1.0;
    const float3 shadowOrigin = surface.worldPos + normal * (normalOffsetSign * 0.75) + lightDir * 0.25;
    const float shadowTMax = max(lightDistance - 0.5, 0.01);
    return TraceSmokeShadowVisibility(shadowOrigin, lightDir, shadowTMax, ignoreInstanceId, ignorePrimitiveIndex, ignoreMaterialId) > 0.0;
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
