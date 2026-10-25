// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Modding-Exception
// Copyright (c) 2026 HelmetOverlaysNG contributors. See COPYING and EXCEPTIONS.md.

#pragma once

namespace HONG::Renderer
{
	// Install the D3D-init thunk and the HUDMenu::PostDisplay draw hook.
	// Call at kPostPostLoad; requires SKSE::AllocTrampoline beforehand.
	void Install();

	// Connect to ImGuiVRHelper as a head-locked HUD-plane client (VR only;
	// no-op on flat). Call at kPostPostLoad.
	void Connect();
}
