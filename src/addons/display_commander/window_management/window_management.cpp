#include "../addon.hpp"
#include "../utils.hpp"
#include "../resolution_helpers.hpp"
#include "window_management.hpp"
#include <thread>
#include <algorithm>
#include <sstream>

// Forward declaration
void ComputeDesiredSize(int& out_w, int& out_h);

HMONITOR FindTargetMonitor(HWND hwnd, const RECT& wr_current, float target_monitor_index, MONITORINFO& mi) {
  HMONITOR hmon = nullptr;
  
  if (target_monitor_index > 0.5f) {
    // Use the legacy target monitor setting
    int index = static_cast<int>(target_monitor_index) - 1;
    g_monitors.clear();
    EnumDisplayMonitors(nullptr, nullptr, MonitorEnumProc, reinterpret_cast<LPARAM>(&g_monitors));
    if (index < 0 || index >= static_cast<int>(g_monitors.size())) {
      LogError("FindTargetMonitor: Invalid target monitor index, using default (0)");
      index = 0;
    }
    hmon = g_monitors[index].handle;
    mi = g_monitors[index].info;
  } else {
    // When target monitor is 0, find the monitor where the game is currently on
    // by comparing window position against each monitor's dimensions
    g_monitors.clear();
    EnumDisplayMonitors(nullptr, nullptr, MonitorEnumProc, reinterpret_cast<LPARAM>(&g_monitors));
    
    // Find which monitor contains the game window
    const int window_center_x = (wr_current.left + wr_current.right) / 2;
    const int window_center_y = (wr_current.top + wr_current.bottom) / 2;
    
    bool found_monitor = false;
    for (const auto& monitor : g_monitors) {
      const RECT& mr = monitor.info.rcMonitor;
      if (window_center_x >= mr.left && window_center_x < mr.right &&
          window_center_y >= mr.top && window_center_y < mr.bottom) {
        hmon = monitor.handle;
        mi = monitor.info;
        found_monitor = true;
        
        std::ostringstream oss;
        oss << "CalculateWindowState: Game window is on monitor " << (&monitor - &g_monitors[0]) 
            << " (position: " << window_center_x << "," << window_center_y << ")";
        LogDebug(oss.str());
        break;
      }
    }
    
    if (!found_monitor) {
      // Fallback: use the monitor closest to the window
      hmon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
      GetMonitorInfoW(hmon, &mi);
      
      std::ostringstream oss;
      oss << "CalculateWindowState: Could not determine exact monitor, using closest monitor as fallback";
      LogWarn(oss.str().c_str());
    }
  }
  
  return hmon;
}

