#pragma once

#include <windows.h>
#include <string>

namespace display_restore {

// Capture the original mode for a monitor if not already captured
void MarkOriginalForMonitor(HMONITOR monitor);

// Capture the original mode for a device name if not already captured
void MarkOriginalForDeviceName(const std::wstring& device_name);

// Convenience helpers using display cache indices
void MarkOriginalForDisplayIndex(int display_index);
void MarkDeviceChangedByDisplayIndex(int display_index);

// Explicitly mark a device name as having been changed
void MarkDeviceChangedByDeviceName(const std::wstring& device_name);

// Restore all displays that were changed (idempotent)
void RestoreAll();

// Restore if the user enabled the setting (idempotent)
void RestoreAllIfEnabled();

// Clear internal state (for tests or re-init)
void Clear();

// Whether any changes were tracked
bool HasAnyChanges();

// Check if a specific device was changed (by device name)
bool WasDeviceChangedByDeviceName(const std::wstring& device_name);

// Check if a specific device was changed (by display cache index)
bool WasDeviceChangedByDisplayIndex(int display_index);

// Restore only a single display (by display cache index). Returns true on success.
bool RestoreDisplayByIndex(int display_index);

// Restore only a single display (by device name). Returns true on success.
bool RestoreDisplayByDeviceName(const std::wstring& device_name);

}  // namespace display_restore
