#include "window_management.hpp"
#include "../addon.hpp"
#include "../display_cache.hpp"
#include "../utils.hpp"
#include <algorithm>
#include <sstream>

// Forward declaration
void ComputeDesiredSize(int &out_w, int &out_h);

// First function: Calculate and update global window state
void CalculateWindowState(HWND hwnd, const char *reason) {
    if (hwnd == nullptr)
        return;

    // First, determine the target monitor using display cache (no
    // FindTargetMonitor / MONITORINFOEXW)
    if (!display_cache::g_displayCache.IsInitialized()) {
        display_cache::g_displayCache.Initialize();
    }

    // Build a local snapshot to avoid readers observing partial state
    GlobalWindowState local_state;
    local_state.reason = reason;

    // Get current styles
    LONG_PTR current_style = GetWindowLongPtrW(hwnd, GWL_STYLE);
    LONG_PTR current_ex_style = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);

    // Calculate new borderless styles
    local_state.new_style =
        current_style & ~(WS_CAPTION | WS_THICKFRAME | WS_MINIMIZEBOX |
                          WS_MAXIMIZEBOX | WS_SYSMENU);
    local_state.new_ex_style =
        current_ex_style & ~(WS_EX_DLGMODALFRAME | WS_EX_WINDOWEDGE |
                             WS_EX_CLIENTEDGE | WS_EX_STATICEDGE);

    // PREVENT ALWAYS ON TOP: Remove WS_EX_TOPMOST and WS_EX_TOOLWINDOW styles
    if (s_prevent_always_on_top.load() &&
        (local_state.new_ex_style & (WS_EX_TOPMOST | WS_EX_TOOLWINDOW))) {
        local_state.new_ex_style &= ~(WS_EX_TOPMOST | WS_EX_TOOLWINDOW);

        // Log if we're removing always on top styles
        if ((current_ex_style & (WS_EX_TOPMOST | WS_EX_TOOLWINDOW)) != 0) {
            std::ostringstream oss;
            oss << "ApplyWindowChange: PREVENTING ALWAYS ON TOP - Removing "
                   "extended styles 0x"
                << std::hex
                << (current_ex_style & (WS_EX_TOPMOST | WS_EX_TOOLWINDOW));
            LogInfo(oss.str().c_str());
        }
    }
    if (current_style != local_state.new_style) {
        local_state.style_changed = true;
    }
    if (current_ex_style != local_state.new_ex_style) {
        local_state.style_changed_ex = true;
    }

    // Get current window state
    RECT wr_current{};
    GetWindowRect(hwnd, &wr_current);

    // Detect window state (maximized, minimized, restored)
    WINDOWPLACEMENT wp = {sizeof(WINDOWPLACEMENT)};
    if (GetWindowPlacement(hwnd, &wp)) {
        local_state.show_cmd = wp.showCmd;
    }

    local_state.style_mode = WindowStyleMode::BORDERLESS;

    HMONITOR target_monitor_handle = nullptr;
    int target_monitor_index = 0;
    auto displays = display_cache::g_displayCache.GetDisplays();
    size_t display_count = displays ? displays->size() : 0;

    int requested_monitor = static_cast<int>(
        s_target_monitor_index
            .load()); // 0-based indexing: 0 = Monitor 1, 1 = Monitor 2, etc.
    if (requested_monitor >= 0 && display_count > 0) {
        // Clamp to available displays; display indices are 0..display_count-1
        // in cache
        target_monitor_index =
            (std::max)(0, (std::min)(requested_monitor,
                                     static_cast<int>(display_count) - 1));
        if (target_monitor_index < static_cast<int>(display_count) &&
            (*displays)[target_monitor_index]) {
            const auto *disp = (*displays)[target_monitor_index].get();
            target_monitor_handle = disp->monitor_handle;
        }
    } else {
        // Fallback: use the monitor where the window currently resides
        target_monitor_handle =
            MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
        // Find the corresponding display index in the cache
        for (size_t i = 0; i < display_count; ++i) {
            if ((*displays)[i] &&
                (*displays)[i]->monitor_handle == target_monitor_handle) {
                target_monitor_index = static_cast<int>(i);
                break;
            }
        }
    }

    // Get current refresh rate from cache
    display_cache::RationalRefreshRate tmp_refresh;
    if (target_monitor_index < static_cast<int>(display_count) &&
        (*displays)[target_monitor_index]) {
        const auto *disp = (*displays)[target_monitor_index].get();
        tmp_refresh = disp->current_refresh_rate;
        local_state.current_monitor_index = target_monitor_index;
        local_state.current_monitor_refresh_rate = tmp_refresh;

        const int monitor_width = disp->width;
        const int monitor_height = disp->height;

        if (local_state.desired_width > monitor_width) {
            std::ostringstream oss;
            oss << "CalculateWindowState: Desired width "
                << local_state.desired_width << " exceeds monitor width "
                << monitor_width << ", clamping";
            LogInfo(oss.str().c_str());
            local_state.desired_width = monitor_width;
        }
        // Get desired dimensions and position from global settings
        // Use manual or aspect ratio mode
        ComputeDesiredSize(local_state.desired_width,
                           local_state.desired_height);

        if (local_state.desired_height > monitor_height) {
            std::ostringstream oss;
            oss << "CalculateWindowState: Desired height "
                << local_state.desired_height << " exceeds monitor height "
                << monitor_height << ", clamping";
            LogInfo(oss.str().c_str());
            local_state.desired_height = monitor_height;
        }

        // Calculate target dimensions
        RECT client_rect =
            RectFromWH(local_state.desired_width, local_state.desired_height);
        if (AdjustWindowRectEx(
                &client_rect, static_cast<DWORD>(local_state.new_style), FALSE,
                static_cast<DWORD>(local_state.new_ex_style)) == FALSE) {
            LogWarn("AdjustWindowRectEx failed for CalculateWindowState.");
            return;
        }
        local_state.target_w = client_rect.right - client_rect.left;
        local_state.target_h = client_rect.bottom - client_rect.top;

        // Calculate target position - start with monitor top-left
        local_state.target_x = disp->x;
        local_state.target_y = disp->y;

        const RECT &mr = {disp->x, disp->y, disp->x + disp->width,
                          disp->y + disp->height};

        // Apply alignment based on setting
        switch (s_window_alignment.load()) {
        default:
        case WindowAlignment::kCenter: // Default to center
            local_state.target_x =
                max(mr.left,
                    mr.left + (mr.right - mr.left - local_state.target_w) / 2);
            local_state.target_y =
                max(mr.top,
                    mr.top + (mr.bottom - mr.top - local_state.target_h) / 2);
            break;
        case WindowAlignment::kTopLeft:
            local_state.target_x = mr.left;
            local_state.target_y = mr.top;
            break;
        case WindowAlignment::kTopRight:
            local_state.target_x =
                max(mr.left, mr.right - local_state.target_w);
            local_state.target_y = mr.top;
            break;
        case WindowAlignment::kBottomLeft:
            local_state.target_x = mr.left;
            local_state.target_y =
                max(mr.top, mr.bottom - local_state.target_h);
            break;
        case WindowAlignment::kBottomRight:
            local_state.target_x =
                max(mr.left, mr.right - local_state.target_w);
            local_state.target_y =
                max(mr.top, mr.bottom - local_state.target_h);
            break;
        }
        local_state.target_w = min(local_state.target_w, mr.right - mr.left);
        local_state.target_h = min(local_state.target_h, mr.bottom - mr.top);

        // Check if any changes are actually needed
        local_state.needs_resize =
            (local_state.target_w != (wr_current.right - wr_current.left)) ||
            (local_state.target_h != (wr_current.bottom - wr_current.top));
        local_state.needs_move = (local_state.target_x != wr_current.left) ||
                                 (local_state.target_y != wr_current.top);

        // Store current monitor dimensions
        local_state.display_width = monitor_width;
        local_state.display_height = monitor_height;

        LogDebug("CalculateWindowState: target_w=%d, target_h=%d",
                 local_state.target_w, local_state.target_h);

        // Publish snapshot under a lightweight lock
        g_window_state.store(std::make_shared<GlobalWindowState>(local_state));
    }
}

