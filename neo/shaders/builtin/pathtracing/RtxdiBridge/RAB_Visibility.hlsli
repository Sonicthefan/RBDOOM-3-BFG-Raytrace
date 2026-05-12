#ifndef RB_PATH_TRACING_RAB_VISIBILITY_HLSLI
#define RB_PATH_TRACING_RAB_VISIBILITY_HLSLI

#include "RAB_LightSamplingCore.hlsli"

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

#endif
