// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Modding-Exception
// Copyright (c) 2026 HelmetOverlaysNG contributors. See COPYING and EXCEPTIONS.md.

#include "ScreenColor.h"

#include <DirectXPackedVector.h>

namespace HONG::ScreenColor
{
	namespace
	{
		constexpr float kSampleIntervalSeconds = 0.2f;
		constexpr std::uint32_t kMaxCellsAcross = 32;  // reduced image is at most this wide
		constexpr std::size_t kSlots = 3;
		constexpr DXGI_FORMAT kSceneFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;

		struct Slot
		{
			Microsoft::WRL::ComPtr<ID3D11Texture2D> staging;
			bool pending{ false };
		};

		struct State
		{
			ID3D11Texture2D* source{ nullptr };
			Microsoft::WRL::ComPtr<ID3D11Texture2D> chain;
			Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> chainSrv;
			std::array<Slot, kSlots> slots;
			std::uint32_t width{ 0 };
			std::uint32_t height{ 0 };
			std::uint32_t eyes{ 1 };  // side-by-side images in the scene target: 2 in VR
			std::uint32_t level{ 0 };
			std::uint32_t cellsX{ 0 };
			std::uint32_t cellsY{ 0 };
			float clock{ 0.0f };
			bool failed{ false };
			bool haveLatest{ false };
			Tint::Rgb latest;
		};
		State g_state;

		ID3D11Texture2D* SceneTexture()
		{
			const auto renderer = RE::BSGraphics::Renderer::GetSingleton();
			if (!renderer) {
				return nullptr;
			}
			const auto& target = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];
			return reinterpret_cast<ID3D11Texture2D*>(target.texture);
		}

		void Fail(const char* a_reason)
		{
			g_state = {};
			g_state.failed = true;
			logger::warn("Screen color tint disabled: {}.", a_reason);
		}

		bool Build(ID3D11Device* a_device, ID3D11Texture2D* a_source)
		{
			D3D11_TEXTURE2D_DESC desc{};
			a_source->GetDesc(&desc);
			if (desc.Format != kSceneFormat || desc.SampleDesc.Count != 1) {
				Fail("the scene color target is not a single-sample 16-bit float image");
				return false;
			}

			g_state = {};
			g_state.source = a_source;
			g_state.width = desc.Width;
			g_state.height = desc.Height;
			g_state.eyes = REL::Module::IsVR() ? 2 : 1;
			while ((desc.Width / g_state.eyes >> g_state.level) > kMaxCellsAcross) {
				++g_state.level;
			}
			g_state.cellsX = std::max(1u, desc.Width >> g_state.level);
			g_state.cellsY = std::max(1u, desc.Height >> g_state.level);

			D3D11_TEXTURE2D_DESC chain = desc;
			chain.MipLevels = 0;
			chain.Usage = D3D11_USAGE_DEFAULT;
			chain.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
			chain.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;
			chain.CPUAccessFlags = 0;
			if (FAILED(a_device->CreateTexture2D(&chain, nullptr, g_state.chain.GetAddressOf())) ||
				FAILED(a_device->CreateShaderResourceView(g_state.chain.Get(), nullptr, g_state.chainSrv.GetAddressOf()))) {
				Fail("the mip chain could not be created");
				return false;
			}

			D3D11_TEXTURE2D_DESC staging{};
			staging.Width = g_state.cellsX;
			staging.Height = g_state.cellsY;
			staging.MipLevels = 1;
			staging.ArraySize = 1;
			staging.Format = kSceneFormat;
			staging.SampleDesc.Count = 1;
			staging.Usage = D3D11_USAGE_STAGING;
			staging.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
			for (auto& slot : g_state.slots) {
				if (FAILED(a_device->CreateTexture2D(&staging, nullptr, slot.staging.GetAddressOf()))) {
					Fail("the readback textures could not be created");
					return false;
				}
			}
			logger::info("Screen color tint sampling {}x{} scene color ({} eye{}) through a {}x{} image.", desc.Width, desc.Height, g_state.eyes, g_state.eyes == 1 ? "" : "s", g_state.cellsX, g_state.cellsY);
			return true;
		}

