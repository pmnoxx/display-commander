#include "resolution_widget.hpp"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <reshade_imgui.hpp>
#include <sstream>
#include <string>
#include "../../display/hdr_control.hpp"
#include "../../display/query_display.hpp"
#include "../../display_cache.hpp"
#include "../../display_initial_state.hpp"
#include "../../display_restore.hpp"
#include "../../globals.hpp"
#include "../../hooks/display_settings_hooks.hpp"
#include "../../resolution_helpers.hpp"
#include "../../settings/main_tab_settings.hpp"
#include "../../utils.hpp"
#include "../../utils/logging.hpp"
#include "utils/timing.hpp"

namespace display_commander::widgets::resolution_widget {

// Helper function to format refresh rate string
std::string FormatRefreshRateString(int refresh_numerator, int refresh_denominator) {
    if (refresh_numerator > 0 && refresh_denominator > 0) {
        double refresh_hz = static_cast<double>(refresh_numerator) / static_cast<double>(refresh_denominator);
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(6) << refresh_hz;
        std::string refresh_str = oss.str();

        // Remove trailing zeros and decimal point if not needed
        refresh_str.erase(refresh_str.find_last_not_of('0') + 1, std::string::npos);
        if (refresh_str.back() == '.') {
            refresh_str.pop_back();
        }

        return "@" + refresh_str + "Hz";
    }
    return "";
}

// Global widget instance
std::unique_ptr<ResolutionWidget> g_resolution_widget = nullptr;

ResolutionWidget::ResolutionWidget() = default;

void ResolutionWidget::Initialize() {
    if (is_initialized_) return;

    LogInfo("ResolutionWidget::Initialize() - Starting resolution widget initialization");

    // Initialize settings if not already done
    InitializeResolutionSettings();

    // Set initial display to current monitor
    selected_display_index_ = 0;  // Auto (Current)
    LogInfo("ResolutionWidget::Initialize() - Set selected_display_index_ = %d (Auto/Current)",
            selected_display_index_);

    // Capture original settings when widget is first initialized
    CaptureOriginalSettings();

    is_initialized_ = true;
    needs_refresh_ = true;

    LogInfo("ResolutionWidget::Initialize() - Resolution widget initialization complete");
}

void ResolutionWidget::Cleanup() {
    if (!is_initialized_) return;

    // Save any pending changes
    if (g_resolution_settings && g_resolution_settings->HasAnyDirty()) {
        g_resolution_settings->SaveAllDirty();
    }

    is_initialized_ = false;
}

void ResolutionWidget::OnDraw() {
    if (!is_initialized_) {
        Initialize();
    }

    if (!g_resolution_settings) {
        ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "Resolution settings not initialized");
        return;
    }

    // Try to capture original settings if not captured yet
    if (!original_settings_.captured) {
        CaptureOriginalSettings();
    }

    // Refresh data if needed
    if (needs_refresh_) {
        RefreshDisplayData();
        needs_refresh_ = false;
    }

    // Apply loaded settings to UI selection (only once)
    if (!settings_applied_to_ui_) {
        //  LogInfo("ResolutionWidget::OnDraw() - First draw, applying loaded settings to UI");
        UpdateCurrentSelectionFromSettings();
        settings_applied_to_ui_ = true;
        //     LogInfo("ResolutionWidget::OnDraw() - Applied settings to UI indices: display=%d, resolution=%d,
        //     refresh=%d",
        //             selected_display_index_, selected_resolution_index_, selected_refresh_index_);
    }
    // Auto-apply checkbox
    DrawAutoApplyCheckbox();
    ImGui::Spacing();

    // Auto-apply on start
    DrawAutoApplyOnStart();
    ImGui::Spacing();

    // Auto-restore checkbox
    DrawAutoRestoreCheckbox();
    ImGui::SameLine();

    // Debug menu
    DrawDebugMenu();
    ImGui::Spacing();

    // HDR auto enable/disable and display HDR capable
    DrawHdrSection();
    ImGui::Spacing();

    // Original settings info
    DrawOriginalSettingsInfo();
    ImGui::Spacing();

    // Display selector
    DrawDisplaySelector();
    ImGui::Spacing();

    // Resolution selector
    DrawResolutionSelector();
    ImGui::Spacing();

    // Refresh rate selector
    DrawRefreshRateSelector();
    ImGui::Spacing();

    // Action buttons
    DrawActionButtons();

    // Confirmation dialog
    if (show_confirmation_) {
        DrawConfirmationDialog();
    }
}

void ResolutionWidget::DrawAutoApplyCheckbox() {
    bool auto_apply = g_resolution_settings->GetAutoApply();
    if (ImGui::Checkbox("Auto-apply changes", &auto_apply)) {
        g_resolution_settings->SetAutoApply(auto_apply);
        LogInfo("ResolutionWidget::DrawAutoApplyCheckbox() - Auto-apply changes set to: %s",
                auto_apply ? "true" : "false");
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Automatically apply resolution changes when selections are made");
    }
}

void ResolutionWidget::DrawAutoApplyOnStart() {
    bool auto_apply_on_start = g_resolution_settings->GetAutoApplyOnStart();
    if (ImGui::Checkbox("Auto-apply on game start", &auto_apply_on_start)) {
        g_resolution_settings->SetAutoApplyOnStart(auto_apply_on_start);
        LogInfo("ResolutionWidget::DrawAutoApplyOnStart() - Auto-apply on start set to: %s",
                auto_apply_on_start ? "true" : "false");
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Automatically apply resolution changes after a delay when the game starts");
    }

    if (auto_apply_on_start) {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(120.0f);
        int delay = g_resolution_settings->GetAutoApplyOnStartDelay();
        if (ImGui::InputInt("##delay_seconds", &delay, 1, 5, ImGuiInputTextFlags_EnterReturnsTrue)) {
            g_resolution_settings->SetAutoApplyOnStartDelay(delay);
        }
        ImGui::SameLine();
        ImGui::Text("s delay");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Delay in seconds before applying resolution on game start (1-300 seconds)");
        }
    }
}

