#pragma once

// Source Code <Display Commander>
// Central list of windows we should not inject addon UI/overlay into (e.g. performance overlay,
// ReShade overlay handling). Use when dispatching reshade_overlay and other addon events.

#include <windows.h>

// Returns true if addon overlay/UI and related logic should be skipped for this window
// (e.g. standalone independent UI window). Call from OnPerformanceOverlay and other
// runtime callbacks that receive an effect_runtime or HWND.
bool should_skip_addon_injection_for_window(HWND hwnd);
