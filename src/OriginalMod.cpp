// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Modding-Exception
// Copyright (c) 2026 HelmetOverlaysNG contributors. See COPYING and EXCEPTIONS.md.

#include "OriginalMod.h"

namespace HONG::OriginalMod
{
	namespace
	{
		constexpr auto kPluginName = "ScribeOverlays.esp"sv;
	}

	bool IsInstalled()
	{
		const auto handler = RE::TESDataHandler::GetSingleton();
		return handler && handler->LookupLoadedModByName(kPluginName);
	}

	void Suppress()
	{
		const auto handler = RE::TESDataHandler::GetSingleton();
		if (!handler || !handler->LookupLoadedModByName(kPluginName)) {
			return;
		}

		std::size_t stopped = 0;
		for (const auto quest : handler->GetFormArray<RE::TESQuest>()) {
			if (!quest) {
				continue;
			}
			const auto file = quest->GetFile(0);
			if (file && kPluginName == file->GetFilename() && quest->IsRunning()) {
				quest->Stop();
				++stopped;
			}
		}
		logger::info("Suppressed {} ScribeOverlays.esp quest(s).", stopped);
	}
}
