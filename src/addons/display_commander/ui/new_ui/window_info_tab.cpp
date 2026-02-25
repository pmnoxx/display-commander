#include "window_info_tab.hpp"
#include "../imgui_wrapper_base.hpp"
#include "../../globals.hpp"
#include "../../hooks/api_hooks.hpp"
#include "../../hooks/window_proc_hooks.hpp"
#include "../../hooks/windows_hooks/windows_message_hooks.hpp"
#include "../../settings/advanced_tab_settings.hpp"
#include "../../utils/timing.hpp"
#include "../../window_management/window_management.hpp"

#include <imgui.h>
#include <iomanip>
#include <sstream>

extern HWND GetCurrentForeGroundWindow();

// Message history storage
static std::vector<ui::new_ui::MessageHistoryEntry> g_message_history;
static const size_t MAX_MESSAGE_HISTORY = 50;

namespace ui::new_ui {

void DrawWindowInfoTab(display_commander::ui::IImGuiWrapper& imgui) {
    imgui.Text("Window Info Tab - Window Debugging and State");
    imgui.Separator();

    DrawBasicWindowInfo(imgui);
    imgui.Spacing();
    DrawWindowStyles(imgui);
    imgui.Spacing();
    DrawWindowState(imgui);
    imgui.Spacing();
    DrawGlobalWindowState(imgui);
    imgui.Spacing();
    DrawFocusAndInputState(imgui);
    imgui.Spacing();
    DrawContinueRenderingAndInputBlocking(imgui);
    imgui.Spacing();
    DrawCursorInfo(imgui);
    imgui.Spacing();
    DrawTargetState(imgui);
    imgui.Spacing();
    DrawMessageSendingUI(imgui);
    imgui.Spacing();
    DrawMessageHistory(imgui);
}

void DrawBasicWindowInfo(display_commander::ui::IImGuiWrapper& imgui) {
    if (imgui.CollapsingHeader("Basic Window Information",
                               display_commander::ui::wrapper_flags::TreeNodeFlags_DefaultOpen)) {
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
            imgui.Text("Window Handle: %p", hwnd);
            imgui.Text("Window Rect: (%ld,%ld) to (%ld,%ld)", window_rect.left, window_rect.top, window_rect.right,
                        window_rect.bottom);
            imgui.Text("Client Rect: (%ld,%ld) to (%ld,%ld)", client_rect.left, client_rect.top, client_rect.right,
                        client_rect.bottom);
            imgui.Text("Window Size: %ldx%ld", window_rect.right - window_rect.left,
                        window_rect.bottom - window_rect.top);
            imgui.Text("Client Size: %ldx%ld", client_rect.right - client_rect.left,
                        client_rect.bottom - client_rect.top);

            imgui.Separator();
            imgui.Text("Backbuffer Size: %dx%d", bb_w, bb_h);

            // Display game render resolution (before any modifications) - matches Special K's render_x/render_y
            int game_render_w = g_game_render_width.load();
            int game_render_h = g_game_render_height.load();
            if (game_render_w > 0 && game_render_h > 0) {
                imgui.Separator();
                imgui.Text("Game Render Resolution: %dx%d", game_render_w, game_render_h);

                // Show difference if backbuffer differs from render resolution
                if (bb_w != game_render_w || bb_h != game_render_h) {
                    imgui.TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f),
                                       "  (Modified: Backbuffer differs from render resolution)");
                } else {
                    imgui.TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f),
                                       "  (Unmodified: Backbuffer matches render resolution)");
                }
            } else {
                imgui.Separator();
                imgui.TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Game Render Resolution: Not captured yet");
            }
        } else {
            imgui.Text("No window available");
        }
    }
}

