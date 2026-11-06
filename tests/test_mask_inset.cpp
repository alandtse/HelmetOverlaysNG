// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Modding-Exception
// Copyright (c) 2026 HelmetOverlaysNG contributors. See COPYING and EXCEPTIONS.md.

#include "MaskInset.h"
#include "MaskShaders.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>
#include <type_traits>

using HONG::MaskInset::Along;
using HONG::MaskInset::kScreenInsetPx;

TEST_CASE("the inset is at least the screen margin in art texels", "[mask]")
{
	const auto v = Along(1080.0f, 1.0f, 270);
	REQUIRE(v == Catch::Approx(1.0f / 270.0f));
	REQUIRE(v * 1080.0f >= kScreenInsetPx);

	// 4K display: 2160/270 = 8 display px per texel, so a 3 px inset is still under one texel.
	const auto uhd = Along(2160.0f, 1.0f, 270);
	REQUIRE(uhd == Catch::Approx(1.0f / 270.0f));
}

TEST_CASE("the inset grows when the art is scaled down on screen", "[mask]")
{
	const auto half = Along(1080.0f, 0.5f, 270);
	REQUIRE(half == Catch::Approx(2.0f / 270.0f));

	const auto quarter = Along(1080.0f, 0.25f, 270);
	REQUIRE(quarter == Catch::Approx(3.0f / 270.0f));

	std::size_t bad = 0;
	for (const float fraction : { 1.0f, 0.5f, 0.25f, 0.1f }) {
		const auto step = Along(1080.0f, fraction, 270);
		CHECK(step * 270.0f * fraction * 1080.0f >= kScreenInsetPx);
		if (!(step * 270.0f * fraction * 1080.0f >= kScreenInsetPx)) {
			++bad;
		}
	}
	CHECK(bad == 0);
}

TEST_CASE("the inset is never fractional", "[mask]")
{
	std::size_t fractional = 0;
	std::size_t subTexel = 0;
	for (const float fraction : { 1.0f, 0.73f, 0.5f, 0.31f, 0.1f }) {
		for (const std::uint32_t texels : { 270u, 480u, 2160u }) {
			const auto step = Along(1920.0f, fraction, texels);
			const auto inTexels = step * static_cast<float>(texels);
			CHECK(inTexels == Catch::Approx(std::ceil(inTexels)).margin(1e-4));
			CHECK(inTexels >= 1.0f);
			if (!(inTexels == Catch::Approx(std::ceil(inTexels)).margin(1e-4))) {
				++fractional;
			}
			if (!(inTexels >= 1.0f)) {
				++subTexel;
			}
		}
	}
	CHECK(fractional == 0);
	CHECK(subTexel == 0);
}

TEST_CASE("a zero inset only ever means degenerate input, never a live draw", "[mask]")
{
	REQUIRE(Along(1920.0f, 1.0f, 0) == 0.0f);
	REQUIRE(Along(0.0f, 1.0f, 270) == 0.0f);
	REQUIRE(Along(1920.0f, 0.0f, 270) == 0.0f);

	std::size_t belowOne = 0;
	for (const std::uint32_t texels : { 1u, 270u, 480u, 1920u, 11520u }) {
		for (const float pixels : { 1.0f, 1080.0f, 2560.0f, 7680.0f }) {
			for (const float fraction : { 0.01f, 0.5f, 1.0f, 1.6f }) {
				const auto inTexels = Along(pixels, fraction, texels) * static_cast<float>(texels);
				CHECK(inTexels >= 1.0f);
				if (!(inTexels >= 1.0f)) {
					++belowOne;
				}
			}
		}
	}
	CHECK(belowOne == 0);
}

