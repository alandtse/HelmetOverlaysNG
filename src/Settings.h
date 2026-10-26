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

	// Accepted range of each tunable; the INI loader clamps to it and the settings sliders span it.
	struct Bounds
	{
		float min;
		float max;
	};
	namespace Range
	{
		inline constexpr Bounds kUnit{ 0.0f, 1.0f };
		inline constexpr Bounds kOpacity{ 0.0f, 1.0f };
		inline constexpr Bounds kScale{ 0.5f, 3.0f };
		inline constexpr Bounds kVerticalOffset{ -0.3f, 0.3f };
		inline constexpr Bounds kLightStrength{ 0.0f, 1.0f };
		inline constexpr Bounds kLightFade{ 0.0f, 10.0f };
		inline constexpr Bounds kLightInterval{ 0.1f, 5.0f };
		inline constexpr Bounds kFade{ 0.0f, 2.0f };
		inline constexpr Bounds kSwayStrength{ 0.0f, 4.0f };
		inline constexpr Bounds kMotionStrength{ 0.0f, 2.0f };
		inline constexpr Bounds kVrDepth{ 0.3f, 3.0f };
		inline constexpr Bounds kVrCoverage{ 0.5f, 1.6f };
		inline constexpr Bounds kMaskMargin{ 4.0f, 32.0f };
		inline constexpr Bounds kAutoArtWeight{ 0.0f, 1.0f };
		inline constexpr Bounds kAttenuateVolume{ 0.0f, 1.0f };
		inline constexpr Bounds kAttenuateLag{ 0.0f, 3.0f };
		inline constexpr Bounds kLowPassHz{ 400.0f, 8000.0f };
	}

	// Every tunable with its default; ResetToDefaults restores them by assigning a fresh instance.
	struct SettingValues
	{
		// [General]
		bool enabled{ true };
		std::string overlayRoot{ "Interface\\overlays" };
		bool firstPersonOnly{ true };
		bool hideInMenus{ true };
		// Publish the overlay's fully opaque region so a renderer plugin can skip shading behind it.
		bool opaqueMask{ true };
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
		float verticalOffset{ 0.0f };  // display height fraction, positive moves the art down
		bool extendEdges{ true };      // stretch the art's edge pixels over any gap the scale or offset opens

		// [Light] darken the art in dark places
		bool reactiveLight{ true };
		float lightStrength{ 0.25f };        // darkening at zero light, 0 = none, 1 = black
		float lightFadeSeconds{ 1.0f };      // time to move to a new light level
		float lightIntervalSeconds{ 1.0f };  // how often the light level is sampled
		bool screenTint{ true };             // flat only: shift the art toward the colour of the scene around the edges
		float screenTintStrength{ 0.5f };    // 0 = no shift, 1 = the full hue

		// [Animation]
		AnimStyle animStyle{ AnimStyle::kSettle };
		float fadeInSeconds{ 0.35f };
		float fadeOutSeconds{ 0.20f };
		bool sway{ false };
		bool motion{ true };          // jump, strafe and sprint nudges (flat only)
		float motionStrength{ 1.0f };
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
	};

	class Settings : public SettingValues
	{
	public:
		static Settings* GetSingleton();

		// Reads Data/SKSE/Plugins/HelmetOverlaysNG.ini; missing keys keep defaults.
		void Load();
		// Writes the current values back (used by the in-game menu).
		void Save() const;
		// In-memory only; Save() to persist.
		void ResetToDefaults();

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
