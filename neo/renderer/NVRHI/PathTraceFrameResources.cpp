#include "precompiled.h"
#pragma hdrstop

#include "PathTraceFrameResources.h"

namespace {

uint64_t EstimateRgba32FloatTextureBytes(int width, int height)
{
    if (width <= 0 || height <= 0)
    {
        return 0;
    }
    return static_cast<uint64_t>(width) * static_cast<uint64_t>(height) * 16ull;
}

void AppendReason(idStr& out, const char* text)
{
    if (out.Length() > 0)
    {
        out.Append("|");
    }
    out.Append(text);
}

}

void RtPathTraceFrameCameraState::Reset()
{
    valid = false;
    width = 0;
    height = 0;
    origin = vec3_origin;
    forward = idVec3(1.0f, 0.0f, 0.0f);
    left = idVec3(0.0f, 1.0f, 0.0f);
    up = idVec3(0.0f, 0.0f, 1.0f);
    tanX = 1.0f;
    tanY = 1.0f;
}

void RtPathTraceFrameResourceDiagnostics::ResetResizeStats()
{
    waitForIdleCalls = 0;
    lastWaitForIdleReason = "";
    outputTexturesCreated = 0;
    diagnosticReadbackResourcesCreated = 0;
    smokeReservoirBuffersReused = 0;
    smokeReservoirBuffersRecreated = 0;
    restirPTReservoirBuffersReused = 0;
    restirPTReservoirBuffersRecreated = 0;
    primarySurfaceHistoryBuffersReused = 0;
    primarySurfaceHistoryBuffersRecreated = 0;
    outputTextureBytes = 0;
    smokeReservoirBytes = 0;
    restirPTReservoirBytes = 0;
    primarySurfaceHistoryBytes = 0;
}

bool RtPathTraceFrameResources::IsValidFor(int requestedWidth, int requestedHeight, rtxdi::CheckerboardMode checkerboardMode) const
{
    return
        outputTexture &&
        accumulationTexture &&
        readbackTexture &&
        smokeReservoirBuffers.IsValidFor(requestedWidth, requestedHeight) &&
        restirPTReservoirBuffers.IsValidFor(static_cast<uint32_t>(requestedWidth), static_cast<uint32_t>(requestedHeight), checkerboardMode) &&
        primarySurfaceHistoryBuffers.IsValidFor(static_cast<uint32_t>(requestedWidth), static_cast<uint32_t>(requestedHeight)) &&
        width == requestedWidth &&
        height == requestedHeight;
}

bool RtPathTraceFrameResources::HasAnyOutputSizedResource() const
{
    return
        outputTexture ||
        accumulationTexture ||
        readbackTexture ||
        smokeReservoirBuffers.current ||
        smokeReservoirBuffers.previous ||
        smokeReservoirBuffers.spatialScratch ||
        restirPTReservoirBuffers.reservoirs ||
        primarySurfaceHistoryBuffers.current ||
        primarySurfaceHistoryBuffers.previous;
}

