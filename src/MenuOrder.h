// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Modding-Exception
// Copyright (c) 2026 HelmetOverlaysNG contributors. See COPYING and EXCEPTIONS.md.

#pragma once

#include <cstddef>
#include <span>

namespace HONG::MenuOrder
{
	struct Entry
	{
		bool isHud;
		bool hasMovie;
	};

	inline constexpr std::size_t kNone = static_cast<std::size_t>(-1);

	/// Index of the first menu in draw order that has a movie, stopping at the HUD. A menu below the HUD's depth
	/// precedes it in the stack and so draws before it; kNone when the stack holds neither.
	inline std::size_t FirstDrawn(std::span<const Entry> a_stack)
	{
		for (std::size_t i = 0; i < a_stack.size(); ++i) {
			if (a_stack[i].isHud || a_stack[i].hasMovie) {
				return i;
			}
		}
		return kNone;
	}
}
