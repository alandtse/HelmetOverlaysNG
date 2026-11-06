// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Modding-Exception
// Copyright (c) 2026 HelmetOverlaysNG contributors. See COPYING and EXCEPTIONS.md.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <limits>

#include "ArtFit.h"

using Catch::Approx;
using namespace HONG::ArtFit;

namespace
{
	constexpr float kArt = 1920.0f / 1080.0f;
}

TEST_CASE("cover on a matching display fills it exactly", "[artfit]")
{
	const auto f = Cover(kArt, kArt);
	REQUIRE(f.x == Approx(1.0f));
	REQUIRE(f.y == Approx(1.0f));
}

TEST_CASE("cover on a wider display crops the height", "[artfit]")
{
	const auto f = Cover(21.0f / 9.0f, kArt);
	REQUIRE(f.x == Approx(1.0f));
	REQUIRE(f.y == Approx((21.0f / 9.0f) / kArt));
}

TEST_CASE("cover on a narrower display crops the width", "[artfit]")
{
	const auto f = Cover(1.0f, kArt);
	REQUIRE(f.x == Approx(kArt));
	REQUIRE(f.y == Approx(1.0f));
}

TEST_CASE("fit with no stretch keeps the art's proportions inside a narrow view", "[artfit]")
{
	const auto f = Fit(0.94f, kArt, 0.0f);
	REQUIRE(f.x == Approx(1.0f));
	REQUIRE(f.y == Approx(0.94f / kArt));
	REQUIRE((f.x * 0.94f) / f.y == Approx(kArt));
}

TEST_CASE("fit with full stretch fills the view", "[artfit]")
{
	const auto f = Fit(0.94f, kArt, 1.0f);
	REQUIRE(f.x == Approx(1.0f));
	REQUIRE(f.y == Approx(1.0f));
}

TEST_CASE("fit blends between the two", "[artfit]")
{
	const auto f = Fit(0.94f, kArt, 0.5f);
	REQUIRE(f.y == Approx(0.5f * (1.0f + 0.94f / kArt)));
}

TEST_CASE("fit on a wider view than the art keeps the height and narrows the width", "[artfit]")
{
	const auto f = Fit(2.5f, kArt, 0.0f);
	REQUIRE(f.x == Approx(kArt / 2.5f));
	REQUIRE(f.y == Approx(1.0f));
}

TEST_CASE("fit clamps the stretch", "[artfit]")
{
	REQUIRE(Fit(0.94f, kArt, -1.0f).y == Approx(Fit(0.94f, kArt, 0.0f).y));
	REQUIRE(Fit(0.94f, kArt, 4.0f).y == Approx(1.0f));
}

TEST_CASE("art fit undoes the plane's coverage so the eye openings stay flat-screen sized", "[artfit]")
{
	// Coverage 1.0 is the identity: the plane already matches the view.
	const auto unity = ArtFitFor(1.0f);
	REQUIRE(unity.x == Approx(1.0f));
	REQUIRE(unity.y == Approx(1.0f));
	for (const float coverage : { 0.5f, 0.8f, 1.0f, 1.25f, 1.6f }) {
		const auto fit = ArtFitFor(coverage);
		REQUIRE(fit.x == Approx(1.0f / coverage));
		REQUIRE(fit.y == Approx(1.0f / coverage));
	}
}

TEST_CASE("art fit is 1:1 when the plane is not in use or a value is unusable", "[artfit]")
{
	for (const float bad : { 0.0f, -1.0f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity() }) {
		const auto fit = ArtFitFor(bad);
		REQUIRE(fit.x == Approx(1.0f));
		REQUIRE(fit.y == Approx(1.0f));
	}
}
