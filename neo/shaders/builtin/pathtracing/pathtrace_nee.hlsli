static const uint SMOKE_NEE_SOURCE_NONE = 0u;
static const uint SMOKE_NEE_SOURCE_SELECTED_LIGHT = 1u;
static const uint SMOKE_NEE_SOURCE_DOOM_ANALYTIC = 2u;
static const uint SMOKE_NEE_SOURCE_EMISSIVE_TRIANGLE = 3u;
static const uint SMOKE_NEE_SOURCE_ENVIRONMENT = 4u;
static const uint SMOKE_NEE_SOURCE_BRDF = 5u;

static const uint SMOKE_NEE_EVENT_NONE = 0u;
static const uint SMOKE_NEE_EVENT_NEXT_EVENT = 1u;
static const uint SMOKE_NEE_EVENT_EMISSIVE_HIT = 2u;
static const uint SMOKE_NEE_EVENT_ENVIRONMENT_MISS = 3u;
static const uint SMOKE_NEE_EVENT_BRDF_MIS = 4u;

static const uint SMOKE_NEE_SELECTED_LIGHT_MODE_OFF = 0u;
static const uint SMOKE_NEE_SELECTED_LIGHT_MODE_SAMPLED = 1u;
static const uint SMOKE_NEE_SELECTED_LIGHT_MODE_LEGACY_FULL = 2u;

static const uint SMOKE_NEE_ANALYTIC_LIGHT_MODE_OFF = 0u;
static const uint SMOKE_NEE_ANALYTIC_LIGHT_MODE_SAMPLED = 1u;
static const uint SMOKE_NEE_ANALYTIC_LIGHT_MODE_LEGACY_FULL = 2u;

static const uint SMOKE_NEE_SAMPLE_FLAG_ANALYTIC_SOLID_ANGLE_SCALED = 0x40000000u;

struct SmokeNeeSurface
{
    float3 position;
    float3 normal;
    float3 geometricNormal;
    float3 viewDir;
    float3 albedo;
    float3 specularColor;
    float3 f0;
    float roughness;
    uint materialId;
    uint materialIndex;
    uint instanceId;
    uint primitiveIndex;
    uint surfaceClass;
    uint flags;
};

// Native extension record for selected-light NEE today and later analytic,
// emissive, environment, BRDF/MIS, visibility replay, and source attribution.
// The record is local and RTXDI-shaped, not an RTXDI reservoir:
// - sourceType/eventType identify how the candidate was produced.
// - sourceIndex is the shader-visible light-buffer index for replay; analytic
//   samples also mirror renderLightIndex in sourceInstanceId as the current
//   best stable Doom light identity.
// - direction/distance describe the sampled direction from the shaded surface.
// - radiance is the evaluable contribution convention for this local NEE path.
//   Doom analytic sphere samples currently store solid-angle-scaled radiance,
//   marked by SMOKE_NEE_SAMPLE_FLAG_ANALYTIC_SOLID_ANGLE_SCALED, so future
//   reservoir code must not divide by solidAnglePdf again.
// - lightSelectionPdf is the discrete proposal PDF over the eligible uploaded
//   light universe. A reservoir selecting one candidate does not mean the
//   authored or uploaded light universe has been reduced to one light.
// - solidAnglePdf is the directional sample PDF for the chosen light surface.
// - samplePdf is lightSelectionPdf * solidAnglePdf for sampled proposals.
// - targetWeight is the local reflected-radiance score used for RIS/ReSTIR
//   weighting; targetPdf remains zero until a true target-PDF evaluator is
//   wired for this smoke NEE record.
// - flags preserves source light flags plus local sample-contract flags.
struct SmokeNeeLightSample
{
    uint sourceType;
    uint eventType;
    uint sourceIndex;
    uint sourceMaterialId;
    uint sourceInstanceId;
    uint sourcePrimitiveIndex;
    float3 direction;
    float distance;
    float3 radiance;
    float lightSelectionPdf;
    float samplePdf;
    float targetPdf;
    float solidAnglePdf;
    float targetWeight;
    float misWeight;
    float2 uv;
    uint flags;
};

