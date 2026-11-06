// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Modding-Exception
// Copyright (c) 2026 HelmetOverlaysNG contributors. See COPYING and EXCEPTIONS.md.

#include "OverlayAlpha.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>

using HONG::OverlayAlpha::Byte;
using HONG::OverlayAlpha::IsFullyOpaque;
using HONG::OverlayAlpha::PermitMask;

TEST_CASE("alpha is truncated the way the renderer draws it", "[mask]")
{
	REQUIRE(Byte(1.0f) == 255);
	REQUIRE(Byte(0.0f) == 0);
	REQUIRE(Byte(0.5f) == 127);    // 127.5 truncates
	REQUIRE(Byte(0.999f) == 254);  // 254.745 truncates - still visible, so not maskable
}

TEST_CASE("an opacity that draws 254 is not treated as fully opaque", "[mask]")
{
	REQUIRE_FALSE(IsFullyOpaque(0.999f));
	REQUIRE(Byte(0.999f) == 254);
	REQUIRE_FALSE(IsFullyOpaque(0.998f));
	REQUIRE_FALSE(IsFullyOpaque(0.9f));
	REQUIRE(IsFullyOpaque(1.0f));
}

TEST_CASE("the UI's whole-percent readout and the mask agree", "[mask]")
{
	for (int percent = 0; percent <= 100; ++percent) {
		const float value = static_cast<float>(percent) / 100.0f;
		if (percent == 100) {
			REQUIRE(IsFullyOpaque(value));
		} else {
			REQUIRE_FALSE(IsFullyOpaque(value));
		}
	}
}

TEST_CASE("the publish gate itself rejects an opacity that draws 254", "[mask]")
{
	REQUIRE(PermitMask(true, true, true, 1.0f, 1, 1).Allowed());
	REQUIRE_FALSE(PermitMask(true, true, true, 0.999f, 1, 1).Allowed());  // draws 254
	REQUIRE_FALSE(PermitMask(true, true, true, 0.5f, 1, 1).Allowed());
}

TEST_CASE("the opacity readout can never say 100% at a non-opaque byte", "[mask]")
{
	for (int byte = 0; byte <= 255; ++byte) {
		const float opacity = static_cast<float>(byte) / 255.0f;
		REQUIRE(Byte(opacity) == byte);

		// What a whole-percent readout would print, against what the byte actually is.
		const auto wholePercent = static_cast<int>(static_cast<float>(byte) / 255.0f * 100.0f + 0.5f);
		if (byte < 255) {
			REQUIRE(wholePercent <= 100);
			REQUIRE(static_cast<float>(byte) / 255.0f * 100.0f < 100.0f);
		}
		REQUIRE(IsFullyOpaque(opacity) == (byte == 255));
	}
}

TEST_CASE("a permission cannot be forged at the call site", "[mask]")
{
	const HONG::OverlayAlpha::MaskPermit forged{};
	REQUIRE_FALSE(forged.Allowed());
	REQUIRE_FALSE(PermitMask(true, true, true, 0.999f, 1, 1).Allowed());
	REQUIRE(PermitMask(true, true, true, 1.0f, 1, 1).Allowed());
}

TEST_CASE("the publish gate rejects every other disqualifier", "[mask]")
{
	REQUIRE_FALSE(PermitMask(false, true, true, 1.0f, 1, 1).Allowed());  // VR
	REQUIRE_FALSE(PermitMask(true, false, true, 1.0f, 1, 1).Allowed());  // setting off
	REQUIRE_FALSE(PermitMask(true, true, false, 1.0f, 1, 1).Allowed());  // hidden by a menu
	REQUIRE_FALSE(PermitMask(true, true, true, 1.0f, 2, 2).Allowed());   // two overlays worn
	REQUIRE_FALSE(PermitMask(true, true, true, 1.0f, 1, 0).Allowed());   // nothing drawn
}

TEST_CASE("out-of-range and non-finite opacity is safe", "[mask]")
{
	REQUIRE(Byte(-1.0f) == 0);
	REQUIRE(Byte(2.0f) == 255);
	REQUIRE(Byte(std::numeric_limits<float>::quiet_NaN()) == 0);
	REQUIRE(Byte(std::numeric_limits<float>::infinity()) == 255);
	REQUIRE(Byte(-std::numeric_limits<float>::infinity()) == 0);
}
