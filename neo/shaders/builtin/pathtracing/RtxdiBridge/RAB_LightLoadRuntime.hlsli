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
    return (((uint)max(UnifiedLightInfo.z, 0.0)) & 1u) != 0u;
}

bool RAB_UnifiedLightSampleEnabled()
{
    return (((uint)max(UnifiedLightInfo.z, 0.0)) & 2u) != 0u;
}

bool RAB_UnifiedPrevToCurrentScanEnabled()
{
    return RestirPTSurfaceInfo.z >= 0.5;
}

#ifndef RB_PATH_TRACE_RESTIR_LIGHT_MANAGER_CONSTANTS
#define RB_PATH_TRACE_RESTIR_LIGHT_MANAGER_CONSTANTS
static const uint PATH_TRACE_RESTIR_LIGHT_INVALID_INDEX = 0xffffffffu;
static const uint PATH_TRACE_RESTIR_LIGHT_SOURCE_EMISSIVE_TRIANGLE = 1u;
static const uint PATH_TRACE_RESTIR_LIGHT_SOURCE_DOOM_ANALYTIC = 2u;
static const uint PATH_TRACE_RESTIR_LIGHT_RECORD_STABLE_IDENTITY = 1u << 0;
static const uint PATH_TRACE_RESTIR_LIGHT_RECORD_CURRENT_ONLY = 1u << 2;
static const uint PATH_TRACE_RESTIR_LIGHT_RECORD_PREVIOUS_ONLY = 1u << 3;
static const uint PATH_TRACE_RESTIR_LIGHT_RECORD_REMAP_VALID = 1u << 4;
static const uint PATH_TRACE_RESTIR_LIGHT_INVALID_REASON_UNKNOWN_IDENTITY = 1u << 5;
static const uint PATH_TRACE_RESTIR_LIGHT_INVALID_REASON_UNSUPPORTED_SOURCE = 1u << 6;
static const uint PATH_TRACE_RESTIR_LIGHT_INVALID_REASON_INCOMPATIBLE_SOURCE = 1u << 15;
static const uint PATH_TRACE_RESTIR_LIGHT_MANAGER_SOURCE_NONE = 0u;
static const uint PATH_TRACE_RESTIR_LIGHT_MANAGER_SOURCE_COMPAT_ACTIVE = 1u;
static const uint PATH_TRACE_RESTIR_LIGHT_MANAGER_SOURCE_REMIX_DENSE = 2u;
#endif

uint RAB_GetCurrentRestirLightManagerCount()
{
#ifdef RB_PT_ENABLE_RESTIR_LIGHT_MANAGER_RAB
    return (uint)max(RestirLightManagerInfo.x, 0.0);
#else
    return 0u;
#endif
}

uint RAB_GetPreviousRestirLightManagerCount()
{
#ifdef RB_PT_ENABLE_RESTIR_LIGHT_MANAGER_RAB
    return (uint)max(RestirLightManagerInfo.y, 0.0);
#else
    return 0u;
#endif
}

uint RAB_GetRestirLightManagerCurrentToPreviousCount()
{
#ifdef RB_PT_ENABLE_RESTIR_LIGHT_MANAGER_RAB
    return (uint)max(RestirLightManagerInfo.z, 0.0);
#else
    return 0u;
#endif
}

uint RAB_GetRestirLightManagerPreviousToCurrentCount()
{
#ifdef RB_PT_ENABLE_RESTIR_LIGHT_MANAGER_RAB
    return (uint)max(RestirLightManagerInfo.w, 0.0);
#else
    return 0u;
#endif
}

uint2 RAB_GetRestirLightManagerEmissiveRange()
{
#ifdef RB_PT_ENABLE_RESTIR_LIGHT_MANAGER_RAB
    return uint2((uint)max(RestirLightManagerRangeInfo.x, 0.0), (uint)max(RestirLightManagerRangeInfo.y, 0.0));
#else
    return uint2(0u, 0u);
#endif
}

uint2 RAB_GetRestirLightManagerDoomAnalyticRange()
{
#ifdef RB_PT_ENABLE_RESTIR_LIGHT_MANAGER_RAB
    return uint2((uint)max(RestirLightManagerRangeInfo.z, 0.0), (uint)max(RestirLightManagerRangeInfo.w, 0.0));
#else
    return uint2(0u, 0u);
#endif
}