void ResolutionWidget::DrawDisplaySelector() {
    // Get available displays
    std::vector<std::string> display_names;

    // Format Auto (Current) option with detailed info like legacy format
    std::string auto_label = "Auto (Current)";
    HWND hwnd = g_last_swapchain_hwnd.load();
    if (hwnd) {
        HMONITOR current_monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
        if (current_monitor) {
            const auto* display = display_cache::g_displayCache.GetDisplayByHandle(current_monitor);
            if (display) {
                int width = display->width;
                int height = display->height;
                double refresh_rate = display->current_refresh_rate.ToHz();

                // Format refresh rate with precision
                std::ostringstream rate_oss;
                rate_oss << std::fixed << std::setprecision(6) << refresh_rate;
                std::string rate_str = rate_oss.str();

                // Remove trailing zeros and decimal point if not needed
                rate_str.erase(rate_str.find_last_not_of('0') + 1, std::string::npos);
                if (rate_str.back() == '.') {
                    rate_str.pop_back();
                }

                // Use cached primary monitor flag and device name
                bool is_primary = display->is_primary;
                std::string primary_text = is_primary ? " Primary" : "";
                std::string extended_device_id(display->simple_device_id.begin(), display->simple_device_id.end());

                auto_label = "Auto (Current) [" + extended_device_id + "] " + std::to_string(width) + "x"
                             + std::to_string(height) + "@" + rate_str + "Hz" + primary_text;
            }
        }
    }
    display_names.push_back(auto_label);

    auto displays = display_cache::g_displayCache.GetDisplays();
    if (displays) {
        for (size_t i = 0; i < (std::min)(displays->size(), static_cast<size_t>(4)); ++i) {
            const auto* display = (*displays)[i].get();
            if (display) {
                // Format with resolution, refresh rate, and primary status like Auto Current
                int width = display->width;
                int height = display->height;
                double refresh_rate = display->current_refresh_rate.ToHz();

                // Format refresh rate with precision
                std::ostringstream rate_oss;
                rate_oss << std::fixed << std::setprecision(6) << refresh_rate;
                std::string rate_str = rate_oss.str();

                // Remove trailing zeros and decimal point if not needed
                rate_str.erase(rate_str.find_last_not_of('0') + 1, std::string::npos);
                if (rate_str.back() == '.') {
                    rate_str.pop_back();
                }

                // Use cached primary monitor flag and device name
                bool is_primary = display->is_primary;
                std::string primary_text = is_primary ? " Primary" : "";
                std::string extended_device_id(display->simple_device_id.begin(), display->simple_device_id.end());

                std::string name = "[" + extended_device_id + "] " + std::to_string(width) + "x"
                                   + std::to_string(height) + "@" + rate_str + "Hz" + primary_text;

                display_names.push_back(name);
            }
        }
    }

    ImGui::PushID("display_selector");
    if (ImGui::BeginCombo("##display", display_names[selected_display_index_].c_str())) {
        for (int i = 0; i < static_cast<int>(display_names.size()); ++i) {
            const bool is_selected = (i == selected_display_index_);
            if (ImGui::Selectable(display_names[i].c_str(), is_selected)) {
                selected_display_index_ = i;
                needs_refresh_ = true;
                UpdateCurrentSelectionFromSettings();
            }
            if (is_selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
    ImGui::PopID();
    ImGui::SameLine();
    ImGui::Text("Display");
}

void ResolutionWidget::DrawResolutionSelector() {
    if (resolution_labels_.empty()) {
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "No resolutions available");
        return;
    }

    ImGui::PushID("resolution_selector");
    if (ImGui::BeginCombo("##resolution", resolution_labels_[selected_resolution_index_].c_str())) {
        for (int i = 0; i < static_cast<int>(resolution_labels_.size()); ++i) {
            const bool is_selected = (i == selected_resolution_index_);
            if (ImGui::Selectable(resolution_labels_[i].c_str(), is_selected)) {
                selected_resolution_index_ = i;
                selected_refresh_index_ = 0;  // Reset refresh rate selection
                UpdateSettingsFromCurrentSelection();

                // Auto-apply if enabled
                if (g_resolution_settings->GetAutoApply()) {
                    ApplyCurrentSelection();
                }
            }
            if (is_selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
    ImGui::PopID();
    ImGui::SameLine();
    ImGui::Text("Resolution");
}

void ResolutionWidget::DrawRefreshRateSelector() {
    if (refresh_labels_.empty()) {
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "No refresh rates available");
        return;
    }

    ImGui::PushID("refresh_selector");
    if (ImGui::BeginCombo("##refresh", refresh_labels_[selected_refresh_index_].c_str())) {
        for (int i = 0; i < static_cast<int>(refresh_labels_.size()); ++i) {
            const bool is_selected = (i == selected_refresh_index_);
            if (ImGui::Selectable(refresh_labels_[i].c_str(), is_selected)) {
                selected_refresh_index_ = i;
                UpdateSettingsFromCurrentSelection();

                // Auto-apply if enabled
                if (g_resolution_settings->GetAutoApply()) {
                    ApplyCurrentSelection();
                }
            }
            if (is_selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
    ImGui::PopID();
    ImGui::SameLine();
    ImGui::Text("Refresh Rate");
}

void ResolutionWidget::DrawActionButtons() {
    int actual_display = GetActualDisplayIndex();
    auto& display_settings = g_resolution_settings->GetDisplaySettings(actual_display);

    // Show dirty state indicator
    if (display_settings.IsDirty()) {
        const ResolutionData& current = display_settings.GetCurrentState();
        const ResolutionData& last_saved = display_settings.GetLastSavedState();

        // Format resolution and refresh rate as "widthxheight@refreshHz"
        auto formatResolution = [this, actual_display](const ResolutionData& data) -> std::string {
            if (data.is_current) {
                // Get actual current resolution and refresh rate
                int current_width, current_height;
                display_cache::RationalRefreshRate current_refresh;

                if (display_cache::g_displayCache.GetCurrentResolution(actual_display, current_width, current_height)
                    && display_cache::g_displayCache.GetCurrentRefreshRate(actual_display, current_refresh)) {
                    std::string resolution = std::to_string(current_width) + "x" + std::to_string(current_height);

                    // Determine refresh rate to use
                    int current_refresh_numerator = data.refresh_numerator;
                    int current_refresh_denominator = data.refresh_denominator;

                    if (current_refresh_numerator == 0) {
                        // Use current refresh rate if no specific refresh rate is set
                        current_refresh_numerator = static_cast<int>(current_refresh.numerator);
                        current_refresh_denominator = static_cast<int>(current_refresh.denominator);
                    }

                    resolution += FormatRefreshRateString(current_refresh_numerator, current_refresh_denominator);
                    return resolution;
                } else {
                    return "Current Resolution";
                }
            }

            std::string resolution = std::to_string(data.width) + "x" + std::to_string(data.height);

            // Determine refresh rate to use
            int current_refresh_numerator = data.refresh_numerator;
            int current_refresh_denominator = data.refresh_denominator;

            if (current_refresh_numerator == 0) {
                // Find current refresh rate
                display_cache::RationalRefreshRate current_refresh;
                if (display_cache::g_displayCache.GetCurrentRefreshRate(actual_display, current_refresh)) {
                    current_refresh_numerator = static_cast<int>(current_refresh.numerator);
                    current_refresh_denominator = static_cast<int>(current_refresh.denominator);
                }
            }

            resolution += FormatRefreshRateString(current_refresh_numerator, current_refresh_denominator);
            return resolution;
        };

        std::string current_str = formatResolution(current);
        std::string saved_str = formatResolution(last_saved);

        ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "● %s -> %s", saved_str.c_str(), current_str.c_str());
    } else {
        ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "● Settings saved");
    }

    ImGui::Spacing();

    // Apply button
    if (ImGui::Button("Apply Resolution")) {
        // Store pending resolution for confirmation
        if (!resolution_data_.empty() && !refresh_data_.empty()) {
            // Store the current resolution before changing
            int current_width, current_height;
            display_cache::RationalRefreshRate current_refresh;

            if (display_cache::g_displayCache.GetCurrentResolution(actual_display, current_width, current_height)
                && display_cache::g_displayCache.GetCurrentRefreshRate(actual_display, current_refresh)) {
                previous_resolution_.width = current_width;
                previous_resolution_.height = current_height;
                previous_resolution_.refresh_numerator = static_cast<int>(current_refresh.numerator);
                previous_resolution_.refresh_denominator = static_cast<int>(current_refresh.denominator);
                previous_resolution_.is_current = false;  // This is a specific resolution, not "current"

                // Store the same data for refresh rate
                previous_refresh_.width = current_width;
                previous_refresh_.height = current_height;
                previous_refresh_.refresh_numerator = static_cast<int>(current_refresh.numerator);
                previous_refresh_.refresh_denominator = static_cast<int>(current_refresh.denominator);
                previous_refresh_.is_current = false;
            }

            pending_resolution_ = resolution_data_[selected_resolution_index_];
            pending_refresh_ = refresh_data_[selected_refresh_index_];
            pending_display_index_ = actual_display;

            // Apply the resolution immediately
            if (TryApplyResolution(actual_display, pending_resolution_, pending_refresh_)) {
                // Start confirmation timer
                show_confirmation_ = true;
                confirmation_start_time_ns_ = utils::get_now_ns();
                confirmation_timer_seconds_ = 30;
            }
        }
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Apply the selected resolution and refresh rate");
    }

    ImGui::SameLine();

    // Save button
    if (display_settings.IsDirty()) {
        if (ImGui::Button("Save Settings")) {
            display_settings.SaveCurrentState();
            display_settings.Save();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Save current settings to configuration");
        }
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
        ImGui::Button("Save Settings");
        ImGui::PopStyleColor();
    }

    ImGui::SameLine();

    // Reset button
    if (display_settings.IsDirty()) {
        if (ImGui::Button("Reset")) {
            display_settings.ResetToLastSaved();
            UpdateCurrentSelectionFromSettings();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Reset to last saved settings");
        }
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
        ImGui::Button("Reset");
        ImGui::PopStyleColor();
    }
}

void ResolutionWidget::RefreshDisplayData() {
    int actual_display = GetActualDisplayIndex();
    LogInfo("ResolutionWidget::RefreshDisplayData() - actual_display=%d, selected_resolution_index_=%d", actual_display,
            selected_resolution_index_);

    // Get resolution labels
    resolution_labels_ = display_cache::g_displayCache.GetResolutionLabels(actual_display);
    resolution_data_.clear();
    LogInfo("ResolutionWidget::RefreshDisplayData() - Found %zu resolution options", resolution_labels_.size());

    // Build resolution data
    for (size_t i = 0; i < resolution_labels_.size(); ++i) {
        ResolutionData data;
        if (i == 0) {
            // Current resolution
            data.is_current = true;
            LogInfo("ResolutionWidget::RefreshDisplayData() - Resolution[%zu]: Current Resolution", i);
        } else {
            // Parse resolution from label
            const std::string& label = resolution_labels_[i];
            size_t x_pos = label.find(" x ");
            if (x_pos != std::string::npos) {
                try {
                    data.width = std::stoi(label.substr(0, x_pos));
                    data.height = std::stoi(label.substr(x_pos + 3));
                    LogInfo("ResolutionWidget::RefreshDisplayData() - Resolution[%zu]: %dx%d", i, data.width,
                            data.height);
                } catch (...) {
                    // Parse failed, use current resolution
                    data.is_current = true;
                    LogInfo("ResolutionWidget::RefreshDisplayData() - Resolution[%zu]: Parse failed, using current", i);
                }
            } else {
                data.is_current = true;
                LogInfo("ResolutionWidget::RefreshDisplayData() - Resolution[%zu]: No 'x' found, using current", i);
            }
        }
        resolution_data_.push_back(data);
    }

    // Get refresh rate labels
    refresh_labels_ = display_cache::g_displayCache.GetRefreshRateLabels(actual_display, selected_resolution_index_);
    refresh_data_.clear();
    LogInfo("ResolutionWidget::RefreshDisplayData() - Found %zu refresh rate options for resolution index %d",
            refresh_labels_.size(), selected_resolution_index_);

    // Build refresh rate data
    for (size_t i = 0; i < refresh_labels_.size(); ++i) {
        ResolutionData data;
        if (i == 0) {
            // Current refresh rate
            data.is_current = true;
            LogInfo("ResolutionWidget::RefreshDisplayData() - Refresh[%zu]: Current Refresh Rate", i);
        } else {
            // Parse refresh rate from label
            const std::string& label = refresh_labels_[i];
            size_t hz_pos = label.find("Hz");
            if (hz_pos != std::string::npos) {
                try {
                    double hz = std::stod(label.substr(0, hz_pos));
                    // Convert to rational (approximate)
                    data.refresh_numerator = static_cast<int>(hz * 1000);
                    data.refresh_denominator = 1000;
                    LogInfo("ResolutionWidget::RefreshDisplayData() - Refresh[%zu]: %.3f Hz (%d/%d)", i, hz,
                            data.refresh_numerator, data.refresh_denominator);
                } catch (...) {
                    data.is_current = true;
                    LogInfo("ResolutionWidget::RefreshDisplayData() - Refresh[%zu]: Parse failed, using current", i);
                }
            } else {
                data.is_current = true;
                LogInfo("ResolutionWidget::RefreshDisplayData() - Refresh[%zu]: No 'Hz' found, using current", i);
            }
        }
        refresh_data_.push_back(data);
    }
}

void ResolutionWidget::RefreshResolutionData() {
    // Refresh resolution data for current display
    RefreshDisplayData();
}

void ResolutionWidget::RefreshRefreshRateData() {
    int actual_display = GetActualDisplayIndex();

    // Get refresh rate labels for current resolution
    refresh_labels_ = display_cache::g_displayCache.GetRefreshRateLabels(actual_display, selected_resolution_index_);
    refresh_data_.clear();

    // Build refresh rate data
    for (size_t i = 0; i < refresh_labels_.size(); ++i) {
        ResolutionData data;
        if (i == 0) {
            data.is_current = true;
        } else {
            const std::string& label = refresh_labels_[i];
            size_t hz_pos = label.find("Hz");
            if (hz_pos != std::string::npos) {
                try {
                    double hz = std::stod(label.substr(0, hz_pos));
                    data.refresh_numerator = static_cast<int>(hz * 1000);
                    data.refresh_denominator = 1000;
                } catch (...) {
                    data.is_current = true;
                }
            } else {
                data.is_current = true;
            }
        }
        refresh_data_.push_back(data);
    }
}

bool ResolutionWidget::ApplyCurrentSelection() {
    if (resolution_data_.empty() || refresh_data_.empty()) {
        return false;
    }

    int actual_display = GetActualDisplayIndex();
    const ResolutionData& resolution = resolution_data_[selected_resolution_index_];
    const ResolutionData& refresh = refresh_data_[selected_refresh_index_];

    return TryApplyResolution(actual_display, resolution, refresh);
}

bool ResolutionWidget::ApplyResolution(int display_index, int width, int height, int refresh_numerator,
                                       int refresh_denominator) {
    ResolutionData resolution(width, height);
    ResolutionData refresh;
    if (refresh_numerator > 0 && refresh_denominator > 0) {
        refresh = ResolutionData(0, 0, refresh_numerator, refresh_denominator);
    } else {
        // Use current refresh rate if not specified
        refresh.is_current = true;
    }
    return TryApplyResolution(display_index, resolution, refresh);
}

void ResolutionWidget::PrepareForAutoApply() {
    // Ensure widget is initialized
    if (!is_initialized_) {
        Initialize();
    }

    if (!g_resolution_settings) {
        return;
    }

    // Refresh display data if needed
    if (needs_refresh_) {
        RefreshDisplayData();
        needs_refresh_ = false;
    }

    // Update selection from saved settings if not already done
    if (!settings_applied_to_ui_) {
        UpdateCurrentSelectionFromSettings();
        settings_applied_to_ui_ = true;
    }

    // Ensure resolution and refresh rate data are loaded
    RefreshResolutionData();
    RefreshRefreshRateData();
}

bool ResolutionWidget::TryApplyResolution(int display_index, const ResolutionData& resolution,
                                          const ResolutionData& refresh) {
    if (resolution.is_current && refresh.is_current) {
        // Both are current, nothing to apply
        return true;
    }
    LogInfo("[TryApplyResolution] resolution: %d %d %d %d", resolution.width, resolution.height,
            resolution.refresh_numerator, resolution.refresh_denominator);

    // Mark original state before applying (to capture current state for restore)
    display_restore::MarkOriginalForDisplayIndex(display_index);

    int width = resolution.width;
    int height = resolution.height;
    int refresh_num = refresh.refresh_numerator;
    int refresh_denom = refresh.refresh_denominator;

    // Get current values if needed
    if (resolution.is_current) {
        if (!display_cache::g_displayCache.GetCurrentResolution(display_index, width, height)) {
            return false;
        }
    }

    if (refresh.is_current) {
        display_cache::RationalRefreshRate current_refresh;
        if (!display_cache::g_displayCache.GetCurrentRefreshRate(display_index, current_refresh)) {
            return false;
        }
        refresh_num = static_cast<int>(current_refresh.numerator);
        refresh_denom = static_cast<int>(current_refresh.denominator);
    }

    // Apply using DXGI first, then fallback to legacy
    if (resolution::ApplyDisplaySettingsDXGI(display_index, width, height, refresh_num, refresh_denom)) {
        s_resolution_applied_at_least_once.store(true);
        // Mark device as changed after successful application
        display_restore::MarkDeviceChangedByDisplayIndex(display_index);
        return true;
    }

    // Fallback: legacy ChangeDisplaySettingsExW
    const auto* display = display_cache::g_displayCache.GetDisplay(display_index);
    if (!display) return false;
    HMONITOR hmon = display->monitor_handle;
    MONITORINFOEXW mi;
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoW(hmon, &mi)) return false;

    DEVMODEW dm;
    ZeroMemory(&dm, sizeof(dm));
    dm.dmSize = sizeof(dm);
    dm.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT | DM_DISPLAYFREQUENCY;
    dm.dmPelsWidth = width;
    dm.dmPelsHeight = height;
    dm.dmDisplayFrequency =
        static_cast<DWORD>(std::lround(static_cast<double>(refresh_num) / static_cast<double>(refresh_denom)));

    LogInfo("ResolutionWidget::TryApplyResolution() - ChangeDisplaySettingsExW_Direct: %S", mi.szDevice);
    LONG result = ChangeDisplaySettingsExW_Direct(mi.szDevice, &dm, nullptr, CDS_UPDATEREGISTRY, nullptr);
    if (result == DISP_CHANGE_SUCCESSFUL) {
        s_resolution_applied_at_least_once.store(true);
        // Mark device as changed after successful application
        display_restore::MarkDeviceChangedByDisplayIndex(display_index);
    }
    return result == DISP_CHANGE_SUCCESSFUL;
}

void ResolutionWidget::DrawConfirmationDialog() {
    // Calculate remaining time using high-precision timing
    LONGLONG now_ns = utils::get_now_ns();
    LONGLONG elapsed_ns = now_ns - confirmation_start_time_ns_;
    LONGLONG elapsed_seconds = elapsed_ns / utils::SEC_TO_NS;
    int remaining_seconds = confirmation_timer_seconds_ - static_cast<int>(elapsed_seconds);

    if (remaining_seconds <= 0) {
        // Timer expired, revert resolution
        RevertResolution();
        show_confirmation_ = false;
        return;
    }

    // Center the dialog
    ImGuiIO& io = ImGui::GetIO();
    ImVec2 center = ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f);
    ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));

    // Create modal dialog
    ImGui::SetNextWindowSize(ImVec2(400, 200), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Resolution Change Confirmation", &show_confirmation_,
                     ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse)) {
        // Format the resolution change
        auto formatResolution = [this](const ResolutionData& data) -> std::string {
            if (data.is_current) {
                // Get actual current resolution and refresh rate
                int current_width, current_height;
                display_cache::RationalRefreshRate current_refresh;

                if (display_cache::g_displayCache.GetCurrentResolution(pending_display_index_, current_width,
                                                                       current_height)
                    && display_cache::g_displayCache.GetCurrentRefreshRate(pending_display_index_, current_refresh)) {
                    std::string resolution = std::to_string(current_width) + "x" + std::to_string(current_height);

                    // Determine refresh rate to use
                    int current_refresh_numerator = data.refresh_numerator;
                    int current_refresh_denominator = data.refresh_denominator;

                    if (current_refresh_numerator == 0) {
                        // Use current refresh rate if no specific refresh rate is set
                        current_refresh_numerator = static_cast<int>(current_refresh.numerator);
                        current_refresh_denominator = static_cast<int>(current_refresh.denominator);
                    }

                    resolution += FormatRefreshRateString(current_refresh_numerator, current_refresh_denominator);
                    return resolution;
                } else {
                    return "Current Resolution";
                }
            }

            std::string resolution = std::to_string(data.width) + "x" + std::to_string(data.height);

            // Determine refresh rate to use
            int current_refresh_numerator = data.refresh_numerator;
            int current_refresh_denominator = data.refresh_denominator;

            if (current_refresh_numerator == 0) {
                // Find current refresh rate
                display_cache::RationalRefreshRate current_refresh;
                if (display_cache::g_displayCache.GetCurrentRefreshRate(pending_display_index_, current_refresh)) {
                    current_refresh_numerator = static_cast<int>(current_refresh.numerator);
                    current_refresh_denominator = static_cast<int>(current_refresh.denominator);
                }
            }

            resolution += FormatRefreshRateString(current_refresh_numerator, current_refresh_denominator);
            return resolution;
        };

        std::string resolution_str = formatResolution(pending_resolution_);

        // Display the change
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "Resolution changed to:");
        ImGui::Text("Resolution: %s", resolution_str.c_str());

        ImGui::Spacing();
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "Auto Revert: %ds", remaining_seconds);

        ImGui::Spacing();

        // Buttons
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.8f, 0.0f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.0f, 1.0f, 0.0f, 1.0f));
        if (ImGui::Button("Confirm", ImVec2(100, 30))) {
            // User confirmed, save the settings
            auto& display_settings = g_resolution_settings->GetDisplaySettings(pending_display_index_);
            display_settings.SetCurrentState(pending_resolution_);
            display_settings.SaveCurrentState();
            display_settings.Save();
            show_confirmation_ = false;
        }
        ImGui::PopStyleColor(2);

        ImGui::SameLine();

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.0f, 0.0f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 0.0f, 0.0f, 1.0f));
        if (ImGui::Button("Revert", ImVec2(100, 30))) {
            // User reverted, restore previous resolution
            RevertResolution();
            show_confirmation_ = false;
        }
        ImGui::PopStyleColor(2);
    }
    ImGui::End();
}

