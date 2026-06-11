#ifndef RB_PATH_TRACING_RAB_BRDF_HLSLI
#define RB_PATH_TRACING_RAB_BRDF_HLSLI

#include "RAB_SurfaceCore.hlsli"
#include "RAB_RandomSamplerState.hlsli"

static const float RB_RAB_BRDF_PI = 3.14159265358979323846;

float3 RAB_CosineHemisphereDirection(float3 normal, float2 randomValues)
{
    const float phi = 2.0 * RB_RAB_BRDF_PI * randomValues.x;
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
    return dot(RAB_GetSurfaceGeoNormal(surface), dir) > 0.0 ? ndotDir / RB_RAB_BRDF_PI : 0.0;
}

float3 RAB_EvaluateSurfaceBrdf(RAB_Surface surface, float3 wi, float3 wo)
{
    if (!RAB_SurfaceSupportsOpaqueDiffuseBrdf(surface))
    {
        return float3(0.0, 0.0, 0.0);
    }

    const float3 normal = RAB_SafeNormalize(RAB_GetSurfaceNormal(surface), RAB_GetSurfaceGeoNormal(surface));
#ifdef RB_RAB_CLEAN_DIAGNOSTIC_RELAX_BRDF_GATES
    if ((CleanRtxdiDiFlags & CLEAN_RAB_DIAGNOSTIC_RELAX_BRDF_GATES) != 0u)
    {
        if (dot(normal, wi) <= 0.0)
        {
            return float3(0.0, 0.0, 0.0);
        }
        return GetDiffuseAlbedo(surface.material) * (1.0 / RB_RAB_BRDF_PI);
    }
#endif
    if (dot(normal, wi) <= 0.0 || dot(normal, wo) <= 0.0 ||
        dot(RAB_GetSurfaceGeoNormal(surface), wi) <= 0.0 ||
        dot(RAB_GetSurfaceGeoNormal(surface), wo) <= 0.0)
    {
        return float3(0.0, 0.0, 0.0);
    }

    return GetDiffuseAlbedo(surface.material) * (1.0 / RB_RAB_BRDF_PI);
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

bool RAB_SurfaceImportanceSampleBrdf(RAB_Surface surface, inout RTXDI_RandomSamplerState rng, out float3 dir)
{
    RAB_RandomSamplerState rabRng = RAB_CreateRandomSamplerFromDirectSeed(rng.seed, rng.index);
    const bool valid = RAB_GetSurfaceBrdfSample(surface, rabRng, dir);
    rng.seed = rabRng.seed;
    rng.index = rabRng.index;
    return valid;
}

float RAB_SurfaceEvaluateBrdfPdf(RAB_Surface surface, float3 dir)
{
    return RAB_GetSurfaceBrdfPdf(surface, dir);
}

#endif
