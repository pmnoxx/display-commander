#pragma once

#include <cstdint>

// ============================================================================
// PERFORMANCE MONITORING TYPES
// ============================================================================

// Frame time mode enum for both call reason and filtering
enum class FrameTimeMode : std::uint8_t {
    kPresent = 0,        // Present-Present (only record Present calls)
    kFrameBegin = 1,     // Frame Begin-Frame Begin (only record FrameBegin calls)
    kDisplayTiming = 2   // Display Timing (record when frames are actually displayed based on GPU completion)
};

// Process priority class enum
enum class ProcessPriority : std::uint8_t {
    kDefault = 0,              // NORMAL_PRIORITY_CLASS (default, no change)
    kBelowNormal = 1,           // BELOW_NORMAL_PRIORITY_CLASS
    kNormal = 2,                // NORMAL_PRIORITY_CLASS
    kAboveNormal = 3,           // ABOVE_NORMAL_PRIORITY_CLASS
    kHigh = 4,                  // HIGH_PRIORITY_CLASS
    kRealtime = 5              // REALTIME_PRIORITY_CLASS (use with caution)
};
