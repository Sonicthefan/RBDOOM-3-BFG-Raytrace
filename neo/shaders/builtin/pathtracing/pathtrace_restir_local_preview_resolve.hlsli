// Purpose:
//     Supplies rbdoom data for the local combined ReSTIR PT preview/final resolve.
//
// NVIDIA reference:
//     E:\prog\references\RTXDI-main\Samples\FullSample\Shaders\LightingPasses\PT\FinalShading.hlsl
//     E:\prog\references\RTXDI-main\Samples\FullSample\Shaders\LightingPasses\ShadingHelpers.hlsli
//
// Current rbdoom supplier:
//     Local direct-light preview, GI reservoir preview, screen-space reflection
//     preview, and RR input color writes.
//
// Current deviation:
//     This is rbdoom's local combined preview path, not NVIDIA PT FinalShading.
//     It does not split diffuse/specular output or store PSR data.
//
// Extraction rule:
//     This file must preserve existing behavior until a later correction task.

#ifndef RB_PATH_TRACING_RESTIR_LOCAL_PREVIEW_RESOLVE_HLSLI
#define RB_PATH_TRACING_RESTIR_LOCAL_PREVIEW_RESOLVE_HLSLI

uint RestirPTReflectionPreviewMode()
{
    return (uint)clamp(floor(GeometryInfo3.w + 0.5), 0.0, 2.0);
}

bool RestirPTSurfaceSupportsReflectionPreview(RAB_Surface surface, uint reflectionMode)
{
    if (reflectionMode == 0u || !RAB_IsSurfaceValid(surface) || !RAB_SurfaceSupportsOpaqueDiffuseBrdf(surface))
    {
        return false;
    }

    const float roughnessLimit = reflectionMode == 1u ? 0.35 : 0.70;
    const float roughness = saturate(RAB_GetSurfaceRoughness(surface));
    const float specularMax = max(max(surface.material.specularF0.x, surface.material.specularF0.y), surface.material.specularF0.z);
    return roughness <= roughnessLimit && specularMax >= 0.035;
}

bool RestirPTProjectWorldToCurrentPixel(float3 worldPosition, uint2 dimensions, out float2 pixelFloat, out float forwardDistance)
{
    pixelFloat = float2(-1.0, -1.0);
    forwardDistance = 0.0;
    const float3 delta = worldPosition - CameraOriginAndTMax.xyz;
    forwardDistance = dot(delta, CameraForwardAndTanX.xyz);
    if (forwardDistance <= 0.05)
    {
        return false;
    }

    const float ndcX = -dot(delta, CameraLeftAndTanY.xyz) / max(forwardDistance * CameraForwardAndTanX.w, 1.0e-5);
    const float ndcY = -dot(delta, CameraUpAndDebugMode.xyz) / max(forwardDistance * CameraLeftAndTanY.w, 1.0e-5);
    if (abs(ndcX) > 1.0 || abs(ndcY) > 1.0)
    {
        return false;
    }

    pixelFloat = (float2(ndcX, ndcY) * 0.5 + 0.5) * float2(dimensions);
    return pixelFloat.x >= 0.0 && pixelFloat.y >= 0.0 &&
        pixelFloat.x < (float)dimensions.x && pixelFloat.y < (float)dimensions.y;
}

RestirPTCombinedLighting RestirPTEvaluateCombinedLightingNoReflection(RAB_Surface surface, uint2 pixel)
{
    RestirPTCombinedLighting result;
    result.preview = float3(0.0, 0.0, 0.0);
    result.hdrRadiance = float3(0.0, 0.0, 0.0);

    if (!RAB_IsSurfaceValid(surface))
    {
        return result;
    }

    const float3 emissiveRadiance = RestirPTSanitizeHdrRadiance(surface.material.emissiveRadiance);
    if (!RAB_SurfaceSupportsOpaqueDiffuseBrdf(surface))
    {
        result.preview = RestirPTVisibleSurfaceBase(surface);
        result.hdrRadiance = emissiveRadiance;
        return result;
    }

    const float3 surfaceBase = RestirPTVisibleSurfaceBase(surface);
    float3 directContribution = float3(0.0, 0.0, 0.0);
    const bool directValid = RestirPTTryEvaluateSpatialDirectLighting(surface, pixel, directContribution);
    const RTXDI_PTReservoir giReservoir = LoadRestirPTInitialReservoir(pixel);
    const bool giValid = RestirPTReservoirHasUsefulSample(giReservoir);
    const float giVisibility = (RestirPTInfo.z >= 0.5 && giValid) ? RestirPTTraceReservoirVisibility(surface, giReservoir) : 1.0;
    const bool giVisible = giValid && giVisibility > 0.0;
    const float3 giContribution = giVisible ? RestirPTReservoirPreviewContribution(giReservoir) * giVisibility : float3(0.0, 0.0, 0.0);
    const float3 lightingRadiance =
        (directValid ? directContribution : float3(0.0, 0.0, 0.0)) +
        (giVisible ? giContribution : float3(0.0, 0.0, 0.0));
    result.hdrRadiance = RestirPTSanitizeHdrRadiance(emissiveRadiance + lightingRadiance);
    if (!directValid && !giVisible)
    {
        result.preview = surfaceBase;
        return result;
    }

    result.preview = saturate(surfaceBase + RestirPTToneMapPreview(lightingRadiance));
    return result;
}