struct SmokeNeeResult
{
    float3 contribution;
    float visibility;
    float targetWeight;
    float targetPdf;
    float samplePdf;
    float lightSelectionPdf;
    float misWeight;
    uint sourceType;
    uint eventType;
    uint sourceIndex;
    uint sourceMaterialId;
    uint sourceInstanceId;
    uint sourcePrimitiveIndex;
    uint flags;
};

SmokeNeeLightSample InitSmokeNeeLightSample()
{
    SmokeNeeLightSample sample;
    sample.sourceType = SMOKE_NEE_SOURCE_NONE;
    sample.eventType = SMOKE_NEE_EVENT_NONE;
    sample.sourceIndex = 0xffffffffu;
    sample.sourceMaterialId = 0xffffffffu;
    sample.sourceInstanceId = 0xffffffffu;
    sample.sourcePrimitiveIndex = 0xffffffffu;
    sample.direction = float3(0.0, 0.0, 1.0);
    sample.distance = 0.0;
    sample.radiance = float3(0.0, 0.0, 0.0);
    sample.lightSelectionPdf = 0.0;
    sample.samplePdf = 0.0;
    sample.targetPdf = 0.0;
    sample.solidAnglePdf = 0.0;
    sample.targetWeight = 0.0;
    sample.misWeight = 1.0;
    sample.uv = float2(0.0, 0.0);
    sample.flags = 0u;
    return sample;
}

SmokeNeeResult InitSmokeNeeResult()
{
    SmokeNeeResult result;
    result.contribution = float3(0.0, 0.0, 0.0);
    result.visibility = 0.0;
    result.targetWeight = 0.0;
    result.targetPdf = 0.0;
    result.samplePdf = 0.0;
    result.lightSelectionPdf = 0.0;
    result.misWeight = 1.0;
    result.sourceType = SMOKE_NEE_SOURCE_NONE;
    result.eventType = SMOKE_NEE_EVENT_NONE;
    result.sourceIndex = 0xffffffffu;
    result.sourceMaterialId = 0xffffffffu;
    result.sourceInstanceId = 0xffffffffu;
    result.sourcePrimitiveIndex = 0xffffffffu;
    result.flags = 0u;
    return result;
}

SmokeNeeSurface BuildSmokeNeeSurface(
    PathTraceSmokePayload payload,
    float3 rayOrigin,
    float3 rayDirection,
    bool useNormalMap,
    bool useSpecular,
    out PathTraceSmokeMaterial material)
{
    material = LoadSmokeMaterial(payload.materialIndex);

    SmokeNeeSurface surface;
    surface.albedo = SampleSmokeSurfaceAlbedo(material, payload.texCoord, payload.surfaceClass, payload.translucentSubtype, payload.vertexColor, payload.vertexColorAdd).rgb;
    surface.geometricNormal = SafeNormalize(payload.geometricNormal, float3(0.0, 0.0, 1.0));
    const float3 baseNormal = SafeNormalize(payload.normal, surface.geometricNormal);
    surface.normal = useNormalMap ? DecodeSmokeNormalTexture(material, payload.texCoord, baseNormal, payload.tangent, payload.bitangent) : baseNormal;
    surface.position = rayOrigin + rayDirection * payload.hitT;
    surface.viewDir = SafeNormalize(rayOrigin - surface.position, -rayDirection);
    surface.specularColor = useSpecular ? SampleSmokeDirectSpecular(material, payload.texCoord) : float3(0.0, 0.0, 0.0);
    surface.f0 = surface.specularColor;
    surface.roughness = 1.0;
    if (useSpecular && SmokeToyFakePBRSpecularEnabled() && max(max(surface.specularColor.r, surface.specularColor.g), surface.specularColor.b) > 0.0)
    {
        SmokePBRFromSpecmap(saturate(surface.specularColor), surface.f0, surface.roughness);
    }
    surface.materialId = payload.materialId;
    surface.materialIndex = payload.materialIndex;
    surface.instanceId = payload.instanceId;
    surface.primitiveIndex = payload.primitiveIndex;
    surface.surfaceClass = payload.surfaceClass;
    surface.flags = payload.triangleClassAndFlags;
    return surface;
}

