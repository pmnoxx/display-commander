#pragma once

#include <atomic>
// Include D3D headers before NVAPI to enable Reflex functions
#include <d3d11.h>
#include <d3d12.h>
#include <nvapi.h>
#include <reshade.hpp>

// Minimal NVIDIA Reflex manager (D3D11/D3D12 only) using NVAPI.
// Avoids re-declaring any NVAPI structs; uses official headers.
// Initialized lazily on first use when enabled.

class ReflexManager {
public:
    ReflexManager() = default;
    ~ReflexManager() = default;

    // Initialize with a ReShade device to obtain underlying D3D device pointer.
    bool Initialize(reshade::api::device* device);
    void Shutdown();

    // Configure Reflex sleep mode (Low Latency + Boost + markers optimization).
    bool ApplySleepMode(bool low_latency, bool boost, bool use_markers);

    NvU64 IncreaseFrameId();

    // Submit a latency marker.
    bool SetMarker(NV_LATENCY_MARKER_TYPE marker);

    // Optional: driver sleep call (minimal usage)
    bool Sleep();

    bool IsInitialized() const { return initialized_; }

private:
    bool EnsureNvApi();

private:
    std::atomic<bool> initialized_{false};
    IUnknown* d3d_device_ = nullptr; // not owned
    std::atomic<NvU64> frame_id_{0};
};


