#include "window_management.hpp"
#include "../addon.hpp"
#include "../display_cache.hpp"
#include "../globals.hpp"
#include "../utils/general_utils.hpp"
#include "../utils/logging.hpp"
#include "../settings/main_tab_settings.hpp"
#include "../settings/developer_tab_settings.hpp"
#include "../ui/ui_display_tab.hpp"

#include <sstream>

// Forward declaration
void ComputeDesiredSize(int display_width, int display_height, int& out_w, int& out_h);

// First function: Calculate and update global window state
void CalculateWindowState(HWND hwnd, const char* reason) {
    if (hwnd == nullptr) return;

    // First, determine the target monitor using display cache (no
    // FindTargetMonitor / MONITORINFOEXW)
    if (!display_cache::g_displayCache.IsInitialized()) {
        display_cache::g_displayCache.Initialize();
    }

    // Build a local snapshot to avoid readers observing partial state
    GlobalWindowState local_state;
    local_state.reason = reason;

    // Get current styles
    local_state.current_style = GetWindowLongPtrW(hwnd, GWL_STYLE);
    local_state.current_ex_style = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);

    // Calculate new borderless styles using the shared helper function
    local_state.new_style = local_state.current_style;
    local_state.new_ex_style = local_state.current_ex_style;

    // Apply window style modifications using the shared helper function
    ModifyWindowStyle(GWL_STYLE, local_state.new_style, settings::g_developerTabSettings.prevent_always_on_top.GetValue());
    ModifyWindowStyle(GWL_EXSTYLE, local_state.new_ex_style, settings::g_developerTabSettings.prevent_always_on_top.GetValue());

    if (local_state.current_style != local_state.new_style) {
        local_state.style_changed = true;
    }
    if (local_state.current_ex_style != local_state.new_ex_style) {
        local_state.style_changed_ex = true;
    }

    // Get current window state
    RECT wr_current{};
    GetWindowRect(hwnd, &wr_current);

    // Detect window state (maximized, minimized, restored)
    WINDOWPLACEMENT wp = {sizeof(WINDOWPLACEMENT)};
    if (GetWindowPlacement(hwnd, &wp) != FALSE) {
        local_state.show_cmd = wp.showCmd;
    }

    local_state.style_mode = WindowStyleMode::BORDERLESS;

    HMONITOR target_monitor_handle = nullptr;
    int target_display_index = 0;
    auto displays = display_cache::g_displayCache.GetDisplays();
    size_t display_count = displays ? displays->size() : 0;

    // Use device ID-based approach for better reliability
    std::string selected_device_id = settings::g_mainTabSettings.selected_extended_display_device_id.GetValue();
    if (!selected_device_id.empty() && selected_device_id != "No Window" && selected_device_id != "No Monitor"
        && selected_device_id != "Monitor Info Failed") {
        // Find monitor by device ID
        target_display_index = ui::FindMonitorIndexByDeviceId(selected_device_id);
        if (target_display_index >= 0 && target_display_index < static_cast<int>(display_count)
            && (*displays)[target_display_index]) {
            const auto* disp = (*displays)[target_display_index].get();
            target_monitor_handle = disp->monitor_handle;
        } else {
            // Device ID not found, fall back to current window monitor
            target_monitor_handle = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
            // Find the corresponding display index in the cache
            for (size_t i = 0; i < display_count; ++i) {
                if ((*displays)[i] && (*displays)[i]->monitor_handle == target_monitor_handle) {
                    target_display_index = static_cast<int>(i);
                    break;
                }
            }
        }
    } else {
        // Fallback: use the monitor where the window currently resides
        target_monitor_handle = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
        // Find the corresponding display index in the cache
        for (size_t i = 0; i < display_count; ++i) {
            if ((*displays)[i] && (*displays)[i]->monitor_handle == target_monitor_handle) {
                target_display_index = static_cast<int>(i);
                break;
            }
        }
    }

    // Get current refresh rate from cache
    display_cache::RationalRefreshRate tmp_refresh;
    if (target_display_index < static_cast<int>(display_count) && (*displays)[target_display_index]) {
        const auto* disp = (*displays)[target_display_index].get();
        tmp_refresh = disp->current_refresh_rate;
        local_state.current_monitor_index = target_display_index;
        local_state.current_monitor_refresh_rate = tmp_refresh;

        const int display_width = disp->width;
        const int display_height = disp->height;

        if (local_state.desired_width > display_width) {
            std::ostringstream oss;
            oss << "CalculateWindowState: Desired width " << local_state.desired_width << " exceeds monitor width "
                << display_width << ", clamping";
            LogInfo(oss.str().c_str());
            local_state.desired_width = display_width;
        }
        // Get desired dimensions and position from global settings
        // Use manual or aspect ratio mode
        ComputeDesiredSize(display_width, display_height, local_state.desired_width, local_state.desired_height);

        if (local_state.desired_height > display_height) {
            std::ostringstream oss;
            oss << "CalculateWindowState: Desired height " << local_state.desired_height << " exceeds monitor height "
                << display_height << ", clamping";
            LogInfo(oss.str().c_str());
            local_state.desired_height = display_height;
        }

        // Calculate target dimensions
        RECT client_rect = RectFromWH(local_state.desired_width, local_state.desired_height);
        if (AdjustWindowRectEx(&client_rect, static_cast<DWORD>(local_state.new_style), FALSE,
                               static_cast<DWORD>(local_state.new_ex_style))
            == FALSE) {
            LogWarn("AdjustWindowRectEx failed for CalculateWindowState.");
            return;
        }
        local_state.target_w = client_rect.right - client_rect.left;
        local_state.target_h = client_rect.bottom - client_rect.top;

        // Calculate target position - start with monitor top-left
        local_state.target_x = disp->x;
        local_state.target_y = disp->y;

        const RECT mr = {disp->x, disp->y, disp->x + disp->width, disp->y + disp->height};

        // Apply alignment based on setting
        switch (s_window_alignment.load()) {
            default:
            case WindowAlignment::kCenter:  // Default to center
                local_state.target_x = max(mr.left, mr.left + ((mr.right - mr.left - local_state.target_w) / 2));
                local_state.target_y = max(mr.top, mr.top + ((mr.bottom - mr.top - local_state.target_h) / 2));
                break;
            case WindowAlignment::kTopLeft:
                local_state.target_x = mr.left;
                local_state.target_y = mr.top;
                break;
            case WindowAlignment::kTopRight:
                local_state.target_x = max(mr.left, mr.right - local_state.target_w);
                local_state.target_y = mr.top;
                break;
            case WindowAlignment::kBottomLeft:
                local_state.target_x = mr.left;
                local_state.target_y = max(mr.top, mr.bottom - local_state.target_h);
                break;
            case WindowAlignment::kBottomRight:
                local_state.target_x = max(mr.left, mr.right - local_state.target_w);
                local_state.target_y = max(mr.top, mr.bottom - local_state.target_h);
                break;
        }
        local_state.target_w = min(local_state.target_w, mr.right - mr.left);
        local_state.target_h = min(local_state.target_h, mr.bottom - mr.top);

        // Check if any changes are actually needed
        local_state.wr_current = wr_current;
        local_state.needs_resize = (local_state.target_w != (wr_current.right - wr_current.left))
                                   || (local_state.target_h != (wr_current.bottom - wr_current.top));
        local_state.needs_move = (local_state.target_x != wr_current.left) || (local_state.target_y != wr_current.top);

        // Store current monitor dimensions
        local_state.display_width = display_width;
        local_state.display_height = display_height;

        // LogDebug("CalculateWindowState: target_w=%d, target_h=%d", local_state.target_w, local_state.target_h);

        // Publish snapshot under a lightweight lock
        g_window_state.store(std::make_shared<GlobalWindowState>(local_state));
    }
}

