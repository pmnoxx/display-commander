#include "hook_stats_tab.hpp"
#include "../../hooks/windows_hooks/windows_message_hooks.hpp"
#include "../../addon.hpp"

namespace ui::new_ui {

void DrawHookStatsTab() {
    ImGui::TextColored(ImVec4(0.8f, 1.0f, 0.8f, 1.0f), "=== Hook Call Statistics ===");
    ImGui::Text("Track the number of times each Windows message hook was called");
    ImGui::Separator();

    // Reset button
    if (ImGui::Button("Reset All Statistics")) {
        renodx::hooks::ResetAllHookStats();
    }
    ImGui::SameLine();
    ImGui::Text("Click to reset all counters to zero");

    ImGui::Spacing();
    ImGui::Separator();

    // Create a table to display hook statistics
    if (ImGui::BeginTable("HookStats", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable)) {
        // Table headers
        ImGui::TableSetupColumn("Hook Name", ImGuiTableColumnFlags_WidthFixed, 200.0f);
        ImGui::TableSetupColumn("Total Calls", ImGuiTableColumnFlags_WidthFixed, 120.0f);
        ImGui::TableSetupColumn("Unsuppressed Calls", ImGuiTableColumnFlags_WidthFixed, 150.0f);
        ImGui::TableSetupColumn("Suppressed Calls", ImGuiTableColumnFlags_WidthFixed, 150.0f);
        ImGui::TableHeadersRow();

        // Display statistics for each hook
        int hook_count = renodx::hooks::GetHookCount();
        for (int i = 0; i < hook_count; ++i) {
            const auto& stats = renodx::hooks::GetHookStats(i);
            const char* hook_name = renodx::hooks::GetHookName(i);

            uint64_t total_calls = stats.total_calls.load();
            uint64_t unsuppressed_calls = stats.unsuppressed_calls.load();
            uint64_t suppressed_calls = total_calls - unsuppressed_calls;

            ImGui::TableNextRow();

            // Hook name
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("%s", hook_name);

            // Total calls
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%llu", total_calls);

            // Unsuppressed calls
            ImGui::TableSetColumnIndex(2);
            ImGui::Text("%llu", unsuppressed_calls);

            // Suppressed calls
            ImGui::TableSetColumnIndex(3);
            if (suppressed_calls > 0) {
                ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.5f, 1.0f), "%llu", suppressed_calls);
            } else {
                ImGui::Text("%llu", suppressed_calls);
            }
        }

        ImGui::EndTable();
    }

    ImGui::Spacing();
    ImGui::Separator();

    // Summary statistics
    ImGui::TextColored(ImVec4(0.8f, 0.8f, 1.0f, 1.0f), "Summary:");

    uint64_t total_all_calls = 0;
    uint64_t total_unsuppressed_calls = 0;
    int hook_count = renodx::hooks::GetHookCount();

    for (int i = 0; i < hook_count; ++i) {
        const auto& stats = renodx::hooks::GetHookStats(i);
        total_all_calls += stats.total_calls.load();
        total_unsuppressed_calls += stats.unsuppressed_calls.load();
    }

    uint64_t total_suppressed_calls = total_all_calls - total_unsuppressed_calls;

    ImGui::Text("Total Hook Calls: %llu", total_all_calls);
    ImGui::Text("Unsuppressed Calls: %llu", total_unsuppressed_calls);
    ImGui::Text("Suppressed Calls: %llu", total_suppressed_calls);

    if (total_all_calls > 0) {
        float suppression_rate = (float)total_suppressed_calls / (float)total_all_calls * 100.0f;
        ImGui::Text("Suppression Rate: %.2f%%", suppression_rate);
    }
}

} // namespace ui::new_ui