float3 EvaluateSmokeNeeSpecular(in SmokeNeeSurface surface, float3 lightDir, float3 radiance, float visibility)
{
    if (visibility <= 0.0 || max(max(surface.specularColor.r, surface.specularColor.g), surface.specularColor.b) <= 0.0)
    {
        return float3(0.0, 0.0, 0.0);
    }

    const float3 halfVector = SafeNormalize(lightDir + surface.viewDir, surface.normal);
    if (SmokeToyFakePBRSpecularEnabled())
    {
        const float ndotl = saturate(dot(surface.normal, lightDir));
        const float hdotN = saturate(dot(surface.normal, halfVector));
        const float ldotH = saturate(dot(lightDir, halfVector));
        const float rr = surface.roughness * surface.roughness;
        const float rrrr = max(rr * rr, 1.0e-4);
        const float D = max((hdotN * hdotN) * (rrrr - 1.0) + 1.0, 1.0e-4);
        const float VFapprox = max((ldotH * ldotH) * (surface.roughness + 0.5), 1.0e-4);
        const float specularTerm = (rrrr / (4.0 * D * D * VFapprox)) * ndotl * visibility;
        return surface.f0 * radiance * specularTerm;
    }

    const float specularNdotH = saturate(dot(surface.normal, halfVector));
    const float specularTerm = pow(specularNdotH, 32.0) * visibility;
    return surface.specularColor * radiance * specularTerm;
}

bool SmokeNeeIsFinitePositive(float value)
{
    return value > 0.0 && value < 3.402823e+38 && value == value;
}

float SmokeNeeAnalyticTargetWeight(in SmokeNeeSurface surface, float3 lightDir, float3 radiance)
{
    const float ndotl = saturate(dot(surface.normal, lightDir));
    if (ndotl <= 0.0)
    {
        return 0.0;
    }

    const float3 diffuse = surface.albedo * max(radiance, float3(0.0, 0.0, 0.0)) * ndotl;
    const float3 specular = EvaluateSmokeNeeSpecular(surface, lightDir, radiance, 1.0);
    const float3 reflected = diffuse + specular;
    return max(max(reflected.r, reflected.g), reflected.b);
}

bool BuildSmokeSelectedLightSample(in SmokeNeeSurface surface, uint lightIndex, float lightScale, float maxToyRayDistance, float samplePdf, out SmokeNeeLightSample sample)
{
    sample = InitSmokeNeeLightSample();

    const float4 lightOriginAndRadius = LightOriginAndRadius[lightIndex];
    const float3 toLight = lightOriginAndRadius.xyz - surface.position;
    const float lightDistance = length(toLight);
    if (lightDistance <= 1.0e-3 || lightDistance > maxToyRayDistance)
    {
        return false;
    }

    const float3 lightDir = toLight / lightDistance;
    const float ndotl = saturate(dot(surface.normal, lightDir));
    if (ndotl <= 0.0)
    {
        return false;
    }

    const float lightAttenuation = saturate(1.0 - lightDistance / max(lightOriginAndRadius.w, 1.0));
    const float directScale = 0.10 + lightAttenuation * lightAttenuation * 0.70;
    sample.sourceType = SMOKE_NEE_SOURCE_SELECTED_LIGHT;
    sample.eventType = SMOKE_NEE_EVENT_NEXT_EVENT;
    sample.sourceIndex = lightIndex;
    sample.direction = lightDir;
    sample.distance = lightDistance;
    sample.radiance = max(LightColorAndIntensity[lightIndex].rgb, float3(0.0, 0.0, 0.0)) * (directScale * lightScale);
    sample.lightSelectionPdf = samplePdf;
    sample.samplePdf = samplePdf;
    sample.targetPdf = 0.0;
    sample.solidAnglePdf = 0.0;
    sample.targetWeight = ndotl * max(max(sample.radiance.r, sample.radiance.g), sample.radiance.b);
    sample.misWeight = 1.0;
    return true;
}

