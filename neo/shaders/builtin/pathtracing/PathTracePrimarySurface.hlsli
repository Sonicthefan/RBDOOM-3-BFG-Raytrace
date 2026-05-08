#ifndef RB_PATH_TRACE_PRIMARY_SURFACE_HLSLI
#define RB_PATH_TRACE_PRIMARY_SURFACE_HLSLI

static const uint RT_PATH_TRACE_PRIMARY_SURFACE_RECORD_VERSION = 2u;
static const uint RT_PATH_TRACE_PRIMARY_SURFACE_RECORD_STRIDE = 176u;

static const uint RT_PRIMARY_SURFACE_VALID = 0x00000001u;
static const uint RT_PRIMARY_SURFACE_HAS_CAMERA_REPROJECTION = 0x00000002u;
static const uint RT_PRIMARY_SURFACE_HAS_OBJECT_MOTION = 0x00000004u;
static const uint RT_PRIMARY_SURFACE_HAS_PREVIOUS_POSITION = 0x00000008u;

static const uint RT_PRIMARY_SURFACE_DEBUG_OK = 0u;
static const uint RT_PRIMARY_SURFACE_DEBUG_MISSING_CURRENT = 1u;
static const uint RT_PRIMARY_SURFACE_DEBUG_MISSING_PREVIOUS = 2u;
static const uint RT_PRIMARY_SURFACE_DEBUG_REJECTED_PREVIOUS = 3u;
static const uint RT_PRIMARY_SURFACE_DEBUG_MATERIAL_MISMATCH = 4u;
static const uint RT_PRIMARY_SURFACE_DEBUG_NORMAL_MISMATCH = 5u;
static const uint RT_PRIMARY_SURFACE_DEBUG_ROUGHNESS_MISMATCH = 6u;
static const uint RT_PRIMARY_SURFACE_DEBUG_NO_OBJECT_MOTION = 7u;

struct PathTracePrimarySurfaceRecord
{
    uint4 header;                       // version, valid flags, debug/status flags, reserved
    float4 worldPositionAndViewDepth;   // xyz = world position, w = view depth
    float4 geometricNormalAndRoughness; // xyz = geometric normal, w = roughness
    float4 shadingNormalAndOpacity;     // xyz = shading normal, w = opacity
    float4 viewDirectionAndReserved;    // xyz = view direction, w = reserved
    float4 albedoAndAlphaCutoff;        // xyz = diffuse albedo, w = alpha cutoff
    float4 specularF0AndReserved;       // xyz = F0/specular, w = reserved
    float4 emissiveAndHeight;           // xyz = emissive radiance, w = height/parallax/displacement placeholder
    float4 previousPositionOrMotion;    // xyz = previous world position or motion placeholder, w = valid selector
    uint4 materialAndSurface;           // material id, material index, material flags, surface class
    uint4 instancePrimitiveObject;      // instance id, primitive id, object/entity id placeholder, emissive texture index
};

#endif

#ifdef RB_PATH_TRACE_PRIMARY_SURFACE_ENABLE_HELPERS
#ifndef RB_PATH_TRACE_PRIMARY_SURFACE_HELPERS
#define RB_PATH_TRACE_PRIMARY_SURFACE_HELPERS

PathTracePrimarySurfaceRecord PackPathTracePrimarySurfaceRecord(RAB_Surface surface)
{
    PathTracePrimarySurfaceRecord record = (PathTracePrimarySurfaceRecord)0;
    record.header.x = RT_PATH_TRACE_PRIMARY_SURFACE_RECORD_VERSION;
    if (!RAB_IsSurfaceValid(surface))
    {
        record.header.z = RT_PRIMARY_SURFACE_DEBUG_MISSING_CURRENT;
        return record;
    }

    uint validFlags = RT_PRIMARY_SURFACE_VALID;
    if (PrevCameraOriginAndValid.w >= 0.5)
    {
        validFlags |= RT_PRIMARY_SURFACE_HAS_CAMERA_REPROJECTION;
    }

    record.header = uint4(RT_PATH_TRACE_PRIMARY_SURFACE_RECORD_VERSION, validFlags, RT_PRIMARY_SURFACE_DEBUG_NO_OBJECT_MOTION, 0u);
    record.worldPositionAndViewDepth = float4(surface.worldPos, surface.linearDepth);
    record.geometricNormalAndRoughness = float4(surface.geometryNormal, surface.material.roughness);
    record.shadingNormalAndOpacity = float4(surface.shadingNormal, surface.material.opacity);
    record.viewDirectionAndReserved = float4(surface.viewDir, 0.0);
    record.albedoAndAlphaCutoff = float4(surface.material.diffuseAlbedo, surface.material.alphaCutoff);
    record.specularF0AndReserved = float4(surface.material.specularF0, 0.0);
    record.emissiveAndHeight = float4(surface.material.emissiveRadiance, 0.0);
    record.previousPositionOrMotion = float4(0.0, 0.0, 0.0, 0.0);
    record.materialAndSurface = uint4(surface.materialId, surface.materialIndex, surface.flags, surface.surfaceClass);
    // TODO: replace the object/entity placeholder with the chosen Doom renderer/game identity
    // once the long-lived ID source is confirmed for map transitions, entity updates, and save/load.
    record.instancePrimitiveObject = uint4(surface.instanceId, surface.primitiveIndex, 0u, surface.material.emissiveTextureIndex);
    return record;
}

