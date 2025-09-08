#pragma once

#include <windows.h>
#include <roapi.h>
#include <wrl.h>
#include <atomic>

namespace renodx::hooks {

// Windows.Gaming.Input Interface IDs
// These are the same IIDs used by Special-K to identify Windows.Gaming.Input interfaces
static const IID IID_IGamepadStatics = { 0x8bbc2d9c, 0x9c00, 0x4c40, { 0x86, 0x4f, 0x08, 0xe9, 0x8d, 0x8e, 0x4b, 0x4c } };
static const IID IID_IGamepadStatics2 = { 0x5f5a9b2d, 0x8b4a, 0x4c3e, { 0x9b, 0x8a, 0x5c, 0x8b, 0x4a, 0x3e, 0x9b, 0x8a } };
static const IID IID_IRawGameControllerStatics = { 0x8bbc2d9c, 0x9c00, 0x4c40, { 0x86, 0x4f, 0x08, 0xe9, 0x8d, 0x8e, 0x4b, 0x4d } };

// Function pointer type for RoGetActivationFactory
using RoGetActivationFactory_pfn = HRESULT(WINAPI*)(HSTRING activatableClassId, REFIID iid, void** factory);

// Original function pointer
extern RoGetActivationFactory_pfn RoGetActivationFactory_Original;

// Hook state
extern std::atomic<bool> g_wgi_hooks_installed;

// Hooked RoGetActivationFactory function
HRESULT WINAPI RoGetActivationFactory_Detour(HSTRING activatableClassId, REFIID iid, void** factory);

// Hook management functions
bool InstallWindowsGamingInputHooks();
void UninstallWindowsGamingInputHooks();
bool AreWindowsGamingInputHooksInstalled();

} // namespace renodx::hooks
