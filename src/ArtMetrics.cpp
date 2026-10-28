// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Modding-Exception
// Copyright (c) 2026 HelmetOverlaysNG contributors. See COPYING and EXCEPTIONS.md.

#include "ArtMetrics.h"

#include <algorithm>
#include <array>

namespace HONG::ArtMetrics
{
	namespace
	{
		constexpr std::size_t kHeaderSize = 128;
		constexpr std::size_t kBlockSize = 16;
		constexpr std::size_t kColorOffset = 8;
		constexpr std::size_t kPixelsPerBlock = 16;
		constexpr std::size_t kHeightOffset = 12;
		constexpr std::size_t kWidthOffset = 16;
		constexpr std::size_t kFourCCOffset = 84;
		constexpr std::uint32_t kMaxDimension = 16384;

		constexpr std::uint32_t FourCC(char a, char b, char c, char d)
		{
			return static_cast<std::uint32_t>(static_cast<std::uint8_t>(a)) | (static_cast<std::uint32_t>(static_cast<std::uint8_t>(b)) << 8) |
			       (static_cast<std::uint32_t>(static_cast<std::uint8_t>(c)) << 16) | (static_cast<std::uint32_t>(static_cast<std::uint8_t>(d)) << 24);
		}

		constexpr std::uint8_t kOpaqueAlpha = 255;

		constexpr std::uint32_t kDxt3 = FourCC('D', 'X', 'T', '3');
		constexpr std::uint32_t kDxt5 = FourCC('D', 'X', 'T', '5');

		std::uint32_t Read32(const std::uint8_t* a_bytes)
		{
			return static_cast<std::uint32_t>(a_bytes[0]) | (static_cast<std::uint32_t>(a_bytes[1]) << 8) | (static_cast<std::uint32_t>(a_bytes[2]) << 16) |
			       (static_cast<std::uint32_t>(a_bytes[3]) << 24);
		}

		// DXT3: sixteen explicit 4-bit alphas.
		std::size_t CountClearDxt3(const std::uint8_t* a_block)
		{
			std::size_t clear = 0;
			for (std::size_t i = 0; i < 8; ++i) {
				const std::uint8_t low = static_cast<std::uint8_t>((a_block[i] & 0x0F) * 17);
				const std::uint8_t high = static_cast<std::uint8_t>((a_block[i] >> 4) * 17);
				clear += (low < kClearAlpha) + (high < kClearAlpha);
			}
			return clear;
		}

		// DXT5: two endpoint alphas and sixteen 3-bit indices into the interpolated table.
		std::size_t CountClearDxt5(const std::uint8_t* a_block)
		{
			const int a0 = a_block[0];
			const int a1 = a_block[1];
			std::array<std::uint8_t, 8> table{};
			table[0] = static_cast<std::uint8_t>(a0);
			table[1] = static_cast<std::uint8_t>(a1);
			if (a0 > a1) {
				for (int i = 1; i <= 6; ++i) {
					table[static_cast<std::size_t>(i) + 1] = static_cast<std::uint8_t>(((7 - i) * a0 + i * a1) / 7);
				}
			} else {
				for (int i = 1; i <= 4; ++i) {
					table[static_cast<std::size_t>(i) + 1] = static_cast<std::uint8_t>(((5 - i) * a0 + i * a1) / 5);
				}
				table[6] = 0;
				table[7] = 255;
			}

			std::uint64_t indices = 0;
			for (std::size_t i = 0; i < 6; ++i) {
				indices |= static_cast<std::uint64_t>(a_block[2 + i]) << (8 * i);
			}
			std::size_t clear = 0;
			for (std::size_t pixel = 0; pixel < kPixelsPerBlock; ++pixel) {
				clear += table[(indices >> (3 * pixel)) & 7] < kClearAlpha;
			}
			return clear;
		}

		struct Layout
		{
			std::uint32_t blocksWide{ 0 };
			std::uint32_t blocksHigh{ 0 };
			std::uint32_t fourCC{ 0 };
		};

