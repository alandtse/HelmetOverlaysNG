// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Modding-Exception
// Copyright (c) 2026 HelmetOverlaysNG contributors. See COPYING and EXCEPTIONS.md.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "HelmetProfile.h"

using Catch::Approx;
using HONG::Profile::ArmorClass;
using HONG::Profile::Compute;

TEST_CASE("without art the armor type decides", "[profile]")
{
	REQUIRE(Compute(-1.0f, ArmorClass::kHeavy, 0.65f).strength == Approx(1.0f));
	REQUIRE(Compute(-1.0f, ArmorClass::kLight, 0.65f).strength == Approx(0.70f));
	REQUIRE(Compute(-1.0f, ArmorClass::kClothing, 0.65f).strength == Approx(0.40f));
	REQUIRE(Compute(-1.0f, ArmorClass::kHeavy, 0.65f).artFactor < 0.0f);
}

TEST_CASE("a closed heavy helm muffles fully", "[profile]")
{
	REQUIRE(Compute(0.0f, ArmorClass::kHeavy, 0.65f).strength == Approx(1.0f));
	REQUIRE(Compute(HONG::Profile::kClosedOpenness, ArmorClass::kHeavy, 0.65f).strength == Approx(1.0f));
}

TEST_CASE("a hood barely muffles", "[profile]")
{
	const auto hood = Compute(0.92f, ArmorClass::kClothing, 0.65f);
	REQUIRE(hood.artFactor == Approx(0.0f));
	REQUIRE(hood.strength == Approx(0.35f * 0.40f).margin(1e-6));
}

TEST_CASE("strength never falls below the floor", "[profile]")
{
	REQUIRE(Compute(0.95f, ArmorClass::kClothing, 1.0f).strength == Approx(HONG::Profile::kMinStrength));
}

TEST_CASE("a more open overlay never muffles more", "[profile]")
{
	float previous = 2.0f;
	for (float openness = 0.0f; openness <= 1.0f; openness += 0.05f) {
		const float strength = Compute(openness, ArmorClass::kHeavy, 0.65f).strength;
		REQUIRE(strength <= previous + 1e-6f);
		previous = strength;
	}
}

TEST_CASE("the art weight blends art and armor", "[profile]")
{
	const float mid = HONG::Profile::kClosedOpenness + 0.5f * (HONG::Profile::kOpenOpenness - HONG::Profile::kClosedOpenness);
	REQUIRE(Compute(mid, ArmorClass::kHeavy, 0.0f).strength == Approx(1.0f));
	REQUIRE(Compute(mid, ArmorClass::kHeavy, 1.0f).strength == Approx(0.5f));
	REQUIRE(Compute(mid, ArmorClass::kHeavy, 0.5f).strength == Approx(0.75f));
	REQUIRE(Compute(mid, ArmorClass::kHeavy, 5.0f).strength == Approx(0.5f));  // weight is clamped to 0..1
}
