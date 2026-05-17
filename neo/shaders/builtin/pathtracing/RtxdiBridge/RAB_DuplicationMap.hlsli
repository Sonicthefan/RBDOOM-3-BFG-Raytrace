#ifndef RB_PATH_TRACING_RAB_DUPLICATION_MAP_HLSLI
#define RB_PATH_TRACING_RAB_DUPLICATION_MAP_HLSLI

uint RAB_GetDuplicationMapCount(int2 pixelPosition)
{
    const uint2 dimensions = max(PathTraceFullOutputSize(), uint2(1u, 1u));
    if (pixelPosition.x < 0 || pixelPosition.y < 0 ||
        (uint)pixelPosition.x >= dimensions.x || (uint)pixelPosition.y >= dimensions.y)
    {
        return 0u;
    }
    return 0u;
}

#endif
