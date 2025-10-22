#include "process_exit_hooks.hpp"
#include "exit_handler.hpp"
#include "globals.hpp"
#include "utils/stack_trace.hpp"
#include "dbghelp_loader.hpp"
#include <atomic>
#include <cstdlib>
#include <windows.h>


namespace process_exit_hooks {

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

    // Ensure DbgHelp is loaded before attempting stack trace
    dbghelp_loader::LoadDbgHelp();

    // Log detailed crash information similar to Special-K's approach
    exit_handler::WriteToDebugLog("=== CRASH DETECTED - DETAILED CRASH REPORT ===");

    // Log exception information
    if (exception_info && exception_info->ExceptionRecord) {
        std::ostringstream exception_details;
        exception_details << "Exception Code: 0x" << std::hex << std::uppercase
                         << exception_info->ExceptionRecord->ExceptionCode;
        exit_handler::WriteToDebugLog(exception_details.str());

        // Log exception flags
        std::ostringstream flags_details;
        flags_details << "Exception Flags: 0x" << std::hex << std::uppercase
                     << exception_info->ExceptionRecord->ExceptionFlags;
        exit_handler::WriteToDebugLog(flags_details.str());

        // Log exception address
        std::ostringstream address_details;
        address_details << "Exception Address: 0x" << std::hex << std::uppercase
                       << reinterpret_cast<uintptr_t>(exception_info->ExceptionRecord->ExceptionAddress);
        exit_handler::WriteToDebugLog(address_details.str());
    }

    // Log system information
    MEMORYSTATUSEX mem_status = {};
    mem_status.dwLength = sizeof(mem_status);
    if (GlobalMemoryStatusEx(&mem_status)) {
        std::ostringstream mem_details;
        mem_details << "System Memory Load: " << mem_status.dwMemoryLoad << "%";
        exit_handler::WriteToDebugLog(mem_details.str());
    }

    // Print stack trace to DbgView
    exit_handler::WriteToDebugLog("=== GENERATING STACK TRACE ===");
    stack_trace::PrintStackTraceToDbgView();

    // Also log stack trace to file
    std::string stack_trace_string = stack_trace::GetStackTraceString();
    exit_handler::WriteToDebugLog(stack_trace_string);

    // Log exit detection
    exit_handler::OnHandleExit(exit_handler::ExitSource::UNHANDLED_EXCEPTION, "Unhandled exception detected");

    // Chain to previous filter if any
    // if (g_prev_filter != nullptr) {
      //   return g_prev_filter(exception_info);
    // }
     //return EXCEPTION_EXECUTE_HANDLER;
     assert(IsDebuggerPresent());
     return EXCEPTION_CONTINUE_EXECUTION;
}

void Initialize() {
    bool expected = false;
    if (!g_installed.compare_exchange_strong(expected, true)) {
        return;
    }

    // atexit for graceful exits
    std::atexit(&AtExitHandler);

    // SEH unhandled exception filter for most crash scenarios
    exit_handler::WriteToDebugLog("Installing SEH unhandled exception filter");
    g_prev_filter = ::SetUnhandledExceptionFilter(&UnhandledExceptionHandler);
}

void Shutdown() {
    bool expected = true;
    if (!g_installed.compare_exchange_strong(expected, false)) {
        return;
    }

    // Restore previous unhandled exception filter
    ::SetUnhandledExceptionFilter(g_prev_filter);
    g_prev_filter = nullptr;
}

} // namespace process_exit_hooks