TEST_CASE("the float path rounds the right way at the boundaries", "[mask]")
{
	REQUIRE(Along(1728.0f, 1.0f, 960) * 960.0f == Catch::Approx(2.0f));
	REQUIRE(Along(1728.0f, 1.0f, 1920) * 1920.0f == Catch::Approx(4.0f));
	REQUIRE(Along(1728.0f, 1.0f, 2880) * 2880.0f == Catch::Approx(5.0f));

	REQUIRE(Along(1832.0f, 1.25f, 3840) * 3840.0f == Catch::Approx(6.0f));

	std::size_t under = 0;
	std::size_t over = 0;
	std::size_t combinations = 0;
	for (const std::uint32_t texels : { 480u, 960u, 1920u, 2560u, 3840u, 11520u }) {
		for (const std::uint32_t pixels : { 1000u, 1440u, 1728u, 1832u, 2064u, 2160u, 2290u, 2560u }) {
			// Coverage is a decimal in the settings, so express it in hundredths too.
			for (const std::uint32_t percent : { 50u, 73u, 100u, 125u, 160u }) {
				++combinations;
				const float fraction = static_cast<float>(percent) / 100.0f;
				const auto inTexels = Along(static_cast<float>(pixels), fraction, texels) * static_cast<float>(texels);

				const std::uint64_t numerator = 3ull * 100ull * texels;
				const std::uint64_t denominator = static_cast<std::uint64_t>(pixels) * percent;
				CHECK(denominator > 0ull);
				const auto exactCeiling = (numerator + denominator - 1ull) / denominator;

				CHECK(inTexels >= static_cast<float>(exactCeiling));
				CHECK(inTexels <= static_cast<float>(exactCeiling) + 1.0f);
				if (!(inTexels >= static_cast<float>(exactCeiling))) {
					++under;
				}
				if (!(inTexels <= static_cast<float>(exactCeiling) + 1.0f)) {
					++over;
				}
			}
		}
	}
	CHECK(combinations == 240);
	CHECK(under == 0);
	CHECK(over == 0);
}

TEST_CASE("a supersampled panel needs a larger texel inset for the same screen margin", "[mask]")
{
	constexpr std::uint32_t kBaseArt = 1920;
	constexpr float kEyeViewWidth = 2560.0f;

	float previousTexels = 0.0f;
	for (const std::uint32_t supersample : { 1u, 2u, 3u }) {
		const auto texels = static_cast<std::uint32_t>(kBaseArt * supersample);
		const auto step = Along(kEyeViewWidth, 1.0f, texels);
		const auto inTexels = step * static_cast<float>(texels);

		// The screen-space margin is what the fix guarantees, at every supersample level.
		REQUIRE(inTexels * kEyeViewWidth / static_cast<float>(texels) >= kScreenInsetPx);
		REQUIRE(inTexels > previousTexels);
		previousTexels = inTexels;
	}

	REQUIRE(Along(kEyeViewWidth, 1.0f, 3840) == Catch::Approx(5.0f / 3840.0f));

	REQUIRE(Along(kEyeViewWidth, 1.0f, 3840) < Along(kEyeViewWidth, 1.0f, 1920));                  // base below viewport: falls
	REQUIRE(Along(kEyeViewWidth, 1.0f, 5120) == Catch::Approx(Along(kEyeViewWidth, 1.0f, 2560)));  // base == viewport: equal

	for (const std::uint32_t base : { 480u, 1280u, 1920u, 2560u, 3840u }) {
		for (const std::uint32_t supersample : { 1u, 2u, 3u }) {
			const auto texels = base * supersample;
			const auto inTexels = Along(kEyeViewWidth, 1.0f, texels) * static_cast<float>(texels);
			REQUIRE(inTexels * kEyeViewWidth / static_cast<float>(texels) >= kScreenInsetPx);
			if (supersample > 1) {
				const auto previous = Along(kEyeViewWidth, 1.0f, base * (supersample - 1)) *
				                      static_cast<float>(base * (supersample - 1));
				REQUIRE(inTexels >= previous);
			}
		}
	}
}

TEST_CASE("the fringe inset on real headset viewports", "[mask]")
{
	constexpr std::uint32_t kArtBase = 1920;
	struct Headset
	{
		const char* name;
		float eyeWidth;
	};
	for (const auto& headset : { Headset{ "Index/Vive Pro", 1440.0f }, Headset{ "Quest 3", 2064.0f },
			 Headset{ "Reverb G2", 2160.0f }, Headset{ "our capture", 2560.0f } }) {
		for (const std::uint32_t supersample : { 1u, 2u, 3u }) {
			const auto texels = kArtBase * supersample;
			const auto inTexels = Along(headset.eyeWidth, 1.0f, texels) * static_cast<float>(texels);
			INFO(headset.name << " at " << supersample << "x");
			// The one property that must never fail, whatever the headset.
			REQUIRE(inTexels * headset.eyeWidth / static_cast<float>(texels) >= kScreenInsetPx);
		}
	}

	// Pins the equal-UX regime, the one a global "UV strictly falls" rule would get wrong.
	const auto index1x = Along(1440.0f, 1.0f, kArtBase);
	const auto index2x = Along(1440.0f, 1.0f, kArtBase * 2);
	REQUIRE(index1x * kArtBase == Catch::Approx(4.0f));
	REQUIRE(index2x * kArtBase * 2 == Catch::Approx(8.0f));
	REQUIRE(index2x == Catch::Approx(index1x));

	// Pins the falls regime at a real viewport, so the other half of the pair stays covered.
	REQUIRE(Along(2560.0f, 1.0f, kArtBase * 2) < Along(2560.0f, 1.0f, kArtBase));
}

