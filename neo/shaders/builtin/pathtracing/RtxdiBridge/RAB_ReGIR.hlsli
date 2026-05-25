#ifndef RB_PATH_TRACING_RAB_REGIR_HLSLI
#define RB_PATH_TRACING_RAB_REGIR_HLSLI

struct PathTraceReGIRCellDebug
{
    uint valid;
    int3 localCoord;
    int3 globalCoord;
    uint localCellIndex;
    float3 localUv;
    float boundary;
};

struct PathTraceReGIRCandidateRecord
{
    uint lightIndex;
    uint lightClass;
    float sourcePdf;
    float invSourcePdf;
    uint cellIndex;
    uint slotIndex;
    uint flags;
    uint globalIdentity;
};

static const uint PATH_TRACE_REGIR_LIGHT_CLASS_NONE = 0u;
static const uint PATH_TRACE_REGIR_LIGHT_CLASS_DOOM_ANALYTIC = 1u;
static const uint PATH_TRACE_REGIR_LIGHT_CLASS_EMISSIVE_TRIANGLE = 2u;
static const uint PATH_TRACE_REGIR_CANDIDATE_VALID = 1u << 0u;
static const uint PATH_TRACE_REGIR_CANDIDATE_WRITTEN = 1u << 1u;
static const uint PATH_TRACE_REGIR_EMPTY_OUT_OF_VOLUME = 1u;
static const uint PATH_TRACE_REGIR_EMPTY_NO_ANALYTIC_LIGHTS = 2u;
static const uint PATH_TRACE_REGIR_EMPTY_NON_ANALYTIC_DOMAIN = 3u;
static const uint PATH_TRACE_REGIR_EMPTY_NO_EMISSIVE_LIGHTS = 4u;
static const uint PATH_TRACE_REGIR_EMPTY_UNSUPPORTED_DOMAIN = 5u;

uint PathTraceReGIRHashCell(int3 coord)
{
    uint3 bits = uint3(asuint(coord.x), asuint(coord.y), asuint(coord.z));
    uint hash = bits.x * 73856093u;
    hash ^= bits.y * 19349663u;
    hash ^= bits.z * 83492791u;
    hash ^= hash >> 13u;
    hash *= 1274126177u;
    return hash;
}

float3 PathTraceReGIRCellColor(int3 coord)
{
    const uint hash = PathTraceReGIRHashCell(coord);
    return float3(
        0.18 + 0.82 * ((hash & 255u) / 255.0),
        0.18 + 0.82 * (((hash >> 8u) & 255u) / 255.0),
        0.18 + 0.82 * (((hash >> 16u) & 255u) / 255.0));
}

PathTraceReGIRCellDebug PathTraceReGIRMapWorldPositionToCell(
    float3 worldPosition,
    float3 cameraOrigin,
    uint centerMode,
    float cellSize,
    uint3 gridDimensions)
{
    PathTraceReGIRCellDebug cell = (PathTraceReGIRCellDebug)0;
    cellSize = max(cellSize, 1.0);
    gridDimensions = max(gridDimensions, uint3(1u, 1u, 1u));

    const float3 globalCell = floor(worldPosition / cellSize);
    cell.globalCoord = int3(globalCell);
    cell.localUv = frac(worldPosition / cellSize);
    const float3 edgeDistance = min(cell.localUv, 1.0 - cell.localUv);
    cell.boundary = min(edgeDistance.x, min(edgeDistance.y, edgeDistance.z));

    const float3 cameraSnappedCenter = floor(cameraOrigin / cellSize + 0.5) * cellSize;
    const float3 fixedOriginCenter = float3(0.0, 0.0, 0.0);
    const float3 gridCenter = centerMode == 0u ? cameraSnappedCenter : fixedOriginCenter;
    const float3 gridMin = gridCenter - float3(gridDimensions) * (0.5 * cellSize);
    const int3 localCoord = int3(floor((worldPosition - gridMin) / cellSize));
    cell.localCoord = localCoord;

    const bool inVolume =
        localCoord.x >= 0 && localCoord.y >= 0 && localCoord.z >= 0 &&
        localCoord.x < int(gridDimensions.x) &&
        localCoord.y < int(gridDimensions.y) &&
        localCoord.z < int(gridDimensions.z);
    cell.valid = inVolume ? 1u : 0u;
    if (inVolume)
    {
        cell.localCellIndex =
            uint(localCoord.x) +
            uint(localCoord.y) * gridDimensions.x +
            uint(localCoord.z) * gridDimensions.x * gridDimensions.y;
    }
    return cell;
}

PathTraceReGIRCellDebug PathTraceReGIRMapLocalCellIndex(
    uint localCellIndex,
    float3 cameraOrigin,
    uint centerMode,
    float cellSize,
    uint3 gridDimensions)
{
    PathTraceReGIRCellDebug cell = (PathTraceReGIRCellDebug)0;
    cellSize = max(cellSize, 1.0);
    gridDimensions = max(gridDimensions, uint3(1u, 1u, 1u));

    const uint cellsPerLayer = gridDimensions.x * gridDimensions.y;
    const uint z = cellsPerLayer > 0u ? localCellIndex / cellsPerLayer : 0u;
    const uint layerCell = cellsPerLayer > 0u ? localCellIndex - z * cellsPerLayer : 0u;
    const uint y = gridDimensions.x > 0u ? layerCell / gridDimensions.x : 0u;
    const uint x = gridDimensions.x > 0u ? layerCell - y * gridDimensions.x : 0u;
    cell.localCoord = int3((int)x, (int)y, (int)z);
    cell.localCellIndex = localCellIndex;
    cell.localUv = float3(0.5, 0.5, 0.5);
    cell.boundary = 0.5;

    const bool inVolume =
        x < gridDimensions.x &&
        y < gridDimensions.y &&
        z < gridDimensions.z;
    cell.valid = inVolume ? 1u : 0u;
    if (inVolume)
    {
        const float3 cameraSnappedCenter = floor(cameraOrigin / cellSize + 0.5) * cellSize;
        const float3 fixedOriginCenter = float3(0.0, 0.0, 0.0);
        const float3 gridCenter = centerMode == 0u ? cameraSnappedCenter : fixedOriginCenter;
        const float3 gridMin = gridCenter - float3(gridDimensions) * (0.5 * cellSize);
        const float3 worldCenter = gridMin + (float3(x, y, z) + 0.5) * cellSize;
        cell.globalCoord = int3(floor(worldCenter / cellSize));
    }
    return cell;
}

PathTraceReGIRCandidateRecord PathTraceReGIREmptyCandidate(uint cellIndex, uint slotIndex, uint reason)
{
    PathTraceReGIRCandidateRecord candidate = (PathTraceReGIRCandidateRecord)0;
    candidate.lightIndex = 0xffffffffu;
    candidate.lightClass = PATH_TRACE_REGIR_LIGHT_CLASS_NONE;
    candidate.cellIndex = cellIndex;
    candidate.slotIndex = slotIndex;
    candidate.flags = reason << 16u;
    candidate.globalIdentity = 0xffffffffu;
    return candidate;
}

#endif
