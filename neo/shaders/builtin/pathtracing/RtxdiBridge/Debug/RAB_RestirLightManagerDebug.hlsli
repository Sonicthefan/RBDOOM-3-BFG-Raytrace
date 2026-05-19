#ifndef RB_PATH_TRACING_RAB_RESTIR_LIGHT_MANAGER_DEBUG_HLSLI
#define RB_PATH_TRACING_RAB_RESTIR_LIGHT_MANAGER_DEBUG_HLSLI

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

bool RestirPTLightManagerRecordSourceSupported(uint sourceType)
{
    return sourceType == PATH_TRACE_RESTIR_LIGHT_SOURCE_EMISSIVE_TRIANGLE ||
        sourceType == PATH_TRACE_RESTIR_LIGHT_SOURCE_DOOM_ANALYTIC;
}

float4 RestirPTLightManagerUnsupportedColor()
{
    return float4(0.55, 0.05, 0.80, 1.0);
}

uint RestirPTLightManagerCurrentCount()
{
    return (uint)max(RestirLightManagerInfo.x, 0.0);
}

uint RestirPTLightManagerPreviousCount()
{
    return (uint)max(RestirLightManagerInfo.y, 0.0);
}

uint RestirPTLightManagerCurrentToPreviousCount()
{
    return (uint)max(RestirLightManagerInfo.z, 0.0);
}

uint RestirPTLightManagerPreviousToCurrentCount()
{
    return (uint)max(RestirLightManagerInfo.w, 0.0);
}

float4 RestirPTLightManagerCurrentStatusColor(uint lightIndex)
{
    if (lightIndex >= RestirPTLightManagerCurrentCount())
    {
        return float4(0.0, 0.0, 0.0, 1.0);
    }

    const PathTraceRestirCurrentLightRecord record = PathTraceRestirLightManagerCurrent[lightIndex];
    const uint invalidMask =
        PATH_TRACE_RESTIR_LIGHT_INVALID_REASON_UNKNOWN_IDENTITY |
        PATH_TRACE_RESTIR_LIGHT_INVALID_REASON_UNSUPPORTED_SOURCE |
        PATH_TRACE_RESTIR_LIGHT_INVALID_REASON_INCOMPATIBLE_SOURCE;
    if (!RestirPTLightManagerRecordSourceSupported(record.sourceType) ||
        record.sourceIndex == PATH_TRACE_RESTIR_LIGHT_INVALID_INDEX ||
        (record.invalidReasonFlags & invalidMask) != 0u)
    {
        return RestirPTLightManagerUnsupportedColor();
    }

    if ((record.flags & PATH_TRACE_RESTIR_LIGHT_RECORD_CURRENT_ONLY) != 0u)
    {
        return float4(0.95, 0.85, 0.05, 1.0);
    }

    const bool remapValid = (record.flags & PATH_TRACE_RESTIR_LIGHT_RECORD_REMAP_VALID) != 0u;
    const bool stableIdentity = (record.flags & PATH_TRACE_RESTIR_LIGHT_RECORD_STABLE_IDENTITY) != 0u;
    if (lightIndex >= RestirPTLightManagerCurrentToPreviousCount() || !remapValid || !stableIdentity)
    {
        return float4(1.0, 0.35, 0.0, 1.0);
    }

    const uint previousIndex = PathTraceRestirLightManagerCurrentToPrevious[lightIndex];
    if (previousIndex == PATH_TRACE_RESTIR_LIGHT_INVALID_INDEX ||
        previousIndex >= RestirPTLightManagerPreviousCount())
    {
        return float4(0.95, 0.05, 0.05, 1.0);
    }

    return float4(0.05, 0.75, 0.12, 1.0);
}

float4 RestirPTLightManagerPreviousStatusColor(uint lightIndex)
{
    if (lightIndex >= RestirPTLightManagerPreviousCount())
    {
        return float4(0.0, 0.0, 0.0, 1.0);
    }

    const PathTraceRestirPreviousLightRecord record = PathTraceRestirLightManagerPrevious[lightIndex];
    const uint invalidMask =
        PATH_TRACE_RESTIR_LIGHT_INVALID_REASON_UNKNOWN_IDENTITY |
        PATH_TRACE_RESTIR_LIGHT_INVALID_REASON_UNSUPPORTED_SOURCE |
        PATH_TRACE_RESTIR_LIGHT_INVALID_REASON_INCOMPATIBLE_SOURCE;
    if (!RestirPTLightManagerRecordSourceSupported(record.sourceType) ||
        record.sourceIndex == PATH_TRACE_RESTIR_LIGHT_INVALID_INDEX ||
        (record.invalidReasonFlags & invalidMask) != 0u)
    {
        return RestirPTLightManagerUnsupportedColor();
    }

    if ((record.flags & PATH_TRACE_RESTIR_LIGHT_RECORD_PREVIOUS_ONLY) != 0u)
    {
        return float4(0.05, 0.18, 0.75, 1.0);
    }

    if (lightIndex >= RestirPTLightManagerPreviousToCurrentCount())
    {
        return float4(1.0, 0.35, 0.0, 1.0);
    }

    const uint currentIndex = PathTraceRestirLightManagerPreviousToCurrent[lightIndex];
    if (currentIndex == PATH_TRACE_RESTIR_LIGHT_INVALID_INDEX)
    {
        return float4(0.05, 0.18, 0.75, 1.0);
    }
    if (currentIndex >= RestirPTLightManagerCurrentCount())
    {
        return float4(0.95, 0.05, 0.05, 1.0);
    }

    return float4(0.05, 0.75, 0.12, 1.0);
}

float4 EvaluateRestirPTLightManagerMapStatusView(uint2 pixel)
{
    const uint lightIndex = RestirPTDebugUnifiedGridIndex(pixel);
    const uint lightCount = max(RestirPTLightManagerCurrentCount(), RestirPTLightManagerPreviousCount());
    if (lightIndex >= lightCount)
    {
        return float4(0.0, 0.0, 0.0, 1.0);
    }

    const uint cellSize = 12u;
    const uint2 localPixel = pixel % cellSize;
    if (localPixel.x < (cellSize / 2u))
    {
        return RestirPTLightManagerCurrentStatusColor(lightIndex);
    }
    return RestirPTLightManagerPreviousStatusColor(lightIndex);
}

#endif