TEST_CASE("the UV step has no base-width rule, it is a rounding residue", "[mask]")
{
	int equalTotal = 0;
	int equalWithBaseBelowView = 0;
	for (const std::uint32_t base : { 960u, 1280u, 1600u, 1920u, 2560u }) {
		for (std::uint32_t eye = 1000; eye <= 3840; eye += 8) {
			const auto s1 = Along(static_cast<float>(eye), 1.0f, base);
			const auto s2 = Along(static_cast<float>(eye), 1.0f, base * 2);
			const auto s3 = Along(static_cast<float>(eye), 1.0f, base * 3);
			if (s1 == s2 && s2 == s3) {
				++equalTotal;
				if (base < eye)
					++equalWithBaseBelowView;
			}
		}
	}
	REQUIRE(equalTotal > 400);
	REQUIRE(equalWithBaseBelowView > 300);
}

TEST_CASE("the screen margin is an identity, not a tuned property", "[mask]")
{
	for (const float pixels : { 1.0f, 270.0f, 960.0f, 1440.0f, 2064.0f, 2160.0f, 2560.0f, 3840.0f }) {
		for (const std::uint32_t texels : { 1u, 2u, 3u, 480u, 1920u, 2560u, 3840u, 11520u }) {
			const auto step = Along(pixels, 1.0f, texels);
			REQUIRE(step * static_cast<float>(texels) * pixels / static_cast<float>(texels) >= kScreenInsetPx);
			REQUIRE(step * static_cast<float>(texels) >= kScreenInsetPx * static_cast<float>(texels) / pixels);
		}
	}
	// Concretely: 3 screen pixels at 2x supersample needs 5 texels, and truncation would give 4.
	REQUIRE(Along(2560.0f, 1.0f, 3840) * 3840.0f == Catch::Approx(5.0f));
	REQUIRE(Along(1440.0f, 1.0f, 1920) * 1920.0f == Catch::Approx(4.0f));
}

TEST_CASE("degenerate input gives no inset", "[mask]")
{
	REQUIRE(Along(1080.0f, 1.0f, 0) == 0.0f);
	REQUIRE(Along(0.0f, 1.0f, 270) == 0.0f);
	REQUIRE(Along(1080.0f, 0.0f, 270) == 0.0f);
	REQUIRE(Along(1080.0f, -1.0f, 270) == 0.0f);
	REQUIRE(Along(1080.0f, std::numeric_limits<float>::quiet_NaN(), 270) == 0.0f);
}

TEST_CASE("a display too large to need an inset still gets a whole texel", "[mask]")
{
	const float step = Along(std::numeric_limits<float>::infinity(), 1.0f, 270);
	REQUIRE(step > 0.0f);
	REQUIRE(step * 270.0f == Catch::Approx(1.0f));

	REQUIRE(Along(1.0e30f, 1.0f, 270) * 270.0f == Catch::Approx(1.0f));
}

TEST_CASE("a sub-pixel-wide art region cannot produce an infinite inset", "[mask]")
{
	const auto tiny = Along(1080.0f, 0.0001f, 270);
	REQUIRE(std::isfinite(tiny));
	REQUIRE(tiny * 270.0f == Catch::Approx(810.0f));

	// Finiteness across a sweep that reaches below anything the call site permits.
	for (const float fraction : { 0.0001f, 0.001f, 0.01f, 0.05f }) {
		for (const std::uint32_t texels : { 270u, 480u, 3840u }) {
			const auto step = Along(1080.0f, fraction, texels);
			REQUIRE(std::isfinite(step));
			REQUIRE(step >= 0.0f);
		}
	}
}

