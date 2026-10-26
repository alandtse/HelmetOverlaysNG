// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Modding-Exception
// Copyright (c) 2026 HelmetOverlaysNG contributors. See COPYING and EXCEPTIONS.md.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "SoundEffect.h"

using Catch::Approx;
using namespace HONG::SoundEffect;

TEST_CASE("volume runs from untouched to the full target", "[sound]")
{
	REQUIRE(Volume(0.0f, 0.6f, 0.5f) == Approx(1.0f));
	REQUIRE(Volume(1.0f, 0.6f, 0.5f) == Approx(0.6f));
}

TEST_CASE("a shape above zero lags the attenuation", "[sound]")
{
	REQUIRE(Volume(0.5f, 0.6f, 1.0f) > Volume(0.5f, 0.6f, 0.0f));
	REQUIRE(Volume(0.5f, 0.6f, 1.0f) == Approx(1.0f - 0.4f * 0.25f));
}

TEST_CASE("without a shape any amount applies the full target", "[sound]")
{
	REQUIRE(Volume(0.1f, 0.6f, 0.0f) == Approx(0.6f));
	REQUIRE(Volume(0.0f, 0.6f, 0.0f) == Approx(1.0f));
}

TEST_CASE("the cutoff is fully open when bare and at the target when worn", "[sound]")
{
	REQUIRE(CutoffHz(0.0f, 1800.0f, 48000.0f) == Approx(8000.0f));
	REQUIRE(CutoffHz(1.0f, 1800.0f, 48000.0f) == Approx(1800.0f));
}

TEST_CASE("the cutoff falls logarithmically", "[sound]")
{
	REQUIRE(CutoffHz(0.5f, 2000.0f, 48000.0f) == Approx(4000.0f));
}

TEST_CASE("the cutoff target is kept inside the audible range", "[sound]")
{
	REQUIRE(CutoffHz(1.0f, 10.0f, 48000.0f) == Approx(kMinCutoffHz));
	REQUIRE(CutoffHz(1.0f, 20000.0f, 48000.0f) == Approx(8000.0f));
}
