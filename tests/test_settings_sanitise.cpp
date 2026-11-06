// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Modding-Exception
// Copyright (c) 2026 HelmetOverlaysNG contributors. See COPYING and EXCEPTIONS.md.

#include "Settings.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>

TEST_CASE("a non-finite value falls back to the default, never to a clamp", "[settings]")
{
	constexpr auto range = HONG::Range::kScale;
	constexpr float fallback = 1.0f;

	REQUIRE(HONG::Sanitise(std::numeric_limits<float>::infinity(), fallback, range) == fallback);
	REQUIRE(HONG::Sanitise(-std::numeric_limits<float>::infinity(), fallback, range) == fallback);
	REQUIRE(HONG::Sanitise(std::numeric_limits<float>::quiet_NaN(), fallback, range) == fallback);
}

TEST_CASE("both ends of the range are enforced, not just one", "[settings]")
{
	constexpr auto range = HONG::Range::kScale;  // 0.5 .. 3.0
	constexpr float fallback = 1.0f;

	REQUIRE(HONG::Sanitise(0.09f, fallback, range) == range.min);
	REQUIRE(HONG::Sanitise(99.0f, fallback, range) == range.max);

	// The boundaries themselves are inside the range and must pass through untouched.
	REQUIRE(HONG::Sanitise(range.min, fallback, range) == range.min);
	REQUIRE(HONG::Sanitise(range.max, fallback, range) == range.max);

	REQUIRE(HONG::Sanitise(1.25f, fallback, range) == 1.25f);
}

TEST_CASE("every tunable's range is usable as a clamp", "[settings]")
{
	constexpr HONG::Bounds all[] = {
		HONG::Range::kUnit,
		HONG::Range::kOpacity,
		HONG::Range::kScale,
		HONG::Range::kVerticalOffset,
		HONG::Range::kLightStrength,
		HONG::Range::kLightFade,
		HONG::Range::kLightInterval,
		HONG::Range::kFade,
		HONG::Range::kSwayStrength,
		HONG::Range::kMotionStrength,
		HONG::Range::kVrDepth,
		HONG::Range::kVrCoverage,
		HONG::Range::kMaskMargin,
		HONG::Range::kAutoArtWeight,
		HONG::Range::kAttenuateVolume,
		HONG::Range::kAttenuateLag,
		HONG::Range::kLowPassHz,
	};
	for (const auto& range : all) {
		CHECK(range.min < range.max);
		CHECK(std::isfinite(range.min));
		CHECK(std::isfinite(range.max));
		// A value far below and far above must land exactly on the two ends.
		CHECK(HONG::Sanitise(-1.0e30f, range.min, range) == range.min);
		CHECK(HONG::Sanitise(1.0e30f, range.max, range) == range.max);
	}
}

TEST_CASE("the VR depth floor is a renderer rule, not a reader rule", "[settings]")
{
	constexpr auto range = HONG::Range::kVrDepth;
	REQUIRE(HONG::Sanitise(0.0f, 1.0f, range) == 0.0f);
	REQUIRE(HONG::Sanitise(0.08f, 1.0f, range) == 0.08f);
	REQUIRE(HONG::Sanitise(9.0f, 1.0f, range) == range.max);
}
