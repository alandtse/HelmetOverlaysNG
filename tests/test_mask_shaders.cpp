// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Modding-Exception
// Copyright (c) 2026 HelmetOverlaysNG contributors. See COPYING and EXCEPTIONS.md.

#include <catch2/catch_test_macros.hpp>

#include <MaskShaders.h>

#include <d3dcompiler.h>

#include <cstddef>
#include <cstdio>
#include <string>
#include <string_view>

namespace
{
	template <class T>
	struct ComPtrLike
	{
		T* ptr{ nullptr };
		~ComPtrLike()
		{
			if (ptr)
				ptr->Release();
		}
		ComPtrLike() = default;
		ComPtrLike(const ComPtrLike&) = delete;
		ComPtrLike& operator=(const ComPtrLike&) = delete;
		T** operator&() { return &ptr; }
		T* operator->() const { return ptr; }
		T* Get() const { return ptr; }
		explicit operator bool() const { return ptr != nullptr; }
	};

	bool Compile(std::string_view a_source, const char* a_target, ComPtrLike<ID3DBlob>& a_blob, std::string& a_error)
	{
		ComPtrLike<ID3DBlob> errors;
		const HRESULT hr = D3DCompile(a_source.data(), a_source.size(), "mask", nullptr, nullptr,
			"main", a_target, D3DCOMPILE_OPTIMIZATION_LEVEL1, 0, &a_blob, &errors);
		if (FAILED(hr)) {
			if (errors) {
				a_error.assign(static_cast<const char*>(errors->GetBufferPointer()), errors->GetBufferSize());
			}
			return false;
		}
		return true;
	}
}

TEST_CASE("the mask shaders compile", "[mask]")
{
	ComPtrLike<ID3DBlob> vs;
	std::string error;
	REQUIRE(Compile(HONG::OverlayMask::Shaders::kVertexShader, "vs_5_0", vs, error));
	REQUIRE(vs);

	ComPtrLike<ID3DBlob> ps;
	error.clear();
	REQUIRE(Compile(HONG::OverlayMask::Shaders::kPixelShader, "ps_5_0", ps, error));
	REQUIRE(ps);
}

TEST_CASE("the stamp shader's cbuffer matches the struct it is filled with", "[mask]")
{
	using HONG::OverlayMask::Shaders::Constants;

	ComPtrLike<ID3DBlob> ps;
	std::string error;
	REQUIRE(Compile(HONG::OverlayMask::Shaders::kPixelShader, "ps_5_0", ps, error));

	ComPtrLike<ID3D11ShaderReflection> reflect;
	REQUIRE(SUCCEEDED(D3DReflect(ps->GetBufferPointer(), ps->GetBufferSize(), __uuidof(ID3D11ShaderReflection), reinterpret_cast<void**>(&reflect))));

	D3D11_SHADER_DESC desc{};
	REQUIRE(SUCCEEDED(reflect->GetDesc(&desc)));
	REQUIRE(desc.ConstantBuffers == 1);

	ID3D11ShaderReflectionConstantBuffer* cb = reflect->GetConstantBufferByIndex(0);
	REQUIRE(cb != nullptr);

	D3D11_SHADER_BUFFER_DESC cbDesc{};
	REQUIRE(SUCCEEDED(cb->GetDesc(&cbDesc)));
	REQUIRE(std::string_view(cbDesc.Name) == "MaskCB");

	REQUIRE(cbDesc.Size == sizeof(Constants));
	REQUIRE(cbDesc.Variables == 7);

	const auto check = [&](const char* a_name, std::size_t a_offset, std::size_t a_size) {
		ID3D11ShaderReflectionVariable* var = cb->GetVariableByName(a_name);
		REQUIRE(var != nullptr);
		D3D11_SHADER_VARIABLE_DESC varDesc{};
		REQUIRE(SUCCEEDED(var->GetDesc(&varDesc)));
		REQUIRE(varDesc.StartOffset == a_offset);
		REQUIRE(varDesc.Size == a_size);
	};

	check("ViewSize", offsetof(Constants, viewSize), sizeof(Constants::viewSize));
	check("Center", offsetof(Constants, center), sizeof(Constants::center));
	check("Size", offsetof(Constants, size), sizeof(Constants::size));
	check("ViewOrigin", offsetof(Constants, origin), sizeof(Constants::origin));
	check("NearDepth", offsetof(Constants, nearDepth), sizeof(Constants::nearDepth));
	check("InsetUV", offsetof(Constants, inset), sizeof(Constants::inset));
	check("Padding", offsetof(Constants, padding), sizeof(Constants::padding));
}

