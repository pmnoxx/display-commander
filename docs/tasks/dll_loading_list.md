# Display Commander DLL Loading List

This document lists all DLL files that Display Commander loads using `LoadLibrary`, `LoadLibraryEx`, or related functions.

## System DLLs (Windows API)

### dbghelp.dll
- **Location**: `src/addons/display_commander/dbghelp_loader.cpp:30`
- **Function**: `LoadDbgHelp()`
- **Purpose**: Stack trace functionality (optional, fails gracefully if not available)
- **Method**: `LoadLibraryA("dbghelp.dll")`

### hid.dll
- **Location**: `src/addons/display_commander/hooks/dualsense_hooks.cpp:47`
- **Function**: `InitializeDirectHIDFunctions()`
- **Purpose**: DualSense controller HID functions (HidD_GetInputReport, HidD_GetAttributes)
- **Method**: `LoadLibraryA("hid.dll")`

### winmm.dll
- **Location**: `src/addons/display_commander/hooks/timeslowdown_hooks.cpp:459`
- **Function**: `InitializeTimeGetTimeDirect()`
- **Purpose**: timeGetTime function for time slowdown hooks
- **Method**: `LoadLibraryA("winmm.dll")`

### combase.dll
- **Location**: `src/addons/display_commander/audio/audio_management.cpp:80, 132`
- **Function**: `GetWindowsRuntimeStringFunctions()`, `GetAudioPolicyConfigFactory()`
- **Purpose**: Windows Runtime string functions for audio management
- **Method**: `LoadLibraryA("combase.dll")` (with GetModuleHandleA check first)

### shcore.dll
- **Location**: `src/addons/display_commander/display_cache.hpp:155`
- **Function**: DPI monitoring (inline code)
- **Purpose**: GetDpiForMonitor API for display DPI detection
- **Method**: `LoadLibraryA("shcore.dll")`

## DirectX Proxy DLLs (from System32)

### version.dll
- **Location**:
  - `src/addons/display_commander/proxy_dll/version_proxy.cpp:46`
  - `src/addons/display_commander/utils/general_utils.cpp:40`
- **Function**: `LoadVersionDll()`, `GetVersionFunction()`
- **Purpose**: Proxy DLL functionality - loads system version.dll
- **Method**: `LoadLibraryW(system32_path + L"\\version.dll")`

### dxgi.dll
- **Location**: `src/addons/display_commander/proxy_dll/dxgi_proxy.cpp:29`
- **Function**: `LoadDxgiDll()`
- **Purpose**: Proxy DLL functionality - loads system dxgi.dll
- **Method**: `LoadLibraryW(system32_path + L"\\dxgi.dll")`

### d3d11.dll
- **Location**: `src/addons/display_commander/proxy_dll/d3d11_proxy.cpp:27`
- **Function**: `LoadD3D11Dll()`
- **Purpose**: Proxy DLL functionality - loads system d3d11.dll
- **Method**: `LoadLibraryW(system32_path + L"\\d3d11.dll")`

### d3d12.dll
- **Location**: `src/addons/display_commander/proxy_dll/d3d12_proxy.cpp:32`
- **Function**: `LoadD3D12Dll()`
- **Purpose**: Proxy DLL functionality - loads system d3d12.dll
- **Method**: `LoadLibraryW(system32_path + L"\\d3d12.dll")`

## XInput DLLs

**Note**: Display Commander does NOT load XInput DLLs. It only checks if they are already loaded using `GetModuleHandleA` in `InitializeXInputDirectFunctions()` at `src/addons/display_commander/hooks/xinput_hooks.cpp:163`. XInput DLLs are typically loaded by the game or Windows itself.

## NVAPI DLLs

### nvapi64.dll (Fake NVAPI from Addon Directory)
- **Location**: `src/addons/display_commander/nvapi/fake_nvapi_manager.cpp:164`
- **Function**: `LoadFakeNvapi()`
- **Purpose**: Load fake NVAPI for non-NVIDIA GPUs (AMD/Intel)
- **Method**: `LoadLibraryA(addon_dir / "nvapi64.dll")`
- **Note**: Only loaded if real NVIDIA GPU is not detected

