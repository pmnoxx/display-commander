#pragma once

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
LONG WINAPI UnhandledExceptionHandler(EXCEPTION_POINTERS *exception_info);

} // namespace process_exit_hooks
