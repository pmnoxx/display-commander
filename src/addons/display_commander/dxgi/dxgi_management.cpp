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
