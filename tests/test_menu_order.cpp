// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Modding-Exception
// Copyright (c) 2026 HelmetOverlaysNG contributors. See COPYING and EXCEPTIONS.md.

#include "MenuOrder.h"

#include <catch2/catch_test_macros.hpp>

#include <vector>

using HONG::MenuOrder::Entry;
using HONG::MenuOrder::FirstDrawn;
using HONG::MenuOrder::kNone;

TEST_CASE("the HUD is first when nothing precedes it", "[menuorder]")
{
	const std::vector<Entry> stack{ { true, true }, { false, true }, { false, false } };
	REQUIRE(FirstDrawn(stack) == 0);
}

TEST_CASE("a widget menu below the HUD's depth is drawn first", "[menuorder]")
{
	const std::vector<Entry> stack{ { false, true }, { true, true }, { false, true } };
	REQUIRE(FirstDrawn(stack) == 0);
}

TEST_CASE("menus without a movie are skipped", "[menuorder]")
{
	const std::vector<Entry> stack{ { false, false }, { false, true }, { true, true } };
	REQUIRE(FirstDrawn(stack) == 1);

	const std::vector<Entry> onlyMovielessBeforeHud{ { false, false }, { true, true } };
	REQUIRE(FirstDrawn(onlyMovielessBeforeHud) == 1);
}

TEST_CASE("menus after the HUD never win", "[menuorder]")
{
	const std::vector<Entry> stack{ { true, true }, { false, true } };
	REQUIRE(FirstDrawn(stack) == 0);
}

TEST_CASE("an empty or HUD-less stack has no first menu", "[menuorder]")
{
	REQUIRE(FirstDrawn({}) == kNone);

	const std::vector<Entry> noMovies{ { false, false }, { false, false } };
	REQUIRE(FirstDrawn(noMovies) == kNone);
}
