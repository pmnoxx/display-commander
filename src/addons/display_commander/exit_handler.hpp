#pragma once

#include <cstdint>
#include <string>

namespace exit_handler {

// Enum indicating where the exit was detected
enum class ExitSource : std::uint8_t {
    DLL_PROCESS_DETACH_EVENT,  // DLL_PROCESS_DETACH in DllMain
    ATEXIT,                    // std::atexit() handler
    UNHANDLED_EXCEPTION,       // SetUnhandledExceptionFilter() handler
    CONSOLE_CTRL,              // SetConsoleCtrlHandler() handler
    WINDOW_QUIT,               // WM_QUIT message
    WINDOW_CLOSE,              // WM_CLOSE message
    WINDOW_DESTROY,            // WM_DESTROY message
    PROCESS_EXIT_HOOK,         // ExitProcess hook
    PROCESS_TERMINATE_HOOK,    // TerminateProcess hook
    THREAD_MONITOR,            // Thread monitoring
    MODULE_UNLOAD,             // FreeLibrary/LoadLibrary hooks
    UNKNOWN                    // Unknown source
};

// Centralized exit handler function
void OnHandleExit(ExitSource source, const std::string& message);

// Helper function to get string representation of exit source
const char* GetExitSourceString(ExitSource source);

// Write multi-line text to debug log line by line; if text is empty, write empty_fallback once.
// Shared by crash report, stuck detection, and exit handler.
void WriteMultiLineToDebugLog(const std::string& text, const char* empty_fallback);

}  // namespace exit_handler
