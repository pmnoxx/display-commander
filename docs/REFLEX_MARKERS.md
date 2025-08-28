### Special-K Reflex integration: markers and timing

This summarizes how Special-K integrates NVIDIA Reflex and when it emits latency markers and ETW events. It mirrors the behavior we replicate in our addon.

### Components
- **NVAPI markers**: `SIMULATION_START`, `SIMULATION_END`, `RENDERSUBMIT_START`, `RENDERSUBMIT_END`, `PRESENT_START`, `PRESENT_END`, `INPUT_SAMPLE`, `TRIGGER_FLASH`, plus out-of-band variants.
- **PCLStats ETW provider**: `PCLStatsTraceLoggingProvider` with events:
  - `PCLStatsInit`, `PCLStatsShutdown`
  - `PCLStatsFlags` (bitfield: enable/low-latency/boost/use-markers)
  - `PCLStatsInput` (periodic heartbeat: thread id or window message)
  - `PCLStatsEvent` (V1 marker: `Marker`, `FrameID`)
  - `PCLStatsEventV2` (V2 marker: `Marker`, `FrameID`, `Flags`)

### Initialization and heartbeat
- On first `SIMULATION_START`, Special-K:
  - Registers ETW provider via `TraceLoggingRegisterEx`
  - Emits `PCLStatsInit` and `PCLStatsFlags`
  - Starts a ping thread that periodically sends `PCLStatsInput` and sets an interlocked signal
  - Records thread id for pinging when requested by the overlay

### Marker emission strategy
- For each NVAPI marker submission, Special-K also emits a matching ETW event:
  - Emits `PCLStatsEvent` with the same marker type and frame id.
  - V2 markers with flags (`PCLStatsEventV2`) are supported; the common path uses V1.
- Present markers (`PRESENT_START`/`PRESENT_END`) are submitted through NVAPI. ETW emission is allowed (same `PCLStatsEvent` API) but may be gated by accurate timing policy in some builds. In practice, SK’s `SK_PCL_Heartbeat` forwards whatever marker type is set.
- If a game places `INPUT_SAMPLE` out of order, SK fixes it by inserting an input marker between `SIMULATION_START` and `SIMULATION_END` and reordering as needed.

### Typical per-frame sequence
1) Frame begin / sim start
   - NVAPI: `SIMULATION_START`
   - ETW: `PCLStatsEvent(SIMULATION_START)`
   - If overlay pings: NVAPI: `PC_LATENCY_PING` (or ETW heartbeat)
2) Optional input
   - NVAPI: `INPUT_SAMPLE` (if absent, SK may synthesize one between sim start/end)
   - ETW: `PCLStatsEvent(INPUT_SAMPLE)`
3) Sim end
   - NVAPI: `SIMULATION_END`
   - ETW: `PCLStatsEvent(SIMULATION_END)`
4) Render submission
   - NVAPI: `RENDERSUBMIT_START` → `RENDERSUBMIT_END`
   - ETW: `PCLStatsEvent(RENDERSUBMIT_START/END)`
5) Present
   - NVAPI: `PRESENT_START` → `PRESENT_END`
   - ETW: commonly also `PCLStatsEvent(PRESENT_START/END)` (implementation-dependent)

### Sleep mode
- SK configures NVAPI sleep mode (`NvAPI_D3D_SetSleepMode`) with low-latency mode/boost and optional frame limiter, then calls `NvAPI_D3D_Sleep` each frame at the enforcement site. Settings may be refreshed periodically.

### Our addon mapping
- We mirror SK behavior by:
  - Initializing an ETW provider with the same name and GUID
  - Emitting `PCLStatsInit`, `PCLStatsFlags`, and periodic `PCLStatsInput`
  - Submitting NVAPI markers for sim, render submit, present, and input each frame
  - Emitting ETW `PCLStatsEvent` (and `V2` with flags) alongside markers
  - Refreshing sleep mode periodically and calling NVAPI `Sleep` at frame start


