// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Modding-Exception
// Copyright (c) 2026 HelmetOverlaysNG contributors. See COPYING and EXCEPTIONS.md.

#include "Debug.h"

#include "ArtMetrics.h"
#include "ArtName.h"
#include "OverlayController.h"
#include "OverlayResolver.h"
#include "Settings.h"
#include "TextMatch.h"

#include <filesystem>
#include <mutex>
#include <unordered_set>

namespace HONG::Debug
{
	namespace
	{
		constexpr std::size_t kScanBatch = 48;
		constexpr std::size_t kClassifyBatch = 4;  // each reads a whole DDS, about 2 MB
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
				if (!Resolver::IsHeadgearCandidate(armor)) {
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

		std::size_t g_classifyNext = 0;
		std::vector<OrphanArt> g_orphans;

		void FindOrphans()
		{
			g_orphans.clear();
			std::unordered_set<std::string> used;
			for (const auto& entry : g_entries) {
				if (!entry.overlay.empty()) {
					used.insert(ArtName::Lower(entry.overlay));
				}
			}

			const auto& root = Settings::GetSingleton()->overlayRoot;
			const auto dir = std::filesystem::path(L"Data") / std::filesystem::path(root);
			std::error_code ec;
			for (std::filesystem::recursive_directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
				if (!it->is_regular_file(ec) || ArtName::Lower(it->path().extension().string()) != ".dds") {
					continue;
				}
				const auto relative = std::filesystem::relative(it->path(), dir, ec).string();
				auto path = root + "\\" + relative;
				if (!used.contains(ArtName::Lower(path))) {
					g_orphans.push_back({ std::move(path), ArtName::SearchTerm(relative) });
				}
			}
			std::ranges::sort(g_orphans, [](const OrphanArt& a_lhs, const OrphanArt& a_rhs) {
				const int byTerm = _stricmp(a_lhs.term.c_str(), a_rhs.term.c_str());
				return byTerm != 0 ? byTerm < 0 : _stricmp(a_lhs.path.c_str(), a_rhs.path.c_str()) < 0;
			});
			g_coverage.orphans = static_cast<std::uint32_t>(g_orphans.size());
		}

		void ClassifyStep()
		{
			if (!g_coverage.classifying) {
				return;
			}
			const std::size_t total = g_entries.size() + g_orphans.size();
			for (std::size_t done = 0; g_classifyNext < total && done < kClassifyBatch; ++g_classifyNext) {
				const bool isEntry = g_classifyNext < g_entries.size();
				const std::string* path = nullptr;
				float* color = nullptr;
				if (isEntry) {
					auto& entry = g_entries[g_classifyNext];
					if (entry.art > 0) {
						path = &entry.overlay;
						color = &entry.color;
					}
				} else {
					auto& orphan = g_orphans[g_classifyNext - g_entries.size()];
					path = &orphan.path;
					color = &orphan.color;
				}
				if (!path || path->empty()) {
					continue;
				}
				const auto bytes = Resolver::ReadFile(*path);
				*color = ArtMetrics::ColoredFraction(bytes.data(), bytes.size());
				++done;
				if (isEntry) {
					++g_coverage.classified;
					if (*color >= 0.0f) {
						++(*color >= ArtMetrics::kColoredShare ? g_coverage.colored : g_coverage.blackMasks);
					}
				}
			}
			if (g_classifyNext >= total) {
				g_coverage.classifying = false;
				g_coverage.checked = true;
				logger::info("Overlay art checked: {} with color, {} black masks, {} unreadable.", g_coverage.colored, g_coverage.blackMasks, g_coverage.classified - g_coverage.colored - g_coverage.blackMasks);
			}
		}

		bool MatchesArt(const HelmetEntry& a_entry, ArtFilter a_art)
		{
			switch (a_art) {
			case ArtFilter::kMissing:
				return a_entry.art == 0;
			case ArtFilter::kColored:
				return a_entry.color >= ArtMetrics::kColoredShare;
			case ArtFilter::kBlack:
				return a_entry.color >= 0.0f && a_entry.color < ArtMetrics::kColoredShare;
			case ArtFilter::kUnchecked:
				return a_entry.art > 0 && a_entry.color < 0.0f;
			default:
				return true;
			}
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
			FindOrphans();

			const float percent = g_coverage.eligible ? 100.0f * static_cast<float>(g_coverage.covered) / static_cast<float>(g_coverage.eligible) : 0.0f;
			logger::info("Overlay coverage: {} of {} playable head/hair armors have art ({:.0f}%); {} without art; {} skipped (no world model).",
				g_coverage.covered, g_coverage.eligible, percent, g_coverage.missing, g_coverage.noModel);
			if (g_coverage.eligible > 0 && g_coverage.covered == 0) {
				logger::warn("No overlay art was found for any helmet. Install an overlay art pack (Helmet Overlays, Helmet Overlays Enhanced Edition or Helmet Overlay Addon) and check that Data\\{} exists.", Settings::GetSingleton()->overlayRoot);
			}
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
			ClassifyStep();
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
			entry.overlay = resolution.path.value_or(std::string());
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
			entry.overlay.clear();
			entry.color = -1.0f;
		}
		g_coverage = Coverage{};
		g_orphans.clear();
		g_classifyNext = 0;
		g_next = 0;
		g_state = ScanState::kIdle;
		g_built = false;
	}

