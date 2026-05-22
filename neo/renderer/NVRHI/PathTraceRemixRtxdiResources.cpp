#include "precompiled.h"
#pragma hdrstop

#include "PathTraceRemixRtxdiResources.h"

namespace {

uint32_t RemixRtxdiReservoirDimension(uint32_t value)
{
    return value > 0 ? value : 1u;
}

RTXDI_ReservoirBufferParameters RemixRtxdiReservoirParameters(uint32_t width, uint32_t height, rtxdi::CheckerboardMode checkerboardMode)
{
    return rtxdi::CalculateReservoirBufferParameters(
        RemixRtxdiReservoirDimension(width),
        RemixRtxdiReservoirDimension(height),
        checkerboardMode);
}

uint64_t RemixRtxdiReservoirElementCount64(uint32_t width, uint32_t height, rtxdi::CheckerboardMode checkerboardMode, uint32_t arrayCount)
{
    const RTXDI_ReservoirBufferParameters params = RemixRtxdiReservoirParameters(width, height, checkerboardMode);
    return static_cast<uint64_t>(params.reservoirArrayPitch) * static_cast<uint64_t>(arrayCount);
}

uint64_t RemixRtxdiReservoirByteSize(uint32_t width, uint32_t height, rtxdi::CheckerboardMode checkerboardMode, uint32_t arrayCount, uint32_t structStride)
{
    return RemixRtxdiReservoirElementCount64(width, height, checkerboardMode, arrayCount) * static_cast<uint64_t>(structStride);
}

bool RemixRtxdiReservoirBufferHasCapacity(
    nvrhi::BufferHandle buffer,
    uint32_t width,
    uint32_t height,
    rtxdi::CheckerboardMode checkerboardMode,
    uint32_t arrayCount,
    uint32_t structStride)
{
    return
        buffer &&
        buffer->getDesc().structStride == structStride &&
        buffer->getDesc().byteSize >= RemixRtxdiReservoirByteSize(width, height, checkerboardMode, arrayCount, structStride);
}

nvrhi::BufferHandle CreateRemixRtxdiReservoirBuffer(
    nvrhi::IDevice* device,
    const char* debugName,
    uint32_t width,
    uint32_t height,
    rtxdi::CheckerboardMode checkerboardMode,
    uint32_t arrayCount,
    uint32_t structStride)
{
    if (!device)
    {
        return nullptr;
    }

    nvrhi::BufferDesc desc;
    desc.debugName = debugName;
    desc.byteSize = RemixRtxdiReservoirByteSize(width, height, checkerboardMode, arrayCount, structStride);
    desc.structStride = structStride;
    desc.canHaveUAVs = true;
    desc.canHaveTypedViews = false;
    desc.initialState = nvrhi::ResourceStates::UnorderedAccess;
    desc.keepInitialState = true;
    return device->createBuffer(desc);
}

nvrhi::BufferHandle ReuseOrCreateRemixRtxdiReservoirBuffer(
    nvrhi::IDevice* device,
    nvrhi::BufferHandle existingBuffer,
    const char* debugName,
    uint32_t width,
    uint32_t height,
    rtxdi::CheckerboardMode checkerboardMode,
    uint32_t arrayCount,
    uint32_t structStride)
{
    if (RemixRtxdiReservoirBufferHasCapacity(existingBuffer, width, height, checkerboardMode, arrayCount, structStride))
    {
        return existingBuffer;
    }

    return CreateRemixRtxdiReservoirBuffer(device, debugName, width, height, checkerboardMode, arrayCount, structStride);
}

uint32_t RemixRtxdiAllowedResetReasons(uint32_t reasonFlags)
{
    return reasonFlags &
        (RT_FRAME_RESET_OUTPUT_RESIZE |
            RT_FRAME_RESET_BACKBUFFER_RESIZE |
            RT_FRAME_RESET_SCENE_RESOURCES |
            RT_FRAME_RESET_GPU_IDLE_WAIT);
}

uint32_t RemixRtxdiIgnoredSmokeResetReasons(uint32_t reasonFlags)
{
    return reasonFlags &
        (RT_FRAME_RESET_RESERVOIR_SCENE_SIGNATURE |
            RT_FRAME_RESET_RESERVOIR_DISPATCH_SIGNATURE);
}

bool PrepareDomain(
    PathTraceRemixRtxdiReservoirDomain& domain,
    PathTraceRemixRtxdiResourceStats& stats,
    nvrhi::IDevice* device,
    const char* debugName,
    uint32_t width,
    uint32_t height,
    rtxdi::CheckerboardMode checkerboardMode,
    uint32_t arrayCount,
    uint32_t structStride,
    uint32_t allowedResetFlags,
    bool isDiDomain)
{
    const bool wasValid = domain.IsValidFor(width, height, checkerboardMode, arrayCount, structStride);
    const bool hadPendingClear = domain.clearPending;
    const bool resetRequested = allowedResetFlags != RT_FRAME_RESET_NONE;
    const bool clearPending = hadPendingClear || !wasValid || resetRequested;
    const uint32_t clearReason = !wasValid
        ? PATH_TRACE_REMIX_RTXDI_RESERVOIR_CLEAR_REASON_RESOURCE_INCOMPATIBLE
        : (resetRequested
            ? PATH_TRACE_REMIX_RTXDI_RESERVOIR_CLEAR_REASON_ALLOWED_RESET
            : (hadPendingClear
                ? PATH_TRACE_REMIX_RTXDI_RESERVOIR_CLEAR_REASON_PENDING
                : PATH_TRACE_REMIX_RTXDI_RESERVOIR_CLEAR_REASON_NONE));

    domain.width = RemixRtxdiReservoirDimension(width);
    domain.height = RemixRtxdiReservoirDimension(height);
    domain.reservoirParams = RemixRtxdiReservoirParameters(width, height, checkerboardMode);
    domain.reservoirArrayCount = arrayCount;
    domain.reservoirStructStride = structStride;

    const uint64_t elementCount = RemixRtxdiReservoirElementCount64(width, height, checkerboardMode, arrayCount);
    if (elementCount > UINT32_MAX)
    {
        common->Printf("PathTraceRemixRtxdiResources: %s element count exceeds 32-bit metadata (%llu)\n",
            debugName ? debugName : "reservoir",
            static_cast<unsigned long long>(elementCount));
        domain.Reset();
        return false;
    }

    domain.reservoirElementCount = static_cast<uint32_t>(elementCount);
    domain.reservoirBytes = RemixRtxdiReservoirByteSize(width, height, checkerboardMode, arrayCount, structStride);
    domain.reservoirs = ReuseOrCreateRemixRtxdiReservoirBuffer(
        device,
        domain.reservoirs,
        debugName,
        width,
        height,
        checkerboardMode,
        arrayCount,
        structStride);
    domain.resetReasonFlags = allowedResetFlags;
    domain.clearPending = clearPending;

    if (!domain.IsValidFor(width, height, checkerboardMode, arrayCount, structStride))
    {
        common->Printf("PathTraceRemixRtxdiResources: failed to create %s output=%ux%u stride=%u arrays=%u bytes=%llu\n",
            debugName ? debugName : "reservoir",
            width,
            height,
            structStride,
            arrayCount,
            static_cast<unsigned long long>(domain.reservoirBytes));
        domain.Reset();
        return false;
    }

    if (isDiDomain)
    {
        stats.diReused = wasValid ? 1u : 0u;
        stats.diRecreated = wasValid ? 0u : 1u;
        stats.diClearPending = domain.clearPending ? 1u : 0u;
        stats.diClearReason = clearReason;
        stats.diArrayCount = domain.reservoirArrayCount;
        stats.diStructStride = domain.reservoirStructStride;
        stats.diElementCount = domain.reservoirElementCount;
        stats.diReservoirBytes = domain.reservoirBytes;
    }
    else
    {
        stats.giReused = wasValid ? 1u : 0u;
        stats.giRecreated = wasValid ? 0u : 1u;
        stats.giClearPending = domain.clearPending ? 1u : 0u;
        stats.giClearReason = clearReason;
        stats.giArrayCount = domain.reservoirArrayCount;
        stats.giStructStride = domain.reservoirStructStride;
        stats.giElementCount = domain.reservoirElementCount;
        stats.giReservoirBytes = domain.reservoirBytes;
    }

    return true;
}

}

