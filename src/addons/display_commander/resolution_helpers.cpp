#include "resolution_helpers.hpp"
#include <d3d11.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <cmath>
#include <cwchar>  // for wcslen
#include "globals.hpp"
#include "utils/logging.hpp"

#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3d11.lib")

using Microsoft::WRL::ComPtr;

// TODO: Remove this file and use display_cache.hpp instead

namespace resolution {

// Helper function to apply display settings using DXGI API with fractional refresh rates
bool ApplyDisplaySettingsDXGI(int monitor_index, int width, int height, UINT32 refresh_numerator,
                              UINT32 refresh_denominator) {
    // Get monitor handle
    std::vector<HMONITOR> monitors;
    EnumDisplayMonitors(
        nullptr, nullptr,
        [](HMONITOR hmon, HDC, LPRECT, LPARAM lparam) -> BOOL {
            auto* monitors_ptr = reinterpret_cast<std::vector<HMONITOR>*>(lparam);
            monitors_ptr->push_back(hmon);
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&monitors));

    if (monitor_index < 0 || monitor_index >= static_cast<int>(monitors.size())) {
        return false;
    }

    HMONITOR hmon = monitors[monitor_index];

    // Create a temporary DXGI factory and device to access the output
    ComPtr<IDXGIFactory1> factory = GetSharedDXGIFactory();
    if (!factory) {
        return false;
    }

    // Find the adapter and output for our monitor
    for (UINT a = 0;; ++a) {
        ComPtr<IDXGIAdapter1> adapter;
        if (factory->EnumAdapters1(a, &adapter) == DXGI_ERROR_NOT_FOUND) break;

        for (UINT o = 0;; ++o) {
            ComPtr<IDXGIOutput> output;
            if (adapter->EnumOutputs(o, &output) == DXGI_ERROR_NOT_FOUND) break;

            DXGI_OUTPUT_DESC desc{};
            if (FAILED(output->GetDesc(&desc))) continue;
            if (desc.Monitor != hmon) continue;

            // Found our monitor's output! Now try to set the mode
            // We need to create a temporary swap chain to use SetFullscreenState

            // Create a temporary D3D11 device and swap chain
            ComPtr<ID3D11Device> device;
            ComPtr<ID3D11DeviceContext> context;
            ComPtr<IDXGISwapChain> swap_chain;

            D3D_FEATURE_LEVEL feature_levels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0};
            D3D_FEATURE_LEVEL feature_level;

            DXGI_SWAP_CHAIN_DESC swap_chain_desc = {};
            swap_chain_desc.BufferCount = 1;
            swap_chain_desc.BufferDesc.Width = width;
            swap_chain_desc.BufferDesc.Height = height;
            swap_chain_desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            swap_chain_desc.BufferDesc.RefreshRate.Numerator = refresh_numerator;
            swap_chain_desc.BufferDesc.RefreshRate.Denominator = refresh_denominator;
            swap_chain_desc.BufferDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
            swap_chain_desc.BufferDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;
            swap_chain_desc.SampleDesc.Count = 1;
            swap_chain_desc.SampleDesc.Quality = 0;
            swap_chain_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
            swap_chain_desc.OutputWindow = nullptr;  // We don't need a window for this
            swap_chain_desc.Windowed = FALSE;        // Start in fullscreen
            swap_chain_desc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

            HRESULT hr = D3D11CreateDeviceAndSwapChain(
                adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0, feature_levels, ARRAYSIZE(feature_levels),
                D3D11_SDK_VERSION, &swap_chain_desc, &swap_chain, &device, &feature_level, &context);

            if (SUCCEEDED(hr) && swap_chain) {
                // Set the output as the target for fullscreen
                hr = swap_chain->SetFullscreenState(TRUE, output.Get());
                if (SUCCEEDED(hr)) {
                    // Now try to resize the target to our desired mode
                    DXGI_MODE_DESC mode_desc = {};
                    mode_desc.Width = width;
                    mode_desc.Height = height;
                    mode_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
                    mode_desc.RefreshRate.Numerator = refresh_numerator;
                    mode_desc.RefreshRate.Denominator = refresh_denominator;
                    mode_desc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
                    mode_desc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;

                    LogInfo("[ApplyDisplaySettingsDXGI] mode_desc: %d %d %d %d %d %d", mode_desc.Width,
                            mode_desc.Height, mode_desc.Format, mode_desc.RefreshRate.Numerator,
                            mode_desc.RefreshRate.Denominator, mode_desc.ScanlineOrdering, mode_desc.Scaling);

                    hr = swap_chain->ResizeTarget(&mode_desc);
                    if (SUCCEEDED(hr)) {
                        // Success! Now exit fullscreen to return to normal desktop
                        swap_chain->SetFullscreenState(FALSE, nullptr);
                        return true;
                    }
                }

                // Clean up: exit fullscreen if we entered it
                swap_chain->SetFullscreenState(FALSE, nullptr);
            }

            // If we get here, this output didn't work, try the next one
            break;
        }
        break;  // Only try the first adapter
    }

    return false;
}

}  // namespace resolution
