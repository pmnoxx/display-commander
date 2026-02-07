# Frame Data Cyclic Buffer — Design & Plan

## 1. Purpose

- **Goal**: Store per-frame timestamps (present start/end and related) in a cyclic buffer keyed by `g_global_frame_id`, so consumers (e.g. `UpdateFrameTimelineCache`) can use **per-frame** data instead of only global smoothed values.
- **Context**: Today we have `g_present_start_time_ns` and derived durations (e.g. `g_present_duration_ns`) as single globals. The frame timeline UI uses smoothed durations. A cyclic buffer allows:
  - Per-frame timelines (e.g. “frame N” start/end in wall-clock ns).
  - Future use in `UpdateFrameTimelineCache` to compute phases from real timestamps for a given frame id.
- **Scope (current phase)**: Add the data structure and buffer only; **no writes and no consumers yet**.

---

## 2. Current Data Flow (Reference)

- **Present “start”**: Set in `HandleFpsLimiter` (swapchain_events.cpp) as `g_present_start_time_ns` after FPS limiter runs (start of the frame that will complete next).
- **Present “end”**: Effectively when `OnPresentUpdateAfter2` runs; `start_time_ns` there is used to compute `g_present_duration_ns = start_time_ns - g_present_start_time_ns`. There is no separate `g_present_end_time_ns` today.
- **Frame ID**: `g_global_frame_id` is incremented in `OnPresentUpdateAfter2` (in `HandlePresentAfter`) **after** present-end work and **before** `HandleOnPresentEnd()`. So:
  - When we **start** a frame: `g_global_frame_id` = N (last completed frame id).
  - The frame we are starting will get id **N+1** when it completes.
  - When we **finish** a frame: we have just completed frame **N**; `g_global_frame_id` is still N; we write frame N’s end data, then `fetch_add(1)` → N+1.

---

## 3. Cyclic Buffer Design

### 3.1 Indexing

- **Size**: 64 slots (power of two for cheap modulo).
- **Index**: `slot_index = frame_id % 64`.
- **Frame start**: The frame we are starting will have `frame_id = g_global_frame_id + 1` when it completes → write to slot `(g_global_frame_id + 1) % 64`.
- **Frame end**: The frame we just completed has `frame_id = g_global_frame_id` (before increment) → write to slot `g_global_frame_id % 64`.

So:
- At **frame start** (in `HandleFpsLimiter`): write `present_start_time_ns` (and any other “start” timestamps) to `g_frame_data[(g_global_frame_id.load() + 1) % 64]`.
- At **frame end** (in `OnPresentUpdateAfter2`, before `g_global_frame_id.fetch_add(1)`): write `present_end_time_ns` and other “end” timestamps to `g_frame_data[g_global_frame_id.load() % 64]`.

### 3.2 Per-Frame Structure (FrameData)

Each slot holds timestamps that belong to the same frame id. All in nanoseconds (QPC-based, same as `utils::get_now_ns()`).

| Field | Description | When set |
|-------|-------------|----------|
| `frame_id` | Frame id this slot corresponds to (for validation). | Frame end |
| `present_start_time_ns` | Start of “present” for this frame (after FPS limiter). | Frame start |
| `present_end_time_ns` | End of present (when OnPresentUpdateAfter2 ran). | Frame end |
| `sim_start_ns`, `submit_start_time_ns`, `render_submit_end_time_ns`, `present_update_after2_time_ns`, `gpu_completion_time_ns` | Phase timestamps. | Frame end |
| `sleep_pre_present_start_time_ns`, `sleep_pre_present_end_time_ns` | Sleep/pacing before present (FPS limiter). | Frame start |
| `sleep_post_present_start_time_ns`, `sleep_post_present_end_time_ns` | Sleep/pacing after present (TimerPresentPacingDelay). | Frame end |

- Use `0` for “not set” so readers can detect missing data.
- **Phase 1**: Add only `present_start_time_ns`, `present_end_time_ns`, and `frame_id`. Leave room (e.g. reserved fields or a short “etc.” comment) for the other timestamps needed by `UpdateFrameTimelineCache` later.

### 3.3 Thread Safety

- **Writers**: Render thread only (same as `g_present_start_time_ns` / present-end path). One frame at a time; no std locks (project rule).
- **Readers**: UI / `UpdateFrameTimelineCache` (later). Reading a slot by index is safe if we only read slots for “recent” frame ids (e.g. `g_global_frame_id - 1` through `g_global_frame_id - 64`). No lock required for “read last completed frame” style access; optional atomic or volatile for the fields if we want a single-read consistency (for now plain struct is acceptable and we can tighten later).

