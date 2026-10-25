// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Modding-Exception
// Copyright (c) 2026 HelmetOverlaysNG contributors. See COPYING and EXCEPTIONS.md.

#pragma once

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <RE/Skyrim.h>
#include <REX/REX.h>
#include <SKSE/SKSE.h>

#include <Windows.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <d3d11.h>
#include <filesystem>
#include <format>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include <wrl/client.h>

#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>

namespace logger = SKSE::log;
using namespace std::literals;
using Microsoft::WRL::ComPtr;

// Call-site offset within a RELOCATION_ID'd function; VR shares the SE offset.
#define OFFSET(se, ae) REL::VariantOffset(se, ae, se)

#define HONG_STR_IMPL(x) #x
#define HONG_STR(x) HONG_STR_IMPL(x)
#define HONG_VERSION_STRING      \
	HONG_STR(HONG_VERSION_MAJOR) \
	"." HONG_STR(HONG_VERSION_MINOR) "." HONG_STR(HONG_VERSION_PATCH)

namespace stl
{
	using namespace SKSE::stl;

	// Trampoline call-site thunk: struct with `static ... thunk(...)` and
	// `static inline REL::Relocation<decltype(thunk)> func`.
	template <class T, std::size_t Size = 5>
	void write_thunk_call(std::uintptr_t a_src)
	{
		auto& trampoline = SKSE::GetTrampoline();
		T::func = trampoline.write_call<Size>(a_src, T::thunk);
	}

	// Vfunc override using the thunk's compile-time `idx` on `To`'s 0th vtable.
	template <class To, class T>
	void write_vfunc()
	{
		REL::Relocation<std::uintptr_t> vtbl{ To::VTABLE[0] };
		T::func = vtbl.write_vfunc(T::idx, T::thunk);
	}
}
