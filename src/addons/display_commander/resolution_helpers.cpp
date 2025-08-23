#include "resolution_helpers.hpp"
#include <dxgi1_6.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <iomanip>
#include <cmath>
#include <cwchar> // for wcslen

using Microsoft::WRL::ComPtr;

namespace renodx::resolution {

// Helper function to get available resolutions for a monitor
std::vector<std::string> GetResolutionLabels(int monitor_index) {
    std::vector<std::string> labels;
    
    // Get monitor handle
    std::vector<HMONITOR> monitors;
    EnumDisplayMonitors(nullptr, nullptr, 
        [](HMONITOR hmon, HDC, LPRECT, LPARAM lparam) -> BOOL {
            auto* monitors_ptr = reinterpret_cast<std::vector<HMONITOR>*>(lparam);
            monitors_ptr->push_back(hmon);
            return TRUE;
        }, 
        reinterpret_cast<LPARAM>(&monitors));
    
    if (monitor_index >= 0 && monitor_index < static_cast<int>(monitors.size())) {
        HMONITOR hmon = monitors[monitor_index];
        
        MONITORINFOEXW mi;
        mi.cbSize = sizeof(mi);
        if (GetMonitorInfoW(hmon, &mi)) {
            std::wstring device_name = mi.szDevice;
            
            // Enumerate all display modes
            DEVMODEW dm;
            dm.dmSize = sizeof(dm);
            
            std::set<std::pair<int, int>> resolution_set; // Use set to avoid duplicates and maintain order
            
            for (int i = 0; EnumDisplaySettingsW(device_name.c_str(), i, &dm); i++) {
                // Only add valid resolutions (width > 0, height > 0)
                if (dm.dmPelsWidth > 0 && dm.dmPelsHeight > 0) {
                    resolution_set.insert({dm.dmPelsWidth, dm.dmPelsHeight});
                }
            }
            
            // Convert to sorted list
            for (const auto& pair : resolution_set) {
                std::ostringstream oss;
                oss << pair.first << " x " << pair.second;
                labels.push_back(oss.str());
            }
            
            // Sort by width (ascending - lowest first, regular order)
            std::sort(labels.begin(), labels.end(), [](const std::string& a, const std::string& b) {
                int width_a, height_a, width_b, height_b;
                    sscanf_s(a.c_str(), "%d x %d", &width_a, &height_a);
    sscanf_s(b.c_str(), "%d x %d", &width_b, &height_b);
                return width_a < width_b; // Changed back to < for ascending order
            });
        }
    }
    
    return labels;
}

// Helper function to get available refresh rates for a monitor and resolution
std::vector<std::string> GetRefreshRateLabels(int monitor_index, int width, int height) {
    std::vector<std::string> labels;
    
    // Get monitor handle
    std::vector<HMONITOR> monitors;
    EnumDisplayMonitors(nullptr, nullptr, 
        [](HMONITOR hmon, HDC, LPRECT, LPARAM lparam) -> BOOL {
            auto* monitors_ptr = reinterpret_cast<std::vector<HMONITOR>*>(lparam);
            monitors_ptr->push_back(hmon);
            return TRUE;
        }, 
        reinterpret_cast<LPARAM>(&monitors));
    
    if (monitor_index >= 0 && monitor_index < static_cast<int>(monitors.size())) {
        HMONITOR hmon = monitors[monitor_index];

        // Try DXGI first for high precision refresh rates
        bool used_dxgi = false;
        ComPtr<IDXGIFactory1> factory;
        if (SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))) && factory) {
            for (UINT a = 0; ; ++a) {
                ComPtr<IDXGIAdapter1> adapter;
                if (factory->EnumAdapters1(a, &adapter) == DXGI_ERROR_NOT_FOUND) break;
                for (UINT o = 0; ; ++o) {
                    ComPtr<IDXGIOutput> output;
                    if (adapter->EnumOutputs(o, &output) == DXGI_ERROR_NOT_FOUND) break;
                    DXGI_OUTPUT_DESC desc{};
                    if (FAILED(output->GetDesc(&desc))) continue;
                    if (desc.Monitor != hmon) continue;

                    ComPtr<IDXGIOutput1> output1;
                    if (FAILED(output.As(&output1)) || !output1) continue;

                    UINT num_modes = 0;
                    if (FAILED(output1->GetDisplayModeList1(
                            DXGI_FORMAT_R8G8B8A8_UNORM,
                            0,
                            &num_modes,
                            nullptr))) {
                        continue;
                    }
                    std::vector<DXGI_MODE_DESC1> modes(num_modes);
                    if (FAILED(output1->GetDisplayModeList1(
                            DXGI_FORMAT_R8G8B8A8_UNORM,
                            0,
                            &num_modes,
                            modes.data()))) {
                        continue;
                    }

                    std::vector<double> rates;
                    // Store rational refresh rate data for later use
                    static std::map<std::string, std::pair<UINT32, UINT32>> rational_rates;
                    rational_rates.clear();
                    
                    for (const auto& m : modes) {
                        if (static_cast<int>(m.Width) == width && static_cast<int>(m.Height) == height) {
                            if (m.RefreshRate.Denominator != 0) {
                                double hz = static_cast<double>(m.RefreshRate.Numerator) / static_cast<double>(m.RefreshRate.Denominator);
                                // Deduplicate with epsilon (e.g., 59.94 vs 59.9401)
                                bool exists = false;
                                for (double r : rates) {
                                    if (std::fabs(r - hz) < 0.001) { exists = true; break; }
                                }
                                if (!exists) {
                                    rates.push_back(hz);
                                    // Store the rational values
                                    std::ostringstream oss;
                                    oss << std::setprecision(10) << hz;
                                    std::string rate_str = oss.str();
                                    // Remove trailing zeros after decimal point
                                    size_t decimal_pos = rate_str.find('.');
                                    if (decimal_pos != std::string::npos) {
                                        size_t last_nonzero = rate_str.find_last_not_of('0');
                                        if (last_nonzero == decimal_pos) {
                                            // All zeros after decimal, remove decimal point too
                                            rate_str = rate_str.substr(0, decimal_pos);
                                        } else if (last_nonzero > decimal_pos) {
                                            // Remove trailing zeros but keep some precision
                                            rate_str = rate_str.substr(0, last_nonzero + 1);
                                        }
                                    }
                                    std::string key = rate_str + "Hz";
                                    rational_rates[key] = {m.RefreshRate.Numerator, m.RefreshRate.Denominator};
                                }
                            }
                        }
                    }

                    std::sort(rates.begin(), rates.end()); // ascending
                    for (double r : rates) {
                        std::ostringstream oss;
                        oss << std::setprecision(10) << r;
                        std::string rate_str = oss.str();
                        // Remove trailing zeros after decimal point
                        size_t decimal_pos = rate_str.find('.');
                        if (decimal_pos != std::string::npos) {
                            size_t last_nonzero = rate_str.find_last_not_of('0');
                            if (last_nonzero == decimal_pos) {
                                // All zeros after decimal, remove decimal point too
                                rate_str = rate_str.substr(0, decimal_pos);
                            } else if (last_nonzero > decimal_pos) {
                                // Remove trailing zeros but keep some precision
                                rate_str = rate_str.substr(0, last_nonzero + 1);
                            }
                        }
                        std::string key = rate_str + "Hz";
                        labels.push_back(key);
                    }

                    used_dxgi = true;
                    break; // matched output found
                }
                if (used_dxgi) break;
            }
        }

        // Fallback to EnumDisplaySettings if DXGI path failed
        if (!used_dxgi) {
            MONITORINFOEXW mi;
            mi.cbSize = sizeof(mi);
            if (GetMonitorInfoW(hmon, &mi)) {
                std::wstring device_name = mi.szDevice;
                DEVMODEW dm;
                dm.dmSize = sizeof(dm);
                for (int i = 0; EnumDisplaySettingsW(device_name.c_str(), i, &dm); i++) {
                    if (dm.dmPelsWidth == width && dm.dmPelsHeight == height) {
                        std::ostringstream oss;
                        // Note: dmDisplayFrequency is integer; present it as xx Hz (no trailing zeros)
                        oss << std::setprecision(10) << static_cast<double>(dm.dmDisplayFrequency);
                        std::string rate_str = oss.str();
                        // Remove trailing zeros after decimal point
                        size_t decimal_pos = rate_str.find('.');
                        if (decimal_pos != std::string::npos) {
                            size_t last_nonzero = rate_str.find_last_not_of('0');
                            if (last_nonzero == decimal_pos) {
                                // All zeros after decimal, remove decimal point too
                                rate_str = rate_str.substr(0, decimal_pos);
                            } else if (last_nonzero > decimal_pos) {
                                // Remove trailing zeros but keep some precision
                                rate_str = rate_str.substr(0, last_nonzero + 1);
                            }
                        }
                        std::string refresh_rate = rate_str + "Hz";
                        bool found = false;
                        for (const auto& existing : labels) {
                            if (existing == refresh_rate) { found = true; break; }
                        }
                        if (!found) labels.push_back(refresh_rate);
                    }
                }

                std::sort(labels.begin(), labels.end(), [](const std::string& a, const std::string& b) {
                    float freq_a = 0.f, freq_b = 0.f;
                        sscanf_s(a.c_str(), "%fHz", &freq_a);
    sscanf_s(b.c_str(), "%fHz", &freq_b);
                    return freq_a < freq_b;
                });
            }
        }
    }
    
    return labels;
}

