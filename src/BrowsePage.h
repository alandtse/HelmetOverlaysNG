// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Modding-Exception
#pragma once

#include <algorithm>
#include <cstddef>
#include <string>
#include <unordered_set>
#include <vector>

namespace HONG::Debug
{
	template <class T>
	struct BrowsePage
	{
		std::vector<T> items;
		std::size_t total{ 0 };
		std::size_t page{ 0 };
		std::size_t pages{ 1 };
	};

	// Filter/group before paging. Clamp stale pages when a scan or filter changes the count.
	template <class T, class Predicate, class Key>
	BrowsePage<T> SelectPage(const std::vector<T>& a_entries, std::size_t a_page, std::size_t a_pageSize,
		Predicate a_matches, Key a_key, bool a_unique = false)
	{
		std::vector<const T*> matches;
		std::unordered_set<std::string> seen;
		for (const auto& entry : a_entries) {
			if (a_matches(entry) && (!a_unique || seen.insert(a_key(entry)).second)) {
				matches.push_back(&entry);
			}
		}
		BrowsePage<T> out;
		out.total = matches.size();
		a_pageSize = std::max(std::size_t{ 1 }, a_pageSize);
		out.pages = out.total ? 1 + (out.total - 1) / a_pageSize : 1;
		out.page = std::min(a_page, out.pages - 1);
		const auto begin = out.page * a_pageSize;
		const auto end = begin + std::min(a_pageSize, out.total - begin);
		for (auto i = begin; i < end; ++i) {
			out.items.push_back(*matches[i]);
		}
		return out;
	}
}
