#include "dinput_hooks.hpp"
#include "hook_suppression_manager.hpp"
#include "windows_hooks/windows_message_hooks.hpp"
#include "../globals.hpp"
#include "../utils/general_utils.hpp"
#include "../utils/logging.hpp"
#include "../utils/detour_call_tracker.hpp"
#include "../utils/timing.hpp"
#include "../utils/srwlock_wrapper.hpp"
#include "../utils/timing.hpp"
#include "../settings/experimental_tab_settings.hpp"
#include <MinHook.h>
#include <vector>
#include <unordered_map>
#include <dinput.h>

namespace display_commanderhooks {

// Original function pointers
DirectInput8Create_pfn DirectInput8Create_Original = nullptr;
DirectInputCreateA_pfn DirectInputCreateA_Original = nullptr;
DirectInputCreateW_pfn DirectInputCreateW_Original = nullptr;

// Hook state
static std::atomic<bool> g_dinput_hooks_installed{false};

// Device tracking
static std::vector<DInputDeviceInfo> g_dinput_devices;
static SRWLOCK g_dinput_devices_mutex = SRWLOCK_INIT;

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
static SRWLOCK g_dinput_device_hooks_mutex = SRWLOCK_INIT;

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
    utils::SRWLockExclusive lock(g_dinput_devices_mutex);

    DInputDeviceInfo info;
    info.device_name = device_name;
    info.device_type = device_type;
    info.interface_name = interface_name;
    info.creation_time = utils::get_now_ns();

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
    utils::SRWLockExclusive lock(g_dinput_devices_mutex);
    g_dinput_devices.clear();
}

// DirectInput8Create detour
HRESULT WINAPI DirectInput8Create_Detour(HINSTANCE hinst, DWORD dwVersion, REFIID riidltf, LPVOID *ppvOut, LPUNKNOWN punkOuter) {
    RECORD_DETOUR_CALL(utils::get_now_ns());
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
    RECORD_DETOUR_CALL(utils::get_now_ns());
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
    RECORD_DETOUR_CALL(utils::get_now_ns());
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

    // Check if DirectInput hooks should be suppressed
    if (display_commanderhooks::HookSuppressionManager::GetInstance().ShouldSuppressHook(display_commanderhooks::HookType::DINPUT)) {
        LogInfo("DirectInput hooks installation suppressed by user setting");
        return false;
    }

    // Initialize MinHook (only if not already initialized)
    MH_STATUS init_status = SafeInitializeMinHook(display_commanderhooks::HookType::DINPUT);
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

    // Mark DirectInput hooks as installed
    display_commanderhooks::HookSuppressionManager::GetInstance().MarkHookInstalled(display_commanderhooks::HookType::DINPUT);

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

    // Clear device tracking and hooks
    ClearDInputDevices();
    ClearAllDirectInputDeviceHooks();

    g_dinput_hooks_installed.store(false);
    LogInfo("DirectInput hooks uninstalled successfully");
}

#if 0
// DirectInput Device State Hook Functions
HRESULT WINAPI DInputDevice_GetDeviceState_Detour(LPVOID pDevice, DWORD cbData, LPVOID lpvData) {
    RECORD_DETOUR_CALL(utils::get_now_ns());
    // Get the original function from the device hook
    utils::SRWLockExclusive lock(g_dinput_device_hooks_mutex);
    auto it = g_dinput_device_hooks.find(pDevice);
    if (it == g_dinput_device_hooks.end()) {
        LogWarn("DInputDevice_GetDeviceState_Detour: Device not found in hooks map");
        return E_FAIL;
    }

    DInputDeviceHook& hook = it->second;

    // Call original function
    typedef HRESULT (STDMETHODCALLTYPE *GetDeviceState_t)(LPVOID, DWORD, LPVOID);
    GetDeviceState_t original_func = reinterpret_cast<GetDeviceState_t>(hook.original_getdevicestate);
    HRESULT hr = original_func(pDevice, cbData, lpvData);

    if (SUCCEEDED(hr) && lpvData != nullptr) {
        // Check device type and apply input blocking
        switch (LOBYTE(hook.device_type)) {
        case DI8DEVTYPE_MOUSE:
            if (ShouldBlockMouseInput() && settings::g_experimentalTabSettings.dinput_device_state_blocking.GetValue()) {
                // Clear mouse button states (similar to ReShade's approach)
                if (cbData == sizeof(DIMOUSESTATE) || cbData == sizeof(DIMOUSESTATE2)) {
                    std::memset(static_cast<LPBYTE>(lpvData) + offsetof(DIMOUSESTATE, rgbButtons), 0,
                               cbData - offsetof(DIMOUSESTATE, rgbButtons));
                    LogDebug("DInputDevice_GetDeviceState: Blocked mouse button input for device %s",
                            hook.device_name.c_str());
                } else {
                    // For other mouse data structures, clear all data
                    std::memset(lpvData, 0, cbData);
                }
            }
            break;
        case DI8DEVTYPE_KEYBOARD:
            if (ShouldBlockKeyboardInput() && settings::g_experimentalTabSettings.dinput_device_state_blocking.GetValue()) {
                // Clear all keyboard data
                std::memset(lpvData, 0, cbData);
                LogDebug("DInputDevice_GetDeviceState: Blocked keyboard input for device %s",
                        hook.device_name.c_str());
            }
            break;
        }
    }

    return hr;
}

