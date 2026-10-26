// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Modding-Exception
// Copyright (c) 2026 HelmetOverlaysNG contributors. See COPYING and EXCEPTIONS.md.

#pragma once

namespace HONG::LowPass
{
	// Hooks BSXAudio2GameSound::UpdateEmitterPosition (vtable slot 0x0D) and
	// ::Update (slot 0x0E). After the engine runs, each non-music voice gets a
	// low-pass on its send to the mastering voice through the same XAudio2 call
	// the engine uses for its own distance filter (SetOutputFilterParameters). The filter is never read back.
	// Call at kPostPostLoad.
	void Install();

	// Main thread, once per frame. amount is 0..1 (OverlayController::WornAmount),
	// cutoffHz is the cutoff reached at full amount. The audio thread reads these
	// through atomics only.
	void SetTarget(bool a_enabled, float a_amount, float a_cutoffHz);

	// Sample rate the engine mixes at, used to turn a cutoff into the filter coefficient.
	float OutputSampleRate();
}
