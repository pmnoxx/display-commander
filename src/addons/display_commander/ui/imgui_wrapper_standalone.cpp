// Compiled with ImGui=ImGuiStandalone so ImGui::* resolves to ImGuiStandalone::*
#include "imgui_wrapper_standalone.hpp"

#include <imgui.h>
#include <cstdarg>

namespace display_commander {
namespace ui {

void ImGuiWrapperStandalone::SameLine(float offset_from_start_x, float spacing_w) {
    ImGui::SameLine(offset_from_start_x, spacing_w);
}
void ImGuiWrapperStandalone::Text(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    ImGui::TextV(fmt, args);
    va_end(args);
}
void ImGuiWrapperStandalone::TextColored(const ImVec4& col, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    ImGui::TextColoredV(col, fmt, args);
    va_end(args);
}
void ImGuiWrapperStandalone::TextUnformatted(const char* text) {
    ImGui::TextUnformatted(text);
}
bool ImGuiWrapperStandalone::Button(const char* label) {
    return ImGui::Button(label);
}
bool ImGuiWrapperStandalone::Button(const char* label, const ImVec2& size) {
    return ImGui::Button(label, size);
}
bool ImGuiWrapperStandalone::SmallButton(const char* label) {
    return ImGui::SmallButton(label);
}
bool ImGuiWrapperStandalone::Checkbox(const char* label, bool* v) {
    return ImGui::Checkbox(label, v);
}
bool ImGuiWrapperStandalone::IsItemHovered() {
    return ImGui::IsItemHovered();
}
bool ImGuiWrapperStandalone::IsItemActive() {
    return ImGui::IsItemActive();
}
bool ImGuiWrapperStandalone::IsItemDeactivatedAfterEdit() {
    return ImGui::IsItemDeactivatedAfterEdit();
}
void ImGuiWrapperStandalone::SetTooltip(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    ImGui::SetTooltipV(fmt, args);
    va_end(args);
}
void ImGuiWrapperStandalone::Spacing() {
    ImGui::Spacing();
}
void ImGuiWrapperStandalone::Separator() {
    ImGui::Separator();
}
bool ImGuiWrapperStandalone::BeginChild(const char* str_id, const ImVec2& size, bool border) {
    return ImGui::BeginChild(str_id, size, border);
}
void ImGuiWrapperStandalone::EndChild() {
    ImGui::EndChild();
}
bool ImGuiWrapperStandalone::CollapsingHeader(const char* label, int flags) {
    return ImGui::CollapsingHeader(label, static_cast<ImGuiTreeNodeFlags>(flags));
}
bool ImGuiWrapperStandalone::BeginTable(const char* str_id, int columns, int flags,
                                        const ImVec2& outer_size) {
    return ImGui::BeginTable(str_id, columns, static_cast<ImGuiTableFlags>(flags), outer_size);
}
void ImGuiWrapperStandalone::EndTable() {
    ImGui::EndTable();
}
void ImGuiWrapperStandalone::TableSetupColumn(const char* label, int flags, float width_weight) {
    ImGui::TableSetupColumn(label, static_cast<ImGuiTableColumnFlags>(flags), width_weight);
}
void ImGuiWrapperStandalone::TableSetupScrollFreeze(int cols, int rows) {
    ImGui::TableSetupScrollFreeze(cols, rows);
}
void ImGuiWrapperStandalone::TableHeadersRow() {
    ImGui::TableHeadersRow();
}
void ImGuiWrapperStandalone::TableNextRow() {
    ImGui::TableNextRow();
}
void ImGuiWrapperStandalone::TableNextColumn() {
    ImGui::TableNextColumn();
}
void ImGuiWrapperStandalone::TableSetColumnIndex(int column_n) {
    ImGui::TableSetColumnIndex(column_n);
}
bool ImGuiWrapperStandalone::BeginCombo(const char* label, const char* preview_value, int flags) {
    return ImGui::BeginCombo(label, preview_value, static_cast<ImGuiComboFlags>(flags));
}
void ImGuiWrapperStandalone::EndCombo() {
    ImGui::EndCombo();
}
bool ImGuiWrapperStandalone::Selectable(const char* label, bool selected) {
    return ImGui::Selectable(label, selected);
}
void ImGuiWrapperStandalone::SetItemDefaultFocus() {
    ImGui::SetItemDefaultFocus();
}
void ImGuiWrapperStandalone::PushID(int id) {
    ImGui::PushID(id);
}
void ImGuiWrapperStandalone::PushID(const char* str_id) {
    ImGui::PushID(str_id);
}
void ImGuiWrapperStandalone::PopID() {
    ImGui::PopID();
}
void ImGuiWrapperStandalone::Indent() {
    ImGui::Indent();
}
void ImGuiWrapperStandalone::Unindent() {
    ImGui::Unindent();
}
bool ImGuiWrapperStandalone::InputText(const char* label, char* buf, size_t buf_size) {
    return ImGui::InputText(label, buf, buf_size);
}
bool ImGuiWrapperStandalone::InputText(const char* label, char* buf, size_t buf_size, int flags) {
    return ImGui::InputText(label, buf, buf_size, static_cast<ImGuiInputTextFlags>(flags));
}
bool ImGuiWrapperStandalone::InputFloat(const char* label, float* v, float step, float step_fast,
                                        const char* format, int flags) {
    return ImGui::InputFloat(label, v, step, step_fast, format, static_cast<ImGuiInputTextFlags>(flags));
}
bool ImGuiWrapperStandalone::InputInt(const char* label, int* v, int step, int step_fast, int flags) {
    return ImGui::InputInt(label, v, step, step_fast, static_cast<ImGuiInputTextFlags>(flags));
}
bool ImGuiWrapperStandalone::SliderInt(const char* label, int* v, int v_min, int v_max, const char* format) {
    return ImGui::SliderInt(label, v, v_min, v_max, format);
}
void ImGuiWrapperStandalone::TextWrapped(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    ImGui::TextWrappedV(fmt, args);
    va_end(args);
}
void ImGuiWrapperStandalone::TextDisabled(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    ImGui::TextDisabledV(fmt, args);
    va_end(args);
}
void ImGuiWrapperStandalone::PushStyleColor(int col_enum, const ImVec4& color) {
    ImGui::PushStyleColor(static_cast<ImGuiCol>(col_enum), color);
}
void ImGuiWrapperStandalone::PopStyleColor(int count) {
    ImGui::PopStyleColor(count);
}
bool ImGuiWrapperStandalone::TreeNodeEx(const char* label, int flags) {
    return ImGui::TreeNodeEx(label, static_cast<ImGuiTreeNodeFlags>(flags));
}
bool ImGuiWrapperStandalone::TreeNode(const char* label) {
    return ImGui::TreeNode(label);
}
void ImGuiWrapperStandalone::TreePop() {
    ImGui::TreePop();
}
ImVec2 ImGuiWrapperStandalone::GetContentRegionAvail() {
    return ImGui::GetContentRegionAvail();
}
float ImGuiWrapperStandalone::GetStyleItemSpacingX() {
    return ImGui::GetStyle().ItemSpacing.x;
}
float ImGuiWrapperStandalone::GetStyleFramePaddingX() {
    return ImGui::GetStyle().FramePadding.x;
}
ImVec2 ImGuiWrapperStandalone::CalcTextSize(const char* text) {
    return ImGui::CalcTextSize(text);
}
void ImGuiWrapperStandalone::SetNextItemWidth(float width) {
    ImGui::SetNextItemWidth(width);
}
void ImGuiWrapperStandalone::BeginDisabled() {
    ImGui::BeginDisabled();
}
void ImGuiWrapperStandalone::EndDisabled() {
    ImGui::EndDisabled();
}

void ImGuiWrapperStandalone::PlotLines(const char* label, const float* values, int values_count, int values_offset,
                                       const char* overlay_text, float scale_min, float scale_max,
                                       const ImVec2& graph_size) {
    ImGui::PlotLines(label, values, values_count, values_offset, overlay_text, scale_min, scale_max, graph_size);
}
bool ImGuiWrapperStandalone::Combo(const char* label, int* current_item, const char* const items[],
                                   int items_count) {
    return ImGui::Combo(label, current_item, items, items_count);
}
void ImDrawListProxyStandalone::AddLine(const ImVec2& p1, const ImVec2& p2, ImU32 col, float thickness) {
    if (list_) list_->AddLine(p1, p2, col, thickness);
}
void ImDrawListProxyStandalone::AddRect(const ImVec2& p_min, const ImVec2& p_max, ImU32 col, float rounding,
                                       int flags, float thickness) {
    if (list_) list_->AddRect(p_min, p_max, col, rounding, static_cast<ImDrawFlags>(flags), thickness);
}
void ImDrawListProxyStandalone::AddRectFilled(const ImVec2& p_min, const ImVec2& p_max, ImU32 col,
                                             float rounding, int flags) {
    if (list_) list_->AddRectFilled(p_min, p_max, col, rounding, static_cast<ImDrawFlags>(flags));
}
void ImDrawListProxyStandalone::AddCircle(const ImVec2& center, float radius, ImU32 col, int num_segments,
                                          float thickness) {
    if (list_) list_->AddCircle(center, radius, col, num_segments, thickness);
}
void ImDrawListProxyStandalone::AddCircleFilled(const ImVec2& center, float radius, ImU32 col, int num_segments) {
    if (list_) list_->AddCircleFilled(center, radius, col, num_segments);
}
void ImDrawListProxyStandalone::AddTriangleFilled(const ImVec2& p1, const ImVec2& p2, const ImVec2& p3, ImU32 col) {
    if (list_) list_->AddTriangleFilled(p1, p2, p3, col);
}

IImDrawList* ImGuiWrapperStandalone::GetWindowDrawList() {
    ImDrawList* L = ImGui::GetWindowDrawList();
    draw_list_proxy_.set(L);
    return L ? &draw_list_proxy_ : nullptr;
}
ImVec2 ImGuiWrapperStandalone::GetCursorScreenPos() {
    return ImGui::GetCursorScreenPos();
}
void ImGuiWrapperStandalone::SetCursorScreenPos(const ImVec2& pos) {
    ImGui::SetCursorScreenPos(pos);
}
float ImGuiWrapperStandalone::GetCursorPosX() {
    return ImGui::GetCursorPosX();
}
void ImGuiWrapperStandalone::Dummy(const ImVec2& size) {
    ImGui::Dummy(size);
}
ImVec2 ImGuiWrapperStandalone::GetItemRectMin() {
    return ImGui::GetItemRectMin();
}
ImVec2 ImGuiWrapperStandalone::GetItemRectSize() {
    return ImGui::GetItemRectSize();
}
void ImGuiWrapperStandalone::ProgressBar(float fraction, const ImVec2& size_arg, const char* overlay) {
    ImGui::ProgressBar(fraction, size_arg, overlay);
}
ImU32 ImGuiWrapperStandalone::GetColorU32(int col_enum) {
    return ImGui::GetColorU32(static_cast<ImGuiCol>(col_enum));
}
ImU32 ImGuiWrapperStandalone::ColorConvertFloat4ToU32(const ImVec4& col) {
    return ImGui::ColorConvertFloat4ToU32(col);
}
bool ImGuiWrapperStandalone::SliderFloat(const char* label, float* v, float v_min, float v_max,
                                         const char* format) {
    return ImGui::SliderFloat(label, v, v_min, v_max, format);
}
void ImGuiWrapperStandalone::Columns(int count, const char* id, bool border) {
    ImGui::Columns(count, id, border);
}
void ImGuiWrapperStandalone::NextColumn() {
    ImGui::NextColumn();
}

void ImGuiWrapperStandalone::SetColumnWidth(int column_index, float width) {
    ImGui::SetColumnWidth(column_index, width);
}
void ImGuiWrapperStandalone::BeginTooltip() {
    ImGui::BeginTooltip();
}
void ImGuiWrapperStandalone::EndTooltip() {
    ImGui::EndTooltip();
}
void ImGuiWrapperStandalone::BulletText(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    ImGui::BulletTextV(fmt, args);
    va_end(args);
}
float ImGuiWrapperStandalone::GetTextLineHeight() {
    return ImGui::GetTextLineHeight();
}
float ImGuiWrapperStandalone::GetTextLineHeightWithSpacing() {
    return ImGui::GetTextLineHeightWithSpacing();
}
bool ImGuiWrapperStandalone::InputTextWithHint(const char* label, const char* hint, char* buf, size_t buf_size) {
    return ImGui::InputTextWithHint(label, hint, buf, buf_size);
}
void ImGuiWrapperStandalone::BeginGroup() {
    ImGui::BeginGroup();
}
void ImGuiWrapperStandalone::EndGroup() {
    ImGui::EndGroup();
}
const ImGuiStyle& ImGuiWrapperStandalone::GetStyle() {
    return ImGui::GetStyle();
}
bool ImGuiWrapperStandalone::Begin(const char* name, bool* p_open, int flags) {
    return ImGui::Begin(name, p_open, static_cast<ImGuiWindowFlags>(flags));
}
void ImGuiWrapperStandalone::End() {
    ImGui::End();
}
void ImGuiWrapperStandalone::SetNextWindowPos(const ImVec2& pos, int cond, const ImVec2& pivot) {
    ImGui::SetNextWindowPos(pos, static_cast<ImGuiCond>(cond), pivot);
}
void ImGuiWrapperStandalone::SetNextWindowSize(const ImVec2& size, int cond) {
    ImGui::SetNextWindowSize(size, static_cast<ImGuiCond>(cond));
}
ImVec2 ImGuiWrapperStandalone::GetDisplaySize() {
    const ImGuiIO& io = ImGui::GetIO();
    return ImVec2(io.DisplaySize.x, io.DisplaySize.y);
}
const ImGuiIO& ImGuiWrapperStandalone::GetIO() {
    return ImGui::GetIO();
}
unsigned int ImGuiWrapperStandalone::GetFrameCount() {
    return static_cast<unsigned int>(ImGui::GetFrameCount());
}
bool ImGuiWrapperStandalone::BeginTabBar(const char* str_id, int flags) {
    return ImGui::BeginTabBar(str_id, static_cast<ImGuiTabBarFlags>(flags));
}
bool ImGuiWrapperStandalone::BeginTabItem(const char* label, bool* p_open, int flags) {
    return ImGui::BeginTabItem(label, p_open, static_cast<ImGuiTabItemFlags>(flags));
}
void ImGuiWrapperStandalone::EndTabItem() {
    ImGui::EndTabItem();
}
void ImGuiWrapperStandalone::EndTabBar() {
    ImGui::EndTabBar();
}
bool ImGuiWrapperStandalone::IsKeyDown(int key) {
    return ImGui::IsKeyDown(static_cast<ImGuiKey>(key));
}
void ImGuiWrapperStandalone::OpenPopup(const char* str_id) {
    ImGui::OpenPopup(str_id);
}
bool ImGuiWrapperStandalone::BeginPopupModal(const char* name, bool* p_open, int flags) {
    return ImGui::BeginPopupModal(name, p_open, static_cast<ImGuiWindowFlags>(flags));
}
void ImGuiWrapperStandalone::EndPopup() {
    ImGui::EndPopup();
}

} // namespace ui
} // namespace display_commander
