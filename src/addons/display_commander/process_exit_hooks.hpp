#pragma once

#include <atomic>
#include <windows.h>

// Simple process-exit safety hooks to ensure display restore runs on normal
// exits and most unhandled crashes. This cannot handle hard kills
// (e.g. external TerminateProcess), but improves coverage when
// device destroy callbacks are skipped.
namespace process_exit_hooks {

// Install atexit handler and unhandled exception/terminate handlers.
void Initialize();

// Remove handlers if needed (best-effort, safe to call multiple times).
void Shutdown();

// Our custom unhandled exception handler function
LONG WINAPI UnhandledExceptionHandler(EXCEPTION_POINTERS* exception_info);

// Last handler set via SetUnhandledExceptionFilter_Detour (not our handler)
extern std::atomic<LPTOP_LEVEL_EXCEPTION_FILTER> g_last_detour_handler;

// Vectored exception handler handle (for AddVectoredExceptionHandler)
extern PVOID g_vectored_exception_handler_handle;

}  // namespace process_exit_hooks
