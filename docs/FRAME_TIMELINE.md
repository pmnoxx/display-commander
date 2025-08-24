## Frame Timeline (for FPS limiters)

This project exposes a per-frame timeline to FPS limiters so any limiter implementation can consume consistent timestamps without directly hooking disparate systems.

### Access
- Include `src/addons/display_commander/dxgi/custom_fps_limiter_manager.hpp` and use the global `dxgi::fps_limiter::g_customFpsLimiterManager`.
- Call `GetTimelineSnapshot()` to obtain a thread-safe snapshot of the latest `FrameTimeline`:

```cpp
auto snapshot = dxgi::fps_limiter::g_customFpsLimiterManager->GetTimelineSnapshot();
```

### Timestamps
All timestamps use `std::chrono::high_resolution_clock::time_point`.

- frame_id: Monotonic frame id if available (from Reflex), otherwise 0.
- simulation_started: First CPU step of the frame simulation.
  - Source: NVIDIA Reflex SIMULATION_START (when Reflex markers enabled and available).
- simulation_done: End of CPU simulation for this frame.
  - Source: NVIDIA Reflex SIMULATION_END.
- render_submit_started: Start of CPU render submission for this frame.
  - Source: NVIDIA Reflex RENDERSUBMIT_START.
- render_submit_done: End of CPU render submission for this frame.
  - Source: NVIDIA Reflex RENDERSUBMIT_END.
- begin_render_pass: CPU begins a render pass.
  - Source: ReShade `begin_render_pass` callback.
- end_render_pass: CPU ends a render pass.
  - Source: ReShade `end_render_pass` callback.
- present_started: Present() entry.
  - Source: NVIDIA Reflex PRESENT_START (NVAPI).
- present_done: Present() exit.
  - Source: NVIDIA Reflex PRESENT_END (NVAPI).
- on_present: ReShade `present` callback (frame boundary observed by ReShade).
  - Source: ReShade `present`.

Notes:
- Not all sources are always available. Fields remain default-initialized when a source is unavailable or disabled.
- ReShade callbacks are always present; Reflex markers require NVIDIA hardware/driver support and enabled settings.

### Implementation Details
- Struct: `dxgi::fps_limiter::FrameTimeline` in `custom_fps_limiter_manager.hpp`.
- Updates: `NotifyTimelineEvent(TimelineEvent, uint64_t frame_id = 0)` called from:
  - ReShade: `OnBeginRenderPass`, `OnEndRenderPass`, `OnPresentUpdate`.
  - Reflex: `SetLatencyMarkers` (simulation, render submit), `SetPresentMarkers` (present start/end).
- Getter: `FrameTimeline GetTimelineSnapshot() const` on `CustomFpsLimiterManager`.

### Example Usage in a Limiter
```cpp
const auto timeline = dxgi::fps_limiter::g_customFpsLimiterManager->GetTimelineSnapshot();
// Use timeline.simulation_done, timeline.render_submit_done, timeline.on_present, etc.
```


### Callback Lifetime and Ordering
- OnBeginRenderPass: Fired when ReShade observes the CPU begin a render pass. May occur multiple times per frame. Updates `timeline.begin_render_pass` and invokes the active limiter’s `OnFrameBegin()`.
- OnEndRenderPass: Fired when the corresponding render pass ends. May occur multiple times per frame. Updates `timeline.end_render_pass` and invokes the active limiter’s `OnFrameEnd()`.
- OnPresentUpdate: Fired once per DXGI `Present` (ReShade present callback). Updates `timeline.on_present` and also drives Reflex each frame (sleep mode refresh and marker emission). Specifically:
  - Calls `SetLatencyMarkers` to emit `SIMULATION_START/END` and `RENDERSUBMIT_START/END` and stamps the timeline for those.
  - Calls `SetPresentMarkers` to emit `PRESENT_START/END` and stamps the timeline for those.

Notes on current Reflex marker placement:
- In this project the Reflex markers are emitted from `OnPresentUpdate` (our layer), not inserted by the game engine at its true simulation/submit boundaries. As a result, these timestamps reflect when our layer calls NVAPI rather than authoritative in-engine boundaries. They remain useful for relative timing and overlays but should not be treated as ground-truth unless the application itself places markers.

