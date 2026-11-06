// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Modding-Exception
// Copyright (c) 2026 HelmetOverlaysNG contributors. See COPYING and EXCEPTIONS.md.

#include "MaskPublish.h"
#include "MaskShaders.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <format>
#include <functional>
#include <limits>
#include <string_view>
#include <vector>

using HONG::MaskPublish::CanPublish;
using HONG::MaskPublish::Publishable;
using HONG::MaskPublish::Refusal;
using HONG::MaskPublish::Why;
using HONG::MaskPublish::WorthLogging;

namespace
{
	// A candidate that stamps, so each case can take away exactly one thing.
	Publishable Good()
	{
		Publishable in{};
		in.permitted = true;
		in.hasMask = true;
		in.maskSized = true;
		in.fitX = 1.0f;
		in.fitY = 1.0f;
		in.onScreenX = 0.5f;
		in.onScreenY = 0.5f;
		return in;
	}
}

TEST_CASE("a fully specified candidate publishes", "[mask]")
{
	REQUIRE(CanPublish(Good()));
	REQUIRE(Why(Good()) == Refusal::None);
}

TEST_CASE("every refusal is reachable and named", "[mask]")
{
	{
		auto in = Good();
		in.permitted = false;
		REQUIRE_FALSE(CanPublish(in));
		REQUIRE(Why(in) == Refusal::NotPermitted);
	}
	{
		auto in = Good();
		in.hasMask = false;
		REQUIRE_FALSE(CanPublish(in));
		REQUIRE(Why(in) == Refusal::NoMask);
	}
	{
		auto in = Good();
		in.maskSized = false;
		REQUIRE_FALSE(CanPublish(in));
		REQUIRE(Why(in) == Refusal::MaskUnSized);
	}
	{
		auto in = Good();
		in.fitX = 0.0f;
		REQUIRE_FALSE(CanPublish(in));
		REQUIRE(Why(in) == Refusal::BadFit);
	}
	{
		auto in = Good();
		in.fitY = -1.0f;
		REQUIRE_FALSE(CanPublish(in));
		REQUIRE(Why(in) == Refusal::BadFit);
	}
	{
		auto in = Good();
		in.onScreenX = HONG::OverlayMask::Shaders::kMinStampSize * 0.5f;
		REQUIRE_FALSE(CanPublish(in));
		REQUIRE(Why(in) == Refusal::TooSmall);
	}
	{
		auto in = Good();
		in.onScreenY = 0.0f;
		REQUIRE_FALSE(CanPublish(in));
		REQUIRE(Why(in) == Refusal::TooSmall);
	}
}

TEST_CASE("a non-finite fit refuses rather than escaping the comparison", "[mask]")
{
	for (float bad : { std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity() }) {
		auto in = Good();
		in.fitX = bad;
		REQUIRE_FALSE(CanPublish(in));
		REQUIRE(Why(in) == Refusal::BadFit);
	}
	for (float bad : { std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity() }) {
		auto in = Good();
		in.onScreenX = bad;
		REQUIRE_FALSE(CanPublish(in));
		REQUIRE(Why(in) == Refusal::TooSmall);
	}
	REQUIRE_FALSE(HONG::MaskPublish::UsableExtent(std::numeric_limits<float>::infinity()));
	REQUIRE(HONG::MaskPublish::UsableExtent(HONG::OverlayMask::Shaders::kMinStampSize));
}

TEST_CASE("the smallest published stamp is the one the inset was tested at", "[mask]")
{
	const auto in = [](float a_size) {
		Publishable p = Good();
		p.onScreenX = a_size;
		p.onScreenY = a_size;
		return p;
	};
	REQUIRE(CanPublish(in(HONG::OverlayMask::Shaders::kMinStampSize)));
	REQUIRE(Why(in(HONG::OverlayMask::Shaders::kMinStampSize)) == Refusal::None);
	const float justUnder = std::nextafter(HONG::OverlayMask::Shaders::kMinStampSize, 0.0f);
	REQUIRE_FALSE(CanPublish(in(justUnder)));
	REQUIRE(Why(in(justUnder)) == Refusal::TooSmall);
}

TEST_CASE("only impossible geometry is worth a log line", "[mask]")
{
	REQUIRE_FALSE(WorthLogging(Refusal::None));
	REQUIRE_FALSE(WorthLogging(Refusal::NotPermitted));
	REQUIRE_FALSE(WorthLogging(Refusal::NoMask));
	REQUIRE_FALSE(WorthLogging(Refusal::TooSmall));  // a tiny overlay is legitimate, not a bug
	REQUIRE(WorthLogging(Refusal::BadFit));
	REQUIRE(WorthLogging(Refusal::MaskUnSized));
}