RAB_Surface UnpackPathTracePrimarySurfaceRecord(PathTracePrimarySurfaceRecord record)
{
    RAB_Surface surface = RAB_EmptySurface();
    if (record.header.x != RT_PATH_TRACE_PRIMARY_SURFACE_RECORD_VERSION ||
        (record.header.y & RT_PRIMARY_SURFACE_VALID) == 0u)
    {
        return surface;
    }

    surface.valid = 1u;
    surface.worldPos = record.worldPositionAndViewDepth.xyz;
    surface.linearDepth = record.worldPositionAndViewDepth.w;
    surface.geometryNormal = SafeNormalize(record.geometricNormalAndRoughness.xyz, float3(0.0, 0.0, 1.0));
    surface.shadingNormal = ConstrainSmokeShadingNormal(record.shadingNormalAndOpacity.xyz, surface.geometryNormal);
    surface.viewDir = SafeNormalize(record.viewDirectionAndReserved.xyz, -surface.shadingNormal);
    surface.materialId = record.materialAndSurface.x;
    surface.materialIndex = record.materialAndSurface.y;
    surface.flags = record.materialAndSurface.z;
    surface.surfaceClass = record.materialAndSurface.w;
    surface.instanceId = record.instancePrimitiveObject.x;
    surface.primitiveIndex = record.instancePrimitiveObject.y;

    RAB_Material material = RAB_EmptyMaterial();
    material.materialId = surface.materialId;
    material.materialIndex = surface.materialIndex;
    material.flags = surface.flags;
    material.alphaCutoff = record.albedoAndAlphaCutoff.w;
    material.diffuseAlbedo = record.albedoAndAlphaCutoff.xyz;
    material.roughness = saturate(record.geometricNormalAndRoughness.w);
    material.specularF0 = max(record.specularF0AndReserved.xyz, float3(0.0, 0.0, 0.0));
    material.opacity = saturate(record.shadingNormalAndOpacity.w);
    material.emissiveRadiance = max(record.emissiveAndHeight.xyz, float3(0.0, 0.0, 0.0));
    material.emissiveTextureIndex = record.instancePrimitiveObject.w;
    surface.material = material;
    return surface;
}

void StorePathTracePrimarySurfaceRecord(uint2 pixel, RAB_Surface surface)
{
    if (PathTraceSafetyDisabled(RT_PT_SAFETY_DISABLE_PRIMARY_SURFACE_HISTORY))
    {
        return;
    }

    const uint2 dimensions = DispatchRaysDimensions().xy;
    if (pixel.x >= dimensions.x || pixel.y >= dimensions.y)
    {
        return;
    }

    const uint index = pixel.y * dimensions.x + pixel.x;
    if (index >= PathTracePrimarySurfaceHistoryCount())
    {
        return;
    }

    PrimarySurfaceHistoryCurrent[index] = PackPathTracePrimarySurfaceRecord(surface);
}

RAB_Surface LoadPathTracePrimarySurfaceRecord(int2 pixelPosition, bool previousFrame)
{
    if (PathTraceSafetyDisabled(RT_PT_SAFETY_DISABLE_PRIMARY_SURFACE_HISTORY))
    {
        return RAB_EmptySurface();
    }

    const uint2 dimensions = DispatchRaysDimensions().xy;
    if (pixelPosition.x < 0 || pixelPosition.y < 0 ||
        (uint)pixelPosition.x >= dimensions.x || (uint)pixelPosition.y >= dimensions.y)
    {
        return RAB_EmptySurface();
    }

    const uint index = (uint)pixelPosition.y * dimensions.x + (uint)pixelPosition.x;
    if (index >= PathTracePrimarySurfaceHistoryCount())
    {
        return RAB_EmptySurface();
    }

    if (previousFrame)
    {
        return UnpackPathTracePrimarySurfaceRecord(PrimarySurfaceHistoryPrevious[index]);
    }
    return UnpackPathTracePrimarySurfaceRecord(PrimarySurfaceHistoryCurrent[index]);
}