void ResolutionWidget::RevertResolution() {
    // Apply the stored previous resolution
    if (previous_resolution_.width > 0 && previous_resolution_.height > 0) {
        TryApplyResolution(pending_display_index_, previous_resolution_, previous_refresh_);
    } else {
        // Fallback: get current resolution and apply it
        int current_width, current_height;
        display_cache::RationalRefreshRate current_refresh;

        if (display_cache::g_displayCache.GetCurrentResolution(pending_display_index_, current_width, current_height)
            && display_cache::g_displayCache.GetCurrentRefreshRate(pending_display_index_, current_refresh)) {
            ResolutionData current_res;
            current_res.width = current_width;
            current_res.height = current_height;
            current_res.refresh_numerator = static_cast<int>(current_refresh.numerator);
            current_res.refresh_denominator = static_cast<int>(current_refresh.denominator);
            current_res.is_current = true;

            TryApplyResolution(pending_display_index_, current_res, current_res);
        }
    }
}

std::string ResolutionWidget::GetDisplayName(int display_index) const {
    if (display_index == 0) {
        // Format Auto (Current) option with detailed info like legacy format
        std::string auto_label = "Auto (Current)";
        HWND hwnd = g_last_swapchain_hwnd.load();
        if (hwnd) {
            HMONITOR current_monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
            if (current_monitor) {
                const auto* display = display_cache::g_displayCache.GetDisplayByHandle(current_monitor);
                if (display) {
                    int width = display->width;
                    int height = display->height;
                    double refresh_rate = display->current_refresh_rate.ToHz();

                    // Format refresh rate with precision
                    std::ostringstream rate_oss;
                    rate_oss << std::fixed << std::setprecision(6) << refresh_rate;
                    std::string rate_str = rate_oss.str();

                    // Remove trailing zeros and decimal point if not needed
                    rate_str.erase(rate_str.find_last_not_of('0') + 1, std::string::npos);
                    if (rate_str.back() == '.') {
                        rate_str.pop_back();
                    }

                    // Use cached primary monitor flag and device name
                    bool is_primary = display->is_primary;
                    std::string primary_text = is_primary ? " Primary" : "";
                    std::string extended_device_id(display->simple_device_id.begin(), display->simple_device_id.end());

                    auto_label = "Auto (Current) [" + extended_device_id + "] " + std::to_string(width) + "x"
                                 + std::to_string(height) + "@" + rate_str + "Hz" + primary_text;
                }
            }
        }
        return auto_label;
    }

    auto displays = display_cache::g_displayCache.GetDisplays();
    if (displays && display_index <= static_cast<int>(displays->size())) {
        const auto* display = (*displays)[display_index - 1].get();
        if (display) {
            // Format with resolution, refresh rate, and primary status like Auto Current
            int width = display->width;
            int height = display->height;
            double refresh_rate = display->current_refresh_rate.ToHz();

            // Format refresh rate with precision
            std::ostringstream rate_oss;
            rate_oss << std::fixed << std::setprecision(6) << refresh_rate;
            std::string rate_str = rate_oss.str();

            // Remove trailing zeros and decimal point if not needed
            rate_str.erase(rate_str.find_last_not_of('0') + 1, std::string::npos);
            if (rate_str.back() == '.') {
                rate_str.pop_back();
            }

            // Use cached primary monitor flag and device name
            bool is_primary = display->is_primary;
            std::string primary_text = is_primary ? " Primary" : "";
            std::string extended_device_id(display->simple_device_id.begin(), display->simple_device_id.end());

            std::string name = "[" + extended_device_id + "] " + std::to_string(width) + "x" + std::to_string(height)
                               + "@" + rate_str + "Hz" + primary_text;

            return name;
        }
    }

    return "Display " + std::to_string(display_index);
}

