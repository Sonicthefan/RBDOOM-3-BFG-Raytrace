// Reference-shaped final-shading diagnostic helpers for ReSTIR PT debug views.

float3 RestirPTReferenceFinalShadingContribution(RTXDI_PTReservoir reservoir)
{
    return RestirPTSanitizePreviewContribution(reservoir.TargetFunction * max(reservoir.WeightSum, 0.0));
}

bool RestirPTReferenceFinalShadingHasUsefulSample(RTXDI_PTReservoir reservoir)
{
    return RestirPTReservoirHasUsefulSample(reservoir) &&
        RTXDI_Luminance(RestirPTReferenceFinalShadingContribution(reservoir)) > 0.0;
}

float4 RestirPTReferenceFinalShadingStateColor(RTXDI_PTReservoir reservoir)
{
    const bool valid = RTXDI_IsValidPTReservoir(reservoir);
    const float targetHeat = saturate(RTXDI_Luminance(max(reservoir.TargetFunction, float3(0.0, 0.0, 0.0))));
    const float weightHeat = saturate(max(reservoir.WeightSum, 0.0) / (1.0 + max(reservoir.WeightSum, 0.0)));
    const float mNorm = saturate((float)reservoir.M / max((float)RestirPTParams.temporalResampling.maxHistoryLength, 1.0));
    return valid ? float4(targetHeat, weightHeat, mNorm, 1.0) : float4(0.35, 0.0, 0.0, 1.0);
}

float4 RestirPTReferenceFinalShadingContractColor(RAB_Surface surface, RTXDI_PTReservoir reservoir)
{
    if (!RAB_IsSurfaceValid(surface))
    {
        return float4(0.0, 0.0, 0.0, 1.0);
    }
    if (!RTXDI_IsValidPTReservoir(reservoir))
    {
        return float4(0.95, 0.05, 0.05, 1.0);
    }
    if (reservoir.WeightSum != reservoir.WeightSum || abs(reservoir.WeightSum) > 65504.0)
    {
        return float4(1.0, 0.0, 1.0, 1.0);
    }
    if (reservoir.WeightSum < 0.0)
    {
        return float4(1.0, 0.35, 0.0, 1.0);
    }
    if (reservoir.WeightSum == 0.0)
    {
        return float4(1.0, 0.9, 0.0, 1.0);
    }
    if (!all(reservoir.TargetFunction == reservoir.TargetFunction) || any(abs(reservoir.TargetFunction) > 65504.0))
    {
        return float4(0.85, 0.0, 1.0, 1.0);
    }
    if (RTXDI_Luminance(max(reservoir.TargetFunction, float3(0.0, 0.0, 0.0))) <= 0.0)
    {
        return float4(0.0, 0.25, 1.0, 1.0);
    }
    const float3 contribution = reservoir.TargetFunction * reservoir.WeightSum;
    if (!all(contribution == contribution))
    {
        return float4(0.55, 0.0, 1.0, 1.0);
    }
    if (any(abs(contribution) > 65504.0))
    {
        return float4(1.0, 1.0, 1.0, 1.0);
    }
    return float4(0.0, 0.85, 0.2, 1.0);
}

float4 RestirPTReferenceTargetReplayColor(RAB_Surface surface, RTXDI_PTReservoir reservoir)
{
    if (!RAB_IsSurfaceValid(surface))
    {
        return float4(0.0, 0.0, 0.0, 1.0);
    }
    if (!RTXDI_IsValidPTReservoir(reservoir))
    {
        return float4(0.95, 0.05, 0.05, 1.0);
    }
    if (!RTXDI_ConnectsToNeeLight(reservoir))
    {
        return float4(0.05, 0.18, 0.75, 1.0);
    }
    if (!all(reservoir.TargetFunction == reservoir.TargetFunction) ||
        RTXDI_Luminance(max(reservoir.TargetFunction, float3(0.0, 0.0, 0.0))) <= 0.0)
    {
        return float4(1.0, 0.0, 1.0, 1.0);
    }

    float3 replayTargetFunction = float3(0.0, 0.0, 0.0);
    if (!RestirPTTryEvaluateNeeReservoirTargetFunction(surface, reservoir, RestirPTInfo.z >= 0.5, replayTargetFunction))
    {
        return float4(1.0, 0.45, 0.0, 1.0);
    }

    const float storedLum = RTXDI_Luminance(max(reservoir.TargetFunction, float3(0.0, 0.0, 0.0)));
    const float replayLum = RTXDI_Luminance(max(replayTargetFunction, float3(0.0, 0.0, 0.0)));
    if (replayLum <= 0.0)
    {
        return float4(0.0, 0.25, 1.0, 1.0);
    }

    const float ratio = replayLum / max(storedLum, 1.0e-6);
    if (ratio < 0.5 || ratio > 2.0)
    {
        return float4(0.95, 0.85, 0.05, 1.0);
    }
    if (ratio < 0.8 || ratio > 1.25)
    {
        return float4(0.05, 0.85, 0.85, 1.0);
    }
    return float4(0.0, 0.85, 0.2, 1.0);
}

