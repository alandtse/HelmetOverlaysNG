// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Modding-Exception
// Copyright (c) 2026 HelmetOverlaysNG contributors. See COPYING and EXCEPTIONS.md.

#include "EyeView.h"

namespace HONG::EyeView
{
	float Aspect()
	{
		const auto renderer = RE::BSGraphics::Renderer::GetSingleton();
		if (!renderer) {
			return 1.0f;
		}
		const auto texture = reinterpret_cast<ID3D11Texture2D*>(renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN].texture);
		if (!texture) {
			return 1.0f;
		}
		D3D11_TEXTURE2D_DESC desc{};
		texture->GetDesc(&desc);
		const std::uint32_t eyes = REL::Module::IsVR() ? 2 : 1;
		return desc.Height > 0 ? static_cast<float>(desc.Width / eyes) / static_cast<float>(desc.Height) : 1.0f;
	}
}
