#pragma once

// RTX Remix-shaped RTXDI/ReSTIR resource ownership.
//
// This owns DI/GI reservoir GPU buffers independently from the old smoke
// reservoir reset path. It does not bind the buffers into shaders or dispatch
// RTXDI passes yet; those contracts are later file-equivalent slices.

#include "PathTraceFrameResources.h"
#include "PathTraceRemixFramePrepare.h"
#include "PathTraceRemixLightManager.h"

#include <nvrhi/nvrhi.h>

#include <cstdint>

#include <Rtxdi/DI/ReSTIRDI.h>
#include <Rtxdi/GI/ReSTIRGI.h>

enum PathTraceRemixRtxdiReservoirDomainKind : uint32_t
{
    PATH_TRACE_REMIX_RTXDI_RESERVOIR_DOMAIN_DI = 0u,
    PATH_TRACE_REMIX_RTXDI_RESERVOIR_DOMAIN_GI = 1u,
    PATH_TRACE_REMIX_RTXDI_RESERVOIR_DOMAIN_COUNT = 2u
};

enum PathTraceRemixRtxdiReservoirClearReason : uint32_t
{
    PATH_TRACE_REMIX_RTXDI_RESERVOIR_CLEAR_REASON_NONE = 0u,
    PATH_TRACE_REMIX_RTXDI_RESERVOIR_CLEAR_REASON_RESOURCE_INCOMPATIBLE = 1u,
    PATH_TRACE_REMIX_RTXDI_RESERVOIR_CLEAR_REASON_ALLOWED_RESET = 2u,
    PATH_TRACE_REMIX_RTXDI_RESERVOIR_CLEAR_REASON_PENDING = 3u
};

struct PathTraceRemixRtxdiReservoirDomain
{
    nvrhi::BufferHandle reservoirs;
    RTXDI_ReservoirBufferParameters reservoirParams = {};
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t reservoirArrayCount = 0;
    uint32_t reservoirElementCount = 0;
    uint32_t reservoirStructStride = 0;
    uint64_t reservoirBytes = 0;
    bool clearPending = true;
    uint32_t resetReasonFlags = RT_FRAME_RESET_NONE;

    bool IsValidFor(uint32_t requestedWidth, uint32_t requestedHeight, rtxdi::CheckerboardMode checkerboardMode, uint32_t arrayCount, uint32_t structStride) const;
    void Reset();
};

struct PathTraceRemixRtxdiResourcePrepareDesc
{
    nvrhi::IDevice* device = nullptr;
    PathTraceRemixFramePrepareObservationPackage framePackage;
    PathTraceRemixLightManagerStats lightManagerStats;
    rtxdi::CheckerboardMode checkerboardMode = rtxdi::CheckerboardMode::Off;
};

struct PathTraceRemixRtxdiResourceStats
{
    uint64_t frameIndex = 0;
    uint32_t outputWidth = 0;
    uint32_t outputHeight = 0;
    uint32_t checkerboardMode = 0;
    uint32_t resetReasonFlags = RT_FRAME_RESET_NONE;
    uint32_t allowedResetReasonFlags = RT_FRAME_RESET_NONE;
    uint32_t ignoredSmokeResetReasonFlags = RT_FRAME_RESET_NONE;
    uint32_t diRecreated = 0;
    uint32_t diReused = 0;
    uint32_t diClearPending = 0;
    uint32_t diClearReason = PATH_TRACE_REMIX_RTXDI_RESERVOIR_CLEAR_REASON_NONE;
    uint32_t giRecreated = 0;
    uint32_t giReused = 0;
    uint32_t giClearPending = 0;
    uint32_t giClearReason = PATH_TRACE_REMIX_RTXDI_RESERVOIR_CLEAR_REASON_NONE;
    uint32_t diArrayCount = 0;
    uint32_t giArrayCount = 0;
    uint32_t diStructStride = 0;
    uint32_t giStructStride = 0;
    uint32_t diElementCount = 0;
    uint32_t giElementCount = 0;
    uint64_t diReservoirBytes = 0;
    uint64_t giReservoirBytes = 0;
    uint64_t lightStructuralSignature = 0;
    uint64_t lightMappingSignature = 0;
    uint64_t lightPayloadSignature = 0;
    uint32_t lightStructuralSignatureChanged = 0;
    uint32_t lightMappingSignatureChanged = 0;
    uint32_t lightPayloadSignatureChanged = 0;
    uint32_t lightPayloadOnlyChange = 0;
    uint32_t oldSmokeReservoirSignatureConsulted = 0;
    uint32_t smokeDoomAnalyticLightCountConsulted = 0;
    uint32_t shaderRouteCount = 0;
    uint32_t bindingHandoffCount = 0;
};

class PathTraceRemixRtxdiResources
{
public:
    void Clear();
    bool PrepareOutputSizedResources(const PathTraceRemixRtxdiResourcePrepareDesc& desc);
    bool ClearPendingDomain(nvrhi::ICommandList* commandList, PathTraceRemixRtxdiReservoirDomainKind kind);

    const PathTraceRemixRtxdiReservoirDomain& GetDomain(PathTraceRemixRtxdiReservoirDomainKind kind) const;
    const PathTraceRemixRtxdiResourceStats& GetStats() const;

private:
    PathTraceRemixRtxdiReservoirDomain m_domains[PATH_TRACE_REMIX_RTXDI_RESERVOIR_DOMAIN_COUNT];
    PathTraceRemixRtxdiResourceStats m_stats;
};
