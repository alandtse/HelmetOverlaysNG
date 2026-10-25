// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Modding-Exception
// Copyright (c) 2026 HelmetOverlaysNG contributors. See COPYING and EXCEPTIONS.md.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <vector>

#include "ArtMetrics.h"
#include "dds_fixture.h"

using Catch::Approx;
using HONG::ArtMetrics::BuildOpaqueMask;
using HONG::ArtMetrics::OpaqueMask;

using fixture::Concat;
using fixture::Dxt3Block;
using fixture::Dxt5Block;
using fixture::MakeDds;

namespace
{
	const std::vector<std::uint8_t> kOpaque = Dxt5Block(255, 255, 0);
	const std::vector<std::uint8_t> kClear = Dxt5Block(0, 0, 0);

	// A grid of blocks in row order, each opaque (true) or clear (false).
	std::vector<std::uint8_t> Grid(std::uint32_t a_wide, std::uint32_t a_high, const std::vector<bool>& a_opaque)
	{
		std::vector<std::vector<std::uint8_t>> parts;
		for (std::uint32_t i = 0; i < a_wide * a_high; ++i) {
			parts.push_back(a_opaque[i] ? kOpaque : kClear);
		}
		return Concat(parts);
	}
}

TEST_CASE("an opaque block is masked with no margin", "[mask]")
{
	const auto file = MakeDds(4, 4, "DXT5", kOpaque);
	OpaqueMask mask;
	REQUIRE(BuildOpaqueMask(file.data(), file.size(), 0, mask));
	REQUIRE(mask.width == 1);
	REQUIRE(mask.height == 1);
	REQUIRE(mask.pixels == std::vector<std::uint8_t>{ 255 });
	REQUIRE(mask.share == Approx(1.0f));
}

TEST_CASE("a block that is almost opaque is not masked", "[mask]")
{
	// Index 1 is the second endpoint, 254, for pixel 0: one pixel below fully opaque.
	const auto file = MakeDds(4, 4, "DXT5", Dxt5Block(255, 254, 1));
	OpaqueMask mask;
	REQUIRE(BuildOpaqueMask(file.data(), file.size(), 0, mask));
	REQUIRE(mask.pixels == std::vector<std::uint8_t>{ 0 });
	REQUIRE(mask.share == Approx(0.0f));
}

TEST_CASE("DXT3 alpha of 15 is fully opaque", "[mask]")
{
	const auto file = MakeDds(4, 4, "DXT3", Dxt3Block(0xFF));
	OpaqueMask mask;
	REQUIRE(BuildOpaqueMask(file.data(), file.size(), 0, mask));
	REQUIRE(mask.pixels == std::vector<std::uint8_t>{ 255 });

	const auto notQuite = MakeDds(4, 4, "DXT3", Dxt3Block(0xEF));
	REQUIRE(BuildOpaqueMask(notQuite.data(), notQuite.size(), 0, mask));
	REQUIRE(mask.pixels == std::vector<std::uint8_t>{ 0 });
}

TEST_CASE("the margin trims the image border", "[mask]")
{
	const auto file = MakeDds(4, 4, "DXT5", kOpaque);
	OpaqueMask mask;
	REQUIRE(BuildOpaqueMask(file.data(), file.size(), 1, mask));
	REQUIRE(mask.pixels == std::vector<std::uint8_t>{ 0 });
}

TEST_CASE("a margin of one leaves only the interior of an opaque block", "[mask]")
{
	const auto file = MakeDds(12, 12, "DXT5", Grid(3, 3, std::vector<bool>(9, true)));
	OpaqueMask mask;
	REQUIRE(BuildOpaqueMask(file.data(), file.size(), 1, mask));
	REQUIRE(mask.width == 3);
	REQUIRE(mask.height == 3);
	const std::vector<std::uint8_t> expected{ 0, 0, 0, 0, 255, 0, 0, 0, 0 };
	REQUIRE(mask.pixels == expected);
	REQUIRE(mask.share == Approx(1.0f / 9.0f));
}

TEST_CASE("a clear block stays clear and its neighbours stay masked at no margin", "[mask]")
{
	// 5 wide, 1 high: opaque everywhere except the middle.
	const auto file = MakeDds(20, 4, "DXT5", Grid(5, 1, { true, true, false, true, true }));
	OpaqueMask mask;
	REQUIRE(BuildOpaqueMask(file.data(), file.size(), 0, mask));
	REQUIRE(mask.pixels == std::vector<std::uint8_t>{ 255, 255, 0, 255, 255 });
}

TEST_CASE("row order follows the image from the top", "[mask]")
{
	// 2 wide, 2 high: only the bottom-left block is opaque.
	const auto file = MakeDds(8, 8, "DXT5", Grid(2, 2, { false, false, true, false }));
	OpaqueMask mask;
	REQUIRE(BuildOpaqueMask(file.data(), file.size(), 0, mask));
	REQUIRE(mask.pixels == std::vector<std::uint8_t>{ 0, 0, 255, 0 });
}

TEST_CASE("the mask grid is a quarter of the image each way", "[mask]")
{
	std::vector<std::vector<std::uint8_t>> blocks(480 * 270, kOpaque);
	const auto file = MakeDds(1920, 1080, "DXT5", Concat(blocks));
	OpaqueMask mask;
	REQUIRE(BuildOpaqueMask(file.data(), file.size(), 2, mask));
	REQUIRE(mask.width == 480);
	REQUIRE(mask.height == 270);
	REQUIRE(mask.pixels.size() == 480u * 270u);
	REQUIRE(mask.pixels[0] == 0);                  // trimmed corner
	REQUIRE(mask.pixels[135 * 480 + 240] == 255);  // interior
}

TEST_CASE("unsupported or truncated files are rejected", "[mask]")
{
	OpaqueMask mask;
	REQUIRE_FALSE(BuildOpaqueMask(nullptr, 0, 0, mask));

	const auto dxt1 = MakeDds(4, 4, "DXT1", std::vector<std::uint8_t>(16, 0));
	REQUIRE_FALSE(BuildOpaqueMask(dxt1.data(), dxt1.size(), 0, mask));

	const auto truncated = MakeDds(8, 4, "DXT5", kOpaque);  // needs two blocks
	REQUIRE_FALSE(BuildOpaqueMask(truncated.data(), truncated.size(), 0, mask));
}
