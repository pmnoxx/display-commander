#pragma once

#include <stdint.h>
#include <windows.h>
#include <vector>
#include <string>

namespace display_commanderhooks {

// Timeslowdown configuration variables - now using global instance

// Timer hook types enum
enum class TimerHookType { None = 0, Enabled = 1, EnableRenderThread = 2, EnableNonRenderThread = 3 };

// Timer hook identifier enum for efficient lookups
enum class TimerHookIdentifier {
    QueryPerformanceCounter = 0,
    GetTickCount = 1,
    GetTickCount64 = 2,
    TimeGetTime = 3,
    GetSystemTime = 4,
    GetSystemTimeAsFileTime = 5,
    GetSystemTimePreciseAsFileTime = 6,
    GetLocalTime = 7,
    NtQuerySystemTime = 8,
    Count = 9
};

// Function pointer types for timeslowdown hooks
using QueryPerformanceCounter_pfn = BOOL(WINAPI *)(LARGE_INTEGER *lpPerformanceCount);
using QueryPerformanceFrequency_pfn = BOOL(WINAPI *)(LARGE_INTEGER *lpFrequency);
using GetTickCount_pfn = DWORD(WINAPI *)();
using GetTickCount64_pfn = ULONGLONG(WINAPI *)();
using timeGetTime_pfn = DWORD(WINAPI *)();
using GetSystemTime_pfn = void(WINAPI *)(LPSYSTEMTIME);
using GetSystemTimeAsFileTime_pfn = void(WINAPI *)(LPFILETIME);
using GetSystemTimePreciseAsFileTime_pfn = void(WINAPI *)(LPFILETIME);
using GetLocalTime_pfn = void(WINAPI *)(LPSYSTEMTIME);
using NtQuerySystemTime_pfn = NTSTATUS(WINAPI *)(PLARGE_INTEGER);

// Timeslowdown hook function pointers
extern QueryPerformanceCounter_pfn QueryPerformanceCounter_Original;
extern QueryPerformanceFrequency_pfn QueryPerformanceFrequency_Original;
extern GetTickCount_pfn GetTickCount_Original;
extern GetTickCount64_pfn GetTickCount64_Original;
extern timeGetTime_pfn timeGetTime_Original;
extern timeGetTime_pfn timeGetTime_Direct;
extern GetSystemTime_pfn GetSystemTime_Original;
extern GetSystemTimeAsFileTime_pfn GetSystemTimeAsFileTime_Original;
extern GetSystemTimePreciseAsFileTime_pfn GetSystemTimePreciseAsFileTime_Original;
extern GetLocalTime_pfn GetLocalTime_Original;
extern NtQuerySystemTime_pfn NtQuerySystemTime_Original;

// Hooked API functions
BOOL WINAPI QueryPerformanceCounter_Detour(LARGE_INTEGER *lpPerformanceCount);
BOOL WINAPI QueryPerformanceFrequency_Detour(LARGE_INTEGER *lpFrequency);
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
void SetTimeslowdownMultiplier(float multiplier);
float GetTimeslowdownMultiplier();
bool IsTimeslowdownEnabled();
void SetTimeslowdownEnabled(bool enabled);

// Individual hook type configuration
void SetTimerHookType(const char *hook_name, TimerHookType type);
TimerHookType GetTimerHookType(const char *hook_name);
bool IsTimerHookEnabled(const char *hook_name);
// Thread-specific modes removed

// Statistics
uint64_t GetTimerHookCallCount(const char *hook_name);

// Efficient enum-based functions (for internal use)
void SetTimerHookTypeById(TimerHookIdentifier id, TimerHookType type);
TimerHookType GetTimerHookTypeById(TimerHookIdentifier id);
bool IsTimerHookEnabledById(TimerHookIdentifier id);
uint64_t GetTimerHookCallCountById(TimerHookIdentifier id);
TimerHookIdentifier GetHookIdentifierByName(const char *hook_name);

// UI helper functions for efficient hook management
const TimerHookIdentifier* GetAllHookIdentifiers();
const char* GetHookNameById(TimerHookIdentifier id);

// Hook type constants
extern const char *HOOK_QUERY_PERFORMANCE_COUNTER;
extern const char *HOOK_GET_TICK_COUNT;
extern const char *HOOK_GET_TICK_COUNT64;
extern const char *HOOK_TIME_GET_TIME;
extern const char *HOOK_GET_SYSTEM_TIME;
extern const char *HOOK_GET_SYSTEM_TIME_AS_FILE_TIME;
extern const char *HOOK_GET_SYSTEM_TIME_PRECISE_AS_FILE_TIME;
extern const char *HOOK_GET_LOCAL_TIME;
extern const char *HOOK_NT_QUERY_SYSTEM_TIME;

// Render thread management
void SetRenderThreadId(DWORD thread_id);
DWORD GetRenderThreadId();
bool IsCurrentThreadRenderThread();

// QPC calling module tracking
std::vector<std::wstring> GetQPCallingModules();
std::vector<std::pair<HMODULE, std::wstring>> GetQPCallingModulesWithHandles();
void ClearQPCallingModules();
bool IsQPCModuleEnabled(HMODULE hModule);
void SetQPCModuleEnabled(HMODULE hModule, bool enabled);
void LoadQPCEnabledModulesFromSettings(const std::string& enabled_modules_str);
std::string SaveQPCEnabledModulesToSettings();

// Apply timeslowdown to a QPC value and return the spoofed value
LONGLONG ApplyTimeslowdownToQPC(LONGLONG original_qpc);

} // namespace display_commanderhooks