SmokeNeeResult EvaluateSmokeSelectedLightSample(in SmokeNeeSurface surface, in SmokeNeeLightSample sample, float maxToyRayDistance, bool traceVisibility)
{
    SmokeNeeResult result = InitSmokeNeeResult();
    result.sourceType = sample.sourceType;
    result.eventType = sample.eventType;
    result.sourceIndex = sample.sourceIndex;
    result.sourceMaterialId = sample.sourceMaterialId;
    result.sourceInstanceId = sample.sourceInstanceId;
    result.sourcePrimitiveIndex = sample.sourcePrimitiveIndex;
    result.lightSelectionPdf = sample.lightSelectionPdf;
    result.samplePdf = sample.samplePdf;
    result.targetPdf = sample.targetPdf;
    result.targetWeight = sample.targetWeight;
    result.misWeight = sample.misWeight;
    if (sample.sourceType != SMOKE_NEE_SOURCE_SELECTED_LIGHT || sample.samplePdf <= 0.0)
    {
        return result;
    }

    const float ndotl = saturate(dot(surface.normal, sample.direction));
    if (ndotl <= 0.0)
    {
        return result;
    }

    const float normalOffsetSign = dot(surface.normal, sample.direction) >= 0.0 ? 1.0 : -1.0;
    const float3 shadowOrigin = surface.position + surface.normal * (normalOffsetSign * 0.75) + sample.direction * 0.25;
    const float shadowTMax = min(max(sample.distance - 0.5, 0.01), maxToyRayDistance);
    const float visibility = traceVisibility ? TraceSmokeShadowVisibility(shadowOrigin, sample.direction, shadowTMax, 0xffffffffu, 0xffffffffu, 0xffffffffu) : 1.0;
    const float invPdf = 1.0 / sample.samplePdf;
    result.visibility = visibility;
    result.targetWeight = ndotl * max(max(sample.radiance.r, sample.radiance.g), sample.radiance.b);
    result.contribution = (surface.albedo * sample.radiance * ndotl + EvaluateSmokeNeeSpecular(surface, sample.direction, sample.radiance, 1.0)) * visibility * invPdf;
    return result;
}

float3 EvaluateSmokeLegacySelectedLightNee(in SmokeNeeSurface surface, float lightScale, float maxToyRayDistance)
{
    const uint lightCount = PathTraceSafetyDisabled(RT_PT_SAFETY_DISABLE_SELECTED_LIGHT_LOOP) ? 0u : min((uint)LightInfo.x, RT_SMOKE_MAX_DEBUG_LIGHTS);
    if (lightCount == 0u)
    {
        return float3(0.0, 0.0, 0.0);
    }

    float3 direct = float3(0.0, 0.0, 0.0);
    const float samplePdf = 1.0;
    [loop]
    for (uint lightIndex = 0u; lightIndex < lightCount; lightIndex++)
    {
        SmokeNeeLightSample sample;
        if (!BuildSmokeSelectedLightSample(surface, lightIndex, lightScale, maxToyRayDistance, samplePdf, sample))
        {
            continue;
        }

        const float normalOffsetSign = dot(surface.normal, sample.direction) >= 0.0 ? 1.0 : -1.0;
        const float3 shadowOrigin = surface.position + surface.normal * (normalOffsetSign * 0.75) + sample.direction * 0.25;
        const float shadowTMax = min(max(sample.distance - 0.5, 0.01), maxToyRayDistance);
        const float visibility = TraceSmokeShadowVisibility(shadowOrigin, sample.direction, shadowTMax, 0xffffffffu, 0xffffffffu, 0xffffffffu);
        const float ndotl = saturate(dot(surface.normal, sample.direction));
        direct += surface.albedo * sample.radiance * (ndotl * visibility);
        direct += EvaluateSmokeNeeSpecular(surface, sample.direction, sample.radiance, visibility);
    }

    return direct;
}

uint SelectSmokeUniformSelectedLight(uint lightCount, uint seed)
{
    if (lightCount == 0u)
    {
        return 0xffffffffu;
    }
    const float randomValue = SmokeHashToUnitFloat(seed ^ 0x6d2b79f5u);
    return min((uint)(randomValue * (float)lightCount), lightCount - 1u);
}

float3 EvaluateSmokeSampledSelectedLightNee(in SmokeNeeSurface surface, float lightScale, float maxToyRayDistance, uint seed, bool traceVisibility)
{
    const uint lightCount = PathTraceSafetyDisabled(RT_PT_SAFETY_DISABLE_SELECTED_LIGHT_LOOP) ? 0u : min((uint)LightInfo.x, RT_SMOKE_MAX_DEBUG_LIGHTS);
    if (lightCount == 0u)
    {
        return float3(0.0, 0.0, 0.0);
    }

    const uint lightIndex = SelectSmokeUniformSelectedLight(lightCount, seed);
    const float samplePdf = 1.0 / (float)lightCount;
    SmokeNeeLightSample sample;
    if (!BuildSmokeSelectedLightSample(surface, lightIndex, lightScale, maxToyRayDistance, samplePdf, sample))
    {
        return float3(0.0, 0.0, 0.0);
    }

    return EvaluateSmokeSelectedLightSample(surface, sample, maxToyRayDistance, traceVisibility).contribution;
}

