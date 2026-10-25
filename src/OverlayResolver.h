// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Modding-Exception
// Copyright (c) 2026 HelmetOverlaysNG contributors. See COPYING and EXCEPTIONS.md.

#pragma once

namespace HONG::Resolver
{
	struct Resolution
	{
		std::optional<std::string> path;  // first existing candidate, if any
		std::string model;                // the armor's world model path as stored in the form
		std::vector<std::string> tried;   // candidate overlay paths checked, in order
	};

	// The worn head-slot armor (kHead, then kHair - the slots the original
	// mod's Papyrus monitor watches), or null when bare-headed.
	RE::TESObjectARMO* FindWornHeadgear(RE::Actor* a_actor);

	// Maps an armor to its overlay DDS path under Settings::overlayRoot, using
	// the same convention as Helmet Overlays / Enhanced Edition: the armor's
	// world model path (e.g. "Armor\Iron\Male\HelmetGND.nif") with .nif ->
	// .dds. Tries the player's sex first, then the other. Records every
	// candidate so a missing overlay can be diagnosed (and drawn by a modder).
	Resolution Resolve(const RE::TESObjectARMO* a_armor, bool a_female);

	// Convenience: Resolve(...).path.
	std::optional<std::string> ResolveOverlayPath(const RE::TESObjectARMO* a_armor, bool a_female);

	// Reads a whole VFS file (loose or archived) into memory.
	std::vector<std::uint8_t> ReadFile(const std::string& a_path);
}
