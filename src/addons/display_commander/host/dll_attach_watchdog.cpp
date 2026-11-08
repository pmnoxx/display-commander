// Source Code <Display Commander> // follow this order for includes in all files + add this comment at the top
#include "dll_attach_watchdog.hpp"

// Source Code <Display Commander>
#include "../globals.hpp"

// Libraries <standard C++>
#include <atomic>

// Libraries <Windows.h>
#include <Windows.h>

namespace {

constexpr DWORD kDllProcessAttachWatchdogTimeoutMs = 30'000;

std::atomic<bool> g_watchdog_started{false};
std::atomic<bool> g_watchdog_cancel{false};
std::atomic<HANDLE> g_watchdog_thread{nullptr};

DWORD WINAPI DllProcessAttachWatchdogThread(LPVOID /*param*/) {
    const DWORD deadline = GetTickCount() + kDllProcessAttachWatchdogTimeoutMs;
    while (GetTickCount() < deadline) {
        if (g_watchdog_cancel.load(std::memory_order_acquire)) {
            return 0;
        }
        if (g_dc_dllmain_process_attach_returned.load(std::memory_order_acquire)) {
            return 0;
        }
        Sleep(200);
    }

    if (g_watchdog_cancel.load(std::memory_order_acquire)) {
        return 0;
    }
    if (g_dc_dllmain_process_attach_returned.load(std::memory_order_acquire)) {
        return 0;
    }

    AppendDisplayCommanderBootLog(
        "[DllMain] PROCESS_ATTACH watchdog: still running after 30s (loader lock likely held)");
    OutputDebugStringA(
        "Display Commander: DLL_PROCESS_ATTACH did not return within 30 seconds (likely stuck in DllMain).\n");

    MessageBoxW(nullptr,
                L"Display Commander is stuck during game startup (DLL load did not finish within 30 seconds).\n\n"
                L"Check DisplayCommander.log for the last boot line, then fix or remove the addon.",
                L"Display Commander is stuck — fix me",
                MB_OK | MB_ICONERROR | MB_SYSTEMMODAL | MB_TOPMOST | MB_SETFOREGROUND);
    return 0;
}

}  // namespace

void StartDllProcessAttachWatchdog() {
    if (g_watchdog_started.exchange(true, std::memory_order_acq_rel)) {
        return;
    }
    g_watchdog_cancel.store(false, std::memory_order_release);
    HANDLE thread = CreateThread(nullptr, 0, &DllProcessAttachWatchdogThread, nullptr, 0, nullptr);
    if (thread == nullptr) {
        g_watchdog_started.store(false, std::memory_order_release);
        return;
    }
    g_watchdog_thread.store(thread, std::memory_order_release);
}

void CancelDllProcessAttachWatchdog() {
    g_watchdog_cancel.store(true, std::memory_order_release);
    HANDLE thread = g_watchdog_thread.exchange(nullptr, std::memory_order_acq_rel);
    if (thread != nullptr) {
        CloseHandle(thread);
    }
}
