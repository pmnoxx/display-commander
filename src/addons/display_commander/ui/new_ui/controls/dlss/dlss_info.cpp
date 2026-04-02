// Source Code <Display Commander> // follow this order for includes in all files + add this comment at the top
// Headers <Display Commander>
#include "dlss_info.hpp"
#include "dlss/dlss_indicator_manager.hpp"
#include "globals.hpp"
#include "hooks/nvidia/ngx_hooks.hpp"
#include "settings/streamline_tab_settings.hpp"
#include "settings/swapchain_tab_settings.hpp"
#include "ui/forkawesome.h"
#include "ui/ui_colors.hpp"
#include "utils/general_utils.hpp"
#include "utils/detour_call_tracker.hpp"
#include "utils/logging.hpp"

// Libraries <ReShade / ImGui>
#include <imgui.h>

// Libraries <C++>
#include <sstream>
#include <string>
#include <vector>

// Libraries <Windows>
#include <Windows.h>

namespace ui::new_ui {

namespace {

// Draw DLSS indicator section (registry toggle + DLSS-FG text level). Shown at top of DLSS Control when active.
void DrawDLSSInfo_IndicatorSection(display_commander::ui::IImGuiWrapper& imgui) {
    if (imgui.TreeNodeEx("DLSS indicator (Registry)", ImGuiTreeNodeFlags_None)) {
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx(
                "Show DLSS on-screen indicator in games. Writes NVIDIA registry; may require restart. Admin if apply "
                "fails.");
        }
        bool reg_enabled = dlss::DlssIndicatorManager::IsDlssIndicatorEnabled();
        imgui.TextColored(reg_enabled ? ui::colors::TEXT_SUCCESS : ui::colors::TEXT_DIMMED, "DLSS indicator: %s",
                          reg_enabled ? "On" : "Off");
        if (imgui.Checkbox("Enable DLSS indicator through Registry##MainTab", &reg_enabled)) {
            LogInfo("DLSS Indicator: %s", reg_enabled ? "enabled" : "disabled");
            if (!dlss::DlssIndicatorManager::SetDlssIndicatorEnabled(reg_enabled)) {
                LogInfo("DLSS Indicator: Apply to registry failed (run as admin if needed).");
            }
        }
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx(
                "Show DLSS on-screen indicator (resolution/version) in games. Writes NVIDIA registry; may require "
                "restart. Admin needed if apply fails.");
        }

        const char* dlssg_indicator_items[] = {"Off", "Minimal", "Detailed"};
        int dlssg_indicator_current = static_cast<int>(dlss::DlssIndicatorManager::GetDlssgIndicatorTextLevel());
        if (dlssg_indicator_current < 0 || dlssg_indicator_current > 2) {
            dlssg_indicator_current = 0;
        }
        if (imgui.Combo("DLSS-FG indicator text##MainTab", &dlssg_indicator_current, dlssg_indicator_items,
                        static_cast<int>(sizeof(dlssg_indicator_items) / sizeof(dlssg_indicator_items[0])))) {
            DWORD level = static_cast<DWORD>(dlssg_indicator_current);
            if (!dlss::DlssIndicatorManager::SetDlssgIndicatorTextLevel(level)) {
                LogInfo("DLSSG_IndicatorText: Apply to registry failed (run as admin).");
            }
        }
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx(
                "DLSS-FG on-screen indicator text level (registry DLSSG_IndicatorText). Off / Minimal / Detailed. "
                "May require restart. Admin needed if apply fails.");
        }
        imgui.TreePop();
    }
}

}  // namespace