uint RAB_GetRestirLightManagerEmissiveSampleCount()
{
#ifdef RB_PT_ENABLE_RESTIR_LIGHT_MANAGER_RAB
    return (uint)max(RestirLightManagerSampleInfo.x, 0.0);
#else
    return 0u;
#endif
}

uint RAB_GetRestirLightManagerDoomAnalyticSampleCount()
{
#ifdef RB_PT_ENABLE_RESTIR_LIGHT_MANAGER_RAB
    return (uint)max(RestirLightManagerSampleInfo.y, 0.0);
#else
    return 0u;
#endif
}

uint RAB_GetRestirLightManagerTotalSampleCount()
{
#ifdef RB_PT_ENABLE_RESTIR_LIGHT_MANAGER_RAB
    return (uint)max(RestirLightManagerSampleInfo.z, 0.0);
#else
    return 0u;
#endif
}

uint RAB_GetRestirLightManagerNonEmptyRangeCount()
{
#ifdef RB_PT_ENABLE_RESTIR_LIGHT_MANAGER_RAB
    return (uint)max(RestirLightManagerSampleInfo.w, 0.0);
#else
    return 0u;
#endif
}

bool RAB_RestirLightManagerRABEnabled()
{
#ifdef RB_PT_ENABLE_RESTIR_LIGHT_MANAGER_RAB
    return RestirLightManagerControlInfo.x >= 0.5;
#else
    return false;
#endif
}

uint RAB_GetRestirLightManagerSource()
{
#ifdef RB_PT_ENABLE_RESTIR_LIGHT_MANAGER_RAB
    return (uint)max(RestirLightManagerControlInfo.y, 0.0);
#else
    return PATH_TRACE_RESTIR_LIGHT_MANAGER_SOURCE_NONE;
#endif
}

bool RAB_RestirLightManagerRemixDenseDomainEnabled()
{
    return RAB_RestirLightManagerRABEnabled() &&
        RAB_GetRestirLightManagerSource() == PATH_TRACE_RESTIR_LIGHT_MANAGER_SOURCE_REMIX_DENSE;
}

int RAB_TranslateUnifiedLightIndex(uint lightIndex, bool currentToPrevious)
{
    if (currentToPrevious)
    {
        if (MotionVectorInfo.y < 0.5 ||
            lightIndex >= RAB_GetCurrentUnifiedLightCount() ||
            lightIndex >= RAB_GetUnifiedLightRemapCount())
        {
            return -1;
        }

        const uint previousIndex = PathTraceUnifiedLightRemap[lightIndex];
        return previousIndex != PATH_TRACE_UNIFIED_LIGHT_INVALID_INDEX &&
            previousIndex < RAB_GetPreviousUnifiedLightCount() ? (int)previousIndex : -1;
    }

    if (MotionVectorInfo.y < 0.5 || !RAB_UnifiedPrevToCurrentScanEnabled() || lightIndex >= RAB_GetPreviousUnifiedLightCount())
    {
        return -1;
    }

    const uint currentCount = min(RAB_GetCurrentUnifiedLightCount(), RAB_GetUnifiedLightRemapCount());
    [loop]
    for (uint currentIndex = 0u; currentIndex < currentCount; ++currentIndex)
    {
        if (PathTraceUnifiedLightRemap[currentIndex] == lightIndex)
        {
            return (int)currentIndex;
        }
    }

    return -1;
}

int RAB_TranslateRestirLightManagerIndex(uint lightIndex, bool currentToPrevious)
{
#ifdef RB_PT_ENABLE_RESTIR_LIGHT_MANAGER_RAB
    if (currentToPrevious)
    {
        if (lightIndex >= RAB_GetCurrentRestirLightManagerCount() ||
            lightIndex >= RAB_GetRestirLightManagerCurrentToPreviousCount())
        {
            return -1;
        }

        const uint previousIndex = PathTraceRestirLightManagerCurrentToPrevious[lightIndex];
        return previousIndex != PATH_TRACE_RESTIR_LIGHT_INVALID_INDEX &&
            previousIndex < RAB_GetPreviousRestirLightManagerCount() ? (int)previousIndex : -1;
    }

    if (lightIndex >= RAB_GetPreviousRestirLightManagerCount() ||
        lightIndex >= RAB_GetRestirLightManagerPreviousToCurrentCount())
    {
        return -1;
    }

    const uint currentIndex = PathTraceRestirLightManagerPreviousToCurrent[lightIndex];
    return currentIndex != PATH_TRACE_RESTIR_LIGHT_INVALID_INDEX &&
        currentIndex < RAB_GetCurrentRestirLightManagerCount() ? (int)currentIndex : -1;
#else
    return -1;
#endif
}

