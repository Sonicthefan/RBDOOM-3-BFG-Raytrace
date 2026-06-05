#pragma once

#include <nvrhi/nvrhi.h>

class DeviceManager;
struct DeviceCreationParameters;
struct RtPathTraceFrameCameraState;
struct viewDef_t;

void PathTraceDLSSRRBridge_Init( nvrhi::GraphicsAPI api );
void PathTraceDLSSRRBridge_AppendVulkanRequirements( DeviceCreationParameters& deviceParams );
void PathTraceDLSSRRBridge_SetDevice( DeviceManager* deviceManager );
bool PathTraceDLSSRRBridge_Evaluate(
	nvrhi::ICommandList* commandList,
	nvrhi::ITexture* inputColor,
	nvrhi::ITexture* outputColor,
	nvrhi::ITexture* albedo,
	nvrhi::ITexture* specularAlbedo,
	nvrhi::ITexture* normalRoughness,
	nvrhi::ITexture* position,
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
	bool resetHistory );
void PathTraceDLSSRRBridge_Shutdown();
