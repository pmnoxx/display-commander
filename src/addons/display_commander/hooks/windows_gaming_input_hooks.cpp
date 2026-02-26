#include "windows_gaming_input_hooks.hpp"
#include <MinHook.h>
#include <windows.gaming.input.h>
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
    // When "Suppress Windows.Gaming.Input" is on (Advanced tab), fail only the same three WGI factory
    // requests that Special K suppresses (blackout_api): IGamepadStatics, IGamepadStatics2,
    // IRawGameControllerStatics. That makes the game fall back to XInput for gamepad; other WGI
    // interfaces (racing wheel, arcade stick, etc.) are left intact. Example: Hollow Knight
    if ((iid == ABI::Windows::Gaming::Input::IID_IGamepadStatics
         || iid == ABI::Windows::Gaming::Input::IID_IGamepadStatics2
         || iid == ABI::Windows::Gaming::Input::IID_IRawGameControllerStatics)) {
        g_wgi_state.wgi_called.store(true);
        const bool suppress = settings::g_advancedTabSettings.suppress_windows_gaming_input.GetValue();
        if (suppress && settings::g_advancedTabSettings.continue_rendering.GetValue()) {
            LogInfo("Suppressing WGI factory request: %s", IIDToGUIDString(iid).c_str());
            return E_NOTIMPL;
        }
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
