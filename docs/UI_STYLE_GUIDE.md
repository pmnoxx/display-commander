## Display Commander UI Style Guide

### Overview

This document defines how the Display Commander UI should look and behave so that all tabs feel consistent. It focuses on **section hierarchy**, **indentation**, and **color usage**.

### Section Hierarchy & Indentation

- **Depth 0 (Main Section)**
  - Used for top‑level groups within a tab (e.g. `NGX Counters`, `Swapchain Event Counters`, `Power Saving Settings`).
  - Implemented with `ImGui::CollapsingHeader(...)` **without** any manual `ImGui::Indent()` before it.
  - Text color: default ImGui text or `ui::colors::TEXT_DEFAULT`.

- **Depth 1 (Subsection)**
  - Used for logical subsections inside a main section (e.g. `Parameter Functions`, `D3D11 Feature Management`, `D3D12 Feature Management` inside `NGX Counters`).
  - Prefer `ImGui::CollapsingHeader(...)` for collapsible groups or `ImGui::TextColored(...)` section labels followed by content.
  - Indentation: call `ImGui::Indent()` once **after** the header/label and **always** pair it with `ImGui::Unindent()` at the end of that subsection.
  - Text color: use `ui::colors::TEXT_LABEL` for labels or `TEXT_DEFAULT` for regular text.

- **Depth 2 (Detail Rows / Leaf Content)**
  - Used for individual values/rows (e.g. counters, flags, status lines).
  - Do **not** add additional indentation beyond the Depth 1 indent, unless there is a strong readability reason.
  - Text color: use semantic colors (`TEXT_SUCCESS`, `TEXT_WARNING`, `TEXT_ERROR`, `TEXT_INFO`, `TEXT_VALUE`) when they communicate meaning; otherwise use `TEXT_DEFAULT`.

- **Consistency Rules**
  - Every time a new nested logical group is introduced, explicitly decide its depth and apply a consistent `Indent`/`Unindent` pattern.
  - Avoid having sub‑menus or subsections at the same indentation and same color as their parent; this should never happen for NGX counters or similar trees.

### Color Conventions

- **Neutral / Default**
  - `ui::colors::TEXT_DEFAULT`: standard labels and text.
  - `ui::colors::TEXT_DIMMED` / `TEXT_SUBTLE`: secondary information, hints, or less important rows.

- **Semantic Colors**
  - **Success / Active**: `TEXT_SUCCESS`, `ICON_SUCCESS`, `STATUS_ACTIVE`.
  - **Warning / Caution**: `TEXT_WARNING`, `ICON_WARNING`, `STATUS_STARTING`.
  - **Error / Critical**: `TEXT_ERROR`, `ICON_ERROR`, `ICON_CRITICAL`.
  - **Info / Neutral Highlight**: `TEXT_INFO`, `ICON_INFO`, `ICON_NEUTRAL`.
  - **Values / Numbers**: `TEXT_VALUE` for numeric values and important parameters.

- **Buttons**
  - Selected/primary buttons should use `ui::colors::PushSelectedButtonColors()` and `PopSelectedButtonColors()` for a consistent green theme.
  - Do not hard‑code `ImGui::PushStyleColor` values when a named helper or constant exists in `ui_colors.hpp`.

### NGX Counters Specific Guidelines

- The `NGX Counters` block is a **Depth 0** section.
- Its internal groups (`Parameter Functions`, `D3D12 Feature Management`, `D3D11 Feature Management`, etc.) are **Depth 1** and must:
  - Use a consistent header style (prefer `ImGui::CollapsingHeader(...)`).
  - Call `ImGui::Indent()` once after the header and `ImGui::Unindent()` once after all rows in that group.
  - Use `TEXT_LABEL` or `TEXT_DEFAULT` for labels and semantic colors for statuses where helpful.
- All individual counter rows inside these groups are **Depth 2** and should:
  - Align visually under their group using the depth‑1 indent.
  - Avoid extra indentation that would visually break the hierarchy.

### Implementation Notes

- **Single Source of Truth**
  - All UI color constants and helpers live in `ui_colors.hpp`. New colors should be added there with a clear comment.
  - This document and `ui_colors.hpp` together define the visual style contract for new UI work.

- **When Adding New UI**
  - Decide each section’s depth (0/1/2).
  - Reuse existing color constants where possible; only add new ones when meaningfully different.
  - Check existing tabs (e.g. `Swapchain` / DXGI UI) for examples of good grouping and color usage.

---

**Last Updated**: November 2025
**Status**: Draft, but stable enough for new UI code to follow.


