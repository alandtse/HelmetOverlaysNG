// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Modding-Exception
// Copyright (c) 2026 HelmetOverlaysNG contributors. See COPYING and EXCEPTIONS.md.

#pragma once

#include "OverlayResolver.h"
#include "Motion.h"
#include "TintColor.h"
#include "TextureCache.h"

namespace HONG
{
	// Overlay canvas size the art is authored at; the flat draw and the opaque mask both map from it.
	inline constexpr float kArtWidth = 1920.0f;
	inline constexpr float kArtHeight = 1080.0f;

	// One overlay quad to draw this frame, in unit screen space: the renderer
	// fits it to the display (cover) and applies scale/offset.
	struct DrawLayer
	{
		ID3D11ShaderResourceView* srv{ nullptr };
		float alpha{ 0.0f };    // 0..1, opacity setting already applied
		float tint{ 1.0f };     // 0..1 brightness multiplier (reactive light)
		Tint::Rgb color;        // per-channel multiplier toward the scene colour, peak 1
		float scale{ 1.0f };    // animation scale about center
		ImVec2 offset{ 0, 0 };  // fraction of display size (slide + sway)
	};

	// Tracks the worn helmet and drives fade/settle/slide animation. Game-thread
	// only (Update runs from the HUD draw hook). Equip events just set a flag.
	class OverlayController :
	    public RE::BSTEventSink<RE::TESEquipEvent>
	{
	public:
		static OverlayController* GetSingleton();

		void Register();

		// Advances animation by a_dt and fills a_out with the layers to draw
		// (up to one fading-in plus fading-out leftovers).
		void Update(float a_dt, std::vector<DrawLayer>& a_out);

		// 0..1: how "helmeted" the player currently is (max layer progress,
		// 0 while the overlay is gated off). Drives sound muffling.
		float WornAmount() const { return _wornAmount; }

		// 0..1 scale on the sound effects for the worn helmet's type (1 when disabled).
		float MuffleStrength() const { return _muffleStrength; }

		// The opaque mask for the frame being rendered next: allowed only while a single, fully faded
		// in overlay at full opacity is shown and has not moved much since the last frame.
		struct MaskCandidate
		{
			TextureCache::Mask mask;
			ImVec2 offset{ 0, 0 };
			float scale{ 1.0f };
			bool allowed{ false };
		};
		const MaskCandidate& GetMaskCandidate() const { return _mask; }

		// True while the overlay is shown and the screen colour tint (or its debug override) is on.
		bool WantsScreenColor() const;

		void MarkDirty() { _dirty.store(true, std::memory_order_relaxed); }

		// Debug: draw this armor's overlay regardless of what is worn (0 = off, use the
		// worn helmet). Safe from any thread; applied on the next frame.
		void SetPreview(RE::FormID a_armor);
		RE::FormID GetPreview() const { return _previewId.load(std::memory_order_relaxed); }

		// Debug: what the controller last resolved, for the in-game menu. Copied under a lock.
		struct Diagnostic
		{
			RE::FormID armorId{ 0 };
			std::string armorName;
			bool previewing{ false };
			Resolver::Resolution resolution;
			float openness{ -1.0f };  // overlay see-through fraction, -1 unknown
			float maskShare{ 0.0f };  // fraction of the overlay in the opaque mask
			float strength{ 1.0f };   // how much this helmet muffles sound
			float light{ -1.0f };     // last sampled light level 0..1, -1 when reactive light is off
			bool sceneSampled{ false };
			Tint::Rgb scene;
			Tint::Rgb tint;
			float lightTint{ 1.0f };
		};
		Diagnostic GetDiagnostic() const;

		RE::BSEventNotifyControl ProcessEvent(const RE::TESEquipEvent* a_event, RE::BSTEventSource<RE::TESEquipEvent>*) override;

	private:
		struct Layer
		{
			TextureCache::Srv srv;
			const RE::TESObjectARMO* armor{ nullptr };
			float progress{ 0.0f };
			bool wanted{ true };
			TextureCache::Mask mask;
		};

		bool Gated() const;
		void Refresh();
		void UpdateSway(float a_dt);
		void UpdateLight(float a_dt);
		void UpdateTint(float a_dt);
		void UpdateMotion(float a_dt);

		std::vector<Layer> _layers;
		const RE::TESObjectARMO* _currentArmor{ nullptr };
		std::atomic<bool> _dirty{ true };
		std::atomic<RE::FormID> _previewId{ 0 };
		mutable std::mutex _diagLock;
		Diagnostic _diag;
		float _wornAmount{ 0.0f };
		float _lightDarkness{ 0.0f };       // 0 lit .. 1 dark, eased toward _lightTarget
		float _lightTarget{ 0.0f };
		float _lightClock{ 0.0f };
		float _lightLogged{ -1.0f };  // last level written to the log
		Tint::Rgb _tint;
		MaskCandidate _mask;
		ImVec2 _lastMaskOffset{ 0, 0 };
		ImVec2 _lastMaskVelocity{ 0, 0 };
		float _muffleStrength{ 1.0f };
		float _timeSinceRefresh{ 0.0f };

		ImVec2 _sway{ 0, 0 };
		Motion::Player _motion;
		float _lastYaw{ 0.0f };
		float _lastPitch{ 0.0f };
		bool _haveAngles{ false };
	};
}
