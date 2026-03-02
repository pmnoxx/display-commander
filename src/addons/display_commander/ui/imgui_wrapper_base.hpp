#pragma once

/**
 * Base types and interface for ImGui abstraction.
 * Shared UI code (e.g. Nvidia Profile tab) uses IImGuiWrapper so it can run
 * with either ReShade's ImGui or the standalone ImGui (ImGuiStandalone).
 * Uses ImGui's ImVec2/ImVec4 for layout and colors.
 */

#include <imgui.h>
#include <cstddef>
#include <cstdint>

namespace display_commander {
namespace ui {

/** Graphics API for display (dx11, dx12, etc.). Used when drawing API-agnostic tabs. */
enum class GraphicsApi : std::uint32_t {
    Unknown = 0,
    D3D9    = 0x9000,
    D3D10   = 0xa000,
    D3D11   = 0xb000,
    D3D12   = 0xc000,
    OpenGL  = 0x10000,
    Vulkan  = 0x20000
};

/**
 * For colors in shared/wrapper UI code, use res/ui_colors.hpp and ui::colors::* (e.g. ui::colors::TEXT_WARNING).
 * The wrapper API uses ImVec4/ImVec2 directly.
 */

/** ImGui table/tree flags as int (same numeric values as imgui.h for compatibility). */
namespace wrapper_flags {
constexpr int TreeNodeFlags_None        = 0;
constexpr int TreeNodeFlags_Leaf        = 1 << 2;   // No expand/collapse
constexpr int TreeNodeFlags_DefaultOpen = 1 << 5;
constexpr int TableFlags_BordersOuter   = (1 << 8) | (1 << 10);
constexpr int TableFlags_BordersH      = (1 << 7) | (1 << 8);
constexpr int TableFlags_SizingStretchProp = 3 << 13;
constexpr int TableFlags_ScrollY       = 1 << 25;
constexpr int TableFlags_RowBg         = 1 << 4;
constexpr int TableFlags_Borders       = (1 << 7) | (1 << 8) | (1 << 9) | (1 << 10);
constexpr int TableFlags_Resizable     = 1 << 12;   // Columns can be resized by user
constexpr int TableFlags_SizingFixedFit = 2 << 13;
constexpr int TableColumnFlags_WidthStretch = 1 << 3;
constexpr int TableColumnFlags_WidthFixed   = 1 << 4;
} // namespace wrapper_flags

/**
 * Proxy interface for ImGui draw list. Use this instead of raw ImDrawList* from the wrapper
 * so that all draw calls go through the same module that owns the ImGui context (avoids
 * linker/ABI issues and crashes when the addon's ImDrawList layout differs from runtime's).
 */
struct IImDrawList {
    virtual ~IImDrawList() = default;
    virtual void AddLine(const ImVec2& p1, const ImVec2& p2, ImU32 col, float thickness = 1.0f) = 0;
    virtual void AddRect(const ImVec2& p_min, const ImVec2& p_max, ImU32 col, float rounding = 0.0f,
                         int flags = 0, float thickness = 1.0f) = 0;
    virtual void AddRectFilled(const ImVec2& p_min, const ImVec2& p_max, ImU32 col,
                               float rounding = 0.0f, int flags = 0) = 0;
    virtual void AddCircle(const ImVec2& center, float radius, ImU32 col, int num_segments = 0,
                           float thickness = 1.0f) = 0;
    virtual void AddCircleFilled(const ImVec2& center, float radius, ImU32 col, int num_segments = 0) = 0;
    virtual void AddTriangleFilled(const ImVec2& p1, const ImVec2& p2, const ImVec2& p3, ImU32 col) = 0;
};

/** Abstract ImGui backend for shared UI code. */
struct IImGuiWrapper {
    virtual ~IImGuiWrapper() = default;

    virtual void SameLine(float offset_from_start_x = 0.f, float spacing_w = -1.f) = 0;
    virtual void Text(const char* fmt, ...) = 0;
    virtual void TextColored(const ImVec4& col, const char* fmt, ...) = 0;
    virtual void TextUnformatted(const char* text) = 0;
    virtual bool Button(const char* label) = 0;
    virtual bool Button(const char* label, const ImVec2& size) = 0;
    virtual bool SmallButton(const char* label) = 0;
    virtual bool Checkbox(const char* label, bool* v) = 0;
    virtual bool IsItemHovered() = 0;
    virtual bool IsItemActive() = 0;
    virtual bool IsItemDeactivatedAfterEdit() = 0;
    virtual void SetTooltip(const char* fmt, ...) = 0;
    virtual void Spacing() = 0;
    virtual void Separator() = 0;
    virtual bool BeginChild(const char* str_id, const ImVec2& size, bool border) = 0;
    virtual void EndChild() = 0;
    virtual bool CollapsingHeader(const char* label, int flags = 0) = 0;
    virtual bool BeginTable(const char* str_id, int columns, int flags, const ImVec2& outer_size = ImVec2(0.f, 0.f)) = 0;
    virtual void EndTable() = 0;
    virtual void TableSetupColumn(const char* label, int flags = 0, float width_weight = 0.f) = 0;
    virtual void TableSetupScrollFreeze(int cols, int rows) = 0;
    virtual void TableHeadersRow() = 0;
    virtual void TableNextRow() = 0;
    virtual void TableNextColumn() = 0;
    virtual void TableSetColumnIndex(int column_n) = 0;
    virtual bool BeginCombo(const char* label, const char* preview_value, int flags = 0) = 0;
    virtual void EndCombo() = 0;
    virtual bool Selectable(const char* label, bool selected = false) = 0;
    virtual void SetItemDefaultFocus() = 0;
    virtual void PushID(int id) = 0;
    virtual void PushID(const char* str_id) = 0;
    virtual void PopID() = 0;
    virtual void Indent() = 0;
    virtual void Unindent() = 0;
    virtual bool InputText(const char* label, char* buf, size_t buf_size) = 0;
    virtual bool InputText(const char* label, char* buf, size_t buf_size, int flags) = 0;
    virtual bool InputFloat(const char* label, float* v, float step = 0.0f, float step_fast = 0.0f,
                            const char* format = "%.3f", int flags = 0) = 0;
    virtual bool InputInt(const char* label, int* v, int step = 1, int step_fast = 0, int flags = 0) = 0;
    virtual bool SliderInt(const char* label, int* v, int v_min, int v_max, const char* format = "%d") = 0;
    virtual void TextWrapped(const char* fmt, ...) = 0;
    virtual void TextDisabled(const char* fmt, ...) = 0;
    virtual void PushStyleColor(int col_enum, const ImVec4& color) = 0;
    virtual void PopStyleColor(int count = 1) = 0;
    virtual bool TreeNodeEx(const char* label, int flags) = 0;
    virtual bool TreeNode(const char* label) = 0;   // Same as ImGui::TreeNode(label)
    virtual void TreePop() = 0;
    virtual ImVec2 GetContentRegionAvail() = 0;
    virtual float GetStyleItemSpacingX() = 0;
    virtual float GetStyleFramePaddingX() = 0;
    virtual ImVec2 CalcTextSize(const char* text) = 0;
    virtual void SetNextItemWidth(float width) = 0;
    virtual void BeginDisabled() = 0;
    virtual void EndDisabled() = 0;

