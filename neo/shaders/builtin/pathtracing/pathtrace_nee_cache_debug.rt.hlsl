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
    if (view == 3u)
    {
        return float4(0.06, 0.04, 0.02, 1.0);
    }
    return float4(0.0, 0.0, 0.0, 1.0);
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
    return float4(0.04, 0.04, 0.04, 1.0);
}

[shader("raygeneration")]
void RayGen()
{
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
