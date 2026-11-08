// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Modding-Exception
// Copyright (c) 2026 HelmetOverlaysNG contributors. See COPYING and EXCEPTIONS.md.

#include "Renderer.h"

#include "ArtFit.h"
#include "Debug.h"
#include "EyeView.h"
#include "ImGuiVRHelperClientSDK.h"
#include "Muffle.h"
#include "OverlayAlpha.h"
#include "OverlayController.h"
#include "OverlayMask.h"
#include "ScreenColor.h"
#include "Settings.h"
#include "TextureCache.h"

namespace HONG::Renderer
{
	namespace
	{
		constexpr float kMaxFrameDelta = 0.1f;

		ImGuiVRHelperPluginAPI::Client g_vrHud;  // head-locked HUD plane (VR)

		std::atomic<bool> g_initialized{ false };
		ID3D11Device* g_d3dDevice = nullptr;
		ID3D11DeviceContext* g_d3dContext = nullptr;

		std::vector<DrawLayer> g_layers;
		float g_appliedVrDepth{ 0.0f };
		float g_appliedVrCoverage{ 0.0f };

		// The helper accepts 0.01 m upwards; map slider zero there because it reads <= 0 as "use the global plane".
		constexpr float kVrDepthMin = 0.01f;
		constexpr float kVrDepthMax = 5.0f;
		constexpr float kVrCoverageMin = 0.2f;
		constexpr float kVrCoverageMax = 1.6f;

		void ApplyVrPlane()
		{
			const auto settings = Settings::GetSingleton();
			const float depth = std::isfinite(settings->vrDepth) ? std::clamp(settings->vrDepth, kVrDepthMin, kVrDepthMax) : g_appliedVrDepth;
			const float coverage = std::isfinite(settings->vrCoverage) ? std::clamp(settings->vrCoverage, kVrCoverageMin, kVrCoverageMax) : g_appliedVrCoverage;
			if (depth != g_appliedVrDepth || coverage != g_appliedVrCoverage) {
				g_vrHud.SetHudPlane(depth, coverage);
				g_appliedVrDepth = depth;
				g_appliedVrCoverage = coverage;
			}
		}

		void DrawEdgeExtension(ImDrawList* a_drawList, ImTextureID a_texture, ImVec2 a_min, ImVec2 a_max, ImVec2 a_display, ImU32 a_color)
		{
			constexpr float kEdgeU = 0.5f / kArtWidth;
			constexpr float kEdgeV = 0.5f / kArtHeight;
			const bool left = a_min.x > 0.0f;
			const bool right = a_max.x < a_display.x;
			const bool top = a_min.y > 0.0f;
			const bool bottom = a_max.y < a_display.y;
			if (left) {
				a_drawList->AddImage(a_texture, ImVec2(0, a_min.y), ImVec2(a_min.x, a_max.y), ImVec2(kEdgeU, 0), ImVec2(kEdgeU, 1), a_color);
			}
			if (right) {
				a_drawList->AddImage(a_texture, ImVec2(a_max.x, a_min.y), ImVec2(a_display.x, a_max.y), ImVec2(1 - kEdgeU, 0), ImVec2(1 - kEdgeU, 1), a_color);
			}
			if (top) {
				a_drawList->AddImage(a_texture, ImVec2(a_min.x, 0), ImVec2(a_max.x, a_min.y), ImVec2(0, kEdgeV), ImVec2(1, kEdgeV), a_color);
			}
			if (bottom) {
				a_drawList->AddImage(a_texture, ImVec2(a_min.x, a_max.y), ImVec2(a_max.x, a_display.y), ImVec2(0, 1 - kEdgeV), ImVec2(1, 1 - kEdgeV), a_color);
			}
			if (left && top) {
				a_drawList->AddImage(a_texture, ImVec2(0, 0), a_min, ImVec2(kEdgeU, kEdgeV), ImVec2(kEdgeU, kEdgeV), a_color);
			}
			if (right && top) {
				a_drawList->AddImage(a_texture, ImVec2(a_max.x, 0), ImVec2(a_display.x, a_min.y), ImVec2(1 - kEdgeU, kEdgeV), ImVec2(1 - kEdgeU, kEdgeV), a_color);
			}
			if (left && bottom) {
				a_drawList->AddImage(a_texture, ImVec2(0, a_max.y), ImVec2(a_min.x, a_display.y), ImVec2(kEdgeU, 1 - kEdgeV), ImVec2(kEdgeU, 1 - kEdgeV), a_color);
			}
			if (right && bottom) {
				a_drawList->AddImage(a_texture, a_max, a_display, ImVec2(1 - kEdgeU, 1 - kEdgeV), ImVec2(1 - kEdgeU, 1 - kEdgeV), a_color);
			}
		}

