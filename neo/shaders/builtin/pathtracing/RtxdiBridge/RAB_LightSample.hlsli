#ifndef RB_PATH_TRACING_RAB_LIGHT_SAMPLE_HLSLI
#define RB_PATH_TRACING_RAB_LIGHT_SAMPLE_HLSLI

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

float RAB_LightSampleSolidAnglePdf(RAB_LightSample lightSample)
{
    return lightSample.solidAnglePdf;
}

#endif
