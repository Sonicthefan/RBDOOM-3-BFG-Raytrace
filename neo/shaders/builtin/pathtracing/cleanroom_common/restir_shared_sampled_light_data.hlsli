#ifndef RB_PATH_TRACING_CLEANROOM_COMMON_RESTIR_SHARED_SAMPLED_LIGHT_DATA_HLSLI
#define RB_PATH_TRACING_CLEANROOM_COMMON_RESTIR_SHARED_SAMPLED_LIGHT_DATA_HLSLI

#ifndef RTXDI_InvalidLightIndex
#define RTXDI_InvalidLightIndex 0xffffffffu
#endif

#ifndef RTXDI_LIGHT_DATA_VALID_BIT
#define RTXDI_LIGHT_DATA_VALID_BIT 0x80000000u
#endif

#ifndef RTXDI_LIGHT_DATA_INDEX_MASK
#define RTXDI_LIGHT_DATA_INDEX_MASK 0x7fffffffu
#endif

struct RTXDI_SampledLightData
{
    uint lightData;
    uint uvData;
};

RTXDI_SampledLightData RTXDI_SampledLightData_CreateInvalidData()
{
    RTXDI_SampledLightData result;
    result.lightData = 0u;
    result.uvData = 0u;
    return result;
}

bool RTXDI_SampledLightData_IsValidLightData(RTXDI_SampledLightData data)
{
    return (data.lightData & RTXDI_LIGHT_DATA_VALID_BIT) != 0u;
}

uint RTXDI_SampledLightData_GetLightIndex(RTXDI_SampledLightData data)
{
    return data.lightData & RTXDI_LIGHT_DATA_INDEX_MASK;
}

void RTXDI_SampledLightData_SetLightData(inout RTXDI_SampledLightData data, uint lightIndex)
{
    data.lightData = RTXDI_LIGHT_DATA_VALID_BIT | (lightIndex & RTXDI_LIGHT_DATA_INDEX_MASK);
}

uint RTXDI_SampledLightData_GetLightData(RTXDI_SampledLightData data)
{
    return data.lightData;
}

void RTXDI_SampledLightData_SetUVData(inout RTXDI_SampledLightData data, uint uvData)
{
    data.uvData = uvData;
}

void RTXDI_SampledLightData_SetUVData(inout RTXDI_SampledLightData data, float2 uv)
{
    const uint2 packed = uint2(saturate(uv) * 65535.0);
    data.uvData = (packed.y << 16) | (packed.x & 0xffffu);
}

uint RTXDI_SampledLightData_GetUVData(RTXDI_SampledLightData data)
{
    return data.uvData;
}

float2 RTXDI_SampledLightData_GetUVDataFloat2(RTXDI_SampledLightData data)
{
    return float2(
        (float)(data.uvData & 0xffffu),
        (float)((data.uvData >> 16) & 0xffffu)) / 65535.0;
}

#endif
