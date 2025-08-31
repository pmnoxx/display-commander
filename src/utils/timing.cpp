#include "timing.hpp"
#include <Windows.h>
#include <algorithm>
#include <cmath>
#include <sstream>
#include "../addons/display_commander/utils.hpp"

// NTSTATUS constants if not already defined
#ifndef STATUS_SUCCESS
#define STATUS_SUCCESS ((NTSTATUS)0x00000000L)
#endif

namespace utils {

// Function pointer types for dynamic loading
typedef NTSTATUS (NTAPI *ZwQueryTimerResolution_t)(PULONG MinimumResolution, PULONG MaximumResolution, PULONG CurrentResolution);
typedef NTSTATUS (NTAPI *ZwSetTimerResolution_t)(ULONG DesiredResolution, BOOLEAN SetResolution, PULONG CurrentResolution);

// Global variables for timer resolution
static double timer_res_ms = 0.0;
static ZwQueryTimerResolution_t ZwQueryTimerResolution = nullptr;
static ZwSetTimerResolution_t ZwSetTimerResolution = nullptr;
static LONGLONG timer_res_qpc_frequency = 0;

// Setup high-resolution timer by setting kernel timer resolution to maximum
bool setup_high_resolution_timer()
{
    // Get QPC frequency
    LARGE_INTEGER frequency;
    QueryPerformanceFrequency(&frequency);
    timer_res_qpc_frequency = frequency.QuadPart;
    
    // Load NTDLL functions dynamically
    HMODULE ntdll = GetModuleHandleA("ntdll.dll");
    if (!ntdll)
        return false;
    
    ZwQueryTimerResolution = reinterpret_cast<ZwQueryTimerResolution_t>(
        GetProcAddress(ntdll, "ZwQueryTimerResolution")
    );
    ZwSetTimerResolution = reinterpret_cast<ZwSetTimerResolution_t>(
        GetProcAddress(ntdll, "ZwSetTimerResolution")
    );
    
    if (ZwQueryTimerResolution == nullptr || ZwSetTimerResolution == nullptr)
        return false;
    
    ULONG min, max, cur;
    if (ZwQueryTimerResolution(&min, &max, &cur) == STATUS_SUCCESS)
    {
        // Store current resolution
        timer_res_ms = static_cast<double>(cur) / 10000.0;
        
        // Set timer resolution to maximum for highest precision
        if (ZwSetTimerResolution(max, TRUE, &cur) == STATUS_SUCCESS)
        {
            timer_res_ms = static_cast<double>(cur) / 10000.0;
            return true;
        }
    }
    
    return false;
}

// Get current timer resolution in milliseconds
double get_timer_resolution_ms()
{
    return timer_res_ms;
}


LONGLONG get_now_qpc() {
    LARGE_INTEGER now_ticks = { };
    QueryPerformanceCounter(&now_ticks);
    return now_ticks.QuadPart;
}

LONGLONG get_now_ns() {
    LARGE_INTEGER now_ticks = { };
    QueryPerformanceCounter(&now_ticks);
    return now_ticks.QuadPart * (1000000000 / timer_res_qpc_frequency);
}

// Wait until the specified QPC time is reached
// Uses a combination of kernel waitable timers and busy waiting for precision
void wait_until_qpc(LONGLONG target_qpc, HANDLE& timer_handle)
{
    LONGLONG current_time = get_now_qpc();
    
    // If target time has already passed, return immediately
    if (target_qpc <= current_time)
        return;
    
    // Create timer handle if it doesn't exist or is invalid
    if (reinterpret_cast<LONG_PTR>(timer_handle) < 0)
    {
        // Try to create a high-resolution waitable timer if available
        timer_handle = CreateWaitableTimerEx(
            nullptr, 
            nullptr,
            CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, 
            TIMER_ALL_ACCESS
        );
        
        // Fallback to regular waitable timer if high-resolution not available
        if (!timer_handle)
        {
            timer_handle = CreateWaitableTimer(nullptr, FALSE, nullptr);
        }
    }
    
    double time_to_wait_seconds = 
        static_cast<double>(target_qpc - current_time) / 
        static_cast<double>(timer_res_qpc_frequency);
    
    // Use kernel waitable timer for longer waits (more than ~2ms)
    // This prevents completely consuming a CPU core during long waits
    if (timer_handle && (time_to_wait_seconds * 1000.0 >= timer_res_ms * 2.875))
    {
        // Schedule timer to wake up slightly before target time
        // Leave some time for busy waiting to achieve precise timing
        LARGE_INTEGER delay{};
        delay.QuadPart = -static_cast<LONGLONG>(
            (time_to_wait_seconds - 3 * timer_res_ms / 1000.0) * timer_res_qpc_frequency
        );
        
        if (SetWaitableTimer(timer_handle, &delay, 0, nullptr, nullptr, FALSE))
        {
            // Wait for the timer to signal
            DWORD wait_result = WaitForSingleObject(timer_handle, INFINITE);
            
            // Log any errors (optional)
            if (wait_result != WAIT_OBJECT_0)
            {
                std::ostringstream oss;
                oss << "Timer wait failed: " << wait_result;
                LogError(oss.str().c_str());
            }
        }
    }
    
    // Busy wait for the remaining time to achieve precise timing
    // This compensates for OS scheduler inaccuracy
    while (true)
    {
        current_time = get_now_qpc();
        if (current_time >= target_qpc)
            break;
        
        // Yield processor to other threads while waiting
        YieldProcessor();
    }
}

} // namespace utils

// Global timing function
LONGLONG get_now_qpc() {
    LARGE_INTEGER now_ticks = { };
    QueryPerformanceCounter(&now_ticks);
    return now_ticks.QuadPart;
}

