#include "precompiled.h"
#pragma hdrstop

#include "PathTraceDebugDumps.h"
#include "PathTraceDoomMaterialClassifier.h"
#include "PathTraceAcceleration.h"
#include "PathTraceDynamicMaterialState.h"
#include "PathTraceEmissiveCandidates.h"
#include "PathTraceGuiSurfaces.h"
#include "PathTraceMaterialTextureDiscovery.h"
#include "PathTracePrimaryPass.h"
#include "PathTraceSceneCapture.h"
#include "PathTraceSkinning.h"
#include "PathTraceSmokeDispatch.h"
#include "PathTraceSmokeResources.h"
#include "PathTraceSurfaceClassification.h"
#include "PathTraceSurfaceDebugDumps.h"
#include "PathTraceTextureRegistry.h"
#include "../RenderCommon.h"
#include "../RenderBackend.h"
#include "../GLMatrix.h"
#include "../Image.h"
#include "../Model_local.h"
#include "../Passes/CommonPasses.h"
#include "../../framework/Common_local.h"
#include "../../sys/DeviceManager.h"

#include <nvrhi/utils.h>
#include <algorithm>
#include <vector>

extern DeviceManager* deviceManager;

idCVar r_pathTracingDebugMode(
    "r_pathTracingDebugMode",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "RT smoke debug output mode: 0 = hit/miss, 1 = depth, 2 = interpolated normal, 3 = surface class, 4 = UV, 5 = geometric normal, 6 = material ID, 7 = material table, 8 = sampled diffuse texture, 9 = alpha test preview, 10 = albedo, 11 = translucent overlay inspection, 12 = translucent subtype, 13 = fixed Lambert lighting, 14 = selected point-light shadows, 15 = selected light influence, 16 = normal map, 17 = specular map, 18 = toy one-bounce path trace, 19 = emissive triangle inventory" );

idCVar r_pathTracingClassDump(
    "r_pathTracingClassDump",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "Set to 1 to dump sampled RT smoke surface classification reasons once" );

idCVar r_pathTracingClassSummary(
    "r_pathTracingClassSummary",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "Set to 1 to include RT smoke class counts in the throttled scene-capture summary" );

idCVar r_pathTracingDebugWidth(
    "r_pathTracingDebugWidth",
    "320",
    CVAR_RENDERER | CVAR_INTEGER | CVAR_ARCHIVE,
    "RT smoke debug output width" );

idCVar r_pathTracingDebugHeight(
    "r_pathTracingDebugHeight",
    "180",
    CVAR_RENDERER | CVAR_INTEGER | CVAR_ARCHIVE,
    "RT smoke debug output height" );

idCVar r_pathTracingTextureProbeIndex(
    "r_pathTracingTextureProbeIndex",
    "-1",
    CVAR_RENDERER | CVAR_INTEGER,
    "RT smoke debug mode 8 material table index to focus in texture probe logging; -1 = first safe texture" );

idCVar r_pathTracingTextureProbeReset(
    "r_pathTracingTextureProbeReset",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "Set to 1 to release the latched RT smoke texture probe and select a new one" );

idCVar r_pathTracingTextureProbeDump(
    "r_pathTracingTextureProbeDump",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "Set to 1 to dump the current RT smoke texture probe and sampled candidate list once" );

idCVar r_pathTracingAlphaDump(
    "r_pathTracingAlphaDump",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "Set to 1 to dump the current RT smoke alpha-test material table once" );

idCVar r_pathTracingTextureFallbackDump(
    "r_pathTracingTextureFallbackDump",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "Set to 1 to dump current RT smoke albedo fallback materials once" );

idCVar r_pathTracingTranslucentDump(
    "r_pathTracingTranslucentDump",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "Set to 1 to dump current RT smoke translucent subtype classifier samples once" );

idCVar r_pathTracingCrosshairMaterialDump(
    "r_pathTracingCrosshairMaterialDump",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "Set to 1 to dump detailed RT smoke material/stage info for the surface under the center crosshair once" );

idCVar r_pathTracingGuiDump(
    "r_pathTracingGuiDump",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "Set to 1 to dump captured RT smoke in-world GUI draw surfaces once" );

idCVar r_pathTracingEmissiveInventoryDump(
    "r_pathTracingEmissiveInventoryDump",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "Set to 1 to dump current RT smoke emissive triangle inventory once" );

idCVar r_pathTracingEmissiveInventoryMaxTriangles(
    "r_pathTracingEmissiveInventoryMaxTriangles",
    "4096",
    CVAR_RENDERER | CVAR_INTEGER,
    "Maximum emissive triangles captured into the RT smoke inventory buffer" );

idCVar r_pathTracingLightDump(
    "r_pathTracingLightDump",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "Set to 1 to dump the current RT smoke selected debug light once" );

idCVar r_pathTracingLightCount(
    "r_pathTracingLightCount",
    "4",
    CVAR_RENDERER | CVAR_INTEGER,
    "Maximum selected visible point lights for RT smoke debug modes 14/15; 0 = fixed directional fallback, max 32" );

idCVar r_pathTracingLightSelection(
    "r_pathTracingLightSelection",
    "1",
    CVAR_RENDERER | CVAR_INTEGER,
    "RT smoke point-light selection: 0 = nearest camera lights, 1 = strongest estimated camera influence" );

idCVar r_pathTracingLightSpriteProxies(
    "r_pathTracingLightSpriteProxies",
    "1",
    CVAR_RENDERER | CVAR_INTEGER,
    "Draw debug-only emissive sprite proxies for selected RT smoke lights in mode 14" );

idCVar r_pathTracingLightSpriteRadiusScale(
    "r_pathTracingLightSpriteRadiusScale",
    "0.04",
    CVAR_RENDERER | CVAR_FLOAT,
    "World-space radius scale for RT smoke selected-light sprite proxies" );

idCVar r_pathTracingLightSpriteIntensity(
    "r_pathTracingLightSpriteIntensity",
    "2.5",
    CVAR_RENDERER | CVAR_FLOAT,
    "Intensity multiplier for RT smoke selected-light sprite proxies" );

idCVar r_pathTracingTextureProbeDumpStart(
    "r_pathTracingTextureProbeDumpStart",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "First safe RT smoke texture candidate to print in the one-shot probe dump" );

idCVar r_pathTracingTextureProbeDumpCount(
    "r_pathTracingTextureProbeDumpCount",
    "12",
    CVAR_RENDERER | CVAR_INTEGER,
    "Number of safe RT smoke texture candidates to print in the one-shot probe dump" );

idCVar r_pathTracingTextureTableLimit(
    "r_pathTracingTextureTableLimit",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "Maximum safe captured material textures to bind for RT smoke texture debug modes; 0 = discovery/logging only, max 2048" );

idCVar r_pathTracingTextureTableStart(
    "r_pathTracingTextureTableStart",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "First safe captured diffuse texture candidate to bind for RT smoke debug mode 8" );

idCVar r_pathTracingTextureForceFallback(
    "r_pathTracingTextureForceFallback",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "RT smoke debug mode 8: bind fallback white texture into active bindless slots instead of captured diffuse textures" );

idCVar r_pathTracingTextureSampleEnable(
    "r_pathTracingTextureSampleEnable",
    "1",
    CVAR_RENDERER | CVAR_INTEGER,
    "RT smoke debug mode 8: enable actual bindless texture sampling; 0 keeps the descriptor table active but shades from material fallback colors" );

idCVar r_pathTracingTextureBindlessEnable(
    "r_pathTracingTextureBindlessEnable",
    "1",
    CVAR_RENDERER | CVAR_INTEGER,
    "RT smoke debug mode 8: sample from the bindless texture table; 0 samples a regular fallback texture SRV for diagnostics" );

idCVar r_pathTracingTextureSampleMethod(
    "r_pathTracingTextureSampleMethod",
    "2",
    CVAR_RENDERER | CVAR_INTEGER,
    "RT smoke debug mode 8 texture fetch method: 0 = disabled/fallback color, 1 = SampleLevel diagnostic, 2 = Texture.Load stable default" );

idCVar r_pathTracingTextureFilter(
    "r_pathTracingTextureFilter",
    "1",
    CVAR_RENDERER | CVAR_INTEGER,
    "RT smoke Texture.Load filtering: 0 = point, 1 = manual bilinear" );

idCVar r_pathTracingTextureDecode(
    "r_pathTracingTextureDecode",
    "1",
    CVAR_RENDERER | CVAR_INTEGER,
    "Decode RT smoke material texture encodings such as Doom diffuse YCoCg" );

idCVar r_pathTracingForceTextureCodeUse(
    "r_pathTracingForceTextureCodeUse",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "Use four-digit Doom texture filename codes as RT smoke material image discovery hints" );

idCVar r_pathTracingUseNormalMaps(
    "r_pathTracingUseNormalMaps",
    "1",
    CVAR_RENDERER | CVAR_INTEGER,
    "Use sampled normal maps for RT smoke debug mode 14 direct lighting" );

idCVar r_pathTracingUseSpecularMaps(
    "r_pathTracingUseSpecularMaps",
    "1",
    CVAR_RENDERER | CVAR_INTEGER,
    "Use sampled specular maps for RT smoke debug mode 14 direct lighting" );

idCVar r_pathTracingUseEmissiveMaps(
    "r_pathTracingUseEmissiveMaps",
    "1",
    CVAR_RENDERER | CVAR_INTEGER,
    "Use sampled emissive/glow material stages for RT smoke debug mode 14 direct lighting" );

idCVar r_pathTracingToyMaxRayDistance(
    "r_pathTracingToyMaxRayDistance",
    "1024",
    CVAR_RENDERER | CVAR_FLOAT,
    "Maximum mode 18 toy path-tracing secondary/direct ray distance; reduces leaks through incomplete visible-surface TLAS geometry" );

idCVar r_pathTracingToyLightScale(
    "r_pathTracingToyLightScale",
    "0.3",
    CVAR_RENDERER | CVAR_FLOAT,
    "Scale selected point-light contribution in mode 18 toy path tracing" );

idCVar r_pathTracingToyEmissiveScale(
    "r_pathTracingToyEmissiveScale",
    "4.0",
    CVAR_RENDERER | CVAR_FLOAT,
    "Scale emissive material contribution in mode 18 toy path tracing" );

