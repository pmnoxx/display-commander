#include "process_exit_hooks.hpp"
#include "exit_handler.hpp"
#include "globals.hpp"
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

    // Log exit detection
    exit_handler::OnHandleExit(exit_handler::ExitSource::UNHANDLED_EXCEPTION, "Unhandled exception detected");
    // Chain to previous filter if any
    if (g_prev_filter != nullptr)
        return g_prev_filter(exception_info);
    return EXCEPTION_EXECUTE_HANDLER;
}
} // namespace

void Initialize() {
    bool expected = false;
    if (!g_installed.compare_exchange_strong(expected, true))
        return;

    // atexit for graceful exits
    std::atexit(&AtExitHandler);

    // SEH unhandled exception filter for most crash scenarios
    g_prev_filter = ::SetUnhandledExceptionFilter(&UnhandledExceptionHandler);
}

void Shutdown() {
    bool expected = true;
    if (!g_installed.compare_exchange_strong(expected, false))
        return;
    // Restore previous unhandled exception filter
    ::SetUnhandledExceptionFilter(g_prev_filter);
    g_prev_filter = nullptr;
}

} // namespace process_exit_hooks
