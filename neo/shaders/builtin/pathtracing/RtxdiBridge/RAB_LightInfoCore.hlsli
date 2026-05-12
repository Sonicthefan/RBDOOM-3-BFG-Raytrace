#ifndef RB_PATH_TRACING_RAB_LIGHT_INFO_CORE_HLSLI
#define RB_PATH_TRACING_RAB_LIGHT_INFO_CORE_HLSLI

struct RAB_LightInfo
{
    uint lightType;
    uint lightIndex;
    uint materialIndex;
    uint flags;
    float3 position;
    float radius;
    float influenceRadius;
    float3 normal;
    float area;
    float3 radiance;
    float weight;
};

static const uint RAB_LIGHT_TYPE_EMISSIVE_TRIANGLE = 0u;
static const uint RAB_LIGHT_TYPE_DOOM_ANALYTIC_SPHERE = 1u;
static const uint RAB_INVALID_LIGHT_INDEX = 0xffffffffu;
static const float RAB_DISTANT_LIGHT_DISTANCE = 1.0e8;

float3 RAB_LightInfoSafeNormalize(float3 value, float3 fallback)
{
    const float lengthSquared = dot(value, value);
    return lengthSquared > 1.0e-8 ? value * rsqrt(lengthSquared) : fallback;
}

RAB_LightInfo RAB_EmptyLightInfo()
{
    RAB_LightInfo lightInfo = (RAB_LightInfo)0;
    lightInfo.lightIndex = RAB_INVALID_LIGHT_INDEX;
    return lightInfo;
}

bool RAB_IsLightInfoValid(RAB_LightInfo lightInfo)
{
    return lightInfo.lightIndex != RAB_INVALID_LIGHT_INDEX && lightInfo.weight > 0.0;
}

#endif