int ResolutionWidget::GetActualDisplayIndex() const {
    if (selected_display_index_ == 0) {
        // Auto (Current) - find current monitor
        HWND hwnd = g_last_swapchain_hwnd.load();
        if (hwnd) {
            HMONITOR current_monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
            if (current_monitor) {
                auto displays = display_cache::g_displayCache.GetDisplays();
                if (displays) {
                    for (size_t i = 0; i < displays->size(); ++i) {
                        const auto* display = (*displays)[i].get();
                        if (display && display->monitor_handle == current_monitor) {
                            return static_cast<int>(i);
                        }
                    }
                }
            }
        }
        return 0;  // Fallback to first display
    } else {
        return selected_display_index_ - 1;
    }
}

void ResolutionWidget::UpdateCurrentSelectionFromSettings() {
    int actual_display = GetActualDisplayIndex();
    auto& display_settings = g_resolution_settings->GetDisplaySettings(actual_display);
    const ResolutionData& current_state = display_settings.GetCurrentState();

    LogInfo(
        "ResolutionWidget::UpdateCurrentSelectionFromSettings() - actual_display=%d, current_state: %dx%d @ %d/%d, "
        "is_current=%s",
        actual_display, current_state.width, current_state.height, current_state.refresh_numerator,
        current_state.refresh_denominator, current_state.is_current ? "true" : "false");

    // Find matching resolution index
    selected_resolution_index_ = 0;
    if (!current_state.is_current && current_state.width > 0 && current_state.height > 0) {
        // Look for exact width/height match
        for (size_t i = 0; i < resolution_data_.size(); ++i) {
            const auto& res = resolution_data_[i];
            if (!res.is_current && res.width == current_state.width && res.height == current_state.height) {
                selected_resolution_index_ = static_cast<int>(i);
                LogInfo(
                    "ResolutionWidget::UpdateCurrentSelectionFromSettings() - Found exact resolution match at "
                    "index %d: %dx%d",
                    selected_resolution_index_, res.width, res.height);
                break;
            }
        }
    }

    // Find matching refresh rate index
    selected_refresh_index_ = 0;
    if (!current_state.is_current && current_state.refresh_numerator > 0 && current_state.refresh_denominator > 0) {
        // Look for refresh rate match (need to refresh data first to get refresh options for selected resolution)
        RefreshDisplayData();

        for (size_t i = 0; i < refresh_data_.size(); ++i) {
            const auto& refresh = refresh_data_[i];
            if (!refresh.is_current && refresh.refresh_numerator == current_state.refresh_numerator
                && refresh.refresh_denominator == current_state.refresh_denominator) {
                selected_refresh_index_ = static_cast<int>(i);
                LogInfo(
                    "ResolutionWidget::UpdateCurrentSelectionFromSettings() - Found exact refresh rate match at "
                    "index %d: %d/%d",
                    selected_refresh_index_, refresh.refresh_numerator, refresh.refresh_denominator);
                break;
            }
        }
    }

    LogInfo(
        "ResolutionWidget::UpdateCurrentSelectionFromSettings() - Set UI indices: display=%d, resolution=%d, "
        "refresh=%d",
        selected_display_index_, selected_resolution_index_, selected_refresh_index_);

    // Apply the loaded settings if they are not "current" (i.e., they are specific resolution/refresh rate settings)
    if (!current_state.is_current && current_state.width > 0 && current_state.height > 0) {
        LogInfo(
            "ResolutionWidget::UpdateCurrentSelectionFromSettings() - Applying loaded resolution settings: %dx%d @ "
            "%d/%d",
            current_state.width, current_state.height, current_state.refresh_numerator,
            current_state.refresh_denominator);

        // Create resolution and refresh data from loaded settings
        ResolutionData resolution_data;
        resolution_data.width = current_state.width;
        resolution_data.height = current_state.height;
        resolution_data.is_current = false;

        ResolutionData refresh_data;
        refresh_data.refresh_numerator = current_state.refresh_numerator;
        refresh_data.refresh_denominator = current_state.refresh_denominator;
        refresh_data.is_current = false;

        // Apply the settings
        if (TryApplyResolution(actual_display, resolution_data, refresh_data)) {
            LogInfo(
                "ResolutionWidget::UpdateCurrentSelectionFromSettings() - Successfully applied loaded resolution "
                "settings");
        } else {
            LogError(
                "ResolutionWidget::UpdateCurrentSelectionFromSettings() - Failed to apply loaded resolution settings");
        }
    } else {
        LogInfo(
            "ResolutionWidget::UpdateCurrentSelectionFromSettings() - Skipping resolution application "
            "(is_current=%s, width=%d, height=%d)",
            current_state.is_current ? "true" : "false", current_state.width, current_state.height);
    }
}

