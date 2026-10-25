// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Modding-Exception
// Copyright (c) 2026 HelmetOverlaysNG contributors. See COPYING and EXCEPTIONS.md.

#include "Muffle.h"

#include "LowPass.h"
#include "Settings.h"

namespace HONG::Muffle
{
	namespace
	{
		// Editor IDs are stripped from shipped plugins, so categories also match by display name.
		struct Wanted
		{
			std::string_view editorId;
			std::string_view name;
		};
		constexpr std::array kWanted{
			Wanted{ "AudioCategorySFX"sv, "SFX"sv },
			Wanted{ "AudioCategoryFootsteps"sv, "Footsteps"sv },
		};

		struct Tracked
		{
			RE::BGSSoundCategory* category{ nullptr };
			float baseVolume{ 1.0f };  // captured when attenuation starts
		};

		std::vector<Tracked> g_categories;
		bool g_applied = false;
	}

	void Init()
	{
		const auto handler = RE::TESDataHandler::GetSingleton();
		for (const auto& wanted : kWanted) {
			RE::BGSSoundCategory* found = RE::TESForm::LookupByEditorID<RE::BGSSoundCategory>(wanted.editorId);
			if (!found && handler) {
				for (const auto category : handler->GetFormArray<RE::BGSSoundCategory>()) {
					if (category && _stricmp(category->GetName(), std::string(wanted.name).c_str()) == 0) {
						found = category;
						break;
					}
				}
			}
			if (found) {
				g_categories.push_back({ found, 1.0f });
			} else {
				logger::warn("Sound category {} not found; attenuation skips it.", wanted.editorId);
			}
		}
		logger::info("Attenuation categories resolved: {}", g_categories.size());
	}

	void Update(float a_amount)
	{
		const auto settings = Settings::GetSingleton();

		LowPass::SetTarget(settings->lowPass, a_amount, settings->lowPassHz);

		const float amount = settings->attenuate ? a_amount : 0.0f;

		if (amount <= 0.0f) {
			if (g_applied) {
				for (const auto& tracked : g_categories) {
					tracked.category->SetCategoryVolume(tracked.baseVolume);
				}
				g_applied = false;
			}
			return;
		}

		if (!g_applied) {
			for (auto& tracked : g_categories) {
				tracked.baseVolume = tracked.category->GetCategoryVolume();
			}
			g_applied = true;
		}

		const float eased = settings->attenuateSeconds > 0.0f ? std::pow(amount, 1.0f + settings->attenuateSeconds) : 1.0f;
		const float mult = 1.0f + (settings->attenuateVolume - 1.0f) * eased;
		for (const auto& tracked : g_categories) {
			tracked.category->SetCategoryVolume(tracked.baseVolume * mult);
		}
	}
}
