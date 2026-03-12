#include "streamline_tab.hpp"
#include "../../globals.hpp"
#include "../../hooks/nvidia/streamline_hooks.hpp"
#include "../../res/forkawesome.h"
#include "../../settings/streamline_tab_settings.hpp"
#include "../../utils.hpp"
#include "../../utils/general_utils.hpp"
#include "../../utils/logging.hpp"
#include "../imgui_wrapper_base.hpp"

#include <imgui.h>
#include <windows.h>
#include <filesystem>
#include <numeric>
#include <string>

namespace ui::new_ui {

void DrawStreamlineTab(display_commander::ui::IImGuiWrapper& imgui) {
    imgui.Text("Streamline Tab - DLSS Information");
    imgui.Separator();

    HMODULE sl_interposer = GetModuleHandleW(L"sl.interposer.dll");
    HMODULE sl_common = GetModuleHandleW(L"sl.common.dll");

    if (sl_interposer == nullptr) {
        imgui.TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "Streamline not detected - sl.interposer.dll not loaded");
        return;
    }

    imgui.TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "Streamline detected");
    imgui.Text("sl.interposer.dll: 0x%p", sl_interposer);

    if (sl_common != nullptr) {
        imgui.Text("sl.common.dll: 0x%p", sl_common);
    } else {
        imgui.TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "sl.common.dll: Not loaded");
    }

    imgui.Spacing();

    imgui.TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "Streamline SDK Information:");
    imgui.Separator();

    uint64_t sdk_version = GetLastStreamlineSDKVersion();
    if (sdk_version > 0) {
        imgui.Text("SDK Version: %llu", sdk_version);
    } else {
        imgui.TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "SDK Version: Not yet called");
        imgui.TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "Note: Version will be updated when slInit is called");
    }

    imgui.Spacing();

    imgui.TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "DLSS Frame Generation Information:");
    imgui.Separator();

    bool dlss_g_loaded = g_dlss_g_loaded.load();
    imgui.Text("DLSS-G Loaded: %s", dlss_g_loaded ? "Yes" : "No");

    if (dlss_g_loaded) {
        auto version_ptr = g_dlss_g_version.load();
        if (version_ptr) {
            imgui.Text("DLSS-G Version: %s", version_ptr->c_str());
        } else {
            imgui.TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "DLSS-G Version: Unknown");
        }
    }

    imgui.Spacing();

    imgui.TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "Streamline Event Counters:");
    imgui.Separator();

    uint32_t sl_init_count = g_streamline_event_counters[STREAMLINE_EVENT_SL_INIT].load();
    uint32_t sl_feature_count = g_streamline_event_counters[STREAMLINE_EVENT_SL_IS_FEATURE_SUPPORTED].load();
    uint32_t sl_interface_count = g_streamline_event_counters[STREAMLINE_EVENT_SL_GET_NATIVE_INTERFACE].load();
    uint32_t sl_upgrade_count = g_streamline_event_counters[STREAMLINE_EVENT_SL_UPGRADE_INTERFACE].load();
    uint32_t sl_dlss_optimal_count = g_streamline_event_counters[STREAMLINE_EVENT_SL_DLSS_GET_OPTIMAL_SETTINGS].load();

    imgui.Text("slInit calls: %u", sl_init_count);
    imgui.Text("slIsFeatureSupported calls: %u", sl_feature_count);
    imgui.Text("slGetNativeInterface calls: %u", sl_interface_count);
    imgui.Text("slUpgradeInterface calls: %u", sl_upgrade_count);
    imgui.Text("slDLSSGetOptimalSettings calls: %u", sl_dlss_optimal_count);

    imgui.Spacing();

    imgui.TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "DLSS Override Settings:");
    imgui.Separator();

    bool dlss_override_enabled = settings::g_streamlineTabSettings.dlss_override_enabled.GetValue();
    if (imgui.Checkbox("Enable DLSS Override", &dlss_override_enabled)) {
        settings::g_streamlineTabSettings.dlss_override_enabled.SetValue(dlss_override_enabled);
    }

    if (dlss_override_enabled) {
        imgui.Indent();
        imgui.Text("Override location: AppData\\Local\\Programs\\Display_Commander\\dlss_override");
        std::vector<std::string> subfolders = GetDlssOverrideSubfolderNames();
        auto draw_dll_row = [&imgui, &subfolders](const char* label, bool* p_check,
                                                  ui::new_ui::StringSetting& subfolder_setting, int dll_index) {
            imgui.Checkbox(label, p_check);
            std::string current_sub = subfolder_setting.GetValue();
            int current_index = -1;
            if (!current_sub.empty()) {
                for (size_t i = 0; i < subfolders.size(); ++i) {
                    if (subfolders[i] == current_sub) {
                        current_index = static_cast<int>(i);
                        break;
                    }
                }
            }
            const char* combo_label = (current_index >= 0)  ? subfolders[static_cast<size_t>(current_index)].c_str()
                                      : current_sub.empty() ? "(root folder)"
                                                            : current_sub.c_str();
            imgui.SameLine();
            imgui.SetNextItemWidth(140.0f);
            if (imgui.BeginCombo((std::string("##dlss_sub_sl_") + std::to_string(dll_index)).c_str(), combo_label)) {
                if (imgui.Selectable("(root folder)", current_sub.empty())) {
                    subfolder_setting.SetValue("");
                }
                for (size_t i = 0; i < subfolders.size(); ++i) {
                    bool selected = (current_index == static_cast<int>(i));
                    if (imgui.Selectable(subfolders[i].c_str(), selected)) {
                        subfolder_setting.SetValue(subfolders[i]);
                    }
                }
                imgui.EndCombo();
            }
            {
                std::string effective_folder = GetEffectiveDefaultDlssOverrideFolder(current_sub).string();
                if (std::filesystem::exists(effective_folder)) {
                    DlssOverrideDllStatus st = GetDlssOverrideFolderDllStatus(effective_folder, (dll_index == 0),
                                                                              (dll_index == 1), (dll_index == 2));
                    if (st.dlls.size() > static_cast<size_t>(dll_index)) {
                        const DlssOverrideDllEntry& e = st.dlls[dll_index];
                        imgui.SameLine();
                        if (e.present) {
                            imgui.TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "%s", e.version.c_str());
                        } else {
                            imgui.TextColored(ImVec4(1.0f, 0.75f, 0.0f, 1.0f), "Missing");
                        }
                    }
                } else if (!effective_folder.empty()) {
                    imgui.SameLine();
                    imgui.TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "Folder not found");
                }
            }
        };
        bool dlss_on = settings::g_streamlineTabSettings.dlss_override_dlss.GetValue();
        bool dlss_fg_on = settings::g_streamlineTabSettings.dlss_override_dlss_fg.GetValue();
        bool dlss_rr_on = settings::g_streamlineTabSettings.dlss_override_dlss_rr.GetValue();
        draw_dll_row("nvngx_dlss.dll (DLSS)##sl", &dlss_on, settings::g_streamlineTabSettings.dlss_override_subfolder,
                     0);
        if (dlss_on != settings::g_streamlineTabSettings.dlss_override_dlss.GetValue()) {
            settings::g_streamlineTabSettings.dlss_override_dlss.SetValue(dlss_on);
        }
        draw_dll_row("nvngx_dlssd.dll (D = denoiser / RR)##sl", &dlss_rr_on,
                     settings::g_streamlineTabSettings.dlss_override_subfolder_dlssd, 1);
        if (dlss_rr_on != settings::g_streamlineTabSettings.dlss_override_dlss_rr.GetValue()) {
            settings::g_streamlineTabSettings.dlss_override_dlss_rr.SetValue(dlss_rr_on);
        }
        draw_dll_row("nvngx_dlssg.dll (G = generation / FG)##sl", &dlss_fg_on,
                     settings::g_streamlineTabSettings.dlss_override_subfolder_dlssg, 2);
        if (dlss_fg_on != settings::g_streamlineTabSettings.dlss_override_dlss_fg.GetValue()) {
            settings::g_streamlineTabSettings.dlss_override_dlss_fg.SetValue(dlss_fg_on);
        }
        static char dlss_add_folder_buf[128] = "";
        imgui.SetNextItemWidth(120.0f);
        imgui.InputTextWithHint("##dlss_add_folder_streamline", "e.g. 310.5.2", dlss_add_folder_buf,
                                sizeof(dlss_add_folder_buf));
        imgui.SameLine();
        if (imgui.Button("Add Folder")) {
            std::string name(dlss_add_folder_buf);
            std::string err;
            if (CreateDlssOverrideSubfolder(name, &err)) {
                dlss_add_folder_buf[0] = '\0';
            } else if (!err.empty()) {
                LogError("DLSS override Add Folder: %s", err.c_str());
            }
        }
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx("Create subfolder under Display_Commander\\dlss_override.");
        }
        imgui.Unindent();
    }

    imgui.Spacing();

    imgui.TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "DLSS DLL Detection:");
    imgui.Separator();
}

}  // namespace ui::new_ui
