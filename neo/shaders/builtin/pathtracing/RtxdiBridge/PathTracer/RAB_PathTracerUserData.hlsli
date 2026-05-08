#ifndef RB_PATH_TRACING_RAB_PATH_TRACER_USER_DATA_HLSLI
#define RB_PATH_TRACING_RAB_PATH_TRACER_USER_DATA_HLSLI

#include "Rtxdi/PT/ReSTIRPTParameters.h"

struct RAB_PathTracerUserData
{
    RTXDI_PTPathTraceInvocationType pathType;
    uint flags;
};

RAB_PathTracerUserData RAB_EmptyPathTracerUserData()
{
    return (RAB_PathTracerUserData)0;
}

void RAB_PathTracerUserDataSetPathType(inout RAB_PathTracerUserData ptud, RTXDI_PTPathTraceInvocationType type)
{
    ptud.pathType = type;
}

void RAB_ReconnectionDenoiserCallback(const RTXDI_PTReservoir neighborSample, RAB_Surface surface, inout RAB_PathTracerUserData ptud)
{
}

void RAB_LastBounceDenoiserCallback(float3 lightPos, RAB_Surface surface, inout RAB_PathTracerUserData ptud)
{
}

#endif
