#pragma once

#include <imgui.h>
#include <vector>
#include <string>
#include <chrono>
#include <windows.h>

namespace display_commander {
namespace ui {
struct IImGuiWrapper;
}
}  // namespace display_commander

namespace ui::new_ui {

// Message history entry
struct MessageHistoryEntry {
    std::string timestamp;
    UINT message;
    WPARAM wParam;
    LPARAM lParam;
    std::string messageName;
    std::string description;
    bool wasSuppressed;
};

// Draw the window info tab content (uses ImGui wrapper for ReShade or standalone UI)
void DrawWindowInfoTab(display_commander::ui::IImGuiWrapper& imgui);

// Draw basic window information
void DrawBasicWindowInfo(display_commander::ui::IImGuiWrapper& imgui);

// Draw window styles and properties
void DrawWindowStyles(display_commander::ui::IImGuiWrapper& imgui);

// Draw window state information
void DrawWindowState(display_commander::ui::IImGuiWrapper& imgui);

// Draw global window state information
void DrawGlobalWindowState(display_commander::ui::IImGuiWrapper& imgui);

// Draw focus and input state
void DrawFocusAndInputState(display_commander::ui::IImGuiWrapper& imgui);

// Draw continue rendering and should-block-input debug info
void DrawContinueRenderingAndInputBlocking(display_commander::ui::IImGuiWrapper& imgui);

// Draw cursor information
void DrawCursorInfo(display_commander::ui::IImGuiWrapper& imgui);

// Draw target state and change requirements
void DrawTargetState(display_commander::ui::IImGuiWrapper& imgui);

// Draw message sending UI
void DrawMessageSendingUI(display_commander::ui::IImGuiWrapper& imgui);

// Draw message history
void DrawMessageHistory(display_commander::ui::IImGuiWrapper& imgui);

// Add message to history
void AddMessageToHistory(UINT message, WPARAM wParam, LPARAM lParam, bool wasSuppressed = false);

// Add message to history only if it's a known message
void AddMessageToHistoryIfKnown(UINT message, WPARAM wParam, LPARAM lParam, bool wasSuppressed = false);

// Get message name from message ID
std::string GetMessageName(UINT message);

// Get message description
std::string GetMessageDescription(UINT message, WPARAM wParam, LPARAM lParam);

} // namespace ui::new_ui