void DrawWindowStyles(display_commander::ui::IImGuiWrapper& imgui) {
    if (imgui.CollapsingHeader("Window Styles and Properties",
                               display_commander::ui::wrapper_flags::TreeNodeFlags_DefaultOpen)) {
        HWND hwnd = g_last_swapchain_hwnd.load();
        if (hwnd != nullptr) {
            // Get current window styles
            LONG_PTR style = GetWindowLongPtr(hwnd, GWL_STYLE);
            LONG_PTR ex_style = GetWindowLongPtr(hwnd, GWL_EXSTYLE);

            imgui.Text("Window Styles:");
            imgui.Text("  Style: 0x%08X", static_cast<unsigned int>(style));
            imgui.Text("  ExStyle: 0x%08X", static_cast<unsigned int>(ex_style));

            // Style analysis
            bool has_caption = (style & WS_CAPTION) != 0;
            bool has_border = (style & WS_BORDER) != 0;
            bool has_thickframe = (style & WS_THICKFRAME) != 0;
            bool has_minimizebox = (style & WS_MINIMIZEBOX) != 0;
            bool has_maximizebox = (style & WS_MAXIMIZEBOX) != 0;
            bool has_sysmenu = (style & WS_SYSMENU) != 0;
            bool is_popup = (style & WS_POPUP) != 0;
            bool is_child = (style & WS_CHILD) != 0;

            imgui.Text("  Has Caption: %s", has_caption ? "Yes" : "No");
            imgui.Text("  Has Border: %s", has_border ? "Yes" : "No");
            imgui.Text("  Has ThickFrame: %s", has_thickframe ? "Yes" : "No");
            imgui.Text("  Has MinimizeBox: %s", has_minimizebox ? "Yes" : "No");
            imgui.Text("  Has MaximizeBox: %s", has_maximizebox ? "Yes" : "No");
            imgui.Text("  Has SysMenu: %s", has_sysmenu ? "Yes" : "No");
            imgui.Text("  Is Popup: %s", is_popup ? "Yes" : "No");
            imgui.Text("  Is Child: %s", is_child ? "Yes" : "No");

            // Additional window properties that affect mouse behavior
            bool is_topmost = (ex_style & WS_EX_TOPMOST) != 0;
            bool is_layered = (ex_style & WS_EX_LAYERED) != 0;
            bool is_transparent = (ex_style & WS_EX_TRANSPARENT) != 0;

            imgui.Separator();
            imgui.Text("Window Properties (Mouse Behavior):");
            imgui.Text("  Always On Top: %s", is_topmost ? "YES" : "No");
            imgui.Text("  Layered: %s", is_layered ? "YES" : "No");
            imgui.Text("  Transparent: %s", is_transparent ? "YES" : "No");
        }
    }
}

void DrawWindowState(display_commander::ui::IImGuiWrapper& imgui) {
    if (imgui.CollapsingHeader("Window State",
                               display_commander::ui::wrapper_flags::TreeNodeFlags_DefaultOpen)) {
        HWND hwnd = g_last_swapchain_hwnd.load();
        if (hwnd != nullptr) {
            // Window state
            bool is_visible = display_commanderhooks::IsWindowVisible_direct(hwnd) != FALSE;
            bool is_iconic = display_commanderhooks::IsIconic_direct(hwnd) != FALSE;
            bool is_zoomed = IsZoomed(hwnd) != FALSE;
            bool is_enabled = IsWindowEnabled(hwnd) != FALSE;

            imgui.Text("Window State:");
            imgui.Text("  Visible: %s", is_visible ? "Yes" : "No");
            imgui.Text("  Iconic (Minimized): %s", is_iconic ? "Yes" : "No");
            imgui.Text("  Zoomed (Maximized): %s", is_zoomed ? "Yes" : "No");
            imgui.Text("  Enabled: %s", is_enabled ? "Yes" : "No");
        }
    }
}

