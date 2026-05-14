uint SelectSmokeLinearWeightedEmissiveTriangle(uint emissiveTriangleCount, float randomValue)
{
    if (PathTraceSafetyDisabled(RT_PT_SAFETY_DISABLE_EMISSIVE_TRIANGLE_SAMPLING) || emissiveTriangleCount == 0u)
    {
        return 0xffffffffu;
    }

    float cumulative = 0.0;
    uint fallbackIndex = 0u;
    float fallbackWeight = -1.0;
    const float target = saturate(randomValue);

    [loop]
    for (uint triangleIndex = 0u; triangleIndex < emissiveTriangleCount; ++triangleIndex)
    {
        const PathTraceSmokeEmissiveTriangle candidate = SmokeEmissiveTriangles[triangleIndex];
        const float pdf = max(candidate.sampleWeightAndPdf.y, 0.0);
        const float weight = max(candidate.sampleWeightAndPdf.x, 0.0);
        if (weight > fallbackWeight)
        {
            fallbackWeight = weight;
            fallbackIndex = triangleIndex;
        }
        cumulative += pdf;
        if (target <= cumulative && pdf > 0.0)
        {
            return triangleIndex;
        }
    }

    return fallbackIndex;
}

uint SelectSmokeDistributionEmissiveTriangle(uint emissiveTriangleCount, float randomValue)
{
    const uint distributionCount = (uint)max(EmissiveDistributionInfo.x, 0.0);
    const bool distributionEnabled = EmissiveDistributionInfo.y > 0.5;
    const uint fallbackIndex = min((uint)max(EmissiveDistributionInfo.z, 0.0), max(emissiveTriangleCount, 1u) - 1u);
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

uint SelectSmokeWeightedEmissiveTriangle(uint emissiveTriangleCount, float randomValue)
{
    if (PathTraceSafetyDisabled(RT_PT_SAFETY_DISABLE_EMISSIVE_TRIANGLE_SAMPLING) || emissiveTriangleCount == 0u)
    {
        return 0xffffffffu;
    }

    const uint distributionIndex = SelectSmokeDistributionEmissiveTriangle(emissiveTriangleCount, randomValue);
    if (distributionIndex < emissiveTriangleCount)
    {
        return distributionIndex;
    }

    return SelectSmokeLinearWeightedEmissiveTriangle(emissiveTriangleCount, randomValue);
}