// Helper function to get selected resolution
bool GetSelectedResolution(int monitor_index, int resolution_index, int& out_width, int& out_height) {
    auto labels = GetResolutionLabels(monitor_index);
    if (resolution_index >= 0 && resolution_index < static_cast<int>(labels.size())) {
        std::string selected_resolution = labels[resolution_index];
        if (sscanf_s(selected_resolution.c_str(), "%d x %d", &out_width, &out_height) == 2) {
            return true;
        }
    }
    return false;
}

// Helper function to get selected refresh rate
bool GetSelectedRefreshRate(int monitor_index, int width, int height, int refresh_rate_index, float& out_refresh_rate) {
    auto labels = GetRefreshRateLabels(monitor_index, width, height);
    if (refresh_rate_index >= 0 && refresh_rate_index < static_cast<int>(labels.size())) {
        std::string selected_refresh_rate = labels[refresh_rate_index];
        if (sscanf_s(selected_refresh_rate.c_str(), "%fHz", &out_refresh_rate) == 1) {
            return true;
        }
    }
    return false;
}

// Helper function to get selected refresh rate as rational values
bool GetSelectedRefreshRateRational(int monitor_index, int width, int height, int refresh_rate_index, 
                                   UINT32& out_numerator, UINT32& out_denominator) {
    // Get monitor handle
    std::vector<HMONITOR> monitors;
    EnumDisplayMonitors(nullptr, nullptr, 
        [](HMONITOR hmon, HDC, LPRECT, LPARAM lparam) -> BOOL {
            auto* monitors_ptr = reinterpret_cast<std::vector<HMONITOR>*>(lparam);
            monitors_ptr->push_back(hmon);
            return TRUE;
        }, 
        reinterpret_cast<LPARAM>(&monitors));
    
    if (monitor_index >= 0 && monitor_index < static_cast<int>(monitors.size())) {
        HMONITOR hmon = monitors[monitor_index];

        // Try DXGI first for high precision refresh rates
        ComPtr<IDXGIFactory1> factory;
        if (SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))) && factory) {
            for (UINT a = 0; ; ++a) {
                ComPtr<IDXGIAdapter1> adapter;
                if (factory->EnumAdapters1(a, &adapter) == DXGI_ERROR_NOT_FOUND) break;
                for (UINT o = 0; ; ++o) {
                    ComPtr<IDXGIOutput> output;
                    if (adapter->EnumOutputs(o, &output) == DXGI_ERROR_NOT_FOUND) break;
                    DXGI_OUTPUT_DESC desc{};
                    if (FAILED(output->GetDesc(&desc))) continue;
                    if (desc.Monitor != hmon) continue;

                    ComPtr<IDXGIOutput1> output1;
                    if (FAILED(output.As(&output1)) || !output1) continue;

                    UINT num_modes = 0;
                    if (FAILED(output1->GetDisplayModeList1(
                            DXGI_FORMAT_R8G8B8A8_UNORM,
                            0,
                            &num_modes,
                            nullptr))) {
                        continue;
                    }
                    std::vector<DXGI_MODE_DESC1> modes(num_modes);
                    if (FAILED(output1->GetDisplayModeList1(
                            DXGI_FORMAT_R8G8B8A8_UNORM,
                            0,
                            &num_modes,
                            modes.data()))) {
                        continue;
                    }

                    std::vector<double> rates;
                    std::vector<std::pair<UINT32, UINT32>> rational_rates;
                    
                    for (const auto& m : modes) {
                        if (static_cast<int>(m.Width) == width && static_cast<int>(m.Height) == height) {
                            if (m.RefreshRate.Denominator != 0) {
                                double hz = static_cast<double>(m.RefreshRate.Numerator) / static_cast<double>(m.RefreshRate.Denominator);
                                // Deduplicate with epsilon (e.g., 59.94 vs 59.9401)
                                bool exists = false;
                                for (double r : rates) {
                                    if (std::fabs(r - hz) < 0.001) { exists = true; break; }
                                }
                                if (!exists) {
                                    rates.push_back(hz);
                                    rational_rates.push_back({m.RefreshRate.Numerator, m.RefreshRate.Denominator});
                                }
                            }
                        }
                    }

                    if (refresh_rate_index >= 0 && refresh_rate_index < static_cast<int>(rational_rates.size())) {
                        out_numerator = rational_rates[refresh_rate_index].first;
                        out_denominator = rational_rates[refresh_rate_index].second;
                        return true;
                    }
                    break; // matched output found
                }
                break;
            }
        }
    }
    
    return false;
}

