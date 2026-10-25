// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Modding-Exception
// Copyright (c) 2026 HelmetOverlaysNG contributors. See COPYING and EXCEPTIONS.md.

#include "TextureCache.h"

#include "ArtMetrics.h"
#include "OverlayResolver.h"

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
		};

		ID3D11Device* g_device = nullptr;
		std::list<Entry> g_lru;  // front = most recent
	}

	void SetDevice(ID3D11Device* a_device)
	{
		g_device = a_device;
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
			}
		}

		g_lru.push_front({ a_path, srv, openness });
		if (g_lru.size() > kCapacity) {
			g_lru.pop_back();
		}
		return srv;
	}
}
