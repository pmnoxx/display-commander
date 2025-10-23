#include "../exit_handler.hpp"
#include "../utils.hpp"
#include "../utils/general_utils.hpp"
#include <MinHook.h>

namespace display_commanderhooks {

// Function pointer types for process exit functions
using ExitProcess_pfn = void(WINAPI *)(UINT uExitCode);
using TerminateProcess_pfn = BOOL(WINAPI *)(HANDLE hProcess, UINT uExitCode);

// Original function pointers
ExitProcess_pfn ExitProcess_Original = nullptr;
TerminateProcess_pfn TerminateProcess_Original = nullptr;

// Hook state
static std::atomic<bool> g_process_exit_hooks_installed{false};

// Hooked ExitProcess function
void WINAPI ExitProcess_Detour(UINT uExitCode) {
    // Log exit detection
    exit_handler::OnHandleExit(exit_handler::ExitSource::PROCESS_EXIT_HOOK,
                               "ExitProcess called with exit code: " + std::to_string(uExitCode));

    // Call original function
    if (ExitProcess_Original) {
        ExitProcess_Original(uExitCode);
    } else {
        ExitProcess(uExitCode);
    }
}

// Hooked TerminateProcess function
BOOL WINAPI TerminateProcess_Detour(HANDLE hProcess, UINT uExitCode) {
    // Log exit detection
    exit_handler::OnHandleExit(exit_handler::ExitSource::PROCESS_TERMINATE_HOOK,
                               "TerminateProcess called with exit code: " + std::to_string(uExitCode));

    // Call original function
    if (TerminateProcess_Original) {
        return TerminateProcess_Original(hProcess, uExitCode);
    } else {
        return TerminateProcess(hProcess, uExitCode);
    }
}

bool InstallProcessExitHooks() {
    if (g_process_exit_hooks_installed.load()) {
        LogInfo("Process exit hooks already installed");
        return true;
    }

    // Initialize MinHook (only if not already initialized)
    MH_STATUS init_status = SafeInitializeMinHook();
    if (init_status != MH_OK && init_status != MH_ERROR_ALREADY_INITIALIZED) {
        LogError("Failed to initialize MinHook for process exit hooks - Status: %d", init_status);
        return false;
    }

    if (init_status == MH_ERROR_ALREADY_INITIALIZED) {
        LogInfo("MinHook already initialized, proceeding with process exit hooks");
    } else {
        LogInfo("MinHook initialized successfully for process exit hooks");
    }

    // Hook ExitProcess
    if (!CreateAndEnableHook(ExitProcess, ExitProcess_Detour, (LPVOID *)&ExitProcess_Original, "ExitProcess")) {
        LogError("Failed to create and enable ExitProcess hook");
        return false;
    }

    // Hook TerminateProcess
    if (!CreateAndEnableHook(TerminateProcess, TerminateProcess_Detour, (LPVOID *)&TerminateProcess_Original, "TerminateProcess")) {
        LogError("Failed to create and enable TerminateProcess hook");
        return false;
    }

    g_process_exit_hooks_installed.store(true);
    LogInfo("Process exit hooks installed successfully");

    return true;
}

void UninstallProcessExitHooks() {
    if (!g_process_exit_hooks_installed.load()) {
        LogInfo("Process exit hooks not installed");
        return;
    }

    // Disable hooks
    MH_DisableHook(MH_ALL_HOOKS);

    // Remove hooks
    MH_RemoveHook(ExitProcess);
    MH_RemoveHook(TerminateProcess);

    // Clean up
    ExitProcess_Original = nullptr;
    TerminateProcess_Original = nullptr;

    g_process_exit_hooks_installed.store(false);
    LogInfo("Process exit hooks uninstalled successfully");
}

} // namespace display_commanderhooks
