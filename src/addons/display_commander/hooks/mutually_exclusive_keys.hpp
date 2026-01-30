#pragma once

#include <set>
#include <string>
#include <vector>

namespace display_commanderhooks {

// Mutually exclusive keys manager
namespace mutually_exclusive_keys {

// Initialize the manager
void Initialize();

// Update key groups from settings (call when settings change)
void UpdateKeyGroups(bool enabled, bool ws_enabled, bool ad_enabled, bool wasd_enabled,
                     const std::string& custom_groups);

// Check if a key should be suppressed (returns true if key should be reported as not pressed)
bool ShouldSuppressKey(int vKey);

// Process key press - call when a key is detected as pressed
// Returns true if opposite keys were suppressed
bool ProcessKeyPress(int vKey);

// Process key release - call when a key is detected as released
void ProcessKeyRelease(int vKey);

// Get currently pressed key in a group (returns 0 if none)
int GetPressedKeyInGroup(int vKey);

// Get all key groups (for debugging)
std::vector<std::set<int>> GetAllKeyGroups();

}  // namespace mutually_exclusive_keys

}  // namespace display_commanderhooks