float4 EvaluateRestirPTEnvironmentMisContractView(RAB_Surface surface, uint2 pixel)
{
    const uint2 dimensions = max(PathTraceFullOutputSize(), uint2(1u, 1u));
    const uint band = min((pixel.x * 3u) / dimensions.x, 2u);
    const float3 fallbackDir = RAB_SafeNormalize(CameraForwardAndTanX.xyz, float3(0.0, 0.0, 1.0));
    const float3 envDirection = RAB_IsSurfaceValid(surface)
        ? RAB_SafeNormalize(RAB_GetSurfaceNormal(surface), fallbackDir)
        : fallbackDir;

    if (band == 0u)
    {
        const float2 envUv = RAB_GetEnvironmentMapRandXYFromDir(envDirection);
        return all(envUv == envUv) && all(envUv >= 0.0) && all(envUv <= 1.0)
            ? float4(0.0, 0.85, 0.2, 1.0)
            : float4(1.0, 0.0, 1.0, 1.0);
    }

    if (band == 1u)
    {
        const float envPdf = RAB_EvaluateEnvironmentMapSamplingPdf(envDirection);
        if (envPdf != envPdf || abs(envPdf) > 65504.0)
        {
            return float4(1.0, 0.0, 1.0, 1.0);
        }
        return envPdf > 0.0
            ? float4(0.0, 0.85, 0.2, 1.0)
            : float4(0.95, 0.05, 0.05, 1.0);
    }

    return float4(1.0, 0.45, 0.0, 1.0);
}

float4 RestirPTReferenceFinalShadingOutputColor(RAB_Surface surface, uint2 pixel)
{
    const uint2 dimensions = max(PathTraceFullOutputSize(), uint2(1u, 1u));
    const uint band = min((pixel.x * 6u) / dimensions.x, 5u);
    const uint2 reservoirPixel = PathTraceFullPixelToRestirDirectPixel(pixel);
    const RTXDI_PTReservoir reservoir = LoadRestirPTFinalShadingInputReservoir(reservoirPixel);

    if (band == 0u)
    {
        if (!RAB_IsSurfaceValid(surface))
        {
            return float4(0.0, 0.0, 0.0, 1.0);
        }
        if (!RTXDI_IsValidPTReservoir(reservoir))
        {
            return float4(0.85, 0.02, 0.02, 1.0);
        }
        return RestirPTReferenceFinalShadingHasUsefulSample(reservoir)
            ? float4(0.05, 0.75, 0.12, 1.0)
            : float4(0.05, 0.18, 0.75, 1.0);
    }
    if (band == 1u)
    {
        if (!RTXDI_IsValidPTReservoir(reservoir))
        {
            return float4(0.85, 0.02, 0.02, 1.0);
        }
        const float3 contribution = RestirPTReferenceFinalShadingContribution(reservoir);
        if (!all(contribution == contribution) || any(abs(contribution) > 65504.0))
        {
            return float4(1.0, 0.0, 0.75, 1.0);
        }
        return RTXDI_Luminance(max(contribution, float3(0.0, 0.0, 0.0))) > 0.0
            ? float4(0.05, 0.75, 0.12, 1.0)
            : float4(0.05, 0.18, 0.75, 1.0);
    }
    if (band == 2u)
    {
        return float4(1.0, 0.45, 0.0, 1.0);
    }
    if (band == 3u)
    {
        return float4(1.0, 0.0, 0.75, 1.0);
    }
    if (band == 4u)
    {
        return float4(0.55, 0.22, 0.75, 1.0);
    }

    return RAB_IsSurfaceValid(surface) ? float4(0.05, 0.62, 0.18, 1.0) : float4(0.0, 0.0, 0.0, 1.0);
}

float4 RestirPTReferenceRemainingSupplierGapColor(uint2 pixel)
{
    const uint2 dimensions = max(PathTraceFullOutputSize(), uint2(1u, 1u));
    const uint band = min((pixel.x * 6u) / dimensions.x, 5u);

    if (band == 0u)
    {
        return float4(1.0, 0.45, 0.0, 1.0);
    }
    if (band == 1u)
    {
        return float4(0.95, 0.85, 0.05, 1.0);
    }
    if (band == 2u)
    {
        return float4(1.0, 0.45, 0.0, 1.0);
    }
    if (band == 3u)
    {
        return float4(1.0, 0.0, 0.75, 1.0);
    }
    if (band == 4u)
    {
        return float4(0.55, 0.22, 0.75, 1.0);
    }
    return float4(0.55, 0.05, 0.80, 1.0);
}
