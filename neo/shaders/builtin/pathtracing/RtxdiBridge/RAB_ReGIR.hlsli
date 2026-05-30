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
    float3 worldCenter;
    float3 worldExtent;
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
static const uint PATH_TRACE_REGIR_EMPTY_NO_LOCAL_ANALYTIC_LIGHTS = 6u;
static const uint PATH_TRACE_REGIR_EMPTY_NO_LOCAL_EMISSIVE_LIGHTS = 7u;
static const uint PATH_TRACE_REGIR_EMPTY_NO_CURRENT_RLU_SOURCE = 8u;

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

float3 PathTraceReGIRResolveGridCenter(
    float3 cameraOrigin,
    uint centerMode,
    float cellSize,
    float3 explicitCenter)
{
    if (centerMode == 0u)
    {
        return floor(cameraOrigin / cellSize + 0.5) * cellSize;
    }
    return explicitCenter;
}

uint PathTraceReGIROnionLayer(float distanceInCells)
{
    return (uint)floor(log2(max(distanceInCells, 0.0) + 1.0));
}

uint PathTraceReGIROnionAxisLocalCoord(float relativePosition, uint dimension, float cellSize)
{
    const uint centerIndex = max(dimension / 2u, 1u);
    const uint layer = PathTraceReGIROnionLayer(abs(relativePosition) / cellSize);
    if (relativePosition < 0.0)
    {
        return centerIndex > layer ? centerIndex - 1u - layer : 0xffffffffu;
    }
    return centerIndex + layer;
}

void PathTraceReGIROnionAxisBounds(
    uint localCoord,
    uint dimension,
    float cellSize,
    out float minOffset,
    out float maxOffset)
{
    const uint centerIndex = max(dimension / 2u, 1u);
    if (localCoord < centerIndex)
    {
        const uint layer = centerIndex - 1u - localCoord;
        const float inner = exp2((float)layer) - 1.0;
        const float outer = exp2((float)(layer + 1u)) - 1.0;
        minOffset = -outer * cellSize;
        maxOffset = -inner * cellSize;
        return;
    }

    const uint layer = localCoord - centerIndex;
    const float inner = exp2((float)layer) - 1.0;
    const float outer = exp2((float)(layer + 1u)) - 1.0;
    minOffset = inner * cellSize;
    maxOffset = outer * cellSize;
}

PathTraceReGIRCellDebug PathTraceReGIRMapWorldPositionToCell(
    float3 worldPosition,
    float3 cameraOrigin,
    uint centerMode,
    uint mode,
    float cellSize,
    uint3 gridDimensions,
    float3 explicitCenter)
{
    PathTraceReGIRCellDebug cell = (PathTraceReGIRCellDebug)0;
    cellSize = max(cellSize, 1.0);
    gridDimensions = max(gridDimensions, uint3(1u, 1u, 1u));

    const float3 gridCenter = PathTraceReGIRResolveGridCenter(cameraOrigin, centerMode, cellSize, explicitCenter);
    if (mode == 2u)
    {
        const float3 relativePosition = worldPosition - gridCenter;
        const uint3 onionCoord = uint3(
            PathTraceReGIROnionAxisLocalCoord(relativePosition.x, gridDimensions.x, cellSize),
            PathTraceReGIROnionAxisLocalCoord(relativePosition.y, gridDimensions.y, cellSize),
            PathTraceReGIROnionAxisLocalCoord(relativePosition.z, gridDimensions.z, cellSize));
        cell.localCoord = int3(onionCoord);

        const bool inOnionVolume =
            onionCoord.x < gridDimensions.x &&
            onionCoord.y < gridDimensions.y &&
            onionCoord.z < gridDimensions.z;
        cell.valid = inOnionVolume ? 1u : 0u;
        if (inOnionVolume)
        {
            float minX;
            float maxX;
            float minY;
            float maxY;
            float minZ;
            float maxZ;
            PathTraceReGIROnionAxisBounds(onionCoord.x, gridDimensions.x, cellSize, minX, maxX);
            PathTraceReGIROnionAxisBounds(onionCoord.y, gridDimensions.y, cellSize, minY, maxY);
            PathTraceReGIROnionAxisBounds(onionCoord.z, gridDimensions.z, cellSize, minZ, maxZ);

            const float3 cellMin = gridCenter + float3(minX, minY, minZ);
            const float3 cellMax = gridCenter + float3(maxX, maxY, maxZ);
            const float3 extent = max(cellMax - cellMin, float3(1.0, 1.0, 1.0));
            cell.worldCenter = 0.5 * (cellMin + cellMax);
            cell.worldExtent = extent;
            cell.localUv = saturate((worldPosition - cellMin) / extent);
            const float3 edgeDistance = min(cell.localUv, 1.0 - cell.localUv);
            cell.boundary = min(edgeDistance.x, min(edgeDistance.y, edgeDistance.z));
            cell.globalCoord = int3(floor(cell.worldCenter / cellSize));
            cell.localCellIndex =
                onionCoord.x +
                onionCoord.y * gridDimensions.x +
                onionCoord.z * gridDimensions.x * gridDimensions.y;
        }
        return cell;
    }

    const float3 gridMin = gridCenter - float3(gridDimensions) * (0.5 * cellSize);
    const float3 localGridPosition = (worldPosition - gridMin) / cellSize;
    const int3 localCoord = int3(floor(localGridPosition));
    cell.localCoord = localCoord;
    const bool inVolume =
        localCoord.x >= 0 && localCoord.y >= 0 && localCoord.z >= 0 &&
        localCoord.x < int(gridDimensions.x) &&
        localCoord.y < int(gridDimensions.y) &&
        localCoord.z < int(gridDimensions.z);
    cell.valid = inVolume ? 1u : 0u;
    if (inVolume)
    {
        cell.localUv = frac(localGridPosition);
        const float3 edgeDistance = min(cell.localUv, 1.0 - cell.localUv);
        cell.boundary = min(edgeDistance.x, min(edgeDistance.y, edgeDistance.z));
        cell.worldCenter = gridMin + (float3(localCoord) + 0.5) * cellSize;
        cell.worldExtent = float3(cellSize, cellSize, cellSize);
        cell.globalCoord = int3(floor(cell.worldCenter / cellSize));
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
    uint mode,
    float cellSize,
    uint3 gridDimensions,
    float3 explicitCenter)
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
        const float3 gridCenter = PathTraceReGIRResolveGridCenter(cameraOrigin, centerMode, cellSize, explicitCenter);
        if (mode == 2u)
        {
            float minX;
            float maxX;
            float minY;
            float maxY;
            float minZ;
            float maxZ;
            PathTraceReGIROnionAxisBounds(x, gridDimensions.x, cellSize, minX, maxX);
            PathTraceReGIROnionAxisBounds(y, gridDimensions.y, cellSize, minY, maxY);
            PathTraceReGIROnionAxisBounds(z, gridDimensions.z, cellSize, minZ, maxZ);
            const float3 cellMin = gridCenter + float3(minX, minY, minZ);
            const float3 cellMax = gridCenter + float3(maxX, maxY, maxZ);
            cell.worldCenter = 0.5 * (cellMin + cellMax);
            cell.worldExtent = max(cellMax - cellMin, float3(1.0, 1.0, 1.0));
        }
        else
        {
            const float3 gridMin = gridCenter - float3(gridDimensions) * (0.5 * cellSize);
            cell.worldCenter = gridMin + (float3(x, y, z) + 0.5) * cellSize;
            cell.worldExtent = float3(cellSize, cellSize, cellSize);
        }
        cell.globalCoord = int3(floor(cell.worldCenter / cellSize));
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