### Measuring Simulation Time
- With Reflex markers available: Use the difference between `simulation_done` and `simulation_started`.
```cpp
const auto t = dxgi::fps_limiter::g_customFpsLimiterManager->GetTimelineSnapshot();
if (t.simulation_started.time_since_epoch().count() != 0 &&
    t.simulation_done.time_since_epoch().count() != 0) {
  const double sim_ms = std::chrono::duration<double, std::milli>(
      t.simulation_done - t.simulation_started).count();
  // consume sim_ms
}
```
- Without Reflex markers: There is no reliable way to infer game simulation time from ReShade/DXGI alone. Consider fallbacks:
  - CPU render-submit span: `render_submit_done - render_submit_started`.
  - Frame cadence: delta between consecutive `on_present` stamps.
  - Any heuristic “simulation” estimate from render-pass boundaries is game-dependent and approximate.


### Frame lifetime overview (what we can observe here)
Typical order in this project’s timeline:
- Simulation: Reflex SIMULATION_START → SIMULATION_END
- Render submit: Reflex RENDERSUBMIT_START → RENDERSUBMIT_END
- Render pass: ReShade OnBeginRenderPass → OnEndRenderPass (may occur multiple times)
- Present: NVAPI PRESENT_START → PRESENT_END, then ReShade OnPresentUpdate (“present” callback)

Note: In this project, Reflex markers are emitted from our layer during OnPresentUpdate, not by the game at the true engine boundaries. They’re useful for relative timings but may not be ground-truth unless the game sets them.

### Mapping your 8 questions to what’s available

1) CPU computation starts (simulation start)
- Where: `timeline.simulation_started`
- Source: Reflex `SIMULATION_START`
- Reliability: Good if the game places markers; in this project they’re emitted from our layer, so treat as approximate engine start.

2) CPU computation ends (simulation end)
- Where: `timeline.simulation_done`
- Source: Reflex `SIMULATION_END`
- Reliability: Same caveat as above.

3) CPU communicates with GPU to start rendering
- Where: `timeline.render_submit_started`
- Source: Reflex `RENDERSUBMIT_START`
- Alt proxy: `timeline.begin_render_pass`
- Reliability: Best via Reflex marker; render-pass begin is a coarse proxy.

4) CPU sends instructions to GPU (during submit)
- Where: duration between `timeline.render_submit_started → timeline.render_submit_done`
- Source: Reflex `RENDERSUBMIT_START/END`
- Reliability: Good as a CPU submit window; does not include GPU execution time.

5) CPU ends instructions
- Where: `timeline.render_submit_done`
- Source: Reflex `RENDERSUBMIT_END`
- Alt proxy: `timeline.end_render_pass` (for the active pass)
- Reliability: Same caveat as 3/4.

6) GPU presents a ready frame to “reader”
- Where: `timeline.present_started / timeline.present_done`
- Source: NVAPI `PRESENT_START/END`
- Meaning: These bracket the CPU Present call. They do NOT mean “on screen” yet; flip/queueing and scanout happen after.

7) Frame is sent to display (scanout begins)
- Where: Not directly available in this repo.
- How to estimate: Use monitor refresh (we already know it) and assume vsync-aligned scanout after Present if vsync is on; otherwise timing varies. Precise timestamps require OS/driver telemetry (e.g., DXGI frame stats/DWM present stats/GPU timestamp queries), which we don’t currently record.

8) Display renders and is ready for next frame (scanout ends)
- Where: Not directly available here.
- How to estimate: `scanout_end ≈ scanout_start + (1 / refresh_rate)`. Again needs display timing APIs for accuracy.

### Quick usage notes
- Simulation time: `simulation_ms = simulation_done − simulation_started`
- Render-submit CPU window: `submit_ms = render_submit_done − render_submit_started`
- Frame cadence: dt between consecutive `on_present` stamps
- Present window: `present_call_ms = present_done − present_started`

### What we cannot see here
- Exact GPU execution duration or precise on-screen times. Those require GPU timestamp queries or OS/driver-present statistics (not currently hooked in this project).


