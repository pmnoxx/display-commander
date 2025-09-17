#include "exit_handler.hpp"
#include "display_restore.hpp"
#include <atomic>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <windows.h>
#include "utils.hpp"

namespace exit_handler {

// Atomic flag to prevent multiple exit calls
static std::atomic<bool> g_exit_handled{false};

// Helper function to write directly to debug.log and flush
void WriteToDebugLog(const std::string& message) {
    try {
        // Get the current time
        SYSTEMTIME time;
        GetLocalTime(&time);

        // Format timestamp
        std::ostringstream timestamp;
        timestamp << std::setfill('0')
                  << std::setw(2) << time.wHour << ":"
                  << std::setw(2) << time.wMinute << ":"
                  << std::setw(2) << time.wSecond << ":"
                  << std::setw(3) << time.wMilliseconds;

        // Format the complete log line
        std::ostringstream log_line;
        log_line << timestamp.str()
                 << " [" << std::setw(5) << GetCurrentThreadId() << "] | INFO  | "
                 << message << "\r\n";

        // Write directly to debug.log file
        std::ofstream log_file("debug.log", std::ios::app);
        if (log_file.is_open()) {
            log_file << log_line.str();
            log_file.flush(); // Force flush to ensure data is written
            log_file.close();
        }

        // Also output to debug console
        OutputDebugStringA(log_line.str().c_str());
    } catch (...) {
        // Ignore any errors during logging to prevent crashes
    }
}

void OnHandleExit(ExitSource source, const std::string& message) {
    // Use atomic compare_exchange to ensure only one thread handles exit
    bool expected = false;
    // Format the exit message
    std::ostringstream exit_message;
    exit_message << "[Exit Handler] Detected exit from "
                 << GetExitSourceString(source)
                 << ": " << message;

    // Write directly to debug.log and flush
    WriteToDebugLog(exit_message.str());

    if (!g_exit_handled.compare_exchange_strong(expected, true)) {
        // Another thread already handled the exit
        return;
    }
    LogInfo("%s", exit_message.str().c_str());

    // Best-effort display restoration on any exit
    display_restore::RestoreAllIfEnabled();
}

const char* GetExitSourceString(ExitSource source) {
    switch (source) {
        case ExitSource::DLL_PROCESS_DETACH_EVENT:  // IMPLEMENTED: Called in main_entry.cpp DLL_PROCESS_DETACH
            return "DLL_PROCESS_DETACH";
        case ExitSource::ATEXIT:                    // IMPLEMENTED: Called in process_exit_hooks.cpp AtExitHandler
            return "ATEXIT";
        case ExitSource::UNHANDLED_EXCEPTION:       // IMPLEMENTED: Called in process_exit_hooks.cpp UnhandledExceptionHandler
            return "UNHANDLED_EXCEPTION";
        case ExitSource::CONSOLE_CTRL:              // NOT IMPLEMENTED: No SetConsoleCtrlHandler found
            return "CONSOLE_CTRL";
        case ExitSource::WINDOW_QUIT:               // IMPLEMENTED: Called in hooks/window_proc_hooks.cpp WM_QUIT handler
            return "WINDOW_QUIT";
        case ExitSource::WINDOW_CLOSE:              // IMPLEMENTED: Called in hooks/window_proc_hooks.cpp WM_CLOSE handler
            return "WINDOW_CLOSE";
        case ExitSource::WINDOW_DESTROY:            // IMPLEMENTED: Called in hooks/window_proc_hooks.cpp WM_DESTROY handler
            return "WINDOW_DESTROY";
        case ExitSource::PROCESS_EXIT_HOOK:         // IMPLEMENTED: Called in hooks/process_exit_hooks.cpp ExitProcess_Detour
            return "PROCESS_EXIT_HOOK";
        case ExitSource::PROCESS_TERMINATE_HOOK:    // IMPLEMENTED: Called in hooks/process_exit_hooks.cpp TerminateProcess_Detour
            return "PROCESS_TERMINATE_HOOK";
        case ExitSource::THREAD_MONITOR:            // NOT IMPLEMENTED: No thread monitoring exit detection found
            return "THREAD_MONITOR";
        case ExitSource::MODULE_UNLOAD:             // NOT IMPLEMENTED: LoadLibrary hooks exist but no exit detection
            return "MODULE_UNLOAD";
        case ExitSource::UNKNOWN:                   // IMPLEMENTED: Default fallback case
        default:
            return "UNKNOWN";
    }
}

} // namespace exit_handler
