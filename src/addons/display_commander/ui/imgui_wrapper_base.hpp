#pragma once

/**
 * Base types and interface for ImGui abstraction.
 * Shared UI code (e.g. Nvidia Profile tab) uses IImGuiWrapper so it can run
 * with either ReShade's ImGui or the standalone ImGui (ImGuiStandalone).
 * No imgui.h dependency here.
 */

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

/** 2D vector for layout (avoids including imgui.h in base). */
struct ImGuiWrapperVec2 {
    float x = 0.f;
    float y = 0.f;
};

/** RGBA color (avoids including imgui.h in base). */
struct ImGuiWrapperColor {
    float r = 0.f, g = 0.f, b = 0.f, a = 1.f;
};

/** Common UI colors used by shared tabs. Match res/ui_colors.hpp semantics. */
namespace wrapper_colors {
constexpr ImGuiWrapperColor TEXT_DIMMED   = {0.7f, 0.7f, 0.7f, 1.0f};
constexpr ImGuiWrapperColor ICON_ERROR   = {1.0f, 0.2f, 0.2f, 1.0f};
constexpr ImGuiWrapperColor ICON_WARNING = {1.0f, 0.7f, 0.0f, 1.0f};
constexpr ImGuiWrapperColor ICON_SUCCESS = {0.2f, 0.8f, 0.2f, 1.0f};
} // namespace wrapper_colors

/** ImGui table/tree flags as int (same numeric values as imgui.h for compatibility). */
namespace wrapper_flags {
constexpr int TreeNodeFlags_DefaultOpen = 1 << 5;
constexpr int TableFlags_BordersOuter   = (1 << 8) | (1 << 10);
constexpr int TableFlags_BordersH      = (1 << 7) | (1 << 8);
constexpr int TableFlags_SizingStretchProp = 3 << 13;
constexpr int TableFlags_ScrollY       = 1 << 25;
constexpr int TableColumnFlags_WidthStretch = 1 << 3;
} // namespace wrapper_flags

/** Abstract ImGui backend for shared UI code. */
struct IImGuiWrapper {
    virtual ~IImGuiWrapper() = default;

    virtual void SameLine(float offset_from_start_x = 0.f) = 0;
    virtual void Text(const char* fmt, ...) = 0;
    virtual void TextColored(ImGuiWrapperColor col, const char* fmt, ...) = 0;
    virtual void TextUnformatted(const char* text) = 0;
    virtual bool Button(const char* label) = 0;
    virtual bool SmallButton(const char* label) = 0;
    virtual bool Checkbox(const char* label, bool* v) = 0;
    virtual bool IsItemHovered() = 0;
    virtual void SetTooltip(const char* fmt, ...) = 0;
    virtual void Spacing() = 0;
    virtual bool BeginChild(const char* str_id, ImGuiWrapperVec2 size, bool border) = 0;
    virtual void EndChild() = 0;
    virtual bool CollapsingHeader(const char* label, int flags = 0) = 0;
    virtual bool BeginTable(const char* str_id, int columns, int flags) = 0;
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
    virtual void PopID() = 0;
    virtual ImGuiWrapperVec2 GetContentRegionAvail() = 0;
    virtual float GetStyleItemSpacingX() = 0;
    virtual float GetStyleFramePaddingX() = 0;
    virtual ImGuiWrapperVec2 CalcTextSize(const char* text) = 0;
    virtual void SetNextItemWidth(float width) = 0;
    virtual void BeginDisabled() = 0;
    virtual void EndDisabled() = 0;
};

} // namespace ui
} // namespace display_commander
