#pragma once

#include <Windows.h>

namespace utils {

// Setup high-resolution timer by setting kernel timer resolution to maximum
bool setup_high_resolution_timer();

// Get current timer resolution in milliseconds
double get_timer_resolution_ms();

// Wait until the specified QPC time is reached
// Uses a combination of kernel waitable timers and busy waiting for precision
void wait_until_qpc(LONGLONG target_qpc, HANDLE& timer_handle);

} // namespace utils