		// How much of the display the art covers: cropped to fill on a flat screen, fitted into the eye view in VR
		// and then divided by the plane's coverage so the painted eye openings keep their flat-screen size.
		ArtFit::Fraction CurrentFit(ImVec2 a_display)
		{
			constexpr float kArtAspect = kArtWidth / kArtHeight;
			const float displayAspect = a_display.x / a_display.y;
			if (!REL::Module::IsVR()) {
				return ArtFit::Cover(displayAspect, kArtAspect);
			}
			const auto fit = ArtFit::Fit(displayAspect, kArtAspect, Settings::GetSingleton()->vrStretch);
			const auto plane = g_appliedVrCoverage > 0.0f ? ArtFit::ArtFitFor(g_appliedVrCoverage) : ArtFit::Fraction{};
			return { fit.x * plane.x, fit.y * plane.y };
		}

		void DrawLayers(ImVec2 a_display)
		{
			const auto drawList = ImGui::GetBackgroundDrawList();
			const auto fit = CurrentFit(a_display);
			const ImVec2 base{ a_display.x * fit.x, a_display.y * fit.y };
			for (const auto& layer : g_layers) {
				const ImVec2 size{ base.x * layer.scale, base.y * layer.scale };
				const ImVec2 center{ a_display.x * (0.5f + layer.offset.x), a_display.y * (0.5f + layer.offset.y) };
				const auto alpha = OverlayAlpha::Byte(layer.alpha);
				const auto channel = [&](float a_color) { return static_cast<std::uint8_t>(std::clamp(layer.tint * a_color, 0.0f, 1.0f) * 255.0f); };
				const ImVec2 min(center.x - size.x * 0.5f, center.y - size.y * 0.5f);
				const ImVec2 max(center.x + size.x * 0.5f, center.y + size.y * 0.5f);
				const auto texture = reinterpret_cast<ImTextureID>(layer.srv);
				const ImU32 color = IM_COL32(channel(layer.color.r), channel(layer.color.g), channel(layer.color.b), alpha);
				drawList->AddImage(texture, min, max, ImVec2(0, 0), ImVec2(1, 1), color);
				if (Settings::GetSingleton()->extendEdges) {
					DrawEdgeExtension(drawList, texture, min, max, a_display, color);
				}
			}
		}

		float FrameDelta()
		{
			using Clock = std::chrono::steady_clock;
			static auto last = Clock::now();
			const auto now = Clock::now();
			const float dt = std::min(std::chrono::duration<float>(now - last).count(), kMaxFrameDelta);
			last = now;
			return dt;
		}

