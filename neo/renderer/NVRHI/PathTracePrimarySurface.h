#pragma once

// CPU-side primary-surface contract for the RT/PT path.
//
// The shader-side mirror lives in
// neo/shaders/builtin/pathtracing/PathTracePrimarySurface.hlsli. Keep the
// record version, stride, and field order in sync when adding denoiser,
// motion-vector, or richer material fields.

#include <cstdint>

static constexpr uint32_t RT_PATH_TRACE_PRIMARY_SURFACE_RECORD_VERSION = 2;
static constexpr uint32_t RT_PATH_TRACE_PRIMARY_SURFACE_RECORD_STRIDE = 176;

enum RtPathTracePrimarySurfaceValidFlags : uint32_t
{
    RT_PRIMARY_SURFACE_VALID = 1u << 0,
    RT_PRIMARY_SURFACE_HAS_CAMERA_REPROJECTION = 1u << 1,
    RT_PRIMARY_SURFACE_HAS_OBJECT_MOTION = 1u << 2,
    RT_PRIMARY_SURFACE_HAS_PREVIOUS_POSITION = 1u << 3
};

enum RtPathTracePrimarySurfaceDebugStatus : uint32_t
{
    RT_PRIMARY_SURFACE_DEBUG_OK = 0,
    RT_PRIMARY_SURFACE_DEBUG_MISSING_CURRENT = 1,
    RT_PRIMARY_SURFACE_DEBUG_MISSING_PREVIOUS = 2,
    RT_PRIMARY_SURFACE_DEBUG_REJECTED_PREVIOUS = 3,
    RT_PRIMARY_SURFACE_DEBUG_MATERIAL_MISMATCH = 4,
    RT_PRIMARY_SURFACE_DEBUG_NORMAL_MISMATCH = 5,
    RT_PRIMARY_SURFACE_DEBUG_ROUGHNESS_MISMATCH = 6,
    RT_PRIMARY_SURFACE_DEBUG_NO_OBJECT_MOTION = 7,
    RT_PRIMARY_SURFACE_DEBUG_SKINNED_MISSING_PREVIOUS = 8,
    RT_PRIMARY_SURFACE_DEBUG_SKINNED_RANGE_MISMATCH = 9,
    RT_PRIMARY_SURFACE_DEBUG_SKINNED_PREVIOUS_OUT_OF_RANGE = 10,
    RT_PRIMARY_SURFACE_DEBUG_RIGID_MISSING_PREVIOUS = 11,
    RT_PRIMARY_SURFACE_DEBUG_RIGID_RANGE_MISMATCH = 12
};

struct RtPathTracePrimarySurfaceRecord
{
    uint32_t header[4];                     // version, valid flags, debug/status flags, reserved
    float worldPositionAndViewDepth[4];     // xyz = world position, w = view depth
    float geometricNormalAndRoughness[4];   // xyz = geometric normal, w = roughness
    float shadingNormalAndOpacity[4];       // xyz = shading normal, w = opacity
    float viewDirectionAndReserved[4];      // xyz = view direction, w = reserved
    float albedoAndAlphaCutoff[4];          // xyz = diffuse albedo, w = alpha cutoff
    float specularF0AndReserved[4];         // xyz = F0/specular, w = reserved
    float emissiveAndHeight[4];             // xyz = emissive radiance, w = height/parallax/displacement placeholder
    float previousPositionOrMotion[4];      // xyz = previous world position or motion placeholder, w = valid selector
    uint32_t materialAndSurface[4];         // material id, material index, material flags, surface class
    uint32_t instancePrimitiveObject[4];    // instance id, primitive id, object/entity id placeholder, emissive texture index
};
static_assert(sizeof(RtPathTracePrimarySurfaceRecord) == RT_PATH_TRACE_PRIMARY_SURFACE_RECORD_STRIDE, "Primary surface CPU/shader record stride mismatch");

struct RtPathTracePrimarySurfaceHistoryState
{
    bool currentValid = false;
    bool previousValid = false;
    bool samePixelHistoryValid = false;
    bool cameraReprojectionAvailable = false;
    bool objectMotionAvailable = false;
    uint32_t resetReasonFlags = 0;

    void Reset(uint32_t reasons = 0)
    {
        currentValid = false;
        previousValid = false;
        samePixelHistoryValid = false;
        cameraReprojectionAvailable = false;
        objectMotionAvailable = false;
        resetReasonFlags = reasons;
    }
};
