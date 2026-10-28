// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Modding-Exception
// Copyright (c) 2026 HelmetOverlaysNG contributors. See COPYING and EXCEPTIONS.md.

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace HONG::ArtMetrics
{
	// Alpha below this counts as see-through.
	inline constexpr std::uint8_t kClearAlpha = 32;

	// Fraction (0..1) of the top mip's pixels that are see-through, read from the
	// alpha of a DXT3 or DXT5 DDS file held in memory. Returns -1 for anything else
	// (other formats, a truncated or malformed file).
	float SeeThroughFraction(const std::uint8_t* a_data, std::size_t a_size);

	// RGB brighter than this on any channel counts as color; below it a pixel reads as black.
	inline constexpr std::uint8_t kBlackLevel = 24;

	// At or above this ColoredFraction the art has real color; below it the file is a black mask.
	inline constexpr float kColoredShare = 0.05f;

	// Fraction (0..1) of the blocks with any visible pixel whose two DXT color endpoints are not black,
	// so a file whose RGB is all black and only the alpha shapes the overlay reads 0. Looks at endpoints
	// only, no pixel decode. Returns -1 for anything else (other formats, a truncated or malformed file).
	float ColoredFraction(const std::uint8_t* a_data, std::size_t a_size);

	// One texel per 4x4 DDS block: 255 where every pixel of the block has alpha 255 and so does every
	// block within the margin, 0 elsewhere. The image border counts as not opaque, so a margin also trims
	// the edge. width and height are the block grid (480x270 for a 1920x1080 overlay).
	struct OpaqueMask
	{
		std::uint32_t width{ 0 };
		std::uint32_t height{ 0 };
		std::vector<std::uint8_t> pixels;
		float share{ 0.0f };  // fraction of texels that are 255
	};

	// Builds the mask from a DXT3 or DXT5 DDS file in memory. a_marginBlocks is the erosion radius in
	// blocks (4 px each). Returns false for any other format or a truncated or malformed file.
	bool BuildOpaqueMask(const std::uint8_t* a_data, std::size_t a_size, std::uint32_t a_marginBlocks, OpaqueMask& a_out);
}
