#include "../PathTracePrimarySurface.hlsli"

StructuredBuffer<PathTracePrimarySurfaceRecord> PathTraceNeeCachePrimarySurfaces : register(t30);

#define PATH_TRACE_NEE_CACHE_COMPUTE_UPDATE 1
#include "../pathtrace_nee_cache_debug.rt.hlsl"

uint PathTraceNeeCachePrimarySurfaceHistoryCount()
{
    return (uint)max(GeometryInfo2.z, 0.0);
}

bool PathTraceNeeCachePrimarySurfaceRecordValid(PathTracePrimarySurfaceRecord record)
{
    return record.header.x == RT_PATH_TRACE_PRIMARY_SURFACE_RECORD_VERSION &&
        (record.header.y & RT_PRIMARY_SURFACE_VALID) != 0u &&
        all(record.worldPositionAndViewDepth.xyz == record.worldPositionAndViewDepth.xyz);
}

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint2 pixel = dispatchThreadId.xy;
    const uint2 dimensions = PathTraceNeeCacheFullOutputSize();
    if (pixel.x >= dimensions.x || pixel.y >= dimensions.y)
    {
        return;
    }

    const uint surfaceIndex = pixel.y * dimensions.x + pixel.x;
    if (surfaceIndex >= PathTraceNeeCachePrimarySurfaceHistoryCount())
    {
        return;
    }

    const PathTracePrimarySurfaceRecord surface = PathTraceNeeCachePrimarySurfaces[surfaceIndex];
    if (!PathTraceNeeCachePrimarySurfaceRecordValid(surface))
    {
        return;
    }

    const uint enabled = (uint)NeeCacheInfo0.x;
    if (enabled == 0u)
    {
        return;
    }

    const uint cellResolution = max((uint)NeeCacheInfo1.x, 1u);
    const float minRange = max(NeeCacheInfo1.y, 1.0);
    const uint cellCount = max((uint)NeeCacheInfo1.z, 1u);
    const PathTraceNeeCacheCellDebug cell = PathTraceNeeCacheMapWorldPositionToCell(
        surface.worldPositionAndViewDepth.xyz,
        CameraOriginAndTMax.xyz,
        cellResolution,
        minRange,
        cellCount);
    if (cell.valid == 0u)
    {
        return;
    }

    PathTraceNeeCacheBuildCandidatesForCell(cell, 11u);
}