bool BuildSmokeDoomAnalyticLightSample(in SmokeNeeSurface surface, uint lightIndex, float intensityScale, float lightSelectionPdf, uint seed, out SmokeNeeLightSample sample)
{
    sample = InitSmokeNeeLightSample();

    const uint uploadedCount = (uint)max(DoomAnalyticLightInfo.x, 0.0);
    const uint traceCap = (uint)max(DoomAnalyticLightInfo.y, 0.0);
    if (lightIndex >= min(uploadedCount, traceCap) || lightSelectionPdf <= 0.0)
    {
        return false;
    }

    const PathTraceDoomAnalyticLightCandidate light = DoomAnalyticLights[lightIndex];
    const float3 toLight = light.originAndRadius.xyz - surface.position;
    const float distanceSquared = max(dot(toLight, toLight), 1.0);
    const float lightDistance = sqrt(distanceSquared);
    const float3 lightDir = toLight / lightDistance;
    const float ndotl = saturate(dot(surface.normal, lightDir));
    if (ndotl <= 0.0)
    {
        return false;
    }

    const float doomRadius = max(light.doomRadiusAndArea.x, 1.0);
    if (lightDistance > doomRadius)
    {
        return false;
    }

    const float sphereRadius = clamp(light.originAndRadius.w, 0.01, doomRadius);
    const float sinThetaMax = saturate(sphereRadius / lightDistance);
    const float cosThetaMax = sqrt(max(0.0, 1.0 - sinThetaMax * sinThetaMax));
    const float solidAngle = max(6.2831853 * (1.0 - cosThetaMax), 1.0e-5);
    const uint lightSeed = seed ^ (light.renderLightIndex + 1u) * 747796405u ^ (lightIndex + 1u) * 2891336453u;
    const float3 sampledLightDir = SmokeSampleSphereSolidAngle(lightDir, cosThetaMax, lightSeed);
    const float sampledNdotL = saturate(dot(surface.normal, sampledLightDir));
    if (sampledNdotL <= 0.0)
    {
        return false;
    }

    const float radiusFraction = saturate(lightDistance / doomRadius);
    const float doomInfluence = saturate(1.0 - radiusFraction * radiusFraction);
    const float directScale = (solidAngle * 0.318309886) * doomInfluence * intensityScale;
    const float3 lightColor = max(light.colorAndIntensity.rgb, float3(0.0, 0.0, 0.0));

    sample.sourceType = SMOKE_NEE_SOURCE_DOOM_ANALYTIC;
    sample.eventType = SMOKE_NEE_EVENT_NEXT_EVENT;
    sample.sourceIndex = lightIndex;
    sample.sourceInstanceId = light.renderLightIndex;
    sample.direction = sampledLightDir;
    sample.distance = lightDistance;
    sample.radiance = lightColor * directScale;
    sample.lightSelectionPdf = lightSelectionPdf;
    sample.solidAnglePdf = 1.0 / solidAngle;
    sample.samplePdf = sample.lightSelectionPdf * sample.solidAnglePdf;
    sample.targetPdf = 0.0;
    sample.targetWeight = SmokeNeeAnalyticTargetWeight(surface, sampledLightDir, sample.radiance);
    sample.misWeight = 1.0;
    sample.flags = light.flags | SMOKE_NEE_SAMPLE_FLAG_ANALYTIC_SOLID_ANGLE_SCALED;
    return true;
}

uint SmokeDoomAnalyticLightCount()
{
    if (!DoomAnalyticLightsEnabled())
    {
        return 0u;
    }

    const uint uploadedCount = (uint)max(DoomAnalyticLightInfo.x, 0.0);
    const uint traceCap = (uint)max(DoomAnalyticLightInfo.y, 0.0);
    return min(uploadedCount, traceCap);
}