// First function: Calculate and update global window state
void  CalculateWindowState(HWND hwnd, const char* reason) {
  if (hwnd == nullptr) return;

  
  
  // Reset global state
  g_window_state.reset();
  g_window_state.reason = reason;

  // Get current styles
  LONG_PTR current_style = GetWindowLongPtrW(hwnd, GWL_STYLE);
  LONG_PTR current_ex_style = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
  
  // Calculate new borderless styles
  g_window_state.new_style = current_style & ~(WS_CAPTION | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU);
  g_window_state.new_ex_style = current_ex_style & ~(WS_EX_DLGMODALFRAME | WS_EX_WINDOWEDGE | WS_EX_CLIENTEDGE | WS_EX_STATICEDGE);
  
  // PREVENT ALWAYS ON TOP: Remove WS_EX_TOPMOST and WS_EX_TOOLWINDOW styles
  if (s_prevent_always_on_top >= 0.5f && (g_window_state.new_ex_style & (WS_EX_TOPMOST | WS_EX_TOOLWINDOW))) {
    g_window_state.new_ex_style &= ~(WS_EX_TOPMOST | WS_EX_TOOLWINDOW);
    
    // Log if we're removing always on top styles
    if ((current_ex_style & (WS_EX_TOPMOST | WS_EX_TOOLWINDOW)) != 0) {
      std::ostringstream oss;
      oss << "ApplyWindowChange: PREVENTING ALWAYS ON TOP - Removing extended styles 0x" << std::hex << (current_ex_style & (WS_EX_TOPMOST | WS_EX_TOOLWINDOW));
      LogInfo(oss.str().c_str());
    }
  }
  if (current_style != g_window_state.new_style || current_ex_style != g_window_state.new_ex_style) {
    g_window_state.style_changed = true;
  }
  
  // Get current window state
  RECT wr_current{};
  GetWindowRect(hwnd, &wr_current);
  
  // Detect window state (maximized, minimized, restored)
  WINDOWPLACEMENT wp = { sizeof(WINDOWPLACEMENT) };
  if (GetWindowPlacement(hwnd, &wp)) {
    g_window_state.show_cmd = wp.showCmd;
  }
  
  g_window_state.style_mode = WindowStyleMode::BORDERLESS;

  // First, determine the target monitor based on game's intended display
  HMONITOR hmon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
  MONITORINFOEXW mi{};
  
  // Use target monitor if specified
  hmon = FindTargetMonitor(hwnd, wr_current, s_target_monitor_index, mi);
  
  
  // Clamp desired size to fit within the target monitor's dimensions
  const int monitor_width = mi.rcMonitor.right - mi.rcMonitor.left;
  const int monitor_height = mi.rcMonitor.bottom - mi.rcMonitor.top;
  
  if (g_window_state.desired_width > monitor_width) {
    std::ostringstream oss;
    oss << "CalculateWindowState: Desired width " << g_window_state.desired_width << " exceeds monitor width " << monitor_width << ", clamping";
    LogInfo(oss.str().c_str());
    g_window_state.desired_width = monitor_width;
  }
  // Get desired dimensions and position from global settings
  // Use manual or aspect ratio mode
  ComputeDesiredSize(g_window_state.desired_width, g_window_state.desired_height);
  
  if (g_window_state.desired_height > monitor_height) {
    std::ostringstream oss;
    oss << "CalculateWindowState: Desired height " << g_window_state.desired_height << " exceeds monitor height " << monitor_height << ", clamping";
    LogInfo(oss.str().c_str());
    g_window_state.desired_height = monitor_height;
  }

  
  // Calculate target dimensions
  RECT client_rect = RectFromWH(g_window_state.desired_width, g_window_state.desired_height);
  if (AdjustWindowRectEx(&client_rect, static_cast<DWORD>(g_window_state.new_style), FALSE, static_cast<DWORD>(g_window_state.new_ex_style)) == FALSE) {
    LogWarn("AdjustWindowRectEx failed for CalculateWindowState.");
    return;
  }
  g_window_state.target_w = client_rect.right - client_rect.left;
  g_window_state.target_h = client_rect.bottom - client_rect.top;
  
  // Calculate target position - start with monitor top-left
  g_window_state.target_x = mi.rcMonitor.left;
  g_window_state.target_y = mi.rcMonitor.top;
  
  const RECT& mr = mi.rcMonitor;
  
  // Apply alignment based on setting
  switch (static_cast<int>(s_move_to_zero_if_out)) {
    default:
    case 1: // Top Left
      g_window_state.target_x = mr.left;
      g_window_state.target_y = mr.top;
      break;
    case 2: // Top Right
      g_window_state.target_x = max(mr.left, mr.right - g_window_state.target_w);
      g_window_state.target_y = mr.top;
      break;
    case 3: // Bottom Left
      g_window_state.target_x = mr.left;
      g_window_state.target_y = max(mr.top, mr.bottom - g_window_state.target_h);
      break;
    case 4: // Bottom Right
      g_window_state.target_x = max(mr.left, mr.right - g_window_state.target_w);
      g_window_state.target_y = max(mr.top, mr.bottom - g_window_state.target_h);
      break;
    case 5: // Center
      g_window_state.target_x = max(mr.left, mr.left + (mr.right - mr.left - g_window_state.target_w) / 2);
      g_window_state.target_y = max(mr.top, mr.top + (mr.bottom - mr.top - g_window_state.target_h) / 2);
      break;
  }
  g_window_state.target_w = min(g_window_state.target_w, mr.right - mr.left);
  g_window_state.target_h = min(g_window_state.target_h, mr.bottom - mr.top);

  // Check if any changes are actually needed
  g_window_state.needs_resize = (g_window_state.target_w != (wr_current.right - wr_current.left)) || 
                                (g_window_state.target_h != (wr_current.bottom - wr_current.top));
  g_window_state.needs_move = (g_window_state.target_x != wr_current.left) || (g_window_state.target_y != wr_current.top);

  LogDebug("CalculateWindowState: target_w=" + std::to_string(g_window_state.target_w) + ", target_h=" + std::to_string(g_window_state.target_h));
}

// Second function: Apply the calculated window changes
void ApplyWindowChange(HWND hwnd, const char* reason, bool force_apply) {
  if (hwnd == nullptr) return;
  
  if (g_window_state.show_cmd == SW_SHOWMAXIMIZED) {
    ShowWindow(hwnd, SW_RESTORE);
    return;
  }
  
  // First calculate the desired window state
  CalculateWindowState(hwnd, reason);

  
  // Check if any changes are needed
  if (g_window_state.needs_resize || g_window_state.needs_move || g_window_state.style_changed) {
    //LogDebug("ApplyWindowChange: No changes needed");
    if (g_window_state.target_w <= 16 || g_window_state.target_h <= 16) {
      std::ostringstream oss;
      oss << "ApplyWindowChange: Invalid target size " << g_window_state.target_w << "x" << g_window_state.target_h << ", skipping";
      LogWarn(oss.str().c_str());
      return;
    }
    if (g_window_state.style_changed) {
      LogDebug("ApplyWindowChange: Setting new style and ex style");
      SetWindowLongPtrW(hwnd, GWL_STYLE, g_window_state.new_style);
      SetWindowLongPtrW(hwnd, GWL_EXSTYLE, g_window_state.new_ex_style);
    }

    UINT flags = SWP_NOZORDER | SWP_NOOWNERZORDER;   
    
    // Apply all changes in a single SetWindowPos call
    if (!g_window_state.needs_resize) flags |= SWP_NOSIZE;
    if (!g_window_state.needs_move) flags |= SWP_NOMOVE;
    if (g_window_state.style_changed) flags |= SWP_FRAMECHANGED;
    
    SetWindowPos(hwnd, nullptr, g_window_state.target_x, g_window_state.target_y, 
      g_window_state.target_w, g_window_state.target_h, flags);
  }
}