float3 RestirPTCombinedLightingNoReflection(RAB_Surface surface, uint2 pixel)
{
    return RestirPTEvaluateCombinedLightingNoReflection(surface, pixel).preview;
}

bool RestirPTTryFindScreenSpaceReflectionHit(RAB_Surface surface, uint2 pixel, uint reflectionMode, out RAB_Surface hitSurface, out uint2 hitPixel)
{
    hitSurface = RAB_EmptySurface();
    hitPixel = pixel;
    if (!RestirPTSurfaceSupportsReflectionPreview(surface, reflectionMode))
    {
        return false;
    }

    const uint2 dimensions = PathTraceFullOutputSize();
    const float3 normal = RAB_SafeNormalize(RAB_GetSurfaceNormal(surface), RAB_GetSurfaceGeoNormal(surface));
    const float3 viewDir = RAB_SafeNormalize(RAB_GetSurfaceViewDir(surface), -normal);
    const float3 reflectionDir = RAB_SafeNormalize(reflect(-viewDir, normal), normal);
    if (dot(reflectionDir, normal) <= 0.01)
    {
        return false;
    }

    const float roughness = saturate(RAB_GetSurfaceRoughness(surface));
    const float3 rayOrigin = surface.worldPos + normal * 0.75 + reflectionDir * 0.25;
    const float maxDistance = min(max(CameraOriginAndTMax.w, 1.0), max(ToyPathInfo.x, 256.0));
    float rayT = 2.0;
    const uint stepCount = reflectionMode == 1u ? 40u : 28u;

    [loop]
    for (uint step = 0u; step < stepCount && rayT < maxDistance; ++step)
    {
        const float3 probePosition = rayOrigin + reflectionDir * rayT;
        float2 probePixelFloat;
        float probeForwardDistance;
        if (RestirPTProjectWorldToCurrentPixel(probePosition, dimensions, probePixelFloat, probeForwardDistance))
        {
            const int2 candidatePixelInt = int2(floor(probePixelFloat));
            if (candidatePixelInt.x >= 0 && candidatePixelInt.y >= 0 &&
                (uint)candidatePixelInt.x < dimensions.x && (uint)candidatePixelInt.y < dimensions.y)
            {
                const RAB_Surface candidate = LoadPathTracePrimarySurfaceRecord(candidatePixelInt, false);
                const bool samePrimitive =
                    candidate.instanceId == surface.instanceId &&
                    candidate.primitiveIndex == surface.primitiveIndex &&
                    candidate.materialId == surface.materialId;
                if (RAB_IsSurfaceValid(candidate) && !samePrimitive)
                {
                    const float3 toCandidate = candidate.worldPos - rayOrigin;
                    const float alongRay = dot(toCandidate, reflectionDir);
                    const float3 closest = rayOrigin + reflectionDir * alongRay;
                    const float missDistance = length(candidate.worldPos - closest);
                    const float thickness = max(2.0, alongRay * (reflectionMode == 1u ? 0.018 : 0.035) + roughness * 24.0);
                    const float normalFacing = dot(RAB_GetSurfaceGeoNormal(candidate), -reflectionDir);
                    if (alongRay > 0.0 && missDistance <= thickness && normalFacing > -0.35)
                    {
                        hitSurface = candidate;
                        hitPixel = uint2(candidatePixelInt);
                        return true;
                    }
                }
            }
        }

        rayT += max(3.0, rayT * (reflectionMode == 1u ? 0.10 : 0.15));
    }

    return false;
}

float3 RestirPTScreenSpaceReflectionPreview(RAB_Surface surface, uint2 pixel)
{
    const uint reflectionMode = RestirPTReflectionPreviewMode();
    RAB_Surface hitSurface;
    uint2 hitPixel;
    if (!RestirPTTryFindScreenSpaceReflectionHit(surface, pixel, reflectionMode, hitSurface, hitPixel))
    {
        return float3(0.0, 0.0, 0.0);
    }

    const float roughness = saturate(RAB_GetSurfaceRoughness(surface));
    const float specularMax = max(max(surface.material.specularF0.x, surface.material.specularF0.y), surface.material.specularF0.z);
    const float roughnessFade = reflectionMode == 1u
        ? saturate((0.38 - roughness) / 0.38)
        : saturate((0.75 - roughness) / 0.75);
    const float reflectionWeight = saturate(specularMax) * roughnessFade;
    return RestirPTCombinedLightingNoReflection(hitSurface, hitPixel) * reflectionWeight;
}

float4 EvaluateCombinedResolve(RAB_Surface surface, uint2 pixel)
{
    const RestirPTCombinedLighting lighting = RestirPTEvaluateCombinedLightingNoReflection(surface, pixel);
    if (!RestirPTSurfaceSupportsReflectionPreview(surface, RestirPTReflectionPreviewMode()))
    {
        PathTraceRRInputColor[pixel] = float4(lighting.hdrRadiance, 1.0);
        return float4(lighting.preview, 1.0);
    }

    const float3 reflectionPreview = RestirPTSanitizeHdrRadiance(RestirPTReflectionOutput[pixel].rgb);
    PathTraceRRInputColor[pixel] = float4(RestirPTSanitizeHdrRadiance(lighting.hdrRadiance + reflectionPreview), 1.0);
    return float4(saturate(lighting.preview + reflectionPreview), 1.0);
}

#endif
