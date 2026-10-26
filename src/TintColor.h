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

	// A rectangle of cells inside a grid that is a_stride cells wide.
	struct Region
	{
		std::uint32_t x{ 0 };
		std::uint32_t y{ 0 };
		std::uint32_t width{ 0 };
		std::uint32_t height{ 0 };
	};

	// Average of the cells on the outer ring of a_region; white for an empty region.
	Rgb RingAverage(const Rgb* a_cells, std::uint32_t a_stride, Region a_region);

	// Per-channel mean of two colours.
	Rgb Mean(Rgb a_first, Rgb a_second);

	// Multiplier that moves white toward the hue of a_average. Its brightest channel stays 1, so the
	// light level remains the only thing that changes brightness.
	Rgb Chroma(Rgb a_average, float a_strength);

	// Moves each channel of a_current toward a_target by at most a_step.
	Rgb Approach(Rgb a_current, Rgb a_target, float a_step);
}
