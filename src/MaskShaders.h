// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Modding-Exception
// Copyright (c) 2026 HelmetOverlaysNG contributors. See COPYING and EXCEPTIONS.md.

#pragma once

// The mask shaders and the constant buffer they read, in one place so tests compile the same string the plugin does.

#include <cstddef>

namespace HONG::OverlayMask::Shaders
{
	inline constexpr char kVertexShader[] = R"(
float4 main(uint id : SV_VertexID) : SV_Position
{
	float2 uv = float2((id << 1) & 2, id & 2);
	return float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
})";

	// Writes the near depth wherever the art is fully opaque. The 0.5 threshold is tested against the mask
	// texture's own bytes, not the user's opacity float.
	inline constexpr char kPixelShader[] = R"(
Texture2D<float> MaskTex : register(t0);
SamplerState PointSampler : register(s0);

cbuffer MaskCB : register(b0)
{
	float2 ViewSize;
	float2 Center;
	float2 Size;
	float2 ViewOrigin;
	float NearDepth;
	float2 InsetUV;
	float Padding;
};

// Opaque only where the mask stays opaque InsetUV away in all four directions; the step is pre-multiplied
// by sqrt(2) because five axial taps deliver only 1/sqrt(2) of it on a diagonal edge.
float main(float4 position : SV_Position) : SV_Depth
{
	float2 maskUV = ((position.xy - ViewOrigin) / ViewSize - Center) / Size + 0.5;
	if (any(maskUV < 0.0) || any(maskUV > 1.0))
		discard;
	float sampled = MaskTex.SampleLevel(PointSampler, maskUV, 0);
	sampled = min(sampled, MaskTex.SampleLevel(PointSampler, maskUV + float2(InsetUV.x, 0.0), 0));
	sampled = min(sampled, MaskTex.SampleLevel(PointSampler, maskUV - float2(InsetUV.x, 0.0), 0));
	sampled = min(sampled, MaskTex.SampleLevel(PointSampler, maskUV + float2(0.0, InsetUV.y), 0));
	sampled = min(sampled, MaskTex.SampleLevel(PointSampler, maskUV - float2(0.0, InsetUV.y), 0));
	if (sampled < 0.5)
		discard;
	return NearDepth;
})";

	// Must match MaskCB member for member, in order: the test compares each offsetof against D3DReflect's
	// offsets for the compiled HLSL, so a reorder on either side fails instead of shifting what the GPU reads.
	struct Constants
	{
		float viewSize[2];  //  0
		float center[2];    //  8
		float size[2];      // 16
		float origin[2];    // 24
		float nearDepth;    // 32
		float inset[2];     // 36
		float padding;      // 44
	};

	static_assert(sizeof(Constants) == 48, "MaskCB is 48 bytes; the HLSL cbuffer must stay the same size");
	static_assert(offsetof(Constants, viewSize) == 0);
	static_assert(offsetof(Constants, center) == 8);
	static_assert(offsetof(Constants, size) == 16);
	static_assert(offsetof(Constants, origin) == 24);
	static_assert(offsetof(Constants, nearDepth) == 32);
	static_assert(offsetof(Constants, inset) == 36);
	static_assert(offsetof(Constants, padding) == 44);

	// Below this display fraction a 3 px inset would remove the art entirely.
	inline constexpr float kMinStampSize = 0.01f;  // display fraction
}
