#include "../../vulkan.hlsli"
#include "RtxdiBridge/RAB_NeeCache.hlsli"

struct PathTraceNeeCachePayload
{
    uint hit;
    float hitT;
};

struct PathTraceNeeCacheShadowPayload
{
    uint hit;
};

struct PathTraceNeeCacheProviderResult
{
    uint selectedDenseRluIndex;
    uint sourceLabel;
    uint fallbackReason;
    uint cellIndex;
    uint candidateSlot;
    uint flags;
    float sourcePdf;
    float invSourcePdf;
    float mixtureProbability;
    float reserved0;
    uint reserved1;
    uint reserved2;
};

struct PathTraceNeeCacheCellRecord
{
    uint flags;
    uint hash;
    uint taskOffset;
    uint taskCount;
    uint candidateOffset;
    uint candidateCount;
    uint reserved0;
    uint reserved1;
};

struct PathTraceNeeCacheTaskRecord
{
    uint denseRluIndex;
    uint taskClass;
    float accumulatedValue;
    float decayState;
    uint cellIndex;
    uint flags;
    uint reserved0;
    uint reserved1;
};

struct PathTraceNeeCacheCandidateRecord
{
    uint denseRluIndex;
    uint lightClass;
    float sourcePdf;
    float invSourcePdf;
    float candidateWeight;
    uint cellIndex;
    uint candidateSlot;
    uint flags;
};

RaytracingAccelerationStructure SmokeScene : register(t0);
VK_IMAGE_FORMAT("rgba32f") RWTexture2D<float4> SmokeOutput : register(u1);
RWStructuredBuffer<PathTraceNeeCacheProviderResult> PathTraceNeeCacheProviderResults : register(u74);
RWStructuredBuffer<PathTraceNeeCacheCellRecord> PathTraceNeeCacheCells : register(u75);
RWStructuredBuffer<PathTraceNeeCacheTaskRecord> PathTraceNeeCacheTasks : register(u76);
RWStructuredBuffer<PathTraceNeeCacheCandidateRecord> PathTraceNeeCacheCandidates : register(u77);

cbuffer PathTraceSmokeConstants : register(b2)
{
    float4 CameraOriginAndTMax : packoffset(c0);
    float4 CameraForwardAndTanX : packoffset(c1);
    float4 CameraLeftAndTanY : packoffset(c2);
    float4 CameraUpAndDebugMode : packoffset(c3);
    float4 TextureInfo : packoffset(c4);
    float4 DispatchTileInfo : packoffset(c91);
    float4 NeeCacheInfo0 : packoffset(c115);
    float4 NeeCacheInfo1 : packoffset(c116);
    float4 NeeCacheInfo2 : packoffset(c117);
    float4 NeeCacheInfo3 : packoffset(c118);
};

uint2 PathTraceNeeCacheDispatchTileOffset()
{
    return uint2(max(DispatchTileInfo.x, 0.0), max(DispatchTileInfo.y, 0.0));
}

uint2 PathTraceNeeCacheFullOutputSize()
{
    const uint width = (uint)max(DispatchTileInfo.z, 0.0);
    const uint height = (uint)max(DispatchTileInfo.w, 0.0);
    return (width > 0u && height > 0u) ? uint2(width, height) : DispatchRaysDimensions().xy;
}

float4 PathTraceNeeCacheMissColor(uint view)
{
    if (view == 1u)
    {
        return float4(0.02, 0.06, 0.16, 1.0);
    }
    if (view == 4u)
    {
        return float4(0.01, 0.01, 0.03, 1.0);
    }
    if (view == 3u)
    {
        return float4(0.06, 0.04, 0.02, 1.0);
    }
    return float4(0.0, 0.0, 0.0, 1.0);
}

uint PathTraceNeeCacheTaskSlotCount()
{
    return max((uint)NeeCacheInfo2.x, 1u);
}

uint PathTraceNeeCacheCandidateSlotCount()
{
    return max((uint)NeeCacheInfo1.w, 1u);
}

uint PathTraceNeeCacheFrameIndex()
{
    return (uint)NeeCacheInfo3.w;
}

uint PathTraceNeeCacheDebugDispatchMode()
{
    return (uint)NeeCacheInfo3.x;
}