TEST_CASE("the stamp is deactivated before the inset maths can be asked for nonsense", "[mask]")
{
	REQUIRE(HONG::OverlayMask::Shaders::kMinStampSize == Catch::Approx(0.01f));

	for (const float pixels : { 1080.0f, 2560.0f }) {
		for (const std::uint32_t texels : { 270u, 1920u, 3840u }) {
			const auto inTexels = Along(pixels, HONG::OverlayMask::Shaders::kMinStampSize, texels) * static_cast<float>(texels);
			REQUIRE(inTexels > 0.0f);
			REQUIRE(inTexels < static_cast<float>(texels));
		}
	}

	REQUIRE(1.0f / 1080.0f < HONG::OverlayMask::Shaders::kMinStampSize);
}

TEST_CASE("the inset covers whole screen pixels, which is the unit that matters", "[mask]")
{
	for (std::uint32_t maskTexels : { 128u, 258u, 270u, 512u }) {
		for (float display : { 1440.0f, 2064.0f, 2160.0f, 2560.0f, 3840.0f }) {
			for (float fraction : { 0.01f, 0.05f, 0.25f, 0.5f, 1.0f }) {
				const float extent = display * fraction;  // art pixels on screen, what Along clamps on
				const float step = HONG::MaskInset::Along(display, fraction, maskTexels);
				REQUIRE(step * extent >= HONG::MaskInset::kScreenInsetPx);
			}
		}
	}
}

TEST_CASE("the one-pixel floor is below everything the gate admits", "[mask]")
{
	const float smallestExtent = 1440.0f * HONG::OverlayMask::Shaders::kMinStampSize;
	REQUIRE(smallestExtent > 1.0f);
	const float step = HONG::MaskInset::Along(1440.0f, HONG::OverlayMask::Shaders::kMinStampSize, 270);
	REQUIRE(step * smallestExtent >= HONG::MaskInset::kScreenInsetPx);
}

TEST_CASE("a collapsed erosion is unrepresentable, not merely discouraged", "[mask]")
{
	SECTION("degenerate axes are refused, never returned as a zero step")
	{
		CHECK_FALSE(HONG::MaskInset::Decide(0.0f, 1080.0f, 1.0f, 1.0f, 270, 270).has_value());     // no display
		CHECK_FALSE(HONG::MaskInset::Decide(-8.0f, 1080.0f, 1.0f, 1.0f, 270, 270).has_value());    // negative display
		CHECK_FALSE(HONG::MaskInset::Decide(1920.0f, 1080.0f, 0.0f, 1.0f, 270, 270).has_value());  // no art on X
		CHECK_FALSE(HONG::MaskInset::Decide(1920.0f, 1080.0f, 1.0f, 1.0f, 0, 270).has_value());    // no mask on X
		CHECK_FALSE(HONG::MaskInset::Decide(1920.0f, 1080.0f, 1.0f, 1.0f, 270, 0).has_value());    // no mask on Y
		CHECK_FALSE(HONG::MaskInset::Decide(std::numeric_limits<float>::quiet_NaN(), 1080.0f, 1.0f, 1.0f, 270, 270).has_value());
	}

	SECTION("one axis refusing is enough to refuse the stamp")
	{
		const auto partial = HONG::MaskInset::Decide(1920.0f, 1080.0f, 1.0f, 0.0f, 270, 270);
		CHECK_FALSE(partial.has_value());
	}

	SECTION("every returned step erodes on both axes")
	{
		for (const float display : { 1280.0f, 1920.0f, 2560.0f, 3840.0f }) {
			for (const float fraction : { 0.01f, 0.25f, 0.5f, 1.0f }) {
				for (const std::uint32_t texels : { 64u, 270u, 512u }) {
					const auto step = HONG::MaskInset::Decide(display, display, fraction, fraction, texels, texels);
					// CHECK expands to a do/while, so it can neither be negated in a condition nor initialise a
					// variable: test the value directly and let CHECK record it standalone.
					const bool haveStep = step.has_value();
					CHECK(haveStep);
					if (!haveStep) {
						continue;
					}
					CHECK(step->x() > 0.0f);
					CHECK(step->y() > 0.0f);
					CHECK(step->x() * static_cast<float>(texels) >= 1.0f);
					CHECK(step->y() * static_cast<float>(texels) >= 1.0f);
				}
			}
		}
	}
}

