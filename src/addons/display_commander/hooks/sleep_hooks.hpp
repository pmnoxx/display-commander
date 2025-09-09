#pragma once

#include <windows.h>
#include <atomic>

namespace renodx::hooks {

// Sleep hook statistics structure
struct SleepHookStats {
    std::atomic<uint64_t> total_calls{0};
    std::atomic<uint64_t> modified_calls{0};
    std::atomic<uint64_t> total_original_duration_ms{0};
    std::atomic<uint64_t> total_modified_duration_ms{0};

    void increment_total() { total_calls.fetch_add(1); }
    void increment_modified() { modified_calls.fetch_add(1); }
    void add_original_duration(DWORD ms) { total_original_duration_ms.fetch_add(ms); }
    void add_modified_duration(DWORD ms) { total_modified_duration_ms.fetch_add(ms); }
    void reset() {
        total_calls.store(0);
        modified_calls.store(0);
        total_original_duration_ms.store(0);
        total_modified_duration_ms.store(0);
    }
};

// Sleep hook statistics
extern SleepHookStats g_sleep_hook_stats;

// Function pointer types for sleep functions
using Sleep_pfn = void(WINAPI*)(DWORD);
using SleepEx_pfn = DWORD(WINAPI*)(DWORD, BOOL);
using WaitForSingleObject_pfn = DWORD(WINAPI*)(HANDLE, DWORD);
using WaitForMultipleObjects_pfn = DWORD(WINAPI*)(DWORD, const HANDLE*, BOOL, DWORD);

// Original function pointers
extern Sleep_pfn Sleep_Original;
extern SleepEx_pfn SleepEx_Original;
extern WaitForSingleObject_pfn WaitForSingleObject_Original;
extern WaitForMultipleObjects_pfn WaitForMultipleObjects_Original;

// Hooked function implementations
void WINAPI Sleep_Detour(DWORD dwMilliseconds);
DWORD WINAPI SleepEx_Detour(DWORD dwMilliseconds, BOOL bAlertable);
DWORD WINAPI WaitForSingleObject_Detour(HANDLE hHandle, DWORD dwMilliseconds);
DWORD WINAPI WaitForMultipleObjects_Detour(DWORD nCount, const HANDLE* lpHandles, BOOL bWaitAll, DWORD dwMilliseconds);

// Installation and cleanup functions
bool InstallSleepHooks();
void UninstallSleepHooks();

} // namespace renodx::hooks