void DrawGlobalWindowState(display_commander::ui::IImGuiWrapper& imgui) {
    if (imgui.CollapsingHeader("Global Window State",
                               display_commander::ui::wrapper_flags::TreeNodeFlags_DefaultOpen)) {
        HWND hwnd = g_last_swapchain_hwnd.load();
        if (hwnd != nullptr) {
            auto window_state = ::g_window_state.load();
            if (window_state) {
                auto s = *window_state;
                imgui.Text("Current State:");
                imgui.Text("  Is Maximized: %s", s.show_cmd == SW_SHOWMAXIMIZED ? "YES" : "No");
                imgui.Text("  Is Minimized: %s", s.show_cmd == SW_SHOWMINIMIZED ? "YES" : "No");
                imgui.Text("  Is Restored: %s", s.show_cmd == SW_SHOWNORMAL ? "YES" : "No");

                // Check for mouse confinement properties
                bool has_system_menu = (GetWindowLongPtr(hwnd, GWL_STYLE) & WS_SYSMENU) != 0;
                bool has_minimize_box = (GetWindowLongPtr(hwnd, GWL_STYLE) & WS_MINIMIZEBOX) != 0;
                bool has_maximize_box = (GetWindowLongPtr(hwnd, GWL_STYLE) & WS_MAXIMIZEBOX) != 0;

                imgui.Separator();
                imgui.Text("Mouse & Input Properties:");
                imgui.Text("  System Menu: %s", has_system_menu ? "YES" : "No");
                imgui.Text("  Minimize Box: %s", has_minimize_box ? "YES" : "No");
                imgui.Text("  Maximize Box: %s", has_maximize_box ? "YES" : "No");
            }
        }
    }
}

void DrawFocusAndInputState(display_commander::ui::IImGuiWrapper& imgui) {
    if (imgui.CollapsingHeader("Focus & Input State",
                               display_commander::ui::wrapper_flags::TreeNodeFlags_DefaultOpen)) {
        HWND hwnd = g_last_swapchain_hwnd.load();
        if (hwnd != nullptr) {
            // Check for cursor confinement and focus
            bool is_foreground = (display_commanderhooks::GetForegroundWindow_Direct() == hwnd);
            bool is_active = (GetActiveWindow() == hwnd);
            bool is_focused = (GetFocus() == hwnd);
            bool is_any_game_window_active = GetCurrentForeGroundWindow() != nullptr;

            HWND foreground_window = display_commanderhooks::GetForegroundWindow_Direct();

            DWORD window_pid = 0;
            DWORD thread_id = GetWindowThreadProcessId(foreground_window, &window_pid);

            DWORD current_process_id = GetCurrentProcessId();

            imgui.Text("Focus & Input State:");
            imgui.Text("  Is Foreground: %s", is_foreground ? "YES" : "No");
            imgui.Text("  Is Active: %s", is_active ? "YES" : "No");
            imgui.Text("  Is Focused: %s", is_focused ? "YES" : "No");
            imgui.Text("  Is Any Game Window Active: %s", is_any_game_window_active ? "YES" : "No");
            imgui.Text("  current process id: %lu", current_process_id);
            imgui.Text("  foreground window pid: %lu", window_pid);
            imgui.Text("  foreground window: %p", foreground_window);
        }
    }
}

