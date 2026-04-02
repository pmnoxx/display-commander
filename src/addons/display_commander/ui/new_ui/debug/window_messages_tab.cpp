// Source Code <Display Commander> // follow this order for includes in all files + add this comment at the top
#include "window_messages_tab.hpp"
#include "../../../hooks/windows_hooks/window_proc_hooks.hpp"

// Libraries <ReShade> / <imgui>
#include <imgui.h>

// Libraries <standard C++>
#include <vector>

namespace ui::new_ui::debug {

void DrawWindowMessagesTab(display_commander::ui::IImGuiWrapper& imgui) {
    bool filter_14fe = display_commanderhooks::GetDebugHistoryFilterEnabledForMessage(0x14FE);
    if (imgui.Checkbox("Filter 0x14FE", &filter_14fe)) {
        display_commanderhooks::SetDebugHistoryFilterEnabledForMessage(0x14FE, filter_14fe);
    }

    bool filter_c2a1 = display_commanderhooks::GetDebugHistoryFilterEnabledForMessage(0xC2A1);
    if (imgui.Checkbox("Filter 0xC2A1", &filter_c2a1)) {
        display_commanderhooks::SetDebugHistoryFilterEnabledForMessage(0xC2A1, filter_c2a1);
    }

    bool filter_0060 = display_commanderhooks::GetDebugHistoryFilterEnabledForMessage(0x0060);
    if (imgui.Checkbox("Filter WM_SETREDRAW (0x0060)", &filter_0060)) {
        display_commanderhooks::SetDebugHistoryFilterEnabledForMessage(0x0060, filter_0060);
    }

    bool filter_0113 = display_commanderhooks::GetDebugHistoryFilterEnabledForMessage(0x0113);
    if (imgui.Checkbox("Filter WM_TIMER (0x0113)", &filter_0113)) {
        display_commanderhooks::SetDebugHistoryFilterEnabledForMessage(0x0113, filter_0113);
    }

    imgui.Separator();

    const std::vector<display_commanderhooks::WindowMessageRecord> messages =
        display_commanderhooks::GetRecentWindowMessagesSnapshot();

    imgui.Text("Last 50 ProcessWindowMessage entries");
    imgui.Text("Captured: %d", static_cast<int>(messages.size()));

    if (imgui.Button("Clear")) {
        display_commanderhooks::ClearRecentWindowMessages();
    }

    imgui.Separator();

    if (messages.empty()) {
        imgui.Text("No messages captured yet.");
        return;
    }

    for (size_t i = 0; i < messages.size(); ++i) {
        const UINT message_id = messages[i].message_id;
        const char* message_name = display_commanderhooks::GetWindowMessageName(message_id);
        imgui.Text("%02d. %s (0x%04X / %u)", static_cast<int>(i + 1), message_name, message_id, message_id);
    }
}

}  // namespace ui::new_ui::debug
