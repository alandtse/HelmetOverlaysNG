// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Modding-Exception
// Copyright (c) 2026 HelmetOverlaysNG contributors. See COPYING and EXCEPTIONS.md.

#pragma once

#include <cstddef>

namespace HONG::Motion
{
	struct Offset
	{
		float x{ 0.0f };
		float y{ 0.0f };
	};

	// What the player is doing this frame. strafe is the lateral move input, -1 left to 1 right.
	struct State
	{
		bool airborne{ false };
		bool sprinting{ false };
		float strafe{ 0.0f };
	};

	// Jump, strafe and sprint nudges of the overlay, as in the original mod's widget: short
	// translation clips that start when the action does. Offsets are fractions of the display.
	class Player
	{
	public:
		// a_strength scales every offset; 0 stops all clips.
		void Update(float a_dt, const State& a_state, float a_strength);
		[[nodiscard]] Offset Current() const { return _offset; }
		void Reset();

	private:
		enum class Clip
		{
			kNone,
			kJump,
			kStrafeLeft,
			kStrafeRight
		};

		Offset _offset;
		State _last;
		Clip _clip{ Clip::kNone };
		float _clipTime{ 0.0f };
		float _sprintTime{ 0.0f };
		Offset _sprintOffset;
		float _sprintRelease{ 0.0f };
	};

	// Piecewise-linear value at a_time through (time, x, y) keys; holds the last key afterwards.
	struct Key
	{
		float time;
		float x;
		float y;
	};
	Offset Sample(const Key* a_keys, std::size_t a_count, float a_time);
}
