#pragma once

#include <windows.h>
#include <atomic>

namespace renodx::hooks {

// Timer hook types enum
enum class TimerHookType {
    None = 0,
    Enabled = 1,
    RenderThreadOnly = 2,
    EverythingExceptRenderThread = 3
};

// Function pointer types for timeslowdown hooks
using QueryPerformanceCounter_pfn = BOOL(WINAPI*)(LARGE_INTEGER* lpPerformanceCount);
using QueryPerformanceFrequency_pfn = BOOL(WINAPI*)(LARGE_INTEGER* lpFrequency);
using GetTickCount_pfn = DWORD(WINAPI*)();
using GetTickCount64_pfn = ULONGLONG(WINAPI*)();
using timeGetTime_pfn = DWORD(WINAPI*)();
using GetSystemTime_pfn = void(WINAPI*)(LPSYSTEMTIME);
using GetSystemTimeAsFileTime_pfn = void(WINAPI*)(LPFILETIME);
using GetSystemTimePreciseAsFileTime_pfn = void(WINAPI*)(LPFILETIME);
using GetLocalTime_pfn = void(WINAPI*)(LPSYSTEMTIME);
using NtQuerySystemTime_pfn = NTSTATUS(WINAPI*)(PLARGE_INTEGER);

// Timeslowdown hook function pointers
extern QueryPerformanceCounter_pfn QueryPerformanceCounter_Original;
extern QueryPerformanceFrequency_pfn QueryPerformanceFrequency_Original;
extern GetTickCount_pfn GetTickCount_Original;
extern GetTickCount64_pfn GetTickCount64_Original;
extern timeGetTime_pfn timeGetTime_Original;
extern GetSystemTime_pfn GetSystemTime_Original;
extern GetSystemTimeAsFileTime_pfn GetSystemTimeAsFileTime_Original;
extern GetSystemTimePreciseAsFileTime_pfn GetSystemTimePreciseAsFileTime_Original;
extern GetLocalTime_pfn GetLocalTime_Original;
extern NtQuerySystemTime_pfn NtQuerySystemTime_Original;

// Hooked API functions
BOOL WINAPI QueryPerformanceCounter_Detour(LARGE_INTEGER* lpPerformanceCount);
BOOL WINAPI QueryPerformanceFrequency_Detour(LARGE_INTEGER* lpFrequency);
DWORD WINAPI GetTickCount_Detour();
ULONGLONG WINAPI GetTickCount64_Detour();
DWORD WINAPI timeGetTime_Detour();
void WINAPI GetSystemTime_Detour(LPSYSTEMTIME lpSystemTime);
void WINAPI GetSystemTimeAsFileTime_Detour(LPFILETIME lpSystemTimeAsFileTime);
void WINAPI GetSystemTimePreciseAsFileTime_Detour(LPFILETIME lpSystemTimeAsFileTime);
void WINAPI GetLocalTime_Detour(LPSYSTEMTIME lpSystemTime);
NTSTATUS WINAPI NtQuerySystemTime_Detour(PLARGE_INTEGER SystemTime);

// Hook management
bool InstallTimeslowdownHooks();
void UninstallTimeslowdownHooks();
bool AreTimeslowdownHooksInstalled();

// Timeslowdown configuration
void SetTimeslowdownMultiplier(double multiplier);
double GetTimeslowdownMultiplier();
bool IsTimeslowdownEnabled();
void SetTimeslowdownEnabled(bool enabled);

// Individual hook type configuration
void SetTimerHookType(const char* hook_name, TimerHookType type);
TimerHookType GetTimerHookType(const char* hook_name);
bool IsTimerHookEnabled(const char* hook_name);
bool IsTimerHookRenderThreadOnly(const char* hook_name);
bool IsTimerHookEverythingExceptRenderThread(const char* hook_name);

// Hook type constants
extern const char* HOOK_QUERY_PERFORMANCE_COUNTER;
extern const char* HOOK_GET_TICK_COUNT;
extern const char* HOOK_GET_TICK_COUNT64;
extern const char* HOOK_TIME_GET_TIME;
extern const char* HOOK_GET_SYSTEM_TIME;
extern const char* HOOK_GET_SYSTEM_TIME_AS_FILE_TIME;
extern const char* HOOK_GET_SYSTEM_TIME_PRECISE_AS_FILE_TIME;
extern const char* HOOK_GET_LOCAL_TIME;
extern const char* HOOK_NT_QUERY_SYSTEM_TIME;

} // namespace renodx::hooks