bool PathTraceRemixRtxdiReservoirDomain::IsValidFor(
    uint32_t requestedWidth,
    uint32_t requestedHeight,
    rtxdi::CheckerboardMode checkerboardMode,
    uint32_t arrayCount,
    uint32_t structStride) const
{
    const uint32_t requiredWidth = RemixRtxdiReservoirDimension(requestedWidth);
    const uint32_t requiredHeight = RemixRtxdiReservoirDimension(requestedHeight);
    const RTXDI_ReservoirBufferParameters requiredParams = RemixRtxdiReservoirParameters(requestedWidth, requestedHeight, checkerboardMode);
    const uint64_t requiredElementCount = RemixRtxdiReservoirElementCount64(requestedWidth, requestedHeight, checkerboardMode, arrayCount);
    const uint64_t requiredBytes = RemixRtxdiReservoirByteSize(requestedWidth, requestedHeight, checkerboardMode, arrayCount, structStride);

    return
        reservoirs &&
        width == requiredWidth &&
        height == requiredHeight &&
        reservoirParams.reservoirBlockRowPitch == requiredParams.reservoirBlockRowPitch &&
        reservoirParams.reservoirArrayPitch == requiredParams.reservoirArrayPitch &&
        reservoirArrayCount == arrayCount &&
        reservoirElementCount >= requiredElementCount &&
        reservoirStructStride == structStride &&
        reservoirBytes >= requiredBytes &&
        reservoirs->getDesc().structStride == structStride &&
        reservoirs->getDesc().byteSize >= requiredBytes;
}

