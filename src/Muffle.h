// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Modding-Exception
// Copyright (c) 2026 HelmetOverlaysNG contributors. See COPYING and EXCEPTIONS.md.

#pragma once

namespace HONG::Muffle
{
	// Resolve the sound categories to attenuate. Call at kDataLoaded.
	void Init();

	// a_amount is OverlayController::WornAmount() (0 = bare head, 1 = fully helmeted). Ramps the configured
	// category volumes toward fAttenuateVolume (bAttenuate) and restores them exactly at 0; also publishes the
	// bLowPass target to LowPass. Call once per frame.
	void Update(float a_amount);

	// What a helmet with a_strength (OverlayController::MuffleStrength) does to the sound while worn.
	struct Effect
	{
		float volume{ 1.0f };    // multiplier on the attenuated categories; 1 when attenuation is off
		float cutoffHz{ 0.0f };  // low-pass cutoff; 0 when the low-pass is off
	};
	Effect EffectAt(float a_strength);
}
