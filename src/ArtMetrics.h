// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Modding-Exception
// Copyright (c) 2026 HelmetOverlaysNG contributors. See COPYING and EXCEPTIONS.md.

#pragma once

#include <cstddef>
#include <cstdint>

namespace HONG::ArtMetrics
{
	// Alpha below this counts as see-through.
	inline constexpr std::uint8_t kClearAlpha = 32;

	// Fraction (0..1) of the top mip's pixels that are see-through, read from the
	// alpha of a DXT3 or DXT5 DDS file held in memory. Returns -1 for anything else
	// (other formats, a truncated or malformed file).
	float SeeThroughFraction(const std::uint8_t* a_data, std::size_t a_size);
}
