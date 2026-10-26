// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Modding-Exception
// Copyright (c) 2026 HelmetOverlaysNG contributors. See COPYING and EXCEPTIONS.md.

#pragma once

#include <atomic>

namespace HONG::Debug
{
	// A value the debug page can force in place of the one the game would compute, so an effect
	// can be pushed to an extreme to hear or see it. Not saved; cleared on every launch.
	class Override
	{
	public:
		[[nodiscard]] bool Active() const { return _active.load(std::memory_order_relaxed); }
		[[nodiscard]] float Value() const { return _value.load(std::memory_order_relaxed); }
		void Set(bool a_active, float a_value)
		{
			_value.store(a_value, std::memory_order_relaxed);
			_active.store(a_active, std::memory_order_relaxed);
		}
		// The forced value while active, otherwise a_natural.
		[[nodiscard]] float Apply(float a_natural) const { return Active() ? Value() : a_natural; }

	private:
		std::atomic<bool> _active{ false };
		std::atomic<float> _value{ 0.0f };
	};

	// 0..1 scale on the sound effects for the worn helmet.
	Override& SoundStrengthOverride();
	// 0..1 light level (0 dark, 1 full light) fed to the reactive light.
	Override& LightLevelOverride();
}
