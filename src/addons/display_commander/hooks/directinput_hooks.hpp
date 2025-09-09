#pragma once

#include <windows.h>
#include <dinput.h>

// Forward declarations for DirectInput functions
extern "C" {
    HRESULT WINAPI DirectInputCreateA(HINSTANCE, DWORD, LPDIRECTINPUTA*, LPUNKNOWN);
    HRESULT WINAPI DirectInputCreateW(HINSTANCE, DWORD, LPDIRECTINPUTW*, LPUNKNOWN);
    HRESULT WINAPI DirectInputCreateEx(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);
    HRESULT WINAPI DirectInput8Create(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);
}

namespace renodx::hooks {

// Function pointer types for DirectInput creation functions
using DirectInputCreateA_pfn = HRESULT(WINAPI*)(HINSTANCE, DWORD, LPDIRECTINPUTA*, LPUNKNOWN);
using DirectInputCreateW_pfn = HRESULT(WINAPI*)(HINSTANCE, DWORD, LPDIRECTINPUTW*, LPUNKNOWN);
using DirectInputCreateEx_pfn = HRESULT(WINAPI*)(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);
using DirectInput8Create_pfn = HRESULT(WINAPI*)(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);

// Function pointer types for IDirectInputDevice methods
using IDirectInputDevice_GetDeviceState_pfn = HRESULT(STDMETHODCALLTYPE*)(IDirectInputDevice*, DWORD, LPVOID);
using IDirectInputDevice_GetDeviceData_pfn = HRESULT(STDMETHODCALLTYPE*)(IDirectInputDevice*, DWORD, LPDIDEVICEOBJECTDATA, LPDWORD, DWORD);

// DirectInput hook function pointers
extern DirectInputCreateA_pfn DirectInputCreateA_Original;
extern DirectInputCreateW_pfn DirectInputCreateW_Original;
extern DirectInputCreateEx_pfn DirectInputCreateEx_Original;
extern DirectInput8Create_pfn DirectInput8Create_Original;

// Hooked DirectInput creation functions
HRESULT WINAPI DirectInputCreateA_Detour(HINSTANCE hinst, DWORD dwVersion, LPDIRECTINPUTA* ppDI, LPUNKNOWN punkOuter);
HRESULT WINAPI DirectInputCreateW_Detour(HINSTANCE hinst, DWORD dwVersion, LPDIRECTINPUTW* ppDI, LPUNKNOWN punkOuter);
HRESULT WINAPI DirectInputCreateEx_Detour(HINSTANCE hinst, DWORD dwVersion, REFIID riid, LPVOID* ppv, LPUNKNOWN punkOuter);
HRESULT WINAPI DirectInput8Create_Detour(HINSTANCE hinst, DWORD dwVersion, REFIID riid, LPVOID* ppv, LPUNKNOWN punkOuter);

// Hook management
bool InstallDirectInputHooks();
void UninstallDirectInputHooks();
bool AreDirectInputHooksInstalled();

// Helper functions for device state filtering
bool ShouldBlockDirectInputDevice(IDirectInputDevice* device);
bool ShouldBlockDirectInputData(const DIDEVICEOBJECTDATA* data, DWORD count);

} // namespace renodx::hooks