idCVar r_pathTracingToyAccumulation(
    "r_pathTracingToyAccumulation",
    "1",
    CVAR_RENDERER | CVAR_INTEGER,
    "Accumulate mode 18 toy path tracing across stable camera frames" );

idCVar r_pathTracingToyAccumMaxFrames(
    "r_pathTracingToyAccumMaxFrames",
    "64",
    CVAR_RENDERER | CVAR_INTEGER,
    "Maximum accumulated frames for mode 18 toy path tracing" );

idCVar r_pathTracingSmokeParticleDither(
    "r_pathTracingSmokeParticleDither",
    "1",
    CVAR_RENDERER | CVAR_INTEGER,
    "Use stable alpha dithering for RT smoke particle cards and ignore them for debug shadow rays" );

idCVar r_pathTracingSmokeParticleAlphaScale(
    "r_pathTracingSmokeParticleAlphaScale",
    "0.25",
    CVAR_RENDERER | CVAR_FLOAT,
    "Opacity scale for RT smoke particle-card alpha dithering" );

idCVar r_pathTracingSmokeParticleEdgeFade(
    "r_pathTracingSmokeParticleEdgeFade",
    "1",
    CVAR_RENDERER | CVAR_INTEGER,
    "Fade RT smoke particle-card dither opacity near card UV edges" );

idCVar r_pathTracingPortalWindowStochastic(
    "r_pathTracingPortalWindowStochastic",
    "1",
    CVAR_RENDERER | CVAR_INTEGER,
    "Use stable stochastic transparency for RT smoke portal/window surfaces" );

idCVar r_pathTracingPortalWindowAlphaScale(
    "r_pathTracingPortalWindowAlphaScale",
    "0.35",
    CVAR_RENDERER | CVAR_FLOAT,
    "Opacity scale for RT smoke portal/window stochastic transparency" );

idCVar r_pathTracingPortalWindowMinOpacity(
    "r_pathTracingPortalWindowMinOpacity",
    "0.05",
    CVAR_RENDERER | CVAR_FLOAT,
    "Minimum primary-ray opacity for RT smoke portal/window stochastic transparency" );

idCVar r_pathTracingPortalWindowShadowOpacity(
    "r_pathTracingPortalWindowShadowOpacity",
    "0.05",
    CVAR_RENDERER | CVAR_FLOAT,
    "Shadow-ray opacity multiplier for RT smoke portal/window stochastic transparency" );

idCVar r_pathTracingAdditiveDecalKey(
    "r_pathTracingAdditiveDecalKey",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "Treat additive translucent decal/signage materials as RGB-keyed RT overlays" );

idCVar r_pathTracingAllowGuiTextures(
    "r_pathTracingAllowGuiTextures",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "Allow strictly validated GUI/SWF-like material textures into RT smoke bindless texture diagnostics" );

idCVar r_pathTracingAllowGuiSurfaces(
    "r_pathTracingAllowGuiSurfaces",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "Allow GUI/SWF draw-surface geometry cards into RT smoke capture diagnostics" );

idCVar r_pathTracingSkipCallbackEntities(
    "r_pathTracingSkipCallbackEntities",
    "1",
    CVAR_RENDERER | CVAR_INTEGER,
    "Skip custom-shader deferred callback render entities in RT smoke capture to avoid item/pickup lifetime hazards" );

idCVar r_pathTracingMaterialMetadataCache(
    "r_pathTracingMaterialMetadataCache",
    "1",
    CVAR_RENDERER | CVAR_INTEGER,
    "Cache RT smoke per-material texture metadata; frame-local material tables are still rebuilt" );

idCVar r_pathTracingSmokeLog(
    "r_pathTracingSmokeLog",
    "1",
    CVAR_RENDERER | CVAR_INTEGER,
    "Enable periodic RT smoke debug logging" );

idCVar r_pathTracingTimingLog(
    "r_pathTracingTimingLog",
    "1",
    CVAR_RENDERER | CVAR_INTEGER,
    "Enable RT smoke slow-frame CPU timing logs" );

idCVar r_pathTracingTimingThreshold(
    "r_pathTracingTimingThreshold",
    "40",
    CVAR_RENDERER | CVAR_INTEGER,
    "RT smoke CPU timing log threshold in milliseconds" );

idCVar r_pathTracingTimingLogInterval(
    "r_pathTracingTimingLogInterval",
    "1000",
    CVAR_RENDERER | CVAR_INTEGER,
    "Minimum milliseconds between repeated RT smoke timing log lines; 0 logs every threshold hit" );

idCVar r_pathTracingReadbackEnable(
    "r_pathTracingReadbackEnable",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "Enable RT smoke UAV readback diagnostics; can stall the GPU/CPU while profiling" );

idCVar r_pathTracingMaterialCache(
    "r_pathTracingMaterialCache",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "Experimental RT smoke material-table cache; currently ignored while material-index stability is validated" );

namespace {

const int RT_SMOKE_MIN_OUTPUT_WIDTH = 16;
const int RT_SMOKE_MIN_OUTPUT_HEIGHT = 16;
const int RT_SMOKE_MAX_OUTPUT_WIDTH = 3840;
const int RT_SMOKE_MAX_OUTPUT_HEIGHT = 2160;
const int RT_SMOKE_READBACK_INTERVAL_FRAMES = 120;
const int RT_SMOKE_SCENE_LOG_INTERVAL_FRAMES = 120;
const int RT_SMOKE_TEXTURE_PROBE_CANDIDATE_SAMPLES = 24;
const int RT_SMOKE_TEXTURE_PROBE_DUMP_CANDIDATES = 64;
const int RT_SMOKE_MAX_EMISSIVE_TRIANGLE_RECORDS = 65536;
const int RT_SMOKE_VERTEX_STRIDE = sizeof(PathTraceSmokeVertex);

int g_smokeLastSceneTimingLogMs = -1000000;
int g_smokeLastReadbackTimingLogMs = -1000000;

}

PathTracePrimaryPass::PathTracePrimaryPass(idRenderBackend* backend)
    : m_backend(backend)
    , m_reportedMode(false)
    , m_rayTracingSupported(false)
    , m_smokeTestInitialized(false)
    , m_smokeSceneBuilt(false)
    , m_smokeSceneRebuildLogged(false)
    , m_smokeTestDispatched(false)
    , m_smokeWaitingForDoomSurfaceLogged(false)
    , m_smokeReadbackQueued(false)
    , m_smokeReadbackLogged(false)
    , m_smokeReadbackDelayFrames(0)
    , m_smokeReadbackCooldownFrames(0)
    , m_smokeSceneLogCooldownFrames(0)
    , m_smokeOutputWidth(0)
    , m_smokeOutputHeight(0)
    , m_smokeStaticBlasCacheValid(false)
    , m_smokeStaticBlasSignature(0)
    , m_smokeStaticBlasCacheHitCount(0)
    , m_smokeStaticBlasCacheMissCount(0)
    , m_smokeTextureProbeMaterialId(0)
    , m_smokeTextureProbeRequestedIndex(-1)
    , m_smokeSceneOrigin(vec3_origin)
{
    nvrhi::IDevice* device = deviceManager ? deviceManager->GetDevice() : nullptr;
    if (device)
    {
        m_rayTracingSupported =
            device->queryFeatureSupport(nvrhi::Feature::RayTracingAccelStruct) &&
            device->queryFeatureSupport(nvrhi::Feature::RayTracingPipeline);
    }

    common->Printf("PathTracePrimaryPass: initialized, ray tracing %s\n",
        m_rayTracingSupported ? "available" : "unavailable");
}

PathTracePrimaryPass::~PathTracePrimaryPass()
{
}

void PathTracePrimaryPass::Execute(const viewDef_t* viewDef)
{
    const int mode = r_pathTracing.GetInteger();

    if (!m_reportedMode)
    {
        common->Printf("PathTracePrimaryPass: mode %d (%s)\n",
            mode, mode == 2 ? "pure primary rays" : "hybrid");

        if (!m_rayTracingSupported)
        {
            common->Printf("PathTracePrimaryPass: RT device features are not available; restart with r_pathTracing enabled before device creation\n");
        }

        m_reportedMode = true;
    }

    if (!m_rayTracingSupported)
    {
        return;
    }

    InitRayTracingSmokeTest();
    const int outputWidth = idMath::ClampInt(RT_SMOKE_MIN_OUTPUT_WIDTH, RT_SMOKE_MAX_OUTPUT_WIDTH, r_pathTracingDebugWidth.GetInteger());
    const int outputHeight = idMath::ClampInt(RT_SMOKE_MIN_OUTPUT_HEIGHT, RT_SMOKE_MAX_OUTPUT_HEIGHT, r_pathTracingDebugHeight.GetInteger());
    if (!ResizeRayTracingSmokeOutput(outputWidth, outputHeight))
    {
        return;
    }

    BuildRayTracingSmokeTestScene(viewDef);
    ExecuteRayTracingSmokeTest(viewDef);
    ReadBackRayTracingSmokeTest();
}

