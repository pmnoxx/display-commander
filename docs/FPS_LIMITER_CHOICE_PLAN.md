# FPS Limiter Choice: ChooseFpsLimiter / GetChosenFpsLimiter Plan

## Goal

Centralize the decision of **which call site should apply the FPS limiter** for the current frame. Replace the current per-site logic (`use_fps_limiter = ...` with various conditions and `RecordFpsLimiterCallSite`) with:

1. **ChooseFpsLimiter(g_global_frame_id, caller_enum)** — called before each current `use_fps_limiter`; records this call site for the frame and, when the frame id has changed, recomputes the chosen source and optionally logs.
2. **GetChosenFpsLimiter(caller_enum)** — returns `true` iff the **current chosen** source for this frame is `caller_enum` (i.e. this call site should run the FPS limiter).

Call sites then use:

```cpp
ChooseFpsLimiter(g_global_frame_id.load(std::memory_order_relaxed), FpsLimiterCallSite::xxx);
auto use_fps_limiter = GetChosenFpsLimiter(FpsLimiterCallSite::xxx);
```

---

## Call Sites (existing)

| Call site enum              | File / location |
|----------------------------|------------------|
| `reflex_marker`            | `nvapi_hooks.cpp` — SetLatencyMarker (PRESENT_START) |
| `dxgi_swapchain`           | `dxgi_present_hooks.cpp` — Present / Present1 detours |
| `dxgi_factory_wrapper`     | `dxgi_factory_wrapper.cpp` — wrapper Present / Present1 |
| `reshade_addon_event`      | `swapchain_events.cpp` — OnPresentUpdateAfter, presentBefore (Vulkan/OpenGL path) |

---

## Priority and “skip if stale” rules

**Priority order (highest wins):**

1. `reflex_marker`
2. `dxgi_swapchain`
3. `dxgi_factory_wrapper`
4. `reshade_addon_event` (default)

**Stale rule:**

- If **no** call to `ChooseFpsLimiter(..., reflex_marker)` occurred within the **last 3 frames**, **skip** `reflex_marker`.
- If **no** call to `ChooseFpsLimiter(..., dxgi_swapchain)` occurred within the **last 3 frames**, **skip** `dxgi_swapchain`.
- If **no** call to `ChooseFpsLimiter(..., dxgi_factory_wrapper)` occurred within the **last 3 frames**, **skip** `dxgi_factory_wrapper`.
- `reshade_addon_event` is **guaranteed** to be called; it is always eligible and is the default when no higher-priority source is eligible.

“Within last 3 frames” means:  
`current_frame_id - g_fps_limiter_last_frame_id[site] <= 3`  
(with unsigned handling so “never called” = 0 is treated as very old).

---

## State

- **chosen_fps_limiter**  
  Enum value indicating which source is currently chosen to run the FPS limiter for the current decision.  
  **Initial value:** a sentinel meaning **unset** (e.g. a value outside the valid enum range, or a dedicated `unset` enumerator).  
  When unset, the first decision that selects a source is logged as a change (from unset to that source).

- **last_decision_frame_id**  
  The `g_global_frame_id` for which `chosen_fps_limiter` was last computed.  
  When `ChooseFpsLimiter(frame_id, caller_enum)` is called with `frame_id != last_decision_frame_id`, we treat this as a new frame and recompute the chosen source.

- **g_fps_limiter_last_frame_id[site]** (existing)  
  Last `g_global_frame_id` at which each call site was hit.  
  Updated by `ChooseFpsLimiter` (or by the existing `RecordFpsLimiterCallSite` which can be folded into `ChooseFpsLimiter`).

---

## ChooseFpsLimiter(frame_id, caller_enum) — behavior

**Order: first 2, then 1, then 3.** Decisions are based on *previous* frames’ data (record is done after the decision).

1. **New frame?**  
   If `frame_id != last_decision_frame_id`:
   - Set `last_decision_frame_id = frame_id`.
   - **Compute new chosen** (using existing `g_fps_limiter_last_frame_id`; do **not** record this call yet):
     - Walk sources in priority order: `reflex_marker` → `dxgi_swapchain` → `dxgi_factory_wrapper` → `reshade_addon_event`.
     - For each source, consider it **eligible** if it was seen within the last 3 frames:  
       `frame_id - g_fps_limiter_last_frame_id[site] <= 3` (with care for “never” = 0).
     - **Skip** `reflex_marker` if not eligible (no call in last 3 frames).
     - **Skip** `dxgi_swapchain` if not eligible (no call in last 3 frames).
     - **Skip** `dxgi_factory_wrapper` if not eligible (no call in last 3 frames).
     - `reshade_addon_event` is **guaranteed** to be called; treat as always eligible (default).
     - First eligible source in priority order becomes **new_chosen**.
   - **Log on change**  
     If `new_chosen != chosen_fps_limiter`, log:  
     `"FPS limiter source: <old> -> <new>"` with `old` = `unset` when `chosen_fps_limiter` was unset.
   - Set `chosen_fps_limiter = new_chosen`.