void ResolutionWidget::UpdateSettingsFromCurrentSelection() {
    if (resolution_data_.empty() || refresh_data_.empty()) {
        return;
    }

    int actual_display = GetActualDisplayIndex();
    auto& display_settings = g_resolution_settings->GetDisplaySettings(actual_display);

    // Create combined resolution data
    ResolutionData combined = resolution_data_[selected_resolution_index_];
    if (!refresh_data_[selected_refresh_index_].is_current) {
        combined.refresh_numerator = refresh_data_[selected_refresh_index_].refresh_numerator;
        combined.refresh_denominator = refresh_data_[selected_refresh_index_].refresh_denominator;
    }

    display_settings.SetCurrentState(combined);
}

void ResolutionWidget::CaptureOriginalSettings() {
    if (original_settings_.captured) {
        return;  // Already captured
    }

    // Try to get current monitor from game window first
    HWND hwnd = g_last_swapchain_hwnd.load();
    HMONITOR current_monitor = nullptr;

    if (hwnd) {
        current_monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    }

    // If no game window available, try to get primary monitor as fallback
    if (!current_monitor) {
        current_monitor = MonitorFromWindow(nullptr, MONITOR_DEFAULTTOPRIMARY);
    }

    if (!current_monitor) {
        return;
    }

    // Get display info from cache
    const auto* display = display_cache::g_displayCache.GetDisplayByHandle(current_monitor);
    if (!display) {
        return;
    }

    // Capture original settings
    original_settings_.width = display->width;
    original_settings_.height = display->height;
    original_settings_.refresh_numerator = static_cast<int>(display->current_refresh_rate.numerator);
    original_settings_.refresh_denominator = static_cast<int>(display->current_refresh_rate.denominator);
    original_settings_.extended_device_id =
        std::string(display->simple_device_id.begin(), display->simple_device_id.end());
    original_settings_.is_primary = display->is_primary;
    original_settings_.captured = true;

    // Mark this display for restore tracking
    display_restore::MarkOriginalForMonitor(current_monitor);
}

