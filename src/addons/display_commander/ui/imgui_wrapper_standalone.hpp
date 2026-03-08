#pragma once

#include "imgui_wrapper_base.hpp"

#include <imgui.h>

namespace display_commander {
namespace ui {

/** Draw list proxy for standalone ImGui: forwards so callers never touch raw ImDrawList* (same TU = same ABI). */
struct ImDrawListProxyStandalone : IImDrawList {
    ImDrawList* list_ = nullptr;
    void set(ImDrawList* L) { list_ = L; }
    void AddLine(const ImVec2& p1, const ImVec2& p2, ImU32 col, float thickness = 1.0f) override;
    void AddRect(const ImVec2& p_min, const ImVec2& p_max, ImU32 col, float rounding = 0.0f, int flags = 0,
                 float thickness = 1.0f) override;
    void AddRectFilled(const ImVec2& p_min, const ImVec2& p_max, ImU32 col, float rounding = 0.0f,
                       int flags = 0) override;
    void AddCircle(const ImVec2& center, float radius, ImU32 col, int num_segments = 0,
                   float thickness = 1.0f) override;
    void AddCircleFilled(const ImVec2& center, float radius, ImU32 col, int num_segments = 0) override;
    void AddTriangleFilled(const ImVec2& p1, const ImVec2& p2, const ImVec2& p3, ImU32 col) override;
};

/** ImGui wrapper that forwards to standalone ImGui (ImGuiStandalone). Used in CLI/installer UI. */
struct ImGuiWrapperStandalone : IImGuiWrapper {
    ImDrawListProxyStandalone draw_list_proxy_;
    void SameLine(float offset_from_start_x = 0.f, float spacing_w = -1.f) override;
    void Text(const char* fmt, ...) override;
    void TextColored(const ImVec4& col, const char* fmt, ...) override;
    void TextUnformatted(const char* text) override;
    bool Button(const char* label) override;
    bool Button(const char* label, const ImVec2& size) override;
    bool SmallButton(const char* label) override;
    bool Checkbox(const char* label, bool* v) override;
    bool IsItemHovered() override;
    bool IsItemActive() override;
    bool IsItemDeactivatedAfterEdit() override;
    void SetTooltip(const char* fmt, ...) override;
    void SetTooltipExV(float wrap_width, const char* fmt, va_list args) override;
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
    bool InputText(const char* label, char* buf, size_t buf_size, int flags) override;
    bool InputFloat(const char* label, float* v, float step = 0.0f, float step_fast = 0.0f, const char* format = "%.3f",
                    int flags = 0) override;
    bool InputInt(const char* label, int* v, int step = 1, int step_fast = 0, int flags = 0) override;
    bool SliderInt(const char* label, int* v, int v_min, int v_max, const char* format = "%d") override;
    void TextWrapped(const char* fmt, ...) override;
    void TextDisabled(const char* fmt, ...) override;
    void PushStyleColor(int col_enum, const ImVec4& color) override;
    void PopStyleColor(int count = 1) override;
    bool TreeNodeEx(const char* label, int flags) override;
    bool TreeNode(const char* label) override;
    void TreePop() override;
    ImVec2 GetContentRegionAvail() override;
    float GetStyleItemSpacingX() override;
    float GetStyleFramePaddingX() override;
    ImVec2 CalcTextSize(const char* text) override;
    void SetNextItemWidth(float width) override;
    void BeginDisabled() override;
    void EndDisabled() override;

    void PlotLines(const char* label, const float* values, int values_count, int values_offset,
                   const char* overlay_text, float scale_min, float scale_max, const ImVec2& graph_size) override;
    bool Combo(const char* label, int* current_item, const char* const items[], int items_count) override;
    IImDrawList* GetWindowDrawList() override;
    ImVec2 GetCursorScreenPos() override;
    void SetCursorScreenPos(const ImVec2& pos) override;
    float GetCursorPosX() override;
    void Dummy(const ImVec2& size) override;
    ImVec2 GetItemRectMin() override;
    ImVec2 GetItemRectSize() override;
    void ProgressBar(float fraction, const ImVec2& size_arg, const char* overlay) override;
    ImU32 GetColorU32(int col_enum) override;
    ImU32 ColorConvertFloat4ToU32(const ImVec4& col) override;
    bool SliderFloat(const char* label, float* v, float v_min, float v_max, const char* format = "%.3f") override;
    void Columns(int count = 1, const char* id = nullptr, bool border = true) override;
    void NextColumn() override;
    void SetColumnWidth(int column_index, float width) override;
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
    void SetNextWindowPos(const ImVec2& pos, int cond = 0, const ImVec2& pivot = ImVec2(0.f, 0.f)) override;
    void SetNextWindowSize(const ImVec2& size, int cond = 0) override;
    void SetNextWindowBgAlpha(float alpha) override;
    ImVec2 GetWindowPos() override;
    IImDrawList* GetForegroundDrawList() override;
    ImVec2 GetDisplaySize() override;
    const ImGuiIO& GetIO() override;
    unsigned int GetFrameCount() override;
    void NewFrame() override;
    void Render() override;
    ImDrawData* GetDrawData() override;
    void CreateContext() override;
    void DestroyContext() override;
    void StyleColorsDark() override;
    void SetConfigFlags(uint32_t flags) override;
    void SetDisplaySize(const ImVec2& size) override;
    void SetFontGlobalScale(float scale) override;
    ImGuiIO* GetIOForFontSetup() override;
    bool BeginTabBar(const char* str_id, int flags = 0) override;
    bool BeginTabItem(const char* label, bool* p_open = nullptr, int flags = 0) override;
    void EndTabItem() override;
    void EndTabBar() override;
    bool IsKeyDown(int key) override;
    bool IsKeyPressed(int key) override;
    void SetKeyboardFocusHere(int offset = 0) override;
    void OpenPopup(const char* str_id) override;
    bool BeginPopupModal(const char* name, bool* p_open, int flags) override;
    void EndPopup() override;
    bool BeginPopupContextItem(const char* str_id, int popup_flags) override;
    bool MenuItem(const char* label, const char* shortcut, bool selected, bool enabled) override;
};

}  // namespace ui
}  // namespace display_commander