bool RtPathTraceFrameResources::ResizeOutputSizedResources(nvrhi::IDevice* device, int requestedWidth, int requestedHeight, rtxdi::CheckerboardMode checkerboardMode)
{
    if (!device)
    {
        return false;
    }

    settings.width = requestedWidth;
    settings.height = requestedHeight;
    settings.checkerboardMode = checkerboardMode;
    settings.frameIndex = restirPTFrameIndex;
    settings.resetReasonFlags = RT_FRAME_RESET_NONE;
    diagnostics.ResetResizeStats();

    if (IsValidFor(requestedWidth, requestedHeight, checkerboardMode))
    {
        return true;
    }

    const bool replacingExistingOutput = HasAnyOutputSizedResource();
    if (replacingExistingOutput)
    {
        common->Printf("PathTraceFrameResources: resizing PT output old=%dx%d new=%dx%d; waitForIdle reason=output-sized-resource-replacement\n",
            width, height, requestedWidth, requestedHeight);
        device->waitForIdle();
        device->runGarbageCollection();
        diagnostics.waitForIdleCalls++;
        diagnostics.lastWaitForIdleReason = "output-sized-resource-replacement";
        MarkResetReason(RT_FRAME_RESET_GPU_IDLE_WAIT);
    }

    nvrhi::TextureDesc outputDesc;
    outputDesc.width = requestedWidth;
    outputDesc.height = requestedHeight;
    outputDesc.mipLevels = 1;
    outputDesc.arraySize = 1;
    outputDesc.format = nvrhi::Format::RGBA32_FLOAT;
    outputDesc.dimension = nvrhi::TextureDimension::Texture2D;
    outputDesc.isUAV = true;
    outputDesc.initialState = nvrhi::ResourceStates::UnorderedAccess;
    outputDesc.keepInitialState = true;
    outputDesc.debugName = "PathTraceSmokeOutput";
    nvrhi::TextureHandle newOutputTexture = device->createTexture(outputDesc);

    if (!newOutputTexture)
    {
        common->Printf("PathTraceFrameResources: failed to create PT output UAV (%dx%d)\n", requestedWidth, requestedHeight);
        return false;
    }

    outputDesc.debugName = "PathTraceSmokeAccumulation";
    nvrhi::TextureHandle newAccumulationTexture = device->createTexture(outputDesc);
    if (!newAccumulationTexture)
    {
        common->Printf("PathTraceFrameResources: failed to create PT accumulation UAV (%dx%d)\n", requestedWidth, requestedHeight);
        return false;
    }

    nvrhi::TextureDesc readbackDesc = outputDesc;
    readbackDesc.isShaderResource = false;
    readbackDesc.isUAV = false;
    readbackDesc.initialState = nvrhi::ResourceStates::Unknown;
    readbackDesc.keepInitialState = false;
    readbackDesc.debugName = "PathTraceSmokeReadback";
    nvrhi::StagingTextureHandle newReadbackTexture = device->createStagingTexture(readbackDesc, nvrhi::CpuAccessMode::Read);

    if (!newReadbackTexture)
    {
        common->Printf("PathTraceFrameResources: failed to create PT readback texture (%dx%d)\n", requestedWidth, requestedHeight);
        return false;
    }

    const bool smokeReservoirWasValid = smokeReservoirBuffers.IsValidFor(requestedWidth, requestedHeight);
    const bool restirReservoirWasValid = restirPTReservoirBuffers.IsValidFor(static_cast<uint32_t>(requestedWidth), static_cast<uint32_t>(requestedHeight), checkerboardMode);
    const bool primaryHistoryWasValid = primarySurfaceHistoryBuffers.IsValidFor(static_cast<uint32_t>(requestedWidth), static_cast<uint32_t>(requestedHeight));

    outputTexture = newOutputTexture;
    accumulationTexture = newAccumulationTexture;
    readbackTexture = newReadbackTexture;
    width = requestedWidth;
    height = requestedHeight;
    diagnostics.outputTexturesCreated += 2;
    diagnostics.diagnosticReadbackResourcesCreated++;
    diagnostics.outputTextureBytes = EstimateRgba32FloatTextureBytes(width, height) * 2ull;
    MarkResetReason(RT_FRAME_RESET_OUTPUT_RESIZE);

    RtSmokeReservoirBufferCreateDesc reservoirDesc;
    reservoirDesc.device = device;
    reservoirDesc.existingBuffers = smokeReservoirBuffers;
    reservoirDesc.width = requestedWidth;
    reservoirDesc.height = requestedHeight;
    const RtSmokeReservoirBufferCreateResult reservoirResult = CreateSmokeReservoirBuffers(reservoirDesc);
    if (!reservoirResult.Succeeded())
    {
        common->Printf("PathTraceFrameResources: %s (%dx%d)\n", reservoirResult.errorMessage ? reservoirResult.errorMessage : "failed to create RT smoke reservoir buffers", requestedWidth, requestedHeight);
        return false;
    }
    smokeReservoirBuffers = reservoirResult.buffers;
    diagnostics.smokeReservoirBytes = static_cast<uint64_t>(smokeReservoirBuffers.reservoirBytes);
    if (smokeReservoirWasValid)
    {
        diagnostics.smokeReservoirBuffersReused += 3;
    }
    else
    {
        diagnostics.smokeReservoirBuffersRecreated += 3;
    }

    RtRestirPTReservoirBufferCreateDesc restirPTReservoirDesc;
    restirPTReservoirDesc.device = device;
    restirPTReservoirDesc.existingBuffers = restirPTReservoirBuffers;
    restirPTReservoirDesc.width = static_cast<uint32_t>(requestedWidth);
    restirPTReservoirDesc.height = static_cast<uint32_t>(requestedHeight);
    restirPTReservoirDesc.checkerboardMode = checkerboardMode;
    const RtRestirPTReservoirBufferCreateResult restirPTReservoirResult = CreateRestirPTReservoirBuffers(restirPTReservoirDesc);
    if (!restirPTReservoirResult.Succeeded())
    {
        common->Printf("PathTraceFrameResources: %s (%dx%d)\n", restirPTReservoirResult.errorMessage ? restirPTReservoirResult.errorMessage : "failed to create RT ReSTIR PT packed reservoir buffer", requestedWidth, requestedHeight);
        return false;
    }
    restirPTReservoirBuffers = restirPTReservoirResult.buffers;
    diagnostics.restirPTReservoirBytes = restirPTReservoirBuffers.reservoirBytes;
    if (restirReservoirWasValid)
    {
        diagnostics.restirPTReservoirBuffersReused++;
    }
    else
    {
        diagnostics.restirPTReservoirBuffersRecreated++;
    }

    RtRestirPTPrimarySurfaceHistoryBufferCreateDesc primaryHistoryDesc;
    primaryHistoryDesc.device = device;
    primaryHistoryDesc.existingBuffers = primarySurfaceHistoryBuffers;
    primaryHistoryDesc.width = static_cast<uint32_t>(requestedWidth);
    primaryHistoryDesc.height = static_cast<uint32_t>(requestedHeight);
    const RtRestirPTPrimarySurfaceHistoryBufferCreateResult primaryHistoryResult = CreateRestirPTPrimarySurfaceHistoryBuffers(primaryHistoryDesc);
    if (!primaryHistoryResult.Succeeded())
    {
        common->Printf("PathTraceFrameResources: %s (%dx%d)\n", primaryHistoryResult.errorMessage ? primaryHistoryResult.errorMessage : "failed to create RT ReSTIR PT primary-surface history buffers", requestedWidth, requestedHeight);
        return false;
    }
    primarySurfaceHistoryBuffers = primaryHistoryResult.buffers;
    diagnostics.primarySurfaceHistoryBytes = primarySurfaceHistoryBuffers.surfaceBytes * 2ull;
    if (primaryHistoryWasValid)
    {
        diagnostics.primarySurfaceHistoryBuffersReused += 2;
    }
    else
    {
        diagnostics.primarySurfaceHistoryBuffersRecreated += 2;
    }

    InvalidatePrimarySurfaceHistory(RT_FRAME_RESET_PRIMARY_HISTORY);
    ResetReadbackQueue();
    smokeAccumulationSignature = 0;
    smokeAccumulationFrameCount = 0;

    RtRestirPTContextUpdateDesc restirPTContextDesc;
    restirPTContextDesc.width = static_cast<uint32_t>(requestedWidth);
    restirPTContextDesc.height = static_cast<uint32_t>(requestedHeight);
    restirPTContextDesc.frameIndex = restirPTFrameIndex;
    restirPTContextDesc.checkerboardMode = checkerboardMode;
    restirPTContextDesc.resamplingMode = rtxdi::ReSTIRPT_ResamplingMode::None;
    if (!UpdateRestirPTContextState(restirPTContextState, restirPTContextDesc))
    {
        common->Printf("PathTraceFrameResources: failed to initialize RT ReSTIR PT context (%dx%d)\n", requestedWidth, requestedHeight);
        return false;
    }

    common->Printf("PathTraceFrameResources: RT ReSTIR PT packed reservoirs output=%dx%d elements=%u bytes=%llu arrayPitch=%u blockRowPitch=%u slices=%u stride=%u\n",
        requestedWidth,
        requestedHeight,
        restirPTReservoirBuffers.reservoirElementCount,
        static_cast<unsigned long long>(restirPTReservoirBuffers.reservoirBytes),
        restirPTReservoirBuffers.reservoirParams.reservoirArrayPitch,
        restirPTReservoirBuffers.reservoirParams.reservoirBlockRowPitch,
        rtxdi::c_NumReSTIRPTReservoirBuffers,
        static_cast<uint32_t>(sizeof(RTXDI_PackedPTReservoir)));

    common->Printf("PathTraceFrameResources: RT ReSTIR PT primary-surface history output=%dx%d records=%u bytes=%llu stride=%u\n",
        requestedWidth,
        requestedHeight,
        primarySurfaceHistoryBuffers.surfaceCount,
        static_cast<unsigned long long>(primarySurfaceHistoryBuffers.surfaceBytes),
        RT_PATH_TRACE_PRIMARY_SURFACE_RECORD_STRIDE);

    common->Printf("PathTraceFrameResources: RT smoke output UAV initialized (%dx%d)\n", requestedWidth, requestedHeight);
    return true;
}

