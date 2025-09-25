#include "directinput_hooks.hpp"
#include "../globals.hpp"
#include "../utils.hpp"
#include "windows_hooks/windows_message_hooks.hpp"
#include <MinHook.h>
#include <memory>
#include <vector>


namespace display_commanderhooks {

// Original function pointers
DirectInputCreateA_pfn DirectInputCreateA_Original = nullptr;
DirectInputCreateW_pfn DirectInputCreateW_Original = nullptr;
DirectInputCreateEx_pfn DirectInputCreateEx_Original = nullptr;
DirectInput8Create_pfn DirectInput8Create_Original = nullptr;

// Hook state
static std::atomic<bool> g_directinput_hooks_installed{false};

// Track hooked DirectInput instances and their devices
struct DirectInputInstance {
    IUnknown *instance;
    std::vector<IDirectInputDevice *> devices;
    bool is_directinput8;
};

static std::vector<std::unique_ptr<DirectInputInstance>> g_directinput_instances;

// Helper function to check if input should be blocked
bool ShouldBlockDirectInputDevice(IDirectInputDevice *device) {
    if (!device)
        return false;

    // Check if continue rendering is enabled
    if (s_continue_rendering.load()) {
        return false; // Allow input when continue rendering is enabled
    }

    // For now, we'll allow all DirectInput devices
    // This can be extended to block specific device types if needed
    return false;
}

bool ShouldBlockDirectInputData(const DIDEVICEOBJECTDATA *data, DWORD count) {
    if (!data || count == 0)
        return false;

    // Check if continue rendering is enabled
    if (s_continue_rendering.load()) {
        return false; // Allow input when continue rendering is enabled
    }

    // For now, we'll allow all DirectInput data
    // This can be extended to filter specific input types if needed
    return false;
}

// Hooked DirectInputCreateA function
HRESULT WINAPI DirectInputCreateA_Detour(HINSTANCE hinst, DWORD dwVersion, LPDIRECTINPUTA *ppDI, LPUNKNOWN punkOuter) {
    // Track hook call statistics
    g_hook_stats[HOOK_DirectInputCreateA].increment_total();

    LogInfo("DirectInputCreateA_Detour called - Version: 0x%08X", dwVersion);

    // Call original function
    HRESULT result = DirectInputCreateA_Original ? DirectInputCreateA_Original(hinst, dwVersion, ppDI, punkOuter)
                                                 : ERROR_CALL_NOT_IMPLEMENTED; // Function not available

    if (SUCCEEDED(result) && ppDI && *ppDI) {
        LogInfo("DirectInputCreateA_Detour: Created DirectInput instance successfully");

        // Track unsuppressed call
        g_hook_stats[HOOK_DirectInputCreateA].increment_unsuppressed();

        // Store the instance for tracking
        auto instance = std::make_unique<DirectInputInstance>();
        instance->instance = *ppDI;
        instance->is_directinput8 = false;
        g_directinput_instances.push_back(std::move(instance));
    } else {
        LogError("DirectInputCreateA_Detour: Failed to create DirectInput instance - HRESULT: 0x%08X", result);
    }

    return result;
}

// Hooked DirectInputCreateW function
HRESULT WINAPI DirectInputCreateW_Detour(HINSTANCE hinst, DWORD dwVersion, LPDIRECTINPUTW *ppDI, LPUNKNOWN punkOuter) {
    // Track hook call statistics
    g_hook_stats[HOOK_DirectInputCreateW].increment_total();

    LogInfo("DirectInputCreateW_Detour called - Version: 0x%08X", dwVersion);

    // Call original function
    HRESULT result = DirectInputCreateW_Original ? DirectInputCreateW_Original(hinst, dwVersion, ppDI, punkOuter)
                                                 : ERROR_CALL_NOT_IMPLEMENTED; // Function not available

    if (SUCCEEDED(result) && ppDI && *ppDI) {
        LogInfo("DirectInputCreateW_Detour: Created DirectInput instance successfully");

        // Track unsuppressed call
        g_hook_stats[HOOK_DirectInputCreateW].increment_unsuppressed();

        // Store the instance for tracking
        auto instance = std::make_unique<DirectInputInstance>();
        instance->instance = *ppDI;
        instance->is_directinput8 = false;
        g_directinput_instances.push_back(std::move(instance));
    } else {
        LogError("DirectInputCreateW_Detour: Failed to create DirectInput instance - HRESULT: 0x%08X", result);
    }

    return result;
}

// Hooked DirectInputCreateEx function
HRESULT WINAPI DirectInputCreateEx_Detour(HINSTANCE hinst, DWORD dwVersion, REFIID riid, LPVOID *ppv,
                                          LPUNKNOWN punkOuter) {
    // Track hook call statistics
    g_hook_stats[HOOK_DirectInputCreateEx].increment_total();

    LogInfo("DirectInputCreateEx_Detour called - Version: 0x%08X", dwVersion);

    // Call original function
    HRESULT result = DirectInputCreateEx_Original ? DirectInputCreateEx_Original(hinst, dwVersion, riid, ppv, punkOuter)
                                                  : ERROR_CALL_NOT_IMPLEMENTED; // Function not available

    if (SUCCEEDED(result) && ppv && *ppv) {
        LogInfo("DirectInputCreateEx_Detour: Created DirectInput instance successfully");

        // Track unsuppressed call
        g_hook_stats[HOOK_DirectInputCreateEx].increment_unsuppressed();

        // Store the instance for tracking
        auto instance = std::make_unique<DirectInputInstance>();
        instance->instance = static_cast<IUnknown *>(*ppv);
        instance->is_directinput8 = false;
        g_directinput_instances.push_back(std::move(instance));
    } else {
        LogError("DirectInputCreateEx_Detour: Failed to create DirectInput instance - HRESULT: 0x%08X", result);
    }

    return result;
}

// Hooked DirectInput8Create function
HRESULT WINAPI DirectInput8Create_Detour(HINSTANCE hinst, DWORD dwVersion, REFIID riid, LPVOID *ppv,
                                         LPUNKNOWN punkOuter) {
    // Track hook call statistics
    g_hook_stats[HOOK_DirectInput8Create].increment_total();

    LogInfo("DirectInput8Create_Detour called - Version: 0x%08X", dwVersion);

    // Call original function
    HRESULT result = DirectInput8Create_Original ? DirectInput8Create_Original(hinst, dwVersion, riid, ppv, punkOuter)
                                                 : ERROR_CALL_NOT_IMPLEMENTED; // Function not available

    if (SUCCEEDED(result) && ppv && *ppv) {
        LogInfo("DirectInput8Create_Detour: Created DirectInput8 instance successfully");

        // Track unsuppressed call
        g_hook_stats[HOOK_DirectInput8Create].increment_unsuppressed();

        // Store the instance for tracking
        auto instance = std::make_unique<DirectInputInstance>();
        instance->instance = static_cast<IUnknown *>(*ppv);
        instance->is_directinput8 = true;
        g_directinput_instances.push_back(std::move(instance));
    } else {
        LogError("DirectInput8Create_Detour: Failed to create DirectInput8 instance - HRESULT: 0x%08X", result);
    }

    return result;
}

bool InstallDirectInputHooks() {
    if (g_directinput_hooks_installed.load()) {
        LogInfo("DirectInput hooks already installed");
        return true;
    }

    // Initialize MinHook (only if not already initialized)
    MH_STATUS init_status = MH_Initialize();
    if (init_status != MH_OK && init_status != MH_ERROR_ALREADY_INITIALIZED) {
        LogError("Failed to initialize MinHook for DirectInput hooks - Status: %d", init_status);
        return false;
    }

    if (init_status == MH_ERROR_ALREADY_INITIALIZED) {
        LogInfo("MinHook already initialized, proceeding with DirectInput hooks");
    } else {
        LogInfo("MinHook initialized successfully for DirectInput hooks");
    }

    // Load DirectInput DLL dynamically
    HMODULE dinput_module = LoadLibraryA("dinput8.dll");
    if (!dinput_module) {
        LogError("Failed to load dinput8.dll");
        return false;
    }

    // Get function addresses
    FARPROC directinput_create_a = GetProcAddress(dinput_module, "DirectInputCreateA");
    FARPROC directinput_create_w = GetProcAddress(dinput_module, "DirectInputCreateW");
    FARPROC directinput_create_ex = GetProcAddress(dinput_module, "DirectInputCreateEx");
    FARPROC directinput8_create = GetProcAddress(dinput_module, "DirectInput8Create");

    // Hook DirectInputCreateA if available
    if (directinput_create_a) {
        if (MH_CreateHook(directinput_create_a, DirectInputCreateA_Detour, (LPVOID *)&DirectInputCreateA_Original) !=
            MH_OK) {
            LogError("Failed to create DirectInputCreateA hook");
        }
    } else {
        LogInfo("DirectInputCreateA not available in dinput8.dll");
    }

    // Hook DirectInputCreateW if available
    if (directinput_create_w) {
        if (MH_CreateHook(directinput_create_w, DirectInputCreateW_Detour, (LPVOID *)&DirectInputCreateW_Original) !=
            MH_OK) {
            LogError("Failed to create DirectInputCreateW hook");
        }
    } else {
        LogInfo("DirectInputCreateW not available in dinput8.dll");
    }

    // Hook DirectInputCreateEx if available
    if (directinput_create_ex) {
        if (MH_CreateHook(directinput_create_ex, DirectInputCreateEx_Detour, (LPVOID *)&DirectInputCreateEx_Original) !=
            MH_OK) {
            LogError("Failed to create DirectInputCreateEx hook");
        }
    } else {
        LogInfo("DirectInputCreateEx not available in dinput8.dll");
    }

    // Hook DirectInput8Create if available
    if (directinput8_create) {
        if (MH_CreateHook(directinput8_create, DirectInput8Create_Detour, (LPVOID *)&DirectInput8Create_Original) !=
            MH_OK) {
            LogError("Failed to create DirectInput8Create hook");
        }
    } else {
        LogInfo("DirectInput8Create not available in dinput8.dll");
    }

    // Enable all hooks
    if (MH_EnableHook(MH_ALL_HOOKS) != MH_OK) {
        LogError("Failed to enable DirectInput hooks");
        return false;
    }

    g_directinput_hooks_installed.store(true);
    LogInfo("DirectInput hooks installed successfully");

    return true;
}

void UninstallDirectInputHooks() {
    if (!g_directinput_hooks_installed.load()) {
        LogInfo("DirectInput hooks not installed");
        return;
    }

    // Disable all hooks
    MH_DisableHook(MH_ALL_HOOKS);

    // Load DirectInput DLL dynamically to get function addresses for removal
    HMODULE dinput_module = LoadLibraryA("dinput8.dll");
    if (dinput_module) {
        // Get function addresses
        FARPROC directinput_create_a = GetProcAddress(dinput_module, "DirectInputCreateA");
        FARPROC directinput_create_w = GetProcAddress(dinput_module, "DirectInputCreateW");
        FARPROC directinput_create_ex = GetProcAddress(dinput_module, "DirectInputCreateEx");
        FARPROC directinput8_create = GetProcAddress(dinput_module, "DirectInput8Create");

        // Remove hooks if they were created
        if (directinput_create_a)
            MH_RemoveHook(directinput_create_a);
        if (directinput_create_w)
            MH_RemoveHook(directinput_create_w);
        if (directinput_create_ex)
            MH_RemoveHook(directinput_create_ex);
        if (directinput8_create)
            MH_RemoveHook(directinput8_create);

        FreeLibrary(dinput_module);
    }

    // Clean up
    DirectInputCreateA_Original = nullptr;
    DirectInputCreateW_Original = nullptr;
    DirectInputCreateEx_Original = nullptr;
    DirectInput8Create_Original = nullptr;

    // Clear tracked instances
    g_directinput_instances.clear();

    g_directinput_hooks_installed.store(false);
    LogInfo("DirectInput hooks uninstalled successfully");
}

bool AreDirectInputHooksInstalled() { return g_directinput_hooks_installed.load(); }

} // namespace display_commanderhooks