std::string ResolutionWidget::FormatOriginalSettingsString() const {
    if (!original_settings_.captured) {
        // Show debug info about why capture failed
        HWND hwnd = g_last_swapchain_hwnd.load();
        if (!hwnd) {
            return "Original settings not captured (no game window)";
        }

        HMONITOR current_monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
        if (!current_monitor) {
            return "Original settings not captured (no monitor)";
        }

        const auto* display = display_cache::g_displayCache.GetDisplayByHandle(current_monitor);
        if (!display) {
            return "Original settings not captured (no display cache)";
        }

        return "Original settings not captured (unknown reason)";
    }

    // Format refresh rate
    std::string refresh_str = "";
    if (original_settings_.refresh_numerator > 0 && original_settings_.refresh_denominator > 0) {
        double refresh_hz = static_cast<double>(original_settings_.refresh_numerator)
                            / static_cast<double>(original_settings_.refresh_denominator);
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(6) << refresh_hz;
        refresh_str = oss.str();

        // Remove trailing zeros and decimal point if not needed
        refresh_str.erase(refresh_str.find_last_not_of('0') + 1, std::string::npos);
        if (refresh_str.back() == '.') {
            refresh_str.pop_back();
        }
        refresh_str = "@" + refresh_str + "Hz";
    }

    std::string primary_text = original_settings_.is_primary ? " Primary" : "";

    return "[" + original_settings_.extended_device_id + "] " + std::to_string(original_settings_.width) + "x"
           + std::to_string(original_settings_.height) + refresh_str + primary_text;
}