void RtPathTraceFrameResources::ResetOutputSizedResources(uint32_t reasonFlags)
{
    outputTexture = nullptr;
    accumulationTexture = nullptr;
    readbackTexture = nullptr;
    width = 0;
    height = 0;
    smokeReservoirBuffers.Reset();
    restirPTReservoirBuffers.Reset();
    primarySurfaceHistoryBuffers.Reset();
    restirPTContextState.Reset();
    smokeReservoirSceneSignature = 0;
    smokeReservoirDispatchSignature = 0;
    smokeReservoirNeedsClear = false;
    primarySurfaceHistoryNeedsClear = true;
    primarySurfaceHistoryState.Reset(reasonFlags);
    primarySurfaceHistoryView.Reset();
    smokeReservoirResetCount = 0;
    smokeReservoirClearCount = 0;
    smokeAccumulationSignature = 0;
    smokeAccumulationFrameCount = 0;
    ResetReadbackQueue();
    MarkResetReason(reasonFlags);
}

void RtPathTraceFrameResources::ResetSceneDependentState()
{
    smokeAccumulationSignature = 0;
    smokeAccumulationFrameCount = 0;
    smokeReservoirSceneSignature = 0;
    smokeReservoirDispatchSignature = 0;
    smokeReservoirNeedsClear = false;
    primarySurfaceHistoryNeedsClear = true;
    primarySurfaceHistoryState.Reset(RT_FRAME_RESET_SCENE_RESOURCES | RT_FRAME_RESET_PRIMARY_HISTORY);
    primarySurfaceHistoryView.Reset();
    smokeReservoirResetCount = 0;
    smokeReservoirClearCount = 0;
    ResetReadbackQueue();
    MarkResetReason(RT_FRAME_RESET_SCENE_RESOURCES | RT_FRAME_RESET_PRIMARY_HISTORY);
}

