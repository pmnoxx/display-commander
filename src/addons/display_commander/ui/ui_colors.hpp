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
 * Section titles: `HEADER` / `HEADER_2` / `HEADER_3` (blue), `WARNING_HEADER*` (yellow),
 *   `EXPERIMENTAL_HEADER*` (red).
 * CollapsingHeader: `Push*Header*Colors`, draw `CollapsingHeader`, then `PopCollapsingHeaderColors` before
 *   drawing expanded body so inner widgets keep default `ImGuiCol_Text`.
 */
#pragma once

#include <imgui.h>

// For wrapper-based overloads (standalone UI)
#include "imgui_wrapper_base.hpp"

namespace ui::colors {

// ============================================================================
// Icon Colors
// ============================================================================

// Success/Positive Actions (Green tones)
constexpr ImVec4 ICON_SUCCESS = ImVec4(0.2f, 0.8f, 0.2f, 1.0f);      // Bright green for success/OK
constexpr ImVec4 ICON_POSITIVE = ImVec4(0.4f, 1.0f, 0.4f, 1.0f);     // Light green for positive states

// Warning (Yellow/Orange tones)
constexpr ImVec4 ICON_WARNING = ImVec4(1.0f, 0.7f, 0.0f, 1.0f);      // Orange for warnings

// Error/Critical (Red tones)
constexpr ImVec4 ICON_ERROR = ImVec4(1.0f, 0.2f, 0.2f, 1.0f);        // Bright red for errors
constexpr ImVec4 ICON_CRITICAL = ImVec4(1.0f, 0.0f, 0.0f, 1.0f);     // Pure red for critical errors

// Analysis (Cyan)
constexpr ImVec4 ICON_ANALYSIS = ImVec4(0.3f, 0.8f, 0.9f, 1.0f);     // Cyan for analysis/search

// Actions (Purple/Magenta tones)
constexpr ImVec4 ICON_ACTION = ImVec4(0.8f, 0.4f, 1.0f, 1.0f);       // Purple for actions
constexpr ImVec4 ICON_SPECIAL = ImVec4(1.0f, 0.4f, 0.8f, 1.0f);      // Magenta for special features

// Utility (Gray tones)
constexpr ImVec4 ICON_DISABLED = ImVec4(0.5f, 0.5f, 0.5f, 1.0f);     // Gray for disabled
constexpr ImVec4 ICON_DARK_GRAY = ImVec4(0.3f, 0.3f, 0.3f, 1.0f);    // Dark gray for unpressed buttons
constexpr ImVec4 ICON_ORANGE = ImVec4(1.0f, 0.5f, 0.0f, 1.0f);       // Orange for low battery/warnings
constexpr ImVec4 ICON_DARK_ORANGE = ImVec4(0.5f, 0.4f, 0.0f, 1.0f);  // Dark orange for home button

// ============================================================================
// Text Colors
// ============================================================================

// Standard text colors
constexpr ImVec4 TEXT_DEFAULT = ImVec4(0.9f, 0.9f, 0.9f, 1.0f);      // Default text
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

// ---------------------------------------------------------------------------
// Section banner / title text (use for "=== Section ===" lines and subtitles)
// ---------------------------------------------------------------------------
// Standard hierarchy (blue): top-level title, subsection, tertiary.
constexpr ImVec4 HEADER = ImVec4(0.80f, 0.80f, 1.00f, 1.0f);
constexpr ImVec4 HEADER_2 = ImVec4(0.62f, 0.66f, 0.95f, 1.0f);
constexpr ImVec4 HEADER_3 = ImVec4(0.48f, 0.54f, 0.82f, 1.0f);

// Warning-styled hierarchy (yellow / amber): caution blocks and sub-lines.
constexpr ImVec4 WARNING_HEADER = ImVec4(1.00f, 0.82f, 0.20f, 1.0f);
constexpr ImVec4 WARNING_HEADER_2 = ImVec4(0.95f, 0.70f, 0.18f, 1.0f);
constexpr ImVec4 WARNING_HEADER_3 = ImVec4(0.78f, 0.56f, 0.15f, 1.0f);

// Experimental / high-risk hierarchy (red): experimental tab emphasis and nested notes.
constexpr ImVec4 EXPERIMENTAL_HEADER = ImVec4(1.00f, 0.38f, 0.40f, 1.0f);
constexpr ImVec4 EXPERIMENTAL_HEADER_2 = ImVec4(0.90f, 0.32f, 0.36f, 1.0f);
constexpr ImVec4 EXPERIMENTAL_HEADER_3 = ImVec4(0.72f, 0.28f, 0.30f, 1.0f);

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

// CollapsingHeader row backgrounds (ImGuiCol_Header / Hovered / Active) — use with HEADER / HEADER_2 / HEADER_3 text
constexpr ImVec4 HEADER_ROW_BG = ImVec4(0.14f, 0.16f, 0.22f, 1.0f);
constexpr ImVec4 HEADER_ROW_BG_HOVERED = ImVec4(0.18f, 0.22f, 0.30f, 1.0f);
constexpr ImVec4 HEADER_ROW_BG_ACTIVE = ImVec4(0.22f, 0.26f, 0.36f, 1.0f);

constexpr ImVec4 HEADER_2_ROW_BG = ImVec4(0.13f, 0.14f, 0.20f, 1.0f);
constexpr ImVec4 HEADER_2_ROW_BG_HOVERED = ImVec4(0.17f, 0.19f, 0.27f, 1.0f);
constexpr ImVec4 HEADER_2_ROW_BG_ACTIVE = ImVec4(0.20f, 0.23f, 0.33f, 1.0f);

constexpr ImVec4 HEADER_3_ROW_BG = ImVec4(0.12f, 0.13f, 0.17f, 1.0f);
constexpr ImVec4 HEADER_3_ROW_BG_HOVERED = ImVec4(0.15f, 0.17f, 0.23f, 1.0f);
constexpr ImVec4 HEADER_3_ROW_BG_ACTIVE = ImVec4(0.18f, 0.20f, 0.28f, 1.0f);

// Warning-styled CollapsingHeader row (level 1): dark warm yellow bar + white title text
constexpr ImVec4 WARNING_HEADER_ROW_BG = ImVec4(0.34f, 0.27f, 0.09f, 1.0f);
constexpr ImVec4 WARNING_HEADER_ROW_BG_HOVERED = ImVec4(0.40f, 0.32f, 0.11f, 1.0f);
constexpr ImVec4 WARNING_HEADER_ROW_BG_ACTIVE = ImVec4(0.44f, 0.35f, 0.12f, 1.0f);
constexpr ImVec4 WARNING_HEADER_ROW_TEXT = ImVec4(0.98f, 0.98f, 0.99f, 1.0f);

constexpr ImVec4 WARNING_HEADER_2_ROW_BG = ImVec4(0.18f, 0.14f, 0.05f, 1.0f);
constexpr ImVec4 WARNING_HEADER_2_ROW_BG_HOVERED = ImVec4(0.24f, 0.19f, 0.07f, 1.0f);
constexpr ImVec4 WARNING_HEADER_2_ROW_BG_ACTIVE = ImVec4(0.28f, 0.22f, 0.08f, 1.0f);

constexpr ImVec4 WARNING_HEADER_3_ROW_BG = ImVec4(0.15f, 0.12f, 0.04f, 1.0f);
constexpr ImVec4 WARNING_HEADER_3_ROW_BG_HOVERED = ImVec4(0.20f, 0.16f, 0.05f, 1.0f);
constexpr ImVec4 WARNING_HEADER_3_ROW_BG_ACTIVE = ImVec4(0.23f, 0.18f, 0.06f, 1.0f);

// Experimental / danger CollapsingHeader rows (red tint)
constexpr ImVec4 EXPERIMENTAL_HEADER_ROW_BG = ImVec4(0.22f, 0.11f, 0.11f, 1.0f);
constexpr ImVec4 EXPERIMENTAL_HEADER_ROW_BG_HOVERED = ImVec4(0.30f, 0.14f, 0.14f, 1.0f);
constexpr ImVec4 EXPERIMENTAL_HEADER_ROW_BG_ACTIVE = ImVec4(0.34f, 0.16f, 0.16f, 1.0f);

constexpr ImVec4 EXPERIMENTAL_HEADER_2_ROW_BG = ImVec4(0.18f, 0.09f, 0.09f, 1.0f);
constexpr ImVec4 EXPERIMENTAL_HEADER_2_ROW_BG_HOVERED = ImVec4(0.24f, 0.12f, 0.12f, 1.0f);
constexpr ImVec4 EXPERIMENTAL_HEADER_2_ROW_BG_ACTIVE = ImVec4(0.28f, 0.14f, 0.14f, 1.0f);

constexpr ImVec4 EXPERIMENTAL_HEADER_3_ROW_BG = ImVec4(0.15f, 0.08f, 0.08f, 1.0f);
constexpr ImVec4 EXPERIMENTAL_HEADER_3_ROW_BG_HOVERED = ImVec4(0.20f, 0.10f, 0.10f, 1.0f);
constexpr ImVec4 EXPERIMENTAL_HEADER_3_ROW_BG_ACTIVE = ImVec4(0.23f, 0.11f, 0.11f, 1.0f);

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

inline void PushCollapsingHeaderColors(display_commander::ui::IImGuiWrapper* w, const ImVec4& header_bg,
                                       const ImVec4& header_bg_hovered, const ImVec4& header_bg_active,
                                       const ImVec4& text) {
    if (w == nullptr) return;
    w->PushStyleColor(ImGuiCol_Header, header_bg);
    w->PushStyleColor(ImGuiCol_HeaderHovered, header_bg_hovered);
    w->PushStyleColor(ImGuiCol_HeaderActive, header_bg_active);
    w->PushStyleColor(ImGuiCol_Text, text);
}

inline void PopCollapsingHeaderColors(display_commander::ui::IImGuiWrapper* w) {
    if (w == nullptr) return;
    w->PopStyleColor(4);
}

inline void PushNestedHeaderColors(display_commander::ui::IImGuiWrapper* w) {
    PushCollapsingHeaderColors(w, HEADER_NESTED_BG, HEADER_NESTED_BG_HOVERED, HEADER_NESTED_BG_ACTIVE, HEADER_NESTED_TEXT);
}
inline void PopNestedHeaderColors(display_commander::ui::IImGuiWrapper* w) {
    PopCollapsingHeaderColors(w);
}

inline void PushHeaderColors(display_commander::ui::IImGuiWrapper* w) {
    PushCollapsingHeaderColors(w, HEADER_ROW_BG, HEADER_ROW_BG_HOVERED, HEADER_ROW_BG_ACTIVE, HEADER);
}
inline void PushHeader2Colors(display_commander::ui::IImGuiWrapper* w) {
    PushCollapsingHeaderColors(w, HEADER_2_ROW_BG, HEADER_2_ROW_BG_HOVERED, HEADER_2_ROW_BG_ACTIVE, HEADER_2);
}
inline void PushHeader3Colors(display_commander::ui::IImGuiWrapper* w) {
    PushCollapsingHeaderColors(w, HEADER_3_ROW_BG, HEADER_3_ROW_BG_HOVERED, HEADER_3_ROW_BG_ACTIVE, HEADER_3);
}

/** Warning-styled CollapsingHeader (primary): dark yellow row, white label (`WARNING_HEADER_ROW_TEXT`). */
inline void PushWarningHeader1Colors(display_commander::ui::IImGuiWrapper* w) {
    PushCollapsingHeaderColors(w, WARNING_HEADER_ROW_BG, WARNING_HEADER_ROW_BG_HOVERED, WARNING_HEADER_ROW_BG_ACTIVE,
                              WARNING_HEADER_ROW_TEXT);
}
inline void PushWarningHeader2Colors(display_commander::ui::IImGuiWrapper* w) {
    PushCollapsingHeaderColors(w, WARNING_HEADER_2_ROW_BG, WARNING_HEADER_2_ROW_BG_HOVERED, WARNING_HEADER_2_ROW_BG_ACTIVE,
                              WARNING_HEADER_2);
}
inline void PushWarningHeader3Colors(display_commander::ui::IImGuiWrapper* w) {
    PushCollapsingHeaderColors(w, WARNING_HEADER_3_ROW_BG, WARNING_HEADER_3_ROW_BG_HOVERED, WARNING_HEADER_3_ROW_BG_ACTIVE,
                              WARNING_HEADER_3);
}

inline void PushExperimentalHeader1Colors(display_commander::ui::IImGuiWrapper* w) {
    PushCollapsingHeaderColors(w, EXPERIMENTAL_HEADER_ROW_BG, EXPERIMENTAL_HEADER_ROW_BG_HOVERED,
                              EXPERIMENTAL_HEADER_ROW_BG_ACTIVE, EXPERIMENTAL_HEADER);
}
inline void PushExperimentalHeader2Colors(display_commander::ui::IImGuiWrapper* w) {
    PushCollapsingHeaderColors(w, EXPERIMENTAL_HEADER_2_ROW_BG, EXPERIMENTAL_HEADER_2_ROW_BG_HOVERED,
                              EXPERIMENTAL_HEADER_2_ROW_BG_ACTIVE, EXPERIMENTAL_HEADER_2);
}
inline void PushExperimentalHeader3Colors(display_commander::ui::IImGuiWrapper* w) {
    PushCollapsingHeaderColors(w, EXPERIMENTAL_HEADER_3_ROW_BG, EXPERIMENTAL_HEADER_3_ROW_BG_HOVERED,
                              EXPERIMENTAL_HEADER_3_ROW_BG_ACTIVE, EXPERIMENTAL_HEADER_3);
}

}  // namespace ui::colors
