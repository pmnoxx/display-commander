#include "nvidia_profile_tab_shared.hpp"

#include "../res/ui_colors.hpp"
#include "../utils.hpp"
#include "nvapi/nvidia_profile_search.hpp"
#include "nvapi/nvpi_reference.hpp"
#include "nvapi/run_nvapi_setdword_as_admin.hpp"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <fstream>
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
static std::string s_adminApplyResultMessage;
static std::atomic<bool> s_adminApplyResultReady{false};
static std::vector<nvapi::ImportantProfileSetting> s_allDriverSettingsCache;
static bool s_allDriverSettingsCacheValid = false;
static bool s_showOnlySetDriverSettings = true;  // default on: show only settings that have a value set in the profile

struct AdminWaitParams {
    HANDLE hProcess = nullptr;
    std::wstring resultPath;
};

static DWORD WINAPI WaitForAdminProcessThenRequestRefresh(LPVOID param) {
    AdminWaitParams* wp = static_cast<AdminWaitParams*>(param);
    if (wp == nullptr) {
        return 0;
    }
    if (wp->hProcess != nullptr) {
        WaitForSingleObject(wp->hProcess, 60000);
        CloseHandle(wp->hProcess);
    }
    if (!wp->resultPath.empty()) {
        std::ifstream f(std::filesystem::path(wp->resultPath));
        if (f) {
            std::string line;
            if (std::getline(f, line)) {
                const char prefix[] = "ERROR: ";
                if (line.size() >= sizeof(prefix) - 1 && line.compare(0, sizeof(prefix) - 1, prefix) == 0) {
                    s_adminApplyResultMessage = line.substr(sizeof(prefix) - 1);
                } else {
                    s_adminApplyResultMessage.clear();
                }
            }
        }
        s_adminApplyResultReady.store(true);
    }
    s_requestProfileRefreshAfterAdmin.store(true);
    delete wp;
    return 0;
}

static bool IsPrivilegeError(const std::string& err) {
    return err.find("INVALID_USER_PRIVILEGE") != std::string::npos || err.find("0xffffff77") != std::string::npos;
}

