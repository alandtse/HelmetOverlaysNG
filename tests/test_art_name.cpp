// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Modding-Exception
// Copyright (c) 2026 HelmetOverlaysNG contributors. See COPYING and EXCEPTIONS.md.

#include <catch2/catch_test_macros.hpp>

#include "ArtName.h"

using HONG::ArtName::SearchTerm;

TEST_CASE("the deepest armor folder names the art", "[art-name]")
{
	REQUIRE(SearchTerm("Armor\\Ahzidal\\AhzidalHelmet_gnd.dds") == "Ahzidal");
	REQUIRE(SearchTerm("Armor\\WarbringerArmor\\helmetgnd.dds") == "WarbringerArmor");
}

TEST_CASE("structural folders are skipped", "[art-name]")
{
	REQUIRE(SearchTerm("Armor\\steel-elven\\m\\HelmetGND.dds") == "steel-elven");
	REQUIRE(SearchTerm("Armor\\TalosHousecarl\\GO\\Helmet_go.dds") == "TalosHousecarl");
	REQUIRE(SearchTerm("Armor\\Iron\\Male\\HelmetGND.dds") == "Iron");
}

TEST_CASE("forward slashes and a bare file name work", "[art-name]")
{
	REQUIRE(SearchTerm("Armor/Ebony Elven/helmetGND.dds") == "Ebony Elven");
	REQUIRE(SearchTerm("SilverHelm_GO.dds") == "SilverHelm");
}

TEST_CASE("only structural folders fall back to the file name", "[art-name]")
{
	REQUIRE(SearchTerm("Armor\\Mask_gnd.dds") == "Mask");
	REQUIRE(SearchTerm("Armor\\HelmetGO.dds") == "Helmet");
}

TEST_CASE("a file name that is only a suffix is kept", "[art-name]")
{
	REQUIRE(SearchTerm("go.dds") == "go");
}