float3 PathTraceNeeCacheTaskHeat(float value)
{
    value = saturate(value);
    if (value < 0.5)
    {
        return lerp(float3(0.02, 0.08, 0.28), float3(0.08, 0.75, 0.55), value * 2.0);
    }
    return lerp(float3(0.08, 0.75, 0.55), float3(1.0, 0.58, 0.08), (value - 0.5) * 2.0);
}

void PathTraceNeeCacheDecayTaskCell(uint cellIndex)
{
    const uint taskSlots = PathTraceNeeCacheTaskSlotCount();
    const uint taskIndex = cellIndex * taskSlots;
    if (PathTraceNeeCacheTasks[taskIndex].flags == 0u)
    {
        return;
    }

    const uint frameIndex = PathTraceNeeCacheFrameIndex();
    const uint previousFrame = PathTraceNeeCacheTasks[taskIndex].reserved1;
    if (previousFrame == frameIndex)
    {
        return;
    }

    const uint frameGap = frameIndex > previousFrame ? min(frameIndex - previousFrame, 16u) : 1u;
    uint count = PathTraceNeeCacheTasks[taskIndex].reserved0;
    [loop]
    for (uint i = 0u; i < frameGap; ++i)
    {
        count = (count * 7u) / 8u;
    }

    PathTraceNeeCacheTasks[taskIndex].reserved0 = count;
    PathTraceNeeCacheTasks[taskIndex].reserved1 = frameIndex;
    PathTraceNeeCacheTasks[taskIndex].accumulatedValue = saturate(log2((float)count + 1.0) / 16.0);
    PathTraceNeeCacheTasks[taskIndex].decayState = 0.875;
    if (count == 0u)
    {
        PathTraceNeeCacheTasks[taskIndex].flags = 0u;
        PathTraceNeeCacheCells[cellIndex].flags = 0u;
        PathTraceNeeCacheCells[cellIndex].taskCount = 0u;
    }
}

float PathTraceNeeCacheAccumulatePrimaryHitTask(PathTraceNeeCacheCellDebug cell)
{
    const uint taskSlots = PathTraceNeeCacheTaskSlotCount();
    const uint taskIndex = cell.cellIndex * taskSlots;
    const uint frameIndex = PathTraceNeeCacheFrameIndex();
    uint previousCount = 0u;
    InterlockedAdd(PathTraceNeeCacheTasks[taskIndex].reserved0, 1u, previousCount);
    const uint nextCount = min(previousCount + 1u, 1048575u);
    const float value = saturate(log2((float)nextCount + 1.0) / 16.0);

    PathTraceNeeCacheTasks[taskIndex].denseRluIndex = 0xffffffffu;
    PathTraceNeeCacheTasks[taskIndex].taskClass = 1u;
    PathTraceNeeCacheTasks[taskIndex].accumulatedValue = value;
    PathTraceNeeCacheTasks[taskIndex].decayState = 1.0;
    PathTraceNeeCacheTasks[taskIndex].cellIndex = cell.cellIndex;
    PathTraceNeeCacheTasks[taskIndex].flags = 1u;

    PathTraceNeeCacheCells[cell.cellIndex].flags = 1u;
    PathTraceNeeCacheCells[cell.cellIndex].hash = cell.hash;
    PathTraceNeeCacheCells[cell.cellIndex].taskOffset = taskIndex;
    PathTraceNeeCacheCells[cell.cellIndex].taskCount = 1u;
    PathTraceNeeCacheCells[cell.cellIndex].candidateOffset = cell.cellIndex * PathTraceNeeCacheCandidateSlotCount();
    PathTraceNeeCacheCells[cell.cellIndex].candidateCount = 0u;
    return value;
}

