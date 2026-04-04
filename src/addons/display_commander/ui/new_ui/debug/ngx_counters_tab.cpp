// Source Code <Display Commander> // follow this order for includes in all files + add this comment at the top
#include "ngx_counters_tab.hpp"
#include "../../../globals.hpp"
#include "../../../hooks/nvidia/ngx_hooks.hpp"

// Libraries <ReShade> / <imgui>
#include <imgui.h>

// Libraries <standard C++>
#include <algorithm>
#include <cctype>
#include <cinttypes>
#include <cstdio>
#include <string>
#include <vector>

namespace ui::new_ui::debug {

namespace {

const char* ParameterTypeLabel(ParameterValue::Type t) {
    switch (t) {
        case ParameterValue::INT:
            return "int";
        case ParameterValue::UINT:
            return "uint";
        case ParameterValue::FLOAT:
            return "float";
        case ParameterValue::DOUBLE:
            return "double";
        case ParameterValue::ULL:
            return "ull";
        default:
            return "?";
    }
}

std::string FormatParameterValue(const ParameterValue& v) {
    char buf[96];
    switch (v.type) {
        case ParameterValue::INT:
            std::snprintf(buf, sizeof(buf), "%d", v.int_val);
            return buf;
        case ParameterValue::UINT:
            std::snprintf(buf, sizeof(buf), "%u", v.uint_val);
            return buf;
        case ParameterValue::FLOAT:
            std::snprintf(buf, sizeof(buf), "%.9g", static_cast<double>(v.float_val));
            return buf;
        case ParameterValue::DOUBLE:
            std::snprintf(buf, sizeof(buf), "%.17g", v.double_val);
            return buf;
        case ParameterValue::ULL:
            std::snprintf(buf, sizeof(buf), "%" PRIu64, static_cast<unsigned long long>(v.ull_val));
            return buf;
        default:
            return "?";
    }
}

bool NameMatchesFilter(const std::string& name, const char* filter_cstr) {
    if (filter_cstr == nullptr || filter_cstr[0] == '\0') {
        return true;
    }
    std::string f(filter_cstr);
    std::string h = name;
    for (char& c : f) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    for (char& c : h) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return h.find(f) != std::string::npos;
}

}  // namespace

void DrawNGXCountersTab(display_commander::ui::IImGuiWrapper& imgui) {
    imgui.Spacing();
    imgui.TextWrapped(
        "Session-only frame generation tweaks apply on D3D11/D3D12 EvaluateFeature for the tracked frame-generation "
        "handle, using that call's parameter object.");
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
    imgui.TextUnformatted("Generated-frame interpolation");
    {
        static const char* kInterpLabels[] = {"Default", "Off", "On"};
        constexpr int kInterpLabelCount = static_cast<int>(sizeof(kInterpLabels) / sizeof(kInterpLabels[0]));
        const int stored_interp = GetDebugDLSSGEnableInterpOverride();
        int interp_combo_idx = (stored_interp < 0) ? 0 : (stored_interp + 1);
        if (imgui.Combo("##debug_ngx_fg_interp", &interp_combo_idx, kInterpLabels, kInterpLabelCount)) {
            SetDebugDLSSGEnableInterpOverride((interp_combo_idx <= 0) ? -1 : (interp_combo_idx - 1));
        }
    }
    imgui.Spacing();

    static char s_ngx_param_filter[256] = {};
    imgui.TextWrapped(
        "Captured NGX parameters (scalar values from hooked SetI/SetUI/SetF/SetD/SetULL and internal mirrors). "
        "Not a full NGX catalog; search matches the parameter name.");
    imgui.InputTextWithHint("##ngx_param_filter", "Search name...", s_ngx_param_filter, sizeof(s_ngx_param_filter));

    const auto snap = g_ngx_parameters.get_all();
    std::vector<std::pair<std::string, ParameterValue>> rows;
    rows.reserve(snap->size());
    for (const auto& kv : *snap) {
        rows.push_back(kv);
    }
    std::sort(rows.begin(), rows.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });

    std::vector<size_t> filtered_ix;
    filtered_ix.reserve(rows.size());
    for (size_t i = 0; i < rows.size(); ++i) {
        if (NameMatchesFilter(rows[i].first, s_ngx_param_filter)) {
            filtered_ix.push_back(i);
        }
    }

    imgui.Text("Rows: %zu (showing %zu)", rows.size(), filtered_ix.size());

    const float table_h = imgui.GetTextLineHeightWithSpacing() * 18.0f;
    const int table_flags = static_cast<int>(ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY);
    if (imgui.BeginTable("ngx_captured_params", 3, table_flags, ImVec2(0.0f, table_h))) {
        imgui.TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
        imgui.TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 52.0f);
        imgui.TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
        imgui.TableHeadersRow();

        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(filtered_ix.size()));
        while (clipper.Step()) {
            for (int r = clipper.DisplayStart; r < clipper.DisplayEnd; ++r) {
                const auto& entry = rows[filtered_ix[static_cast<size_t>(r)]];
                imgui.TableNextRow();
                imgui.TableNextColumn();
                imgui.TextUnformatted(entry.first.c_str());
                imgui.TableNextColumn();
                imgui.TextUnformatted(ParameterTypeLabel(entry.second.type));
                imgui.TableNextColumn();
                {
                    const std::string val_str = FormatParameterValue(entry.second);
                    imgui.TextUnformatted(val_str.c_str());
                }
            }
        }
        imgui.EndTable();
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