void DrawContinueRenderingAndInputBlocking(display_commander::ui::IImGuiWrapper& imgui) {
    if (imgui.CollapsingHeader("Continue Rendering & Input Blocking",
                               display_commander::ui::wrapper_flags::TreeNodeFlags_DefaultOpen)) {
        bool continue_rendering = settings::g_advancedTabSettings.continue_rendering.GetValue();
        bool same_as_hook = (display_commanderhooks::IsContinueRenderingEnabled() == continue_rendering);

        imgui.Text("Continue Rendering (in background):");
        imgui.Text("  Setting: %s", continue_rendering ? "Enabled" : "Disabled");
        imgui.Text("  IsContinueRenderingEnabled(): %s",
                    display_commanderhooks::IsContinueRenderingEnabled() ? "Yes" : "No");
        if (!same_as_hook) {
            imgui.TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "  (Mismatch - hook state differs from setting)");
        }
        if (imgui.IsItemHovered()) {
            imgui.SetTooltip(
                "When enabled, rendering continues when the game loses focus (e.g. alt-tab).\n"
                "Uses window proc hooks to spoof focus/activation.");
        }

        imgui.Separator();
        imgui.Text("Should block input (current runtime result):");
        if (imgui.IsItemHovered()) {
            imgui.SetTooltip(
                "Whether input is currently blocked for the game.\n"
                "Depends on input blocking mode (Main tab), app in background, and Ctrl+I toggle.");
        }
        bool block_kb = display_commanderhooks::ShouldBlockKeyboardInput(false);
        bool block_mouse = display_commanderhooks::ShouldBlockMouseInput(false);
        bool block_gamepad = display_commanderhooks::ShouldBlockGamepadInput();
        imgui.Text("  Keyboard: %s", block_kb ? "Yes" : "No");
        imgui.Text("  Mouse:    %s", block_mouse ? "Yes" : "No");
        imgui.Text("  Gamepad:  %s", block_gamepad ? "Yes" : "No");

        imgui.Separator();
        bool debug_suppress_all = display_commanderhooks::GetDebugSuppressAllGetMessage();
        if (imgui.Checkbox("Debug: Suppress all GetMessage/PeekMessage", &debug_suppress_all)) {
            display_commanderhooks::SetDebugSuppressAllGetMessage(debug_suppress_all);
        }
        if (imgui.IsItemHovered()) {
            imgui.SetTooltip(
                "When on, every message from GetMessage/PeekMessage is skipped (game receives none).\n"
                "Use to see if we forgot to spoof some message type for continue rendering.\n"
                "Default off, not saved to config.");
        }

        imgui.Separator();
            if (imgui.TreeNodeEx("Continue Rendering API debug",
                                 display_commander::ui::wrapper_flags::TreeNodeFlags_DefaultOpen)) {
            if (imgui.IsItemHovered()) {
                imgui.SetTooltip(
                    "Last return value (HWND or BOOL) and override state for each API.\n"
                    "'(game window)' = returned HWND is the game/swapchain window.");
            }
            display_commanderhooks::ContinueRenderingApiDebugSnapshot snap[display_commanderhooks::CR_DEBUG_API_COUNT];
            display_commanderhooks::GetContinueRenderingApiDebugSnapshots(snap);
            const uint64_t now_ns = static_cast<uint64_t>(utils::get_now_ns());
            HWND swap_hwnd = g_last_swapchain_hwnd.load();
            if (imgui.BeginTable("CR API Debug", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
                imgui.TableSetupColumn("API", ImGuiTableColumnFlags_WidthFixed, 140.0f);
                imgui.TableSetupColumn("Returned", ImGuiTableColumnFlags_WidthStretch);
                imgui.TableSetupColumn("Override", ImGuiTableColumnFlags_WidthFixed, 60.0f);
                imgui.TableSetupColumn("Last call", ImGuiTableColumnFlags_WidthFixed, 80.0f);
                imgui.TableHeadersRow();
                for (int i = 0; i < display_commanderhooks::CR_DEBUG_API_COUNT; ++i) {
                    imgui.TableNextRow();
                    imgui.TableSetColumnIndex(0);
                    imgui.TextUnformatted(snap[i].api_name);
                    imgui.TableSetColumnIndex(1);
                    if (snap[i].value_is_bool) {
                        imgui.Text("%s", snap[i].last_value ? "TRUE" : "FALSE");
                    } else {
                        HWND h = reinterpret_cast<HWND>(snap[i].last_value);
                        if (h == nullptr) {
                            imgui.Text("null");
                        } else {
                            imgui.Text("0x%p", (void*)h);
                            if (h == swap_hwnd) {
                                imgui.SameLine();
                                imgui.TextColored(ImVec4(0.2f, 0.8f, 0.2f, 1.0f), "(game window)");
                            }
                        }
                    }
                    imgui.TableSetColumnIndex(2);
                    imgui.TextColored(
                        snap[i].did_override ? ImVec4(0.2f, 0.8f, 0.2f, 1.0f) : ImVec4(0.6f, 0.6f, 0.6f, 1.0f),
                        snap[i].did_override ? "Yes" : "No");
                    imgui.TableSetColumnIndex(3);
                    if (snap[i].last_call_time_ns == 0) {
                        imgui.TextUnformatted("never");
                    } else {
                        uint64_t ago_ns = now_ns - snap[i].last_call_time_ns;
                        double ago_s = static_cast<double>(ago_ns) / 1e9;
                        if (ago_s < 1.0) {
                            imgui.Text("%.0f ms ago", ago_s * 1000.0);
                        } else {
                            imgui.Text("%.2f s ago", ago_s);
                        }
                    }
                }
                imgui.EndTable();
            }
            imgui.TreePop();
        }
    }
}

