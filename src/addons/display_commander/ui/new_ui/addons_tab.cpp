#include "addons_tab.hpp"
#include <imgui.h>
#include <reshade.hpp>
#include "../../res/forkawesome.h"
#include "../../res/ui_colors.hpp"
#include "../../utils/logging.hpp"

#include <ShlObj.h>
#include <Windows.h>
#include <algorithm>
#include <atomic>
#include <filesystem>
#include <sstream>
#include <string>
#include <vector>

namespace ui::new_ui {

namespace {
// Global addon list
std::vector<AddonInfo> g_addon_list;
std::atomic<bool> g_addon_list_dirty(true);  // Set to true to trigger refresh

// Get the global addons directory path
std::filesystem::path GetGlobalAddonsDirectory() {
    wchar_t documents_path[MAX_PATH];
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_MYDOCUMENTS, nullptr, SHGFP_TYPE_CURRENT, documents_path))) {
        return std::filesystem::path();
    }
    std::filesystem::path documents_dir(documents_path);
    return documents_dir / L"Display Commander" / L"Reshade" / L"Addons";
}

// Get disabled addons from ReShade config
std::vector<std::string> GetDisabledAddons() {
    std::vector<std::string> disabled_addons;

    // Read DisabledAddons from ReShade config
    // ReShade stores this as a comma-separated list or multiple entries
    char buffer[4096] = {0};
    size_t buffer_size = sizeof(buffer);

    if (reshade::get_config_value(nullptr, "ADDON", "DisabledAddons", buffer, &buffer_size)) {
        // Parse comma-separated list
        std::string disabled_str(buffer);
        std::stringstream ss(disabled_str);
        std::string item;

        while (std::getline(ss, item, ',')) {
            // Trim whitespace
            item.erase(0, item.find_first_not_of(" \t"));
            item.erase(item.find_last_not_of(" \t") + 1);
            if (!item.empty()) {
                disabled_addons.push_back(item);
            }
        }
    }

    return disabled_addons;
}

// Set disabled addons in ReShade config
void SetDisabledAddons(const std::vector<std::string>& disabled_addons) {
    if (disabled_addons.empty()) {
        // Clear the setting if no addons are disabled
        reshade::set_config_value(nullptr, "ADDON", "DisabledAddons", "");
        return;
    }

    // Join with commas
    std::stringstream ss;
    for (size_t i = 0; i < disabled_addons.size(); ++i) {
        if (i > 0) ss << ",";
        ss << disabled_addons[i];
    }

    std::string disabled_str = ss.str();
    reshade::set_config_value(nullptr, "ADDON", "DisabledAddons", disabled_str.c_str());
}

// Check if an addon is disabled
bool IsAddonDisabled(const std::string& addon_name, const std::string& addon_file) {
    std::vector<std::string> disabled = GetDisabledAddons();

    for (const auto& disabled_entry : disabled) {
        // ReShade supports two formats:
        // 1. Just the name: "AddonName"
        // 2. Name with file: "AddonName@filename.addon64"
        size_t at_pos = disabled_entry.find('@');
        if (at_pos == std::string::npos) {
            // Just name match
            if (disabled_entry == addon_name) {
                return true;
            }
        } else {
            // Name@file format
            std::string disabled_name = disabled_entry.substr(0, at_pos);
            std::string disabled_file = disabled_entry.substr(at_pos + 1);
            if (disabled_name == addon_name && disabled_file == addon_file) {
                return true;
            }
        }
    }

    return false;
}

// Enable or disable an addon
void SetAddonEnabled(const std::string& addon_name, const std::string& addon_file, bool enabled) {
    std::vector<std::string> disabled = GetDisabledAddons();

    // Remove existing entry if present
    disabled.erase(std::remove_if(disabled.begin(), disabled.end(),
                                  [&](const std::string& entry) {
                                      size_t at_pos = entry.find('@');
                                      if (at_pos == std::string::npos) {
                                          return entry == addon_name;
                                      } else {
                                          std::string entry_name = entry.substr(0, at_pos);
                                          std::string entry_file = entry.substr(at_pos + 1);
                                          return entry_name == addon_name && entry_file == addon_file;
                                      }
                                  }),
                   disabled.end());

    // Add to disabled list if disabling
    if (!enabled) {
        // Use name@file format for specificity
        std::string disabled_entry = addon_name + "@" + addon_file;
        disabled.push_back(disabled_entry);
    }

    SetDisabledAddons(disabled);
    g_addon_list_dirty.store(true);
}

