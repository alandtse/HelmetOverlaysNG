// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Modding-Exception
// Copyright (c) 2026 HelmetOverlaysNG contributors. See COPYING and EXCEPTIONS.md.

#include "LightLevel.h"

namespace HONG::LightLevel
{
	namespace
	{
		constexpr float kBrightLevel = 100.0f;  // GetLightLevel reading treated as full light
		constexpr int kBisectionSteps = 8;      // resolves kBrightLevel to about 0.4

		bool LevelAbove(RE::PlayerCharacter* a_player, float a_threshold)
		{
			RE::TESConditionItem item;
			item.data.functionData.function = RE::FUNCTION_DATA::FunctionID::kGetLightLevel;
			item.data.object = RE::CONDITIONITEMOBJECT::kSelf;
			item.data.flags.opCode = RE::CONDITION_ITEM_DATA::OpCode::kGreaterThan;
			item.data.comparisonValue.f = a_threshold;
			RE::ConditionCheckParams params{ a_player, nullptr };
			return item.IsTrue(params);
		}
	}

	float SamplePlayer()
	{
		const auto player = RE::PlayerCharacter::GetSingleton();
		if (!player) {
			return -1.0f;
		}
		float low = 0.0f;
		float high = kBrightLevel;
		for (int step = 0; step < kBisectionSteps; ++step) {
			const float mid = 0.5f * (low + high);
			(LevelAbove(player, mid) ? low : high) = mid;
		}
		return std::clamp(0.5f * (low + high) / kBrightLevel, 0.0f, 1.0f);
	}
}
