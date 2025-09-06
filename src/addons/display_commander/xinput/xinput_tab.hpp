#pragma once

#include <string>
#include <Windows.h>

// Forward declaration
namespace display_commander::xinput {
    struct ControllerState;
}

namespace display_commander::xinput {

// Initialize XInput tab
void InitXInputTab();

// Draw XInput tab content
void DrawXInputTab();

// Get XInput tab name
const std::string& GetXInputTabName();

// Get XInput tab ID
const std::string& GetXInputTabId();

// Draw controller visualization
void DrawControllerVisualization(DWORD controller_index, const ControllerState& controller);

} // namespace display_commander::xinput
