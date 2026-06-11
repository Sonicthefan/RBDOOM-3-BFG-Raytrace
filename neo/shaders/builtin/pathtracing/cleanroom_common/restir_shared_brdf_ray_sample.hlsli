#ifndef RB_PATH_TRACING_CLEANROOM_COMMON_RESTIR_SHARED_BRDF_RAY_SAMPLE_HLSLI
#define RB_PATH_TRACING_CLEANROOM_COMMON_RESTIR_SHARED_BRDF_RAY_SAMPLE_HLSLI

static const uint RTXDI_BRDF_RAY_SAMPLE_PROPERTY_DELTA_BIT = 1u << 0;
static const uint RTXDI_BRDF_RAY_SAMPLE_PROPERTY_SPECULAR_BIT = 1u << 1;
static const uint RTXDI_BRDF_RAY_SAMPLE_PROPERTY_TRANSMISSION_BIT = 1u << 2;

struct RTXDI_BrdfRaySampleProperties
{
    uint flags;

    void SetDelta()
    {
        flags |= RTXDI_BRDF_RAY_SAMPLE_PROPERTY_DELTA_BIT;
    }

    void SetDiffuse()
    {
        flags &= ~RTXDI_BRDF_RAY_SAMPLE_PROPERTY_SPECULAR_BIT;
    }

    void SetSpecular()
    {
        flags |= RTXDI_BRDF_RAY_SAMPLE_PROPERTY_SPECULAR_BIT;
    }

    void SetReflection()
    {
        flags &= ~RTXDI_BRDF_RAY_SAMPLE_PROPERTY_TRANSMISSION_BIT;
    }

    void SetTransmission()
    {
        flags |= RTXDI_BRDF_RAY_SAMPLE_PROPERTY_TRANSMISSION_BIT;
    }

    void SetContinuous()
    {
        flags &= ~RTXDI_BRDF_RAY_SAMPLE_PROPERTY_DELTA_BIT;
    }

    bool IsDelta()
    {
        return (flags & RTXDI_BRDF_RAY_SAMPLE_PROPERTY_DELTA_BIT) != 0u;
    }

    bool IsDiffuse()
    {
        return (flags & RTXDI_BRDF_RAY_SAMPLE_PROPERTY_SPECULAR_BIT) == 0u;
    }

    bool IsSpecular()
    {
        return (flags & RTXDI_BRDF_RAY_SAMPLE_PROPERTY_SPECULAR_BIT) != 0u;
    }

    bool IsReflection()
    {
        return (flags & RTXDI_BRDF_RAY_SAMPLE_PROPERTY_TRANSMISSION_BIT) == 0u;
    }

    bool IsTransmission()
    {
        return (flags & RTXDI_BRDF_RAY_SAMPLE_PROPERTY_TRANSMISSION_BIT) != 0u;
    }

    bool IsContinuous()
    {
        return (flags & RTXDI_BRDF_RAY_SAMPLE_PROPERTY_DELTA_BIT) == 0u;
    }
};

RTXDI_BrdfRaySampleProperties RTXDI_DefaultBrdfRaySampleProperties()
{
    RTXDI_BrdfRaySampleProperties result = (RTXDI_BrdfRaySampleProperties)0;
    return result;
}

struct RTXDI_BrdfRaySample
{
    float3 Direction;
    float3 OutDirection;
    float3 BrdfTimesNoL;
    float Pdf;
    float OutPdf;
    RTXDI_BrdfRaySampleProperties properties;
};

RTXDI_BrdfRaySample RTXDI_EmptyBrdfRaySample()
{
    RTXDI_BrdfRaySample result = (RTXDI_BrdfRaySample)0;
    result.properties = RTXDI_DefaultBrdfRaySampleProperties();
    return result;
}

#endif
