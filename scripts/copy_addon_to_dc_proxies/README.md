# Copy Addon to DC Proxies

Pure CMD scripts that copy the Display Commander addon to the **proxy DLL names that DC supports** (the names defined in `proxy_dll/exports.def`), so the addon can be loaded when a game loads any of those DLLs.

## Proxy names (11)

`d3d9.dll`, `d3d11.dll`, `d3d12.dll`, `ddraw.dll`, `dinput8.dll`, `opengl32.dll`, `dxgi.dll`, `version.dll`, `winmm.dll`, `dbghelp.dll`, `vulkan-1.dll`.

## Prerequisites

- **64-bit**: `zzz_display_commander.addon64` in the target folder
- **32-bit**: `zzz_display_commander.addon32` in the target folder

Build the project first or copy the addon into the target folder.

## Usage

Run from the folder that contains the addon, or pass that folder as the first argument.

```cmd
REM 64-bit: copy addon to proxy DLLs in current directory
copy_addon_to_dc_proxies_64bit.cmd

REM 64-bit: copy addon to proxy DLLs in a specific folder
copy_addon_to_dc_proxies_64bit.cmd "C:\Games\MyGame"

REM 32-bit
copy_addon_to_dc_proxies_32bit.cmd
copy_addon_to_dc_proxies_32bit.cmd "C:\Games\MyGame"
```

## Difference from try_all_proxies

- **try_all_proxies** (Python): copies the addon to *every* Wine/Proton proxy DLL name (hundreds) to discover which one a game loads.
- **copy_addon_to_dc_proxies** (CMD): copies only to the **11** proxy names that Display Commander actually implements; use this when you already know the game uses one of them (e.g. dxgi, d3d11) or want a minimal set of proxies in the folder.
