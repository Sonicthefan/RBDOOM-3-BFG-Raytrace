#include "precompiled.h"
#pragma hdrstop

#include "PathTraceDLSSRRBridge.h"
#include "PathTraceCVars.h"
#include "PathTraceFrameResources.h"

#include <nvrhi/vulkan.h>
#if USE_DX12
#include <nvrhi/d3d12.h>
#endif

#include "../../framework/Common_local.h"
#include "../../sys/DeviceManager.h"
#include "../GLMatrix.h"
#include "../RenderCommon.h"

#include <algorithm>

#if RB_PT_STREAMLINE_DLSS_RR
#include <sl_consts.h>
#include <sl.h>
#include <sl_dlss.h>
#include <sl_dlss_d.h>
#include <sl_helpers.h>
#include <sl_helpers_vk.h>

#if USE_DX12
#include <d3d12.h>
#endif
#endif

extern DeviceManager* deviceManager;

namespace
{

bool g_streamlineInitialized = false;
bool g_streamlineDeviceSet = false;
bool g_streamlineEvaluateWarned = false;
bool g_streamlineChecklistWarned = false;
uint32_t g_streamlineLastChecklistPreset = 0xffffffffu;

#if RB_PT_STREAMLINE_DLSS_RR
const char* StreamlineLogTypeName( sl::LogType type )
{
	switch( type )
	{
		case sl::LogType::eInfo: return "info";
		case sl::LogType::eWarn: return "warn";
		case sl::LogType::eError: return "error";
		default: return "unknown";
	}
}

void StreamlineLogCallback( sl::LogType type, const char* msg )
{
	if( !common || !msg )
	{
		return;
	}

	if( type == sl::LogType::eError || type == sl::LogType::eWarn || r_pathTracingDLSSRRVerbose.GetInteger() != 0 )
	{
		common->Printf( "PathTraceDLSSRR: Streamline %s: %s\n", StreamlineLogTypeName( type ), msg );
	}
}

void DumpFeatureRequirements( sl::Feature feature, const char* label )
{
	sl::FeatureRequirements requirements{};
	const sl::Result result = slGetFeatureRequirements( feature, requirements );
	if( result != sl::Result::eOk )
	{
		common->Printf( "PathTraceDLSSRR: %s requirements unavailable: %s\n", label, sl::getResultAsStr( result ) );
		return;
	}

	common->Printf(
		"PathTraceDLSSRR: %s requirements flags=0x%08x cpuThreads=%u viewports=%u tags=%u vkDeviceExt=%u vkInstanceExt=%u vkGraphicsQueues=%u vkComputeQueues=%u\n",
		label,
		static_cast<unsigned int>( requirements.flags ),
		requirements.maxNumCPUThreads,
		requirements.maxNumViewports,
		requirements.numRequiredTags,
		requirements.vkNumDeviceExtensions,
		requirements.vkNumInstanceExtensions,
		requirements.vkNumGraphicsQueuesRequired,
		requirements.vkNumComputeQueuesRequired );

	if( r_pathTracingDLSSRRVerbose.GetInteger() != 0 )
	{
		for( uint32_t i = 0; i < requirements.numRequiredTags; ++i )
		{
			common->Printf( "PathTraceDLSSRR: %s requiredTag[%u]=%s\n", label, i, sl::getBufferTypeAsStr( requirements.requiredTags[i] ) );
		}
		for( uint32_t i = 0; i < requirements.vkNumDeviceExtensions; ++i )
		{
			common->Printf( "PathTraceDLSSRR: %s vkDeviceExt[%u]=%s\n", label, i, requirements.vkDeviceExtensions[i] );
		}
		for( uint32_t i = 0; i < requirements.vkNumInstanceExtensions; ++i )
		{
			common->Printf( "PathTraceDLSSRR: %s vkInstanceExt[%u]=%s\n", label, i, requirements.vkInstanceExtensions[i] );
		}
	}
}

void DumpFeatureVersion( sl::Feature feature, const char* label )
{
	sl::FeatureVersion version{};
	const sl::Result result = slGetFeatureVersion( feature, version );
	if( result != sl::Result::eOk )
	{
		common->Printf( "PathTraceDLSSRR: %s version unavailable: %s\n", label, sl::getResultAsStr( result ) );
		return;
	}

	common->Printf(
		"PathTraceDLSSRR: %s version SL=%u.%u.%u NGX=%u.%u.%u\n",
		label,
		version.versionSL.major,
		version.versionSL.minor,
		version.versionSL.build,
		version.versionNGX.major,
		version.versionNGX.minor,
		version.versionNGX.build );
}

void DumpFeatureSupport( sl::Feature feature, const char* label, const sl::AdapterInfo& adapterInfo )
{
	const sl::Result result = slIsFeatureSupported( feature, adapterInfo );
	common->Printf( "PathTraceDLSSRR: %s support=%s\n", label, sl::getResultAsStr( result ) );
}
#endif

bool StreamlineProbeRequested()
{
	return r_pathTracingDLSSRRProbe.GetInteger() != 0 || r_pathTracingDLSSRR.GetInteger() != 0;
}

#if RB_PT_STREAMLINE_DLSS_RR
sl::float4x4 StreamlineIdentityMatrix()
{
	sl::float4x4 matrix{};
	matrix.setRow( 0, sl::float4( 1.0f, 0.0f, 0.0f, 0.0f ) );
	matrix.setRow( 1, sl::float4( 0.0f, 1.0f, 0.0f, 0.0f ) );
	matrix.setRow( 2, sl::float4( 0.0f, 0.0f, 1.0f, 0.0f ) );
	matrix.setRow( 3, sl::float4( 0.0f, 0.0f, 0.0f, 1.0f ) );
	return matrix;
}

void CopyIdTechMatrixToStreamline( const float* source, sl::float4x4& target )
{
	if( !source )
	{
		target = StreamlineIdentityMatrix();
		return;
	}

	for( uint32_t row = 0; row < 4; ++row )
	{
		target.setRow(
			row,
			sl::float4(
				source[row * 4 + 0],
				source[row * 4 + 1],
				source[row * 4 + 2],
				source[row * 4 + 3] ) );
	}
}

void CopyIdTechWorldCameraMatricesToStreamline( const viewDef_t* viewDef, sl::float4x4& worldToCameraView, sl::float4x4& cameraViewToWorld )
{
	if( !viewDef )
	{
		worldToCameraView = StreamlineIdentityMatrix();
		cameraViewToWorld = StreamlineIdentityMatrix();
		return;
	}

	CopyIdTechMatrixToStreamline( viewDef->worldSpace.modelViewMatrix, worldToCameraView );
	float inverseModelView[16];
	R_MatrixFullInverse( viewDef->worldSpace.modelViewMatrix, inverseModelView );
	CopyIdTechMatrixToStreamline( inverseModelView, cameraViewToWorld );
}

void BuildPathTraceProjectionMatrix( float tanX, float tanY, float zNear, float projectionMatrix[16] )
{
	std::fill( projectionMatrix, projectionMatrix + 16, 0.0f );
	projectionMatrix[0 * 4 + 0] = 1.0f / Max( tanX, 1.0e-6f );
	projectionMatrix[1 * 4 + 1] = 1.0f / Max( tanY, 1.0e-6f );
	projectionMatrix[2 * 4 + 2] = -0.999f;
	projectionMatrix[3 * 4 + 2] = -zNear;
	projectionMatrix[2 * 4 + 3] = -1.0f;
}

void BuildPathTraceModelViewMatrix( const RtPathTraceFrameCameraState& camera, float modelViewMatrix[16] )
{
	const float flipMatrix[16] =
	{
		0.0f, 0.0f, -1.0f, 0.0f,
		-1.0f, 0.0f, 0.0f, 0.0f,
		0.0f, 1.0f, 0.0f, 0.0f,
		0.0f, 0.0f, 0.0f, 1.0f
	};
	float viewerMatrix[16] = {};
	const idVec3& origin = camera.origin;
	const idVec3& forward = camera.forward;
	const idVec3& left = camera.left;
	const idVec3& up = camera.up;

	viewerMatrix[0 * 4 + 0] = forward.x;
	viewerMatrix[1 * 4 + 0] = forward.y;
	viewerMatrix[2 * 4 + 0] = forward.z;
	viewerMatrix[3 * 4 + 0] = -origin.x * forward.x - origin.y * forward.y - origin.z * forward.z;
	viewerMatrix[0 * 4 + 1] = left.x;
	viewerMatrix[1 * 4 + 1] = left.y;
	viewerMatrix[2 * 4 + 1] = left.z;
	viewerMatrix[3 * 4 + 1] = -origin.x * left.x - origin.y * left.y - origin.z * left.z;
	viewerMatrix[0 * 4 + 2] = up.x;
	viewerMatrix[1 * 4 + 2] = up.y;
	viewerMatrix[2 * 4 + 2] = up.z;
	viewerMatrix[3 * 4 + 2] = -origin.x * up.x - origin.y * up.y - origin.z * up.z;
	viewerMatrix[3 * 4 + 3] = 1.0f;
	R_MatrixMultiply( viewerMatrix, flipMatrix, modelViewMatrix );
}

bool BuildPathTraceClipTransforms(
	const viewDef_t* viewDef,
	const RtPathTraceFrameCameraState* previousCamera,
	bool resetHistory,
	float clipToCameraView[16],
	float clipToPrevClip[16],
	float prevClipToClip[16] )
{
	if( !viewDef )
	{
		std::fill( clipToCameraView, clipToCameraView + 16, 0.0f );
		return false;
	}

	R_MatrixFullInverse( viewDef->unjitteredProjectionMatrix, clipToCameraView );
	if( resetHistory || previousCamera == nullptr || !previousCamera->valid )
	{
		return false;
	}

	float currentCameraToWorld[16];
	R_MatrixFullInverse( viewDef->worldSpace.modelViewMatrix, currentCameraToWorld );
	float previousWorldToCamera[16];
	BuildPathTraceModelViewMatrix( *previousCamera, previousWorldToCamera );
	float previousViewToClip[16];
	BuildPathTraceProjectionMatrix( previousCamera->tanX, previousCamera->tanY, r_znear.GetFloat(), previousViewToClip );

	float clipToWorld[16];
	float clipToPreviousCamera[16];
	R_MatrixMultiply( clipToCameraView, currentCameraToWorld, clipToWorld );
	R_MatrixMultiply( clipToWorld, previousWorldToCamera, clipToPreviousCamera );
	R_MatrixMultiply( clipToPreviousCamera, previousViewToClip, clipToPrevClip );
	R_MatrixFullInverse( clipToPrevClip, prevClipToClip );
	return true;
}

sl::CommandBuffer* GetStreamlineCommandBuffer( nvrhi::ICommandList* commandList )
{
	if( !commandList || !deviceManager )
	{
		return nullptr;
	}

	if( deviceManager->GetGraphicsAPI() == nvrhi::GraphicsAPI::VULKAN )
	{
		return ( sl::CommandBuffer* )commandList->getNativeObject( nvrhi::ObjectTypes::VK_CommandBuffer );
	}

#if USE_DX12
	if( deviceManager->GetGraphicsAPI() == nvrhi::GraphicsAPI::D3D12 )
	{
		return ( sl::CommandBuffer* )commandList->getNativeObject( nvrhi::ObjectTypes::D3D12_GraphicsCommandList );
	}
#endif

	return nullptr;
}

sl::Resource BuildStreamlineTextureResource( nvrhi::ITexture* texture, uint32_t state )
{
	if( !texture || !deviceManager )
	{
		return sl::Resource( sl::ResourceType::eTex2d, nullptr, state );
	}

	const nvrhi::TextureDesc& desc = texture->getDesc();
	if( deviceManager->GetGraphicsAPI() == nvrhi::GraphicsAPI::VULKAN )
	{
		auto nativeImage = texture->getNativeObject( nvrhi::ObjectTypes::VK_Image );
		auto nativeMemory = texture->getNativeObject( nvrhi::ObjectTypes::VK_DeviceMemory );
		auto nativeView = texture->getNativeView(
			nvrhi::ObjectTypes::VK_ImageView,
			nvrhi::Format::UNKNOWN,
			nvrhi::TextureSubresourceSet( 0, 1, 0, 1 ),
			nvrhi::TextureDimension::Texture2D );
		sl::Resource resource( sl::ResourceType::eTex2d, nativeImage, nativeMemory, nativeView, state );
		resource.width = desc.width;
		resource.height = desc.height;
		resource.nativeFormat = static_cast<uint32_t>( nvrhi::vulkan::convertFormat( desc.format ) );
		resource.mipLevels = desc.mipLevels;
		resource.arrayLayers = desc.arraySize;
		return resource;
	}

#if USE_DX12
	if( deviceManager->GetGraphicsAPI() == nvrhi::GraphicsAPI::D3D12 )
	{
		auto nativeResource = texture->getNativeObject( nvrhi::ObjectTypes::D3D12_Resource );
		sl::Resource resource( sl::ResourceType::eTex2d, nativeResource, state );
		resource.width = desc.width;
		resource.height = desc.height;
		resource.mipLevels = desc.mipLevels;
		resource.arrayLayers = desc.arraySize;
		return resource;
	}
#endif

	return sl::Resource( sl::ResourceType::eTex2d, nullptr, state );
}

uint32_t StreamlineShaderResourceState()
{
	if( deviceManager && deviceManager->GetGraphicsAPI() == nvrhi::GraphicsAPI::VULKAN )
	{
		return static_cast<uint32_t>( VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL );
	}

#if USE_DX12
	return D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
#else
	return 0;
#endif
}

uint32_t StreamlineUnorderedAccessState()
{
	if( deviceManager && deviceManager->GetGraphicsAPI() == nvrhi::GraphicsAPI::VULKAN )
	{
		return static_cast<uint32_t>( VK_IMAGE_LAYOUT_GENERAL );
	}

#if USE_DX12
	return D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
#else
	return 0;
#endif
}

sl::DLSSDPreset PathTraceDLSSRRDenoiserPreset()
{
	switch( idMath::ClampInt( 0, 5, r_pathTracingDLSSRRDenoiserPreset.GetInteger() ) )
	{
		case 4: return sl::DLSSDPreset::ePresetD;
		case 5: return sl::DLSSDPreset::ePresetE;
		default: return sl::DLSSDPreset::eDefault;
	}
}

const char* PathTraceDLSSRRDenoiserPresetName( sl::DLSSDPreset preset )
{
	switch( preset )
	{
		case sl::DLSSDPreset::ePresetD: return "D";
		case sl::DLSSDPreset::ePresetE: return "E";
		default: return "default";
	}
}
#endif

} // namespace

