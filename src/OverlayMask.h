// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Modding-Exception
// Copyright (c) 2026 HelmetOverlaysNG contributors. See COPYING and EXCEPTIONS.md.

#pragma once

#include "OverlayController.h"

namespace HONG::OverlayMask
{
	// Hooks the start of the world main batch render so the depth write below runs first.
	// Call at kPostPostLoad, after SKSE::AllocTrampoline.
	void Install();

	// Sets the mask for the next world render. Main thread, after the overlay has been updated;
	// a_display is the display size in pixels; a_vrCoverage is the VR plane coverage of each eye.
	void Publish(const OverlayController::MaskCandidate& a_candidate, ImVec2 a_display, float a_vrCoverage);
}
