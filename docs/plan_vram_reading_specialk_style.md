# Plan: VRAM Usage / Total VRAM Reading (SpecialK-Style)

## Summary of How SpecialK Reads VRAM

SpecialK exposes **VRAM used** and **VRAM total/budget** through a small GPU monitor API. The actual data comes from **two layers**: DXGI 1.4 when available, and an NVAPI fallback for “used” on NVIDIA when DXGI data is not available.

---

## 1. Public API (What Callers Use)

**Header:** `external-src/SpecialK/include/SpecialK/performance/gpu_monitor.h`

| Function | Returns | Meaning |
|----------|---------|--------|
| `SK_GPU_GetVRAMUsed(int gpu)` | `uint64_t` | Current video memory usage in **bytes** |
| `SK_GPU_GetVRAMBudget(int gpu)` | `uint64_t` | Memory budget (target / “total” for app) in **bytes** |
| `SK_GPU_GetVRAMCapacity(int gpu)` | `uint64_t` | Physical GPU memory capacity in **bytes** |
| `SK_GPU_GetVRAMShared(int gpu)` | `uint64_t` | Non-local (shared) usage in **bytes** |

- `gpu` is the adapter/node index (0 = first GPU).
- **Used** = current usage; **Budget** = OS-given budget (can be less than capacity); **Capacity** = physical VRAM size.

---

## 2. Data Sources (Implementation)

**File:** `external-src/SpecialK/src/performance/gpu_monitor.cpp` (lines ~1270–1328)

### 2.1 Primary: DXGI 1.4 `QueryVideoMemoryInfo`

- **API:** `IDXGIAdapter3::QueryVideoMemoryInfo(NodeIndex, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &DXGI_QUERY_VIDEO_MEMORY_INFO)`.
- **Structure:** `DXGI_QUERY_VIDEO_MEMORY_INFO` (from `dxgi1_4.h`):
  - `Budget` — OS budget in bytes (used as “total” for the app).
  - `CurrentUsage` — current usage in bytes.
  - `CurrentReservation` / `AvailableForReservation` — for residency hints.
- **Where it’s filled:** `external-src/SpecialK/src/render/dxgi/dxgi.cpp`:
  - Global: `mem_info_t dxgi_mem_info[NumBuffers]` (double-buffered).
  - **Budget thread** (`BudgetThread`) periodically calls:
    - `params->pAdapter->QueryVideoMemoryInfo(node, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &dxgi_mem_info[buffer].local[node])`
    - Same for `DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL` → `nonlocal[node]`.
  - `mem_info_t` holds:
    - `local[node]`, `nonlocal[node]` — per-node `DXGI_QUERY_VIDEO_MEMORY_INFO`.
    - `buffer` (Front/Back), `nodes` (count).
- **Logic in getters:**
  - If `dxgi_mem_info` has valid data for the GPU node (`nodes >= gpu`):
    - **VRAM used** = `dxgi_mem_info[buffer].local[gpu].CurrentUsage`
    - **VRAM budget** = `dxgi_mem_info[buffer].local[gpu].Budget`
  - Otherwise, fallback to sensor data (see below).

### 2.2 Fallback: GPU Sensor Data (`gpu_sensors_t` / `memory_B`)

- **Used when:** No DXGI 1.4 budget thread data (e.g. no `IDXGIAdapter3`, or thread not started).
- **Source:** `gpu_sensors_t` (e.g. `SK_GPU_CurrentSensorData()->gpus[gpu].memory_B`):
  - `memory_B.local` — used (local).
  - `memory_B.capacity` — total VRAM.
  - `memory_B.total` / `memory_B.nonlocal` — derived.
- **How `memory_B` is filled (NVIDIA only in the code path checked):**  
  `gpu_monitor.cpp` uses **NvAPI_GPU_GetMemoryInfoEx**:
  - `dedicatedVideoMemory` → capacity (×1024 → bytes).
  - `availableDedicatedVideoMemory` and `curAvailableDedicatedVideoMemory` → used:
    - `local = (availableDedicatedVideoMemory - curAvailableDedicatedVideoMemory) * 1024` (bytes).
  - So **VRAM used** = (available − current available) × 1024; **total** = dedicated × 1024.
- **Vendor-agnostic path:** SpecialK can also start a “budget thread” without an adapter by using **CreateDXGIFactory2** + **EnumAdapters(0)** to get adapter 0, then `StartBudgetThread(&adapter)`. That thread only uses DXGI 1.4; no NVAPI in that path.

---

## 3. Getting the Adapter (For DXGI 1.4)

- **With a device/swapchain:** From D3D11/D3D12 device get `IDXGIDevice` → `GetAdapter()` → `IDXGIAdapter*`. Then `QueryInterface(IID_PPV_ARGS(&IDXGIAdapter3))`.
- **Without a device:** `CreateDXGIFactory2(0, IID_PPV_ARGS(&factory))` → `factory->EnumAdapters(0, &adapter)` for first GPU. Then QI to `IDXGIAdapter3`.
- **Budget thread:** SpecialK starts a background thread that waits on events and periodically calls `QueryVideoMemoryInfo` for each node (local + non-local) and double-buffers the result. Readers use the “current” buffer so they don’t need locks.

---

## 4. Relevant SpecialK Files (Reference)

