// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Modding-Exception
// Copyright (c) 2026 HelmetOverlaysNG contributors. See COPYING and EXCEPTIONS.md.

#pragma once

#include "TintColor.h"

namespace HONG::ScreenColor
{
	// Once per frame from the HUD draw, where the HDR scene colour is already complete.
	// While a_wanted, the scene colour is reduced on the GPU every few tenths of a second and the
	// average of its outer ring (per eye in VR) is read back a few frames later, so the CPU never waits on the GPU.
	void Update(float a_dt, bool a_wanted);

	// Latest ring average of the scene colour in linear HDR units; false until the first readback.
	bool Latest(Tint::Rgb& a_out);
}
