#pragma once

// rbdoom-owned host/shared ReSTIR PT parameter ABI.
//
// This header deliberately covers only the CPU-visible parameter upload and
// resource-sizing surface used by PathTraceRestirPT*. The PT HLSL reservoir,
// initial sampling, temporal, spatial, context, and random-context modules are
// still separate remediation work.

#include <cstddef>
#include <cstdint>

namespace rbdoom::restir_pt {

enum class CheckerboardMode : uint32_t
{
    Off = 0
};

enum class ResamplingMode : uint32_t
{
    None = 0,
    Temporal = 1,
    Spatial = 2,
    TemporalAndSpatial = 3
};

enum class ReconnectionMode : uint32_t
{
    Footprint = 0
};

constexpr uint32_t kNumReservoirBuffers = 2;
constexpr uint32_t kReservoirBlockSize = 16;
constexpr uint32_t kReservoirBlockArea = kReservoirBlockSize * kReservoirBlockSize;

struct ReservoirBufferParameters
{
    uint32_t reservoirBlockRowPitch = 0;
    uint32_t reservoirArrayPitch = 0;
};

constexpr uint32_t ReservoirBlockCount(uint32_t dimension)
{
    return (dimension + kReservoirBlockSize - 1u) / kReservoirBlockSize;
}

constexpr ReservoirBufferParameters CalculateReservoirBufferParameters(uint32_t width, uint32_t height, CheckerboardMode checkerboardMode)
{
    (void)checkerboardMode;
    const uint32_t safeWidth = width > 0 ? width : 1;
    const uint32_t safeHeight = height > 0 ? height : 1;
    const uint32_t blockCountX = ReservoirBlockCount(safeWidth);
    const uint32_t blockCountY = ReservoirBlockCount(safeHeight);

    ReservoirBufferParameters parameters;
    parameters.reservoirBlockRowPitch = blockCountX * kReservoirBlockArea;
    parameters.reservoirArrayPitch = parameters.reservoirBlockRowPitch * blockCountY;
    return parameters;
}

inline uint32_t HashFrameIndex(uint32_t value)
{
    value ^= value >> 16;
    value *= 0x7feb352du;
    value ^= value >> 15;
    value *= 0x846ca68bu;
    value ^= value >> 16;
    return value;
}

struct BufferIndices
{
    uint32_t initialPathTracerOutputBufferIndex = 0;
    uint32_t temporalResamplingInputBufferIndex = 0;
    uint32_t temporalResamplingOutputBufferIndex = 0;
    uint32_t spatialResamplingInputBufferIndex = 0;
    uint32_t spatialResamplingOutputBufferIndex = 0;
    uint32_t finalShadingInputBufferIndex = 0;
};

struct InitialSamplingParameters
{
    uint32_t numInitialSamples = 0;
    uint32_t maxBounceDepth = 0;
    uint32_t maxRcVertexLength = 0;
};

struct ReconnectionParameters
{
    float minConnectionFootprint = 0.0f;
    float minConnectionFootprintSigma = 0.0f;
    float minPdfRoughness = 0.0f;
    float minPdfRoughnessSigma = 0.0f;
    float roughnessThreshold = 0.0f;
    float distanceThreshold = 0.0f;
    ReconnectionMode reconnectionMode = ReconnectionMode::Footprint;
};

struct HybridShiftPerFrameParameters
{
    uint32_t maxBounceDepth = 0;
    uint32_t maxRcVertexLength = 0;
};

struct TemporalResamplingParameters
{
    float depthThreshold = 0.0f;
    float normalThreshold = 0.0f;
    uint32_t enablePermutationSampling = 0;
    uint32_t maxHistoryLength = 0;
    uint32_t maxReservoirAge = 0;
    uint32_t enableFallbackSampling = 0;
    uint32_t uniformRandomNumber = 0;
    uint32_t duplicationBasedHistoryReduction = 0;
    float historyReductionStrength = 0.0f;
};

struct BoilingFilterParameters
{
    float boilingFilterStrength = 0.0f;
    uint32_t enableBoilingFilter = 0;
};

struct SpatialResamplingParameters
{
    uint32_t numSpatialSamples = 0;
    uint32_t numDisocclusionBoostSamples = 0;
    uint32_t maxTemporalHistory = 0;
    uint32_t duplicationBasedHistoryReduction = 0;
    float samplingRadius = 0.0f;
    float normalThreshold = 0.0f;
    float depthThreshold = 0.0f;
};

struct Parameters
{
    ReservoirBufferParameters reservoirBuffer = {};
    BufferIndices bufferIndices = {};
    InitialSamplingParameters initialSampling = {};
    ReconnectionParameters reconnection = {};
    TemporalResamplingParameters temporalResampling = {};
    HybridShiftPerFrameParameters hybridShift = {};
    BoilingFilterParameters boilingFilter = {};
    SpatialResamplingParameters spatialResampling = {};
    uint32_t padding[2] = {};
};

struct PackedReservoir
{
    uint32_t opaque[16] = {};
};

static_assert((sizeof(Parameters) % 16) == 0, "ReSTIR PT parameters must stay constant-buffer aligned");
static_assert((sizeof(PackedReservoir) % 16) == 0, "ReSTIR PT packed reservoir stride must stay 16-byte aligned");
static_assert(CalculateReservoirBufferParameters(1, 1, CheckerboardMode::Off).reservoirBlockRowPitch == kReservoirBlockArea, "ReSTIR PT reservoir row pitch must match tiled shader addressing");
static_assert(CalculateReservoirBufferParameters(kReservoirBlockSize, kReservoirBlockSize, CheckerboardMode::Off).reservoirArrayPitch == kReservoirBlockArea, "ReSTIR PT reservoir array pitch must match one shader addressing block");
static_assert(CalculateReservoirBufferParameters(kReservoirBlockSize + 1, kReservoirBlockSize + 1, CheckerboardMode::Off).reservoirArrayPitch == (2u * kReservoirBlockArea * 2u), "ReSTIR PT reservoir array pitch must round dimensions up to shader addressing blocks");

} // namespace rbdoom::restir_pt

using RtRestirPTCheckerboardMode = rbdoom::restir_pt::CheckerboardMode;
using RtRestirPTResamplingMode = rbdoom::restir_pt::ResamplingMode;
using RtRestirPTReservoirBufferParameters = rbdoom::restir_pt::ReservoirBufferParameters;
using RtRestirPTBufferIndices = rbdoom::restir_pt::BufferIndices;
using RtRestirPTInitialSamplingParameters = rbdoom::restir_pt::InitialSamplingParameters;
using RtRestirPTReconnectionParameters = rbdoom::restir_pt::ReconnectionParameters;
using RtRestirPTHybridShiftPerFrameParameters = rbdoom::restir_pt::HybridShiftPerFrameParameters;
using RtRestirPTTemporalResamplingParameters = rbdoom::restir_pt::TemporalResamplingParameters;
using RtRestirPTBoilingFilterParameters = rbdoom::restir_pt::BoilingFilterParameters;
using RtRestirPTSpatialResamplingParameters = rbdoom::restir_pt::SpatialResamplingParameters;
using RtRestirPTParameters = rbdoom::restir_pt::Parameters;
using RtRestirPTPackedReservoir = rbdoom::restir_pt::PackedReservoir;
