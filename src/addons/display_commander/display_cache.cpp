#include "display_cache.hpp"
#include "display/query_display.hpp"
#include "globals.hpp"
#include "settings/main_tab_settings.hpp"
#include "utils.hpp"
#include <algorithm>
#include <dxgi1_6.h>
#include <immintrin.h>
#include <iomanip>
#include <set>
#include <sstream>
#include <windows.h>
#include <wrl/client.h>


using Microsoft::WRL::ComPtr;

namespace display_cache {

// Global instance
DisplayCache g_displayCache;

// Helper function to get monitor friendly name
std::wstring GetMonitorFriendlyName(MONITORINFOEXW &mi) {
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

// Helper function to enumerate resolutions and refresh rates using DXGI
void EnumerateDisplayModes(HMONITOR monitor, std::vector<Resolution> &resolutions) {
    ComPtr<IDXGIFactory1> factory = GetSharedDXGIFactory();
    if (!factory) {
        return;
    }

    // Use a set to avoid duplicate resolutions
    std::set<std::pair<int, int>> resolution_set;

    for (UINT a = 0;; ++a) {
        ComPtr<IDXGIAdapter1> adapter;
        if (factory->EnumAdapters1(a, &adapter) == DXGI_ERROR_NOT_FOUND)
            break;

        for (UINT o = 0;; ++o) {
            ComPtr<IDXGIOutput> output;
            if (adapter->EnumOutputs(o, &output) == DXGI_ERROR_NOT_FOUND)
                break;

            DXGI_OUTPUT_DESC desc{};
            if (FAILED(output->GetDesc(&desc)))
                continue;
            if (desc.Monitor != monitor)
                continue;

            ComPtr<IDXGIOutput1> output1;
            if (FAILED(output.As(&output1)) || !output1)
                continue;

            UINT num_modes = 0;
            if (FAILED(output1->GetDisplayModeList1(DXGI_FORMAT_R8G8B8A8_UNORM, 0, &num_modes, nullptr))) {
                continue;
            }

            std::vector<DXGI_MODE_DESC1> modes(num_modes);
            if (FAILED(output1->GetDisplayModeList1(DXGI_FORMAT_R8G8B8A8_UNORM, 0, &num_modes, modes.data()))) {
                continue;
            }

            // Group modes by resolution
            std::map<std::pair<int, int>, std::vector<DXGI_RATIONAL>> resolution_modes;

            for (const auto &mode : modes) {
                if (mode.Width > 0 && mode.Height > 0 && mode.RefreshRate.Denominator > 0) {
                    auto key = std::make_pair(static_cast<int>(mode.Width), static_cast<int>(mode.Height));
                    resolution_modes[key].push_back(mode.RefreshRate);
                }
            }

            // Convert to our Resolution structures
            for (const auto &[res_pair, refresh_rates] : resolution_modes) {
                if (resolution_set.insert(res_pair).second) { // Only add if new
                    Resolution res(res_pair.first, res_pair.second);

                    // Convert refresh rates and deduplicate
                    std::set<RationalRefreshRate> unique_rates;
                    for (const auto &rate : refresh_rates) {
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

bool DisplayCache::Initialize() { return Refresh(); }

bool DisplayCache::Refresh() {
    // Build a fresh cache snapshot locally (no locking, allows system calls)
    std::vector<std::unique_ptr<DisplayInfo>> new_displays;

    // Enumerate all monitors
    std::vector<HMONITOR> monitors;
    EnumDisplayMonitors(
        nullptr, nullptr,
        [](HMONITOR hmon, HDC, LPRECT, LPARAM lparam) -> BOOL {
            auto *monitors_ptr = reinterpret_cast<std::vector<HMONITOR> *>(lparam);
            monitors_ptr->push_back(hmon);
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&monitors));
    if (monitors.empty()) {
        LogError("DisplayCache: No monitors found");
        return false;
    }

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
        display_info->friendly_name = GetMonitorFriendlyName(mi);

        // Store monitor properties from MONITORINFOEXW
        display_info->is_primary = (mi.dwFlags & MONITORINFOF_PRIMARY) != 0;
        display_info->monitor_rect = mi.rcMonitor;
        display_info->work_rect = mi.rcWork;

        // Get current settings
        if (!GetCurrentDisplaySettingsQueryConfig(
                monitor, display_info->width, display_info->height, display_info->current_refresh_rate.numerator,
                display_info->current_refresh_rate.denominator, display_info->x, display_info->y)) {
            continue;
        }

        // Enumerate resolutions and refresh rates
        EnumerateDisplayModes(monitor, display_info->resolutions);

        // Sort resolutions
        std::sort(display_info->resolutions.begin(), display_info->resolutions.end());

        // Add to snapshot
        new_displays.push_back(std::move(display_info));
    }

    // Atomically swap the new displays data
    displays.store(std::make_shared<std::vector<std::unique_ptr<DisplayInfo>>>(std::move(new_displays)),
                   std::memory_order_release);
    is_initialized.store(true, std::memory_order_release);

    // Update target_display if it's unset or the current display is not found
    std::string current_target_display = settings::g_mainTabSettings.target_display.GetValue();
    if (current_target_display.empty() || current_target_display == "No Window" ||
        current_target_display == "No Monitor" || current_target_display == "Monitor Info Failed") {
        // Try to get the display device ID from the current game window
        settings::UpdateTargetDisplayFromGameWindow();
    }

    // Update FPS limit maximums based on monitor refresh rates
    settings::UpdateFpsLimitMaximums();

    auto displays_ptr = displays.load(std::memory_order_acquire);
    return displays_ptr && !displays_ptr->empty();
}

const DisplayInfo *DisplayCache::GetDisplayByHandle(HMONITOR monitor) const {
    auto displays_ptr = displays.load(std::memory_order_acquire);
    if (!displays_ptr)
        return nullptr;

    for (const auto &display : *displays_ptr) {
        if (display->monitor_handle == monitor) {
            return display.get();
        }
    }
    return nullptr;
}

const DisplayInfo *DisplayCache::GetDisplayByDeviceName(const std::wstring &device_name) const {
    auto displays_ptr = displays.load(std::memory_order_acquire);
    if (!displays_ptr)
        return nullptr;

    for (const auto &display : *displays_ptr) {
        if (display->device_name == device_name) {
            return display.get();
        }
    }
    return nullptr;
}

int DisplayCache::GetDisplayIndexByDeviceName(const std::string &device_name) const {
    auto displays_ptr = displays.load(std::memory_order_acquire);
    if (!displays_ptr)
        return -1;

    // Convert string to wstring for comparison
    std::wstring wdevice_name(device_name.begin(), device_name.end());

    for (size_t i = 0; i < displays_ptr->size(); ++i) {
        const auto &display = (*displays_ptr)[i];
        if (display && display->device_name == wdevice_name) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

std::vector<std::string> DisplayCache::GetResolutionLabels(size_t display_index) const {
    auto displays_ptr = displays.load(std::memory_order_acquire);
    if (!displays_ptr || display_index >= displays_ptr->size())
        return {};
    const auto *display = (*displays_ptr)[display_index].get();
    if (!display)
        return {};
    return display->GetResolutionLabels();
}

std::vector<std::string> DisplayCache::GetRefreshRateLabels(size_t display_index, size_t resolution_index) const {
    auto displays_ptr = displays.load(std::memory_order_acquire);
    if (!displays_ptr || display_index >= displays_ptr->size())
        return {};
    const auto *display = (*displays_ptr)[display_index].get();
    if (!display)
        return {};
    return display->GetRefreshRateLabels(resolution_index);
}

bool DisplayCache::GetCurrentResolution(size_t display_index, int &width, int &height) const {
    auto displays_ptr = displays.load(std::memory_order_acquire);
    if (!displays_ptr || display_index >= displays_ptr->size())
        return false;
    const auto *display = (*displays_ptr)[display_index].get();
    if (!display)
        return false;
    width = display->width;
    height = display->height;
    return true;
}

bool DisplayCache::GetCurrentRefreshRate(size_t display_index, RationalRefreshRate &refresh_rate) const {
    auto displays_ptr = displays.load(std::memory_order_acquire);
    if (!displays_ptr || display_index >= displays_ptr->size())
        return false;
    const auto *display = (*displays_ptr)[display_index].get();
    if (!display)
        return false;
    refresh_rate = display->current_refresh_rate;
    return true;
}

bool DisplayCache::GetRationalRefreshRate(size_t display_index, size_t resolution_index, size_t refresh_rate_index,
                                          RationalRefreshRate &refresh_rate) const {
    auto displays_ptr = displays.load(std::memory_order_acquire);
    if (!displays_ptr || display_index >= displays_ptr->size())
        return false;
    const auto *display = (*displays_ptr)[display_index].get();
    if (!display)
        return false;
    // Map UI resolution index: 0 = Current Resolution, otherwise shift by one
    size_t effective_index = resolution_index;
    if (resolution_index == 0) {
        auto idx = display->FindResolutionIndex(display->width, display->height);
        if (!idx.has_value())
            return false;
        effective_index = idx.value();
    } else {
        if ((resolution_index - 1) >= display->resolutions.size())
            return false;
        effective_index = resolution_index - 1;
    }
    const auto &res = display->resolutions[effective_index];
    if (refresh_rate_index == 0) {
        refresh_rate = display->current_refresh_rate;
        return true;
    }
    if (refresh_rate_index == 1 && !res.refresh_rates.empty()) {
        auto max_rate = std::max_element(res.refresh_rates.begin(), res.refresh_rates.end());
        if (max_rate != res.refresh_rates.end()) {
            refresh_rate = *max_rate;
            return true;
        }
    }
    if (refresh_rate_index >= 2 && (refresh_rate_index - 2) < res.refresh_rates.size()) {
        refresh_rate = res.refresh_rates[refresh_rate_index - 2];
        return true;
    }
    return false;
}

bool DisplayCache::GetCurrentDisplayInfo(size_t display_index, int &width, int &height,
                                         RationalRefreshRate &refresh_rate) const {
    auto displays_ptr = displays.load(std::memory_order_acquire);
    if (!displays_ptr || display_index >= displays_ptr->size())
        return false;
    const auto *display = (*displays_ptr)[display_index].get();
    if (!display)
        return false;
    width = display->width;
    height = display->height;
    refresh_rate = display->current_refresh_rate;
    return true;
}

bool DisplayCache::GetSupportedModes(size_t display_index, std::vector<Resolution> &resolutions) const {
    auto displays_ptr = displays.load(std::memory_order_acquire);
    if (!displays_ptr || display_index >= displays_ptr->size())
        return false;
    const auto *display = (*displays_ptr)[display_index].get();
    if (!display)
        return false;
    resolutions = display->resolutions;
    return true;
}

std::shared_ptr<std::vector<std::unique_ptr<DisplayInfo>>> DisplayCache::GetDisplays() const {
    return displays.load(std::memory_order_acquire);
}

size_t DisplayCache::GetDisplayCount() const {
    auto displays_ptr = displays.load(std::memory_order_acquire);
    return displays_ptr ? displays_ptr->size() : 0;
}

const DisplayInfo *DisplayCache::GetDisplay(size_t index) const {
    auto displays_ptr = displays.load(std::memory_order_acquire);
    if (!displays_ptr || index >= displays_ptr->size())
        return nullptr;
    return (*displays_ptr)[index].get();
}

void DisplayCache::SwapFrom(DisplayCache &&other) {
    if (&other == this)
        return;
    // Atomically swap the displays data
    auto other_displays = other.displays.load(std::memory_order_acquire);
    displays.store(other_displays, std::memory_order_release);
    is_initialized.store(other.is_initialized.load(std::memory_order_acquire), std::memory_order_release);
}

bool DisplayCache::CopyDisplay(size_t index, DisplayInfo &out) const {
    auto displays_ptr = displays.load(std::memory_order_acquire);
    if (!displays_ptr || index >= displays_ptr->size() || !(*displays_ptr)[index])
        return false;
    out = *(*displays_ptr)[index];
    return true;
}

void DisplayCache::PrintVSyncFreqDivider() const {
    auto displays_ptr = displays.load(std::memory_order_acquire);

    if (!displays_ptr || displays_ptr->empty()) {
        LogInfo("DisplayCache: No displays available to print vSyncFreqDivider");
        return;
    }

    for (size_t i = 0; i < displays_ptr->size(); ++i) {
        const auto &display = (*displays_ptr)[i];
        if (!display)
            continue;

        std::ostringstream oss;
        oss << "Display " << i << " (";
        // Convert wstring to string for output
        std::string friendly_name_str(display->friendly_name.begin(), display->friendly_name.end());
        oss << friendly_name_str << "): ";
        oss << "Current refresh rate: " << display->current_refresh_rate.ToString();
        oss << " [Raw: " << display->current_refresh_rate.numerator << "/" << display->current_refresh_rate.denominator
            << "]";

        // Calculate vSyncFreqDivider equivalent (this is a conceptual representation)
        // In SpecialK, vSyncFreqDivider is used to divide the vsync frequency
        // Here we'll show the current refresh rate and potential divider values
        double current_hz = display->current_refresh_rate.ToHz();
        if (current_hz > 0.0) {
            oss << " | vSyncFreqDivider equivalents: ";
            for (int divider = 1; divider <= 6; ++divider) {
                double divided_hz = current_hz / divider;
                oss << divider << ":" << std::fixed << std::setprecision(2) << divided_hz << "Hz";
                if (divider < 6)
                    oss << ", ";
            }
        }

        LogInfo(oss.str().c_str());
    }
}

double DisplayCache::GetMaxRefreshRateAcrossAllMonitors() const {
    auto displays_ptr = displays.load(std::memory_order_acquire);
    if (!displays_ptr || displays_ptr->empty()) {
        return 60.0; // Default fallback
    }

    // Ensure we have a reasonable minimum
    double max_refresh_rate = 60.0;

    for (const auto &display : *displays_ptr) {
        if (!display)
            continue;

        // Check current refresh rate
        double current_rate = display->current_refresh_rate.ToHz();
        if (current_rate > max_refresh_rate) {
            max_refresh_rate = current_rate;
        }

        // Check all supported refresh rates for all resolutions
        for (const auto &resolution : display->resolutions) {
            for (const auto &refresh_rate : resolution.refresh_rates) {
                double rate_hz = refresh_rate.ToHz();
                if (rate_hz > max_refresh_rate) {
                    max_refresh_rate = rate_hz;
                }
            }
        }
    }

    return max_refresh_rate;
}

} // namespace display_cache