void PathTracePrimaryPass::InitRayTracingSmokeTest()
{
    if (m_smokeTestInitialized)
    {
        return;
    }

    m_smokeTestInitialized = true;

    nvrhi::IDevice* device = deviceManager ? deviceManager->GetDevice() : nullptr;
    if (!device)
    {
        common->Printf("PathTracePrimaryPass: cannot initialize RT smoke test without an NVRHI device\n");
        return;
    }

    const char* shaderPath = nullptr;
    if (deviceManager->GetGraphicsAPI() == nvrhi::GraphicsAPI::D3D12)
    {
        shaderPath = "renderprogs2/dxil/builtin/pathtracing/pathtrace_smoke.rt.bin";
    }
    else if (deviceManager->GetGraphicsAPI() == nvrhi::GraphicsAPI::VULKAN)
    {
        shaderPath = "renderprogs2/spirv/builtin/pathtracing/pathtrace_smoke.rt.bin";
    }
    else
    {
        common->Printf("PathTracePrimaryPass: RT smoke test does not support this graphics API\n");
        return;
    }

    void* shaderData = nullptr;
    ID_TIME_T shaderTimestamp = 0;
    const int shaderSize = fileSystem->ReadFile(shaderPath, &shaderData, &shaderTimestamp);
    if (shaderSize <= 0 || !shaderData)
    {
        common->Printf("PathTracePrimaryPass: couldn't read %s\n", shaderPath);
        return;
    }

    common->Printf("PathTracePrimaryPass: loaded RT smoke shader %s (%d bytes, timestamp %u)\n",
        shaderPath, shaderSize, static_cast<unsigned int>(shaderTimestamp));

    m_smokeShaderLibrary = device->createShaderLibrary(shaderData, shaderSize);
    Mem_Free(shaderData);

    if (!m_smokeShaderLibrary)
    {
        common->Printf("PathTracePrimaryPass: failed to create RT smoke shader library\n");
        return;
    }

    m_smokeTlas = device->createAccelStruct(nvrhi::rt::AccelStructDesc()
        .setTopLevelMaxInstances(2)
        .setBuildFlags(nvrhi::rt::AccelStructBuildFlags::PreferFastTrace)
        .setDebugName("PathTraceSmokeTLAS"));

    if (!m_smokeTlas)
    {
        common->Printf("PathTracePrimaryPass: failed to create RT smoke TLAS\n");
        return;
    }

    nvrhi::BufferDesc constantsDesc;
    constantsDesc.byteSize = GetPathTraceSmokeConstantsSize();
    constantsDesc.debugName = "PathTraceSmokeConstants";
    constantsDesc.isConstantBuffer = true;
    constantsDesc.initialState = nvrhi::ResourceStates::ConstantBuffer;
    constantsDesc.keepInitialState = true;
    m_smokeConstantsBuffer = device->createBuffer(constantsDesc);

    if (!m_smokeConstantsBuffer)
    {
        common->Printf("PathTracePrimaryPass: failed to create RT smoke constants buffer\n");
        return;
    }

    nvrhi::BindingLayoutDesc bindingLayoutDesc;
    bindingLayoutDesc.visibility = nvrhi::ShaderType::AllRayTracing;
    bindingLayoutDesc.bindingOffsets = nvrhi::VulkanBindingOffsets()
        .setShaderResourceOffset(0)
        .setConstantBufferOffset(0)
        .setUnorderedAccessViewOffset(0);
    bindingLayoutDesc.addItem(nvrhi::BindingLayoutItem::RayTracingAccelStruct(0));
    bindingLayoutDesc.addItem(nvrhi::BindingLayoutItem::Texture_UAV(1));
    bindingLayoutDesc.addItem(nvrhi::BindingLayoutItem::ConstantBuffer(2));
    bindingLayoutDesc.addItem(nvrhi::BindingLayoutItem::StructuredBuffer_SRV(3));
    bindingLayoutDesc.addItem(nvrhi::BindingLayoutItem::StructuredBuffer_SRV(4));
    bindingLayoutDesc.addItem(nvrhi::BindingLayoutItem::StructuredBuffer_SRV(5));
    bindingLayoutDesc.addItem(nvrhi::BindingLayoutItem::StructuredBuffer_SRV(6));
    bindingLayoutDesc.addItem(nvrhi::BindingLayoutItem::StructuredBuffer_SRV(7));
    bindingLayoutDesc.addItem(nvrhi::BindingLayoutItem::StructuredBuffer_SRV(8));
    bindingLayoutDesc.addItem(nvrhi::BindingLayoutItem::StructuredBuffer_SRV(9));
    bindingLayoutDesc.addItem(nvrhi::BindingLayoutItem::StructuredBuffer_SRV(10));
    bindingLayoutDesc.addItem(nvrhi::BindingLayoutItem::StructuredBuffer_SRV(11));
    bindingLayoutDesc.addItem(nvrhi::BindingLayoutItem::StructuredBuffer_SRV(12));
    bindingLayoutDesc.addItem(nvrhi::BindingLayoutItem::StructuredBuffer_SRV(13));
    bindingLayoutDesc.addItem(nvrhi::BindingLayoutItem::Texture_SRV(14));
    bindingLayoutDesc.addItem(nvrhi::BindingLayoutItem::Texture_UAV(15));
    bindingLayoutDesc.addItem(nvrhi::BindingLayoutItem::StructuredBuffer_SRV(16));
    bindingLayoutDesc.addItem(nvrhi::BindingLayoutItem::Sampler(0));
    m_smokeBindingLayout = device->createBindingLayout(bindingLayoutDesc);

    if (!m_smokeBindingLayout)
    {
        common->Printf("PathTracePrimaryPass: failed to create RT smoke binding layout\n");
        return;
    }

    nvrhi::BindlessLayoutDesc textureBindlessLayoutDesc;
    textureBindlessLayoutDesc
        .setVisibility(nvrhi::ShaderType::AllRayTracing)
        .setFirstSlot(0)
        .setMaxCapacity(RT_SMOKE_TEXTURE_DESCRIPTOR_CAPACITY)
        .addRegisterSpace(nvrhi::BindingLayoutItem::Texture_SRV(1));
    m_smokeTextureBindlessLayout = device->createBindlessLayout(textureBindlessLayoutDesc);
    if (!m_smokeTextureBindlessLayout)
    {
        common->Printf("PathTracePrimaryPass: failed to create RT smoke bindless texture layout\n");
        return;
    }

    m_smokeTextureDescriptorTable = device->createDescriptorTable(m_smokeTextureBindlessLayout);
    if (!m_smokeTextureDescriptorTable)
    {
        common->Printf("PathTracePrimaryPass: failed to create RT smoke texture descriptor table\n");
        return;
    }

    nvrhi::rt::PipelineDesc pipelineDesc;
    pipelineDesc.globalBindingLayouts = { m_smokeBindingLayout, m_smokeTextureBindlessLayout };
    pipelineDesc.shaders = {
        { "", m_smokeShaderLibrary->getShader("RayGen", nvrhi::ShaderType::RayGeneration), nullptr },
        { "", m_smokeShaderLibrary->getShader("Miss", nvrhi::ShaderType::Miss), nullptr }
    };
    pipelineDesc.hitGroups = {
        {
            "HitGroup",
            m_smokeShaderLibrary->getShader("ClosestHit", nvrhi::ShaderType::ClosestHit),
            m_smokeShaderLibrary->getShader("AnyHit", nvrhi::ShaderType::AnyHit),
            nullptr,
            nullptr,
            false
        }
    };
    pipelineDesc.maxPayloadSize = 64;
    pipelineDesc.maxAttributeSize = 8;
    pipelineDesc.maxRecursionDepth = 1;

    m_smokePipeline = device->createRayTracingPipeline(pipelineDesc);
    if (!m_smokePipeline)
    {
        common->Printf("PathTracePrimaryPass: failed to create RT smoke pipeline\n");
        return;
    }

    m_smokeShaderTable = m_smokePipeline->createShaderTable();
    if (!m_smokeShaderTable)
    {
        common->Printf("PathTracePrimaryPass: failed to create RT smoke shader table\n");
        return;
    }

    m_smokeShaderTable->setRayGenerationShader("RayGen");
    m_smokeShaderTable->addMissShader("Miss");
    m_smokeShaderTable->addHitGroup("HitGroup");

    common->Printf("PathTracePrimaryPass: RT smoke pipeline initialized\n");
}

bool PathTracePrimaryPass::ResizeRayTracingSmokeOutput(int width, int height)
{
    if (!m_smokeTestInitialized || !m_smokeShaderTable)
    {
        return false;
    }

    if (m_smokeOutputTexture && m_smokeAccumulationTexture && m_smokeReadbackTexture && width == m_smokeOutputWidth && height == m_smokeOutputHeight)
    {
        return true;
    }

    nvrhi::IDevice* device = deviceManager ? deviceManager->GetDevice() : nullptr;
    if (!device)
    {
        return false;
    }

    nvrhi::TextureDesc outputDesc;
    outputDesc.width = width;
    outputDesc.height = height;
    outputDesc.mipLevels = 1;
    outputDesc.arraySize = 1;
    outputDesc.format = nvrhi::Format::RGBA32_FLOAT;
    outputDesc.dimension = nvrhi::TextureDimension::Texture2D;
    outputDesc.isUAV = true;
    outputDesc.initialState = nvrhi::ResourceStates::UnorderedAccess;
    outputDesc.keepInitialState = true;
    outputDesc.debugName = "PathTraceSmokeOutput";
    nvrhi::TextureHandle outputTexture = device->createTexture(outputDesc);

    if (!outputTexture)
    {
        common->Printf("PathTracePrimaryPass: failed to create RT smoke output UAV (%dx%d)\n", width, height);
        return false;
    }

    outputDesc.debugName = "PathTraceSmokeAccumulation";
    nvrhi::TextureHandle accumulationTexture = device->createTexture(outputDesc);
    if (!accumulationTexture)
    {
        common->Printf("PathTracePrimaryPass: failed to create RT smoke accumulation UAV (%dx%d)\n", width, height);
        return false;
    }

    nvrhi::TextureDesc readbackDesc = outputDesc;
    readbackDesc.isShaderResource = false;
    readbackDesc.isUAV = false;
    readbackDesc.initialState = nvrhi::ResourceStates::Unknown;
    readbackDesc.keepInitialState = false;
    readbackDesc.debugName = "PathTraceSmokeReadback";
    nvrhi::StagingTextureHandle readbackTexture = device->createStagingTexture(readbackDesc, nvrhi::CpuAccessMode::Read);

    if (!readbackTexture)
    {
        common->Printf("PathTracePrimaryPass: failed to create RT smoke readback texture (%dx%d)\n", width, height);
        return false;
    }

    m_smokeOutputTexture = outputTexture;
    m_smokeAccumulationTexture = accumulationTexture;
    m_smokeReadbackTexture = readbackTexture;
    m_smokeOutputWidth = width;
    m_smokeOutputHeight = height;
    m_smokeAccumulationSignature = 0;
    m_smokeAccumulationFrameCount = 0;
    m_smokeBindingSet = nullptr;
    m_smokeSceneBuilt = false;
    m_smokeTestDispatched = false;
    m_smokeReadbackQueued = false;
    m_smokeReadbackDelayFrames = 0;
    m_smokeReadbackCooldownFrames = 0;
    m_smokeStaticBlasCacheValid = false;
    m_smokeStaticBlas = nullptr;
    m_smokeDynamicBlas = nullptr;
    m_smokeStaticVertexBuffer = nullptr;
    m_smokeStaticIndexBuffer = nullptr;
    m_smokeStaticTriangleClassBuffer = nullptr;
    m_smokeStaticTriangleMaterialBuffer = nullptr;
    m_smokeStaticTriangleMaterialIndexBuffer = nullptr;
    m_smokeDynamicVertexBuffer = nullptr;
    m_smokeDynamicIndexBuffer = nullptr;
    m_smokeDynamicTriangleClassBuffer = nullptr;
    m_smokeDynamicTriangleMaterialBuffer = nullptr;
    m_smokeDynamicTriangleMaterialIndexBuffer = nullptr;
    m_smokeMaterialTableBuffer = nullptr;
    m_smokeEmissiveTriangleBuffer = nullptr;
    m_smokeActiveTextureTable.clear();
    m_smokeMaterialTableEntryCount = 0;
    m_smokeEmissiveTriangleCount = 0;
    m_smokeEmissiveStaticTriangleCount = 0;
    m_smokeEmissiveDynamicTriangleCount = 0;

    common->Printf("PathTracePrimaryPass: RT smoke output UAV initialized (%dx%d)\n", width, height);
    return true;
}

