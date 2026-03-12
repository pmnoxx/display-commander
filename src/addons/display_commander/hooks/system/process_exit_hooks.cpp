#include <MinHook.h>
#include <Windows.h>
#include <string>
#include "../../exit_handler.hpp"
#include "../../globals.hpp"
#include "../../nvapi/nvapi_init.hpp"
#include "../../utils/general_utils.hpp"
#include "../../utils/logging.hpp"
#include "../../utils/stack_trace.hpp"
#include "../../utils/timing.hpp"
#include "../hook_suppression_manager.hpp"

namespace display_commanderhooks {

// Function pointer types for process exit functions
using ExitProcess_pfn = void(WINAPI*)(UINT uExitCode);
using TerminateProcess_pfn = BOOL(WINAPI*)(HANDLE hProcess, UINT uExitCode);

// Original function pointers
ExitProcess_pfn ExitProcess_Original = nullptr;
TerminateProcess_pfn TerminateProcess_Original = nullptr;

// Hook state
static std::atomic<bool> g_process_exit_hooks_installed{false};

// Log caller module (captured early in detour), mode/state data, caller frame (stack frame 1), and full stack trace
static void LogExitCallerAndStackTrace(const char* exit_api_name, HMODULE caller_mod) {
    nvapi::EnsureNvApiInitialized();
    if (caller_mod != nullptr) {
        wchar_t module_path[MAX_PATH] = {};
        if (GetModuleFileNameW(caller_mod, module_path, MAX_PATH) != 0) {
            LogInfo("%s caller module: %ls", exit_api_name, module_path);
        } else {
            LogInfo("%s caller module: 0x%p (path unavailable)", exit_api_name, static_cast<void*>(caller_mod));
        }
    }
    // Mode/state data (same idea as crash report and stuck detection)
    uint64_t frame_id = g_global_frame_id.load(std::memory_order_acquire);
    LONGLONG last_updated_ns = g_global_frame_id_last_updated_ns.load(std::memory_order_acquire);
    const char* monitoring_section = g_continuous_monitoring_section.load(std::memory_order_acquire);
    const char* ui_section = g_rendering_ui_section.load(std::memory_order_acquire);
    std::string last_updated_str = "never";
    if (last_updated_ns != 0) {
        double ago_s = static_cast<double>(utils::get_real_time_ns() - last_updated_ns) / 1e9;
        last_updated_str = std::to_string(ago_s) + "s ago";
    }
    LogInfo("%s g_global_frame_id=%llu last_updated=%s", exit_api_name, static_cast<unsigned long long>(frame_id),
            last_updated_str.c_str());
    LogInfo("%s g_continuous_monitoring_section: %s", exit_api_name,
            monitoring_section != nullptr ? monitoring_section : "(null)");
    LogInfo("%s g_rendering_ui_section: %s", exit_api_name, ui_section != nullptr ? ui_section : "(null)");

    auto trace = stack_trace::GenerateStackTrace();
    const bool trace_unavailable = trace.empty()
                                   || (trace.size() == 1
                                       && (trace[0].find("cannot generate") != std::string::npos
                                           || trace[0].find("not available") != std::string::npos));
    if (trace_unavailable) {
        LogInfo("%s: stack trace unavailable (caller module logged above if present)", exit_api_name);
        return;
    }
    LogInfo("%s caller: %s", exit_api_name, (trace.size() >= 2 ? trace[1] : trace[0]).c_str());
    LogInfo("=== %s STACK TRACE ===", exit_api_name);
    std::string trace_str;
    for (size_t i = 0; i < trace.size(); ++i) {
        if (i != 0) trace_str += '\n';
        trace_str += trace[i];
    }
    exit_handler::WriteMultiLineToDebugLog(trace_str, "(no frames)");
    LogInfo("=== END %s STACK TRACE ===", exit_api_name);
}

// Best-effort process image path for logging (uses hProcess if provided, else opens by pid)
static void GetProcessImagePathForLog(HANDLE hProcess, DWORD pid, wchar_t* out_buf, size_t out_buf_chars) {
    out_buf[0] = L'\0';
    DWORD size = static_cast<DWORD>(out_buf_chars);
    if (QueryFullProcessImageNameW(hProcess, 0, out_buf, &size)) return;
    if (hProcess == GetCurrentProcess()) return;
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) return;
    size = static_cast<DWORD>(out_buf_chars);
    if (QueryFullProcessImageNameW(h, 0, out_buf, &size)) {
        CloseHandle(h);
        return;
    }
    CloseHandle(h);
}

