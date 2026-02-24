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
constexpr int TreeNodeFlags_DefaultOpen = 1 << 5;
constexpr int TableFlags_BordersOuter   = (1 << 8) | (1 << 10);
constexpr int TableFlags_BordersH      = (1 << 7) | (1 << 8);
constexpr int TableFlags_SizingStretchProp = 3 << 13;
constexpr int TableFlags_ScrollY       = 1 << 25;
constexpr int TableFlags_RowBg         = 1 << 4;
constexpr int TableFlags_Borders       = (1 << 7) | (1 << 8) | (1 << 9) | (1 << 10);
constexpr int TableFlags_SizingFixedFit = 2 << 13;
constexpr int TableColumnFlags_WidthStretch = 1 << 3;
constexpr int TableColumnFlags_WidthFixed   = 1 << 4;
} // namespace wrapper_flags

/** Abstract ImGui backend for shared UI code. */
struct IImGuiWrapper {
    virtual ~IImGuiWrapper() = default;

    virtual void SameLine(float offset_from_start_x = 0.f) = 0;
    virtual void Text(const char* fmt, ...) = 0;
    virtual void TextColored(const ImVec4& col, const char* fmt, ...) = 0;
    virtual void TextUnformatted(const char* text) = 0;
    virtual bool Button(const char* label) = 0;
    virtual bool SmallButton(const char* label) = 0;
    virtual bool Checkbox(const char* label, bool* v) = 0;
    virtual bool IsItemHovered() = 0;
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
    virtual bool SliderInt(const char* label, int* v, int v_min, int v_max, const char* format = "%d") = 0;
    virtual void TextWrapped(const char* fmt, ...) = 0;
    virtual void PushStyleColor(int col_enum, const ImVec4& color) = 0;
    virtual void PopStyleColor(int count = 1) = 0;
    virtual bool TreeNodeEx(const char* label, int flags) = 0;
    virtual void TreePop() = 0;
    virtual ImVec2 GetContentRegionAvail() = 0;
    virtual float GetStyleItemSpacingX() = 0;
    virtual float GetStyleFramePaddingX() = 0;
    virtual ImVec2 CalcTextSize(const char* text) = 0;
    virtual void SetNextItemWidth(float width) = 0;
    virtual void BeginDisabled() = 0;
    virtual void EndDisabled() = 0;
};

} // namespace ui
} // namespace display_commander
