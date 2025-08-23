#include "display_cache.hpp"
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <cmath>
#include <algorithm>
#include <set>
#include <windows.h>

using Microsoft::WRL::ComPtr;

namespace renodx::display_cache {

// Global instance
DisplayCache g_displayCache;

// Helper function to get monitor friendly name
std::wstring GetMonitorFriendlyName(HMONITOR monitor) {
    MONITORINFOEXW mi;
    mi.cbSize = sizeof(mi);
    if (GetMonitorInfoW(monitor, &mi)) {
        // Try to get the monitor name from registry
        DISPLAY_DEVICEW dd;
        dd.cb = sizeof(dd);
        if (EnumDisplayDevicesW(mi.szDevice, 0, &dd, 0)) {
            if (dd.DeviceString[0] != '\0') {
                return std::wstring(dd.DeviceString);
            }
        }
        
        // Fallback to device name
        return std::wstring(mi.szDevice);
    }
    return L"Unknown Monitor";
}

// Helper function to check HDR support
bool CheckHDRSupport(HMONITOR monitor) {
    // This is a simplified check - in practice you'd want to use more sophisticated methods
    // like checking DXGI capabilities or using the modern display configuration APIs
    return false; // Placeholder
}

// Helper function to check VRR support
bool CheckVRRSupport(HMONITOR monitor) {
    // This is a simplified check - in practice you'd want to use more sophisticated methods
    // like checking DXGI capabilities or using the modern display configuration APIs
    return false; // Placeholder
}

// Helper function to get current refresh rate using modern Display Configuration API
bool GetCurrentRefreshRateModern(const std::wstring& device_name, RationalRefreshRate& refresh_rate) {
    // For now, return false to indicate this method is not available
    // The Windows Display Configuration API requires specific SDK headers that may not be available
    // We'll fall back to the DXGI approach which is more reliable
    return false;
}

// Helper function to get current display settings
bool GetCurrentDisplaySettings(HMONITOR monitor, int& width, int& height, RationalRefreshRate& refresh_rate) {
    MONITORINFOEXW mi;
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoW(monitor, &mi)) {
        return false;
    }
    
    // First try to get current refresh rate using modern Display Configuration API
    if (GetCurrentRefreshRateModern(mi.szDevice, refresh_rate)) {
        // Get current resolution from DEVMODE
        DEVMODEW dm;
        dm.dmSize = sizeof(dm);
        if (EnumDisplaySettingsW(mi.szDevice, ENUM_CURRENT_SETTINGS, &dm)) {
            width = static_cast<int>(dm.dmPelsWidth);
            height = static_cast<int>(dm.dmPelsHeight);
            
            // Debug: Log what we found via modern API
            OutputDebugStringA(("Modern API: Current mode " + std::to_string(width) + "x" + 
                               std::to_string(height) + " @ " + std::to_string(refresh_rate.numerator) + 
                               "/" + std::to_string(refresh_rate.denominator) + "Hz\n").c_str());
            return true;
        }
    }
    
    // Fallback: Try to get current settings using DXGI for precise refresh rate
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
                if (desc.Monitor != monitor) continue;
                
