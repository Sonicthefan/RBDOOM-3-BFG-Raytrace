#ifndef RB_PATH_TRACING_REMIX_RAB_TEMPORAL_OUTPUT_BRIDGE_HLSLI
#define RB_PATH_TRACING_REMIX_RAB_TEMPORAL_OUTPUT_BRIDGE_HLSLI

// Remix temporal reuse output bridge.
//
// Spatial reuse, gradients, and GI validation consume these products from the
// temporal pass. The active dispatch slice must provide concrete texture/UAV
// bindings for reprojection confidence and temporal sample position.

struct RemixRAB_TemporalOutput
{
    uint validTemporalSample;
    int2 temporalSamplePixel;
    float reprojectionConfidence;
};

static const int2 REMIX_RAB_INVALID_TEMPORAL_SAMPLE_PIXEL = int2(-1, -1);

#ifndef REMIX_RAB_TEMPORAL_OUTPUT_EXTERNAL_CALLBACKS
float RemixRAB_LoadReprojectionConfidence(int2 pixel)
{
    return 0.0;
}

void RemixRAB_StoreTemporalOutput(int2 pixel, RemixRAB_TemporalOutput output)
{
}
#endif

bool RemixRAB_IsTemporalSamplePixelValid(int2 pixel)
{
    return all(pixel >= int2(0, 0));
}

RemixRAB_TemporalOutput RemixRAB_BuildTemporalOutput(
    int2 temporalSamplePixel,
    uint reprojectionConfidenceHistoryLength)
{
    RemixRAB_TemporalOutput output = (RemixRAB_TemporalOutput)0;
    output.temporalSamplePixel = temporalSamplePixel;
    output.validTemporalSample = RemixRAB_IsTemporalSamplePixelValid(temporalSamplePixel) ? 1u : 0u;

    float previousConfidence = 0.0;
    if (output.validTemporalSample != 0u)
    {
        previousConfidence = RemixRAB_LoadReprojectionConfidence(temporalSamplePixel);
    }

    const float historyLength = max(float(reprojectionConfidenceHistoryLength), 1.0);
    output.reprojectionConfidence = min(previousConfidence + 1.0 / historyLength, 1.0);
    return output;
}

#endif