void DrawNvidiaProfileTab(GraphicsApi /* api */, IImGuiWrapper& imgui, bool* show_advanced_profile_settings) {
    if (s_requestProfileRefreshAfterAdmin.exchange(false)) {
        if (s_adminApplyResultReady.exchange(false)) {
            s_nvidiaProfileSetError = s_adminApplyResultMessage;
        } else {
            s_nvidiaProfileSetError.clear();
        }
        s_lastFailedSettingId = 0;
        s_allDriverSettingsCacheValid = false;
        nvapi::InvalidateProfileSearchCache();
    }
    {
        static bool s_nvidiaProfileTabOpenedOnce = false;
        if (!s_nvidiaProfileTabOpenedOnce) {
            s_nvidiaProfileTabOpenedOnce = true;
            s_allDriverSettingsCacheValid = false;
            nvapi::InvalidateProfileSearchCache();
        }
    }
    imgui.Text("NVIDIA Inspector profile search");
    imgui.SameLine();
    if (imgui.Button("Refresh")) {
        s_allDriverSettingsCacheValid = false;
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
        imgui.SetTooltip(
            "Enumerate all setting IDs recognized by the current NVIDIA driver and write them to a text file next to "
            "the addon DLL.");
    }
    if (!s_dumpDriverSettingsResult.empty()) {
        imgui.SameLine();
        imgui.TextColored(s_dumpDriverSettingsResult.substr(0, 5) == "Dumped" ? TEXT_DIMMED : TEXT_ERROR, "%s",
                          s_dumpDriverSettingsResult.c_str());
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
                    && !r.current_exe_path.empty()) {
                    imgui.SameLine();
                    if (imgui.Button("Apply as administrator")) {
                        std::wstring exePathW;
                        int n = MultiByteToWideChar(CP_UTF8, 0, r.current_exe_path.c_str(), -1, nullptr, 0);
                        if (n > 0) {
                            exePathW.resize(n);
                            MultiByteToWideChar(CP_UTF8, 0, r.current_exe_path.c_str(), -1, exePathW.data(), n);
                            exePathW.resize(n - 1);
                        }
                        if (exePathW.empty()) {
                            s_nvidiaProfileSetError =
                                "Apply as administrator failed: no executable path (profile not matched).";
                        } else {
                            wchar_t tempDir[MAX_PATH] = {};
                            if (GetTempPathW(static_cast<DWORD>(sizeof(tempDir) / sizeof(tempDir[0])), tempDir) == 0) {
                                s_nvidiaProfileSetError = "Apply as administrator failed: could not get temp path.";
                            } else {
                                wchar_t resultPathBuf[MAX_PATH] = {};
                                (void)swprintf_s(resultPathBuf, L"%sDisplayCommander_AdminResult_%lu_%lu.txt", tempDir,
                                                 static_cast<unsigned long>(GetCurrentProcessId()),
                                                 static_cast<unsigned long>(GetTickCount64()));
                                std::wstring resultFilePath(resultPathBuf);
                                HANDLE hProcess = nullptr;
                                std::string adminError;
                                bool ok = RunNvApiSetDwordAsAdmin(s_lastFailedSettingId, s_lastFailedSettingValue,
                                                                  exePathW, &hProcess, &adminError, &resultFilePath);
                                if (ok && hProcess != nullptr) {
                                    AdminWaitParams* wp = new AdminWaitParams;
                                    wp->hProcess = hProcess;
                                    wp->resultPath = resultFilePath;
                                    HANDLE hThread =
                                        CreateThread(nullptr, 0, WaitForAdminProcessThenRequestRefresh, wp, 0, nullptr);
                                    if (hThread != nullptr) {
                                        CloseHandle(hThread);
                                    } else {
                                        CloseHandle(hProcess);
                                        delete wp;
                                        s_requestProfileRefreshAfterAdmin.store(true);
                                    }
                                } else if (ok) {
                                    s_nvidiaProfileSetError.clear();
                                    s_lastFailedSettingId = 0;
                                    nvapi::InvalidateProfileSearchCache();
                                } else {
                                    s_nvidiaProfileSetError = adminError;
                                }
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
                    if (s.requires_admin) {
                        imgui.TextColored(TEXT_WARNING, "%s", s.label.c_str());
                    } else {
                        imgui.TextUnformatted(s.label.c_str());
                    }
                    if (imgui.IsItemHovered()) {
                        std::string tip = nvapi::GetSettingDriverDebugTooltip(s.setting_id, s.label);
                        if (s.requires_admin && !tip.empty()) tip += "\n";
                        if (s.requires_admin) tip += "Requires admin to change.";
                        if (s.min_required_driver_version != 0) {
                            if (!tip.empty()) tip += "\n";
                            char buf[64];
                            snprintf(buf, sizeof(buf), "Requires driver %u.%02u or newer.",
                                     s.min_required_driver_version / 100, s.min_required_driver_version % 100);
                            tip += buf;
                        }
                        imgui.SetTooltip("%s", tip.c_str());
                    }
                    imgui.TableSetColumnIndex(1);
                    if (s.setting_id != 0) {
                        if (s.is_bit_field && s.setting_id == nvapi::NVPI_SMOOTH_MOTION_ALLOWED_APIS_ID) {
                            // Show current value; single action "Allow - All [DX11/12, VK]" (no per-API disallow).
                            imgui.TextUnformatted(s.value.c_str());
                            imgui.SameLine();
                            const std::uint32_t allApisVal = nvapi::NVPI_SMOOTH_MOTION_ALLOWED_APIS_ALL;
                            const bool alreadyAll = (s.value_id == allApisVal);
                            if (alreadyAll) {
                                imgui.BeginDisabled();
                            }
                            imgui.PushID(static_cast<int>(s.setting_id));
                            if (imgui.SmallButton("Allow - All [DX11/12, VK]")) {
                                auto [ok, err] = nvapi::SetProfileSetting(s.setting_id, allApisVal);
                                if (ok) {
                                    s_nvidiaProfileSetError.clear();
                                    nvapi::InvalidateProfileSearchCache();
                                } else {
                                    s_nvidiaProfileSetError = err;
                                    if (IsPrivilegeError(err)) {
                                        s_lastFailedSettingId = s.setting_id;
                                        s_lastFailedSettingValue = allApisVal;
                                    }
                                }
                            }
                            imgui.PopID();
                            if (alreadyAll) {
                                imgui.EndDisabled();
                            }
                            if (imgui.IsItemHovered()) {
                                imgui.SetTooltip("Set allowed APIs to DX11, DX12, and Vulkan (saved immediately).");
                            }
                        } else {
                            ImVec2 avail = imgui.GetContentRegionAvail();
                            ImVec2 textSize = imgui.CalcTextSize("Default");
                            float comboWidth =
                                avail.x
                                - (imgui.GetStyleItemSpacingX() + textSize.x + imgui.GetStyleFramePaddingX() * 2.f);
                            if (comboWidth < 80.f) comboWidth = 80.f;
                            imgui.SetNextItemWidth(comboWidth);
                            char comboBuf[64];
                            (void)snprintf(comboBuf, sizeof(comboBuf), "##NvidiaProfileSetting_%u",
                                           static_cast<unsigned>(s.setting_id));
                            if (imgui.BeginCombo(comboBuf, s.value.c_str(), 0)) {
                                const bool useGlobalSelected = !s.set_in_profile;
                                if (imgui.Selectable("Use global (Default)", useGlobalSelected)) {
                                    auto [ok, err] = nvapi::DeleteProfileSettingForCurrentExe(s.setting_id);
                                    if (ok) {
                                        s_nvidiaProfileSetError.clear();
                                        nvapi::InvalidateProfileSearchCache();
                                    } else {
                                        s_nvidiaProfileSetError = err;
                                    }
                                }
                                if (useGlobalSelected) imgui.SetItemDefaultFocus();
                                std::vector<std::pair<std::uint32_t, std::string>> opts =
                                    nvapi::GetSettingAvailableValues(s.setting_id);
                                for (const auto& opt : opts) {
                                    const bool selected = (s.set_in_profile && opt.first == s.value_id);
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
                                    if (selected) imgui.SetItemDefaultFocus();
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
            imgui.TextColored(TEXT_DIMMED, "Every setting present in the first matching profile (%zu total).",
                              r.all_settings.size());
            if (imgui.BeginChild("NvidiaProfileAllSettingsTable", ImVec2(-1.f, 320.f), true)) {
                const int tableFlags2 =
                    TableFlags_BordersOuter | TableFlags_BordersH | TableFlags_SizingStretchProp | TableFlags_ScrollY;
                if (imgui.BeginTable("NvidiaProfileAllSettings", 2, tableFlags2)) {
                    imgui.TableSetupColumn("Setting", TableColumnFlags_WidthStretch, 0.5f);
                    imgui.TableSetupColumn("Value", TableColumnFlags_WidthStretch, 0.5f);
                    imgui.TableSetupScrollFreeze(0, 1);
                    imgui.TableHeadersRow();
                    for (const auto& s : r.all_settings) {
                        imgui.TableNextRow();
                        imgui.TableSetColumnIndex(0);
                        if (s.requires_admin) {
                            imgui.TextColored(TEXT_WARNING, "%s", s.label.c_str());
                        } else {
                            imgui.TextUnformatted(s.label.c_str());
                        }
                        if (imgui.IsItemHovered()) {
                            std::string tip = nvapi::GetSettingDriverDebugTooltip(s.setting_id, s.label);
                            if (s.requires_admin && !tip.empty()) tip += "\n";
                            if (s.requires_admin) tip += "Requires admin to change.";
                            if (s.min_required_driver_version != 0) {
                                if (!tip.empty()) tip += "\n";
                                char buf[64];
                                snprintf(buf, sizeof(buf), "Requires driver %u.%02u or newer.",
                                         s.min_required_driver_version / 100, s.min_required_driver_version % 100);
                                tip += buf;
                            }
                            imgui.SetTooltip("%s", tip.c_str());
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

    imgui.Spacing();
    if (imgui.CollapsingHeader("All driver settings (editable)")) {
        imgui.TextColored(TEXT_DIMMED,
                          "Every setting recognized by the current driver. Same list as the dump file. Set value or "
                          "reset to default.");
        if (imgui.Checkbox("Show only settings with values set", &s_showOnlySetDriverSettings)) {
        }
        if (imgui.IsItemHovered()) {
            imgui.SetTooltip(
                "When on, only settings that have a value in this profile are shown. Turn off to see all driver "
                "settings.");
        }
        if (!s_allDriverSettingsCacheValid) {
            s_allDriverSettingsCache = nvapi::GetDriverSettingsWithProfileValues();
            s_allDriverSettingsCacheValid = true;
        }
        if (imgui.BeginChild("NvidiaProfileAllDriverSettingsTable", ImVec2(-1.f, 400.f), true)) {
            const int tableFlagsAll =
                TableFlags_BordersOuter | TableFlags_BordersH | TableFlags_SizingStretchProp | TableFlags_ScrollY;
            if (imgui.BeginTable("NvidiaProfileAllDriverSettings", 3, tableFlagsAll)) {
                imgui.TableSetupColumn("Setting", TableColumnFlags_WidthStretch, 0.4f);
                imgui.TableSetupColumn("Value", TableColumnFlags_WidthStretch, 0.45f);
                imgui.TableSetupColumn("", TableColumnFlags_WidthFixed, 120.f);
                imgui.TableSetupScrollFreeze(0, 1);
                imgui.TableHeadersRow();
                for (size_t idx = 0; idx < s_allDriverSettingsCache.size(); ++idx) {
                    nvapi::ImportantProfileSetting& s = s_allDriverSettingsCache[idx];
                    if (s_showOnlySetDriverSettings && s.value == "Not set") {
                        continue;
                    }
                    imgui.TableNextRow();
                    imgui.TableSetColumnIndex(0);
                    if (s.requires_admin) {
                        imgui.TextColored(TEXT_WARNING, "%s", s.label.c_str());
                    } else {
                        imgui.TextUnformatted(s.label.c_str());
                    }
                    if (imgui.IsItemHovered()) {
                        std::string tip = nvapi::GetSettingDriverDebugTooltip(s.setting_id, s.label);
                        if (s.requires_admin && !tip.empty()) tip += "\n";
                        if (s.requires_admin) tip += "Requires admin to change.";
                        if (s.min_required_driver_version != 0) {
                            if (!tip.empty()) tip += "\n";
                            char buf[64];
                            snprintf(buf, sizeof(buf), "Requires driver %u.%02u or newer.",
                                     s.min_required_driver_version / 100, s.min_required_driver_version % 100);
                            tip += buf;
                        }
                        imgui.SetTooltip("%s", tip.c_str());
                    }
                    imgui.TableSetColumnIndex(1);
                    if (!s.known_to_driver) {
                        imgui.TextUnformatted(s.value.c_str());
                        if (imgui.IsItemHovered()) {
                            imgui.SetTooltip(
                                "In profile but not in driver's recognized list. Edit in NVIDIA Profile Inspector or "
                                "Delete to remove.");
                        }
                    } else if (s.is_bit_field && s.setting_id == nvapi::NVPI_SMOOTH_MOTION_ALLOWED_APIS_ID) {
                        imgui.TextUnformatted(s.value.c_str());
                        imgui.SameLine();
                        const std::uint32_t allApisVal = nvapi::NVPI_SMOOTH_MOTION_ALLOWED_APIS_ALL;
                        const bool alreadyAll = (s.value_id == allApisVal);
                        if (alreadyAll) {
                            imgui.BeginDisabled();
                        }
                        imgui.PushID(static_cast<int>(s.setting_id + (static_cast<std::uint32_t>(idx) << 16)));
                        if (imgui.SmallButton("Allow - All [DX11/12, VK]")) {
                            auto [ok, err] = nvapi::SetProfileSetting(s.setting_id, allApisVal);
                            if (ok) {
                                s_nvidiaProfileSetError.clear();
                                s_allDriverSettingsCacheValid = false;
                                nvapi::InvalidateProfileSearchCache();
                            } else {
                                s_nvidiaProfileSetError = err;
                                if (IsPrivilegeError(err)) {
                                    s_lastFailedSettingId = s.setting_id;
                                    s_lastFailedSettingValue = allApisVal;
                                }
                            }
                        }
                        imgui.PopID();
                        if (alreadyAll) {
                            imgui.EndDisabled();
                        }
                        if (imgui.IsItemHovered()) {
                            imgui.SetTooltip("Set allowed APIs to DX11, DX12, and Vulkan (saved immediately).");
                        }
                    } else {
                        std::vector<std::pair<std::uint32_t, std::string>> opts =
                            nvapi::GetSettingAvailableValues(s.setting_id);
                        if (!opts.empty()) {
                            ImVec2 avail = imgui.GetContentRegionAvail();
                            imgui.SetNextItemWidth(avail.x - 100.f);
                            char comboBuf[64];
                            (void)snprintf(comboBuf, sizeof(comboBuf), "##AllDriver_%zu", idx);
                            if (imgui.BeginCombo(comboBuf, s.value.c_str(), 0)) {
                                const bool useGlobalSelected = !s.set_in_profile;
                                if (imgui.Selectable("Use global (Default)", useGlobalSelected)) {
                                    auto [ok, err] = nvapi::DeleteProfileSettingForCurrentExe(s.setting_id);
                                    if (ok) {
                                        s_nvidiaProfileSetError.clear();
                                        s_allDriverSettingsCacheValid = false;
                                        nvapi::InvalidateProfileSearchCache();
                                    } else {
                                        s_nvidiaProfileSetError = err;
                                    }
                                }
                                if (useGlobalSelected) imgui.SetItemDefaultFocus();
                                for (const auto& opt : opts) {
                                    const bool selected = (s.set_in_profile && opt.first == s.value_id);
                                    if (imgui.Selectable(opt.second.c_str(), selected)) {
                                        auto [ok, err] = nvapi::SetProfileSetting(s.setting_id, opt.first);
                                        if (ok) {
                                            s_nvidiaProfileSetError.clear();
                                            s_allDriverSettingsCacheValid = false;
                                            nvapi::InvalidateProfileSearchCache();
                                        } else {
                                            s_nvidiaProfileSetError = err;
                                            if (IsPrivilegeError(err)) {
                                                s_lastFailedSettingId = s.setting_id;
                                                s_lastFailedSettingValue = opt.first;
                                            }
                                        }
                                    }
                                    if (selected) imgui.SetItemDefaultFocus();
                                }
                                imgui.EndCombo();
                            }
                        } else {
                            imgui.TextUnformatted(s.value.c_str());
                        }
                    }
                    imgui.TableSetColumnIndex(2);
                    imgui.PushID(static_cast<int>(idx + (s.setting_id << 16)));
                    if (s.known_to_driver) {
                        const bool atDefault = (s.value_id == s.default_value);
                        if (atDefault) imgui.BeginDisabled();
                        if (imgui.SmallButton("Default")) {
                            auto [ok, err] = nvapi::SetProfileSetting(s.setting_id, s.default_value);
                            if (ok) {
                                s_nvidiaProfileSetError.clear();
                                s_allDriverSettingsCacheValid = false;
                                nvapi::InvalidateProfileSearchCache();
                            } else {
                                s_nvidiaProfileSetError = err;
                                if (IsPrivilegeError(err)) {
                                    s_lastFailedSettingId = s.setting_id;
                                    s_lastFailedSettingValue = s.default_value;
                                }
                            }
                        }
                        if (atDefault) imgui.EndDisabled();
                        if (imgui.IsItemHovered()) {
                            imgui.SetTooltip("Set to driver default.");
                        }
                        imgui.SameLine();
                    }
                    if (imgui.SmallButton("Delete")) {
                        auto [ok, err] = nvapi::DeleteProfileSettingForCurrentExe(s.setting_id);
                        if (ok) {
                            s_nvidiaProfileSetError.clear();
                            s_allDriverSettingsCacheValid = false;
                        } else {
                            s_nvidiaProfileSetError = err;
                        }
                    }
                    if (imgui.IsItemHovered()) {
                        imgui.SetTooltip("Remove from profile (use driver default).");
                    }
                    imgui.PopID();
                }
                imgui.EndTable();
            }
            imgui.EndChild();
        }
    }

    imgui.TextColored(TEXT_DIMMED,
                      "These profiles will apply when this game runs. Edit with NVIDIA Profile Inspector.");
}

}  // namespace ui
}  // namespace display_commander