float4 PathTraceNeeCacheDebugColor(float3 worldPosition, uint view)
{
    const uint enabled = (uint)NeeCacheInfo0.x;
    const uint cellResolution = max((uint)NeeCacheInfo1.x, 1u);
    const float minRange = max(NeeCacheInfo1.y, 1.0);
    const uint cellCount = max((uint)NeeCacheInfo1.z, 1u);
    const PathTraceNeeCacheCellDebug cell = PathTraceNeeCacheMapWorldPositionToCell(
        worldPosition,
        CameraOriginAndTMax.xyz,
        cellResolution,
        minRange,
        cellCount);

    if (enabled == 0u || cell.valid == 0u)
    {
        return float4(0.55, 0.05, 0.04, 1.0);
    }

    const float boundary = cell.boundary < 0.035 ? 1.0 : 0.0;
    if (view == 1u)
    {
        const float band = 0.35 + 0.65 * (((cell.hash >> 5u) & 31u) / 31.0);
        return float4(0.08, 0.45 + 0.35 * band, 0.18 + 0.20 * band, 1.0);
    }
    if (view == 2u)
    {
        return float4(PathTraceNeeCacheCellColor(cell.hash), 1.0);
    }
    if (view == 3u)
    {
        const float3 emptyColor = lerp(float3(0.18, 0.12, 0.05), float3(0.75, 0.48, 0.14), ((cell.hash >> 11u) & 15u) / 15.0);
        return float4(boundary > 0.0 ? float3(1.0, 0.92, 0.35) : emptyColor, 1.0);
    }
    if (view == 4u)
    {
        const float taskValue = PathTraceNeeCacheAccumulatePrimaryHitTask(cell);
        return float4(PathTraceNeeCacheTaskHeat(taskValue), 1.0);
    }
    return float4(0.04, 0.04, 0.04, 1.0);
}

[shader("raygeneration")]
void RayGen()
{
    if (PathTraceNeeCacheDebugDispatchMode() == 2u)
    {
        const uint2 dispatchPixel = DispatchRaysIndex().xy;
        const uint2 dispatchDimensions = DispatchRaysDimensions().xy;
        const uint linearDispatchIndex = dispatchPixel.x + dispatchPixel.y * dispatchDimensions.x;
        const uint cellCount = max((uint)NeeCacheInfo1.z, 1u);
        if (linearDispatchIndex < cellCount)
        {
            PathTraceNeeCacheDecayTaskCell(linearDispatchIndex);
        }
        return;
    }

    const uint2 pixel = DispatchRaysIndex().xy + PathTraceNeeCacheDispatchTileOffset();
    const uint2 dimensions = PathTraceNeeCacheFullOutputSize();
    if (pixel.x >= dimensions.x || pixel.y >= dimensions.y)
    {
        return;
    }

    const uint view = (uint)NeeCacheInfo0.y;
    const float2 uv = (float2(pixel) + 0.5) / float2(dimensions);
    const float2 ndc = uv * 2.0 - 1.0;

    RayDesc ray;
    ray.Origin = CameraOriginAndTMax.xyz;
    ray.Direction = normalize(
        CameraForwardAndTanX.xyz +
        CameraLeftAndTanY.xyz * (-ndc.x * CameraForwardAndTanX.w) +
        CameraUpAndDebugMode.xyz * (-ndc.y * CameraLeftAndTanY.w));
    ray.TMin = 0.1;
    ray.TMax = CameraOriginAndTMax.w;

    PathTraceNeeCachePayload payload;
    payload.hit = 0u;
    payload.hitT = 0.0;
    TraceRay(SmokeScene, RAY_FLAG_FORCE_OPAQUE, 0xff, 0, 1, 0, ray, payload);
    if (payload.hit == 0u)
    {
        SmokeOutput[pixel] = PathTraceNeeCacheMissColor(view);
        return;
    }

    const float3 worldPosition = ray.Origin + ray.Direction * payload.hitT;
    SmokeOutput[pixel] = PathTraceNeeCacheDebugColor(worldPosition, view);
}

[shader("miss")]
void Miss(inout PathTraceNeeCachePayload payload)
{
    payload.hit = 0u;
}

[shader("miss")]
void ShadowMiss(inout PathTraceNeeCacheShadowPayload payload)
{
    payload.hit = 0u;
}

[shader("anyhit")]
void AnyHit(inout PathTraceNeeCachePayload payload, BuiltInTriangleIntersectionAttributes attributes)
{
}

[shader("anyhit")]
void ShadowAnyHit(inout PathTraceNeeCacheShadowPayload payload, BuiltInTriangleIntersectionAttributes attributes)
{
    payload.hit = 1u;
}

[shader("closesthit")]
void ClosestHit(inout PathTraceNeeCachePayload payload, BuiltInTriangleIntersectionAttributes attributes)
{
    payload.hit = 1u;
    payload.hitT = RayTCurrent();
}

[shader("closesthit")]
void ShadowClosestHit(inout PathTraceNeeCacheShadowPayload payload, BuiltInTriangleIntersectionAttributes attributes)
{
    payload.hit = 1u;
}
