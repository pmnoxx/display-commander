#include "debug_output_hooks.hpp"
#include "../utils/general_utils.hpp"
#include "../globals.hpp"
#include "../settings/experimental_tab_settings.hpp"
#include <MinHook.h>
#include <atomic>
#include <string>
#include <sstream>

namespace display_commanderhooks::debug_output {

// Original function pointers
OutputDebugStringA_pfn OutputDebugStringA_Original = nullptr;
OutputDebugStringW_pfn OutputDebugStringW_Original = nullptr;

// Hook state
static std::atomic<bool> g_debug_output_hooks_installed{false};

// Statistics
static DebugOutputStats g_debug_output_stats;

// Helper function to safely convert wide string to narrow string
std::string WideStringToNarrowString(LPCWSTR wideStr) {
    if (!wideStr) return "";

    int size = WideCharToMultiByte(CP_UTF8, 0, wideStr, -1, nullptr, 0, nullptr, nullptr);
    if (size <= 0) return "";

    std::string result(size - 1, 0);
    WideCharToMultiByte(CP_UTF8, 0, wideStr, -1, &result[0], size, nullptr, nullptr);
    return result;
}

// Helper function to log debug output with proper formatting
void LogDebugOutput(const std::string& function_name, const std::string& message) {
    // Check if logging to ReShade is enabled
    if (!settings::g_experimentalTabSettings.debug_output_log_to_reshade.GetValue()) {
        return;
    }


    // Log to ReShade log
    LogInfo("[Debug Output] %s: %s", function_name.c_str(), message.c_str());

    // Update statistics
    g_debug_output_stats.total_bytes_logged.fetch_add(message.length());
}

// Hooked OutputDebugStringA function
void WINAPI OutputDebugStringA_Detour(LPCSTR lpOutputString) {
    // Update call count
    g_debug_output_stats.output_debug_string_a_calls.fetch_add(1);

    // Log the debug output if enabled
    if (lpOutputString != nullptr) {
        LogDebugOutput("OutputDebugStringA", lpOutputString);
    } else {
        // Log when we get nullptr strings for debugging
        LogInfo("[Debug Output] OutputDebugStringA called with nullptr string");
    }

    // Call original function
    if (OutputDebugStringA_Original) {
        OutputDebugStringA_Original(lpOutputString);
    } else {
        // Fallback to system function if hook failed
        OutputDebugStringA(lpOutputString);
    }
}

// Hooked OutputDebugStringW function
void WINAPI OutputDebugStringW_Detour(LPCWSTR lpOutputString) {
    // Update call count
    g_debug_output_stats.output_debug_string_w_calls.fetch_add(1);

    // Log the debug output if enabled
    if (lpOutputString != nullptr) {
        std::string narrow_string = WideStringToNarrowString(lpOutputString);
        LogDebugOutput("OutputDebugStringW", narrow_string);
    } else {
        // Log when we get nullptr strings for debugging
        LogInfo("[Debug Output] OutputDebugStringW called with nullptr string");
    }

    // Call original function
    if (OutputDebugStringW_Original) {
        OutputDebugStringW_Original(lpOutputString);
    } else {
        // Fallback to system function if hook failed
        OutputDebugStringW(lpOutputString);
    }
}

bool InstallDebugOutputHooks() {
    if (g_debug_output_hooks_installed.load()) {
        LogInfo("Debug output hooks already installed");
        return true;
    }

    // Initialize MinHook (only if not already initialized)
    MH_STATUS init_status = MH_Initialize();
    if (init_status != MH_OK && init_status != MH_ERROR_ALREADY_INITIALIZED) {
        LogError("Failed to initialize MinHook for debug output hooks - Status: %d", init_status);
        return false;
    }

    if (init_status == MH_ERROR_ALREADY_INITIALIZED) {
        LogInfo("MinHook already initialized, proceeding with debug output hooks");
    } else {
        LogInfo("MinHook initialized successfully for debug output hooks");
    }

    // Get kernel32.dll module handle
    HMODULE kernel32_module = GetModuleHandleW(L"kernel32.dll");
    if (kernel32_module == nullptr) {
        LogError("Failed to get kernel32.dll module handle");
        return false;
    }

    // Get the actual function addresses
    auto OutputDebugStringA_sys = reinterpret_cast<decltype(&OutputDebugStringA)>(GetProcAddress(kernel32_module, "OutputDebugStringA"));
    auto OutputDebugStringW_sys = reinterpret_cast<decltype(&OutputDebugStringW)>(GetProcAddress(kernel32_module, "OutputDebugStringW"));

    if (OutputDebugStringA_sys == nullptr) {
        LogError("Failed to get OutputDebugStringA address from kernel32.dll");
        return false;
    }

    if (OutputDebugStringW_sys == nullptr) {
        LogError("Failed to get OutputDebugStringW address from kernel32.dll");
        return false;
    }

    // Hook OutputDebugStringA using the actual function address
    if (!CreateAndEnableHook(OutputDebugStringA_sys, OutputDebugStringA_Detour,
                            reinterpret_cast<LPVOID *>(&OutputDebugStringA_Original), "OutputDebugStringA")) {
        LogError("Failed to create and enable OutputDebugStringA hook");
        return false;
    }

    // Hook OutputDebugStringW using the actual function address
    if (!CreateAndEnableHook(OutputDebugStringW_sys, OutputDebugStringW_Detour,
                            reinterpret_cast<LPVOID *>(&OutputDebugStringW_Original), "OutputDebugStringW")) {
        LogError("Failed to create and enable OutputDebugStringW hook");
        return false;
    }

    g_debug_output_hooks_installed.store(true);
    LogInfo("Debug output hooks installed successfully - OutputDebugStringA: %p, OutputDebugStringW: %p",
            OutputDebugStringA_sys, OutputDebugStringW_sys);

    return true;
}

void UninstallDebugOutputHooks() {
    if (!g_debug_output_hooks_installed.load()) {
        LogInfo("Debug output hooks not installed");
        return;
    }

    // Get kernel32.dll module handle
    HMODULE kernel32_module = GetModuleHandleW(L"kernel32.dll");
    if (kernel32_module != nullptr) {
        // Get the actual function addresses
        auto OutputDebugStringA_sys = reinterpret_cast<decltype(&OutputDebugStringA)>(GetProcAddress(kernel32_module, "OutputDebugStringA"));
        auto OutputDebugStringW_sys = reinterpret_cast<decltype(&OutputDebugStringW)>(GetProcAddress(kernel32_module, "OutputDebugStringW"));

        // Disable hooks using actual addresses
        if (OutputDebugStringA_sys) {
            MH_DisableHook(OutputDebugStringA_sys);
        }
        if (OutputDebugStringW_sys) {
            MH_DisableHook(OutputDebugStringW_sys);
        }

        // Remove hooks using actual addresses
        if (OutputDebugStringA_sys) {
            MH_RemoveHook(OutputDebugStringA_sys);
        }
        if (OutputDebugStringW_sys) {
            MH_RemoveHook(OutputDebugStringW_sys);
        }
    }

    // Clean up
    OutputDebugStringA_Original = nullptr;
    OutputDebugStringW_Original = nullptr;

    g_debug_output_hooks_installed.store(false);
    LogInfo("Debug output hooks uninstalled successfully");
}


DebugOutputStats& GetDebugOutputStats() {
    return g_debug_output_stats;
}

} // namespace display_commanderhooks::debug_output