void PathTraceDLSSRRBridge_Init( nvrhi::GraphicsAPI api )
{
	if( g_streamlineInitialized || !StreamlineProbeRequested() )
	{
		return;
	}

#if RB_PT_STREAMLINE_DLSS_RR
	static const sl::Feature features[] =
	{
		sl::kFeatureDLSS,
		sl::kFeatureDLSS_RR
	};

	sl::Preferences preferences{};
	preferences.showConsole = r_pathTracingDLSSRRVerbose.GetInteger() != 0;
	preferences.logLevel = r_pathTracingDLSSRRVerbose.GetInteger() != 0 ? sl::LogLevel::eVerbose : sl::LogLevel::eDefault;
	preferences.logMessageCallback = StreamlineLogCallback;
	preferences.flags = sl::PreferenceFlags::eDisableCLStateTracking | sl::PreferenceFlags::eDisableDebugText | sl::PreferenceFlags::eUseManualHooking | sl::PreferenceFlags::eUseFrameBasedResourceTagging;
	preferences.featuresToLoad = features;
	preferences.numFeaturesToLoad = static_cast<uint32_t>( std::size( features ) );
	preferences.engine = sl::EngineType::eCustom;
	preferences.engineVersion = "rbdoom3bfg-rt";
	preferences.projectId = "0f1fd5d4-b456-4c8c-9c08-9de33599b1a6";
	preferences.renderAPI = api == nvrhi::GraphicsAPI::VULKAN ? sl::RenderAPI::eVulkan : sl::RenderAPI::eD3D12;

	const sl::Result result = slInit( preferences, sl::kSDKVersion );
	common->Printf( "PathTraceDLSSRR: slInit api=%s result=%s\n", api == nvrhi::GraphicsAPI::VULKAN ? "vulkan" : "d3d12", sl::getResultAsStr( result ) );
	if( result != sl::Result::eOk )
	{
		return;
	}

	g_streamlineInitialized = true;
	DumpFeatureRequirements( sl::kFeatureDLSS, "DLSS" );
	DumpFeatureRequirements( sl::kFeatureDLSS_RR, "DLSS_RR" );
#else
	common->Printf( "PathTraceDLSSRR: Streamline SDK support was not compiled into this executable\n" );
#endif
}