void RtPathTraceFrameResources::ResetReadbackQueue()
{
    readbackQueued = false;
    readbackDelayFrames = 0;
    readbackCooldownFrames = 0;
}

void RtPathTraceFrameResources::MarkResetReason(uint32_t reasonFlags)
{
    settings.resetReasonFlags |= reasonFlags;
}

void RtPathTraceFrameResources::ClearResetReasons()
{
    settings.resetReasonFlags = RT_FRAME_RESET_NONE;
}

void RtPathTraceFrameResources::SetPrimarySurfaceHistoryView(const RtPathTraceFrameCameraState& view, bool objectMotionAvailable)
{
    primarySurfaceHistoryView = view;
    primarySurfaceHistoryView.valid = true;
    primarySurfaceHistoryState.currentValid = true;
    primarySurfaceHistoryState.previousValid = true;
    primarySurfaceHistoryState.samePixelHistoryValid = true;
    primarySurfaceHistoryState.cameraReprojectionAvailable = true;
    primarySurfaceHistoryState.objectMotionAvailable = objectMotionAvailable;
}

void RtPathTraceFrameResources::InvalidatePrimarySurfaceHistory(uint32_t reasonFlags)
{
    primarySurfaceHistoryNeedsClear = true;
    primarySurfaceHistoryState.Reset(reasonFlags | RT_FRAME_RESET_PRIMARY_HISTORY);
    primarySurfaceHistoryView.Reset();
    MarkResetReason(reasonFlags | RT_FRAME_RESET_PRIMARY_HISTORY);
}

void RtPathTraceFrameResources::RecordSceneResourceCommit(uint64_t uploadBytes, bool rebuiltBindingSet, bool committedAccelerationStructures)
{
    diagnostics.sceneUploadBytes = uploadBytes;
    if (rebuiltBindingSet)
    {
        diagnostics.descriptorBindingSetRebuilds++;
    }
    if (committedAccelerationStructures)
    {
        diagnostics.blasTlasCommits++;
    }
}

void RtPathTraceFrameResources::RecordReadbackQueued()
{
    diagnostics.readbacksQueued++;
}

void RtPathTraceFrameResources::RecordReadbackMapped()
{
    diagnostics.readbacksMapped++;
}

void RtPathTraceFrameResources::RecordReadbackUnmapped()
{
    diagnostics.readbacksUnmapped++;
}

