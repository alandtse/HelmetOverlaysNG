// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Modding-Exception
// Copyright (c) 2026 HelmetOverlaysNG contributors. See COPYING and EXCEPTIONS.md.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <vector>

#include "ArtMetrics.h"
#include "dds_fixture.h"

using Catch::Approx;
using HONG::ArtMetrics::SeeThroughFraction;

using fixture::Dxt3Block;
using fixture::Dxt5Block;
using fixture::MakeDds;

TEST_CASE("an opaque DXT5 block has no see-through pixels", "[art]")
{
	const auto file = MakeDds(4, 4, "DXT5", Dxt5Block(255, 255, 0));
	REQUIRE(SeeThroughFraction(file.data(), file.size()) == Approx(0.0f));
}

TEST_CASE("a fully transparent DXT5 block is all see-through", "[art]")
{
	const auto file = MakeDds(4, 4, "DXT5", Dxt5Block(0, 0, 0));
	REQUIRE(SeeThroughFraction(file.data(), file.size()) == Approx(1.0f));
}

TEST_CASE("DXT5 interpolated mode counts the clear half", "[art]")
{
	std::uint64_t indices = 0;
	for (std::uint64_t pixel = 8; pixel < 16; ++pixel) {
		indices |= 1ull << (3 * pixel);  // index 1 = a1 = 0
	}
	const auto file = MakeDds(4, 4, "DXT5", Dxt5Block(255, 0, indices));
	REQUIRE(SeeThroughFraction(file.data(), file.size()) == Approx(0.5f));
}

TEST_CASE("DXT5 six-alpha mode uses the fixed 0 and 255 entries", "[art]")
{
	// a0 <= a1 selects the mode where index 6 is 0 and index 7 is 255.
	std::uint64_t indices = 0;
	for (std::uint64_t pixel = 0; pixel < 16; ++pixel) {
		indices |= (pixel == 0 ? 6ull : 7ull) << (3 * pixel);
	}
	const auto file = MakeDds(4, 4, "DXT5", Dxt5Block(0, 255, indices));
	REQUIRE(SeeThroughFraction(file.data(), file.size()) == Approx(1.0f / 16.0f));
}

TEST_CASE("alpha at the threshold is not see-through", "[art]")
{
	const auto file = MakeDds(4, 4, "DXT5", Dxt5Block(HONG::ArtMetrics::kClearAlpha, HONG::ArtMetrics::kClearAlpha, 0));
	REQUIRE(SeeThroughFraction(file.data(), file.size()) == Approx(0.0f));
}

TEST_CASE("DXT3 reads explicit 4-bit alpha", "[art]")
{
	const auto clear = MakeDds(4, 4, "DXT3", Dxt3Block(0x00));
	REQUIRE(SeeThroughFraction(clear.data(), clear.size()) == Approx(1.0f));
	const auto opaque = MakeDds(4, 4, "DXT3", Dxt3Block(0xFF));
	REQUIRE(SeeThroughFraction(opaque.data(), opaque.size()) == Approx(0.0f));
}

TEST_CASE("fractions average over every block", "[art]")
{
	auto blocks = Dxt5Block(0, 0, 0);
	const auto opaque = Dxt5Block(255, 255, 0);
	blocks.insert(blocks.end(), opaque.begin(), opaque.end());
	const auto file = MakeDds(8, 4, "DXT5", blocks);
	REQUIRE(SeeThroughFraction(file.data(), file.size()) == Approx(0.5f));
}

TEST_CASE("unsupported or malformed input returns -1", "[art]")
{
	REQUIRE(SeeThroughFraction(nullptr, 0) == Approx(-1.0f));

	const std::vector<std::uint8_t> tiny(16, 0);
	REQUIRE(SeeThroughFraction(tiny.data(), tiny.size()) == Approx(-1.0f));

	auto badMagic = MakeDds(4, 4, "DXT5", Dxt5Block(0, 0, 0));
	badMagic[0] = 'X';
	REQUIRE(SeeThroughFraction(badMagic.data(), badMagic.size()) == Approx(-1.0f));

	const auto dxt1 = MakeDds(4, 4, "DXT1", std::vector<std::uint8_t>(16, 0));
	REQUIRE(SeeThroughFraction(dxt1.data(), dxt1.size()) == Approx(-1.0f));

	const auto truncated = MakeDds(8, 4, "DXT5", Dxt5Block(0, 0, 0));  // needs two blocks, has one
	REQUIRE(SeeThroughFraction(truncated.data(), truncated.size()) == Approx(-1.0f));

	const auto zeroSize = MakeDds(0, 4, "DXT5", Dxt5Block(0, 0, 0));
	REQUIRE(SeeThroughFraction(zeroSize.data(), zeroSize.size()) == Approx(-1.0f));
}
