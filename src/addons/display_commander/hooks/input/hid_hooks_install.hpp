#pragma once

#include <windows.h>

namespace display_commanderhooks {

// Install HID-related hooks for kernel32.dll (ReadFile, CreateFileA, CreateFileW, WriteFile, DeviceIoControl).
// Called from OnModuleLoaded when kernel32.dll is loaded. Uses CreateAndEnableHookFromModule.
// Returns true if at least one hook was installed (respects HID_SUPPRESSION and HID suppress settings).
bool InstallHIDKernel32Hooks(HMODULE hModule);

// Install HID-related hooks for hid.dll (HidD_*, HidP_* APIs).
// Called from OnModuleLoaded when hid.dll is loaded. Uses CreateAndEnableHookFromModule.
// Returns true if at least one hook was installed (respects HID_SUPPRESSION and HID suppress settings).
bool InstallHIDDHooks(HMODULE hModule);

}  // namespace display_commanderhooks
