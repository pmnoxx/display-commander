#pragma once

#include <atomic>
#include <roapi.h>
#include <windows.gaming.input.h>
#include <windows.h>
#include <wrl.h>

namespace display_commanderhooks {

// Function pointer type for RoGetActivationFactory
using RoGetActivationFactory_pfn = HRESULT(WINAPI *)(HSTRING activatableClassId, REFIID iid, void **factory);

// Original function pointer
extern RoGetActivationFactory_pfn RoGetActivationFactory_Original;

// Hook state
extern std::atomic<bool> g_wgi_hooks_installed;

// Hooked RoGetActivationFactory function
HRESULT WINAPI RoGetActivationFactory_Detour(HSTRING activatableClassId, REFIID iid, void **factory);

// Hook management functions
bool InstallWindowsGamingInputHooks(HMODULE module = nullptr);
void UninstallWindowsGamingInputHooks();

} // namespace display_commanderhooks