		// Validates the header of a DXT3 or DXT5 DDS and that every block is present.
		bool ParseLayout(const std::uint8_t* a_data, std::size_t a_size, Layout& a_out)
		{
			if (!a_data || a_size < kHeaderSize || a_data[0] != 'D' || a_data[1] != 'D' || a_data[2] != 'S' || a_data[3] != ' ') {
				return false;
			}
			const std::uint32_t height = Read32(a_data + kHeightOffset);
			const std::uint32_t width = Read32(a_data + kWidthOffset);
			const std::uint32_t fourCC = Read32(a_data + kFourCCOffset);
			if (width == 0 || height == 0 || width > kMaxDimension || height > kMaxDimension || (fourCC != kDxt3 && fourCC != kDxt5)) {
				return false;
			}
			a_out.blocksWide = (width + 3) / 4;
			a_out.blocksHigh = (height + 3) / 4;
			a_out.fourCC = fourCC;
			const std::size_t blocks = static_cast<std::size_t>(a_out.blocksWide) * a_out.blocksHigh;
			return a_size - kHeaderSize >= blocks * kBlockSize;
		}

		// Number of pixels whose alpha is exactly 255 in a DXT3 block.
		std::size_t CountOpaqueDxt3(const std::uint8_t* a_block)
		{
			std::size_t opaque = 0;
			for (std::size_t i = 0; i < 8; ++i) {
				opaque += ((a_block[i] & 0x0F) * 17 == kOpaqueAlpha) + ((a_block[i] >> 4) * 17 == kOpaqueAlpha);
			}
			return opaque;
		}

		std::size_t CountOpaqueDxt5(const std::uint8_t* a_block)
		{
			const int a0 = a_block[0];
			const int a1 = a_block[1];
			std::array<std::uint8_t, 8> table{};
			table[0] = static_cast<std::uint8_t>(a0);
			table[1] = static_cast<std::uint8_t>(a1);
			if (a0 > a1) {
				for (int i = 1; i <= 6; ++i) {
					table[static_cast<std::size_t>(i) + 1] = static_cast<std::uint8_t>(((7 - i) * a0 + i * a1) / 7);
				}
			} else {
				for (int i = 1; i <= 4; ++i) {
					table[static_cast<std::size_t>(i) + 1] = static_cast<std::uint8_t>(((5 - i) * a0 + i * a1) / 5);
				}
				table[6] = 0;
				table[7] = 255;
			}

			std::uint64_t indices = 0;
			for (std::size_t i = 0; i < 6; ++i) {
				indices |= static_cast<std::uint64_t>(a_block[2 + i]) << (8 * i);
			}
			std::size_t opaque = 0;
			for (std::size_t pixel = 0; pixel < kPixelsPerBlock; ++pixel) {
				opaque += table[(indices >> (3 * pixel)) & 7] == kOpaqueAlpha;
			}
			return opaque;
		}

		// An RGB565 color endpoint with any channel above kBlackLevel once widened to 8 bits.
		bool IsColorEndpoint(const std::uint8_t* a_endpoint)
		{
			const auto packed = static_cast<std::uint32_t>(a_endpoint[0]) | (static_cast<std::uint32_t>(a_endpoint[1]) << 8);
			const std::uint32_t red = ((packed >> 11) & 0x1F) * 255 / 31;
			const std::uint32_t green = ((packed >> 5) & 0x3F) * 255 / 63;
			const std::uint32_t blue = (packed & 0x1F) * 255 / 31;
			return std::max({ red, green, blue }) > kBlackLevel;
		}

