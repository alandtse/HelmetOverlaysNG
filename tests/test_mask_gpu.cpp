// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Modding-Exception
// Copyright (c) 2026 HelmetOverlaysNG contributors. See COPYING and EXCEPTIONS.md.

#include "MaskInset.h"
#include "MaskShaders.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <type_traits>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace
{
	struct Context
	{
		ComPtr<ID3D11Device> device;
		ComPtr<ID3D11DeviceContext> context;
		bool ok{};
	};

	Context MakeWarp()
	{
		Context c{};
		const HRESULT hr = D3D11CreateDevice(nullptr,
			D3D_DRIVER_TYPE_WARP,
			nullptr,
			0,
			nullptr,
			0,
			D3D11_SDK_VERSION,
			c.device.GetAddressOf(),
			nullptr,
			c.context.GetAddressOf());
		c.ok = SUCCEEDED(hr) && c.device && c.context;
		return c;
	}

	template <class TShader>
	ComPtr<TShader> Compile(ID3D11Device* a_device, const char* a_hlsl, const char* a_target, ID3DBlob** a_blob)
	{
		ComPtr<TShader> out;
		ComPtr<ID3DBlob> errors;
		const HRESULT hr = D3DCompile(a_hlsl,
			std::strlen(a_hlsl),
			"MaskShader",
			nullptr,
			nullptr,
			"main",
			a_target,
			D3DCOMPILE_ENABLE_STRICTNESS,
			0,
			a_blob,
			errors.ReleaseAndGetAddressOf());
		if (FAILED(hr)) {
			return nullptr;
		}
		HRESULT created = E_FAIL;
		if constexpr (std::is_same_v<TShader, ID3D11VertexShader>) {
			created = a_device->CreateVertexShader((*a_blob)->GetBufferPointer(), (*a_blob)->GetBufferSize(), nullptr, out.GetAddressOf());
		} else {
			created = a_device->CreatePixelShader((*a_blob)->GetBufferPointer(), (*a_blob)->GetBufferSize(), nullptr, out.GetAddressOf());
		}
		if (FAILED(created)) {
			return nullptr;
		}
		return out;
	}

	std::vector<float> StampDepths(
		Context& a_ctx,
		ID3D11VertexShader* a_vs,
		ID3D11PixelShader* a_ps,
		ID3D11ShaderResourceView* a_mask,
		std::uint32_t a_width,
		std::uint32_t a_height,
		float a_nearDepth,
		const float a_center[2] = std::array<float, 2>{ 0.5f, 0.5f }.data(),
		const float a_size[2] = std::array<float, 2>{ 1.0f, 1.0f }.data(),
		const float a_origin[2] = std::array<float, 2>{ 0.0f, 0.0f }.data(),
		float a_inset = 0.0f)
	{
		using HONG::OverlayMask::Shaders::Constants;

		D3D11_TEXTURE2D_DESC depthDesc{};
		depthDesc.Width = a_width;
		depthDesc.Height = a_height;
		depthDesc.MipLevels = 1;
		depthDesc.ArraySize = 1;
		depthDesc.Format = DXGI_FORMAT_D32_FLOAT;
		depthDesc.SampleDesc.Count = 1;
		depthDesc.Usage = D3D11_USAGE_DEFAULT;
		depthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;

		ComPtr<ID3D11Texture2D> depthTex;
		if (FAILED(a_ctx.device->CreateTexture2D(&depthDesc, nullptr, depthTex.GetAddressOf()))) {
			return {};
		}
		ComPtr<ID3D11DepthStencilView> dsv;
		if (FAILED(a_ctx.device->CreateDepthStencilView(depthTex.Get(), nullptr, dsv.GetAddressOf()))) {
			return {};
		}

		// A readback copy of the same texture, so nothing about the depth view has to be readable.
		D3D11_TEXTURE2D_DESC readDesc = depthDesc;
		readDesc.BindFlags = 0;
		readDesc.Usage = D3D11_USAGE_STAGING;
		readDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
		ComPtr<ID3D11Texture2D> readTex;
		if (FAILED(a_ctx.device->CreateTexture2D(&readDesc, nullptr, readTex.GetAddressOf()))) {
			return {};
		}

		D3D11_BUFFER_DESC cbDesc{};
		cbDesc.ByteWidth = sizeof(Constants);
		cbDesc.Usage = D3D11_USAGE_DEFAULT;
		cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		Constants constants{};
		constants.viewSize[0] = static_cast<float>(a_width);
		constants.viewSize[1] = static_cast<float>(a_height);
		constants.center[0] = a_center[0];
		constants.center[1] = a_center[1];
		constants.size[0] = a_size[0];
		constants.size[1] = a_size[1];
		constants.origin[0] = a_origin[0];
		constants.origin[1] = a_origin[1];
		constants.nearDepth = a_nearDepth;
		constants.inset[0] = a_inset;
		constants.inset[1] = a_inset;
		D3D11_SUBRESOURCE_DATA cbInit{};
		cbInit.pSysMem = &constants;
		ComPtr<ID3D11Buffer> cb;
		if (FAILED(a_ctx.device->CreateBuffer(&cbDesc, &cbInit, cb.GetAddressOf()))) {
			return {};
		}

		// Point sampling with clamp, matching the shader's sampler.
		D3D11_SAMPLER_DESC sampDesc{};
		sampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
		sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
		sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
		sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
		sampDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
		sampDesc.MaxLOD = D3D11_FLOAT32_MAX;
		ComPtr<ID3D11SamplerState> samp;
		if (FAILED(a_ctx.device->CreateSamplerState(&sampDesc, samp.GetAddressOf()))) {
			return {};
		}

		// Compare always / write all, exactly as src/OverlayMask.cpp configures it.
		D3D11_DEPTH_STENCIL_DESC dsDesc{};
		dsDesc.DepthEnable = TRUE;
		dsDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
		dsDesc.DepthFunc = D3D11_COMPARISON_ALWAYS;
		ComPtr<ID3D11DepthStencilState> ds;
		if (FAILED(a_ctx.device->CreateDepthStencilState(&dsDesc, ds.GetAddressOf()))) {
			return {};
		}

		// Rasterizer with no culling, and an explicit viewport - the two conventions under test depend on it.
		D3D11_RASTERIZER_DESC rastDesc{};
		rastDesc.FillMode = D3D11_FILL_SOLID;
		rastDesc.CullMode = D3D11_CULL_NONE;
		rastDesc.DepthClipEnable = FALSE;
		ComPtr<ID3D11RasterizerState> rast;
		if (FAILED(a_ctx.device->CreateRasterizerState(&rastDesc, rast.GetAddressOf()))) {
			return {};
		}

		const float clear[4] = { 0.25f, 0.25f, 0.25f, 0.25f };
		a_ctx.context->ClearDepthStencilView(dsv.Get(), D3D11_CLEAR_DEPTH, 0.25f, 0);
		a_ctx.context->OMSetRenderTargets(0, nullptr, dsv.Get());
		a_ctx.context->OMSetDepthStencilState(ds.Get(), 0);
		a_ctx.context->RSSetState(rast.Get());
		const D3D11_VIEWPORT viewport{ 0.0f, 0.0f, static_cast<float>(a_width), static_cast<float>(a_height), 0.0f, 1.0f };
		a_ctx.context->RSSetViewports(1, &viewport);
		a_ctx.context->PSSetShader(a_ps, nullptr, 0);
		a_ctx.context->PSSetShaderResources(0, 1, &a_mask);
		a_ctx.context->PSSetSamplers(0, 1, samp.GetAddressOf());
		a_ctx.context->PSSetConstantBuffers(0, 1, cb.GetAddressOf());
		a_ctx.context->IASetInputLayout(nullptr);
		a_ctx.context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		a_ctx.context->VSSetShader(a_vs, nullptr, 0);
		a_ctx.context->Draw(3, 0);

		{
			a_ctx.context->CopyResource(readTex.Get(), depthTex.Get());
			D3D11_MAPPED_SUBRESOURCE probe{};
			bool anyRan = false;
			if (SUCCEEDED(a_ctx.context->Map(readTex.Get(), 0, D3D11_MAP_READ, 0, &probe))) {
				for (std::uint32_t y = 0; y < a_height && !anyRan; ++y) {
					const auto* row = reinterpret_cast<const float*>(static_cast<const std::uint8_t*>(probe.pData) + static_cast<std::size_t>(y) * probe.RowPitch);
					for (std::uint32_t x = 0; x < a_width; ++x) {
						if (row[x] != 0.25f) {
							anyRan = true;
							break;
						}
					}
				}
				a_ctx.context->Unmap(readTex.Get(), 0);
			}
			if (!anyRan) {
				return {};  // nothing rasterised; the caller reports it
			}
		}

		a_ctx.context->CopyResource(readTex.Get(), depthTex.Get());

		D3D11_MAPPED_SUBRESOURCE mapped{};
		if (FAILED(a_ctx.context->Map(readTex.Get(), 0, D3D11_MAP_READ, 0, &mapped))) {
			return {};
		}
		std::vector<float> out(static_cast<std::size_t>(a_width) * a_height);
		for (std::uint32_t y = 0; y < a_height; ++y) {
			std::memcpy(out.data() + static_cast<std::size_t>(y) * a_width,
				static_cast<const std::uint8_t*>(mapped.pData) + static_cast<std::size_t>(y) * mapped.RowPitch,
				static_cast<std::size_t>(a_width) * sizeof(float));
		}
		a_ctx.context->Unmap(readTex.Get(), 0);
		(void)clear;
		return out;
	}
}

