// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Modding-Exception
// Copyright (c) 2026 HelmetOverlaysNG contributors. See COPYING and EXCEPTIONS.md.

#include "OverlayMask.h"

#include "HelmetOverlaysNG_API.h"

namespace
{
	HONG_OverlayMask g_mask{ HONG_OVERLAY_MASK_VERSION, sizeof(HONG_OverlayMask) };

	bool Differs(const HONG_OverlayMask& a_lhs, const HONG_OverlayMask& a_rhs)
	{
		return a_lhs.active != a_rhs.active || a_lhs.maskSrv != a_rhs.maskSrv || a_lhs.center[0] != a_rhs.center[0] || a_lhs.center[1] != a_rhs.center[1] ||
		       a_lhs.size[0] != a_rhs.size[0] || a_lhs.size[1] != a_rhs.size[1] || a_lhs.isVR != a_rhs.isVR;
	}
}

extern "C" const HONG_OverlayMask* HONG_GetOverlayMask(void)
{
	return &g_mask;
}

namespace HONG::OverlayMaskExport
{
	void Publish(const OverlayController::MaskCandidate& a_candidate, ImVec2 a_display, bool a_vr)
	{
		HONG_OverlayMask next = g_mask;
		next.isVR = a_vr ? 1 : 0;
		next.active = a_candidate.allowed && !a_vr && a_display.x > 0.0f && a_display.y > 0.0f ? 1 : 0;
		if (next.active) {
			const float cover = std::max(a_display.x / kArtWidth, a_display.y / kArtHeight);
			next.maskSrv = a_candidate.mask.srv.Get();
			next.maskWidth = a_candidate.mask.width;
			next.maskHeight = a_candidate.mask.height;
			next.center[0] = 0.5f + a_candidate.offset.x;
			next.center[1] = 0.5f + a_candidate.offset.y;
			next.size[0] = kArtWidth * cover * a_candidate.scale / a_display.x;
			next.size[1] = kArtHeight * cover * a_candidate.scale / a_display.y;
		}
		if (Differs(g_mask, next)) {
			next.frame = g_mask.frame + 1;
			g_mask = next;
		}
	}
}
