// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Modding-Exception
// Copyright (c) 2026 HelmetOverlaysNG contributors. See COPYING and EXCEPTIONS.md.

#pragma once

namespace HONG::Muffle
{
	// Resolve the sound categories to attenuate. Call at kDataLoaded.
	void Init();

	// a_amount is OverlayController::WornAmount() (0 = bare head, 1 = fully
	// helmeted). Volume attenuation (bAttenuate): ramps the configured category
	// volumes toward fAttenuateVolume and restores them exactly when it
	// returns to 0. Call once per frame.
	//
	// The low-pass "distortion" (bLowPass) is a separate option: this also
	// publishes its target to LowPass, which applies it on the audio thread.
	void Update(float a_amount);

	// What a helmet with a_strength (OverlayController::MuffleStrength) does to the sound while worn.
	struct Effect
	{
		float volume{ 1.0f };    // multiplier on the attenuated categories; 1 when attenuation is off
		float cutoffHz{ 0.0f };  // low-pass cutoff; 0 when the low-pass is off
	};
	Effect EffectAt(float a_strength);
}