RAB_Surface RAB_GetGBufferSurface(int2 pixelPosition, bool previousFrame)
{
    return LoadPathTracePrimarySurfaceRecord(pixelPosition, previousFrame);
}

RAB_Material RAB_GetGBufferMaterial(int2 pixelPosition, bool previousFrame)
{
    return RAB_GetGBufferSurface(pixelPosition, previousFrame).material;
}

bool ProjectPathTracePrimarySurfaceToPreviousPixel(float3 worldPosition, uint2 dimensions, out int2 previousPixel)
{
    previousPixel = int2(-1, -1);
    if (PrevCameraOriginAndValid.w < 0.5)
    {
        return false;
    }

    const float3 delta = worldPosition - PrevCameraOriginAndValid.xyz;
    const float forwardDistance = dot(delta, PrevCameraForwardAndTanX.xyz);
    if (forwardDistance <= 0.05)
    {
        return false;
    }

    const float ndcX = -dot(delta, PrevCameraLeftAndTanY.xyz) / max(forwardDistance * PrevCameraForwardAndTanX.w, 1.0e-5);
    const float ndcY = -dot(delta, PrevCameraUpAndTanY.xyz) / max(forwardDistance * PrevCameraLeftAndTanY.w, 1.0e-5);
    if (abs(ndcX) > 1.0 || abs(ndcY) > 1.0)
    {
        return false;
    }

    const float2 previousPixelFloat = (float2(ndcX, ndcY) * 0.5 + 0.5) * float2(dimensions);
    previousPixel = int2(floor(previousPixelFloat));
    return previousPixel.x >= 0 && previousPixel.y >= 0 &&
        (uint)previousPixel.x < dimensions.x && (uint)previousPixel.y < dimensions.y;
}

bool RestirPTProjectWorldToPreviousPixel(float3 worldPosition, uint2 dimensions, out int2 previousPixel)
{
    return ProjectPathTracePrimarySurfaceToPreviousPixel(worldPosition, dimensions, previousPixel);
}

bool PathTracePrimarySurfacesAreSimilar(RAB_Surface a, RAB_Surface b, out uint debugStatus)
{
    debugStatus = RT_PRIMARY_SURFACE_DEBUG_OK;
    const bool currentValid = RAB_IsSurfaceValid(a);
    const bool previousValid = RAB_IsSurfaceValid(b);
    if (!currentValid)
    {
        debugStatus = RT_PRIMARY_SURFACE_DEBUG_MISSING_CURRENT;
        return false;
    }
    if (!previousValid)
    {
        debugStatus = RT_PRIMARY_SURFACE_DEBUG_MISSING_PREVIOUS;
        return false;
    }

    const float normalSimilarity = dot(RAB_GetSurfaceNormal(a), RAB_GetSurfaceNormal(b));
    const float roughnessDelta = abs(GetRoughness(a.material) - GetRoughness(b.material));
    const bool materialMatch = a.materialId == b.materialId &&
        a.materialIndex == b.materialIndex &&
        a.surfaceClass == b.surfaceClass;
    if (!materialMatch)
    {
        debugStatus = RT_PRIMARY_SURFACE_DEBUG_MATERIAL_MISMATCH;
        return false;
    }
    if (normalSimilarity < 0.85)
    {
        debugStatus = RT_PRIMARY_SURFACE_DEBUG_NORMAL_MISMATCH;
        return false;
    }
    if (roughnessDelta > 0.20)
    {
        debugStatus = RT_PRIMARY_SURFACE_DEBUG_ROUGHNESS_MISMATCH;
        return false;
    }

    return true;
}

bool RAB_AreMaterialsSimilar(RAB_Surface a, RAB_Surface b)
{
    uint debugStatus;
    return PathTracePrimarySurfacesAreSimilar(a, b, debugStatus);
}