void PathTraceDLSSRRBridge_AppendVulkanRequirements( DeviceCreationParameters& deviceParams )
{
	if( !g_streamlineInitialized )
	{
		return;
	}

#if RB_PT_STREAMLINE_DLSS_RR && USE_VK
	const sl::Feature features[] =
	{
		sl::kFeatureDLSS,
		sl::kFeatureDLSS_RR
	};

	for( sl::Feature feature : features )
	{
		sl::FeatureRequirements requirements{};
		const sl::Result result = slGetFeatureRequirements( feature, requirements );
		if( result != sl::Result::eOk )
		{
			common->Printf( "PathTraceDLSSRR: cannot append Vulkan requirements for %s: %s\n", sl::getFeatureAsStr( feature ), sl::getResultAsStr( result ) );
			continue;
		}

		for( uint32_t i = 0; i < requirements.vkNumInstanceExtensions; ++i )
		{
			deviceParams.requiredVulkanInstanceExtensions.push_back( requirements.vkInstanceExtensions[i] );
			if( r_pathTracingDLSSRRVerbose.GetInteger() != 0 )
			{
				common->Printf( "PathTraceDLSSRR: requiring %s Vulkan instance extension %s\n", sl::getFeatureAsStr( feature ), requirements.vkInstanceExtensions[i] );
			}
		}

		for( uint32_t i = 0; i < requirements.vkNumDeviceExtensions; ++i )
		{
			deviceParams.requiredVulkanDeviceExtensions.push_back( requirements.vkDeviceExtensions[i] );
			if( r_pathTracingDLSSRRVerbose.GetInteger() != 0 )
			{
				common->Printf( "PathTraceDLSSRR: requiring %s Vulkan device extension %s\n", sl::getFeatureAsStr( feature ), requirements.vkDeviceExtensions[i] );
			}
		}
	}
#endif
}

