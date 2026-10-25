// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Modding-Exception
// Copyright (c) 2026 HelmetOverlaysNG contributors. See COPYING and EXCEPTIONS.md.
//
// Self-contained (no PCH dependency): the SKSE Menu Framework page builds in a
// PCH-free TU and includes this header directly.

#pragma once

#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace HONG
{
	enum class AnimStyle : std::uint8_t
	{
		kFade,    // alpha only
		kSettle,  // alpha + slight scale-down onto the face (helmet "seats")
		kSlide,   // alpha + drop in from above
	};

	class Settings
	{
	public:
		static Settings* GetSingleton();

		// Reads Data/SKSE/Plugins/HelmetOverlaysNG.ini; missing keys keep defaults.
		void Load();
		// Writes the current values back (used by the in-game menu).
		void Save() const;
		// In-memory only; Save() to persist.
		void ResetToDefaults();

		// [General]
		bool enabled{ true };
		std::string overlayRoot{ "Interface\\overlays" };
		bool firstPersonOnly{ true };
		bool hideInMenus{ true };
		// Publish the overlay's fully opaque region so a renderer plugin can skip shading behind it.
		bool opaqueMask{ false };
		int maskMarginPx{ 8 };
		// Stop ScribeOverlays.esp's quests so its SWF widget doesn't draw a second overlay.
		bool suppressOriginal{ true };
		// Wigs (hair slot alone) and circlets (circlet slot without the hair or head slot, which helmets and
		// masks also occupy) have no helmet art; skip them in the coverage scan and when picking the worn headgear.
		bool skipWigs{ true };
		bool skipCirclets{ true };
		// Shows the debug page in the in-game menu (live armor/path info + manual helmet preview).
		bool debugTools{ false };

		// [Appearance]
		float opacity{ 1.0f };
		float scale{ 1.0f };

		// [Light] darken the art in dark places
		bool reactiveLight{ false };
		float lightStrength{ 0.6f };         // darkening at zero light, 0 = none, 1 = black
		float lightFadeSeconds{ 1.5f };      // time to move to a new light level
		float lightIntervalSeconds{ 0.5f };  // how often the light level is sampled

		// [Animation]
		AnimStyle animStyle{ AnimStyle::kSettle };
		float fadeInSeconds{ 0.35f };
		float fadeOutSeconds{ 0.20f };
		bool sway{ true };
		float swayStrength{ 1.0f };

		// [VR]
		float vrDepth{ 0.6f };      // meters; head-locked visor plane
		float vrCoverage{ 1.15f };  // > 1 over-fills each eye's FOV

		// [Sound] - two independent effects; either, both or neither can be on.
		bool attenuate{ true };         // lower world-sound volume while helmeted
		float attenuateVolume{ 0.6f };  // multiplier reached at full helmet
		float attenuateSeconds{ 0.5f };
		bool autoStrength{ true };     // scale both effects by helmet type (armor class and overlay art)
		float autoArtWeight{ 0.65f };  // how much the overlay art outweighs the armor class
		bool lowPass{ false };         // real low-pass "distortion" (muffled highs), see LowPass.cpp
		float lowPassHz{ 1800.0f };

		// [Menus] - which open menus fade the overlay out (when hideInMenus is on).
		// Thread-safe: the in-game menu edits these while the HUD hook reads them.
		bool IsMenuHidden(std::string_view a_name) const;
		void SetMenuHidden(std::string_view a_name, bool a_hidden);
		std::vector<std::string> HiddenMenus() const;
		// True if the predicate holds for any hidden-menu name (no allocation).
		bool AnyHiddenMenu(const std::function<bool(const std::string&)>& a_pred) const;
		void ResetMenusToDefaults();

	private:
		mutable std::mutex _menuLock;
		std::vector<std::string> _hiddenMenus;
	};
}
