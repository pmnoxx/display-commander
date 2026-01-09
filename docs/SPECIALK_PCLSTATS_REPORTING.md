# Special K: PCLStats (ETW) reporting for NVIDIA latency overlay

This doc summarizes how **Special K** reports Reflex-style latency markers to consumers (e.g. NVIDIA tooling/overlay) via **ETW TraceLogging**, based on the implementation in:

- `external-src/SpecialK/src/render/reflex/pclstats.cpp`
- `external-src/SpecialK/depends/include/reflex/pclstats.h`
- `external-src/SpecialK/src/render/reflex/reflex.cpp` (integration point)

## What is reported (ETW provider + events)

Special K defines a TraceLogging provider:

- **Provider name**: `"PCLStatsTraceLoggingProvider"`
- **Provider GUID**: `{0D216F06-82A6-4D49-BC4F-8F38AE56EFAB}`

The key marker event is emitted as:

- **Event name**: `"PCLStatsEvent"`
  - **Field** `"Marker"`: `UInt32`
  - **Field** `"FrameID"`: `UInt64`

There’s also a v2 variant:

- **Event name**: `"PCLStatsEventV2"`
  - **Field** `"Marker"`: `UInt32`
  - **Field** `"FrameID"`: `UInt64`
  - **Field** `"Flags"`: `UInt32` (e.g. `PCLSTATS_NO_PRESENT_MARKERS`)

Special K also emits a few lifecycle/aux events that are helpful for debugging but are not part of the core “marker stream”:

- `"PCLStatsInit"`
- `"PCLStatsShutdown"`
- `"PCLStatsFlags"` (emitted on `EVENT_CONTROL_CODE_CAPTURE_STATE`)
- `"PCLStatsInput"` (emitted by the ping thread; see below)

## Marker IDs / meaning

Special K uses the NVIDIA / PCLStats marker numbering. The PCLStats header defines (subset):

- `0` = `SIMULATION_START`
- `1` = `SIMULATION_END`
- `2` = `RENDERSUBMIT_START`
- `3` = `RENDERSUBMIT_END`
- `4` = `PRESENT_START`
- `5` = `PRESENT_END`
- `7` = `TRIGGER_FLASH`
- `8` = `PC_LATENCY_PING`
- `9..12` = out-of-band variants
- `13` = `CONTROLLER_INPUT_SAMPLE`

In practice, Special K reports the marker numeric value as `"Marker"` and the associated frame counter as `"FrameID"`.

## Provider enable/disable is controlled by ETW

Special K registers the provider using `TraceLoggingRegisterEx(...)` with a callback.

The callback reacts to ETW control codes:

- `EVENT_CONTROL_CODE_ENABLE_PROVIDER` → `g_PCLStatsEnable = true`
- `EVENT_CONTROL_CODE_DISABLE_PROVIDER` → `g_PCLStatsEnable = false`
- `EVENT_CONTROL_CODE_CAPTURE_STATE` → emits `"PCLStatsFlags"` with current `g_PCLStatsFlags`

So an external consumer can toggle whether PCLStats is actively emitting markers by enabling/disabling the ETW provider.

## “Ping” behavior (periodic PC_LATENCY_PING injection)

Special K starts a small background thread (`PCLStatsPingThreadProc`) when PCLStats is initialized:

- It wakes up at a randomized interval (roughly **100–300ms**).
- If the provider is enabled, it sets an internal, edge-triggered signal (`g_PCLStatsSignal = TRUE`).
- It may also emit `"PCLStatsInput"` containing one of:
  - `"IdThread"` (preferred path when known), or
  - `"VirtualKey"` (F13–F15), or
  - `"MsgId"` (registered window message id for `"PC_Latency_Stats_Ping"`)

Important nuance: in the current Special K code, actually posting the ping message (`PostThreadMessageW` / `PostMessageW`) is **commented out**; the thread instead just flips `g_PCLStatsSignal`.

On the marker-emission side, Special K checks that signal via:

- `PCLSTATS_IS_SIGNALED()`:
  - returns `true` only once per ping (it uses `InterlockedCompareExchange` to clear the signal back to `FALSE`).

When signaled, Special K injects an extra marker:

- NV latency marker `PC_LATENCY_PING` (`markerType == 8`)

This happens on (or just after) `SIMULATION_START` in `SK_PCL_Heartbeat` (see next section).

## Integration point: `SK_PCL_Heartbeat` (Reflex markers → ETW markers)

Special K reports PCLStats markers from `SK_PCL_Heartbeat(const NV_LATENCY_MARKER_PARAMS&)` in `reflex.cpp`.

High-level flow:

- If the game is detected as **native Reflex** (`config.nvidia.reflex.native == true`):
  - `SK_PCL_Heartbeat` returns immediately (no ETW marker reporting).
  - When native Reflex is detected in the NvAPI hook, Special K also calls `PCLSTATS_SHUTDOWN()` to stop its provider/thread (avoids duplicate/conflicting reporting).

- Otherwise (non-native / Special K-driven marker stream):
  - On the first `SIMULATION_START`, Special K initializes PCLStats:
    - `PCLSTATS_SET_ID_THREAD((DWORD)-1)`, then `PCLSTATS_INIT(0)`, then sets `g_PCLStatsEnable = true`.
    - On subsequent `SIMULATION_START`, it sets the ping thread’s `IdThread` to the current thread id (the first time it sees a real thread).
  - If `PCLSTATS_IS_SIGNALED()` returns true on `SIMULATION_START`:
    - Special K injects `PC_LATENCY_PING` via its render backend (`rb.setLatencyMarkerNV(PC_LATENCY_PING)`), or a Vulkan-specific path.
  - For every marker passed in, it emits:
    - `PCLSTATS_MARKER(marker.markerType, marker.frameID)`
      - which becomes the ETW `"PCLStatsEvent"` with `"Marker"` and `"FrameID"`.

## Practical takeaways (if you’re re-implementing)

- **ETW contract**: the critical piece is emitting `"PCLStatsEvent"` with the expected field names and types (`Marker: UInt32`, `FrameID: UInt64`) from a provider named `"PCLStatsTraceLoggingProvider"` with the GUID above.
- **Enable gating**: don’t do work unless the provider is enabled (Special K uses the ETW provider enable callback to flip a boolean).
- **Ping marker**: Special K periodically injects `PC_LATENCY_PING` using an internal signal driven by a ping thread; it does not rely on actually delivering a Windows message in the code shown.
- **Native Reflex games**: Special K explicitly shuts down its PCLStats reporting when a game uses Reflex natively, to avoid conflicts/duplicates.
- **FrameID monotonicity**: the consumer assumes frame IDs behave like a frame counter; Special K derives them from its internal “frames drawn” counter when it generates markers itself, and uses the game-provided `frameID` when receiving NVAPI markers.


