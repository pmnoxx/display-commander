#pragma once

#include <functional>
#include <string>
#include <vector>

namespace ui::new_ui {

// Hotkey action callback type
using HotkeyAction = std::function<void()>;

// Parsed hotkey structure
struct ParsedHotkey {
    int key_code = 0;              // Virtual key code (VK_*)
    bool ctrl = false;              // Control modifier
    bool shift = false;             // Shift modifier
    bool alt = false;               // Alt modifier
    bool win = false;              // Windows key modifier
    std::string original_string;   // Original string for display

    bool IsValid() const { return key_code != 0; }
    bool IsEmpty() const { return key_code == 0 && !ctrl && !shift && !alt && !win; }
};

// Hotkey definition structure
struct HotkeyDefinition {
    std::string id;                 // Unique identifier
    std::string name;               // Display name
    std::string default_shortcut;   // Default shortcut string
    std::string description;        // Tooltip description
    HotkeyAction action;            // Action to execute when triggered

    ParsedHotkey parsed;            // Parsed shortcut (updated when setting changes)
    bool enabled = true;            // Whether this hotkey is enabled
};

// Parse a shortcut string like "ctrl+t" or "ctrl+shift+backspace"
ParsedHotkey ParseHotkeyString(const std::string& shortcut);

// Format a parsed hotkey back to a string
std::string FormatHotkeyString(const ParsedHotkey& hotkey);

// Initialize hotkeys tab
void InitHotkeysTab();

// Draw hotkeys tab
void DrawHotkeysTab();

// Process all hotkeys (call from continuous monitoring loop)
void ProcessHotkeys();

}  // namespace ui::new_ui

