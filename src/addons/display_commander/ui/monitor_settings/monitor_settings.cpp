#include "monitor_settings.hpp"
#include "../ui_display_tab.hpp"
#include "../../display_cache.hpp"
#include "../../resolution_helpers.hpp"
#include "../../utils.hpp"
#include <sstream>
#include <thread>
#include <iomanip>
#include <atomic>
#include <chrono>
#include <algorithm>
#include <deps/imgui/imgui.h>
// Track and restore original display settings
#include "../../display_restore.hpp"
#include "../new_ui/settings_wrapper.hpp"

// External declarations
extern std::atomic<HWND> g_last_swapchain_hwnd;
extern float s_selected_monitor_index;
extern float s_selected_resolution_index;
extern float s_selected_refresh_rate_index;
extern bool s_initial_auto_selection_done;
extern bool s_auto_restore_resolution_on_close;
extern bool s_auto_apply_resolution_change;
extern bool s_auto_apply_refresh_rate_change;


namespace renodx::ui::monitor_settings {

// Persistent settings for monitor settings UI
static renodx::ui::new_ui::BoolSetting g_setting_auto_apply_resolution("AutoApplyResolution", false);
static renodx::ui::new_ui::BoolSetting g_setting_auto_apply_refresh("AutoApplyRefresh", false);
static renodx::ui::new_ui::IntSetting g_setting_selected_resolution_index("SelectedResolutionIndex", 0, 0, 10000);
static renodx::ui::new_ui::IntSetting g_setting_selected_refresh_index("SelectedRefreshIndex", 0, 0, 10000);

static void EnsurePersistentSettingsLoadedOnce() {
    static bool loaded = false;
    if (loaded) return;
    loaded = true;
    g_setting_auto_apply_resolution.Load();
    g_setting_auto_apply_refresh.Load();
    g_setting_selected_resolution_index.Load();
    g_setting_selected_refresh_index.Load();
    s_auto_apply_resolution_change = g_setting_auto_apply_resolution.GetValue();
    s_auto_apply_refresh_rate_change = g_setting_auto_apply_refresh.GetValue();
    // Initialize selected indices from saved state
    s_selected_resolution_index = static_cast<float>(g_setting_selected_resolution_index.GetValue());
    s_selected_refresh_rate_index = static_cast<float>(g_setting_selected_refresh_index.GetValue());
}

// Auto-apply retry state
static std::atomic<uint64_t> g_resolution_apply_task_id{0};
static std::atomic<uint64_t> g_refresh_apply_task_id{0};
static std::atomic<bool> g_resolution_auto_apply_failed{false};
static std::atomic<bool> g_refresh_auto_apply_failed{false};

// Helper: Apply current selection (DXGI first, then legacy) and return success
static bool TryApplyCurrentSelectionOnce(int &out_monitor_index, int &out_width, int &out_height, renodx::display_cache::RationalRefreshRate &out_refresh_rate) {
    // Determine monitor index
    int actual_monitor_index = static_cast<int>(s_selected_monitor_index);
    if (s_selected_monitor_index == 0) {
        HWND hwnd = g_last_swapchain_hwnd.load();
        if (hwnd) {
            HMONITOR current_monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
            if (current_monitor) {
                for (int j = 0; j < static_cast<int>(renodx::display_cache::g_displayCache.GetDisplayCount()); j++) {
                    const auto* display = renodx::display_cache::g_displayCache.GetDisplay(j);
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
    renodx::display_restore::MarkOriginalForDisplayIndex(actual_monitor_index);
    renodx::display_restore::MarkDeviceChangedByDisplayIndex(actual_monitor_index);

    // Get width/height
    auto resolution_labels = renodx::display_cache::g_displayCache.GetResolutionLabels(actual_monitor_index);
    if (s_selected_resolution_index < 0 || s_selected_resolution_index >= static_cast<int>(resolution_labels.size())) {
        return false;
    }
    int width = 0;
    int height = 0;
    if (static_cast<int>(s_selected_resolution_index) == 0) {
        // Option 0: Current Resolution
        if (!renodx::display_cache::g_displayCache.GetCurrentResolution(actual_monitor_index, width, height)) {
            return false;
        }
    } else {
        std::string selected_resolution = resolution_labels[static_cast<int>(s_selected_resolution_index)];
        if (sscanf_s(selected_resolution.c_str(), "%d x %d", &width, &height) != 2) {
            return false;
        }
    }

    // Get rational refresh
    renodx::display_cache::RationalRefreshRate refresh_rate{};
    bool has_rational = renodx::display_cache::g_displayCache.GetRationalRefreshRate(
        actual_monitor_index,
        static_cast<int>(s_selected_resolution_index),
        static_cast<int>(s_selected_refresh_rate_index),
        refresh_rate);

    if (!has_rational) {
        return false;
    }

    // Try DXGI first
    if (renodx::resolution::ApplyDisplaySettingsDXGI(
            actual_monitor_index, width, height, refresh_rate.numerator, refresh_rate.denominator)) {
        out_monitor_index = actual_monitor_index;
        out_width = width;
        out_height = height;
        out_refresh_rate = refresh_rate;
        return true;
    }

    // Fallback: legacy ChangeDisplaySettingsExW
    const auto* display = renodx::display_cache::g_displayCache.GetDisplay(actual_monitor_index);
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

    LONG result = ChangeDisplaySettingsExW(mi.szDevice, &dm, nullptr, CDS_UPDATEREGISTRY, nullptr);
    if (result == DISP_CHANGE_SUCCESSFUL) {
        out_monitor_index = actual_monitor_index;
        out_width = width;
        out_height = height;
        out_refresh_rate = refresh_rate;
        return true;
    }
    return false;
}

// Forward declaration for confirmation countdown used below
static void BeginConfirmationCountdown(int display_index, const std::string &label, int seconds);

static void StartResolutionAutoApplyWithBackoff() {
    uint64_t task_id = g_resolution_apply_task_id.fetch_add(1) + 1;
    g_resolution_auto_apply_failed.store(false);
    std::thread([task_id]() {
        const int delays_sec[5] = {1, 2, 4, 8, 16};
        for (int i = 0; i < 5; ++i) {
            // Cancellation check
            if (g_resolution_apply_task_id.load() != task_id) return;
            int mon = -1, w = 0, h = 0; renodx::display_cache::RationalRefreshRate rr{};
            if (TryApplyCurrentSelectionOnce(mon, w, h, rr)) {
                std::ostringstream label; label << w << "x" << h << " @ " << std::fixed << std::setprecision(3) << rr.ToHz() << "Hz";
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
            int mon = -1, w = 0, h = 0; renodx::display_cache::RationalRefreshRate rr{};
            if (TryApplyCurrentSelectionOnce(mon, w, h, rr)) {
                std::ostringstream label; label << w << "x" << h << " @ " << std::fixed << std::setprecision(3) << rr.ToHz() << "Hz";
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
static int g_last_applied_display_index = -1; // cache index used when applying
static std::string g_last_applied_label;

static void BeginConfirmationCountdown(int display_index, const std::string &label, int seconds = 15) {
    g_last_applied_display_index = display_index;
    g_last_applied_label = label;
    g_confirm_seconds_remaining.store(seconds);
    g_has_pending_confirmation.store(true);
    uint64_t session_id = g_confirm_session_id.fetch_add(1) + 1;
    std::thread([session_id]() {
        while (g_has_pending_confirmation.load()) {
            if (g_confirm_session_id.load() != session_id) return; // superseded
            int remaining = g_confirm_seconds_remaining.load();
            if (remaining <= 0) {
                // Time up: auto-revert
                renodx::display_restore::RestoreDisplayByIndex(g_last_applied_display_index);
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
        renodx::display_restore::RestoreDisplayByIndex(g_last_applied_display_index);
        g_has_pending_confirmation.store(false);
    }
}

// Handle display cache refresh logic (every 60 frames)
void HandleDisplayCacheRefresh() {
    // Reset display cache every 60 frames to keep it fresh
    static int frame_counter = 0;
    frame_counter++;
    if (frame_counter >= 60) {
        renodx::display_cache::g_displayCache.Refresh();
        frame_counter = 0;
        
        // Debug: Log current display info after refresh
        HWND hwnd = g_last_swapchain_hwnd.load();
        if (hwnd) {
            HMONITOR current_monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
            if (current_monitor) {
                const auto* display = renodx::display_cache::g_displayCache.GetDisplayByHandle(current_monitor);
                if (display) {
                    std::ostringstream debug_oss;
                    debug_oss << "Cache refreshed - Current: " << display->GetCurrentResolutionString() 
                              << " @ " << display->GetCurrentRefreshRateString()
                              << " [Raw: " << display->current_refresh_rate.numerator << "/" 
                              << display->current_refresh_rate.denominator << "]";
                    LogInfo(debug_oss.str().c_str());
                }
            }
        }
    }
}

// Handle auto-detection of current display settings
void HandleAutoDetection() {
    if (!s_initial_auto_selection_done) {
        s_initial_auto_selection_done = true;
        
        // Get current display info for the selected monitor
        HWND hwnd = g_last_swapchain_hwnd.load();
        if (hwnd) {
            HMONITOR current_monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
            if (current_monitor) {
                                // Get monitor labels to find which monitor index this corresponds to
                auto monitor_labels = renodx::ui::GetMonitorLabelsFromCache();
                
                // Find which monitor index this corresponds to
                for (int i = 0; i < static_cast<int>(monitor_labels.size()); i++) {
                    const auto* display = renodx::display_cache::g_displayCache.GetDisplay(i);
                    if (display && display->monitor_handle == current_monitor) {
                        // Set to Auto (Current) since that's where this monitor will be
                        s_selected_monitor_index = 0.0f;
                        
                        // Use the new methods to find closest supported modes to current settings
                        auto closest_resolution_index = display->FindClosestResolutionIndex();
                        if (closest_resolution_index.has_value()) {
                            s_selected_resolution_index = static_cast<float>(closest_resolution_index.value());
                            
                            // Find closest refresh rate within this resolution
                            // Default to Current Refresh Rate (index 0 in UI)
                            s_selected_refresh_rate_index = 0.0f;
                            std::ostringstream found_oss;
                            found_oss << "Auto-detected: Resolution " << s_selected_resolution_index 
                                      << " (closest to current " << display->GetCurrentResolutionString() 
                                      << "), Refresh Rate 0 (Current: " << display->GetCurrentRefreshRateString() << ")";
                            LogInfo(found_oss.str().c_str());
                        } else {
                            // Fallback to first resolution if no match found
                            s_selected_resolution_index = 0.0f;
                            s_selected_refresh_rate_index = 0.0f;
                            LogWarn("No resolution match found, using first available");
                        }
                        break;
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
        const auto* display = renodx::display_cache::g_displayCache.GetDisplay(static_cast<int>(s_selected_monitor_index));
        if (display && display->monitor_handle) {
            MONITORINFOEXW mi;
            mi.cbSize = sizeof(mi);
            if (GetMonitorInfoW(display->monitor_handle, &mi)) {
                if (mi.dwFlags & MONITORINFOF_PRIMARY) {
                    monitor_label = "Monitor (Primary)";
                }
            }
        }
    }
    
    // Monitor selection
    if (ImGui::BeginCombo(monitor_label.c_str(), monitor_labels[static_cast<int>(s_selected_monitor_index)].c_str())) {
        for (int i = 0; i < static_cast<int>(monitor_labels.size()); i++) {
            const bool is_selected = (i == static_cast<int>(s_selected_monitor_index));
            if (ImGui::Selectable(monitor_labels[i].c_str(), is_selected)) {
                s_selected_monitor_index = static_cast<float>(i);
                
                if (i == 0) {
                    // Auto (Current) option selected - find the current monitor where the game is running
                    HWND hwnd = g_last_swapchain_hwnd.load();
                    if (hwnd) {
                        HMONITOR current_monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
                        if (current_monitor) {
                            // Find which monitor index this corresponds to in the display cache
                            for (int j = 0; j < static_cast<int>(renodx::display_cache::g_displayCache.GetDisplayCount()); j++) {
                                const auto* display = renodx::display_cache::g_displayCache.GetDisplay(j);
                                if (display && display->monitor_handle == current_monitor) {
                                    // Use this monitor for resolution and refresh rate selection
                                    auto closest_resolution_index = display->FindClosestResolutionIndex();
                                    if (closest_resolution_index.has_value()) {
                                        s_selected_resolution_index = static_cast<float>(closest_resolution_index.value());
                                        
                                        // Default to Current Refresh Rate (index 0 in UI)
                                        s_selected_refresh_rate_index = 0.0f;
                                        std::ostringstream auto_select_oss;
                                        auto_select_oss << "Auto (Current) selected - using monitor " << j << ": Resolution " 
                                                       << s_selected_resolution_index << " (closest to current " 
                                                       << display->GetCurrentResolutionString() << "), Refresh Rate 0 (Current: " 
                                                       << display->GetCurrentRefreshRateString() << ")";
                                        LogInfo(auto_select_oss.str().c_str());
                                    } else {
                                        s_selected_resolution_index = 0.0f;
                                        s_selected_refresh_rate_index = 0.0f;
                                        LogWarn("No resolution match found for Auto (Current), using first available");
                                    }
                                    break;
                                }
                            }
                        }
                    }
                } else {
                    // Regular monitor selected - auto-select closest resolution and refresh rate for the newly selected monitor
                    const auto* display = renodx::display_cache::g_displayCache.GetDisplay(i - 1); // -1 because monitors start at index 1 now
                    if (display) {
                        // Find closest supported resolution to current settings
                        auto closest_resolution_index = display->FindClosestResolutionIndex();
                        if (closest_resolution_index.has_value()) {
                            s_selected_resolution_index = static_cast<float>(closest_resolution_index.value());
                            
                            // Find closest refresh rate within this resolution
                            // Default to Current Refresh Rate (index 0 in UI)
                            s_selected_refresh_rate_index = 0.0f;
                            std::ostringstream auto_select_oss;
                            auto_select_oss << "Auto-selected for monitor " << (i - 1) << ": Resolution " 
                                           << s_selected_resolution_index << " (closest to current " 
                                           << display->GetCurrentResolutionString() << "), Refresh Rate 0 (Current: " 
                                           << display->GetCurrentRefreshRateString() << ")";
                            LogInfo(auto_select_oss.str().c_str());
                        } else {
                            // Fallback to first resolution if no match found
                            s_selected_resolution_index = 0.0f;
                            s_selected_refresh_rate_index = 0.0f;
                            LogWarn("No resolution match found for auto-selection, using first available");
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
                for (int j = 0; j < static_cast<int>(renodx::display_cache::g_displayCache.GetDisplayCount()); j++) {
                    const auto* display = renodx::display_cache::g_displayCache.GetDisplay(j);
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
    
    auto resolution_labels = renodx::display_cache::g_displayCache.GetResolutionLabels(actual_monitor_index);
    if (!resolution_labels.empty()) {
        // Clamp saved index to available range
        if (s_selected_resolution_index < 0 || s_selected_resolution_index >= static_cast<int>(resolution_labels.size())) {
            s_selected_resolution_index = 0.0f;
            g_setting_selected_resolution_index.SetValue(0);
            g_setting_selected_resolution_index.Save();
        }
        ImGui::BeginGroup();
        ImGui::PushID("resolution_combo");
        if (ImGui::BeginCombo("Resolution", resolution_labels[static_cast<int>(s_selected_resolution_index)].c_str())) {
            for (int i = 0; i < static_cast<int>(resolution_labels.size()); i++) {
                const bool is_selected = (i == static_cast<int>(s_selected_resolution_index));
                if (ImGui::Selectable(resolution_labels[i].c_str(), is_selected)) {
                    s_selected_resolution_index = static_cast<float>(i);
                    s_selected_refresh_rate_index = 0.f; // Reset refresh rate when resolution changes
                    g_setting_selected_resolution_index.SetValue(i);
                    g_setting_selected_resolution_index.Save();
                    g_setting_selected_refresh_index.SetValue(0);
                    g_setting_selected_refresh_index.Save();
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
        ImGui::SameLine();
        if (ImGui::Checkbox("Auto-apply##resolution", &s_auto_apply_resolution_change)) {
            g_setting_auto_apply_resolution.SetValue(s_auto_apply_resolution_change);
            g_setting_auto_apply_resolution.Save();
            if (s_auto_apply_resolution_change) {
                if (!g_has_pending_confirmation.load()) {
                    StartResolutionAutoApplyWithBackoff();
                }
            } else {
                g_resolution_apply_task_id.fetch_add(1);
            }
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Automatically apply when Resolution changes");
        }
        if (g_resolution_auto_apply_failed.load()) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1.0f, 0.2f, 0.2f, 1.0f), "Auto-apply failed after retries");
        }
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
                    for (int j = 0; j < static_cast<int>(renodx::display_cache::g_displayCache.GetDisplayCount()); j++) {
                        const auto* display = renodx::display_cache::g_displayCache.GetDisplay(j);
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
        
        auto resolution_labels = renodx::display_cache::g_displayCache.GetResolutionLabels(actual_monitor_index);
        if (s_selected_resolution_index < static_cast<int>(resolution_labels.size())) {
            auto refresh_rate_labels = renodx::display_cache::g_displayCache.GetRefreshRateLabels(actual_monitor_index, selected_resolution_index);
            if (!refresh_rate_labels.empty()) {
                // Clamp saved index to available range
                if (s_selected_refresh_rate_index < 0 || s_selected_refresh_rate_index >= static_cast<int>(refresh_rate_labels.size())) {
                    s_selected_refresh_rate_index = 0.0f;
                    g_setting_selected_refresh_index.SetValue(0);
                    g_setting_selected_refresh_index.Save();
                }
                ImGui::BeginGroup();
                ImGui::PushID("refresh_rate_combo");
                if (ImGui::BeginCombo("Refresh Rate", refresh_rate_labels[static_cast<int>(s_selected_refresh_rate_index)].c_str())) {
                    for (int i = 0; i < static_cast<int>(refresh_rate_labels.size()); i++) {
                        const bool is_selected = (i == static_cast<int>(s_selected_refresh_rate_index));
                        if (ImGui::Selectable(refresh_rate_labels[i].c_str(), is_selected)) {
                            s_selected_refresh_rate_index = static_cast<float>(i);
                            g_setting_selected_refresh_index.SetValue(i);
                            g_setting_selected_refresh_index.Save();
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
                ImGui::SameLine();
                if (ImGui::Checkbox("Auto-apply##refresh", &s_auto_apply_refresh_rate_change)) {
                    g_setting_auto_apply_refresh.SetValue(s_auto_apply_refresh_rate_change);
                    g_setting_auto_apply_refresh.Save();
                    if (s_auto_apply_refresh_rate_change) {
                        if (!g_has_pending_confirmation.load()) {
                            StartRefreshAutoApplyWithBackoff();
                        }
                    } else {
                        g_refresh_apply_task_id.fetch_add(1);
                    }
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Automatically apply when Refresh Rate changes");
                }
                if (g_refresh_auto_apply_failed.load()) {
                    ImGui::SameLine();
                    ImGui::TextColored(ImVec4(1.0f, 0.2f, 0.2f, 1.0f), "Auto-apply failed after retries");
                }
                ImGui::EndGroup();
            }
        }
    }
}

// Handle auto-restore resolution checkbox
void HandleAutoRestoreResolutionCheckbox() {
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    
    if (ImGui::Checkbox("Restore display settings when game closes", &s_auto_restore_resolution_on_close)) {
        // Log the setting change
        if (s_auto_restore_resolution_on_close) {
            LogInfo("Restore display settings when game closes: ENABLED");
        } else {
            LogInfo("Restore display settings when game closes: DISABLED");
        }
    }
    
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("When enabled, automatically restores the original monitor resolution and refresh rate when the game closes.\nThis ensures your display settings return to normal after gaming sessions.");
    }
}

// Handle auto-apply resolution and refresh rate checkboxes
void HandleAutoApplyCheckboxes() {
    ImGui::Spacing();
    
    if (ImGui::Checkbox("Auto-apply resolution changes (TODO: implement)", &s_auto_apply_resolution_change)) {
        // Log the setting change
        if (s_auto_apply_resolution_change) {
            LogInfo("Auto-apply resolution changes: ENABLED");
        } else {
            LogInfo("Auto-apply resolution changes: DISABLED");
        }
    }
    
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("When enabled, automatically applies resolution changes immediately when a new resolution is selected.\nThis provides instant feedback without needing to click the apply button.");
    }
    
    if (ImGui::Checkbox("Auto-apply refresh rate changes (TODO: implement)", &s_auto_apply_refresh_rate_change)) {
        // Log the setting change
        if (s_auto_apply_refresh_rate_change) {
            LogInfo("Auto-apply refresh rate changes: ENABLED");
        } else {
            LogInfo("Auto-apply refresh rate changes: DISABLED");
        }
    }
    
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("When enabled, automatically applies refresh rate changes immediately when a new refresh rate is selected.\nThis provides instant feedback without needing to click the apply button.");
    }
}

// Handle the "Apply with DXGI API" button
void HandleDXGIAPIApplyButton() {
    // DXGI API Button (Alternative Fractional Method)
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Uses DXGI SetFullscreenState + ResizeTarget to set fractional refresh rates.\nThis method creates a temporary swap chain to apply the mode.");
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
        std::thread([](){
            // If Auto (Current) is selected, find the current monitor where the game is running
            int actual_monitor_index = static_cast<int>(s_selected_monitor_index);
            if (s_selected_monitor_index == 0) {
                HWND hwnd = g_last_swapchain_hwnd.load();
                if (hwnd) {
                    HMONITOR current_monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
                    if (current_monitor) {
                        // Find which monitor index this corresponds to in the display cache
                        for (int j = 0; j < static_cast<int>(renodx::display_cache::g_displayCache.GetDisplayCount()); j++) {
                            const auto* display = renodx::display_cache::g_displayCache.GetDisplay(j);
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
            renodx::display_restore::MarkOriginalForDisplayIndex(actual_monitor_index);
            renodx::display_restore::MarkDeviceChangedByDisplayIndex(actual_monitor_index);

            // Get the selected resolution from the cache
            auto resolution_labels = renodx::display_cache::g_displayCache.GetResolutionLabels(actual_monitor_index);
            if (s_selected_resolution_index >= 0 && s_selected_resolution_index < static_cast<int>(resolution_labels.size())) {
                int width = 0, height = 0;
                if (static_cast<int>(s_selected_resolution_index) == 0) {
                    // Current Resolution
                    if (!renodx::display_cache::g_displayCache.GetCurrentResolution(actual_monitor_index, width, height)) {
                        return;
                    }
                } else {
                    std::string selected_resolution = resolution_labels[static_cast<int>(s_selected_resolution_index)];
                    if (sscanf_s(selected_resolution.c_str(), "%d x %d", &width, &height) != 2) {
                        return;
                    }
                }
                
                // Get the selected refresh rate from the cache
                renodx::display_cache::RationalRefreshRate refresh_rate;
                bool has_rational = false;
                
                // Use selected refresh rate from cache
                auto refresh_rate_labels = renodx::display_cache::g_displayCache.GetRefreshRateLabels(actual_monitor_index, static_cast<int>(s_selected_resolution_index));
                if (s_selected_refresh_rate_index >= 0 && s_selected_refresh_rate_index < static_cast<int>(refresh_rate_labels.size())) {
                    // Get rational refresh rate values from the cache
                    has_rational = renodx::display_cache::g_displayCache.GetRationalRefreshRate(
                        actual_monitor_index, 
                        static_cast<int>(s_selected_resolution_index),
                        static_cast<int>(s_selected_refresh_rate_index), 
                        refresh_rate);
                }
                
                if (has_rational) {
                    // Log the values we're trying to apply with DXGI API
                    std::ostringstream debug_oss;
                    debug_oss << "Attempting to apply display changes with DXGI API: Monitor=" << s_selected_monitor_index 
                              << ", Resolution=" << width << "x" << height 
                              << ", Refresh Rate=" << std::fixed << std::setprecision(10) << refresh_rate.ToHz() << "Hz"
                              << " (Rational: " << refresh_rate.numerator << "/" << refresh_rate.denominator << ")";
                    LogInfo(debug_oss.str().c_str());
                    
                    // Try DXGI API for fractional refresh rates
                    bool dxgi_ok = renodx::resolution::ApplyDisplaySettingsDXGI(
                        actual_monitor_index,
                        width, height,
                        refresh_rate.numerator, refresh_rate.denominator);
                    if (dxgi_ok) {
                        // Start confirmation timer on success
                        std::ostringstream label;
                        label << width << "x" << height << " @ " << std::fixed << std::setprecision(3) << refresh_rate.ToHz() << "Hz";
                        BeginConfirmationCountdown(actual_monitor_index, label.str(), 15);
                        std::ostringstream oss;
                        oss << "DXGI API SUCCESS: " << width << "x" << height 
                            << " @ " << std::fixed << std::setprecision(10) << refresh_rate.ToHz() << "Hz"
                            << " (Exact fractional refresh rate applied via DXGI)";
                        LogInfo(oss.str().c_str());
                    } else {
                        // DXGI API failed, fall back to legacy API
                        LogWarn("DXGI API failed, falling back to legacy API");
                        
                        // Get the last Windows error for debugging
                        DWORD error = GetLastError();
                        if (error != 0) {
                            LPSTR messageBuffer = nullptr;
                            size_t size = FormatMessageA(
                                FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                                NULL, error, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPSTR)&messageBuffer, 0, NULL);
                            
                            if (size > 0) {
                                std::ostringstream error_oss;
                                error_oss << "DXGI API error details: " << std::string(messageBuffer, size);
                                LogWarn(error_oss.str().c_str());
                                LocalFree(messageBuffer);
                            }
                        }
                        
                        // Fallback to legacy API
                        const auto* display = renodx::display_cache::g_displayCache.GetDisplay(actual_monitor_index);
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
                                
                                // Apply the changes using legacy API
                                LONG result = ChangeDisplaySettingsExW(device_name.c_str(), &dm, nullptr, CDS_UPDATEREGISTRY, nullptr);
                                
                                if (result == DISP_CHANGE_SUCCESSFUL) {
                                    std::ostringstream label;
                                    label << width << "x" << height << " @ " << std::fixed << std::setprecision(3) << refresh_rate.ToHz() << "Hz";
                                    BeginConfirmationCountdown(actual_monitor_index, label.str(), 15);
                                    std::ostringstream oss;
                                    oss << "Legacy API fallback SUCCESS: " << width << "x" << height 
                                        << " @ " << std::fixed << std::setprecision(3) << refresh_rate.ToHz() << "Hz"
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
} // namespace renodx::ui::monitor_settings