void ResolutionWidget::DrawOriginalSettingsInfo() {
    ImGui::TextColored(ImVec4(0.7f, 0.9f, 0.7f, 1.0f), "Original Settings:");
    ImGui::SameLine();

    // Get the currently selected display
    int actual_display = GetActualDisplayIndex();
    const auto* display = display_cache::g_displayCache.GetDisplay(actual_display);

    if (!display) {
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "No display selected");
        return;
    }

    // Get initial state for this display
    const auto* initial_state =
        display_initial_state::g_initialDisplayState.GetInitialStateForDevice(display->simple_device_id);

    if (!initial_state) {
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Not recorded");
        return;
    }

    // Format refresh rate
    std::string refresh_str = "";
    if (initial_state->refresh_numerator > 0 && initial_state->refresh_denominator > 0) {
        double refresh_hz = static_cast<double>(initial_state->refresh_numerator)
                            / static_cast<double>(initial_state->refresh_denominator);
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(6) << refresh_hz;
        refresh_str = oss.str();

        // Remove trailing zeros and decimal point if not needed
        refresh_str.erase(refresh_str.find_last_not_of('0') + 1, std::string::npos);
        if (refresh_str.back() == '.') {
            refresh_str.pop_back();
        }
        refresh_str = "@" + refresh_str + "Hz";
    }

    std::string device_id = WideCharToUTF8(display->simple_device_id);
    std::string primary_text = initial_state->is_primary ? " Primary" : "";

    std::string original_settings_str = "[" + device_id + "] " + std::to_string(initial_state->width) + "x"
                                        + std::to_string(initial_state->height) + refresh_str + primary_text;

    ImGui::Text("%s", original_settings_str.c_str());
}

void ResolutionWidget::DrawAutoRestoreCheckbox() {
    bool auto_restore = s_auto_restore_resolution_on_close.load();
    if (ImGui::Checkbox("Auto-restore on exit", &auto_restore)) {
        s_auto_restore_resolution_on_close.store(auto_restore);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Automatically restore original display settings when the game closes");
    }
}

void ResolutionWidget::DrawHdrSection() {
    bool auto_hdr = settings::g_mainTabSettings.auto_enable_disable_hdr.GetValue();
    if (ImGui::Checkbox("Auto enable/disable HDR", &auto_hdr)) {
        settings::g_mainTabSettings.auto_enable_disable_hdr.SetValue(auto_hdr);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            "When enabled, automatically turn Windows HDR on for the game display when the game starts, "
            "and turn it off when the game exits.");
    }

    int actual_display = GetActualDisplayIndex();
    bool hdr_supported = false;
    bool hdr_enabled = false;
    bool got_state = display_commander::display::hdr_control::GetHdrStateForDisplayIndex(actual_display, &hdr_supported,
                                                                                         &hdr_enabled);

    if (got_state) {
        ImGui::SameLine();
        ImGui::TextColored(hdr_supported ? ImVec4(0.5f, 1.0f, 0.5f, 1.0f) : ImVec4(0.7f, 0.7f, 0.7f, 1.0f),
                           "Display HDR capable: %s", hdr_supported ? "Yes" : "No");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Whether the selected display supports Windows HDR (advanced color).");
        }
        if (hdr_supported) {
            ImGui::SameLine();
            ImGui::TextColored(hdr_enabled ? ImVec4(0.5f, 1.0f, 0.5f, 1.0f) : ImVec4(0.8f, 0.8f, 0.5f, 1.0f), "HDR: %s",
                               hdr_enabled ? "On" : "Off");
            ImGui::SameLine();
            if (ImGui::Button(hdr_enabled ? "Disable HDR" : "Enable HDR")) {
                if (display_commander::display::hdr_control::SetHdrForDisplayIndex(actual_display, !hdr_enabled)) {
                    display_cache::g_displayCache.Refresh();
                    needs_refresh_ = true;
                }
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Turn Windows HDR (advanced color) on or off for the selected display.");
            }
        }
    } else {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Display HDR: N/A");
    }

    // Details/Advanced: Override HDR static metadata (ignore source MaxCLL/MaxFALL) - Sony/display fix
    if (ImGui::CollapsingHeader("Miscellaneous", ImGuiTreeNodeFlags_None)) {
        ImGui::Indent();
        bool auto_maxmdl = settings::g_mainTabSettings.auto_apply_maxmdl_1000_hdr_metadata.GetValue();
        if (ImGui::Checkbox("Override HDR metadata (ignore source MaxCLL/MaxFALL)", &auto_maxmdl)) {
            settings::g_mainTabSettings.auto_apply_maxmdl_1000_hdr_metadata.SetValue(auto_maxmdl);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Inject HDR10 static metadata (e.g. 1000 nits) instead of using source values. "
                "Use when HDR looks dim or washed out on PC. TVs that often need this: Samsung, Sony, Panasonic "
                "(they handle MaxCLL/MaxFALL differently or ignore source metadata).");
        }
        ImGui::Unindent();
    }
}

