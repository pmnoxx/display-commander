#pragma once

namespace display_commander {
namespace ui {
struct IImGuiWrapper;
}  // namespace ui
}  // namespace display_commander

#include <array>
#include <functional>
#include <string>
#include <vector>

namespace ui::new_ui {

// Hotkey action callback type
using HotkeyAction = std::function<void()>;

// Internal storage is numeric: key_code (VK_*) + modifier flags.
// Display strings are derived via FormatHotkeyString; no per-frame parsing.

// Expected number of hotkey definitions (must match g_hotkey_definitions size in InitializeHotkeyDefinitions).
static constexpr int kHotkeyDefinitionCount = 22;

// Parsed hotkey structure (canonical in-memory representation)
struct ParsedHotkey {
    int key_code = 0;              // Virtual key code (VK_*)
    bool ctrl = false;              // Control modifier
    bool shift = false;            // Shift modifier
    bool alt = false;              // Alt modifier
    bool win = false;              // Windows key modifier
    std::string original_string;   // Original string (for legacy/display; prefer FormatHotkeyString)

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

// Format a parsed hotkey back to a string (primary display form; no parsing needed)
std::string FormatHotkeyString(const ParsedHotkey& hotkey);

// Primary display string for a hotkey binding (first of possible representations)
std::string GetHotkeyDisplayString(const HotkeyDefinition& def);

// Push in-memory parsed state to string settings before save (so file reflects numeric source of truth)
void SyncHotkeySettingsFromParsed();

// Initialize hotkeys tab
void InitHotkeysTab();

// Draw hotkeys tab (accepts ImGui wrapper for ReShade or independent UI)
void DrawHotkeysTab(display_commander::ui::IImGuiWrapper& imgui);

// Process all hotkeys (call from continuous monitoring loop)
void ProcessHotkeys();

// True while user is capturing a hotkey in the UI (hotkeys should not fire)
bool IsCapturingHotkey();

}  // namespace ui::new_ui