void DrawCursorInfo(display_commander::ui::IImGuiWrapper& imgui) {
    if (imgui.CollapsingHeader("Cursor Information",
                               display_commander::ui::wrapper_flags::TreeNodeFlags_DefaultOpen)) {
        HWND hwnd = g_last_swapchain_hwnd.load();
        if (hwnd != nullptr) {
            RECT window_rect;
            GetWindowRect(hwnd, &window_rect);

            // Check for cursor confinement
            POINT cursor_pos;
            GetCursorPos(&cursor_pos);
            bool cursor_in_window = (cursor_pos.x >= window_rect.left && cursor_pos.x <= window_rect.right
                                     && cursor_pos.y >= window_rect.top && cursor_pos.y <= window_rect.bottom);

            imgui.Text("Cursor Information:");
            imgui.Text("  Cursor Pos: (%ld, %ld)", cursor_pos.x, cursor_pos.y);
            imgui.Text("  Cursor In Window: %s", cursor_in_window ? "YES" : "No");
            imgui.Text("  Window Bounds: (%ld,%ld) to (%ld,%ld)", window_rect.left, window_rect.top, window_rect.right,
                        window_rect.bottom);
        }
    }
}

void DrawTargetState(display_commander::ui::IImGuiWrapper& imgui) {
    if (imgui.CollapsingHeader("Target State & Changes",
                               display_commander::ui::wrapper_flags::TreeNodeFlags_DefaultOpen)) {
        HWND hwnd = g_last_swapchain_hwnd.load();
        if (hwnd != nullptr) {
            auto window_state2 = ::g_window_state.load();
            if (window_state2) {
                auto s2 = *window_state2;
                imgui.Text("Target State:");
                imgui.Text("  Target Size: %dx%d", s2.target_w, s2.target_h);
                imgui.Text("  Target Position: (%d,%d)", s2.target_x, s2.target_y);

                imgui.Separator();
                imgui.Text("Change Requirements:");
                imgui.Text("  Needs Resize: %s", s2.needs_resize ? "YES" : "No");
                imgui.Text("  Needs Move: %s", s2.needs_move ? "YES" : "No");
                imgui.Text("  Style Changed: %s", s2.style_changed ? "YES" : "No");

                imgui.Text("Style Mode: %s", s2.style_mode == WindowStyleMode::BORDERLESS          ? "BORDERLESS"
                                              : s2.style_mode == WindowStyleMode::OVERLAPPED_WINDOW ? "WINDOWED"
                                                                                                    : "KEEP");

                imgui.Text("Last Reason: %s", s2.reason ? s2.reason : "unknown");
            }
        }
    }
}