HRESULT WINAPI DInputDevice_GetDeviceData_Detour(LPVOID pDevice, DWORD cbObjectData, LPDIDEVICEOBJECTDATA rgdod, LPDWORD pdwInOut, DWORD dwFlags) {
    RECORD_DETOUR_CALL(utils::get_now_ns());
    // Get the original function from the device hook
    utils::SRWLockExclusive lock(g_dinput_device_hooks_mutex);
    auto it = g_dinput_device_hooks.find(pDevice);
    if (it == g_dinput_device_hooks.end()) {
        LogWarn("DInputDevice_GetDeviceData_Detour: Device not found in hooks map");
        return E_FAIL;
    }

    DInputDeviceHook& hook = it->second;

    // Call original function
    typedef HRESULT (STDMETHODCALLTYPE *GetDeviceData_t)(LPVOID, DWORD, LPDIDEVICEOBJECTDATA, LPDWORD, DWORD);
    GetDeviceData_t original_func = reinterpret_cast<GetDeviceData_t>(hook.original_getdevicedata);
    HRESULT hr = original_func(pDevice, cbObjectData, rgdod, pdwInOut, dwFlags);

    if (SUCCEEDED(hr) && (dwFlags & DIGDD_PEEK) == 0 && rgdod != nullptr && pdwInOut != nullptr && *pdwInOut != 0) {
        // Check device type and apply input blocking
        switch (LOBYTE(hook.device_type)) {
        case DI8DEVTYPE_MOUSE:
            if (ShouldBlockMouseInput() && settings::g_experimentalTabSettings.dinput_device_state_blocking.GetValue()) {
                *pdwInOut = 0; // Clear the data count
                hr = DI_OK; // Overwrite potential 'DI_BUFFEROVERFLOW'
                LogDebug("DInputDevice_GetDeviceData: Blocked mouse input events for device %s",
                        hook.device_name.c_str());
            }
            break;
        case DI8DEVTYPE_KEYBOARD:
            if (ShouldBlockKeyboardInput() && settings::g_experimentalTabSettings.dinput_device_state_blocking.GetValue()) {
                *pdwInOut = 0; // Clear the data count
                hr = DI_OK;
                LogDebug("DInputDevice_GetDeviceData: Blocked keyboard input events for device %s",
                        hook.device_name.c_str());
            }
            break;
        }
    }

    return hr;
}