// Hooked ExitProcess function
void WINAPI ExitProcess_Detour(UINT uExitCode) {
    HMODULE caller_mod = GetCallingDLL();  // capture as early as possible (before any other calls)
    LogExitCallerAndStackTrace("ExitProcess", caller_mod);

    if (g_no_exit_mode.load(std::memory_order_acquire)) {
        LogInfo("ExitProcess: .NO_EXIT active - blocking exit (exit code %u); opening independent UI.", uExitCode);
        RequestShowIndependentWindow();
        return;  // Block exit
    }

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
    DWORD current_pid = GetProcessId(GetCurrentProcess());
    DWORD target_pid = GetProcessId(hProcess);
    wchar_t image_path[MAX_PATH] = {};

    if (current_pid != 0 && target_pid != 0 && current_pid == target_pid) {
        if (g_no_exit_mode.load(std::memory_order_acquire)) {
            GetProcessImagePathForLog(GetCurrentProcess(), current_pid, image_path, MAX_PATH);
            LogInfo(
                "TerminateProcess: .NO_EXIT active - blocking terminate (target current process, exit code %u); "
                "opening independent UI. image: %ls",
                uExitCode, image_path[0] ? image_path : L"(unknown)");
            RequestShowIndependentWindow();
            return FALSE;  // Block termination
        }
        HMODULE caller_mod = GetCallingDLL();  // capture as early as possible (before any other calls)
        GetProcessImagePathForLog(GetCurrentProcess(), current_pid, image_path, MAX_PATH);
        LogInfo(
            "TerminateProcess: target is current process (current_pid == target_pid == %lu), triggering exit handler; "
            "image: %ls",
            static_cast<unsigned long>(current_pid), image_path[0] ? image_path : L"(unknown)");
        LogExitCallerAndStackTrace("TerminateProcess", caller_mod);
        exit_handler::OnHandleExit(exit_handler::ExitSource::PROCESS_TERMINATE_HOOK,
                                   "TerminateProcess called with exit code: " + std::to_string(uExitCode));
    } else {
        GetProcessImagePathForLog(hProcess, target_pid, image_path, MAX_PATH);
        LogInfo(
            "TerminateProcess: app is terminating another process (target pid %lu, current pid %lu); target image: %ls",
            static_cast<unsigned long>(target_pid), static_cast<unsigned long>(current_pid),
            image_path[0] ? image_path : L"(could not query)");
    }

    if (TerminateProcess_Original) {
        return TerminateProcess_Original(hProcess, uExitCode);
    }
    return TerminateProcess(hProcess, uExitCode);
}

bool InstallProcessExitHooks() {
    if (g_process_exit_hooks_installed.load()) {
        LogInfo("Process exit hooks already installed");
        return true;
    }

    // Check if process exit hooks should be suppressed
    if (display_commanderhooks::HookSuppressionManager::GetInstance().ShouldSuppressHook(
            display_commanderhooks::HookType::PROCESS_EXIT)) {
        LogInfo("Process exit hooks installation suppressed by user setting");
        return false;
    }

    // Initialize MinHook (only if not already initialized)
    MH_STATUS init_status = SafeInitializeMinHook(display_commanderhooks::HookType::PROCESS_EXIT);
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
    if (!CreateAndEnableHook(ExitProcess, ExitProcess_Detour, (LPVOID*)&ExitProcess_Original, "ExitProcess")) {
        LogError("Failed to create and enable ExitProcess hook");
        return false;
    }

    // Hook TerminateProcess
    if (!CreateAndEnableHook(TerminateProcess, TerminateProcess_Detour, (LPVOID*)&TerminateProcess_Original,
                             "TerminateProcess")) {
        LogError("Failed to create and enable TerminateProcess hook");
        return false;
    }

    g_process_exit_hooks_installed.store(true);
    LogInfo("Process exit hooks installed successfully");

    // Mark process exit hooks as installed
    display_commanderhooks::HookSuppressionManager::GetInstance().MarkHookInstalled(
        display_commanderhooks::HookType::PROCESS_EXIT);

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

}  // namespace display_commanderhooks