bool ValidateSmokeDoomAnalyticLightSampleForReplay(in SmokeNeeLightSample sample)
{
    const uint lightCount = SmokeDoomAnalyticLightCount();
    if (sample.sourceType != SMOKE_NEE_SOURCE_DOOM_ANALYTIC ||
        sample.eventType != SMOKE_NEE_EVENT_NEXT_EVENT ||
        sample.sourceIndex >= lightCount)
    {
        return false;
    }

    const PathTraceDoomAnalyticLightCandidate light = DoomAnalyticLights[sample.sourceIndex];
    const float expectedSamplePdf = sample.lightSelectionPdf * sample.solidAnglePdf;
    return
        sample.sourceInstanceId == light.renderLightIndex &&
        SmokeNeeIsFinitePositive(sample.lightSelectionPdf) &&
        SmokeNeeIsFinitePositive(sample.solidAnglePdf) &&
        SmokeNeeIsFinitePositive(sample.samplePdf) &&
        SmokeNeeIsFinitePositive(sample.distance) &&
        sample.samplePdf >= expectedSamplePdf * 0.999 &&
        sample.samplePdf <= expectedSamplePdf * 1.001;
}

SmokeNeeResult EvaluateSmokeDoomAnalyticLightSampleForSurface(in SmokeNeeSurface surface, in SmokeNeeLightSample sample, bool traceVisibility)
{
    SmokeNeeResult result = InitSmokeNeeResult();
    result.sourceType = sample.sourceType;
    result.eventType = sample.eventType;
    result.sourceIndex = sample.sourceIndex;
    result.sourceMaterialId = sample.sourceMaterialId;
    result.sourceInstanceId = sample.sourceInstanceId;
    result.sourcePrimitiveIndex = sample.sourcePrimitiveIndex;
    result.lightSelectionPdf = sample.lightSelectionPdf;
    result.samplePdf = sample.samplePdf;
    result.targetPdf = sample.targetPdf;
    result.targetWeight = sample.targetWeight;
    result.misWeight = sample.misWeight;
    result.flags = sample.flags;
    if (!ValidateSmokeDoomAnalyticLightSampleForReplay(sample))
    {
        return result;
    }

    const float ndotl = saturate(dot(surface.normal, sample.direction));
    if (ndotl <= 0.0)
    {
        return result;
    }

    const float normalOffsetSign = dot(surface.normal, sample.direction) >= 0.0 ? 1.0 : -1.0;
    const float3 shadowOrigin = surface.position + surface.normal * (normalOffsetSign * 0.75) + sample.direction * 0.25;
    const PathTraceDoomAnalyticLightCandidate light = DoomAnalyticLights[sample.sourceIndex];
    const float sphereRadius = clamp(light.originAndRadius.w, 0.01, max(light.doomRadiusAndArea.x, 1.0));
    const float sampledHitT = SmokeRaySphereHitT(shadowOrigin, sample.direction, light.originAndRadius.xyz, sphereRadius, sample.distance);
    const float shadowTMax = max(sampledHitT - 0.5, 0.01);
    const float visibility = traceVisibility && (sample.flags & RT_DOOM_ANALYTIC_LIGHT_CASTS_SHADOWS) != 0u
        ? TraceSmokeShadowVisibility(shadowOrigin, sample.direction, shadowTMax, 0xffffffffu, 0xffffffffu, 0xffffffffu)
        : 1.0;

    result.visibility = visibility;
    result.targetWeight = SmokeNeeAnalyticTargetWeight(surface, sample.direction, sample.radiance);
    result.contribution = surface.albedo * sample.radiance * (ndotl * visibility);
    result.contribution += EvaluateSmokeNeeSpecular(surface, sample.direction, sample.radiance, visibility);
    return result;
}

SmokeNeeResult EvaluateSmokeDoomAnalyticLightSample(in SmokeNeeSurface surface, in SmokeNeeLightSample sample)
{
    return EvaluateSmokeDoomAnalyticLightSampleForSurface(surface, sample, true);
}

