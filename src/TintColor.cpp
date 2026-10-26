// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Modding-Exception
// Copyright (c) 2026 HelmetOverlaysNG contributors. See COPYING and EXCEPTIONS.md.

#include "TintColor.h"

#include <algorithm>

namespace HONG::Tint
{
	namespace
	{
		constexpr float kDarkestAverage = 1e-4f;

		float Step(float a_current, float a_target, float a_step)
		{
			return a_current + std::clamp(a_target - a_current, -a_step, a_step);
		}
	}

	Rgb RingAverage(const Rgb* a_cells, std::uint32_t a_stride, Region a_region)
	{
		if (!a_cells || a_region.width == 0 || a_region.height == 0) {
			return {};
		}
		double r = 0.0, g = 0.0, b = 0.0;
		std::uint32_t count = 0;
		for (std::uint32_t y = 0; y < a_region.height; ++y) {
			for (std::uint32_t x = 0; x < a_region.width; ++x) {
				if (x != 0 && y != 0 && x != a_region.width - 1 && y != a_region.height - 1) {
					continue;
				}
				const auto& cell = a_cells[(a_region.y + y) * a_stride + a_region.x + x];
				r += cell.r;
				g += cell.g;
				b += cell.b;
				++count;
			}
		}
		const auto n = static_cast<double>(count);
		return { static_cast<float>(r / n), static_cast<float>(g / n), static_cast<float>(b / n) };
	}

	Rgb Mean(Rgb a_first, Rgb a_second)
	{
		return { (a_first.r + a_second.r) * 0.5f, (a_first.g + a_second.g) * 0.5f, (a_first.b + a_second.b) * 0.5f };
	}

	Rgb Chroma(Rgb a_average, float a_strength)
	{
		const float peak = std::max({ a_average.r, a_average.g, a_average.b });
		if (!(peak > kDarkestAverage)) {
			return {};
		}
		const float amount = std::clamp(a_strength, 0.0f, 1.0f);
		const auto mix = [&](float a_channel) { return 1.0f + (std::clamp(a_channel / peak, 0.0f, 1.0f) - 1.0f) * amount; };
		return { mix(a_average.r), mix(a_average.g), mix(a_average.b) };
	}

	Rgb Approach(Rgb a_current, Rgb a_target, float a_step)
	{
		return { Step(a_current.r, a_target.r, a_step), Step(a_current.g, a_target.g, a_step), Step(a_current.b, a_target.b, a_step) };
	}
}
