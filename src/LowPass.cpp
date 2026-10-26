// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Modding-Exception
// Copyright (c) 2026 HelmetOverlaysNG contributors. See COPYING and EXCEPTIONS.md.

#include "LowPass.h"

#include "SoundEffect.h"

#include <mutex>

namespace HONG::LowPass
{
	namespace
	{
		std::atomic<bool> g_enabled{ false };
		std::atomic<float> g_amount{ 0.0f };
		std::atomic<float> g_cutoffHz{ 1800.0f };

		// Voice -> filter frequency we last wrote. A voice that still reports that
		// value was not rewritten by the engine, so its engine base is fully open.
		std::mutex g_lock;
		std::unordered_map<const void*, float> g_applied;
		std::uint64_t g_inactiveSince = 0;

		constexpr float kPi = 3.14159265358979f;
		constexpr float kMaxFilterFrequency = 1.0f;  // XAUDIO2_MAX_FILTER_FREQUENCY, a cutoff of SampleRate / 6
		constexpr float kSameEpsilon = 1e-4f;
		constexpr float kMinSampleRate = 20000.0f;
		constexpr float kMaxSampleRate = 48000.0f;
		constexpr std::size_t kMaxTracked = 2048;
		constexpr std::uint64_t kReleaseWindowMs = 2000;

		float SampleRate(const RE::BSXAudio2Audio* a_impl)
		{
			const auto rate = static_cast<float>(a_impl->outputFormat.Format.nSamplesPerSec);
			return std::clamp(rate, kMinSampleRate, kMaxSampleRate);
		}

		float Coefficient(float a_cutoffHz, float a_sampleRate)
		{
			return std::clamp(2.0f * std::sin(kPi * a_cutoffHz / a_sampleRate), 0.0f, kMaxFilterFrequency);
		}

		void Apply(RE::BSXAudio2GameSound* a_sound)
		{
			const bool active = g_enabled.load(std::memory_order_relaxed) && g_amount.load(std::memory_order_relaxed) > 0.0f;

			std::scoped_lock lock{ g_lock };

			if (active) {
				g_inactiveSince = 0;
			} else {
				if (g_applied.empty()) {
					return;
				}
				const auto now = GetTickCount64();
				if (g_inactiveSince == 0) {
					g_inactiveSince = now;
				}
				if (now - g_inactiveSince > kReleaseWindowMs) {
					g_applied.clear();
					g_inactiveSince = 0;
					return;
				}
			}

			if (a_sound->IsMusic() || !a_sound->sourceVoice) {
				return;
			}
			const auto impl = RE::BSXAudio2GameSound::GetAudioImplementation();
			if (!impl || !impl->masteringVoice) {
				return;
			}

			auto* voice = reinterpret_cast<RE::IXAudio2Voice*>(a_sound->sourceVoice);
			auto* master = reinterpret_cast<RE::IXAudio2Voice*>(impl->masteringVoice);

			RE::XAUDIO2_FILTER_PARAMETERS current{};
			voice->GetOutputFilterParameters(master, &current);

			const auto tracked = g_applied.find(voice);
			const bool ours = tracked != g_applied.end() && std::fabs(current.Frequency - tracked->second) < kSameEpsilon;
			const float engineBase = (!ours && current.Type == RE::XAUDIO2_FILTER_TYPE::LowPassFilter) ? current.Frequency : kMaxFilterFrequency;

			float target = engineBase;
			if (active) {
				const float sampleRate = SampleRate(impl);
				const float hz = SoundEffect::CutoffHz(g_amount.load(std::memory_order_relaxed), g_cutoffHz.load(std::memory_order_relaxed), sampleRate);
				target = std::min(engineBase, Coefficient(hz, sampleRate));
			} else if (tracked == g_applied.end()) {
				return;
			}

			if (current.Type != RE::XAUDIO2_FILTER_TYPE::LowPassFilter || std::fabs(current.Frequency - target) > kSameEpsilon) {
				RE::XAUDIO2_FILTER_PARAMETERS params{};
				params.Type = RE::XAUDIO2_FILTER_TYPE::LowPassFilter;
				params.Frequency = target;
				params.OneOverQ = current.OneOverQ > 0.0f ? current.OneOverQ : 1.0f;
				const auto hr = voice->SetOutputFilterParameters(master, &params);
				if (hr < 0) {
					static std::atomic<bool> warned{ false };
					if (!warned.exchange(true)) {
						logger::warn("SetOutputFilterParameters failed (hr {:#x}); low-pass may be inactive on some voices.", static_cast<std::uint32_t>(hr));
					}
				}
			}

			if (active) {
				if (g_applied.size() >= kMaxTracked) {
					g_applied.clear();
				}
				g_applied[voice] = target;
			} else {
				g_applied.erase(voice);
			}
		}

		struct UpdateEmitterPosition
		{
			static void thunk(RE::BSXAudio2GameSound* a_this)
			{
				func(a_this);
				Apply(a_this);
			}
			static inline REL::Relocation<decltype(thunk)> func;
			static inline std::size_t idx{ 0x0D };
		};

		struct Update
		{
			static bool thunk(RE::BSXAudio2GameSound* a_this)
			{
				const bool result = func(a_this);
				Apply(a_this);
				return result;
			}
			static inline REL::Relocation<decltype(thunk)> func;
			static inline std::size_t idx{ 0x0E };
		};
	}

	void Install()
	{
		stl::write_vfunc<RE::BSXAudio2GameSound, UpdateEmitterPosition>();
		stl::write_vfunc<RE::BSXAudio2GameSound, Update>();
		logger::info("Low-pass audio hooks installed.");
	}

	float OutputSampleRate()
	{
		const auto impl = RE::BSXAudio2GameSound::GetAudioImplementation();
		return impl ? SampleRate(impl) : kMaxSampleRate;
	}

	void SetTarget(bool a_enabled, float a_amount, float a_cutoffHz)
	{
		const bool active = a_enabled && a_amount > 0.0f;
		if (active != (g_enabled.load(std::memory_order_relaxed) && g_amount.load(std::memory_order_relaxed) > 0.0f)) {
			logger::info("Low-pass {} (target cutoff {:.0f} Hz).", active ? "on" : "off", a_cutoffHz);
		}
		g_enabled.store(a_enabled, std::memory_order_relaxed);
		g_amount.store(a_amount, std::memory_order_relaxed);
		g_cutoffHz.store(a_cutoffHz, std::memory_order_relaxed);
	}
}
