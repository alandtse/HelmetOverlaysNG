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
			float color{ -1.0f };
			Mask mask;
		};

		ID3D11Device* g_device = nullptr;
		std::list<Entry> g_lru;  // front = most recent

		// The mask settings the cached entries were built with; a change clears the cache.
		bool g_maskBuilt{ false };
		int g_maskMargin{ 0 };
		bool g_maskSettingsKnown{ false };

		const Entry* Find(const std::string& a_path)
		{
			const auto it = std::ranges::find_if(g_lru, [&](const Entry& a_entry) { return _stricmp(a_entry.key.c_str(), a_path.c_str()) == 0; });
			return it != g_lru.end() ? &*it : nullptr;
		}
	}

	namespace
	{
		constexpr std::uint32_t kMaskBlockPixels = 4;

		Mask CreateMask(const std::vector<std::uint8_t>& a_bytes)
		{
			Mask mask;
			ArtMetrics::OpaqueMask built;
			const auto marginPx = Settings::GetSingleton()->maskMarginPx;
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
		g_maskSettingsKnown = false;  // a new device owns the old textures
		g_lru.clear();
	}

	bool NoteMaskSettings(bool a_enabled, int a_marginPx)
	{
		if (g_maskSettingsKnown && a_enabled == g_maskBuilt && a_marginPx == g_maskMargin) {
			return false;
		}
		// The mask lives inside the same cache entry as the texture, so the only way to rebuild it is to
		// drop the entry. Art in use keeps its SRV (already held by the layer) and reloads on the next refresh.
		const bool changed = g_maskSettingsKnown;
		g_maskSettingsKnown = true;
		g_maskBuilt = a_enabled;
		g_maskMargin = a_marginPx;
		if (changed) {
			g_lru.clear();
		}
		return changed;
	}

	Mask MaskFor(const std::string& a_path)
	{
		const auto entry = Find(a_path);
		return entry ? entry->mask : Mask{};
	}

	float Openness(const std::string& a_path)
	{
		const auto entry = Find(a_path);
		return entry ? entry->openness : -1.0f;
	}

	float ColoredShare(const std::string& a_path)
	{
		const auto entry = Find(a_path);
		return entry ? entry->color : -1.0f;
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
		float color = -1.0f;
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
				color = ArtMetrics::ColoredFraction(bytes.data(), bytes.size());
				// VR opacity/occlusion execution is owned by the helper, not this flat-screen mask.
				if (!REL::Module::IsVR() && Settings::GetSingleton()->opaqueMask) {
					mask = CreateMask(bytes);
				}
			}
		}

		g_lru.push_front({ a_path, srv, openness, color, std::move(mask) });
		if (g_lru.size() > kCapacity) {
			g_lru.pop_back();
		}
		return srv;
	}
}