                // Get current display mode using DXGI
                // We need to enumerate modes and find the one closest to current settings
                UINT num_modes = 0;
                if (SUCCEEDED(output->GetDisplayModeList(
                        DXGI_FORMAT_R8G8B8A8_UNORM,
                        0,
                        &num_modes,
                        nullptr)) && num_modes > 0) {
                    
                    std::vector<DXGI_MODE_DESC> modes(num_modes);
                    if (SUCCEEDED(output->GetDisplayModeList(
                            DXGI_FORMAT_R8G8B8A8_UNORM,
                            0,
                            &num_modes,
                            modes.data()))) {
                        
                        // Get current settings from DEVMODE for comparison
                        DEVMODEW dm;
                        dm.dmSize = sizeof(dm);
                        if (EnumDisplaySettingsW(mi.szDevice, ENUM_CURRENT_SETTINGS, &dm)) {
                            // Among modes with the current resolution, pick the refresh closest to current setting
                            const double current_hz = static_cast<double>(dm.dmDisplayFrequency);
                            double best_diff = 1e10; // Large number instead of infinity
                            DXGI_MODE_DESC best_mode{};
                            bool have_best = false;

                            for (const auto& mode : modes) {
                                if (mode.Width == static_cast<UINT>(dm.dmPelsWidth) &&
                                    mode.Height == static_cast<UINT>(dm.dmPelsHeight) &&
                                    mode.RefreshRate.Denominator > 0) {
                                    const double mode_hz = static_cast<double>(mode.RefreshRate.Numerator) /
                                                           static_cast<double>(mode.RefreshRate.Denominator);
                                    const double diff = std::abs(mode_hz - current_hz);
                                    if (diff < best_diff) {
                                        best_diff = diff;
                                        best_mode = mode;
                                        have_best = true;
                                    }
                                }
                            }

                            if (have_best) {
                                width = static_cast<int>(best_mode.Width);
                                height = static_cast<int>(best_mode.Height);
                                refresh_rate.numerator = best_mode.RefreshRate.Numerator;
                                refresh_rate.denominator = best_mode.RefreshRate.Denominator;

                                // Debug: Log what we found via DXGI
                                OutputDebugStringA(("DXGI: Chose closest current mode " + std::to_string(width) + "x" +
                                                   std::to_string(height) + " @ " + std::to_string(refresh_rate.numerator) +
                                                   "/" + std::to_string(refresh_rate.denominator) + "Hz (target " +
                                                   std::to_string(current_hz) + "Hz)\n").c_str());
                                return true;
                            }

                            // Debug: Log that we didn't find a DXGI match
                            OutputDebugStringA(("DXGI: No resolution match found for current mode " +
                                               std::to_string(dm.dmPelsWidth) + "x" + std::to_string(dm.dmPelsHeight) +
                                               " @ ~" + std::to_string(dm.dmDisplayFrequency) + "Hz\n").c_str());
                        }
                    }
                }
                break; // Found our monitor
            }
            break; // Found our adapter
        }
    }
    
    // Fallback to legacy API if DXGI fails
    DEVMODEW dm;
    dm.dmSize = sizeof(dm);
    if (!EnumDisplaySettingsW(mi.szDevice, ENUM_CURRENT_SETTINGS, &dm)) {
        return false;
    }
    
    width = static_cast<int>(dm.dmPelsWidth);
    height = static_cast<int>(dm.dmPelsHeight);
    
    // For current refresh rate, we'll use the DEVMODE value as a fallback
    // In practice, you might want to use the modern display configuration APIs
    // to get the exact rational refresh rate
    refresh_rate.numerator = static_cast<UINT32>(dm.dmDisplayFrequency);
    refresh_rate.denominator = 1;
    
    // Debug: Log what we got from legacy API
    OutputDebugStringA(("Legacy API: Current mode " + std::to_string(width) + "x" + 
                       std::to_string(height) + " @ " + std::to_string(refresh_rate.numerator) + 
                       "/" + std::to_string(refresh_rate.denominator) + "Hz\n").c_str());
    
    return true;
}