		struct CreateD3DAndSwapChain
		{
			static void thunk()
			{
				func();

				const auto renderer = RE::BSGraphics::Renderer::GetSingleton();
				if (!renderer) {
					return;
				}
				const auto swapChain = reinterpret_cast<IDXGISwapChain*>(renderer->GetRuntimeData().renderWindows[0].swapChain);
				if (!swapChain) {
					logger::error("couldn't find swapChain");
					return;
				}
				DXGI_SWAP_CHAIN_DESC desc{};
				if (FAILED(swapChain->GetDesc(std::addressof(desc)))) {
					logger::error("IDXGISwapChain::GetDesc failed.");
					return;
				}

				g_d3dDevice = reinterpret_cast<ID3D11Device*>(renderer->GetRuntimeData().forwarder);
				g_d3dContext = reinterpret_cast<ID3D11DeviceContext*>(renderer->GetRuntimeData().context);
				TextureCache::SetDevice(g_d3dDevice);

				if (!REL::Module::IsVR()) {
					ImGui::CreateContext();
					ImGui::GetIO().IniFilename = nullptr;
					if (!ImGui_ImplWin32_Init(desc.OutputWindow) || !ImGui_ImplDX11_Init(g_d3dDevice, g_d3dContext)) {
						logger::error("ImGui initialization failed (flat).");
						return;
					}
				}
				g_initialized.store(true);
				logger::info("D3D captured ({}).", REL::Module::IsVR() ? "VR HUD plane" : "flat ImGui");
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		void RunFrame()
		{
			const float dt = FrameDelta();
			OverlayController::GetSingleton()->Update(dt, g_layers);
			ScreenColor::Update(dt, OverlayController::GetSingleton()->WantsScreenColor());
			Debug::Tick();
			Muffle::Update(OverlayController::GetSingleton()->WornAmount() * Debug::SoundStrengthOverride().Apply(OverlayController::GetSingleton()->MuffleStrength()));

			static const auto screenSize = RE::BSGraphics::Renderer::GetScreenSize();
			ImVec2 display{ static_cast<float>(screenSize.width), static_cast<float>(screenSize.height) };
			if (REL::Module::IsVR()) {
				// The helper maps this canvas onto each eye's field of view, so give it the view's shape.
				display.x = display.y * EyeView::Aspect();
			}

			if (REL::Module::IsVR()) {
				if (!g_vrHud.IsConnected()) {
					return;
				}
				ApplyVrPlane();
				// Draws with no layers too, so the panel clears its last frame.
				g_vrHud.RenderHud(g_d3dDevice, g_d3dContext, display, [display]() {
					GImGui->NavWindowingTarget = nullptr;
					DrawLayers(display);
				});
			} else {
				OverlayMask::Publish(OverlayController::GetSingleton()->GetMaskCandidate(), CurrentFit(display));
				if (g_layers.empty()) {
					return;
				}
				ImGui_ImplDX11_NewFrame();
				ImGui_ImplWin32_NewFrame();
				ImGui::GetIO().DisplaySize = display;
				ImGui::NewFrame();
				DrawLayers(display);
				ImGui::EndFrame();
				ImGui::Render();
				ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
			}
		}

		// The vtable is shared by every Scaleform movie, so the thunk must filter on the HUD's own.
		std::atomic<RE::GFxMovieView*> g_hudMovie{ nullptr };
		std::atomic<bool> g_displayHooked{ false };
		std::atomic<bool> g_ranBeforeHud{ false };

		struct HudMovieDisplay
		{
			static void thunk(RE::GFxMovieView* a_movie)
			{
				if (a_movie && a_movie == g_hudMovie.load(std::memory_order_relaxed) && g_initialized.load() && Settings::GetSingleton()->hudOnTop) {
					g_ranBeforeHud.store(true, std::memory_order_relaxed);
					RunFrame();
				}
				func(a_movie);
			}
			static inline REL::Relocation<decltype(thunk)> func;
			static inline std::size_t idx{ 0x26 };
		};

		void HookHudMovie()
		{
			if (REL::Module::IsVR() || g_displayHooked.load()) {
				return;
			}
			const auto ui = RE::UI::GetSingleton();
			const auto hud = ui ? ui->GetMenu<RE::HUDMenu>() : nullptr;
			const auto movie = hud ? hud->uiMovie.get() : nullptr;
			if (!movie) {
				return;
			}
			REL::Relocation<std::uintptr_t> vtbl{ *reinterpret_cast<std::uintptr_t*>(movie) };
			HudMovieDisplay::func = vtbl.write_vfunc(HudMovieDisplay::idx, HudMovieDisplay::thunk);
			g_hudMovie.store(movie);
			g_displayHooked.store(true);
			logger::info("Overlay draws beneath the HUD.");
		}

		struct PostDisplay
		{
			static void thunk(RE::IMenu* a_menu)
			{
				func(a_menu);

				if (!g_initialized.load()) {
					return;
				}
				HookHudMovie();
				if (g_ranBeforeHud.exchange(false, std::memory_order_relaxed)) {
					return;
				}
				RunFrame();
			}
			static inline REL::Relocation<decltype(thunk)> func;
			static inline std::size_t idx{ 0x6 };
		};
	}

	void Connect()
	{
		if (!REL::Module::IsVR()) {
			return;
		}
		if (!g_vrHud.Connect("HelmetOverlaysNG", HONG_VERSION_STRING, ImGuiVRHelperPluginAPI::kClientFlag_HUDMode)) {
			logger::warn("ImGuiVRHelper not found or registration failed - overlay disabled in VR.");
			return;
		}
		g_appliedVrDepth = 0.0f;
		g_appliedVrCoverage = 0.0f;
		ApplyVrPlane();
		logger::info("Connected to ImGuiVRHelper (per-client HUD plane {}).",
			g_vrHud.HasHudPlane() ? "supported" : "unsupported - using the helper's global plane");
	}

	void Install()
	{
		REL::Relocation<std::uintptr_t> target{ RELOCATION_ID(75595, 77226), OFFSET(0x9, 0x275) };  // BSGraphics::InitD3D
		stl::write_thunk_call<CreateD3DAndSwapChain>(target.address());

		stl::write_vfunc<RE::HUDMenu, PostDisplay>();
		logger::info("Renderer hooks installed.");
	}
}
