#pragma once

// Host-side RTXDI ReSTIR PT parameter upload scaffold.
//
// This owns no shader pass. It keeps buffer-index selection and PT parameter
// packing separate from the existing smoke reservoir and dispatch constants.

#include "PathTraceRestirPTParameters.h"

struct RtRestirPTContextUpdateDesc
{
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t frameIndex = 0;
    RtRestirPTCheckerboardMode checkerboardMode = RtRestirPTCheckerboardMode::Off;
    RtRestirPTResamplingMode resamplingMode = RtRestirPTResamplingMode::None;
    float temporalDepthThreshold = 0.1f;
    float temporalNormalThreshold = 0.35f;
    bool temporalReservoirReuse = true;
    bool temporalFallbackSampling = false;
    uint32_t spatialSamples = 1;
    float spatialRadius = 16.0f;
};

struct RtRestirPTContextState
{
    RtRestirPTParameters parameters = {};
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t frameIndex = 0;
    RtRestirPTCheckerboardMode checkerboardMode = RtRestirPTCheckerboardMode::Off;
    RtRestirPTResamplingMode resamplingMode = RtRestirPTResamplingMode::None;

    bool IsValidFor(uint32_t requestedWidth, uint32_t requestedHeight, RtRestirPTCheckerboardMode requestedCheckerboardMode) const;
    void Reset();
};

size_t GetRestirPTParametersSize();
bool UpdateRestirPTContextState(RtRestirPTContextState& state, const RtRestirPTContextUpdateDesc& desc);
