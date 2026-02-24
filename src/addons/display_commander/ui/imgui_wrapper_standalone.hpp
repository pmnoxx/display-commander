#pragma once

#include "imgui_wrapper_base.hpp"

namespace display_commander {
namespace ui {

/** ImGui wrapper that forwards to standalone ImGui (ImGuiStandalone). Used in CLI/installer UI. */
struct ImGuiWrapperStandalone : IImGuiWrapper {
    void SameLine(float offset_from_start_x = 0.f) override;
    void Text(const char* fmt, ...) override;
    void TextColored(const ImVec4& col, const char* fmt, ...) override;
    void TextUnformatted(const char* text) override;
    bool Button(const char* label) override;
    bool SmallButton(const char* label) override;
    bool Checkbox(const char* label, bool* v) override;
    bool IsItemHovered() override;
    void SetTooltip(const char* fmt, ...) override;
    void Spacing() override;
    void Separator() override;
    bool BeginChild(const char* str_id, const ImVec2& size, bool border) override;
    void EndChild() override;
    bool CollapsingHeader(const char* label, int flags = 0) override;
    bool BeginTable(const char* str_id, int columns, int flags, const ImVec2& outer_size = ImVec2(0.f, 0.f)) override;
    void EndTable() override;
    void TableSetupColumn(const char* label, int flags = 0, float width_weight = 0.f) override;
    void TableSetupScrollFreeze(int cols, int rows) override;
    void TableHeadersRow() override;
    void TableNextRow() override;
    void TableSetColumnIndex(int column_n) override;
    bool BeginCombo(const char* label, const char* preview_value, int flags = 0) override;
    void EndCombo() override;
    bool Selectable(const char* label, bool selected = false) override;
    void SetItemDefaultFocus() override;
    void PushID(int id) override;
    void PushID(const char* str_id) override;
    void PopID() override;
    void Indent() override;
    void Unindent() override;
    bool InputText(const char* label, char* buf, size_t buf_size) override;
    bool SliderInt(const char* label, int* v, int v_min, int v_max, const char* format = "%d") override;
    void TextWrapped(const char* fmt, ...) override;
    void PushStyleColor(int col_enum, const ImVec4& color) override;
    void PopStyleColor(int count = 1) override;
    bool TreeNodeEx(const char* label, int flags) override;
    void TreePop() override;
    ImVec2 GetContentRegionAvail() override;
    float GetStyleItemSpacingX() override;
    float GetStyleFramePaddingX() override;
    ImVec2 CalcTextSize(const char* text) override;
    void SetNextItemWidth(float width) override;
    void BeginDisabled() override;
    void EndDisabled() override;
};

} // namespace ui
} // namespace display_commander