// Draw DLSS information (same format as performance overlay). Caller must pass pre-fetched summary.
void DrawDLSSInfo(display_commander::ui::IImGuiWrapper& imgui, const DLSSGSummary& dlssg_summary) {
    (void)imgui;
    CALL_GUARD_NO_TS();
    const bool any_dlss_active =
        dlssg_summary.dlss_active || dlssg_summary.dlss_g_active || dlssg_summary.ray_reconstruction_active;

    DrawDLSSInfo_IndicatorSection(imgui);

    // Tracked DLSS modules (from OnModuleLoaded: nvngx_dlss/dlssg/dlssd.dll or .bin identified as such)
    {
        auto path_dlss = GetDlssTrackedPath(DlssTrackedKind::DLSS);
        auto path_dlssg = GetDlssTrackedPath(DlssTrackedKind::DLSSG);
        auto path_dlssd = GetDlssTrackedPath(DlssTrackedKind::DLSSD);
        if (path_dlss.has_value() || path_dlssg.has_value() || path_dlssd.has_value()) {
            if (imgui.TreeNodeEx("DLSS module paths (tracked)", ImGuiTreeNodeFlags_None)) {
                if (imgui.IsItemHovered()) {
                    imgui.SetTooltipEx("Paths from OnModuleLoaded (DLL name or .bin identified as DLSS/DLSS-G/DLSS-D).");
                }
                if (path_dlss.has_value()) {
                    imgui.Text("nvngx_dlss.dll: %s", path_dlss->c_str());
                } else {
                    imgui.TextColored(ui::colors::TEXT_DIMMED, "nvngx_dlss.dll: (not tracked)");
                }
                if (path_dlssg.has_value()) {
                    imgui.Text("nvngx_dlssg.dll: %s", path_dlssg->c_str());
                } else {
                    imgui.TextColored(ui::colors::TEXT_DIMMED, "nvngx_dlssg.dll: (not tracked)");
                }
                if (path_dlssd.has_value()) {
                    imgui.Text("nvngx_dlssd.dll: %s", path_dlssd->c_str());
                } else {
                    imgui.TextColored(ui::colors::TEXT_DIMMED, "nvngx_dlssd.dll: (not tracked)");
                }
                imgui.TreePop();
            }
        }
    }

    // CreateFeature seen (NGX/Streamline): whether we observed CreateFeature for each feature, or loaded too late
    if (imgui.TreeNodeEx("CreateFeature seen (tracked)", ImGuiTreeNodeFlags_None)) {
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx(
                "Whether our NGX/Streamline hooks observed CreateFeature for each feature. \"Loaded too late\" means "
                "the game created the feature before we hooked.");
        }
        const bool dlss_seen = g_dlss_was_active_once.load();
        const bool dlss_late = dlssg_summary.dlss_active && !dlss_seen;
        if (dlss_seen) {
            imgui.TextColored(ui::colors::TEXT_SUCCESS, "DLSS: CreateFeature seen");
        } else if (dlss_late) {
            imgui.TextColored(ui::colors::TEXT_WARNING, "DLSS: Loaded too late (CreateFeature not seen)");
        } else {
            imgui.TextColored(ui::colors::TEXT_DIMMED, "DLSS: Not seen");
        }
        const bool dlssfg_seen = g_dlssg_was_active_once.load();
        const bool dlssfg_late = dlssg_summary.dlss_g_active && !dlssfg_seen;
        if (dlssfg_seen) {
            imgui.TextColored(ui::colors::TEXT_SUCCESS, "DLSS FG: CreateFeature seen");
        } else if (dlssfg_late) {
            imgui.TextColored(ui::colors::TEXT_WARNING, "DLSS FG: Loaded too late (CreateFeature not seen)");
        } else {
            imgui.TextColored(ui::colors::TEXT_DIMMED, "DLSS FG: Not seen");
        }
        const bool dlssrr_seen = g_ray_reconstruction_was_active_once.load();
        const bool dlssrr_late = dlssg_summary.ray_reconstruction_active && !dlssrr_seen;
        if (dlssrr_seen) {
            imgui.TextColored(ui::colors::TEXT_SUCCESS, "DLSS-RR: CreateFeature seen");
        } else if (dlssrr_late) {
            imgui.TextColored(ui::colors::TEXT_WARNING, "DLSS-RR: Loaded too late (CreateFeature not seen)");
        } else {
            imgui.TextColored(ui::colors::TEXT_DIMMED, "DLSS-RR: Not seen");
        }
        imgui.TreePop();
    }

    // FG Mode
    if (any_dlss_active
        && (dlssg_summary.fg_mode == "2x" || dlssg_summary.fg_mode == "3x" || dlssg_summary.fg_mode == "4x")) {
        imgui.Text("FG: %s", dlssg_summary.fg_mode.c_str());
    } else {
        imgui.TextColored(ui::colors::TEXT_DIMMED, "FG: OFF");
    }

    // DLSS Internal Resolution (same format as performance overlay: internal -> output -> backbuffer)
    if (any_dlss_active && dlssg_summary.internal_resolution != "N/A") {
        std::string res_text = dlssg_summary.internal_resolution;
        const int bb_w = g_game_render_width.load();
        const int bb_h = g_game_render_height.load();
        if (bb_w > 0 && bb_h > 0) {
            res_text += " -> " + std::to_string(bb_w) + "x" + std::to_string(bb_h);
        }
        imgui.Text("DLSS Internal->Output: %s", res_text.c_str());
    } else {
        imgui.TextColored(ui::colors::TEXT_DIMMED, "DLSS Internal->Output: N/A");
    }

    // DLSS Status
    if (any_dlss_active) {
        std::string status_text = "DLSS: On";
        if (dlssg_summary.ray_reconstruction_active) {
            status_text += " (RR)";
        } else if (dlssg_summary.dlss_g_active) {
            status_text += " (DLSS-G)";
        }
        imgui.TextColored(ui::colors::TEXT_SUCCESS, "%s", status_text.c_str());
    } else {
        imgui.TextColored(ui::colors::TEXT_DIMMED, "DLSS: Off");
    }

    // DLSS Quality Preset
    if (any_dlss_active && dlssg_summary.quality_preset != "N/A") {
        imgui.Text("DLSS Quality: %s", dlssg_summary.quality_preset.c_str());
    } else {
        imgui.TextColored(ui::colors::TEXT_DIMMED, "DLSS Quality: N/A");
    }

    // DLSS Render Preset
    if (any_dlss_active) {
        DLSSModelProfile model_profile = GetDLSSModelProfile();
        if (model_profile.is_valid) {
            std::string current_quality = dlssg_summary.quality_preset;
            int render_preset_value = 0;

            // Use Ray Reconstruction presets if RR is active, otherwise use Super Resolution presets
            if (dlssg_summary.ray_reconstruction_active) {
                if (current_quality == "Quality") {
                    render_preset_value = model_profile.rr_quality_preset;
                } else if (current_quality == "Balanced") {
                    render_preset_value = model_profile.rr_balanced_preset;
                } else if (current_quality == "Performance") {
                    render_preset_value = model_profile.rr_performance_preset;
                } else if (current_quality == "Ultra Performance") {
                    render_preset_value = model_profile.rr_ultra_performance_preset;
                } else if (current_quality == "Ultra Quality") {
                    render_preset_value = model_profile.rr_ultra_quality_preset;
                } else {
                    render_preset_value = model_profile.rr_quality_preset;
                }
            } else {
                if (current_quality == "Quality") {
                    render_preset_value = model_profile.sr_quality_preset;
                } else if (current_quality == "Balanced") {
                    render_preset_value = model_profile.sr_balanced_preset;
                } else if (current_quality == "Performance") {
                    render_preset_value = model_profile.sr_performance_preset;
                } else if (current_quality == "Ultra Performance") {
                    render_preset_value = model_profile.sr_ultra_performance_preset;
                } else if (current_quality == "Ultra Quality") {
                    render_preset_value = model_profile.sr_ultra_quality_preset;
                } else if (current_quality == "DLAA") {
                    render_preset_value = model_profile.sr_dlaa_preset;
                } else {
                    render_preset_value = model_profile.sr_quality_preset;
                }
            }

            std::string render_preset_letter = ConvertRenderPresetToLetter(render_preset_value);
            imgui.Text("DLSS Render: %s", render_preset_letter.c_str());
        } else {
            imgui.TextColored(ui::colors::TEXT_DIMMED, "DLSS Render: N/A");
        }
    } else {
        imgui.TextColored(ui::colors::TEXT_DIMMED, "DLSS Render: N/A");
    }

    // DLSS Render Preset override (same settings as Swapchain tab)
    if (any_dlss_active) {
        bool preset_override_enabled = settings::g_swapchainTabSettings.dlss_preset_override_enabled.GetValue();
        if (imgui.Checkbox("Enable DLSS Preset Override##MainTab", &preset_override_enabled)) {
            settings::g_swapchainTabSettings.dlss_preset_override_enabled.SetValue(preset_override_enabled);
            ResetNGXPresetInitialization();
        }
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx(
                "Override DLSS presets at runtime (Game Default / DLSS Default / Preset A, B, C, etc.). Same as "
                "Swapchain tab.");
        }

        if (g_dlss_from_nvidia_app_bin.load()) {
            imgui.TextColored(ImVec4(1.0f, 0.6f, 0.0f, 1.0f),
                              "NVIDIA App DLSS override detected (.bin). Version and presets are controlled by the NVIDIA app.");
            if (imgui.IsItemHovered()) {
                imgui.SetTooltipEx(
                    "DLSS was loaded from a .bin bundle (Streamline/NVIDIA App). Preset override may have limited effect.");
            }
        }

        if (settings::g_swapchainTabSettings.dlss_preset_override_enabled.GetValue()) {
            const bool rr_active = dlssg_summary.ray_reconstruction_active;

            std::vector<std::string> sr_preset_options = GetDLSSPresetOptions(dlssg_summary.supported_dlss_presets);
            std::vector<const char*> sr_preset_cstrs;
            sr_preset_cstrs.reserve(sr_preset_options.size());
            for (const auto& option : sr_preset_options) {
                sr_preset_cstrs.push_back(option.c_str());
            }
            std::string sr_current_value = settings::g_swapchainTabSettings.dlss_sr_preset_override.GetValue();
            int sr_current_selection = 0;
            for (size_t i = 0; i < sr_preset_options.size(); ++i) {
                if (sr_current_value == sr_preset_options[i]) {
                    sr_current_selection = static_cast<int>(i);
                    break;
                }
            }
            const char* sr_label = rr_active ? "SR Preset##MainTabSRPreset" : "SR Preset (active)##MainTabSRPreset";
            imgui.SetNextItemWidth(250.0f);
            if (imgui.Combo(sr_label, &sr_current_selection, sr_preset_cstrs.data(),
                            static_cast<int>(sr_preset_cstrs.size()))) {
                settings::g_swapchainTabSettings.dlss_sr_preset_override.SetValue(sr_preset_options[sr_current_selection]);
                ResetNGXPresetInitialization();
            }
            if (imgui.IsItemHovered()) {
                imgui.SetTooltipEx("Preset: Game Default = no override, DLSS Default = 0, Preset A/B/C... = 1/2/3...");
            }

            std::vector<std::string> rr_preset_options = GetDLSSPresetOptions(dlssg_summary.supported_dlss_rr_presets);
            std::vector<const char*> rr_preset_cstrs;
            rr_preset_cstrs.reserve(rr_preset_options.size());
            for (const auto& option : rr_preset_options) {
                rr_preset_cstrs.push_back(option.c_str());
            }
            std::string rr_current_value = settings::g_swapchainTabSettings.dlss_rr_preset_override.GetValue();
            int rr_current_selection = 0;
            for (size_t i = 0; i < rr_preset_options.size(); ++i) {
                if (rr_current_value == rr_preset_options[i]) {
                    rr_current_selection = static_cast<int>(i);
                    break;
                }
            }
            const char* rr_label = rr_active ? "RR Preset (active)##MainTabRRPreset" : "RR Preset##MainTabRRPreset";
            imgui.SetNextItemWidth(250.0f);
            if (imgui.Combo(rr_label, &rr_current_selection, rr_preset_cstrs.data(),
                            static_cast<int>(rr_preset_cstrs.size()))) {
                settings::g_swapchainTabSettings.dlss_rr_preset_override.SetValue(rr_preset_options[rr_current_selection]);
                ResetNGXPresetInitialization();
            }
            if (imgui.IsItemHovered()) {
                imgui.SetTooltipEx("Preset: Game Default = no override, DLSS Default = 0, Preset A/B/C... = 1/2/3...");
            }
        }
    }

    // DLSS.Feature.Create.Flags (own field)
    if (any_dlss_active) {
        int create_flags_val = 0;
        bool has_create_flags = ::g_ngx_parameters.get_as_int("DLSS.Feature.Create.Flags", create_flags_val);
        std::string create_flags_list;
        if (has_create_flags) {
            static const struct {
                unsigned int mask;
                const char* name;
            } k_dlss_feature_bits[] = {
                {1u << 0, "IsHDR"},         {1u << 1, "MVLowRes"},       {1u << 2, "MVJittered"},
                {1u << 3, "DepthInverted"}, {1u << 4, "Reserved_0"},     {1u << 5, "DoSharpening"},
                {1u << 6, "AutoExposure"},  {1u << 7, "AlphaUpscaling"}, {1u << 31, "IsInvalid"},
            };
            unsigned int uflags = static_cast<unsigned int>(create_flags_val);
            unsigned int known_mask = 0;
            for (const auto& b : k_dlss_feature_bits) {
                known_mask |= b.mask;
                if ((uflags & b.mask) != 0u) {
                    if (!create_flags_list.empty()) create_flags_list += ", ";
                    create_flags_list += b.name;
                }
            }
            unsigned int unknown_bits = uflags & ~known_mask;
            if (unknown_bits != 0) {
                if (!create_flags_list.empty()) create_flags_list += ", ";
                create_flags_list += "+0x";
                std::ostringstream oss;
                oss << std::hex << unknown_bits;
                create_flags_list += oss.str();
                create_flags_list += " (other)";
            }
            if (create_flags_list.empty()) create_flags_list = "None";
        }
        if (has_create_flags) {
            imgui.Text("Create.Flags: %d (%s)", create_flags_val, create_flags_list.c_str());
        } else {
            imgui.TextColored(ui::colors::TEXT_DIMMED, "Create.Flags: N/A");
        }
    } else {
        imgui.TextColored(ui::colors::TEXT_DIMMED, "Create.Flags: N/A");
    }
    std::string ae_current = settings::g_swapchainTabSettings.dlss_forced_auto_exposure.GetValue();

    static auto original_auto_exposure_setting = ae_current;
    // Auto Exposure (info + override combo, same as Special-K). Shown only when Create.Flags has
    // the AutoExposure bit and we have a resolved state (ae_idx != 0, i.e. not N/A).
    bool show_auto_exposure = false;
    if (any_dlss_active) {
        int create_flags_ae = 0;
        const bool has_create_flags_ae = ::g_ngx_parameters.get_as_int("DLSS.Feature.Create.Flags", create_flags_ae);
        constexpr unsigned int k_auto_exposure_bit = 1u << 6;
        const bool flags_have_auto_exposure =
            has_create_flags_ae && ((static_cast<unsigned int>(create_flags_ae) & k_auto_exposure_bit) != 0u);
        int ae_idx = 0;  // 0 = N/A, 1 = Off, 2 = On (game state)
        if (dlssg_summary.auto_exposure == "Off") {
            ae_idx = 1;
        } else if (dlssg_summary.auto_exposure == "On") {
            ae_idx = 2;
        }
        show_auto_exposure = (ae_idx != 0 || original_auto_exposure_setting != ae_current) && flags_have_auto_exposure;
    }
    if (show_auto_exposure) {
        imgui.Text("Auto Exposure: %s", dlssg_summary.auto_exposure.c_str());
        const char* ae_items[] = {"Game Default", "Force Off", "Force On"};
        int ae_idx = 0;
        if (ae_current == "Force Off") {
            ae_idx = 1;
        } else if (ae_current == "Force On") {
            ae_idx = 2;
        }
        imgui.SetNextItemWidth(imgui.CalcTextSize("Force On").x + (imgui.GetStyle().FramePadding.x * 2.0f) + 20.0f);
        if (imgui.Combo("Auto Exposure Override##DLSS", &ae_idx, ae_items, 3)) {
            settings::g_swapchainTabSettings.dlss_forced_auto_exposure.SetValue(ae_items[ae_idx]);
        }
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx(
                "Override DLSS auto-exposure. Takes effect when DLSS feature is (re)created.\n"
                "See Create.Flags field for current DLSS.Feature.Create.Flags value and decoded bits.");
        }
        if (original_auto_exposure_setting != ae_current) {
            imgui.TextColored(ui::colors::TEXT_WARNING, "Restart required for change to take effect.");
        }
    } else {
        imgui.TextColored(ui::colors::TEXT_DIMMED, "Auto Exposure: N/A");
    }

    // DLSS DLL Versions
    imgui.Spacing();
    if (dlssg_summary.dlss_dll_version != "N/A") {
        imgui.TextColored(ImVec4(0.0f, 1.0f, 1.0f, 1.0f), "DLSS DLL: %s", dlssg_summary.dlss_dll_version.c_str());
        if (dlssg_summary.supported_dlss_presets != "N/A") {
            imgui.SameLine();
            imgui.TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), " [%s]", dlssg_summary.supported_dlss_presets.c_str());
        }
    } else {
        imgui.TextColored(ui::colors::TEXT_DIMMED, "DLSS DLL: N/A");
    }

    if (dlssg_summary.dlssg_dll_version != "N/A") {
        imgui.TextColored(ImVec4(0.0f, 1.0f, 1.0f, 1.0f), "DLSS-G DLL: %s", dlssg_summary.dlssg_dll_version.c_str());
    } else {
        imgui.TextColored(ui::colors::TEXT_DIMMED, "DLSS-G DLL: N/A");
    }

    if (dlssg_summary.dlssd_dll_version != "N/A" && dlssg_summary.dlssd_dll_version != "Not loaded") {
        imgui.TextColored(ImVec4(0.0f, 1.0f, 1.0f, 1.0f), "DLSS-D DLL: %s", dlssg_summary.dlssd_dll_version.c_str());
    } else {
        imgui.TextColored(ui::colors::TEXT_DIMMED, "DLSS-D DLL: N/A");
    }
    if (settings::g_streamlineTabSettings.dlss_override_enabled.GetValue()) {
        std::string not_applied;
        if (settings::g_streamlineTabSettings.dlss_override_dlss.GetValue() && !dlssg_summary.dlss_override_applied) {
            if (!not_applied.empty()) not_applied += ", ";
            not_applied += "nvngx_dlss.dll";
        }
        if (settings::g_streamlineTabSettings.dlss_override_dlss_rr.GetValue() && !dlssg_summary.dlssd_override_applied) {
            if (!not_applied.empty()) not_applied += ", ";
            not_applied += "nvngx_dlssd.dll";
        }
        if (settings::g_streamlineTabSettings.dlss_override_dlss_fg.GetValue() && !dlssg_summary.dlssg_override_applied) {
            if (!not_applied.empty()) not_applied += ", ";
            not_applied += "nvngx_dlssg.dll";
        }
        if (!not_applied.empty()) {
            imgui.TextColored(ImVec4(1.0f, 0.6f, 0.0f, 1.0f),
                              ICON_FK_WARNING
                              " Override not applied for: %s. Restart game with override enabled before launch.",
                              not_applied.c_str());
            if (imgui.IsItemHovered()) {
                imgui.SetTooltipEx(
                    "The game loaded these DLLs before our hooks were active. Enable override and restart the game to use override versions.");
            }
        }
    }
}

}  // namespace ui::new_ui

