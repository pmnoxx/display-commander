#include "dinput_hooks.hpp"
#include <dinput.h>
#include <MinHook.h>
#include <unordered_map>
#include <vector>
#include "../../globals.hpp"
#include "../../settings/experimental_tab_settings.hpp"
#include "../../utils/detour_call_tracker.hpp"
#include "../../utils/general_utils.hpp"
#include "../../utils/logging.hpp"
#include "../../utils/srwlock_wrapper.hpp"
#include "../../utils/timing.hpp"
#include "../hook_suppression_manager.hpp"
#include "../windows_hooks/windows_message_hooks.hpp"

namespace display_commanderhooks {

// Original function pointers
DirectInput8Create_pfn DirectInput8Create_Original = nullptr;
DirectInputCreateA_pfn DirectInputCreateA_Original = nullptr;
DirectInputCreateW_pfn DirectInputCreateW_Original = nullptr;

// Hook state
static std::atomic<bool> g_dinput_hooks_installed{false};

// Device tracking
static std::vector<DInputDeviceInfo> g_dinput_devices;

// Device state hooking
struct DInputDeviceHook {
    LPVOID device;
    std::string device_name;
    DWORD device_type;
    LPVOID original_getdevicestate;
    LPVOID original_getdevicedata;
    bool vtable_hooked;
};

static std::unordered_map<LPVOID, DInputDeviceHook> g_dinput_device_hooks;

// Hook statistics are now part of the main system

// Helper function to check if DirectInput hooks should be suppressed
bool ShouldSuppressDInputHooks() { return s_suppress_dinput_hooks.load(); }

// Helper function to get device type name
std::string GetDeviceTypeName(DWORD device_type) {
    switch (device_type) {
        case 0x00000000: return "Keyboard";        // DIDEVTYPE_KEYBOARD
        case 0x00000001: return "Mouse";           // DIDEVTYPE_MOUSE
        case 0x00000002: return "Joystick";        // DIDEVTYPE_JOYSTICK
        case 0x00000003: return "Gamepad";         // DIDEVTYPE_GAMEPAD
        case 0x00000004: return "Generic Device";  // DIDEVTYPE_DEVICE
        default:         return "Unknown Device";
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
    utils::SRWLockExclusive lock(utils::g_dinput_devices_mutex);

    DInputDeviceInfo info;
    info.device_name = device_name;
    info.device_type = device_type;
    info.interface_name = interface_name;
    info.creation_time = utils::get_now_ns();

    g_dinput_devices.push_back(info);

    LogInfo("DirectInput device created: %s (%s) via %s", device_name.c_str(), GetDeviceTypeName(device_type).c_str(),
            interface_name.c_str());
}

const std::vector<DInputDeviceInfo>& GetDInputDevices() { return g_dinput_devices; }

void ClearDInputDevices() {
    utils::SRWLockExclusive lock(utils::g_dinput_devices_mutex);
    g_dinput_devices.clear();
}

// DirectInput8Create detour
HRESULT WINAPI DirectInput8Create_Detour(HINSTANCE hinst, DWORD dwVersion, REFIID riidltf, LPVOID* ppvOut,
                                         LPUNKNOWN punkOuter) {
    CALL_GUARD(utils::get_now_ns());
    // Track total calls
    g_hook_stats[HOOK_DInput8CreateDevice].increment_total();
    display_commanderhooks::UpdateHookLastCallTime(HOOK_DInput8CreateDevice);

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
HRESULT WINAPI DirectInputCreateA_Detour(HINSTANCE hinst, DWORD dwVersion, LPDIRECTINPUTA* ppDI, LPUNKNOWN punkOuter) {
    CALL_GUARD(utils::get_now_ns());
    // Track total calls
    g_hook_stats[HOOK_DInputCreateDevice].increment_total();
    display_commanderhooks::UpdateHookLastCallTime(HOOK_DInputCreateDevice);

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
HRESULT WINAPI DirectInputCreateW_Detour(HINSTANCE hinst, DWORD dwVersion, LPDIRECTINPUTW* ppDI, LPUNKNOWN punkOuter) {
    CALL_GUARD(utils::get_now_ns());
    // Track total calls
    g_hook_stats[HOOK_DInputCreateDevice].increment_total();
    display_commanderhooks::UpdateHookLastCallTime(HOOK_DInputCreateDevice);

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

// Install DirectInput 8 hooks for the given dinput8.dll module. Called from OnModuleLoaded.
bool InstallDirectInput8Hooks(HMODULE hModule) {
    if (!hModule) {
        return false;
    }
    if (DirectInput8Create_Original) {
        return true;  // already hooked
    }
    if (display_commanderhooks::HookSuppressionManager::GetInstance().ShouldSuppressHook(
            display_commanderhooks::HookType::DINPUT8)) {
        LogInfo("DirectInput 8 hooks installation suppressed by user setting");
        return false;
    }
    MH_STATUS init_status = SafeInitializeMinHook(display_commanderhooks::HookType::DINPUT8);
    if (init_status != MH_OK && init_status != MH_ERROR_ALREADY_INITIALIZED) {
        LogError("Failed to initialize MinHook for DirectInput 8 hooks - Status: %d", init_status);
        return false;
    }
    auto DirectInput8Create_sys =
        reinterpret_cast<DirectInput8Create_pfn>(GetProcAddress(hModule, "DirectInput8Create"));
    if (!DirectInput8Create_sys) {
        LogWarn("DirectInput8Create not found in dinput8.dll");
        return false;
    }
    if (!CreateAndEnableHook(DirectInput8Create_sys, DirectInput8Create_Detour, (LPVOID*)&DirectInput8Create_Original,
                             "DirectInput8Create")) {
        LogError("Failed to create and enable DirectInput8Create hook");
        return false;
    }
    LogInfo("DirectInput8Create hook installed successfully");
    g_dinput_hooks_installed.store(true);
    display_commanderhooks::HookSuppressionManager::GetInstance().MarkHookInstalled(
        display_commanderhooks::HookType::DINPUT8);
    return true;
}

// Install legacy DirectInput hooks (DirectInputCreateA/W) for the given dinput.dll module. Called from OnModuleLoaded.
bool InstallDirectInputHooks(HMODULE hModule) {
    if (!enabled_experimental_features || true) {
        LogInfo("DirectInput (legacy) hooks installation suppressed by user setting");
        return false;
    }
    if (!hModule) {
        return false;
    }
    if (DirectInputCreateA_Original && DirectInputCreateW_Original) {
        return true;  // already hooked
    }
    if (display_commanderhooks::HookSuppressionManager::GetInstance().ShouldSuppressHook(
            display_commanderhooks::HookType::DINPUT)) {
        LogInfo("DirectInput (legacy) hooks installation suppressed by user setting");
        return false;
    }
    MH_STATUS init_status = SafeInitializeMinHook(display_commanderhooks::HookType::DINPUT);
    if (init_status != MH_OK && init_status != MH_ERROR_ALREADY_INITIALIZED) {
        LogError("Failed to initialize MinHook for DirectInput (legacy) hooks - Status: %d", init_status);
        return false;
    }
    bool any_installed = false;
    if (!DirectInputCreateA_Original) {
        auto DirectInputCreateA_sys =
            reinterpret_cast<DirectInputCreateA_pfn>(GetProcAddress(hModule, "DirectInputCreateA"));
        if (DirectInputCreateA_sys) {
            if (CreateAndEnableHook(DirectInputCreateA_sys, DirectInputCreateA_Detour,
                                    (LPVOID*)&DirectInputCreateA_Original, "DirectInputCreateA")) {
                LogInfo("DirectInputCreateA hook installed successfully");
                any_installed = true;
            } else {
                LogError("Failed to create and enable DirectInputCreateA hook");
            }
        } else {
            LogWarn("DirectInputCreateA not found in dinput.dll");
        }
    }
    if (!DirectInputCreateW_Original) {
        auto DirectInputCreateW_sys =
            reinterpret_cast<DirectInputCreateW_pfn>(GetProcAddress(hModule, "DirectInputCreateW"));
        if (DirectInputCreateW_sys) {
            if (CreateAndEnableHook(DirectInputCreateW_sys, DirectInputCreateW_Detour,
                                    (LPVOID*)&DirectInputCreateW_Original, "DirectInputCreateW")) {
                LogInfo("DirectInputCreateW hook installed successfully");
                any_installed = true;
            } else {
                LogError("Failed to create and enable DirectInputCreateW hook");
            }
        } else {
            LogWarn("DirectInputCreateW not found in dinput.dll");
        }
    }
    if (any_installed) {
        g_dinput_hooks_installed.store(true);
        display_commanderhooks::HookSuppressionManager::GetInstance().MarkHookInstalled(
            display_commanderhooks::HookType::DINPUT);
    }
    return any_installed;
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

    // Clear device tracking and hooks
    ClearDInputDevices();
    ClearAllDirectInputDeviceHooks();

    g_dinput_hooks_installed.store(false);
    LogInfo("DirectInput hooks uninstalled successfully");
}

// Clear all DirectInput device hooks
void ClearAllDirectInputDeviceHooks() {
    utils::SRWLockExclusive lock(utils::g_dinput_device_hooks_mutex);

    for (auto& pair : g_dinput_device_hooks) {
        DInputDeviceHook& hook = pair.second;

        // Disable hooks
        if (hook.original_getdevicestate != nullptr) {
            MH_DisableHook(hook.original_getdevicestate);
            MH_RemoveHook(hook.original_getdevicestate);
        }

        if (hook.original_getdevicedata != nullptr) {
            MH_DisableHook(hook.original_getdevicedata);
            MH_RemoveHook(hook.original_getdevicedata);
        }
    }

    g_dinput_device_hooks.clear();
    LogInfo("ClearAllDirectInputDeviceHooks: All DirectInput device hooks cleared");
}

// Hook all DirectInput devices (for manual hooking)
void HookAllDirectInputDevices() {
    utils::SRWLockExclusive lock(utils::g_dinput_device_hooks_mutex);

    // This is a placeholder function - in a real implementation, you would need to
    // enumerate all existing DirectInput devices and hook them
    // For now, this function exists to provide the interface for manual hooking

    LogInfo("HookAllDirectInputDevices: Manual device hooking requested (not implemented yet)");
}

// Get count of hooked DirectInput devices
int GetDirectInputDeviceHookCount() {
    utils::SRWLockExclusive lock(utils::g_dinput_device_hooks_mutex);
    return static_cast<int>(g_dinput_device_hooks.size());
}

}  // namespace display_commanderhooks
