#pragma once

#include <imgui.h>
#include <vector>
#include <string>
#include <chrono>
#include <windows.h>

namespace ui::new_ui {

// Message history entry
struct MessageHistoryEntry {
    std::string timestamp;
    UINT message;
    WPARAM wParam;
    LPARAM lParam;
    std::string messageName;
    std::string description;
};

// Draw the window info tab content
void DrawWindowInfoTab();

// Draw basic window information
void DrawBasicWindowInfo();

// Draw window styles and properties
void DrawWindowStyles();

// Draw window state information
void DrawWindowState();

// Draw global window state information
void DrawGlobalWindowState();

// Draw focus and input state
void DrawFocusAndInputState();

// Draw cursor information
void DrawCursorInfo();

// Draw target state and change requirements
void DrawTargetState();

// Draw message sending UI
void DrawMessageSendingUI();

// Draw message history
void DrawMessageHistory();

// Add message to history
void AddMessageToHistory(UINT message, WPARAM wParam, LPARAM lParam);

// Add message to history only if it's a known message
void AddMessageToHistoryIfKnown(UINT message, WPARAM wParam, LPARAM lParam);

// Get message name from message ID
std::string GetMessageName(UINT message);

// Get message description
std::string GetMessageDescription(UINT message, WPARAM wParam, LPARAM lParam);

} // namespace ui::new_ui