### fakenvapi.dll (Fallback Fake NVAPI)
- **Location**: `src/addons/display_commander/nvapi/fake_nvapi_manager.cpp:176`
- **Function**: `LoadFakeNvapi()`
- **Purpose**: Fallback fake NVAPI if nvapi64.dll not found in addon directory
- **Method**: `LoadLibraryA(addon_dir / "fakenvapi.dll")`
- **Note**: Only loaded if nvapi64.dll not found in addon directory

## ReShade DLLs

### Reshade64.dll
- **Location**: `src/addons/display_commander/main_entry.cpp:2237`
- **Function**: ReShade loading code (64-bit)
- **Purpose**: Load ReShade for 64-bit games
- **Method**: `LoadLibraryA("Reshade64.dll")` (from current directory)

### Reshade32.dll
- **Location**: `src/addons/display_commander/main_entry.cpp:2256`
- **Function**: ReShade loading code (32-bit)
- **Purpose**: Load ReShade for 32-bit games
- **Method**: `LoadLibraryA("Reshade32.dll")` (from current directory)

### ReShade DLL (Dynamic Path)
- **Location**:
  - `src/addons/display_commander/main_entry.cpp:2420`
  - `src/addons/display_commander/proxy_dll/reshade_loader.cpp:84`
- **Function**: ReShade loading from various paths
- **Purpose**: Load ReShade DLL from Documents folder or other locations
- **Method**: `LoadLibraryW(absolute_path.c_str())`

## Dynamic Addon DLLs

### Addon DLLs from Addons Directory
- **Location**: `src/addons/display_commander/main_entry.cpp:1428`
- **Function**: Addon loading code
- **Purpose**: Load enabled ReShade addons from Addons directory
- **Method**: `LoadLibraryExW(path.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS)`
- **Note**: Dynamically loads any enabled .addon64 or .addon32 files from the Addons directory

## Dynamic Module Loading (Function Parameters)

### Various Modules (via LoadProcCached)
- **Location**:
  - `src/addons/display_commander/latent_sync/latent_sync_limiter.cpp:24`
  - `src/addons/display_commander/latent_sync/vblank_monitor.hpp:97`
- **Function**: `LoadProcCached()` helper function
- **Purpose**: Dynamically load modules passed as parameters (module name is a function parameter)
- **Method**: `LoadLibraryW(mod)` where `mod` is a parameter
- **Note**: Used for loading various Windows APIs on-demand (e.g., dxgi.dll, user32.dll, etc.)

## Summary by Category

### Required System DLLs
- dbghelp.dll (optional - fails gracefully)
- hid.dll (optional - for DualSense support)
- winmm.dll (optional - for time slowdown hooks)
- combase.dll (optional - for audio management)
- shcore.dll (optional - for DPI monitoring)

### Proxy DLLs (System32)
- version.dll
- dxgi.dll
- d3d11.dll
- d3d12.dll

### Input DLLs
- **None** - Display Commander does not load XInput DLLs (only checks if already loaded)

### NVIDIA DLLs
- nvapi64.dll (fake - from addon directory)
- fakenvapi.dll (fake - fallback)
- **Note**: Real nvapi64.dll detection uses `GetModuleHandleA` (does not load the DLL)

### ReShade DLLs
- Reshade64.dll
- Reshade32.dll
- ReShade DLLs from various paths

### Dynamic DLLs
- Addon DLLs from Addons directory
- Various modules loaded via LoadProcCached (dynamic based on function parameters)

## Notes

1. **Error Handling**: Most DLL loads include error handling and will fail gracefully if the DLL is not available.

2. **Optional Dependencies**: Many DLLs are optional and the code continues to function if they cannot be loaded (e.g., dbghelp.dll, hid.dll).

3. **Dynamic Loading**: Some DLLs are loaded dynamically based on runtime conditions (e.g., fake NVAPI only loads if real NVIDIA GPU is not detected).

4. **Proxy DLLs**: The proxy DLLs (version.dll, dxgi.dll, d3d11.dll, d3d12.dll) are loaded from System32 to avoid loading the proxy itself.

5. **XInput Priority**: XInput DLLs are tried in order from newest to oldest version until one is found.

6. **ReShade Loading**: ReShade DLLs may be loaded from multiple locations (current directory, Documents folder, etc.).