void PathTraceRemixRtxdiReservoirDomain::Reset()
{
    reservoirs = nullptr;
    reservoirParams = {};
    width = 0;
    height = 0;
    reservoirArrayCount = 0;
    reservoirElementCount = 0;
    reservoirStructStride = 0;
    reservoirBytes = 0;
    clearPending = true;
    resetReasonFlags = RT_FRAME_RESET_NONE;
}

void PathTraceRemixRtxdiResources::Clear()
{
    for (uint32_t i = 0; i < PATH_TRACE_REMIX_RTXDI_RESERVOIR_DOMAIN_COUNT; ++i)
    {
        m_domains[i].Reset();
    }
    m_stats = PathTraceRemixRtxdiResourceStats();
}

bool PathTraceRemixRtxdiResources::PrepareOutputSizedResources(const PathTraceRemixRtxdiResourcePrepareDesc& desc)
{
    m_stats = PathTraceRemixRtxdiResourceStats();
    m_stats.frameIndex = desc.framePackage.frameIndex;
    m_stats.outputWidth = static_cast<uint32_t>(desc.framePackage.outputWidth > 0 ? desc.framePackage.outputWidth : 0);
    m_stats.outputHeight = static_cast<uint32_t>(desc.framePackage.outputHeight > 0 ? desc.framePackage.outputHeight : 0);
    m_stats.checkerboardMode = static_cast<uint32_t>(desc.checkerboardMode);
    m_stats.resetReasonFlags = desc.framePackage.resetReasonFlags;
    m_stats.allowedResetReasonFlags = RemixRtxdiAllowedResetReasons(desc.framePackage.resetReasonFlags);
    m_stats.ignoredSmokeResetReasonFlags = RemixRtxdiIgnoredSmokeResetReasons(desc.framePackage.resetReasonFlags);
    m_stats.lightStructuralSignature = desc.lightManagerStats.structuralSignature;
    m_stats.lightMappingSignature = desc.lightManagerStats.mappingSignature;
    m_stats.lightPayloadSignature = desc.lightManagerStats.payloadSignature;
    m_stats.lightStructuralSignatureChanged = desc.lightManagerStats.structuralSignatureChanged;
    m_stats.lightMappingSignatureChanged = desc.lightManagerStats.mappingSignatureChanged;
    m_stats.lightPayloadSignatureChanged = desc.lightManagerStats.payloadSignatureChanged;
    m_stats.lightPayloadOnlyChange = desc.lightManagerStats.payloadOnlyChange;

    if (!desc.device)
    {
        return false;
    }

    const bool diReady = PrepareDomain(
        m_domains[PATH_TRACE_REMIX_RTXDI_RESERVOIR_DOMAIN_DI],
        m_stats,
        desc.device,
        "PathTraceRemixRtxdiDIReservoirs",
        m_stats.outputWidth,
        m_stats.outputHeight,
        desc.checkerboardMode,
        rtxdi::c_NumReSTIRDIReservoirBuffers,
        static_cast<uint32_t>(sizeof(RTXDI_PackedDIReservoir)),
        m_stats.allowedResetReasonFlags,
        true);

    const bool giReady = PrepareDomain(
        m_domains[PATH_TRACE_REMIX_RTXDI_RESERVOIR_DOMAIN_GI],
        m_stats,
        desc.device,
        "PathTraceRemixRtxdiGIReservoirs",
        m_stats.outputWidth,
        m_stats.outputHeight,
        desc.checkerboardMode,
        rtxdi::c_NumReSTIRGIReservoirBuffers,
        static_cast<uint32_t>(sizeof(RTXDI_PackedGIReservoir)),
        m_stats.allowedResetReasonFlags,
        false);

    return diReady && giReady;
}

