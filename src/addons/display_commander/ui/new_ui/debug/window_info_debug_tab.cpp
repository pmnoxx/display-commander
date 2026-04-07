// Source Code <Display Commander> // follow this order for includes in all files + add this comment at the top
#include "window_info_debug_tab.hpp"
#include "../../../globals.hpp"
#include "../../../hooks/windows_hooks/api_hooks.hpp"
#include "../../../hooks/windows_hooks/windows_message_hooks.hpp"
#include "../../../settings/main_tab_settings.hpp"

// Libraries <ReShade> / <imgui>
#include <imgui.h>

// Libraries <standard C++>
#include <cstdio>

// Libraries <Windows.h> — before other Windows headers
#include <Windows.h>

namespace ui::new_ui::debug {

namespace {

const char* InputBlockingModeLabel(InputBlockingMode m) {
    switch (m) {
        case InputBlockingMode::kDisabled:                  return "Disabled";
        case InputBlockingMode::kEnabled:                   return "Always on";
        case InputBlockingMode::kEnabledInBackground:       return "In background only";
        case InputBlockingMode::kEnabledWhenXInputDetected: return "When XInput detected";
        default:                                            return "?";
    }
}

void RowBool(display_commander::ui::IImGuiWrapper& imgui, const char* label, bool v) {
    imgui.TableNextRow();
    imgui.TableNextColumn();
    imgui.TextUnformatted(label);
    imgui.TableNextColumn();
    imgui.TextUnformatted(v ? "true" : "false");
}

void RowText(display_commander::ui::IImGuiWrapper& imgui, const char* label, const char* value) {
    imgui.TableNextRow();
    imgui.TableNextColumn();
    imgui.TextUnformatted(label);
    imgui.TableNextColumn();
    imgui.TextUnformatted(value);
}

void RowHwnd(display_commander::ui::IImGuiWrapper& imgui, const char* label, HWND hwnd) {
    imgui.TableNextRow();
    imgui.TableNextColumn();
    imgui.TextUnformatted(label);
    imgui.TableNextColumn();
    if (hwnd == nullptr) {
        imgui.TextUnformatted("nullptr");
    } else {
        imgui.Text("0x%p", reinterpret_cast<void*>(hwnd));
    }
}

}  // namespace

void DrawWindowInfoDebugTab(display_commander::ui::IImGuiWrapper& imgui) {
    imgui.TextWrapped(
        "Live window and input-blocking state. "
        "OnReShadePresent (addon_event::reshade_present) calls block_input_next_frame() when "
        "ShouldBlockMouseInput() || ShouldBlockKeyboardInput() (not AND). Build with DEBUG_TABS / bd.ps1 -DebugTabs.");
    imgui.Spacing();

    const HWND game_hwnd = display_commanderhooks::GetGameWindow();
    const HWND fg_hwnd = display_commanderhooks::GetForegroundWindow_Direct();
    DWORD fg_pid = 0;
    if (fg_hwnd != nullptr) {
        GetWindowThreadProcessId(fg_hwnd, &fg_pid);
    }
    const DWORD self_pid = GetCurrentProcessId();
    const bool fg_is_self = (fg_pid == self_pid);

    const bool block_mouse = display_commanderhooks::ShouldBlockMouseInput();
    const bool block_kbd = display_commanderhooks::ShouldBlockKeyboardInput();
    const bool block_pad = display_commanderhooks::ShouldBlockGamepadInput();
    const bool block_reshade_present = block_mouse || block_kbd;

    const auto kb_mode = static_cast<InputBlockingMode>(settings::g_mainTabSettings.keyboard_input_blocking.GetValue());
    const auto mouse_mode = static_cast<InputBlockingMode>(settings::g_mainTabSettings.mouse_input_blocking.GetValue());
    const auto pad_mode = static_cast<InputBlockingMode>(settings::g_mainTabSettings.gamepad_input_blocking.GetValue());

    constexpr int kCols = 2;
    if (imgui.BeginTable("window_info_debug", kCols, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
        imgui.TableSetupColumn("Field");
        imgui.TableSetupColumn("Value");
        imgui.TableHeadersRow();

        RowBool(imgui, "g_app_in_background (continuous monitoring)", g_app_in_background.load());
        RowBool(imgui, "s_input_blocking_toggle (e.g. Ctrl+I)", s_input_blocking_toggle.load());

        RowHwnd(imgui, "Game window (swapchain HWND)", game_hwnd);
        RowHwnd(imgui, "Foreground (GetForegroundWindow_Direct)", fg_hwnd);

        imgui.TableNextRow();
        imgui.TableNextColumn();
        imgui.TextUnformatted("Foreground process");
        imgui.TableNextColumn();
        imgui.Text("pid=%lu %s", static_cast<unsigned long>(fg_pid), fg_is_self ? "(this process)" : "(other)");

        if (game_hwnd != nullptr && IsWindow(game_hwnd)) {
            RECT r{};
            if (GetWindowRect(game_hwnd, &r)) {
                imgui.TableNextRow();
                imgui.TableNextColumn();
                imgui.TextUnformatted("Game GetWindowRect");
                imgui.TableNextColumn();
                imgui.Text("LTRB %ld,%ld,%ld,%ld", static_cast<long>(r.left), static_cast<long>(r.top),
                           static_cast<long>(r.right), static_cast<long>(r.bottom));
            }
            RowBool(imgui, "IsIconic_direct(game)", display_commanderhooks::IsIconic_direct(game_hwnd));
            RowBool(imgui, "IsWindowVisible_direct(game)", display_commanderhooks::IsWindowVisible_direct(game_hwnd));
        }

        imgui.TableNextRow();
        imgui.TableNextColumn();
        imgui.TextUnformatted("--- Input blocking ---");
        imgui.TableNextColumn();
        imgui.TextUnformatted("");

        RowText(imgui, "Keyboard mode (setting)", InputBlockingModeLabel(kb_mode));
        RowText(imgui, "Mouse mode (setting)", InputBlockingModeLabel(mouse_mode));
        RowText(imgui, "Gamepad mode (setting)", InputBlockingModeLabel(pad_mode));

        RowBool(imgui, "ShouldBlockKeyboardInput()", block_kbd);
        RowBool(imgui, "ShouldBlockMouseInput()", block_mouse);
        RowBool(imgui, "ShouldBlockGamepadInput()", block_pad);

        imgui.TableNextRow();
        imgui.TableNextColumn();
        imgui.TextUnformatted("block_input_next_frame (reshade_present)");
        imgui.TableNextColumn();
        imgui.Text("%s  (mouse || keyboard)", block_reshade_present ? "would call" : "no");

        RowBool(imgui, "Both mouse AND keyboard would block", block_mouse && block_kbd);

        imgui.EndTable();
    }
}

}  // namespace ui::new_ui::debug