void ResolutionWidget::DrawDebugMenu() {
    static bool show_debug_menu = false;

    if (ImGui::Button("Debug menu")) {
        show_debug_menu = !show_debug_menu;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Show debug information about display resolution tracking");
    }

    if (!show_debug_menu) {
        return;
    }

    ImGui::Begin("Display Debug Menu", &show_debug_menu, ImGuiWindowFlags_AlwaysAutoResize);

    // Get initial display states
    auto initial_states = display_initial_state::g_initialDisplayState.GetInitialStates();
    bool has_initial_states = initial_states && !initial_states->empty();

    // Get current displays from cache
    auto displays = display_cache::g_displayCache.GetDisplays();
    if (!displays || displays->empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "No displays found in cache");
        ImGui::End();
        return;
    }

    // Determine which display will be targeted for auto-apply on start (if enabled)
    int target_display_index = -1;
    if (g_resolution_settings && g_resolution_settings->GetAutoApplyOnStart()) {
        target_display_index = GetActualDisplayIndex();
    }

    // Create table header
    if (ImGui::BeginTable("DisplayDebugTable", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("Display", ImGuiTableColumnFlags_WidthFixed, 200.0f);
        ImGui::TableSetupColumn("Initial Resolution/Refresh", ImGuiTableColumnFlags_WidthFixed, 250.0f);
        ImGui::TableSetupColumn("Applied Change", ImGuiTableColumnFlags_WidthFixed, 120.0f);
        ImGui::TableSetupColumn("Auto-Apply Target", ImGuiTableColumnFlags_WidthFixed, 150.0f);
        ImGui::TableSetupColumn("Current Resolution/Refresh", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();

        // Iterate through all displays
        for (size_t i = 0; i < displays->size(); ++i) {
            const auto* display = (*displays)[i].get();
            if (!display) {
                continue;
            }

            ImGui::TableNextRow();

            // Display name/ID
            ImGui::TableSetColumnIndex(0);
            std::string display_name = WideCharToUTF8(display->friendly_name);
            std::string device_id = WideCharToUTF8(display->simple_device_id);
            std::string display_label = "[" + device_id + "] " + display_name;
            if (display->is_primary) {
                display_label += " (Primary)";
            }
            ImGui::Text("%s", display_label.c_str());

            // Initial resolution/refresh rate
            ImGui::TableSetColumnIndex(1);
            if (has_initial_states) {
                const auto* initial_state =
                    display_initial_state::g_initialDisplayState.GetInitialStateForDevice(display->simple_device_id);
                if (initial_state) {
                    double refresh_hz = initial_state->GetRefreshRateHz();
                    std::ostringstream oss;
                    oss << initial_state->width << "x" << initial_state->height;
                    oss << " @ " << std::fixed << std::setprecision(6) << refresh_hz << "Hz";
                    std::string refresh_str = oss.str();
                    // Remove trailing zeros
                    size_t pos = refresh_str.find_last_not_of('0');
                    if (pos != std::string::npos && pos > refresh_str.find('.')) {
                        if (refresh_str[pos] == '.') {
                            refresh_str = refresh_str.substr(0, pos);
                        } else {
                            refresh_str = refresh_str.substr(0, pos + 1);
                        }
                    }
                    ImGui::TextColored(ImVec4(0.7f, 0.9f, 0.7f, 1.0f), "%s", refresh_str.c_str());
                } else {
                    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Not recorded");
                }
            } else {
                ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Not captured");
            }

            // Applied change status
            ImGui::TableSetColumnIndex(2);
            bool was_changed = display_restore::WasDeviceChangedByDeviceName(display->simple_device_id);
            if (was_changed) {
                ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "True");
            } else {
                ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "False");
            }

            // Auto-apply target status
            ImGui::TableSetColumnIndex(3);
            if (target_display_index >= 0 && static_cast<size_t>(target_display_index) == i) {
                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "Yes (On Start)");
            } else {
                ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "No");
            }

            // Current resolution/refresh rate
            ImGui::TableSetColumnIndex(4);
            double current_refresh_hz = display->current_refresh_rate.ToHz();
            std::ostringstream current_oss;
            current_oss << display->width << "x" << display->height;
            current_oss << " @ " << std::fixed << std::setprecision(6) << current_refresh_hz << "Hz";
            std::string current_refresh_str = current_oss.str();
            // Remove trailing zeros
            size_t current_pos = current_refresh_str.find_last_not_of('0');
            if (current_pos != std::string::npos && current_pos > current_refresh_str.find('.')) {
                if (current_refresh_str[current_pos] == '.') {
                    current_refresh_str = current_refresh_str.substr(0, current_pos);
                } else {
                    current_refresh_str = current_refresh_str.substr(0, current_pos + 1);
                }
            }
            ImGui::Text("%s", current_refresh_str.c_str());
        }

        ImGui::EndTable();
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Test restore button
    if (ImGui::Button("Test Restore on Exit", ImVec2(-1, 0))) {
        LogInfo("ResolutionWidget::DrawDebugMenu() - Test restore button clicked, calling RestoreAllIfEnabled()");
        display_restore::RestoreAllIfEnabled();
        // Refresh display cache to show updated resolutions
        display_cache::g_displayCache.Refresh();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            "Test the restore functionality that runs on game exit. "
            "This will restore all displays that had resolution changes applied.");
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::TextWrapped("Initial Resolution/Refresh: Resolution and refresh rate recorded on startup");
    ImGui::TextWrapped("Applied Change: True if a resolution change was applied to this display");
    ImGui::TextWrapped(
        "Auto-Apply Target: Shows which display will have resolution change applied on game start (if auto-apply on "
        "start is enabled)");
    ImGui::TextWrapped("Current Resolution/Refresh: Current display resolution and refresh rate");

    ImGui::End();
}

// Global functions
void InitializeResolutionWidget() {
    if (!g_resolution_widget) {
        g_resolution_widget = std::make_unique<ResolutionWidget>();
        g_resolution_widget->Initialize();
    }
}

void CleanupResolutionWidget() {
    if (g_resolution_widget) {
        g_resolution_widget->Cleanup();
        g_resolution_widget.reset();
    }
}

void DrawResolutionWidget() {
    if (g_resolution_widget) {
        g_resolution_widget->OnDraw();
    }
}

}  // namespace display_commander::widgets::resolution_widget