bool PathTraceRemixRtxdiResources::ClearPendingDomain(nvrhi::ICommandList* commandList, PathTraceRemixRtxdiReservoirDomainKind kind)
{
    const uint32_t index = static_cast<uint32_t>(kind);
    if (!commandList || index >= PATH_TRACE_REMIX_RTXDI_RESERVOIR_DOMAIN_COUNT)
    {
        return false;
    }

    PathTraceRemixRtxdiReservoirDomain& domain = m_domains[index];
    if (!domain.reservoirs || !domain.clearPending)
    {
        return false;
    }

    commandList->clearBufferUInt(domain.reservoirs, 0);
    domain.clearPending = false;
    domain.resetReasonFlags = RT_FRAME_RESET_NONE;
    if (kind == PATH_TRACE_REMIX_RTXDI_RESERVOIR_DOMAIN_DI)
    {
        m_stats.diClearPending = 0u;
        m_stats.diClearReason = PATH_TRACE_REMIX_RTXDI_RESERVOIR_CLEAR_REASON_NONE;
    }
    else if (kind == PATH_TRACE_REMIX_RTXDI_RESERVOIR_DOMAIN_GI)
    {
        m_stats.giClearPending = 0u;
        m_stats.giClearReason = PATH_TRACE_REMIX_RTXDI_RESERVOIR_CLEAR_REASON_NONE;
    }
    return true;
}

const PathTraceRemixRtxdiReservoirDomain& PathTraceRemixRtxdiResources::GetDomain(PathTraceRemixRtxdiReservoirDomainKind kind) const
{
    const uint32_t index = static_cast<uint32_t>(kind);
    return m_domains[index < PATH_TRACE_REMIX_RTXDI_RESERVOIR_DOMAIN_COUNT ? index : 0u];
}

const PathTraceRemixRtxdiResourceStats& PathTraceRemixRtxdiResources::GetStats() const
{
    return m_stats;
}
