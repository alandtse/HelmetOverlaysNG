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
}
