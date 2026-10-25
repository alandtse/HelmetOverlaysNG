// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Modding-Exception
// Copyright (c) 2026 HelmetOverlaysNG contributors. See COPYING and EXCEPTIONS.md.

#include "TextureCache.h"

#include "ArtMetrics.h"
#include "OverlayResolver.h"
#include "Settings.h"

#include <DDSTextureLoader.h>
#include <list>

namespace HONG::TextureCache
{
	namespace
	{
		constexpr std::size_t kCapacity = 4;

		struct Entry
		{
			std::string key;
			Srv srv;  // null = remembered load failure
			float openness{ -1.0f };
			Mask mask;
		};

		ID3D11Device* g_device = nullptr;
		std::list<Entry> g_lru;  // front = most recent
	}

	namespace
	{
		constexpr std::uint32_t kMaskBlockPixels = 4;
		constexpr std::uint32_t kVrMinMarginPixels = 48;

		Mask CreateMask(const std::vector<std::uint8_t>& a_bytes)
		{
			Mask mask;
			ArtMetrics::OpaqueMask built;
			// The head-locked plane shifts a few percent of the eye width between the eyes, and one mask serves both.
			const auto marginPx = std::max<std::uint32_t>(Settings::GetSingleton()->maskMarginPx, REL::Module::IsVR() ? kVrMinMarginPixels : 0);
			const auto marginBlocks = (marginPx + kMaskBlockPixels - 1) / kMaskBlockPixels;
			if (!ArtMetrics::BuildOpaqueMask(a_bytes.data(), a_bytes.size(), marginBlocks, built)) {
				return mask;
			}

			D3D11_TEXTURE2D_DESC desc{};
			desc.Width = built.width;
			desc.Height = built.height;
			desc.MipLevels = 1;
			desc.ArraySize = 1;
			desc.Format = DXGI_FORMAT_R8_UNORM;
			desc.SampleDesc.Count = 1;
			desc.Usage = D3D11_USAGE_IMMUTABLE;
			desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
			D3D11_SUBRESOURCE_DATA data{};
			data.pSysMem = built.pixels.data();
			data.SysMemPitch = built.width;

			ComPtr<ID3D11Texture2D> texture;
			if (FAILED(g_device->CreateTexture2D(&desc, &data, texture.GetAddressOf())) || FAILED(g_device->CreateShaderResourceView(texture.Get(), nullptr, mask.srv.GetAddressOf()))) {
				mask.srv.Reset();
				return mask;
			}
			mask.width = built.width;
			mask.height = built.height;
			mask.share = built.share;
			return mask;
		}
	}

	void SetDevice(ID3D11Device* a_device)
	{
		g_device = a_device;
	}

	Mask MaskFor(const std::string& a_path)
	{
		for (const auto& entry : g_lru) {
			if (_stricmp(entry.key.c_str(), a_path.c_str()) == 0) {
				return entry.mask;
			}
		}
		return {};
	}

	float Openness(const std::string& a_path)
	{
		for (const auto& entry : g_lru) {
			if (_stricmp(entry.key.c_str(), a_path.c_str()) == 0) {
				return entry.openness;
			}
		}
		return -1.0f;
	}

	Srv Get(const std::string& a_path)
	{
		if (!g_device) {
			return nullptr;
		}

		for (auto it = g_lru.begin(); it != g_lru.end(); ++it) {
			if (_stricmp(it->key.c_str(), a_path.c_str()) == 0) {
				g_lru.splice(g_lru.begin(), g_lru, it);
				return it->srv;
			}
		}

		Srv srv;
		Mask mask;
		float openness = -1.0f;
		const auto bytes = Resolver::ReadFile(a_path);
		if (bytes.empty()) {
			logger::warn("Could not read overlay {}", a_path);
		} else {
			ComPtr<ID3D11Resource> resource;
			const auto hr = DirectX::CreateDDSTextureFromMemory(g_device, bytes.data(), bytes.size(), resource.GetAddressOf(), srv.GetAddressOf());
			if (FAILED(hr)) {
				logger::warn("Could not create texture for {} (hr {:#x})", a_path, static_cast<std::uint32_t>(hr));
				srv.Reset();
			} else {
				openness = ArtMetrics::SeeThroughFraction(bytes.data(), bytes.size());
				if (Settings::GetSingleton()->opaqueMask) {
					mask = CreateMask(bytes);
				}
			}
		}

		g_lru.push_front({ a_path, srv, openness, std::move(mask) });
		if (g_lru.size() > kCapacity) {
			g_lru.pop_back();
		}
		return srv;
	}
}
