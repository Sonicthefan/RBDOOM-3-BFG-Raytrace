#ifndef RB_PATH_TRACING_CLEANROOM_COMMON_RESTIR_PT_PARAMETERS_HLSLI
#define RB_PATH_TRACING_CLEANROOM_COMMON_RESTIR_PT_PARAMETERS_HLSLI

// rbdoom-owned shader-side ReSTIR PT parameter ABI.
//
// This header mirrors neo/renderer/NVRHI/PathTraceRestirPTParameters.h for
// constant-buffer layout only. PT reservoir storage, initial sampling,
// temporal resampling, spatial resampling, path tracing context, and random
// context remain separate replacement work.

static const uint RtRestirPTPathTraceInvocationType_Initial = 1u;
static const uint RtRestirPTPathTraceInvocationType_Temporal = 2u;
static const uint RtRestirPTPathTraceInvocationType_TemporalInverse = 3u;
static const uint RtRestirPTPathTraceInvocationType_Spatial = 4u;
static const uint RtRestirPTPathTraceInvocationType_SpatialInverse = 5u;
static const uint RtRestirPTPathTraceInvocationType_DebugTemporalRetrace = 11u;
static const uint RtRestirPTPathTraceInvocationType_DebugSpatialRetrace = 12u;

#ifdef RB_RESTIR_PT_USE_RTXDI_RESERVOIR_BUFFER_PARAMETERS
#define RtRestirPTReservoirBufferParameters RTXDI_ReservoirBufferParameters
#else
struct RtRestirPTReservoirBufferParameters
{
    uint reservoirBlockRowPitch;
    uint reservoirArrayPitch;
    uint pad1;
    uint pad2;
};
#endif

struct RtRestirPTBufferIndices
{
    uint initialPathTracerOutputBufferIndex;
    uint temporalResamplingInputBufferIndex;
    uint temporalResamplingOutputBufferIndex;
    uint spatialResamplingInputBufferIndex;
    uint spatialResamplingOutputBufferIndex;
    uint finalShadingInputBufferIndex;
};

struct RtRestirPTInitialSamplingParameters
{
    uint numInitialSamples;
    uint maxBounceDepth;
    uint maxRcVertexLength;
};

struct RtRestirPTReconnectionParameters
{
    float minConnectionFootprint;
    float minConnectionFootprintSigma;
    float minPdfRoughness;
    float minPdfRoughnessSigma;
    float roughnessThreshold;
    float distanceThreshold;
    uint reconnectionMode;
};

struct RtRestirPTHybridShiftPerFrameParameters
{
    uint maxBounceDepth;
    uint maxRcVertexLength;
};

struct RtRestirPTTemporalResamplingParameters
{
    float depthThreshold;
    float normalThreshold;
    uint enablePermutationSampling;
    uint maxHistoryLength;
    uint maxReservoirAge;
    uint enableFallbackSampling;
    uint uniformRandomNumber;
    uint duplicationBasedHistoryReduction;
    float historyReductionStrength;
};

struct RtRestirPTBoilingFilterParameters
{
    float boilingFilterStrength;
    uint enableBoilingFilter;
};

struct RtRestirPTSpatialResamplingParameters
{
    uint numSpatialSamples;
    uint numDisocclusionBoostSamples;
    uint maxTemporalHistory;
    uint duplicationBasedHistoryReduction;
    float samplingRadius;
    float normalThreshold;
    float depthThreshold;
};

struct RtRestirPTParameters
{
    uint reservoirBuffer_reservoirBlockRowPitch;
    uint reservoirBuffer_reservoirArrayPitch;
    uint bufferIndices_initialPathTracerOutputBufferIndex;
    uint bufferIndices_temporalResamplingInputBufferIndex;
    uint bufferIndices_temporalResamplingOutputBufferIndex;
    uint bufferIndices_spatialResamplingInputBufferIndex;
    uint bufferIndices_spatialResamplingOutputBufferIndex;
    uint bufferIndices_finalShadingInputBufferIndex;
    uint initialSampling_numInitialSamples;
    uint initialSampling_maxBounceDepth;
    uint initialSampling_maxRcVertexLength;
    float reconnection_minConnectionFootprint;
    float reconnection_minConnectionFootprintSigma;
    float reconnection_minPdfRoughness;
    float reconnection_minPdfRoughnessSigma;
    float reconnection_roughnessThreshold;
    float reconnection_distanceThreshold;
    uint reconnection_reconnectionMode;
    float temporalResampling_depthThreshold;
    float temporalResampling_normalThreshold;
    uint temporalResampling_enablePermutationSampling;
    uint temporalResampling_maxHistoryLength;
    uint temporalResampling_maxReservoirAge;
    uint temporalResampling_enableFallbackSampling;
    uint temporalResampling_uniformRandomNumber;
    uint temporalResampling_duplicationBasedHistoryReduction;
    float temporalResampling_historyReductionStrength;
    uint hybridShift_maxBounceDepth;
    uint hybridShift_maxRcVertexLength;
    float boilingFilter_boilingFilterStrength;
    uint boilingFilter_enableBoilingFilter;
    uint spatialResampling_numSpatialSamples;
    uint spatialResampling_numDisocclusionBoostSamples;
    uint spatialResampling_maxTemporalHistory;
    uint spatialResampling_duplicationBasedHistoryReduction;
    float spatialResampling_samplingRadius;
    float spatialResampling_normalThreshold;
    float spatialResampling_depthThreshold;
    uint padding0;
    uint padding1;
};