void PathTracePrimaryPass::BuildRayTracingSmokeTestScene(const viewDef_t* viewDef)
{
    const int sceneStartMs = Sys_Milliseconds();
    m_smokeSceneBuilt = false;
    const int requestedDebugMode = idMath::ClampInt(0, 19, r_pathTracingDebugMode.GetInteger());
    const bool enableTextureProbe = (requestedDebugMode >= 8 && requestedDebugMode <= 19);

    if (!m_smokeTlas || !m_smokeBindingLayout || !m_smokeTextureBindlessLayout || !m_smokeTextureDescriptorTable || !m_smokeOutputTexture || !m_smokeAccumulationTexture || !m_smokeConstantsBuffer)
    {
        return;
    }

    nvrhi::IDevice* device = deviceManager ? deviceManager->GetDevice() : nullptr;
    if (!device)
    {
        return;
    }

    nvrhi::ICommandList* commandList = m_backend ? m_backend->GL_GetCommandList() : nullptr;
    if (!commandList)
    {
        return;
    }

    std::vector<PathTraceSmokeVertex> dynamicVertexData;
    std::vector<uint32_t> dynamicIndexData;
    std::vector<uint32_t> dynamicTriangleClassData;
    std::vector<uint32_t> dynamicTriangleMaterialData;
    int sourceSurfaces = 0;
    int sourceVerts = 0;
    int sourceIndexes = 0;
    int anchorTriangle = -1;
    RtSmokeSurfaceClassStats classStats;
    RtSmokeSurfaceSkipStats skipStats;
    RtSmokeDynamicGeometryStats dynamicStats;
    RtSmokeAttributeStats attributeStats;
    RtSmokeMaterialStats materialStats;
    RtSmokeBucketRanges bucketRanges;
    const bool dumpClassReasons = r_pathTracingClassDump.GetInteger() != 0;
    RtSmokeSurfaceClassReasonSamples reasonSamples;
    bool staticCacheChanged = false;
    RtSmokeSceneCaptureTiming captureTiming;
    const int captureStartMs = Sys_Milliseconds();
    const bool usingDoomSurfaces = CaptureDoomSurfacesForSmokeTest(viewDef, dynamicVertexData, dynamicIndexData, dynamicTriangleClassData, dynamicTriangleMaterialData, m_smokeStaticSurfaceKeys, m_smokeStaticVertexCache, m_smokeStaticIndexCache, m_smokeStaticTriangleClassCache, m_smokeStaticTriangleMaterialCache, staticCacheChanged, m_smokeSceneOrigin, sourceSurfaces, sourceVerts, sourceIndexes, anchorTriangle, classStats, skipStats, dynamicStats, attributeStats, materialStats, bucketRanges, captureTiming, dumpClassReasons ? &reasonSamples : nullptr);
    const int captureMs = Sys_Milliseconds() - captureStartMs;
    if (!usingDoomSurfaces)
    {
        if (!m_smokeWaitingForDoomSurfaceLogged)
        {
            common->Printf("PathTracePrimaryPass: waiting for center camera ray Doom surface hit to build RT smoke BLAS\n");
            m_smokeWaitingForDoomSurfaceLogged = true;
        }
        return;
    }

    g_smokeMaterialMetadataFrameStats = RtSmokeMaterialMetadataFrameStats();
    const int metadataStartMs = Sys_Milliseconds();
    int metadataValidationMs = 0;
    int metadataRegistrationMs = 0;
    if (enableTextureProbe)
    {
        std::vector<uint32_t> registeredMaterialIds;
        registeredMaterialIds.reserve(viewDef->numDrawSurfs);
        for (int surfaceIndex = 0; surfaceIndex < viewDef->numDrawSurfs; ++surfaceIndex)
        {
            const drawSurf_t* drawSurf = viewDef->drawSurfs[surfaceIndex];
            const srfTriangles_t* tri = nullptr;
            const int validationStartMs = Sys_Milliseconds();
            if (!ValidateSmokeDrawSurface(viewDef, drawSurf, tri, nullptr))
            {
                metadataValidationMs += Sys_Milliseconds() - validationStartMs;
                continue;
            }
            metadataValidationMs += Sys_Milliseconds() - validationStartMs;

            const uint32_t materialId = SmokeMaterialId(drawSurf->material);
            if (std::find(registeredMaterialIds.begin(), registeredMaterialIds.end(), materialId) != registeredMaterialIds.end())
            {
                ++g_smokeMaterialMetadataFrameStats.duplicateSkips;
                continue;
            }

            registeredMaterialIds.push_back(materialId);
            const int registrationStartMs = Sys_Milliseconds();
            RegisterSmokeMaterialTextureInfo(drawSurf->material);
            metadataRegistrationMs += Sys_Milliseconds() - registrationStartMs;
        }
    }
    const int metadataMs = Sys_Milliseconds() - metadataStartMs;

    const int materialStartMs = Sys_Milliseconds();
    RtSmokeMaterialTableBuild materialTable;
    uint64 materialTableSignature = 0;
    bool materialTableCacheHit = false;
    BuildSmokeMaterialTableCached(materialTable, m_smokeStaticTriangleMaterialCache, dynamicTriangleMaterialData, m_smokeTextureProbeMaterialId, m_smokeTextureProbeRequestedIndex, enableTextureProbe, materialTableSignature, materialTableCacheHit);
    const RtSmokeMaterialTableCacheStats materialTableCacheStats = GetSmokeMaterialTableCacheStats();
    if (!ValidateSmokeMaterialIndexes(materialTable))
    {
        common->Printf("PathTracePrimaryPass: invalid RT smoke material table, skipping scene build\n");
        return;
    }
    RtSmokeTextureCoverageStats textureCoverageStats;
    const bool needTextureCoverageStats = enableTextureProbe && r_pathTracingSmokeLog.GetInteger() != 0;
    if (needTextureCoverageStats)
    {
        textureCoverageStats = BuildSmokeTextureCoverageStats(
            materialTable,
            m_smokeStaticTriangleClassCache,
            materialTable.staticMaterialIndexes,
            dynamicTriangleClassData,
            materialTable.dynamicMaterialIndexes);
    }
    const int materialMs = Sys_Milliseconds() - materialStartMs;
    static uint32_t lastLoggedTextureProbeMaterialId = 0;
    if (enableTextureProbe && r_pathTracingSmokeLog.GetInteger() != 0 && materialTable.textureProbeBoundMaterialId != lastLoggedTextureProbeMaterialId)
    {
        LogSmokeTextureProbeSwitch(materialTable);
        lastLoggedTextureProbeMaterialId = materialTable.textureProbeBoundMaterialId;
    }
    if (enableTextureProbe && r_pathTracingTextureProbeDump.GetInteger() != 0)
    {
        LogSmokeTextureProbeDump(materialTable);
        r_pathTracingTextureProbeDump.SetInteger(0);
    }
    if (enableTextureProbe && r_pathTracingAlphaDump.GetInteger() != 0)
    {
        LogSmokeAlphaMaterialDump(materialTable);
        r_pathTracingAlphaDump.SetInteger(0);
    }
    if (enableTextureProbe && r_pathTracingTextureFallbackDump.GetInteger() != 0)
    {
        LogSmokeTextureFallbackDump(materialTable);
        r_pathTracingTextureFallbackDump.SetInteger(0);
    }
    if (r_pathTracingTranslucentDump.GetInteger() != 0)
    {
        LogSmokeTranslucentSubtypeDump(materialStats);
        r_pathTracingTranslucentDump.SetInteger(0);
    }
    if (r_pathTracingCrosshairMaterialDump.GetInteger() != 0)
    {
        LogSmokeCrosshairMaterialDump(viewDef, materialTable);
        r_pathTracingCrosshairMaterialDump.SetInteger(0);
    }
    if (r_pathTracingGuiDump.GetInteger() != 0)
    {
        LogSmokeGuiSurfaceDump(viewDef, materialTable);
        r_pathTracingGuiDump.SetInteger(0);
    }
    if (enableTextureProbe && r_pathTracingSmokeLog.GetInteger() != 0)
    {
        LogSmokeTextureActiveWindow(materialTable);
    }

    RtSmokeEmissiveInventoryStats emissiveInventoryStats;
    const int emissiveStartMs = Sys_Milliseconds();
    std::vector<PathTraceSmokeEmissiveTriangle> emissiveTriangles = BuildSmokeEmissiveTriangleInventory(
        materialTable.materials,
        m_smokeStaticVertexCache,
        m_smokeStaticIndexCache,
        m_smokeStaticTriangleClassCache,
        materialTable.staticMaterialIndexes,
        dynamicVertexData,
        dynamicIndexData,
        dynamicTriangleClassData,
        materialTable.dynamicMaterialIndexes,
        RT_SMOKE_MATERIAL_EMISSIVE,
        RT_SMOKE_TRIANGLE_CLASS_MASK,
        static_cast<uint32_t>(RtSmokeSurfaceClass::SkinnedDeformed),
        idMath::ClampInt(1, RT_SMOKE_MAX_EMISSIVE_TRIANGLE_RECORDS, r_pathTracingEmissiveInventoryMaxTriangles.GetInteger()),
        emissiveInventoryStats);
    const int emissiveMs = Sys_Milliseconds() - emissiveStartMs;
    if (r_pathTracingEmissiveInventoryDump.GetInteger() != 0)
    {
        LogSmokeEmissiveInventoryDump(materialTable.materialIds, emissiveTriangles, emissiveInventoryStats);
        r_pathTracingEmissiveInventoryDump.SetInteger(0);
    }

    const int bufferCreateStartMs = Sys_Milliseconds();
    nvrhi::BufferHandle smokeStaticVertexBuffer = CreateSmokeGeometryBuffer(device, "PathTraceSmokeStaticWorldVertices", m_smokeStaticVertexCache.size() * sizeof(m_smokeStaticVertexCache[0]), RT_SMOKE_VERTEX_STRIDE, true, false, true);
    nvrhi::BufferHandle smokeStaticIndexBuffer = CreateSmokeGeometryBuffer(device, "PathTraceSmokeStaticWorldIndices", m_smokeStaticIndexCache.size() * sizeof(m_smokeStaticIndexCache[0]), sizeof(uint32_t), false, true, true);
    nvrhi::BufferHandle smokeStaticTriangleClassBuffer = CreateSmokeGeometryBuffer(device, "PathTraceSmokeStaticWorldTriangleClasses", m_smokeStaticTriangleClassCache.size() * sizeof(m_smokeStaticTriangleClassCache[0]), sizeof(uint32_t), false, false, false);
    nvrhi::BufferHandle smokeStaticTriangleMaterialBuffer = CreateSmokeGeometryBuffer(device, "PathTraceSmokeStaticWorldTriangleMaterials", m_smokeStaticTriangleMaterialCache.size() * sizeof(m_smokeStaticTriangleMaterialCache[0]), sizeof(uint32_t), false, false, false);
    nvrhi::BufferHandle smokeStaticTriangleMaterialIndexBuffer = CreateSmokeGeometryBuffer(device, "PathTraceSmokeStaticWorldTriangleMaterialIndexes", materialTable.staticMaterialIndexes.size() * sizeof(materialTable.staticMaterialIndexes[0]), sizeof(uint32_t), false, false, false);
    nvrhi::BufferHandle smokeDynamicVertexBuffer = CreateSmokeGeometryBuffer(device, "PathTraceSmokeDynamicCandidateVertices", dynamicVertexData.size() * sizeof(dynamicVertexData[0]), RT_SMOKE_VERTEX_STRIDE, true, false, true);
    nvrhi::BufferHandle smokeDynamicIndexBuffer = CreateSmokeGeometryBuffer(device, "PathTraceSmokeDynamicCandidateIndices", dynamicIndexData.size() * sizeof(dynamicIndexData[0]), sizeof(uint32_t), false, true, true);
    nvrhi::BufferHandle smokeDynamicTriangleClassBuffer = CreateSmokeGeometryBuffer(device, "PathTraceSmokeDynamicCandidateTriangleClasses", dynamicTriangleClassData.size() * sizeof(dynamicTriangleClassData[0]), sizeof(uint32_t), false, false, false);
    nvrhi::BufferHandle smokeDynamicTriangleMaterialBuffer = CreateSmokeGeometryBuffer(device, "PathTraceSmokeDynamicCandidateTriangleMaterials", dynamicTriangleMaterialData.size() * sizeof(dynamicTriangleMaterialData[0]), sizeof(uint32_t), false, false, false);
    nvrhi::BufferHandle smokeDynamicTriangleMaterialIndexBuffer = CreateSmokeGeometryBuffer(device, "PathTraceSmokeDynamicCandidateTriangleMaterialIndexes", materialTable.dynamicMaterialIndexes.size() * sizeof(materialTable.dynamicMaterialIndexes[0]), sizeof(uint32_t), false, false, false);
    nvrhi::BufferHandle smokeMaterialTableBuffer = CreateSmokeGeometryBuffer(device, "PathTraceSmokeMaterialTable", materialTable.materials.size() * sizeof(materialTable.materials[0]), sizeof(PathTraceSmokeMaterial), false, false, false);
    nvrhi::BufferHandle smokeEmissiveTriangleBuffer = CreateSmokeGeometryBuffer(device, "PathTraceSmokeEmissiveTriangles", emissiveTriangles.size() * sizeof(emissiveTriangles[0]), sizeof(PathTraceSmokeEmissiveTriangle), false, false, false);

    if (!smokeStaticVertexBuffer || !smokeStaticIndexBuffer || !smokeStaticTriangleClassBuffer || !smokeStaticTriangleMaterialBuffer || !smokeStaticTriangleMaterialIndexBuffer || !smokeDynamicVertexBuffer || !smokeDynamicIndexBuffer || !smokeDynamicTriangleClassBuffer || !smokeDynamicTriangleMaterialBuffer || !smokeDynamicTriangleMaterialIndexBuffer || !smokeMaterialTableBuffer || !smokeEmissiveTriangleBuffer)
    {
        common->Printf("PathTracePrimaryPass: failed to create RT smoke geometry buffers\n");
        return;
    }
    const int bufferCreateMs = Sys_Milliseconds() - bufferCreateStartMs;

    const int staticVertexCount = static_cast<int>(m_smokeStaticVertexCache.size());
    const int dynamicVertexCount = static_cast<int>(dynamicVertexData.size());
    const int staticIndexCount = bucketRanges.buckets[0].indexCount;
    const int dynamicIndexCount =
        bucketRanges.buckets[1].indexCount +
        bucketRanges.buckets[2].indexCount +
        bucketRanges.buckets[3].indexCount +
        bucketRanges.buckets[4].indexCount;
    const bool hasStaticBlas = staticIndexCount > 0;
    const bool hasDynamicBlas = dynamicIndexCount > 0;
    const RtSmokeBucketRange& staticBucketRange = bucketRanges.buckets[0];
    RtSmokeGeometryRange staticGeometryRange;
    staticGeometryRange.vertexOffset = staticBucketRange.vertexOffset;
    staticGeometryRange.vertexCount = staticBucketRange.vertexCount;
    staticGeometryRange.indexOffset = staticBucketRange.indexOffset;
    staticGeometryRange.indexCount = staticBucketRange.indexCount;
    staticGeometryRange.triangleOffset = staticBucketRange.triangleOffset;
    staticGeometryRange.triangleCount = staticBucketRange.triangleCount;
    const RtSmokeStaticBlasSignature staticSignature = ComputeSmokeStaticBlasSignature(m_smokeStaticVertexCache, m_smokeStaticIndexCache, m_smokeStaticTriangleClassCache, m_smokeStaticTriangleMaterialCache, staticGeometryRange, vec3_origin);
    const bool staticBlasCacheHit = hasStaticBlas && m_smokeStaticBlasCacheValid && m_smokeStaticBlas &&
        m_smokeStaticVertexBuffer && m_smokeStaticIndexBuffer && m_smokeStaticTriangleClassBuffer && m_smokeStaticTriangleMaterialBuffer && m_smokeStaticTriangleMaterialIndexBuffer &&
        !staticCacheChanged && m_smokeStaticBlasSignature == staticSignature.hash;
    if (staticBlasCacheHit)
    {
        smokeStaticVertexBuffer = m_smokeStaticVertexBuffer;
        smokeStaticIndexBuffer = m_smokeStaticIndexBuffer;
        smokeStaticTriangleClassBuffer = m_smokeStaticTriangleClassBuffer;
        smokeStaticTriangleMaterialBuffer = m_smokeStaticTriangleMaterialBuffer;
        smokeStaticTriangleMaterialIndexBuffer = m_smokeStaticTriangleMaterialIndexBuffer;
    }

    if (!hasStaticBlas && !hasDynamicBlas)
    {
        common->Printf("PathTracePrimaryPass: no RT smoke BLAS ranges to build\n");
        return;
    }

    nvrhi::rt::AccelStructDesc smokeStaticBlasDesc;
    nvrhi::rt::AccelStructHandle smokeStaticBlas;
    if (hasStaticBlas)
    {
        if (staticBlasCacheHit)
        {
            smokeStaticBlas = m_smokeStaticBlas;
            smokeStaticBlasDesc = m_smokeStaticBlasDesc;
            ++m_smokeStaticBlasCacheHitCount;
        }
        else
        {
            nvrhi::rt::GeometryTriangles staticTriangleGeometry;
            InitSmokeTriangleGeometry(staticTriangleGeometry, smokeStaticVertexBuffer, smokeStaticIndexBuffer, staticVertexCount, 0, staticIndexCount);

            nvrhi::rt::GeometryDesc staticGeometryDesc;
            staticGeometryDesc.setTriangles(staticTriangleGeometry);

            smokeStaticBlasDesc = nvrhi::rt::AccelStructDesc()
                .addBottomLevelGeometry(staticGeometryDesc)
                .setBuildFlags(nvrhi::rt::AccelStructBuildFlags::PreferFastTrace)
                .setDebugName("PathTraceSmokeStaticWorldBLAS");
            smokeStaticBlas = device->createAccelStruct(smokeStaticBlasDesc);
            if (!smokeStaticBlas)
            {
                common->Printf("PathTracePrimaryPass: failed to create RT smoke static BLAS\n");
                return;
            }
            ++m_smokeStaticBlasCacheMissCount;
        }
    }

    nvrhi::rt::AccelStructDesc smokeDynamicBlasDesc;
    nvrhi::rt::AccelStructHandle smokeDynamicBlas;
    if (hasDynamicBlas)
    {
        nvrhi::rt::GeometryTriangles dynamicTriangleGeometry;
        InitSmokeTriangleGeometry(dynamicTriangleGeometry, smokeDynamicVertexBuffer, smokeDynamicIndexBuffer, dynamicVertexCount, 0, dynamicIndexCount);

        nvrhi::rt::GeometryDesc dynamicGeometryDesc;
        dynamicGeometryDesc.setTriangles(dynamicTriangleGeometry);

        smokeDynamicBlasDesc = nvrhi::rt::AccelStructDesc()
            .addBottomLevelGeometry(dynamicGeometryDesc)
            .setBuildFlags(nvrhi::rt::AccelStructBuildFlags::PreferFastTrace)
            .setDebugName("PathTraceSmokeDynamicCandidateBLAS");
        smokeDynamicBlas = device->createAccelStruct(smokeDynamicBlasDesc);
        if (!smokeDynamicBlas)
        {
            common->Printf("PathTracePrimaryPass: failed to create RT smoke dynamic BLAS\n");
            return;
        }
    }

    if (!staticBlasCacheHit)
    {
        commandList->beginTrackingBufferState(smokeStaticVertexBuffer, nvrhi::ResourceStates::Common);
        commandList->beginTrackingBufferState(smokeStaticIndexBuffer, nvrhi::ResourceStates::Common);
        commandList->beginTrackingBufferState(smokeStaticTriangleClassBuffer, nvrhi::ResourceStates::Common);
        commandList->beginTrackingBufferState(smokeStaticTriangleMaterialBuffer, nvrhi::ResourceStates::Common);
        commandList->beginTrackingBufferState(smokeStaticTriangleMaterialIndexBuffer, nvrhi::ResourceStates::Common);
    }
    commandList->beginTrackingBufferState(smokeDynamicVertexBuffer, nvrhi::ResourceStates::Common);
    commandList->beginTrackingBufferState(smokeDynamicIndexBuffer, nvrhi::ResourceStates::Common);
    commandList->beginTrackingBufferState(smokeDynamicTriangleClassBuffer, nvrhi::ResourceStates::Common);
    commandList->beginTrackingBufferState(smokeDynamicTriangleMaterialBuffer, nvrhi::ResourceStates::Common);
    commandList->beginTrackingBufferState(smokeDynamicTriangleMaterialIndexBuffer, nvrhi::ResourceStates::Common);
    commandList->beginTrackingBufferState(smokeMaterialTableBuffer, nvrhi::ResourceStates::Common);
    commandList->beginTrackingBufferState(smokeEmissiveTriangleBuffer, nvrhi::ResourceStates::Common);
    const int bufferUploadStartMs = Sys_Milliseconds();
    if (!staticBlasCacheHit && !m_smokeStaticVertexCache.empty())
    {
        commandList->writeBuffer(smokeStaticVertexBuffer, m_smokeStaticVertexCache.data(), m_smokeStaticVertexCache.size() * sizeof(m_smokeStaticVertexCache[0]));
    }
    if (!staticBlasCacheHit && !m_smokeStaticIndexCache.empty())
    {
        commandList->writeBuffer(smokeStaticIndexBuffer, m_smokeStaticIndexCache.data(), m_smokeStaticIndexCache.size() * sizeof(m_smokeStaticIndexCache[0]));
    }
    if (!staticBlasCacheHit && !m_smokeStaticTriangleClassCache.empty())
    {
        commandList->writeBuffer(smokeStaticTriangleClassBuffer, m_smokeStaticTriangleClassCache.data(), m_smokeStaticTriangleClassCache.size() * sizeof(m_smokeStaticTriangleClassCache[0]));
    }
    if (!staticBlasCacheHit && !m_smokeStaticTriangleMaterialCache.empty())
    {
        commandList->writeBuffer(smokeStaticTriangleMaterialBuffer, m_smokeStaticTriangleMaterialCache.data(), m_smokeStaticTriangleMaterialCache.size() * sizeof(m_smokeStaticTriangleMaterialCache[0]));
    }
    if (!staticBlasCacheHit && !materialTable.staticMaterialIndexes.empty())
    {
        commandList->writeBuffer(smokeStaticTriangleMaterialIndexBuffer, materialTable.staticMaterialIndexes.data(), materialTable.staticMaterialIndexes.size() * sizeof(materialTable.staticMaterialIndexes[0]));
    }
    if (!dynamicVertexData.empty())
    {
        commandList->writeBuffer(smokeDynamicVertexBuffer, dynamicVertexData.data(), dynamicVertexData.size() * sizeof(dynamicVertexData[0]));
    }
    if (!dynamicIndexData.empty())
    {
        commandList->writeBuffer(smokeDynamicIndexBuffer, dynamicIndexData.data(), dynamicIndexData.size() * sizeof(dynamicIndexData[0]));
    }
    if (!dynamicTriangleClassData.empty())
    {
        commandList->writeBuffer(smokeDynamicTriangleClassBuffer, dynamicTriangleClassData.data(), dynamicTriangleClassData.size() * sizeof(dynamicTriangleClassData[0]));
    }
    if (!dynamicTriangleMaterialData.empty())
    {
        commandList->writeBuffer(smokeDynamicTriangleMaterialBuffer, dynamicTriangleMaterialData.data(), dynamicTriangleMaterialData.size() * sizeof(dynamicTriangleMaterialData[0]));
    }
    if (!materialTable.dynamicMaterialIndexes.empty())
    {
        commandList->writeBuffer(smokeDynamicTriangleMaterialIndexBuffer, materialTable.dynamicMaterialIndexes.data(), materialTable.dynamicMaterialIndexes.size() * sizeof(materialTable.dynamicMaterialIndexes[0]));
    }
    if (!materialTable.materials.empty())
    {
        commandList->writeBuffer(smokeMaterialTableBuffer, materialTable.materials.data(), materialTable.materials.size() * sizeof(materialTable.materials[0]));
    }
    if (!emissiveTriangles.empty())
    {
        commandList->writeBuffer(smokeEmissiveTriangleBuffer, emissiveTriangles.data(), emissiveTriangles.size() * sizeof(emissiveTriangles[0]));
    }
    if (!staticBlasCacheHit)
    {
        commandList->setBufferState(smokeStaticVertexBuffer, nvrhi::ResourceStates::AccelStructBuildInput);
        commandList->setBufferState(smokeStaticIndexBuffer, nvrhi::ResourceStates::AccelStructBuildInput);
        commandList->setBufferState(smokeStaticTriangleClassBuffer, nvrhi::ResourceStates::ShaderResource);
        commandList->setBufferState(smokeStaticTriangleMaterialBuffer, nvrhi::ResourceStates::ShaderResource);
        commandList->setBufferState(smokeStaticTriangleMaterialIndexBuffer, nvrhi::ResourceStates::ShaderResource);
    }
    commandList->setBufferState(smokeDynamicVertexBuffer, nvrhi::ResourceStates::AccelStructBuildInput);
    commandList->setBufferState(smokeDynamicIndexBuffer, nvrhi::ResourceStates::AccelStructBuildInput);
    commandList->setBufferState(smokeDynamicTriangleClassBuffer, nvrhi::ResourceStates::ShaderResource);
    commandList->setBufferState(smokeDynamicTriangleMaterialBuffer, nvrhi::ResourceStates::ShaderResource);
    commandList->setBufferState(smokeDynamicTriangleMaterialIndexBuffer, nvrhi::ResourceStates::ShaderResource);
    commandList->setBufferState(smokeMaterialTableBuffer, nvrhi::ResourceStates::ShaderResource);
    commandList->setBufferState(smokeEmissiveTriangleBuffer, nvrhi::ResourceStates::ShaderResource);
    commandList->commitBarriers();
    const int bufferUploadMs = Sys_Milliseconds() - bufferUploadStartMs;

    const int accelSubmitStartMs = Sys_Milliseconds();
    const int blasSubmitStartMs = Sys_Milliseconds();
    if (hasStaticBlas && !staticBlasCacheHit)
    {
        nvrhi::utils::BuildBottomLevelAccelStruct(commandList, smokeStaticBlas, smokeStaticBlasDesc);
    }

    if (hasDynamicBlas)
    {
        nvrhi::utils::BuildBottomLevelAccelStruct(commandList, smokeDynamicBlas, smokeDynamicBlasDesc);
    }
    const int blasSubmitMs = Sys_Milliseconds() - blasSubmitStartMs;

    nvrhi::rt::InstanceDesc instanceDescs[2];
    int instanceCount = 0;
    if (hasStaticBlas)
    {
        instanceDescs[instanceCount]
            .setInstanceID(0)
            .setInstanceMask(0xff)
            .setInstanceContributionToHitGroupIndex(0)
            .setFlags(nvrhi::rt::InstanceFlags::TriangleCullDisable)
            .setBLAS(smokeStaticBlas);
        ++instanceCount;
    }
    if (hasDynamicBlas)
    {
        instanceDescs[instanceCount]
            .setInstanceID(1)
            .setInstanceMask(0xff)
            .setInstanceContributionToHitGroupIndex(0)
            .setFlags(nvrhi::rt::InstanceFlags::TriangleCullDisable)
            .setBLAS(smokeDynamicBlas);
        ++instanceCount;
    }

    const int tlasSubmitStartMs = Sys_Milliseconds();
    commandList->buildTopLevelAccelStruct(m_smokeTlas, instanceDescs, instanceCount, nvrhi::rt::AccelStructBuildFlags::PreferFastTrace);
    const int tlasSubmitMs = Sys_Milliseconds() - tlasSubmitStartMs;
    const int accelSubmitMs = Sys_Milliseconds() - accelSubmitStartMs;

    const nvrhi::TextureHandle fallbackTexture = globalImages && globalImages->whiteImage ? globalImages->whiteImage->GetTextureHandle() : nullptr;
    if (!fallbackTexture)
    {
        common->Printf("PathTracePrimaryPass: failed to find RT smoke fallback material texture\n");
        return;
    }

    RtSmokeSceneBufferHandles smokeBuffers;
    smokeBuffers.staticVertexBuffer = smokeStaticVertexBuffer;
    smokeBuffers.staticIndexBuffer = smokeStaticIndexBuffer;
    smokeBuffers.staticTriangleClassBuffer = smokeStaticTriangleClassBuffer;
    smokeBuffers.staticTriangleMaterialBuffer = smokeStaticTriangleMaterialBuffer;
    smokeBuffers.staticTriangleMaterialIndexBuffer = smokeStaticTriangleMaterialIndexBuffer;
    smokeBuffers.dynamicVertexBuffer = smokeDynamicVertexBuffer;
    smokeBuffers.dynamicIndexBuffer = smokeDynamicIndexBuffer;
    smokeBuffers.dynamicTriangleClassBuffer = smokeDynamicTriangleClassBuffer;
    smokeBuffers.dynamicTriangleMaterialBuffer = smokeDynamicTriangleMaterialBuffer;
    smokeBuffers.dynamicTriangleMaterialIndexBuffer = smokeDynamicTriangleMaterialIndexBuffer;
    smokeBuffers.materialTableBuffer = smokeMaterialTableBuffer;
    smokeBuffers.emissiveTriangleBuffer = smokeEmissiveTriangleBuffer;

    RtSmokeBindingBuildDesc bindingBuildDesc;
    bindingBuildDesc.device = device;
    bindingBuildDesc.tlas = m_smokeTlas;
    bindingBuildDesc.outputTexture = m_smokeOutputTexture;
    bindingBuildDesc.accumulationTexture = m_smokeAccumulationTexture;
    bindingBuildDesc.fallbackTexture = fallbackTexture;
    bindingBuildDesc.constantsBuffer = m_smokeConstantsBuffer;
    bindingBuildDesc.bindingLayout = m_smokeBindingLayout;
    bindingBuildDesc.textureBindlessLayout = m_smokeTextureBindlessLayout;
    bindingBuildDesc.existingTextureDescriptorTable = m_smokeTextureDescriptorTable;
    bindingBuildDesc.sampler = m_backend->GetCommonPasses().m_AnisotropicWrapSampler;
    bindingBuildDesc.buffers = smokeBuffers;
    bindingBuildDesc.enableTextureProbe = enableTextureProbe;
    bindingBuildDesc.forceFallbackTexture = r_pathTracingTextureForceFallback.GetInteger() != 0;
    bindingBuildDesc.maxActiveTextures = RT_SMOKE_TEXTURE_EXPERIMENTAL_ACTIVE_CAP;

    RtSmokeBindingBuildResult bindingBuildResult = CreateSmokeBindingResources(bindingBuildDesc, materialTable);
    if (!bindingBuildResult.Succeeded())
    {
        if (bindingBuildResult.failedTextureSlot >= 0)
        {
            common->Printf("PathTracePrimaryPass: %s %d\n", bindingBuildResult.errorMessage, bindingBuildResult.failedTextureSlot);
        }
        else
        {
            common->Printf("PathTracePrimaryPass: %s\n", bindingBuildResult.errorMessage ? bindingBuildResult.errorMessage : "failed to create RT smoke binding resources");
        }
        return;
    }

    m_smokeStaticVertexBuffer = smokeStaticVertexBuffer;
    m_smokeStaticIndexBuffer = smokeStaticIndexBuffer;
    m_smokeStaticTriangleClassBuffer = smokeStaticTriangleClassBuffer;
    m_smokeStaticTriangleMaterialBuffer = smokeStaticTriangleMaterialBuffer;
    m_smokeStaticTriangleMaterialIndexBuffer = smokeStaticTriangleMaterialIndexBuffer;
    m_smokeDynamicVertexBuffer = smokeDynamicVertexBuffer;
    m_smokeDynamicIndexBuffer = smokeDynamicIndexBuffer;
    m_smokeDynamicTriangleClassBuffer = smokeDynamicTriangleClassBuffer;
    m_smokeDynamicTriangleMaterialBuffer = smokeDynamicTriangleMaterialBuffer;
    m_smokeDynamicTriangleMaterialIndexBuffer = smokeDynamicTriangleMaterialIndexBuffer;
    m_smokeMaterialTableBuffer = smokeMaterialTableBuffer;
    m_smokeEmissiveTriangleBuffer = smokeEmissiveTriangleBuffer;
    m_smokeStaticBlasDesc = smokeStaticBlasDesc;
    m_smokeDynamicBlasDesc = smokeDynamicBlasDesc;
    m_smokeStaticBlas = smokeStaticBlas;
    m_smokeDynamicBlas = smokeDynamicBlas;
    if (hasStaticBlas)
    {
        m_smokeStaticBlasCacheValid = true;
        m_smokeStaticBlasSignature = staticSignature.hash;
    }
    m_smokeBindingSet = bindingBuildResult.bindingSet;
    m_smokeTextureDescriptorTable = bindingBuildResult.textureDescriptorTable;
    m_smokeActiveTextureTable = bindingBuildResult.activeTextureTable;
    m_smokeMaterialTableEntryCount = static_cast<int>(materialTable.materials.size());
    m_smokeEmissiveTriangleCount = emissiveInventoryStats.capturedTriangles;
    m_smokeEmissiveStaticTriangleCount = emissiveInventoryStats.staticTriangles;
    m_smokeEmissiveDynamicTriangleCount = emissiveInventoryStats.dynamicTriangles;
    m_smokeSceneBuilt = true;

    const int sceneMs = Sys_Milliseconds() - sceneStartMs;
    if (ShouldLogSmokeTiming(sceneMs, Sys_Milliseconds(), g_smokeLastSceneTimingLogMs))
    {
        common->Printf("PathTracePrimaryPass: RT smoke slow scene build %d ms (capture=%d validate=%d append=%d merge=%d metadata=%d metaValidate=%d metaRegister=%d material=%d emissive=%d bufferCreate=%d bufferSubmit=%d accelSubmit=%d blas=%d tlas=%d) surfaces=%d verts=%d indexes=%d dynamicIndexes=%d skinnedRtCpu=%d(%di) staticCacheHit=%d materialCacheHit=%d materialCache=%d/%d metadataCache=%d metadataFrame=%d/%d/%d/%d/%d metadataRegistry=%d guiTextures=%d/%d/%d additiveDecals=%d lightCount=%d debugMode=%d\n",
            sceneMs,
            captureMs,
            captureTiming.validationMs,
            captureTiming.appendMs,
            captureTiming.bucketMergeMs,
            metadataMs,
            metadataValidationMs,
            metadataRegistrationMs,
            materialMs,
            emissiveMs,
            bufferCreateMs,
            bufferUploadMs,
            accelSubmitMs,
            blasSubmitMs,
            tlasSubmitMs,
            sourceSurfaces,
            sourceVerts,
            sourceIndexes,
            dynamicIndexCount,
            dynamicStats.skinnedRtCpuSkinnedSurfaces,
            dynamicStats.skinnedRtCpuSkinnedIndexes,
            staticBlasCacheHit ? 1 : 0,
            materialTableCacheHit ? 1 : 0,
            materialTableCacheStats.hits,
            materialTableCacheStats.misses,
            r_pathTracingMaterialMetadataCache.GetInteger() != 0 ? 1 : 0,
            g_smokeMaterialMetadataFrameStats.cacheRefreshes,
            g_smokeMaterialMetadataFrameStats.fullDiscovers,
            g_smokeMaterialMetadataFrameStats.newEntries,
            g_smokeMaterialMetadataFrameStats.registrations,
            g_smokeMaterialMetadataFrameStats.duplicateSkips,
            SmokeMaterialTextureRegistrySize(),
            materialTable.guiTextureCandidates,
            materialTable.guiTexturesAccepted,
            materialTable.guiTexturesRejected,
            materialTable.materialsAdditiveDecals,
            r_pathTracingLightCount.GetInteger(),
            requestedDebugMode);
    }

    if (r_pathTracingSmokeLog.GetInteger() != 0 && !m_smokeSceneRebuildLogged)
    {
        common->Printf("PathTracePrimaryPass: rebuilding RT smoke BLAS/TLAS every frame from current visible Doom surfaces (first frame: %d surfaces, %d verts, %d indexes, anchor triangle %d)\n",
            sourceSurfaces, sourceVerts, sourceIndexes, anchorTriangle);
        common->Printf("PathTracePrimaryPass: RT smoke capture buckets static-world=%d rigid-entity=%d skinned=%d particle/alpha=%d unknown=%d\n",
            classStats.staticWorldSurfaces,
            classStats.rigidEntitySurfaces,
            classStats.skinnedDeformedSurfaces,
            classStats.particleAlphaSurfaces,
            classStats.unknownSurfaces);
        common->Printf("PathTracePrimaryPass: RT smoke BLAS split static-world=%d indexes, dynamic-candidate=%d indexes, TLAS instances=%d\n",
            staticIndexCount, dynamicIndexCount, instanceCount);
        common->Printf("PathTracePrimaryPass: RT smoke dynamic geometry rigid=%d(%di) skinnedCpu=%d(%di) skinnedRtCpu=%d(%di) skinnedLikelyBasePose=%d(%di) particle/alpha=%d(%di) unknown=%d(%di)\n",
            dynamicStats.rigidSurfaces, dynamicStats.rigidIndexes,
            dynamicStats.skinnedCpuCurrentSurfaces, dynamicStats.skinnedCpuCurrentIndexes,
            dynamicStats.skinnedRtCpuSkinnedSurfaces, dynamicStats.skinnedRtCpuSkinnedIndexes,
            dynamicStats.skinnedLikelyBasePoseSurfaces, dynamicStats.skinnedLikelyBasePoseIndexes,
            dynamicStats.particleAlphaSurfaces, dynamicStats.particleAlphaIndexes,
            dynamicStats.unknownSurfaces, dynamicStats.unknownIndexes);
        common->Printf("PathTracePrimaryPass: RT smoke static BLAS cache %s signature=%llu cacheSurfaces=%d hits=%d misses=%d\n",
            staticBlasCacheHit ? "hit" : "rebuild",
            static_cast<unsigned long long>(staticSignature.hash),
            static_cast<int>(m_smokeStaticSurfaceKeys.size()),
            m_smokeStaticBlasCacheHitCount,
            m_smokeStaticBlasCacheMissCount);
        common->Printf("PathTracePrimaryPass: RT smoke material table cache %s signature=%llu hits=%d misses=%d materials=%d textures=%d\n",
            materialTableCacheHit ? "hit" : "rebuild",
            static_cast<unsigned long long>(materialTableSignature),
            materialTableCacheStats.hits,
            materialTableCacheStats.misses,
            static_cast<int>(materialTable.materials.size()),
            static_cast<int>(materialTable.diffuseTextures.size()));
        LogSmokeMaterialStats(materialStats);
        LogSmokeMaterialTable(materialTable);
        if (enableTextureProbe)
        {
            LogSmokeTextureProbe(materialTable);
            LogSmokeTextureCoverage(textureCoverageStats);
            LogSmokeMaterialTextureDiscovery(materialTable);
        }
        LogSmokeAttributeStats(attributeStats);
        LogSmokeBucketRanges(bucketRanges);
        m_smokeSceneRebuildLogged = true;
    }

    if (dumpClassReasons)
    {
        common->Printf("PathTracePrimaryPass: RT smoke one-shot surface classification sample dump\n");
        LogSmokeSurfaceClassReasonSamples(reasonSamples);
        r_pathTracingClassDump.SetInteger(0);
    }

    if (r_pathTracingSmokeLog.GetInteger() != 0 && m_smokeSceneLogCooldownFrames <= 0)
    {
        common->Printf("PathTracePrimaryPass: RT smoke scene capture %d surfaces (dynamic cap %d), %d/%d verts, %d/%d indexes, anchor triangle %d; skipped null=%d geo=%d material=%d gui=%d allowGuiSurfaces=%d space=%d model=%d callback=%d skipCallbacks=%d indexes=%d cache=%d limits=%d zeroArea=%d emptyClass=%d; buckets static-world=%d(%dv/%di/%dt) rigid-entity=%d(%dv/%di/%dt) skinned=%d(%dv/%di/%dt) particle/alpha=%d(%dv/%di/%dt) unknown=%d(%dv/%di/%dt)\n",
            sourceSurfaces, RT_SMOKE_MAX_SURFACES,
            sourceVerts, RT_SMOKE_MAX_VERTS,
            sourceIndexes, RT_SMOKE_MAX_INDEXES,
            anchorTriangle,
            skipStats.nullSurface,
            skipStats.missingGeometry,
            skipStats.nullMaterial,
            skipStats.guiSurface,
            r_pathTracingAllowGuiSurfaces.GetInteger() != 0 ? 1 : 0,
            skipStats.nullSpace,
            skipStats.nullModel,
            skipStats.callbackEntity,
            r_pathTracingSkipCallbackEntities.GetInteger() != 0 ? 1 : 0,
            skipStats.invalidIndexCount,
            skipStats.nonCurrentCache,
            skipStats.limitExceeded,
            skipStats.zeroAreaOnly,
            skipStats.emptyClassBuffer,
            classStats.staticWorldSurfaces, classStats.staticWorldVerts, classStats.staticWorldIndexes, classStats.staticWorldTriangles,
            classStats.rigidEntitySurfaces, classStats.rigidEntityVerts, classStats.rigidEntityIndexes, classStats.rigidEntityTriangles,
            classStats.skinnedDeformedSurfaces, classStats.skinnedDeformedVerts, classStats.skinnedDeformedIndexes, classStats.skinnedDeformedTriangles,
            classStats.particleAlphaSurfaces, classStats.particleAlphaVerts, classStats.particleAlphaIndexes, classStats.particleAlphaTriangles,
            classStats.unknownSurfaces, classStats.unknownVerts, classStats.unknownIndexes, classStats.unknownTriangles);
        common->Printf("PathTracePrimaryPass: RT smoke BLAS split static-world=%d indexes, dynamic-candidate=%d indexes, TLAS instances=%d\n",
            staticIndexCount, dynamicIndexCount, instanceCount);
        common->Printf("PathTracePrimaryPass: RT smoke dynamic geometry rigid=%d(%di) skinnedCpu=%d(%di) skinnedRtCpu=%d(%di) skinnedLikelyBasePose=%d(%di) particle/alpha=%d(%di) unknown=%d(%di)\n",
            dynamicStats.rigidSurfaces, dynamicStats.rigidIndexes,
            dynamicStats.skinnedCpuCurrentSurfaces, dynamicStats.skinnedCpuCurrentIndexes,
            dynamicStats.skinnedRtCpuSkinnedSurfaces, dynamicStats.skinnedRtCpuSkinnedIndexes,
            dynamicStats.skinnedLikelyBasePoseSurfaces, dynamicStats.skinnedLikelyBasePoseIndexes,
            dynamicStats.particleAlphaSurfaces, dynamicStats.particleAlphaIndexes,
            dynamicStats.unknownSurfaces, dynamicStats.unknownIndexes);
        common->Printf("PathTracePrimaryPass: RT smoke static BLAS cache %s signature=%llu cacheSurfaces=%d hits=%d misses=%d\n",
            staticBlasCacheHit ? "hit" : "rebuild",
            static_cast<unsigned long long>(staticSignature.hash),
            static_cast<int>(m_smokeStaticSurfaceKeys.size()),
            m_smokeStaticBlasCacheHitCount,
            m_smokeStaticBlasCacheMissCount);
        common->Printf("PathTracePrimaryPass: RT smoke material table cache %s signature=%llu hits=%d misses=%d materials=%d textures=%d\n",
            materialTableCacheHit ? "hit" : "rebuild",
            static_cast<unsigned long long>(materialTableSignature),
            materialTableCacheStats.hits,
            materialTableCacheStats.misses,
            static_cast<int>(materialTable.materials.size()),
            static_cast<int>(materialTable.diffuseTextures.size()));
        LogSmokeMaterialStats(materialStats);
        LogSmokeMaterialTable(materialTable);
        if (enableTextureProbe)
        {
            LogSmokeTextureProbe(materialTable);
            LogSmokeTextureCoverage(textureCoverageStats);
            LogSmokeMaterialTextureDiscovery(materialTable);
        }
        LogSmokeAttributeStats(attributeStats);
        LogSmokeBucketRanges(bucketRanges);
        m_smokeSceneLogCooldownFrames = RT_SMOKE_SCENE_LOG_INTERVAL_FRAMES;
    }
    else
    {
        --m_smokeSceneLogCooldownFrames;
    }
}