TEST_CASE("the tested predicate and the running predicate are one object", "[mask]")
{
	auto sweep = [](const Publishable& a_in) {
		REQUIRE(CanPublish(a_in) == (Why(a_in) == Refusal::None));
	};

	// Every combination of the discrete gates, each against a usable and an unusable float pair.
	for (int bits = 0; bits < 8; ++bits) {
		for (float scale : { 0.0f, -1.0f, 0.005f, 0.01f, 1.0f, 500.0f }) {
			Publishable in{};
			in.permitted = (bits & 1) != 0;
			in.hasMask = (bits & 2) != 0;
			in.maskSized = (bits & 4) != 0;
			in.fitX = scale > 0.0f ? 1.0f : scale;
			in.fitY = 1.0f;
			in.onScreenX = scale;
			in.onScreenY = 1.0f;
			sweep(in);
		}
	}

	// The non-finite inputs, including the two that a plain `> 0.0f` comparison lets through.
	for (float bad : { std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity() }) {
		auto fitBad = Good();
		fitBad.fitX = bad;
		sweep(fitBad);
		auto extentBad = Good();
		extentBad.onScreenY = bad;
		sweep(extentBad);
	}

	// And the single publishable case, so the sweep cannot pass by refusing everything.
	REQUIRE(CanPublish(Good()));
}

TEST_CASE("when several gates fail, the one logged is the one first in the chain", "[mask]")
{
	struct Gate
	{
		const char* name;
		Refusal reason;
		std::function<void(Publishable&)> break_;
	};

	// In the order the chain tests them.
	const std::vector<Gate> gates{
		{ "permitted", Refusal::NotPermitted, [](Publishable& a_in) { a_in.permitted = false; } },
		{ "has-mask", Refusal::NoMask, [](Publishable& a_in) { a_in.hasMask = false; } },
		{ "mask-sized", Refusal::MaskUnSized, [](Publishable& a_in) { a_in.maskSized = false; } },
		{ "fit", Refusal::BadFit, [](Publishable& a_in) { a_in.fitX = 0.0f; } },
		{ "extent", Refusal::TooSmall, [](Publishable& a_in) { a_in.onScreenX = 0.0f; } },
	};

	for (const auto& gate : gates) {
		auto in = Good();
		gate.break_(in);
		INFO(gate.name);
		CHECK(Why(in) == gate.reason);
	}

	for (std::size_t first = 0; first < gates.size(); ++first) {
		for (std::size_t second = first + 1; second < gates.size(); ++second) {
			INFO(gates[first].name << " and " << gates[second].name);
			auto forward = Good();
			gates[first].break_(forward);
			gates[second].break_(forward);
			CHECK(Why(forward) == gates[first].reason);

			auto backward = Good();
			gates[second].break_(backward);
			gates[first].break_(backward);
			CHECK(Why(backward) == gates[first].reason);
		}
	}

	// And all of them at once, which must still name the first.
	auto all = Good();
	for (const auto& gate : gates) {
		gate.break_(all);
	}
	REQUIRE(Why(all) == Refusal::NotPermitted);
}

TEST_CASE("every refusal is classified explicitly, and none is silently unlogged", "[mask]")
{
	const std::vector<Refusal> all{
		Refusal::None,
		Refusal::NotPermitted,
		Refusal::NoMask,
		Refusal::MaskUnSized,
		Refusal::BadFit,
		Refusal::TooSmall,
	};
	REQUIRE(all.size() == 6);

	// The two geometry faults are the ones a user can act on; everything else is ordinary idle.
	for (const auto reason : all) {
		INFO(static_cast<int>(reason));
		const bool actionable = reason == Refusal::BadFit || reason == Refusal::MaskUnSized;
		REQUIRE(WorthLogging(reason) == actionable);
	}

	auto unsized = Good();
	unsized.maskSized = false;
	REQUIRE(WorthLogging(Why(unsized)));

	auto badFit = Good();
	badFit.fitY = std::numeric_limits<float>::infinity();
	REQUIRE(WorthLogging(Why(badFit)));

	// And the ordinary states must stay quiet, or the log fills with one line per frame.
	auto idle = Good();
	idle.permitted = false;
	REQUIRE_FALSE(WorthLogging(Why(idle)));
	REQUIRE_FALSE(WorthLogging(Why(Good())));
}

