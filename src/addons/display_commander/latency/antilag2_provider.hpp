#pragma once

#include "latency_manager.hpp"
#include <d3d11.h>
#include <d3d12.h>
#include "../../../external/AntiLag2-SDK/ffx_antilag2_dx11.h"
#include "../../../external/AntiLag2-SDK/ffx_antilag2_dx12.h"

// AMD Anti-Lag 2 implementation of ILatencyProvider
class AntiLag2Provider : public ILatencyProvider {
public:
    AntiLag2Provider();
    ~AntiLag2Provider() override;

    // ILatencyProvider interface
    bool Initialize(reshade::api::device* device) override;
    void Shutdown() override;
    bool IsInitialized() const override;

    uint64_t IncreaseFrameId() override;
    bool SetMarker(LatencyMarkerType marker) override;
    bool ApplySleepMode(bool low_latency, bool boost, bool use_markers) override;
    bool Sleep() override;
    bool Update() override;
    void SetTargetFPS(float fps) override;

    LatencyTechnology GetTechnology() const override { return LatencyTechnology::AMD_AntiLag2; }
    const char* GetTechnologyName() const override { return "AMD Anti-Lag 2"; }

private:
    // Helper to get native D3D device from ReShade device
    IUnknown* GetNativeD3DDeviceFromReshade(reshade::api::device* device);

    // Determine which API to use based on device
    bool IsD3D12Device(reshade::api::device* device);

    // D3D11 specific members
    AMD::AntiLag2DX11::Context dx11_context_;
    bool dx11_initialized_;

    // D3D12 specific members
    AMD::AntiLag2DX12::Context dx12_context_;
    bool dx12_initialized_;

    // Common state
    std::atomic<bool> initialized_;
    std::atomic<uint64_t> frame_id_;
    bool low_latency_mode_;
    bool boost_mode_;
    bool use_markers_;
    float target_fps_;

    // Current API being used
    enum class APIType {
        None,
        D3D11,
        D3D12
    } current_api_;
};
