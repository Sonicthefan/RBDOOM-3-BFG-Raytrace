// Purpose:
//     Supplies rbdoom data for RTXDI/RAB light-index translation and light
//     loading.
//
// NVIDIA reference:
//     E:\prog\references\RTXDI-main\Samples\FullSample\Shaders\LightingPasses\RtxdiApplicationBridge\RAB_LightInfo.hlsli
//     E:\prog\references\RTXDI-main\Samples\FullSample\Shaders\LightingPasses\RtxdiApplicationBridge\RAB_Buffers.hlsli
//     E:\prog\references\RTXDI-main\Libraries\Rtxdi\Include\Rtxdi\PT\HybridShift.hlsli
//     E:\prog\references\RTXDI-main\Libraries\Rtxdi\Include\Rtxdi\DI\TemporalResampling.hlsli
//     E:\prog\references\RTXDI-main\Libraries\Rtxdi\Include\Rtxdi\DI\SpatioTemporalResampling.hlsli
//
// Current rbdoom supplier:
//     Smoke emissive current/previous arrays, Doom analytic current/previous
//     arrays, remap buffers, and local light-domain count helpers.
//
// Current deviation:
//     rbdoom adapts two local light domains into the RTXDI RAB light load and
//     cross-frame light-index mapping shape.
//
// Extraction rule:
//     This file must preserve existing behavior until a later correction task.

#ifndef RB_PATH_TRACING_RAB_LIGHT_LOAD_RUNTIME_HLSLI
#define RB_PATH_TRACING_RAB_LIGHT_LOAD_RUNTIME_HLSLI

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

RAB_LightInfo RAB_LoadSplitLightInfo(uint index, bool previousFrame)
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

uint RAB_GetCurrentUnifiedLightCount()
{
    return (uint)max(UnifiedLightInfo.x, 0.0);
}

uint RAB_GetPreviousUnifiedLightCount()
{
    return (uint)max(UnifiedLightInfo.y, 0.0);
}

uint RAB_GetUnifiedLightRemapCount()
{
    return (uint)max(UnifiedLightInfo.w, 0.0);
}

bool RAB_UnifiedLightLoadEnabled()
{
    return UnifiedLightInfo.z >= 0.5;
}

bool RAB_UnifiedDoomAnalyticRecordSampleable(PathTraceUnifiedLightRecord record)
{
    return record.sourceIndex != PATH_TRACE_UNIFIED_LIGHT_INVALID_INDEX &&
        (record.flags & RAB_DOOM_ANALYTIC_IDENTITY_VALID) != 0u &&
        (record.flags & RAB_DOOM_ANALYTIC_IDENTITY_SAMPLEABLE) != 0u;
}

RAB_LightInfo RAB_BuildLightInfoFromUnifiedRecord(PathTraceUnifiedLightRecord record, uint unifiedIndex)
{
    if (record.type == PATH_TRACE_UNIFIED_LIGHT_TYPE_EMISSIVE_TRIANGLE)
    {
        if (record.sourceIndex == PATH_TRACE_UNIFIED_LIGHT_INVALID_INDEX ||
            record.normalAndArea.w <= 1.0e-6 ||
            record.sourceWeight <= 0.0)
        {
            return RAB_EmptyLightInfo();
        }

        RAB_LightInfo lightInfo = RAB_EmptyLightInfo();
        lightInfo.lightType = RAB_LIGHT_TYPE_EMISSIVE_TRIANGLE;
        lightInfo.lightIndex = unifiedIndex;
        lightInfo.materialIndex = record.materialOrLightId;
        lightInfo.flags = record.flags;
        lightInfo.position = record.positionAndRadius.xyz;
        lightInfo.radius = 0.0;
        lightInfo.influenceRadius = 0.0;
        lightInfo.normal = RAB_LightInfoSafeNormalize(record.normalAndArea.xyz, float3(0.0, 0.0, 1.0));
        lightInfo.area = max(record.normalAndArea.w, 1.0e-4);
        lightInfo.radiance = max(record.radianceAndLuminance.rgb, float3(0.0, 0.0, 0.0)) * max(ToyPathInfo.z, 0.0);
        lightInfo.weight = record.sourceWeight;
        return lightInfo;
    }

    if (record.type == PATH_TRACE_UNIFIED_LIGHT_TYPE_DOOM_ANALYTIC)
    {
        const float radius = max(record.positionAndRadius.w, 0.01);
        const float influenceRadius = max(record.uvOrDoomParams.x, 1.0);
        const float intensityScale = max(DoomAnalyticLightInfo.z, 0.0);
        const float3 radiance = max(record.radianceAndLuminance.rgb, float3(0.0, 0.0, 0.0)) * intensityScale;
        if (!RAB_UnifiedDoomAnalyticRecordSampleable(record) ||
            !all(record.positionAndRadius.xyz == record.positionAndRadius.xyz) ||
            !all(radiance == radiance) ||
            radius <= 0.0 ||
            influenceRadius <= 0.0 ||
            record.sourceWeight <= 0.0)
        {
            return RAB_EmptyLightInfo();
        }

        RAB_LightInfo lightInfo = RAB_EmptyLightInfo();
        lightInfo.lightType = RAB_LIGHT_TYPE_DOOM_ANALYTIC_SPHERE;
        lightInfo.lightIndex = unifiedIndex;
        lightInfo.materialIndex = RAB_INVALID_LIGHT_INDEX;
        lightInfo.flags = record.flags;
        lightInfo.position = record.positionAndRadius.xyz;
        lightInfo.radius = radius;
        lightInfo.influenceRadius = influenceRadius;
        lightInfo.normal = float3(0.0, 0.0, 1.0);
        lightInfo.area = max(record.normalAndArea.w, 1.0e-4);
        lightInfo.radiance = radiance;
        lightInfo.weight = record.sourceWeight * intensityScale;
        return lightInfo;
    }

    return RAB_EmptyLightInfo();
}

RAB_LightInfo RAB_LoadUnifiedLightInfo(uint index, bool previousFrame)
{
    if (previousFrame)
    {
        if (MotionVectorInfo.y < 0.5 || index >= RAB_GetCurrentUnifiedLightCount() || index >= RAB_GetUnifiedLightRemapCount())
        {
            return RAB_EmptyLightInfo();
        }

        const uint previousIndex = PathTraceUnifiedLightRemap[index];
        if (previousIndex == PATH_TRACE_UNIFIED_LIGHT_INVALID_INDEX || previousIndex >= RAB_GetPreviousUnifiedLightCount())
        {
            return RAB_EmptyLightInfo();
        }
        return RAB_BuildLightInfoFromUnifiedRecord(PathTraceUnifiedPreviousLights[previousIndex], previousIndex);
    }

    if (index >= RAB_GetCurrentUnifiedLightCount())
    {
        return RAB_EmptyLightInfo();
    }
    return RAB_BuildLightInfoFromUnifiedRecord(PathTraceUnifiedLights[index], index);
}

RAB_LightInfo RAB_LoadLightInfo(uint index, bool previousFrame)
{
    if (RAB_UnifiedLightLoadEnabled())
    {
        return RAB_LoadUnifiedLightInfo(index, previousFrame);
    }
    return RAB_LoadSplitLightInfo(index, previousFrame);
}

#endif
