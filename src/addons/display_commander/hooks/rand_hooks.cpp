#include "rand_hooks.hpp"
#include <errno.h>
#include <MinHook.h>
#include <atomic>
#include "../globals.hpp"
#include "../settings/experimental_tab_settings.hpp"
#include "../utils/general_utils.hpp"
#include "../utils/logging.hpp"
#include "hook_suppression_manager.hpp"

namespace display_commanderhooks {

// Original function pointers
Rand_pfn Rand_Original = nullptr;
Rand_s_pfn Rand_s_Original = nullptr;

// Hook state
std::atomic<bool> g_rand_hooks_installed{false};

// Call counters
std::atomic<uint64_t> g_rand_call_count{0};
std::atomic<uint64_t> g_rand_s_call_count{0};

// Hooked rand function
int __cdecl Rand_Detour() {
    g_rand_call_count.fetch_add(1, std::memory_order_relaxed);

    // Check if rand hook is enabled
    if (settings::g_experimentalTabSettings.rand_hook_enabled.GetValue()) {
        // Return the configured constant value
        int constant_value = settings::g_experimentalTabSettings.rand_hook_value.GetValue();
        return constant_value;
    }

    // Call original function if hook is disabled
    if (Rand_Original) {
        return Rand_Original();
    }

    // Fallback to system rand if original is not available
    return rand();
}

// Hooked rand_s function
errno_t __cdecl Rand_s_Detour(unsigned int* randomValue) {
    g_rand_s_call_count.fetch_add(1, std::memory_order_relaxed);

    // Check if rand_s hook is enabled
    if (settings::g_experimentalTabSettings.rand_s_hook_enabled.GetValue()) {
        // Set the configured constant value
        *randomValue = static_cast<unsigned int>(settings::g_experimentalTabSettings.rand_s_hook_value.GetValue());
        return 0;  // Success
    }

    // Check if randomValue pointer is valid
    if (randomValue == nullptr) {
        // Return error if pointer is null (matching standard behavior)
        if (Rand_s_Original) {
            return Rand_s_Original(nullptr);
        }
        return EINVAL;
    }

    // Call original function if hook is disabled
    if (Rand_s_Original) {
        return Rand_s_Original(randomValue);
    }

    // Fallback: try to call system rand_s if original is not available
    // Note: This might not work if rand_s is not available
    return EINVAL;  // Return error if we can't call the original
}

// Install rand hooks
bool InstallRandHooks() {
    if (!enabled_experimental_features) {
        return true;
    }
    if (g_rand_hooks_installed.load()) {
        LogInfo("Rand hooks already installed");
        return true;
    }

    // Check if rand hooks should be suppressed
    if (display_commanderhooks::HookSuppressionManager::GetInstance().ShouldSuppressHook(
            display_commanderhooks::HookType::API)) {
        LogInfo("Rand hooks installation suppressed by user setting");
        return false;
    }

    // Initialize MinHook (only if not already initialized)
    MH_STATUS init_status = SafeInitializeMinHook(display_commanderhooks::HookType::API);
    if (init_status != MH_OK && init_status != MH_ERROR_ALREADY_INITIALIZED) {
        LogError("Failed to initialize MinHook for rand hooks - Status: %d", init_status);
        return false;
    }

    if (init_status == MH_ERROR_ALREADY_INITIALIZED) {
        LogInfo("MinHook already initialized, proceeding with rand hooks");
    } else {
        LogInfo("MinHook initialized successfully for rand hooks");
    }

    LogInfo("Installing rand hooks...");

    bool any_hook_installed = false;

    // Try to hook rand from msvcrt.dll (older Windows)
    HMODULE msvcrt_module = GetModuleHandleW(L"msvcrt.dll");
    if (msvcrt_module != nullptr) {
        auto rand_msvcrt = reinterpret_cast<Rand_pfn>(GetProcAddress(msvcrt_module, "rand"));
        if (rand_msvcrt != nullptr) {
            if (!CreateAndEnableHook(rand_msvcrt, Rand_Detour, reinterpret_cast<LPVOID*>(&Rand_Original),
                                     "rand (msvcrt)")) {
                LogWarn("Failed to create and enable rand hook from msvcrt.dll");
            } else {
                LogInfo("Rand hook from msvcrt.dll created successfully");
                any_hook_installed = true;
            }
        } else {
            LogWarn("Failed to get rand address from msvcrt.dll");
        }

        // Try to hook rand_s from msvcrt.dll
        auto rand_s_msvcrt = reinterpret_cast<Rand_s_pfn>(GetProcAddress(msvcrt_module, "rand_s"));
        if (rand_s_msvcrt != nullptr) {
            if (!CreateAndEnableHook(rand_s_msvcrt, Rand_s_Detour, reinterpret_cast<LPVOID*>(&Rand_s_Original),
                                     "rand_s (msvcrt)")) {
                LogWarn("Failed to create and enable rand_s hook from msvcrt.dll");
            } else {
                LogInfo("Rand_s hook from msvcrt.dll created successfully");
                any_hook_installed = true;
            }
        } else {
            LogWarn("Failed to get rand address from msvcrt.dll");
        }
    } else {
        LogInfo("msvcrt.dll not loaded, trying ucrtbase.dll");
    }

    // Try to hook rand from ucrtbase.dll (Windows 10+)
    HMODULE ucrtbase_module = GetModuleHandleW(L"ucrtbase.dll");
    if (ucrtbase_module != nullptr) {
        auto rand_ucrtbase = reinterpret_cast<Rand_pfn>(GetProcAddress(ucrtbase_module, "rand"));
        if (rand_ucrtbase != nullptr) {
            // If we already hooked from msvcrt, we'll hook ucrtbase too (some games use both)
            if (Rand_Original == nullptr) {
                if (!CreateAndEnableHook(rand_ucrtbase, Rand_Detour, reinterpret_cast<LPVOID*>(&Rand_Original),
                                         "rand (ucrtbase)")) {
                    LogWarn("Failed to create and enable rand hook from ucrtbase.dll");
                } else {
                    LogInfo("Rand hook from ucrtbase.dll created successfully");
                    any_hook_installed = true;
                }
            } else {
                // Hook ucrtbase version separately if msvcrt was already hooked
                Rand_pfn rand_ucrtbase_original = nullptr;
                if (!CreateAndEnableHook(rand_ucrtbase, Rand_Detour, reinterpret_cast<LPVOID*>(&rand_ucrtbase_original),
                                         "rand (ucrtbase)")) {
                    LogWarn("Failed to create and enable rand hook from ucrtbase.dll (second instance)");
                } else {
                    LogInfo("Rand hook from ucrtbase.dll created successfully (second instance)");
                    any_hook_installed = true;
                }
            }
        } else {
            LogWarn("Failed to get rand address from ucrtbase.dll");
        }

        // Try to hook rand_s from ucrtbase.dll
        auto rand_s_ucrtbase = reinterpret_cast<Rand_s_pfn>(GetProcAddress(ucrtbase_module, "rand_s"));
        if (rand_s_ucrtbase != nullptr) {
            // If we already hooked from msvcrt, we'll hook ucrtbase too (some games use both)
            if (Rand_s_Original == nullptr) {
                if (!CreateAndEnableHook(rand_s_ucrtbase, Rand_s_Detour, reinterpret_cast<LPVOID*>(&Rand_s_Original),
                                         "rand_s (ucrtbase)")) {
                    LogWarn("Failed to create and enable rand_s hook from ucrtbase.dll");
                } else {
                    LogInfo("Rand_s hook from ucrtbase.dll created successfully");
                    any_hook_installed = true;
                }
            } else {
                // Hook ucrtbase version separately if msvcrt was already hooked
                Rand_s_pfn rand_s_ucrtbase_original = nullptr;
                if (!CreateAndEnableHook(rand_s_ucrtbase, Rand_s_Detour,
                                         reinterpret_cast<LPVOID*>(&rand_s_ucrtbase_original), "rand_s (ucrtbase)")) {
                    LogWarn("Failed to create and enable rand_s hook from ucrtbase.dll (second instance)");
                } else {
                    LogInfo("Rand_s hook from ucrtbase.dll created successfully (second instance)");
                    any_hook_installed = true;
                }
            }
        } else {
            LogWarn("Failed to get rand_s address from ucrtbase.dll");
        }
    } else {
        LogInfo("ucrtbase.dll not loaded");
    }

    // Also try to hook the standard rand function (might be statically linked)
    // Note: This might not work if rand is statically linked, but worth trying
    if (Rand_Original == nullptr) {
        // Try to get address of rand from the current module
        // This is a fallback for statically linked rand
        LogInfo("Attempting to hook statically linked rand function");
        // Note: We can't easily hook statically linked functions without knowing their address
        // This would require more complex techniques like IAT hooking
    }

    if (any_hook_installed) {
        g_rand_hooks_installed.store(true);
        LogInfo("Rand hooks installed successfully");
    } else {
        LogWarn("Rand hooks installation completed but no hooks were successfully created");
    }

    return g_rand_hooks_installed.load();
}

// Uninstall rand hooks
void UninstallRandHooks() {
    if (!g_rand_hooks_installed.load()) {
        LogInfo("Rand hooks not installed");
        return;
    }

    LogInfo("Uninstalling rand hooks...");

    // Disable hooks
    MH_DisableHook(MH_ALL_HOOKS);

    // Try to remove hooks from both DLLs
    HMODULE msvcrt_module = GetModuleHandleW(L"msvcrt.dll");
    if (msvcrt_module != nullptr) {
        auto rand_msvcrt = reinterpret_cast<Rand_pfn>(GetProcAddress(msvcrt_module, "rand"));
        if (rand_msvcrt != nullptr) {
            MH_RemoveHook(rand_msvcrt);
        }
        auto rand_s_msvcrt = reinterpret_cast<Rand_s_pfn>(GetProcAddress(msvcrt_module, "rand_s"));
        if (rand_s_msvcrt != nullptr) {
            MH_RemoveHook(rand_s_msvcrt);
        }
    }

    HMODULE ucrtbase_module = GetModuleHandleW(L"ucrtbase.dll");
    if (ucrtbase_module != nullptr) {
        auto rand_ucrtbase = reinterpret_cast<Rand_pfn>(GetProcAddress(ucrtbase_module, "rand"));
        if (rand_ucrtbase != nullptr) {
            MH_RemoveHook(rand_ucrtbase);
        }
        auto rand_s_ucrtbase = reinterpret_cast<Rand_s_pfn>(GetProcAddress(ucrtbase_module, "rand_s"));
        if (rand_s_ucrtbase != nullptr) {
            MH_RemoveHook(rand_s_ucrtbase);
        }
    }

    // Clean up
    Rand_Original = nullptr;
    Rand_s_Original = nullptr;
    g_rand_hooks_installed.store(false);

    LogInfo("Rand hooks uninstalled successfully");
}

// Check if rand hooks are installed
bool AreRandHooksInstalled() { return g_rand_hooks_installed.load(); }

// Get call count
uint64_t GetRandCallCount() { return g_rand_call_count.load(); }

// Get rand_s call count
uint64_t GetRand_sCallCount() { return g_rand_s_call_count.load(); }

}  // namespace display_commanderhooks