float3 EvaluateDoomAnalyticSphereLightsLegacyFullForSurface(in SmokeNeeSurface surface, uint seed)
{
    if (!DoomAnalyticLightsEnabled())
    {
        return float3(0.0, 0.0, 0.0);
    }

    const uint uploadedCount = (uint)max(DoomAnalyticLightInfo.x, 0.0);
    const uint traceCap = (uint)max(DoomAnalyticLightInfo.y, 0.0);
    const uint lightCount = min(uploadedCount, traceCap);
    const float intensityScale = max(DoomAnalyticLightInfo.z, 0.0);
    if (lightCount == 0u || intensityScale <= 0.0)
    {
        return float3(0.0, 0.0, 0.0);
    }

    float3 direct = float3(0.0, 0.0, 0.0);
    [loop]
    for (uint lightIndex = 0u; lightIndex < lightCount; ++lightIndex)
    {
        SmokeNeeLightSample sample;
        if (!BuildSmokeDoomAnalyticLightSample(surface, lightIndex, intensityScale, 1.0, seed, sample))
        {
            continue;
        }

        direct += EvaluateSmokeDoomAnalyticLightSample(surface, sample).contribution;
    }

    return direct;
}

uint SelectSmokeUniformDoomAnalyticLight(uint lightCount, uint seed, uint proposalIndex)
{
    if (lightCount == 0u)
    {
        return 0xffffffffu;
    }

    const uint proposalSeed = seed ^ 0xa511e9b3u ^ (proposalIndex + 1u) * 2246822519u;
    const float randomValue = SmokeHashToUnitFloat(proposalSeed);
    return min((uint)(randomValue * (float)lightCount), lightCount - 1u);
}

float3 EvaluateSmokeSampledDoomAnalyticNee(in SmokeNeeSurface surface, uint seed, uint requestedSampleCount)
{
    if (!DoomAnalyticLightsEnabled())
    {
        return float3(0.0, 0.0, 0.0);
    }

    const uint uploadedCount = (uint)max(DoomAnalyticLightInfo.x, 0.0);
    const uint traceCap = (uint)max(DoomAnalyticLightInfo.y, 0.0);
    const uint lightCount = min(uploadedCount, traceCap);
    const uint sampleCount = min(clamp(requestedSampleCount, 0u, 8u), lightCount);
    const float intensityScale = max(DoomAnalyticLightInfo.z, 0.0);
    if (lightCount == 0u || sampleCount == 0u || intensityScale <= 0.0)
    {
        return float3(0.0, 0.0, 0.0);
    }

    const float lightSelectionPdf = 1.0 / (float)lightCount;
    const float sampleAverageScale = 1.0 / (float)sampleCount;
    float3 direct = float3(0.0, 0.0, 0.0);

    [loop]
    for (uint proposalIndex = 0u; proposalIndex < sampleCount; ++proposalIndex)
    {
        const uint lightIndex = SelectSmokeUniformDoomAnalyticLight(lightCount, seed, proposalIndex);
        SmokeNeeLightSample sample;
        if (!BuildSmokeDoomAnalyticLightSample(surface, lightIndex, intensityScale, lightSelectionPdf, seed ^ proposalIndex * 3266489917u, sample))
        {
            continue;
        }

        // The per-light evaluator uses a one-sample solid-angle estimator whose
        // radiance already includes the solid-angle scale. Sampled secondary
        // mode compensates only the discrete light proposal PDF here.
        direct += EvaluateSmokeDoomAnalyticLightSample(surface, sample).contribution * (sampleAverageScale / sample.lightSelectionPdf);
    }

    return direct;
}

float3 EvaluateDoomAnalyticSphereLightsForSurface(in SmokeNeeSurface surface, uint seed)
{
    return EvaluateDoomAnalyticSphereLightsLegacyFullForSurface(surface, seed);
}

float3 EvaluateSmokeSecondaryAnalyticNee(in SmokeNeeSurface surface, uint seed, uint analyticLightMode)
{
    const uint mode = clamp(analyticLightMode, SMOKE_NEE_ANALYTIC_LIGHT_MODE_OFF, SMOKE_NEE_ANALYTIC_LIGHT_MODE_LEGACY_FULL);
    if (mode == SMOKE_NEE_ANALYTIC_LIGHT_MODE_LEGACY_FULL)
    {
        return EvaluateDoomAnalyticSphereLightsLegacyFullForSurface(surface, seed);
    }

    if (mode == SMOKE_NEE_ANALYTIC_LIGHT_MODE_SAMPLED)
    {
        return EvaluateSmokeSampledDoomAnalyticNee(surface, seed, PathTraceIntegratorSecondaryAnalyticNeeSamples());
    }

    return float3(0.0, 0.0, 0.0);
}