	Coverage GetCoverage()
	{
		std::scoped_lock lock{ g_lock };
		return g_coverage;
	}

	void ClassifyArt()
	{
		std::scoped_lock lock{ g_lock };
		if (g_state != ScanState::kDone || g_coverage.classifying) {
			return;
		}
		for (auto& entry : g_entries) {
			entry.color = -1.0f;
		}
		for (auto& orphan : g_orphans) {
			orphan.color = -1.0f;
		}
		g_coverage.classified = g_coverage.colored = g_coverage.blackMasks = 0;
		g_coverage.checked = false;
		g_coverage.classifying = true;
		g_classifyNext = 0;
	}

	std::vector<HelmetEntry> FilterHelmets(const std::string& a_filter, ArtFilter a_art, std::size_t a_limit, std::size_t& a_totalMatches)
	{
		std::scoped_lock lock{ g_lock };
		if (!g_built) {
			BuildEntries();
		}
		std::vector<HelmetEntry> out;
		a_totalMatches = 0;
		for (const auto& entry : g_entries) {
			if (!MatchesArt(entry, a_art)) {
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

	std::vector<OrphanArt> FilterOrphans(const std::string& a_filter, std::size_t a_limit, std::size_t& a_totalMatches)
	{
		std::scoped_lock lock{ g_lock };
		std::vector<OrphanArt> out;
		a_totalMatches = 0;
		for (const auto& orphan : g_orphans) {
			if (!ContainsNoCase(orphan.path, a_filter)) {
				continue;
			}
			if (out.size() < a_limit) {
				out.push_back(orphan);
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
		out.color = diag.color;
		out.strength = diag.strength;
		out.maskShare = diag.maskShare;
		out.light = diag.light;
		out.sceneSampled = diag.sceneSampled;
		out.scene = diag.scene;
		out.tint = diag.tint;
		out.lightTint = diag.lightTint;
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

	void PreviewFile(const std::string& a_path)
	{
		OverlayController::GetSingleton()->SetPreviewFile(a_path);
	}

	std::string PreviewingFile()
	{
		return OverlayController::GetSingleton()->GetPreviewFile();
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

	Override& SoundStrengthOverride()
	{
		static Override instance;
		return instance;
	}

	Override& TintStrengthOverride()
	{
		static Override instance;
		return instance;
	}

	Override& LightLevelOverride()
	{
		static Override instance;
		return instance;
	}
}
