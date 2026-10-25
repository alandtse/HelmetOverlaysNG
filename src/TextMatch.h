// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Modding-Exception
// Copyright (c) 2026 HelmetOverlaysNG contributors. See COPYING and EXCEPTIONS.md.

#pragma once

#include <algorithm>
#include <cctype>
#include <string_view>

namespace HONG
{
	// Case-insensitive substring test; an empty needle matches everything. Shared by every filter box.
	inline bool ContainsNoCase(std::string_view a_haystack, std::string_view a_needle)
	{
		if (a_needle.empty()) {
			return true;
		}
		const auto it = std::ranges::search(a_haystack, a_needle, [](char a, char b) {
			return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
		});
		return !it.empty();
	}
}
