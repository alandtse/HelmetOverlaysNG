// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Modding-Exception
// Copyright (c) 2026 HelmetOverlaysNG contributors. See COPYING and EXCEPTIONS.md.

#pragma once

namespace HONG::LightLevel
{
	// The light level the engine reports at the player (the GetLightLevel condition function, the
	// value Papyrus Actor.GetLightLevel returns), scaled to 0 (dark) .. 1 (full daylight). Game
	// thread. Returns a negative value when the player is not available.
	float SamplePlayer();
}
