uint PathTraceCleanRoomSelectDistributionEmissiveTriangle(uint emissiveTriangleCount, float randomValue)
{
    const uint distributionCount = (uint)max(CleanRtxdiDiEmissiveDistributionInfo.x, 0.0);
    const bool distributionEnabled = CleanRtxdiDiEmissiveDistributionInfo.y > 0.5;
    const uint fallbackIndex = min((uint)max(CleanRtxdiDiEmissiveDistributionInfo.z, 0.0), max(emissiveTriangleCount, 1u) - 1u);
    if (!distributionEnabled || distributionCount == 0u || emissiveTriangleCount == 0u)
    {
        return 0xffffffffu;
    }

    uint low = 0u;
    uint high = distributionCount;
    const float target = saturate(randomValue);
    [loop]
    while (low < high)
    {
        const uint mid = low + ((high - low) >> 1u);
        const PathTraceEmissiveDistributionEntry entry = SmokeEmissiveDistribution[mid];
        if (target <= entry.cumulativePdf)
        {
            high = mid;
        }
        else
        {
            low = mid + 1u;
        }
    }

    if (low < distributionCount)
    {
        const uint triangleIndex = SmokeEmissiveDistribution[low].emissiveTriangleIndex;
        if (triangleIndex < emissiveTriangleCount)
        {
            return triangleIndex;
        }
    }

    return fallbackIndex;
}

bool PathTraceCleanRoomSelectWeightedEmissiveRluCandidate(
    uint2 range,
    uint sampleCount,
    uint totalSampleCount,
    float randomValue,
    out uint lightIndex,
    out float sourcePdf)
{
    lightIndex = 0xffffffffu;
    sourcePdf = 0.0;
    if (range.y == 0u || sampleCount == 0u || totalSampleCount == 0u)
    {
        return false;
    }

    const uint emissiveTriangleCount = min(CleanRtxdiDiCurrentEmissiveTriangleCount, range.y);
    const uint triangleIndex = PathTraceCleanRoomSelectDistributionEmissiveTriangle(emissiveTriangleCount, randomValue);
    if (triangleIndex >= emissiveTriangleCount)
    {
        return false;
    }

    const uint candidateLightIndex = range.x + triangleIndex;
    if (candidateLightIndex >= CleanRtxdiDiRluCurrentLightCount)
    {
        return false;
    }

    const PathTraceUnifiedLightRecord record = CleanRtxdiDiRluCurrentLights[candidateLightIndex];
    if (record.type != PATH_TRACE_UNIFIED_LIGHT_TYPE_EMISSIVE_TRIANGLE ||
        record.sourceIndex != triangleIndex ||
        record.sourcePdf <= 0.0)
    {
        return false;
    }

    lightIndex = candidateLightIndex;
    sourcePdf = record.sourcePdf * ((float)sampleCount / max((float)totalSampleCount, 1.0e-8));
    return sourcePdf > 0.0;
}
