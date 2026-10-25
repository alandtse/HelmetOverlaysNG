// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Modding-Exception
// Copyright (c) 2026 HelmetOverlaysNG contributors. See COPYING and EXCEPTIONS.md.

#include "Debug.h"

#include "OverlayController.h"

namespace HONG::Debug
{
	const std::vector<HelmetEntry>& Helmets()
	{
		static std::vector<HelmetEntry> entries;
		static bool built = false;
		if (built) {
			return entries;
		}

		const auto handler = RE::TESDataHandler::GetSingleton();
		if (!handler) {
			return entries;
		}
		built = true;

		using Slot = RE::BGSBipedObjectForm::BipedObjectSlot;
		for (const auto armor : handler->GetFormArray<RE::TESObjectARMO>()) {
			if (!armor || (!armor->HasPartOf(Slot::kHead) && !armor->HasPartOf(Slot::kHair))) {
				continue;
			}
			const char* name = armor->GetName();
			if (!name || !*name) {
				continue;
			}
			const auto file = armor->GetFile(0);
			entries.push_back({ armor->GetFormID(), name, file ? std::string(file->GetFilename()) : std::string() });
		}
		std::ranges::sort(entries, [](const HelmetEntry& a_lhs, const HelmetEntry& a_rhs) {
			return _stricmp(a_lhs.name.c_str(), a_rhs.name.c_str()) < 0;
		});
		logger::info("Debug: {} head/hair armors indexed.", entries.size());
		return entries;
	}

	Current GetCurrent()
	{
		const auto diag = OverlayController::GetSingleton()->GetDiagnostic();
		Current out;
		out.armorId = diag.armorId;
		out.armorName = diag.armorName;
		out.previewing = diag.previewing;
		out.model = diag.resolution.model;
		out.found = diag.resolution.path.has_value();
		out.overlayPath = diag.resolution.path.value_or(std::string());
		out.tried = diag.resolution.tried;
		return out;
	}

	void Preview(std::uint32_t a_armorId)
	{
		OverlayController::GetSingleton()->SetPreview(a_armorId);
	}

	void ClearPreview()
	{
		OverlayController::GetSingleton()->SetPreview(0);
	}

	std::uint32_t Previewing()
	{
		return OverlayController::GetSingleton()->GetPreview();
	}

	std::vector<std::string> RegisteredMenus()
	{
		std::vector<std::string> out;
		if (const auto ui = RE::UI::GetSingleton()) {
			for (const auto& [name, entry] : ui->menuMap) {
				out.emplace_back(name.c_str());
			}
		}
		std::ranges::sort(out, [](const std::string& a_lhs, const std::string& a_rhs) {
			return _stricmp(a_lhs.c_str(), a_rhs.c_str()) < 0;
		});
		return out;
	}
}
