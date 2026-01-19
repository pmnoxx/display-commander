#pragma once

#include <atomic>
// Include D3D headers before NVAPI to enable Reflex functions
#include <d3d11.h>
#include <d3d12.h>
#include <nvapi.h>
#include <reshade.hpp>

#include "../globals.hpp"
// Minimal NVIDIA Reflex manager (D3D11/D3D12 only) using NVAPI.
// Avoids re-declaring any NVAPI structs; uses official headers.
// Initialized lazily on first use when enabled.

class ReflexManager {
  public:
    ReflexManager() = default;
    ~ReflexManager() = default;

    // Initialize with a ReShade device to obtain underlying D3D device pointer.
    bool Initialize(reshade::api::device *device);

    // Initialize with native device directly
    bool InitializeNative(void* native_device, DeviceTypeDC device_type);

    void Shutdown();

    // Configure Reflex sleep mode (Low Latency + Boost + markers optimization).
    bool ApplySleepMode(bool low_latency, bool boost, bool use_markers, float fps_limit);

    NvU64 IncreaseFrameId();

    // Submit a latency marker.
    bool SetMarker(NV_LATENCY_MARKER_TYPE marker);

    // Optional: driver sleep call (minimal usage)
    bool Sleep();

    // Get current sleep status (low latency mode, VRR, etc.)
    bool GetSleepStatus(NV_GET_SLEEP_STATUS_PARAMS* status_params);

    // Restore sleep mode to default settings
    static void RestoreSleepMode(IUnknown *d3d_device_, NV_SET_SLEEP_MODE_PARAMS *params);

    bool IsInitialized() const { return initialized_; }

  private:
    bool EnsureNvApi();

  private:
    std::atomic<bool> initialized_{false};
    IUnknown *d3d_device_ = nullptr; // not owned
};