float4 PathTracePrimarySurfaceDebugColor(uint debugStatus, RAB_Surface currentSurface)
{
    if (debugStatus == RT_PRIMARY_SURFACE_DEBUG_MISSING_CURRENT)
    {
        return float4(0.55, 0.05, 0.04, 1.0);
    }
    if (debugStatus == RT_PRIMARY_SURFACE_DEBUG_MISSING_PREVIOUS)
    {
        return float4(0.05, 0.12, 0.55, 1.0);
    }
    if (debugStatus == RT_PRIMARY_SURFACE_DEBUG_REJECTED_PREVIOUS)
    {
        return float4(0.55, 0.02, 0.16, 1.0);
    }
    if (debugStatus == RT_PRIMARY_SURFACE_DEBUG_MATERIAL_MISMATCH)
    {
        return float4(0.55, 0.42, 0.04, 1.0);
    }
    if (debugStatus == RT_PRIMARY_SURFACE_DEBUG_NORMAL_MISMATCH)
    {
        return float4(0.35, 0.06, 0.55, 1.0);
    }
    if (debugStatus == RT_PRIMARY_SURFACE_DEBUG_ROUGHNESS_MISMATCH)
    {
        return float4(0.55, 0.22, 0.04, 1.0);
    }
    if (debugStatus == RT_PRIMARY_SURFACE_DEBUG_NO_OBJECT_MOTION)
    {
        return float4(0.02, 0.22, 0.24, 1.0);
    }

    const float currentRoughness = saturate(GetRoughness(currentSurface.material));
    const float3 albedo = saturate(currentSurface.material.diffuseAlbedo);
    return float4(saturate(float3(0.02, 0.30, 0.08) + albedo * 0.25 + currentRoughness * float3(0.0, 0.18, 0.08)), 1.0);
}

float4 EvaluateRestirPTPrimarySurfacePairDebug(RAB_Surface currentSurface, RAB_Surface previousSurface)
{
    if (!RAB_IsSurfaceValid(currentSurface) && !RAB_IsSurfaceValid(previousSurface))
    {
        return float4(0.0, 0.0, 0.0, 1.0);
    }

    uint debugStatus;
    if (!PathTracePrimarySurfacesAreSimilar(currentSurface, previousSurface, debugStatus))
    {
        return PathTracePrimarySurfaceDebugColor(debugStatus, currentSurface);
    }

    return PathTracePrimarySurfaceDebugColor(RT_PRIMARY_SURFACE_DEBUG_OK, currentSurface);
}

float4 EvaluateRestirPTPrimarySurfaceHistoryDebug(RAB_Surface currentSurface, uint2 pixel)
{
    const RAB_Surface previousSurface = RAB_GetGBufferSurface(int2((int)pixel.x, (int)pixel.y), true);
    return EvaluateRestirPTPrimarySurfacePairDebug(currentSurface, previousSurface);
}

float4 EvaluateRestirPTPrimarySurfaceReprojectionDebug(RAB_Surface currentSurface)
{
    if (!RAB_IsSurfaceValid(currentSurface))
    {
        return PathTracePrimarySurfaceDebugColor(RT_PRIMARY_SURFACE_DEBUG_MISSING_CURRENT, currentSurface);
    }

    int2 previousPixel;
    const bool projected = ProjectPathTracePrimarySurfaceToPreviousPixel(
        RAB_GetSurfaceWorldPos(currentSurface),
        DispatchRaysDimensions().xy,
        previousPixel);
    if (!projected)
    {
        return PathTracePrimarySurfaceDebugColor(RT_PRIMARY_SURFACE_DEBUG_REJECTED_PREVIOUS, currentSurface);
    }

    const RAB_Surface previousSurface = RAB_GetGBufferSurface(previousPixel, true);
    return EvaluateRestirPTPrimarySurfacePairDebug(currentSurface, previousSurface);
}

// Compatibility wrappers for the existing smoke raygen until task 05 splits
// primary-surface generation out of the monolithic dispatch.
PathTracePrimarySurfaceRecord RestirPTPackPrimarySurfaceHistory(RAB_Surface surface)
{
    return PackPathTracePrimarySurfaceRecord(surface);
}

RAB_Surface RestirPTUnpackPrimarySurfaceHistory(PathTracePrimarySurfaceRecord record)
{
    return UnpackPathTracePrimarySurfaceRecord(record);
}

void StoreRestirPTPrimarySurfaceHistory(uint2 pixel, RAB_Surface surface)
{
    StorePathTracePrimarySurfaceRecord(pixel, surface);
}

#endif
#endif
