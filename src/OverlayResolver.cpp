// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Modding-Exception
// Copyright (c) 2026 HelmetOverlaysNG contributors. See COPYING and EXCEPTIONS.md.

#include "OverlayResolver.h"

#include "Settings.h"
#include "TextMatch.h"

namespace HONG::Resolver
{
	namespace
	{
		constexpr std::uint32_t kMaxOverlayBytes = 32u * 1024u * 1024u;

		// The record's model path without its extension. Some armor mods write a leading "meshes\" into the
		// record, and overlay packs name their files after either form, so a_stripMeshes selects which one.
		std::string NormalizeModelPath(std::string a_path, bool a_stripMeshes)
		{
			std::ranges::replace(a_path, '/', '\\');
			constexpr auto kMeshes = "meshes\\"sv;
			if (a_stripMeshes && a_path.size() > kMeshes.size() && _strnicmp(a_path.c_str(), kMeshes.data(), kMeshes.size()) == 0) {
				a_path.erase(0, kMeshes.size());
			}
			if (const auto dot = a_path.rfind('.'); dot != std::string::npos) {
				a_path.erase(dot);
			}
			return a_path;
		}

		bool IsSafeRelativePath(std::string_view a_path)
		{
			return !a_path.empty() && a_path.find("..") == std::string_view::npos && a_path.find(':') == std::string_view::npos && a_path.front() != '\\' && a_path.front() != '/';
		}

		bool Exists(const std::string& a_path)
		{
			return IsSafeRelativePath(a_path) && RE::BSResourceNiBinaryStream(a_path).good();
		}
	}

	namespace
	{
		constexpr std::string_view kHeadgearWords[] = { "hood", "hat", "cowl", "helm", "mask", "crown" };

		// Vanilla helmets occupy the hair and circlet slots rather than the head slot, so a wig is hair
		// alone: no head, circlet or body slot, and no headgear word in its name (hoods use the hair slot too).
		bool IsWig(const RE::TESObjectARMO* a_armor)
		{
			using Slot = RE::BGSBipedObjectForm::BipedObjectSlot;
			if (!a_armor->HasPartOf(Slot::kHair) || a_armor->HasPartOf(Slot::kHead) || a_armor->HasPartOf(Slot::kCirclet) || a_armor->HasPartOf(Slot::kBody)) {
				return false;
			}
			const std::string_view name = a_armor->GetName();
			return std::ranges::none_of(kHeadgearWords, [&](std::string_view a_word) { return ContainsNoCase(name, a_word); });
		}

		// Masks such as the dragon priest masks use the head slot as well as the circlet slot.
		bool IsCircletOnly(const RE::TESObjectARMO* a_armor)
		{
			return a_armor->IsCirclet() && !a_armor->HasPartOf(RE::BGSBipedObjectForm::BipedObjectSlot::kHead);
		}
	}

	bool IsHeadgearCandidate(const RE::TESObjectARMO* a_armor)
	{
		if (!a_armor) {
			return false;
		}
		const auto* settings = Settings::GetSingleton();
		if (settings->skipWigs && IsWig(a_armor)) {
			return false;
		}
		return !(settings->skipCirclets && IsCircletOnly(a_armor));
	}

	RE::TESObjectARMO* FindWornHeadgear(RE::Actor* a_actor)
	{
		if (!a_actor) {
			return nullptr;
		}
		using Slot = RE::BGSBipedObjectForm::BipedObjectSlot;
		for (const auto slot : { Slot::kHead, Slot::kHair }) {
			if (const auto armor = a_actor->GetWornArmor(slot); IsHeadgearCandidate(armor)) {
				return armor;
			}
		}
		return nullptr;
	}

	Resolution Resolve(const RE::TESObjectARMO* a_armor, bool a_female)
	{
		Resolution result;
		if (!a_armor) {
			return result;
		}

		const auto& root = Settings::GetSingleton()->overlayRoot;
		const std::size_t order[2] = { a_female ? 1u : 0u, a_female ? 0u : 1u };
		for (const auto sex : order) {
			const char* model = a_armor->worldModels[sex].GetModel();
			if (!model || !*model) {
				continue;
			}
			if (result.model.empty()) {
				result.model = model;
			}
			for (const bool strip : { true, false }) {
				auto candidate = std::format("{}\\{}.dds", root, NormalizeModelPath(model, strip));
				if (std::ranges::find(result.tried, candidate) != result.tried.end()) {
					continue;
				}
				const bool found = Exists(candidate);
				result.tried.push_back(candidate);
				if (found) {
					result.path = std::move(candidate);
					return result;
				}
			}
		}
		return result;
	}

	std::optional<std::string> ResolveOverlayPath(const RE::TESObjectARMO* a_armor, bool a_female)
	{
		return Resolve(a_armor, a_female).path;
	}

	std::vector<std::uint8_t> ReadFile(const std::string& a_path)
	{
		RE::BSResourceNiBinaryStream stream(a_path);
		if (!stream.good()) {
			return {};
		}
		const std::uint32_t size = stream.stream->totalSize;
		if (size == 0 || size > kMaxOverlayBytes) {
			return {};
		}
		std::vector<std::uint8_t> bytes(size);
		if (!stream.read(reinterpret_cast<char*>(bytes.data()), size)) {
			return {};
		}
		return bytes;
	}
}
