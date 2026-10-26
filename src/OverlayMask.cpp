// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Modding-Exception
// Copyright (c) 2026 HelmetOverlaysNG contributors. See COPYING and EXCEPTIONS.md.

#include "OverlayMask.h"

#include <d3dcompiler.h>

namespace HONG::OverlayMask
{
	namespace
	{
		constexpr char kVertexShader[] = R"(
float4 main(uint id : SV_VertexID) : SV_Position
{
	float2 uv = float2((id << 1) & 2, id & 2);
	return float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
})";

		// Maps the pixel to the overlay art and writes the near depth wherever the art is fully opaque.
		constexpr char kPixelShader[] = R"(
Texture2D<float> MaskTex : register(t0);
SamplerState PointSampler : register(s0);

cbuffer MaskCB : register(b0)
{
	float2 ViewSize;
	float2 Center;
	float2 Size;
	float2 ViewOrigin;
	float NearDepth;
	float3 Padding;
};

float main(float4 position : SV_Position) : SV_Depth
{
	float2 maskUV = ((position.xy - ViewOrigin) / ViewSize - Center) / Size + 0.5;
	if (any(maskUV < 0.0) || any(maskUV > 1.0))
		discard;
	if (MaskTex.SampleLevel(PointSampler, maskUV, 0) < 0.5)
		discard;
	return NearDepth;
})";

		struct Constants
		{
			float viewSize[2];
			float center[2];
			float size[2];
			float origin[2];
			float nearDepth;
			float padding[3];
		};

		struct Pending
		{
			ComPtr<ID3D11ShaderResourceView> mask;
			float center[2]{};
			float size[2]{};
			bool active{ false };
			bool vr{ false };
		};

		struct Pipeline
		{
			ComPtr<ID3D11VertexShader> vertexShader;
			ComPtr<ID3D11PixelShader> pixelShader;
			ComPtr<ID3D11Buffer> constants;
			ComPtr<ID3D11SamplerState> sampler;
			ComPtr<ID3D11DepthStencilState> depthWrite;
			ComPtr<ID3D11DepthStencilState> depthStencilWrite;
			ComPtr<ID3D11RasterizerState> rasterizer;
			ComPtr<ID3D11BlendState> noColor;
		};

		Pending g_pending;
		Pipeline g_pipeline;
		bool g_pipelineFailed{ false };

		ComPtr<ID3DBlob> Compile(const char* a_source, const char* a_target)
		{
			ComPtr<ID3DBlob> code;
			ComPtr<ID3DBlob> errors;
			if (FAILED(D3DCompile(a_source, std::strlen(a_source), nullptr, nullptr, nullptr, "main", a_target, D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, code.GetAddressOf(), errors.GetAddressOf()))) {
				logger::error("Overlay mask shader failed: {}", errors ? static_cast<const char*>(errors->GetBufferPointer()) : "no message");
				return nullptr;
			}
			return code;
		}

		bool CreatePipeline(ID3D11Device* a_device)
		{
			const auto vs = Compile(kVertexShader, "vs_5_0");
			const auto ps = Compile(kPixelShader, "ps_5_0");
			if (!vs || !ps) {
				return false;
			}
			if (FAILED(a_device->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(), nullptr, g_pipeline.vertexShader.GetAddressOf())) ||
				FAILED(a_device->CreatePixelShader(ps->GetBufferPointer(), ps->GetBufferSize(), nullptr, g_pipeline.pixelShader.GetAddressOf()))) {
				return false;
			}

			D3D11_BUFFER_DESC bufferDesc{};
			bufferDesc.ByteWidth = sizeof(Constants);
			bufferDesc.Usage = D3D11_USAGE_DYNAMIC;
			bufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
			bufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

			D3D11_SAMPLER_DESC samplerDesc{};
			samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
			samplerDesc.AddressU = samplerDesc.AddressV = samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
			samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;

			D3D11_DEPTH_STENCIL_DESC depthDesc{};
			depthDesc.DepthEnable = TRUE;
			depthDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
			depthDesc.DepthFunc = D3D11_COMPARISON_ALWAYS;

			// VR passes skip pixels the hidden-area mesh marked with stencil 0xFF; flat has no such consumer.
			D3D11_DEPTH_STENCIL_DESC stencilDesc = depthDesc;
			stencilDesc.StencilEnable = TRUE;
			stencilDesc.StencilReadMask = 0xFF;
			stencilDesc.StencilWriteMask = 0xFF;
			stencilDesc.FrontFace = { D3D11_STENCIL_OP_REPLACE, D3D11_STENCIL_OP_REPLACE, D3D11_STENCIL_OP_REPLACE, D3D11_COMPARISON_ALWAYS };
			stencilDesc.BackFace = stencilDesc.FrontFace;

			D3D11_RASTERIZER_DESC rasterDesc{};
			rasterDesc.FillMode = D3D11_FILL_SOLID;
			rasterDesc.CullMode = D3D11_CULL_NONE;

			D3D11_BLEND_DESC blendDesc{};
			blendDesc.RenderTarget[0].RenderTargetWriteMask = 0;

			return SUCCEEDED(a_device->CreateBuffer(&bufferDesc, nullptr, g_pipeline.constants.GetAddressOf())) &&
			       SUCCEEDED(a_device->CreateSamplerState(&samplerDesc, g_pipeline.sampler.GetAddressOf())) &&
			       SUCCEEDED(a_device->CreateDepthStencilState(&depthDesc, g_pipeline.depthWrite.GetAddressOf())) &&
			       SUCCEEDED(a_device->CreateDepthStencilState(&stencilDesc, g_pipeline.depthStencilWrite.GetAddressOf())) &&
			       SUCCEEDED(a_device->CreateRasterizerState(&rasterDesc, g_pipeline.rasterizer.GetAddressOf())) &&
			       SUCCEEDED(a_device->CreateBlendState(&blendDesc, g_pipeline.noColor.GetAddressOf()));
		}

		// The context state the draw overwrites, put back on scope exit.
		struct ScopedState
		{
			explicit ScopedState(ID3D11DeviceContext* a_context) :
				context(a_context)
			{
				context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, targets, depthView.GetAddressOf());
				context->OMGetDepthStencilState(depthState.GetAddressOf(), &stencilRef);
				context->OMGetBlendState(blendState.GetAddressOf(), blendFactor, &sampleMask);
				context->RSGetState(rasterizer.GetAddressOf());
				context->RSGetViewports(&viewportCount, viewports);
				context->IAGetInputLayout(layout.GetAddressOf());
				context->IAGetPrimitiveTopology(&topology);
				context->VSGetShader(vs.GetAddressOf(), nullptr, nullptr);
				context->PSGetShader(ps.GetAddressOf(), nullptr, nullptr);
				context->PSGetShaderResources(0, 1, srv.GetAddressOf());
				context->PSGetSamplers(0, 1, sampler.GetAddressOf());
				context->PSGetConstantBuffers(0, 1, constants.GetAddressOf());
			}

			~ScopedState()
			{
				context->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, targets, depthView.Get());
				context->OMSetDepthStencilState(depthState.Get(), stencilRef);
				context->OMSetBlendState(blendState.Get(), blendFactor, sampleMask);
				context->RSSetState(rasterizer.Get());
				context->RSSetViewports(viewportCount, viewports);
				context->IASetInputLayout(layout.Get());
				context->IASetPrimitiveTopology(topology);
				context->VSSetShader(vs.Get(), nullptr, 0);
				context->PSSetShader(ps.Get(), nullptr, 0);
				context->PSSetShaderResources(0, 1, srv.GetAddressOf());
				context->PSSetSamplers(0, 1, sampler.GetAddressOf());
				context->PSSetConstantBuffers(0, 1, constants.GetAddressOf());
				for (auto* target : targets) {
					if (target) {
						target->Release();
					}
				}
			}

			ScopedState(const ScopedState&) = delete;
			ScopedState& operator=(const ScopedState&) = delete;

			ID3D11DeviceContext* context;
			ID3D11RenderTargetView* targets[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT]{};
			ComPtr<ID3D11DepthStencilView> depthView;
			ComPtr<ID3D11DepthStencilState> depthState;
			UINT stencilRef{ 0 };
			ComPtr<ID3D11BlendState> blendState;
			FLOAT blendFactor[4]{};
			UINT sampleMask{ 0 };
			ComPtr<ID3D11RasterizerState> rasterizer;
			UINT viewportCount{ D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE };
			D3D11_VIEWPORT viewports[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE]{};
			ComPtr<ID3D11InputLayout> layout;
			D3D11_PRIMITIVE_TOPOLOGY topology{ D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED };
			ComPtr<ID3D11VertexShader> vs;
			ComPtr<ID3D11PixelShader> ps;
			ComPtr<ID3D11ShaderResourceView> srv;
			ComPtr<ID3D11SamplerState> sampler;
			ComPtr<ID3D11Buffer> constants;
		};

		// The engine records which depth target the pass renders to before the D3D binding catches up, so the
		// bound view cannot be trusted at this point (VR still has a shadow map bound).
		bool RendersToMainDepth()
		{
			const auto shadowState = RE::BSGraphics::RendererShadowState::GetSingleton();
			const auto index = REL::Module::IsVR() ? shadowState->GetVRRuntimeData().depthStencil : shadowState->GetRuntimeData().depthStencil;
			return index == static_cast<std::uint32_t>(RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN);
		}

		// Another renderer mod can run the depth buffer reversed (near = 1.0). It publishes the reversed projection
		// in the engine camera data, where the product of the depth and perspective terms is no longer positive.
		bool ProjectionReversed()
		{
			const auto shadowState = RE::BSGraphics::RendererShadowState::GetSingleton();
			const RE::BSGraphics::ViewData* view = REL::Module::IsVR() ?
			                                           reinterpret_cast<const RE::BSGraphics::ViewData*>(std::addressof(shadowState->GetVRRuntimeData().cameraData)) :
			                                           reinterpret_cast<const RE::BSGraphics::ViewData*>(std::addressof(shadowState->GetRuntimeData().cameraData));
			const auto* m = reinterpret_cast<const float*>(std::addressof(view->projMat));
			if (std::abs(m[15]) > 1e-4f) {
				return false;
			}
			const bool column = std::abs(std::abs(m[14]) - 1.0f) < 1e-3f;  // perspective term in m[3][2]
			const bool row = std::abs(std::abs(m[11]) - 1.0f) < 1e-3f;     // perspective term in m[2][3]
			if (!column && !row) {
				return false;
			}
			const float perspective = column && !row ? m[14] : m[11];
			return m[10] * perspective <= 0.0f;
		}

		// Logs when what the draw does changes, so a mask that never appears can be traced from the log.
		void Note(std::string_view a_state)
		{
			static std::string last;
			if (last != a_state) {
				last = a_state;
				logger::info("Overlay mask: {}", a_state);
			}
		}

		void Draw()
		{
			if (!g_pending.active || g_pipelineFailed) {
				Note("idle");
				return;
			}
			const auto renderer = RE::BSGraphics::Renderer::GetSingleton();
			if (!renderer) {
				return;
			}
			const auto& runtime = renderer->GetRuntimeData();
			auto* device = reinterpret_cast<ID3D11Device*>(runtime.forwarder);
			auto* context = reinterpret_cast<ID3D11DeviceContext*>(runtime.context);
			auto* depthView = reinterpret_cast<ID3D11DepthStencilView*>(
				renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN].views[0]);
			if (!device || !context || !depthView) {
				return;
			}
			if (!g_pipeline.vertexShader && !CreatePipeline(device)) {
				g_pipelineFailed = true;
				logger::error("Overlay mask pipeline could not be created; mask disabled.");
				return;
			}

			if (!RendersToMainDepth()) {
				return;  // a shadow map, reflection or cubemap pass
			}
			ScopedState restore(context);
			std::vector<D3D11_VIEWPORT> views;
			if (g_pending.vr) {
				// One side-by-side target: the same mask is drawn into each eye half.
				ComPtr<ID3D11Resource> resource;
				depthView->GetResource(resource.GetAddressOf());
				ComPtr<ID3D11Texture2D> texture;
				D3D11_TEXTURE2D_DESC desc{};
				if (!resource || FAILED(resource.As(&texture))) {
					return;
				}
				texture->GetDesc(&desc);
				const auto state = RE::BSGraphics::State::GetSingleton();
				const auto& stateData = state->GetRuntimeData();
				const float eyeWidth = static_cast<float>(desc.Width) * 0.5f;
				const float width = eyeWidth * stateData.dynamicResolutionWidthRatio;
				const float height = static_cast<float>(desc.Height) * stateData.dynamicResolutionHeightRatio;
				static bool logged = false;
				if (!logged) {
					logged = true;
					logger::info("Overlay mask VR target {}x{}, screen {}x{}, DRS {:.3f}x{:.3f}", desc.Width, desc.Height, state->screenWidth, state->screenHeight,
						stateData.dynamicResolutionWidthRatio, stateData.dynamicResolutionHeightRatio);
				}
				views = { { 0.0f, 0.0f, width, height, 0.0f, 1.0f }, { eyeWidth, 0.0f, width, height, 0.0f, 1.0f } };
			} else {
				const auto state = RE::BSGraphics::State::GetSingleton();
				const auto& stateData = state->GetRuntimeData();
				views = { { 0.0f, 0.0f, static_cast<float>(state->screenWidth) * stateData.dynamicResolutionWidthRatio, static_cast<float>(state->screenHeight) * stateData.dynamicResolutionHeightRatio, 0.0f, 1.0f } };
			}
			if (views[0].Width < 1.0f || views[0].Height < 1.0f) {
				return;
			}

			ID3D11ShaderResourceView* mask = g_pending.mask.Get();
			ID3D11SamplerState* sampler = g_pipeline.sampler.Get();
			ID3D11Buffer* constants = g_pipeline.constants.Get();
			const FLOAT blendFactor[4]{};

			context->OMSetRenderTargets(0, nullptr, depthView);
			context->OMSetDepthStencilState(g_pending.vr ? g_pipeline.depthStencilWrite.Get() : g_pipeline.depthWrite.Get(), 0xFF);
			context->OMSetBlendState(g_pipeline.noColor.Get(), blendFactor, D3D11_DEFAULT_SAMPLE_MASK);
			context->RSSetState(g_pipeline.rasterizer.Get());
			context->IASetInputLayout(nullptr);
			context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			context->VSSetShader(g_pipeline.vertexShader.Get(), nullptr, 0);
			context->PSSetShader(g_pipeline.pixelShader.Get(), nullptr, 0);
			context->PSSetShaderResources(0, 1, &mask);
			context->PSSetSamplers(0, 1, &sampler);
			context->PSSetConstantBuffers(0, 1, &constants);

			const bool reversed = ProjectionReversed();
			const float nearDepth = reversed ? 1.0f : 0.0f;
			Note(std::format("drawing{}, {} depth", g_pending.vr ? " (VR)" : "", reversed ? "reversed" : "standard"));
			for (const auto& viewport : views) {
				D3D11_MAPPED_SUBRESOURCE mapped{};
				if (FAILED(context->Map(g_pipeline.constants.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
					return;
				}
				*static_cast<Constants*>(mapped.pData) = Constants{
					{ viewport.Width, viewport.Height },
					{ g_pending.center[0], g_pending.center[1] },
					{ g_pending.size[0], g_pending.size[1] },
					{ viewport.TopLeftX, viewport.TopLeftY },
					nearDepth,
					{}
				};
				context->Unmap(g_pipeline.constants.Get(), 0);
				context->RSSetViewports(1, &viewport);
				context->Draw(3, 0);
			}
		}

		struct RenderWorldStart
		{
			static void thunk(RE::BSBatchRenderer* a_this, std::uint32_t a_startRange, std::uint32_t a_endRange, std::uint32_t a_flags, int a_group)
			{
				Draw();
				func(a_this, a_startRange, a_endRange, a_flags, a_group);
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};
	}

	void Install()
	{
		REL::Relocation<std::uintptr_t> target{ RELOCATION_ID(99938, 106583), OFFSET(0x8E, 0x84) };  // BSShaderAccumulator::FinishAccumulatingCommonPasses
		stl::write_thunk_call<RenderWorldStart>(target.address());
		logger::info("Overlay mask hook installed.");
	}

	void Publish(const OverlayController::MaskCandidate& a_candidate, ArtFit::Fraction a_fit, float a_vrCoverage)
	{
		g_pending.active = a_candidate.allowed && a_fit.x > 0.0f && a_fit.y > 0.0f;
		if (!g_pending.active) {
			g_pending.mask.Reset();
			return;
		}
		g_pending.mask = a_candidate.mask.srv;
		g_pending.vr = REL::Module::IsVR();
		if (g_pending.vr) {
			// The head-locked plane covers each eye field of view, scaled by the coverage.
			g_pending.center[0] = g_pending.center[1] = 0.5f;
			g_pending.size[0] = a_fit.x * a_vrCoverage;
			g_pending.size[1] = a_fit.y * a_vrCoverage;
			return;
		}
		g_pending.center[0] = 0.5f + a_candidate.offset.x;
		g_pending.center[1] = 0.5f + a_candidate.offset.y;
		g_pending.size[0] = a_fit.x * a_candidate.scale;
		g_pending.size[1] = a_fit.y * a_candidate.scale;
	}
}
