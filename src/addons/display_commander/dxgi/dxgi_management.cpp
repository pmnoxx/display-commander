#include "../addon.hpp"
#include "../utils/logging.hpp"
#include "../utils/perf_measurement.hpp"
#include "../autoclick/autoclick_manager.hpp"
#include "../settings/main_tab_settings.hpp"
#include "../ui/new_ui/new_ui_tabs.hpp"

// Forward declaration for Streamline's base interface retrieval
// GUID: ADEC44E2-61F0-45C3-AD9F-1B37379284FF
struct DECLSPEC_UUID("ADEC44E2-61F0-45C3-AD9F-1B37379284FF") StreamlineRetrieveBaseInterface : IUnknown {};

DxgiBypassMode GetIndependentFlipState(IDXGISwapChain *dxgi_swapchain) {
    perf_measurement::ScopedTimer perf_timer(perf_measurement::Metric::GetIndependentFlipState);

    if (perf_measurement::IsSuppressionEnabled() &&
        perf_measurement::IsMetricSuppressed(perf_measurement::Metric::GetIndependentFlipState)) {
        return DxgiBypassMode::kUnset;
    }

    if (dxgi_swapchain == nullptr) {
        LogDebug("DXGI IF state: swapchain is null");
        return DxgiBypassMode::kQueryFailedSwapchainNull;
    }
    // Only query GetFrameStatisticsMedia when UI is open (main tab) to avoid performance overhead
    const bool overlay_open = autoclick::g_ui_overlay_open.load();
    const bool ui_enabled = settings::g_mainTabSettings.show_display_commander_ui.GetValue();
    const bool main_tab_active = (ui::new_ui::g_tab_manager.GetActiveTab() == 0);

 //   if (!overlay_open || !ui_enabled || !main_tab_active) {
        // UI is not open or main tab is not active, skip expensive query
   //     return DxgiBypassMode::kUnset;
 //   }

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
                LogDebug("DXGI IF state: QI IDXGISwapChainMedia failed hr=0x%x, attempting Streamline base interface fallback", hr);
                log_count++;
            }

            // Fallback: Try to retrieve base interface if this is a Streamline proxy
            // StreamlineRetrieveBaseInterface returns m_base which is IDXGISwapChain* directly
            Microsoft::WRL::ComPtr<IDXGISwapChain> base_swapchain;
            Microsoft::WRL::ComPtr<IUnknown> base_unknown;
            if (SUCCEEDED(dxgi_swapchain->QueryInterface(__uuidof(StreamlineRetrieveBaseInterface), reinterpret_cast<void**>(base_unknown.GetAddressOf())))) {
                // StreamlineRetrieveBaseInterface returns the base IDXGISwapChain* directly
                // Query it as IDXGISwapChain (it already is, but we need to get the ComPtr)
                if (SUCCEEDED(base_unknown->QueryInterface(IID_PPV_ARGS(&base_swapchain)))) {
                    // Query IDXGISwapChain1 from base swapchain
                    Microsoft::WRL::ComPtr<IDXGISwapChain1> base_sc1;
                    if (SUCCEEDED(base_swapchain->QueryInterface(IID_PPV_ARGS(&base_sc1)))) {
                        // Retry IDXGISwapChainMedia query on base interface
                        hr = base_sc1->QueryInterface(IID_PPV_ARGS(&media));
                        if (SUCCEEDED(hr) && media != nullptr) {
                            if (log_count < 10) {
                                LogDebug("DXGI IF state: Successfully retrieved IDXGISwapChainMedia from Streamline base interface");
                                log_count++;
                            }
                            // Continue with media interface successfully obtained
                        } else {
                            if (log_count < 10) {
                                LogDebug("DXGI IF state: QI IDXGISwapChainMedia on base interface also failed hr=0x%x", hr);
                                log_count++;
                            }
                            return DxgiBypassMode::kQueryFailedNoMedia;
                        }
                    } else {
                        if (log_count < 10) {
                            LogDebug("DXGI IF state: Failed to query IDXGISwapChain1 from base interface");
                            log_count++;
                        }
                        return DxgiBypassMode::kQueryFailedNoMedia;
                    }
                } else {
                    if (log_count < 10) {
                        LogDebug("DXGI IF state: Failed to query IDXGISwapChain from Streamline base interface");
                        log_count++;
                    }
                    return DxgiBypassMode::kQueryFailedNoMedia;
                }
            } else {
                // Not a Streamline proxy, or base interface retrieval failed
                return DxgiBypassMode::kQueryFailedNoMedia;
            }
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
