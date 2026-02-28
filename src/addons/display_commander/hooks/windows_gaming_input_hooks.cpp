#include "windows_gaming_input_hooks.hpp"
#include <MinHook.h>
#include <atomic>
#include <string>
#include "../settings/advanced_tab_settings.hpp"
#include "../utils.hpp"
#include "../utils/general_utils.hpp"
#include "../utils/logging.hpp"
#include "hook_suppression_manager.hpp"

namespace display_commanderhooks {

// Original function pointer
RoGetActivationFactory_pfn RoGetActivationFactory_Original = nullptr;

// Hardcoded IIDs for the three WGI factory interfaces that Special K suppresses (same values as
// Windows SDK / windows-rs / Special K). Avoids depending on windows.gaming.input.h.
// See: https://github.com/SpecialKO/SpecialK/blob/main/src/input/windows.gaming.input.cpp
// See: https://github.com/microsoft/windows-rs/blob/master/crates/libs/windows/src/Windows/Gaming/Input/mod.rs
static const IID IID_IGamepadStatics_SK = {
    0x8BBCE529, 0xD49C, 0x39E9, {0x95, 0x60, 0xE4, 0x7D, 0xDE, 0x96, 0xB7, 0xC8}};
static const IID IID_IGamepadStatics2_SK = {
    0x42676DC5, 0x0856, 0x47C4, {0x92, 0x13, 0xB3, 0x95, 0x50, 0x4C, 0x3A, 0x3C}};
static const IID IID_IRawGameControllerStatics_SK = {
    0xEB8D0792, 0xE95A, 0x4B19, {0xAF, 0xC7, 0x0A, 0x59, 0xF8, 0xBF, 0x75, 0x9E}};

// Helper function to convert IID to GUID string format
std::string IIDToGUIDString(const IID& iid) {
    char guid_str[64];
    sprintf_s(guid_str, sizeof(guid_str), "{%08lX-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}", iid.Data1, iid.Data2,
              iid.Data3, iid.Data4[0], iid.Data4[1], iid.Data4[2], iid.Data4[3], iid.Data4[4], iid.Data4[5],
              iid.Data4[6], iid.Data4[7]);
    return std::string(guid_str);
}

// Hook state
WindowsGamingInputState g_wgi_state;

// Hooked RoGetActivationFactory function
// This function handles all Windows.Gaming.Input ABI interfaces:
// - IArcadeStick, IArcadeStickStatics, IArcadeStickStatics2
// - IFlightStick, IFlightStickStatics
// - IGameController, IGameControllerBatteryInfo
// - IGamepad, IGamepad2, IGamepadStatics, IGamepadStatics2
// - IHeadset
// - IRacingWheel, IRacingWheelStatics, IRacingWheelStatics2
// - IRawGameController, IRawGameController2, IRawGameControllerStatics
// - IUINavigationController, IUINavigationControllerStatics, IUINavigationControllerStatics2
//
// Also handles other Windows Runtime interfaces:
// - ICoreWindow: (1294176261, 15402, 16817, 144, 34, 83, 107, 185, 207, 147, 177)
//   Reference: https://learn.microsoft.com/en-us/uwp/api/windows.ui.core.icorewindow?view=winrt-26100
// ABI::Windows::UI::Core::IID_ICoreWindow

HRESULT WINAPI RoGetActivationFactory_Detour(HSTRING activatableClassId, REFIID iid, void** factory) {
    // Block WGI for Unity only: when UnityPlayer.dll is loaded, fail the same three WGI factory
    // requests that Special K suppresses (IGamepadStatics, IGamepadStatics2, IRawGameControllerStatics)
    // so Unity games (e.g. Hollow Knight) fall back to XInput. Non-Unity games keep full WGI.
    const bool is_blocked_iid =
        (IsEqualGUID(iid, IID_IGamepadStatics_SK) != 0 || IsEqualGUID(iid, IID_IGamepadStatics2_SK) != 0
         || IsEqualGUID(iid, IID_IRawGameControllerStatics_SK) != 0);

    const bool suppress = settings::g_advancedTabSettings.suppress_windows_gaming_input.GetValue();
    if (is_blocked_iid) {
        g_wgi_state.wgi_called.store(true);
        if (suppress && settings::g_advancedTabSettings.continue_rendering.GetValue()) {
            LogInfo("Suppressing WGI factory request (Unity): %s", IIDToGUIDString(iid).c_str());
            return E_NOTIMPL;
        }
    }

    bool iUnityPlayer = GetModuleHandleA("UnityPlayer.dll") != nullptr;
    if (iUnityPlayer && suppress) {
        LogInfo("Suppressing WGI factory request (Unity): %s", IIDToGUIDString(iid).c_str());
        return E_NOTIMPL;
    }

    return RoGetActivationFactory_Original(activatableClassId, iid, factory);
}

bool InstallWindowsGamingInputHooks(HMODULE module) {
    if (g_wgi_state.hooks_installed.load()) {
        LogInfo("Windows.Gaming.Input hooks already installed");
        return true;
    }

    // Only install when user has "Suppress Windows.Gaming.Input" enabled in Advanced tab (default on).
    if (!settings::g_advancedTabSettings.suppress_windows_gaming_input.GetValue()) {
        LogInfo("Windows Gaming Input hooks not installed (Suppress Windows.Gaming.Input is off in Advanced tab)");
        return false;
    }
    if (display_commanderhooks::HookSuppressionManager::GetInstance().ShouldSuppressHook(
            display_commanderhooks::HookType::WINDOWS_GAMING_INPUT)) {
        LogInfo("Windows Gaming Input hooks installation suppressed by user setting");
        return false;
    }

    // MinHook is already initialized by API hooks, so we don't need to initialize it again

    // Get the address of RoGetActivationFactory from combase.dll
    // Note: The passed module is typically windows.gaming.input.dll, but we need combase.dll
    HMODULE combase_module = GetModuleHandleA("combase.dll");
    if (!combase_module) {
        LogError("Failed to get combase.dll module handle");
        return false;
    }

    FARPROC ro_get_activation_factory_proc = GetProcAddress(combase_module, "RoGetActivationFactory");
    if (!ro_get_activation_factory_proc) {
        LogError("Failed to get RoGetActivationFactory address from combase.dll");
        return false;
    }

    LogInfo("Found RoGetActivationFactory at: 0x%p", ro_get_activation_factory_proc);

    // Create and enable the hook
    if (CreateAndEnableHook(ro_get_activation_factory_proc, RoGetActivationFactory_Detour,
                            (LPVOID*)&RoGetActivationFactory_Original, "RoGetActivationFactory")) {
        g_wgi_state.hooks_installed.store(true);
        g_wgi_state.wgi_called.store(true);
        LogInfo("Successfully hooked RoGetActivationFactory");

        // Mark Windows Gaming Input hooks as installed
        display_commanderhooks::HookSuppressionManager::GetInstance().MarkHookInstalled(
            display_commanderhooks::HookType::WINDOWS_GAMING_INPUT);

        return true;
    } else {
        LogError("Failed to create and enable RoGetActivationFactory hook");
        return false;
    }
}

void UninstallWindowsGamingInputHooks() {
    if (!g_wgi_state.hooks_installed.load()) {
        LogInfo("Windows.Gaming.Input hooks not installed");
        return;
    }

    // Get the address of RoGetActivationFactory from combase.dll
    HMODULE combase_module = GetModuleHandleA("combase.dll");
    if (combase_module) {
        FARPROC ro_get_activation_factory_proc = GetProcAddress(combase_module, "RoGetActivationFactory");
        if (ro_get_activation_factory_proc) {
            LogInfo("Unhooking RoGetActivationFactory");
            MH_DisableHook(ro_get_activation_factory_proc);
            MH_RemoveHook(ro_get_activation_factory_proc);
        }
    }

    // Clean up
    RoGetActivationFactory_Original = nullptr;
    g_wgi_state.hooks_installed.store(false);
    g_wgi_state.wgi_called.store(false);
    LogInfo("Windows.Gaming.Input hooks uninstalled successfully");
}

}  // namespace display_commanderhooks
