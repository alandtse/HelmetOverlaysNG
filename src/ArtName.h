// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Modding-Exception
// Copyright (c) 2026 HelmetOverlaysNG contributors. See COPYING and EXCEPTIONS.md.
//
// Self-contained so the unit tests can include it without the game headers.

#pragma once

#include <algorithm>
#include <array>
#include <cctype>
#include <string>
#include <string_view>

namespace HONG::ArtName
{
	// ASCII lower case, for comparing VFS paths.
	inline std::string Lower(std::string_view a_text)
	{
		std::string out(a_text);
		std::ranges::transform(out, out.begin(), [](unsigned char a_c) { return static_cast<char>(std::tolower(a_c)); });
		return out;
	}

	// a_path without a leading a_root and separator when it has one (compared ignoring case), else unchanged.
	inline std::string StripRoot(std::string_view a_path, std::string_view a_root)
	{
		if (a_path.size() > a_root.size() + 1 && a_path[a_root.size()] == '\\' && Lower(a_path.substr(0, a_root.size())) == Lower(a_root)) {
			return std::string(a_path.substr(a_root.size() + 1));
		}
		return std::string(a_path);
	}

	namespace detail
	{
		// Folder names the art packs use for structure rather than for the armor.
		inline bool IsGeneric(const std::string& a_lowerName)
		{
			static constexpr std::array<std::string_view, 9> kGeneric{ "m", "f", "male", "female", "go", "gnd", "armor", "helmet", "helmets" };
			return std::ranges::find(kGeneric, a_lowerName) != kGeneric.end();
		}

		inline std::string StripSuffix(std::string a_stem)
		{
			for (const std::string_view suffix : { "_gnd", "_go", "gnd", "go" }) {
				const auto lower = Lower(a_stem);
				if (lower.size() > suffix.size() && lower.ends_with(suffix)) {
					a_stem.resize(a_stem.size() - suffix.size());
					break;
				}
			}
			return a_stem;
		}
	}

	// A name to search for from an overlay path below the overlay root, such as
	// "Armor\\Ahzidal\\AhzidalHelmet_gnd.dds": the deepest folder that names an armor, else the file name
	// without its extension and the usual ground-overlay suffix.
	inline std::string SearchTerm(std::string_view a_relativePath)
	{
		std::string path(a_relativePath);
		std::ranges::replace(path, '/', '\\');
		const auto fileStart = path.find_last_of('\\');
		std::string folders = fileStart == std::string::npos ? std::string() : path.substr(0, fileStart);
		std::string file = fileStart == std::string::npos ? path : path.substr(fileStart + 1);

		while (!folders.empty()) {
			const auto split = folders.find_last_of('\\');
			const std::string folder = split == std::string::npos ? folders : folders.substr(split + 1);
			if (!detail::IsGeneric(Lower(folder))) {
				return folder;
			}
			folders = split == std::string::npos ? std::string() : folders.substr(0, split);
		}

		if (const auto dot = file.find_last_of('.'); dot != std::string::npos) {
			file.resize(dot);
		}
		return detail::StripSuffix(std::move(file));
	}
}
