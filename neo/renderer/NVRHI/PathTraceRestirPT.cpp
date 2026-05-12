#include "precompiled.h"
#pragma hdrstop

#include "PathTraceRestirPT.h"

static_assert((sizeof(RTXDI_PTParameters) % 16) == 0, "RTXDI_PTParameters must stay constant-buffer aligned");

namespace {

uint32_t RestirPTContextDimension(uint32_t value)
{
    return value > 0 ? value : 1;
}

}

bool RtRestirPTContextState::IsValidFor(uint32_t requestedWidth, uint32_t requestedHeight, rtxdi::CheckerboardMode requestedCheckerboardMode) const
{
    return
        context != nullptr &&
        width == RestirPTContextDimension(requestedWidth) &&
        height == RestirPTContextDimension(requestedHeight) &&
        checkerboardMode == requestedCheckerboardMode;
}

void RtRestirPTContextState::Reset()
{
    context.reset();
    parameters = {};
    width = 0;
    height = 0;
    frameIndex = 0;
    checkerboardMode = rtxdi::CheckerboardMode::Off;
    resamplingMode = rtxdi::ReSTIRPT_ResamplingMode::None;
}

size_t GetRestirPTParametersSize()
{
    return sizeof(RTXDI_PTParameters);
}

void FillRestirPTParameters(RTXDI_PTParameters& parameters, const rtxdi::ReSTIRPTContext& context)
{
    parameters = {};
    parameters.reservoirBuffer = context.GetReservoirBufferParameters();
    parameters.bufferIndices = context.GetBufferIndices();
    parameters.initialSampling = context.GetInitialSamplingParameters();
    parameters.hybridShift = context.GetHybridShiftParameters();
    parameters.reconnection = context.GetReconnectionParameters();
    parameters.temporalResampling = context.GetTemporalResamplingParameters();
    parameters.boilingFilter = context.GetBoilingFilterParameters();
    parameters.spatialResampling = context.GetSpatialResamplingParameters();
}

bool UpdateRestirPTContextState(RtRestirPTContextState& state, const RtRestirPTContextUpdateDesc& desc)
{
    const uint32_t width = RestirPTContextDimension(desc.width);
    const uint32_t height = RestirPTContextDimension(desc.height);

    if (!state.IsValidFor(width, height, desc.checkerboardMode))
    {
        rtxdi::ReSTIRPTStaticParameters staticParams = {};
        staticParams.RenderWidth = width;
        staticParams.RenderHeight = height;
        staticParams.CheckerboardSamplingMode = desc.checkerboardMode;
        state.context = std::make_unique<rtxdi::ReSTIRPTContext>(staticParams);
        state.width = width;
        state.height = height;
        state.checkerboardMode = desc.checkerboardMode;
    }

    if (!state.context)
    {
        state.Reset();
        return false;
    }

    state.context->SetFrameIndex(desc.frameIndex);
    state.context->SetResamplingMode(desc.resamplingMode);
    RTXDI_PTTemporalResamplingParameters temporalParams = state.context->GetTemporalResamplingParameters();
    temporalParams.depthThreshold = desc.temporalDepthThreshold;
    temporalParams.normalThreshold = desc.temporalNormalThreshold;
    state.context->SetTemporalResamplingParameters(temporalParams);
    RTXDI_PTSpatialResamplingParameters spatialParams = state.context->GetSpatialResamplingParameters();
    spatialParams.depthThreshold = desc.temporalDepthThreshold;
    spatialParams.normalThreshold = desc.temporalNormalThreshold;
    spatialParams.numSpatialSamples = desc.spatialSamples;
    spatialParams.numDisocclusionBoostSamples = desc.spatialSamples;
    spatialParams.samplingRadius = desc.spatialRadius;
    state.context->SetSpatialResamplingParameters(spatialParams);
    FillRestirPTParameters(state.parameters, *state.context);
    state.frameIndex = desc.frameIndex;
    state.resamplingMode = desc.resamplingMode;
    return true;
}
