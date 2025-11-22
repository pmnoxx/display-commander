#include "../addon.hpp"
#include "../utils/logging.hpp"

DxgiBypassMode GetIndependentFlipState(IDXGISwapChain *dxgi_swapchain) {
    if (dxgi_swapchain == nullptr) {
        LogDebug("DXGI IF state: swapchain is null");
        return DxgiBypassMode::kQueryFailedSwapchainNull;
    }

    // Per DXGI guidance, query for IDXGISwapChain1 first, then obtain IDXGISwapChainMedia
    Microsoft::WRL::ComPtr<IDXGISwapChain1> sc1;
    {
        HRESULT hr = dxgi_swapchain->QueryInterface(IID_PPV_ARGS(&sc1));
        if (FAILED(hr) || sc1 == nullptr) {
            // up to 3 times
            static int log_count = 0;
            // Log swap effect for diagnostics
            DXGI_SWAP_CHAIN_DESC scd{};
            if (SUCCEEDED(dxgi_swapchain->GetDesc(&scd))) {
                if (log_count < 3) {
                    LogDebug("DXGI IF state: SwapEffect=%d", static_cast<int>(scd.SwapEffect));
                }
            }
            if (log_count < 3) {
                LogDebug("DXGI IF state: QI IDXGISwapChain1 failed hr=0x%x", hr);
                log_count++;
            }
            return DxgiBypassMode::kQueryFailedNoSwapchain1;
        }
    }

    Microsoft::WRL::ComPtr<IDXGISwapChainMedia> media;
    {
        HRESULT hr = sc1->QueryInterface(IID_PPV_ARGS(&media));
        if (FAILED(hr) || media == nullptr) {
            // log up to 10 times
            static int log_count = 0;
            // Log swap effect for diagnostics
            DXGI_SWAP_CHAIN_DESC scd{};
            if (SUCCEEDED(dxgi_swapchain->GetDesc(&scd))) {
                if (log_count < 10) {
                    LogDebug("DXGI IF state: SwapEffect=%d", static_cast<int>(scd.SwapEffect));
                }
            }
            if (log_count < 10) {
                LogDebug("DXGI IF state: QI IDXGISwapChainMedia failed hr=0x%x", hr);
                log_count++;
            }
            return DxgiBypassMode::kQueryFailedNoMedia;
        }
    }

    DXGI_FRAME_STATISTICS_MEDIA stats = {};
    {
        HRESULT hr = media->GetFrameStatisticsMedia(&stats);
        if (FAILED(hr)) {
            // up to 3 times
            static int log_count = 0;
            DXGI_SWAP_CHAIN_DESC scd{};
            if (SUCCEEDED(dxgi_swapchain->GetDesc(&scd))) {
                if (log_count < 3) {
                    LogDebug("DXGI IF state: SwapEffect=%d", static_cast<int>(scd.SwapEffect));
                }
            }
            if (log_count < 3) {
                LogDebug("DXGI IF state: GetFrameStatisticsMedia failed hr=0x%x (call after at least one Present)", hr);
                log_count++;
            }
            return DxgiBypassMode::kQueryFailedNoStats; // Call after at least one Present
        }
    }

    switch (stats.CompositionMode) {
    case DXGI_FRAME_PRESENTATION_MODE_COMPOSED:
        return DxgiBypassMode::kComposed;
    case DXGI_FRAME_PRESENTATION_MODE_OVERLAY:
        return DxgiBypassMode::kOverlay;
    case DXGI_FRAME_PRESENTATION_MODE_NONE:
        return DxgiBypassMode::kIndependentFlip;
    default:
        return DxgiBypassMode::kUnknown;
    }
}

const char *DxgiBypassModeToString(DxgiBypassMode mode) {
    switch (mode) {
    case DxgiBypassMode::kUnset:
        return "Unset";
    case DxgiBypassMode::kComposed:
        return "Composed";
    case DxgiBypassMode::kOverlay:
        return "Hardware Overlay (MPO)";
    case DxgiBypassMode::kIndependentFlip:
        return "Independent Flip";
    case DxgiBypassMode::kQueryFailedSwapchainNull:
        return "Query Failed: Swapchain Null";
    case DxgiBypassMode::kQueryFailedNoSwapchain1:
        return "Query Failed: No Swapchain1";
    case DxgiBypassMode::kQueryFailedNoMedia:
        return "Query Failed: No Media Interface";
    case DxgiBypassMode::kQueryFailedNoStats:
        return "Query Failed: No Statistics";
    case DxgiBypassMode::kUnknown:
    default:
        return "Unknown";
    }
}

