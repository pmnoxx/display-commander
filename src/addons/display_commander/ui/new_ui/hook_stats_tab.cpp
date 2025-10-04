#include "hook_stats_tab.hpp"
#include "../../hooks/windows_hooks/windows_message_hooks.hpp"
#include <imgui.h>
#include <reshade.hpp>

namespace ui::new_ui {

void DrawHookStatsTab() {
    ImGui::TextColored(ImVec4(0.8f, 1.0f, 0.8f, 1.0f), "=== Hook Call Statistics ===");
    ImGui::Text("Track the number of times each Windows message hook was called");
    ImGui::Separator();

    // Reset button
    if (ImGui::Button("Reset All Statistics")) {
        display_commanderhooks::ResetAllHookStats();
    }
    ImGui::SameLine();
    ImGui::Text("Click to reset all counters to zero");

    ImGui::Spacing();
    ImGui::Separator();

    // Define DLL groupings for hooks
    struct DllGroup {
        const char* name;
        int start_index;
        int end_index;
    };

    static const DllGroup DLL_GROUPS[] = {
        {.name = "user32.dll", .start_index = 0, .end_index = 34},      // GetMessageA to DisplayConfigGetDeviceInfo
        {.name = "xinput1_4.dll", .start_index = 35, .end_index = 36},  // XInputGetState, XInputGetStateEx
        {.name = "dinput8.dll", .start_index = 37, .end_index = 40},    // DirectInput functions
        {.name = "kernel32.dll", .start_index = 41, .end_index = 44}    // Sleep, SleepEx, WaitForSingleObject, WaitForMultipleObjects
    };

    // Display statistics grouped by DLL
    int hook_count = display_commanderhooks::GetHookCount();

    for (const auto& group : DLL_GROUPS) {
        // Calculate group totals
        uint64_t group_total_calls = 0;
        uint64_t group_unsuppressed_calls = 0;
        int group_hook_count = 0;

        for (int i = group.start_index; i <= group.end_index && i < hook_count; ++i) {
            const auto &stats = display_commanderhooks::GetHookStats(i);
            group_total_calls += stats.total_calls.load();
            group_unsuppressed_calls += stats.unsuppressed_calls.load();
            group_hook_count++;
        }

        uint64_t group_suppressed_calls = group_total_calls - group_unsuppressed_calls;

        // Create collapsible header for each DLL group
        ImGui::PushID(group.name);

        bool group_open = ImGui::CollapsingHeader(
            group.name,
            ImGuiTreeNodeFlags_DefaultOpen |
            (group_total_calls > 0 ? ImGuiTreeNodeFlags_None : ImGuiTreeNodeFlags_Leaf)
        );

        // Show group summary in header
        if (group_total_calls > 0) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f),
                "(%llu calls, %.1f%% suppressed)",
                group_total_calls,
                group_total_calls > 0 ? static_cast<float>(group_suppressed_calls) / static_cast<float>(group_total_calls) * 100.0f : 0.0f);
        }

        if (group_open) {
            ImGui::Indent();

            // Create table for this DLL's hooks
            if (ImGui::BeginTable("HookStats", 4,
                                  ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable)) {
                // Table headers
                ImGui::TableSetupColumn("Hook Name", ImGuiTableColumnFlags_WidthFixed, 200.0f);
                ImGui::TableSetupColumn("Total Calls", ImGuiTableColumnFlags_WidthFixed, 120.0f);
                ImGui::TableSetupColumn("Unsuppressed Calls", ImGuiTableColumnFlags_WidthFixed, 150.0f);
                ImGui::TableSetupColumn("Suppressed Calls", ImGuiTableColumnFlags_WidthFixed, 150.0f);
                ImGui::TableHeadersRow();

                // Display statistics for each hook in this group
                for (int i = group.start_index; i <= group.end_index && i < hook_count; ++i) {
                    const auto &stats = display_commanderhooks::GetHookStats(i);
                    const char *hook_name = display_commanderhooks::GetHookName(i);

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

            ImGui::Unindent();
        }

        ImGui::PopID();
        ImGui::Spacing();
    }

    ImGui::Spacing();
    ImGui::Separator();

    // Summary statistics
    ImGui::TextColored(ImVec4(0.8f, 0.8f, 1.0f, 1.0f), "Summary:");

    uint64_t total_all_calls = 0;
    uint64_t total_unsuppressed_calls = 0;

    // Calculate totals across all DLL groups
    for (const auto& group : DLL_GROUPS) {
        for (int i = group.start_index; i <= group.end_index && i < hook_count; ++i) {
            const auto &stats = display_commanderhooks::GetHookStats(i);
            total_all_calls += stats.total_calls.load();
            total_unsuppressed_calls += stats.unsuppressed_calls.load();
        }
    }

    uint64_t total_suppressed_calls = total_all_calls - total_unsuppressed_calls;

    ImGui::Text("Total Hook Calls: %llu", total_all_calls);
    ImGui::Text("Unsuppressed Calls: %llu", total_unsuppressed_calls);
    ImGui::Text("Suppressed Calls: %llu", total_suppressed_calls);

    if (total_all_calls > 0) {
        float suppression_rate = static_cast<float>(total_suppressed_calls) / static_cast<float>(total_all_calls) * 100.0f;
        ImGui::Text("Suppression Rate: %.2f%%", suppression_rate);
    }
}

} // namespace ui::new_ui