TEST_CASE("a Step cannot be built except by Decide", "[mask]")
{
	static_assert(!std::is_constructible_v<HONG::MaskInset::Step, float, float>, "Step must only be built by Decide, or a collapsed erosion becomes representable again");
	static_assert(!std::is_aggregate_v<HONG::MaskInset::Step>, "Step must not be an aggregate: brace-initialising it would bypass Decide");
	static_assert(!std::is_default_constructible_v<HONG::MaskInset::Step>, "a default Step would be a zero step");
	static_assert(std::is_copy_constructible_v<HONG::MaskInset::Step>, "optional<Step> must be able to return one");

	const auto step = HONG::MaskInset::Decide(2560.0f, 1440.0f, 1.0f, 1.0f, 270, 270);
	REQUIRE(step.has_value());
	CHECK(step->x() > 0.0f);
	CHECK(step->y() > 0.0f);
}

TEST_CASE("the inset holds in every direction, not just along the axes", "[mask]")
{
	constexpr float kSqrt2 = 1.41421356f;

	SECTION("the worst direction is exactly 45 degrees")
	{
		const auto factor = [](double a_degrees) {
			const double rad = a_degrees * 3.14159265358979323846 / 180.0;
			const double nx = std::cos(rad);
			const double ny = std::sin(rad);
			const double offsets[5][2] = { { 0.0, 0.0 }, { 1.0, 0.0 }, { -1.0, 0.0 }, { 0.0, 1.0 }, { 0.0, -1.0 } };
			double binding = -1.0e9;
			for (const auto& o : offsets) {
				binding = std::max(binding, nx * o[0] + ny * o[1]);
			}
			return binding;
		};

		double worst = 1.0e9;
		double worstAt = 0.0;
		for (int i = 0; i <= 90000; ++i) {
			const double degrees = static_cast<double>(i) / 1000.0;
			const double f = factor(degrees);
			if (f < worst) {
				worst = f;
				worstAt = degrees;
			}
		}
		CHECK(worstAt == Catch::Approx(45.0).margin(0.01));
		CHECK(worst == Catch::Approx(1.0 / std::sqrt(2.0)).epsilon(1e-9));
		CHECK(HONG::MaskInset::kScreenInsetPx * worst == Catch::Approx(2.1213).margin(0.001));
	}

	SECTION("the axial request is compensated so the promise holds everywhere")
	{
		CHECK(HONG::MaskInset::kAxisInsetPx >= HONG::MaskInset::kScreenInsetPx * kSqrt2);

		const auto factor = 1.0 / std::sqrt(2.0);
		CHECK(HONG::MaskInset::kAxisInsetPx * static_cast<float>(factor) >= HONG::MaskInset::kScreenInsetPx);
	}

	SECTION("a step along an axis is worth less in the diagonal direction")
	{
		const std::uint32_t resolutions[] = { 270, 512, 1024, 2048, 4096, 8192 };
		const float viewports[] = { 2560.0f, 4096.0f };
		for (const auto texels : resolutions) {
			for (const auto viewport : viewports) {
				const auto step = HONG::MaskInset::Decide(viewport, viewport, 1.0f, 1.0f, texels, texels);
				const bool haveStep = step.has_value();
				CHECK(haveStep);
				if (!haveStep) {
					continue;
				}
				const float axial = step->x() * viewport;
				const float diagonal = axial * static_cast<float>(1.0 / std::sqrt(2.0));
				CHECK(axial >= HONG::MaskInset::kScreenInsetPx);
				CHECK(diagonal >= HONG::MaskInset::kScreenInsetPx * 0.99f);
			}
		}
	}
}

TEST_CASE("the axis factor reaches the promise on its own, without ceil", "[mask]")
{
	const float factor = HONG::MaskInset::kAxisInsetPx / HONG::MaskInset::kScreenInsetPx;
	CHECK(factor >= static_cast<float>(std::sqrt(2.0)));
	CHECK(factor <= 1.5f);

	const float achieved = HONG::MaskInset::kAxisInsetPx * static_cast<float>(1.0 / std::sqrt(2.0));
	CHECK(achieved >= HONG::MaskInset::kScreenInsetPx);

	const auto step = HONG::MaskInset::Decide(2560.0f, 1440.0f, 1.0f, 1.0f, 4096, 4096);
	REQUIRE(step.has_value());
	const float diagonalPx = step->x() * 2560.0f * static_cast<float>(1.0 / std::sqrt(2.0));
	CHECK(diagonalPx >= HONG::MaskInset::kScreenInsetPx);
}
