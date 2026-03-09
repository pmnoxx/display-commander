#include "windows_gaming_input_hooks.hpp"
#include <MinHook.h>
#include <windows.gaming.input.h>
#include <atomic>
#include <set>
#include <string>
#include <utility>
#include "../settings/advanced_tab_settings.hpp"
#include "../utils/general_utils.hpp"
#include "../utils/logging.hpp"
#include "../utils/srwlock_wrapper.hpp"
#include "hook_suppression_manager.hpp"
#include "input_activity_stats.hpp"

namespace {

using WindowsGetStringRawBuffer_pfn = PCWSTR(WINAPI*)(HSTRING string, UINT32* length);

WindowsGetStringRawBuffer_pfn GetWindowsGetStringRawBuffer() {
    static WindowsGetStringRawBuffer_pfn s_fn = nullptr;
    if (s_fn != nullptr) {
        return s_fn;
    }
    HMODULE combase = GetModuleHandleA("combase.dll");
    if (combase != nullptr) {
        s_fn = reinterpret_cast<WindowsGetStringRawBuffer_pfn>(GetProcAddress(combase, "WindowsGetStringRawBuffer"));
    }
    return s_fn;
}

// Converts HSTRING to UTF-8 for logging. If hstr is invalid, the Windows API may raise; we do not use SEH.
std::string HStringToNarrowSafe(HSTRING hstr) {
    if (hstr == nullptr) {
        return "(null)";
    }
    WindowsGetStringRawBuffer_pfn get_string_raw_buffer = GetWindowsGetStringRawBuffer();
    if (get_string_raw_buffer == nullptr) {
        return "(no WindowsGetStringRawBuffer)";
    }
    UINT32 len = 0;
    PCWSTR raw = get_string_raw_buffer(hstr, &len);
    if (raw == nullptr || len == 0) {
        return "(empty)";
    }
    int need = WideCharToMultiByte(CP_UTF8, 0, raw, static_cast<int>(len), nullptr, 0, nullptr, nullptr);
    if (need <= 0) {
        return "(convert failed)";
    }
    std::string out(static_cast<size_t>(need), '\0');
    WideCharToMultiByte(CP_UTF8, 0, raw, static_cast<int>(len), out.data(), need, nullptr, nullptr);
    return out;
}

SRWLOCK g_seen_riid_class_lock = SRWLOCK_INIT;
std::set<std::pair<std::string, std::string>> g_seen_riid_class_pairs;

}  // namespace

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
    // Log each new (iid, activatableClassId) pair once. HStringToNarrowSafe is crash-safe (invalid HSTRING → no AV).
    const std::string iid_str = IIDToGUIDString(iid);
    const std::string class_str = HStringToNarrowSafe(activatableClassId);
    {
        utils::SRWLockExclusive lock(g_seen_riid_class_lock);
        if (g_seen_riid_class_pairs.insert({iid_str, class_str}).second) {
            LogInfo("RoGetActivationFactory new pair: riid=%s activatableClassId=%s", iid_str.c_str(),
                    class_str.c_str());
        }
    }

    // Always block those iids.
    const bool is_blocked_iid = (iid == ABI::Windows::Gaming::Input::IID_IGamepadStatics
                                 || iid == ABI::Windows::Gaming::Input::IID_IGamepadStatics2
                                 || iid == ABI::Windows::Gaming::Input::IID_IRawGameControllerStatics);

    if (is_blocked_iid) {
        return RoGetActivationFactory_Original(activatableClassId, iid, factory);
    }

    static bool is_unity_player = GetModuleHandleA("UnityPlayer.dll") != nullptr;
    const bool global_on = settings::g_advancedTabSettings.suppress_wgi_globally.GetValue();
    const bool master = settings::g_advancedTabSettings.suppress_wgi_enabled.GetValue();
    const bool suppress_for_unity = settings::g_advancedTabSettings.suppress_wgi_for_unity.GetValue();
    const bool suppress_for_non_unity = settings::g_advancedTabSettings.suppress_wgi_for_non_unity_games.GetValue();
    const bool per_game_ok =
        (is_unity_player && (suppress_for_unity || global_on)) || (!is_unity_player && (suppress_for_non_unity || global_on));
    const bool should_suppress = (master || global_on) && per_game_ok;

    if (should_suppress) {
        g_wgi_state.wgi_suppressed_ever.store(true);
        LogInfo("Suppressing WGI factory request: %s", iid_str.c_str());
        return E_NOTIMPL;
    }

    // Game is using WGI; show in Active inputs and forward to original.
    InputActivityStats::GetInstance().MarkActive(InputApiId::WindowsGamingInput);
    return RoGetActivationFactory_Original(activatableClassId, iid, factory);
}

bool InstallWindowsGamingInputHooks(HMODULE module) {
    if (g_wgi_state.hooks_installed.load()) {
        LogInfo("Windows.Gaming.Input hooks already installed");
        return true;
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
    g_wgi_state.wgi_suppressed_ever.store(false);
    LogInfo("Windows.Gaming.Input hooks uninstalled successfully");
}

}  // namespace display_commanderhooks
