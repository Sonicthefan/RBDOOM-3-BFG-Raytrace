#pragma once

#include <nvrhi/nvrhi.h>

class DeviceManager;
struct DeviceCreationParameters;
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
	nvrhi::ITexture* linearDepth,
	nvrhi::ITexture* motionVectors,
	nvrhi::ITexture* specularHitDistance,
	const viewDef_t* viewDef,
	uint32_t frameIndex,
	int width,
	int height,
	bool resetHistory );
void PathTraceDLSSRRBridge_Shutdown();
