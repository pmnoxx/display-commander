#pragma once

#include "imgui_wrapper_base.hpp"

#include <imgui.h>
#include <cstdarg>

namespace display_commander {
namespace ui {

/** ImGui wrapper that forwards to ReShade's ImGui (used in addon overlay). Header-only so ImGui symbols stay in the same TU as overlay code. */
struct ImGuiWrapperReshade : IImGuiWrapper {
    static inline ImVec2 to_ImVec2(ImGuiWrapperVec2 v) { return ImVec2(v.x, v.y); }
    static inline ImVec4 to_ImVec4(ImGuiWrapperColor c) { return ImVec4(c.r, c.g, c.b, c.a); }
    static inline ImGuiWrapperVec2 from_ImVec2(const ImVec2& v) { return {v.x, v.y}; }

    void SameLine(float offset_from_start_x = 0.f) override { ImGui::SameLine(offset_from_start_x); }
    void Text(const char* fmt, ...) override {
        va_list args;
        va_start(args, fmt);
        ImGui::TextV(fmt, args);
        va_end(args);
    }
    void TextColored(ImGuiWrapperColor col, const char* fmt, ...) override {
        va_list args;
        va_start(args, fmt);
        ImGui::TextColoredV(to_ImVec4(col), fmt, args);
        va_end(args);
    }
    void TextUnformatted(const char* text) override { ImGui::TextUnformatted(text); }
    bool Button(const char* label) override { return ImGui::Button(label); }
    bool SmallButton(const char* label) override { return ImGui::SmallButton(label); }
    bool Checkbox(const char* label, bool* v) override { return ImGui::Checkbox(label, v); }
    bool IsItemHovered() override { return ImGui::IsItemHovered(); }
    void SetTooltip(const char* fmt, ...) override {
        va_list args;
        va_start(args, fmt);
        ImGui::SetTooltipV(fmt, args);
        va_end(args);
    }
    void Spacing() override { ImGui::Spacing(); }
    void Separator() override { ImGui::Separator(); }
    bool BeginChild(const char* str_id, ImGuiWrapperVec2 size, bool border) override {
        return ImGui::BeginChild(str_id, to_ImVec2(size), border);
    }
    void EndChild() override { ImGui::EndChild(); }
    bool CollapsingHeader(const char* label, int flags = 0) override {
        return ImGui::CollapsingHeader(label, static_cast<ImGuiTreeNodeFlags>(flags));
    }
    bool BeginTable(const char* str_id, int columns, int flags, ImGuiWrapperVec2 outer_size = {0.f, 0.f}) override {
        return ImGui::BeginTable(str_id, columns, static_cast<ImGuiTableFlags>(flags), to_ImVec2(outer_size));
    }
    void EndTable() override { ImGui::EndTable(); }
    void TableSetupColumn(const char* label, int flags = 0, float width_weight = 0.f) override {
        ImGui::TableSetupColumn(label, static_cast<ImGuiTableColumnFlags>(flags), width_weight);
    }
    void TableSetupScrollFreeze(int cols, int rows) override { ImGui::TableSetupScrollFreeze(cols, rows); }
    void TableHeadersRow() override { ImGui::TableHeadersRow(); }
    void TableNextRow() override { ImGui::TableNextRow(); }
    void TableSetColumnIndex(int column_n) override { ImGui::TableSetColumnIndex(column_n); }
    bool BeginCombo(const char* label, const char* preview_value, int flags = 0) override {
        return ImGui::BeginCombo(label, preview_value, flags);
    }
    void EndCombo() override { ImGui::EndCombo(); }
    bool Selectable(const char* label, bool selected = false) override { return ImGui::Selectable(label, selected); }
    void SetItemDefaultFocus() override { ImGui::SetItemDefaultFocus(); }
    void PushID(int id) override { ImGui::PushID(id); }
    void PushID(const char* str_id) override { ImGui::PushID(str_id); }
    void PopID() override { ImGui::PopID(); }
    void Indent() override { ImGui::Indent(); }
    void Unindent() override { ImGui::Unindent(); }
    bool InputText(const char* label, char* buf, size_t buf_size) override {
        return ImGui::InputText(label, buf, buf_size);
    }
    bool SliderInt(const char* label, int* v, int v_min, int v_max, const char* format = "%d") override {
        return ImGui::SliderInt(label, v, v_min, v_max, format);
    }
    void TextWrapped(const char* fmt, ...) override {
        va_list args;
        va_start(args, fmt);
        ImGui::TextWrappedV(fmt, args);
        va_end(args);
    }
    void PushStyleColor(int col_enum, ImGuiWrapperColor color) override {
        ImGui::PushStyleColor(static_cast<ImGuiCol>(col_enum), to_ImVec4(color));
    }
    void PopStyleColor(int count = 1) override { ImGui::PopStyleColor(count); }
    bool TreeNodeEx(const char* label, int flags) override {
        return ImGui::TreeNodeEx(label, static_cast<ImGuiTreeNodeFlags>(flags));
    }
    void TreePop() override { ImGui::TreePop(); }
    ImGuiWrapperVec2 GetContentRegionAvail() override { return from_ImVec2(ImGui::GetContentRegionAvail()); }
    float GetStyleItemSpacingX() override { return ImGui::GetStyle().ItemSpacing.x; }
    float GetStyleFramePaddingX() override { return ImGui::GetStyle().FramePadding.x; }
    ImGuiWrapperVec2 CalcTextSize(const char* text) override { return from_ImVec2(ImGui::CalcTextSize(text)); }
    void SetNextItemWidth(float width) override { ImGui::SetNextItemWidth(width); }
    void BeginDisabled() override { ImGui::BeginDisabled(); }
    void EndDisabled() override { ImGui::EndDisabled(); }
};

} // namespace ui
} // namespace display_commander
