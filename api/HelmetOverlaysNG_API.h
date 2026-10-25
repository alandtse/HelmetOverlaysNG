// SPDX-License-Identifier: MIT
// Copyright (c) 2026 HelmetOverlaysNG contributors.
//
// Interface between HelmetOverlaysNG and a renderer plugin (Open Shaders) that can skip shading behind
// the overlay's fully opaque pixels. HelmetOverlaysNG exports one function; the renderer finds it with
// GetModuleHandle + GetProcAddress, so neither plugin needs the other at load time.
//
// Threading: the structure is written on the game's main thread at the end of each frame (after the
// scene is drawn) and must be read on the main thread, early in the next frame's world render.

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define HONG_OVERLAY_MASK_VERSION 1

	typedef struct HONG_OverlayMask
	{
		uint32_t version;     // HONG_OVERLAY_MASK_VERSION
		uint32_t structSize;  // sizeof(HONG_OverlayMask), for forward growth

		// 1 when the mask may be drawn this frame. 0 when the overlay is absent, fading, below full
		// opacity, hidden by a menu, in VR (not supported yet), or the feature is disabled.
		uint32_t active;
		uint32_t frame;  // changes whenever the contents below change

		// ID3D11ShaderResourceView* on the game's device. R8_UNORM, nearest sampling: 255 where the overlay
		// pixel is fully opaque with the safety margin applied, 0 elsewhere. Owned by HelmetOverlaysNG; valid
		// while active is 1.
		void* maskSrv;
		uint32_t maskWidth;
		uint32_t maskHeight;

		// Placement of the overlay quad, normalized to the display (0..1, y down). The overlay sample for a
		// screen position s in 0..1 is (s - center) / size + 0.5; positions whose result is outside 0..1 are
		// not covered by the overlay and must not be masked. This is the same transform the overlay is drawn
		// with, in display-normalized space, so it holds at any render resolution.
		float center[2];
		float size[2];

		uint32_t isVR;  // 1 when running on VR; the mapping above is flat only
	} HONG_OverlayMask;

	// Exported by HelmetOverlaysNG.dll. Never null once the plugin has loaded; check `active` and `version`.
	__declspec(dllexport) const HONG_OverlayMask* HONG_GetOverlayMask(void);

#ifdef __cplusplus
}
#endif
