// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Modding-Exception
// Copyright (c) 2026 HelmetOverlaysNG contributors. See COPYING and EXCEPTIONS.md.
//
// Self-contained so the unit tests can include it without the game headers.

#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <unordered_set>

namespace HONG
{
	// The voices the low-pass has written to, so each can be opened again once the effect ends.
	// The lock covers only this set and is never held across a call into XAudio2.
	class VoiceTracker
	{
	public:
		static constexpr std::size_t kMaxTracked = 2048;
		static constexpr std::uint64_t kReleaseWindowMs = 2000;

		void Remember(const void* a_voice)
		{
			std::scoped_lock lock{ _lock };
			_inactiveSince = 0;
			if (_voices.size() >= kMaxTracked) {
				_voices.clear();
			}
			_voices.insert(a_voice);
		}

		// True if a_voice had been written to.
		bool Forget(const void* a_voice)
		{
			std::scoped_lock lock{ _lock };
			return _voices.erase(a_voice) > 0;
		}

		// For use while the effect is off: true while tracked voices may still be released. After
		// kReleaseWindowMs the set is dropped, since voices that never updated again are gone.
		bool Releasing(std::uint64_t a_nowMs)
		{
			std::scoped_lock lock{ _lock };
			if (_voices.empty()) {
				return false;
			}
			if (_inactiveSince == 0) {
				_inactiveSince = a_nowMs;
			}
			if (a_nowMs - _inactiveSince > kReleaseWindowMs) {
				_voices.clear();
				_inactiveSince = 0;
				return false;
			}
			return true;
		}

	private:
		std::mutex _lock;
		std::unordered_set<const void*> _voices;
		std::uint64_t _inactiveSince{ 0 };
	};
}
