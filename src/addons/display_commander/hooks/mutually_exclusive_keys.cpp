#include "mutually_exclusive_keys.hpp"
#include <windows.h>
#include <sstream>
#include "../utils/logging.hpp"


namespace display_commanderhooks {

namespace mutually_exclusive_keys {

// Key groups - each set contains keys that are mutually exclusive
static std::vector<std::set<int>> s_key_groups;
static bool s_enabled = false;
static std::set<int> s_currently_pressed_keys;  // Track keys currently pressed in groups

// Parse a string of keys (e.g., "ws" or "1234567890") into a set of virtual key codes
static std::set<int> ParseKeyGroup(const std::string& group_str) {
    std::set<int> keys;
    for (char c : group_str) {
        int vKey = 0;
        if (c >= 'a' && c <= 'z') {
            vKey = std::toupper(static_cast<unsigned char>(c));
        } else if (c >= 'A' && c <= 'Z') {
            vKey = c;
        } else if (c >= '0' && c <= '9') {
            vKey = c;  // VK_0 through VK_9
        } else {
            // Skip invalid characters
            continue;
        }
        keys.insert(vKey);
    }
    return keys;
}

// Parse custom groups string (comma-separated, e.g., "1234567890,qwerty")
static std::vector<std::set<int>> ParseCustomGroups(const std::string& custom_groups_str) {
    std::vector<std::set<int>> groups;
    if (custom_groups_str.empty()) {
        return groups;
    }

    std::istringstream iss(custom_groups_str);
    std::string group_str;
    while (std::getline(iss, group_str, ',')) {
        // Trim whitespace
        group_str.erase(0, group_str.find_first_not_of(" \t"));
        group_str.erase(group_str.find_last_not_of(" \t") + 1);
        if (!group_str.empty()) {
            auto keys = ParseKeyGroup(group_str);
            if (keys.size() >= 2) {  // Only add groups with at least 2 keys
                groups.push_back(keys);
            }
        }
    }
    return groups;
}

void Initialize() {
    s_key_groups.clear();
    s_currently_pressed_keys.clear();
    s_enabled = false;
}

void UpdateKeyGroups(bool enabled, bool ws_enabled, bool ad_enabled, bool wasd_enabled,
                     const std::string& custom_groups) {
    s_enabled = enabled;
    s_key_groups.clear();
    s_currently_pressed_keys.clear();

    if (!enabled) {
        return;
    }

    // Add predefined groups
    if (ws_enabled) {
        std::set<int> ws_group;
        ws_group.insert('W');
        ws_group.insert('S');
        s_key_groups.push_back(ws_group);
    }

    if (ad_enabled) {
        std::set<int> ad_group;
        ad_group.insert('A');
        ad_group.insert('D');
        s_key_groups.push_back(ad_group);
    }

    if (wasd_enabled) {
        std::set<int> wasd_group;
        wasd_group.insert('W');
        wasd_group.insert('A');
        wasd_group.insert('S');
        wasd_group.insert('D');
        s_key_groups.push_back(wasd_group);
    }

    // Parse and add custom groups
    auto custom = ParseCustomGroups(custom_groups);
    s_key_groups.insert(s_key_groups.end(), custom.begin(), custom.end());

    // Log groups for debugging
    if (s_enabled && !s_key_groups.empty()) {
        std::ostringstream oss;
        oss << "Mutually exclusive keys enabled with " << s_key_groups.size() << " group(s)";
        LogInfo(oss.str().c_str());
    }
}

bool ShouldSuppressKey(int vKey) {
    if (!s_enabled || s_key_groups.empty()) {
        return false;
    }

    // Find which group this key belongs to
    for (const auto& group : s_key_groups) {
        if (group.find(vKey) != group.end()) {
            // Check if any other key in this group is currently pressed
            for (int pressed_key : s_currently_pressed_keys) {
                if (pressed_key != vKey && group.find(pressed_key) != group.end()) {
                    // Another key in the same group is pressed, suppress this one
                    return true;
                }
            }
            break;
        }
    }

    return false;
}

bool ProcessKeyPress(int vKey) {
    if (!s_enabled || s_key_groups.empty()) {
        return false;
    }

    // Find which group this key belongs to
    for (const auto& group : s_key_groups) {
        if (group.find(vKey) != group.end()) {
            // Check if any other key in this group is currently pressed
            bool suppressed_any = false;
            std::set<int> keys_to_remove;
            for (int pressed_key : s_currently_pressed_keys) {
                if (pressed_key != vKey && group.find(pressed_key) != group.end()) {
                    // Another key in the same group is pressed, mark it for removal
                    keys_to_remove.insert(pressed_key);
                    suppressed_any = true;

                    // Send key-up event for the suppressed key
                    // Use keybd_event to simulate key release
                    keybd_event(static_cast<BYTE>(pressed_key), 0, KEYEVENTF_KEYUP, 0);
                }
            }

            // Remove suppressed keys from tracking
            for (int key : keys_to_remove) {
                s_currently_pressed_keys.erase(key);
            }

            // Mark this key as pressed (after removing others)
            s_currently_pressed_keys.insert(vKey);

            return suppressed_any;
        }
    }

    return false;
}

// Process key release - call when a key is detected as released
void ProcessKeyRelease(int vKey) {
    if (!s_enabled) {
        return;
    }

    // Remove from tracking
    s_currently_pressed_keys.erase(vKey);
}

int GetPressedKeyInGroup(int vKey) {
    if (!s_enabled || s_key_groups.empty()) {
        return 0;
    }

    // Find which group this key belongs to
    for (const auto& group : s_key_groups) {
        if (group.find(vKey) != group.end()) {
            // Return the first pressed key in this group (excluding the queried key)
            for (int pressed_key : s_currently_pressed_keys) {
                if (pressed_key != vKey && group.find(pressed_key) != group.end()) {
                    return pressed_key;
                }
            }
            break;
        }
    }

    return 0;
}

std::vector<std::set<int>> GetAllKeyGroups() { return s_key_groups; }

}  // namespace mutually_exclusive_keys

}  // namespace display_commanderhooks
