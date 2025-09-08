#include "windows_gaming_input_hooks.hpp"
#include "../utils.hpp"
#include <MinHook.h>
#include <atomic>

namespace renodx::hooks {

// Original function pointer
RoGetActivationFactory_pfn RoGetActivationFactory_Original = nullptr;

// Hook state
std::atomic<bool> g_wgi_hooks_installed{false};

// Hooked RoGetActivationFactory function
HRESULT WINAPI RoGetActivationFactory_Detour(HSTRING activatableClassId, REFIID iid, void** factory) {
    LogInfo("RoGetActivationFactory called with IID: {%08lX-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}",
            iid.Data1, iid.Data2, iid.Data3,
            iid.Data4[0], iid.Data4[1], iid.Data4[2], iid.Data4[3],
            iid.Data4[4], iid.Data4[5], iid.Data4[6], iid.Data4[7]);

    // Check if this is a Windows.Gaming.Input interface we want to disable
    if (iid == ABI::Windows::Gaming::Input::IID_IGamepadStatics) {
        LogInfo("RoGetActivationFactory (IID_IGamepadStatics)");
        LogInfo(" => Disabling Interface for Current Game.");
        return E_NOTIMPL;
    }

    if (iid == ABI::Windows::Gaming::Input::IID_IGamepadStatics2) {
        LogInfo("RoGetActivationFactory (IID_IGamepadStatics2)");
        LogInfo(" => Disabling Interface for Current Game.");
        return E_NOTIMPL;
    }

    if (iid == ABI::Windows::Gaming::Input::IID_IRawGameControllerStatics) {
        LogInfo("RoGetActivationFactory (IID_IRawGameControllerStatics)");
        LogInfo(" => Disabling Interface for Current Game.");
        return E_NOTIMPL;
    }

    // For all other interfaces, call the original function
    return RoGetActivationFactory_Original(activatableClassId, iid, factory);
}

bool InstallWindowsGamingInputHooks() {
    if (g_wgi_hooks_installed.load()) {
        LogInfo("Windows.Gaming.Input hooks already installed");
        return true;
    }

    // MinHook is already initialized by API hooks, so we don't need to initialize it again

    // Get the address of RoGetActivationFactory from combase.dll
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

    // Create the hook
    if (MH_CreateHook(ro_get_activation_factory_proc, RoGetActivationFactory_Detour, (LPVOID*)&RoGetActivationFactory_Original) == MH_OK) {
        if (MH_EnableHook(ro_get_activation_factory_proc) == MH_OK) {
            g_wgi_hooks_installed.store(true);
            LogInfo("Successfully hooked RoGetActivationFactory");
            return true;
        } else {
            LogError("Failed to enable RoGetActivationFactory hook");
            MH_RemoveHook(ro_get_activation_factory_proc);
            return false;
        }
    } else {
        LogError("Failed to create RoGetActivationFactory hook");
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

bool AreWindowsGamingInputHooksInstalled() {
    return g_wgi_hooks_installed.load();
}

} // namespace renodx::hooks
