// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Modding-Exception
// Copyright (c) 2026 HelmetOverlaysNG contributors. See COPYING and EXCEPTIONS.md.

#pragma once

namespace HONG::OriginalMod
{
	// True when ScribeOverlays.esp (Helmet Overlays 2.0, the SWF widget) is in
	// the load order. Valid from kDataLoaded.
	bool IsInstalled();

	// Stops the original mod's quests so its SWF widget never draws a second
	// overlay. The original also ships the art, so its ESP stays in the load
	// order; only its scripts are silenced. No-op when not installed. Call on
	// each game load/new game (a start-enabled quest restarts per session).
	void Suppress();
}
