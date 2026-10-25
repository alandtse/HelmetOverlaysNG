-- HelmetOverlaysNG xmake build script
-- SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Modding-Exception

set_xmakever("2.8.2")

set_config("rex_ini", true)

includes("lib/commonlibsse-ng")

set_project("HelmetOverlaysNG")
set_license("GPL-3.0")

local version = "0.1.0"
local ver = version:split("%.")
set_version(version)

set_languages("c++23")
set_warnings("allextra")
set_policy("package.requires_lock", true)
add_rules("mode.debug", "mode.releasedbg")
set_defaultmode("releasedbg")
add_rules("plugin.vsxmake.autoupdate")

-- Local package repo: pins the imgui-vr-helper client API to an exact
-- upstream commit so it is pulled at build time instead of vendored.
add_repositories("hong-pkgs xmake-pkgs")

add_requires("imgui", { configs = { dx11 = true, win32 = true } })
add_requires("simpleini") -- Data/SKSE/Plugins/HelmetOverlaysNG.ini
add_requires("nlohmann_json") -- imgui-vr-helper client SDK payloads
add_requires("directxtk") -- DDSTextureLoader: overlay .dds -> SRV
add_requires("imgui-vr-helper-api 1.5.2") -- VR head-locked HUD plane client API (LGPL, source-form)
add_requires("skse-menu-framework-api 3.7.0") -- optional in-game settings host (LGPL header)
add_requires("catch2") -- unit tests (HelmetOverlaysNG-Tests target only)

-- The SMF-hosted settings UI lives in its own PCH-free static lib: the SMF
-- client header's cimgui-style typedefs cannot coexist with the real imgui.h
-- that the main target's PCH pulls in.
target("HelmetOverlaysNG-UI")
set_kind("static")
set_warnings("all") -- third-party SMF/CommonLib headers compile in this TU without the PCH's suppressions
add_deps("commonlibsse-ng")
add_packages("skse-menu-framework-api")
add_defines("_SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING") -- SMF header uses std::wstring_convert
add_defines("UNICODE", "_UNICODE") -- SMF header passes a wide string to GetModuleHandle
add_files("src/UI.cpp")
add_includedirs("src")
target_end()

target("HelmetOverlaysNG")
add_deps("commonlibsse-ng", "HelmetOverlaysNG-UI")
add_packages("imgui", "simpleini", "nlohmann_json", "directxtk", "imgui-vr-helper-api")

set_basename("HelmetOverlaysNG")

-- Dev override: build against a local imgui-vr-helper checkout's api/ folder
-- (e.g. an unreleased interface) instead of the pinned package. Unset for CI.
local localHelperApi = os.getenv("HONG_VRHELPER_API")
if localHelperApi and localHelperApi ~= "" then
    add_includedirs(localHelperApi, { public = false })
end

add_shflags("/DEBUG", { force = true })

add_defines("HONG_VERSION_MAJOR=" .. ver[1], "HONG_VERSION_MINOR=" .. ver[2], "HONG_VERSION_PATCH=" .. ver[3])

add_rules("commonlibsse-ng.plugin", {
    name = "HelmetOverlaysNG",
    author = "alandtse",
    description = "First-person helmet overlays drawn natively (flat and VR), replacing the SWF widget",
})

add_files("src/**.cpp|UI.cpp")
add_headerfiles("src/**.h")

add_includedirs("src")
add_syslinks("d3dcompiler")
set_pcxxheader("src/pch.h")

-- Auto-deploy on build: SkyrimVRPluginTargets / SkyrimPluginTargets
-- (semicolon-separated Data folders or mod-manager mod folders), else
-- SkyrimVRPath (install root or Data folder). No-op when none is set.
after_build(function(target)
    local function to_data_folder(p)
        p = p:trim()
        if os.isdir(path.join(p, "Data")) then
            return path.join(p, "Data")
        end
        return p
    end

    local function nonblank(v)
        if v and v:trim() ~= "" then
            return v
        end
        return nil
    end

    local function collect_targets()
        local explicit = nonblank(os.getenv("SkyrimVRPluginTargets")) or nonblank(os.getenv("SkyrimPluginTargets"))
        if explicit then
            local out = {}
            for _, dir in ipairs(explicit:split(";")) do
                dir = dir:trim()
                if dir ~= "" then
                    table.insert(out, dir)
                end
            end
            return out
        end
        local vr_root = os.getenv("SkyrimVRPath")
        if vr_root and vr_root ~= "" then
            return { to_data_folder(vr_root) }
        end
        return {}
    end

    local targets = collect_targets()
    if #targets == 0 then
        print("No deploy target (set SkyrimVRPath, SkyrimVRPluginTargets, or SkyrimPluginTargets)")
        return
    end

    local dll = target:targetfile()
    local pdb = target:symbolfile()
    for _, dir in ipairs(targets) do
        local dest = path.join(dir, "SKSE", "Plugins")
        os.mkdir(dest)
        -- A running game holds the DLL/PDB locks; skip with a warning.
        try({
            function()
                os.cp(dll, dest)
                if os.isfile(pdb) then
                    os.cp(pdb, dest)
                end
                print("Deployed DLL to " .. dest)
            end,
            catch({
                function()
                    print("Skipped DLL for " .. dest .. " (game running?)")
                end,
            }),
        })
    end
end)

-- Headless unit tests for the dependency-free logic (no CommonLib / SKSE /
-- Skyrim runtime). Defined last so it does not inherit the plugin's deploy
-- hook. Not built by a plain `xmake`:
--   xmake build HelmetOverlaysNG-Tests && xmake run HelmetOverlaysNG-Tests
target("HelmetOverlaysNG-Tests")
set_kind("binary")
set_default(false)
set_group("tests")
add_packages("catch2")
add_includedirs("src")
add_files("tests/**.cpp", "src/ArtMetrics.cpp", "src/HelmetProfile.cpp")