| Purpose | File(s) |
|--------|--------|
| VRAM getters | `src/performance/gpu_monitor.cpp` (SK_GPU_GetVRAMUsed/Budget/Capacity/Shared) |
| DXGI memory state + budget thread | `src/render/dxgi/dxgi.cpp` (dxgi_mem_info, BudgetThread, StartBudgetThread, StartBudgetThread_NoAdapter) |
| mem_info_t layout | `include/SpecialK/render/dxgi/dxgi_backend.h` (mem_info_t, buffer_t, DXGI_QUERY_VIDEO_MEMORY_INFO arrays) |
| Sensor fallback (NVAPI memory) | `src/performance/gpu_monitor.cpp` (NvAPI_GPU_GetMemoryInfoEx → memory_B) |
| Usage (e.g. overlay, warnings) | `src/widgets/gpu_widget.cpp`, `src/core.cpp` (VRAM gauge, over-quota toasts) |

---

## 5. Plan for Display Commander (ReShade Addon)

**Constraint:** We do **not** include SpecialK source; we only use it as a reference. So we need a small, self-contained way to get “VRAM used” and “VRAM total/budget” that matches SpecialK’s semantics.

### Option A: DXGI 1.4 only (recommended baseline)

- **Used:** `IDXGIAdapter3::QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &info)` → **VRAM used** = `info.CurrentUsage`, **VRAM total/budget** = `info.Budget`.
- **Adapter:** From ReShade’s runtime (device/swapchain) get `IDXGIDevice` → `GetAdapter()` → QI to `IDXGIAdapter3`. If ReShade doesn’t expose the adapter, get it once from the swapchain: `IDXGISwapChain::GetDevice(IDXGIDevice)` → `GetAdapter()` → `IDXGIAdapter3`.
- **When to call:** Either on demand when the UI needs it, or from a lightweight periodic job (no need for a full “budget thread” unless we want budget-change notifications). No double-buffering required for a simple “current value” display.
- **Multi-GPU:** Use `NodeIndex` 0 for primary; if we later need multiple adapters, we can enumerate and match by LUID or adapter index.

### Option B: Add NVAPI fallback for “used” / “total” on NVIDIA

- When `QueryVideoMemoryInfo` is unavailable or fails (e.g. no DXGI 1.4, or driver quirk):
  - **NVIDIA:** Use `NvAPI_GPU_GetMemoryInfoEx` to get used (from available − curAvailable, in KB → bytes) and capacity (dedicatedVideoMemory × 1024). We already use NVAPI elsewhere (e.g. reflex_manager, vrr_status).
  - **AMD/Intel:** Either leave “used” as 0 / “total” from adapter desc, or document as “DXGI 1.4 only” for accurate used/budget.

### Option C: Optional background refresh

- If we want a “live” gauge without calling DXGI on every frame, run a small timer or ReShade present callback that updates cached `CurrentUsage` and `Budget` every 500 ms–1 s (similar to SpecialK’s budget thread interval). Use atomics or a single producer/consumer so the UI reads the cached values.

### Implementation steps (concrete)

1. **Add a small VRAM helper module** (e.g. under `dxgi/` or `display/`):
   - `GetVramInfo(uint64_t* out_used_bytes, uint64_t* out_budget_bytes)` (and optionally `out_capacity_bytes`).
   - Inside: get current swapchain/device from ReShade (or our existing DXGI swapchain tracking); get `IDXGIAdapter3`; call `QueryVideoMemoryInfo(0, LOCAL, &info)`; set `*out_used_bytes = info.CurrentUsage`, `*out_budget_bytes = info.Budget`. Return success/failure.
2. **Integrate with existing DXGI/device access:** Use the same path we use for swapchain/device (e.g. from present hooks or ReShade runtime) so we don’t create new device/adapter references unnecessarily.
3. **UI:** Where we want to show “VRAM used / total” (e.g. device info tab or overlay), call this helper and display bytes in MiB/GiB (same as SpecialK’s `SK_File_SizeToStringF` usage).
4. **Optional:** Add NVAPI fallback in the same helper when DXGI 1.4 fails and NVAPI is available; then optionally add a slow refresh path (timer or present) and cache the result for the UI.
5. **Testing:** Run on DX11/DX12 games with ReShade; confirm values match SpecialK’s overlay or GPU widget when both are active (if possible), or match Task Manager / GPU-Z for sanity.

---

## 6. API Summary (What We Need from the OS/SDK)

| Source | Used | Total/Budget | Notes |
|--------|------|--------------|--------|
| **IDXGIAdapter3::QueryVideoMemoryInfo** (DXGI 1.4) | `CurrentUsage` | `Budget` | Preferred; works for all vendors on Windows 10+ with DXGI 1.4. |
| **NvAPI_GPU_GetMemoryInfoEx** (NVIDIA) | `(available - curAvailable) * 1024` | `dedicatedVideoMemory * 1024` | Fallback when DXGI path fails; NVAPI already used in project. |
| **DXGI_ADAPTER_DESC.DedicatedVideoMemory** | — | Bytes (SIZE_T) | Only “total” capacity; no “used”; use only if no better source. |

This plan gives a clear path to implement VRAM used/total in Display Commander in line with SpecialK’s behavior, without depending on SpecialK code.
