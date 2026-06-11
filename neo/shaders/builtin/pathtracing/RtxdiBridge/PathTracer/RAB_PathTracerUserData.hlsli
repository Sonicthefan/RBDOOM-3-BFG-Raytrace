#ifndef RB_PATH_TRACING_RAB_PATH_TRACER_USER_DATA_HLSLI
#define RB_PATH_TRACING_RAB_PATH_TRACER_USER_DATA_HLSLI

#include "../../cleanroom_common/restir_pt_parameters.hlsli"

static const uint RAB_PTUD_FLAG_RECONNECTION_DENOISER_CALLBACK = 0x00000001u;
static const uint RAB_PTUD_FLAG_LAST_BOUNCE_DENOISER_CALLBACK = 0x00000002u;
static const uint RAB_PTUD_FLAG_ENVIRONMENT_MAP_MISS_UNBRIDGED = 0x00000004u;
static const uint RAB_PTUD_FLAG_PATH_TYPE_SET = 0x00000008u;
static const uint RAB_PTUD_FLAG_PATH_TYPE_INITIAL = 0x00000010u;
static const uint RAB_PTUD_FLAG_PATH_TYPE_TEMPORAL = 0x00000020u;
static const uint RAB_PTUD_FLAG_PATH_TYPE_TEMPORAL_INVERSE = 0x00000040u;
static const uint RAB_PTUD_FLAG_PATH_TYPE_SPATIAL = 0x00000080u;
static const uint RAB_PTUD_FLAG_PATH_TYPE_SPATIAL_INVERSE = 0x00000100u;
static const uint RAB_PTUD_FLAG_PATH_TYPE_DEBUG_TEMPORAL_RETRACE = 0x00000200u;
static const uint RAB_PTUD_FLAG_PATH_TYPE_DEBUG_SPATIAL_RETRACE = 0x00000400u;
static const uint RAB_PTUD_FLAG_PATH_TYPE_MASK = 0x000007f8u;

struct RAB_PathTracerUserData
{
    uint pathType;
    uint flags;
    float reconnectionDenoiserHitDistance;
    float lastBounceDenoiserHitDistance;
};

RAB_PathTracerUserData RAB_EmptyPathTracerUserData()
{
    return (RAB_PathTracerUserData)0;
}

void RAB_PathTracerUserDataSetPathType(inout RAB_PathTracerUserData ptud, uint type)
{
    ptud.pathType = type;
    ptud.flags &= ~RAB_PTUD_FLAG_PATH_TYPE_MASK;
    ptud.flags |= RAB_PTUD_FLAG_PATH_TYPE_SET;
    if (type == RtRestirPTPathTraceInvocationType_Initial)
    {
        ptud.flags |= RAB_PTUD_FLAG_PATH_TYPE_INITIAL;
    }
    else if (type == RtRestirPTPathTraceInvocationType_Temporal)
    {
        ptud.flags |= RAB_PTUD_FLAG_PATH_TYPE_TEMPORAL;
    }
    else if (type == RtRestirPTPathTraceInvocationType_TemporalInverse)
    {
        ptud.flags |= RAB_PTUD_FLAG_PATH_TYPE_TEMPORAL_INVERSE;
    }
    else if (type == RtRestirPTPathTraceInvocationType_Spatial)
    {
        ptud.flags |= RAB_PTUD_FLAG_PATH_TYPE_SPATIAL;
    }
    else if (type == RtRestirPTPathTraceInvocationType_SpatialInverse)
    {
        ptud.flags |= RAB_PTUD_FLAG_PATH_TYPE_SPATIAL_INVERSE;
    }
    else if (type == RtRestirPTPathTraceInvocationType_DebugTemporalRetrace)
    {
        ptud.flags |= RAB_PTUD_FLAG_PATH_TYPE_DEBUG_TEMPORAL_RETRACE;
    }
    else if (type == RtRestirPTPathTraceInvocationType_DebugSpatialRetrace)
    {
        ptud.flags |= RAB_PTUD_FLAG_PATH_TYPE_DEBUG_SPATIAL_RETRACE;
    }
}

void RAB_ReconnectionDenoiserCallback(const RTXDI_PTReservoir neighborSample, RAB_Surface surface, inout RAB_PathTracerUserData ptud)
{
    ptud.flags |= RAB_PTUD_FLAG_RECONNECTION_DENOISER_CALLBACK;
    ptud.reconnectionDenoiserHitDistance = length(neighborSample.TranslatedWorldPosition - RAB_GetSurfaceWorldPos(surface));
}

void RAB_LastBounceDenoiserCallback(float3 lightPos, RAB_Surface surface, inout RAB_PathTracerUserData ptud)
{
    ptud.flags |= RAB_PTUD_FLAG_LAST_BOUNCE_DENOISER_CALLBACK;
    ptud.lastBounceDenoiserHitDistance = length(lightPos - RAB_GetSurfaceWorldPos(surface));
}

void RAB_NoteEnvironmentMapMissUnbridged(inout RAB_PathTracerUserData ptud)
{
    ptud.flags |= RAB_PTUD_FLAG_ENVIRONMENT_MAP_MISS_UNBRIDGED;
}

#endif
