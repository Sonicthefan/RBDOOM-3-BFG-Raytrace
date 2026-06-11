#include "precompiled.h"
#pragma hdrstop

#include "PathTraceRestirPT.h"

static_assert((sizeof(RtRestirPTParameters) % 16) == 0, "ReSTIR PT parameters must stay constant-buffer aligned");

namespace {

constexpr uint32_t kRestirPTDefaultInitialSamples = 1;
constexpr uint32_t kRestirPTDefaultMaxBounceDepth = 3;
constexpr uint32_t kRestirPTDefaultMaxRcVertexLength = 5;
constexpr uint32_t kRestirPTDefaultMaxHistoryLength = 8;
constexpr uint32_t kRestirPTDefaultMaxReservoirAge = 30;
constexpr float kRestirPTDefaultHistoryReductionStrength = 0.8f;
constexpr float kRestirPTDefaultBoilingFilterStrength = 0.2f;

uint32_t RestirPTContextDimension(uint32_t value)
{
    return value > 0 ? value : 1;
}

RtRestirPTBufferIndices GetLocalRestirPTBufferIndices(RtRestirPTResamplingMode resamplingMode, uint32_t frameIndex)
{
    RtRestirPTBufferIndices bufferIndices = {};

    switch (resamplingMode)
    {
    case RtRestirPTResamplingMode::None:
        bufferIndices.initialPathTracerOutputBufferIndex = 0;
        bufferIndices.finalShadingInputBufferIndex = 0;
        break;
    case RtRestirPTResamplingMode::Temporal:
        bufferIndices.initialPathTracerOutputBufferIndex = frameIndex & 1u;
        bufferIndices.temporalResamplingInputBufferIndex = 1u - bufferIndices.initialPathTracerOutputBufferIndex;
        bufferIndices.temporalResamplingOutputBufferIndex = bufferIndices.initialPathTracerOutputBufferIndex;
        bufferIndices.finalShadingInputBufferIndex = bufferIndices.temporalResamplingOutputBufferIndex;
        break;
    case RtRestirPTResamplingMode::Spatial:
        bufferIndices.initialPathTracerOutputBufferIndex = 0;
        bufferIndices.spatialResamplingInputBufferIndex = 0;
        bufferIndices.spatialResamplingOutputBufferIndex = 1;
        bufferIndices.finalShadingInputBufferIndex = 1;
        break;
    case RtRestirPTResamplingMode::TemporalAndSpatial:
        bufferIndices.initialPathTracerOutputBufferIndex = 0;
        bufferIndices.temporalResamplingInputBufferIndex = 1;
        bufferIndices.temporalResamplingOutputBufferIndex = 0;
        bufferIndices.spatialResamplingInputBufferIndex = 0;
        bufferIndices.spatialResamplingOutputBufferIndex = 1;
        bufferIndices.finalShadingInputBufferIndex = 1;
        break;
    }

    return bufferIndices;
}

RtRestirPTInitialSamplingParameters GetLocalRestirPTInitialSamplingParameters()
{
    RtRestirPTInitialSamplingParameters params = {};
    params.numInitialSamples = kRestirPTDefaultInitialSamples;
    params.maxBounceDepth = kRestirPTDefaultMaxBounceDepth;
    params.maxRcVertexLength = kRestirPTDefaultMaxRcVertexLength;
    return params;
}

RtRestirPTReconnectionParameters GetLocalRestirPTReconnectionParameters()
{
    RtRestirPTReconnectionParameters params = {};
    params.minConnectionFootprint = 0.02f;
    params.minConnectionFootprintSigma = 0.2f;
    params.minPdfRoughness = 0.1f;
    params.minPdfRoughnessSigma = 0.01f;
    params.roughnessThreshold = 0.1f;
    params.distanceThreshold = 0.0f;
    params.reconnectionMode = rbdoom::restir_pt::ReconnectionMode::Footprint;
    return params;
}

RtRestirPTHybridShiftPerFrameParameters GetLocalRestirPTHybridShiftParameters()
{
    RtRestirPTHybridShiftPerFrameParameters params = {};
    params.maxBounceDepth = kRestirPTDefaultMaxBounceDepth;
    params.maxRcVertexLength = kRestirPTDefaultMaxRcVertexLength;
    return params;
}

RtRestirPTTemporalResamplingParameters GetLocalRestirPTTemporalResamplingParameters(const RtRestirPTContextUpdateDesc& desc)
{
    RtRestirPTTemporalResamplingParameters params = {};
    params.depthThreshold = desc.temporalDepthThreshold;
    params.normalThreshold = desc.temporalNormalThreshold;
    params.enablePermutationSampling = false;
    params.maxHistoryLength = kRestirPTDefaultMaxHistoryLength;
    params.maxReservoirAge = desc.temporalReservoirReuse ? kRestirPTDefaultMaxReservoirAge : 0;
    params.enableFallbackSampling = desc.temporalFallbackSampling;
    params.uniformRandomNumber = rbdoom::restir_pt::HashFrameIndex(desc.frameIndex);
    params.duplicationBasedHistoryReduction = false;
    params.historyReductionStrength = kRestirPTDefaultHistoryReductionStrength;
    return params;
}

RtRestirPTBoilingFilterParameters GetLocalRestirPTBoilingFilterParameters()
{
    RtRestirPTBoilingFilterParameters params = {};
    params.boilingFilterStrength = kRestirPTDefaultBoilingFilterStrength;
    params.enableBoilingFilter = true;
    return params;
}

RtRestirPTSpatialResamplingParameters GetLocalRestirPTSpatialResamplingParameters(const RtRestirPTContextUpdateDesc& desc)
{
    RtRestirPTSpatialResamplingParameters params = {};
    params.numSpatialSamples = desc.spatialSamples;
    params.numDisocclusionBoostSamples = desc.spatialSamples;
    params.maxTemporalHistory = kRestirPTDefaultMaxHistoryLength;
    params.duplicationBasedHistoryReduction = 0;
    params.samplingRadius = desc.spatialRadius;
    params.normalThreshold = desc.temporalNormalThreshold;
    params.depthThreshold = desc.temporalDepthThreshold;
    return params;
}

void FillRestirPTParameters(RtRestirPTParameters& parameters, const RtRestirPTContextUpdateDesc& desc, uint32_t width, uint32_t height)
{
    parameters = {};
    parameters.reservoirBuffer = rbdoom::restir_pt::CalculateReservoirBufferParameters(width, height, desc.checkerboardMode);
    parameters.bufferIndices = GetLocalRestirPTBufferIndices(desc.resamplingMode, desc.frameIndex);
    parameters.initialSampling = GetLocalRestirPTInitialSamplingParameters();
    parameters.reconnection = GetLocalRestirPTReconnectionParameters();
    parameters.temporalResampling = GetLocalRestirPTTemporalResamplingParameters(desc);
    parameters.hybridShift = GetLocalRestirPTHybridShiftParameters();
    parameters.boilingFilter = GetLocalRestirPTBoilingFilterParameters();
    parameters.spatialResampling = GetLocalRestirPTSpatialResamplingParameters(desc);
}

}

