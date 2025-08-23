#pragma once

#include <deps/imgui/imgui.h>
#include <functional>

namespace renodx::ui::new_ui {

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

} // namespace renodx::ui::new_ui
