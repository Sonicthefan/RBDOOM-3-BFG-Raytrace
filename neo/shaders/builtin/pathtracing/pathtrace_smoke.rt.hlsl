#include "../../vulkan.hlsli"

struct PathTraceSmokePayload
{
    uint value;
};

RaytracingAccelerationStructure SmokeScene : register(t0);
VK_IMAGE_FORMAT("rgba32f") RWTexture2D<float4> SmokeOutput : register(u1);

cbuffer PathTraceSmokeConstants : register(b2)
{
    float4 CameraOriginAndTMax;
    float4 CameraForwardAndTanX;
    float4 CameraLeftAndTanY;
    float4 CameraUpAndPad;
};

[shader("raygeneration")]
void RayGen()
{
    PathTraceSmokePayload payload;
    payload.value = 0;

    RayDesc ray;
    ray.Origin = CameraOriginAndTMax.xyz;
    ray.Direction = normalize(CameraForwardAndTanX.xyz);
    ray.TMin = 0.1;
    ray.TMax = CameraOriginAndTMax.w;

    TraceRay(SmokeScene, RAY_FLAG_NONE, 0xff, 0, 1, 0, ray, payload);
    SmokeOutput[uint2(0, 0)] = payload.value != 0 ? float4(0.0, 1.0, 0.0, 1.0) : float4(1.0, 0.0, 0.0, 1.0);
}

[shader("miss")]
void Miss(inout PathTraceSmokePayload payload)
{
    payload.value = 0;
}

[shader("closesthit")]
void ClosestHit(inout PathTraceSmokePayload payload, BuiltInTriangleIntersectionAttributes attributes)
{
    payload.value = 1;
}
