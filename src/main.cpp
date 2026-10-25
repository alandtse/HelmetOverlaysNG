// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Modding-Exception
// Copyright (c) 2026 HelmetOverlaysNG contributors. See COPYING and EXCEPTIONS.md.

#include "pch.h"

#include "LowPass.h"
#include "Muffle.h"
#include "OriginalMod.h"
#include "OverlayController.h"
#include "Renderer.h"
#include "Settings.h"
#include "UI.h"

namespace
{
	void OnSKSELifecycle(SKSE::MessagingInterface::Message* a_msg)
	{
		if (!a_msg) {
			return;
		}

		switch (a_msg->type) {
		case SKSE::MessagingInterface::kPostLoad:
			HONG::Settings::GetSingleton()->Load();
			break;
		case SKSE::MessagingInterface::kPostPostLoad:
			// Must run before BSGraphics::InitD3D.
			HONG::Renderer::Install();
			HONG::LowPass::Install();
			HONG::Renderer::Connect();
			break;
		case SKSE::MessagingInterface::kDataLoaded:
			HONG::OverlayController::GetSingleton()->Register();
			HONG::Muffle::Init();
			HONG::UI::Register();
			if (HONG::OriginalMod::IsInstalled()) {
				logger::info("ScribeOverlays.esp detected; original widget {}.",
					HONG::Settings::GetSingleton()->suppressOriginal ? "will be suppressed" : "left running (two overlays will draw)");
			}
			break;
		case SKSE::MessagingInterface::kPostLoadGame:
		case SKSE::MessagingInterface::kNewGame:
			if (HONG::Settings::GetSingleton()->suppressOriginal) {
				HONG::OriginalMod::Suppress();
			}
			HONG::OverlayController::GetSingleton()->MarkDirty();
			break;
		default:
			break;
		}
	}
}

SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
	SKSE::Init(a_skse);

	logger::info("HelmetOverlaysNG {} loading", HONG_VERSION_STRING);

	SKSE::AllocTrampoline(64);

	auto* messaging = SKSE::GetMessagingInterface();
	if (!messaging || !messaging->RegisterListener(OnSKSELifecycle)) {
		SKSE::stl::report_and_fail("failed to register SKSE lifecycle listener"sv);
	}

	logger::info("HelmetOverlaysNG loaded");
	return true;
}