		// Erodes a binary grid by a_radius in each direction; cells outside the grid count as clear.
		std::vector<std::uint8_t> Erode(const std::vector<std::uint8_t>& a_grid, std::uint32_t a_width, std::uint32_t a_height, std::uint32_t a_radius)
		{
			if (a_radius == 0) {
				return a_grid;
			}
			const auto radius = static_cast<std::int64_t>(a_radius);
			const auto at = [&](const std::vector<std::uint8_t>& a_src, std::int64_t a_x, std::int64_t a_y) -> bool {
				if (a_x < 0 || a_y < 0 || a_x >= static_cast<std::int64_t>(a_width) || a_y >= static_cast<std::int64_t>(a_height)) {
					return false;
				}
				return a_src[static_cast<std::size_t>(a_y) * a_width + static_cast<std::size_t>(a_x)] != 0;
			};

			std::vector<std::uint8_t> horizontal(a_grid.size(), 0);
			for (std::uint32_t y = 0; y < a_height; ++y) {
				for (std::uint32_t x = 0; x < a_width; ++x) {
					bool keep = true;
					for (std::int64_t dx = -radius; dx <= radius && keep; ++dx) {
						keep = at(a_grid, static_cast<std::int64_t>(x) + dx, y);
					}
					horizontal[static_cast<std::size_t>(y) * a_width + x] = keep ? 255 : 0;
				}
			}
			std::vector<std::uint8_t> out(a_grid.size(), 0);
			for (std::uint32_t y = 0; y < a_height; ++y) {
				for (std::uint32_t x = 0; x < a_width; ++x) {
					bool keep = true;
					for (std::int64_t dy = -radius; dy <= radius && keep; ++dy) {
						keep = at(horizontal, x, static_cast<std::int64_t>(y) + dy);
					}
					out[static_cast<std::size_t>(y) * a_width + x] = keep ? 255 : 0;
				}
			}
			return out;
		}
	}

	float SeeThroughFraction(const std::uint8_t* a_data, std::size_t a_size)
	{
		Layout layout;
		if (!ParseLayout(a_data, a_size, layout)) {
			return -1.0f;
		}

		const std::size_t blocks = static_cast<std::size_t>(layout.blocksWide) * layout.blocksHigh;
		std::size_t clear = 0;
		for (std::size_t block = 0; block < blocks; ++block) {
			const std::uint8_t* alpha = a_data + kHeaderSize + block * kBlockSize;
			clear += layout.fourCC == kDxt5 ? CountClearDxt5(alpha) : CountClearDxt3(alpha);
		}
		return static_cast<float>(clear) / static_cast<float>(blocks * kPixelsPerBlock);
	}

	float ColoredFraction(const std::uint8_t* a_data, std::size_t a_size)
	{
		Layout layout;
		if (!ParseLayout(a_data, a_size, layout)) {
			return -1.0f;
		}

		const std::size_t blocks = static_cast<std::size_t>(layout.blocksWide) * layout.blocksHigh;
		std::size_t visible = 0;
		std::size_t colored = 0;
		for (std::size_t block = 0; block < blocks; ++block) {
			const std::uint8_t* data = a_data + kHeaderSize + block * kBlockSize;
			const std::size_t clear = layout.fourCC == kDxt5 ? CountClearDxt5(data) : CountClearDxt3(data);
			if (clear == kPixelsPerBlock) {
				continue;
			}
			++visible;
			colored += IsColorEndpoint(data + kColorOffset) || IsColorEndpoint(data + kColorOffset + 2);
		}
		return visible ? static_cast<float>(colored) / static_cast<float>(visible) : 0.0f;
	}

	bool BuildOpaqueMask(const std::uint8_t* a_data, std::size_t a_size, std::uint32_t a_marginBlocks, OpaqueMask& a_out)
	{
		Layout layout;
		if (!ParseLayout(a_data, a_size, layout)) {
			return false;
		}

		const std::size_t blocks = static_cast<std::size_t>(layout.blocksWide) * layout.blocksHigh;
		std::vector<std::uint8_t> grid(blocks, 0);
		for (std::size_t block = 0; block < blocks; ++block) {
			const std::uint8_t* alpha = a_data + kHeaderSize + block * kBlockSize;
			const std::size_t opaque = layout.fourCC == kDxt5 ? CountOpaqueDxt5(alpha) : CountOpaqueDxt3(alpha);
			grid[block] = opaque == kPixelsPerBlock ? 255 : 0;
		}

		a_out.width = layout.blocksWide;
		a_out.height = layout.blocksHigh;
		a_out.pixels = Erode(grid, layout.blocksWide, layout.blocksHigh, a_marginBlocks);
		const auto set = static_cast<std::size_t>(std::ranges::count(a_out.pixels, std::uint8_t{ 255 }));
		a_out.share = static_cast<float>(set) / static_cast<float>(blocks);
		return true;
	}
}