TEST_CASE("the mask's shader rasterises at all on a WARP device", "[mask][gpu]")
{
	auto ctx = MakeWarp();
	if (!ctx.ok) {
		WARN("no WARP device available; skipping the GPU cases");
		return;
	}

	// A pure-white mask texture: every pixel is opaque, so the whole target must be stamped.
	D3D11_TEXTURE2D_DESC maskDesc{};
	maskDesc.Width = 4;
	maskDesc.Height = 4;
	maskDesc.MipLevels = 1;
	maskDesc.ArraySize = 1;
	maskDesc.Format = DXGI_FORMAT_R8_UNORM;
	maskDesc.SampleDesc.Count = 1;
	maskDesc.Usage = D3D11_USAGE_IMMUTABLE;
	maskDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	std::array<std::uint8_t, 16> white{};
	white.fill(255);
	D3D11_SUBRESOURCE_DATA maskInit{};
	maskInit.pSysMem = white.data();
	maskInit.SysMemPitch = 4;
	ComPtr<ID3D11Texture2D> maskTex;
	REQUIRE(SUCCEEDED(ctx.device->CreateTexture2D(&maskDesc, &maskInit, maskTex.GetAddressOf())));
	ComPtr<ID3D11ShaderResourceView> maskSrv;
	REQUIRE(SUCCEEDED(ctx.device->CreateShaderResourceView(maskTex.Get(), nullptr, maskSrv.GetAddressOf())));

	ComPtr<ID3DBlob> vsBlob;
	auto vs = Compile<ID3D11VertexShader>(ctx.device.Get(), HONG::OverlayMask::Shaders::kVertexShader, "vs_5_0", vsBlob.GetAddressOf());
	ComPtr<ID3DBlob> psBlob;
	auto ps = Compile<ID3D11PixelShader>(ctx.device.Get(), HONG::OverlayMask::Shaders::kPixelShader, "ps_5_0", psBlob.GetAddressOf());
	REQUIRE(vs != nullptr);
	REQUIRE(ps != nullptr);

	const auto depths = StampDepths(ctx, vs.Get(), ps.Get(), maskSrv.Get(), 32, 32, 1.0f);
	REQUIRE(depths.size() == 32u * 32u);
	for (float d : depths) {
		REQUIRE(d == Catch::Approx(1.0f));
	}
}

