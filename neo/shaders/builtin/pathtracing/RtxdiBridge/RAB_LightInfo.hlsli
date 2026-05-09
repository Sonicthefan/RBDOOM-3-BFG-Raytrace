#ifndef RB_PATH_TRACING_RAB_LIGHT_INFO_HLSLI
#define RB_PATH_TRACING_RAB_LIGHT_INFO_HLSLI

#include "RAB_LightSample.hlsli"

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

uint RAB_GetCurrentLightCount()
{
    const uint emissiveTriangleCount = PathTraceSafetyDisabled(RT_PT_SAFETY_DISABLE_EMISSIVE_TRIANGLE_SAMPLING) ? 0u : (uint)max(EmissiveInfo.x, 0.0);
    const uint uploadedAnalyticCount = PathTraceSafetyDisabled(RT_PT_SAFETY_DISABLE_ANALYTIC_LIGHT_LOOP) ? 0u : (uint)max(DoomAnalyticLightInfo.x, 0.0);
    const uint analyticTraceCap = (uint)max(DoomAnalyticLightInfo.y, 0.0);
    const uint analyticCount = DoomAnalyticLightInfo.w >= 0.5 && !PathTraceSafetyDisabled(RT_PT_SAFETY_DISABLE_ANALYTIC_LIGHT_LOOP) ? min(uploadedAnalyticCount, analyticTraceCap) : 0u;
    return emissiveTriangleCount + analyticCount;
}

int RAB_TranslateLightIndex(uint lightIndex, bool currentToPrevious)
{
    return lightIndex < RAB_GetCurrentLightCount() ? (int)lightIndex : -1;
}

RAB_LightInfo RAB_LoadLightInfo(uint index, bool previousFrame)
{
    if (previousFrame)
    {
        return RAB_EmptyLightInfo();
    }

    const uint emissiveTriangleCount = PathTraceSafetyDisabled(RT_PT_SAFETY_DISABLE_EMISSIVE_TRIANGLE_SAMPLING) ? 0u : (uint)max(EmissiveInfo.x, 0.0);
    if (index < emissiveTriangleCount)
    {
        const PathTraceSmokeEmissiveTriangle emissiveTriangle = SmokeEmissiveTriangles[index];
        RAB_LightInfo lightInfo = RAB_EmptyLightInfo();
        lightInfo.lightType = RAB_LIGHT_TYPE_EMISSIVE_TRIANGLE;
        lightInfo.lightIndex = index;
        lightInfo.materialIndex = emissiveTriangle.materialIndex;
        lightInfo.flags = emissiveTriangle.flags | emissiveTriangle.padding0;
        lightInfo.position = emissiveTriangle.centerAndArea.xyz;
        lightInfo.radius = 0.0;
        lightInfo.influenceRadius = 0.0;
        lightInfo.normal = RAB_LightInfoSafeNormalize(emissiveTriangle.normalAndLuminance.xyz, float3(0.0, 0.0, 1.0));
        lightInfo.area = max(emissiveTriangle.centerAndArea.w, 1.0e-4);
        lightInfo.radiance = max(emissiveTriangle.estimatedRadianceAndLuminance.rgb, float3(0.0, 0.0, 0.0)) * max(ToyPathInfo.z, 0.0);
        lightInfo.weight = max(emissiveTriangle.sampleWeightAndPdf.x, emissiveTriangle.estimatedRadianceAndLuminance.w);
        return lightInfo;
    }

    const uint uploadedAnalyticCount = PathTraceSafetyDisabled(RT_PT_SAFETY_DISABLE_ANALYTIC_LIGHT_LOOP) ? 0u : (uint)max(DoomAnalyticLightInfo.x, 0.0);
    const uint analyticTraceCap = (uint)max(DoomAnalyticLightInfo.y, 0.0);
    const uint analyticCount = DoomAnalyticLightInfo.w >= 0.5 && !PathTraceSafetyDisabled(RT_PT_SAFETY_DISABLE_ANALYTIC_LIGHT_LOOP) ? min(uploadedAnalyticCount, analyticTraceCap) : 0u;
    const uint analyticIndex = index - emissiveTriangleCount;
    if (analyticIndex < analyticCount)
    {
        const PathTraceDoomAnalyticLightCandidate analyticLight = DoomAnalyticLights[analyticIndex];
        const float radius = max(analyticLight.originAndRadius.w, 0.01);
        const float influenceRadius = max(analyticLight.doomRadiusAndArea.x, 1.0);
        const float intensityScale = max(DoomAnalyticLightInfo.z, 0.0);
        RAB_LightInfo lightInfo = RAB_EmptyLightInfo();
        lightInfo.lightType = RAB_LIGHT_TYPE_DOOM_ANALYTIC_SPHERE;
        lightInfo.lightIndex = index;
        lightInfo.materialIndex = RAB_INVALID_LIGHT_INDEX;
        lightInfo.flags = analyticLight.flags;
        lightInfo.position = analyticLight.originAndRadius.xyz;
        lightInfo.radius = radius;
        lightInfo.influenceRadius = influenceRadius;
        lightInfo.normal = float3(0.0, 0.0, 1.0);
        lightInfo.area = 4.0 * RTXDI_PI * radius * radius;
        lightInfo.radiance = max(analyticLight.colorAndIntensity.rgb, float3(0.0, 0.0, 0.0)) * intensityScale;
        lightInfo.weight = max(max(lightInfo.radiance.r, lightInfo.radiance.g), lightInfo.radiance.b) * lightInfo.area * influenceRadius;
        return lightInfo;
    }

    return RAB_EmptyLightInfo();
}

#endif
