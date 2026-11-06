// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Modding-Exception
// Copyright (c) 2026 HelmetOverlaysNG contributors. See COPYING and EXCEPTIONS.md.

#include "AnimStyleCase.h"
#include "HelmetProfile.h"

#include <catch2/catch_test_macros.hpp>

#include <set>
#include <string>

TEST_CASE("every animation style has a name of its own", "[style]")
{
	std::set<std::string> names;
	for (const auto style : HONG::AnimStyleCase::kAllStyles) {
		names.insert(std::string{ HONG::AnimStyleCase::Name(style) });
	}
	CHECK(names.size() == std::size(HONG::AnimStyleCase::kAllStyles));
	CHECK(std::size(HONG::AnimStyleCase::kAllStyles) == 3);
}

TEST_CASE("exactly one animation applies to a style", "[style]")
{
	for (const auto style : HONG::AnimStyleCase::kAllStyles) {
		const bool settles = HONG::AnimStyleCase::Settles(style);
		const bool slides = HONG::AnimStyleCase::SlidesDown(style);
		CHECK_FALSE((settles && slides));
	}
	// Fade is the only style with no movement, and it is named for that.
	CHECK_FALSE(HONG::AnimStyleCase::Settles(HONG::AnimStyle::kFade));
	CHECK_FALSE(HONG::AnimStyleCase::SlidesDown(HONG::AnimStyle::kFade));
	CHECK(HONG::AnimStyleCase::Settles(HONG::AnimStyle::kSettle));
	CHECK(HONG::AnimStyleCase::SlidesDown(HONG::AnimStyle::kSlide));
}

TEST_CASE("an unknown armour class is treated as light on purpose", "[style]")
{
	const auto opennessFor = [](HONG::Profile::ArmorClass a_class) {
		return HONG::Profile::Compute(1.0f, a_class, 1.0f).armorFactor;
	};
	CHECK(opennessFor(HONG::Profile::ArmorClass::kUnknown) == opennessFor(HONG::Profile::ArmorClass::kLight));
	CHECK(opennessFor(HONG::Profile::ArmorClass::kClothing) != opennessFor(HONG::Profile::ArmorClass::kHeavy));
	CHECK(opennessFor(HONG::Profile::ArmorClass::kLight) != opennessFor(HONG::Profile::ArmorClass::kHeavy));
}