float3 EvaluateDoomAnalyticSphereLights(float3 albedo, float3 specularColor, float3 normal, float3 viewDir, float3 hitPosition, uint seed)
{
    SmokeNeeSurface surface;
    surface.position = hitPosition;
    surface.normal = SafeNormalize(normal, float3(0.0, 0.0, 1.0));
    surface.geometricNormal = surface.normal;
    surface.viewDir = SafeNormalize(viewDir, float3(0.0, 0.0, 1.0));
    surface.albedo = albedo;
    surface.specularColor = specularColor;
    surface.f0 = specularColor;
    surface.roughness = 1.0;
    if (SmokeToyFakePBRSpecularEnabled() && max(max(specularColor.r, specularColor.g), specularColor.b) > 0.0)
    {
        SmokePBRFromSpecmap(saturate(specularColor), surface.f0, surface.roughness);
    }
    surface.materialId = 0xffffffffu;
    surface.materialIndex = 0xffffffffu;
    surface.instanceId = 0xffffffffu;
    surface.primitiveIndex = 0xffffffffu;
    surface.surfaceClass = 0u;
    surface.flags = 0u;
    return EvaluateDoomAnalyticSphereLightsForSurface(surface, seed);
}

float3 EvaluateSmokeMode18NeeDirectLighting(PathTraceSmokePayload payload, float3 rayOrigin, float3 rayDirection, bool useNormalMap, bool useSpecular, bool includeEmissive, uint seed, uint selectedLightMode, uint analyticLightMode)
{
    PathTraceSmokeMaterial material;
    const SmokeNeeSurface surface = BuildSmokeNeeSurface(payload, rayOrigin, rayDirection, useNormalMap, useSpecular, material);
    const float lightScale = max(ToyPathInfo.y, 0.0);
    const float emissiveScale = max(ToyPathInfo.z, 0.0);
    const bool activeEmissiveStage = (payload.triangleClassAndFlags & RT_SMOKE_TRIANGLE_EMISSIVE_STAGE_OFF) == 0u;
    const float3 emissive = includeEmissive ? SampleSmokeEmissive(material, payload.texCoord, payload.surfaceClass, activeEmissiveStage) * emissiveScale : float3(0.0, 0.0, 0.0);
    const float ambientScale = saturate(LightSpriteInfo.w);
    const float maxToyRayDistance = max(ToyPathInfo.x, 64.0);
    float3 direct = surface.albedo * ambientScale + emissive;

    if (!PathTraceIntegratorNextEventEstimationEnabled())
    {
        return direct;
    }

    const uint mode = clamp(selectedLightMode, SMOKE_NEE_SELECTED_LIGHT_MODE_OFF, SMOKE_NEE_SELECTED_LIGHT_MODE_LEGACY_FULL);
    if (mode == SMOKE_NEE_SELECTED_LIGHT_MODE_LEGACY_FULL)
    {
        direct += EvaluateSmokeLegacySelectedLightNee(surface, lightScale, maxToyRayDistance);
    }
    else if (mode == SMOKE_NEE_SELECTED_LIGHT_MODE_SAMPLED)
    {
        direct += EvaluateSmokeSampledSelectedLightNee(surface, lightScale, maxToyRayDistance, seed, PathTraceIntegratorSecondaryNeeVisibilityEnabled());
    }

    const uint analyticMode = clamp(analyticLightMode, SMOKE_NEE_ANALYTIC_LIGHT_MODE_OFF, SMOKE_NEE_ANALYTIC_LIGHT_MODE_LEGACY_FULL);
    if (analyticMode == SMOKE_NEE_ANALYTIC_LIGHT_MODE_LEGACY_FULL)
    {
        direct += EvaluateDoomAnalyticSphereLightsLegacyFullForSurface(surface, seed);
    }
    else if (analyticMode == SMOKE_NEE_ANALYTIC_LIGHT_MODE_SAMPLED)
    {
        direct += EvaluateSmokeSecondaryAnalyticNee(surface, seed, analyticMode);
    }

    return direct;
}
