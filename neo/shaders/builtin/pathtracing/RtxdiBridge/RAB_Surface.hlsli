#ifndef RB_PATH_TRACING_RAB_SURFACE_HLSLI
#define RB_PATH_TRACING_RAB_SURFACE_HLSLI

#include "RAB_Material.hlsli"
#include "RAB_RandomSamplerState.hlsli"

struct RAB_Surface
{
    uint valid;
    float3 worldPos;
    float linearDepth;
    float3 geometryNormal;
    uint materialId;
    float3 shadingNormal;
    uint materialIndex;
    float3 viewDir;
    uint instanceId;
    uint primitiveIndex;
    uint surfaceClass;
    uint flags;
    RAB_Material material;
};

RAB_Surface RAB_EmptySurface()
{
    RAB_Surface surface = (RAB_Surface)0;
    surface.material = RAB_EmptyMaterial();
    return surface;
}

bool RAB_IsSurfaceValid(RAB_Surface surface)
{
    return surface.valid != 0;
}

float3 RAB_GetSurfaceWorldPos(RAB_Surface surface)
{
    return surface.worldPos;
}

void RAB_SetSurfaceWorldPos(inout RAB_Surface surface, float3 worldPos)
{
    surface.worldPos = worldPos;
}

float3 RAB_GetSurfaceNormal(RAB_Surface surface)
{
    return surface.shadingNormal;
}

float3 RAB_GetSurfaceGeoNormal(RAB_Surface surface)
{
    return surface.geometryNormal;
}

void RAB_SetSurfaceNormal(inout RAB_Surface surface, float3 normal)
{
    surface.shadingNormal = normal;
}

float3 RAB_GetSurfaceViewDir(RAB_Surface surface)
{
    return surface.viewDir;
}

float RAB_GetSurfaceLinearDepth(RAB_Surface surface)
{
    return surface.linearDepth;
}

RAB_Material RAB_GetMaterial(RAB_Surface surface)
{
    return surface.material;
}

float RAB_GetSurfaceRoughness(RAB_Surface surface)
{
    return GetRoughness(surface.material);
}

float3 RAB_SafeNormalize(float3 value, float3 fallback)
{
    const float lengthSquared = dot(value, value);
    return lengthSquared > 1.0e-8 ? value * rsqrt(lengthSquared) : fallback;
}

float3 RAB_BuildPerpendicular(float3 normal)
{
    const float3 axis = abs(normal.z) < 0.999 ? float3(0.0, 0.0, 1.0) : float3(0.0, 1.0, 0.0);
    return RAB_SafeNormalize(cross(axis, normal), float3(1.0, 0.0, 0.0));
}

bool RAB_SurfaceSupportsOpaqueDiffuseBrdf(RAB_Surface surface)
{
    return RAB_IsSurfaceValid(surface) && surface.surfaceClass != 3u && surface.material.opacity > 0.0;
}

float3 RAB_CosineHemisphereDirection(float3 normal, float2 randomValues)
{
    const float phi = 2.0 * RTXDI_PI * randomValues.x;
    const float radius = sqrt(saturate(randomValues.y));
    const float x = cos(phi) * radius;
    const float y = sin(phi) * radius;
    const float z = sqrt(max(0.0, 1.0 - saturate(randomValues.y)));
    const float3 tangent = RAB_BuildPerpendicular(normal);
    const float3 bitangent = RAB_SafeNormalize(cross(normal, tangent), float3(0.0, 1.0, 0.0));
    return RAB_SafeNormalize(tangent * x + bitangent * y + normal * z, normal);
}

bool RAB_GetSurfaceBrdfSample(RAB_Surface surface, inout RAB_RandomSamplerState rng, out float3 dir)
{
    dir = float3(0.0, 0.0, 0.0);
    if (!RAB_SurfaceSupportsOpaqueDiffuseBrdf(surface))
    {
        return false;
    }

    const float3 normal = RAB_SafeNormalize(RAB_GetSurfaceNormal(surface), RAB_GetSurfaceGeoNormal(surface));
    const float2 randomValues = float2(RAB_GetNextRandom(rng), RAB_GetNextRandom(rng));
    dir = RAB_CosineHemisphereDirection(normal, randomValues);
    return dot(normal, dir) > 0.0 && dot(RAB_GetSurfaceGeoNormal(surface), dir) > 0.0;
}

float RAB_GetSurfaceBrdfPdf(RAB_Surface surface, float3 dir)
{
    if (!RAB_SurfaceSupportsOpaqueDiffuseBrdf(surface))
    {
        return 0.0;
    }

    const float3 normal = RAB_SafeNormalize(RAB_GetSurfaceNormal(surface), RAB_GetSurfaceGeoNormal(surface));
    const float ndotDir = saturate(dot(normal, RAB_SafeNormalize(dir, normal)));
    return dot(RAB_GetSurfaceGeoNormal(surface), dir) > 0.0 ? ndotDir / RTXDI_PI : 0.0;
}

float3 RAB_EvaluateSurfaceBrdf(RAB_Surface surface, float3 wi, float3 wo)
{
    if (!RAB_SurfaceSupportsOpaqueDiffuseBrdf(surface))
    {
        return float3(0.0, 0.0, 0.0);
    }

    const float3 normal = RAB_SafeNormalize(RAB_GetSurfaceNormal(surface), RAB_GetSurfaceGeoNormal(surface));
    if (dot(normal, wi) <= 0.0 || dot(normal, wo) <= 0.0 ||
        dot(RAB_GetSurfaceGeoNormal(surface), wi) <= 0.0 ||
        dot(RAB_GetSurfaceGeoNormal(surface), wo) <= 0.0)
    {
        return float3(0.0, 0.0, 0.0);
    }

    return GetDiffuseAlbedo(surface.material) * (1.0 / RTXDI_PI);
}

float3 RAB_EvaluateSurfaceBrdfOverPdf(RAB_Surface surface, float3 wi, float3 wo)
{
    const float pdf = RAB_GetSurfaceBrdfPdf(surface, wi);
    if (pdf <= 1.0e-6)
    {
        return float3(0.0, 0.0, 0.0);
    }

    const float3 normal = RAB_SafeNormalize(RAB_GetSurfaceNormal(surface), RAB_GetSurfaceGeoNormal(surface));
    const float ndotWi = saturate(dot(normal, RAB_SafeNormalize(wi, normal)));
    return RAB_EvaluateSurfaceBrdf(surface, wi, wo) * (ndotWi / pdf);
}

bool RAB_SurfaceImportanceSampleBrdf(RAB_Surface surface, inout RAB_RandomSamplerState rng, out float3 dir)
{
    return RAB_GetSurfaceBrdfSample(surface, rng, dir);
}

float RAB_SurfaceEvaluateBrdfPdf(RAB_Surface surface, float3 dir)
{
    return RAB_GetSurfaceBrdfPdf(surface, dir);
}

#endif