TEST_CASE("the shader strings are extractable from a binary, so the shipped-artefact check has something to find", "[mask]")
{
	using namespace std::string_view_literals;
	constexpr std::string_view vs{ HONG::OverlayMask::Shaders::kVertexShader };
	constexpr std::string_view ps{ HONG::OverlayMask::Shaders::kPixelShader };

	// No embedded NULs, or a byte search would stop early and match a prefix.
	REQUIRE(vs.find(static_cast<char>(0)) == std::string_view::npos);
	REQUIRE(ps.find(static_cast<char>(0)) == std::string_view::npos);

	REQUIRE(vs.size() > 128);
	REQUIRE(ps.size() > 640);

	REQUIRE(ps[0] == 10);
	REQUIRE(ps.substr(1).starts_with("Texture2D<float> MaskTex"sv));
	REQUIRE(ps.find("SV_Depth"sv) != std::string_view::npos);
}

TEST_CASE("the stamp shader's comparisons are not silently inverted", "[mask]")
{
	using namespace std::string_view_literals;
	constexpr std::string_view src{ HONG::OverlayMask::Shaders::kPixelShader };

	REQUIRE(src.find("if (sampled < 0.5)"sv) != std::string_view::npos);
	REQUIRE(src.find("if (sampled <= 0.5)"sv) == std::string_view::npos);
	REQUIRE(src.find("if (sampled > 0.5)"sv) == std::string_view::npos);
	REQUIRE(src.find("if (sampled >= 0.5)"sv) == std::string_view::npos);

	// The UV bounds test must reject outside the art, so the comparison direction matters too.
	REQUIRE(src.find("any(maskUV < 0.0) || any(maskUV > 1.0)"sv) != std::string_view::npos);

	// The erosion must be a min of taps; a max would stamp where the art is transparent.
	REQUIRE(src.find("min(sampled,"sv) != std::string_view::npos);
	REQUIRE(src.find("max(sampled,"sv) == std::string_view::npos);

	REQUIRE(src.find("return NearDepth;"sv) != std::string_view::npos);
	REQUIRE(src.find("return 1.0;"sv) == std::string_view::npos);
	REQUIRE(src.find("return 0.0;"sv) == std::string_view::npos);
}

TEST_CASE("the stamp shader declares the outputs and taps it is supposed to", "[mask]")
{
	ComPtrLike<ID3DBlob> ps;
	std::string error;
	REQUIRE(Compile(HONG::OverlayMask::Shaders::kPixelShader, "ps_5_0", ps, error));

	ComPtrLike<ID3D11ShaderReflection> reflect;
	REQUIRE(SUCCEEDED(D3DReflect(ps->GetBufferPointer(), ps->GetBufferSize(), __uuidof(ID3D11ShaderReflection), reinterpret_cast<void**>(&reflect))));

	D3D11_SHADER_DESC desc{};
	REQUIRE(SUCCEEDED(reflect->GetDesc(&desc)));

	REQUIRE(desc.OutputParameters == 1);
	D3D11_SIGNATURE_PARAMETER_DESC sig{};
	REQUIRE(SUCCEEDED(reflect->GetOutputParameterDesc(0, &sig)));
	REQUIRE(std::string_view(sig.SemanticName) == "SV_Depth");

	// The mask texture and its sampler must be bound where the plugin binds them.
	D3D11_SHADER_INPUT_BIND_DESC bind{};
	REQUIRE(SUCCEEDED(reflect->GetResourceBindingDescByName("MaskTex", &bind)));
	REQUIRE(bind.Type == D3D_SIT_TEXTURE);
	REQUIRE(bind.BindPoint == 0);
	REQUIRE(SUCCEEDED(reflect->GetResourceBindingDescByName("PointSampler", &bind)));
	REQUIRE(bind.Type == D3D_SIT_SAMPLER);
	REQUIRE(bind.BindPoint == 0);

	REQUIRE(desc.TextureLoadInstructions == 5);
	REQUIRE(desc.TextureNormalInstructions == 0);
}
