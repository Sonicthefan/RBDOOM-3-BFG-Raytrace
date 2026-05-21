#pragma once

struct PathTraceRemixRtxdiResourceGateDesc
{
    bool restirPTCombinedMode = false;
    int restirPTDiDebugView = 0;
    bool remixRtxdiResourcesEnabled = false;
    bool debugFlatContribution = false;
    bool reservoirWritesDisabled = false;
};

enum PathTraceRemixRtxdiDiClearSource : uint32_t
{
    PATH_TRACE_REMIX_RTXDI_DI_CLEAR_SOURCE_NONE = 0u,
    PATH_TRACE_REMIX_RTXDI_DI_CLEAR_SOURCE_ACTIVE_RESOURCES = 1u,
    PATH_TRACE_REMIX_RTXDI_DI_CLEAR_SOURCE_PROBE_VIEW = 2u
};

inline bool PathTraceRemixRtxdiResourceGateUsesProbeView(int view)
{
    return view == 60 || (view >= 63 && view <= 66) || view == 68 || view == 69 || view == 70;
}

inline bool PathTraceRemixRtxdiResourceGateRequestsDiResources(const PathTraceRemixRtxdiResourceGateDesc& desc)
{
    if (!desc.restirPTCombinedMode)
    {
        return false;
    }

    return desc.remixRtxdiResourcesEnabled ||
        desc.debugFlatContribution ||
        PathTraceRemixRtxdiResourceGateUsesProbeView(desc.restirPTDiDebugView);
}

inline bool PathTraceRemixRtxdiResourceGateRequestsDiClear(const PathTraceRemixRtxdiResourceGateDesc& desc)
{
    return !desc.reservoirWritesDisabled && PathTraceRemixRtxdiResourceGateRequestsDiResources(desc);
}

inline PathTraceRemixRtxdiDiClearSource PathTraceRemixRtxdiResourceGateDiClearSource(const PathTraceRemixRtxdiResourceGateDesc& desc)
{
    if (!desc.restirPTCombinedMode || desc.reservoirWritesDisabled)
    {
        return PATH_TRACE_REMIX_RTXDI_DI_CLEAR_SOURCE_NONE;
    }
    if (desc.remixRtxdiResourcesEnabled || desc.debugFlatContribution)
    {
        return PATH_TRACE_REMIX_RTXDI_DI_CLEAR_SOURCE_ACTIVE_RESOURCES;
    }
    if (PathTraceRemixRtxdiResourceGateUsesProbeView(desc.restirPTDiDebugView))
    {
        return PATH_TRACE_REMIX_RTXDI_DI_CLEAR_SOURCE_PROBE_VIEW;
    }
    return PATH_TRACE_REMIX_RTXDI_DI_CLEAR_SOURCE_NONE;
}