// Hook DirectInput device vtable
void HookDirectInputDeviceVTable(LPVOID device, const std::string& device_name, DWORD device_type) {
    if (device == nullptr) {
        LogWarn("HookDirectInputDeviceVTable: Device is nullptr");
        return;
    }

    utils::SRWLockExclusive lock(g_dinput_device_hooks_mutex);

    // Check if already hooked
    if (g_dinput_device_hooks.find(device) != g_dinput_device_hooks.end()) {
        LogDebug("HookDirectInputDeviceVTable: Device %s already hooked", device_name.c_str());
        return;
    }

    // Get vtable
    LPVOID* vtable = *reinterpret_cast<LPVOID**>(device);
    if (vtable == nullptr) {
        LogWarn("HookDirectInputDeviceVTable: Failed to get vtable for device %s", device_name.c_str());
        return;
    }

    // Create device hook entry
    DInputDeviceHook hook;
    hook.device = device;
    hook.device_name = device_name;
    hook.device_type = device_type;
    hook.original_getdevicestate = vtable[9]; // GetDeviceState is at index 9
    hook.original_getdevicedata = vtable[10]; // GetDeviceData is at index 10
    hook.vtable_hooked = false;

    // Hook GetDeviceState
    if (hook.original_getdevicestate != nullptr) {
        if (MH_CreateHook(hook.original_getdevicestate, DInputDevice_GetDeviceState_Detour,
                         &hook.original_getdevicestate) == MH_OK) {
            if (MH_EnableHook(hook.original_getdevicestate) == MH_OK) {
                hook.vtable_hooked = true;
                LogInfo("HookDirectInputDeviceVTable: Successfully hooked GetDeviceState for device %s",
                       device_name.c_str());
            } else {
                LogWarn("HookDirectInputDeviceVTable: Failed to enable GetDeviceState hook for device %s",
                       device_name.c_str());
            }
        } else {
            LogWarn("HookDirectInputDeviceVTable: Failed to create GetDeviceState hook for device %s",
                   device_name.c_str());
        }
    }

    // Hook GetDeviceData
    if (hook.original_getdevicedata != nullptr) {
        if (MH_CreateHook(hook.original_getdevicedata, DInputDevice_GetDeviceData_Detour,
                         &hook.original_getdevicedata) == MH_OK) {
            if (MH_EnableHook(hook.original_getdevicedata) == MH_OK) {
                hook.vtable_hooked = true;
                LogInfo("HookDirectInputDeviceVTable: Successfully hooked GetDeviceData for device %s",
                       device_name.c_str());
            } else {
                LogWarn("HookDirectInputDeviceVTable: Failed to enable GetDeviceData hook for device %s",
                       device_name.c_str());
            }
        } else {
            LogWarn("HookDirectInputDeviceVTable: Failed to create GetDeviceData hook for device %s",
                   device_name.c_str());
        }
    }

    // Store the hook
    g_dinput_device_hooks[device] = hook;

    LogInfo("HookDirectInputDeviceVTable: Device %s (%s) vtable hooked successfully",
           device_name.c_str(), GetDeviceTypeName(device_type).c_str());
}
#endif

// Unhook DirectInput device vtable
void UnhookDirectInputDeviceVTable(LPVOID device) {
    if (device == nullptr) {
        return;
    }

    utils::SRWLockExclusive lock(g_dinput_device_hooks_mutex);

    auto it = g_dinput_device_hooks.find(device);
    if (it == g_dinput_device_hooks.end()) {
        return;
    }

    DInputDeviceHook& hook = it->second;

    // Disable hooks
    if (hook.original_getdevicestate != nullptr) {
        MH_DisableHook(hook.original_getdevicestate);
        MH_RemoveHook(hook.original_getdevicestate);
    }

    if (hook.original_getdevicedata != nullptr) {
        MH_DisableHook(hook.original_getdevicedata);
        MH_RemoveHook(hook.original_getdevicedata);
    }

    // Remove from map
    g_dinput_device_hooks.erase(it);

    LogInfo("UnhookDirectInputDeviceVTable: Device %s vtable unhooked", hook.device_name.c_str());
}

// Clear all DirectInput device hooks
void ClearAllDirectInputDeviceHooks() {
    utils::SRWLockExclusive lock(g_dinput_device_hooks_mutex);

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
    utils::SRWLockExclusive lock(g_dinput_device_hooks_mutex);

    // This is a placeholder function - in a real implementation, you would need to
    // enumerate all existing DirectInput devices and hook them
    // For now, this function exists to provide the interface for manual hooking

    LogInfo("HookAllDirectInputDevices: Manual device hooking requested (not implemented yet)");
}

// Get count of hooked DirectInput devices
int GetDirectInputDeviceHookCount() {
    utils::SRWLockExclusive lock(g_dinput_device_hooks_mutex);
    return static_cast<int>(g_dinput_device_hooks.size());
}

} // namespace display_commanderhooks
