#ifndef RB_PATH_TRACING_RAB_UNIFIED_LIGHT_RECORD_HLSLI
#define RB_PATH_TRACING_RAB_UNIFIED_LIGHT_RECORD_HLSLI

static const uint PATH_TRACE_UNIFIED_LIGHT_TYPE_INVALID = 0u;
static const uint PATH_TRACE_UNIFIED_LIGHT_TYPE_EMISSIVE_TRIANGLE = 1u;
static const uint PATH_TRACE_UNIFIED_LIGHT_TYPE_DOOM_ANALYTIC = 2u;
static const uint PATH_TRACE_UNIFIED_LIGHT_INVALID_INDEX = 0xffffffffu;

struct PathTraceUnifiedLightRecord
{
    float4 positionAndRadius;
    float4 normalAndArea;
    float4 radianceAndLuminance;
    float4 uvOrDoomParams;

    uint type;
    uint sourceIndex;
    uint flags;
    uint materialOrLightId;

    uint instanceId;
    uint primitiveIndex;
    uint identityA;
    uint identityB;

    float sourcePdf;
    float sourceWeight;
    uint previousIndex;
    uint padding0;
};

#endif
