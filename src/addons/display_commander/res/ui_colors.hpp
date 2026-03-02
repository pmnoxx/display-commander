/**
 * UI Colors for Display Commander
 * Centralized color definitions for consistent theming across the UI.
 *
 * Visual hierarchy (section depths, indentation rules, and how these colors
 * should be applied) is documented in `docs/UI_STYLE_GUIDE.md`.
 * Whenever you add or change UI sections (including NGX counters and other
 * nested menus), please follow that style guide for:
 *  - Depth 0 / 1 / 2 layout
 *  - Indent / Unindent usage
 *  - Which semantic text/icon colors to use
 */
#pragma once

#include <imgui.h>

// For wrapper-based overloads (standalone UI); path from res/ to ui/
#include "../ui/imgui_wrapper_base.hpp"

namespace ui::colors {

// ============================================================================
// Icon Colors
// ============================================================================

// Success/Positive Actions (Green tones)
constexpr ImVec4 ICON_SUCCESS = ImVec4(0.2f, 0.8f, 0.2f, 1.0f);      // Bright green for success/OK
constexpr ImVec4 ICON_POSITIVE = ImVec4(0.4f, 1.0f, 0.4f, 1.0f);     // Light green for positive states

// Warning/Caution (Yellow/Orange tones)
constexpr ImVec4 ICON_WARNING = ImVec4(1.0f, 0.7f, 0.0f, 1.0f);      // Orange for warnings
constexpr ImVec4 ICON_CAUTION = ImVec4(1.0f, 0.9f, 0.2f, 1.0f);      // Yellow for caution

// Error/Danger (Red tones)
constexpr ImVec4 ICON_ERROR = ImVec4(1.0f, 0.2f, 0.2f, 1.0f);        // Bright red for errors
constexpr ImVec4 ICON_DANGER = ImVec4(0.9f, 0.3f, 0.3f, 1.0f);       // Softer red for danger
constexpr ImVec4 ICON_CRITICAL = ImVec4(1.0f, 0.0f, 0.0f, 1.0f);     // Pure red for critical errors

// Info/Neutral (Blue/Cyan tones)
constexpr ImVec4 ICON_INFO = ImVec4(0.4f, 0.7f, 1.0f, 1.0f);         // Light blue for info
constexpr ImVec4 ICON_NEUTRAL = ImVec4(0.6f, 0.8f, 1.0f, 1.0f);      // Soft cyan for neutral
constexpr ImVec4 ICON_ANALYSIS = ImVec4(0.3f, 0.8f, 0.9f, 1.0f);     // Cyan for analysis/search

// Actions (Purple/Magenta tones)
constexpr ImVec4 ICON_ACTION = ImVec4(0.8f, 0.4f, 1.0f, 1.0f);       // Purple for actions
constexpr ImVec4 ICON_SPECIAL = ImVec4(1.0f, 0.4f, 0.8f, 1.0f);      // Magenta for special features

// Utility (Gray tones)
constexpr ImVec4 ICON_DISABLED = ImVec4(0.5f, 0.5f, 0.5f, 1.0f);     // Gray for disabled
constexpr ImVec4 ICON_MUTED = ImVec4(0.6f, 0.6f, 0.6f, 1.0f);        // Light gray for muted
constexpr ImVec4 ICON_DARK_GRAY = ImVec4(0.3f, 0.3f, 0.3f, 1.0f);    // Dark gray for unpressed buttons
constexpr ImVec4 ICON_ORANGE = ImVec4(1.0f, 0.5f, 0.0f, 1.0f);       // Orange for low battery/warnings
constexpr ImVec4 ICON_DARK_ORANGE = ImVec4(0.5f, 0.4f, 0.0f, 1.0f);  // Dark orange for home button

// ============================================================================
// Text Colors
// ============================================================================

// Standard text colors
constexpr ImVec4 TEXT_DEFAULT = ImVec4(0.9f, 0.9f, 0.9f, 1.0f);      // Default text
constexpr ImVec4 TEXT_BRIGHT = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);       // Bright white
constexpr ImVec4 TEXT_DIMMED = ImVec4(0.7f, 0.7f, 0.7f, 1.0f);       // Dimmed text
constexpr ImVec4 TEXT_SUBTLE = ImVec4(0.6f, 0.6f, 0.6f, 1.0f);       // Subtle text

// Semantic text colors
constexpr ImVec4 TEXT_SUCCESS = ImVec4(0.4f, 1.0f, 0.4f, 1.0f);      // Success messages
constexpr ImVec4 TEXT_WARNING = ImVec4(1.0f, 0.7f, 0.0f, 1.0f);      // Warning messages
constexpr ImVec4 TEXT_ERROR = ImVec4(1.0f, 0.4f, 0.4f, 1.0f);        // Error messages
constexpr ImVec4 TEXT_INFO = ImVec4(0.5f, 0.8f, 1.0f, 1.0f);         // Info messages

