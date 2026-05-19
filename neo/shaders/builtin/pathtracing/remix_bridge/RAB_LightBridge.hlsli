#ifndef RB_PATH_TRACING_REMIX_RAB_LIGHT_BRIDGE_HLSLI
#define RB_PATH_TRACING_REMIX_RAB_LIGHT_BRIDGE_HLSLI

// RTX Remix-shaped light bridge for future ReSTIR DI/GI passes.
//
// This is a shader contract header only. It is intentionally not routed into
// the current smoke/ReSTIR dispatch path. Future Remix RTXDI passes can include
// this header after binding the current/previous payload and remap buffers.

#include "../RtxdiBridge/RAB_LightInfoCore.hlsli"
#include "../RtxdiBridge/RAB_UnifiedLightRecord.hlsli"

#ifndef REMIX_RAB_LIGHT_BRIDGE_EXTERNAL_BINDINGS
StructuredBuffer<PathTraceUnifiedLightRecord> RemixRAB_CurrentLightPayload;
StructuredBuffer<PathTraceUnifiedLightRecord> RemixRAB_PreviousLightPayload;
StructuredBuffer<uint> RemixRAB_CurrentToPreviousLightMap;
StructuredBuffer<uint> RemixRAB_PreviousToCurrentLightMap;

cbuffer RemixRABLightBridgeConstants
{
    uint4 RemixRABLightCounts;
    uint4 RemixRABLightRangeInfo;
    uint4 RemixRABLightSampleInfo;
    uint4 RemixRABLightControlInfo;
};
#endif

static const uint REMIX_RAB_INVALID_LIGHT_INDEX = 0xffffffffu;

uint RemixRAB_GetCurrentLightCount()
{
    return RemixRABLightCounts.x;
}

uint RemixRAB_GetPreviousLightCount()
{
    return RemixRABLightCounts.y;
}

uint RemixRAB_GetCurrentToPreviousMapCount()
{
    return RemixRABLightCounts.z;
}

uint RemixRAB_GetPreviousToCurrentMapCount()
{
    return RemixRABLightCounts.w;
}

uint2 RemixRAB_GetEmissiveRange()
{
    return RemixRABLightRangeInfo.xy;
}

uint2 RemixRAB_GetDoomAnalyticRange()
{
    return RemixRABLightRangeInfo.zw;
}

uint RemixRAB_GetEmissiveSampleCount()
{
    return RemixRABLightSampleInfo.x;
}

uint RemixRAB_GetDoomAnalyticSampleCount()
{
    return RemixRABLightSampleInfo.y;
}

uint RemixRAB_GetTotalSampleCount()
{
    return RemixRABLightSampleInfo.z;
}

uint RemixRAB_GetNonEmptyRangeCount()
{
    return RemixRABLightSampleInfo.w;
}

bool RemixRAB_TranslateLightIndex(uint lightIndex, bool currentToPrevious, out uint translatedIndex)
{
    translatedIndex = REMIX_RAB_INVALID_LIGHT_INDEX;

    if (currentToPrevious)
    {
        if (lightIndex >= RemixRAB_GetCurrentLightCount() ||
            lightIndex >= RemixRAB_GetCurrentToPreviousMapCount())
        {
            return false;
        }

        const uint previousIndex = RemixRAB_CurrentToPreviousLightMap[lightIndex];
        if (previousIndex == REMIX_RAB_INVALID_LIGHT_INDEX ||
            previousIndex >= RemixRAB_GetPreviousLightCount())
        {
            return false;
        }

        translatedIndex = previousIndex;
        return true;
    }

    if (lightIndex >= RemixRAB_GetPreviousLightCount() ||
        lightIndex >= RemixRAB_GetPreviousToCurrentMapCount())
    {
        return false;
    }

    const uint currentIndex = RemixRAB_PreviousToCurrentLightMap[lightIndex];
    if (currentIndex == REMIX_RAB_INVALID_LIGHT_INDEX ||
        currentIndex >= RemixRAB_GetCurrentLightCount())
    {
        return false;
    }

    translatedIndex = currentIndex;
    return true;
}

