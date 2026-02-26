#pragma once

#include <roapi.h>
#include <windows.gaming.input.h>
#include <windows.h>
#include <wrl.h>
#include <atomic>

namespace display_commanderhooks {

// Function pointer type for RoGetActivationFactory
using RoGetActivationFactory_pfn = HRESULT(WINAPI*)(HSTRING activatableClassId, REFIID iid, void** factory);

// Original function pointer
extern RoGetActivationFactory_pfn RoGetActivationFactory_Original;

// Hook state: single structure for WGI-related atomics
struct WindowsGamingInputState {
    std::atomic<bool> hooks_installed{false};
    std::atomic<bool> wgi_called{false};
};
extern WindowsGamingInputState g_wgi_state;

// Hooked RoGetActivationFactory function
HRESULT WINAPI RoGetActivationFactory_Detour(HSTRING activatableClassId, REFIID iid, void** factory);

// Hook management functions
bool InstallWindowsGamingInputHooks(HMODULE module = nullptr);
void UninstallWindowsGamingInputHooks();

}  // namespace display_commanderhooks
