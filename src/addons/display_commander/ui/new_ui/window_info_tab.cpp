#include "window_info_tab.hpp"
#include "../../addon.hpp"
#include "../../window_management/window_management.hpp"
#include <deps/imgui/imgui.h>
#include <sstream>

namespace renodx::ui::new_ui {

void DrawWindowInfoTab() {
    ImGui::Text("Window Info Tab - Window Debugging and State");
    ImGui::Separator();
    
    // Draw all window info sections
    DrawBasicWindowInfo();
    ImGui::Spacing();
    DrawWindowStyles();
    ImGui::Spacing();
    DrawWindowState();
    ImGui::Spacing();
    DrawGlobalWindowState();
    ImGui::Spacing();
    DrawFocusAndInputState();
    ImGui::Spacing();
    DrawCursorInfo();
    ImGui::Spacing();
    DrawTargetState();
}

void DrawBasicWindowInfo() {
    if (ImGui::CollapsingHeader("Basic Window Information", ImGuiTreeNodeFlags_DefaultOpen)) {
        // Get current window info
        HWND hwnd = g_last_swapchain_hwnd.load();
        int bb_w = g_last_backbuffer_width.load();
        int bb_h = g_last_backbuffer_height.load();
        
        if (hwnd != nullptr) {
            // Get window rect
            RECT window_rect, client_rect;
            GetWindowRect(hwnd, &window_rect);
            GetClientRect(hwnd, &client_rect);
            
            // Display window information
            ImGui::Text("Window Handle: %p", hwnd);
            ImGui::Text("Window Rect: (%ld,%ld) to (%ld,%ld)", 
                       window_rect.left, window_rect.top, 
                       window_rect.right, window_rect.bottom);
            ImGui::Text("Client Rect: (%ld,%ld) to (%ld,%ld)", 
                       client_rect.left, client_rect.top, 
                       client_rect.right, client_rect.bottom);
            ImGui::Text("Window Size: %ldx%ld", 
                       window_rect.right - window_rect.left, 
                       window_rect.bottom - window_rect.top);
            ImGui::Text("Client Size: %ldx%ld", 
                       client_rect.right - client_rect.left, 
                       client_rect.bottom - client_rect.top);
            
            ImGui::Separator();
            ImGui::Text("Backbuffer Size: %dx%d", bb_w, bb_h);
        } else {
            ImGui::Text("No window available");
        }
    }
}

void DrawWindowStyles() {
    if (ImGui::CollapsingHeader("Window Styles and Properties", ImGuiTreeNodeFlags_DefaultOpen)) {
        HWND hwnd = g_last_swapchain_hwnd.load();
        if (hwnd != nullptr) {
            // Get current window styles
            LONG_PTR style = GetWindowLongPtr(hwnd, GWL_STYLE);
            LONG_PTR ex_style = GetWindowLongPtr(hwnd, GWL_EXSTYLE);
            
            ImGui::Text("Window Styles:");
            ImGui::Text("  Style: 0x%08X", static_cast<unsigned int>(style));
            ImGui::Text("  ExStyle: 0x%08X", static_cast<unsigned int>(ex_style));
            
            // Style analysis
            bool has_caption = (style & WS_CAPTION) != 0;
            bool has_border = (style & WS_BORDER) != 0;
            bool has_thickframe = (style & WS_THICKFRAME) != 0;
            bool has_minimizebox = (style & WS_MINIMIZEBOX) != 0;
            bool has_maximizebox = (style & WS_MAXIMIZEBOX) != 0;
            bool has_sysmenu = (style & WS_SYSMENU) != 0;
            bool is_popup = (style & WS_POPUP) != 0;
            bool is_child = (style & WS_CHILD) != 0;
            
            ImGui::Text("  Has Caption: %s", has_caption ? "Yes" : "No");
            ImGui::Text("  Has Border: %s", has_border ? "Yes" : "No");
            ImGui::Text("  Has ThickFrame: %s", has_thickframe ? "Yes" : "No");
            ImGui::Text("  Has MinimizeBox: %s", has_minimizebox ? "Yes" : "No");
            ImGui::Text("  Has MaximizeBox: %s", has_maximizebox ? "Yes" : "No");
            ImGui::Text("  Has SysMenu: %s", has_sysmenu ? "Yes" : "No");
            ImGui::Text("  Is Popup: %s", is_popup ? "Yes" : "No");
            ImGui::Text("  Is Child: %s", is_child ? "Yes" : "No");
            
            // Additional window properties that affect mouse behavior
            bool is_topmost = (ex_style & WS_EX_TOPMOST) != 0;
            bool is_layered = (ex_style & WS_EX_LAYERED) != 0;
            bool is_transparent = (ex_style & WS_EX_TRANSPARENT) != 0;
            
            ImGui::Separator();
            ImGui::Text("Window Properties (Mouse Behavior):");
            ImGui::Text("  Always On Top: %s", is_topmost ? "YES" : "No");
            ImGui::Text("  Layered: %s", is_layered ? "YES" : "No");
            ImGui::Text("  Transparent: %s", is_transparent ? "YES" : "No");
        }
    }
}

void DrawWindowState() {
    if (ImGui::CollapsingHeader("Window State", ImGuiTreeNodeFlags_DefaultOpen)) {
        HWND hwnd = g_last_swapchain_hwnd.load();
        if (hwnd != nullptr) {
            // Window state
            bool is_visible = IsWindowVisible(hwnd) != FALSE;
            bool is_iconic = IsIconic(hwnd) != FALSE;
            bool is_zoomed = IsZoomed(hwnd) != FALSE;
            bool is_enabled = IsWindowEnabled(hwnd) != FALSE;
            
            ImGui::Text("Window State:");
            ImGui::Text("  Visible: %s", is_visible ? "Yes" : "No");
            ImGui::Text("  Iconic (Minimized): %s", is_iconic ? "Yes" : "No");
            ImGui::Text("  Zoomed (Maximized): %s", is_zoomed ? "Yes" : "No");
            ImGui::Text("  Enabled: %s", is_enabled ? "Yes" : "No");
        }
    }
}

void DrawGlobalWindowState() {
    if (ImGui::CollapsingHeader("Global Window State", ImGuiTreeNodeFlags_DefaultOpen)) {
        HWND hwnd = g_last_swapchain_hwnd.load();
        if (hwnd != nullptr) {
            // Calculate desired state using global window state
            CalculateWindowState(hwnd, "ui_display");
            
            ImGui::Text("Current State:");
            ImGui::Text("  Is Maximized: %s", g_window_state.show_cmd == SW_SHOWMAXIMIZED ? "YES" : "No");
            ImGui::Text("  Is Minimized: %s", g_window_state.show_cmd == SW_SHOWMINIMIZED ? "YES" : "No");
            ImGui::Text("  Is Restored: %s", g_window_state.show_cmd == SW_SHOWNORMAL ? "YES" : "No");
            
            // Check for mouse confinement properties
            bool has_system_menu = (GetWindowLongPtr(hwnd, GWL_STYLE) & WS_SYSMENU) != 0;
            bool has_minimize_box = (GetWindowLongPtr(hwnd, GWL_STYLE) & WS_MINIMIZEBOX) != 0;
            bool has_maximize_box = (GetWindowLongPtr(hwnd, GWL_STYLE) & WS_MAXIMIZEBOX) != 0;
            
            ImGui::Separator();
            ImGui::Text("Mouse & Input Properties:");
            ImGui::Text("  System Menu: %s", has_system_menu ? "YES" : "No");
            ImGui::Text("  Minimize Box: %s", has_minimize_box ? "YES" : "No");
            ImGui::Text("  Maximize Box: %s", has_maximize_box ? "YES" : "No");
        }
    }
}

void DrawFocusAndInputState() {
    if (ImGui::CollapsingHeader("Focus & Input State", ImGuiTreeNodeFlags_DefaultOpen)) {
        HWND hwnd = g_last_swapchain_hwnd.load();
        if (hwnd != nullptr) {
            // Check for cursor confinement and focus
            bool is_foreground = (GetForegroundWindow() == hwnd);
            bool is_active = (GetActiveWindow() == hwnd);
            bool is_focused = (GetFocus() == hwnd);
            
            ImGui::Text("Focus & Input State:");
            ImGui::Text("  Is Foreground: %s", is_foreground ? "YES" : "No");
            ImGui::Text("  Is Active: %s", is_active ? "YES" : "No");
            ImGui::Text("  Is Focused: %s", is_focused ? "YES" : "No");
        }
    }
}

void DrawCursorInfo() {
    if (ImGui::CollapsingHeader("Cursor Information", ImGuiTreeNodeFlags_DefaultOpen)) {
        HWND hwnd = g_last_swapchain_hwnd.load();
        if (hwnd != nullptr) {
            RECT window_rect;
            GetWindowRect(hwnd, &window_rect);
            
            // Check for cursor confinement
            POINT cursor_pos;
            GetCursorPos(&cursor_pos);
            bool cursor_in_window = (cursor_pos.x >= window_rect.left && cursor_pos.x <= window_rect.right &&
                                   cursor_pos.y >= window_rect.top && cursor_pos.y <= window_rect.bottom);
            
            ImGui::Text("Cursor Information:");
            ImGui::Text("  Cursor Pos: (%ld, %ld)", cursor_pos.x, cursor_pos.y);
            ImGui::Text("  Cursor In Window: %s", cursor_in_window ? "YES" : "No");
            ImGui::Text("  Window Bounds: (%ld,%ld) to (%ld,%ld)", 
                       window_rect.left, window_rect.top, window_rect.right, window_rect.bottom);
        }
    }
}

void DrawTargetState() {
    if (ImGui::CollapsingHeader("Target State & Changes", ImGuiTreeNodeFlags_DefaultOpen)) {
        HWND hwnd = g_last_swapchain_hwnd.load();
        if (hwnd != nullptr) {
            ImGui::Text("Target State:");
            ImGui::Text("  Target Size: %dx%d", g_window_state.target_w, g_window_state.target_h);
            ImGui::Text("  Target Position: (%d,%d)", g_window_state.target_x, g_window_state.target_y);
            
            ImGui::Separator();
            ImGui::Text("Change Requirements:");
            ImGui::Text("  Needs Resize: %s", g_window_state.needs_resize ? "YES" : "No");
            ImGui::Text("  Needs Move: %s", g_window_state.needs_move ? "YES" : "No");
            ImGui::Text("  Style Changed: %s", g_window_state.style_changed ? "YES" : "No");
            
            ImGui::Text("Style Mode: %s", 
                g_window_state.style_mode == WindowStyleMode::BORDERLESS ? "BORDERLESS" :
                g_window_state.style_mode == WindowStyleMode::OVERLAPPED_WINDOW ? "WINDOWED" :
                "KEEP");
            
            ImGui::Text("Last Reason: %s", g_window_state.reason.c_str());
        }
    }
}

} // namespace renodx::ui::new_ui
