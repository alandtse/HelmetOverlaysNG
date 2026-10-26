// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Modding-Exception
// Copyright (c) 2026 HelmetOverlaysNG contributors. See COPYING and EXCEPTIONS.md.

#include "SoundEffect.h"

#include <algorithm>
#include <cmath>

namespace HONG::SoundEffect
{
	float Volume(float a_amount, float a_volumeAtFull, float a_shape)
	{
		const float amount = std::clamp(a_amount, 0.0f, 1.0f);
		const float eased = a_shape > 0.0f ? std::pow(amount, 1.0f + a_shape) : (amount > 0.0f ? 1.0f : 0.0f);
		return 1.0f + (a_volumeAtFull - 1.0f) * eased;
	}

	float CutoffHz(float a_amount, float a_targetHz, float a_sampleRate)
	{
		const float openHz = a_sampleRate / kOpenCutoffDivisor;
		const float targetHz = std::clamp(a_targetHz, kMinCutoffHz, openHz);
		return openHz * std::pow(targetHz / openHz, std::clamp(a_amount, 0.0f, 1.0f));
	}
}