void RtPathTraceFrameResources::DescribeResetReasons(idStr& out) const
{
    out.Clear();
    const uint32_t reasons = settings.resetReasonFlags;
    if (reasons == RT_FRAME_RESET_NONE)
    {
        out = "none";
        return;
    }
    if ((reasons & RT_FRAME_RESET_OUTPUT_RESIZE) != 0)
    {
        AppendReason(out, "output-resize");
    }
    if ((reasons & RT_FRAME_RESET_BACKBUFFER_RESIZE) != 0)
    {
        AppendReason(out, "backbuffer-resize");
    }
    if ((reasons & RT_FRAME_RESET_SCENE_RESOURCES) != 0)
    {
        AppendReason(out, "scene-resources");
    }
    if ((reasons & RT_FRAME_RESET_RESERVOIR_SCENE_SIGNATURE) != 0)
    {
        AppendReason(out, "reservoir-scene-signature");
    }
    if ((reasons & RT_FRAME_RESET_RESERVOIR_DISPATCH_SIGNATURE) != 0)
    {
        AppendReason(out, "reservoir-dispatch-signature");
    }
    if ((reasons & RT_FRAME_RESET_PRIMARY_HISTORY) != 0)
    {
        AppendReason(out, "primary-history");
    }
    if ((reasons & RT_FRAME_RESET_GPU_IDLE_WAIT) != 0)
    {
        AppendReason(out, "gpu-idle-wait");
    }
}

void RtPathTraceFrameResources::PrintDiagnostics(const char* prefix) const
{
    idStr resetReasons;
    DescribeResetReasons(resetReasons);

    common->Printf("%s: PT frame resources output=%dx%d debugMode=%d checkerboard=%d frame=%u resetReasons=%s valid output/accum/readback=%d/%d/%d smokeReservoir=%d restirReservoir=%d primaryHistory=%d primaryState current/previous/samePixel/reproject/objectMotion=%d/%d/%d/%d/%d bytes output=%llu smokeReservoir=%llu restirReservoir=%llu primaryHistory=%llu sceneUpload=%llu recreate output/readback=%d/%d buffers smoke(reuse/recreate)=%d/%d restir(reuse/recreate)=%d/%d primaryHistory(reuse/recreate)=%d/%d descriptors=%d blasTlas=%d readback queued/mapped/unmapped=%d/%d/%d waitForIdle=%d reason=%s\n",
        prefix ? prefix : "PathTraceFrameResources",
        width,
        height,
        settings.debugMode,
        static_cast<int>(settings.checkerboardMode),
        settings.frameIndex,
        resetReasons.c_str(),
        outputTexture ? 1 : 0,
        accumulationTexture ? 1 : 0,
        readbackTexture ? 1 : 0,
        smokeReservoirBuffers.IsValidFor(width, height) ? 1 : 0,
        restirPTReservoirBuffers.IsValidFor(static_cast<uint32_t>(width), static_cast<uint32_t>(height), settings.checkerboardMode) ? 1 : 0,
        primarySurfaceHistoryBuffers.IsValidFor(static_cast<uint32_t>(width), static_cast<uint32_t>(height)) ? 1 : 0,
        primarySurfaceHistoryState.currentValid ? 1 : 0,
        primarySurfaceHistoryState.previousValid ? 1 : 0,
        primarySurfaceHistoryState.samePixelHistoryValid ? 1 : 0,
        primarySurfaceHistoryState.cameraReprojectionAvailable ? 1 : 0,
        primarySurfaceHistoryState.objectMotionAvailable ? 1 : 0,
        static_cast<unsigned long long>(diagnostics.outputTextureBytes),
        static_cast<unsigned long long>(diagnostics.smokeReservoirBytes),
        static_cast<unsigned long long>(diagnostics.restirPTReservoirBytes),
        static_cast<unsigned long long>(diagnostics.primarySurfaceHistoryBytes),
        static_cast<unsigned long long>(diagnostics.sceneUploadBytes),
        diagnostics.outputTexturesCreated,
        diagnostics.diagnosticReadbackResourcesCreated,
        diagnostics.smokeReservoirBuffersReused,
        diagnostics.smokeReservoirBuffersRecreated,
        diagnostics.restirPTReservoirBuffersReused,
        diagnostics.restirPTReservoirBuffersRecreated,
        diagnostics.primarySurfaceHistoryBuffersReused,
        diagnostics.primarySurfaceHistoryBuffersRecreated,
        diagnostics.descriptorBindingSetRebuilds,
        diagnostics.blasTlasCommits,
        diagnostics.readbacksQueued,
        diagnostics.readbacksMapped,
        diagnostics.readbacksUnmapped,
        diagnostics.waitForIdleCalls,
        diagnostics.lastWaitForIdleReason ? diagnostics.lastWaitForIdleReason : "");
}
