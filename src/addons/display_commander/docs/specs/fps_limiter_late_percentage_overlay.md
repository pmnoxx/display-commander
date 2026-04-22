# FPS limiter late-time percentage (overlay) spec

## Purpose

Expose a lightweight overlay metric that answers:

> "What percentage of recent time did the FPS limiter spend in a **late** state?"

The metric uses a bounded recent window:

- at most **10,000 frames**
- at most **10 seconds**
- effective window = the smaller of the two bounds

This is intended to make limiter stability visible during gameplay, especially for `OnPresentSync`.

## Scope

- **Primary timing source**: `src/addons/display_commander/swapchain_events.cpp` (OnPresentSync branch around late/on-time decision).
- **Overlay render**: `src/addons/display_commander/ui/new_ui/controls/performance_overlay/overlay_content.cpp`.
- **Overlay toggle setting**: `src/addons/display_commander/settings/main_tab_settings.hpp/.cpp`.
- **Overlay controls UI**: `src/addons/display_commander/ui/new_ui/controls/performance_overlay/important_info.cpp`.

## Terminology

- **Frame start**: `start_time_ns` sampled inside FPS limiter pre-present path.
- **Late frame**: branch where `start_time_ns >= (ideal_frame_start_ns - post_sleep_ns)` (current `else` branch that writes `late_amount_ns`).
- **Late time contribution** (per frame): non-negative nanoseconds of lateness for the frame.
- **Window**: sliding set of latest frame samples, constrained by frame count and age.

## Functional behavior

### 1) When sample is recorded

Record one sample per frame in the FPS limiter pre-present path, only when limiter logic is active.

- If mode is `OnPresentSync` and limiter branch executes:
  - `late_ns = max(0, start_time_ns - (ideal_frame_start_ns - post_sleep_ns))`
  - This is equivalent to existing late branch logic intent.
- If limiter is active in other modes (Reflex / LatentSync), record sample with `late_ns = 0` and mode tag (optional) to keep time continuity.
- If limiter is disabled or target FPS is not limiting, no sample is appended.

### 2) Window construction

Maintain a lock-free ring buffer of fixed capacity:

- `kLateWindowMaxFrames = 10000`
- sample fields:
  - `frame_start_ns`
  - `late_ns`

When reading for overlay:

1. Start from newest sample and walk backward.
2. Stop when either:
   - collected 10,000 samples, or
   - sample age exceeds 10 seconds from newest sample timestamp.
3. Compute:
   - `window_span_ns = newest.frame_start_ns - oldest.frame_start_ns` (clamped >= 1)
   - `late_sum_ns = sum(late_ns)`
   - `late_time_pct = clamp(100.0 * late_sum_ns / window_span_ns, 0, 100)`

Notes:

- Using time span (not sample count ratio) matches request wording: "percentage of time".
- At startup / sparse history, metric uses available samples only.

### 3) Overlay value display

Add an optional scalar row to the OSD:

- **Short label**: `Late%`
- **Full label**: `FPS limiter late time`
- **Format**: `%.1f%%`
- **Tooltip**:
  - "Percentage of recent time the FPS limiter was late (window: last 10,000 frames or 10 s, whichever is smaller)."

### 4) Visibility / guard conditions

- Row is shown only when new setting toggle is enabled.
- If not enough data (`< 2 samples` or invalid span), show dimmed `N/A`.
- In `DC_LITE`, behavior is unchanged (feature allowed; no dependency on excluded modules).

## Data model and thread safety

### Storage

Add a dedicated stats container in `swapchain_events.cpp` (or related fps limiter stats unit):

- fixed-size array/ring of samples (`10000`)
- atomic write index
- atomic sample count (bounded)

Recommended pattern:

- writer thread (present path) writes sample payload first, then publishes index/count with release semantics.
- reader (overlay UI) snapshots index/count with acquire semantics and reads stable entries.

No `std::mutex` usage.

### Numeric safety

- `late_ns` must be clamped to `>= 0`.
- Accumulator uses 64-bit signed (`LONGLONG`) or wider temporary where needed.
- If `window_span_ns <= 0`, return invalid metric (`N/A`).

## Settings and config keys

Add a new persisted bool in main tab settings:

- member name: `show_fps_limiter_late_time_pct`
- key: `"show_fps_limiter_late_time_pct"`
- section: `"DisplayCommander"`
- default: `false`

Include it in `all_settings_`.

## UI wiring

### Overlay controls panel

In overlay options (same area as other metric toggles), add checkbox:

- label: `Late % (FPS limiter)`
- binds to `show_fps_limiter_late_time_pct`

### Overlay content

In `DrawPerformanceOverlayContent`:

- read toggle setting
- fetch computed metric snapshot from swapchain/fps-limiter stats helper
- render scalar row with existing table helpers (`OverlayTableRow_Text*`)

## Non-goals

- No graph for this metric in this change.
- No per-mode breakdown (Reflex vs OnPresentSync vs LatentSync) in overlay row.
- No historical persistence across process restarts.

## Performance requirements

- Per-frame write path adds O(1) work and no heap allocation.
- Overlay read cost is bounded by at most 10,000 samples and occurs only when overlay row is enabled.
- No additional blocking waits, locks, or OS calls in hot path.

## Edge cases

- **Startup**: insufficient samples -> `N/A`.
- **Long stalls / pause**: 10-second cap naturally excludes stale older history.
- **Clock anomalies**: if timestamp order is invalid, fail safely to `N/A` for that read.
- **Limiter off/on transitions**: metric naturally reflects only periods with recorded samples.

## Acceptance criteria

1. With limiter active and mostly on-time, overlay `Late%` remains near `0.0%`.
2. Injected consistent lateness increases `Late%` proportionally to late-time share.
3. After >10 seconds, old late spikes age out and metric decays accordingly.
4. With >10,000 recent frames in <10 seconds, only latest 10,000 frames are considered.
5. Feature introduces no `std::mutex` and no frame-time regressions attributable to synchronization.

## Suggested implementation steps

1. Add settings key + checkbox toggle for overlay control.
2. Add ring-buffer sample writer in FPS limiter pre-present path.
3. Add metric snapshot helper (`GetFpsLimiterLateTimePercentageSnapshot`).
4. Render overlay row with `N/A` fallback.
5. Add lightweight debug logging only if needed (disabled by default).
