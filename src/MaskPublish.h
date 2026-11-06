// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Modding-Exception
// Copyright (c) 2026 HelmetOverlaysNG contributors. See COPYING and EXCEPTIONS.md.

#pragma once

#include "MaskShaders.h"

#include <atomic>
#include <cmath>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <type_traits>

namespace HONG::MaskPublish
{
	// A refused publish clears the mask, so the world shades normally: a missing saving, never a hole.
	struct Publishable
	{
		bool permitted{};   // OverlayAlpha::PermitMask allowed it this frame
		bool hasMask{};     // the mask texture view exists in the candidate
		bool maskSized{};   // ...and it has a non-zero texel count on both axes
		float fitX{};       // horizontal fit fraction, must be > 0
		float fitY{};       // vertical fit fraction, must be > 0
		float onScreenX{};  // on-screen width of the art, in display fractions
		float onScreenY{};  // on-screen height of the art, in display fractions
	};

	/// Finite and positive. +infinity must be refused: the shader would divide its fixed 3 px inset by it,
	/// get a zero UV step, and silently lose the erosion.
	inline bool UsableFit(float a_fraction)
	{
		return std::isfinite(a_fraction) && a_fraction > 0.0f;
	}

	/// Finite and at least kMinStampSize; an infinite extent has the same zero-UV-step failure as an infinite fit.
	inline bool UsableExtent(float a_extent)
	{
		return std::isfinite(a_extent) && a_extent >= OverlayMask::Shaders::kMinStampSize;
	}

	/// Built positionally at the call site, so an appended field would default to 0 and compile silently:
	/// the binding count (compile error on add/remove) and the member-order static_assert below pin it.
	static_assert(std::is_standard_layout_v<Publishable>, "Publishable is passed across a TU boundary; keep it layout-stable");
	static_assert(sizeof(Publishable) == 20, "Publishable layout changed; update the structured binding below and the call site");

	/// Constexpr so the static_assert below actually evaluates it.
	inline constexpr bool FieldCountAndOrderMatch(const Publishable& a_in)
	{
		const auto& [permitted, hasMask, maskSized, fitX, fitY, onScreenX, onScreenY] = a_in;
		return &permitted == &a_in.permitted && &hasMask == &a_in.hasMask && &maskSized == &a_in.maskSized && &fitX == &a_in.fitX && &fitY == &a_in.fitY && &onScreenX == &a_in.onScreenX && &onScreenY == &a_in.onScreenY;
	}

	static_assert(FieldCountAndOrderMatch(Publishable{}), "Publishable member order changed; update the binding and the call site");

	/// Why a candidate was refused, and the single authority on whether it may be stamped.
	enum class Refusal
	{
		None,
		NotPermitted,
		NoMask,
		MaskUnSized,
		BadFit,
		TooSmall,
	};

	/// The only implementation of the decision; CanPublish derives from it so the tested and running predicates cannot diverge.
	inline Refusal Why(const Publishable& a_in)
	{
		if (!a_in.permitted) {
			return Refusal::NotPermitted;
		}
		if (!a_in.hasMask) {
			return Refusal::NoMask;
		}
		if (!a_in.maskSized) {
			return Refusal::MaskUnSized;
		}
		if (!UsableFit(a_in.fitX) || !UsableFit(a_in.fitY)) {
			return Refusal::BadFit;
		}
		if (!UsableExtent(a_in.onScreenX) || !UsableExtent(a_in.onScreenY)) {
			return Refusal::TooSmall;
		}
		return Refusal::None;
	}

	/// True only when every condition holds.
	inline bool CanPublish(const Publishable& a_in)
	{
		return Why(a_in) == Refusal::None;
	}

	/// The reason as logged. BadFit (geometry) and MaskUnSized (mask never built) must read differently in the user's log.
	inline std::string_view Name(Refusal a_reason)
	{
#pragma warning(push)
#pragma warning(error: 4062)  // enumerator not handled: /W4 does not report this
		switch (a_reason) {
		case Refusal::None:
			return "stamped";
		case Refusal::NotPermitted:
			return "not permitted this frame";
		case Refusal::NoMask:
			return "no mask texture";
		case Refusal::MaskUnSized:
			return "mask texture has no size";
		case Refusal::BadFit:
			return "fit is not a usable fraction";
		case Refusal::TooSmall:
			return "art is smaller than the minimum stamp";
		}
#pragma warning(pop)
		return "unrecognised";
	}

	/// Built here so tests assert the logged text itself, not a reconstruction of it.
	inline std::string Message(Refusal a_reason, float a_fitX, float a_fitY, float a_scale)
	{
		// 17 significant digits: 4 decimals would print a fit of 0.0099999 as 0.0100 and hide that it is under kMinStampSize (0.01).
		return std::format("Overlay mask not stamped: {} - fit ({:.17g}, {:.17g}) scale {:.17g}.", Name(a_reason), a_fitX, a_fitY, a_scale);
	}

	/// Short status for the settings page. Deliberately not Name()'s strings: Name diagnoses for the log, Summary reports status to the user.
	inline std::string_view Summary(Refusal a_reason)
	{
#pragma warning(push)
#pragma warning(error: 4062)  // enumerator not handled: a new Refusal must not land on the "unknown" tail
		switch (a_reason) {
		case Refusal::None:
			return "stamped";
		case Refusal::NotPermitted:
			return "idle";
		case Refusal::NoMask:
			return "no mask built yet";
		case Refusal::MaskUnSized:
			return "mask has no size";
		case Refusal::BadFit:
			return "the art fits nowhere";
		case Refusal::TooSmall:
			return "the art covers no pixels";
		}
#pragma warning(pop)
		return "unknown";
	}

	/// Most recent refusal, for the settings page. Single writer (render thread); a stale read is harmless.
	inline std::atomic<Refusal>& LastRefusal()
	{
		static std::atomic<Refusal> value{ Refusal::NotPermitted };
		return value;
	}

	inline void RecordRefusal(Refusal a_reason)
	{
		LastRefusal().store(a_reason, std::memory_order_relaxed);
	}

	inline Refusal CurrentRefusal()
	{
		return LastRefusal().load(std::memory_order_relaxed);
	}

	/// How long a stamp stays valid: a frame where the HUD menu does not display publishes nothing, so without
	/// an expiry the last stamp keeps cutting the world out.
	inline constexpr std::uint64_t kMaxStampAgeMs = 100;

	/// False for a stamp never published, and for one older than kMaxStampAgeMs. An unsigned clock that went
	/// backwards wraps to a huge age, which also reads as stale: the safe direction.
	inline bool StampFresh(std::uint64_t a_publishedMs, std::uint64_t a_nowMs)
	{
		return a_publishedMs != 0 && a_nowMs - a_publishedMs <= kMaxStampAgeMs;
	}

	/// Refusals worth logging; the idle ones (NotPermitted, NoMask) would log every frame. No default case:
	/// MSVC /W4 omits C4062, so the pragma below makes a new Refusal a compile error. The order of checks in
	/// Why() decides which reason is named when several hold.
	inline bool WorthLogging(Refusal a_reason)
	{
#pragma warning(push)
#pragma warning(error: 4062)  // enumerator not handled: /W4 does not report this
		switch (a_reason) {
		case Refusal::BadFit:
		case Refusal::MaskUnSized:
			return true;
		case Refusal::None:
		case Refusal::NotPermitted:
		case Refusal::NoMask:
		case Refusal::TooSmall:
			return false;
		}
#pragma warning(pop)
		return false;
	}
}