void PathTracePrimaryPass::ReadBackRayTracingSmokeTest()
{
    if (r_pathTracingReadbackEnable.GetInteger() == 0)
    {
        m_smokeReadbackQueued = false;
        m_smokeReadbackDelayFrames = 0;
        m_smokeReadbackCooldownFrames = RT_SMOKE_READBACK_INTERVAL_FRAMES;
        return;
    }

    if (!m_smokeReadbackQueued || !m_smokeReadbackTexture)
    {
        if (m_smokeReadbackCooldownFrames > 0)
        {
            --m_smokeReadbackCooldownFrames;
        }
        return;
    }

    if (m_smokeReadbackDelayFrames > 0)
    {
        --m_smokeReadbackDelayFrames;
        return;
    }

    nvrhi::IDevice* device = deviceManager ? deviceManager->GetDevice() : nullptr;
    if (!device)
    {
        return;
    }

    const int readbackStartMs = Sys_Milliseconds();
    device->waitForIdle();
    const int waitForIdleMs = Sys_Milliseconds() - readbackStartMs;

    size_t rowPitch = 0;
    void* readbackData = device->mapStagingTexture(m_smokeReadbackTexture, nvrhi::TextureSlice(), nvrhi::CpuAccessMode::Read, &rowPitch);
    if (!readbackData)
    {
        common->Printf("PathTracePrimaryPass: RT smoke UAV readback map failed\n");
        m_smokeReadbackQueued = false;
        m_smokeReadbackCooldownFrames = RT_SMOKE_READBACK_INTERVAL_FRAMES;
        return;
    }

    const int sampleX = m_smokeOutputWidth / 2;
    const int sampleY = m_smokeOutputHeight / 2;
    const byte* readbackBytes = static_cast<const byte*>(readbackData);
    const float* centerRgba = reinterpret_cast<const float*>(readbackBytes + rowPitch * sampleY + sizeof(float) * 4 * sampleX);

    int greenHits = 0;
    int redMisses = 0;
    for (int y = 0; y < m_smokeOutputHeight; ++y)
    {
        const float* row = reinterpret_cast<const float*>(readbackBytes + rowPitch * y);
        for (int x = 0; x < m_smokeOutputWidth; ++x)
        {
            const float* rgba = row + x * 4;
            if (rgba[1] > 0.5f)
            {
                ++greenHits;
            }
            else if (rgba[0] > 0.5f)
            {
                ++redMisses;
            }
        }
    }

    const int readbackMs = Sys_Milliseconds() - readbackStartMs;
    if (r_pathTracingSmokeLog.GetInteger() != 0 || ShouldLogSmokeTiming(readbackMs, Sys_Milliseconds(), g_smokeLastReadbackTimingLogMs))
    {
        common->Printf("PathTracePrimaryPass: RT smoke UAV readback %dx%d center rgba=(%.3f, %.3f, %.3f, %.3f), hits=%d, misses=%d, rowPitch=%u, total=%d ms, waitForIdle=%d ms\n",
            m_smokeOutputWidth, m_smokeOutputHeight,
            centerRgba[0], centerRgba[1], centerRgba[2], centerRgba[3],
            greenHits, redMisses, static_cast<unsigned int>(rowPitch),
            readbackMs, waitForIdleMs);
    }

    device->unmapStagingTexture(m_smokeReadbackTexture);
    m_smokeReadbackLogged = true;
    m_smokeReadbackQueued = false;
    m_smokeReadbackCooldownFrames = RT_SMOKE_READBACK_INTERVAL_FRAMES;
}

