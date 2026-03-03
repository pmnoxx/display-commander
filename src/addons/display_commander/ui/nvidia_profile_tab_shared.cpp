#include "nvidia_profile_tab_shared.hpp"

#include "nvapi/nvidia_profile_search.hpp"
#include "nvapi/nvpi_reference.hpp"
#include "nvapi/run_nvapi_setdword_as_admin.hpp"
#include "../res/ui_colors.hpp"
#include "../utils.hpp"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include <filesystem>

namespace display_commander {
namespace ui {

using namespace ::ui::colors;
using namespace wrapper_flags;

static std::string s_nvidiaProfileCreateError;
static std::string s_nvidiaProfileSetError;
static std::string s_nvidiaProfileDeleteError;
static std::string s_dumpDriverSettingsResult;
static std::uint32_t s_lastFailedSettingId = 0;
static std::uint32_t s_lastFailedSettingValue = 0;
static std::atomic<bool> s_requestProfileRefreshAfterAdmin{false};

static DWORD WINAPI WaitForAdminProcessThenRequestRefresh(LPVOID param) {
    HANDLE hProcess = static_cast<HANDLE>(param);
    if (hProcess != nullptr) {
        WaitForSingleObject(hProcess, 60000);
        CloseHandle(hProcess);
        s_requestProfileRefreshAfterAdmin.store(true);
    }
    return 0;
}

static bool IsPrivilegeError(const std::string& err) {
    return err.find("INVALID_USER_PRIVILEGE") != std::string::npos || err.find("0xffffff77") != std::string::npos;
}

void DrawNvidiaProfileTab(GraphicsApi /* api */, IImGuiWrapper& imgui, bool* show_advanced_profile_settings) {
    if (s_requestProfileRefreshAfterAdmin.exchange(false)) {
        s_nvidiaProfileSetError.clear();
        s_lastFailedSettingId = 0;
        nvapi::InvalidateProfileSearchCache();
    }
    imgui.Text("NVIDIA Inspector profile search");
    imgui.SameLine();
    if (imgui.Button("Refresh")) {
        nvapi::InvalidateProfileSearchCache();
        s_nvidiaProfileSetError.clear();
        s_nvidiaProfileDeleteError.clear();
        s_lastFailedSettingId = 0;
    }
    if (imgui.IsItemHovered()) {
        imgui.SetTooltip("Re-scan driver profiles (e.g. after changing settings in NVIDIA Profile Inspector).");
    }
    imgui.SameLine();
    if (imgui.Button("Dump driver settings to file")) {
        std::string dir = nvapi::GetAddonModuleDirectory();
        if (dir.empty()) {
            s_dumpDriverSettingsResult = "Addon directory not found.";
        } else {
            std::filesystem::path path = std::filesystem::path(dir) / "nvidia_driver_settings_dump.txt";
            auto [ok, err] = nvapi::DumpDriverSettingsToFile(path.string());
            s_dumpDriverSettingsResult = ok ? ("Dumped to " + path.string()) : err;
        }
    }
    if (imgui.IsItemHovered()) {
        imgui.SetTooltip("Enumerate all setting IDs recognized by the current NVIDIA driver and write them to a text file next to the addon DLL.");
    }
    if (!s_dumpDriverSettingsResult.empty()) {
        imgui.SameLine();
        imgui.TextColored(s_dumpDriverSettingsResult.substr(0, 5) == "Dumped" ? TEXT_DIMMED : TEXT_ERROR,
                          "%s", s_dumpDriverSettingsResult.c_str());
    }
    imgui.TextColored(TEXT_DIMMED,
                      "Searches all NVIDIA driver profiles for one that includes the current game executable.");
    imgui.Spacing();

    nvapi::NvidiaProfileSearchResult r = nvapi::GetCachedProfileSearchResult();

    // Show result of GetCurrentProcessPathW() (fully qualified path used for DRS lookup)
    std::wstring pathW = GetCurrentProcessPathW();
    std::string pathUtf8;
    if (!pathW.empty()) {
        int n = ::WideCharToMultiByte(CP_UTF8, 0, pathW.c_str(), -1, nullptr, 0, nullptr, nullptr);
        if (n > 0) {
            pathUtf8.resize(static_cast<size_t>(n), '\0');
            ::WideCharToMultiByte(CP_UTF8, 0, pathW.c_str(), -1, pathUtf8.data(), n, nullptr, nullptr);
            if (!pathUtf8.empty() && pathUtf8.back() == '\0') {
                pathUtf8.pop_back();
            }
        }
    }
    const char* pathDisplay = pathUtf8.empty() ? "(GetCurrentProcessPathW failed)" : pathUtf8.c_str();

    imgui.Text("Current executable:");
    imgui.TextColored(TEXT_DIMMED, "  Fully qualified path: %s", pathDisplay);
    if (imgui.IsItemHovered() && !pathUtf8.empty()) {
        imgui.SetTooltip("%s", pathUtf8.c_str());
    }
    imgui.TextColored(TEXT_DIMMED, "  Name: %s", r.current_exe_name.c_str());
    imgui.Spacing();

    if (!r.success) {
        imgui.TextColored(ICON_ERROR, "Error: %s", r.error.c_str());
        if (imgui.IsItemHovered()) {
            imgui.SetTooltip("NVAPI/DRS unavailable. Requires NVIDIA GPU and driver.");
        }
        return;
    }

    if (r.matching_profile_names.empty()) {
        imgui.TextColored(ICON_WARNING, "No NVIDIA Inspector profile found for this exe.");
        imgui.TextColored(TEXT_DIMMED, "Create a profile to manage NVIDIA driver settings for this game.");
        imgui.Spacing();
        if (imgui.Button("Create profile for this game")) {
            auto [ok, err] = nvapi::CreateProfileForCurrentExe();
            if (!ok) {
                s_nvidiaProfileCreateError = err;
            } else {
                s_nvidiaProfileCreateError.clear();
            }
        }
        if (imgui.IsItemHovered()) {
            imgui.SetTooltip(
                "Creates an NVIDIA driver profile named \"Display Commander - <exe>\" and adds this executable. You "
                "can then edit settings here or in NVIDIA Profile Inspector.");
        }
        if (!s_nvidiaProfileCreateError.empty()) {
            imgui.TextColored(ICON_ERROR, "Error: %s", s_nvidiaProfileCreateError.c_str());
        }
        imgui.Spacing();
        imgui.TextColored(TEXT_DIMMED,
                          "Alternatively, create a profile in NVIDIA Profile Inspector and add this executable to it.");
        return;
    }

    imgui.TextColored(ICON_SUCCESS, "Matching profile(s): %zu", r.matching_profile_names.size());
    if (nvapi::HasDisplayCommanderProfile(r)) {
        imgui.SameLine();
        if (imgui.Button("Remove Display Commander profile")) {
            auto [ok, err] = nvapi::DeleteDisplayCommanderProfileForCurrentExe();
            if (ok) {
                s_nvidiaProfileDeleteError.clear();
                nvapi::InvalidateProfileSearchCache();
            } else {
                s_nvidiaProfileDeleteError = err;
            }
        }
        if (imgui.IsItemHovered()) {
            imgui.SetTooltip(
                "Remove the NVIDIA profile created by Display Commander for this game (name starts with \"Display "
                "Commander -\").");
        }
    }
    if (!s_nvidiaProfileDeleteError.empty()) {
        imgui.TextColored(ICON_ERROR, "Remove failed: %s", s_nvidiaProfileDeleteError.c_str());
    }
    imgui.Spacing();
    if (imgui.BeginChild("NvidiaProfileSearchList", ImVec2(-1.f, 180.f), true)) {
        if (!r.matching_profiles.empty()) {
            for (const nvapi::MatchedProfileEntry& entry : r.matching_profiles) {
                imgui.Text("  %s", entry.profile_name.c_str());
                if (imgui.IsItemHovered()) {
                    std::string tip = "Profile: " + entry.profile_name + "\n";
                    tip += "Match score: " + std::to_string(entry.score)
                           + " (higher = more specific; non-empty app-entry fields)\n";
                    tip += "App (exe): " + (entry.app_name.empty() ? "(empty)" : entry.app_name) + "\n";
                    if (!entry.user_friendly_name.empty()) {
                        tip += "User-friendly name: " + entry.user_friendly_name + "\n";
                    }
                    if (!entry.launcher.empty()) {
                        tip += "Launcher: " + entry.launcher + "\n";
                    }
                    if (!entry.file_in_folder.empty()) {
                        tip += "File in folder: " + entry.file_in_folder + "\n";
                    }
                    if (entry.is_metro) {
                        tip += "Metro/UWP app: Yes\n";
                    }
                    if (entry.is_command_line && !entry.command_line.empty()) {
                        tip += "Command line: " + entry.command_line + "\n";
                    }
                    imgui.SetTooltip("%s", tip.c_str());
                }
            }
        } else {
            for (const std::string& name : r.matching_profile_names) {
                imgui.Text("  %s", name.c_str());
            }
        }
    }
    imgui.EndChild();

    if (!r.important_settings.empty() && show_advanced_profile_settings != nullptr) {
        imgui.Spacing();
        bool show_advanced = *show_advanced_profile_settings;
        if (imgui.Checkbox("Show advanced profile settings", &show_advanced)) {
            *show_advanced_profile_settings = show_advanced;
        }
        if (imgui.IsItemHovered()) {
            imgui.SetTooltip(
                "Show Ansel profile settings (allow, allowlisted, enable) in addition to the key profile settings.");
        }
        if (imgui.CollapsingHeader("Important profile settings (first matching profile)", TreeNodeFlags_DefaultOpen)) {
            if (!s_nvidiaProfileSetError.empty()) {
                imgui.TextColored(ICON_ERROR, "Last change failed: %s", s_nvidiaProfileSetError.c_str());
                if (imgui.IsItemHovered()) {
                    imgui.SetTooltip(
                        "Try running the game/ReShade as administrator, or change this setting in NVIDIA Profile "
                        "Inspector.");
                }
                if (IsPrivilegeError(s_nvidiaProfileSetError) && s_lastFailedSettingId != 0
                    && !r.current_exe_name.empty()) {
                    imgui.SameLine();
                    if (imgui.Button("Apply as administrator")) {
                        std::wstring exeNameW;
                        int n = MultiByteToWideChar(CP_UTF8, 0, r.current_exe_name.c_str(), -1, nullptr, 0);
                        if (n > 0) {
                            exeNameW.resize(n);
                            MultiByteToWideChar(CP_UTF8, 0, r.current_exe_name.c_str(), -1, exeNameW.data(), n);
                            exeNameW.resize(n - 1);
                        }
                        if (!exeNameW.empty()) {
                            HANDLE hProcess = nullptr;
                            bool ok = RunNvApiSetDwordAsAdmin(
                                s_lastFailedSettingId, s_lastFailedSettingValue, exeNameW, &hProcess);
                            if (ok && hProcess != nullptr) {
                                HANDLE hThread = CreateThread(nullptr, 0, WaitForAdminProcessThenRequestRefresh,
                                                              hProcess, 0, nullptr);
                                if (hThread != nullptr) {
                                    CloseHandle(hThread);
                                } else {
                                    CloseHandle(hProcess);
                                    s_requestProfileRefreshAfterAdmin.store(true);
                                }
                            } else if (ok) {
                                s_nvidiaProfileSetError.clear();
                                s_lastFailedSettingId = 0;
                                nvapi::InvalidateProfileSearchCache();
                            }
                        }
                    }
                    if (imgui.IsItemHovered()) {
                        imgui.SetTooltip(
                            "Run rundll32 as administrator to apply the last failed setting (UAC prompt may appear). "
                            "Profile data will refresh when the process exits.");
                    }
                }
            }
            imgui.TextColored(TEXT_DIMMED, "Key driver settings from the first matching profile above.");
            const int tableFlags = TableFlags_BordersOuter | TableFlags_BordersH | TableFlags_SizingStretchProp;
            if (imgui.BeginTable("NvidiaProfileImportantSettings", 2, tableFlags)) {
                imgui.TableSetupColumn("Setting", TableColumnFlags_WidthStretch, 0.5f);
                imgui.TableSetupColumn("Value", TableColumnFlags_WidthStretch, 0.5f);
                imgui.TableHeadersRow();
                std::vector<const nvapi::ImportantProfileSetting*> to_show;
                for (const auto& e : r.important_settings) {
                    to_show.push_back(&e);
                }
                if (*show_advanced_profile_settings) {
                    for (const auto& e : r.advanced_settings) {
                        to_show.push_back(&e);
                    }
                }
                for (size_t idx = 0; idx < to_show.size(); ++idx) {
                    const auto& s = *to_show[idx];
                    imgui.TableNextRow();
                    imgui.TableSetColumnIndex(0);
                    imgui.TextUnformatted(s.label.c_str());
                    if (imgui.IsItemHovered() && s.setting_id != 0) {
                        char keyBuf[24];
                        (void)snprintf(keyBuf, sizeof(keyBuf), "Key: 0x%08X", static_cast<unsigned>(s.setting_id));
                        imgui.SetTooltip("%s", keyBuf);
                    }
                    imgui.TableSetColumnIndex(1);
                    if (s.setting_id != 0) {
                        if (s.is_bit_field && s.setting_id == nvapi::NVPI_SMOOTH_MOTION_ALLOWED_APIS_ID) {
                            std::vector<std::pair<std::uint32_t, std::string>> flags =
                                nvapi::GetSmoothMotionAllowedApisFlags();
                            std::uint32_t cur = s.value_id;
                            for (size_t fi = 0; fi < flags.size(); ++fi) {
                                const auto& fl = flags[fi];
                                bool checked = (cur & fl.first) != 0;
                                imgui.PushID(static_cast<int>(fl.first));
                                {
                                    std::string checkbox_label =
                                        fl.second + "##" + std::to_string(static_cast<unsigned>(s.setting_id)) + "_"
                                        + std::to_string(static_cast<unsigned>(fl.first));
                                    if (imgui.Checkbox(checkbox_label.c_str(), &checked)) {
                                        std::uint32_t newVal = (cur & ~fl.first) | (checked ? fl.first : 0);
                                        auto [ok, err] = nvapi::SetProfileSetting(s.setting_id, newVal);
                                        if (ok) {
                                            s_nvidiaProfileSetError.clear();
                                            nvapi::InvalidateProfileSearchCache();
                                        } else {
                                            s_nvidiaProfileSetError = err;
                                            if (IsPrivilegeError(err)) {
                                                s_lastFailedSettingId = s.setting_id;
                                                s_lastFailedSettingValue = newVal;
                                            }
                                        }
                                    }
                                }
                                imgui.PopID();
                                if (imgui.IsItemHovered()) {
                                    imgui.SetTooltip("Toggle flag; value is a bitmask (saved immediately).");
                                }
                                if (fi + 1 < flags.size()) {
                                    imgui.SameLine();
                                }
                            }
                            imgui.SameLine();
                            const bool atDefault = (cur == s.default_value);
                            if (atDefault) {
                                imgui.BeginDisabled();
                            }
                            imgui.PushID(static_cast<int>(s.setting_id));
                            {
                                char defaultBtnBuf[64];
                                (void)snprintf(defaultBtnBuf, sizeof(defaultBtnBuf), "Default##%u",
                                               static_cast<unsigned>(s.setting_id));
                                if (imgui.SmallButton(defaultBtnBuf)) {
                                    auto [ok, err] = nvapi::SetProfileSetting(s.setting_id, s.default_value);
                                    if (ok) {
                                        s_nvidiaProfileSetError.clear();
                                        nvapi::InvalidateProfileSearchCache();
                                    } else {
                                        s_nvidiaProfileSetError = err;
                                        if (IsPrivilegeError(err)) {
                                            s_lastFailedSettingId = s.setting_id;
                                            s_lastFailedSettingValue = s.default_value;
                                        }
                                    }
                                }
                            }
                            imgui.PopID();
                            if (atDefault) {
                                imgui.EndDisabled();
                            }
                            if (imgui.IsItemHovered()) {
                                imgui.SetTooltip("Reset to NVIDIA default value.");
                            }
                        } else {
                            ImVec2 avail = imgui.GetContentRegionAvail();
                            ImVec2 textSize = imgui.CalcTextSize("Default");
                            float comboWidth = avail.x
                                - (imgui.GetStyleItemSpacingX() + textSize.x + imgui.GetStyleFramePaddingX() * 2.f);
                            if (comboWidth < 80.f) comboWidth = 80.f;
                            imgui.SetNextItemWidth(comboWidth);
                            char comboBuf[64];
                            (void)snprintf(comboBuf, sizeof(comboBuf), "##NvidiaProfileSetting_%u",
                                           static_cast<unsigned>(s.setting_id));
                            if (imgui.BeginCombo(comboBuf, s.value.c_str(), 0)) {
                                std::vector<std::pair<std::uint32_t, std::string>> opts =
                                    nvapi::GetSettingAvailableValues(s.setting_id);
                                for (const auto& opt : opts) {
                                    const bool selected = (opt.first == s.value_id);
                                    if (imgui.Selectable(opt.second.c_str(), selected)) {
                                        auto [ok, err] = nvapi::SetProfileSetting(s.setting_id, opt.first);
                                        if (ok) {
                                            s_nvidiaProfileSetError.clear();
                                            nvapi::InvalidateProfileSearchCache();
                                        } else {
                                            s_nvidiaProfileSetError = err;
                                            if (IsPrivilegeError(err)) {
                                                s_lastFailedSettingId = s.setting_id;
                                                s_lastFailedSettingValue = opt.first;
                                            }
                                        }
                                    }
                                    if (selected) {
                                        imgui.SetItemDefaultFocus();
                                    }
                                }
                                imgui.EndCombo();
                            }
                            if (imgui.IsItemHovered()) {
                                imgui.SetTooltip("Change value and apply to profile (saved immediately).");
                            }
                            imgui.SameLine();
                            const bool atDefault = (s.value_id == s.default_value);
                            if (atDefault) {
                                imgui.BeginDisabled();
                            }
                            imgui.PushID(static_cast<int>(s.setting_id));
                            {
                                char defaultBtnBuf[64];
                                (void)snprintf(defaultBtnBuf, sizeof(defaultBtnBuf), "Default##%u",
                                               static_cast<unsigned>(s.setting_id));
                                if (imgui.SmallButton(defaultBtnBuf)) {
                                    auto [ok, err] = nvapi::SetProfileSetting(s.setting_id, s.default_value);
                                    if (ok) {
                                        s_nvidiaProfileSetError.clear();
                                        nvapi::InvalidateProfileSearchCache();
                                    } else {
                                        s_nvidiaProfileSetError = err;
                                        if (IsPrivilegeError(err)) {
                                            s_lastFailedSettingId = s.setting_id;
                                            s_lastFailedSettingValue = s.default_value;
                                        }
                                    }
                                }
                            }
                            imgui.PopID();
                            if (atDefault) {
                                imgui.EndDisabled();
                            }
                            if (imgui.IsItemHovered()) {
                                imgui.SetTooltip("Reset to NVIDIA default value.");
                            }
                        }
                    } else {
                        imgui.TextUnformatted(s.value.c_str());
                    }
                }
                imgui.EndTable();
            }
        }
    }

    if (!r.all_settings.empty()) {
        imgui.Spacing();
        if (imgui.CollapsingHeader("All settings in profile", TreeNodeFlags_DefaultOpen)) {
            imgui.TextColored(TEXT_DIMMED,
                              "Every setting present in the first matching profile (%zu total).",
                              r.all_settings.size());
            if (imgui.BeginChild("NvidiaProfileAllSettingsTable", ImVec2(-1.f, 320.f), true)) {
                const int tableFlags2 = TableFlags_BordersOuter | TableFlags_BordersH | TableFlags_SizingStretchProp
                    | TableFlags_ScrollY;
                if (imgui.BeginTable("NvidiaProfileAllSettings", 2, tableFlags2)) {
                    imgui.TableSetupColumn("Setting", TableColumnFlags_WidthStretch, 0.5f);
                    imgui.TableSetupColumn("Value", TableColumnFlags_WidthStretch, 0.5f);
                    imgui.TableSetupScrollFreeze(0, 1);
                    imgui.TableHeadersRow();
                    for (const auto& s : r.all_settings) {
                        imgui.TableNextRow();
                        imgui.TableSetColumnIndex(0);
                        imgui.TextUnformatted(s.label.c_str());
                        if (imgui.IsItemHovered() && s.setting_id != 0) {
                            char keyBuf[24];
                            (void)snprintf(keyBuf, sizeof(keyBuf), "Key: 0x%08X", static_cast<unsigned>(s.setting_id));
                            imgui.SetTooltip("%s", keyBuf);
                        }
                        imgui.TableSetColumnIndex(1);
                        imgui.TextUnformatted(s.value.c_str());
                    }
                    imgui.EndTable();
                }
            }
            imgui.EndChild();
        }
    }

    imgui.TextColored(TEXT_DIMMED,
                      "These profiles will apply when this game runs. Edit with NVIDIA Profile Inspector.");
}

} // namespace ui
} // namespace display_commander
