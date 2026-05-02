#include "precompiled.h"
#pragma hdrstop

#include "PathTraceCVars.h"
#include "PathTraceSmokeDispatch.h"
#include "PathTracePrimaryPass.h"
#include "PathTraceAcceleration.h"
#include "PathTraceDebugDumps.h"
#include "PathTraceLightSelection.h"
#include "../RenderBackend.h"
#include "../RenderCommon.h"

extern idCVar r_forceAmbient;

namespace {

const int RT_SMOKE_MAX_EMISSIVE_TRIANGLE_RECORDS = 65536;
int g_smokeLastDispatchTimingLogMs = -1000000;

struct PathTraceSmokeConstants
{
    float cameraOriginAndTMax[4];
    float cameraForwardAndTanX[4];
    float cameraLeftAndTanY[4];
    float cameraUpAndDebugMode[4];
    float textureInfo[4];
    float lightOriginAndRadius[RT_SMOKE_MAX_DEBUG_LIGHTS][4];
    float lightColorAndIntensity[RT_SMOKE_MAX_DEBUG_LIGHTS][4];
    float lightInfo[4];
    float portalWindowInfo[4];
    float lightSpriteInfo[4];
    float toyPathInfo[4];
    float emissiveInfo[4];
};

uint64 HashSmokeDispatchValue(uint64 hash, uint64 value)
{
    hash ^= value + 0x9e3779b97f4a7c15ull + (hash << 6) + (hash >> 2);
    return hash;
}

}

