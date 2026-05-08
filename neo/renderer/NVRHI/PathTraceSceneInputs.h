#pragma once

// CPU-side scene input contract for PT passes.
//
// This is deliberately not a shader ABI. It packages the current smoke-path
// resource handles, counts, signatures, and provenance so later PT/ReSTIR
// stages can consume a named input set instead of reaching back into scene
// capture or PathTracePrimaryPass private state.
//
// Source3 policy baseline:
// - source3 is the default path for the current RT/PT BVH.
// - source0 remains an emergency comparison/fallback path.
// - portal-area membership drives static preload, rigid residency, emissive
//   light residency, and Doom analytic light selection.
// - PS_BLOCK_VIEW portals are counted/reported by existing selection code but
//   do not hard-stop the current source3 default.
// - current-area plus short view-origin/view-axis probe seeds and four portal
//   steps is the validated greedy baseline for source3.
// - portal-area membership is preferred over screen-visible-only capture.
// - future line-of-sight portal supplementation should add visible boundary
//   areas only, without recursively walking outward from those LoS-added areas.

#include <nvrhi/nvrhi.h>

#include <cstdint>

enum RtPathTraceSceneInputCapabilityFlags : uint32_t
{
    RT_SCENE_INPUT_SOURCE3_BASELINE = 1u << 0,
    RT_SCENE_INPUT_SOURCE0_EMERGENCY_FALLBACK = 1u << 1,
    RT_SCENE_INPUT_PORTAL_AREA_RESIDENCY = 1u << 2,
    RT_SCENE_INPUT_PORTAL_BLOCK_VIEW_REPORTED = 1u << 3,
    RT_SCENE_INPUT_GEOMETRY_PREVIOUS_TRANSFORM_RESERVED = 1u << 4,
    RT_SCENE_INPUT_GEOMETRY_PREVIOUS_VERTEX_RESERVED = 1u << 5,
    RT_SCENE_INPUT_MATERIAL_STOPGAP_CLASSIFIER = 1u << 6,
    RT_SCENE_INPUT_MATERIAL_IDTECH4_SEMANTICS_RESERVED = 1u << 7,
    RT_SCENE_INPUT_MATERIAL_PBR_ROLES_RESERVED = 1u << 8,
    RT_SCENE_INPUT_LIGHT_PREVIOUS_IDENTITY_RESERVED = 1u << 9
};

struct RtPathTraceSceneInputSignatures
{
    uint64 geometryMembership = 0;
    uint64 materialTable = 0;
    uint64 lightMembership = 0;
    uint64 outputResolution = 0;
    uint64 cameraProjection = 0;
    uint64 debugFeaturePolicy = 0;
    uint64 cpuUploadGeneration = 0;
    uint64 reservoirScene = 0;
};

struct RtPathTraceSceneInputPortalPolicy
{
    int sceneSource = 0;
    int currentArea = -1;
    int totalAreas = 0;
    int staticAreaPreloadSteps = 0;
    int rigidResidencySteps = 0;
    int lightAreaSteps = 0;
    int sceneUniverseSteps = 0;
    int selectedAreaCount = 0;
    int portalEdges = 0;
    int blockedPortalEdges = 0;
    int rigidSelectedAreaCount = 0;
    int rigidPortalEdges = 0;
    int rigidBlockedPortalEdges = 0;
    bool defaultPolicyEquivalent = false;
};

struct RtPathTraceSceneInputGeometry
{
    nvrhi::rt::AccelStructHandle tlas;
    nvrhi::rt::AccelStructHandle staticBlas;
    nvrhi::rt::AccelStructHandle dynamicBlas;
    nvrhi::BufferHandle staticVertexBuffer;
    nvrhi::BufferHandle staticIndexBuffer;
    nvrhi::BufferHandle staticTriangleClassBuffer;
    nvrhi::BufferHandle staticTriangleMaterialBuffer;
    nvrhi::BufferHandle staticTriangleMaterialIndexBuffer;
    nvrhi::BufferHandle dynamicVertexBuffer;
    nvrhi::BufferHandle dynamicIndexBuffer;
    nvrhi::BufferHandle dynamicTriangleClassBuffer;
    nvrhi::BufferHandle dynamicTriangleMaterialBuffer;
    nvrhi::BufferHandle dynamicTriangleMaterialIndexBuffer;
    nvrhi::BufferHandle rigidRouteVertexBuffer;
    nvrhi::BufferHandle rigidRouteIndexBuffer;
    nvrhi::BufferHandle rigidRouteTriangleMaterialBuffer;
    nvrhi::BufferHandle rigidRouteTriangleMaterialIndexBuffer;
    nvrhi::BufferHandle rigidRouteInstanceBuffer;
    int staticVertexCount = 0;
    int staticIndexCount = 0;
    int staticTriangleCount = 0;
    int dynamicVertexCount = 0;
    int dynamicIndexCount = 0;
    int dynamicTriangleCount = 0;
    int rigidRouteVertexCount = 0;
    int rigidRouteIndexCount = 0;
    int rigidRouteTriangleCount = 0;
    int rigidRouteInstanceCount = 0;
    int skinnedSurfaceCount = 0;
    int skinnedTriangleCount = 0;
    bool currentGeometryValid = false;
    bool previousTransformAvailable = false;
    bool previousVertexDataAvailable = false;
    bool skinnedPreviousVertexDataAvailable = false;
    uint32_t capabilityFlags = 0;
};

struct RtPathTraceSceneInputMaterials
{
    nvrhi::BufferHandle materialTableBuffer;
    nvrhi::DescriptorTableHandle textureDescriptorTable;
    int materialTableEntryCount = 0;
    int activeTextureCount = 0;
    const char* materialTablePath = "unknown";
    uint32_t capabilityFlags = 0;
};

struct RtPathTraceSceneInputLights
{
    nvrhi::BufferHandle emissiveTriangleBuffer;
    nvrhi::BufferHandle lightCandidateBuffer;
    nvrhi::BufferHandle doomAnalyticLightBuffer;
    int emissiveTriangleCount = 0;
    int emissiveStaticTriangleCount = 0;
    int emissiveDynamicTriangleCount = 0;
    int lightCandidateCount = 0;
    int texturedLightCandidateCount = 0;
    int doomAnalyticLightCount = 0;
    uint64 lightUniverseGeneration = 0;
    uint32_t capabilityFlags = 0;
};

struct RtPathTraceSceneInputDiagnostics
{
    uint64_t geometryUploadBytes = 0;
    uint64_t materialUploadBytes = 0;
    uint64_t lightUploadBytes = 0;
    int sceneBuildMs = 0;
    int captureMs = 0;
    int materialMs = 0;
    int emissiveMs = 0;
    int bufferCreateMs = 0;
    int bufferUploadMs = 0;
    int accelSubmitMs = 0;
};

struct RtPathTraceSceneInputs
{
    bool valid = false;
    int sceneSource = 0;
    int debugMode = 0;
    int outputWidth = 0;
    int outputHeight = 0;
    uint32_t capabilityFlags = 0;
    RtPathTraceSceneInputSignatures signatures;
    RtPathTraceSceneInputPortalPolicy portalPolicy;
    RtPathTraceSceneInputGeometry geometry;
    RtPathTraceSceneInputMaterials materials;
    RtPathTraceSceneInputLights lights;
    RtPathTraceSceneInputDiagnostics diagnostics;
};