void PathTracePrimaryPass::PresentDebugOutput()
{
    if (!deviceManager)
    {
        return;
    }

    nvrhi::IFramebuffer* targetFramebuffer = deviceManager->GetCurrentFramebuffer();
    if (!targetFramebuffer)
    {
        return;
    }

    BlitDebugOutput(targetFramebuffer, nvrhi::Viewport(renderSystem->GetNativeWidth(), renderSystem->GetNativeHeight()));
}

void PathTracePrimaryPass::BlitDebugOutput(nvrhi::IFramebuffer* targetFramebuffer, const nvrhi::Viewport& targetViewport)
{
    if (!m_smokeTestDispatched || !m_smokeOutputTexture || !m_backend || !targetFramebuffer)
    {
        return;
    }

    nvrhi::ICommandList* commandList = m_backend->GL_GetCommandList();
    if (!commandList)
    {
        return;
    }

    commandList->setTextureState(m_smokeOutputTexture, nvrhi::AllSubresources, nvrhi::ResourceStates::ShaderResource);
    commandList->commitBarriers();

    BlitParameters blitParms;
    blitParms.sourceTexture = m_smokeOutputTexture;
    blitParms.targetFramebuffer = targetFramebuffer;
    blitParms.targetViewport = targetViewport;
    blitParms.sampler = BlitSampler::Point;
    m_backend->GetCommonPasses().BlitTexture(commandList, blitParms, nullptr);
}
