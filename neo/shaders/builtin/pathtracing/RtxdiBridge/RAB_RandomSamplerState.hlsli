#ifndef RB_PATH_TRACING_RAB_RANDOM_SAMPLER_STATE_HLSLI
#define RB_PATH_TRACING_RAB_RANDOM_SAMPLER_STATE_HLSLI

#include "Rtxdi/Utils/RandomSamplerState.hlsli"

struct RAB_RandomSamplerState
{
    uint seed;
    uint index;
};

RAB_RandomSamplerState RAB_CreateRandomSamplerFromDirectSeed(uint seed, uint index)
{
    RAB_RandomSamplerState rng;
    rng.seed = seed;
    rng.index = index;
    return rng;
}

RAB_RandomSamplerState RAB_InitRandomSampler(uint seed, uint dimensionBase)
{
    return RAB_CreateRandomSamplerFromDirectSeed(seed, dimensionBase);
}

RAB_RandomSamplerState RAB_InitRandomSampler(uint2 pixel, uint frameIndex, uint dimensionBase)
{
    const RTXDI_RandomSamplerState rtxdiRng = RTXDI_InitRandomSampler(pixel, frameIndex, dimensionBase);
    return RAB_CreateRandomSamplerFromDirectSeed(rtxdiRng.seed, rtxdiRng.index);
}

float RAB_GetNextRandom(inout RAB_RandomSamplerState rng)
{
    RTXDI_RandomSamplerState rtxdiRng = RTXDI_CreateRandomSamplerFromDirectSeed(rng.seed, rng.index);
    const float value = RTXDI_GetNextRandom(rtxdiRng);
    rng.seed = rtxdiRng.seed;
    rng.index = rtxdiRng.index;
    return value;
}

#endif
