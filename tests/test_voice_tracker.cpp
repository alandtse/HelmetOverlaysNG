// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Modding-Exception
// Copyright (c) 2026 HelmetOverlaysNG contributors. See COPYING and EXCEPTIONS.md.

#include <catch2/catch_test_macros.hpp>

#include "VoiceTracker.h"

using HONG::VoiceTracker;

namespace
{
	int g_a, g_b;
}

TEST_CASE("a remembered voice is forgotten once", "[voices]")
{
	VoiceTracker voices;
	voices.Remember(&g_a);
	REQUIRE(voices.Forget(&g_a));
	REQUIRE_FALSE(voices.Forget(&g_a));
	REQUIRE_FALSE(voices.Forget(&g_b));
}

TEST_CASE("nothing is released when no voice was written", "[voices]")
{
	VoiceTracker voices;
	REQUIRE_FALSE(voices.Releasing(1000));
}

TEST_CASE("tracked voices are released inside the window and dropped after it", "[voices]")
{
	VoiceTracker voices;
	voices.Remember(&g_a);
	REQUIRE(voices.Releasing(1000));
	REQUIRE(voices.Releasing(1000 + VoiceTracker::kReleaseWindowMs));
	REQUIRE_FALSE(voices.Releasing(1001 + VoiceTracker::kReleaseWindowMs));
	REQUIRE_FALSE(voices.Forget(&g_a));
}

TEST_CASE("writing a voice again restarts the release window", "[voices]")
{
	VoiceTracker voices;
	voices.Remember(&g_a);
	REQUIRE(voices.Releasing(1000));
	voices.Remember(&g_b);
	REQUIRE(voices.Releasing(1000 + VoiceTracker::kReleaseWindowMs + 500));
}

TEST_CASE("the tracked set is bounded", "[voices]")
{
	VoiceTracker voices;
	static int ids[VoiceTracker::kMaxTracked + 1];
	for (auto& id : ids) {
		voices.Remember(&id);
	}
	REQUIRE_FALSE(voices.Forget(&ids[0]));
	REQUIRE(voices.Forget(&ids[VoiceTracker::kMaxTracked]));
}