void DrawMessageSendingUI(display_commander::ui::IImGuiWrapper& imgui) {
    if (imgui.CollapsingHeader("Message Sending",
                               display_commander::ui::wrapper_flags::TreeNodeFlags_DefaultOpen)) {
        HWND hwnd = g_last_swapchain_hwnd.load();
        if (hwnd == nullptr) {
            imgui.Text("No window available for message sending");
            return;
        }

        static int selected_message = 0;
        static char wparam_input[32] = "0";
        static char lparam_input[32] = "0";
        static char custom_message[32] = "0";

        // Common Windows messages
        const char* message_names[] = {"WM_ACTIVATE (0x0006)",
                                       "WM_SETFOCUS (0x0007)",
                                       "WM_KILLFOCUS (0x0008)",
                                       "WM_ACTIVATEAPP (0x001C)",
                                       "WM_NCACTIVATE (0x0086)",
                                       "WM_WINDOWPOSCHANGING (0x0046)",
                                       "WM_WINDOWPOSCHANGED (0x0047)",
                                       "WM_SHOWWINDOW (0x0018)",
                                       "WM_MOUSEACTIVATE (0x0021)",
                                       "WM_SYSCOMMAND (0x0112)",
                                       "WM_ENTERSIZEMOVE (0x0231)",
                                       "WM_EXITSIZEMOVE (0x0232)",
                                       "WM_QUIT (0x0012)",
                                       "WM_CLOSE (0x0010)",
                                       "WM_DESTROY (0x0002)",
                                       "Custom Message"};

        const UINT message_values[] = {
            WM_ACTIVATE,
            WM_SETFOCUS,
            WM_KILLFOCUS,
            WM_ACTIVATEAPP,
            WM_NCACTIVATE,
            WM_WINDOWPOSCHANGING,
            WM_WINDOWPOSCHANGED,
            WM_SHOWWINDOW,
            WM_MOUSEACTIVATE,
            WM_SYSCOMMAND,
            WM_ENTERSIZEMOVE,
            WM_EXITSIZEMOVE,
            WM_QUIT,
            WM_CLOSE,
            WM_DESTROY,
            0  // Custom message placeholder
        };

        imgui.Text("Send Window Message");
        imgui.Separator();

        // Message selection
        imgui.Text("Message:");
        if (imgui.Combo("##MessageSelect", &selected_message, message_names, IM_ARRAYSIZE(message_names))) {
            // Clear custom message input when selecting predefined message
            if (selected_message < IM_ARRAYSIZE(message_values) - 1) {
                strcpy_s(custom_message, "0");
            }
        }

        // Custom message input
        if (selected_message == IM_ARRAYSIZE(message_values) - 1) {
            imgui.InputText("Custom Message ID", custom_message, sizeof(custom_message));
        }

        // Parameter inputs
        imgui.InputText("wParam (hex)", wparam_input, sizeof(wparam_input));
        imgui.InputText("lParam (hex)", lparam_input, sizeof(lparam_input));

        // Send button
        if (imgui.Button("Send Message")) {
            UINT message = (selected_message == IM_ARRAYSIZE(message_values) - 1)
                               ? static_cast<UINT>(std::strtoul(custom_message, nullptr, 16))
                               : message_values[selected_message];

            WPARAM wParam = static_cast<WPARAM>(std::strtoul(wparam_input, nullptr, 16));
            LPARAM lParam = static_cast<LPARAM>(std::strtoul(lparam_input, nullptr, 16));

            // Send the message
            LRESULT result = SendMessage(hwnd, message, wParam, lParam);

            // Add to history
            AddMessageToHistory(message, wParam, lParam, false);

            imgui.Text("Message sent! Result: 0x%08X", static_cast<unsigned int>(result));
        }

        imgui.SameLine();
        if (imgui.Button("Post Message")) {
            UINT message = (selected_message == IM_ARRAYSIZE(message_values) - 1)
                               ? static_cast<UINT>(std::strtoul(custom_message, nullptr, 16))
                               : message_values[selected_message];

            WPARAM wParam = static_cast<WPARAM>(std::strtoul(wparam_input, nullptr, 16));
            LPARAM lParam = static_cast<LPARAM>(std::strtoul(lparam_input, nullptr, 16));

            // Post the message
            BOOL result = PostMessage(hwnd, message, wParam, lParam);

            // Add to history
            AddMessageToHistory(message, wParam, lParam, false);

            imgui.Text("Message posted! Result: %s", result ? "Success" : "Failed");
        }

        // Quick send buttons for common messages
        imgui.Separator();
        imgui.Text("Quick Send:");

        if (imgui.Button("Send WM_ACTIVATE (WA_ACTIVE)")) {
            SendMessage(hwnd, WM_ACTIVATE, WA_ACTIVE, 0);
            AddMessageToHistory(WM_ACTIVATE, WA_ACTIVE, 0);
        }
        imgui.SameLine();
        if (imgui.Button("Send WM_SETFOCUS")) {
            SendMessage(hwnd, WM_SETFOCUS, 0, 0);
            AddMessageToHistory(WM_SETFOCUS, 0, 0);
        }
        imgui.SameLine();
        if (imgui.Button("Send WM_ACTIVATEAPP (TRUE)")) {
            SendMessage(hwnd, WM_ACTIVATEAPP, TRUE, 0);
            AddMessageToHistory(WM_ACTIVATEAPP, TRUE, 0);
        }
        imgui.SameLine();
        if (imgui.Button("Send WM_NCACTIVATE (TRUE)")) {
            SendMessage(hwnd, WM_NCACTIVATE, TRUE, 0);
            AddMessageToHistory(WM_NCACTIVATE, TRUE, 0);
        }
    }
}

