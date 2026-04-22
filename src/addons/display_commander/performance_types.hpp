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

/** OSD text: no prefixes, short tokens, or full phrases (Main tab > OSD). */
enum class OverlayLabelMode : std::uint8_t {
    kNone = 0,
    kShort = 1,
    kFull = 2,
};
