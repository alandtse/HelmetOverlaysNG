// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Modding-Exception
// Copyright (c) 2026 HelmetOverlaysNG contributors. See COPYING and EXCEPTIONS.md.

#pragma once

#include <cstdint>
#include <cstring>
#include <vector>

// Builds tiny DDS files in memory for the art tests.
namespace fixture
{
	inline constexpr std::size_t kHeader = 128;

	inline std::vector<std::uint8_t> MakeDds(std::uint32_t a_width, std::uint32_t a_height, const char (&a_fourCC)[5], const std::vector<std::uint8_t>& a_blocks)
	{
		std::vector<std::uint8_t> file(kHeader + a_blocks.size(), 0);
		std::memcpy(file.data(), "DDS ", 4);
		std::memcpy(file.data() + 12, &a_height, 4);
		std::memcpy(file.data() + 16, &a_width, 4);
		std::memcpy(file.data() + 84, a_fourCC, 4);
		std::memcpy(file.data() + kHeader, a_blocks.data(), a_blocks.size());
		return file;
	}

	// One DXT5 block: alpha endpoints, 3-bit indices (pixel p at bits 3p), 8 bytes of color.
	inline std::vector<std::uint8_t> Dxt5Block(std::uint8_t a_a0, std::uint8_t a_a1, std::uint64_t a_indices)
	{
		std::vector<std::uint8_t> block(16, 0);
		block[0] = a_a0;
		block[1] = a_a1;
		for (std::size_t i = 0; i < 6; ++i) {
			block[2 + i] = static_cast<std::uint8_t>((a_indices >> (8 * i)) & 0xFF);
		}
		return block;
	}

	inline std::vector<std::uint8_t> Dxt3Block(std::uint8_t a_fill)
	{
		std::vector<std::uint8_t> block(16, 0);
		for (std::size_t i = 0; i < 8; ++i) {
			block[i] = a_fill;
		}
		return block;
	}

	inline std::vector<std::uint8_t> Concat(const std::vector<std::vector<std::uint8_t>>& a_parts)
	{
		std::vector<std::uint8_t> out;
		for (const auto& part : a_parts) {
			out.insert(out.end(), part.begin(), part.end());
		}
		return out;
	}
}
