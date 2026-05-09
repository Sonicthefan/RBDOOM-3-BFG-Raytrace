#ifndef RB_PATH_TRACING_RAB_LIGHT_SAMPLE_HLSLI
#define RB_PATH_TRACING_RAB_LIGHT_SAMPLE_HLSLI

// Bridge light sample consumed by the local RTXDI/ReSTIR callbacks. This is a
// replayable single-light sample, not an accumulated full-loop lighting result.
// lightIndex is the global RAB light index used by reservoirs, position is the
// sampled point or proxy point, radiance is the evaluable source radiance for
// that sample, solidAnglePdf is the directional sampling PDF from the current
// surface, and areaPdf is informational until an area-domain sampler needs it.
struct RAB_LightSample
{
    uint valid;
    uint lightType;
    uint lightIndex;
    uint flags;
    float3 position;
    float solidAnglePdf;
    float3 normal;
    float distance;
    float3 radiance;
    float areaPdf;
};

RAB_LightSample RAB_EmptyLightSample()
{
    RAB_LightSample lightSample = (RAB_LightSample)0;
    return lightSample;
}

float3 RAB_LightSamplePosition(RAB_LightSample lightSample)
{
    return lightSample.position;
}

float3 RAB_LightSampleRadiance(RAB_LightSample lightSample)
{
    return lightSample.radiance;
}

bool RAB_IsAnalyticLightSample(RAB_LightSample lightSample)
{
    return lightSample.lightType == 1u;
}

bool RAB_IsFinitePositive(float value)
{
    return value > 0.0 && value < 3.402823e+38 && value == value;
}

bool RAB_IsReplayableLightSample(RAB_LightSample lightSample)
{
    return
        lightSample.valid != 0u &&
        lightSample.solidAnglePdf > 0.0 &&
        RAB_IsFinitePositive(lightSample.distance) &&
        RAB_IsFinitePositive(lightSample.solidAnglePdf);
}

float RAB_LightSampleSolidAnglePdf(RAB_LightSample lightSample)
{
    return lightSample.solidAnglePdf;
}

#endif
