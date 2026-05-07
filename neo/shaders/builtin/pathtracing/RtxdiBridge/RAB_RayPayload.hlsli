#ifndef RB_PATH_TRACING_RAB_RAY_PAYLOAD_HLSLI
#define RB_PATH_TRACING_RAB_RAY_PAYLOAD_HLSLI

struct RAB_RayPayload
{
    uint hit;
    float hitT;
    uint instanceId;
    uint primitiveId;
    float2 barycentrics;
    uint materialId;
    uint materialIndex;
    uint surfaceClass;
    uint flags;
    uint frontFace;
};

RAB_RayPayload RAB_EmptyRayPayload()
{
    RAB_RayPayload payload = (RAB_RayPayload)0;
    return payload;
}

bool RAB_IsRayPayloadHit(RAB_RayPayload payload)
{
    return payload.hit != 0;
}

float RAB_RayPayloadGetCommittedHitT(RAB_RayPayload payload)
{
    return payload.hitT;
}

bool RAB_RayPayloadIsFrontFace(RAB_RayPayload payload)
{
    return payload.frontFace != 0;
}

#endif