---

## 4. Relation to UpdateFrameTimelineCache

- **Current**: `UpdateFrameTimelineCache` uses global smoothed values: `g_frame_time_ns`, `g_simulation_duration_ns`, `g_render_submit_duration_ns`, `g_present_duration_ns`, `fps_sleep_before_on_present_ns`, `fps_sleep_after_on_present_ns`, `g_reshade_overhead_duration_ns`, `g_gpu_duration_ns`.
- **Future**: With the cyclic buffer, we can optionally derive durations for a **specific** frame id from `g_frame_data[frame_id % 64]` (e.g. `present_end_time_ns - present_start_time_ns` for that frame, and similarly for other phases when we add those timestamps). The design doc and struct reserve space for those extra timestamps so we don’t have to change the layout when we add them.

---

## 5. Implementation Plan

### Phase 1 — Add structure only (no writes, no use)

1. **Constants**
   - Define `kFrameDataBufferSize = 64` (power of two).

2. **Struct**
   - Define `FrameData` with:
     - `uint64_t frame_id`
     - `LONGLONG present_start_time_ns`
     - `LONGLONG present_end_time_ns`
     - Reserved / placeholder fields or comment for: sim_start_ns, submit_start_time_ns, render_submit_end_time_ns, present_update_after2_time_ns, gpu_completion_time_ns (to align with UpdateFrameTimelineCache and existing globals).

3. **Buffer**
   - Define `g_frame_data[64]` (or `std::array<FrameData, 64>`), default-initialized (zeros).

4. **Location**
   - Struct and buffer in a single place: either `globals.hpp` / `globals.cpp` (with extern declaration in header) or a small dedicated header (e.g. `frame_data.hpp`) plus one .cpp for the buffer. Prefer keeping with existing globals pattern (`g_present_start_time_ns` in globals.hpp/globals.cpp).

5. **No behavior change**
   - Do **not** add any writes to `g_frame_data` in this phase.
   - Do **not** change `UpdateFrameTimelineCache` or any other consumer.

### Phase 2 — Populate buffer (implemented)

1. At **frame start** (in `HandleFpsLimiter`, swapchain_events.cpp): write `present_start_time_ns` to `g_frame_data[g_global_frame_id.load() % kFrameDataBufferSize]` (the frame we’re starting has id = current g_global_frame_id).

2. At **frame start** also write `sleep_pre_present_start_time_ns`, `sleep_pre_present_end_time_ns` (from HandleFpsLimiter locals).

3. At **frame end** (in `OnPresentUpdateAfter2`): call `TimerPresentPacingDelayEnd(start_ns)` right after `HandleFpsLimiterPost()` to get `end_ns`. Write to `g_frame_data[current_frame_id % kFrameDataBufferSize]`: `frame_id`, `sim_start_ns`, `submit_start_time_ns`, `render_submit_end_time_ns`, `present_start_time_ns`, `present_end_time_ns`, `present_update_after2_time_ns`, `gpu_completion_time_ns`, `sleep_post_present_start_time_ns` (= start_ns), `sleep_post_present_end_time_ns` (= end_ns).

### Phase 3 — Use in UpdateFrameTimelineCache (implemented)

- `UpdateFrameTimelineCache()` (main_new_tab.cpp) uses **only** `g_frame_data`: last completed frame = `(g_global_frame_id - 1) % kFrameDataBufferSize`. All phase boundaries (sim_start_ms, sim_end_ms, render_end_ms, present_start_ms, present_end_ms, gpu_end_ms) are computed directly from timestamps relative to `sim_start_ns` (base). No `double t = 0.0` accumulation; phases are Simulation, Render Submit, ReShade, Present, and optionally GPU. FPS Sleep (before/after) use `sleep_pre_present_*` and `sleep_post_present_*` timestamps.

---

## 6. Summary

- **Add**: Cyclic buffer `g_frame_data[64]` of `FrameData` with `frame_id`, `present_start_time_ns`, `present_end_time_ns`, and reserved space for other timestamps.
- **Index**: `g_global_frame_id % 64` (frame end) and `(g_global_frame_id + 1) % 64` (frame start).
- **Now**: Structure and buffer only; no writes, no consumers.
- **Later**: Phase 2 = populate from present start/end; Phase 3 = use in UpdateFrameTimelineCache.
