// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Modding-Exception
// Copyright (c) 2026 HelmetOverlaysNG contributors. See COPYING and EXCEPTIONS.md.

#pragma once

namespace HONG::TextureCache
{
	using Srv = ComPtr<ID3D11ShaderResourceView>;

	// Device used for SRV creation; set once D3D is captured.
	void SetDevice(ID3D11Device* a_device);

	// Returns the overlay SRV for a VFS path, loading it on first use. Holds a
	// small LRU (each 1080p DXT5 overlay is ~2 MB VRAM). Null on failure
	// (failures are remembered so a bad file is not re-read every equip).
	Srv Get(const std::string& a_path);

	// The overlay's fully opaque region as an R8 texture (255 = opaque), built on load when the opaque
	// mask setting is on. srv is null when it was not built or the file is not DXT3/DXT5.
	struct Mask
	{
		Srv srv;
		std::uint32_t width{ 0 };
		std::uint32_t height{ 0 };
		float share{ 0.0f };  // fraction of the overlay that is masked
	};
	Mask MaskFor(const std::string& a_path);

	// See-through fraction (0..1) of a path already passed to Get, or -1 if unknown
	// (not loaded, or not a DXT3/DXT5 file).
	float Openness(const std::string& a_path);
}
