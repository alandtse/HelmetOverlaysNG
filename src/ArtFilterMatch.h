// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Modding-Exception
// Copyright (c) 2026 HelmetOverlaysNG contributors. See COPYING and EXCEPTIONS.md.

#pragma once

#include "ArtMetrics.h"
#include "Debug.h"

namespace HONG::ArtFilterMatch
{
	using HONG::Debug::ArtFilter;

	/// Whether one helmet entry passes the preview's art filter. No default case: MSVC /W4 omits C4062, so the
	/// pragma below makes an unhandled filter a compile error instead of silently matching every helmet.
	inline bool Matches(std::uint32_t a_art, float a_color, ArtFilter a_filter)
	{
#pragma warning(push)
#pragma warning(error: 4062)  // enumerator not handled: /W4 does not report this
		switch (a_filter) {
		case ArtFilter::kMissing:
			return a_art == 0;
		case ArtFilter::kColored:
			return a_color >= ArtMetrics::kColoredShare;
		case ArtFilter::kBlack:
			return a_color >= 0.0f && a_color < ArtMetrics::kColoredShare;
		case ArtFilter::kUnchecked:
			return a_art > 0 && a_color < 0.0f;
		case ArtFilter::kAll:
			return true;
		}
#pragma warning(pop)
		return true;
	}

	/// Every filter that exists, so a test can insist each one is distinguishable and that a new one cannot
	/// join without the list being extended. Kept next to Matches because the two must agree.
	inline constexpr ArtFilter kAllFilters[] = {
		ArtFilter::kAll,
		ArtFilter::kMissing,
		ArtFilter::kColored,
		ArtFilter::kBlack,
		ArtFilter::kUnchecked,
	};
}