// Special text colors
constexpr ImVec4 TEXT_HIGHLIGHT = ImVec4(0.8f, 1.0f, 0.8f, 1.0f);    // Highlighted text
constexpr ImVec4 TEXT_VALUE = ImVec4(1.0f, 1.0f, 0.0f, 1.0f);        // Values/numbers (yellow)
constexpr ImVec4 TEXT_LABEL = ImVec4(0.8f, 0.8f, 1.0f, 1.0f);        // Labels (light blue)

// ============================================================================
// Button Colors
// ============================================================================

// Selected button colors (green theme)
constexpr ImVec4 BUTTON_SELECTED = ImVec4(0.20f, 0.60f, 0.20f, 1.0f);
constexpr ImVec4 BUTTON_SELECTED_HOVERED = ImVec4(0.20f, 0.70f, 0.20f, 1.0f);
constexpr ImVec4 BUTTON_SELECTED_ACTIVE = ImVec4(0.10f, 0.50f, 0.10f, 1.0f);

// ============================================================================
// Performance/State Colors
// ============================================================================

// Flip state colors
constexpr ImVec4 FLIP_COMPOSED = ImVec4(1.0f, 0.0f, 0.0f, 1.0f);      // Red for composed flip (bad)
constexpr ImVec4 FLIP_INDEPENDENT = ImVec4(0.8f, 1.0f, 0.8f, 1.0f);   // Green for independent flip (good)
constexpr ImVec4 FLIP_UNKNOWN = ImVec4(1.0f, 1.0f, 0.8f, 1.0f);       // Yellow for unknown

// Status colors
constexpr ImVec4 STATUS_ACTIVE = ImVec4(0.0f, 1.0f, 0.0f, 1.0f);      // Active/running (green)
constexpr ImVec4 STATUS_INACTIVE = ImVec4(0.8f, 0.8f, 0.8f, 1.0f);    // Inactive (gray)
constexpr ImVec4 STATUS_STARTING = ImVec4(1.0f, 0.5f, 0.0f, 1.0f);    // Starting/loading (orange)

// ============================================================================
// Header Colors (for nested CollapsingHeaders - Depth 1)
// ============================================================================

// Nested header background (slightly different from default to show hierarchy)
constexpr ImVec4 HEADER_NESTED_BG = ImVec4(0.15f, 0.15f, 0.18f, 1.0f);      // Darker background for nested headers
constexpr ImVec4 HEADER_NESTED_BG_HOVERED = ImVec4(0.20f, 0.20f, 0.25f, 1.0f);  // Hovered state
constexpr ImVec4 HEADER_NESTED_BG_ACTIVE = ImVec4(0.25f, 0.25f, 0.30f, 1.0f);   // Active/pressed state

// Nested header text color (uses TEXT_LABEL for visual distinction)
constexpr ImVec4 HEADER_NESTED_TEXT = TEXT_LABEL;  // Light blue for nested headers

// ============================================================================
// Helper Functions
// ============================================================================

// Wrapper-based overloads (use when drawing via IImGuiWrapper, e.g. standalone UI)
inline void PushSelectedButtonColors(display_commander::ui::IImGuiWrapper* w) {
    if (w == nullptr) return;
    w->PushStyleColor(ImGuiCol_Button, BUTTON_SELECTED);
    w->PushStyleColor(ImGuiCol_ButtonHovered, BUTTON_SELECTED_HOVERED);
    w->PushStyleColor(ImGuiCol_ButtonActive, BUTTON_SELECTED_ACTIVE);
}
inline void PopSelectedButtonColors(display_commander::ui::IImGuiWrapper* w) {
    if (w == nullptr) return;
    w->PopStyleColor(3);
}

inline void PushIconColor(display_commander::ui::IImGuiWrapper* w, const ImVec4& color) {
    if (w == nullptr) return;
    w->PushStyleColor(ImGuiCol_Text, color);
}
inline void PopIconColor(display_commander::ui::IImGuiWrapper* w) {
    if (w == nullptr) return;
    w->PopStyleColor(1);
}

inline void PushNestedHeaderColors(display_commander::ui::IImGuiWrapper* w) {
    if (w == nullptr) return;
    w->PushStyleColor(ImGuiCol_Header, HEADER_NESTED_BG);
    w->PushStyleColor(ImGuiCol_HeaderHovered, HEADER_NESTED_BG_HOVERED);
    w->PushStyleColor(ImGuiCol_HeaderActive, HEADER_NESTED_BG_ACTIVE);
    w->PushStyleColor(ImGuiCol_Text, HEADER_NESTED_TEXT);
}
inline void PopNestedHeaderColors(display_commander::ui::IImGuiWrapper* w) {
    if (w == nullptr) return;
    w->PopStyleColor(4);
}

}  // namespace ui::colors
