// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Modding-Exception
// Copyright (c) 2026 HelmetOverlaysNG contributors. See COPYING and EXCEPTIONS.md.

#pragma once

#include "ArtFit.h"
#include "OverlayController.h"

namespace HONG::OverlayMask
{
	// Hooks the start of the world main batch render so the depth write below runs first.
	// Call at kPostPostLoad, after SKSE::AllocTrampoline.
	void Install();

	// Sets the mask for the next world render. Main thread, after the overlay has been updated;
	// a_fit is how much of the display the art covers; a_vrCoverage is the VR plane coverage of each eye.
	void Publish(const OverlayController::MaskCandidate& a_candidate, ArtFit::Fraction a_fit, float a_vrCoverage);
}