void PathTraceDLSSRRBridge_SetDevice( DeviceManager* deviceManager )
{
	if( g_streamlineDeviceSet || !g_streamlineInitialized || deviceManager == nullptr || deviceManager->GetDevice() == nullptr )
	{
		return;
	}

#if RB_PT_STREAMLINE_DLSS_RR
	sl::AdapterInfo adapterInfo{};

	if( deviceManager->GetGraphicsAPI() == nvrhi::GraphicsAPI::D3D12 )
	{
#if USE_DX12
		ID3D12Device* nativeDevice = static_cast<ID3D12Device*>( deviceManager->GetDevice()->getNativeObject( nvrhi::ObjectTypes::D3D12_Device ) );
		if( nativeDevice == nullptr )
		{
			common->Printf( "PathTraceDLSSRR: D3D12 native device unavailable for slSetD3DDevice\n" );
			return;
		}

		const sl::Result result = slSetD3DDevice( nativeDevice );
		common->Printf( "PathTraceDLSSRR: slSetD3DDevice result=%s\n", sl::getResultAsStr( result ) );
		if( result != sl::Result::eOk )
		{
			return;
		}

		LUID adapterLuid = nativeDevice->GetAdapterLuid();
		adapterInfo.deviceLUID = reinterpret_cast<uint8_t*>( &adapterLuid );
		adapterInfo.deviceLUIDSizeInBytes = sizeof( adapterLuid );
#else
		common->Printf( "PathTraceDLSSRR: D3D12 Streamline device hook requested but USE_DX12 is disabled\n" );
		return;
#endif
	}
	else if( deviceManager->GetGraphicsAPI() == nvrhi::GraphicsAPI::VULKAN )
	{
#if USE_VK
		VkInstance instance = static_cast<VkInstance>( deviceManager->GetDevice()->getNativeObject( nvrhi::ObjectTypes::VK_Instance ) );
		VkPhysicalDevice physicalDevice = static_cast<VkPhysicalDevice>( deviceManager->GetDevice()->getNativeObject( nvrhi::ObjectTypes::VK_PhysicalDevice ) );
		VkDevice device = static_cast<VkDevice>( deviceManager->GetDevice()->getNativeObject( nvrhi::ObjectTypes::VK_Device ) );
		const int graphicsFamily = deviceManager->GetGraphicsFamilyIndex();
		if( instance == VK_NULL_HANDLE || physicalDevice == VK_NULL_HANDLE || device == VK_NULL_HANDLE || graphicsFamily < 0 )
		{
			common->Printf( "PathTraceDLSSRR: Vulkan native handles unavailable for slSetVulkanInfo\n" );
			return;
		}

		sl::VulkanInfo vulkanInfo{};
		vulkanInfo.instance = instance;
		vulkanInfo.physicalDevice = physicalDevice;
		vulkanInfo.device = device;
		vulkanInfo.graphicsQueueFamily = static_cast<uint32_t>( graphicsFamily );
		vulkanInfo.graphicsQueueIndex = 0;
		vulkanInfo.computeQueueFamily = static_cast<uint32_t>( graphicsFamily );
		vulkanInfo.computeQueueIndex = 0;

		const sl::Result result = slSetVulkanInfo( vulkanInfo );
		common->Printf( "PathTraceDLSSRR: slSetVulkanInfo result=%s graphicsFamily=%d\n", sl::getResultAsStr( result ), graphicsFamily );
		if( result != sl::Result::eOk )
		{
			return;
		}

		adapterInfo.vkPhysicalDevice = physicalDevice;
#else
		common->Printf( "PathTraceDLSSRR: Vulkan Streamline device hook requested but USE_VK is disabled\n" );
		return;
#endif
	}
	else
	{
		common->Printf( "PathTraceDLSSRR: unsupported graphics API for Streamline device hook\n" );
		return;
	}

	g_streamlineDeviceSet = true;
	DumpFeatureVersion( sl::kFeatureDLSS, "DLSS" );
	DumpFeatureVersion( sl::kFeatureDLSS_RR, "DLSS_RR" );
	DumpFeatureSupport( sl::kFeatureDLSS, "DLSS", adapterInfo );
	DumpFeatureSupport( sl::kFeatureDLSS_RR, "DLSS_RR", adapterInfo );
#endif
}

