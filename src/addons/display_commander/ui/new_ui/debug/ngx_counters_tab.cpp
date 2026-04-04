// Source Code <Display Commander> // follow this order for includes in all files + add this comment at the top
#include "ngx_counters_tab.hpp"
#include "../../../globals.hpp"
#include "../../../hooks/nvidia/ngx_hooks.hpp"

// Libraries <ReShade> / <imgui>
#include <imgui.h>

// Libraries <standard C++>
#include <cinttypes>

namespace ui::new_ui::debug {

void DrawNGXCountersTab(display_commander::ui::IImGuiWrapper& imgui) {
    imgui.Spacing();
    imgui.TextUnformatted("Multiplier (1x–6x)");
    {
        static const char* kMfcLabels[] = {"Default", "1x", "2x", "3x", "4x", "5x", "6x"};
        constexpr int kMfcLabelCount = static_cast<int>(sizeof(kMfcLabels) / sizeof(kMfcLabels[0]));
        const int stored = GetDebugDLSSGMultiFrameCountOverride();
        int combo_idx = (stored < 0) ? 0 : (stored + 1);
        if (imgui.Combo("##debug_ngx_mfc", &combo_idx, kMfcLabels, kMfcLabelCount)) {
            SetDebugDLSSGMultiFrameCountOverride((combo_idx <= 0) ? -1 : (combo_idx - 1));
        }
    }
    imgui.TextUnformatted("Operating mode");
    {
        static const char* kModeLabels[] = {"Default", "Off", "On", "Auto"};
        constexpr int kModeLabelCount = static_cast<int>(sizeof(kModeLabels) / sizeof(kModeLabels[0]));
        const int stored_mode = GetDebugDLSSGModeOverride();
        int mode_combo_idx = (stored_mode < 0) ? 0 : (stored_mode + 1);
        if (imgui.Combo("##debug_ngx_fg_mode", &mode_combo_idx, kModeLabels, kModeLabelCount)) {
            SetDebugDLSSGModeOverride((mode_combo_idx <= 0) ? -1 : (mode_combo_idx - 1));
        }
    }
    imgui.Spacing();

    imgui.Text("NGX Parameter vtable hooks installed: %s",
               AreNGXParameterVTableHooksInstalled() ? "yes" : "no");
    imgui.Spacing();

    if (imgui.Button("Reset NGX counters")) {
        g_ngx_counters.reset();
    }
    imgui.Spacing();

    constexpr int kCols = 3;
    if (imgui.BeginTable("ngx_debug_counters", kCols, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
        imgui.TableSetupColumn("#");
        imgui.TableSetupColumn("Label");
        imgui.TableSetupColumn("Calls");
        imgui.TableHeadersRow();
        for (int i = 0; i < static_cast<int>(NGXCounterKind::Count_); ++i) {
            const auto kind = static_cast<NGXCounterKind>(i);
            imgui.TableNextRow();
            imgui.TableNextColumn();
            imgui.Text("%d", i);
            imgui.TableNextColumn();
            imgui.TextUnformatted(GetNGXCounterKindLabel(kind));
            imgui.TableNextColumn();
            imgui.Text("%" PRIu32, static_cast<unsigned long>(GetNGXCounterValue(kind)));
        }
        imgui.EndTable();
    }
}

}  // namespace ui::new_ui::debug
