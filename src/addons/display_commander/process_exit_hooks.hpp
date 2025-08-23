#pragma once


// Simple process-exit safety hooks to ensure display restore runs on normal
// exits and most unhandled crashes. This cannot handle hard kills
// (e.g. external TerminateProcess), but improves coverage when
// device destroy callbacks are skipped.
namespace renodx::process_exit_hooks {

// Install atexit handler and unhandled exception/terminate handlers.
void Initialize();

// Remove handlers if needed (best-effort, safe to call multiple times).
void Shutdown();

} // namespace renodx::process_exit_hooks


