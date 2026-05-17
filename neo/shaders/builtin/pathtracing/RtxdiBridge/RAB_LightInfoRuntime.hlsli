#ifndef RB_PATH_TRACING_RAB_LIGHT_INFO_RUNTIME_HLSLI
#define RB_PATH_TRACING_RAB_LIGHT_INFO_RUNTIME_HLSLI

#include "RAB_LightInfoCore.hlsli"
#include "RAB_LightIdentityRemap.hlsli"

uint RAB_GetCurrentEmissiveTriangleCount()
{
    return PathTraceSafetyDisabled(RT_PT_SAFETY_DISABLE_EMISSIVE_TRIANGLE_SAMPLING) ? 0u : (uint)max(EmissiveInfo.x, 0.0);
}

uint RAB_GetPreviousEmissiveTriangleCount()
{
    return PathTraceSafetyDisabled(RT_PT_SAFETY_DISABLE_EMISSIVE_TRIANGLE_SAMPLING) ? 0u : (uint)max(DoomAnalyticLightRemapInfo.w, 0.0);
}

uint RAB_GetCurrentDoomAnalyticLightCount()
{
    const uint uploadedAnalyticCount = PathTraceSafetyDisabled(RT_PT_SAFETY_DISABLE_ANALYTIC_LIGHT_LOOP) ? 0u : (uint)max(DoomAnalyticLightInfo.x, 0.0);
    const uint analyticTraceCap = (uint)max(DoomAnalyticLightInfo.y, 0.0);
    return (((uint)max(DoomAnalyticLightInfo.w, 0.0)) & 1u) != 0u && !PathTraceSafetyDisabled(RT_PT_SAFETY_DISABLE_ANALYTIC_LIGHT_LOOP) ? min(uploadedAnalyticCount, analyticTraceCap) : 0u;
}

uint RAB_GetCurrentLightCount()
{
    return RAB_GetCurrentEmissiveTriangleCount() + RAB_GetCurrentDoomAnalyticLightCount();
}

int RAB_TranslateLightIndex(uint lightIndex, bool currentToPrevious)
{
    const uint currentEmissiveTriangleCount = RAB_GetCurrentEmissiveTriangleCount();
    const uint previousEmissiveTriangleCount = RAB_GetPreviousEmissiveTriangleCount();
    const uint analyticCount = RAB_GetCurrentDoomAnalyticLightCount();
    const uint currentIdentityCount = (uint)max(DoomAnalyticLightRemapInfo.x, 0.0);
    const uint previousIdentityCount = (uint)max(DoomAnalyticLightRemapInfo.y, 0.0);
    const uint remapCount = (uint)max(DoomAnalyticLightRemapInfo.z, 0.0);

    if (!currentToPrevious && MotionVectorInfo.y < 0.5)
    {
        return -1;
    }

    if (currentToPrevious)
    {
        if (lightIndex < currentEmissiveTriangleCount)
        {
            const PathTraceEmissiveLightRemap remap = SmokeEmissiveRemap[lightIndex];
            if (!RAB_SmokeEmissiveRemapValid(remap) || remap.currentToPreviousIndex < 0)
            {
                return -1;
            }

            const uint previousEmissiveIndex = (uint)remap.currentToPreviousIndex;
            return previousEmissiveIndex < previousEmissiveTriangleCount ? (int)previousEmissiveIndex : -1;
        }
        const uint currentAnalyticIndex = lightIndex - currentEmissiveTriangleCount;
        if (currentAnalyticIndex >= analyticCount || currentAnalyticIndex >= currentIdentityCount)
        {
            return -1;
        }

        const PathTraceDoomAnalyticLightCandidateIdentity currentIdentity = DoomAnalyticCurrentIdentities[currentAnalyticIndex];
        if (!RAB_DoomAnalyticIdentityValid(currentIdentity) || currentIdentity.universeIndex >= remapCount)
        {
            return -1;
        }

        const PathTraceDoomAnalyticLightRemap remap = DoomAnalyticRemap[currentIdentity.universeIndex];
        if (!RAB_DoomAnalyticRemapValid(remap) || remap.currentToPreviousCandidateIndex < 0)
        {
            return -1;
        }

        const uint previousAnalyticIndex = (uint)remap.currentToPreviousCandidateIndex;
        if (previousAnalyticIndex >= previousIdentityCount)
        {
            return -1;
        }
        if (!RAB_DoomAnalyticLightStateCompatible(DoomAnalyticLights[currentAnalyticIndex], DoomAnalyticPreviousLights[previousAnalyticIndex]))
        {
            return -1;
        }

        return (int)(previousEmissiveTriangleCount + previousAnalyticIndex);
    }

    if (lightIndex < previousEmissiveTriangleCount)
    {
        const PathTraceEmissiveLightRemap remap = SmokeEmissiveRemap[lightIndex];
        if (!RAB_SmokeEmissiveRemapValid(remap) || remap.previousToCurrentIndex < 0)
        {
            return -1;
        }

        const uint currentEmissiveIndex = (uint)remap.previousToCurrentIndex;
        return currentEmissiveIndex < currentEmissiveTriangleCount ? (int)currentEmissiveIndex : -1;
    }

    const uint previousAnalyticIndex = lightIndex - previousEmissiveTriangleCount;
    if (previousAnalyticIndex >= previousIdentityCount)
    {
        return -1;
    }

    const PathTraceDoomAnalyticLightCandidateIdentity previousIdentity = DoomAnalyticPreviousIdentities[previousAnalyticIndex];
    if (!RAB_DoomAnalyticIdentityValid(previousIdentity) || previousIdentity.universeIndex >= remapCount)
    {
        return -1;
    }

    const PathTraceDoomAnalyticLightRemap remap = DoomAnalyticRemap[previousIdentity.universeIndex];
    if (!RAB_DoomAnalyticRemapValid(remap) || remap.previousToCurrentCandidateIndex < 0)
    {
        return -1;
    }

    const uint currentAnalyticIndex = (uint)remap.previousToCurrentCandidateIndex;
    if (currentAnalyticIndex >= analyticCount || currentAnalyticIndex >= currentIdentityCount)
    {
        return -1;
    }
    if (!RAB_DoomAnalyticLightStateCompatible(DoomAnalyticLights[currentAnalyticIndex], DoomAnalyticPreviousLights[previousAnalyticIndex]))
    {
        return -1;
    }

    return (int)(currentEmissiveTriangleCount + currentAnalyticIndex);
}

