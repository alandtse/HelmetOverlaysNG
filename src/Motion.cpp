// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Modding-Exception
// Copyright (c) 2026 HelmetOverlaysNG contributors. See COPYING and EXCEPTIONS.md.

#include "Motion.h"

#include <algorithm>
#include <iterator>

namespace HONG::Motion
{
	namespace
	{
		// Keyframes read from the original widget's timeline (30 fps, 1280x720 stage), as
		// display fractions. Times are seconds from the start of each clip.
		constexpr float kStageWidth = 1280.0f;
		constexpr float kStageHeight = 720.0f;
		constexpr float kStrafeThreshold = 0.5f;
		constexpr float kSprintReleaseSeconds = 0.25f;

		constexpr Key kJump[] = {
			{ 0.0f, 0.0f, 5.0f / kStageHeight },
			{ 0.1f, 0.0f, 20.0f / kStageHeight },
			{ 0.233f, 0.0f, 18.2f / kStageHeight },
			{ 0.367f, 0.0f, 9.1f / kStageHeight },
			{ 0.5f, 0.0f, 0.0f },
		};
		constexpr Key kStrafeLeft[] = {
			{ 0.0f, 0.0f, 0.0f },
			{ 0.1f, -22.5f / kStageWidth, 0.0f },
			{ 0.233f, -20.0f / kStageWidth, 0.0f },
			{ 0.367f, -6.7f / kStageWidth, 0.0f },
			{ 0.5f, 0.0f, 0.0f },
		};
		constexpr Key kStrafeRight[] = {
			{ 0.0f, 0.0f, 0.0f },
			{ 0.067f, 15.0f / kStageWidth, 0.0f },
			{ 0.2f, 23.4f / kStageWidth, 0.0f },
			{ 0.333f, 10.0f / kStageWidth, 0.0f },
			{ 0.467f, 0.0f, 0.0f },
		};
		constexpr Key kSprint[] = {
			{ 0.0f, 0.0f, 0.0f },
			{ 0.033f, 3.0f / kStageWidth, -3.0f / kStageHeight },
			{ 0.167f, 15.0f / kStageWidth, -15.0f / kStageHeight },
			{ 0.3f, 3.0f / kStageWidth, -3.0f / kStageHeight },
			{ 0.433f, -9.0f / kStageWidth, -9.0f / kStageHeight },
		};
	}

	Offset Sample(const Key* a_keys, std::size_t a_count, float a_time)
	{
		if (a_count == 0) {
			return {};
		}
		if (a_time <= a_keys[0].time) {
			return { a_keys[0].x, a_keys[0].y };
		}
		for (std::size_t i = 1; i < a_count; ++i) {
			if (a_time <= a_keys[i].time) {
				const auto& from = a_keys[i - 1];
				const auto& to = a_keys[i];
				const float span = to.time - from.time;
				const float t = span > 0.0f ? (a_time - from.time) / span : 1.0f;
				return { from.x + (to.x - from.x) * t, from.y + (to.y - from.y) * t };
			}
		}
		return { a_keys[a_count - 1].x, a_keys[a_count - 1].y };
	}

	void Player::Reset()
	{
		*this = Player{};
	}

	void Player::Update(float a_dt, const State& a_state, float a_strength)
	{
		if (a_strength <= 0.0f) {
			Reset();
			return;
		}

		if (a_state.airborne && !_last.airborne) {
			_clip = Clip::kJump;
			_clipTime = 0.0f;
		}
		const bool wasLeft = _last.strafe < -kStrafeThreshold;
		const bool wasRight = _last.strafe > kStrafeThreshold;
		if (a_state.strafe < -kStrafeThreshold && !wasLeft) {
			_clip = Clip::kStrafeLeft;
			_clipTime = 0.0f;
		} else if (a_state.strafe > kStrafeThreshold && !wasRight) {
			_clip = Clip::kStrafeRight;
			_clipTime = 0.0f;
		}

		if (a_state.sprinting) {
			_sprintTime = _last.sprinting ? _sprintTime + a_dt : 0.0f;
			_sprintOffset = Sample(kSprint, std::size(kSprint), _sprintTime);
			_sprintRelease = 1.0f;
		} else if (_sprintRelease > 0.0f) {
			_sprintRelease = std::max(0.0f, _sprintRelease - a_dt / kSprintReleaseSeconds);
		}

		Offset total{ _sprintOffset.x * _sprintRelease, _sprintOffset.y * _sprintRelease };
		if (_clip != Clip::kNone) {
			_clipTime += a_dt;
			const Key* keys = kJump;
			std::size_t count = std::size(kJump);
			if (_clip == Clip::kStrafeLeft) {
				keys = kStrafeLeft;
				count = std::size(kStrafeLeft);
			} else if (_clip == Clip::kStrafeRight) {
				keys = kStrafeRight;
				count = std::size(kStrafeRight);
			}
			const float end = keys[count - 1].time;
			if (_clipTime >= end) {
				_clip = Clip::kNone;
			} else {
				const auto clip = Sample(keys, count, _clipTime);
				total.x += clip.x;
				total.y += clip.y;
			}
		}

		_offset = { total.x * a_strength, total.y * a_strength };
		_last = a_state;
	}
}
