// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Modding-Exception
// Copyright (c) 2026 HelmetOverlaysNG contributors. See COPYING and EXCEPTIONS.md.

#include "ArtMetrics.h"

#include <array>

namespace HONG::ArtMetrics
{
	namespace
	{
		constexpr std::size_t kHeaderSize = 128;
		constexpr std::size_t kBlockSize = 16;
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
	}

	float SeeThroughFraction(const std::uint8_t* a_data, std::size_t a_size)
	{
		if (!a_data || a_size < kHeaderSize || a_data[0] != 'D' || a_data[1] != 'D' || a_data[2] != 'S' || a_data[3] != ' ') {
			return -1.0f;
		}
		const std::uint32_t height = Read32(a_data + kHeightOffset);
		const std::uint32_t width = Read32(a_data + kWidthOffset);
		const std::uint32_t fourCC = Read32(a_data + kFourCCOffset);
		if (width == 0 || height == 0 || width > kMaxDimension || height > kMaxDimension || (fourCC != kDxt3 && fourCC != kDxt5)) {
			return -1.0f;
		}

		const std::size_t blocksWide = (width + 3) / 4;
		const std::size_t blocksHigh = (height + 3) / 4;
		const std::size_t blocks = blocksWide * blocksHigh;
		if (a_size - kHeaderSize < blocks * kBlockSize) {
			return -1.0f;
		}

		std::size_t clear = 0;
		for (std::size_t block = 0; block < blocks; ++block) {
			const std::uint8_t* alpha = a_data + kHeaderSize + block * kBlockSize;
			clear += fourCC == kDxt5 ? CountClearDxt5(alpha) : CountClearDxt3(alpha);
		}
		return static_cast<float>(clear) / static_cast<float>(blocks * kPixelsPerBlock);
	}
}