TEST_CASE("the log distinguishes the two reasons it is allowed to print", "[mask]")
{
	REQUIRE(Name(Refusal::BadFit) != Name(Refusal::MaskUnSized));

	// Both must be named at all - an empty string would satisfy inequality and still be useless.
	REQUIRE_FALSE(Name(Refusal::BadFit).empty());
	REQUIRE_FALSE(Name(Refusal::MaskUnSized).empty());

	const std::vector<Refusal> all{
		Refusal::None, Refusal::NotPermitted, Refusal::NoMask,
		Refusal::MaskUnSized, Refusal::BadFit, Refusal::TooSmall
	};
	for (std::size_t i = 0; i < all.size(); ++i) {
		for (std::size_t j = i + 1; j < all.size(); ++j) {
			INFO(Name(all[i]) << " vs " << Name(all[j]));
			REQUIRE(Name(all[i]) != Name(all[j]));
		}
	}

	const auto message = Message(Refusal::BadFit, 0.25f, 0.5f, 1.0f);
	REQUIRE(message.find("fit is not a usable fraction") != std::string::npos);

	REQUIRE(message.find(std::format("{:.17g}", 0.25f)) != std::string::npos);
	REQUIRE(message.find(std::format("{:.17g}", 0.5f)) != std::string::npos);
	REQUIRE(message.find(std::format("{:.17g}", 1.0f)) != std::string::npos);

	const auto boundary = Message(Refusal::BadFit, 0.0099999f, 1.0f, 1.0f);
	REQUIRE(boundary.find("0.0100") == std::string::npos);
	REQUIRE(boundary.find(std::format("{:.17g}", 0.0099999f)) != std::string::npos);

	for (const auto reason : all) {
		if (reason == Refusal::None) {
			REQUIRE(Name(reason) == Summary(reason));
			continue;
		}
		INFO(Name(reason) << " / " << Summary(reason));
		REQUIRE(Name(reason) != Summary(reason));
	}

	// The two logged reasons must produce different messages end to end, not merely different names.
	REQUIRE(Message(Refusal::BadFit, 0.25f, 0.5f, 1.0f) != Message(Refusal::MaskUnSized, 0.25f, 0.5f, 1.0f));
}

TEST_CASE("both user-facing mask signals agree about whether it is a fault", "[mask]")
{
	constexpr HONG::MaskPublish::Refusal all[] = {
		HONG::MaskPublish::Refusal::None,
		HONG::MaskPublish::Refusal::NotPermitted,
		HONG::MaskPublish::Refusal::NoMask,
		HONG::MaskPublish::Refusal::MaskUnSized,
		HONG::MaskPublish::Refusal::BadFit,
		HONG::MaskPublish::Refusal::TooSmall,
	};

	for (const auto reason : all) {
		const auto summary = HONG::MaskPublish::Summary(reason);
		CHECK(!summary.empty());

		const bool stamped = summary == "stamped";
		CHECK(stamped == (reason == HONG::MaskPublish::Refusal::None));
	}
}

TEST_CASE("the recorded refusal round-trips every value", "[mask]")
{
	constexpr HONG::MaskPublish::Refusal all[] = {
		HONG::MaskPublish::Refusal::None,
		HONG::MaskPublish::Refusal::NotPermitted,
		HONG::MaskPublish::Refusal::NoMask,
		HONG::MaskPublish::Refusal::MaskUnSized,
		HONG::MaskPublish::Refusal::BadFit,
		HONG::MaskPublish::Refusal::TooSmall,
	};

	for (const auto reason : all) {
		HONG::MaskPublish::RecordRefusal(reason);
		CHECK(HONG::MaskPublish::CurrentRefusal() == reason);
	}
}

TEST_CASE("every refusal has its own summary and none falls through to the tail", "[mask]")
{
	constexpr HONG::MaskPublish::Refusal all[] = {
		HONG::MaskPublish::Refusal::None,
		HONG::MaskPublish::Refusal::NotPermitted,
		HONG::MaskPublish::Refusal::NoMask,
		HONG::MaskPublish::Refusal::MaskUnSized,
		HONG::MaskPublish::Refusal::BadFit,
		HONG::MaskPublish::Refusal::TooSmall,
	};

	std::vector<std::string_view> seen;
	for (const auto reason : all) {
		const auto text = HONG::MaskPublish::Summary(reason);
		CHECK_FALSE(text.empty());
		CHECK(text != "unknown");
		CHECK(std::ranges::find(seen, text) == seen.end());
		seen.push_back(text);
	}
	CHECK(seen.size() == 6);
}

TEST_CASE("a stamp is fresh inside the age window and stale after it", "[mask-publish]")
{
	using HONG::MaskPublish::kMaxStampAgeMs;
	using HONG::MaskPublish::StampFresh;
	REQUIRE(StampFresh(5000, 5000));
	REQUIRE(StampFresh(5000, 5000 + kMaxStampAgeMs));
	REQUIRE_FALSE(StampFresh(5000, 5001 + kMaxStampAgeMs));
}

TEST_CASE("a stamp that was never published, or was cleared, is stale", "[mask-publish]")
{
	REQUIRE_FALSE(HONG::MaskPublish::StampFresh(0, 0));
	REQUIRE_FALSE(HONG::MaskPublish::StampFresh(0, 1000));
}

TEST_CASE("a clock that went backwards reads as stale", "[mask-publish]")
{
	REQUIRE_FALSE(HONG::MaskPublish::StampFresh(5000, 4000));
}