// Scan for addon files in the global directory
void ScanGlobalAddonsDirectory(std::vector<AddonInfo>& addons) {
    std::filesystem::path addons_dir = GetGlobalAddonsDirectory();

    if (!std::filesystem::exists(addons_dir)) {
        return;
    }

    try {
        std::error_code ec;
        for (const auto& entry : std::filesystem::directory_iterator(
                 addons_dir, std::filesystem::directory_options::skip_permission_denied, ec)) {
            if (ec) {
                continue;
            }

            if (!entry.is_regular_file()) {
                continue;
            }

            const auto& path = entry.path();
            const auto extension = path.extension();

            // Check for .addon64 (64-bit) or .addon32 (32-bit) extensions
            if (extension != L".addon64" && extension != L".addon32") {
                continue;
            }

            // Only include architecture-appropriate addons
#ifdef _WIN64
            if (extension != L".addon64") {
                continue;
            }
#else
            if (extension != L".addon32") {
                continue;
            }
#endif

            AddonInfo info;
            info.file_path = path.string();
            info.file_name = path.filename().string();
            info.name = path.stem().string();  // Name without extension
            info.description = "External addon";
            info.author = "Unknown";
            info.is_external = true;
            info.is_loaded = false;  // We'll check this against ReShade's list
            info.is_enabled = !IsAddonDisabled(info.name, info.file_name);

            addons.push_back(info);
        }
    } catch (const std::exception& e) {
        LogWarn("Exception while scanning addons directory: %s", e.what());
    }
}

// Merge with ReShade's loaded addon info
void MergeReShadeAddonInfo(std::vector<AddonInfo>& addons) {
    // Try to access ReShade's addon_loaded_info
    // Note: This requires accessing ReShade's internal state, which may not be directly accessible
    // For now, we'll mark addons as loaded if they exist in the directory and are not disabled

    // We can't directly access reshade::addon_loaded_info from here as it's in ReShade's namespace
    // So we'll infer loaded status from whether the file exists and is not disabled
    for (auto& addon : addons) {
        if (std::filesystem::exists(addon.file_path) && addon.is_enabled) {
            // Check if the module is loaded by trying to get its handle
            // This is a heuristic - the addon might be loaded by ReShade
            addon.is_loaded = true;  // Optimistic assumption
        }
    }
}

// Refresh the addon list
void RefreshAddonListInternal() {
    g_addon_list.clear();

    // Scan global addons directory
    ScanGlobalAddonsDirectory(g_addon_list);

    // Merge with ReShade's info
    MergeReShadeAddonInfo(g_addon_list);

    // Sort by name
    std::sort(g_addon_list.begin(), g_addon_list.end(),
              [](const AddonInfo& a, const AddonInfo& b) { return a.name < b.name; });
}
}  // namespace

void InitAddonsTab() {
    // Initial refresh
    RefreshAddonListInternal();
}

void RefreshAddonList() { g_addon_list_dirty.store(true); }

