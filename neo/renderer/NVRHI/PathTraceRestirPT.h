#pragma once

// Host-side RTXDI ReSTIR PT context and parameter upload scaffold.
//
// This owns no shader pass. It keeps RTXDI's runtime context, buffer-index
// selection, and PT parameter packing separate from the existing smoke reservoir
// and dispatch constants.

#include <memory>

#include <Rtxdi/PT/ReSTIRPT.h>

struct RtRestirPTContextUpdateDesc
{
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t frameIndex = 0;
    rtxdi::CheckerboardMode checkerboardMode = rtxdi::CheckerboardMode::Off;
    rtxdi::ReSTIRPT_ResamplingMode resamplingMode = rtxdi::ReSTIRPT_ResamplingMode::None;
    float temporalDepthThreshold = 0.1f;
    float temporalNormalThreshold = 0.35f;
    bool temporalReservoirReuse = true;
    bool temporalFallbackSampling = false;
    uint32_t spatialSamples = 1;
    float spatialRadius = 16.0f;
};

struct RtRestirPTContextState
{
    std::unique_ptr<rtxdi::ReSTIRPTContext> context;
    RTXDI_PTParameters parameters = {};
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t frameIndex = 0;
    rtxdi::CheckerboardMode checkerboardMode = rtxdi::CheckerboardMode::Off;
    rtxdi::ReSTIRPT_ResamplingMode resamplingMode = rtxdi::ReSTIRPT_ResamplingMode::None;

    bool IsValidFor(uint32_t requestedWidth, uint32_t requestedHeight, rtxdi::CheckerboardMode requestedCheckerboardMode) const;
    void Reset();
};

size_t GetRestirPTParametersSize();
void FillRestirPTParameters(RTXDI_PTParameters& parameters, const rtxdi::ReSTIRPTContext& context);
bool UpdateRestirPTContextState(RtRestirPTContextState& state, const RtRestirPTContextUpdateDesc& desc);
