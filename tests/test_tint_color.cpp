// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Modding-Exception
// Copyright (c) 2026 HelmetOverlaysNG contributors. See COPYING and EXCEPTIONS.md.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "TintColor.h"

#include <vector>

using Catch::Approx;
using namespace HONG::Tint;

TEST_CASE("a grey scene leaves the overlay untouched", "[tint]")
{
	const auto tint = Chroma({ 0.4f, 0.4f, 0.4f }, 1.0f);
	REQUIRE(tint.r == Approx(1.0f));
	REQUIRE(tint.g == Approx(1.0f));
	REQUIRE(tint.b == Approx(1.0f));
}

TEST_CASE("firelight shifts the overlay toward orange", "[tint]")
{
	const auto tint = Chroma({ 8.0f, 3.2f, 0.8f }, 1.0f);
	REQUIRE(tint.r == Approx(1.0f));
	REQUIRE(tint.g == Approx(0.4f));
	REQUIRE(tint.b == Approx(0.1f));
}

TEST_CASE("strength scales between white and the full hue", "[tint]")
{
	const auto half = Chroma({ 8.0f, 3.2f, 0.8f }, 0.5f);
	REQUIRE(half.g == Approx(0.7f));
	REQUIRE(half.b == Approx(0.55f));
	REQUIRE(Chroma({ 8.0f, 3.2f, 0.8f }, 0.0f).b == Approx(1.0f));
}

TEST_CASE("a black scene gives no tint", "[tint]")
{
	const auto tint = Chroma({ 0.0f, 0.0f, 0.0f }, 1.0f);
	REQUIRE(tint.r == Approx(1.0f));
	REQUIRE(tint.b == Approx(1.0f));
}

TEST_CASE("the ring average ignores the interior", "[tint]")
{
	std::vector<Rgb> cells(5 * 4, Rgb{ 0.0f, 0.0f, 0.0f });
	for (std::uint32_t y = 1; y < 3; ++y) {
		for (std::uint32_t x = 1; x < 4; ++x) {
			cells[y * 5 + x] = { 100.0f, 100.0f, 100.0f };
		}
	}
	const auto average = RingAverage(cells.data(), 5, { 0, 0, 5, 4 });
	REQUIRE(average.r == Approx(0.0f));
}

TEST_CASE("the ring average counts every border cell once", "[tint]")
{
	std::vector<Rgb> cells(3 * 3, Rgb{ 1.0f, 2.0f, 4.0f });
	const auto average = RingAverage(cells.data(), 3, { 0, 0, 3, 3 });
	REQUIRE(average.r == Approx(1.0f));
	REQUIRE(average.g == Approx(2.0f));
	REQUIRE(average.b == Approx(4.0f));
}

TEST_CASE("approach moves by at most the step", "[tint]")
{
	const auto next = Approach({ 1.0f, 1.0f, 1.0f }, { 0.0f, 0.95f, 1.0f }, 0.1f);
	REQUIRE(next.r == Approx(0.9f));
	REQUIRE(next.g == Approx(0.95f));
	REQUIRE(next.b == Approx(1.0f));
}

TEST_CASE("a ring average reads only its own region of the grid", "[tint]")
{
	std::vector<Rgb> cells(8 * 3, Rgb{ 1.0f, 1.0f, 1.0f });
	for (std::uint32_t y = 0; y < 3; ++y) {
		for (std::uint32_t x = 4; x < 8; ++x) {
			cells[y * 8 + x] = { 5.0f, 5.0f, 5.0f };
		}
	}
	REQUIRE(RingAverage(cells.data(), 8, { 0, 0, 4, 3 }).r == Approx(1.0f));
	REQUIRE(RingAverage(cells.data(), 8, { 4, 0, 4, 3 }).r == Approx(5.0f));
}

TEST_CASE("the mean of two colors is per channel", "[tint]")
{
	const auto mean = Mean({ 1.0f, 2.0f, 3.0f }, { 3.0f, 2.0f, 1.0f });
	REQUIRE(mean.r == Approx(2.0f));
	REQUIRE(mean.g == Approx(2.0f));
	REQUIRE(mean.b == Approx(2.0f));
}