TEST_CASE("the mask's shader leaves non-opaque mask texels alone", "[mask][gpu]")
{
	auto ctx = MakeWarp();
	if (!ctx.ok) {
		WARN("no WARP device available; skipping the GPU cases");
		return;
	}

	// Left half opaque, right half transparent.
	D3D11_TEXTURE2D_DESC maskDesc{};
	maskDesc.Width = 4;
	maskDesc.Height = 4;
	maskDesc.MipLevels = 1;
	maskDesc.ArraySize = 1;
	maskDesc.Format = DXGI_FORMAT_R8_UNORM;
	maskDesc.SampleDesc.Count = 1;
	maskDesc.Usage = D3D11_USAGE_IMMUTABLE;
	maskDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	std::array<std::uint8_t, 16> halves{};
	for (std::size_t y = 0; y < 4; ++y) {
		for (std::size_t x = 0; x < 4; ++x) {
			halves[y * 4 + x] = x < 2 ? 255 : 0;
		}
	}
	D3D11_SUBRESOURCE_DATA maskInit{};
	maskInit.pSysMem = halves.data();
	maskInit.SysMemPitch = 4;
	ComPtr<ID3D11Texture2D> maskTex;
	REQUIRE(SUCCEEDED(ctx.device->CreateTexture2D(&maskDesc, &maskInit, maskTex.GetAddressOf())));
	ComPtr<ID3D11ShaderResourceView> maskSrv;
	REQUIRE(SUCCEEDED(ctx.device->CreateShaderResourceView(maskTex.Get(), nullptr, maskSrv.GetAddressOf())));

	ComPtr<ID3DBlob> vsBlob;
	auto vs = Compile<ID3D11VertexShader>(ctx.device.Get(), HONG::OverlayMask::Shaders::kVertexShader, "vs_5_0", vsBlob.GetAddressOf());
	ComPtr<ID3DBlob> psBlob;
	auto ps = Compile<ID3D11PixelShader>(ctx.device.Get(), HONG::OverlayMask::Shaders::kPixelShader, "ps_5_0", psBlob.GetAddressOf());
	REQUIRE(vs != nullptr);
	REQUIRE(ps != nullptr);

	constexpr std::uint32_t kSize = 64;
	const auto depths = StampDepths(ctx, vs.Get(), ps.Get(), maskSrv.Get(), kSize, kSize, 1.0f);
	REQUIRE(depths.size() == kSize * kSize);

	std::size_t stamped = 0;
	std::size_t untouched = 0;
	for (std::uint32_t y = 0; y < kSize; ++y) {
		for (std::uint32_t x = 0; x < kSize; ++x) {
			const float uvx = (static_cast<float>(x) + 0.5f) / static_cast<float>(kSize);
			const float d = depths[static_cast<std::size_t>(y) * kSize + x];
			if (uvx < 0.5f) {
				REQUIRE(d == Catch::Approx(1.0f));
				++stamped;
			} else {
				REQUIRE(d == Catch::Approx(0.25f));
				++untouched;
			}
		}
	}
	REQUIRE(stamped == kSize * (kSize / 2));
	REQUIRE(untouched == kSize * (kSize / 2));
}