// Helper function to enumerate resolutions and refresh rates using DXGI
void EnumerateDisplayModes(HMONITOR monitor, std::vector<Resolution>& resolutions) {
    ComPtr<IDXGIFactory1> factory;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))) || !factory) {
        return;
    }
    
    // Use a set to avoid duplicate resolutions
    std::set<std::pair<int, int>> resolution_set;
    
    for (UINT a = 0; ; ++a) {
        ComPtr<IDXGIAdapter1> adapter;
        if (factory->EnumAdapters1(a, &adapter) == DXGI_ERROR_NOT_FOUND) break;
        
        for (UINT o = 0; ; ++o) {
            ComPtr<IDXGIOutput> output;
            if (adapter->EnumOutputs(o, &output) == DXGI_ERROR_NOT_FOUND) break;
            
            DXGI_OUTPUT_DESC desc{};
            if (FAILED(output->GetDesc(&desc))) continue;
            if (desc.Monitor != monitor) continue;
            
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
            
            // Group modes by resolution
            std::map<std::pair<int, int>, std::vector<DXGI_RATIONAL>> resolution_modes;
            
            for (const auto& mode : modes) {
                if (mode.Width > 0 && mode.Height > 0 && mode.RefreshRate.Denominator > 0) {
                    auto key = std::make_pair(static_cast<int>(mode.Width), static_cast<int>(mode.Height));
                    resolution_modes[key].push_back(mode.RefreshRate);
                }
            }
            
            // Convert to our Resolution structures
            for (const auto& [res_pair, refresh_rates] : resolution_modes) {
                if (resolution_set.insert(res_pair).second) { // Only add if new
                    Resolution res(res_pair.first, res_pair.second);
                    
                    // Convert refresh rates and deduplicate
                    std::set<RationalRefreshRate> unique_rates;
                    for (const auto& rate : refresh_rates) {
                        RationalRefreshRate rational_rate(rate.Numerator, rate.Denominator);
                        unique_rates.insert(rational_rate);
                    }
                    
                    // Sort refresh rates
                    res.refresh_rates.assign(unique_rates.begin(), unique_rates.end());
                    std::sort(res.refresh_rates.begin(), res.refresh_rates.end());
                    
                    resolutions.push_back(std::move(res));
                }
            }
            
            break; // Found our monitor
        }
        break; // Found our adapter
    }
}

// Helper function to enumerate resolutions and refresh rates using legacy API
void EnumerateDisplayModesLegacy(const std::wstring& device_name, std::vector<Resolution>& resolutions) {
    // Use a set to avoid duplicate resolutions
    std::set<std::pair<int, int>> resolution_set;
    
    DEVMODEW dm;
    dm.dmSize = sizeof(dm);
    
    for (int i = 0; EnumDisplaySettingsW(device_name.c_str(), i, &dm); i++) {
        if (dm.dmPelsWidth > 0 && dm.dmPelsHeight > 0) {
            auto key = std::make_pair(static_cast<int>(dm.dmPelsWidth), static_cast<int>(dm.dmPelsHeight));
            
            if (resolution_set.insert(key).second) { // Only add if new
                Resolution res(key.first, key.second);
                
                // For legacy API, we only have integer refresh rates
                RationalRefreshRate rate(static_cast<UINT32>(dm.dmDisplayFrequency), 1);
                res.refresh_rates.push_back(rate);
                
                resolutions.push_back(std::move(res));
            }
        }
    }
}

bool DisplayCache::Initialize() {
    return Refresh();
}

bool DisplayCache::Refresh() {
    Clear();
    
    // Enumerate all monitors
    std::vector<HMONITOR> monitors;
    EnumDisplayMonitors(nullptr, nullptr, 
        [](HMONITOR hmon, HDC, LPRECT, LPARAM lparam) -> BOOL {
            auto* monitors_ptr = reinterpret_cast<std::vector<HMONITOR>*>(lparam);
            monitors_ptr->push_back(hmon);
            return TRUE;
        }, 
        reinterpret_cast<LPARAM>(&monitors));
    
    // Process each monitor
    for (HMONITOR monitor : monitors) {
        auto display_info = std::make_unique<DisplayInfo>();
        display_info->monitor_handle = monitor;
        
        // Get monitor information
        MONITORINFOEXW mi;
        mi.cbSize = sizeof(mi);
        if (!GetMonitorInfoW(monitor, &mi)) {
            continue;
        }
        
        display_info->device_name = mi.szDevice;
        display_info->friendly_name = GetMonitorFriendlyName(monitor);
        
        // Get current settings
        if (!GetCurrentDisplaySettings(monitor, display_info->current_width, 
                                     display_info->current_height, 
                                     display_info->current_refresh_rate)) {
            continue;
        }
        
        // Check capabilities
        display_info->supports_hdr = CheckHDRSupport(monitor);
        display_info->supports_vrr = CheckVRRSupport(monitor);
        
        // Enumerate resolutions and refresh rates
        EnumerateDisplayModes(monitor, display_info->resolutions);
        
        // If DXGI enumeration failed, fall back to legacy API
        if (display_info->resolutions.empty()) {
            EnumerateDisplayModesLegacy(display_info->device_name, display_info->resolutions);
        }
        
        // Sort resolutions
        std::sort(display_info->resolutions.begin(), display_info->resolutions.end());
        
        // Add to cache
        displays.push_back(std::move(display_info));
    }
    
    is_initialized = true;
    return !displays.empty();
}

