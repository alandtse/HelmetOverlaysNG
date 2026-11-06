// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Modding-Exception
// Copyright (c) 2026 HelmetOverlaysNG contributors. See COPYING and EXCEPTIONS.md.
//
// Data and controls behind the in-game pages. Self-contained (no PCH, no
// CommonLib types) so the SKSE Menu Framework page, which builds in its own
// TU, can use it. Implemented in Debug.cpp.

#pragma once

#include "BrowsePage.h"
#include "DebugOverride.h"
#include "TintColor.h"

#include <cstddef>
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
		std::string model;       // world model path; empty until scanned
		std::int8_t art{ -1 };   // -1 not scanned or no world model, 0 no overlay art, 1 has art
		std::string overlay;     // VFS path of the art; empty without art
		float color{ -1.0f };    // ArtMetrics::ColoredFraction of the art, -1 until classified
		std::string appearance;  // sorted attachments and both world models; fallback grouping key
	};

	// An overlay file on disk that no helmet in the load order resolves to, such as art for an armor mod
	// that is not installed.
	struct OrphanArt
	{
		std::string path;      // VFS path
		std::string term;      // armor name to search for
		float color{ -1.0f };  // ArtMetrics::ColoredFraction, -1 until classified
	};

	enum class ArtFilter
	{
		kAll,
		kMissing,   // no overlay art
		kColored,   // art with real color
		kBlack,     // art whose RGB is black, shaped by alpha alone
		kUnchecked  // art not classified yet
	};

	struct PluginCoverage
	{
		std::string plugin;
		std::uint32_t total{ 0 };
		std::uint32_t covered{ 0 };
	};

	// Playable head or hair armors with a world model, and how many have overlay art.
	struct Coverage
	{
		bool done{ false };
		float progress{ 0.0f };  // 0..1 while scanning
		std::uint32_t eligible{ 0 };
		std::uint32_t covered{ 0 };
		std::uint32_t missing{ 0 };
		std::uint32_t noModel{ 0 };            // head/hair armors skipped: no world model
		std::vector<PluginCoverage> byPlugin;  // most missing first
		std::uint32_t orphans{ 0 };            // overlay files with no matching helmet
		bool classifying{ false };
		bool checked{ false };          // a color check has run to the end
		std::uint32_t classified{ 0 };  // art files checked for color so far
		std::uint32_t colored{ 0 };
		std::uint32_t blackMasks{ 0 };
	};

	// Starts on its own once the player is loaded; call every frame from the main thread.
	void Tick();
	// Discards results and scans again (for example after installing art).
	void RescanCoverage();
	Coverage GetCoverage();
	// Reads each helmet's art a few files per frame and records whether it has color or is a black mask.
	void ClassifyArt();

	// Matching records, optionally grouped by overlay (or appearance when no art exists), then paginated.
	BrowsePage<HelmetEntry> FilterHelmets(const std::string& a_filter, ArtFilter a_art, std::size_t a_page, std::size_t a_pageSize, bool a_unique);

	struct Current
	{
		std::uint32_t armorId{ 0 };  // 0 = nothing resolved yet / bare head
		std::string armorName;
		bool previewing{ false };
		std::string model;
		std::string overlayPath;
		bool found{ false };
		std::vector<std::string> tried;
		float openness{ -1.0f };  // overlay see-through fraction, -1 unknown
		float color{ -1.0f };     // ArtMetrics::ColoredFraction of the overlay, -1 unknown
		float strength{ 1.0f };   // sound muffling scale for this helmet
		float maskShare{ 0.0f };  // fraction of the overlay in the opaque mask (0 when not built)
		float light{ -1.0f };     // sampled light level 0..1, -1 when reactive light is off
		bool sceneSampled{ false };
		Tint::Rgb scene;          // ring average of the scene color, linear HDR
		Tint::Rgb tint;           // color multiplier applied to the art after easing
		float lightTint{ 1.0f };  // brightness multiplier from the light level alone
	};
	Current GetCurrent();

	// Matching unused overlay files, paginated. Empty until the coverage scan finishes.
	BrowsePage<OrphanArt> FilterOrphans(const std::string& a_filter, std::size_t a_page, std::size_t a_pageSize);

	// Draw this armor's overlay without equipping it; 0 / ClearPreview() goes
	// back to the worn helmet.
	void Preview(std::uint32_t a_armorId);
	void ClearPreview();
	std::uint32_t Previewing();
	// Draws an overlay file by VFS path with no armor; ClearPreview() ends it too.
	void PreviewFile(const std::string& a_path);
	std::string PreviewingFile();

	// Names of every menu the game has registered, sorted.
	std::vector<std::string> RegisteredMenus();
}
