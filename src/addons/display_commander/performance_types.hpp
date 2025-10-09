#pragma once

// ============================================================================
// PERFORMANCE MONITORING TYPES
// ============================================================================

// Frame time mode enum for both call reason and filtering
enum class FrameTimeMode {
    Present,     // 0: Present-Present (only record Present calls)
    FrameBegin   // 1: Frame Begin-Frame Begin (only record FrameBegin calls)
};