// Second function: Apply the calculated window changes
void ApplyWindowChange(HWND hwnd, const char *reason, bool force_apply) {
    if (hwnd == nullptr)
        return;

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

        // Check if any changes are needed
        if (s.needs_resize || s.needs_move || s.style_changed ||
            s.style_changed_ex) {
            if (s.target_w <= 16 || s.target_h <= 16) {
                std::ostringstream oss;
                oss << "ApplyWindowChange: Invalid target size " << s.target_w
                    << "x" << s.target_h << ", skipping";
                LogWarn(oss.str().c_str());
                return;
            }
            if (s.style_changed) {
                LogDebug("ApplyWindowChange: Setting new style and ex style");
                SetWindowLongPtrW(hwnd, GWL_STYLE, s.new_style);
            }
            if (s.style_changed_ex) {
                LogDebug("ApplyWindowChange: Setting new ex style");
                SetWindowLongPtrW(hwnd, GWL_EXSTYLE, s.new_ex_style);
            }

            UINT flags = SWP_NOZORDER | SWP_NOOWNERZORDER;

            // Apply all changes in a single SetWindowPos call
            if (!s.needs_resize)
                flags |= SWP_NOSIZE;
            if (!s.needs_move)
                flags |= SWP_NOMOVE;
            if (s.style_changed || s.style_changed_ex)
                flags |= SWP_FRAMECHANGED;

            SetWindowPos(hwnd, nullptr, s.target_x, s.target_y, s.target_w,
                         s.target_h, flags);
        }
    }
}
