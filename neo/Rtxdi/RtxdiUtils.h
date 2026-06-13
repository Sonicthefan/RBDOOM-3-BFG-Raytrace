#pragma once

// rbdoom-owned host RTXDI utility ABI.
//
// This intentionally covers only the C++ surface consumed by the local
// renderer after shader-side RTXDI remediation. The reservoir addressing
// formula matches cleanroom_common/restir_shared_reservoir_addressing.hlsli.

#include <cstdint>

struct RTXDI_ReservoirBufferParameters
{
	uint32_t reservoirBlockRowPitch = 0;
	uint32_t reservoirArrayPitch = 0;
	uint32_t pad1 = 0;
	uint32_t pad2 = 0;
};

namespace rtxdi {

enum class CheckerboardMode : uint32_t
{
	Off = 0,
	Black = 1,
	White = 2
};

constexpr uint32_t c_ReservoirBlockSize = 16u;
constexpr uint32_t c_ReservoirBlockArea = c_ReservoirBlockSize * c_ReservoirBlockSize;

constexpr uint32_t ReservoirBlockCount(uint32_t dimension)
{
	return (dimension + c_ReservoirBlockSize - 1u) / c_ReservoirBlockSize;
}

constexpr uint32_t ReservoirWidthForCheckerboard(uint32_t width, CheckerboardMode checkerboardMode)
{
	return checkerboardMode == CheckerboardMode::Off ? width : ((width + 1u) >> 1u);
}

constexpr RTXDI_ReservoirBufferParameters CalculateReservoirBufferParameters(
	uint32_t width,
	uint32_t height,
	CheckerboardMode checkerboardMode)
{
	const uint32_t reservoirWidth = ReservoirWidthForCheckerboard(width > 0 ? width : 1u, checkerboardMode);
	const uint32_t reservoirHeight = height > 0 ? height : 1u;
	const uint32_t blockCountX = ReservoirBlockCount(reservoirWidth);
	const uint32_t blockCountY = ReservoirBlockCount(reservoirHeight);

	RTXDI_ReservoirBufferParameters parameters;
	parameters.reservoirBlockRowPitch = blockCountX * c_ReservoirBlockArea;
	parameters.reservoirArrayPitch = parameters.reservoirBlockRowPitch * blockCountY;
	return parameters;
}

} // namespace rtxdi

static_assert(sizeof(RTXDI_ReservoirBufferParameters) == 16, "RTXDI reservoir parameters must match HLSL cbuffer layout");
static_assert(rtxdi::CalculateReservoirBufferParameters(1, 1, rtxdi::CheckerboardMode::Off).reservoirBlockRowPitch == rtxdi::c_ReservoirBlockArea, "RTXDI reservoir row pitch must match tiled shader addressing");
static_assert(rtxdi::CalculateReservoirBufferParameters(16, 16, rtxdi::CheckerboardMode::Off).reservoirArrayPitch == rtxdi::c_ReservoirBlockArea, "RTXDI reservoir array pitch must match one shader addressing block");
static_assert(rtxdi::CalculateReservoirBufferParameters(17, 17, rtxdi::CheckerboardMode::Off).reservoirArrayPitch == (2u * rtxdi::c_ReservoirBlockArea * 2u), "RTXDI reservoir array pitch must round dimensions up to shader addressing blocks");
