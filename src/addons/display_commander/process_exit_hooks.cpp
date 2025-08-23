#include "process_exit_hooks.hpp"
#include "display_restore.hpp"
#include <windows.h>
#include <cstdlib>
#include <atomic>

namespace renodx::process_exit_hooks {

namespace {
std::atomic<bool> g_installed{false};
LPTOP_LEVEL_EXCEPTION_FILTER g_prev_filter = nullptr;

void AtExitHandler() {
  // Best-effort restore on normal process exit
  renodx::display_restore::RestoreAllIfEnabled();
}

LONG WINAPI UnhandledExceptionHandler(EXCEPTION_POINTERS* exception_info) {
  // Best-effort restore on crash paths
  renodx::display_restore::RestoreAllIfEnabled();
  // Chain to previous filter if any
  if (g_prev_filter != nullptr) return g_prev_filter(exception_info);
  return EXCEPTION_EXECUTE_HANDLER;
}
} // namespace

void Initialize() {
  bool expected = false;
  if (!g_installed.compare_exchange_strong(expected, true)) return;

  // atexit for graceful exits
  std::atexit(&AtExitHandler);

  // SEH unhandled exception filter for most crash scenarios
  g_prev_filter = ::SetUnhandledExceptionFilter(&UnhandledExceptionHandler);
}

void Shutdown() {
  bool expected = true;
  if (!g_installed.compare_exchange_strong(expected, false)) return;
  // Restore previous unhandled exception filter
  ::SetUnhandledExceptionFilter(g_prev_filter);
  g_prev_filter = nullptr;
}

} // namespace renodx::process_exit_hooks