const DisplayInfo* DisplayCache::GetDisplayByHandle(HMONITOR monitor) const {
    for (const auto& display : displays) {
        if (display->monitor_handle == monitor) {
            return display.get();
        }
    }
    return nullptr;
}

const DisplayInfo* DisplayCache::GetDisplayByDeviceName(const std::wstring& device_name) const {
    for (const auto& display : displays) {
        if (display->device_name == device_name) {
            return display.get();
        }
    }
    return nullptr;
}

std::vector<std::string> DisplayCache::GetResolutionLabels(size_t display_index) const {
    const auto* display = GetDisplay(display_index);
    if (!display) return {};
    
    return display->GetResolutionLabels();
}

std::vector<std::string> DisplayCache::GetRefreshRateLabels(size_t display_index, size_t resolution_index) const {
    const auto* display = GetDisplay(display_index);
    if (!display) return {};
    
    return display->GetRefreshRateLabels(resolution_index);
}

bool DisplayCache::GetCurrentResolution(size_t display_index, int& width, int& height) const {
    const auto* display = GetDisplay(display_index);
    if (!display) return false;
    
    width = display->current_width;
    height = display->current_height;
    return true;
}

bool DisplayCache::GetCurrentRefreshRate(size_t display_index, RationalRefreshRate& refresh_rate) const {
    const auto* display = GetDisplay(display_index);
    if (!display) return false;
    
    refresh_rate = display->current_refresh_rate;
    return true;
}

bool DisplayCache::GetRationalRefreshRate(size_t display_index, size_t resolution_index, size_t refresh_rate_index,
                                         RationalRefreshRate& refresh_rate) const {
    const auto* display = GetDisplay(display_index);
    if (!display) return false;
    
    // Map UI resolution index: 0 = Current Resolution, otherwise shift by one
    size_t effective_index = resolution_index;
    if (resolution_index == 0) {
        auto idx = display->FindResolutionIndex(display->current_width, display->current_height);
        if (!idx.has_value()) return false;
        effective_index = idx.value();
    } else {
        if ((resolution_index - 1) >= display->resolutions.size()) return false;
        effective_index = resolution_index - 1;
    }
    
    const auto& res = display->resolutions[effective_index];
    
    // Handle option 0: Current refresh rate
    if (refresh_rate_index == 0) {
        refresh_rate = display->current_refresh_rate;
        return true;
    }

    // Handle option 1: Max supported refresh rate
    if (refresh_rate_index == 1 && !res.refresh_rates.empty()) {
        auto max_rate = std::max_element(res.refresh_rates.begin(), res.refresh_rates.end());
        if (max_rate != res.refresh_rates.end()) {
            refresh_rate = *max_rate;
            return true;
        }
    }
    
    // Handle regular refresh rate indices (2 and above)
    if (refresh_rate_index >= 2 && (refresh_rate_index - 2) < res.refresh_rates.size()) {
        refresh_rate = res.refresh_rates[refresh_rate_index - 2];
        return true;
    }
    
    return false;
}

bool DisplayCache::GetCurrentDisplayInfo(size_t display_index, int& width, int& height, RationalRefreshRate& refresh_rate) const {
    const auto* display = GetDisplay(display_index);
    if (!display) return false;
    
    width = display->current_width;
    height = display->current_height;
    refresh_rate = display->current_refresh_rate;
    return true;
}

bool DisplayCache::GetSupportedModes(size_t display_index, std::vector<Resolution>& resolutions) const {
    const auto* display = GetDisplay(display_index);
    if (!display) return false;
    
    resolutions = display->resolutions;
    return true;
}

} // namespace renodx::display_cache
