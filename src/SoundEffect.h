// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Modding-Exception
// Copyright (c) 2026 HelmetOverlaysNG contributors. See COPYING and EXCEPTIONS.md.

#pragma once

namespace HONG::SoundEffect
{
	// Fully open cutoff of the engine's filter: its radian frequency tops out at SampleRate / 6.
	inline constexpr float kOpenCutoffDivisor = 6.0f;
	inline constexpr float kMinCutoffHz = 200.0f;

	// Multiplier for the attenuated sound categories at a_amount (0..1 of the helmet worn).
	// a_shape raises the amount to a power so the sound can lag the visual fade; 0 applies the full
	// a_volumeAtFull as soon as the amount is above zero.
	float Volume(float a_amount, float a_volumeAtFull, float a_shape);

	// Low-pass cutoff at a_amount, log-interpolated from fully open to a_targetHz.
	float CutoffHz(float a_amount, float a_targetHz, float a_sampleRate);
}
