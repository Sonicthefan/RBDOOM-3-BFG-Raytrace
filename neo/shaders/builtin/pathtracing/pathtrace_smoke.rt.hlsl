#include "../../vulkan.hlsli"

struct PathTraceSmokePayload
{
    uint value;
    float hitT;
    float3 normal;
    uint surfaceClass;
};

RaytracingAccelerationStructure SmokeScene : register(t0);
VK_IMAGE_FORMAT("rgba32f") RWTexture2D<float4> SmokeOutput : register(u1);
StructuredBuffer<float3> SmokeVertices : register(t3);
StructuredBuffer<uint> SmokeIndices : register(t4);
StructuredBuffer<uint> SmokeTriangleClasses : register(t5);
StructuredBuffer<uint2> SmokeInstanceRanges : register(t6);

cbuffer PathTraceSmokeConstants : register(b2)
{
    float4 CameraOriginAndTMax;
    float4 CameraForwardAndTanX;
    float4 CameraLeftAndTanY;
    float4 CameraUpAndDebugMode;
};

[shader("raygeneration")]
void RayGen()
{
    const uint2 pixel = DispatchRaysIndex().xy;
    const uint2 dimensions = DispatchRaysDimensions().xy;
    const float2 uv = (float2(pixel) + 0.5) / float2(dimensions);
    const float2 ndc = uv * 2.0 - 1.0;

    PathTraceSmokePayload payload;
    payload.value = 0;
    payload.hitT = 0.0;
    payload.normal = float3(0.0, 0.0, 0.0);
    payload.surfaceClass = 4;

    RayDesc ray;
    ray.Origin = CameraOriginAndTMax.xyz;
    ray.Direction = normalize(
        CameraForwardAndTanX.xyz +
        CameraLeftAndTanY.xyz * (-ndc.x * CameraForwardAndTanX.w) +
        CameraUpAndDebugMode.xyz * (-ndc.y * CameraLeftAndTanY.w));
    ray.TMin = 0.1;
    ray.TMax = CameraOriginAndTMax.w;

    TraceRay(SmokeScene, RAY_FLAG_NONE, 0xff, 0, 1, 0, ray, payload);

    const uint debugMode = (uint)CameraUpAndDebugMode.w;
    if (payload.value == 0)
    {
        SmokeOutput[pixel] = float4(1.0, 0.0, 0.0, 1.0);
    }
    else if (debugMode == 1)
    {
        const float normalizedDepth = saturate(payload.hitT / 512.0);
        SmokeOutput[pixel] = float4(normalizedDepth, normalizedDepth, normalizedDepth, 1.0);
    }
    else if (debugMode == 2)
    {
        SmokeOutput[pixel] = float4(payload.normal * 0.5 + 0.5, 1.0);
    }
    else if (debugMode == 3)
    {
        if (payload.surfaceClass == 0)
        {
            SmokeOutput[pixel] = float4(0.0, 1.0, 0.0, 1.0);
        }
        else if (payload.surfaceClass == 1)
        {
            SmokeOutput[pixel] = float4(0.0, 0.35, 1.0, 1.0);
        }
        else if (payload.surfaceClass == 2)
        {
            SmokeOutput[pixel] = float4(1.0, 0.0, 1.0, 1.0);
        }
        else if (payload.surfaceClass == 3)
        {
            SmokeOutput[pixel] = float4(1.0, 0.75, 0.0, 1.0);
        }
        else
        {
            SmokeOutput[pixel] = float4(1.0, 1.0, 1.0, 1.0);
        }
    }
    else
    {
        SmokeOutput[pixel] = float4(0.0, 1.0, 0.0, 1.0);
    }
}

[shader("miss")]
void Miss(inout PathTraceSmokePayload payload)
{
    payload.value = 0;
}

[shader("closesthit")]
void ClosestHit(inout PathTraceSmokePayload payload, BuiltInTriangleIntersectionAttributes attributes)
{
    const uint2 instanceRange = SmokeInstanceRanges[InstanceID()];
    const uint globalPrimitiveIndex = instanceRange.y + PrimitiveIndex();
    const uint indexOffset = instanceRange.x + PrimitiveIndex() * 3;
    const uint i0 = SmokeIndices[indexOffset + 0];
    const uint i1 = SmokeIndices[indexOffset + 1];
    const uint i2 = SmokeIndices[indexOffset + 2];

    const float3 p0 = SmokeVertices[i0];
    const float3 p1 = SmokeVertices[i1];
    const float3 p2 = SmokeVertices[i2];

    payload.value = 1;
    payload.hitT = RayTCurrent();
    payload.normal = normalize(cross(p1 - p0, p2 - p0));
    payload.surfaceClass = SmokeTriangleClasses[globalPrimitiveIndex];
}
