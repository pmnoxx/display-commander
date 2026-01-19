# OnPresentSync Delay Bias Implementation Formulas

## Overview

This document defines the mathematical formulas for implementing Special-K style delay bias in the OnPresentSync FPS limiter mode. The delay bias controls the allocation of frame time between **pre-present sleep** (display stability) and **post-present sleep** (input latency reduction).

## Core Concept

The delay bias (`delay_bias`) is a value between 0.0 and 1.0 that determines:
- **delay_bias = 0.0**: 100% Display / 0% Input (maximum frame stability)
- **delay_bias = 1.0**: 0% Display / 100% Input (maximum input responsiveness)

## Ratio to Delay Bias Mapping

The low latency ratio selector maps to delay_bias as follows:

| Ratio Index | Display % | Input % | delay_bias | Formula |
|-------------|-----------|---------|------------|---------|
| 0 | 100% | 0% | 0.0 | `delay_bias = 0.0` |
| 1 | 75% | 25% | 0.25 | `delay_bias = 0.25` |
| 2 | 50% | 50% | 0.5 | `delay_bias = 0.5` |
| 3 | 25% | 75% | 0.75 | `delay_bias = 0.75` |
| 4 | 0% | 100% | 1.0 | `delay_bias = 1.0` |

**Formula:**
```cpp
float GetDelayBiasFromRatio(int ratio_index) {
    // Clamp ratio_index to valid range [0, 4]
    ratio_index = std::max(0, std::min(4, ratio_index));
    return ratio_index * 0.25f;
}
```

## Frame Time Calculation

Frame time is calculated from the target FPS:

```cpp
float target_fps = GetTargetFps();
if (target_fps <= 0.0f) {
    // No FPS limit, cannot calculate frame time
    return;
}

// Frame time in nanoseconds
LONGLONG frame_time_ns = static_cast<LONGLONG>(1'000'000'000.0 / target_fps);
```

**Alternative:** Use measured frame time from performance samples:
```cpp
const uint32_t count = g_perf_ring.GetCount();
if (count > 0) {
    const PerfSample& last_sample = g_perf_ring.GetSample(0);
    if (last_sample.dt > 0.0f) {
        float frame_time_ms = 1000.0f * last_sample.dt;
        LONGLONG frame_time_ns = static_cast<LONGLONG>(frame_time_ms * utils::NS_TO_MS);
    }
}
```

## Sleep Time Allocation Formulas

### Total Sleep Time

The total sleep time per frame is determined by the FPS limiter's target frame time:

```
total_sleep_time = frame_time_ns - (time_spent_rendering + time_spent_presenting)
```

For simplicity, we assume the FPS limiter already handles the base frame pacing, so we only need to allocate the **additional delay** based on delay_bias.

### Pre-Present Sleep (HandleFpsLimiterPre)

**Purpose:** Controls when frame processing begins (display stability)

**Formula:**
```cpp
LONGLONG pre_sleep_ns = static_cast<LONGLONG>((1.0f - delay_bias) * frame_time_ns);
```

**Behavior:**
- `delay_bias = 0.0` → `pre_sleep_ns = frame_time_ns` (full sleep before present, maximum stability)
- `delay_bias = 1.0` → `pre_sleep_ns = 0` (no sleep before present, start immediately)
- `delay_bias = 0.5` → `pre_sleep_ns = 0.5 * frame_time_ns` (balanced)