void DrawMessageHistory(display_commander::ui::IImGuiWrapper& imgui) {
    if (imgui.CollapsingHeader("Message History (Last 50 Messages)",
                               display_commander::ui::wrapper_flags::TreeNodeFlags_DefaultOpen)) {
        if (g_message_history.empty()) {
            imgui.Text("No messages received yet");
            return;
        }

        // Display message history in reverse order (newest first)
        imgui.Text("Received Messages:");

        // Add helpful information about suppression
        imgui.TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f),
                           "Note: Messages are only suppressed when Continue Rendering is enabled or Input Blocking is "
                           "active (Ctrl+I)");
        imgui.Separator();

        // Create a table-like display
        if (imgui.BeginTable("MessageHistory", 6, ImGuiTableFlags_Borders | ImGuiTableFlags_Resizable)) {
            imgui.TableSetupColumn("Time", ImGuiTableColumnFlags_WidthFixed, 80);
            imgui.TableSetupColumn("Message", ImGuiTableColumnFlags_WidthFixed, 120);
            imgui.TableSetupColumn("wParam", ImGuiTableColumnFlags_WidthFixed, 80);
            imgui.TableSetupColumn("lParam", ImGuiTableColumnFlags_WidthFixed, 80);
            imgui.TableSetupColumn("Suppressed", ImGuiTableColumnFlags_WidthFixed, 80);
            imgui.TableSetupColumn("Description", ImGuiTableColumnFlags_WidthStretch);
            imgui.TableHeadersRow();

            // Display messages in reverse order (newest first)
            for (int i = static_cast<int>(g_message_history.size()) - 1; i >= 0; i--) {
                const auto& entry = g_message_history[i];

                imgui.TableNextRow();
                imgui.TableNextColumn();
                imgui.Text("%s", entry.timestamp.c_str());

                imgui.TableNextColumn();
                imgui.Text("%s", entry.messageName.c_str());

                imgui.TableNextColumn();
                imgui.Text("0x%08X", static_cast<unsigned int>(entry.wParam));

                imgui.TableNextColumn();
                imgui.Text("0x%08X", static_cast<unsigned int>(entry.lParam));

                imgui.TableNextColumn();
                if (entry.wasSuppressed) {
                    imgui.TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "YES");
                } else {
                    imgui.TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "NO");
                }

                imgui.TableNextColumn();
                imgui.Text("%s", entry.description.c_str());
            }

            imgui.EndTable();
        }

        // Clear history button
        if (imgui.Button("Clear History")) {
            g_message_history.clear();
        }
    }
}

void AddMessageToHistory(UINT message, WPARAM wParam, LPARAM lParam, bool wasSuppressed) {
    // Get current time
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;

    std::stringstream ss;
    ss << std::put_time(std::localtime(&time_t), "%H:%M:%S");
    ss << '.' << std::setfill('0') << std::setw(3) << ms.count();

    ui::new_ui::MessageHistoryEntry entry;
    entry.timestamp = ss.str();
    entry.message = message;
    entry.wParam = wParam;
    entry.lParam = lParam;
    entry.messageName = GetMessageName(message);
    entry.description = GetMessageDescription(message, wParam, lParam);
    entry.wasSuppressed = wasSuppressed;

    g_message_history.push_back(entry);

    // Keep only the last MAX_MESSAGE_HISTORY messages
    if (g_message_history.size() > MAX_MESSAGE_HISTORY) {
        g_message_history.erase(g_message_history.begin());
    }
}

