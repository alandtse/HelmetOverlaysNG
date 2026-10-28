// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Modding-Exception
// Copyright (c) 2026 HelmetOverlaysNG contributors. See COPYING and EXCEPTIONS.md.

#include "OverlayController.h"

#include "Debug.h"
#include "HelmetProfile.h"
#include "LightLevel.h"
#include "OverlayResolver.h"
#include "ScreenColor.h"
#include "Settings.h"

namespace HONG
{
	namespace
	{
		constexpr float kRefreshIntervalSeconds = 1.0f;  // safety net if an equip event is missed
		constexpr float kTintFadeSeconds = 0.3f;         // time for the screen color tint to follow the scene
		constexpr float kLightLogStep = 0.05f;           // log the light level when it moves this much
		constexpr float kSettleScale = 0.06f;            // extra scale at progress 0 for kSettle
		constexpr float kSlideDistance = 0.12f;          // fraction of display height for kSlide
		constexpr float kSwayGain = 0.9f;                // display fraction per radian of turn
		constexpr float kSwayDecayPerSecond = 7.0f;
		constexpr float kSwayLimit = 0.025f;  // fraction; the art has bleed margin for this

		Profile::ArmorClass ClassOf(const RE::TESObjectARMO* a_armor)
		{
			using Type = RE::BGSBipedObjectForm::ArmorType;
			switch (a_armor->GetArmorType()) {
			case Type::kHeavyArmor:
				return Profile::ArmorClass::kHeavy;
			case Type::kLightArmor:
				return Profile::ArmorClass::kLight;
			case Type::kClothing:
				return Profile::ArmorClass::kClothing;
			default:
				return Profile::ArmorClass::kUnknown;
			}
		}

		constexpr float kMaskMinOpacity = 0.999f;
		constexpr float kMaskMaxAccel = 0.002f;  // display fraction per frame of change in speed, below the 8 px default margin

		float SmoothStep(float a_t)
		{
			a_t = std::clamp(a_t, 0.0f, 1.0f);
			return a_t * a_t * (3.0f - 2.0f * a_t);
		}

		float WrapAngle(float a_rad)
		{
			constexpr float kTwoPi = 6.28318530718f;
			while (a_rad > kTwoPi * 0.5f) {
				a_rad -= kTwoPi;
			}
			while (a_rad < -kTwoPi * 0.5f) {
				a_rad += kTwoPi;
			}
			return a_rad;
		}
	}

	OverlayController* OverlayController::GetSingleton()
	{
		static OverlayController singleton;
		return std::addressof(singleton);
	}

	void OverlayController::SetPreview(RE::FormID a_armor)
	{
		_previewId.store(a_armor, std::memory_order_relaxed);
		{
			std::scoped_lock lock{ _diagLock };
			_previewFile.clear();
		}
		MarkDirty();
	}

	void OverlayController::SetPreviewFile(std::string a_path)
	{
		{
			std::scoped_lock lock{ _diagLock };
			_previewFile = std::move(a_path);
		}
		MarkDirty();
	}

	std::string OverlayController::GetPreviewFile() const
	{
		std::scoped_lock lock{ _diagLock };
		return _previewFile;
	}

	OverlayController::Diagnostic OverlayController::GetDiagnostic() const
	{
		std::scoped_lock lock{ _diagLock };
		return _diag;
	}

	void OverlayController::Register()
	{
		if (const auto holder = RE::ScriptEventSourceHolder::GetSingleton()) {
			holder->AddEventSink<RE::TESEquipEvent>(this);
		}
	}

	RE::BSEventNotifyControl OverlayController::ProcessEvent(const RE::TESEquipEvent* a_event, RE::BSTEventSource<RE::TESEquipEvent>*)
	{
		if (a_event && a_event->actor && a_event->actor->IsPlayerRef()) {
			MarkDirty();
		}
		return RE::BSEventNotifyControl::kContinue;
	}

	bool OverlayController::Gated() const
	{
		const auto settings = Settings::GetSingleton();
		if (!settings->enabled) {
			return false;
		}
		const auto player = RE::PlayerCharacter::GetSingleton();
		if (!player || !player->Is3DLoaded() || player->IsDead()) {
			return false;
		}
		if (settings->firstPersonOnly && !REL::Module::IsVR()) {
			const auto camera = RE::PlayerCamera::GetSingleton();
			if (!camera || !camera->IsInFirstPerson()) {
				return false;
			}
		}
		if (settings->hideInMenus) {
			const auto ui = RE::UI::GetSingleton();
			if (!ui) {
				return false;
			}
			if (settings->AnyHiddenMenu([ui](const std::string& a_menu) { return ui->IsMenuOpen(a_menu); })) {
				return false;
			}
		}
		return true;
	}