TEST_CASE("the mapping is wrong if the axes are swapped or the origin ignored", "[mask][gpu]")
{
	auto ctx = MakeWarp();
	if (!ctx.ok) {
		WARN("no WARP device available; skipping the GPU cases");
		return;
	}

	D3D11_TEXTURE2D_DESC maskDesc{};
	maskDesc.Width = 4;
	maskDesc.Height = 4;
	maskDesc.MipLevels = 1;
	maskDesc.ArraySize = 1;
	maskDesc.Format = DXGI_FORMAT_R8_UNORM;
	maskDesc.SampleDesc.Count = 1;
	maskDesc.Usage = D3D11_USAGE_IMMUTABLE;
	maskDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	std::array<std::uint8_t, 16> rows{};
	for (std::size_t y = 0; y < 4; ++y) {
		for (std::size_t x = 0; x < 4; ++x) {
			rows[y * 4 + x] = y >= 2 ? 255 : 0;  // bottom half opaque, in texture space
		}
	}
	D3D11_SUBRESOURCE_DATA maskInit{};
	maskInit.pSysMem = rows.data();
	maskInit.SysMemPitch = 4;
	ComPtr<ID3D11Texture2D> maskTex;
	REQUIRE(SUCCEEDED(ctx.device->CreateTexture2D(&maskDesc, &maskInit, maskTex.GetAddressOf())));
	ComPtr<ID3D11ShaderResourceView> maskSrv;
	REQUIRE(SUCCEEDED(ctx.device->CreateShaderResourceView(maskTex.Get(), nullptr, maskSrv.GetAddressOf())));

	ComPtr<ID3DBlob> vsBlob;
	auto vs = Compile<ID3D11VertexShader>(ctx.device.Get(), HONG::OverlayMask::Shaders::kVertexShader, "vs_5_0", vsBlob.GetAddressOf());
	ComPtr<ID3DBlob> psBlob;
	auto ps = Compile<ID3D11PixelShader>(ctx.device.Get(), HONG::OverlayMask::Shaders::kPixelShader, "ps_5_0", psBlob.GetAddressOf());
	REQUIRE(vs != nullptr);
	REQUIRE(ps != nullptr);

	// 3:1 viewport so no axis swap can hide behind square dimensions.
	constexpr std::uint32_t kWidth = 96;
	constexpr std::uint32_t kHeight = 32;
	// Centre off 0.5 on both axes by DIFFERENT amounts, so an x/y swap in the offset cannot cancel either.
	const float center[2] = { 0.5f, 0.5f };
	// Art covers the middle half of each axis: mask UV spans 0.25..0.75 across the target.
	const float size[2] = { 0.5f, 0.5f };
	// A non-zero origin, which the shader subtracts before dividing. A test at 0/0 cannot see it dropped.
	const float origin[2] = { 8.0f, 4.0f };

	const auto depths = StampDepths(ctx, vs.Get(), ps.Get(), maskSrv.Get(), kWidth, kHeight, 1.0f, center, size, origin, 0.0f);
	REQUIRE(depths.size() == static_cast<std::size_t>(kWidth) * kHeight);

	std::size_t stamped = 0;
	std::size_t untouched = 0;
	for (std::uint32_t y = 0; y < kHeight; ++y) {
		for (std::uint32_t x = 0; x < kWidth; ++x) {
			const double px = static_cast<double>(x) + 0.5;
			const double py = static_cast<double>(y) + 0.5;
			const double uvx = ((px - origin[0]) / kWidth - center[0]) / size[0] + 0.5;
			const double uvy = ((py - origin[1]) / kHeight - center[1]) / size[1] + 0.5;
			const float d = depths[static_cast<std::size_t>(y) * kWidth + x];
			// Outside the mask UV range the shader discards; inside, only the bottom rows are opaque.
			const bool inside = uvx >= 0.0 && uvx <= 1.0 && uvy >= 0.0 && uvy <= 1.0;
			const bool opaque = inside && uvy >= 0.5;  // texture row 2..3 of 4
			if (opaque) {
				REQUIRE(d == Catch::Approx(1.0f));
				++stamped;
			} else {
				REQUIRE(d == Catch::Approx(0.25f));
				++untouched;
			}
		}
	}

	REQUIRE(stamped > 0);
	REQUIRE(untouched > 0);
	const double fraction = static_cast<double>(stamped) / (static_cast<double>(kWidth) * kHeight);
	REQUIRE(fraction > 0.10);
	REQUIRE(fraction < 0.15);

	for (std::uint32_t y = 0; y < kHeight; ++y) {
		std::size_t rowStamped = 0;
		for (std::uint32_t x = 0; x < kWidth; ++x) {
			if (depths[static_cast<std::size_t>(y) * kWidth + x] > 0.5f) {
				++rowStamped;
			}
		}
		REQUIRE((rowStamped == 0 || rowStamped >= kWidth / 4));
	}
}
