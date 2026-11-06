// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Modding-Exception
// Copyright (c) 2026 HelmetOverlaysNG contributors. See COPYING and EXCEPTIONS.md.

#pragma once

#include <algorithm>
#include <cmath>

namespace HONG::ArtFit
{
	// Size of the art as a fraction of the display, before the layer's scale.
	struct Fraction
	{
		float x{ 1.0f };
		float y{ 1.0f };
	};

	// Keeps the art's proportions and fills the display, cropping what overflows.
	inline Fraction Cover(float a_displayAspect, float a_artAspect)
	{
		return a_displayAspect >= a_artAspect ? Fraction{ 1.0f, a_displayAspect / a_artAspect } : Fraction{ a_artAspect / a_displayAspect, 1.0f };
	}

	// Keeps the art's proportions and shows all of it, then blends toward filling the display as a_stretch
	// goes from 0 to 1. A narrow VR view would otherwise squeeze a 16:9 frame to a fraction of its width.
	inline Fraction Fit(float a_displayAspect, float a_artAspect, float a_stretch)
	{
		const float stretch = std::clamp(a_stretch, 0.0f, 1.0f);
		const float containX = std::min(1.0f, a_artAspect / a_displayAspect);
		const float containY = std::min(1.0f, a_displayAspect / a_artAspect);
		return { containX + (1.0f - containX) * stretch, containY + (1.0f - containY) * stretch };
	}

	// VR: the helper sizes its plane from the eye's field of view times the coverage, so the art shrinks by the
	// same coverage to keep the painted eye openings at their flat-screen size.
	inline Fraction ArtFitFor(float a_coverage)
	{
		// Library-boundary guard, unreachable from the plugin: an infinite coverage would give 1.0f / inf == 0, a zero
		// fit, i.e. invisible art reported as drawn, and no downstream check rejects it.
		if (!std::isfinite(a_coverage) || a_coverage <= 0.0f) {
			return {};
		}
		const float inverse = 1.0f / a_coverage;
		return { inverse, inverse };
	}
}
