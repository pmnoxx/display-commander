#include "dinput_hooks.hpp"
#include "../utils.hpp"
#include "windows_hooks/windows_message_hooks.hpp"
#include "../globals.hpp"
#include "../utils/general_utils.hpp"
#include <MinHook.h>
#include <chrono>
#include <vector>
#include <mutex>

namespace display_commanderhooks {

// Original function pointers
DirectInput8Create_pfn DirectInput8Create_Original = nullptr;
DirectInputCreateA_pfn DirectInputCreateA_Original = nullptr;
DirectInputCreateW_pfn DirectInputCreateW_Original = nullptr;

// Hook state
static std::atomic<bool> g_dinput_hooks_installed{false};

// Device tracking
static std::vector<DInputDeviceInfo> g_dinput_devices;
static std::mutex g_dinput_devices_mutex;

// Hook statistics are now part of the main system

// Helper function to check if DirectInput hooks should be suppressed
bool ShouldSuppressDInputHooks() {
    return s_suppress_dinput_hooks.load();
}

// Helper function to get device type name
std::string GetDeviceTypeName(DWORD device_type) {
    switch (device_type) {
        case 0x00000000: return "Keyboard";  // DIDEVTYPE_KEYBOARD
        case 0x00000001: return "Mouse";     // DIDEVTYPE_MOUSE
        case 0x00000002: return "Joystick";  // DIDEVTYPE_JOYSTICK
        case 0x00000003: return "Gamepad";   // DIDEVTYPE_GAMEPAD
        case 0x00000004: return "Generic Device"; // DIDEVTYPE_DEVICE
        default: return "Unknown Device";
    }
}

// Helper function to get interface name from IID
std::string GetInterfaceName(REFIID riid) {
    // For now, just return a generic name since we can't easily detect the specific interface
    // This could be enhanced later with proper GUID checking
    return "DirectInput Interface";
}

// Track device creation
void TrackDInputDeviceCreation(const std::string& device_name, DWORD device_type, const std::string& interface_name) {
    std::lock_guard<std::mutex> lock(g_dinput_devices_mutex);

    DInputDeviceInfo info;
    info.device_name = device_name;
    info.device_type = device_type;
    info.interface_name = interface_name;
    info.creation_time = std::chrono::steady_clock::now();

    g_dinput_devices.push_back(info);

    LogInfo("DirectInput device created: %s (%s) via %s",
            device_name.c_str(),
            GetDeviceTypeName(device_type).c_str(),
            interface_name.c_str());
}

const std::vector<DInputDeviceInfo>& GetDInputDevices() {
    return g_dinput_devices;
}

void ClearDInputDevices() {
    std::lock_guard<std::mutex> lock(g_dinput_devices_mutex);
    g_dinput_devices.clear();
}

// DirectInput8Create detour
HRESULT WINAPI DirectInput8Create_Detour(HINSTANCE hinst, DWORD dwVersion, REFIID riidltf, LPVOID *ppvOut, LPUNKNOWN punkOuter) {
    // Track total calls
    g_hook_stats[HOOK_DInput8CreateDevice].increment_total();

    // Call original function
    HRESULT result = DirectInput8Create_Original(hinst, dwVersion, riidltf, ppvOut, punkOuter);

    // Check if hooks should be suppressed
    if (!ShouldSuppressDInputHooks()) {
        // Track unsuppressed calls
        g_hook_stats[HOOK_DInput8CreateDevice].increment_unsuppressed();

        if (SUCCEEDED(result) && ppvOut && *ppvOut) {
            // Track device creation
            std::string interface_name = GetInterfaceName(riidltf);
            TrackDInputDeviceCreation("DirectInput8", 0, interface_name);

            LogInfo("DirectInput8Create succeeded - Interface: %s", interface_name.c_str());
        } else {
            LogWarn("DirectInput8Create failed - HRESULT: 0x%08X", result);
        }
    }

    return result;
}

// DirectInputCreateA detour
HRESULT WINAPI DirectInputCreateA_Detour(HINSTANCE hinst, DWORD dwVersion, LPDIRECTINPUTA *ppDI, LPUNKNOWN punkOuter) {
    // Track total calls
    g_hook_stats[HOOK_DInputCreateDevice].increment_total();

    // Call original function
    HRESULT result = DirectInputCreateA_Original(hinst, dwVersion, ppDI, punkOuter);

    // Check if hooks should be suppressed
    if (!ShouldSuppressDInputHooks()) {
        // Track unsuppressed calls
        g_hook_stats[HOOK_DInputCreateDevice].increment_unsuppressed();

        if (SUCCEEDED(result) && ppDI && *ppDI) {
            // Track device creation
            TrackDInputDeviceCreation("DirectInputA", 0, "IDirectInputA");

            LogInfo("DirectInputCreateA succeeded");
        } else {
            LogWarn("DirectInputCreateA failed - HRESULT: 0x%08X", result);
        }
    }

    return result;
}

// DirectInputCreateW detour
HRESULT WINAPI DirectInputCreateW_Detour(HINSTANCE hinst, DWORD dwVersion, LPDIRECTINPUTW *ppDI, LPUNKNOWN punkOuter) {
    // Track total calls
    g_hook_stats[HOOK_DInputCreateDevice].increment_total();

    // Call original function
    HRESULT result = DirectInputCreateW_Original(hinst, dwVersion, ppDI, punkOuter);

    // Check if hooks should be suppressed
    if (!ShouldSuppressDInputHooks()) {
        // Track unsuppressed calls
        g_hook_stats[HOOK_DInputCreateDevice].increment_unsuppressed();

        if (SUCCEEDED(result) && ppDI && *ppDI) {
            // Track device creation
            TrackDInputDeviceCreation("DirectInputW", 0, "IDirectInputW");

            LogInfo("DirectInputCreateW succeeded");
        } else {
            LogWarn("DirectInputCreateW failed - HRESULT: 0x%08X", result);
        }
    }

    return result;
}

// Install DirectInput hooks
bool InstallDirectInputHooks() {
    if (g_dinput_hooks_installed.load()) {
        LogInfo("DirectInput hooks already installed");
        return true;
    }

    // Initialize MinHook (only if not already initialized)
    MH_STATUS init_status = SafeInitializeMinHook();
    if (init_status != MH_OK && init_status != MH_ERROR_ALREADY_INITIALIZED) {
        LogError("Failed to initialize MinHook for DirectInput hooks - Status: %d", init_status);
        return false;
    }

    if (init_status == MH_ERROR_ALREADY_INITIALIZED) {
        LogInfo("MinHook already initialized, proceeding with DirectInput hooks");
    } else {
        LogInfo("MinHook initialized successfully for DirectInput hooks");
    }

    // Hook DirectInput8Create
    HMODULE dinput8_module = GetModuleHandleW(L"dinput8.dll");
    if (dinput8_module) {
        auto DirectInput8Create_sys = reinterpret_cast<DirectInput8Create_pfn>(GetProcAddress(dinput8_module, "DirectInput8Create"));
        if (DirectInput8Create_sys) {
            if (CreateAndEnableHook(DirectInput8Create_sys, DirectInput8Create_Detour, (LPVOID*)&DirectInput8Create_Original, "DirectInput8Create")) {
                LogInfo("DirectInput8Create hook installed successfully");
            } else {
                LogError("Failed to create and enable DirectInput8Create hook");
            }
        } else {
            LogWarn("DirectInput8Create not found in dinput8.dll");
        }
    } else {
        LogWarn("dinput8.dll not loaded, skipping DirectInput8Create hook");
    }

    // Hook DirectInputCreateA
    HMODULE dinput_module = GetModuleHandleW(L"dinput.dll");
    if (dinput_module) {
        auto DirectInputCreateA_sys = reinterpret_cast<DirectInputCreateA_pfn>(GetProcAddress(dinput_module, "DirectInputCreateA"));
        if (DirectInputCreateA_sys) {
            if (CreateAndEnableHook(DirectInputCreateA_sys, DirectInputCreateA_Detour, (LPVOID*)&DirectInputCreateA_Original, "DirectInputCreateA")) {
                LogInfo("DirectInputCreateA hook installed successfully");
            } else {
                LogError("Failed to create and enable DirectInputCreateA hook");
            }
        } else {
            LogWarn("DirectInputCreateA not found in dinput.dll");
        }

        // Hook DirectInputCreateW
        auto DirectInputCreateW_sys = reinterpret_cast<DirectInputCreateW_pfn>(GetProcAddress(dinput_module, "DirectInputCreateW"));
        if (DirectInputCreateW_sys) {
            if (CreateAndEnableHook(DirectInputCreateW_sys, DirectInputCreateW_Detour, (LPVOID*)&DirectInputCreateW_Original, "DirectInputCreateW")) {
                LogInfo("DirectInputCreateW hook installed successfully");
            } else {
                LogError("Failed to create and enable DirectInputCreateW hook");
            }
        } else {
            LogWarn("DirectInputCreateW not found in dinput.dll");
        }
    } else {
        LogWarn("dinput.dll not loaded, skipping DirectInputCreate hooks");
    }

    g_dinput_hooks_installed.store(true);
    LogInfo("DirectInput hooks installation completed");

    return true;
}

// Uninstall DirectInput hooks
void UninstallDirectInputHooks() {
    if (!g_dinput_hooks_installed.load()) {
        return;
    }

    // Disable hooks
    if (DirectInput8Create_Original) {
        MH_DisableHook(DirectInput8Create_Original);
        DirectInput8Create_Original = nullptr;
    }

    if (DirectInputCreateA_Original) {
        MH_DisableHook(DirectInputCreateA_Original);
        DirectInputCreateA_Original = nullptr;
    }

    if (DirectInputCreateW_Original) {
        MH_DisableHook(DirectInputCreateW_Original);
        DirectInputCreateW_Original = nullptr;
    }

    // Clear device tracking
    ClearDInputDevices();

    g_dinput_hooks_installed.store(false);
    LogInfo("DirectInput hooks uninstalled successfully");
}
} // namespace display_commanderhooks
