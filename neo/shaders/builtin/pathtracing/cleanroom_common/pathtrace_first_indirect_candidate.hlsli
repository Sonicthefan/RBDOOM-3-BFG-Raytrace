// Shared first-indirect path candidate records.
//
// These records are intentionally estimator-neutral. Clean ReSTIR GI consumes
// them today, but the layout is meant to survive a later ReSTIR PT / unified
// DI-GI experiment without inheriting GI-only names.

#ifndef PATH_TRACE_FIRST_INDIRECT_CANDIDATE_HLSLI
#define PATH_TRACE_FIRST_INDIRECT_CANDIDATE_HLSLI

static const uint PATH_TRACE_FIRST_INDIRECT_CANDIDATE_SURFACE_STRIDE_BYTES = 144u;

// Trace -> shade surface record for a first-indirect hit. Mirrors the subset of
// RAB_Surface + RAB_Material needed to shade the candidate without reloading hit
// geometry. Keep this in lockstep with the host buffer stride.
struct PathTraceFirstIndirectCandidateSurface
{
    float3 worldPos;
    uint valid;
    float3 geometryNormal;
    float linearDepth;
    float3 shadingNormal;
    uint materialId;
    float3 viewDir;
    uint materialIndex;
    float3 diffuseAlbedo;
    float roughness;
    float3 specularF0;
    float opacity;
    float3 emissiveRadiance;
    float alphaCutoff;
    uint instanceId;
    uint primitiveIndex;
    uint surfaceClass;
    uint surfaceFlags;
    uint materialFlags;
    uint emissiveTextureIndex;
    uint primarySampledSpecular;
    uint pad0;
};

// Shaded first-indirect candidate. Radiance is incoming radiance at the
// receiver and excludes the receiver BSDF/albedo.
struct PathTraceFirstIndirectCandidateResult
{
    uint valid;
    float3 radiance;
    float pathLength;
    float3 hitPosition;
    float3 hitNormal;
    float3 materialAlbedo;
    float materialOpacity;
    uint materialFlags;
    uint diffuseTextureIndex;
};

#endif // PATH_TRACE_FIRST_INDIRECT_CANDIDATE_HLSLI
