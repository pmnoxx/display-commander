#include "updates_tab.hpp"
#include "../imgui_wrapper_base.hpp"
#include "../../res/forkawesome.h"
#include "../../res/ui_colors.hpp"
#include "../../utils/logging.hpp"
#include "../../utils/version_check.hpp"
#include "../../version.hpp"

#include <Windows.h>

#include <shellapi.h>
#include <ShlObj.h>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

#include <imgui.h>
#include <reshade.hpp>

namespace ui::new_ui {

namespace {
// Structure to hold information about a downloaded update file
struct DownloadedUpdateInfo {
    std::filesystem::path file_path;
    std::string version;
    bool is_64bit;
    std::filesystem::file_time_type last_write_time;
    uintmax_t file_size;
};

// Extract build number from filename (e.g., "zzz_display_commander_001234.addon64" -> "001234")
std::string ExtractBuildFromFilename(const std::string& filename) {
    // Pattern: zzz_display_commander_BUILD.addon64/32
    size_t prefix_pos = filename.find("zzz_display_commander_");
    if (prefix_pos == std::string::npos) {
        return "";
    }

    size_t build_start = prefix_pos + 22;  // Length of "zzz_display_commander_"
    size_t build_end = filename.find(".addon", build_start);
    if (build_end == std::string::npos) {
        return "";
    }

    return filename.substr(build_start, build_end - build_start);
}

// Get list of downloaded update files
std::vector<DownloadedUpdateInfo> GetDownloadedUpdates() {
    std::vector<DownloadedUpdateInfo> updates;

    auto download_dir = display_commander::utils::version_check::GetDownloadDirectory();
    if (download_dir.empty() || !std::filesystem::exists(download_dir)) {
        return updates;
    }

    // Scan for all files matching pattern: zzz_display_commander_*.addon64 or .addon32
    try {
        for (const auto& entry : std::filesystem::directory_iterator(download_dir)) {
            if (!entry.is_regular_file()) {
                continue;
            }

            std::string filename = entry.path().filename().string();

            // Check if it matches our pattern
            bool is_64bit = false;
            if (filename.find("zzz_display_commander_") == 0) {
                if (filename.find(".addon64") != std::string::npos) {
                    is_64bit = true;
                } else if (filename.find(".addon32") != std::string::npos) {
                    is_64bit = false;
                } else {
                    continue;  // Not a valid addon file
                }

                DownloadedUpdateInfo info;
                info.file_path = entry.path();
                info.is_64bit = is_64bit;
                info.last_write_time = std::filesystem::last_write_time(entry.path());
                info.file_size = std::filesystem::file_size(entry.path());

                // Extract build number from filename
                std::string build_str = ExtractBuildFromFilename(filename);
                if (!build_str.empty()) {
                    info.version = build_str;
                } else {
                    info.version = "Unknown";
                }

                updates.push_back(info);
            }
        }
    } catch (const std::exception&) {
        // Ignore directory iteration errors
    }

    return updates;
}

// Find latest downloaded version for each architecture
void GetLatestDownloadedVersions(const std::vector<DownloadedUpdateInfo>& updates, DownloadedUpdateInfo* latest_64,
                                 DownloadedUpdateInfo* latest_32) {
    if (latest_64 != nullptr) {
        *latest_64 = DownloadedUpdateInfo{};
        latest_64->version = "";
    }
    if (latest_32 != nullptr) {
        *latest_32 = DownloadedUpdateInfo{};
        latest_32->version = "";
    }

    for (const auto& update : updates) {
        if (update.version == "Unknown") {
            continue;  // Skip files without version info
        }

        DownloadedUpdateInfo* target = update.is_64bit ? latest_64 : latest_32;
        if (target == nullptr) {
            continue;
        }

        // Compare versions (build numbers as strings, but they should be numeric)
        if (target->version.empty() || update.version > target->version) {
            *target = update;
        }
    }
}

// Format file size in human-readable format
std::string FormatFileSize(uintmax_t size) {
    const char* units[] = {"B", "KB", "MB", "GB"};
    int unit_index = 0;
    double file_size = static_cast<double>(size);

    while (file_size >= 1024.0 && unit_index < 3) {
        file_size /= 1024.0;
        unit_index++;
    }

    char buffer[64];
    snprintf(buffer, sizeof(buffer), "%.2f %s", file_size, units[unit_index]);
    return std::string(buffer);
}

// Format file time
std::string FormatFileTime(const std::filesystem::file_time_type& time) {
    auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
        time - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now());
    std::time_t tt = std::chrono::system_clock::to_time_t(sctp);
    char buffer[64];
    struct tm timeinfo;
    if (localtime_s(&timeinfo, &tt) == 0) {
        strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &timeinfo);
        return std::string(buffer);
    }
    return "Unknown";
}

