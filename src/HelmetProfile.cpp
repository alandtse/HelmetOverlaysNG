// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Modding-Exception
// Copyright (c) 2026 HelmetOverlaysNG contributors. See COPYING and EXCEPTIONS.md.

#include "HelmetProfile.h"

#include <algorithm>

namespace HONG::Profile
{
	namespace
	{
		constexpr float kHeavyFactor = 1.0f;
		constexpr float kLightFactor = 0.70f;
		constexpr float kClothingFactor = 0.40f;

		float ArmorFactor(ArmorClass a_class)
		{
			// Named cases, no default, so a new armour class fails to build instead of inheriting the light-armour
			// factor. kUnknown is a real answer: an unrecognised armour type is treated as light.
#pragma warning(push)
#pragma warning(error: 4062)
			switch (a_class) {
			case ArmorClass::kHeavy:
				return kHeavyFactor;
			case ArmorClass::kClothing:
				return kClothingFactor;
			case ArmorClass::kLight:
				return kLightFactor;
			case ArmorClass::kUnknown:
				return kLightFactor;
			}
#pragma warning(pop)
			return kLightFactor;
		}
	}

	Result Compute(float a_openness, ArmorClass a_armorClass, float a_artWeight)
	{
		Result result;
		result.armorFactor = ArmorFactor(a_armorClass);
		if (a_openness < 0.0f) {
			result.strength = std::clamp(result.armorFactor, kMinStrength, 1.0f);
			return result;
		}

		result.artFactor = std::clamp((kOpenOpenness - a_openness) / (kOpenOpenness - kClosedOpenness), 0.0f, 1.0f);
		const float weight = std::clamp(a_artWeight, 0.0f, 1.0f);
		result.strength = std::clamp(weight * result.artFactor + (1.0f - weight) * result.armorFactor, kMinStrength, 1.0f);
		return result;
	}
}