bool RtRestirPTContextState::IsValidFor(uint32_t requestedWidth, uint32_t requestedHeight, RtRestirPTCheckerboardMode requestedCheckerboardMode) const
{
    return
        width == RestirPTContextDimension(requestedWidth) &&
        height == RestirPTContextDimension(requestedHeight) &&
        checkerboardMode == requestedCheckerboardMode;
}

void RtRestirPTContextState::Reset()
{
    parameters = {};
    width = 0;
    height = 0;
    frameIndex = 0;
    checkerboardMode = RtRestirPTCheckerboardMode::Off;
    resamplingMode = RtRestirPTResamplingMode::None;
}

size_t GetRestirPTParametersSize()
{
    return sizeof(RtRestirPTParameters);
}

bool UpdateRestirPTContextState(RtRestirPTContextState& state, const RtRestirPTContextUpdateDesc& desc)
{
    const uint32_t width = RestirPTContextDimension(desc.width);
    const uint32_t height = RestirPTContextDimension(desc.height);

    if (!state.IsValidFor(width, height, desc.checkerboardMode))
    {
        state.width = width;
        state.height = height;
        state.checkerboardMode = desc.checkerboardMode;
    }

    FillRestirPTParameters(state.parameters, desc, width, height);
    state.frameIndex = desc.frameIndex;
    state.resamplingMode = desc.resamplingMode;
    return true;
}
