# App Closing Detection Checklist

## Core Detection Mechanisms

- [ ] DLL_PROCESS_DETACH handler in DllMain
- [ ] DLL_THREAD_DETACH handler in DllMain
- [ ] std::atexit() handler for normal program termination
- [ ] SetUnhandledExceptionFilter() for crash detection
- [ ] SetConsoleCtrlHandler() for console app termination (Ctrl+C, Ctrl+Break)
- [ ] Windows signal handlers (SIGINT, SIGTERM, SIGABRT)
- [ ] Process exit hooks (ExitProcess, TerminateProcess) using MinHook
- [ ] Window message monitoring (WM_QUIT, WM_CLOSE, WM_DESTROY)
- [ ] Thread monitoring for main thread termination
- [ ] Module pinning to prevent premature DLL unload
- [ ] Unload library detection (FreeLibrary, LoadLibrary hooks)

## Implementation Details

- [ ] Create centralized exit handler function
- [ ] Use atomic flags to prevent multiple exit calls
- [ ] Implement proper cleanup sequence (hooks → resources → display restore)
- [ ] Add debug logging for all exit scenarios
- [ ] Use idempotent cleanup functions
- [ ] Handle exceptions during cleanup gracefully
- [ ] Test all exit scenarios (normal, crash, forced termination)
- [ ] Ensure display restoration works in all cases
- [ ] Add timeout mechanisms for cleanup operations
- [ ] Use thread-safe cleanup procedures
