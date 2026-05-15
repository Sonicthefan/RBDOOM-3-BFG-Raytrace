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

uint64_t EstimateRg16FloatTextureBytes(int width, int height)
{
    if (width <= 0 || height <= 0)
    {
        return 0;
    }
    return static_cast<uint64_t>(width) * static_cast<uint64_t>(height) * 4ull;
}

uint64_t EstimateRgba16FloatTextureBytes(int width, int height)
{
    if (width <= 0 || height <= 0)
    {
        return 0;
    }
    return static_cast<uint64_t>(width) * static_cast<uint64_t>(height) * 8ull;
}

uint64_t EstimateR32FloatTextureBytes(int width, int height)
{
    if (width <= 0 || height <= 0)
    {
        return 0;
    }
    return static_cast<uint64_t>(width) * static_cast<uint64_t>(height) * 4ull;
}

uint64_t EstimateR32UintTextureBytes(int width, int height)
{
    if (width <= 0 || height <= 0)
    {
        return 0;
    }
    return static_cast<uint64_t>(width) * static_cast<uint64_t>(height) * 4ull;
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
    motionVectorTexturesCreated = 0;
    motionVectorMaskTexturesCreated = 0;
    rrGuideTexturesCreated = 0;
    outputTextureBytes = 0;
    smokeReservoirBytes = 0;
    restirPTReservoirBytes = 0;
    primarySurfaceHistoryBytes = 0;
    motionVectorBytes = 0;
    motionVectorMaskBytes = 0;
    rrGuideBytes = 0;
}

