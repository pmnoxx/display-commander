#include "timing.hpp"
#include "../globals.hpp"
#include "../hooks/api_hooks.hpp"
#include "../hooks/timeslowdown_hooks.hpp"
#include "../utils.hpp"
#include "../utils/logging.hpp"
#include "detour_call_tracker.hpp"

#include <windows.h>

#include <intrin.h>

#include <sstream>

// NTSTATUS constants if not already defined
#ifndef STATUS_SUCCESS
#define STATUS_SUCCESS ((NTSTATUS)0x00000000L)
#endif

namespace utils {

// QPC timing constants - initialized at runtime
LONGLONG QPC_TO_NS = 100;  // Default fallback value
LONGLONG QPC_PER_SECOND = SEC_TO_NS / QPC_TO_NS;
LONGLONG QPC_TO_MS = NS_TO_MS / QPC_TO_NS;
bool initialized = false;

// Initialize QPC timing constants based on actual QueryPerformanceCounter frequency
// This should be called early in program initialization (e.g., DllMain)
bool initialize_qpc_timing_constants() {
    if (initialized) return true;
    LARGE_INTEGER frequency;
    if (display_commanderhooks::QueryPerformanceFrequency_Original) {
        if (!display_commanderhooks::QueryPerformanceFrequency_Original(&frequency)) {
            LogError("QueryPerformanceFrequency failed, using default values");
            return false;
        }
    } else {
        if (!QueryPerformanceFrequency(&frequency)) {
            LogError("QueryPerformanceFrequency failed, using default values");
            // If QueryPerformanceFrequency fails, keep default values
            return false;
        }
    }

    if (frequency.QuadPart > SEC_TO_NS) {
        LogError("QPC frequency is too high, using default value");
        return false;
    }

    // Calculate the conversion factor from QPC ticks to nanoseconds
    // QPC_TO_NS = (1 second in ns) / (QPC frequency)
    // This gives us how many nanoseconds each QPC tick represents
    QPC_TO_NS = SEC_TO_NS / frequency.QuadPart;

    // Recalculate dependent constants
    QPC_PER_SECOND = frequency.QuadPart;
    QPC_TO_MS = NS_TO_MS / QPC_TO_NS;
    initialized = true;

    return true;
}

// Function pointer types for dynamic loading
typedef NTSTATUS(NTAPI* ZwQueryTimerResolution_t)(PULONG MinimumResolution, PULONG MaximumResolution,
                                                  PULONG CurrentResolution);
typedef NTSTATUS(NTAPI* ZwSetTimerResolution_t)(ULONG DesiredResolution, BOOLEAN SetResolution,
                                                PULONG CurrentResolution);

// Global variables for timer resolution
static LONGLONG timer_res_qpc = 0;
static ZwQueryTimerResolution_t ZwQueryTimerResolution = nullptr;
static ZwSetTimerResolution_t ZwSetTimerResolution = nullptr;
static LONGLONG timer_res_qpc_frequency = 0;

// Cached MWAITX support result
static bool mwaitx_supported_cached = false;
static bool mwaitx_support_checked = false;

bool supports_mwaitx(void) {
    if (mwaitx_support_checked) return mwaitx_supported_cached;
    // Use original function if hook is installed, otherwise use direct call
    PVECTORED_EXCEPTION_HANDLER handler_func = [](_EXCEPTION_POINTERS* ExceptionInfo) -> LONG {
        if (ExceptionInfo->ExceptionRecord->ExceptionCode == EXCEPTION_ILLEGAL_INSTRUCTION) {
            mwaitx_supported_cached = false;
        }

#ifdef _AMD64_
        ExceptionInfo->ContextRecord->Rip++;
#else
        ExceptionInfo->ContextRecord->Eip++;
#endif

        return EXCEPTION_CONTINUE_EXECUTION;
    };

    auto handler = display_commanderhooks::AddVectoredExceptionHandler_Original
                       ? display_commanderhooks::AddVectoredExceptionHandler_Original(1, handler_func)
                       : AddVectoredExceptionHandler(1, handler_func);
    if (handler == nullptr) {
        LogError("Failed to add vectored exception handler");
        return false;
    }

    mwaitx_supported_cached = true;
    static __declspec(align(64)) uint64_t monitor = 0ULL;
    _mm_monitorx(&monitor, 0, 0);
    _mm_mwaitx(0x2, 0, 1);

    RemoveVectoredExceptionHandler(handler);

    return mwaitx_supported_cached;
}

// Setup high-resolution timer by setting kernel timer resolution to maximum
bool setup_high_resolution_timer() {
    // Get QPC frequency
    LARGE_INTEGER frequency;
    if (display_commanderhooks::QueryPerformanceFrequency_Original != nullptr) {
        display_commanderhooks::QueryPerformanceFrequency_Original(&frequency);
    } else {
        QueryPerformanceFrequency(&frequency);
    }
    timer_res_qpc_frequency = frequency.QuadPart;

    // Load NTDLL functions dynamically
    HMODULE ntdll = GetModuleHandleA("ntdll.dll");
    if (ntdll == nullptr) {
        LogError("Failed to get ntdll.dll handle");
        return false;
    }

    ZwQueryTimerResolution =
        reinterpret_cast<ZwQueryTimerResolution_t>(GetProcAddress(ntdll, "ZwQueryTimerResolution"));
    ZwSetTimerResolution = reinterpret_cast<ZwSetTimerResolution_t>(GetProcAddress(ntdll, "ZwSetTimerResolution"));

    if (ZwQueryTimerResolution == nullptr || ZwSetTimerResolution == nullptr) {
        LogError("Failed to load ZwQueryTimerResolution or ZwSetTimerResolution");
        return false;
    }

    ULONG min, max, cur;
    if (ZwQueryTimerResolution(&min, &max, &cur) == STATUS_SUCCESS) {
        // Store current resolution
        timer_res_qpc = cur;

        // Set timer resolution to maximum for highest precision
        if (ZwSetTimerResolution(max, TRUE, &cur) == STATUS_SUCCESS) {
            timer_res_qpc = cur;
            LogInfo("Timer resolution set to maximum");
            return true;
        }
    }
    LogError("Failed to set timer resolution to maximum");

    return false;
}

// Get current timer resolution in milliseconds
LONGLONG get_timer_resolution_qpc() { return timer_res_qpc; }

// Wait until the specified QPC time is reached
// Uses a combination of kernel waitable timers and busy waiting for precision
void wait_until_qpc(LONGLONG target_qpc, HANDLE& timer_handle) {
    {
        static bool once_setup_timer = false;
        if (!once_setup_timer) {
            LogInfo("setup_high_resolution_timer");
            setup_high_resolution_timer();
            once_setup_timer = true;
        }
    }
    static __declspec(align(64)) uint64_t monitor = 0ULL;
    LONGLONG current_time_qpc = get_now_qpc();
    RECORD_DETOUR_CALL(current_time_qpc * 100);

    // If target time has already passed, return immediately
    if (target_qpc <= current_time_qpc) return;

    // Create timer handle if it doesn't exist or is invalid
    if (timer_handle == nullptr) {
        // Try to create a high-resolution waitable timer if available
        timer_handle = CreateWaitableTimerEx(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);

        // Fallback to regular waitable timer if high-resolution not available
        if (!timer_handle) {
            timer_handle = CreateWaitableTimer(nullptr, FALSE, nullptr);
        }
    }

    LONGLONG time_to_wait_qpc = target_qpc - current_time_qpc;

    // Use kernel waitable timer for longer waits (more than ~2ms)
    // This prevents completely consuming a CPU core during long waits
    if (timer_handle && (time_to_wait_qpc >= 2.5 * timer_res_qpc)) {
        // Schedule timer to wake up slightly before target time
        // Leave some time for busy waiting to achieve precise timing
        LONGLONG delay_qpc = time_to_wait_qpc - 2 * timer_res_qpc;
        LARGE_INTEGER delay{};
        delay.QuadPart = -delay_qpc;

        if (SetWaitableTimer(timer_handle, &delay, 0, nullptr, nullptr, FALSE)) {
            // Wait for the timer to signal
            DWORD wait_result = WaitForSingleObject(timer_handle, INFINITE);

            // Log any errors (optional)
            if (wait_result != WAIT_OBJECT_0) {
                std::ostringstream oss;
                oss << "Timer wait failed: " << wait_result;
                LogError(oss.str().c_str());
            }
        }
    }
    {
        // print once timer_res_qpc
        static bool once = false;
        if (!once) {
            auto timer_res_is_ms = static_cast<double>(timer_res_qpc) / QPC_TO_MS;
            LogInfo("timer_res_qpc: %lld timer_res_is_ms: %f", timer_res_qpc, timer_res_is_ms);
            once = true;

            if (timer_res_is_ms > 1.0) {
                LogError("timer_res_qpc is too high!!! timer_res_is_ms: %.3fms > 1.0ms", timer_res_is_ms);
                return;
            }
        }
    }

    // Busy wait for the remaining time to achieve precise timing
    // This compensates for OS scheduler inaccuracy

    /*if (supports_mwaitx()) {
        while (true) {
            current_time_qpc = get_now_qpc();
            LONGLONG time_to_wait_qpc = target_qpc - current_time_qpc;
            if (time_to_wait_qpc <= 0) break;

            // WARNING: causes crashes in Exedra/Arknights: Endfield
            _mm_monitorx(&monitor, 0, 0);
            //
            unsigned int ticks_to_wait = static_cast<unsigned int>((std::min)(1'000'000'000LL, 240 * time_to_wait_qpc));
            // no more than 4ms
            _mm_mwaitx(0x2, 0, ticks_to_wait);  // 2.4 GHz steamdeck
        }
    } else {*/
    while (true) {
        current_time_qpc = get_now_qpc();
        if (current_time_qpc >= target_qpc) break;

        YieldProcessor();
    }
    //  }
}

void wait_until_ns(LONGLONG target_ns, HANDLE& timer_handle) {
    utils::wait_until_qpc(target_ns / utils::QPC_TO_NS, timer_handle);
}

LONGLONG get_now_qpc() {
    LARGE_INTEGER now_ticks = {};
    if (enabled_experimental_features && display_commanderhooks::QueryPerformanceCounter_Original) {
        display_commanderhooks::QueryPerformanceCounter_Original(&now_ticks);
    } else {
        QueryPerformanceCounter(&now_ticks);
    }
    return now_ticks.QuadPart;
}

// Global timing function
LONGLONG get_now_ns() { return get_now_qpc() * utils::QPC_TO_NS; }

// Get real time bypassing any hooks (for comparison with spoofed time)
LONGLONG get_real_time_ns() {
    LARGE_INTEGER now_ticks = {};
    // Always use the original Windows API, never the hooked version
    QueryPerformanceCounter(&now_ticks);
    return now_ticks.QuadPart * utils::QPC_TO_NS;
}

}  // namespace utils
