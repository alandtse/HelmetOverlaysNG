// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Modding-Exception
// Copyright (c) 2026 HelmetOverlaysNG contributors. See COPYING and EXCEPTIONS.md.

#pragma once

#include <cstdint>

namespace HONG::Profile
{
	enum class ArmorClass : std::uint8_t
	{
		kUnknown,
		kClothing,
		kLight,
		kHeavy,
	};

	// See-through fractions measured over the Enhanced Edition overlays: dragon priest
	// masks and other closed helms sit at or below kClosedOpenness, hoods and masks at
	// or above kOpenOpenness.
	inline constexpr float kClosedOpenness = 0.45f;
	inline constexpr float kOpenOpenness = 0.90f;
	inline constexpr float kMinStrength = 0.10f;

	struct Result
	{
		float strength{ 1.0f };    // 0..1 scale for how much the helmet muffles sound
		float artFactor{ -1.0f };  // -1 when the overlay was not measurable
		float armorFactor{ 1.0f };
	};

	// a_openness is the overlay's see-through fraction (negative = unknown); a_artWeight
	// is how much the art outweighs the armor type (0 = armor only, 1 = art only).
	Result Compute(float a_openness, ArmorClass a_armorClass, float a_artWeight);
}
