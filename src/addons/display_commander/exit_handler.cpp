#include "exit_handler.hpp"
#include <windows.h>
#include <atomic>
#include <reshade.hpp>
#include <sstream>
#include <vector>
#include "config/display_commander_config.hpp"
#include "display/display_restore.hpp"
#include "settings/advanced_tab_settings.hpp"
#include "settings/main_tab_settings.hpp"
#include "utils/general_utils.hpp"
#include "hooks/loadlibrary_hooks.hpp"
#include "presentmon/presentmon_manager.hpp"
#include "utils.hpp"
#include "utils/detour_call_tracker.hpp"
#include "utils/display_commander_logger.hpp"
#include "utils/logging.hpp"
#include "utils/taskbar_helper.hpp"
#include "utils/timing.hpp"

namespace exit_handler {

// Atomic flag to prevent multiple exit calls
static std::atomic<bool> g_exit_handled{false};

void WriteMultiLineToDebugLog(const std::string& text, const char* empty_fallback) {
    if (text.empty()) {
        return;
    }
    std::istringstream iss(text);
    std::string line;
    while (std::getline(iss, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (!line.empty()) {
            LogInfo("%s", line.c_str());
        }
    }
}

void OnHandleExit(ExitSource source, const std::string& message) {
    // Ensure only one thread performs exit logging and cleanup (avoids duplicate "[Exit Handler] Detected exit..."
    // lines)
    bool expected = false;
    if (!g_exit_handled.compare_exchange_strong(expected, true)) {
        return;
    }
    LogInfo("[exit_handler] OnHandleExit: Detected exit from %s: %s", GetExitSourceString(source), message.c_str());

    std::ostringstream exit_message;
    exit_message << "[exit_handler] Detected exit from " << GetExitSourceString(source) << ": " << message;
    LogInfo("%s", exit_message.str().c_str());

    // Print undestroyed guard information (crash detection)
    uint64_t exit_timestamp_ns = utils::get_real_time_ns();  // Use real time to avoid spoofed timers
    std::string undestroyed_guards_info = detour_call_tracker::FormatUndestroyedGuards(exit_timestamp_ns);
    LogInfo("=== UNDESTROYED DETOUR GUARDS (CRASH DETECTION) ===");
    WriteMultiLineToDebugLog(undestroyed_guards_info, "Undestroyed Detour Guards: 0");
    LogInfo("=== END UNDESTROYED DETOUR GUARDS ===");

    // Enumerate loaded modules and report any hookable module we never saw (e.g. LdrLoadDll or load before hooks)
    std::vector<std::string> missed_modules = display_commanderhooks::ReportMissedModulesOnExit();
    if (!missed_modules.empty()) {
        LogInfo("=== MISSED MODULES (loaded but we never received OnModuleLoaded) ===");
        for (const std::string& name : missed_modules) {
            LogInfo("Missed: %s", name.c_str());
        }
        LogInfo("=== END MISSED MODULES ===");
    }

    // Best-effort display restoration on any exit
    display_restore::RestoreAllIfEnabled();

    // Restore Windows taskbar if we hid it (e.g. ADHD auto-hide taskbar)
    display_commander::utils::RestoreTaskbarIfHidden();

    // ReShade config backup on exit when enabled (global or per-game)
    if (settings::g_advancedTabSettings.auto_enable_reshade_config_backup.GetValue()
        || settings::g_mainTabSettings.auto_reshade_config_backup.GetValue()) {
        CopyGameIniFilesToReshadeConfigBackupFolder();
    }

    display_commander::config::DisplayCommanderConfigManager::GetInstance().SetAutoFlushLogs(true);
    display_commander::logger::FlushLogs();

    if (presentmon::kPresentMonEnabled) {
        presentmon::StopAndDestroyPresentMon(presentmon::PresentMonStopReason::AddonShutdownExitHandler);
    }
    // Flush all logs before exit to ensure all messages are written to disk
}

const char* GetExitSourceString(ExitSource source) {
    switch (source) {
        case ExitSource::DLL_PROCESS_DETACH_EVENT:  // IMPLEMENTED: Called in main_entry.cpp DLL_PROCESS_DETACH
            return "DLL_PROCESS_DETACH";
        case ExitSource::ATEXIT:  // IMPLEMENTED: Called in process_exit_hooks.cpp AtExitHandler
            return "ATEXIT";
        case ExitSource::UNHANDLED_EXCEPTION:  // IMPLEMENTED: Called in process_exit_hooks.cpp
                                               // UnhandledExceptionHandler
            return "UNHANDLED_EXCEPTION";
        case ExitSource::CONSOLE_CTRL:  // NOT IMPLEMENTED: No SetConsoleCtrlHandler found
            return "CONSOLE_CTRL";
        case ExitSource::WINDOW_QUIT:  // IMPLEMENTED: Called in hooks/window_proc_hooks.cpp WM_QUIT handler
            return "WINDOW_QUIT";
        case ExitSource::WINDOW_CLOSE:  // IMPLEMENTED: Called in hooks/window_proc_hooks.cpp WM_CLOSE handler
            return "WINDOW_CLOSE";
        case ExitSource::WINDOW_DESTROY:  // IMPLEMENTED: Called in hooks/window_proc_hooks.cpp WM_DESTROY handler
            return "WINDOW_DESTROY";
        case ExitSource::PROCESS_EXIT_HOOK:  // IMPLEMENTED: Called in hooks/process_exit_hooks.cpp ExitProcess_Detour
            return "PROCESS_EXIT_HOOK";
        case ExitSource::PROCESS_TERMINATE_HOOK:  // IMPLEMENTED: Called in hooks/process_exit_hooks.cpp
                                                  // TerminateProcess_Detour
            return "PROCESS_TERMINATE_HOOK";
        case ExitSource::THREAD_MONITOR:  // NOT IMPLEMENTED: No thread monitoring exit detection found
            return "THREAD_MONITOR";
        case ExitSource::MODULE_UNLOAD:  // NOT IMPLEMENTED: LoadLibrary hooks exist but no exit detection
            return "MODULE_UNLOAD";
        case ExitSource::UNKNOWN:  // IMPLEMENTED: Default fallback case
        default:                  return "UNKNOWN";
    }
}

}  // namespace exit_handler