size_t GetPathTraceSmokeConstantsSize()
{
    return sizeof(PathTraceSmokeConstants);
}
void PathTracePrimaryPass::ExecuteRayTracingSmokeTest(const viewDef_t* viewDef)
{
    const int executeStartMs = Sys_Milliseconds();
    if (!viewDef || !m_smokeSceneBuilt || !m_smokeShaderTable || !m_smokeBindingSet || !m_smokeTextureDescriptorTable || !m_smokeOutputTexture || !m_smokeAccumulationTexture || !m_smokeReadbackTexture || !m_smokeConstantsBuffer ||
        !m_smokeStaticVertexBuffer || !m_smokeStaticIndexBuffer || !m_smokeStaticTriangleClassBuffer || !m_smokeStaticTriangleMaterialBuffer || !m_smokeStaticTriangleMaterialIndexBuffer ||
        !m_smokeDynamicVertexBuffer || !m_smokeDynamicIndexBuffer || !m_smokeDynamicTriangleClassBuffer || !m_smokeDynamicTriangleMaterialBuffer || !m_smokeDynamicTriangleMaterialIndexBuffer || !m_smokeMaterialTableBuffer || !m_smokeEmissiveTriangleBuffer)
    {
        return;
    }

    nvrhi::ICommandList* commandList = m_backend ? m_backend->GL_GetCommandList() : nullptr;
    if (!commandList)
    {
        return;
    }

    nvrhi::rt::State state;
    state.shaderTable = m_smokeShaderTable;
    state.bindings = { m_smokeBindingSet, m_smokeTextureDescriptorTable };
    int debugMode = idMath::ClampInt(0, 19, r_pathTracingDebugMode.GetInteger());
    if ((debugMode == 8 || debugMode == 9 || debugMode == 10 || debugMode == 11 || debugMode == 12 || debugMode == 13 || debugMode == 14 || debugMode == 15 || debugMode == 18 || debugMode == 19) && r_pathTracingTextureTableLimit.GetInteger() <= 0)
    {
        debugMode = 7;
    }

    idVec3 cameraOrigin = viewDef->renderView.vieworg;
    idVec3 cameraForward = viewDef->renderView.viewaxis[0];
    idVec3 cameraLeft = viewDef->renderView.viewaxis[1];
    idVec3 cameraUp = viewDef->renderView.viewaxis[2];
    cameraForward.Normalize();
    cameraLeft.Normalize();
    cameraUp.Normalize();

    const int requestedLightCount = idMath::ClampInt(0, RT_SMOKE_MAX_DEBUG_LIGHTS, r_pathTracingLightCount.GetInteger());
    const int lightSelectionMode = idMath::ClampInt(0, 1, r_pathTracingLightSelection.GetInteger());

    uint64 accumulationSignature = 1469598103934665603ull;
    accumulationSignature = HashSmokeBytes(accumulationSignature, &debugMode, sizeof(debugMode));
    accumulationSignature = HashSmokeBytes(accumulationSignature, &m_smokeOutputWidth, sizeof(m_smokeOutputWidth));
    accumulationSignature = HashSmokeBytes(accumulationSignature, &m_smokeOutputHeight, sizeof(m_smokeOutputHeight));
    accumulationSignature = HashSmokeFloatQuantized(accumulationSignature, cameraOrigin.x, 100.0f);
    accumulationSignature = HashSmokeFloatQuantized(accumulationSignature, cameraOrigin.y, 100.0f);
    accumulationSignature = HashSmokeFloatQuantized(accumulationSignature, cameraOrigin.z, 100.0f);
    accumulationSignature = HashSmokeFloatQuantized(accumulationSignature, cameraForward.x, 10000.0f);
    accumulationSignature = HashSmokeFloatQuantized(accumulationSignature, cameraForward.y, 10000.0f);
    accumulationSignature = HashSmokeFloatQuantized(accumulationSignature, cameraForward.z, 10000.0f);
    accumulationSignature = HashSmokeFloatQuantized(accumulationSignature, cameraLeft.x, 10000.0f);
    accumulationSignature = HashSmokeFloatQuantized(accumulationSignature, cameraLeft.y, 10000.0f);
    accumulationSignature = HashSmokeFloatQuantized(accumulationSignature, cameraLeft.z, 10000.0f);
    accumulationSignature = HashSmokeFloatQuantized(accumulationSignature, cameraUp.x, 10000.0f);
    accumulationSignature = HashSmokeFloatQuantized(accumulationSignature, cameraUp.y, 10000.0f);
    accumulationSignature = HashSmokeFloatQuantized(accumulationSignature, cameraUp.z, 10000.0f);
    accumulationSignature = HashSmokeFloatQuantized(accumulationSignature, viewDef->renderView.fov_x, 100.0f);
    accumulationSignature = HashSmokeFloatQuantized(accumulationSignature, viewDef->renderView.fov_y, 100.0f);
    accumulationSignature = HashSmokeFloatQuantized(accumulationSignature, r_forceAmbient.GetFloat(), 1000.0f);
    accumulationSignature = HashSmokeFloatQuantized(accumulationSignature, r_pathTracingToyLightScale.GetFloat(), 1000.0f);
    accumulationSignature = HashSmokeFloatQuantized(accumulationSignature, r_pathTracingToyEmissiveScale.GetFloat(), 1000.0f);
    accumulationSignature = HashSmokeFloatQuantized(accumulationSignature, r_pathTracingToyMaxRayDistance.GetFloat(), 10.0f);
    accumulationSignature = HashSmokeDispatchValue(accumulationSignature, static_cast<uint64>(requestedLightCount));
    accumulationSignature = HashSmokeDispatchValue(accumulationSignature, static_cast<uint64>(lightSelectionMode));
    accumulationSignature = HashSmokeDispatchValue(accumulationSignature, static_cast<uint64>(r_pathTracingToyAccumulation.GetInteger() != 0 ? 1 : 0));
    if (debugMode != 18 || r_pathTracingToyAccumulation.GetInteger() == 0 || accumulationSignature != m_smokeAccumulationSignature)
    {
        m_smokeAccumulationSignature = accumulationSignature;
        m_smokeAccumulationFrameCount = 0;
    }
    const int accumulationMaxFrames = idMath::ClampInt(1, 4096, r_pathTracingToyAccumMaxFrames.GetInteger());
    const int accumulationFrameCount = debugMode == 18 && r_pathTracingToyAccumulation.GetInteger() != 0
        ? Min(m_smokeAccumulationFrameCount, accumulationMaxFrames - 1)
        : 0;

    PathTraceSmokeConstants constants = {};
    constants.cameraOriginAndTMax[0] = cameraOrigin.x;
    constants.cameraOriginAndTMax[1] = cameraOrigin.y;
    constants.cameraOriginAndTMax[2] = cameraOrigin.z;
    constants.cameraOriginAndTMax[3] = 100000.0f;
    constants.cameraForwardAndTanX[0] = cameraForward.x;
    constants.cameraForwardAndTanX[1] = cameraForward.y;
    constants.cameraForwardAndTanX[2] = cameraForward.z;
    constants.cameraForwardAndTanX[3] = idMath::Tan(DEG2RAD(viewDef->renderView.fov_x * 0.5f));
    constants.cameraLeftAndTanY[0] = cameraLeft.x;
    constants.cameraLeftAndTanY[1] = cameraLeft.y;
    constants.cameraLeftAndTanY[2] = cameraLeft.z;
    constants.cameraLeftAndTanY[3] = idMath::Tan(DEG2RAD(viewDef->renderView.fov_y * 0.5f));
    constants.cameraUpAndDebugMode[0] = cameraUp.x;
    constants.cameraUpAndDebugMode[1] = cameraUp.y;
    constants.cameraUpAndDebugMode[2] = cameraUp.z;
    constants.cameraUpAndDebugMode[3] = static_cast<float>(debugMode);
    constants.textureInfo[0] = static_cast<float>(Max(0, static_cast<int>(m_smokeActiveTextureTable.size()) - 1));
    const int textureSampleMethod = r_pathTracingTextureSampleEnable.GetInteger() != 0
        ? idMath::ClampInt(0, 2, r_pathTracingTextureSampleMethod.GetInteger())
        : 0;
    constants.textureInfo[1] = static_cast<float>(textureSampleMethod);
    constants.textureInfo[2] = static_cast<float>(Max(0, m_smokeMaterialTableEntryCount));
    const uint32_t textureFlags =
        (r_pathTracingTextureBindlessEnable.GetInteger() != 0 ? 1u : 0u) |
        (r_pathTracingTextureFilter.GetInteger() != 0 ? 2u : 0u) |
        (r_pathTracingTextureDecode.GetInteger() != 0 ? 4u : 0u) |
        (r_pathTracingUseNormalMaps.GetInteger() != 0 && (debugMode == 14 || debugMode == 18) ? 8u : 0u) |
        (r_pathTracingUseSpecularMaps.GetInteger() != 0 && debugMode == 14 ? 16u : 0u) |
        (r_pathTracingUseEmissiveMaps.GetInteger() != 0 && (debugMode == 14 || debugMode == 18 || debugMode == 19) ? 32u : 0u);
    constants.textureInfo[3] = static_cast<float>(textureFlags);
    RtSmokeSelectedLight selectedLights[RT_SMOKE_MAX_DEBUG_LIGHTS];
    const int selectedLightCount = (debugMode == 14 || debugMode == 15 || debugMode == 18)
        ? CollectSelectedSmokePointLights(viewDef, cameraOrigin, selectedLights, requestedLightCount, lightSelectionMode)
        : 0;
    constants.lightInfo[0] = static_cast<float>(selectedLightCount);
    constants.lightInfo[1] = static_cast<float>(lightSelectionMode);
    constants.lightInfo[2] = idMath::ClampFloat(0.0f, 1.0f, r_pathTracingSmokeParticleAlphaScale.GetFloat());
    constants.lightInfo[3] =
        (r_pathTracingSmokeParticleDither.GetInteger() != 0 ? 1.0f : 0.0f) +
        (r_pathTracingSmokeParticleEdgeFade.GetInteger() != 0 ? 2.0f : 0.0f);
    constants.portalWindowInfo[0] = r_pathTracingPortalWindowStochastic.GetInteger() != 0 ? 1.0f : 0.0f;
    constants.portalWindowInfo[1] = idMath::ClampFloat(0.0f, 1.0f, r_pathTracingPortalWindowAlphaScale.GetFloat());
    constants.portalWindowInfo[2] = idMath::ClampFloat(0.0f, 1.0f, r_pathTracingPortalWindowMinOpacity.GetFloat());
    constants.portalWindowInfo[3] = idMath::ClampFloat(0.0f, 1.0f, r_pathTracingPortalWindowShadowOpacity.GetFloat());
    constants.lightSpriteInfo[0] = r_pathTracingLightSpriteProxies.GetInteger() != 0 ? 1.0f : 0.0f;
    constants.lightSpriteInfo[1] = idMath::ClampFloat(0.001f, 0.25f, r_pathTracingLightSpriteRadiusScale.GetFloat());
    constants.lightSpriteInfo[2] = idMath::ClampFloat(0.0f, 16.0f, r_pathTracingLightSpriteIntensity.GetFloat());
    constants.lightSpriteInfo[3] = idMath::ClampFloat(0.0f, 1.0f, r_forceAmbient.GetFloat());
    constants.toyPathInfo[0] = idMath::ClampFloat(64.0f, 100000.0f, r_pathTracingToyMaxRayDistance.GetFloat());
    constants.toyPathInfo[1] = idMath::ClampFloat(0.0f, 16.0f, r_pathTracingToyLightScale.GetFloat());
    constants.toyPathInfo[2] = idMath::ClampFloat(0.0f, 32.0f, r_pathTracingToyEmissiveScale.GetFloat());
    constants.toyPathInfo[3] = static_cast<float>(accumulationFrameCount);
    constants.emissiveInfo[0] = static_cast<float>(m_smokeEmissiveTriangleCount);
    constants.emissiveInfo[1] = static_cast<float>(m_smokeEmissiveStaticTriangleCount);
    constants.emissiveInfo[2] = static_cast<float>(m_smokeEmissiveDynamicTriangleCount);
    constants.emissiveInfo[3] = static_cast<float>(RT_SMOKE_MAX_EMISSIVE_TRIANGLE_RECORDS);
    for (int i = 0; i < selectedLightCount; i++)
    {
        constants.lightOriginAndRadius[i][0] = selectedLights[i].origin.x;
        constants.lightOriginAndRadius[i][1] = selectedLights[i].origin.y;
        constants.lightOriginAndRadius[i][2] = selectedLights[i].origin.z;
        constants.lightOriginAndRadius[i][3] = selectedLights[i].radius;
        constants.lightColorAndIntensity[i][0] = selectedLights[i].color.x;
        constants.lightColorAndIntensity[i][1] = selectedLights[i].color.y;
        constants.lightColorAndIntensity[i][2] = selectedLights[i].color.z;
        constants.lightColorAndIntensity[i][3] = selectedLights[i].spriteProxy ? 1.0f : 0.0f;
    }
    if ((debugMode == 14 || debugMode == 15 || debugMode == 18) && r_pathTracingLightDump.GetInteger() != 0)
    {
        common->Printf("PathTracePrimaryPass: RT smoke selected %d debug point lights selection=%s\n",
            selectedLightCount,
            lightSelectionMode == 0 ? "nearest" : "cameraInfluence");
        for (int i = 0; i < selectedLightCount; i++)
        {
            common->Printf("  light[%d]: index=%d origin=(%.2f %.2f %.2f) radius=%.2f distance=%.2f score=%.6f color=(%.3f %.3f %.3f) intensity=%.3f sprite=%d shader='%s'\n",
                i,
                selectedLights[i].index,
                selectedLights[i].origin.x,
                selectedLights[i].origin.y,
                selectedLights[i].origin.z,
                selectedLights[i].radius,
                idMath::Sqrt(selectedLights[i].distanceSquared),
                selectedLights[i].score,
                selectedLights[i].color.x,
                selectedLights[i].color.y,
                selectedLights[i].color.z,
                selectedLights[i].color.w,
                selectedLights[i].spriteProxy ? 1 : 0,
                selectedLights[i].shaderName.c_str());
        }
        r_pathTracingLightDump.SetInteger(0);
    }

    commandList->writeBuffer(m_smokeConstantsBuffer, &constants, sizeof(constants));
    commandList->setBufferState(m_smokeStaticVertexBuffer, nvrhi::ResourceStates::ShaderResource);
    commandList->setBufferState(m_smokeStaticIndexBuffer, nvrhi::ResourceStates::ShaderResource);
    commandList->setBufferState(m_smokeStaticTriangleClassBuffer, nvrhi::ResourceStates::ShaderResource);
    commandList->setBufferState(m_smokeStaticTriangleMaterialBuffer, nvrhi::ResourceStates::ShaderResource);
    commandList->setBufferState(m_smokeStaticTriangleMaterialIndexBuffer, nvrhi::ResourceStates::ShaderResource);
    commandList->setBufferState(m_smokeDynamicVertexBuffer, nvrhi::ResourceStates::ShaderResource);
    commandList->setBufferState(m_smokeDynamicIndexBuffer, nvrhi::ResourceStates::ShaderResource);
    commandList->setBufferState(m_smokeDynamicTriangleClassBuffer, nvrhi::ResourceStates::ShaderResource);
    commandList->setBufferState(m_smokeDynamicTriangleMaterialBuffer, nvrhi::ResourceStates::ShaderResource);
    commandList->setBufferState(m_smokeDynamicTriangleMaterialIndexBuffer, nvrhi::ResourceStates::ShaderResource);
    commandList->setBufferState(m_smokeMaterialTableBuffer, nvrhi::ResourceStates::ShaderResource);
    commandList->setBufferState(m_smokeEmissiveTriangleBuffer, nvrhi::ResourceStates::ShaderResource);
    for (nvrhi::TextureHandle texture : m_smokeActiveTextureTable)
    {
        if (texture)
        {
            commandList->setTextureState(texture, nvrhi::AllSubresources, nvrhi::ResourceStates::ShaderResource);
        }
    }
    commandList->setTextureState(m_smokeOutputTexture, nvrhi::AllSubresources, nvrhi::ResourceStates::UnorderedAccess);
    commandList->setTextureState(m_smokeAccumulationTexture, nvrhi::AllSubresources, nvrhi::ResourceStates::UnorderedAccess);
    commandList->commitBarriers();
    commandList->clearTextureFloat(m_smokeOutputTexture, nvrhi::AllSubresources, nvrhi::Color(0.25f, 0.50f, 0.75f, 1.0f));
    if (accumulationFrameCount == 0)
    {
        commandList->clearTextureFloat(m_smokeAccumulationTexture, nvrhi::AllSubresources, nvrhi::Color(0.0f, 0.0f, 0.0f, 0.0f));
    }
    commandList->setRayTracingState(state);

    nvrhi::rt::DispatchRaysArguments args;
    args.width = m_smokeOutputWidth;
    args.height = m_smokeOutputHeight;
    args.depth = 1;
    commandList->dispatchRays(args);
    if (debugMode == 18 && r_pathTracingToyAccumulation.GetInteger() != 0)
    {
        m_smokeAccumulationFrameCount = Min(m_smokeAccumulationFrameCount + 1, accumulationMaxFrames);
    }
    else
    {
        m_smokeAccumulationFrameCount = 0;
    }
    const int dispatchSubmitMs = Sys_Milliseconds() - executeStartMs;
    if (ShouldLogSmokeTiming(dispatchSubmitMs, Sys_Milliseconds(), g_smokeLastDispatchTimingLogMs))
    {
        common->Printf("PathTracePrimaryPass: RT smoke slow dispatch submit %d ms output=%dx%d debugMode=%d lightCount=%d\n",
            dispatchSubmitMs,
            m_smokeOutputWidth,
            m_smokeOutputHeight,
            debugMode,
            requestedLightCount);
    }

    if (r_pathTracingReadbackEnable.GetInteger() != 0 && !m_smokeReadbackQueued && m_smokeReadbackCooldownFrames <= 0)
    {
        commandList->setTextureState(m_smokeOutputTexture, nvrhi::AllSubresources, nvrhi::ResourceStates::CopySource);
        commandList->commitBarriers();
        commandList->copyTexture(m_smokeReadbackTexture, nvrhi::TextureSlice(), m_smokeOutputTexture, nvrhi::TextureSlice());
        m_smokeReadbackQueued = true;
        m_smokeReadbackDelayFrames = 2;
        if (r_pathTracingSmokeLog.GetInteger() != 0)
        {
            common->Printf("PathTracePrimaryPass: queued RT smoke UAV readback\n");
        }
    }

    if (!m_smokeTestDispatched)
    {
        common->Printf("PathTracePrimaryPass: dispatched RT smoke camera raygen (%dx%d, debugMode=%d)\n", m_smokeOutputWidth, m_smokeOutputHeight, debugMode);
    }
    m_smokeTestDispatched = true;
}
