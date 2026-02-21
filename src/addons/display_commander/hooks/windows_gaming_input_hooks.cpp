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
std::atomic<bool> g_wgi_hooks_installed{false};

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
    // When "Suppress Windows.Gaming.Input" is on (Advanced tab), fail all WGI factory requests so the game
    // falls back to XInput. That makes continue rendering in background work with gamepad (WGI drops input when
    // inactive). Example: Hollow Knight
    if (settings::g_advancedTabSettings.suppress_windows_gaming_input.GetValue()) {
        return E_NOTIMPL;
    }
    std::string guid_str = IIDToGUIDString(iid);
    //  LogInfo("RoGetActivationFactory called with IID: (%lu, %u, %u, %u, %u, %u, %u, %u, %u, %u, %u)", iid.Data1,
    //         iid.Data2, iid.Data3, iid.Data4[0], iid.Data4[1], iid.Data4[2], iid.Data4[3], iid.Data4[4], iid.Data4[5],
    //        iid.Data4[6], iid.Data4[7]);
    // LogInfo(" => GUID: %s", guid_str.c_str());

    // Switch statement to handle all Windows.Gaming.Input ABI interfaces
    if (iid == ABI::Windows::Gaming::Input::IID_IArcadeStick) {
        LogInfo("RoGetActivationFactory (IID_IArcadeStick) - UNHANDLED");
        return RoGetActivationFactory_Original(activatableClassId, iid, factory);
    } else if (iid == ABI::Windows::Gaming::Input::IID_IArcadeStickStatics) {
        LogInfo("RoGetActivationFactory (IID_IArcadeStickStatics) - UNHANDLED");
        return RoGetActivationFactory_Original(activatableClassId, iid, factory);
    } else if (iid == ABI::Windows::Gaming::Input::IID_IArcadeStickStatics2) {
        LogInfo("RoGetActivationFactory (IID_IArcadeStickStatics2) - UNHANDLED");
        return RoGetActivationFactory_Original(activatableClassId, iid, factory);
    } else if (iid == ABI::Windows::Gaming::Input::IID_IFlightStick) {
        LogInfo("RoGetActivationFactory (IID_IFlightStick) - UNHANDLED");
        return RoGetActivationFactory_Original(activatableClassId, iid, factory);
    } else if (iid == ABI::Windows::Gaming::Input::IID_IFlightStickStatics) {
        LogInfo("RoGetActivationFactory (IID_IFlightStickStatics) - UNHANDLED");
        return RoGetActivationFactory_Original(activatableClassId, iid, factory);
    } else if (iid == ABI::Windows::Gaming::Input::IID_IGameController) {
        LogInfo("RoGetActivationFactory (IID_IGameController) - UNHANDLED");
        return RoGetActivationFactory_Original(activatableClassId, iid, factory);
    } else if (iid == ABI::Windows::Gaming::Input::IID_IGameControllerBatteryInfo) {
        LogInfo("RoGetActivationFactory (IID_IGameControllerBatteryInfo) - UNHANDLED");
        return RoGetActivationFactory_Original(activatableClassId, iid, factory);
    } else if (iid == ABI::Windows::Gaming::Input::IID_IGamepad) {
        LogInfo("RoGetActivationFactory (IID_IGamepad) - UNHANDLED");
        return RoGetActivationFactory_Original(activatableClassId, iid, factory);
    } else if (iid == ABI::Windows::Gaming::Input::IID_IGamepad2) {
        LogInfo("RoGetActivationFactory (IID_IGamepad2) - UNHANDLED");
        return RoGetActivationFactory_Original(activatableClassId, iid, factory);
    } else if (iid == ABI::Windows::Gaming::Input::IID_IGamepadStatics) {
        LogInfo("RoGetActivationFactory (IID_IGamepadStatics)");
        LogInfo(" => Disabling Interface for Current Game.");
        return E_NOTIMPL;
    } else if (iid == ABI::Windows::Gaming::Input::IID_IGamepadStatics2) {
        LogInfo("RoGetActivationFactory (IID_IGamepadStatics2)");
        LogInfo(" => Disabling Interface for Current Game.");
        return E_NOTIMPL;
    } else if (iid == ABI::Windows::Gaming::Input::IID_IHeadset) {
        LogInfo("RoGetActivationFactory (IID_IHeadset) - UNHANDLED");
        return RoGetActivationFactory_Original(activatableClassId, iid, factory);
    } else if (iid == ABI::Windows::Gaming::Input::IID_IRacingWheel) {
        LogInfo("RoGetActivationFactory (IID_IRacingWheel) - UNHANDLED");
        return RoGetActivationFactory_Original(activatableClassId, iid, factory);
    } else if (iid == ABI::Windows::Gaming::Input::IID_IRacingWheelStatics) {
        LogInfo("RoGetActivationFactory (IID_IRacingWheelStatics) - UNHANDLED");
        return RoGetActivationFactory_Original(activatableClassId, iid, factory);
    } else if (iid == ABI::Windows::Gaming::Input::IID_IRacingWheelStatics2) {
        LogInfo("RoGetActivationFactory (IID_IRacingWheelStatics2) - UNHANDLED");
        return RoGetActivationFactory_Original(activatableClassId, iid, factory);
    } else if (iid == ABI::Windows::Gaming::Input::IID_IRawGameController) {
        LogInfo("RoGetActivationFactory (IID_IRawGameController) - UNHANDLED");
        return RoGetActivationFactory_Original(activatableClassId, iid, factory);
    } else if (iid == ABI::Windows::Gaming::Input::IID_IRawGameController2) {
        LogInfo("RoGetActivationFactory (IID_IRawGameController2) - UNHANDLED");
        return RoGetActivationFactory_Original(activatableClassId, iid, factory);
    } else if (iid == ABI::Windows::Gaming::Input::IID_IRawGameControllerStatics) {
        LogInfo("RoGetActivationFactory (IID_IRawGameControllerStatics)");
        LogInfo(" => Disabling Interface for Current Game.");
        return E_NOTIMPL;
    } else if (iid == ABI::Windows::Gaming::Input::IID_IUINavigationController) {
        LogInfo("RoGetActivationFactory (IID_IUINavigationController) - UNHANDLED");
        return RoGetActivationFactory_Original(activatableClassId, iid, factory);
    } else if (iid == ABI::Windows::Gaming::Input::IID_IUINavigationControllerStatics) {
        LogInfo("RoGetActivationFactory (IID_IUINavigationControllerStatics) - UNHANDLED");
        return RoGetActivationFactory_Original(activatableClassId, iid, factory);
    } else if (iid == ABI::Windows::Gaming::Input::IID_IUINavigationControllerStatics2) {
        LogInfo("RoGetActivationFactory (IID_IUINavigationControllerStatics2) - UNHANDLED");
        return RoGetActivationFactory_Original(activatableClassId, iid, factory);
    }
    // Handle ICoreWindow interface (Windows.UI.Core.ICoreWindow)
    else if (iid == ABI::Windows::UI::Core::IID_ICoreWindow) {
        LogInfo("RoGetActivationFactory (ICoreWindow) - DISABLED");
        LogInfo(
            " => Windows.UI.Core.ICoreWindow interface - "
            "https://learn.microsoft.com/en-us/uwp/api/windows.ui.core.icorewindow?view=winrt-26100");
        LogInfo(" => Disabling Interface for Current Game.");
        return E_NOTIMPL;
    }
    // Handle unknown IID that appears frequently: (242708616, 30204, 18607, 135, 88, 6, 82, 246, 240, 124, 89)
    // GUID: {0E6A0E38-761F-4A57-5888-06-52F6F07C59}
    else if (iid.Data1 == 242708616 && iid.Data2 == 30204 && iid.Data3 == 18607 && iid.Data4[0] == 135
             && iid.Data4[1] == 88 && iid.Data4[2] == 6 && iid.Data4[3] == 82 && iid.Data4[4] == 246
             && iid.Data4[5] == 240 && iid.Data4[6] == 124 && iid.Data4[7] == 89) {
        LogInfo("RoGetActivationFactory (Unknown Interface) - UNHANDLED");
        LogInfo(" => GUID: {0E6A0E38-761F-4A57-5888-06-52F6F07C59} - Need to identify this interface");
        LogInfo(" => This appears to be a non-standard Windows Gaming Input interface");
        LogInfo(" => Possible sources: Third-party library, custom interface, or undocumented Windows API");
        LogInfo(" => Calling original function for unknown interface");
        return RoGetActivationFactory_Original(activatableClassId, iid, factory);
    } else {
        // Log all unhandled IID calls for debugging
        std::string guid_str = IIDToGUIDString(iid);
        //  LogInfo("RoGetActivationFactory - UNHANDLED IID: (%lu, %u, %u, %u, %u, %u, %u, %u, %u, %u, %u)", iid.Data1,
        //         iid.Data2, iid.Data3, iid.Data4[0], iid.Data4[1], iid.Data4[2], iid.Data4[3], iid.Data4[4],
        //         iid.Data4[5], iid.Data4[6], iid.Data4[7]);
        //  LogInfo(" => GUID: %s", guid_str.c_str());
        //  LogInfo(" => Calling original function for non-Windows.Gaming.Input interface");
        return RoGetActivationFactory_Original(activatableClassId, iid, factory);
    }
}

bool InstallWindowsGamingInputHooks(HMODULE module) {
    if (g_wgi_hooks_installed.load()) {
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
        g_wgi_hooks_installed.store(true);
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
    if (!g_wgi_hooks_installed.load()) {
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
    g_wgi_hooks_installed.store(false);
    LogInfo("Windows.Gaming.Input hooks uninstalled successfully");
}

}  // namespace display_commanderhooks
