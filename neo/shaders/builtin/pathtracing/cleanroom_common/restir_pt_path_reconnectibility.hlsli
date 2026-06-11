#ifndef RB_PATH_TRACING_CLEANROOM_COMMON_RESTIR_PT_PATH_RECONNECTIBILITY_HLSLI
#define RB_PATH_TRACING_CLEANROOM_COMMON_RESTIR_PT_PATH_RECONNECTIBILITY_HLSLI

struct RTXDI_PathReconnectibility
{
    uint valid;
    uint pathLength;
    uint rcVertexLength;
    float partialJacobian;
    float rcWiPdf;
};

RTXDI_PathReconnectibility RTXDI_EmptyPathReconnectibility()
{
    RTXDI_PathReconnectibility result = (RTXDI_PathReconnectibility)0;
    return result;
}

bool RTXDI_IsValidPathReconnectibility(RTXDI_PathReconnectibility value)
{
    return value.valid != 0u;
}

#endif