int RemixRAB_TranslateLightIndexSigned(uint lightIndex, bool currentToPrevious)
{
    uint translatedIndex = REMIX_RAB_INVALID_LIGHT_INDEX;
    return RemixRAB_TranslateLightIndex(lightIndex, currentToPrevious, translatedIndex)
        ? int(translatedIndex)
        : -1;
}

bool RemixRAB_UnifiedRecordLoadable(PathTraceUnifiedLightRecord record)
{
    return
        record.type != PATH_TRACE_UNIFIED_LIGHT_TYPE_INVALID &&
        record.sourceIndex != PATH_TRACE_UNIFIED_LIGHT_INVALID_INDEX &&
        record.sourceWeight > 0.0;
}

RAB_LightInfo RemixRAB_BuildLightInfoFromUnifiedRecord(PathTraceUnifiedLightRecord record, uint lightIndex)
{
    if (!RemixRAB_UnifiedRecordLoadable(record))
    {
        return RAB_EmptyLightInfo();
    }

    RAB_LightInfo lightInfo = RAB_EmptyLightInfo();
    lightInfo.lightIndex = lightIndex;
    lightInfo.unifiedLightType = record.type;
    lightInfo.materialIndex = record.materialOrLightId;
    lightInfo.flags = record.flags;
    lightInfo.position = record.positionAndRadius.xyz;
    lightInfo.radius = max(record.positionAndRadius.w, 0.0);
    lightInfo.normal = RAB_LightInfoSafeNormalize(record.normalAndArea.xyz, float3(0.0, 0.0, 1.0));
    lightInfo.area = max(record.normalAndArea.w, 1.0e-4);
    lightInfo.radiance = max(record.radianceAndLuminance.rgb, float3(0.0, 0.0, 0.0));
    lightInfo.weight = record.sourceWeight;

    if (record.type == PATH_TRACE_UNIFIED_LIGHT_TYPE_EMISSIVE_TRIANGLE)
    {
        lightInfo.lightType = RAB_LIGHT_TYPE_EMISSIVE_TRIANGLE;
        lightInfo.influenceRadius = 0.0;
        return lightInfo;
    }

    if (record.type == PATH_TRACE_UNIFIED_LIGHT_TYPE_DOOM_ANALYTIC)
    {
        lightInfo.lightType = RAB_LIGHT_TYPE_DOOM_ANALYTIC_SPHERE;
        lightInfo.materialIndex = RAB_INVALID_LIGHT_INDEX;
        lightInfo.radius = max(record.positionAndRadius.w, 0.01);
        lightInfo.influenceRadius = max(record.uvOrDoomParams.x, 1.0);
        return lightInfo;
    }

    return RAB_EmptyLightInfo();
}

RAB_LightInfo RemixRAB_LoadLightInfo(uint lightIndex, bool previousFrame)
{
    if (previousFrame)
    {
        if (lightIndex >= RemixRAB_GetPreviousLightCount())
        {
            return RAB_EmptyLightInfo();
        }
        return RemixRAB_BuildLightInfoFromUnifiedRecord(RemixRAB_PreviousLightPayload[lightIndex], lightIndex);
    }

    if (lightIndex >= RemixRAB_GetCurrentLightCount())
    {
        return RAB_EmptyLightInfo();
    }
    return RemixRAB_BuildLightInfoFromUnifiedRecord(RemixRAB_CurrentLightPayload[lightIndex], lightIndex);
}

#ifdef REMIX_RAB_EXPORT_RAB_NAMES
int RAB_TranslateLightIndex(uint lightIndex, bool currentToPrevious)
{
    return RemixRAB_TranslateLightIndexSigned(lightIndex, currentToPrevious);
}

RAB_LightInfo RAB_LoadLightInfo(uint lightIndex, bool previousFrame)
{
    return RemixRAB_LoadLightInfo(lightIndex, previousFrame);
}
#endif

#endif
