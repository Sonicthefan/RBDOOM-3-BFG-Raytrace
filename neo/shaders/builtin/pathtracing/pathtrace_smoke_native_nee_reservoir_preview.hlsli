// Purpose:
//     Supplies rbdoom's native smoke reservoir direct-light preview helpers.
//
// rbdoom local context:
//     neo\shaders\builtin\pathtracing\pathtrace_nee.hlsli
//
// NVIDIA reference:
//     E:\prog\references\RTXDI-main\Samples\FullSample\Shaders\LightingPasses\RtxdiApplicationBridge\PathTracer\RAB_PathTracer.hlsli
//     E:\prog\references\RTXDI-main\Samples\FullSample\Shaders\LightingPasses\RtxdiApplicationBridge\PathTracer\RAB_MISCallbacks.hlsli
//
// Current rbdoom supplier:
//     Smoke emissive triangles, native smoke reservoirs, Doom analytic sphere
//     lights, smoke material sampling, and smoke shadow visibility.
//
// Current deviation:
//     This is legacy/native reservoir preview code, not the reference RAB
//     path-tracer NEE path.
//
// Extraction rule:
//     This file must preserve existing behavior until a later correction task.

#ifndef RB_PATH_TRACING_SMOKE_NATIVE_NEE_RESERVOIR_PREVIEW_HLSLI
#define RB_PATH_TRACING_SMOKE_NATIVE_NEE_RESERVOIR_PREVIEW_HLSLI

uint FindSmokeLightCandidateForTriangle(PathTraceSmokeEmissiveTriangle emissiveTriangle, uint candidateCount)
{
    const uint searchCount = min(candidateCount, 64u);
    [loop]
    for (uint candidateIndex = 0u; candidateIndex < searchCount; ++candidateIndex)
    {
        const PathTraceSmokeLightCandidate candidate = SmokeLightCandidates[candidateIndex];
        if (candidate.materialId == emissiveTriangle.materialId &&
            candidate.universeMaterialIndex == emissiveTriangle.universeMaterialIndex)
        {
            return candidateIndex;
        }
    }
    return 0xffffffffu;
}

void StoreSmokeReservoirCurrentSafe(uint reservoirIndex, PathTraceSmokeReservoir reservoir)
{
    if (!PathTraceSafetyDisabled(RT_PT_SAFETY_DISABLE_RESERVOIR_WRITES) &&
        reservoirIndex < PathTraceSmokeReservoirCount())
    {
        SmokeReservoirCurrent[reservoirIndex] = reservoir;
    }
}

float EvaluateSmokeReservoirCandidatePotential(PathTraceSmokeEmissiveTriangle emissiveTriangle, float3 hitPosition, float3 normal, float randomFallbackPdf, out float3 lightDir, out float distance, out float area, out float candidatePdf, out float lightFacing)
{
    const float3 lightCenter = emissiveTriangle.centerAndArea.xyz;
    area = max(emissiveTriangle.centerAndArea.w, 1.0e-4);
    const float3 lightNormal = SafeNormalize(emissiveTriangle.normalAndLuminance.xyz, float3(0.0, 0.0, 1.0));
    const float3 toLight = lightCenter - hitPosition;
    const float distanceSquared = max(dot(toLight, toLight), 1.0);
    distance = sqrt(distanceSquared);
    lightDir = toLight / distance;
    const float ndotl = saturate(dot(normal, lightDir));
    const bool historicalDynamicEmissive = (emissiveTriangle.padding0 & RT_SMOKE_EMISSIVE_TRIANGLE_HISTORY_DYNAMIC) != 0u;
    const bool twoSidedEmissive = !historicalDynamicEmissive && ((((uint)TextureInfo.w) & RT_SMOKE_TEXTURE_FLAG_RESERVOIR_TWO_SIDED_EMISSIVES) != 0u);
    const float lightFacingRaw = dot(lightNormal, -lightDir);
    lightFacing = twoSidedEmissive ? abs(lightFacingRaw) : saturate(lightFacingRaw);
    candidatePdf = max(emissiveTriangle.sampleWeightAndPdf.y, randomFallbackPdf);
    return area * ndotl * lightFacing / distanceSquared / max(candidatePdf, 1.0e-6);
}

