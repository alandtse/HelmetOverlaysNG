// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Modding-Exception
// Copyright (c) 2026 HelmetOverlaysNG contributors. See COPYING and EXCEPTIONS.md.

#include "Settings.h"

#include <SimpleIni.h>
#include <algorithm>
#include <format>
#include <string>

#include <SKSE/SKSE.h>

namespace HONG
{
	namespace
	{
		namespace logger = SKSE::log;

		constexpr auto kIniPath = "Data\\SKSE\\Plugins\\HelmetOverlaysNG.ini";

		const std::vector<std::string> kDefaultHiddenMenus{
			"Journal Menu", "MapMenu", "StatsMenu", "LevelUp Menu", "Lockpicking Menu", "Book Menu",
			"Sleep/Wait Menu", "Training Menu", "Crafting Menu", "RaceSex Menu", "Loading Menu", "Main Menu",
			"Console", "MessageBoxMenu", "Credits Menu", "Creation Club Menu", "Mod Manager Menu", "TitleSequence Menu"
		};

		float GetFloat(const CSimpleIniA& a_ini, const char* a_section, const char* a_key, float a_default, float a_min, float a_max)
		{
			const auto value = static_cast<float>(a_ini.GetDoubleValue(a_section, a_key, a_default));
			return std::clamp(value, a_min, a_max);
		}

		const char* StyleName(AnimStyle a_style)
		{
			switch (a_style) {
			case AnimStyle::kFade:
				return "Fade";
			case AnimStyle::kSlide:
				return "Slide";
			default:
				return "Settle";
			}
		}

		AnimStyle ParseStyle(const std::string& a_value, AnimStyle a_default)
		{
			if (_stricmp(a_value.c_str(), "Fade") == 0) {
				return AnimStyle::kFade;
			}
			if (_stricmp(a_value.c_str(), "Settle") == 0) {
				return AnimStyle::kSettle;
			}
			if (_stricmp(a_value.c_str(), "Slide") == 0) {
				return AnimStyle::kSlide;
			}
			return a_default;
		}

		// A root outside Data (traversal, drive letter, rooted path) falls back to the default.
		std::string SafeRoot(const std::string& a_value, const std::string& a_fallback)
		{
			const bool unsafe = a_value.empty() || a_value.find("..") != std::string::npos || a_value.find(':') != std::string::npos || a_value.front() == '\\' || a_value.front() == '/';
			return unsafe ? a_fallback : a_value;
		}

		std::vector<std::string> SplitList(const std::string& a_text)
		{
			std::vector<std::string> out;
			std::size_t start = 0;
			while (start <= a_text.size()) {
				auto end = a_text.find(';', start);
				if (end == std::string::npos) {
					end = a_text.size();
				}
				auto item = a_text.substr(start, end - start);
				const auto first = item.find_first_not_of(" \t");
				const auto last = item.find_last_not_of(" \t");
				if (first != std::string::npos) {
					out.push_back(item.substr(first, last - first + 1));
				}
				start = end + 1;
			}
			return out;
		}
	}

	Settings* Settings::GetSingleton()
	{
		static Settings singleton;
		static std::once_flag once;
		std::call_once(once, [] { singleton._hiddenMenus = kDefaultHiddenMenus; });
		return std::addressof(singleton);
	}

	bool Settings::IsMenuHidden(std::string_view a_name) const
	{
		std::scoped_lock lock{ _menuLock };
		return std::ranges::any_of(_hiddenMenus, [&](const std::string& a_menu) { return a_menu == a_name; });
	}

	void Settings::SetMenuHidden(std::string_view a_name, bool a_hidden)
	{
		std::scoped_lock lock{ _menuLock };
		const auto it = std::ranges::find_if(_hiddenMenus, [&](const std::string& a_menu) { return a_menu == a_name; });
		if (a_hidden && it == _hiddenMenus.end()) {
			_hiddenMenus.emplace_back(a_name);
		} else if (!a_hidden && it != _hiddenMenus.end()) {
			_hiddenMenus.erase(it);
		}
	}

	std::vector<std::string> Settings::HiddenMenus() const
	{
		std::scoped_lock lock{ _menuLock };
		return _hiddenMenus;
	}

	bool Settings::AnyHiddenMenu(const std::function<bool(const std::string&)>& a_pred) const
	{
		std::scoped_lock lock{ _menuLock };
		return std::ranges::any_of(_hiddenMenus, a_pred);
	}

	void Settings::ResetMenusToDefaults()
	{
		std::scoped_lock lock{ _menuLock };
		_hiddenMenus = kDefaultHiddenMenus;
	}

