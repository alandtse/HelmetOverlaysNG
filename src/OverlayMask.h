// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Modding-Exception
// Copyright (c) 2026 HelmetOverlaysNG contributors. See COPYING and EXCEPTIONS.md.

#pragma once

#include "OverlayController.h"

namespace HONG::OverlayMaskExport
{
	// Publishes the mask for the next frame's world render through HONG_GetOverlayMask. Main thread,
	// after the overlay has been updated; a_display is the display size in pixels.
	void Publish(const OverlayController::MaskCandidate& a_candidate, ImVec2 a_display, bool a_vr);
}
