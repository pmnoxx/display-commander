#pragma once

#include <windows.h>

namespace display_commander::display::hdr_control {

// Get HDR support and current state for the display that contains the given monitor.
// Returns true if the query succeeded; supported and enabled are valid only then.
bool GetHdrStateForMonitor(HMONITOR monitor, bool* out_supported, bool* out_enabled);

// Set Windows HDR (advanced color) on/off for the display that contains the given monitor.
// Returns true if the change was applied successfully. Only has effect if display is HDR capable.
bool SetHdrForMonitor(HMONITOR monitor, bool enable);

// Get HDR support and current state for the display at the given display index (0-based).
// Uses display cache to resolve index to HMONITOR. Returns true if the query succeeded.
bool GetHdrStateForDisplayIndex(int display_index, bool* out_supported, bool* out_enabled);

// Set Windows HDR on/off for the display at the given display index.
bool SetHdrForDisplayIndex(int display_index, bool enable);

}  // namespace display_commander::display::hdr_control