bool RtPathTraceFrameResources::IsValidFor(int requestedWidth, int requestedHeight, rtxdi::CheckerboardMode checkerboardMode) const
{
    return
        outputTexture &&
        accumulationTexture &&
        restirPTReflectionTexture &&
        rrInputColorTexture &&
        motionVectorTexture &&
        motionVectorMaskTexture &&
        rrGuideAlbedoTexture &&
        rrGuideSpecularAlbedoTexture &&
        rrGuideNormalRoughnessTexture &&
        rrGuideDepthTexture &&
        rrGuideHitDistanceTexture &&
        rrGuideResetMaskTexture &&
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
        restirPTReflectionTexture ||
        rrInputColorTexture ||
        motionVectorTexture ||
        motionVectorMaskTexture ||
        rrGuideAlbedoTexture ||
        rrGuideSpecularAlbedoTexture ||
        rrGuideNormalRoughnessTexture ||
        rrGuideDepthTexture ||
        rrGuideHitDistanceTexture ||
        rrGuideResetMaskTexture ||
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

    outputDesc.debugName = "PathTraceRestirPTReflection";
    nvrhi::TextureHandle newRestirPTReflectionTexture = device->createTexture(outputDesc);
    if (!newRestirPTReflectionTexture)
    {
        common->Printf("PathTraceFrameResources: failed to create PT ReSTIR reflection UAV (%dx%d)\n", requestedWidth, requestedHeight);
        return false;
    }

    outputDesc.debugName = "PathTraceRRInputColor";
    nvrhi::TextureHandle newRrInputColorTexture = device->createTexture(outputDesc);
    if (!newRrInputColorTexture)
    {
        common->Printf("PathTraceFrameResources: failed to create PT RR input-color UAV (%dx%d)\n", requestedWidth, requestedHeight);
        return false;
    }

    nvrhi::TextureDesc motionVectorDesc = outputDesc;
    motionVectorDesc.format = nvrhi::Format::RG16_FLOAT;
    motionVectorDesc.debugName = "PathTraceSmokeMotionVectors";
    nvrhi::TextureHandle newMotionVectorTexture = device->createTexture(motionVectorDesc);
    if (!newMotionVectorTexture)
    {
        common->Printf("PathTraceFrameResources: failed to create PT motion-vector UAV (%dx%d)\n", requestedWidth, requestedHeight);
        return false;
    }

    nvrhi::TextureDesc motionVectorMaskDesc = outputDesc;
    motionVectorMaskDesc.format = nvrhi::Format::R32_UINT;
    motionVectorMaskDesc.debugName = "PathTraceSmokeMotionVectorMask";
    nvrhi::TextureHandle newMotionVectorMaskTexture = device->createTexture(motionVectorMaskDesc);
    if (!newMotionVectorMaskTexture)
    {
        common->Printf("PathTraceFrameResources: failed to create PT motion-vector mask UAV (%dx%d)\n", requestedWidth, requestedHeight);
        return false;
    }

    nvrhi::TextureDesc rrGuideRgba16Desc = outputDesc;
    rrGuideRgba16Desc.format = nvrhi::Format::RGBA16_FLOAT;
    rrGuideRgba16Desc.debugName = "PathTraceRRGuideAlbedo";
    nvrhi::TextureHandle newRrGuideAlbedoTexture = device->createTexture(rrGuideRgba16Desc);
    if (!newRrGuideAlbedoTexture)
    {
        common->Printf("PathTraceFrameResources: failed to create PT RR albedo guide UAV (%dx%d)\n", requestedWidth, requestedHeight);
        return false;
    }

    rrGuideRgba16Desc.debugName = "PathTraceRRGuideNormalRoughness";
    nvrhi::TextureHandle newRrGuideNormalRoughnessTexture = device->createTexture(rrGuideRgba16Desc);
    if (!newRrGuideNormalRoughnessTexture)
    {
        common->Printf("PathTraceFrameResources: failed to create PT RR normal/roughness guide UAV (%dx%d)\n", requestedWidth, requestedHeight);
        return false;
    }

    rrGuideRgba16Desc.debugName = "PathTraceRRGuideSpecularAlbedo";
    nvrhi::TextureHandle newRrGuideSpecularAlbedoTexture = device->createTexture(rrGuideRgba16Desc);
    if (!newRrGuideSpecularAlbedoTexture)
    {
        common->Printf("PathTraceFrameResources: failed to create PT RR specular-albedo guide UAV (%dx%d)\n", requestedWidth, requestedHeight);
        return false;
    }

    nvrhi::TextureDesc rrGuideR32Desc = outputDesc;
    rrGuideR32Desc.format = nvrhi::Format::R32_FLOAT;
    rrGuideR32Desc.debugName = "PathTraceRRGuideDepth";
    nvrhi::TextureHandle newRrGuideDepthTexture = device->createTexture(rrGuideR32Desc);
    if (!newRrGuideDepthTexture)
    {
        common->Printf("PathTraceFrameResources: failed to create PT RR depth guide UAV (%dx%d)\n", requestedWidth, requestedHeight);
        return false;
    }

    rrGuideR32Desc.debugName = "PathTraceRRGuideHitDistance";
    nvrhi::TextureHandle newRrGuideHitDistanceTexture = device->createTexture(rrGuideR32Desc);
    if (!newRrGuideHitDistanceTexture)
    {
        common->Printf("PathTraceFrameResources: failed to create PT RR hit-distance guide UAV (%dx%d)\n", requestedWidth, requestedHeight);
        return false;
    }

    nvrhi::TextureDesc rrGuideResetMaskDesc = outputDesc;
    rrGuideResetMaskDesc.format = nvrhi::Format::R32_UINT;
    rrGuideResetMaskDesc.debugName = "PathTraceRRGuideResetMask";
    nvrhi::TextureHandle newRrGuideResetMaskTexture = device->createTexture(rrGuideResetMaskDesc);
    if (!newRrGuideResetMaskTexture)
    {
        common->Printf("PathTraceFrameResources: failed to create PT RR reset/disocclusion mask UAV (%dx%d)\n", requestedWidth, requestedHeight);
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
    restirPTReflectionTexture = newRestirPTReflectionTexture;
    rrInputColorTexture = newRrInputColorTexture;
    motionVectorTexture = newMotionVectorTexture;
    motionVectorMaskTexture = newMotionVectorMaskTexture;
    rrGuideAlbedoTexture = newRrGuideAlbedoTexture;
    rrGuideSpecularAlbedoTexture = newRrGuideSpecularAlbedoTexture;
    rrGuideNormalRoughnessTexture = newRrGuideNormalRoughnessTexture;
    rrGuideDepthTexture = newRrGuideDepthTexture;
    rrGuideHitDistanceTexture = newRrGuideHitDistanceTexture;
    rrGuideResetMaskTexture = newRrGuideResetMaskTexture;
    readbackTexture = newReadbackTexture;
    width = requestedWidth;
    height = requestedHeight;
    diagnostics.outputTexturesCreated += 4;
    diagnostics.motionVectorTexturesCreated++;
    diagnostics.motionVectorMaskTexturesCreated++;
    diagnostics.rrGuideTexturesCreated += 6;
    diagnostics.diagnosticReadbackResourcesCreated++;
    diagnostics.outputTextureBytes = EstimateRgba32FloatTextureBytes(width, height) * 4ull;
    diagnostics.motionVectorBytes = EstimateRg16FloatTextureBytes(width, height);
    diagnostics.motionVectorMaskBytes = EstimateR32UintTextureBytes(width, height);
    diagnostics.rrGuideBytes =
        EstimateRgba16FloatTextureBytes(width, height) * 3ull +
        EstimateR32FloatTextureBytes(width, height) * 2ull +
        EstimateR32UintTextureBytes(width, height);
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

    common->Printf("PathTraceFrameResources: RT motion-vector export scaffold output=%dx%d vectorFormat=RG16_FLOAT vectorBytes=%llu vectorUav=u39 maskFormat=R32_UINT maskBytes=%llu maskUav=u40 consumer=debug-or-dlssrr\n",
        requestedWidth,
        requestedHeight,
        static_cast<unsigned long long>(diagnostics.motionVectorBytes),
        static_cast<unsigned long long>(diagnostics.motionVectorMaskBytes));

    common->Printf("PathTraceFrameResources: RT DLSS RR guide scaffold output=%dx%d albedo=RGBA16_FLOAT/u48 normalRoughness=RGBA16_FLOAT/u49 depth=R32_FLOAT/u50 hitDistance=R32_FLOAT/u51 resetMask=R32_UINT/u52 specularAlbedo=RGBA16_FLOAT/u53 rrInputColor=RGBA32_FLOAT/u54 guideBytes=%llu producer=primary-surface-prepass\n",
        requestedWidth,
        requestedHeight,
        static_cast<unsigned long long>(diagnostics.rrGuideBytes));

    common->Printf("PathTraceFrameResources: RT smoke output UAV initialized (%dx%d) reflectionUav=u47 rrInputColorUav=u54\n", requestedWidth, requestedHeight);
    return true;
}

void RtPathTraceFrameResources::ResetOutputSizedResources(uint32_t reasonFlags)
{
    outputTexture = nullptr;
    accumulationTexture = nullptr;
    restirPTReflectionTexture = nullptr;
    rrInputColorTexture = nullptr;
    motionVectorTexture = nullptr;
    motionVectorMaskTexture = nullptr;
    rrGuideAlbedoTexture = nullptr;
    rrGuideSpecularAlbedoTexture = nullptr;
    rrGuideNormalRoughnessTexture = nullptr;
    rrGuideDepthTexture = nullptr;
    rrGuideHitDistanceTexture = nullptr;
    rrGuideResetMaskTexture = nullptr;
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
    restirPTReservoirNeedsClear = true;
    primarySurfaceHistoryNeedsClear = true;
    primarySurfaceHistoryState.Reset(reasonFlags);
    primarySurfaceHistoryView.Reset();
    smokeReservoirResetCount = 0;
    smokeReservoirClearCount = 0;
    restirPTReservoirClearCount = 0;
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
    restirPTReservoirNeedsClear = true;
    primarySurfaceHistoryNeedsClear = true;
    primarySurfaceHistoryState.Reset(RT_FRAME_RESET_SCENE_RESOURCES | RT_FRAME_RESET_PRIMARY_HISTORY);
    primarySurfaceHistoryView.Reset();
    smokeReservoirResetCount = 0;
    smokeReservoirClearCount = 0;
    restirPTReservoirClearCount = 0;
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

    common->Printf("%s: PT frame resources output=%dx%d debugMode=%d checkerboard=%d frame=%u resetReasons=%s valid output/accum/rrInput/motion/motionMask/rrGuides/readback=%d/%d/%d/%d/%d/%d/%d smokeReservoir=%d restirReservoir=%d primaryHistory=%d primaryState current/previous/samePixel/reproject/objectMotion=%d/%d/%d/%d/%d bytes output=%llu motion=%llu motionMask=%llu rrGuides=%llu smokeReservoir=%llu restirReservoir=%llu primaryHistory=%llu sceneUpload=%llu recreate output/motion/motionMask/rrGuides/readback=%d/%d/%d/%d/%d buffers smoke(reuse/recreate)=%d/%d restir(reuse/recreate)=%d/%d primaryHistory(reuse/recreate)=%d/%d descriptors=%d blasTlas=%d readback queued/mapped/unmapped=%d/%d/%d waitForIdle=%d reason=%s\n",
        prefix ? prefix : "PathTraceFrameResources",
        width,
        height,
        settings.debugMode,
        static_cast<int>(settings.checkerboardMode),
        settings.frameIndex,
        resetReasons.c_str(),
        outputTexture ? 1 : 0,
        accumulationTexture ? 1 : 0,
        rrInputColorTexture ? 1 : 0,
        motionVectorTexture ? 1 : 0,
        motionVectorMaskTexture ? 1 : 0,
        (rrGuideAlbedoTexture && rrGuideSpecularAlbedoTexture && rrGuideNormalRoughnessTexture && rrGuideDepthTexture && rrGuideHitDistanceTexture && rrGuideResetMaskTexture) ? 1 : 0,
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
        static_cast<unsigned long long>(diagnostics.motionVectorBytes),
        static_cast<unsigned long long>(diagnostics.motionVectorMaskBytes),
        static_cast<unsigned long long>(diagnostics.rrGuideBytes),
        static_cast<unsigned long long>(diagnostics.smokeReservoirBytes),
        static_cast<unsigned long long>(diagnostics.restirPTReservoirBytes),
        static_cast<unsigned long long>(diagnostics.primarySurfaceHistoryBytes),
        static_cast<unsigned long long>(diagnostics.sceneUploadBytes),
        diagnostics.outputTexturesCreated,
        diagnostics.motionVectorTexturesCreated,
        diagnostics.motionVectorMaskTexturesCreated,
        diagnostics.rrGuideTexturesCreated,
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
