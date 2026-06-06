#ifndef RB_PATH_TRACING_RAB_NEE_CACHE_HLSLI
#define RB_PATH_TRACING_RAB_NEE_CACHE_HLSLI

struct PathTraceNeeCacheCellDebug
{
    uint valid;
    uint cellIndex;
    uint hash;
    uint level;
    int3 coord;
    float3 localUv;
    float boundary;
    float cellSize;
};

uint PathTraceNeeCacheHashCell(int3 coord, uint level)
{
    uint hash = asuint(coord.x) * 73856093u;
    hash ^= asuint(coord.y) * 19349663u;
    hash ^= asuint(coord.z) * 83492791u;
    hash ^= level * 2654435761u;
    hash ^= hash >> 16u;
    hash *= 2246822519u;
    hash ^= hash >> 13u;
    hash *= 3266489917u;
    hash ^= hash >> 16u;
    return hash;
}

float3 PathTraceNeeCacheCellColor(uint hash)
{
    return float3(
        0.16 + 0.84 * ((hash & 255u) / 255.0),
        0.16 + 0.84 * (((hash >> 8u) & 255u) / 255.0),
        0.16 + 0.84 * (((hash >> 16u) & 255u) / 255.0));
}

PathTraceNeeCacheCellDebug PathTraceNeeCacheMapWorldPositionToCell(
    float3 worldPosition,
    float3 cameraOrigin,
    uint cellResolution,
    float minRange,
    uint cellCount)
{
    PathTraceNeeCacheCellDebug cell = (PathTraceNeeCacheCellDebug)0;

    cellResolution = max(cellResolution, 1u);
    minRange = max(minRange, 1.0);
    cellCount = max(cellCount, 1u);

    const float baseCellSize = max(minRange / (float)cellResolution, 1.0);
    const float3 snappedCameraOrigin = floor(cameraOrigin / baseCellSize + 0.5) * baseCellSize;
    const float distanceToMovingHighResCenter = max(length(worldPosition - snappedCameraOrigin), 1.0);
    const float normalizedDistance = max(distanceToMovingHighResCenter / minRange, 1.0);
    const uint level = (uint)floor(log2(normalizedDistance));
    const float cellSize = max(baseCellSize * exp2((float)level), 1.0);
    const float3 cellPosition = worldPosition / cellSize;
    const int3 coord = int3(floor(cellPosition));
    const uint hash = PathTraceNeeCacheHashCell(coord, level);

    cell.valid = 1u;
    cell.coord = coord;
    cell.level = level;
    cell.hash = hash;
    cell.cellIndex = hash % cellCount;
    cell.localUv = frac(cellPosition);
    const float3 edgeDistance = min(cell.localUv, 1.0 - cell.localUv);
    cell.boundary = min(edgeDistance.x, min(edgeDistance.y, edgeDistance.z));
    cell.cellSize = cellSize;
    return cell;
}

#endif
