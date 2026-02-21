#include "monitor_settings.hpp"
#include "../../display_cache.hpp"
#include "../../display_restore.hpp"
#include "../ui_display_tab.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <iomanip>
#include <reshade_imgui.hpp>
#include <sstream>
#include <thread>
#include "../../globals.hpp"
#include "../../hooks/display_settings_hooks.hpp"
#include "../../resolution_helpers.hpp"
#include "../../settings/main_tab_settings.hpp"
#include "../../utils.hpp"
#include "../../utils/logging.hpp"
#include "../new_ui/settings_wrapper.hpp"

namespace ui::monitor_settings {

// Persistent settings for monitor settings UI
ui::new_ui::BoolSetting g_setting_auto_apply_resolution("AutoApplyResolution", false);
ui::new_ui::BoolSetting g_setting_auto_apply_refresh("AutoApplyRefresh", false);
ui::new_ui::BoolSetting g_setting_apply_display_settings_at_start("ApplyDisplaySettingsAtStart", false);

// Per-display persisted selections (support displays 0..3) - NEW FORMAT
static ui::new_ui::ResolutionPairSetting g_setting_selected_resolution_0("SelectedResolutionIndex_0", 0, 0);
static ui::new_ui::ResolutionPairSetting g_setting_selected_resolution_1("SelectedResolutionIndex_1", 0, 0);
static ui::new_ui::ResolutionPairSetting g_setting_selected_resolution_2("SelectedResolutionIndex_2", 0, 0);
static ui::new_ui::ResolutionPairSetting g_setting_selected_resolution_3("SelectedResolutionIndex_3", 0, 0);

static ui::new_ui::RefreshRatePairSetting g_setting_selected_refresh_0("SelectedRefreshIndex_0", 0, 0);
static ui::new_ui::RefreshRatePairSetting g_setting_selected_refresh_1("SelectedRefreshIndex_1", 0, 0);
static ui::new_ui::RefreshRatePairSetting g_setting_selected_refresh_2("SelectedRefreshIndex_2", 0, 0);
static ui::new_ui::RefreshRatePairSetting g_setting_selected_refresh_3("SelectedRefreshIndex_3", 0, 0);

static ui::new_ui::ResolutionPairSetting& GetResSettingForDisplay(int display_index) {
    switch (display_index) {
        case 0:  return g_setting_selected_resolution_0;
        case 1:  return g_setting_selected_resolution_1;
        case 2:  return g_setting_selected_resolution_2;
        case 3:  return g_setting_selected_resolution_3;
        default: return g_setting_selected_resolution_0;
    }
}

static ui::new_ui::RefreshRatePairSetting& GetRefreshSettingForDisplay(int display_index) {
    switch (display_index) {
        case 0:  return g_setting_selected_refresh_0;
        case 1:  return g_setting_selected_refresh_1;
        case 2:  return g_setting_selected_refresh_2;
        case 3:  return g_setting_selected_refresh_3;
        default: return g_setting_selected_refresh_0;
    }
}

static void EnsurePersistentSettingsLoadedOnce() {
    static bool loaded = false;
    if (loaded) return;
    loaded = true;

    // Load new format settings
    g_setting_auto_apply_resolution.Load();
    g_setting_auto_apply_refresh.Load();
    g_setting_apply_display_settings_at_start.Load();
    s_auto_apply_resolution_change = g_setting_auto_apply_resolution.GetValue();
    s_auto_apply_refresh_rate_change = g_setting_auto_apply_refresh.GetValue();
    s_apply_display_settings_at_start = g_setting_apply_display_settings_at_start.GetValue();

    // Load new format resolution and refresh rate settings
    g_setting_selected_resolution_0.Load();
    g_setting_selected_resolution_1.Load();
    g_setting_selected_resolution_2.Load();
    g_setting_selected_resolution_3.Load();
    g_setting_selected_refresh_0.Load();
    g_setting_selected_refresh_1.Load();
    g_setting_selected_refresh_2.Load();
    g_setting_selected_refresh_3.Load();
}

// Auto-apply retry state
static std::atomic<uint64_t> g_resolution_apply_task_id{0};
static std::atomic<uint64_t> g_refresh_apply_task_id{0};
static std::atomic<bool> g_resolution_auto_apply_failed{false};
static std::atomic<bool> g_refresh_auto_apply_failed{false};

// Helper: Apply current selection (DXGI first, then legacy) and return success
static bool TryApplyCurrentSelectionOnce(int& out_monitor_index, int& out_width, int& out_height,
                                         display_cache::RationalRefreshRate& out_refresh_rate) {
    // Determine monitor index
    int actual_monitor_index = static_cast<int>(s_selected_monitor_index.load());
    if (s_selected_monitor_index.load() == 0) {
        HWND hwnd = g_last_swapchain_hwnd.load();
        if (hwnd) {
            HMONITOR current_monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
            if (current_monitor) {
                for (int j = 0; j < static_cast<int>(display_cache::g_displayCache.GetDisplayCount()); j++) {
                    const auto* display = display_cache::g_displayCache.GetDisplay(j);
                    if (display && display->monitor_handle == current_monitor) {
                        actual_monitor_index = j;
                        break;
                    }
                }
            }
        }
    } else {
        actual_monitor_index = static_cast<int>(s_selected_monitor_index) - 1;
    }

    // Mark for restore and device changed
    display_restore::MarkOriginalForDisplayIndex(actual_monitor_index);
    display_restore::MarkDeviceChangedByDisplayIndex(actual_monitor_index);

    // Get resolution from new pair-based settings
    int persist_slot = (actual_monitor_index < 0) ? 0 : ((actual_monitor_index > 3) ? 3 : actual_monitor_index);
    auto& resSetting = GetResSettingForDisplay(persist_slot);
    auto& refreshSetting = GetRefreshSettingForDisplay(persist_slot);

    int width = resSetting.GetWidth();
    int height = resSetting.GetHeight();

    // If width/height is 0,0, use current resolution
    if (width == 0 && height == 0) {
        if (!display_cache::g_displayCache.GetCurrentResolution(actual_monitor_index, width, height)) {
            return false;
        }
    }

    // Get refresh rate from new pair-based settings
    int numerator = refreshSetting.GetNumerator();
    int denominator = refreshSetting.GetDenominator();

    display_cache::RationalRefreshRate refresh_rate{};
    if (numerator == 0 && denominator == 0) {
        // Use current refresh rate
        if (!display_cache::g_displayCache.GetCurrentRefreshRate(actual_monitor_index, refresh_rate)) {
            return false;
        }
    } else {
        refresh_rate.numerator = numerator;
        refresh_rate.denominator = denominator;
    }

    // Try DXGI first
    if (resolution::ApplyDisplaySettingsDXGI(actual_monitor_index, width, height, refresh_rate.numerator,
                                             refresh_rate.denominator)) {
        s_resolution_applied_at_least_once.store(true);
        out_monitor_index = actual_monitor_index;
        out_width = width;
        out_height = height;
        out_refresh_rate = refresh_rate;
        return true;
    }

    // Fallback: legacy ChangeDisplaySettingsExW
    const auto* display = display_cache::g_displayCache.GetDisplay(actual_monitor_index);
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
    dm.dmDisplayFrequency = static_cast<DWORD>(std::lround(refresh_rate.ToHz()));

    LogInfo("TryApplyCurrentSelectionOnce() - ChangeDisplaySettingsExW_Direct: %S", mi.szDevice);
    LONG result = ChangeDisplaySettingsExW_Direct(mi.szDevice, &dm, nullptr, CDS_UPDATEREGISTRY, nullptr);
    if (result == DISP_CHANGE_SUCCESSFUL) {
        s_resolution_applied_at_least_once.store(true);
        out_monitor_index = actual_monitor_index;
        out_width = width;
        out_height = height;
        out_refresh_rate = refresh_rate;
        return true;
    }
    return false;
}

// Forward declaration for confirmation countdown used below
static void BeginConfirmationCountdown(int display_index, const std::string& label, int seconds);

static void StartResolutionAutoApplyWithBackoff() {
    uint64_t task_id = g_resolution_apply_task_id.fetch_add(1) + 1;
    g_resolution_auto_apply_failed.store(false);
    std::thread([task_id]() {
        const int delays_sec[5] = {1, 2, 4, 8, 16};
        for (int i = 0; i < 5; ++i) {
            // Cancellation check
            if (g_resolution_apply_task_id.load() != task_id) return;
            int mon = -1, w = 0, h = 0;
            display_cache::RationalRefreshRate rr{};
            if (TryApplyCurrentSelectionOnce(mon, w, h, rr)) {
                std::ostringstream label;
                label << w << "x" << h << " @ " << std::fixed << std::setprecision(3) << rr.ToHz() << "Hz";
                BeginConfirmationCountdown(mon, label.str(), 15);
                return;
            }
            // Wait before next attempt
            int remaining_ms = delays_sec[i] * 1000;
            while (remaining_ms > 0) {
                if (g_resolution_apply_task_id.load() != task_id) return;
                int step = (std::min)(remaining_ms, 100);
                std::this_thread::sleep_for(std::chrono::milliseconds(step));
                remaining_ms -= step;
            }
        }
        // All attempts failed: disable and remember failure
        g_resolution_auto_apply_failed.store(true);
        s_auto_apply_resolution_change = false;
        g_setting_auto_apply_resolution.SetValue(false);
        g_setting_auto_apply_resolution.Save();
    }).detach();
}

static void StartRefreshAutoApplyWithBackoff() {
    uint64_t task_id = g_refresh_apply_task_id.fetch_add(1) + 1;
    g_refresh_auto_apply_failed.store(false);
    std::thread([task_id]() {
        const int delays_sec[5] = {1, 2, 4, 8, 16};
        for (int i = 0; i < 5; ++i) {
            if (g_refresh_apply_task_id.load() != task_id) return;
            int mon = -1, w = 0, h = 0;
            display_cache::RationalRefreshRate rr{};
            if (TryApplyCurrentSelectionOnce(mon, w, h, rr)) {
                std::ostringstream label;
                label << w << "x" << h << " @ " << std::fixed << std::setprecision(3) << rr.ToHz() << "Hz";
                BeginConfirmationCountdown(mon, label.str(), 15);
                return;
            }
            int remaining_ms = delays_sec[i] * 1000;
            while (remaining_ms > 0) {
                if (g_refresh_apply_task_id.load() != task_id) return;
                int step = (std::min)(remaining_ms, 100);
                std::this_thread::sleep_for(std::chrono::milliseconds(step));
                remaining_ms -= step;
            }
        }
        g_refresh_auto_apply_failed.store(true);
        s_auto_apply_refresh_rate_change = false;
        g_setting_auto_apply_refresh.SetValue(false);
        g_setting_auto_apply_refresh.Save();
    }).detach();
}

// Pending confirmation state
static std::atomic<bool> g_has_pending_confirmation{false};
static std::atomic<int> g_confirm_seconds_remaining{0};
static std::atomic<uint64_t> g_confirm_session_id{0};
static int g_last_applied_display_index = -1;  // cache index used when applying
static std::string g_last_applied_label;

static void BeginConfirmationCountdown(int display_index, const std::string& label, int seconds = 15) {
    g_last_applied_display_index = display_index;
    g_last_applied_label = label;
    g_confirm_seconds_remaining.store(seconds);
    g_has_pending_confirmation.store(true);
    uint64_t session_id = g_confirm_session_id.fetch_add(1) + 1;
    std::thread([session_id]() {
        while (g_has_pending_confirmation.load()) {
            if (g_confirm_session_id.load() != session_id) return;  // superseded
            int remaining = g_confirm_seconds_remaining.load();
            if (remaining <= 0) {
                // Time up: auto-revert
                display_restore::RestoreDisplayByIndex(g_last_applied_display_index);
                g_has_pending_confirmation.store(false);
                return;
            }
            std::this_thread::sleep_for(std::chrono::seconds(1));
            // decrement if still same session
            if (g_confirm_session_id.load() != session_id) return;
            g_confirm_seconds_remaining.store(remaining - 1);
        }
    }).detach();
}

// Render UI and actions for pending confirmation
void HandlePendingConfirmationUI() {
    if (!g_has_pending_confirmation.load()) return;

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImVec4 warnColor(1.0f, 0.85f, 0.2f, 1.0f);
    ImGui::TextColored(warnColor, "Confirm new display mode (%s)", g_last_applied_label.c_str());
    int remaining = g_confirm_seconds_remaining.load();
    ImGui::SameLine();
    ImGui::TextColored(warnColor, "(%ds)", remaining);

    // Confirm and Cancel (Revert) buttons
    if (ImGui::Button("Confirm")) {
        // Keep the new mode; stop countdown
        g_has_pending_confirmation.store(false);
    }
    ImGui::SameLine();
    if (ImGui::Button("Revert")) {
        // Revert immediately
        display_restore::RestoreDisplayByIndex(g_last_applied_display_index);
        g_has_pending_confirmation.store(false);
    }
}

// Handle auto-detection of current display settings
void HandleAutoDetection() {
    if (!s_initial_auto_selection_done) {
        s_initial_auto_selection_done = true;

        // Get current display info for the selected monitor
        HWND hwnd = g_last_swapchain_hwnd.load();
        if (hwnd) {
            // First try to use the saved device ID for more reliable matching
            std::string saved_device_id = settings::g_mainTabSettings.game_window_extended_display_device_id.GetValue();
            int monitor_index = ui::FindMonitorIndexByDeviceId(saved_device_id);

            if (monitor_index >= 0) {
                // Found matching monitor by device ID
                s_selected_monitor_index.store(static_cast<float>(monitor_index));  // Set to direct monitor index
                LogInfo("Auto-detection: Found monitor by device ID: %s (index %d)", saved_device_id.c_str(),
                        monitor_index);

                // Prefer saved indices for this display (0..3). If out of range, fallback to closest
                if (monitor_index >= 0 && monitor_index <= 3) {
                    auto& resSetting = GetResSettingForDisplay(monitor_index);
                    auto& refSetting = GetRefreshSettingForDisplay(monitor_index);
                    // For new format, we need to find the matching index
                    // For now, default to current resolution/refresh rate
                    s_selected_resolution_index.store(0);
                    s_selected_refresh_rate_index.store(0);
                } else {
                    // Use the new methods to find closest supported modes to current settings
                    const auto* display = display_cache::g_displayCache.GetDisplay(monitor_index);
                    if (display) {
                        auto closest_resolution_index = display->FindClosestResolutionIndex();
                        if (closest_resolution_index.has_value()) {
                            s_selected_resolution_index.store(closest_resolution_index.value());
                            // Default to Current Refresh Rate (index 0 in UI)
                            s_selected_refresh_rate_index.store(0);
                        } else {
                            // Fallback to first resolution if no match found
                            s_selected_resolution_index.store(0);
                            s_selected_refresh_rate_index.store(0);
                        }
                    }
                }
            } else {
                // Fallback to monitor handle matching
                HMONITOR current_monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
                if (current_monitor) {
                    // Get monitor labels to find which monitor index this corresponds to
                    auto monitor_labels = display_cache::g_displayCache.GetMonitorLabels();

                    // Find which monitor index this corresponds to
                    for (int i = 0; i < static_cast<int>(monitor_labels.size()); i++) {
                        const auto* display = display_cache::g_displayCache.GetDisplay(i);
                        if (display && display->monitor_handle == current_monitor) {
                            // Set to direct monitor index
                            s_selected_monitor_index.store(static_cast<float>(i));
                            LogInfo("Auto-detection: Found monitor by handle (fallback) at index %d", i);

                            // Prefer saved indices for this display (0..3). If out of range, fallback to closest
                            if (i >= 0 && i <= 3) {
                                auto& resSetting = GetResSettingForDisplay(i);
                                auto& refSetting = GetRefreshSettingForDisplay(i);
                                // For new format, we need to find the matching index
                                // For now, default to current resolution/refresh rate
                                s_selected_resolution_index.store(0);
                                s_selected_refresh_rate_index.store(0);
                            } else {
                                // Use the new methods to find closest supported modes to current settings
                                auto closest_resolution_index = display->FindClosestResolutionIndex();
                                if (closest_resolution_index.has_value()) {
                                    s_selected_resolution_index.store(closest_resolution_index.value());
                                    // Default to Current Refresh Rate (index 0 in UI)
                                    s_selected_refresh_rate_index.store(0);
                                } else {
                                    // Fallback to first resolution if no match found
                                    s_selected_resolution_index.store(0);
                                    s_selected_refresh_rate_index.store(0);
                                }
                            }
                            break;
                        }
                    }
                }
            }
        }
    }
}

// Handle monitor selection UI
void HandleMonitorSelection(const std::vector<std::string>& monitor_labels) {
    // Check if current monitor is primary
    std::string monitor_label = "Monitor";
    if (s_selected_monitor_index >= 0 && s_selected_monitor_index < static_cast<int>(monitor_labels.size())) {
        const auto* display = display_cache::g_displayCache.GetDisplay(static_cast<int>(s_selected_monitor_index));
        if (display && display->monitor_handle) {
            // Use cached primary monitor flag instead of calling GetMonitorInfoW
            if (display->is_primary) {
                monitor_label = "Monitor (Primary)";
            }
        }
    }

    // Monitor selection
    if (ImGui::BeginCombo(monitor_label.c_str(), monitor_labels[static_cast<int>(s_selected_monitor_index)].c_str())) {
        for (int i = 0; i < static_cast<int>(monitor_labels.size()); i++) {
            const bool is_selected = (i == static_cast<int>(s_selected_monitor_index));
            if (ImGui::Selectable(monitor_labels[i].c_str(), is_selected)) {
                s_selected_monitor_index.store(i);

                if (i == 0) {
                    // Auto (Current) option selected - find the current monitor where the game is running
                    HWND hwnd = g_last_swapchain_hwnd.load();
                    if (hwnd) {
                        HMONITOR current_monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
                        if (current_monitor) {
                            // Find which monitor index this corresponds to in the display cache
                            for (int j = 0; j < static_cast<int>(display_cache::g_displayCache.GetDisplayCount());
                                 j++) {
                                const auto* display = display_cache::g_displayCache.GetDisplay(j);
                                if (display && display->monitor_handle == current_monitor) {
                                    // Prefer saved indices for this display (0..3)
                                    if (j >= 0 && j <= 3) {
                                        auto& resSetting = GetResSettingForDisplay(j);
                                        auto& refSetting = GetRefreshSettingForDisplay(j);
                                        // For new format, we need to find the matching index
                                        // For now, default to current resolution/refresh rate
                                        s_selected_resolution_index.store(0);
                                        s_selected_refresh_rate_index.store(0);
                                    } else {
                                        // Fallback: closest to current
                                        auto closest_resolution_index = display->FindClosestResolutionIndex();
                                        if (closest_resolution_index.has_value()) {
                                            s_selected_resolution_index.store(closest_resolution_index.value());
                                            s_selected_refresh_rate_index.store(0);
                                        } else {
                                            s_selected_resolution_index.store(0);
                                            s_selected_refresh_rate_index.store(0);
                                        }
                                    }
                                    break;
                                }
                            }
                        }
                    }
                } else {
                    // Regular monitor selected - auto-select closest resolution and refresh rate for the newly selected
                    // monitor
                    const auto* display =
                        display_cache::g_displayCache.GetDisplay(i - 1);  // -1 because monitors start at index 1 now
                    if (display) {
                        int actual = i - 1;
                        if (actual >= 0 && actual <= 3) {
                            auto& resSetting = GetResSettingForDisplay(actual);
                            auto& refSetting = GetRefreshSettingForDisplay(actual);
                            // For new format, we need to find the matching index
                            // For now, default to current resolution/refresh rate
                            s_selected_resolution_index.store(0);
                            s_selected_refresh_rate_index.store(0);
                        } else {
                            // Fallback to closest/current
                            auto closest_resolution_index = display->FindClosestResolutionIndex();
                            if (closest_resolution_index.has_value()) {
                                s_selected_resolution_index.store(closest_resolution_index.value());
                                s_selected_refresh_rate_index.store(0);
                            } else {
                                s_selected_resolution_index.store(0);
                                s_selected_refresh_rate_index.store(0);
                            }
                        }
                    }
                }
            }
            if (is_selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
}

// Handle resolution selection UI
void HandleResolutionSelection(int selected_monitor_index) {
    EnsurePersistentSettingsLoadedOnce();
    // If Auto (Current) is selected, find the current monitor where the game is running
    int actual_monitor_index = selected_monitor_index;
    if (selected_monitor_index == 0) {
        HWND hwnd = g_last_swapchain_hwnd.load();
        if (hwnd) {
            HMONITOR current_monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
            if (current_monitor) {
                // Find which monitor index this corresponds to in the display cache
                for (int j = 0; j < static_cast<int>(display_cache::g_displayCache.GetDisplayCount()); j++) {
                    const auto* display = display_cache::g_displayCache.GetDisplay(j);
                    if (display && display->monitor_handle == current_monitor) {
                        actual_monitor_index = j;
                        break;
                    }
                }
            }
        }
    } else {
        // Regular monitor selected - adjust index because monitors start at index 1 now
        actual_monitor_index = selected_monitor_index - 1;
    }

    // Determine persistence slot (0..3)
    int persist_slot = (actual_monitor_index < 0) ? 0 : ((actual_monitor_index > 3) ? 3 : actual_monitor_index);
    auto& resSetting = GetResSettingForDisplay(persist_slot);
    auto& refSetting = GetRefreshSettingForDisplay(persist_slot);

    auto resolution_labels = display_cache::g_displayCache.GetResolutionLabels(actual_monitor_index);
    if (!resolution_labels.empty()) {
        // Find current selection index based on stored resolution values
        int current_selection_index = 0;  // Default to "Current Resolution"

        int stored_width = resSetting.GetWidth();
        int stored_height = resSetting.GetHeight();

        // If not current resolution (0,0), find matching index
        if (stored_width != 0 || stored_height != 0) {
            for (int i = 1; i < static_cast<int>(resolution_labels.size()); i++) {
                int width, height;
                // TODO simplify so we don't use sscanf_s
                if (sscanf_s(resolution_labels[i].c_str(), "%d x %d", &width, &height) == 2) {
                    if (width == stored_width && height == stored_height) {
                        current_selection_index = i;
                        break;
                    }
                }
            }
        }

        // Update the global index for compatibility
        s_selected_resolution_index.store(current_selection_index);

        ImGui::BeginGroup();
        ImGui::PushID("resolution_combo");
        if (ImGui::BeginCombo("Resolution", resolution_labels[current_selection_index].c_str())) {
            for (int i = 0; i < static_cast<int>(resolution_labels.size()); i++) {
                const bool is_selected = (i == current_selection_index);
                if (ImGui::Selectable(resolution_labels[i].c_str(), is_selected)) {
                    s_selected_resolution_index.store(static_cast<float>(i));
                    s_selected_refresh_rate_index.store(0);  // Reset refresh rate when resolution changes

                    if (i == 0) {
                        // Current resolution
                        resSetting.SetCurrentResolution();
                        refSetting.SetCurrentRefreshRate();
                    } else {
                        // Specific resolution
                        int width, height;
                        if (sscanf_s(resolution_labels[i].c_str(), "%d x %d", &width, &height) == 2) {
                            resSetting.SetResolution(width, height);
                            refSetting.SetCurrentRefreshRate();  // Reset to current refresh rate
                        }
                    }

                    if (s_auto_apply_resolution_change) {
                        // Disable auto-apply if a confirmation is pending
                        if (!g_has_pending_confirmation.load()) {
                            StartResolutionAutoApplyWithBackoff();
                        }
                    }
                }
                if (is_selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
        ImGui::PopID();
        ImGui::EndGroup();
    }
}

// Handle refresh rate selection UI
void HandleRefreshRateSelection(int selected_monitor_index, int selected_resolution_index) {
    EnsurePersistentSettingsLoadedOnce();
    if (s_selected_resolution_index >= 0) {
        // If Auto (Current) is selected, find the current monitor where the game is running
        int actual_monitor_index = selected_monitor_index;
        if (selected_monitor_index == 0) {
            HWND hwnd = g_last_swapchain_hwnd.load();
            if (hwnd) {
                HMONITOR current_monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
                if (current_monitor) {
                    // Find which monitor index this corresponds to in the display cache
                    for (int j = 0; j < static_cast<int>(display_cache::g_displayCache.GetDisplayCount()); j++) {
                        const auto* display = display_cache::g_displayCache.GetDisplay(j);
                        if (display && display->monitor_handle == current_monitor) {
                            actual_monitor_index = j;
                            break;
                        }
                    }
                }
            }
        } else {
            // Regular monitor selected - adjust index because monitors start at index 1 now
            actual_monitor_index = selected_monitor_index - 1;
        }

        // Determine persistence slot (0..3)
        int persist_slot = (actual_monitor_index < 0) ? 0 : ((actual_monitor_index > 3) ? 3 : actual_monitor_index);
        auto& refSetting = GetRefreshSettingForDisplay(persist_slot);

        auto resolution_labels = display_cache::g_displayCache.GetResolutionLabels(actual_monitor_index);
        if (s_selected_resolution_index < static_cast<int>(resolution_labels.size())) {
            auto refresh_rate_labels =
                display_cache::g_displayCache.GetRefreshRateLabels(actual_monitor_index, selected_resolution_index);
            if (!refresh_rate_labels.empty()) {
                // Find current selection index based on stored refresh rate values
                int current_refresh_index = 0;  // Default to "Current Refresh Rate"

                int stored_numerator = refSetting.GetNumerator();
                int stored_denominator = refSetting.GetDenominator();

                // If not current refresh rate (0,0), find matching index
                if (stored_numerator != 0 || stored_denominator != 0) {
                    for (int i = 1; i < static_cast<int>(refresh_rate_labels.size()); i++) {
                        // Parse refresh rate from label (e.g., "59.997 Hz")
                        double refresh_hz = 0.0;
                        if (sscanf_s(refresh_rate_labels[i].c_str(), "%lf Hz", &refresh_hz) == 1) {
                            // Convert to rational and compare (simple approximation)
                            // For now, use a simple conversion - in a real implementation, you'd want more precision
                            UINT32 approx_numerator = static_cast<UINT32>(std::round(refresh_hz * 1000));
                            UINT32 approx_denominator = 1000;
                            if (approx_numerator == stored_numerator && approx_denominator == stored_denominator) {
                                current_refresh_index = i;
                                break;
                            }
                        }
                    }
                }

                // Update the global index for compatibility
                s_selected_refresh_rate_index.store(current_refresh_index);

                ImGui::BeginGroup();
                ImGui::PushID("refresh_rate_combo");
                if (ImGui::BeginCombo("Refresh Rate", refresh_rate_labels[current_refresh_index].c_str())) {
                    for (int i = 0; i < static_cast<int>(refresh_rate_labels.size()); i++) {
                        const bool is_selected = (i == current_refresh_index);
                        if (ImGui::Selectable(refresh_rate_labels[i].c_str(), is_selected)) {
                            s_selected_refresh_rate_index.store(i);

                            if (i == 0) {
                                // Current refresh rate
                                refSetting.SetCurrentRefreshRate();
                            } else {
                                // Specific refresh rate
                                double refresh_hz = 0.0;
                                if (sscanf_s(refresh_rate_labels[i].c_str(), "%lf Hz", &refresh_hz) == 1) {
                                    // Simple conversion - in a real implementation, you'd want more precision
                                    UINT32 numerator = static_cast<UINT32>(std::round(refresh_hz * 1000));
                                    UINT32 denominator = 1000;
                                    refSetting.SetRefreshRate(numerator, denominator);
                                }
                            }

                            if (s_auto_apply_refresh_rate_change) {
                                if (!g_has_pending_confirmation.load()) {
                                    StartRefreshAutoApplyWithBackoff();
                                }
                            }
                        }
                        if (is_selected) {
                            ImGui::SetItemDefaultFocus();
                        }
                    }
                    ImGui::EndCombo();
                }
                ImGui::PopID();
                ImGui::EndGroup();
            }
        }
    }
}
// Handle auto-restore resolution checkbox
void HandleAutoRestoreResolutionCheckbox() {
    ImGui::Spacing();

    bool auto_restore_resolution_on_close = s_auto_restore_resolution_on_close.load();
    if (ImGui::Checkbox("Restore display settings when game closes (WIP)", &auto_restore_resolution_on_close)) {
        s_auto_restore_resolution_on_close.store(auto_restore_resolution_on_close);
        // Log the setting change
        if (auto_restore_resolution_on_close) {
            LogInfo("Restore display settings when game closes: ENABLED");
        } else {
            LogInfo("Restore display settings when game closes: DISABLED");
        }
    }

    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            "When enabled, automatically restores the original monitor resolution and refresh rate when the game "
            "closes.\nThis ensures your display settings return to normal after gaming sessions.");
    }
}

// Handle the "Apply with DXGI API" button
void HandleDXGIAPIApplyButton() {
    // DXGI API Button (Alternative Fractional Method)
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            "Uses DXGI SetFullscreenState + ResizeTarget to set fractional refresh rates.\nThis method "
            "creates a temporary swap chain to apply the mode.");
    }
    bool pending = g_has_pending_confirmation.load();
    if (pending) ImGui::BeginDisabled();
    bool clicked = ImGui::Button("Apply with DXGI API");
    if (pending) ImGui::EndDisabled();
    if (clicked) {
        if (g_has_pending_confirmation.load()) {
            // Ignore clicks while confirmation is pending
            return;
        }
        // Apply the changes using the DXGI API for fractional refresh rates
        std::thread([]() {
            // If Auto (Current) is selected, find the current monitor where the game is running
            int actual_monitor_index = s_selected_monitor_index.load();
            if (s_selected_monitor_index.load() == 0) {
                HWND hwnd = g_last_swapchain_hwnd.load();
                if (hwnd) {
                    HMONITOR current_monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
                    if (current_monitor) {
                        // Find which monitor index this corresponds to in the display cache
                        for (int j = 0; j < static_cast<int>(display_cache::g_displayCache.GetDisplayCount()); j++) {
                            const auto* display = display_cache::g_displayCache.GetDisplay(j);
                            if (display && display->monitor_handle == current_monitor) {
                                actual_monitor_index = j;
                                break;
                            }
                        }
                    }
                }
            } else {
                // Regular monitor selected - adjust index because monitors start at index 1 now
                actual_monitor_index = static_cast<int>(s_selected_monitor_index) - 1;
            }

            // Before applying any change, mark original mode and this device as changed
            display_restore::MarkOriginalForDisplayIndex(actual_monitor_index);
            display_restore::MarkDeviceChangedByDisplayIndex(actual_monitor_index);

            // Get the selected resolution from the cache
            auto resolution_labels = display_cache::g_displayCache.GetResolutionLabels(actual_monitor_index);
            if (s_selected_resolution_index >= 0
                && s_selected_resolution_index < static_cast<int>(resolution_labels.size())) {
                int width = 0, height = 0;
                if (static_cast<int>(s_selected_resolution_index) == 0) {
                    // Current Resolution
                    if (!display_cache::g_displayCache.GetCurrentResolution(actual_monitor_index, width, height)) {
                        return;
                    }
                } else {
                    std::string selected_resolution = resolution_labels[static_cast<int>(s_selected_resolution_index)];
                    if (sscanf_s(selected_resolution.c_str(), "%d x %d", &width, &height) != 2) {
                        return;
                    }
                }

                // Get the selected refresh rate from the cache
                display_cache::RationalRefreshRate refresh_rate;
                bool has_rational = false;

                // Use selected refresh rate from cache
                auto refresh_rate_labels = display_cache::g_displayCache.GetRefreshRateLabels(
                    actual_monitor_index, static_cast<int>(s_selected_resolution_index));
                if (s_selected_refresh_rate_index >= 0
                    && s_selected_refresh_rate_index < static_cast<int>(refresh_rate_labels.size())) {
                    // Get rational refresh rate values from the cache
                    has_rational = display_cache::g_displayCache.GetRationalRefreshRate(
                        actual_monitor_index, static_cast<int>(s_selected_resolution_index),
                        static_cast<int>(s_selected_refresh_rate_index), refresh_rate);
                }

                if (has_rational) {
                    // Log the values we're trying to apply with DXGI API
                    std::ostringstream debug_oss;
                    debug_oss << "Attempting to apply display changes with DXGI API: Monitor="
                              << s_selected_monitor_index << ", Resolution=" << width << "x" << height
                              << ", Refresh Rate=" << std::fixed << std::setprecision(10) << refresh_rate.ToHz() << "Hz"
                              << " (Rational: " << refresh_rate.numerator << "/" << refresh_rate.denominator << ")";
                    LogInfo(debug_oss.str().c_str());

                    // Try DXGI API for fractional refresh rates
                    bool dxgi_ok = resolution::ApplyDisplaySettingsDXGI(
                        actual_monitor_index, width, height, refresh_rate.numerator, refresh_rate.denominator);
                    if (dxgi_ok) {
                        // Start confirmation timer on success
                        std::ostringstream label;
                        label << width << "x" << height << " @ " << std::fixed << std::setprecision(3)
                              << refresh_rate.ToHz() << "Hz";
                        BeginConfirmationCountdown(actual_monitor_index, label.str(), 15);
                        std::ostringstream oss;
                        oss << "DXGI API SUCCESS: " << width << "x" << height << " @ " << std::fixed
                            << std::setprecision(10) << refresh_rate.ToHz() << "Hz"
                            << " (Exact fractional refresh rate applied via DXGI)";
                        LogInfo(oss.str().c_str());
                    } else {
                        // DXGI API failed, fall back to legacy API
                        LogWarn("DXGI API failed, falling back to legacy API");

                        // Get the last Windows error for debugging
                        DWORD error = GetLastError();
                        if (error != 0) {
                            LPSTR messageBuffer = nullptr;
                            size_t size = FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM
                                                             | FORMAT_MESSAGE_IGNORE_INSERTS,
                                                         NULL, error, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                                                         (LPSTR)&messageBuffer, 0, NULL);

                            if (size > 0) {
                                std::ostringstream error_oss;
                                error_oss << "DXGI API error details: " << std::string(messageBuffer, size);
                                LogWarn(error_oss.str().c_str());
                                LocalFree(messageBuffer);
                            }
                        }

                        // Fallback to legacy API
                        const auto* display = display_cache::g_displayCache.GetDisplay(actual_monitor_index);
                        if (display) {
                            HMONITOR hmon = display->monitor_handle;

                            MONITORINFOEXW mi;
                            mi.cbSize = sizeof(mi);
                            if (GetMonitorInfoW(hmon, &mi)) {
                                std::wstring device_name = mi.szDevice;

                                // Create DEVMODE structure with selected resolution and refresh rate
                                DEVMODEW dm;
                                dm.dmSize = sizeof(dm);
                                dm.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT | DM_DISPLAYFREQUENCY;
                                dm.dmPelsWidth = width;
                                dm.dmPelsHeight = height;

                                // Round the refresh rate to the nearest integer for DEVMODE fallback
                                dm.dmDisplayFrequency = static_cast<DWORD>(std::round(refresh_rate.ToHz()));

                                LogInfo("HandleDXGIAPIApplyButton() - ChangeDisplaySettingsExW_Direct: %S",
                                        device_name.c_str());
                                // Apply the changes using legacy API (direct, bypasses hooks)
                                LONG result = ChangeDisplaySettingsExW_Direct(device_name.c_str(), &dm, nullptr,
                                                                              CDS_UPDATEREGISTRY, nullptr);

                                if (result == DISP_CHANGE_SUCCESSFUL) {
                                    std::ostringstream label;
                                    label << width << "x" << height << " @ " << std::fixed << std::setprecision(3)
                                          << refresh_rate.ToHz() << "Hz";
                                    BeginConfirmationCountdown(actual_monitor_index, label.str(), 15);
                                    std::ostringstream oss;
                                    oss << "Legacy API fallback SUCCESS: " << width << "x" << height << " @ "
                                        << std::fixed << std::setprecision(3) << refresh_rate.ToHz() << "Hz"
                                        << " (Note: Refresh rate was rounded for compatibility)";
                                    LogInfo(oss.str().c_str());
                                } else {
                                    std::ostringstream oss;
                                    oss << "Legacy API fallback also failed. Error code: " << result;
                                    LogWarn(oss.str().c_str());
                                }
                            }
                        }
                    }
                } else {
                    LogWarn("Failed to get rational refresh rate from cache for DXGI API");
                }
            }
        }).detach();
    }
}
}  // namespace ui::monitor_settings
