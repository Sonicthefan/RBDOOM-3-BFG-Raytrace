#ifndef RB_PATH_TRACING_CLEANROOM_COMMON_RESTIR_SHARED_MATH_HLSLI
#define RB_PATH_TRACING_CLEANROOM_COMMON_RESTIR_SHARED_MATH_HLSLI

#ifndef RTXDI_PI
#define RTXDI_PI 3.14159265358979323846
#endif

float RTXDI_CompareRelativeDifference(float a, float b)
{
    return abs(a - b) / max(max(abs(a), abs(b)), 1.0e-6);
}

bool RTXDI_CompareRelativeDifference(float a, float b, float threshold)
{
    return threshold <= 0.0 || abs(a - b) <= threshold * max(a, b);
}

void SetBit(inout uint target, uint bit, bool value)
{
    const uint mask = 1u << bit;
    target = value ? (target | mask) : (target & ~mask);
}

bool GetBit(uint target, uint bit)
{
    return ((target >> bit) & 1u) != 0u;
}

float square(float value)
{
    return value * value;
}

float2 square(float2 value)
{
    return value * value;
}

float3 square(float3 value)
{
    return value * value;
}

float clampedDot(float3 a, float3 b)
{
    return max(dot(a, b), 0.0);
}

bool RTXDI_IsValidNeighbor(
    float3 ourNorm,
    float3 theirNorm,
    float ourDepth,
    float theirDepth,
    float normalThreshold,
    float depthThreshold)
{
    return dot(theirNorm, ourNorm) >= normalThreshold &&
        RTXDI_CompareRelativeDifference(ourDepth, theirDepth, depthThreshold);
}

float RTXDI_CalculatePartialJacobian(const float distance, const float3 rayDir, const float3 sampleNormal)
{
    const float cosineEmissionAngle = saturate(dot(sampleNormal, -rayDir));
    return (distance * distance) / max(cosineEmissionAngle, 1.0e-20);
}

void RTXDI_CalculatePartialJacobian(
    const float3 receiverPosition,
    const float3 samplePosition,
    const float3 sampleNormal,
    out float distanceToSurfaceSqr,
    out float cosineEmissionAngle)
{
    const float3 receiverToSample = receiverPosition - samplePosition;
    distanceToSurfaceSqr = max(dot(receiverToSample, receiverToSample), 1.0e-20);
    cosineEmissionAngle = saturate(dot(sampleNormal, receiverToSample * rsqrt(distanceToSurfaceSqr)));
}

float RTXDI_CalculateJacobian(float3 receiverPosition, float3 previousReceiverPosition, float3 samplePosition, float3 sampleNormal)
{
    float newDistanceSqr = 0.0;
    float newCosine = 0.0;
    float originalDistanceSqr = 0.0;
    float originalCosine = 0.0;
    RTXDI_CalculatePartialJacobian(receiverPosition, samplePosition, sampleNormal, newDistanceSqr, newCosine);
    RTXDI_CalculatePartialJacobian(previousReceiverPosition, samplePosition, sampleNormal, originalDistanceSqr, originalCosine);

    float jacobian = (newCosine * originalDistanceSqr) / max(originalCosine * newDistanceSqr, 1.0e-20);
    return (isinf(jacobian) || isnan(jacobian)) ? 0.0 : jacobian;
}

float powerHeuristic(float a, float b)
{
    const float a2 = a * a;
    const float b2 = b * b;
    return a2 / max(a2 + b2, 1.0e-20);
}

float RTXDI_MFactor(float q0, float q1)
{
    return q0 <= 0.0 ? 1.0 : clamp(pow(min(q1 / q0, 1.0), 8.0), 0.0, 1.0);
}

float RTXDI_PairwiseMisWeight(float w0, float w1, float M0, float M1)
{
    const float balanceDenom = M0 * w0 + M1 * w1;
    return balanceDenom <= 0.0 ? 0.0 : max(0.0, M0 * w0) / balanceDenom;
}

uint RTXDI_IntegerExplode(uint x)
{
    x = (x | (x << 8u)) & 0x00FF00FFu;
    x = (x | (x << 4u)) & 0x0F0F0F0Fu;
    x = (x | (x << 2u)) & 0x33333333u;
    x = (x | (x << 1u)) & 0x55555555u;
    return x;
}

uint RTXDI_IntegerCompact(uint x)
{
    x = (x & 0x11111111u) | ((x & 0x44444444u) >> 1u);
    x = (x & 0x03030303u) | ((x & 0x30303030u) >> 2u);
    x = (x & 0x000F000Fu) | ((x & 0x0F000F00u) >> 4u);
    x = (x & 0x000000FFu) | ((x & 0x00FF0000u) >> 8u);
    return x;
}

uint RTXDI_ZCurveToLinearIndex(uint2 xy)
{
    return RTXDI_IntegerExplode(xy.x) | (RTXDI_IntegerExplode(xy.y) << 1u);
}

uint2 RTXDI_LinearIndexToZCurve(uint index)
{
    return uint2(RTXDI_IntegerCompact(index), RTXDI_IntegerCompact(index >> 1u));
}

uint RTXDI_JenkinsHash(uint value)
{
    value = (value + 0x7ed55d16u) + (value << 12u);
    value = (value ^ 0xc761c23cu) ^ (value >> 19u);
    value = (value + 0x165667b1u) + (value << 5u);
    value = (value + 0xd3a2646cu) ^ (value << 9u);
    value = (value + 0xfd7046c5u) + (value << 3u);
    value = (value ^ 0xb55a4f09u) ^ (value >> 16u);
    return value;
}

float RadicalInverse_VdC(uint bits)
{
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10;
}

float2 hammersley2D(uint index, uint sampleCount)
{
    return float2((float)index / max((float)sampleCount, 1.0), RadicalInverse_VdC(index));
}

float3 cosineSampleHemisphere(float2 u)
{
    const float phi = 2.0 * RTXDI_PI * u.x;
    const float cosTheta = sqrt(saturate(1.0 - u.y));
    const float sinTheta = sqrt(saturate(u.y));
    return float3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);
}

#endif