float4 EvaluateSmokeReservoirDirectLighting(float3 rayOrigin, float3 rayDirection, PathTraceSmokePayload payload, uint2 pixel)
{
    const uint2 dimensions = PathTraceFullOutputSize();
    const uint reservoirIndex = pixel.y * dimensions.x + pixel.x;

    PathTraceSmokeReservoir reservoir = (PathTraceSmokeReservoir)0;
    reservoir.lightCandidateIndex = 0xffffffffu;
    reservoir.emissiveTriangleIndex = 0xffffffffu;

    if (SmokePayloadIsGuiScreen(payload))
    {
        StoreSmokeReservoirCurrentSafe(reservoirIndex, reservoir);
        return CompositeSmokeGuiLayers(rayOrigin, rayDirection, payload);
    }

    const PathTraceSmokeMaterial material = LoadSmokeMaterial(payload.materialIndex);
    const float3 albedo = SampleSmokeSurfaceAlbedo(material, payload.texCoord, payload.surfaceClass, payload.translucentSubtype, payload.vertexColor, payload.vertexColorAdd).rgb;
    const float3 baseNormal = SafeNormalize(payload.normal, payload.geometricNormal);
    const float3 normal = DecodeSmokeNormalTexture(material, payload.texCoord, baseNormal, payload.tangent, payload.bitangent);
    const float3 hitPosition = rayOrigin + rayDirection * payload.hitT;
    const float ambientScale = saturate(LightSpriteInfo.w);

    const uint seed =
        pixel.x * 1973u ^
        pixel.y * 9277u ^
        payload.materialId * 26699u ^
        ((uint)payload.hitT) * 7919u ^
        payload.materialIndex * 104729u;
    const uint emissiveTriangleCount = PathTraceSafetyDisabled(RT_PT_SAFETY_DISABLE_EMISSIVE_TRIANGLE_SAMPLING) ? 0u : (uint)max(EmissiveInfo.x, 0.0);
    const uint lightCandidateCount = PathTraceSafetyDisabled(RT_PT_SAFETY_DISABLE_EMISSIVE_TRIANGLE_SAMPLING) ? 0u : (uint)max(EmissiveInfo.w, 0.0);
    if (emissiveTriangleCount == 0u)
    {
        StoreSmokeReservoirCurrentSafe(reservoirIndex, reservoir);
        const float3 viewDir = SafeNormalize(rayOrigin - hitPosition, -rayDirection);
        const float3 analyticDirect = EvaluateDoomAnalyticSphereLights(albedo, float3(0.0, 0.0, 0.0), normal, viewDir, hitPosition, seed);
        const float3 debugAnalyticDirect = analyticDirect / (1.0 + analyticDirect);
        return float4(saturate(albedo * ambientScale + debugAnalyticDirect), 1.0);
    }

    const uint reservoirCandidateTrials = clamp((uint)max(EmissiveInfo.z, 1.0), 1u, 16u);
    uint emissiveTriangleIndex = 0u;
    float bestPotential = -1.0;
    [loop]
    for (uint trialIndex = 0u; trialIndex < reservoirCandidateTrials; ++trialIndex)
    {
        const float sampleXi = SmokeHashToUnitFloat(seed ^ (trialIndex + 1u) * 747796405u);
        const uint trialTriangleIndex = SelectSmokeWeightedEmissiveTriangle(emissiveTriangleCount, sampleXi);
        if (trialTriangleIndex >= emissiveTriangleCount)
        {
            continue;
        }
        const PathTraceSmokeEmissiveTriangle trialTriangle = SmokeEmissiveTriangles[trialTriangleIndex];
        float3 trialLightDir;
        float trialDistance;
        float trialArea;
        float trialPdf;
        float trialFacing;
        const float trialPotential = EvaluateSmokeReservoirCandidatePotential(trialTriangle, hitPosition, normal, 1.0 / max((float)emissiveTriangleCount, 1.0), trialLightDir, trialDistance, trialArea, trialPdf, trialFacing);
        if (trialPotential > bestPotential)
        {
            bestPotential = trialPotential;
            emissiveTriangleIndex = trialTriangleIndex;
        }
    }
    if (emissiveTriangleIndex >= emissiveTriangleCount)
    {
        StoreSmokeReservoirCurrentSafe(reservoirIndex, reservoir);
        return float4(saturate(albedo * (ambientScale * 0.04)), 1.0);
    }
    const PathTraceSmokeEmissiveTriangle emissiveTriangle = SmokeEmissiveTriangles[emissiveTriangleIndex];
    float3 lightDir;
    float distance;
    float area;
    float candidatePdf;
    float lightFacing;
    const float selectedPotential = EvaluateSmokeReservoirCandidatePotential(emissiveTriangle, hitPosition, normal, 1.0 / max((float)emissiveTriangleCount, 1.0), lightDir, distance, area, candidatePdf, lightFacing);
    const float distanceSquared = max(distance * distance, 1.0);
    const float ndotl = saturate(dot(normal, lightDir));
    const bool historicalDynamicEmissive = (emissiveTriangle.padding0 & RT_SMOKE_EMISSIVE_TRIANGLE_HISTORY_DYNAMIC) != 0u;
    float3 direct = float3(0.0, 0.0, 0.0);
    float visibility = 0.0;

    if (selectedPotential > 0.0 && ndotl > 0.0 && lightFacing > 0.0)
    {
        const float normalOffsetSign = dot(normal, lightDir) >= 0.0 ? 1.0 : -1.0;
        const float3 shadowOrigin = hitPosition + normal * (normalOffsetSign * 0.75) + lightDir * 0.25;
        const float shadowTMax = max(distance - 0.5, 0.01);
        const uint ignoreInstanceId = historicalDynamicEmissive ? 0xffffffffu : emissiveTriangle.instanceId;
        const uint ignorePrimitiveIndex = historicalDynamicEmissive ? 0xffffffffu : emissiveTriangle.primitiveIndex;
        visibility = TraceSmokeShadowVisibility(shadowOrigin, lightDir, shadowTMax, ignoreInstanceId, ignorePrimitiveIndex, emissiveTriangle.materialId);
        const float3 radiance = max(emissiveTriangle.estimatedRadianceAndLuminance.rgb, float3(0.0, 0.0, 0.0)) * max(ToyPathInfo.z, 0.0);
        const float sampleWeight = area * ndotl * lightFacing * visibility / distanceSquared / max(candidatePdf, 1.0e-6);
        direct = albedo * radiance * sampleWeight;

        reservoir.radianceAndTargetPdf = float4(direct, candidatePdf);
        reservoir.weightSumAndSampleCount = float4(sampleWeight, 1.0, area, distance);
        reservoir.lightCandidateIndex = FindSmokeLightCandidateForTriangle(emissiveTriangle, lightCandidateCount);
        reservoir.emissiveTriangleIndex = emissiveTriangleIndex;
        reservoir.flags = 1u;
    }

    StoreSmokeReservoirCurrentSafe(reservoirIndex, reservoir);
    const float3 viewDir = SafeNormalize(rayOrigin - hitPosition, -rayDirection);
    direct += EvaluateDoomAnalyticSphereLights(albedo, float3(0.0, 0.0, 0.0), normal, viewDir, hitPosition, seed);
    const float3 debugDirect = direct / (1.0 + direct);
    return float4(saturate(albedo * (ambientScale * 0.04) + debugDirect), 1.0);
}

#endif
