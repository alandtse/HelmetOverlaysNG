// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Modding-Exception
// Copyright (c) 2026 HelmetOverlaysNG contributors. See COPYING and EXCEPTIONS.md.

#include "Debug.h"

#include "OverlayController.h"
#include "OverlayResolver.h"
#include "TextMatch.h"

#include <mutex>

namespace HONG::Debug
{
	namespace
	{
		constexpr std::size_t kScanBatch = 48;
		constexpr std::size_t kLogWorstPlugins = 5;

		enum class ScanState
		{
			kIdle,
			kRunning,
			kDone
		};

		std::mutex g_lock;
		std::vector<HelmetEntry> g_entries;
		bool g_built = false;
		ScanState g_state = ScanState::kIdle;
		std::size_t g_next = 0;
		Coverage g_coverage;

		bool BuildEntries()
		{
			const auto handler = RE::TESDataHandler::GetSingleton();
			if (!handler) {
				return false;
			}

			using Slot = RE::BGSBipedObjectForm::BipedObjectSlot;
			g_entries.clear();
			for (const auto armor : handler->GetFormArray<RE::TESObjectARMO>()) {
				if (!armor || (!armor->HasPartOf(Slot::kHead) && !armor->HasPartOf(Slot::kHair))) {
					continue;
				}
				if (armor->GetFormFlags() & RE::TESObjectARMO::RecordFlags::kNonPlayable) {
					continue;
				}
				const char* name = armor->GetName();
				if (!name || !*name) {
					continue;
				}
				const auto file = armor->GetFile(0);
				g_entries.push_back({ armor->GetFormID(), name, file ? std::string(file->GetFilename()) : std::string() });
			}
			std::ranges::sort(g_entries, [](const HelmetEntry& a_lhs, const HelmetEntry& a_rhs) {
				return _stricmp(a_lhs.name.c_str(), a_rhs.name.c_str()) < 0;
			});
			g_built = true;
			return true;
		}

		void FinishScan()
		{
			Coverage coverage;
			std::unordered_map<std::string, PluginCoverage> perPlugin;
			for (const auto& entry : g_entries) {
				if (entry.art < 0) {
					++coverage.noModel;
					continue;
				}
				++coverage.eligible;
				auto& plugin = perPlugin[entry.plugin];
				plugin.plugin = entry.plugin;
				++plugin.total;
				if (entry.art > 0) {
					++coverage.covered;
					++plugin.covered;
				}
			}
			coverage.missing = coverage.eligible - coverage.covered;
			for (auto& [name, plugin] : perPlugin) {
				coverage.byPlugin.push_back(std::move(plugin));
			}
			std::ranges::sort(coverage.byPlugin, [](const PluginCoverage& a_lhs, const PluginCoverage& a_rhs) {
				const auto lhsMissing = a_lhs.total - a_lhs.covered;
				const auto rhsMissing = a_rhs.total - a_rhs.covered;
				return lhsMissing != rhsMissing ? lhsMissing > rhsMissing : a_lhs.plugin < a_rhs.plugin;
			});
			coverage.done = true;
			coverage.progress = 1.0f;
			g_coverage = std::move(coverage);
			g_state = ScanState::kDone;

			const float percent = g_coverage.eligible ? 100.0f * static_cast<float>(g_coverage.covered) / static_cast<float>(g_coverage.eligible) : 0.0f;
			logger::info("Overlay coverage: {} of {} playable head/hair armors have art ({:.0f}%); {} without art; {} skipped (no world model).",
				g_coverage.covered, g_coverage.eligible, percent, g_coverage.missing, g_coverage.noModel);
			std::size_t logged = 0;
			for (const auto& plugin : g_coverage.byPlugin) {
				if (plugin.total == plugin.covered || logged++ >= kLogWorstPlugins) {
					continue;
				}
				logger::info("  {}: {} of {} covered", plugin.plugin, plugin.covered, plugin.total);
			}
		}
	}

	void Tick()
	{
		std::scoped_lock lock{ g_lock };

		if (g_state == ScanState::kDone) {
			return;
		}
		const auto player = RE::PlayerCharacter::GetSingleton();
		if (g_state == ScanState::kIdle) {
			if (!player || !player->Is3DLoaded() || (!g_built && !BuildEntries())) {
				return;
			}
			g_next = 0;
			g_state = ScanState::kRunning;
		}

		const auto base = player ? player->GetActorBase() : nullptr;
		const bool female = base && base->IsFemale();
		const auto end = std::min(g_next + kScanBatch, g_entries.size());
		for (; g_next < end; ++g_next) {
			auto& entry = g_entries[g_next];
			const auto armor = RE::TESForm::LookupByID<RE::TESObjectARMO>(entry.id);
			const auto resolution = Resolver::Resolve(armor, female);
			entry.model = resolution.model;
			entry.art = resolution.model.empty() ? std::int8_t{ -1 } : (resolution.path ? std::int8_t{ 1 } : std::int8_t{ 0 });
		}
		g_coverage.progress = g_entries.empty() ? 1.0f : static_cast<float>(g_next) / static_cast<float>(g_entries.size());
		if (g_next >= g_entries.size()) {
			FinishScan();
		}
	}

	void RescanCoverage()
	{
		std::scoped_lock lock{ g_lock };
		for (auto& entry : g_entries) {
			entry.art = -1;
			entry.model.clear();
		}
		g_coverage = Coverage{};
		g_next = 0;
		g_state = ScanState::kIdle;
	}

	Coverage GetCoverage()
	{
		std::scoped_lock lock{ g_lock };
		return g_coverage;
	}

	std::vector<HelmetEntry> FilterHelmets(const std::string& a_filter, bool a_missingOnly, std::size_t a_limit, std::size_t& a_totalMatches)
	{
		std::scoped_lock lock{ g_lock };
		if (!g_built) {
			BuildEntries();
		}
		std::vector<HelmetEntry> out;
		a_totalMatches = 0;
		for (const auto& entry : g_entries) {
			if (a_missingOnly && entry.art != 0) {
				continue;
			}
			if (!ContainsNoCase(entry.name, a_filter) && !ContainsNoCase(entry.plugin, a_filter)) {
				continue;
			}
			if (out.size() < a_limit) {
				out.push_back(entry);
			}
			++a_totalMatches;
		}
		return out;
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
		out.openness = diag.openness;
		out.strength = diag.strength;
		out.maskShare = diag.maskShare;
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