int RAB_TranslateLightIndex(uint lightIndex, bool currentToPrevious)
{
    if (RAB_RestirLightManagerRABEnabled())
    {
        return RAB_TranslateRestirLightManagerIndex(lightIndex, currentToPrevious);
    }

    if (RAB_UnifiedLightLoadEnabled())
    {
        return RAB_TranslateUnifiedLightIndex(lightIndex, currentToPrevious);
    }

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
        const uint remapIndex = RAB_DoomAnalyticIdentityRemapIndex(currentIdentity);
        if (!RAB_DoomAnalyticIdentityValid(currentIdentity) || remapIndex >= remapCount)
        {
            return -1;
        }

        const PathTraceDoomAnalyticLightRemap remap = DoomAnalyticRemap[remapIndex];
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
    const uint remapIndex = RAB_DoomAnalyticIdentityRemapIndex(previousIdentity);
    if (!RAB_DoomAnalyticIdentityValid(previousIdentity) || remapIndex >= remapCount)
    {
        return -1;
    }

    const PathTraceDoomAnalyticLightRemap remap = DoomAnalyticRemap[remapIndex];
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

RAB_LightInfo RAB_BuildLightInfoFromEmissivePayload(PathTraceSmokeEmissiveTriangle emissiveTriangle, uint lightIndex)
{
    RAB_LightInfo lightInfo = RAB_EmptyLightInfo();
    lightInfo.lightType = RAB_LIGHT_TYPE_EMISSIVE_TRIANGLE;
    lightInfo.lightIndex = lightIndex;
    lightInfo.unifiedLightType = PATH_TRACE_UNIFIED_LIGHT_TYPE_EMISSIVE_TRIANGLE;
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

RAB_LightInfo RAB_BuildLightInfoFromDoomAnalyticPayload(PathTraceDoomAnalyticLightCandidate analyticLight, uint lightIndex)
{
    const float radius = analyticLight.originAndRadius.w;
    const float influenceRadius = analyticLight.doomRadiusAndArea.x;
    const float intensityScale = max(DoomAnalyticLightInfo.z, 0.0);
    const float3 radiance = max(analyticLight.colorAndIntensity.rgb, float3(0.0, 0.0, 0.0)) * intensityScale;
    const float radianceLuminance = dot(radiance, float3(0.2126, 0.7152, 0.0722));
    if (!all(analyticLight.originAndRadius.xyz == analyticLight.originAndRadius.xyz) ||
        !all(abs(analyticLight.originAndRadius.xyz) < float3(3.402823e+38, 3.402823e+38, 3.402823e+38)) ||
        !all(radiance == radiance) ||
        !all(abs(radiance) < float3(3.402823e+38, 3.402823e+38, 3.402823e+38)) ||
        radianceLuminance <= 0.0 ||
        radius <= 0.0 ||
        radius >= 3.402823e+38 ||
        influenceRadius <= 0.0 ||
        influenceRadius >= 3.402823e+38)
    {
        return RAB_EmptyLightInfo();
    }

    RAB_LightInfo lightInfo = RAB_EmptyLightInfo();
    lightInfo.lightType = RAB_LIGHT_TYPE_DOOM_ANALYTIC_SPHERE;
    lightInfo.lightIndex = lightIndex;
    lightInfo.unifiedLightType = PATH_TRACE_UNIFIED_LIGHT_TYPE_DOOM_ANALYTIC;
    lightInfo.materialIndex = RAB_INVALID_LIGHT_INDEX;
    lightInfo.flags = analyticLight.flags;
    lightInfo.position = analyticLight.originAndRadius.xyz;
    lightInfo.radius = radius;
    lightInfo.influenceRadius = influenceRadius;
    lightInfo.normal = float3(0.0, 0.0, 1.0);
    lightInfo.area = 4.0 * RTXDI_PI * radius * radius;
    lightInfo.radiance = radiance;
    lightInfo.weight = radianceLuminance * lightInfo.area * influenceRadius;
    return lightInfo;
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
        return RAB_BuildLightInfoFromEmissivePayload(emissiveTriangle, index);
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
        return RAB_BuildLightInfoFromDoomAnalyticPayload(analyticLight, index);
    }

    return RAB_EmptyLightInfo();
}

RAB_LightInfo RAB_BuildLightInfoFromUnifiedRecord(PathTraceUnifiedLightRecord record, uint unifiedIndex);

RAB_LightInfo RAB_LoadRestirLightManagerLightInfo(uint index, bool previousFrame)
{
#ifdef RB_PT_ENABLE_RESTIR_LIGHT_MANAGER_RAB
    if (previousFrame)
    {
        if (index >= RAB_GetPreviousRestirLightManagerCount())
        {
            return RAB_EmptyLightInfo();
        }
        return RAB_BuildLightInfoFromUnifiedRecord(PathTraceRestirLightManagerPreviousPayload[index], index);
    }

    if (index >= RAB_GetCurrentRestirLightManagerCount())
    {
        return RAB_EmptyLightInfo();
    }
    return RAB_BuildLightInfoFromUnifiedRecord(PathTraceRestirLightManagerCurrentPayload[index], index);
#else
    return RAB_EmptyLightInfo();
#endif
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
        lightInfo.unifiedLightType = PATH_TRACE_UNIFIED_LIGHT_TYPE_EMISSIVE_TRIANGLE;
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
        const float radius = record.positionAndRadius.w;
        const float influenceRadius = record.uvOrDoomParams.x;
        const float intensityScale = max(DoomAnalyticLightInfo.z, 0.0);
        const float3 radiance = max(record.radianceAndLuminance.rgb, float3(0.0, 0.0, 0.0)) * intensityScale;
        const float radianceLuminance = dot(radiance, float3(0.2126, 0.7152, 0.0722));
        if (!RAB_UnifiedDoomAnalyticRecordSampleable(record) ||
            !all(record.positionAndRadius.xyz == record.positionAndRadius.xyz) ||
            !all(abs(record.positionAndRadius.xyz) < float3(3.402823e+38, 3.402823e+38, 3.402823e+38)) ||
            !all(radiance == radiance) ||
            !all(abs(radiance) < float3(3.402823e+38, 3.402823e+38, 3.402823e+38)) ||
            radianceLuminance <= 0.0 ||
            radius <= 0.0 ||
            radius >= 3.402823e+38 ||
            influenceRadius <= 0.0 ||
            influenceRadius >= 3.402823e+38 ||
            record.sourceWeight <= 0.0)
        {
            return RAB_EmptyLightInfo();
        }

        RAB_LightInfo lightInfo = RAB_EmptyLightInfo();
        lightInfo.lightType = RAB_LIGHT_TYPE_DOOM_ANALYTIC_SPHERE;
        lightInfo.lightIndex = unifiedIndex;
        lightInfo.unifiedLightType = PATH_TRACE_UNIFIED_LIGHT_TYPE_DOOM_ANALYTIC;
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
    if (RAB_RestirLightManagerRABEnabled())
    {
        return RAB_LoadRestirLightManagerLightInfo(index, previousFrame);
    }

    if (RAB_UnifiedLightLoadEnabled())
    {
        return RAB_LoadUnifiedLightInfo(index, previousFrame);
    }
    return RAB_LoadSplitLightInfo(index, previousFrame);
}

int RAB_TranslateActiveRrxLightIndex(uint lightIndex, bool currentToPrevious)
{
    if (!RAB_RestirLightManagerRABEnabled())
    {
        return -1;
    }
    return RAB_TranslateRestirLightManagerIndex(lightIndex, currentToPrevious);
}

RAB_LightInfo RAB_LoadActiveRrxLightInfo(uint index, bool previousFrame)
{
    if (!RAB_RestirLightManagerRABEnabled())
    {
        return RAB_EmptyLightInfo();
    }
    return RAB_LoadRestirLightManagerLightInfo(index, previousFrame);
}

#endif
