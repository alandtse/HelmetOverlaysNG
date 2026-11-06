// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Modding-Exception
#include "BrowsePage.h"

#include <catch2/catch_test_macros.hpp>
#include <limits>

namespace
{
	struct Entry
	{
		int id;
		std::string group;
	};
	const auto all = [](const Entry&) { return true; };
	const auto key = [](const Entry& entry) { return entry.group; };
}

TEST_CASE("Every record beyond the old preview cap is reachable", "[browse]")
{
	std::vector<Entry> entries;
	for (int i = 0; i < 125; ++i) {
		entries.push_back({ i, std::to_string(i) });
	}
	std::vector<int> visited;
	for (std::size_t page = 0; page < 3; ++page) {
		const auto result = HONG::Debug::SelectPage(entries, page, 60, all, key);
		REQUIRE(result.total == 125);
		REQUIRE(result.pages == 3);
		REQUIRE(result.page == page);
		for (const auto& entry : result.items) {
			visited.push_back(entry.id);
		}
	}
	REQUIRE(visited.size() == entries.size());
	for (std::size_t i = 0; i < visited.size(); ++i) {
		REQUIRE(visited[i] == static_cast<int>(i));
	}
}

TEST_CASE("Grouping happens before pagination and after filtering", "[browse]")
{
	const std::vector<Entry> entries{ { 1, "same art" }, { 2, "same art" }, { 3, "other art" }, { 4, "third art" } };
	const auto result = HONG::Debug::SelectPage(entries, 1, 2, all, key, true);
	REQUIRE(result.total == 3);
	REQUIRE(result.items.size() == 1);
	REQUIRE(result.items[0].id == 4);
	const auto filtered = HONG::Debug::SelectPage(entries, 0, 60, [](const Entry& entry) { return entry.id > 1; }, key, true);
	REQUIRE(filtered.total == 3);
	REQUIRE(filtered.items.front().id == 2);  // a matching variant must remain searchable
	const auto raw = HONG::Debug::SelectPage(entries, 0, 60, all, key);
	REQUIRE(raw.total == 4);
}

TEST_CASE("Empty and stale preview pages are safe", "[browse]")
{
	const std::vector<Entry> entries{ { 1, "a" }, { 2, "b" }, { 3, "c" } };
	const auto stale = HONG::Debug::SelectPage(entries, std::numeric_limits<std::size_t>::max(), 2, all, key);
	REQUIRE(stale.page == 1);
	REQUIRE(stale.items.size() == 1);
	REQUIRE(stale.items[0].id == 3);
	const auto empty = HONG::Debug::SelectPage(entries, 99, 60, [](const Entry&) { return false; }, key);
	REQUIRE(empty.total == 0);
	REQUIRE(empty.page == 0);
	REQUIRE(empty.pages == 1);
	REQUIRE(empty.items.empty());
	const auto zeroSize = HONG::Debug::SelectPage(entries, 0, 0, all, key);
	REQUIRE(zeroSize.items.size() == 1);
}
