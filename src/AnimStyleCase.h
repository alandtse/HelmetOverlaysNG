// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Modding-Exception
// Copyright (c) 2026 HelmetOverlaysNG contributors. See COPYING and EXCEPTIONS.md.

#pragma once

#include "Settings.h"

#include <string_view>
#include <type_traits>

namespace HONG::AnimStyleCase
{
	/// The two decisions over AnimStyle, kept together because they must agree. Neither switch has a default:
	/// MSVC /W4 omits C4062, so the pragma (repeated per switch, as it is scoped) makes an unhandled style a compile error.

	/// The INI name written for a style, and read back by ParseStyle. Must be a bijection: two styles sharing a
	/// name means one of them cannot be selected at all.
	inline std::string_view Name(AnimStyle a_style)
	{
#pragma warning(push)
#pragma warning(error: 4062)
		switch (a_style) {
		case AnimStyle::kFade:
			return "Fade";
		case AnimStyle::kSettle:
			return "Settle";
		case AnimStyle::kSlide:
			return "Slide";
		}
#pragma warning(pop)
		return "Settle";
	}

	/// How much this style scales the overlay as it seats. Fade and Slide are handled by their own outputs, so
	/// zero here is a real answer rather than an absent case.
	inline bool SlidesDown(AnimStyle a_style)
	{
#pragma warning(push)
#pragma warning(error: 4062)
		switch (a_style) {
		case AnimStyle::kFade:
			return false;
		case AnimStyle::kSettle:
			return false;
		case AnimStyle::kSlide:
			return true;
		}
#pragma warning(pop)
		return false;
	}

	inline bool Settles(AnimStyle a_style)
	{
#pragma warning(push)
#pragma warning(error: 4062)
		switch (a_style) {
		case AnimStyle::kFade:
			return false;
		case AnimStyle::kSettle:
			return true;
		case AnimStyle::kSlide:
			return false;
		}
#pragma warning(pop)
		return false;
	}

	/// Name returns a view of a string literal, so .data() is null-terminated and lives for the program.
	/// StyleName hands that pointer to Callbacks that expect `const char*`; this assert records the requirement
	/// so a future edit returning a built string does not silently dangle.
	static_assert(std::is_same_v<decltype(Name(AnimStyle::kFade)), std::string_view>, "Name must return a view of static storage; StyleName returns .data() to the INI layer");

	/// Every style, so a test can insist each has a distinct name and that exactly one animation applies.
	inline constexpr AnimStyle kAllStyles[] = {
		AnimStyle::kFade,
		AnimStyle::kSettle,
		AnimStyle::kSlide,
	};
}
