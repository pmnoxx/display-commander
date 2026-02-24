// Compiled with ImGui=ImGuiStandalone so ImGui::* resolves to ImGuiStandalone::*
#include "imgui_wrapper_standalone.hpp"

#include <imgui.h>
#include <cstdarg>

namespace display_commander {
namespace ui {

static ImVec2 to_ImVec2(ImGuiWrapperVec2 v) { return ImVec2(v.x, v.y); }
static ImVec4 to_ImVec4(ImGuiWrapperColor c) { return ImVec4(c.r, c.g, c.b, c.a); }
static ImGuiWrapperVec2 from_ImVec2(const ImVec2& v) { return {v.x, v.y}; }

void ImGuiWrapperStandalone::SameLine(float offset_from_start_x) {
    ImGui::SameLine(offset_from_start_x);
}
void ImGuiWrapperStandalone::Text(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    ImGui::TextV(fmt, args);
    va_end(args);
}
void ImGuiWrapperStandalone::TextColored(ImGuiWrapperColor col, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    ImGui::TextColoredV(to_ImVec4(col), fmt, args);
    va_end(args);
}
void ImGuiWrapperStandalone::TextUnformatted(const char* text) {
    ImGui::TextUnformatted(text);
}
bool ImGuiWrapperStandalone::Button(const char* label) {
    return ImGui::Button(label);
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
void ImGuiWrapperStandalone::SetTooltip(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    ImGui::SetTooltipV(fmt, args);
    va_end(args);
}
void ImGuiWrapperStandalone::Spacing() {
    ImGui::Spacing();
}
bool ImGuiWrapperStandalone::BeginChild(const char* str_id, ImGuiWrapperVec2 size, bool border) {
    return ImGui::BeginChild(str_id, to_ImVec2(size), border);
}
void ImGuiWrapperStandalone::EndChild() {
    ImGui::EndChild();
}
bool ImGuiWrapperStandalone::CollapsingHeader(const char* label, int flags) {
    return ImGui::CollapsingHeader(label, static_cast<ImGuiTreeNodeFlags>(flags));
}
bool ImGuiWrapperStandalone::BeginTable(const char* str_id, int columns, int flags) {
    return ImGui::BeginTable(str_id, columns, static_cast<ImGuiTableFlags>(flags));
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
void ImGuiWrapperStandalone::TableSetColumnIndex(int column_n) {
    ImGui::TableSetColumnIndex(column_n);
}
bool ImGuiWrapperStandalone::BeginCombo(const char* label, const char* preview_value, int flags) {
    return ImGui::BeginCombo(label, preview_value, flags);
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
void ImGuiWrapperStandalone::PopID() {
    ImGui::PopID();
}
ImGuiWrapperVec2 ImGuiWrapperStandalone::GetContentRegionAvail() {
    return from_ImVec2(ImGui::GetContentRegionAvail());
}
float ImGuiWrapperStandalone::GetStyleItemSpacingX() {
    return ImGui::GetStyle().ItemSpacing.x;
}
float ImGuiWrapperStandalone::GetStyleFramePaddingX() {
    return ImGui::GetStyle().FramePadding.x;
}
ImGuiWrapperVec2 ImGuiWrapperStandalone::CalcTextSize(const char* text) {
    return from_ImVec2(ImGui::CalcTextSize(text));
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

} // namespace ui
} // namespace display_commander