		// Ring average of each eye's half of the reduced image, averaged so both eyes get the same tint.
		Tint::Rgb EyeAverage(const Tint::Rgb* a_cells)
		{
			const std::uint32_t eyeWidth = g_state.cellsX / g_state.eyes;
			Tint::Rgb sum = Tint::RingAverage(a_cells, g_state.cellsX, { 0, 0, eyeWidth, g_state.cellsY });
			if (g_state.eyes == 2) {
				sum = Tint::Mean(sum, Tint::RingAverage(a_cells, g_state.cellsX, { g_state.cellsX - eyeWidth, 0, eyeWidth, g_state.cellsY }));
			}
			return sum;
		}

		void Harvest(ID3D11DeviceContext* a_context)
		{
			for (auto& slot : g_state.slots) {
				if (!slot.pending) {
					continue;
				}
				D3D11_MAPPED_SUBRESOURCE mapped{};
				const auto hr = a_context->Map(slot.staging.Get(), 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &mapped);
				if (FAILED(hr)) {
					continue;
				}
				std::vector<Tint::Rgb> cells(static_cast<std::size_t>(g_state.cellsX) * g_state.cellsY);
				for (std::uint32_t y = 0; y < g_state.cellsY; ++y) {
					const auto* row = reinterpret_cast<const DirectX::PackedVector::HALF*>(static_cast<const std::uint8_t*>(mapped.pData) + static_cast<std::size_t>(y) * mapped.RowPitch);
					for (std::uint32_t x = 0; x < g_state.cellsX; ++x) {
						cells[y * g_state.cellsX + x] = {
							DirectX::PackedVector::XMConvertHalfToFloat(row[x * 4 + 0]),
							DirectX::PackedVector::XMConvertHalfToFloat(row[x * 4 + 1]),
							DirectX::PackedVector::XMConvertHalfToFloat(row[x * 4 + 2]),
						};
					}
				}
				a_context->Unmap(slot.staging.Get(), 0);
				slot.pending = false;
				g_state.latest = EyeAverage(cells.data());
				if (!std::exchange(g_state.haveLatest, true)) {
					logger::info("First scene color sample: edge ring average {:.3f} {:.3f} {:.3f}.", g_state.latest.r, g_state.latest.g, g_state.latest.b);
				}
			}
		}

		Slot* FreeSlot()
		{
			for (auto& slot : g_state.slots) {
				if (!slot.pending) {
					return std::addressof(slot);
				}
			}
			return nullptr;
		}
	}

	void Update(float a_dt, bool a_wanted)
	{
		if (!a_wanted || g_state.failed) {
			g_state.clock = kSampleIntervalSeconds;
			return;
		}
		const auto renderer = RE::BSGraphics::Renderer::GetSingleton();
		if (!renderer) {
			return;
		}
		const auto device = reinterpret_cast<ID3D11Device*>(renderer->GetRuntimeData().forwarder);
		const auto context = reinterpret_cast<ID3D11DeviceContext*>(renderer->GetRuntimeData().context);
		const auto source = SceneTexture();
		if (!device || !context || !source) {
			return;
		}

		if (source != g_state.source && !Build(device, source)) {
			return;
		}

		Harvest(context);

		g_state.clock += a_dt;
		if (g_state.clock < kSampleIntervalSeconds) {
			return;
		}
		const auto slot = FreeSlot();
		if (!slot) {
			return;
		}
		g_state.clock = 0.0f;
		context->CopySubresourceRegion(g_state.chain.Get(), 0, 0, 0, 0, source, 0, nullptr);
		context->GenerateMips(g_state.chainSrv.Get());
		context->CopySubresourceRegion(slot->staging.Get(), 0, 0, 0, 0, g_state.chain.Get(), g_state.level, nullptr);
		slot->pending = true;
	}

	bool Latest(Tint::Rgb& a_out)
	{
		if (!g_state.haveLatest) {
			return false;
		}
		a_out = g_state.latest;
		return true;
	}
}