// Open folder in Windows Explorer
void OpenFolderInExplorer(const std::filesystem::path& folder_path) {
    std::wstring folder_wstr = folder_path.wstring();
    ShellExecuteW(nullptr, L"open", L"explorer.exe", folder_wstr.c_str(), nullptr, SW_SHOWNORMAL);
}

}  // anonymous namespace

void DrawUpdatesTab(display_commander::ui::IImGuiWrapper& imgui) {
    using namespace display_commander::ui::wrapper_flags;
    using display_commander::utils::version_check::CheckForUpdates;
    using display_commander::utils::version_check::DownloadUpdate;
    using display_commander::utils::version_check::GetDownloadDirectory;
    using display_commander::utils::version_check::GetVersionCheckState;
    using display_commander::utils::version_check::VersionComparison;

    imgui.Spacing();
    imgui.TextColored(ui::colors::TEXT_DEFAULT, "Update Management");
    imgui.Separator();
    imgui.Spacing();

    // Current version info
    imgui.Text("Current Version:");
    imgui.SameLine();
    imgui.TextColored(ui::colors::TEXT_HIGHLIGHT, "%s", DISPLAY_COMMANDER_VERSION_STRING);
    imgui.Spacing();

    // Version check status from main tab
    auto& state = GetVersionCheckState();
    VersionComparison status = state.status.load();
    std::string* latest_version_ptr = state.latest_version.load();

    // Show version status
    if (status == VersionComparison::UpdateAvailable && latest_version_ptr != nullptr) {
        imgui.TextColored(ui::colors::TEXT_WARNING, ICON_FK_WARNING " New version available: v%s",
                           latest_version_ptr->c_str());
    } else if (status == VersionComparison::UpToDate) {
        imgui.TextColored(ui::colors::TEXT_SUCCESS, ICON_FK_OK " You are running the latest version");
        if (latest_version_ptr != nullptr) {
            imgui.SameLine();
            imgui.TextColored(ui::colors::TEXT_DIMMED, "(v%s)", latest_version_ptr->c_str());
        }
    } else if (status == VersionComparison::Checking) {
        imgui.TextColored(ui::colors::TEXT_DIMMED, ICON_FK_REFRESH " Checking for updates...");
    } else if (status == VersionComparison::CheckFailed) {
        std::string* error_ptr = state.error_message.load();
        if (error_ptr != nullptr) {
            imgui.TextColored(ui::colors::TEXT_ERROR, ICON_FK_WARNING " Check failed: %s", error_ptr->c_str());
        } else {
            imgui.TextColored(ui::colors::TEXT_DIMMED, "Version check not performed yet");
        }
    }

    imgui.Spacing();

    // Download buttons - show whenever we have download URLs available
    std::string* download_url_64 = state.download_url_64.load();
    std::string* download_url_32 = state.download_url_32.load();

    if ((download_url_64 != nullptr && !download_url_64->empty())
        || (download_url_32 != nullptr && !download_url_32->empty())) {
        imgui.Text("Download latest version:");
        imgui.Spacing();

        if (download_url_64 != nullptr && !download_url_64->empty()) {
            if (imgui.Button("Download 64-bit")) {
                std::thread download_thread([]() {
                    if (DownloadUpdate(true)) {
                        LogInfo("64-bit update downloaded successfully");
                    } else {
                        LogError("Failed to download 64-bit update");
                    }
                });
                download_thread.detach();
            }
            if (imgui.IsItemHovered()) {
                auto download_dir = GetDownloadDirectory();
                std::string download_path_str = download_dir.string();
                imgui.SetTooltip("Download 64-bit version to:\n%s\nFilename: zzz_display_commander_BUILD.addon64",
                                  download_path_str.c_str());
            }
            imgui.SameLine();
        }

        if (download_url_32 != nullptr && !download_url_32->empty()) {
            if (imgui.Button("Download 32-bit")) {
                std::thread download_thread([]() {
                    if (DownloadUpdate(false)) {
                        LogInfo("32-bit update downloaded successfully");
                    } else {
                        LogError("Failed to download 32-bit update");
                    }
                });
                download_thread.detach();
            }
            if (imgui.IsItemHovered()) {
                auto download_dir = GetDownloadDirectory();
                std::string download_path_str = download_dir.string();
                imgui.SetTooltip("Download 32-bit version to:\n%s\nFilename: zzz_display_commander_BUILD.addon32",
                                  download_path_str.c_str());
            }
        }
        imgui.Spacing();
    } else if (status != VersionComparison::Checking) {
        imgui.TextColored(ui::colors::TEXT_DIMMED, "Download URLs not available. Check for updates first.");
        imgui.Spacing();
    }

    // Manual check button
    if (imgui.Button(ICON_FK_REFRESH " Check for Updates")) {
        if (!state.checking.load()) {
            CheckForUpdates();
        }
    }
    if (imgui.IsItemHovered()) {
        imgui.SetTooltip("Check GitHub for the latest release");
    }

    imgui.Spacing();
    imgui.Separator();
    imgui.Spacing();

    // Latest downloaded versions section
    imgui.TextColored(ui::colors::TEXT_DEFAULT, "Latest Downloaded Versions");
    imgui.Spacing();

    auto downloaded_updates = GetDownloadedUpdates();
    DownloadedUpdateInfo latest_64, latest_32;
    GetLatestDownloadedVersions(downloaded_updates, &latest_64, &latest_32);

    // Show latest 64-bit
    if (!latest_64.version.empty() && latest_64.version != "Unknown") {
        imgui.Text("64-bit: Build %s", latest_64.version.c_str());
        imgui.SameLine();
        imgui.TextColored(ui::colors::TEXT_DIMMED, "(%s, %s)", FormatFileSize(latest_64.file_size).c_str(),
                           FormatFileTime(latest_64.last_write_time).c_str());
    } else {
        imgui.TextColored(ui::colors::TEXT_DIMMED, "64-bit: No downloaded version");
    }

    // Show latest 32-bit
    if (!latest_32.version.empty() && latest_32.version != "Unknown") {
        imgui.Text("32-bit: Build %s", latest_32.version.c_str());
        imgui.SameLine();
        imgui.TextColored(ui::colors::TEXT_DIMMED, "(%s, %s)", FormatFileSize(latest_32.file_size).c_str(),
                           FormatFileTime(latest_32.last_write_time).c_str());
    } else {
        imgui.TextColored(ui::colors::TEXT_DIMMED, "32-bit: No downloaded version");
    }

    imgui.Spacing();
    imgui.Separator();
    imgui.Spacing();

    // All downloaded updates section
    imgui.TextColored(ui::colors::TEXT_DEFAULT, "All Downloaded Updates");
    imgui.Spacing();

    if (downloaded_updates.empty()) {
        imgui.TextColored(ui::colors::TEXT_DIMMED,
                           "No downloaded updates found in %%localappdata%%\\Programs\\Display_Commander");
        imgui.Spacing();
        imgui.TextColored(ui::colors::TEXT_DIMMED,
                           "Downloaded updates will appear here after downloading from the Main tab or this tab.");
    } else {
        const int table_flags = TableFlags_Borders | TableFlags_RowBg | TableFlags_Resizable;
        if (imgui.BeginTable("DownloadedUpdates", 6, table_flags)) {
            imgui.TableSetupColumn("Architecture", TableColumnFlags_WidthFixed, 100.0f);
            imgui.TableSetupColumn("Build", TableColumnFlags_WidthFixed, 100.0f);
            imgui.TableSetupColumn("File", TableColumnFlags_WidthStretch);
            imgui.TableSetupColumn("Size", TableColumnFlags_WidthFixed, 100.0f);
            imgui.TableSetupColumn("Downloaded", TableColumnFlags_WidthFixed, 150.0f);
            imgui.TableSetupColumn("Actions", TableColumnFlags_WidthFixed, 250.0f);
            imgui.TableHeadersRow();

            for (const auto& update : downloaded_updates) {
                imgui.TableNextRow();

                // Architecture
                imgui.TableSetColumnIndex(0);
                imgui.Text("%s", update.is_64bit ? "64-bit" : "32-bit");

                // Build number
                imgui.TableSetColumnIndex(1);
                if (update.version != "Unknown") {
                    imgui.TextColored(ui::colors::TEXT_HIGHLIGHT, "%s", update.version.c_str());
                } else {
                    imgui.TextColored(ui::colors::TEXT_DIMMED, "Unknown");
                }

                // File name
                imgui.TableSetColumnIndex(2);
                std::string filename = update.file_path.filename().string();
                imgui.Text("%s", filename.c_str());

                // File size
                imgui.TableSetColumnIndex(3);
                imgui.Text("%s", FormatFileSize(update.file_size).c_str());

                // Download time
                imgui.TableSetColumnIndex(4);
                imgui.Text("%s", FormatFileTime(update.last_write_time).c_str());

                // Actions
                imgui.TableSetColumnIndex(5);
                std::string open_label = "Open Folder##" + filename;
                if (imgui.SmallButton(open_label.c_str())) {
                    OpenFolderInExplorer(update.file_path.parent_path());
                }
                if (imgui.IsItemHovered()) {
                    imgui.SetTooltip("Open folder containing the downloaded file");
                }

                imgui.SameLine();

                std::string delete_label = ICON_FK_CANCEL "##Delete" + filename;
                imgui.PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.2f, 0.2f, 1.0f));
                imgui.PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.9f, 0.3f, 0.3f, 1.0f));
                imgui.PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.5f, 0.1f, 0.1f, 1.0f));
                if (imgui.SmallButton(delete_label.c_str())) {
                    try {
                        if (std::filesystem::exists(update.file_path)) {
                            std::filesystem::remove(update.file_path);
                            LogInfo("Deleted downloaded update: %ls", update.file_path.c_str());
                        }
                    } catch (const std::exception& e) {
                        LogError("Failed to delete file %ls: %s", update.file_path.c_str(), e.what());
                    } catch (...) {
                        LogError("Failed to delete file %ls: Unknown error", update.file_path.c_str());
                    }
                }
                imgui.PopStyleColor(3);
                if (imgui.IsItemHovered()) {
                    imgui.SetTooltip("Delete this downloaded file\nWarning: This action cannot be undone");
                }
            }

            imgui.EndTable();
        }

        imgui.Spacing();
        imgui.TextColored(ui::colors::TEXT_DIMMED, "To install an update:");
        imgui.BulletText("Close the game");
        imgui.BulletText("Copy the downloaded file to your ReShade addons folder");
        imgui.BulletText("Replace the existing zzz_display_commander.addon64 (or .addon32) file");
        imgui.BulletText("Restart the game");

        imgui.Spacing();
        auto download_dir = GetDownloadDirectory();
        if (!download_dir.empty()) {
            if (imgui.Button("Open Downloads Folder")) {
                OpenFolderInExplorer(download_dir);
            }
            if (imgui.IsItemHovered()) {
                std::string download_path_str = download_dir.string();
                imgui.SetTooltip("Open: %s", download_path_str.c_str());
            }
        }
    }

    imgui.Spacing();
    imgui.Separator();
    imgui.Spacing();

    // Check for both architectures
    imgui.TextColored(ui::colors::TEXT_DEFAULT, "Architecture Information");
    imgui.Spacing();
#ifdef _WIN64
    imgui.Text("Current build: 64-bit");
#else
    imgui.Text("Current build: 32-bit");
#endif
    imgui.TextColored(ui::colors::TEXT_DIMMED, "Note: Both 64-bit and 32-bit versions can be downloaded and stored.");
}

}  // namespace ui::new_ui