// Helper function to apply display settings using modern API with rational refresh rates
bool ApplyDisplaySettingsModern(int monitor_index, int width, int height, UINT32 refresh_numerator, UINT32 refresh_denominator) {
    // Get monitor handle
    std::vector<HMONITOR> monitors;
    EnumDisplayMonitors(nullptr, nullptr, 
        [](HMONITOR hmon, HDC, LPRECT, LPARAM lparam) -> BOOL {
            auto* monitors_ptr = reinterpret_cast<std::vector<HMONITOR>*>(lparam);
            monitors_ptr->push_back(hmon);
            return TRUE;
        }, 
        reinterpret_cast<LPARAM>(&monitors));
    
    if (monitor_index < 0 || monitor_index >= static_cast<int>(monitors.size())) {
        // Log error: Invalid monitor index
        std::ostringstream oss;
        oss << "ApplyDisplaySettingsModern: Invalid monitor index " << monitor_index << " (valid range: 0-" << (monitors.size() - 1) << ")";
        // Note: We can't use LogWarn here as it's not available in this file
        return false;
    }
    
    HMONITOR hmon = monitors[monitor_index];

    // Get the path info for this monitor
    MONITORINFOEXW mi;
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoW(hmon, &mi)) {
        // Log error: Failed to get monitor info
        std::ostringstream oss;
        oss << "ApplyDisplaySettingsModern: Failed to get monitor info for monitor " << monitor_index;
        return false;
    }

    // Try to use modern SetDisplayConfig API
    UINT32 path_elements = 0;
    UINT32 mode_elements = 0;
    LONG result = GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &path_elements, &mode_elements);
    if (result != ERROR_SUCCESS) {
        // Log error: Failed to get buffer sizes
        std::ostringstream oss;
        oss << "ApplyDisplaySettingsModern: GetDisplayConfigBufferSizes failed with error " << result;
        return false;
    }
    
    if (path_elements == 0 || mode_elements == 0) {
        // Log error: No display paths or modes found
        std::ostringstream oss;
        oss << "ApplyDisplaySettingsModern: No display paths (" << path_elements << ") or modes (" << mode_elements << ") found";
        return false;
    }
    
    std::vector<DISPLAYCONFIG_PATH_INFO> paths(path_elements);
    std::vector<DISPLAYCONFIG_MODE_INFO> modes(mode_elements);
    
    result = QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &path_elements, paths.data(), &mode_elements, modes.data(), nullptr);
    if (result != ERROR_SUCCESS) {
        // Log error: Failed to query display config
        std::ostringstream oss;
        oss << "ApplyDisplaySettingsModern: QueryDisplayConfig failed with error " << result;
        return false;
    }
    
    // Find the path for our monitor
    bool found_monitor = false;
    for (UINT32 i = 0; i < path_elements; i++) {
        DISPLAYCONFIG_SOURCE_DEVICE_NAME source_name = {};
        source_name.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
        source_name.header.size = sizeof(source_name);
        source_name.header.adapterId = paths[i].sourceInfo.adapterId;
        source_name.header.id = paths[i].sourceInfo.id;
        
        result = DisplayConfigGetDeviceInfo(&source_name.header);
        if (result == ERROR_SUCCESS) {
            // Convert monitor device name to wide string for comparison
            std::wstring monitor_device(mi.szDevice);
            if (monitor_device == source_name.viewGdiDeviceName) {
                found_monitor = true;
                
                // Found our monitor path, now modify the mode
                if (paths[i].sourceInfo.modeInfoIdx < mode_elements) {
                    auto& source_mode = modes[paths[i].sourceInfo.modeInfoIdx].sourceMode;
                    source_mode.width = width;
                    source_mode.height = height;
                    
                    // Set the rational refresh rate
                    paths[i].targetInfo.refreshRate.Numerator = refresh_numerator;
                    paths[i].targetInfo.refreshRate.Denominator = refresh_denominator;
                    
                    // Apply the changes
                    result = SetDisplayConfig(path_elements, paths.data(), mode_elements, modes.data(), 
                                           SDC_APPLY | SDC_USE_SUPPLIED_DISPLAY_CONFIG);
                    if (result == ERROR_SUCCESS) {
                        return true;
                    } else {
                        // Log error: SetDisplayConfig failed
                        std::ostringstream oss;
                        oss << "ApplyDisplaySettingsModern: SetDisplayConfig failed with error " << result;
                        return false;
                    }
                } else {
                    // Log error: Invalid mode index
                    std::ostringstream oss;
                    oss << "ApplyDisplaySettingsModern: Invalid mode index " << paths[i].sourceInfo.modeInfoIdx 
                        << " (max: " << (mode_elements - 1) << ")";
                    return false;
                }
                break;
            }
        } else {
            // Log error: Failed to get source device name
            std::ostringstream oss;
            oss << "ApplyDisplaySettingsModern: DisplayConfigGetDeviceInfo failed for path " << i << " with error " << result;
        }
    }
    
    if (!found_monitor) {
        // Log error: Monitor not found in display config
        std::ostringstream oss;
        oss << "ApplyDisplaySettingsModern: Monitor device '" << std::string(mi.szDevice, mi.szDevice + wcslen(mi.szDevice)) 
            << "' not found in display config paths";
        return false;
    }
    
    return false;
}

