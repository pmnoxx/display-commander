#pragma once

#include <dinput.h>
#include <windows.h>
#include <string>
#include <vector>

namespace display_commanderhooks {

// DirectInput hook indices are now part of the main hook system
// HOOK_DInput8CreateDevice = 41
// HOOK_DInputCreateDevice = 42

// Function pointer types for DirectInput functions
using DirectInput8Create_pfn = HRESULT(WINAPI*)(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);
using DirectInputCreateA_pfn = HRESULT(WINAPI*)(HINSTANCE, DWORD, LPDIRECTINPUTA*, LPUNKNOWN);
using DirectInputCreateW_pfn = HRESULT(WINAPI*)(HINSTANCE, DWORD, LPDIRECTINPUTW*, LPUNKNOWN);

// Original function pointers
extern DirectInput8Create_pfn DirectInput8Create_Original;
extern DirectInputCreateA_pfn DirectInputCreateA_Original;
extern DirectInputCreateW_pfn DirectInputCreateW_Original;

// Hooked DirectInput functions
HRESULT WINAPI DirectInput8Create_Detour(HINSTANCE hinst, DWORD dwVersion, REFIID riidltf, LPVOID* ppvOut,
                                         LPUNKNOWN punkOuter);
HRESULT WINAPI DirectInputCreateA_Detour(HINSTANCE hinst, DWORD dwVersion, LPDIRECTINPUTA* ppDI, LPUNKNOWN punkOuter);
HRESULT WINAPI DirectInputCreateW_Detour(HINSTANCE hinst, DWORD dwVersion, LPDIRECTINPUTW* ppDI, LPUNKNOWN punkOuter);

// Hook management (call from OnModuleLoaded with the loaded module handle)
bool InstallDirectInput8Hooks(HMODULE hModule);
bool InstallDirectInputHooks(HMODULE hModule);  // dinput.dll (DirectInputCreateA/W)
void UninstallDirectInputHooks();

// Device creation tracking
struct DInputDeviceInfo {
    std::string device_name;
    DWORD device_type;
    std::string interface_name;
    LONGLONG creation_time;  // Time in nanoseconds from get_now_ns()
};

// Device tracking functions
void TrackDInputDeviceCreation(const std::string& device_name, DWORD device_type, const std::string& interface_name);
const std::vector<DInputDeviceInfo>& GetDInputDevices();
void ClearDInputDevices();

// Device state hooking (ClearAllDirectInputDeviceHooks / HookAllDirectInputDevices / GetDirectInputDeviceHookCount)
void ClearAllDirectInputDeviceHooks();
void HookAllDirectInputDevices();
int GetDirectInputDeviceHookCount();

}  // namespace display_commanderhooks