void DrawAddonsTab() {
    ImGui::Text("Global Addons Management");
    ImGui::Separator();
    ImGui::Spacing();

    // Check if we need to refresh
    if (g_addon_list_dirty.load()) {
        RefreshAddonListInternal();
        g_addon_list_dirty.store(false);
    }

    // Refresh button
    ui::colors::PushIconColor(ui::colors::ICON_ACTION);
    if (ImGui::Button(ICON_FK_REFRESH " Refresh")) {
        RefreshAddonList();
    }
    ui::colors::PopIconColor();
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Refresh the list of available addons");
    }

    ImGui::SameLine();

    // Open Addons Folder button
    ui::colors::PushIconColor(ui::colors::ICON_ACTION);
    if (ImGui::Button(ICON_FK_FOLDER_OPEN " Open Addons Folder")) {
        std::filesystem::path addons_dir = GetGlobalAddonsDirectory();

        // Create directory if it doesn't exist
        if (!std::filesystem::exists(addons_dir)) {
            try {
                std::filesystem::create_directories(addons_dir);
            } catch (const std::exception& e) {
                LogError("Failed to create addons directory: %s", e.what());
            }
        }

        if (std::filesystem::exists(addons_dir)) {
            std::string addons_dir_str = addons_dir.string();
            HINSTANCE result = ShellExecuteA(nullptr, "explore", addons_dir_str.c_str(), nullptr, nullptr, SW_SHOW);

            if (reinterpret_cast<intptr_t>(result) <= 32) {
                LogError("Failed to open addons folder: %s (Error: %ld)", addons_dir_str.c_str(),
                         reinterpret_cast<intptr_t>(result));
            } else {
                LogInfo("Opened addons folder: %s", addons_dir_str.c_str());
            }
        }
    }
    ui::colors::PopIconColor();
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Open the global addons directory in Windows Explorer");
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Display addon list
    if (g_addon_list.empty()) {
        ImGui::TextColored(ui::colors::TEXT_DIMMED, "No addons found in global directory.");
        ImGui::Spacing();
        ImGui::TextWrapped("Addons should be placed in: %s", GetGlobalAddonsDirectory().string().c_str());
        return;
    }

    // Create table for addon list
    if (ImGui::BeginTable("AddonsTable", 4,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable)) {
        ImGui::TableSetupColumn("Enabled", ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("File", ImGuiTableColumnFlags_WidthFixed, 200.0f);
        ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed, 100.0f);
        ImGui::TableHeadersRow();

        for (size_t i = 0; i < g_addon_list.size(); ++i) {
            auto& addon = g_addon_list[i];

            ImGui::TableNextRow();

            // Enabled checkbox
            ImGui::TableNextColumn();
            bool enabled = addon.is_enabled;
            if (ImGui::Checkbox(("##Enabled" + std::to_string(i)).c_str(), &enabled)) {
                SetAddonEnabled(addon.name, addon.file_name, enabled);
                addon.is_enabled = enabled;
                g_addon_list_dirty.store(true);
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s this addon", enabled ? "Disable" : "Enable");
            }

            // Name
            ImGui::TableNextColumn();
            ImGui::Text("%s", addon.name.c_str());
            if (!addon.description.empty() && ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", addon.description.c_str());
            }

            // File name
            ImGui::TableNextColumn();
            ImGui::TextColored(ui::colors::TEXT_DIMMED, "%s", addon.file_name.c_str());

            // Actions (Open Folder button)
            ImGui::TableNextColumn();
            std::string folder_button_id = "Folder##" + std::to_string(i);
            if (ImGui::Button(folder_button_id.c_str())) {
                std::filesystem::path addon_path(addon.file_path);
                std::filesystem::path folder_path = addon_path.parent_path();

                if (std::filesystem::exists(folder_path)) {
                    std::string folder_str = folder_path.string();
                    HINSTANCE result = ShellExecuteA(nullptr, "explore", folder_str.c_str(), nullptr, nullptr, SW_SHOW);

                    if (reinterpret_cast<intptr_t>(result) <= 32) {
                        LogError("Failed to open folder: %s (Error: %ld)", folder_str.c_str(),
                                 reinterpret_cast<intptr_t>(result));
                    } else {
                        LogInfo("Opened folder: %s", folder_str.c_str());
                    }
                }
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Open the folder containing this addon");
            }
        }

        ImGui::EndTable();
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Info text
    ImGui::TextColored(ui::colors::TEXT_DIMMED,
                       "Note: Changes to addon enabled/disabled state require a game restart to take effect.");
    ImGui::TextColored(ui::colors::TEXT_DIMMED, "Addons directory: %s", GetGlobalAddonsDirectory().string().c_str());
}

}  // namespace ui::new_ui
