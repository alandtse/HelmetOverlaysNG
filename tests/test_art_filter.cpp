// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Modding-Exception
// Copyright (c) 2026 HelmetOverlaysNG contributors. See COPYING and EXCEPTIONS.md.

#include "ArtFilterMatch.h"

#include <catch2/catch_test_macros.hpp>

#include <string_view>
#include <vector>

TEST_CASE("every art filter is distinct and none matches everything by accident", "[filter]")
{
	struct Sample
	{
		std::uint32_t art;
		float color;
		std::string_view what;
	};
	constexpr Sample samples[] = {
		{ 0, -1.0f, "no art" },
		{ 1, 0.9f, "colored art" },
		{ 1, 0.0f, "black mask" },
		{ 1, -1.0f, "unchecked art" },
	};

	std::vector<std::string> signatures;
	for (const auto filter : HONG::ArtFilterMatch::kAllFilters) {
		std::string signature;
		for (const auto& sample : samples) {
			signature += HONG::ArtFilterMatch::Matches(sample.art, sample.color, filter) ? '1' : '0';
		}
		// kAll is the only filter allowed to accept everything, and it is named that way on purpose.
		if (filter != HONG::Debug::ArtFilter::kAll) {
			CHECK(signature != "1111");
		}
		CHECK(std::ranges::find(signatures, signature) == signatures.end());
		signatures.push_back(signature);
	}
	CHECK(signatures.size() == 5);
}

TEST_CASE("each filter selects what its name says", "[filter]")
{
	using HONG::ArtFilterMatch::Matches;

	// "no overlay art" must be exactly the artless entries, not a superset that also catches unread ones.
	CHECK(Matches(0, -1.0f, HONG::Debug::ArtFilter::kMissing));
	CHECK_FALSE(Matches(1, 0.9f, HONG::Debug::ArtFilter::kMissing));

	CHECK(Matches(1, 0.9f, HONG::Debug::ArtFilter::kColored));
	CHECK_FALSE(Matches(1, HONG::ArtMetrics::kColoredShare * 0.5f, HONG::Debug::ArtFilter::kColored));
	CHECK(Matches(1, HONG::ArtMetrics::kColoredShare, HONG::Debug::ArtFilter::kColored));

	// Black is the complement WITHIN classified art: it must not claim entries nobody has read yet.
	CHECK(Matches(1, 0.0f, HONG::Debug::ArtFilter::kBlack));
	CHECK_FALSE(Matches(1, -1.0f, HONG::Debug::ArtFilter::kBlack));
	CHECK_FALSE(Matches(1, 0.9f, HONG::Debug::ArtFilter::kBlack));

	CHECK(Matches(1, -1.0f, HONG::Debug::ArtFilter::kUnchecked));
	CHECK_FALSE(Matches(0, -1.0f, HONG::Debug::ArtFilter::kUnchecked));
	CHECK_FALSE(Matches(1, 0.9f, HONG::Debug::ArtFilter::kUnchecked));
	CHECK_FALSE(Matches(1, 0.0f, HONG::Debug::ArtFilter::kUnchecked));

	// kAll is the identity filter and has to accept every sample, including the artless one.
	for (int i = 0; i < 4; ++i) {
		CHECK(Matches(i == 0 ? 0u : 1u, i == 1 ? 0.9f : (i == 2 ? 0.0f : -1.0f), HONG::Debug::ArtFilter::kAll));
	}
}
