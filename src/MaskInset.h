// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Modding-Exception
// Copyright (c) 2026 HelmetOverlaysNG contributors. See COPYING and EXCEPTIONS.md.

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>

namespace HONG::MaskInset
{
	// Screen-pixel clearance the stamp keeps from any non-opaque mask texel; the mask margin is in art pixels and
	// shrinks on a small display, this cannot. The shader's five axial taps give only 1/sqrt(2) of the step as
	// margin on a 45-degree edge, so the request is scaled by sqrt(2) in kAxisInsetPx.
	inline constexpr float kScreenInsetPx = 3.0f;

	// The axial request that yields kScreenInsetPx in the worst direction. Must be >= sqrt(2): the decimal
	// 1.41421356f rounds below it and leaves the guarantee short.
	inline constexpr float kAxisInsetPx = kScreenInsetPx * 1.4142137f;

	// One axis, in whole mask texels. Returns zero for degenerate input, which is not a usable step, so only
	// Decide's result should reach the call site. The sqrt(2) compensation belongs to Decide, not here.
	inline float Along(float a_displayPixels, float a_artFraction, std::uint32_t a_maskTexels, float a_requestedPixels = kScreenInsetPx)
	{
		if (a_maskTexels == 0 || !(a_artFraction > 0.0f) || !(a_displayPixels > 0.0f)) {
			return 0.0f;
		}
		const float artPixels = std::max(a_displayPixels * a_artFraction, 1.0f);
		const float texels = a_requestedPixels * static_cast<float>(a_maskTexels) / artPixels;
		// A texel count below one would round to zero; one whole texel is the floor because the shader samples
		// the mask at discrete texels and a sub-texel step tests the sample against itself.
		const auto whole = static_cast<std::uint32_t>(std::ceil(texels));
		return static_cast<float>(std::max(whole, 1u)) / static_cast<float>(a_maskTexels);
	}

	/// An inset known to erode on both axes. Only Decide constructs it, so a zero (collapsed) erosion cannot be drawn.
	class Step
	{
	public:
		float x() const { return _x; }
		float y() const { return _y; }

	private:
		friend std::optional<Step> Decide(float, float, float, float, std::uint32_t, std::uint32_t);

		Step(float a_x, float a_y) : _x(a_x), _y(a_y) {}

		float _x;
		float _y;
	};

	/// Whether this stamp can be drawn with a real erosion, and the inset to use. nullopt means do not draw.
	inline std::optional<Step> Decide(float a_displayWidth, float a_displayHeight, float a_fitX, float a_fitY, std::uint32_t a_maskWidth, std::uint32_t a_maskHeight)
	{
		// sqrt(2) lives here because it compensates for the tap pattern (see kScreenInsetPx).
		const float x = Along(a_displayWidth, a_fitX, a_maskWidth, kAxisInsetPx);
		const float y = Along(a_displayHeight, a_fitY, a_maskHeight, kAxisInsetPx);
		// Along's one-texel floor makes any nonzero result usable; both axes must be checked.
		if (!(x > 0.0f) || !(y > 0.0f)) {
			return std::nullopt;
		}
		return Step{ x, y };
	}
}
