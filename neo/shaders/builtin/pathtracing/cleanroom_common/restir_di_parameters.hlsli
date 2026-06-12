#ifndef RB_PATH_TRACING_CLEANROOM_COMMON_RESTIR_DI_PARAMETERS_HLSLI
#define RB_PATH_TRACING_CLEANROOM_COMMON_RESTIR_DI_PARAMETERS_HLSLI

#include "cleanroom_common/restir_shared_parameters.hlsli"
#include "cleanroom_common/restir_shared_reservoir_addressing.hlsli"

typedef uint ReSTIRDI_LocalLightSamplingMode;

struct RTXDI_DIInitialSamplingParameters
{
    uint numLocalLightSamples;
    uint numInfiniteLightSamples;
    uint numEnvironmentSamples;
    uint numBrdfSamples;
    float brdfCutoff;
    float brdfRayMinT;
    uint localLightSamplingMode;
    uint enableInitialVisibility;
    uint environmentMapImportanceSampling;
};

struct RTXDI_DITemporalResamplingParameters
{
    uint maxHistoryLength;
    uint biasCorrectionMode;
    float depthThreshold;
    float normalThreshold;
    uint enableVisibilityShortcut;
    uint enablePermutationSampling;
    uint uniformRandomNumber;
    float permutationSamplingThreshold;
};

struct RTXDI_DISpatialResamplingParameters
{
    uint numSamples;
    uint numDisocclusionBoostSamples;
    float samplingRadius;
    uint biasCorrectionMode;
    float depthThreshold;
    float normalThreshold;
    uint targetHistoryLength;
    uint enableMaterialSimilarityTest;
    uint discountNaiveSamples;
};

#endif