// Helper function to apply display settings using DXGI API with fractional refresh rates
bool ApplyDisplaySettingsDXGI(int monitor_index, int width, int height, UINT32 refresh_numerator, UINT32 refresh_denominator) {
    // Get monitor handle
    std::vector<HMONITOR> monitors;
    EnumDisplayMonitors(nullptr, nullptr, 
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
    ComPtr<IDXGIFactory1> factory;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
        return false;
    }
    
    // Find the adapter and output for our monitor
    for (UINT a = 0; ; ++a) {
        ComPtr<IDXGIAdapter1> adapter;
        if (factory->EnumAdapters1(a, &adapter) == DXGI_ERROR_NOT_FOUND) break;
        
        for (UINT o = 0; ; ++o) {
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
            
            D3D_FEATURE_LEVEL feature_levels[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
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
            swap_chain_desc.OutputWindow = nullptr; // We don't need a window for this
            swap_chain_desc.Windowed = FALSE; // Start in fullscreen
            swap_chain_desc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
            
            HRESULT hr = D3D11CreateDeviceAndSwapChain(
                adapter.Get(),
                D3D_DRIVER_TYPE_UNKNOWN,
                nullptr,
                0,
                feature_levels,
                ARRAYSIZE(feature_levels),
                D3D11_SDK_VERSION,
                &swap_chain_desc,
                &swap_chain,
                &device,
                &feature_level,
                &context
            );
            
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
        break; // Only try the first adapter
    }
    
    return false;
}

} // namespace renodx::resolution