RAB_LightInfo RAB_LoadLightInfo(uint index, bool previousFrame)
{
    if (previousFrame)
    {
        // RTXDI PT inverse shift asks for a previous-frame light evaluation while
        // carrying the current reservoir light index. Translate it before
        // decoding the previous analytic table.
        const int previousIndex = RAB_TranslateLightIndex(index, true);
        if (previousIndex < 0)
        {
            return RAB_EmptyLightInfo();
        }
        index = (uint)previousIndex;
    }

    const uint emissiveTriangleCount = previousFrame ? RAB_GetPreviousEmissiveTriangleCount() : RAB_GetCurrentEmissiveTriangleCount();
    if (index < emissiveTriangleCount)
    {
        PathTraceSmokeEmissiveTriangle emissiveTriangle;
        if (previousFrame)
        {
            emissiveTriangle = SmokePreviousEmissiveTriangles[index];
        }
        else
        {
            emissiveTriangle = SmokeEmissiveTriangles[index];
        }
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
        const bool structurallyLoadableEmissive = emissiveTriangle.centerAndArea.w > 1.0e-6;
        const float structuralWeightFallback = structurallyLoadableEmissive ? 1.0e-6 : 0.0;
        lightInfo.weight = max(max(emissiveTriangle.sampleWeightAndPdf.x, emissiveTriangle.estimatedRadianceAndLuminance.w), structuralWeightFallback);
        return lightInfo;
    }

    const uint analyticCount = previousFrame ? (uint)max(DoomAnalyticLightRemapInfo.y, 0.0) : RAB_GetCurrentDoomAnalyticLightCount();
    const uint identityCount = previousFrame ? (uint)max(DoomAnalyticLightRemapInfo.y, 0.0) : (uint)max(DoomAnalyticLightRemapInfo.x, 0.0);
    const uint analyticIndex = index - emissiveTriangleCount;
    if (analyticIndex < analyticCount && analyticIndex < identityCount)
    {
        PathTraceDoomAnalyticLightCandidateIdentity identity;
        if (previousFrame)
        {
            identity = DoomAnalyticPreviousIdentities[analyticIndex];
        }
        else
        {
            identity = DoomAnalyticCurrentIdentities[analyticIndex];
        }
        if (!RAB_DoomAnalyticIdentitySampleable(identity))
        {
            return RAB_EmptyLightInfo();
        }

        PathTraceDoomAnalyticLightCandidate analyticLight;
        if (previousFrame)
        {
            analyticLight = DoomAnalyticPreviousLights[analyticIndex];
        }
        else
        {
            analyticLight = DoomAnalyticLights[analyticIndex];
        }
        const float radius = max(analyticLight.originAndRadius.w, 0.01);
        const float influenceRadius = max(analyticLight.doomRadiusAndArea.x, 1.0);
        const float intensityScale = max(DoomAnalyticLightInfo.z, 0.0);
        const float3 radiance = max(analyticLight.colorAndIntensity.rgb, float3(0.0, 0.0, 0.0)) * intensityScale;
        if (!all(analyticLight.originAndRadius.xyz == analyticLight.originAndRadius.xyz) ||
            !all(radiance == radiance) ||
            radius <= 0.0 ||
            influenceRadius <= 0.0)
        {
            return RAB_EmptyLightInfo();
        }

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
        lightInfo.radiance = radiance;
        lightInfo.weight = max(max(lightInfo.radiance.r, lightInfo.radiance.g), lightInfo.radiance.b) * lightInfo.area * influenceRadius;
        return lightInfo;
    }

    return RAB_EmptyLightInfo();
}

#endif