RtRestirPTReservoirBufferParameters RtRestirPTGetReservoirBufferParameters(RtRestirPTParameters params)
{
    RtRestirPTReservoirBufferParameters result;
    result.reservoirBlockRowPitch = params.reservoirBuffer_reservoirBlockRowPitch;
    result.reservoirArrayPitch = params.reservoirBuffer_reservoirArrayPitch;
    result.pad1 = 0u;
    result.pad2 = 0u;
    return result;
}

RtRestirPTBufferIndices RtRestirPTGetBufferIndices(RtRestirPTParameters params)
{
    RtRestirPTBufferIndices result;
    result.initialPathTracerOutputBufferIndex = params.bufferIndices_initialPathTracerOutputBufferIndex;
    result.temporalResamplingInputBufferIndex = params.bufferIndices_temporalResamplingInputBufferIndex;
    result.temporalResamplingOutputBufferIndex = params.bufferIndices_temporalResamplingOutputBufferIndex;
    result.spatialResamplingInputBufferIndex = params.bufferIndices_spatialResamplingInputBufferIndex;
    result.spatialResamplingOutputBufferIndex = params.bufferIndices_spatialResamplingOutputBufferIndex;
    result.finalShadingInputBufferIndex = params.bufferIndices_finalShadingInputBufferIndex;
    return result;
}

RtRestirPTInitialSamplingParameters RtRestirPTGetInitialSamplingParameters(RtRestirPTParameters params)
{
    RtRestirPTInitialSamplingParameters result;
    result.numInitialSamples = params.initialSampling_numInitialSamples;
    result.maxBounceDepth = params.initialSampling_maxBounceDepth;
    result.maxRcVertexLength = params.initialSampling_maxRcVertexLength;
    return result;
}

RtRestirPTReconnectionParameters RtRestirPTGetReconnectionParameters(RtRestirPTParameters params)
{
    RtRestirPTReconnectionParameters result;
    result.minConnectionFootprint = params.reconnection_minConnectionFootprint;
    result.minConnectionFootprintSigma = params.reconnection_minConnectionFootprintSigma;
    result.minPdfRoughness = params.reconnection_minPdfRoughness;
    result.minPdfRoughnessSigma = params.reconnection_minPdfRoughnessSigma;
    result.roughnessThreshold = params.reconnection_roughnessThreshold;
    result.distanceThreshold = params.reconnection_distanceThreshold;
    result.reconnectionMode = params.reconnection_reconnectionMode;
    return result;
}

RtRestirPTTemporalResamplingParameters RtRestirPTGetTemporalResamplingParameters(RtRestirPTParameters params)
{
    RtRestirPTTemporalResamplingParameters result;
    result.depthThreshold = params.temporalResampling_depthThreshold;
    result.normalThreshold = params.temporalResampling_normalThreshold;
    result.enablePermutationSampling = params.temporalResampling_enablePermutationSampling;
    result.maxHistoryLength = params.temporalResampling_maxHistoryLength;
    result.maxReservoirAge = params.temporalResampling_maxReservoirAge;
    result.enableFallbackSampling = params.temporalResampling_enableFallbackSampling;
    result.uniformRandomNumber = params.temporalResampling_uniformRandomNumber;
    result.duplicationBasedHistoryReduction = params.temporalResampling_duplicationBasedHistoryReduction;
    result.historyReductionStrength = params.temporalResampling_historyReductionStrength;
    return result;
}

RtRestirPTHybridShiftPerFrameParameters RtRestirPTGetHybridShiftParameters(RtRestirPTParameters params)
{
    RtRestirPTHybridShiftPerFrameParameters result;
    result.maxBounceDepth = params.hybridShift_maxBounceDepth;
    result.maxRcVertexLength = params.hybridShift_maxRcVertexLength;
    return result;
}

RtRestirPTSpatialResamplingParameters RtRestirPTGetSpatialResamplingParameters(RtRestirPTParameters params)
{
    RtRestirPTSpatialResamplingParameters result;
    result.numSpatialSamples = params.spatialResampling_numSpatialSamples;
    result.numDisocclusionBoostSamples = params.spatialResampling_numDisocclusionBoostSamples;
    result.maxTemporalHistory = params.spatialResampling_maxTemporalHistory;
    result.duplicationBasedHistoryReduction = params.spatialResampling_duplicationBasedHistoryReduction;
    result.samplingRadius = params.spatialResampling_samplingRadius;
    result.normalThreshold = params.spatialResampling_normalThreshold;
    result.depthThreshold = params.spatialResampling_depthThreshold;
    return result;
}

struct RtRestirPTPackedReservoir
{
    uint4 Data0;
    uint4 Data1;
    uint4 Data2;
    uint4 Data3;
};

#endif
