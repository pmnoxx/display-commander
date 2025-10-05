#include "process_exit_hooks.hpp"
#include "exit_handler.hpp"
#include "globals.hpp"
#include "stack_trace.hpp"
#include <atomic>
#include <cstdlib>
#include <windows.h>


namespace process_exit_hooks {

namespace {
std::atomic<bool> g_installed{false};
LPTOP_LEVEL_EXCEPTION_FILTER g_prev_filter = nullptr;

void AtExitHandler() {
    // Log exit detection
    exit_handler::OnHandleExit(exit_handler::ExitSource::ATEXIT, "Normal process exit via atexit");
}

LONG WINAPI UnhandledExceptionHandler(EXCEPTION_POINTERS *exception_info) {
    // Check if shutdown is in progress to avoid crashes during DLL unload
    if (g_shutdown.load()) {
        // During shutdown, just return without doing anything to avoid crashes
        return EXCEPTION_CONTINUE_EXECUTION;
    }

    // Capture stack trace if available
    if (stack_trace::IsAvailable() && exception_info != nullptr) {
        try {
            auto frames = stack_trace::CaptureStackTraceFromException(exception_info, 32);
            std::string stack_trace = stack_trace::FormatStackTrace(frames, true);

            // Log stack trace to debug.log
            exit_handler::WriteToDebugLog("=== CRASH STACK TRACE ===");
            exit_handler::WriteToDebugLog("Exception Code: 0x" + std::to_string(exception_info->ExceptionRecord->ExceptionCode));
            exit_handler::WriteToDebugLog("Exception Address: 0x" + std::to_string(reinterpret_cast<uintptr_t>(exception_info->ExceptionRecord->ExceptionAddress)));
            exit_handler::WriteToDebugLog("Stack Trace:");
            exit_handler::WriteToDebugLog(stack_trace);
            exit_handler::WriteToDebugLog("=== END STACK TRACE ===");
        } catch (...) {
            // If stack trace capture fails, just log that it failed
            exit_handler::WriteToDebugLog("Failed to capture stack trace during crash");
        }
    }

    // Log exit detection
    exit_handler::OnHandleExit(exit_handler::ExitSource::UNHANDLED_EXCEPTION, "Unhandled exception detected");
    // Chain to previous filter if any
    if (g_prev_filter != nullptr) {
        return g_prev_filter(exception_info);
    }
    return EXCEPTION_EXECUTE_HANDLER;
}
} // namespace

void Initialize() {
    bool expected = false;
    if (!g_installed.compare_exchange_strong(expected, true)) {
        return;
    }

    // Initialize stack trace functionality
    stack_trace::Initialize();

    // atexit for graceful exits
    std::atexit(&AtExitHandler);

    // SEH unhandled exception filter for most crash scenarios
    g_prev_filter = ::SetUnhandledExceptionFilter(&UnhandledExceptionHandler);
}

void Shutdown() {
    bool expected = true;
    if (!g_installed.compare_exchange_strong(expected, false)) {
        return;
    }

    // Shutdown stack trace functionality
    stack_trace::Shutdown();

    // Restore previous unhandled exception filter
    ::SetUnhandledExceptionFilter(g_prev_filter);
    g_prev_filter = nullptr;
}

} // namespace process_exit_hooks
