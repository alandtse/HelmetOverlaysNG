// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Modding-Exception
// Copyright (c) 2026 HelmetOverlaysNG contributors. See COPYING and EXCEPTIONS.md.

#pragma once

#include <cstdint>

namespace HONG::Tint
{
	struct Rgb
	{
		float r{ 1.0f };
		float g{ 1.0f };
		float b{ 1.0f };
	};

	// Average of the cells on the outer ring of a_width x a_height cells; white for an empty grid.
	Rgb RingAverage(const Rgb* a_cells, std::uint32_t a_width, std::uint32_t a_height);

	// Multiplier that moves white toward the hue of a_average. Its brightest channel stays 1, so the
	// light level remains the only thing that changes brightness.
	Rgb Chroma(Rgb a_average, float a_strength);

	// Moves each channel of a_current toward a_target by at most a_step.
	Rgb Approach(Rgb a_current, Rgb a_target, float a_step);
}