void AddMessageToHistoryIfKnown(UINT message, WPARAM wParam, LPARAM lParam, bool wasSuppressed) {
    // Only track known messages
    switch (message) {
        case WM_ACTIVATE:
        case WM_SETFOCUS:
        case WM_KILLFOCUS:
        case WM_ACTIVATEAPP:
        case WM_NCACTIVATE:
        case WM_WINDOWPOSCHANGING:
        case WM_WINDOWPOSCHANGED:
        case WM_SHOWWINDOW:
        case WM_MOUSEACTIVATE:
        case WM_SYSCOMMAND:
        case WM_ENTERSIZEMOVE:
        case WM_EXITSIZEMOVE:
        case WM_QUIT:
        case WM_CLOSE:
        case WM_DESTROY:           AddMessageToHistory(message, wParam, lParam, wasSuppressed); break;
        default:
            // Don't track unknown messages
            break;
    }
}

std::string GetMessageName(UINT message) {
    switch (message) {
        case WM_ACTIVATE:          return "WM_ACTIVATE";
        case WM_SETFOCUS:          return "WM_SETFOCUS";
        case WM_KILLFOCUS:         return "WM_KILLFOCUS";
        case WM_ACTIVATEAPP:       return "WM_ACTIVATEAPP";
        case WM_NCACTIVATE:        return "WM_NCACTIVATE";
        case WM_WINDOWPOSCHANGING: return "WM_WINDOWPOSCHANGING";
        case WM_WINDOWPOSCHANGED:  return "WM_WINDOWPOSCHANGED";
        case WM_SHOWWINDOW:        return "WM_SHOWWINDOW";
        case WM_MOUSEACTIVATE:     return "WM_MOUSEACTIVATE";
        case WM_SYSCOMMAND:        return "WM_SYSCOMMAND";
        case WM_ENTERSIZEMOVE:     return "WM_ENTERSIZEMOVE";
        case WM_EXITSIZEMOVE:      return "WM_EXITSIZEMOVE";
        case WM_QUIT:              return "WM_QUIT";
        case WM_CLOSE:             return "WM_CLOSE";
        case WM_DESTROY:           return "WM_DESTROY";
        default:                   {
            std::stringstream ss;
            ss << "0x" << std::hex << std::uppercase << message;
            return ss.str();
        }
    }
}

std::string GetMessageDescription(UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_ACTIVATE: {
            if (LOWORD(wParam) == WA_ACTIVE) return "Window activated";
            if (LOWORD(wParam) == WA_INACTIVE) return "Window deactivated";
            if (LOWORD(wParam) == WA_CLICKACTIVE) return "Window activated by click";
            return "Window activation state changed";
        }
        case WM_SETFOCUS:    return "Window gained focus";
        case WM_KILLFOCUS:   return "Window lost focus";
        case WM_ACTIVATEAPP: {
            return wParam ? "Application activated" : "Application deactivated";
        }
        case WM_NCACTIVATE: {
            return wParam ? "Non-client area activated" : "Non-client area deactivated";
        }
        case WM_WINDOWPOSCHANGING: return "Window position changing";
        case WM_WINDOWPOSCHANGED:  return "Window position changed";
        case WM_SHOWWINDOW:        {
            return wParam ? "Window shown" : "Window hidden";
        }
        case WM_MOUSEACTIVATE: return "Mouse activation";
        case WM_SYSCOMMAND:    {
            if (wParam == SC_MINIMIZE) return "System command: Minimize";
            if (wParam == SC_MAXIMIZE) return "System command: Maximize";
            if (wParam == SC_RESTORE) return "System command: Restore";
            return "System command";
        }
        case WM_ENTERSIZEMOVE: return "Enter size/move mode";
        case WM_EXITSIZEMOVE:  return "Exit size/move mode";
        case WM_QUIT:          return "Quit message";
        case WM_CLOSE:         return "Close message";
        case WM_DESTROY:       return "Destroy message";
        default:               return "Unknown message";
    }
}

}  // namespace ui::new_ui
