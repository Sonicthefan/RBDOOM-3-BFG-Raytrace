#pragma once

struct PathTraceRemixRtxdiResourceGateDesc
{
    bool restirPTCombinedMode = false;
    int restirPTDiDebugView = 0;
    bool remixRtxdiResourcesEnabled = false;
    bool reservoirWritesDisabled = false;
};

inline bool PathTraceRemixRtxdiResourceGateUsesProbeView(int view)
{
    return view == 60 || (view >= 63 && view <= 66) || view == 68;
}

inline bool PathTraceRemixRtxdiResourceGateRequestsDiResources(const PathTraceRemixRtxdiResourceGateDesc& desc)
{
    if (!desc.restirPTCombinedMode)
    {
        return false;
    }

    return desc.remixRtxdiResourcesEnabled || PathTraceRemixRtxdiResourceGateUsesProbeView(desc.restirPTDiDebugView);
}

inline bool PathTraceRemixRtxdiResourceGateRequestsDiClear(const PathTraceRemixRtxdiResourceGateDesc& desc)
{
    return !desc.reservoirWritesDisabled && PathTraceRemixRtxdiResourceGateRequestsDiResources(desc);
}