	void OverlayController::Refresh()
	{
		_timeSinceRefresh = 0.0f;
		_dirty.store(false, std::memory_order_relaxed);

		const auto player = RE::PlayerCharacter::GetSingleton();
		const auto previewId = _previewId.load(std::memory_order_relaxed);
		const auto previewFile = GetPreviewFile();
		const RE::TESObjectARMO* armor = nullptr;
		if (previewFile.empty()) {
			armor = previewId ? RE::TESForm::LookupByID<RE::TESObjectARMO>(previewId) : Resolver::FindWornHeadgear(player);
		}
		if (armor == _currentArmor && previewFile == _currentFile) {
			return;
		}
		_currentArmor = armor;
		_currentFile = previewFile;

		for (auto& layer : _layers) {
			layer.wanted = false;
		}

		Resolver::Resolution resolution;
		if (!previewFile.empty()) {
			resolution.path = previewFile;
			std::scoped_lock lock{ _diagLock };
			_diag.armorId = 0;
			_diag.armorName.clear();
			_diag.previewing = true;
			_diag.resolution = resolution;
		} else {
			if (!armor) {
				return;
			}
			const auto base = player->GetActorBase();
			const bool female = base && base->IsFemale();
			resolution = Resolver::Resolve(armor, female);
			{
				std::scoped_lock lock{ _diagLock };
				_diag.armorId = armor->GetFormID();
				_diag.armorName = armor->GetName();
				_diag.previewing = previewId != 0;
				_diag.resolution = resolution;
			}
			if (!resolution.path) {
				std::string tried;
				for (const auto& candidate : resolution.tried) {
					tried += tried.empty() ? candidate : ", " + candidate;
				}
				logger::info("No overlay art for '{}' ({:08X}); model '{}'; tried: {}", armor->GetName(), armor->GetFormID(), resolution.model, tried.empty() ? "(no world model)" : tried);
				return;
			}
		}
		const auto& path = resolution.path;
		auto srv = TextureCache::Get(*path);
		if (!srv) {
			return;
		}

		const auto settings = Settings::GetSingleton();
		const float openness = TextureCache::Openness(*path);
		const auto profile = Profile::Compute(openness, armor ? ClassOf(armor) : Profile::ArmorClass::kUnknown, settings->autoArtWeight);
		_muffleStrength = settings->autoStrength ? profile.strength : 1.0f;
		{
			std::scoped_lock lock{ _diagLock };
			_diag.openness = openness;
			_diag.color = TextureCache::ColoredShare(*path);
			_diag.strength = _muffleStrength;
		}
		logger::info("Overlay '{}' for '{}' (see-through {:.0f}%, muffle strength {:.2f})", *path, armor ? armor->GetName() : "(file preview)", openness * 100.0f, _muffleStrength);
		auto mask = TextureCache::MaskFor(*path);
		{
			std::scoped_lock lock{ _diagLock };
			_diag.maskShare = mask.share;
		}
		_layers.push_back({ std::move(srv), armor, 0.0f, true, std::move(mask) });
	}

	void OverlayController::UpdateSway(float a_dt)
	{
		const auto settings = Settings::GetSingleton();
		const auto player = RE::PlayerCharacter::GetSingleton();
		if (!settings->sway || REL::Module::IsVR() || !player) {
			_sway = { 0, 0 };
			_haveAngles = false;
			return;
		}

		const float pitch = player->data.angle.x;
		const float yaw = player->data.angle.z;
		if (_haveAngles) {
			const float dYaw = WrapAngle(yaw - _lastYaw);
			const float dPitch = WrapAngle(pitch - _lastPitch);
			_sway.x -= dYaw * kSwayGain * settings->swayStrength;
			_sway.y += dPitch * kSwayGain * settings->swayStrength;
		}
		_lastYaw = yaw;
		_lastPitch = pitch;
		_haveAngles = true;

		const float decay = std::exp(-kSwayDecayPerSecond * a_dt);
		_sway.x = std::clamp(_sway.x * decay, -kSwayLimit, kSwayLimit);
		_sway.y = std::clamp(_sway.y * decay, -kSwayLimit, kSwayLimit);
	}

	void OverlayController::UpdateMotion(float a_dt)
	{
		const auto settings = Settings::GetSingleton();
		const auto player = RE::PlayerCharacter::GetSingleton();
		const auto controls = RE::PlayerControls::GetSingleton();
		if (!settings->motion || REL::Module::IsVR() || !player || !controls) {
			_motion.Reset();
			return;
		}
		Motion::State state;
		state.airborne = player->IsInMidair();
		state.sprinting = player->AsActorState()->IsSprinting();
		state.strafe = controls->data.moveInputVec.x;
		_motion.Update(a_dt, state, settings->motionStrength);
	}

	void OverlayController::UpdateLight(float a_dt)
	{
		const auto settings = Settings::GetSingleton();
		if (!settings->reactiveLight) {
			_lightDarkness = 0.0f;
			_lightTarget = 0.0f;
			_lightClock = settings->lightIntervalSeconds;
			std::scoped_lock lock{ _diagLock };
			_diag.light = -1.0f;
			return;
		}
		_lightClock += a_dt;
		if (_lightClock >= settings->lightIntervalSeconds) {
			_lightClock = 0.0f;
			if (const float level = LightLevel::SamplePlayer(); level >= 0.0f) {
				_lightTarget = 1.0f - level;
				std::scoped_lock lock{ _diagLock };
				_diag.light = level;
				if (std::abs(level - _lightLogged) >= kLightLogStep) {
					_lightLogged = level;
					logger::info("Light level {:.0f}% (overlay tint {:.0f}%)", level * 100.0f, (1.0f - settings->lightStrength * (1.0f - level)) * 100.0f);
				}
			}
		}
		if (const auto& forced = Debug::LightLevelOverride(); forced.Active()) {
			_lightTarget = 1.0f - forced.Value();
		}
		const float step = settings->lightFadeSeconds > 0.0f ? a_dt / settings->lightFadeSeconds : 1.0f;
		_lightDarkness += std::clamp(_lightTarget - _lightDarkness, -step, step);
	}