    // Plot / graphs
    virtual void PlotLines(const char* label, const float* values, int values_count, int values_offset,
                          const char* overlay_text, float scale_min, float scale_max, const ImVec2& graph_size) = 0;

    // Combo (int selection, array of item strings)
    virtual bool Combo(const char* label, int* current_item, const char* const items[], int items_count) = 0;

    // Layout / cursor (returns proxy to avoid using wrong ImDrawList ABI/layout across modules)
    virtual IImDrawList* GetWindowDrawList() = 0;
    virtual ImVec2 GetCursorScreenPos() = 0;
    virtual void SetCursorScreenPos(const ImVec2& pos) = 0;
    virtual float GetCursorPosX() = 0;
    virtual void Dummy(const ImVec2& size) = 0;
    virtual ImVec2 GetItemRectMin() = 0;
    virtual ImVec2 GetItemRectSize() = 0;

    // Progress bar (fraction 0..1, size_arg, overlay text)
    virtual void ProgressBar(float fraction, const ImVec2& size_arg, const char* overlay) = 0;

    // Colors (for draw lists)
    virtual ImU32 GetColorU32(int col_enum) = 0;
    virtual ImU32 ColorConvertFloat4ToU32(const ImVec4& col) = 0;

    // Slider float
    virtual bool SliderFloat(const char* label, float* v, float v_min, float v_max,
                             const char* format = "%.3f") = 0;

    // Columns (legacy layout)
    virtual void Columns(int count = 1, const char* id = nullptr, bool border = true) = 0;
    virtual void NextColumn() = 0;
    virtual void SetColumnWidth(int column_index, float width) = 0;

    // Tooltip (explicit begin/end for multi-line content)
    virtual void BeginTooltip() = 0;
    virtual void EndTooltip() = 0;

    // Text helpers
    virtual void BulletText(const char* fmt, ...) = 0;
    virtual float GetTextLineHeight() = 0;
    virtual float GetTextLineHeightWithSpacing() = 0;

    // Input
    virtual bool InputTextWithHint(const char* label, const char* hint, char* buf, size_t buf_size) = 0;

    // Groups
    virtual void BeginGroup() = 0;
    virtual void EndGroup() = 0;

    // Style (read-only access for colors and layout)
    virtual const ImGuiStyle& GetStyle() = 0;

    // Window (Begin/End for e.g. debug windows)
    virtual bool Begin(const char* name, bool* p_open, int flags = 0) = 0;
    virtual void End() = 0;
    virtual void SetNextWindowPos(const ImVec2& pos, int cond = 0, const ImVec2& pivot = ImVec2(0.f, 0.f)) = 0;
    virtual void SetNextWindowSize(const ImVec2& size, int cond = 0) = 0;
    virtual ImVec2 GetDisplaySize() = 0;
    virtual const ImGuiIO& GetIO() = 0;
    virtual unsigned int GetFrameCount() = 0;

    // Tab bar
    virtual bool BeginTabBar(const char* str_id, int flags = 0) = 0;
    virtual bool BeginTabItem(const char* label, bool* p_open = nullptr, int flags = 0) = 0;
    virtual void EndTabItem() = 0;
    virtual void EndTabBar() = 0;

    // Input (key state; key is ImGuiKey_* as int)
    virtual bool IsKeyDown(int key) = 0;

    // Popup / modal
    virtual void OpenPopup(const char* str_id) = 0;
    virtual bool BeginPopupModal(const char* name, bool* p_open, int flags = 0) = 0;
    virtual void EndPopup() = 0;
    virtual bool BeginPopupContextItem(const char* str_id = nullptr, int popup_flags = 1) = 0;
    virtual bool MenuItem(const char* label, const char* shortcut = nullptr, bool selected = false,
                          bool enabled = true) = 0;
};

} // namespace ui
} // namespace display_commander
