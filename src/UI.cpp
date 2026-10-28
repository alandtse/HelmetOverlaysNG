// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Modding-Exception
// Copyright (c) 2026 HelmetOverlaysNG contributors. See COPYING and EXCEPTIONS.md.
//

// Built without the project PCH: the SMF header's typedefs conflict with imgui.h.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <RE/Skyrim.h>
#include <REX/REX.h>
#include <SKSE/SKSE.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <shellapi.h>
#include <string>
#include <vector>

namespace logger = SKSE::log;

#include "SKSEMenuFramework.h"

#include "ArtMetrics.h"
#include "Debug.h"
#include "Muffle.h"
#include "Settings.h"
#include "TextMatch.h"
#include "UI.h"

namespace HONG::UI
{
	namespace
	{
		// A text box for narrowing a list. The SMF API has no PushID, so each page passes a distinct a_id.
		void FilterField(const char* a_id, const char* a_hint, char* a_buffer, std::size_t a_size)
		{
			ImGuiMCP::InputTextWithHint(a_id, a_hint, a_buffer, a_size, 0, nullptr, nullptr);
		}
		void Tip(const char* a_text)
		{
			if (ImGuiMCP::IsItemHovered(ImGuiMCP::ImGuiHoveredFlags_AllowWhenDisabled)) {
				ImGuiMCP::SetTooltip("%s", a_text);
			}
		}

		// A slider over a 0..1 setting shown as a percentage.
		bool PercentSlider(const char* a_label, float* a_value, Bounds a_range)
		{
			float percent = *a_value * 100.0f;
			const bool changed = ImGuiMCP::SliderFloat(a_label, &percent, a_range.min * 100.0f, a_range.max * 100.0f, "%.0f%%", ImGuiMCP::ImGuiSliderFlags_AlwaysClamp);
			if (changed) {
				*a_value = percent / 100.0f;
			}
			return changed;
		}

		constexpr float kSilenceVolume = 0.001f;  // floor for the dB readout so a zero volume does not print -inf

		// A block of a color; a_exposure scales it before the display curve so HDR scene values stay readable.
		void Swatch(const char* a_label, Tint::Rgb a_color, float a_exposure)
		{
			const auto display = [&](float a_channel) {
				const float scaled = std::max(a_channel * a_exposure, 0.0f);
				return std::pow(scaled / (1.0f + scaled), 1.0f / 2.2f);
			};
			constexpr float kSwatchWidth = 56.0f;
			constexpr float kSwatchHeight = 18.0f;
			ImGuiMCP::ColorButton((std::string("##hong_swatch_") + a_label).c_str(), { display(a_color.r), display(a_color.g), display(a_color.b), 1.0f }, 0, { kSwatchWidth, kSwatchHeight });
			ImGuiMCP::SameLine(0.0f, -1.0f);
			ImGuiMCP::Text("%s", a_label);
		}

		// Lets the debug page force a value the game would otherwise compute, to see or hear an effect at an extreme.
		void OverrideSlider(const char* a_label, Debug::Override& a_override, float a_natural, const char* a_tip)
		{
			bool active = a_override.Active();
			float value = a_override.Value();
			if (ImGuiMCP::Checkbox((std::string("Force ") + a_label).c_str(), &active)) {
				value = a_natural;
			}
			Tip(a_tip);
			if (active) {
				ImGuiMCP::SameLine(0.0f, -1.0f);
				ImGuiMCP::TextDisabled("(game value %.0f%%)", a_natural * 100.0f);
				PercentSlider((std::string("##hong_force_") + a_label).c_str(), &value, Range::kUnit);
			}
			a_override.Set(active, value);
		}

		// The same buttons close every page and act on every setting; reset needs a second click.
		void SaveReloadRow()
		{
			static bool resetArmed = false;
			auto* s = Settings::GetSingleton();
			ImGuiMCP::Separator();
			if (ImGuiMCP::Button("Save all to INI", { 0, 0 })) {
				s->Save();
				resetArmed = false;
			}
			ImGuiMCP::SameLine(0.0f, -1.0f);
			if (ImGuiMCP::Button("Reload all from INI", { 0, 0 })) {
				s->Load();
				resetArmed = false;
			}
			ImGuiMCP::SameLine(0.0f, -1.0f);
			if (ImGuiMCP::Button(resetArmed ? "Click again to reset ALL pages###hong_reset_all" : "Reset all to defaults###hong_reset_all", { 0, 0 })) {
				if (resetArmed) {
					s->ResetToDefaults();
				}
				resetArmed = !resetArmed;
			} else if (resetArmed && !ImGuiMCP::IsItemHovered(0)) {
				resetArmed = false;
			}
			ImGuiMCP::TextDisabled("These buttons cover every page. Changes apply immediately; Save writes them to the INI.");
		}

