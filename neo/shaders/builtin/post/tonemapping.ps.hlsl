/*
* Copyright (c) 2014-2021, NVIDIA CORPORATION. All rights reserved.
*
* Permission is hereby granted, free of charge, to any person obtaining a
* copy of this software and associated documentation files (the "Software"),
* to deal in the Software without restriction, including without limitation
* the rights to use, copy, modify, merge, publish, distribute, sublicense,
* and/or sell copies of the Software, and to permit persons to whom the
* Software is furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in
* all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
* THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
* FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
* DEALINGS IN THE SOFTWARE.
*/

#pragma pack_matrix(row_major)

#include "vulkan.hlsli"
#include "tonemapping_cb.h"

// *INDENT-OFF*
#if SOURCE_ARRAY
Texture2DArray t_Source : register(t0);
#else
Texture2D t_Source : register(t0);
#endif
Buffer<uint> t_Exposure : register(t1);

Texture2D t_ColorLUT : register(t2);
SamplerState s_ColorLUTSampler : register(s0);

#if USE_PUSH_CONSTANTS
VK_PUSH_CONSTANT ConstantBuffer<ToneMappingConstants> g_ToneMapping : register( b0 );
#else
cbuffer c_ToneMapping : register( b0 )
{
    ToneMappingConstants g_ToneMapping;
};
#endif
// *INDENT-ON*

float3 ACESFilm( float3 x )
{
	float a = 2.51;
	float b = 0.03;
	float c = 2.43;
	float d = 0.59;
	float e = 0.14;
	return saturate( ( x * ( a * x + b ) ) / ( x * ( c * x + d ) + e ) );
}

float Luminance( float3 color )
{
	return 0.2126 * color.r + 0.7152 * color.g + 0.0722 * color.b;
}

float3 ConvertToLDR( float3 color )
{
	float srcLuminance = Luminance( color );

	if( srcLuminance <= 0 )
	{
		return 0;
	}

	float adaptedLuminance = asfloat( t_Exposure[0] );
	if( adaptedLuminance <= 0 )
	{
		adaptedLuminance = g_ToneMapping.minAdaptedLuminance;
	}

	float scaledLuminance = g_ToneMapping.exposureScale * srcLuminance / adaptedLuminance;
	float mappedLuminance = ( scaledLuminance * ( 1 + scaledLuminance * g_ToneMapping.whitePointInvSquared ) ) / ( 1 + scaledLuminance );

	return color * ( mappedLuminance / srcLuminance );
}

float3 ReadColorLUTTexel( int x, int y )
{
	return t_ColorLUT.Load( int3( x, y, 0 ) ).rgb;
}

float3 ApplyColorLUT( float3 color )
{
	color = saturate( color );

	uint lutWidth = 0;
	uint lutHeight = 0;
	t_ColorLUT.GetDimensions( lutWidth, lutHeight );

	const int size = max( 1, ( int )lutHeight );
	const int maxIndex = size - 1;
	float3 scaled = color * maxIndex;
	int3 index0 = int3( floor( scaled ) );
	int3 index1 = min( index0 + 1, int3( maxIndex, maxIndex, maxIndex ) );
	float3 fracIndex = scaled - index0;

	int x000 = index0.r + index0.b * size;
	int x100 = index1.r + index0.b * size;
	int x010 = index0.r + index0.b * size;
	int x110 = index1.r + index0.b * size;
	int x001 = index0.r + index1.b * size;
	int x101 = index1.r + index1.b * size;
	int x011 = index0.r + index1.b * size;
	int x111 = index1.r + index1.b * size;

	float3 c000 = ReadColorLUTTexel( x000, index0.g );
	float3 c100 = ReadColorLUTTexel( x100, index0.g );
	float3 c010 = ReadColorLUTTexel( x010, index1.g );
	float3 c110 = ReadColorLUTTexel( x110, index1.g );
	float3 c001 = ReadColorLUTTexel( x001, index0.g );
	float3 c101 = ReadColorLUTTexel( x101, index0.g );
	float3 c011 = ReadColorLUTTexel( x011, index1.g );
	float3 c111 = ReadColorLUTTexel( x111, index1.g );

	float3 c00 = lerp( c000, c100, fracIndex.r );
	float3 c10 = lerp( c010, c110, fracIndex.r );
	float3 c01 = lerp( c001, c101, fracIndex.r );
	float3 c11 = lerp( c011, c111, fracIndex.r );
	float3 c0 = lerp( c00, c10, fracIndex.g );
	float3 c1 = lerp( c01, c11, fracIndex.g );
	return saturate( lerp( c0, c1, fracIndex.b ) );
}

float3 ApplyToneAdjustments( float3 color )
{
	color = saturate( color );
	color = saturate( ( color - 0.5 ) * g_ToneMapping.contrast + 0.5 );

	float luminance = Luminance( color );
	color = lerp( luminance.xxx, color, g_ToneMapping.saturation );
	return saturate( color );
}

void main(
	in float4 pos : SV_Position,
	in float2 uv : UV,
	out float4 o_rgba : SV_Target )
{
#if SOURCE_ARRAY
	float4 HdrColor = t_Source[uint3( pos.xy, g_ToneMapping.sourceSlice )];
#else
	float4 HdrColor = t_Source[pos.xy];
#endif
	o_rgba.rgb = ConvertToLDR( HdrColor.rgb );
	o_rgba.a = HdrColor.a;

	// Tonemapping curve is applied after exposure. User strip LUTs are authored
	// for the display-encoded result, so LUT sampling happens after gamma encode.
	if( g_ToneMapping.enableACES != 0 )
	{
		o_rgba.rgb = ACESFilm( o_rgba.rgb );
	}

	o_rgba.rgb = ApplyToneAdjustments( o_rgba.rgb );

	// Gamma correction since we are not rendering to an sRGB render target.
	const float hdrGamma = 2.2;
	float gamma = 1.0 / hdrGamma;
	o_rgba.r = pow( o_rgba.r, gamma );
	o_rgba.g = pow( o_rgba.g, gamma );
	o_rgba.b = pow( o_rgba.b, gamma );

	if( g_ToneMapping.colorLUTTextureSize.x > 0 )
	{
		if( g_ToneMapping.colorLUTDebugMode == 1 )
		{
			o_rgba.rgb = saturate( o_rgba.rgb );
		}
		else if( g_ToneMapping.colorLUTDebugMode == 2 )
		{
			uint lutWidth = 0;
			uint lutHeight = 0;
			t_ColorLUT.GetDimensions( lutWidth, lutHeight );
			o_rgba.rgb = ReadColorLUTTexel( ( int )lutWidth - 1, ( int )lutHeight - 1 );
		}
		else if( g_ToneMapping.colorLUTDebugMode == 3 )
		{
			o_rgba.rgb = ( g_ToneMapping.colorLUTUseOverride != 0 ) ? float3( 0.0, 1.0, 0.0 ) : float3( 1.0, 0.0, 1.0 );
		}
		else if( g_ToneMapping.colorLUTDebugMode == 4 )
		{
			float size01 = saturate( g_ToneMapping.colorLUTTextureSize.y / 64.0 );
			o_rgba.rgb = float3( size01, g_ToneMapping.colorLUTUseOverride != 0 ? 1.0 : 0.0, g_ToneMapping.colorLUTTextureSize.x > 0 ? 1.0 : 0.0 );
		}
		else
		{
			o_rgba.rgb = ApplyColorLUT( o_rgba.rgb );
		}
	}
}