	void Settings::ResetToDefaults()
	{
		const bool debug = debugTools;
		auto menus = kDefaultHiddenMenus;
		{
			std::scoped_lock lock{ _menuLock };
			_hiddenMenus = std::move(menus);
		}
		enabled = true;
		overlayRoot = "Interface\\overlays";
		firstPersonOnly = true;
		opaqueMask = false;
		maskMarginPx = 8;
		hideInMenus = true;
		suppressOriginal = true;
		skipWigs = true;
		skipCirclets = true;
		debugTools = debug;  // a view preference, not a tuning value
		opacity = 1.0f;
		scale = 1.0f;
		verticalOffset = 0.0f;
		extendEdges = true;
		reactiveLight = true;
		lightStrength = 0.25f;
		lightFadeSeconds = 1.0f;
		lightIntervalSeconds = 1.0f;
		animStyle = AnimStyle::kSettle;
		fadeInSeconds = 0.35f;
		fadeOutSeconds = 0.20f;
		sway = false;
		swayStrength = 1.0f;
		motion = true;
		motionStrength = 1.0f;
		vrDepth = 0.6f;
		vrCoverage = 1.15f;
		attenuate = true;
		attenuateVolume = 0.6f;
		attenuateSeconds = 0.5f;
		autoStrength = true;
		autoArtWeight = 0.65f;
		lowPass = false;
		lowPassHz = 1800.0f;
	}

	void Settings::Load()
	{
		CSimpleIniA ini;
		ini.SetUnicode();
		if (ini.LoadFile(kIniPath) < 0) {
			logger::info("No {} found; using defaults.", kIniPath);
			return;
		}

		enabled = ini.GetBoolValue("General", "bEnabled", enabled);
		overlayRoot = SafeRoot(ini.GetValue("General", "sOverlayRoot", overlayRoot.c_str()), overlayRoot);
		firstPersonOnly = ini.GetBoolValue("General", "bFirstPersonOnly", firstPersonOnly);
		opaqueMask = ini.GetBoolValue("General", "bOpaqueMask", opaqueMask);
		maskMarginPx = static_cast<int>(GetFloat(ini, "General", "iMaskMarginPx", static_cast<float>(maskMarginPx), 4.0f, 32.0f));
		hideInMenus = ini.GetBoolValue("General", "bHideInMenus", hideInMenus);
		suppressOriginal = ini.GetBoolValue("General", "bSuppressOriginal", suppressOriginal);
		skipWigs = ini.GetBoolValue("General", "bSkipWigs", skipWigs);
		skipCirclets = ini.GetBoolValue("General", "bSkipCirclets", skipCirclets);
		debugTools = ini.GetBoolValue("General", "bDebugTools", debugTools);

		opacity = GetFloat(ini, "Appearance", "fOpacity", opacity, 0.0f, 1.0f);
		scale = GetFloat(ini, "Appearance", "fScale", scale, 0.25f, 4.0f);
		verticalOffset = GetFloat(ini, "Appearance", "fVerticalOffset", verticalOffset, -0.5f, 0.5f);
		extendEdges = ini.GetBoolValue("Appearance", "bExtendEdges", extendEdges);

		reactiveLight = ini.GetBoolValue("Light", "bReactive", reactiveLight);
		lightStrength = GetFloat(ini, "Light", "fStrength", lightStrength, 0.0f, 1.0f);
		lightFadeSeconds = GetFloat(ini, "Light", "fFadeSeconds", lightFadeSeconds, 0.0f, 10.0f);
		lightIntervalSeconds = GetFloat(ini, "Light", "fIntervalSeconds", lightIntervalSeconds, 0.1f, 5.0f);

		animStyle = ParseStyle(ini.GetValue("Animation", "sStyle", ""), animStyle);
		fadeInSeconds = GetFloat(ini, "Animation", "fFadeInSeconds", fadeInSeconds, 0.0f, 5.0f);
		fadeOutSeconds = GetFloat(ini, "Animation", "fFadeOutSeconds", fadeOutSeconds, 0.0f, 5.0f);
		sway = ini.GetBoolValue("Animation", "bSway", sway);
		swayStrength = GetFloat(ini, "Animation", "fSwayStrength", swayStrength, 0.0f, 4.0f);
		motion = ini.GetBoolValue("Animation", "bMotion", motion);
		motionStrength = GetFloat(ini, "Animation", "fMotionStrength", motionStrength, 0.0f, 2.0f);

		vrDepth = GetFloat(ini, "VR", "fDepthMeters", vrDepth, 0.2f, 5.0f);
		vrCoverage = GetFloat(ini, "VR", "fCoverage", vrCoverage, 0.2f, 1.6f);

		attenuate = ini.GetBoolValue("Sound", "bAttenuate", attenuate);
		attenuateVolume = GetFloat(ini, "Sound", "fAttenuateVolume", attenuateVolume, 0.0f, 1.0f);
		attenuateSeconds = GetFloat(ini, "Sound", "fAttenuateSeconds", attenuateSeconds, 0.0f, 5.0f);
		autoStrength = ini.GetBoolValue("Sound", "bAutoStrength", autoStrength);
		autoArtWeight = GetFloat(ini, "Sound", "fAutoArtWeight", autoArtWeight, 0.0f, 1.0f);
		lowPass = ini.GetBoolValue("Sound", "bLowPass", lowPass);
		lowPassHz = GetFloat(ini, "Sound", "fLowPassHz", lowPassHz, 200.0f, 20000.0f);

		if (const auto* menus = ini.GetValue("Menus", "sHidden", nullptr)) {
			std::scoped_lock lock{ _menuLock };
			_hiddenMenus = SplitList(menus);
		}

		logger::info("Settings loaded (opacity {:.2f}, scale {:.2f}, attenuate {}, lowpass {}).", opacity, scale, attenuate, lowPass);
	}