// Second function: Apply the calculated window changes
void ApplyWindowChange(HWND hwnd, const char* reason, bool force_apply) {
    if (hwnd == nullptr) {
        LogWarn("ApplyWindowChange: Null window handle provided");
        return;
    }

    // Validate window handle
    if (IsWindow(hwnd) == FALSE) {
        LogWarn("ApplyWindowChange: Invalid window handle 0x%p", hwnd);
        return;
    }

    // Log suppression hint once (only if setting is not already enabled)
    static bool suppression_hint_logged = false;
    if (!suppression_hint_logged && !settings::g_developerTabSettings.suppress_window_changes.GetValue()) {
        LogInfo("To suppress ApplyWindowChange, set SuppressWindowChanges=1 in [DisplayCommander] section of DisplayCommander.ini");
        suppression_hint_logged = true;
    }

    // First calculate the desired window state
    CalculateWindowState(hwnd, reason);

    // Copy the calculated state into a local snapshot for consistent use
    auto window_state = g_window_state.load();
    if (window_state) {
        auto s = *window_state;

        if (s.show_cmd == SW_SHOWMAXIMIZED) {
            ShowWindow(hwnd, SW_RESTORE);
            return;
        }

        if (s.style_changed) {
            LogDebug("ApplyWindowChange: Setting new style %d -> %d", s.current_style, s.new_style);
            // print error
            auto result = SetWindowLongPtrW(hwnd, GWL_STYLE, s.new_style);
            if (result == 0) {
                LogError("ApplyWindowChange: SetWindowLongPtrW failed with error %lu (0x%lx)", GetLastError(), GetLastError());
            }
        }
        if (s.style_changed_ex) {
            LogDebug("ApplyWindowChange: Setting new ex style %d -> %d", s.current_ex_style, s.new_ex_style);
            auto result = SetWindowLongPtrW(hwnd, GWL_EXSTYLE, s.new_ex_style);
            if (result == 0) {
                LogError("ApplyWindowChange: SetWindowLongPtrW failed with error %lu (0x%lx)", GetLastError(), GetLastError());
            }
        }

        if ((s.style_changed || s.style_changed_ex) && !s.needs_resize && !s.needs_move) {
            SetWindowPos(hwnd, nullptr, 0, 0, 0, 0, SWP_NOSIZE | SWP_NOMOVE | SWP_FRAMECHANGED);
        } else if (s.needs_resize || s.needs_move) {
            if (s.target_w <= 16 || s.target_h <= 16) {
                std::ostringstream oss;
                oss << "ApplyWindowChange: Invalid target size " << s.target_w << "x" << s.target_h << ", skipping";
                LogWarn(oss.str().c_str());
                return;
            }

            UINT flags = SWP_NOZORDER | SWP_NOOWNERZORDER;

            // Apply all changes in a single SetWindowPos call
            if (!s.needs_resize) flags |= SWP_NOSIZE;
            if (!s.needs_move) flags |= SWP_NOMOVE;
            if (s.style_changed || s.style_changed_ex) flags |= SWP_FRAMECHANGED;

            float scaling_percentage_width = 1.0f;
            float scaling_percentage_height = 1.0f;
            {
                const auto* disp = display_cache::g_displayCache.GetDisplay(s.current_monitor_index);
                if (disp != nullptr) {
                    float dpi = disp->GetDpiScaling();
                    LogInfo("ApplyWindowChange: Setting window position and size, target_x: %d, target_y: %d, target_w: %d, target_h: %d, dpi: %f", s.target_x, s.target_y, s.target_w, s.target_h,
                        dpi);
                }

                // Print DPI using GetDpiForWindow with g_last_swapchain_hwnd
                HWND swapchain_hwnd = g_last_swapchain_hwnd.load();
                if (swapchain_hwnd != nullptr) {
                    UINT dpi = GetDpiForWindow(swapchain_hwnd);
                    LogInfo("ApplyWindowChange: Window DPI from GetDpiForWindow: %u", dpi);
                } else {
                    LogInfo("ApplyWindowChange: g_last_swapchain_hwnd is null, cannot get DPI");
                }
                // Method 2: Modern system DPI (Windows 10+)
                UINT modern_system_dpi = GetDpiForSystem();
                if (modern_system_dpi > 0) {
                    float modern_scaling = (static_cast<float>(modern_system_dpi) / 96.0f) * 100.0f;
                    LogInfo("ApplyWindowChange: Modern System DPI - DPI: %u, Scaling: %.0f%%",
                           modern_system_dpi, modern_scaling);
                }
                // Query Windows display scaling settings
                // Method 1: System DPI using GetDeviceCaps (works on all Windows versions)
                HDC hdc = GetDC(swapchain_hwnd);
                if (hdc != nullptr) {
                    int system_dpi_x = GetDeviceCaps(hdc, LOGPIXELSX);
                    int system_dpi_y = GetDeviceCaps(hdc, LOGPIXELSY);

                    // Get virtual resolution (logical resolution before DPI scaling)
                    int virtual_width = GetDeviceCaps(hdc, HORZRES);
                    int virtual_height = GetDeviceCaps(hdc, VERTRES);

                    // Get physical resolution (actual pixel resolution)
                    int physical_width = GetDeviceCaps(hdc, DESKTOPHORZRES);
                    int physical_height = GetDeviceCaps(hdc, DESKTOPVERTRES);

                    ReleaseDC(nullptr, hdc);

                    // Prevent division by zero
                    if (physical_width > 0 && physical_height > 0) {
                        scaling_percentage_width = static_cast<float>(virtual_width) / static_cast<float>(physical_width);
                        scaling_percentage_height = static_cast<float>(virtual_height) / static_cast<float>(physical_height);
                    } else {
                        LogWarn("ApplyWindowChange: Invalid physical resolution %dx%d, using default scaling", physical_width, physical_height);
                        scaling_percentage_width = 1.0f;
                        scaling_percentage_height = 1.0f;
                    }
                    LogInfo("ApplyWindowChange: Windows Display Scaling - Width: %.2f%%, Height: %.2f%%",
                           100.0f * scaling_percentage_width, 100.0f * scaling_percentage_height);
                    float scaling_percentage = (static_cast<float>(system_dpi_x) / 96.0f) * 100.0f;
                    LogInfo("ApplyWindowChange: Windows Display Scaling - DPI: %d, Scaling: %.0f%%",
                           system_dpi_x, scaling_percentage);
                    // local_state.wr_current
                    LogInfo("ApplyWindowChange: width %d -> %d height %d -> %d x: %d -> %d y: %d -> %d",
                           s.wr_current.right - s.wr_current.left, s.target_w, s.wr_current.bottom - s.wr_current.top, s.target_h,
                           s.wr_current.left, s.target_x, s.wr_current.top, s.target_y);
                    LogInfo("ApplyWindowChange: Virtual Resolution: %dx%d, Physical Resolution: %dx%d",
                           virtual_width, virtual_height, physical_width, physical_height);

                }
            }

            // Calculate final dimensions with scaling
            int final_width = static_cast<int>(round(s.target_w * scaling_percentage_width));
            int final_height = static_cast<int>(round(s.target_h * scaling_percentage_height));

            // Validate parameters before SetWindowPos call
            if (final_width <= 0 || final_height <= 0) {
                LogWarn("ApplyWindowChange: Invalid calculated dimensions %dx%d, skipping SetWindowPos", final_width, final_height);
                return;
            }

            if (s.target_x < -32768 || s.target_x > 32767 || s.target_y < -32768 || s.target_y > 32767) {
                LogWarn("ApplyWindowChange: Invalid coordinates (%d, %d), skipping SetWindowPos", s.target_x, s.target_y);
                return;
            }

            // Validate window handle
            if (IsWindow(hwnd) == FALSE) {
                LogWarn("ApplyWindowChange: Invalid window handle 0x%p, skipping SetWindowPos", hwnd);
                return;
            }

            LogDebug("ApplyWindowChange: Calling SetWindowPos with x=%d, y=%d, w=%d, h=%d, flags=0x%x",
                     s.target_x, s.target_y, final_width, final_height, flags);

            BOOL result = SetWindowPos(hwnd, nullptr, s.target_x, s.target_y, final_width, final_height, flags);
            if (result == FALSE) {
                DWORD error = GetLastError();
                LogError("ApplyWindowChange: SetWindowPos failed with error %lu (0x%lx)", error, error);
            } else {
                LogDebug("ApplyWindowChange: SetWindowPos succeeded");
            }
        }
    }
}
