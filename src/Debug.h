// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Modding-Exception
// Copyright (c) 2026 HelmetOverlaysNG contributors. See COPYING and EXCEPTIONS.md.
//
// Data and controls behind the in-game debug page. Self-contained (no PCH, no
// CommonLib types) so the SKSE Menu Framework page, which builds in its own
// TU, can use it. Implemented in Debug.cpp.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace HONG::Debug
{
	struct HelmetEntry
	{
		std::uint32_t id{ 0 };
		std::string name;
		std::string plugin;
	};

	// Every named armor that occupies the head or hair slot, sorted by name.
	// Built on first use (after data load) and cached.
	const std::vector<HelmetEntry>& Helmets();

	struct Current
	{
		std::uint32_t armorId{ 0 };  // 0 = nothing resolved yet / bare head
		std::string armorName;
		bool previewing{ false };
		std::string model;  // the armor's world model path
		std::string overlayPath;
		bool found{ false };
		std::vector<std::string> tried;
	};
	Current GetCurrent();

	// Draw this armor's overlay without equipping it; 0 / ClearPreview() goes
	// back to the worn helmet.
	void Preview(std::uint32_t a_armorId);
	void ClearPreview();
	std::uint32_t Previewing();

	// Names of every menu the game has registered, sorted - the choices for
	// "hide the overlay while this menu is open".
	std::vector<std::string> RegisteredMenus();
}