	void Settings::Save() const
	{
		CSimpleIniA ini;
		ini.SetUnicode();
		ini.LoadFile(kIniPath);  // keep unknown keys where possible

		ini.SetBoolValue("General", "bEnabled", enabled);
		ini.SetValue("General", "sOverlayRoot", overlayRoot.c_str());
		ini.SetBoolValue("General", "bFirstPersonOnly", firstPersonOnly);
		ini.SetBoolValue("General", "bOpaqueMask", opaqueMask);
		ini.SetLongValue("General", "iMaskMarginPx", maskMarginPx);
		ini.SetBoolValue("General", "bHideInMenus", hideInMenus);
		ini.SetBoolValue("General", "bSuppressOriginal", suppressOriginal);
		ini.SetBoolValue("General", "bSkipWigs", skipWigs);
		ini.SetBoolValue("General", "bSkipCirclets", skipCirclets);
		ini.SetBoolValue("General", "bDebugTools", debugTools);

		ini.SetDoubleValue("Appearance", "fOpacity", opacity);
		ini.SetDoubleValue("Appearance", "fScale", scale);
		ini.SetDoubleValue("Appearance", "fVerticalOffset", verticalOffset);
		ini.SetBoolValue("Appearance", "bExtendEdges", extendEdges);

		ini.SetBoolValue("Light", "bReactive", reactiveLight);
		ini.SetDoubleValue("Light", "fStrength", lightStrength);
		ini.SetDoubleValue("Light", "fFadeSeconds", lightFadeSeconds);
		ini.SetDoubleValue("Light", "fIntervalSeconds", lightIntervalSeconds);

		ini.SetValue("Animation", "sStyle", StyleName(animStyle));
		ini.SetDoubleValue("Animation", "fFadeInSeconds", fadeInSeconds);
		ini.SetDoubleValue("Animation", "fFadeOutSeconds", fadeOutSeconds);
		ini.SetBoolValue("Animation", "bSway", sway);
		ini.SetDoubleValue("Animation", "fSwayStrength", swayStrength);
		ini.SetBoolValue("Animation", "bMotion", motion);
		ini.SetDoubleValue("Animation", "fMotionStrength", motionStrength);

		ini.SetDoubleValue("VR", "fDepthMeters", vrDepth);
		ini.SetDoubleValue("VR", "fCoverage", vrCoverage);

		ini.SetBoolValue("Sound", "bAttenuate", attenuate);
		ini.SetDoubleValue("Sound", "fAttenuateVolume", attenuateVolume);
		ini.SetDoubleValue("Sound", "fAttenuateSeconds", attenuateSeconds);
		ini.SetBoolValue("Sound", "bAutoStrength", autoStrength);
		ini.SetDoubleValue("Sound", "fAutoArtWeight", autoArtWeight);
		ini.SetBoolValue("Sound", "bLowPass", lowPass);
		ini.SetDoubleValue("Sound", "fLowPassHz", lowPassHz);

		std::string joined;
		for (const auto& menu : HiddenMenus()) {
			joined += joined.empty() ? menu : ";" + menu;
		}
		ini.SetValue("Menus", "sHidden", joined.c_str());

		if (ini.SaveFile(kIniPath) < 0) {
			logger::warn("Could not write {}.", kIniPath);
		}
	}
}