### Q2

Which of points 1-6 in "Mapping to your 8 points (Special-K perspective)" we can determine from ReShade calls: `OnBeginRenderPass` / `OnEndRenderPass` / `OnPresentUpdate`, and which we can't?

### A2

From only ReShade callbacks (`OnBeginRenderPass` / `OnEndRenderPass` / `OnPresentUpdate`):
1) CPU simulation start (SIMULATION_START): Not determinable.
2) CPU simulation end (SIMULATION_END): Not determinable.
3) CPU begins render submit (RENDERSUBMIT_START): Approximate via first `OnBeginRenderPass` of the frame.
4) CPU sending commands window: Approximate as span between first `OnBeginRenderPass` and last `OnEndRenderPass` of the frame.
5) CPU ends instructions (RENDERSUBMIT_END): Approximate via last `OnEndRenderPass` before present.
6) Present call window (PRESENT_START/END): Not determinable; `OnPresentUpdate` fires after Present returns, giving only a single timestamp post-present (useful for cadence, not bracketing).

### Q3

Which extra calls from ReShade would be needed to compute RTSS-like stats (Simulation, Render submit, Present, Driver, Render Queue, GPU render, GPU Active)?

### A3

From only `OnBeginRenderPass` / `OnEndRenderPass` / `OnPresentUpdate` you cannot derive all RTSS stages. You need these extra hooks/APIs:

- Simulation
  - Need: Reflex SIMULATION_START/END timestamps.
  - How: Integrate NVIDIA Reflex and read stage times:
    - D3D11/12: `NvAPI_D3D_SetLatencyMarker` + `NvAPI_D3D_GetLatency`
    - Vulkan: `vkSetLatencyMarkerNV` + `vkGetLatencyTimingsNV` (or `NvLL_VK_GetLatency`)
  - Without Reflex: not derivable from ReShade alone.

- Render submit
  - Better proxy than render passes: catch first/last submit of rendering work.
  - Need: callbacks around “first render work of frame” and “last render work before Present”:
    - D3D11: first Draw/Dispatch of frame and last before Present
    - D3D12/Vulkan: first `ExecuteCommandLists`/queue submit and last before Present
  - Best: Reflex RENDERSUBMIT_START/END via APIs above.

- Present
  - Need: a pre-present callback to bracket the call.
  - Add: `OnBeforePresent` (PRESENT_START). `OnPresentUpdate` already maps to PRESENT_END (after Present returns).

- Driver
  - Only available via vendor telemetry.
  - Need: `NvAPI_D3D_GetLatency` (D3D) or `vkGetLatencyTimingsNV`/`NvLL_VK_GetLatency` (Vulkan).
  - Not derivable from ReShade callbacks.

- OS Render Queue
  - Same as Driver: comes from Reflex latency results.
  - Need: the same NVAPI/VK low latency query.
  - Not derivable from ReShade callbacks.

- GPU render
  - Option A: Reflex query gives `gpuRenderStart/End`.
  - Option B (vendor-agnostic): insert GPU timestamp queries around main workload and read back each frame:
    - D3D11: `ID3D11Query` (TIMESTAMP + DISJOINT)
    - D3D12: `ID3D12QueryHeap` (TIMESTAMP) + resolve/copy/readback
    - Vulkan: `vkCmdWriteTimestamp`
  - Requires per-frame query management in ReShade.

- GPU active
  - Option A: use Reflex `gpuActiveRenderTimeUs` / `gpuFrameTimeUs`.
  - Option B: compute from GPU timestamps (active = sum of busy intervals). Still needs careful placement and readback.

In short:
- Add `OnBeforePresent`.
- Add low-level hooks around first/last draw/submit per frame (or adopt Reflex RENDERSUBMIT markers).
- Integrate NVIDIA Reflex latency queries (D3D: `NvAPI_D3D_GetLatency`; VK: `vkGetLatencyTimingsNV`/`NvLL_VK_GetLatency`) to get Driver/OS Queue/GPU timings.
- Optionally, add GPU timestamp query support for non-NVIDIA paths.
