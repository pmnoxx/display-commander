# Special-K Low Latency Mode Selector Implementation

## Overview

Special-K implements a low latency mode selector that controls the balance between **display latency** (frame stability) and **input latency** (responsiveness). This is implemented through a "Delay Bias" system that allocates frame time between input processing and display/rendering.

## Core Concept: Delay Bias

The low latency ratio selector controls a `delay_bias` value that determines:
- **Display portion**: Time allocated to frame rendering and display synchronization
- **Input portion**: Time allocated to input polling and processing before rendering

### Ratio Options

Special-K typically offers these ratio options:

| Option | Display % | Input % | Use Case |
|--------|-----------|---------|----------|
| 100% Display / 0% Input | 100% | 0% | Maximum frame stability (current default behavior) |
| 75% Display / 25% Input | 75% | 25% | Slight input latency reduction, still stable |
| 50% Display / 50% Input | 50% | 50% | Balanced approach |
| 25% Display / 75% Input | 25% | 75% | Prioritizes input responsiveness |
| 0% Display / 100% Input | 0% | 100% | Maximum input responsiveness (may reduce frame stability) |

## Implementation Details

### 1. PostDelay Mechanism

Special-K applies the delay **AFTER** `Present()` completes, not before:

```cpp
// Pseudo-code from Special-K's implementation
__SK_LatentSyncPostDelay = (delay_bias == 0.0f) ? 0 :
    static_cast<LONGLONG>(ticks_per_frame * delay_bias);
```

**Timing Sequence:**
1. Frame limiter waits until target time (before present)
2. `Present()` is called
3. If `delay_bias > 0`, sleep for `PostDelay` ticks (after present)
4. Next frame begins

### 2. How Delay Bias Maps to Ratios

The `delay_bias` value represents the **proportion of frame time** used for the delay:

- **100% Display / 0% Input**: `delay_bias = 0.0` (no delay after present)
  - Frame processing starts immediately after present
  - Maximum frame stability, but input is processed later in the frame

- **0% Display / 100% Input**: `delay_bias = 1.0` (full frame time delay)
  - Maximum delay after present
  - Input gets processed early, but display timing may be less stable

- **50% Display / 50% Input**: `delay_bias = 0.5` (half frame time delay)
  - Balanced allocation between input and display

### 3. Auto-Bias System (Advanced)

Special-K includes an auto-bias system that dynamically adjusts `delay_bias` based on measured input latency:

```cpp
// From Special-K's framerate.cpp
if (latency_avg.getInput() > (auto_bias_target_ms * 1.05f)) {
    // Input latency too high - increase delay_bias
    delta = effective_frametime() * SK_LatentSyncDeltaMultiplier;
    delay_bias = SK_LatentSyncAlpha * delay_bias + (1.0f - SK_LatentSyncAlpha) * delta;
}
else if (latency_avg.getInput() < (auto_bias_target_ms * 0.95f)) {
    // Input latency too low - decrease delay_bias
    delta = effective_frametime() * SK_LatentSyncDeltaMultiplier * SK_LatentSyncBackOffMultiplier;
    delay_bias = SK_LatentSyncAlpha * delay_bias - (1.0f - SK_LatentSyncAlpha) * delta;
}
```

**Constants:**
- `SK_LatentSyncAlpha = 0.991f` (smoothing factor)
- `SK_LatentSyncDeltaMultiplier = 0.133f` (step size)
- `SK_LatentSyncBackOffMultiplier = 1.020f` (backoff strength)

**Latency Measurement:**
- `input[i] = (1000 / limit) - effective_frametime()` (input latency)
- `display[i] = effective_frametime()` (display latency)

## How It Works in Practice

### Frame Timing Allocation

For a 60 FPS game (16.67ms frame time):

**100% Display / 0% Input:**
- Present() completes
- No delay → next frame starts immediately
- Input is polled during frame rendering
- Result: Stable frame timing, higher input latency

**50% Display / 50% Input:**
- Present() completes
- Sleep for ~8.33ms (50% of frame time)
- Input is polled during this delay period
- Next frame starts
- Result: Balanced input/display latency

**0% Display / 100% Input:**
- Present() completes
- Sleep for ~16.67ms (100% of frame time)
- Input is polled during this delay period
- Next frame starts
- Result: Lower input latency, potentially less stable frame timing

## Integration with OnPresentSync

For Display Commander's OnPresentSync mode, the low latency ratio would control:

1. **Present Pacing Delay**: Similar to Special-K's PostDelay
   - Currently implemented as `present_pacing_delay_percentage`
   - Applied after present via `TimerPresentPacingDelayStart()`

2. **Frame Timing**: When the next frame processing begins
   - Higher display ratio = start frame processing earlier (more stable)
   - Higher input ratio = delay frame processing (lower input latency)

## Implementation Notes

### Current State in Display Commander

- **Present Pacing Delay**: Already implemented in `swapchain_events.cpp`
  - `TimerPresentPacingDelayStart()` applies delay after present
  - Uses percentage-based delay (0-80%)
  - Similar concept to Special-K's PostDelay

### Future Implementation

To fully implement the low latency ratio selector:

1. **Map ratio to delay_bias**:
   ```cpp
   float GetDelayBiasFromRatio(int ratio_index) {
       // 0 = 100% Display / 0% Input → delay_bias = 0.0
       // 1 = 75% Display / 25% Input → delay_bias = 0.25
       // 2 = 50% Display / 50% Input → delay_bias = 0.5
       // 3 = 25% Display / 75% Input → delay_bias = 0.75
       // 4 = 0% Display / 100% Input → delay_bias = 1.0
       return ratio_index * 0.25f;
   }
   ```

2. **Apply delay_bias to Present Pacing Delay**:
   - Replace or modify `present_pacing_delay_percentage` calculation
   - Use `delay_bias * frame_time` as the delay duration

3. **Optional: Auto-Bias System**:
   - Measure input latency (time from input to display)
   - Measure display latency (frame time)
   - Automatically adjust delay_bias to target ratio

## References

- Special-K Source: `external/SpecialK/src/framerate.cpp`
- Documentation: `docs/SpecialK_LatentSync_Backoff.md`
- Web Sources:
  - [Shacknews: Special-K's Latent Sync](https://www.shacknews.com/cortex/article/1743/special-ks-new-latent-sync-is-vsync-with-as-much-or-even-less-input-latency-than-no-vsync)
  - [Special-K Discourse: Framerate Limiter](https://discourse.differentk.fyi/t/special-k-v-0-11-0-48-framerate-limiter-stuff/763)