	bool OverlayController::WantsScreenColor() const
	{
		const auto settings = Settings::GetSingleton();
		return settings->enabled && _wornAmount > 0.0f && (settings->screenTint || Debug::TintStrengthOverride().Active());
	}

	void OverlayController::UpdateTint(float a_dt)
	{
		const auto settings = Settings::GetSingleton();
		const float strength = Debug::TintStrengthOverride().Apply(settings->screenTint ? settings->screenTintStrength : 0.0f);
		Tint::Rgb scene;
		const bool sampled = WantsScreenColor() && ScreenColor::Latest(scene);
		const auto target = sampled ? Tint::Chroma(scene, strength) : Tint::Rgb{};
		_tint = Tint::Approach(_tint, target, a_dt / kTintFadeSeconds);

		std::scoped_lock lock{ _diagLock };
		_diag.sceneSampled = sampled;
		_diag.scene = scene;
		_diag.tint = _tint;
		_diag.lightTint = 1.0f - settings->lightStrength * _lightDarkness;
	}

	void OverlayController::Update(float a_dt, std::vector<DrawLayer>& a_out)
	{
		a_out.clear();
		const auto settings = Settings::GetSingleton();

		_timeSinceRefresh += a_dt;
		if (_dirty.load(std::memory_order_relaxed) || _timeSinceRefresh >= kRefreshIntervalSeconds) {
			Refresh();
		}

		const bool gated = Gated();
		UpdateSway(a_dt);
		UpdateLight(a_dt);
		UpdateTint(a_dt);
		UpdateMotion(a_dt);

		float worn = 0.0f;
		for (auto& layer : _layers) {
			const bool show = layer.wanted && gated;
			const float seconds = show ? settings->fadeInSeconds : settings->fadeOutSeconds;
			const float step = seconds > 0.0f ? a_dt / seconds : 1.0f;
			layer.progress = std::clamp(layer.progress + (show ? step : -step), 0.0f, 1.0f);
			if (layer.wanted) {
				worn = std::max(worn, layer.progress);
			}
		}
		std::erase_if(_layers, [](const Layer& a_layer) { return !a_layer.wanted && a_layer.progress <= 0.0f; });
		_wornAmount = worn;

		for (const auto& layer : _layers) {
			if (layer.progress <= 0.0f) {
				continue;
			}
			const float eased = SmoothStep(layer.progress);
			DrawLayer out;
			out.srv = layer.srv.Get();
			out.alpha = eased * settings->opacity;
			out.tint = 1.0f - settings->lightStrength * _lightDarkness;
			out.color = _tint;
			const auto motion = _motion.Current();
			out.offset = { _sway.x + motion.x, _sway.y + motion.y + settings->verticalOffset };
			out.scale = settings->scale;
			switch (settings->animStyle) {
			case AnimStyle::kSettle:
				out.scale = settings->scale * (1.0f + (1.0f - eased) * kSettleScale);
				break;
			case AnimStyle::kSlide:
				out.offset.y -= (1.0f - eased) * kSlideDistance;
				break;
			default:
				break;
			}
			a_out.push_back(out);
		}

		_mask = {};
		const bool vrPlacementMoved = REL::Module::IsVR() && (settings->scale != 1.0f || settings->verticalOffset != 0.0f);
		if (settings->opaqueMask && !vrPlacementMoved && gated && settings->opacity >= kMaskMinOpacity && _layers.size() == 1 && a_out.size() == 1) {
			const auto& layer = _layers.front();
			const auto& drawn = a_out.front();
			// The mask is used by the next frame's world render, so it follows the overlay one frame ahead.
			const ImVec2 velocity{ drawn.offset.x - _lastMaskOffset.x, drawn.offset.y - _lastMaskOffset.y };
			const bool smooth = std::abs(velocity.x - _lastMaskVelocity.x) <= kMaskMaxAccel && std::abs(velocity.y - _lastMaskVelocity.y) <= kMaskMaxAccel;
			if (layer.wanted && layer.progress >= 1.0f && layer.mask.srv && smooth) {
				_mask = { layer.mask, { drawn.offset.x + velocity.x, drawn.offset.y + velocity.y }, drawn.scale, true };
			}
			_lastMaskVelocity = velocity;
		} else {
			_lastMaskVelocity = { 0, 0 };
		}
		_lastMaskOffset = a_out.empty() ? ImVec2{ 0, 0 } : a_out.front().offset;
	}
}