// Attempt to configure the DXGI swapchain for conditions that allow Independent Flip
// Only uses DXGI APIs (no Win32 window sizing). Returns true if a change was applied.
bool SetIndependentFlipState(reshade::api::swapchain *swapchain) {
    if (swapchain == nullptr) {
        LogWarn("SetIndependentFlipState: null swapchain");
        return false;
    }
    auto *device = swapchain->get_device();
    if (device == nullptr) {
        LogWarn("SetIndependentFlipState: null device");
        return false;
    }
    const auto api = device->get_api();
    if (api != reshade::api::device_api::d3d10 && api != reshade::api::device_api::d3d11 &&
        api != reshade::api::device_api::d3d12) {
        LogWarn("SetIndependentFlipState: non-DXGI backend");
        return false;
    }

    IDXGISwapChain *sc = reinterpret_cast<IDXGISwapChain *>(swapchain->get_native());
    if (sc == nullptr) {
        LogWarn("SetIndependentFlipState: native swapchain is null");
        return false;
    }

    Microsoft::WRL::ComPtr<IDXGISwapChain4> sc4;
    if (FAILED(sc->QueryInterface(IID_PPV_ARGS(&sc4)))) {
        LogWarn("SetIndependentFlipState: IDXGISwapChain4 not available");
        return false;
    }

    DXGI_SWAP_CHAIN_DESC1 desc1{};
    if (FAILED(sc4->GetDesc1(&desc1))) {
        LogWarn("SetIndependentFlipState: GetDesc1 failed");
        return false;
    }

    // Must be flip-model for Independent Flip
    if (desc1.SwapEffect != DXGI_SWAP_EFFECT_FLIP_DISCARD && desc1.SwapEffect != DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL) {
        LogWarn("SetIndependentFlipState: not a flip-model swapchain (cannot change SwapEffect without recreation)");
        return false;
    }

    // Align backbuffer size to the containing output's resolution
    Microsoft::WRL::ComPtr<IDXGIOutput> output;
    if (FAILED(sc4->GetContainingOutput(&output)) || output == nullptr) {
        LogWarn("SetIndependentFlipState: GetContainingOutput failed");
        return false;
    }

    Microsoft::WRL::ComPtr<IDXGIOutput6> output6;
    if (FAILED(output->QueryInterface(IID_PPV_ARGS(&output6))) || output6 == nullptr) {
        LogWarn("SetIndependentFlipState: IDXGIOutput6 not available");
        return false;
    }

    DXGI_OUTPUT_DESC1 out_desc{};
    if (FAILED(output6->GetDesc1(&out_desc))) {
        LogWarn("SetIndependentFlipState: GetDesc1 on output failed");
        return false;
    }

    const UINT target_width = static_cast<UINT>(out_desc.DesktopCoordinates.right - out_desc.DesktopCoordinates.left);
    const UINT target_height = static_cast<UINT>(out_desc.DesktopCoordinates.bottom - out_desc.DesktopCoordinates.top);

    bool changed = false;
    if (desc1.Width != target_width || desc1.Height != target_height) {
        LogInfo("SetIndependentFlipState: Resizing buffers to match output resolution");
        const HRESULT hr = sc4->ResizeBuffers(0, // keep buffer count
                                              target_width, target_height, desc1.Format, desc1.Flags);
        if (FAILED(hr)) {
            LogWarn("SetIndependentFlipState: ResizeBuffers failed hr=0x%x", hr);
            return false;
        }
        changed = true;
    }

    // Optional: Hint tearing allowed when supported (does not enforce IF, but can help latency)
    // This keeps flags as-is to avoid impacting app behavior.

    if (changed) {
        LogInfo("SetIndependentFlipState: Applied DXGI-only changes. Present a few frames to allow promotion.");
    } else {
        LogInfo("SetIndependentFlipState: Already matching output resolution; waiting for promotion conditions.");
    }
    return changed;
}
