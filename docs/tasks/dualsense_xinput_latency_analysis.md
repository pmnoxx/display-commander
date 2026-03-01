# DualSense-to-XInput Latency: Causes and Special K Comparison

## Why our implementation can add latency

### 1. **ReadFile on the game’s thread (main cause)**

We read HID **synchronously** on every `XInputGetState` call:

- Game calls `XInputGetState(0)` → we call `ConvertDualSenseToXInput(0, pState)` → `DualSensePollingOnce()` → `ReadDualSenseState()` → **`UpdateDeviceStates()`** → for each device **`ReadFile(handle, …)`** (blocking/sync).
- So every game poll does a kernel transition and a synchronous HID read on the **same thread** the game uses for input. That adds:
  - **Kernel/driver cost** every poll (e.g. 250×/s).
  - **Possible blocking** if the driver waits for a report (even with `setPollingFrequency(0)` behaviour can vary).
  - **Extra work** (parse, copy) on the hot path.

So the main structural issue is: **HID is read on the game’s poll path**, not on a dedicated thread.

### 2. **No background polling**

We have a `DualSensePollingThread` but its loop is **commented out**. The thread exits immediately, so we never keep state fresh in the background. All reads happen only when the game calls `XInputGetState`, which ties HID read latency directly to the game’s poll.

### 3. **We don’t set HID buffer count**

Special K uses **`setBufferCount`** (`IOCTL_SET_NUM_DEVICE_INPUT_BUFFERS`) to cap the HID input queue (e.g. 2). That:

- Limits how many reports can sit in the queue.
- Reduces “old report” latency when the game doesn’t read for a while (fewer stale reports).
- Can make “latest report” semantics more predictable.

We only added **`setPollingFrequency(0)`**; we did not add `setBufferCount`. So we don’t get this part of Special K’s tuning.

---

## What Special K does besides setPollingFrequency

From Special K’s `hid.cpp` and related behaviour:

1. **`setPollingFrequency(0)`**  
   Same idea we use: “irregular reads”, get latest report per read, no fixed poll interval.

2. **`setBufferCount(config.input.gamepad.hid.max_allowed_buffers)`**  
   They call **`IOCTL_SET_NUM_DEVICE_INPUT_BUFFERS`** (e.g. 2) so the HID driver doesn’t keep a long queue. Fewer buffered reports ⇒ less chance of returning an old report and less “catch-up” latency.

3. **Who does ReadFile**  
   In Special K, the **game** (or its input stack) opens the HID device and calls `ReadFile`; Special K’s **ReadFile detour** sees those reads and can cache/rewrite. So the game (or a driver) is doing the reads, possibly from a dedicated input thread or the same thread that calls XInput. They also record `config.input.gamepad.scepad.pollig_thread_tid` when a ReadFile happens on a Sony device — so they know which thread is doing the HID read. They don’t necessarily run a *separate* SK-owned polling thread; the latency win comes from driver tuning (poll frequency + buffer count) and from not adding an extra layer that does its own sync ReadFile on every GetState.

4. **No extra “DualSense → XInput” read in GetState**  
   If the game uses XInput and the controller is natively XInput, GetState just returns. If they translate HID to XInput, that translation likely uses state that was **already read** by the game’s (or system’s) HID read path, not a *new* ReadFile inside the GetState detour. So they avoid “ReadFile inside GetState” on the game thread.

So besides `setPollingFrequency`, the important differences are: **buffer count**, and **where/when** the HID read happens (not inside our GetState path).

---

## Recommendations (in order of impact)

1. **Background polling thread (largest gain)**  
   Re-enable and use a **dedicated thread** that:
   - Calls `UpdateDeviceStates()` in a loop at a fixed rate (e.g. 250–500 Hz or match device report rate).
   - Updates shared DualSense state (e.g. `g_dualsense_states`) under a lock or lock-free structure.
   - `ConvertDualSenseToXInput` / `XInputGetState_Detour` then **only read from that cached state** and do **not** call `UpdateDeviceStates()` or `ReadFile` on the game thread.  
   That removes HID I/O and parsing from the game’s input path and should noticeably reduce perceived latency.

2. **Add setBufferCount**  
   After opening the HID device, call **`IOCTL_SET_NUM_DEVICE_INPUT_BUFFERS`** with a small value (e.g. 2), same as Special K. Use the same pattern as `setPollingFrequency` (e.g. in `CreateHIDDevice`). This keeps the HID queue short so “latest” is fresher when we do read.

3. **Keep setPollingFrequency(0)**  
   Already done; ensures we’re in “irregular read / latest report” mode and not fighting a fixed poll interval.

4. **Optional: reduce work in GetState when using DualSense**  
   Ensure we don’t do heavy work (logging, UI updates, etc.) on every GetState when DualSense is active; keep the hot path to a simple read of the cached state and conversion to XInput.

---

## Summary

| Aspect                         | Our code                          | Special K (relevant parts)                |
|--------------------------------|-----------------------------------|-------------------------------------------|
| When we read HID               | On every XInputGetState (game thread) | Game/driver does ReadFile; not necessarily inside GetState |
| setPollingFrequency(0)        | Yes                               | Yes                                        |
| setBufferCount                 | No                                | Yes (e.g. 2)                               |
| Background polling thread      | Present but disabled (empty loop) | N/A (they don’t own the read path the same way) |
| ReadFile in GetState path      | Yes (main latency source)         | No (they don’t add a sync read there)      |

The main fix for user-reported latency is to **stop doing ReadFile inside the game’s XInputGetState path** and instead **serve from state that a dedicated polling thread keeps up to date**. Adding **setBufferCount** is a small, consistent improvement on top of that.
