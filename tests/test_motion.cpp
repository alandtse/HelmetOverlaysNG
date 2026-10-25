// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Modding-Exception
// Copyright (c) 2026 HelmetOverlaysNG contributors. See COPYING and EXCEPTIONS.md.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "Motion.h"

using Catch::Approx;
using namespace HONG::Motion;

TEST_CASE("sampling interpolates between keys and holds the last", "[motion]")
{
	const Key keys[] = { { 0.0f, 0.0f, 0.0f }, { 1.0f, 2.0f, 4.0f } };
	REQUIRE(Sample(keys, 2, 0.5f).x == Approx(1.0f));
	REQUIRE(Sample(keys, 2, 0.5f).y == Approx(2.0f));
	REQUIRE(Sample(keys, 2, 3.0f).x == Approx(2.0f));
	REQUIRE(Sample(keys, 0, 0.5f).x == 0.0f);
}

TEST_CASE("a jump drops the overlay and returns it to rest", "[motion]")
{
	Player player;
	State state;
	player.Update(0.016f, state, 1.0f);
	state.airborne = true;
	player.Update(0.1f, state, 1.0f);
	REQUIRE(player.Current().y > 0.0f);
	REQUIRE(player.Current().x == 0.0f);
	for (int i = 0; i < 40; ++i) {
		player.Update(0.016f, state, 1.0f);
	}
	REQUIRE(player.Current().y == Approx(0.0f));
}

TEST_CASE("strafing nudges toward the strafe side once per press", "[motion]")
{
	Player player;
	State state;
	state.strafe = -1.0f;
	player.Update(0.1f, state, 1.0f);
	REQUIRE(player.Current().x < 0.0f);
	for (int i = 0; i < 60; ++i) {
		player.Update(0.016f, state, 1.0f);
	}
	REQUIRE(player.Current().x == Approx(0.0f));

	state.strafe = 0.0f;
	player.Update(0.016f, state, 1.0f);
	state.strafe = 1.0f;
	player.Update(0.1f, state, 1.0f);
	REQUIRE(player.Current().x > 0.0f);
}

TEST_CASE("sprinting holds an offset and eases back when it ends", "[motion]")
{
	Player player;
	State state;
	state.sprinting = true;
	for (int i = 0; i < 60; ++i) {
		player.Update(0.016f, state, 1.0f);
	}
	REQUIRE(player.Current().y < 0.0f);

	state.sprinting = false;
	for (int i = 0; i < 40; ++i) {
		player.Update(0.016f, state, 1.0f);
	}
	REQUIRE(player.Current().x == Approx(0.0f));
	REQUIRE(player.Current().y == Approx(0.0f));
}

TEST_CASE("zero strength stops all motion", "[motion]")
{
	Player player;
	State state;
	state.airborne = true;
	player.Update(0.1f, state, 0.0f);
	REQUIRE(player.Current().y == 0.0f);
}
