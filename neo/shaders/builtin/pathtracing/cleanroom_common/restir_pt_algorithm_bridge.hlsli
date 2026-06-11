#ifndef RB_PATH_TRACING_CLEANROOM_COMMON_RESTIR_PT_ALGORITHM_BRIDGE_HLSLI
#define RB_PATH_TRACING_CLEANROOM_COMMON_RESTIR_PT_ALGORITHM_BRIDGE_HLSLI

// Local converters from rbdoom's flat uploaded PT ABI to the PT algorithm
// parameter structs that remain provided by the still-included PT modules.

RTXDI_PTBufferIndices RtRestirPTBuildRtxdiBufferIndices(RtRestirPTParameters params)
{
    RTXDI_PTBufferIndices result;
    result.initialPathTracerOutputBufferIndex = params.bufferIndices_initialPathTracerOutputBufferIndex;
    result.temporalResamplingInputBufferIndex = params.bufferIndices_temporalResamplingInputBufferIndex;
    result.temporalResamplingOutputBufferIndex = params.bufferIndices_temporalResamplingOutputBufferIndex;
    result.spatialResamplingInputBufferIndex = params.bufferIndices_spatialResamplingInputBufferIndex;
    result.spatialResamplingOutputBufferIndex = params.bufferIndices_spatialResamplingOutputBufferIndex;
    result.finalShadingInputBufferIndex = params.bufferIndices_finalShadingInputBufferIndex;
    return result;
}

RTXDI_PTInitialSamplingParameters RtRestirPTBuildRtxdiInitialSamplingParameters(RtRestirPTParameters params)
{
    RTXDI_PTInitialSamplingParameters result;
    result.numInitialSamples = params.initialSampling_numInitialSamples;
    result.maxBounceDepth = params.initialSampling_maxBounceDepth;
    result.maxRcVertexLength = params.initialSampling_maxRcVertexLength;
    return result;
}

RTXDI_PTReconnectionParameters RtRestirPTBuildRtxdiReconnectionParameters(RtRestirPTParameters params)
{
    RTXDI_PTReconnectionParameters result;
    result.minConnectionFootprint = params.reconnection_minConnectionFootprint;
    result.minConnectionFootprintSigma = params.reconnection_minConnectionFootprintSigma;
    result.minPdfRoughness = params.reconnection_minPdfRoughness;
    result.minPdfRoughnessSigma = params.reconnection_minPdfRoughnessSigma;
    result.roughnessThreshold = params.reconnection_roughnessThreshold;
    result.distanceThreshold = params.reconnection_distanceThreshold;
    result.reconnectionMode = params.reconnection_reconnectionMode;
    return result;
}

RTXDI_PTTemporalResamplingParameters RtRestirPTBuildRtxdiTemporalResamplingParameters(RtRestirPTParameters params)
{
    RTXDI_PTTemporalResamplingParameters result;
    result.depthThreshold = params.temporalResampling_depthThreshold;
    result.normalThreshold = params.temporalResampling_normalThreshold;
    result.enablePermutationSampling = params.temporalResampling_enablePermutationSampling;
    result.maxHistoryLength = params.temporalResampling_maxHistoryLength;
    result.maxReservoirAge = params.temporalResampling_maxReservoirAge;
    result.enableFallbackSampling = params.temporalResampling_enableFallbackSampling;
    result.uniformRandomNumber = params.temporalResampling_uniformRandomNumber;
    result.duplicationBasedHistoryReduction = params.temporalResampling_duplicationBasedHistoryReduction;
    result.historyReductionStrength = params.temporalResampling_historyReductionStrength;
    result.enableVisibilityBeforeCombine = 0u;
    return result;
}

RTXDI_PTHybridShiftPerFrameParameters RtRestirPTBuildRtxdiHybridShiftParameters(RtRestirPTParameters params)
{
    RTXDI_PTHybridShiftPerFrameParameters result;
    result.maxBounceDepth = params.hybridShift_maxBounceDepth;
    result.maxRcVertexLength = params.hybridShift_maxRcVertexLength;
    return result;
}

RTXDI_PTSpatialResamplingParameters RtRestirPTBuildRtxdiSpatialResamplingParameters(RtRestirPTParameters params)
{
    RTXDI_PTSpatialResamplingParameters result;
    result.numSpatialSamples = params.spatialResampling_numSpatialSamples;
    result.numDisocclusionBoostSamples = params.spatialResampling_numDisocclusionBoostSamples;
    result.maxTemporalHistory = params.spatialResampling_maxTemporalHistory;
    result.duplicationBasedHistoryReduction = params.spatialResampling_duplicationBasedHistoryReduction;
    result.samplingRadius = params.spatialResampling_samplingRadius;
    result.normalThreshold = params.spatialResampling_normalThreshold;
    result.depthThreshold = params.spatialResampling_depthThreshold;
    return result;
}

#endif
