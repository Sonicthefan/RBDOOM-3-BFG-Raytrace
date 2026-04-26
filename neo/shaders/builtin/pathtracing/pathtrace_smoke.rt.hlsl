struct PathTraceSmokePayload
{
    uint value;
};

RaytracingAccelerationStructure SmokeScene : register(t0);

[shader("raygeneration")]
void RayGen()
{
    PathTraceSmokePayload payload;
    payload.value = 0;

    RayDesc ray;
    ray.Origin = float3(0.0, 0.0, -1.0);
    ray.Direction = float3(0.0, 0.0, 1.0);
    ray.TMin = 0.0;
    ray.TMax = 10.0;

    TraceRay(SmokeScene, RAY_FLAG_NONE, 0xff, 0, 1, 0, ray, payload);
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