		// The overlay's file under the game's Data folder; empty when it is not a loose file there (inside an
		// archive, or in a virtual file system the shell cannot see).
		std::filesystem::path LooseOverlayFile(const std::string& a_vfsPath)
		{
			std::error_code ec;
			auto file = std::filesystem::absolute(std::filesystem::path(L"Data") / std::filesystem::path(a_vfsPath), ec);
			return !ec && std::filesystem::is_regular_file(file, ec) ? file : std::filesystem::path();
		}

		void OverlayFileButtons(const std::string& a_vfsPath)
		{
			const auto file = LooseOverlayFile(a_vfsPath);
			ImGuiMCP::BeginDisabled(file.empty());
			if (ImGuiMCP::Button("Open file", { 0, 0 })) {
				ShellExecuteW(nullptr, L"open", file.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
			}
			ImGuiMCP::SameLine(0.0f, -1.0f);
			if (ImGuiMCP::Button("Show in folder", { 0, 0 })) {
				const auto args = L"/select,\"" + file.wstring() + L"\"";
				ShellExecuteW(nullptr, L"open", L"explorer.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
			}
			ImGuiMCP::EndDisabled();
			Tip(file.empty() ? "Not a loose file in the Data folder (it is inside an archive or a virtual file system), so there is nothing to open. The path above is where mods put it." : "Open the DDS in your default image program, or show where it sits on disk so you can edit or replace it.");
		}

		std::string UrlEncode(const std::string& a_text)
		{
			std::string out;
			for (const unsigned char c : a_text) {
				if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
					out += static_cast<char>(c);
				} else {
					out += std::format("%{:02X}", c);
				}
			}
			return out;
		}

		// Suffix for a list row from ArtMetrics::ColoredFraction; empty while it is unknown.
		const char* ArtTag(float a_color)
		{
			if (a_color < 0.0f) {
				return "";
			}
			return a_color >= ArtMetrics::kColoredShare ? "  (colored)" : "  (black mask)";
		}

		// A list row that reads amber when its art has color; black masks and unchecked rows keep the default.
		bool ArtSelectable(const std::string& a_label, bool a_selected, float a_color)
		{
			const bool colored = a_color >= ArtMetrics::kColoredShare;
			if (colored) {
				ImGuiMCP::PushStyleColor(ImGuiMCP::ImGuiCol_Text, ImGuiMCP::ImVec4(0.90f, 0.62f, 0.0f, 1.0f));
			}
			const bool clicked = ImGuiMCP::Selectable(a_label.c_str(), a_selected, 0, { 0, 0 });
			if (colored) {
				ImGuiMCP::PopStyleColor(1);
			}
			return clicked;
		}

		std::string NexusSearchUrl(const std::string& a_query)
		{
			return "https://www.nexusmods.com/skyrimspecialedition/search/?gsearch=" + UrlEncode(a_query) + "&gsearchtype=mods";
		}

		// Text that opens a URL in the default browser when clicked.
		void Link(const char* a_label, const char* a_url)
		{
			ImGuiMCP::TextColored({ 0.45f, 0.70f, 1.0f, 1.0f }, "%s", a_label);
			if (ImGuiMCP::IsItemHovered(0)) {
				ImGuiMCP::SetTooltip("Open %s in your browser", a_url);
			}
			if (ImGuiMCP::IsItemClicked(0)) {
				const std::string url{ a_url };
				ShellExecuteW(nullptr, L"open", std::wstring(url.begin(), url.end()).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
			}
		}

		void ArtSourceLinks()
		{
			Link("Helmet Overlays (Nexus 35605)", "https://www.nexusmods.com/skyrimspecialedition/mods/35605");
			Link("Helmet Overlays Enhanced Edition (Nexus 189092)", "https://www.nexusmods.com/skyrimspecialedition/mods/189092");
			Link("Helmet Overlay Addon (Nexus 93800)", "https://www.nexusmods.com/skyrimspecialedition/mods/93800");
		}

		void CoverageSummary()
		{
			const auto coverage = Debug::GetCoverage();
			if (!coverage.done) {
				ImGuiMCP::TextDisabled("Overlay coverage: scanning helmets (%.0f%%)...", coverage.progress * 100.0f);
				return;
			}
			if (coverage.eligible > 0 && coverage.covered == 0) {
				ImGuiMCP::TextColored({ 1.0f, 0.5f, 0.4f, 1.0f }, "No overlay art found. This mod ships none.");
				ImGuiMCP::TextWrapped("Install an overlay art pack (click to open):");
				ArtSourceLinks();
				return;
			}
			const float percent = coverage.eligible ? 100.0f * static_cast<float>(coverage.covered) / static_cast<float>(coverage.eligible) : 0.0f;
			ImGuiMCP::Text("Overlay coverage: %u of %u helmets and hoods have art (%.0f%%)", coverage.covered, coverage.eligible, percent);
			Tip("Playable head and hair armors in your load order that have a world model, checked against the overlay art on disk. Details are on the Debug page.");
		}

		void __stdcall RenderOverlay()
		{
			auto* s = Settings::GetSingleton();
			const bool vr = REL::Module::IsVR();

			CoverageSummary();
			ImGuiMCP::Separator();

			if (ImGuiMCP::CollapsingHeader("General", ImGuiMCP::ImGuiTreeNodeFlags_DefaultOpen)) {
				ImGuiMCP::Checkbox("Enable helmet overlays", &s->enabled);
				Tip("Master switch. Off hides the overlay (and sound changes) without unloading the plugin.");
				if (!vr) {
					ImGuiMCP::Checkbox("First person only", &s->firstPersonOnly);
					Tip("Only draw in the first-person camera.");
				}
				ImGuiMCP::TextDisabled("Changes apply immediately. Close this panel to see the whole view.");
			}

			if (ImGuiMCP::CollapsingHeader("Appearance", ImGuiMCP::ImGuiTreeNodeFlags_DefaultOpen)) {
				PercentSlider("Opacity", &s->opacity, Range::kOpacity);
				Tip("Overall strength of the overlay art (the original MCM's \"Alpha\").");
				ImGuiMCP::SliderFloat("Scale", &s->scale, Range::kScale.min, Range::kScale.max, "%.2f", ImGuiMCP::ImGuiSliderFlags_AlwaysClamp);
				Tip("Uniform scale about the screen center. 1.0 covers the screen with the 16:9 art; larger crops the edges.");
				PercentSlider("Vertical position (% of height)", &s->verticalOffset, Range::kVerticalOffset);
				Tip("Moves the art up (negative) or down (positive).");
				ImGuiMCP::Checkbox("Extend image edges", &s->extendEdges);
				Tip("Stretches the art's outermost pixels over any gap that a smaller scale or a vertical shift opens at the screen edge.");
			}

			if (ImGuiMCP::CollapsingHeader("Light", ImGuiMCP::ImGuiTreeNodeFlags_DefaultOpen)) {
				ImGuiMCP::Checkbox("Darken in dark places", &s->reactiveLight);
				Tip("Dims the overlay art with the light level around you. Fully black parts of the art stay black.");
				ImGuiMCP::BeginDisabled(!s->reactiveLight);
				PercentSlider("Darkness strength", &s->lightStrength, Range::kLightStrength);
				Tip("How dark the art gets with no light. 100% is black.");
				ImGuiMCP::SliderFloat("Fade time (s)", &s->lightFadeSeconds, Range::kLightFade.min, Range::kLightFade.max, "%.1f", ImGuiMCP::ImGuiSliderFlags_AlwaysClamp);
				Tip("Time to move to a new light level.");
				ImGuiMCP::SliderFloat("Check interval (s)", &s->lightIntervalSeconds, Range::kLightInterval.min, Range::kLightInterval.max, "%.1f", ImGuiMCP::ImGuiSliderFlags_AlwaysClamp);
				Tip("How often the light level is read.");
				ImGuiMCP::EndDisabled();
				ImGuiMCP::Checkbox("Tint with screen color", &s->screenTint);
				Tip("Shifts the art toward the color of the scene around the screen edges, so firelight turns it orange and lightning purple. Brightness still comes from the light level. With debug tools on, the Debug page shows the effect side by side.");
				ImGuiMCP::BeginDisabled(!s->screenTint);
				PercentSlider("Tint strength", &s->screenTintStrength, Range::kUnit);
				Tip("0% keeps the art's own color; 100% takes the full hue of the scene.");
				ImGuiMCP::EndDisabled();
			}

			if (ImGuiMCP::CollapsingHeader("Animation", ImGuiMCP::ImGuiTreeNodeFlags_DefaultOpen)) {
				static const char* const kStyles[] = { "Fade", "Settle", "Slide" };
				int style = static_cast<int>(s->animStyle);
				if (ImGuiMCP::Combo("Style", &style, kStyles, 3, -1)) {
					s->animStyle = static_cast<AnimStyle>(style);
				}
				Tip("How the overlay appears and disappears. Fade changes opacity only, Settle also starts slightly larger, Slide moves in from above.");
				ImGuiMCP::SliderFloat("Fade in (s)", &s->fadeInSeconds, Range::kFade.min, Range::kFade.max, "%.2f", ImGuiMCP::ImGuiSliderFlags_AlwaysClamp);
				Tip("Time to bring the overlay in when a helmet is equipped.");
				ImGuiMCP::SliderFloat("Fade out (s)", &s->fadeOutSeconds, Range::kFade.min, Range::kFade.max, "%.2f", ImGuiMCP::ImGuiSliderFlags_AlwaysClamp);
				Tip("Time to remove the overlay when the helmet comes off.");
				if (!vr) {
					ImGuiMCP::Checkbox("Head-turn sway", &s->sway);
					Tip("The overlay lags behind camera turns. The opaque mask follows it.");
					ImGuiMCP::BeginDisabled(!s->sway);
					ImGuiMCP::SliderFloat("Sway strength", &s->swayStrength, Range::kSwayStrength.min, Range::kSwayStrength.max, "%.2f", ImGuiMCP::ImGuiSliderFlags_AlwaysClamp);
					Tip("Multiplier on how far the overlay lags; 1.0 is the default amount.");
					ImGuiMCP::EndDisabled();
					ImGuiMCP::Checkbox("Jump, strafe and sprint motion", &s->motion);
					Tip("Short nudges of the overlay when you jump, strafe or start sprinting, as in the original mod. The opaque mask follows them.");
					ImGuiMCP::BeginDisabled(!s->motion);
					ImGuiMCP::SliderFloat("Motion strength", &s->motionStrength, Range::kMotionStrength.min, Range::kMotionStrength.max, "%.2f", ImGuiMCP::ImGuiSliderFlags_AlwaysClamp);
					Tip("Multiplier on the size of the nudges; 1.0 matches the original mod.");
					ImGuiMCP::EndDisabled();
				}
			}

			if (vr && ImGuiMCP::CollapsingHeader("VR", ImGuiMCP::ImGuiTreeNodeFlags_DefaultOpen)) {
				ImGuiMCP::SliderFloat("Plane depth (m)", &s->vrDepth, Range::kVrDepth.min, Range::kVrDepth.max, "%.2f", ImGuiMCP::ImGuiSliderFlags_AlwaysClamp);
				Tip("Distance of the head-locked visor plane. Most lenses can't focus nearer than ~0.5 m.");
				ImGuiMCP::SliderFloat("Coverage", &s->vrCoverage, Range::kVrCoverage.min, Range::kVrCoverage.max, "%.2f", ImGuiMCP::ImGuiSliderFlags_AlwaysClamp);
				Tip("Fraction of each eye's view the plane fills; above 1 over-fills so no edge gap shows.");
				PercentSlider("Art stretch", &s->vrStretch, Range::kUnit);
				Tip("0% keeps the art's own proportions, as on a flat screen, and fills the rest of the view with the art's edge. 100% stretches the art to fill the whole view.");
			}

			if (ImGuiMCP::CollapsingHeader("Armor filtering", 0)) {
				ImGuiMCP::Checkbox("Ignore wigs", &s->skipWigs);
				Tip("Wigs and hairstyles that use only the hair slot never have helmet art. They are left out of the coverage scan and never get an overlay. Rescan on the Debug page to refresh the counts.");
				ImGuiMCP::Checkbox("Ignore circlets", &s->skipCirclets);
				Tip("Circlets do not cover the view. They are left out of the coverage scan and never get an overlay. Rescan on the Debug page to refresh the counts.");
			}

			if (ImGuiMCP::CollapsingHeader("Overlay art sources", 0)) {
				ImGuiMCP::TextWrapped("This mod ships no art. Any of these provides it (click to open):");
				ArtSourceLinks();
			}

			if (ImGuiMCP::CollapsingHeader("Compatibility", 0)) {
				ImGuiMCP::Checkbox("Suppress original Helmet Overlays widget", &s->suppressOriginal);
				Tip("Stops ScribeOverlays.esp's quests on load so its SWF doesn't draw a second overlay. Takes effect on the next load.");
			}

			if (ImGuiMCP::CollapsingHeader("Performance", 0)) {
				ImGuiMCP::Checkbox("Skip shading behind opaque overlay", &s->opaqueMask);
				Tip("Skips shading the scene behind the overlay's fully opaque pixels to save GPU time. Applies when the helmet is next equipped.");
				ImGuiMCP::BeginDisabled(!s->opaqueMask);
				ImGuiMCP::SliderInt("Mask safety margin (px)", &s->maskMarginPx, static_cast<int>(Range::kMaskMargin.min), static_cast<int>(Range::kMaskMargin.max), "%d", ImGuiMCP::ImGuiSliderFlags_AlwaysClamp);
				Tip(vr ? "How far inside the opaque area the mask stops. VR always keeps at least 48 px so both eyes stay covered. Applies when the helmet is next equipped." : "How far inside the opaque area the mask stops. Larger is safer against fringes, smaller saves a little more. Applies when the helmet is next equipped.");
				ImGuiMCP::EndDisabled();
			}
			SaveReloadRow();
		}

		void __stdcall RenderSound()
		{
			auto* s = Settings::GetSingleton();

			ImGuiMCP::TextDisabled("Two independent effects while a helmet is worn. Use either, both or neither.");
			ImGuiMCP::Separator();

			if (ImGuiMCP::CollapsingHeader("Current helmet", ImGuiMCP::ImGuiTreeNodeFlags_DefaultOpen)) {
				const auto current = Debug::GetCurrent();
				if (current.armorId == 0) {
					ImGuiMCP::TextDisabled("No helmet worn. Equip one, or preview one on the Debug page.");
				} else {
					const auto& forced = Debug::SoundStrengthOverride();
					const float strength = forced.Apply(current.strength);
					const auto effect = Muffle::EffectAt(strength);
					ImGuiMCP::Text("%s", current.armorName.c_str());
					ImGuiMCP::Text("Muffle strength: %.0f%%%s", strength * 100.0f, forced.Active() ? "  (forced on the Debug page)" : "");
					if (s->attenuate) {
						ImGuiMCP::Text("World sound: %.0f%% volume (%.1f dB)", effect.volume * 100.0f, 20.0f * std::log10(std::max(effect.volume, kSilenceVolume)));
					} else {
						ImGuiMCP::TextDisabled("World sound: attenuation off");
					}
					if (s->lowPass) {
						ImGuiMCP::Text("Low-pass: %.0f Hz cutoff", effect.cutoffHz);
					} else {
						ImGuiMCP::TextDisabled("Low-pass: off");
					}
				}
			}

			ImGuiMCP::Checkbox("Scale by helmet type", &s->autoStrength);
			Tip("Closed helms muffle fully; open helmets and hoods muffle less. Uses the armor class (heavy, light, clothing) and how much of the overlay art is see-through. Applies when the helmet is next equipped.");
			ImGuiMCP::BeginDisabled(!s->autoStrength);
			ImGuiMCP::SliderFloat("Weight of overlay art", &s->autoArtWeight, Range::kAutoArtWeight.min, Range::kAutoArtWeight.max, "%.2f", ImGuiMCP::ImGuiSliderFlags_AlwaysClamp);
			Tip("0 uses only the armor class; 1 uses only how closed the overlay is.");
			ImGuiMCP::EndDisabled();
			ImGuiMCP::Separator();

			if (ImGuiMCP::CollapsingHeader("Volume attenuation", ImGuiMCP::ImGuiTreeNodeFlags_DefaultOpen)) {
				ImGuiMCP::Checkbox("Attenuate world sound", &s->attenuate);
				Tip("Lowers the volume of world sound effects. Voice and music are untouched.");
				ImGuiMCP::BeginDisabled(!s->attenuate);
				PercentSlider("Volume at full helmet", &s->attenuateVolume, Range::kAttenuateVolume);
				Tip("World sound volume when the helmet is fully worn; 100% leaves it unchanged.");
				ImGuiMCP::SliderFloat("Sound fade lag (s)", &s->attenuateSeconds, Range::kAttenuateLag.min, Range::kAttenuateLag.max, "%.2f", ImGuiMCP::ImGuiSliderFlags_AlwaysClamp);
				Tip("Extra time the sound takes to follow the visual fade; higher values lag more.");
				ImGuiMCP::EndDisabled();
			}

			if (ImGuiMCP::CollapsingHeader("Low-pass distortion", ImGuiMCP::ImGuiTreeNodeFlags_DefaultOpen)) {
				ImGuiMCP::Checkbox("Muffle highs (low-pass)", &s->lowPass);
				Tip("Rolls off high frequencies like a real helmet, instead of just turning things down.");
				ImGuiMCP::TextDisabled("Applies to every sound except music. While the helmet is worn it replaces the game's distance muffling on those sounds.");
				ImGuiMCP::BeginDisabled(!s->lowPass);
				ImGuiMCP::SliderFloat("Cutoff at full helmet (Hz)", &s->lowPassHz, Range::kLowPassHz.min, Range::kLowPassHz.max, "%.0f", ImGuiMCP::ImGuiSliderFlags_AlwaysClamp);
				Tip("Highest frequency that still passes. The engine's filter tops out at SampleRate/6 (about 8000 Hz), so that is fully open.");
				ImGuiMCP::EndDisabled();
			}
			SaveReloadRow();
		}

		void __stdcall RenderMenus()
		{
			auto* s = Settings::GetSingleton();

			ImGuiMCP::Checkbox("Hide overlay while these menus are open", &s->hideInMenus);
			Tip("The overlay always draws beneath menus; this fades it out so it doesn't dim a UI you are reading.");
			ImGuiMCP::Separator();

			static char filter[64]{};
			FilterField("##hong_menus_filter", "Type to filter menus...", filter, sizeof(filter));

			ImGuiMCP::BeginDisabled(!s->hideInMenus);
			static std::vector<std::string> menus;
			if (menus.empty() || ImGuiMCP::Button("Refresh list", { 0, 0 })) {
				menus = Debug::RegisteredMenus();
			}
			ImGuiMCP::Text("%zu menus registered by the game and mods:", menus.size());
			for (const auto& name : menus) {
				if (!ContainsNoCase(name, filter)) {
					continue;
				}
				bool hidden = s->IsMenuHidden(name);
				if (ImGuiMCP::Checkbox((name + "##hong_menu").c_str(), &hidden)) {
					s->SetMenuHidden(name, hidden);
				}
			}

			for (const auto& name : s->HiddenMenus()) {
				if (std::ranges::find(menus, name) == menus.end() && ContainsNoCase(name, filter)) {
					bool hidden = true;
					if (ImGuiMCP::Checkbox((name + "  (not registered)##hong_menu_missing").c_str(), &hidden) && !hidden) {
						s->SetMenuHidden(name, false);
					}
				}
			}
			ImGuiMCP::EndDisabled();

			ImGuiMCP::Separator();
			if (ImGuiMCP::Button("Reset menu list to defaults", { 0, 0 })) {
				s->ResetMenusToDefaults();
			}
			SaveReloadRow();
		}

		void __stdcall RenderDebug()
		{
			auto* s = Settings::GetSingleton();

			ImGuiMCP::Checkbox("Enable debug tools", &s->debugTools);
			Tip("Shows what the plugin resolved for the current helmet and lets you preview any helmet's overlay without equipping it.");
			if (!s->debugTools) {
				return;
			}
			ImGuiMCP::Separator();

			const auto current = Debug::GetCurrent();
			if (ImGuiMCP::CollapsingHeader("Current helmet", ImGuiMCP::ImGuiTreeNodeFlags_DefaultOpen)) {
				if (current.armorId == 0 && !current.previewing) {
					ImGuiMCP::TextDisabled("No helmet resolved (bare head, or nothing refreshed yet).");
				} else {
					if (current.armorId == 0) {
						ImGuiMCP::Text("Overlay file preview (no armor)");
					} else {
						ImGuiMCP::Text("%s  [%08X]%s", current.armorName.c_str(), current.armorId, current.previewing ? "  (previewing)" : "");
						ImGuiMCP::Text("World model: %s", current.model.empty() ? "(none)" : current.model.c_str());
					}
					if (current.found) {
						ImGuiMCP::Text("Overlay: %s", current.overlayPath.c_str());
						OverlayFileButtons(current.overlayPath);
						if (current.openness >= 0.0f) {
							ImGuiMCP::Text("Overlay see-through: %.0f%%", current.openness * 100.0f);
						}
						if (current.color >= 0.0f) {
							ImGuiMCP::Text("Overlay art: %s (%.0f%% of the visible area has color)", current.color >= ArtMetrics::kColoredShare ? "colored" : "black mask", current.color * 100.0f);
						}
						ImGuiMCP::Text("Sound muffle strength: %.2f", current.strength);
						ImGuiMCP::Text("Opaque mask: %.0f%% of the overlay", current.maskShare * 100.0f);
						if (current.light >= 0.0f) {
							ImGuiMCP::Text("Light level: %.0f%%", current.light * 100.0f);
						}
					} else {
						ImGuiMCP::TextColored({ 1.0f, 0.5f, 0.4f, 1.0f }, "No overlay art found. Paths tried:");
						for (const auto& path : current.tried) {
							ImGuiMCP::BulletText("%s", path.c_str());
						}
						const auto search = NexusSearchUrl(current.armorName + " helmet overlay");
						Link("Search Nexus for art for this helmet", search.c_str());
						ImGuiMCP::TextDisabled("Art goes at the first path above; a modder can draw it there.");
					}
				}
			}

			if (ImGuiMCP::CollapsingHeader("Screen color tint", ImGuiMCP::ImGuiTreeNodeFlags_DefaultOpen)) {
				if (!current.sceneSampled) {
					ImGuiMCP::TextDisabled("No scene color sampled (no overlay shown, or the tint is off).");
				} else {
					const Tint::Rgb light{ current.lightTint, current.lightTint, current.lightTint };
					const Tint::Rgb tinted{ current.lightTint * current.tint.r, current.lightTint * current.tint.g, current.lightTint * current.tint.b };
					Swatch("Scene edge color (sampled)", current.scene, 1.0f);
					Swatch("Art multiplier, light level only (default look)", light, 1.0f);
					Swatch("Art multiplier, with screen color", tinted, 1.0f);
					ImGuiMCP::TextDisabled("Multiplier RGB %.2f %.2f %.2f", tinted.r, tinted.g, tinted.b);
				}
			}

			if (ImGuiMCP::CollapsingHeader("Force effects", ImGuiMCP::ImGuiTreeNodeFlags_DefaultOpen)) {
				ImGuiMCP::TextDisabled("Push an effect to an extreme to see or hear it. Not saved; cleared on restart.");
				OverrideSlider("sound muffle strength", Debug::SoundStrengthOverride(), current.strength,
					"Replaces the helmet's muffle strength for both sound effects. 100% applies the full volume drop and cutoff from the Sound page.");
				OverrideSlider("screen tint strength", Debug::TintStrengthOverride(), s->screenTint ? s->screenTintStrength : 0.0f,
					"Replaces the tint strength. 0% shows the default light-level look, 100% the full hue of the scene color, even with the tint turned off.");
				ImGuiMCP::BeginDisabled(!s->reactiveLight);
				OverrideSlider("light level", Debug::LightLevelOverride(), current.light >= 0.0f ? current.light : 1.0f,
					"Replaces the sampled light level (0% is dark, 100% is fully lit). Needs \"Darken in dark places\".");
				ImGuiMCP::EndDisabled();
			}

			if (ImGuiMCP::CollapsingHeader("Coverage", ImGuiMCP::ImGuiTreeNodeFlags_DefaultOpen)) {
				const auto coverage = Debug::GetCoverage();
				CoverageSummary();
				if (coverage.done) {
					ImGuiMCP::Text("%u without art, %u skipped (no world model)", coverage.missing, coverage.noModel);
					constexpr std::size_t kMaxPluginRows = 25;
					std::size_t rows = 0;
					for (const auto& plugin : coverage.byPlugin) {
						if (rows++ >= kMaxPluginRows) {
							ImGuiMCP::TextDisabled("... %zu more plugins", coverage.byPlugin.size() - kMaxPluginRows);
							break;
						}
						ImGuiMCP::BulletText("%s: %u of %u covered", plugin.plugin.c_str(), plugin.covered, plugin.total);
					}
				}
				if (ImGuiMCP::Button("Rescan", { 0, 0 })) {
					Debug::RescanCoverage();
				}
				Tip("Run again after installing or removing overlay art.");
				if (coverage.done) {
					ImGuiMCP::SameLine(0.0f, -1.0f);
					ImGuiMCP::BeginDisabled(coverage.classifying);
					if (ImGuiMCP::Button("Check art colors", { 0, 0 })) {
						Debug::ClassifyArt();
					}
					ImGuiMCP::EndDisabled();
					Tip("Reads each overlay file a few per frame and tells colored art from black masks, whose color comes entirely from the light and screen tint. Then use the filter under Preview a helmet.");
					if (coverage.classifying) {
						ImGuiMCP::Text("Checking art... %u of %u", coverage.classified, coverage.covered);
					} else if (coverage.classified > 0) {
						ImGuiMCP::Text("%u colored, %u black masks", coverage.colored, coverage.blackMasks);
					}
				}
			}

			if (ImGuiMCP::CollapsingHeader("Preview a helmet", ImGuiMCP::ImGuiTreeNodeFlags_DefaultOpen)) {
				const auto previewing = Debug::Previewing();
				const auto previewingFile = Debug::PreviewingFile();
				if (previewing != 0 || !previewingFile.empty()) {
					if (ImGuiMCP::Button("Stop preview (use worn helmet)", { 0, 0 })) {
						Debug::ClearPreview();
					}
				} else {
					ImGuiMCP::TextDisabled("Pick a helmet to draw its overlay without equipping it.");
				}

				static char filter[64]{};
				FilterField("##hong_debug_helmet_filter", "Type to filter helmets...", filter, sizeof(filter));

				static int artFilter = 0;
				static const char* const kArtFilters[] = { "All helmets", "Without art", "Colored art", "Black masks", "Not checked yet", "Art with no helmet" };
				constexpr int kOrphanFilter = 5;
				ImGuiMCP::Combo("Show", &artFilter, kArtFilters, static_cast<int>(std::size(kArtFilters)), -1);
				Tip("Colored art, Black masks and Art with no helmet check the art colors the first time they are chosen. Art with no helmet lists installed overlay files that no helmet in your load order uses, for armor mods you do not have.");
				const auto shown = static_cast<Debug::ArtFilter>(artFilter);
				const auto coverage = Debug::GetCoverage();
				const bool needsColors = shown == Debug::ArtFilter::kColored || shown == Debug::ArtFilter::kBlack || artFilter == kOrphanFilter;
				if (needsColors && coverage.done && !coverage.classifying && !coverage.checked) {
					Debug::ClassifyArt();
				}
				if (coverage.classifying) {
					ImGuiMCP::TextDisabled("Checking art colors... %u of %u (matches fill in as it goes)", coverage.classified, coverage.covered);
				}

				constexpr std::size_t kMaxRows = 60;
				std::size_t totalMatches = 0;
				if (artFilter == kOrphanFilter) {
					const auto orphans = Debug::FilterOrphans(filter, kMaxRows, totalMatches);
					ImGuiMCP::Text("%zu overlay files with no helmet in your load order", totalMatches);
					const Debug::OrphanArt* picked = nullptr;
					for (const auto& orphan : orphans) {
						const bool selected = orphan.path == previewingFile;
						const std::string label = orphan.path + ArtTag(orphan.color) + "##" + orphan.path;
						if (ArtSelectable(label, selected, orphan.color)) {
							Debug::PreviewFile(orphan.path);
						}
						if (selected) {
							picked = &orphan;
						}
					}
					if (totalMatches > orphans.size()) {
						ImGuiMCP::TextDisabled("... narrow the filter to see more.");
					}
					if (picked) {
						const auto search = NexusSearchUrl(picked->term + " armor");
						Link(("Search Nexus for \"" + picked->term + "\"").c_str(), search.c_str());
					}
				} else {
					const auto helmets = Debug::FilterHelmets(filter, shown, kMaxRows, totalMatches);
					ImGuiMCP::Text("%zu matching helmets", totalMatches);
					for (const auto& helmet : helmets) {
						const char* tag = helmet.art == 0 ? "  (no art)" : ArtTag(helmet.color);
						const std::string label = helmet.name + "  [" + helmet.plugin + "]" + tag + "##" + std::to_string(helmet.id);
						if (ArtSelectable(label, helmet.id == previewing, helmet.color)) {
							Debug::Preview(helmet.id);
						}
					}
					if (totalMatches > helmets.size()) {
						ImGuiMCP::TextDisabled("... narrow the filter to see more.");
					}
				}
			}
			SaveReloadRow();
		}
	}

	void Register()
	{
		if (!SKSEMenuFramework::IsInstalled()) {
			logger::info("SKSE Menu Framework not installed; in-game settings disabled.");
			return;
		}
		// The header caches the module handle in a static initialised when this DLL loads,
		// which is null if SKSEMenuFramework.dll loads after us.
		menuFramework = GetModuleHandle(L"SKSEMenuFramework");
		SKSEMenuFramework::SetSection("Helmet Overlays NG");
		SKSEMenuFramework::AddSectionItem("Overlay", RenderOverlay);
		SKSEMenuFramework::AddSectionItem("Sound", RenderSound);
		SKSEMenuFramework::AddSectionItem("Menus", RenderMenus);
		SKSEMenuFramework::AddSectionItem("Debug", RenderDebug);
		logger::info("Registered SMF pages (framework v{:.1f}).", SKSEMenuFramework::GetMenuFrameworkVersion());
	}
}
