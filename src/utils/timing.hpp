#pragma once

#include <Windows.h>

namespace utils {
    const LONGLONG SEC_TO_NS = 1000000000;
    const LONGLONG NS_TO_MS = 1000000;
    const LONGLONG QPC_TO_NS = 100;
    const LONGLONG QPC_PER_SECOND = SEC_TO_NS / QPC_TO_NS;
    const LONGLONG QPC_TO_MS = NS_TO_MS / QPC_TO_NS;

    // Setup high-resolution timer by setting kernel timer resolution to maximum
    bool setup_high_resolution_timer();

    // Get current timer resolution in milliseconds
    double get_timer_resolution_ms();

    // Wait until the specified QPC time is reached
    // Uses a combination of kernel waitable timers and busy waiting for precision
    void wait_until_qpc(LONGLONG target_qpc, HANDLE& timer_handle);
    LONGLONG get_now_qpc(); 

    void wait_until_ns(LONGLONG target_ns, HANDLE& timer_handle);
    LONGLONG get_now_ns();

} // namespace utils


