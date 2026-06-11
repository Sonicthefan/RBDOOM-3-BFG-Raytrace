#ifndef RB_PATH_TRACING_CLEANROOM_COMMON_RESTIR_SHARED_PARAMETERS_HLSLI
#define RB_PATH_TRACING_CLEANROOM_COMMON_RESTIR_SHARED_PARAMETERS_HLSLI

#ifndef RTXDI_PI
#define RTXDI_PI 3.14159265358979323846
#endif

#ifndef RTXDI_InvalidLightIndex
#define RTXDI_InvalidLightIndex 0xffffffffu
#endif

#ifndef RTXDI_INVALID_LIGHT_INDEX
#define RTXDI_INVALID_LIGHT_INDEX 0xffffffffu
#endif

#ifndef RTXDI_MAX_FLOAT32
#define RTXDI_MAX_FLOAT32 3.402823466e+38F
#endif

#ifndef RTXDI_ENABLE_PRESAMPLING
#define RTXDI_ENABLE_PRESAMPLING 1
#endif

#ifndef RTXDI_LIGHT_COMPACT_BIT
#define RTXDI_LIGHT_COMPACT_BIT 0x80000000u
#endif

#ifndef RTXDI_LIGHT_INDEX_MASK
#define RTXDI_LIGHT_INDEX_MASK 0x7fffffffu
#endif

#ifndef RTXDI_LIGHT_DATA_VALID_BIT
#define RTXDI_LIGHT_DATA_VALID_BIT 0x80000000u
#endif

#ifndef RTXDI_LIGHT_DATA_INDEX_MASK
#define RTXDI_LIGHT_DATA_INDEX_MASK 0x7fffffffu
#endif

#ifndef RTXDI_RESERVOIR_BLOCK_SIZE
#define RTXDI_RESERVOIR_BLOCK_SIZE 16u
#endif

#ifndef RTXDI_TILE_SIZE_IN_PIXELS
#define RTXDI_TILE_SIZE_IN_PIXELS 8u
#endif

#ifndef RTXDI_DEFAULT
#define RTXDI_DEFAULT(value) = value
#endif

#ifndef RTXDI_BIAS_CORRECTION_OFF
#define RTXDI_BIAS_CORRECTION_OFF 0u
#endif

#ifndef RTXDI_BIAS_CORRECTION_BASIC
#define RTXDI_BIAS_CORRECTION_BASIC 1u
#endif

#ifndef RTXDI_BIAS_CORRECTION_PAIRWISE
#define RTXDI_BIAS_CORRECTION_PAIRWISE 2u
#endif

#ifndef RTXDI_BIAS_CORRECTION_RAY_TRACED
#define RTXDI_BIAS_CORRECTION_RAY_TRACED 3u
#endif

#ifndef RTXDI_NAIVE_SAMPLING_M_THRESHOLD
#define RTXDI_NAIVE_SAMPLING_M_THRESHOLD 2u
#endif

#ifndef ReSTIRDI_LocalLightSamplingMode_UNIFORM
#define ReSTIRDI_LocalLightSamplingMode_UNIFORM 0u
#endif

#ifndef ReSTIRDI_LocalLightSamplingMode_POWER_RIS
#define ReSTIRDI_LocalLightSamplingMode_POWER_RIS 1u
#endif

#ifndef ReSTIRDI_LocalLightSamplingMode_REGIR_RIS
#define ReSTIRDI_LocalLightSamplingMode_REGIR_RIS 2u
#endif

struct RTXDI_ReservoirBufferParameters
{
    uint reservoirBlockRowPitch;
    uint reservoirArrayPitch;
    uint pad1;
    uint pad2;
};

struct RTXDI_RuntimeParameters
{
    uint neighborOffsetMask;
    uint activeCheckerboardField;
    uint frameIndex;
    uint pad0;
};

struct RTXDI_LightBufferRegion
{
    uint firstLightIndex;
    uint numLights;
    uint pad1;
    uint pad2;
};

struct RTXDI_EnvironmentLightBufferParameters
{
    uint lightPresent;
    uint lightIndex;
    uint pad1;
    uint pad2;
};

struct RTXDI_LightBufferParameters
{
    RTXDI_LightBufferRegion localLightBufferRegion;
    RTXDI_LightBufferRegion infiniteLightBufferRegion;
    RTXDI_EnvironmentLightBufferParameters environmentLightParams;
};

struct RTXDI_PackedDIReservoir
{
    uint lightData;
    uint uvData;
    uint mVisibility;
    uint distanceAge;
    float targetPdf;
    float weight;
};

struct RTXDI_BoilingFilterParameters
{
    uint enableBoilingFilter;
    float boilingFilterStrength;
    uint pad0;
    uint pad1;
};

uint RTXDI_LightIndexToLightData(uint lightIndex)
{
    return RTXDI_LIGHT_DATA_VALID_BIT | (lightIndex & RTXDI_LIGHT_DATA_INDEX_MASK);
}

#endif