**Implementation Logic:**
1. Calculate ideal frame start time: `ideal_frame_start_ns = last_frame_end_ns + frame_time_ns`
2. Calculate sleep target: `sleep_until_ns = ideal_frame_start_ns - pre_sleep_ns`
3. Sleep until `sleep_until_ns` if we're not already past it:
   - If `sleep_until_ns > now_ns`: Sleep until `sleep_until_ns` (on time, sleeps for `pre_sleep_ns`)
   - If `sleep_until_ns <= now_ns`: Start immediately (late, don't add extra delay)

## Formula Verification and Test Cases

Let's verify the formula works correctly for different `delay_bias` values. We'll use a 60 FPS example (frame_time_ns = 16,666,667 ns).

### Test Case 1: delay_bias = 0.0 (100% Display / 0% Input)

**Setup:**
- `frame_time_ns = 16,666,667 ns` (60 FPS)
- `delay_bias = 0.0`
- `last_frame_end_ns = 1,000,000,000 ns` (Frame N ended at 1 second)
- `now_ns = 1,000,000,000 ns` (We enter HandleFpsLimiterPre exactly when frame ended)

**Calculations:**
```
pre_sleep_ns = (1.0 - 0.0) * 16,666,667 = 16,666,667 ns
ideal_frame_start_ns = 1,000,000,000 + 16,666,667 = 1,016,666,667 ns
sleep_until_ns = 1,016,666,667 - 16,666,667 = 1,000,000,000 ns
```

**Result:** `sleep_until_ns = now_ns`, so we don't sleep. **PROBLEM!** We should sleep for the full frame time.

**Fix:** When `delay_bias = 0.0`, we need to ensure we always sleep for `pre_sleep_ns`. The correct calculation should be:
```
sleep_until_ns = ideal_frame_start_ns - pre_sleep_ns
But if sleep_until_ns <= now_ns, we should still sleep for pre_sleep_ns from now
```

**Corrected Logic:**
```
if (sleep_until_ns > now_ns) {
    sleep until sleep_until_ns  // Sleeps for pre_sleep_ns
} else {
    sleep until now_ns + pre_sleep_ns  // Still sleep for pre_sleep_ns
}
```

### Test Case 2: delay_bias = 0.0 (Late Entry)

**Setup:**
- `frame_time_ns = 16,666,667 ns`
- `delay_bias = 0.0`
- `last_frame_end_ns = 1,000,000,000 ns`
- `now_ns = 1,010,000,000 ns` (We're 10ms late)

**Calculations:**
```
pre_sleep_ns = 16,666,667 ns
ideal_frame_start_ns = 1,000,000,000 + 16,666,667 = 1,016,666,667 ns
sleep_until_ns = 1,016,666,667 - 16,666,667 = 1,000,000,000 ns
```

**Result:** `sleep_until_ns < now_ns`, so with corrected logic we sleep until `1,010,000,000 + 16,666,667 = 1,026,666,667 ns`. This ensures we always sleep for the full frame time.

### Test Case 3: delay_bias = 0.5 (50% Display / 50% Input)

**Setup:**
- `frame_time_ns = 16,666,667 ns`
- `delay_bias = 0.5`
- `last_frame_end_ns = 1,000,000,000 ns`
- `now_ns = 1,000,000,000 ns`

**Calculations:**
```
pre_sleep_ns = (1.0 - 0.5) * 16,666,667 = 8,333,333 ns
ideal_frame_start_ns = 1,000,000,000 + 16,666,667 = 1,016,666,667 ns
sleep_until_ns = 1,016,666,667 - 8,333,333 = 1,008,333,334 ns
```

**Result:** We sleep from `1,000,000,000 ns` until `1,008,333,334 ns`, which is `8,333,333 ns` (half frame time). ✓ Correct!

### Test Case 4: delay_bias = 1.0 (0% Display / 100% Input)

**Setup:**
- `frame_time_ns = 16,666,667 ns`
- `delay_bias = 1.0`
- `last_frame_end_ns = 1,000,000,000 ns`
- `now_ns = 1,000,000,000 ns`

**Calculations:**
```
pre_sleep_ns = (1.0 - 1.0) * 16,666,667 = 0 ns
ideal_frame_start_ns = 1,000,000,000 + 16,666,667 = 1,016,666,667 ns
sleep_until_ns = 1,016,666,667 - 0 = 1,016,666,667 ns
```

**Result:** `pre_sleep_ns = 0`, so we don't sleep and start immediately. ✓ Correct!

### Test Case 5: delay_bias = 0.25 (75% Display / 25% Input)

**Setup:**
- `frame_time_ns = 16,666,667 ns`
- `delay_bias = 0.25`
- `last_frame_end_ns = 1,000,000,000 ns`
- `now_ns = 1,000,000,000 ns`

**Calculations:**
```
pre_sleep_ns = (1.0 - 0.25) * 16,666,667 = 12,500,000 ns
ideal_frame_start_ns = 1,000,000,000 + 16,666,667 = 1,016,666,667 ns
sleep_until_ns = 1,016,666,667 - 12,500,000 = 1,004,166,667 ns
```

**Result:** We sleep from `1,000,000,000 ns` until `1,004,166,667 ns`, which is `4,166,667 ns` (75% of frame time). ✓ Correct!

## Complete Frame Cycle Analysis (Starting from 0 ns)

Let's trace through complete frame cycles starting from time 0 to verify frame spacing is maintained.

**Assumptions:**
- Start time: `0 ns`
- Frame time: `16,666,667 ns` (60 FPS)
- Render + Present time: `1,000,000 ns` (1ms, constant for simplicity)
- `last_frame_end_ns` is set in HandleFpsLimiterPost to when post-sleep ends

### Frame Cycle with delay_bias = 0.0

**Frame 1:**
```
HandleFpsLimiterPre (now = 0):
  last_frame_end_ns = 0 (first frame)
  pre_sleep_ns = 16,666,667 ns
  ideal_frame_start_ns = 0 + 16,666,667 = 16,666,667 ns
  sleep_until_ns = 16,666,667 - 16,666,667 = 0 ns
  Since sleep_until_ns <= now (0 <= 0), sleep until now + pre_sleep = 0 + 16,666,667 = 16,666,667 ns
  Frame actually starts at: 16,666,667 ns

Frame processing (render + present):
  Start: 16,666,667 ns
  End: 16,666,667 + 1,000,000 = 17,666,667 ns

HandleFpsLimiterPost (now = 17,666,667):
  delay_bias = 0.0
  post_sleep_ns = 0 * 16,666,667 = 0 ns
  No post-sleep
  last_frame_end_ns = 17,666,667 ns (set to now since no post-sleep)
```

**Frame 2:**
```
HandleFpsLimiterPre (now = 17,666,667):
  last_frame_end_ns = 17,666,667
  pre_sleep_ns = 16,666,667 ns
  ideal_frame_start_ns = 17,666,667 + 16,666,667 = 34,333,334 ns
  sleep_until_ns = 34,333,334 - 16,666,667 = 17,666,667 ns
  Since sleep_until_ns <= now (17,666,667 <= 17,666,667), sleep until now + pre_sleep = 17,666,667 + 16,666,667 = 34,333,334 ns
  Frame actually starts at: 34,333,334 ns
```

**Problem:** Frame 1 starts at 16,666,667 ns, Frame 2 starts at 34,333,334 ns
- Time between starts: 34,333,334 - 16,666,667 = 17,666,667 ns
- Expected: 16,666,667 ns
- **ERROR: Frame spacing is 17.67ms instead of 16.67ms!**

### Frame Cycle with delay_bias = 0.5

**Frame 1:**
```
HandleFpsLimiterPre (now = 0):
  last_frame_end_ns = 0
  pre_sleep_ns = 8,333,333 ns
  ideal_frame_start_ns = 0 + 16,666,667 = 16,666,667 ns
  sleep_until_ns = 16,666,667 - 8,333,333 = 8,333,333 ns
  Since sleep_until_ns > now (8,333,333 > 0), sleep until 8,333,333 ns
  Frame actually starts at: 8,333,333 ns

Frame processing:
  Start: 8,333,333 ns
  End: 8,333,333 + 1,000,000 = 9,333,333 ns

HandleFpsLimiterPost (now = 9,333,333):
  delay_bias = 0.5
  post_sleep_ns = 0.5 * 16,666,667 = 8,333,333 ns
  Sleep until: 9,333,333 + 8,333,333 = 17,666,666 ns
  last_frame_end_ns = 17,666,666 ns
```

**Frame 2:**
```
HandleFpsLimiterPre (now = 17,666,666):
  last_frame_end_ns = 17,666,666
  pre_sleep_ns = 8,333,333 ns
  ideal_frame_start_ns = 17,666,666 + 16,666,667 = 34,333,333 ns
  sleep_until_ns = 34,333,333 - 8,333,333 = 26,000,000 ns
  Since sleep_until_ns > now (26,000,000 > 17,666,666), sleep until 26,000,000 ns
  Frame actually starts at: 26,000,000 ns
```

**Problem:** Frame 1 starts at 8,333,333 ns, Frame 2 starts at 26,000,000 ns
- Time between starts: 26,000,000 - 8,333,333 = 17,666,667 ns
- Expected: 16,666,667 ns
- **ERROR: Frame spacing is 17.67ms instead of 16.67ms!**

### Root Cause Analysis

The problem is that we're calculating `ideal_frame_start_ns = last_frame_end_ns + frame_time_ns`, but this is **WRONG**!

**The correct approach:**
- Frames should be spaced by `frame_time_ns` from **start to start**, not from end to start
- We should track when the **previous frame started** (`g_onpresent_sync_frame_start_ns`)
- Next frame should start at: `previous_frame_start_ns + frame_time_ns`
- We sleep for `pre_sleep_ns` before starting, so: `sleep_until_ns = (previous_frame_start_ns + frame_time_ns) - pre_sleep_ns`

### Corrected Frame Cycle with delay_bias = 0.5

**Frame 1:**
```
HandleFpsLimiterPre (now = 0):
  previous_frame_start_ns = 0 (first frame)
  pre_sleep_ns = 8,333,333 ns
  ideal_frame_start_ns = 0 + 16,666,667 = 16,666,667 ns
  sleep_until_ns = 16,666,667 - 8,333,333 = 8,333,333 ns
  Sleep until 8,333,333 ns
  Frame actually starts at: 8,333,333 ns (stored in g_onpresent_sync_frame_start_ns)

Frame processing:
  Start: 8,333,333 ns
  End: 8,333,333 + 1,000,000 = 9,333,333 ns

HandleFpsLimiterPost (now = 9,333,333):
  delay_bias = 0.5
  post_sleep_ns = 0.5 * 16,666,667 = 8,333,333 ns
  Sleep until: 9,333,333 + 8,333,333 = 17,666,666 ns
  last_frame_end_ns = 17,666,666 ns
```

**Frame 2:**
```
HandleFpsLimiterPre (now = 17,666,666):
  previous_frame_start_ns = 8,333,333 (from Frame 1)
  pre_sleep_ns = 8,333,333 ns
  ideal_frame_start_ns = 8,333,333 + 16,666,667 = 25,000,000 ns
  sleep_until_ns = 25,000,000 - 8,333,333 = 16,666,667 ns
  Since sleep_until_ns < now (16,666,667 < 17,666,666), sleep until now + pre_sleep = 17,666,666 + 8,333,333 = 26,000,000 ns
  Frame actually starts at: 26,000,000 ns
```

**Wait, this still gives 26,000,000 ns, which is wrong!**

The issue is that when we're late, we're sleeping from `now` instead of from the ideal start time. Let me recalculate:

**Frame 2 (corrected):**
```
HandleFpsLimiterPre (now = 17,666,666):
  previous_frame_start_ns = 8,333,333
  pre_sleep_ns = 8,333,333 ns
  ideal_frame_start_ns = 8,333,333 + 16,666,667 = 25,000,000 ns
  sleep_until_ns = 25,000,000 - 8,333,333 = 16,666,667 ns
  Since sleep_until_ns < now, we're late
  But we should still try to maintain spacing: sleep until ideal_frame_start_ns = 25,000,000 ns
  Frame actually starts at: 25,000,000 ns
```

**Result:** Frame 1 starts at 8,333,333 ns, Frame 2 starts at 25,000,000 ns
- Time between starts: 25,000,000 - 8,333,333 = 16,666,667 ns ✓ **CORRECT!**

### Corrected Frame Cycle with delay_bias = 0.0

**Frame 1:**
```
HandleFpsLimiterPre (now = 0):
  previous_frame_start_ns = 0
  pre_sleep_ns = 16,666,667 ns
  ideal_frame_start_ns = 0 + 16,666,667 = 16,666,667 ns
  sleep_until_ns = 16,666,667 - 16,666,667 = 0 ns
  Since sleep_until_ns <= now, sleep until ideal_frame_start_ns = 16,666,667 ns
  Frame actually starts at: 16,666,667 ns

Frame processing:
  Start: 16,666,667 ns
  End: 16,666,667 + 1,000,000 = 17,666,667 ns

HandleFpsLimiterPost:
  post_sleep_ns = 0
  last_frame_end_ns = 17,666,667 ns
```

**Frame 2:**
```
HandleFpsLimiterPre (now = 17,666,667):
  previous_frame_start_ns = 16,666,667
  pre_sleep_ns = 16,666,667 ns
  ideal_frame_start_ns = 16,666,667 + 16,666,667 = 33,333,334 ns
  sleep_until_ns = 33,333,334 - 16,666,667 = 16,666,667 ns
  Since sleep_until_ns < now (16,666,667 < 17,666,667), we're late
  Sleep until ideal_frame_start_ns = 33,333,334 ns
  Frame actually starts at: 33,333,334 ns
```

**Result:** Frame 1 starts at 16,666,667 ns, Frame 2 starts at 33,333,334 ns
- Time between starts: 33,333,334 - 16,666,667 = 16,666,667 ns ✓ **CORRECT!**

## Corrected Implementation Logic

**KEY INSIGHT:** Frames must be spaced by `frame_time_ns` from **start to start**, not from end to start!

The correct implementation should use the **previous frame's start time** (`g_onpresent_sync_frame_start_ns`), not the end time:

```cpp
LONGLONG pre_sleep_ns = static_cast<LONGLONG>((1.0f - delay_bias) * frame_time_ns);
LONGLONG previous_frame_start_ns = g_onpresent_sync_frame_start_ns.load();
LONGLONG ideal_frame_start_ns;

if (previous_frame_start_ns == 0) {
    // First frame - start after pre_sleep from now
    ideal_frame_start_ns = now_ns + pre_sleep_ns;
} else {
    // Subsequent frame - maintain frame pacing from start to start
    ideal_frame_start_ns = previous_frame_start_ns + frame_time_ns;
}

LONGLONG sleep_until_ns = ideal_frame_start_ns - pre_sleep_ns;

if (pre_sleep_ns > 0) {
    if (sleep_until_ns > now_ns) {
        // On time - sleep until calculated time
        utils::wait_until_ns(sleep_until_ns, g_timer_handle);
    } else {
        // Late - but still sleep until ideal_frame_start_ns to maintain frame spacing
        // This ensures frames are always spaced by frame_time_ns from start to start
        utils::wait_until_ns(ideal_frame_start_ns, g_timer_handle);
    }
}

// Record when frame actually started
g_onpresent_sync_frame_start_ns.store(utils::get_now_ns());
```

**Critical Notes:**
1. **Use `g_onpresent_sync_frame_start_ns` (previous frame start) instead of `g_onpresent_sync_last_frame_end_ns` (previous frame end)**
2. **Calculate `ideal_frame_start_ns = previous_frame_start_ns + frame_time_ns`** (start-to-start spacing)
3. **When late, sleep until `ideal_frame_start_ns`** (not `now + pre_sleep_ns`) to maintain correct frame spacing
4. This ensures frames are always spaced by exactly `frame_time_ns` regardless of `delay_bias` value

### Post-Present Sleep (HandleFpsLimiterPost)

**Purpose:** Controls delay after present completes (input latency reduction)

**Formula:**
```cpp
LONGLONG post_sleep_ns = static_cast<LONGLONG>(delay_bias * frame_time_ns);
```

**Behavior:**
- `delay_bias = 0.0` → `post_sleep_ns = 0` (no sleep after present)
- `delay_bias = 1.0` → `post_sleep_ns = frame_time_ns` (full frame time sleep after present)
- `delay_bias = 0.5` → `post_sleep_ns = 0.5 * frame_time_ns` (balanced)

## Complete Implementation Logic

### Understanding the Current FPS Limiter

The `CustomFpsLimiter::LimitFrameRate()` function:
1. Calculates `wait_target_ns = max(now, last_time_point_ns + frame_time_ns)`
2. Sleeps until `wait_target_ns`
3. Updates `last_time_point_ns = wait_target_ns`

This ensures frames are spaced by exactly `frame_time_ns`.

### Delay Bias Implementation Strategy

Since the limiter already sleeps for the full frame time, we need to:
1. **Modify the limiter's target time** to account for delay_bias (reduce pre-sleep)
2. **Add post-sleep** after present completes (when delay_bias > 0)

**Key Insight:** The total sleep time per frame remains `frame_time_ns`, but we split it:
- Pre-sleep: `(1 - delay_bias) * frame_time_ns`
- Post-sleep: `delay_bias * frame_time_ns`

### HandleFpsLimiterPre Implementation

**Recommended Approach:** Implement custom sleep logic that replaces the limiter's behavior when delay_bias is active:

```cpp
void HandleFpsLimiterPre(bool from_present_detour, bool from_wrapper = false) {
    // ... existing code for target_fps calculation ...

    if (s_fps_limiter_mode.load() == FpsLimiterMode::kOnPresentSync) {
        float target_fps = GetTargetFps();
        if (target_fps > 0.0f) {
            // Get delay_bias from ratio selector
            int ratio_index = settings::g_mainTabSettings.onpresent_sync_low_latency_ratio.GetValue();
            float delay_bias = GetDelayBiasFromRatio(ratio_index);

            // Calculate frame time
            LONGLONG frame_time_ns = static_cast<LONGLONG>(1'000'000'000.0 / target_fps);

            // Store for post-sleep calculation
            g_onpresent_sync_delay_bias.store(delay_bias);
            g_onpresent_sync_frame_time_ns.store(frame_time_ns);

            // Apply Reflex adjustment if enabled
            if (settings::g_mainTabSettings.onpresent_sync_enable_reflex.GetValue()) {
                target_fps *= 0.995f;  // Subtract 0.5%
                frame_time_ns = static_cast<LONGLONG>(1'000'000'000.0 / target_fps);
            }

            // Calculate pre-sleep time
            LONGLONG pre_sleep_ns = static_cast<LONGLONG>((1.0f - delay_bias) * frame_time_ns);

            // Get current time and calculate target frame start time
            LONGLONG now_ns = utils::get_now_ns();
            LONGLONG last_frame_end_ns = g_onpresent_sync_last_frame_end_ns.load();

            // Calculate when this frame should start
            // If this is the first frame, start immediately (or after minimal delay)
            LONGLONG target_frame_start_ns;
            if (last_frame_end_ns == 0) {
                // First frame - start after pre_sleep
                target_frame_start_ns = now_ns + pre_sleep_ns;
            } else {
                // Subsequent frame - maintain frame pacing
                // Target is last_frame_end + frame_time, but we start earlier by delay_bias amount
                target_frame_start_ns = last_frame_end_ns + frame_time_ns - pre_sleep_ns;
            }

            // Sleep until target_frame_start_ns (if we're not already past it)
            if (target_frame_start_ns > now_ns) {
                utils::wait_until_ns(target_frame_start_ns, g_timer_handle);
            }

            // Record when frame processing actually started
            g_onpresent_sync_frame_start_ns.store(utils::get_now_ns());
        }
    } else {
        // For other modes, use existing limiter
        if (dxgi::fps_limiter::g_customFpsLimiter) {
            auto& limiter = dxgi::fps_limiter::g_customFpsLimiter;
            if (target_fps > 0.0f) {
                limiter->LimitFrameRate(target_fps);
            }
        }
    }
}
```

**Alternative Simpler Approach:** Modify the limiter to accept a sleep fraction, or use a wrapper:

```cpp
if (s_fps_limiter_mode.load() == FpsLimiterMode::kOnPresentSync) {
    float target_fps = GetTargetFps();
    if (target_fps > 0.0f) {
        int ratio_index = settings::g_mainTabSettings.onpresent_sync_low_latency_ratio.GetValue();
        float delay_bias = GetDelayBiasFromRatio(ratio_index);
        LONGLONG frame_time_ns = static_cast<LONGLONG>(1'000'000'000.0 / target_fps);

        // Store for post-sleep
        g_onpresent_sync_delay_bias.store(delay_bias);
        g_onpresent_sync_frame_time_ns.store(frame_time_ns);

        // If delay_bias = 0, use normal limiter (full pre-sleep)
        // If delay_bias > 0, we need custom logic
        if (delay_bias == 0.0f) {
            // Normal behavior - use limiter as-is
            if (dxgi::fps_limiter::g_customFpsLimiter) {
                auto& limiter = dxgi::fps_limiter::g_customFpsLimiter;
                if (settings::g_mainTabSettings.onpresent_sync_enable_reflex.GetValue()) {
                    target_fps *= 0.995f;
                }
                limiter->LimitFrameRate(target_fps);
            }
        } else {
            // Custom sleep logic for delay_bias > 0
            // Calculate adjusted target FPS that accounts for reduced pre-sleep
            // Adjusted FPS = target_fps / (1 - delay_bias)
            float adjusted_target_fps = target_fps / (1.0f - delay_bias);

            if (dxgi::fps_limiter::g_customFpsLimiter) {
                auto& limiter = dxgi::fps_limiter::g_customFpsLimiter;
                if (settings::g_mainTabSettings.onpresent_sync_enable_reflex.GetValue()) {
                    adjusted_target_fps *= 0.995f;
                }
                // This will sleep for (1 - delay_bias) * frame_time
                limiter->LimitFrameRate(adjusted_target_fps);
            }
        }
    }
}
```

### HandleFpsLimiterPost Implementation

```cpp
void HandleFpsLimiterPost(bool from_present_detour, bool from_wrapper = false) {
    if (s_fps_limiter_mode.load() == FpsLimiterMode::kOnPresentSync) {
        float delay_bias = g_onpresent_sync_delay_bias.load();
        LONGLONG frame_time_ns = g_onpresent_sync_frame_time_ns.load();

        if (delay_bias > 0.0f && frame_time_ns > 0) {
            // Calculate post-sleep time
            LONGLONG post_sleep_ns = static_cast<LONGLONG>(delay_bias * frame_time_ns);

            // Account for any late amount (if we're behind schedule)
            post_sleep_ns -= late_amount_ns.load();

            // Sleep after present if we have time remaining
            if (post_sleep_ns > 0) {
                LONGLONG sleep_until_ns = utils::get_now_ns() + post_sleep_ns;
                utils::wait_until_ns(sleep_until_ns, g_timer_handle);
            }
        }
    }
}
```

## Timing Diagram

For a 60 FPS game (16.67ms frame time) with delay_bias = 0.5:

```
Frame N-1 ends
    ↓
[Pre-Sleep: 8.33ms] ← (1 - 0.5) * 16.67ms
    ↓
Frame N processing starts
    ↓
Render frame N
    ↓
Present() called
    ↓
Present() completes
    ↓
[Post-Sleep: 8.33ms] ← 0.5 * 16.67ms
    ↓
Frame N ends, Frame N+1 can start
```

## Edge Cases and Validation

### Edge Case 1: delay_bias = 0.0 (100% Display / 0% Input)
- Pre-sleep: Full frame time (maximum stability)
- Post-sleep: 0 (no delay after present)
- **Validation:** Should match current behavior (delay_bias = 0)

### Edge Case 2: delay_bias = 1.0 (0% Display / 100% Input)
- Pre-sleep: 0 (start immediately)
- Post-sleep: Full frame time (maximum input latency reduction)
- **Validation:** Should provide lowest input latency but potentially less stable timing

### Edge Case 3: target_fps = 0 (FPS limiter disabled)
- **Behavior:** delay_bias should not be applied (no frame time to calculate)
- **Validation:** Skip delay_bias logic when target_fps <= 0

### Edge Case 4: Very high FPS (target_fps > 1000)
- Frame time becomes very small (< 1ms)
- Sleep precision may be limited by timer resolution
- **Validation:** Ensure sleep times are clamped to minimum timer resolution

### Edge Case 5: Late frames (late_amount_ns > 0)
- If we're behind schedule, reduce post-sleep time
- **Formula:** `post_sleep_ns = max(0, delay_bias * frame_time_ns - late_amount_ns)`

## State Variables Required

```cpp
// Global state for delay_bias implementation
std::atomic<float> g_onpresent_sync_delay_bias{0.0f};
std::atomic<LONGLONG> g_onpresent_sync_frame_time_ns{0};
std::atomic<LONGLONG> g_last_frame_end_ns{0};  // Track when last frame ended
```

## Integration with Existing Code

### Current FPS Limiter Behavior
- `limiter->LimitFrameRate(target_fps)` already sleeps until the next frame time
- This sleep happens in `HandleFpsLimiterPre`

### Modification Strategy
1. **Option A:** Modify the FPS limiter to accept a sleep fraction parameter
2. **Option B:** Let limiter run normally, then add additional sleep logic
3. **Option C:** Replace limiter sleep with custom delay_bias-aware sleep

**Recommended: Option C** - Implement custom sleep logic that accounts for delay_bias:
- Calculate target frame start time
- Sleep for `(1 - delay_bias) * frame_time` before allowing frame processing
- After present, sleep for `delay_bias * frame_time`

## Performance Considerations

1. **Timer Precision:** Use high-resolution timer (`utils::wait_until_ns`)
2. **Sleep Overhead:** Account for sleep function overhead in calculations
3. **Frame Time Measurement:** Use rolling average for stable frame time calculation
4. **Late Frame Handling:** Reduce post-sleep when behind schedule to catch up

## Debug Information

For debugging, track and display:
- `delay_bias`: Current delay bias value (0.0 - 1.0)
- `frame_time_ns`: Calculated frame time in nanoseconds
- `pre_sleep_ns`: Actual pre-sleep time applied
- `post_sleep_ns`: Actual post-sleep time applied
- `late_amount_ns`: How late we are (if any)
- `ratio_index`: Current ratio selector index (0-4)