2. **Record**  
   Set `g_fps_limiter_last_frame_id[caller_enum] = frame_id` (so the next frame’s decision can use this call).

3. No return value; this is side-effect only (maybe recompute + log, then record).

---

## GetChosenFpsLimiter(caller_enum) — behavior

- Returns `true` if and only if `chosen_fps_limiter == caller_enum`.
- So only one call site per frame returns `true`; that site should run the FPS limiter.  
- Thread-safety: use atomics or the same locking as for `chosen_fps_limiter` / `last_decision_frame_id` so that callers see a consistent view (see implementation notes below).

---

## Default and “unset”

- **Initially:** `chosen_fps_limiter == unset`.  
- First time we compute a chosen source (on first new frame), we log: `unset -> reflex_marker` (or whatever won).  
- **Default when no higher-priority source is eligible:** use `reshade_addon_event` so that ReShade addon event path remains the fallback.

---

## Replacement at call sites

- **Before:** each site had its own condition (e.g. `api == vulkan || api == opengl || ...`, or `!safe_mode && !ShouldUseNativeFpsLimiterFromFramePacing()`, etc.) and then `RecordFpsLimiterCallSite(...)`.
- **After:** at each of the existing `use_fps_limiter` locations:
  1. Call `ChooseFpsLimiter(g_global_frame_id.load(std::memory_order_relaxed), FpsLimiterCallSite::xxx)`.
  2. Set `bool use_fps_limiter = GetChosenFpsLimiter(FpsLimiterCallSite::xxx);`
  3. Remove the previous local condition and the separate `RecordFpsLimiterCallSite` (recording is inside `ChooseFpsLimiter`).

So the logic becomes: “register that I was called this frame, then ask: am I the chosen source for this frame?”

---

## Edge cases

- **Same frame, multiple call sites**  
  The first call in a given frame that sees `frame_id != last_decision_frame_id` triggers the decision for that frame. Later calls in the same frame only record their site; they do not recompute. So the decision is made with the **latest** `g_fps_limiter_last_frame_id[*]` updates that have happened so far this frame. If reflex runs before DXGI in the same frame, we have already recorded reflex for this frame when we recompute; DXGI might not be recorded yet for this frame, so it might look “not in last 3” only if it wasn’t called in the previous 3 frames. This is acceptable: we prefer reflex when it’s active recently.

- **Frame id never advances**  
  If `g_global_frame_id` never increments (e.g. no present), we never recompute again; last chosen remains. GetChosenFpsLimiter still returns based on that last chosen.

- **Thread safety**  
  `g_global_frame_id` and `g_fps_limiter_last_frame_id` are already atomics. `chosen_fps_limiter` and `last_decision_frame_id` are updated in `ChooseFpsLimiter`. Prefer storing `chosen_fps_limiter` (and possibly `last_decision_frame_id`) in atomics so that `GetChosenFpsLimiter` can read without a lock. If we need a lock for multi-step decision, keep the critical section small and only in `ChooseFpsLimiter`.

---

## Logging format (by default)

- When the decision **changes** (including from unset to first chosen), log once, e.g.:
  - `"FPS limiter source: unset -> reflex_marker"`
  - `"FPS limiter source: dxgi_swapchain -> reshade_addon_event"`
- Use a stable string for the enum (e.g. `reflex_marker`, `dxgi_swapchain`, `dxgi_factory_wrapper`, `reshade_addon_event`, `unset`).

---

## Summary

| Item | Description |
|------|-------------|
| **ChooseFpsLimiter(frame_id, caller_enum)** | Record call site for this frame; if frame id changed, recompute chosen source (priority + “last 3 frames” skip for reflex and dxgi_swapchain), default reshade_addon_event; log when chosen changes (default unset). |
| **GetChosenFpsLimiter(caller_enum)** | Returns whether this caller is the chosen source for the current decision. |
| **Priority** | reflex_marker > dxgi_swapchain > dxgi_factory_wrapper > reshade_addon_event. |
| **Skip if stale** | reflex_marker and dxgi_swapchain are skipped if not called in last 3 frames. |
| **Initial** | chosen_fps_limiter = unset; first decision is logged as unset -> X. |
| **Call sites** | Replace existing `use_fps_limiter` logic with the two calls above. |

This keeps a single place that defines “who runs the FPS limiter this frame” and makes changes visible in the log when the chosen source changes.
