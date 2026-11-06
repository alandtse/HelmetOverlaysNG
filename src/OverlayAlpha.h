// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Modding-Exception
// Copyright (c) 2026 HelmetOverlaysNG contributors. See COPYING and EXCEPTIONS.md.

#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace HONG::OverlayAlpha
{
	// The overlay is drawn with this byte, and the mask assumes a pixel it stamps is never visible: 0.999f * 255
	// truncates to 254, still 0.4% transparent, so the mask must be gated on this byte, not a float below 1.0.
	inline std::uint8_t Byte(float a_alpha)
	{
		if (!std::isfinite(a_alpha)) {
			return a_alpha > 0.0f ? 255 : 0;
		}
		return static_cast<std::uint8_t>(std::clamp(a_alpha, 0.0f, 1.0f) * 255.0f);
	}

	// True only when the art is drawn fully opaque, i.e. nothing behind it shows through anywhere.
	inline bool IsFullyOpaque(float a_alpha)
	{
		return Byte(a_alpha) == 255;
	}

	class MaskPermit;

	// The eligibility decision, as a value only PermitMask can construct, so a call site that skips the gate
	// does not compile.
	class MaskPermit
	{
	public:
		// Absence of permission. This is what a default-constructed candidate carries.
		constexpr MaskPermit() = default;

		[[nodiscard]] constexpr bool Allowed() const { return _allowed; }

	private:
		friend MaskPermit PermitMask(bool, bool, bool, float, std::size_t, std::size_t);
		bool _allowed{ false };
	};

	// The single definition of "may this frame stamp the mask". a_flatScreen is false in VR, where the helper owns
	// occlusion; a_layerCount and a_drawCount must both be 1 because the mask describes one overlay's art.
	inline MaskPermit PermitMask(bool a_flatScreen, bool a_enabled, bool a_gated, float a_opacity, std::size_t a_layerCount, std::size_t a_drawCount)
	{
		MaskPermit permit;
		permit._allowed = a_flatScreen && a_enabled && a_gated && IsFullyOpaque(a_opacity) && a_layerCount == 1 && a_drawCount == 1;
		return permit;
	}
}