bool PathTraceDLSSRRBridge_Evaluate(
	nvrhi::ICommandList* commandList,
	nvrhi::ITexture* inputColor,
	nvrhi::ITexture* outputColor,
	nvrhi::ITexture* albedo,
	nvrhi::ITexture* specularAlbedo,
	nvrhi::ITexture* normalRoughness,
	nvrhi::ITexture* linearDepth,
	nvrhi::ITexture* motionVectors,
	nvrhi::ITexture* specularHitDistance,
	nvrhi::ITexture* disocclusionMask,
	const viewDef_t* viewDef,
	uint32_t frameIndex,
	int width,
	int height,
	float jitterOffsetX,
	float jitterOffsetY,
	const RtPathTraceFrameCameraState* previousCamera,
	bool resetHistory )
{
	if( r_pathTracingDLSSRR.GetInteger() == 0 )
	{
		return false;
	}

	if( !commandList || !inputColor || !outputColor || !albedo || !specularAlbedo || !normalRoughness || !linearDepth || !motionVectors || !viewDef || width <= 0 || height <= 0 )
	{
		if( !g_streamlineEvaluateWarned )
		{
			common->Printf( "PathTraceDLSSRR: evaluate skipped because required DLSS RR resources are missing\n" );
			g_streamlineEvaluateWarned = true;
		}
		return false;
	}

#if RB_PT_STREAMLINE_DLSS_RR
	if( !g_streamlineDeviceSet )
	{
		if( !g_streamlineEvaluateWarned )
		{
			common->Printf( "PathTraceDLSSRR: evaluate skipped because Streamline device setup is incomplete\n" );
			g_streamlineEvaluateWarned = true;
		}
		return false;
	}

	sl::CommandBuffer* nativeCommandBuffer = GetStreamlineCommandBuffer( commandList );
	if( !nativeCommandBuffer )
	{
		if( !g_streamlineEvaluateWarned )
		{
			common->Printf( "PathTraceDLSSRR: evaluate skipped because native command buffer is unavailable\n" );
			g_streamlineEvaluateWarned = true;
		}
		return false;
	}

	sl::FrameToken* frameToken = nullptr;
	sl::Result result = slGetNewFrameToken( frameToken, &frameIndex );
	if( result != sl::Result::eOk || frameToken == nullptr )
	{
		common->Printf( "PathTraceDLSSRR: slGetNewFrameToken failed: %s\n", sl::getResultAsStr( result ) );
		return false;
	}

	sl::ViewportHandle viewport( 0 );
	sl::Constants constants{};
	float clipToCameraView[16];
	float clipToPrevClip[16];
	float prevClipToClip[16];
	const bool validClipHistory = BuildPathTraceClipTransforms(
		viewDef,
		previousCamera,
		resetHistory,
		clipToCameraView,
		clipToPrevClip,
		prevClipToClip );
	CopyIdTechMatrixToStreamline( viewDef->unjitteredProjectionMatrix, constants.cameraViewToClip );
	CopyIdTechMatrixToStreamline( clipToCameraView, constants.clipToCameraView );
	constants.clipToLensClip = StreamlineIdentityMatrix();
	if( validClipHistory )
	{
		CopyIdTechMatrixToStreamline( clipToPrevClip, constants.clipToPrevClip );
		CopyIdTechMatrixToStreamline( prevClipToClip, constants.prevClipToClip );
	}
	else
	{
		constants.clipToPrevClip = StreamlineIdentityMatrix();
		constants.prevClipToClip = StreamlineIdentityMatrix();
	}
	constants.jitterOffset = sl::float2( jitterOffsetX, jitterOffsetY );
	constants.mvecScale = sl::float2( 1.0f / static_cast<float>( width ), 1.0f / static_cast<float>( height ) );
	constants.cameraPinholeOffset = sl::float2( 0.0f, 0.0f );
	constants.cameraPos = sl::float3( viewDef->renderView.vieworg.x, viewDef->renderView.vieworg.y, viewDef->renderView.vieworg.z );
	constants.cameraFwd = sl::float3( viewDef->renderView.viewaxis[0].x, viewDef->renderView.viewaxis[0].y, viewDef->renderView.viewaxis[0].z );
	constants.cameraRight = sl::float3( -viewDef->renderView.viewaxis[1].x, -viewDef->renderView.viewaxis[1].y, -viewDef->renderView.viewaxis[1].z );
	constants.cameraUp = sl::float3( viewDef->renderView.viewaxis[2].x, viewDef->renderView.viewaxis[2].y, viewDef->renderView.viewaxis[2].z );
	constants.cameraNear = r_znear.GetFloat();
	constants.cameraFar = 100000.0f;
	constants.cameraFOV = DEG2RAD( viewDef->renderView.fov_y );
	constants.cameraAspectRatio = static_cast<float>( width ) / static_cast<float>( height );
	constants.motionVectorsInvalidValue = 0.0f;
	constants.depthInverted = sl::Boolean::eFalse;
	constants.cameraMotionIncluded = sl::Boolean::eTrue;
	constants.motionVectors3D = sl::Boolean::eFalse;
	constants.reset = resetHistory ? sl::Boolean::eTrue : sl::Boolean::eFalse;
	constants.motionVectorsDilated = sl::Boolean::eFalse;
	constants.motionVectorsJittered = sl::Boolean::eFalse;

	result = slSetConstants( constants, *frameToken, viewport );
	if( result != sl::Result::eOk )
	{
		common->Printf( "PathTraceDLSSRR: slSetConstants failed: %s\n", sl::getResultAsStr( result ) );
		return false;
	}

	sl::DLSSDOptions options{};
	options.mode = sl::DLSSMode::eDLAA;
	options.outputWidth = static_cast<uint32_t>( width );
	options.outputHeight = static_cast<uint32_t>( height );
	options.sharpness = idMath::ClampFloat( 0.0f, 1.0f, r_pathTracingDLSSRRSharpness.GetFloat() );
	options.preExposure = idMath::ClampFloat( 0.0001f, 1024.0f, r_pathTracingDLSSRRPreExposure.GetFloat() );
	options.exposureScale = idMath::ClampFloat( 0.0001f, 1024.0f, r_pathTracingDLSSRRExposureScale.GetFloat() );
	options.colorBuffersHDR = r_pathTracingDLSSRRColorBuffersHDR.GetInteger() != 0 ? sl::Boolean::eTrue : sl::Boolean::eFalse;
	options.normalRoughnessMode = sl::DLSSDNormalRoughnessMode::ePacked;
	CopyIdTechWorldCameraMatricesToStreamline( viewDef, options.worldToCameraView, options.cameraViewToWorld );
	options.alphaUpscalingEnabled = sl::Boolean::eFalse;
	const sl::DLSSDPreset rrDenoiserPreset = PathTraceDLSSRRDenoiserPreset();
	options.dlaaPreset = rrDenoiserPreset;
	options.qualityPreset = rrDenoiserPreset;
	options.balancedPreset = rrDenoiserPreset;
	options.performancePreset = rrDenoiserPreset;
	options.ultraPerformancePreset = rrDenoiserPreset;
	options.ultraQualityPreset = rrDenoiserPreset;

	const uint32_t rrDenoiserPresetValue = static_cast<uint32_t>( rrDenoiserPreset );
	if( r_pathTracingDLSSRRVerbose.GetInteger() != 0 &&
		( !g_streamlineChecklistWarned || g_streamlineLastChecklistPreset != rrDenoiserPresetValue ) )
	{
		common->Printf(
			"PathTraceDLSSRR: checklist status projectId=%s dlss+rr=loaded linearDepth=tagged invertedDepth=0 preset=%s(%u) jitter=%s clipHistory=%d specularMotion=missing disocclusion=not-tagged-until-float-mask mipBias=not-controlled-by-bridge\n",
			"0f1fd5d4-b456-4c8c-9c08-9de33599b1a6",
			PathTraceDLSSRRDenoiserPresetName( rrDenoiserPreset ),
			static_cast<unsigned int>( rrDenoiserPresetValue ),
			( idMath::Fabs( jitterOffsetX ) > 1.0e-6f || idMath::Fabs( jitterOffsetY ) > 1.0e-6f ) ? "pathtrace-primary-rays-jittered" : "zero-offset",
			validClipHistory ? 1 : 0 );
		g_streamlineChecklistWarned = true;
		g_streamlineLastChecklistPreset = rrDenoiserPresetValue;
	}

	result = slDLSSDSetOptions( viewport, options );
	if( result != sl::Result::eOk )
	{
		common->Printf( "PathTraceDLSSRR: slDLSSDSetOptions failed: %s\n", sl::getResultAsStr( result ) );
		return false;
	}

	const uint32_t shaderResourceState = StreamlineShaderResourceState();
	const uint32_t unorderedAccessState = StreamlineUnorderedAccessState();
	sl::Resource inputColorResource = BuildStreamlineTextureResource( inputColor, shaderResourceState );
	sl::Resource outputColorResource = BuildStreamlineTextureResource( outputColor, unorderedAccessState );
	sl::Resource albedoResource = BuildStreamlineTextureResource( albedo, shaderResourceState );
	sl::Resource specularAlbedoResource = BuildStreamlineTextureResource( specularAlbedo, shaderResourceState );
	sl::Resource normalRoughnessResource = BuildStreamlineTextureResource( normalRoughness, shaderResourceState );
	sl::Resource linearDepthResource = BuildStreamlineTextureResource( linearDepth, shaderResourceState );
	sl::Resource motionVectorResource = BuildStreamlineTextureResource( motionVectors, shaderResourceState );
	sl::Resource specularHitDistanceResource{};
	sl::Resource disocclusionMaskResource{};
	if( specularHitDistance )
	{
		specularHitDistanceResource = BuildStreamlineTextureResource( specularHitDistance, shaderResourceState );
	}
	if( disocclusionMask )
	{
		disocclusionMaskResource = BuildStreamlineTextureResource( disocclusionMask, shaderResourceState );
	}
	sl::Extent extent{};
	extent.width = static_cast<uint32_t>( width );
	extent.height = static_cast<uint32_t>( height );

	sl::ResourceTag tags[9];
	uint32_t tagCount = 0;
	tags[tagCount++] = sl::ResourceTag( &inputColorResource, sl::kBufferTypeScalingInputColor, sl::ResourceLifecycle::eValidUntilEvaluate, &extent );
	tags[tagCount++] = sl::ResourceTag( &outputColorResource, sl::kBufferTypeScalingOutputColor, sl::ResourceLifecycle::eValidUntilEvaluate, &extent );
	tags[tagCount++] = sl::ResourceTag( &albedoResource, sl::kBufferTypeAlbedo, sl::ResourceLifecycle::eValidUntilEvaluate, &extent );
	tags[tagCount++] = sl::ResourceTag( &specularAlbedoResource, sl::kBufferTypeSpecularAlbedo, sl::ResourceLifecycle::eValidUntilEvaluate, &extent );
	tags[tagCount++] = sl::ResourceTag( &normalRoughnessResource, sl::kBufferTypeNormalRoughness, sl::ResourceLifecycle::eValidUntilEvaluate, &extent );
	tags[tagCount++] = sl::ResourceTag( &linearDepthResource, sl::kBufferTypeLinearDepth, sl::ResourceLifecycle::eValidUntilEvaluate, &extent );
	tags[tagCount++] = sl::ResourceTag( &motionVectorResource, sl::kBufferTypeMotionVectors, sl::ResourceLifecycle::eValidUntilEvaluate, &extent );
	if( specularHitDistance )
	{
		tags[tagCount++] = sl::ResourceTag( &specularHitDistanceResource, sl::kBufferTypeSpecularHitDistance, sl::ResourceLifecycle::eValidUntilEvaluate, &extent );
	}
	if( disocclusionMask )
	{
		tags[tagCount++] = sl::ResourceTag( &disocclusionMaskResource, sl::kBufferTypeDisocclusionMask, sl::ResourceLifecycle::eValidUntilEvaluate, &extent );
	}

	result = slSetTagForFrame( *frameToken, viewport, tags, tagCount, nativeCommandBuffer );
	if( result != sl::Result::eOk )
	{
		common->Printf( "PathTraceDLSSRR: slSetTagForFrame failed: %s\n", sl::getResultAsStr( result ) );
		return false;
	}

	const sl::BaseStructure* evaluateInputs[] =
	{
		&viewport
	};
	result = slEvaluateFeature( sl::kFeatureDLSS_RR, *frameToken, evaluateInputs, static_cast<uint32_t>( std::size( evaluateInputs ) ), nativeCommandBuffer );
	if( result != sl::Result::eOk )
	{
		common->Printf( "PathTraceDLSSRR: slEvaluateFeature(DLSS_RR) failed: %s\n", sl::getResultAsStr( result ) );
		return false;
	}

	if( r_pathTracingDLSSRRVerbose.GetInteger() != 0 )
	{
		common->Printf( "PathTraceDLSSRR: evaluated DLSS_RR frame=%u output=%dx%d reset=%d hdr=%d preExposure=%.4f exposureScale=%.4f sharpness=%.3f jitter=%.4f,%.4f clipHistory=%d specHit=%d disocclusion=%d\n",
			frameIndex,
			width,
			height,
			resetHistory ? 1 : 0,
			options.colorBuffersHDR == sl::Boolean::eTrue ? 1 : 0,
			options.preExposure,
			options.exposureScale,
			options.sharpness,
			jitterOffsetX,
			jitterOffsetY,
			validClipHistory ? 1 : 0,
			specularHitDistance ? 1 : 0,
			disocclusionMask ? 1 : 0 );
	}
	return true;
#else
	if( !g_streamlineEvaluateWarned )
	{
		common->Printf( "PathTraceDLSSRR: evaluate requested but Streamline SDK support was not compiled in\n" );
		g_streamlineEvaluateWarned = true;
	}
	return false;
#endif
}

void PathTraceDLSSRRBridge_Shutdown()
{
#if RB_PT_STREAMLINE_DLSS_RR
	if( g_streamlineInitialized )
	{
		const sl::Result result = slShutdown();
		common->Printf( "PathTraceDLSSRR: slShutdown result=%s\n", sl::getResultAsStr( result ) );
	}
#endif

	g_streamlineInitialized = false;
	g_streamlineDeviceSet = false;
	g_streamlineEvaluateWarned = false;
}
