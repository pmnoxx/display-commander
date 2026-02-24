#pragma once

#include "imgui_wrapper_base.hpp"

namespace display_commander {
namespace ui {

/** ImGui wrapper that forwards to standalone ImGui (ImGuiStandalone). Used in CLI/installer UI. */
struct ImGuiWrapperStandalone : IImGuiWrapper {
    void SameLine(float offset_from_start_x = 0.f, float spacing_w = -1.f) override;
    void Text(const char* fmt, ...) override;
    void TextColored(const ImVec4& col, const char* fmt, ...) override;
    void TextUnformatted(const char* text) override;
    bool Button(const char* label) override;
    bool SmallButton(const char* label) override;
    bool Checkbox(const char* label, bool* v) override;
    bool IsItemHovered() override;
    bool IsItemActive() override;
    bool IsItemDeactivatedAfterEdit() override;
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
    void TableNextColumn() override;
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
    void TextDisabled(const char* fmt, ...) override;
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

    void PlotLines(const char* label, const float* values, int values_count, int values_offset,
                   const char* overlay_text, float scale_min, float scale_max,
                   const ImVec2& graph_size) override;
    bool Combo(const char* label, int* current_item, const char* const items[], int items_count) override;
    ImDrawList* GetWindowDrawList() override;
    ImVec2 GetCursorScreenPos() override;
    void SetCursorScreenPos(const ImVec2& pos) override;
    float GetCursorPosX() override;
    void Dummy(const ImVec2& size) override;
    ImU32 GetColorU32(int col_enum) override;
    ImU32 ColorConvertFloat4ToU32(const ImVec4& col) override;
    bool SliderFloat(const char* label, float* v, float v_min, float v_max,
                     const char* format = "%.3f") override;
    void Columns(int count = 1, const char* id = nullptr, bool border = true) override;
    void NextColumn() override;
    void BeginTooltip() override;
    void EndTooltip() override;
    void BulletText(const char* fmt, ...) override;
    float GetTextLineHeight() override;
    float GetTextLineHeightWithSpacing() override;
    bool InputTextWithHint(const char* label, const char* hint, char* buf, size_t buf_size) override;
    void BeginGroup() override;
    void EndGroup() override;
    const ImGuiStyle& GetStyle() override;
    bool Begin(const char* name, bool* p_open, int flags = 0) override;
    void End() override;
};

} // namespace ui
} // namespace display_commander
