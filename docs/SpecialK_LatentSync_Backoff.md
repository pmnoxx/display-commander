### Special-K Latent Sync: Auto-Bias Backoff Analysis

This note explains how Special-K adjusts `delay_bias` in the latent sync auto-bias logic when input latency is below target (the "backoff" branch).

#### Location
- File: `external/SpecialK/src/framerate.cpp`

Code excerpt:
```2030:2045:external/SpecialK/src/framerate.cpp
        if (latency_avg.getInput () > (auto_bias_target_ms * 1.05f))
        {
          delta = (float)effective_frametime () * SK_LatentSyncDeltaMultiplier;

          config.render.framerate.latent_sync.delay_bias =
            SK_LatentSyncAlpha * config.render.framerate.latent_sync.delay_bias + (1.0f - SK_LatentSyncAlpha) * delta;
        }

        else if (latency_avg.getInput () < (auto_bias_target_ms * .95f))
        {
          delta = (float)effective_frametime () * SK_LatentSyncDeltaMultiplier * SK_LatentSyncBackOffMultiplier;

          config.render.framerate.latent_sync.delay_bias =
            SK_LatentSyncAlpha * config.render.framerate.latent_sync.delay_bias - (1.0f - SK_LatentSyncAlpha) * delta;
          //config.render.framerate.latent_sync.delay_bias -= static_cast <float> (effective_frametime () * 0.000666);
        }
```

Relevant constants (defaults):
- `SK_LatentSyncAlpha = 0.991f`
- `SK_LatentSyncDeltaMultiplier = 0.133f`
- `SK_LatentSyncBackOffMultiplier = 1.020f`

Other context:
- `effective_frametime()` returns the current effective frame time in milliseconds.
- `latency_avg` maintains simple moving averages of input and display timings:
  - `input[i] = (1000 / limit) - effective_frametime()`
  - `display[i] = effective_frametime()`
- Target is either a fixed ms or derived from a percent of `(input + display)`.
- Deadband: ±5% around target to avoid oscillation.
- After update, `delay_bias` is clamped to `[0, max_auto_bias]` and converted to ticks for `__SK_LatentSyncPostDelay`.

#### Backoff branch behavior (below target)
Trigger: `latency_avg.getInput() < auto_bias_target_ms * 0.95`.

Computation:
- `delta = effective_frametime() * SK_LatentSyncDeltaMultiplier * SK_LatentSyncBackOffMultiplier`
- `delay_bias_new = SK_LatentSyncAlpha * delay_bias_old - (1 - SK_LatentSyncAlpha) * delta`

Interpretation:
- Uses an exponential moving average (EMA) with smoothing factor `alpha ≈ 0.991` to gradually reduce `delay_bias`.
- The backoff uses a slightly larger step than the increase path (`BackOffMultiplier > 1`) so reductions happen marginally faster than increases, aiding convergence when overshooting low-latency.
- Scaling by `effective_frametime()` makes adjustments proportional to the frame duration, maintaining similar behavior across frame rates.

Contrast (above target):
- `delta = effective_frametime() * SK_LatentSyncDeltaMultiplier`
- `delay_bias_new = alpha * delay_bias_old + (1 - alpha) * delta`

#### Intuition and tuning
- **`alpha` (0.991)**: Higher values smooth more and slow adjustment. `1 - alpha = 0.009` is the per-frame blend weight.
- **`DeltaMultiplier` (0.133)**: Scales the raw step size from `effective_frametime()`.
- **`BackOffMultiplier` (1.020)**: Increases the step when backing off to slightly favor decreasing bias when under target.
- **Deadband 5%**: Prevents constant small corrections around the target.
- **Clamping**: Keeps `delay_bias` non-negative and within configured maximum.

#### Quick numeric example
Assume 60 FPS: `effective_frametime ≈ 16.67 ms`.
- Above target delta: `16.67 * 0.133 ≈ 2.22 ms` → per-frame EMA change ≈ `(1 - 0.991) * 2.22 ≈ 0.020 ms` increase.
- Below target delta: `16.67 * 0.133 * 1.020 ≈ 2.26 ms` → per-frame EMA change ≈ `0.009 * 2.26 ≈ 0.020 ms` decrease.

The backoff is ~2% stronger than the increase path, aiding quicker exit from over-biased states while maintaining stability due to the high `alpha` and the ±5% deadband.

#### How `delta` affects sleep timing

The `delta` value computed in the auto-bias logic directly controls when Special-K sleeps relative to the present call:

**Sleep AFTER present (PostDelay):**
- `__SK_LatentSyncPostDelay` is computed from `delay_bias`:
  ```cpp
  __SK_LatentSyncPostDelay = (delay_bias == 0.0f) ? 0 : 
    static_cast<LONGLONG>(ticks_per_frame * delay_bias);
  ```
- This creates a sleep period **after** `Present()` completes
- The sleep uses `SK_Framerate_WaitUntilQPC()` with a high-resolution waitable timer
- Sleep duration = `delay_bias * frame_time` (e.g., 0.1 * 16.67ms = 1.67ms at 60 FPS)

**Sleep BEFORE present (Frame Limiting):**
- The main frame limiter runs **before** present and controls the overall frame pacing
- It uses `next_` timing calculated from scanline sync or frame intervals
- `__SK_LatentSyncPostDelay` adds additional delay **on top of** the base frame timing

**Timing sequence:**
1. Frame limiter waits until `next_` (before present)
2. `Present()` is called
3. If `delay_bias > 0`, sleep for `__SK_LatentSyncPostDelay` ticks (after present)
4. Next frame begins

**Why this matters:**
- **Before present**: Controls when the GPU starts rendering the next frame
- **After present**: Fine-tunes the timing between frame completion and the next frame start
- The auto-bias logic adjusts `delay_bias` to find the optimal balance between input latency and display timing
- Higher `delay_bias` = longer sleep after present = more time for the GPU to complete work = potentially lower input latency

#### Summary
- The backoff branch reduces `delay_bias` via a smoothed EMA proportional to frame time and tuned multipliers.
- It activates when input latency is more than 5% below the target, uses a slightly larger step, and clamps the result.
- The design provides stable, slow adjustments with minimal oscillation while favoring faster recovery from under-target scenarios.
- **`delta` → `delay_bias` → `__SK_LatentSyncPostDelay`** creates sleep time **after** present calls, fine-tuning the gap between frame completion and next frame start.


