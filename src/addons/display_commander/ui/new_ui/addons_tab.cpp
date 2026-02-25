#include "addons_tab.hpp"
#include "../imgui_wrapper_base.hpp"
#include <imgui.h>
#include <reshade.hpp>
#include "../../config/display_commander_config.hpp"
#include "../../res/forkawesome.h"
#include "../../res/ui_colors.hpp"
#include "../../settings/reshade_tab_settings.hpp"
#include "../../utils/general_utils.hpp"
#include "../../utils/logging.hpp"
#include "../../utils/reshade_global_config.hpp"
#include "../../utils/version_check.hpp"

#include <psapi.h>
#include <ShlObj.h>
#include <Windows.h>
#include <algorithm>
#include <atomic>
#include <filesystem>
#include <set>
#include <string>
#include <vector>

namespace ui::new_ui {

// Forward declaration
void DrawReShadeGlobalConfigSettings(display_commander::ui::IImGuiWrapper& imgui);

namespace {
// Global addon list
std::vector<AddonInfo> g_addon_list;
std::atomic<bool> g_addon_list_dirty(true);  // Set to true to trigger refresh

// Warning dialog state for addon enable/disable
std::atomic<bool> g_show_addon_restart_warning(false);

// Get the global addons directory path
std::filesystem::path GetGlobalAddonsDirectory() {
    wchar_t localappdata_path[MAX_PATH];
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, localappdata_path))) {
        return std::filesystem::path();
    }
    std::filesystem::path localappdata_dir(localappdata_path);
    return localappdata_dir / L"Programs" / L"Display_Commander" / L"Reshade" / L"Addons";
}

// Get the shaders directory path
std::filesystem::path GetShadersDirectory() {
    wchar_t localappdata_path[MAX_PATH];
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, localappdata_path))) {
        return std::filesystem::path();
    }
    std::filesystem::path localappdata_dir(localappdata_path);
    return localappdata_dir / L"Programs" / L"Display_Commander" / L"Reshade" / L"Shaders";
}

// Get the textures directory path
std::filesystem::path GetTexturesDirectory() {
    wchar_t localappdata_path[MAX_PATH];
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, localappdata_path))) {
        return std::filesystem::path();
    }
    std::filesystem::path localappdata_dir(localappdata_path);
    return localappdata_dir / L"Programs" / L"Display_Commander" / L"Reshade" / L"Textures";
}

// Get the LocalAppData folder path
std::filesystem::path GetDocumentsDirectory() {
    wchar_t localappdata_path[MAX_PATH];
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, localappdata_path))) {
        return std::filesystem::path();
    }
    return std::filesystem::path(localappdata_path);
}

// Get the Reshade directory path (where reshade64.dll/reshade32.dll are located)
std::filesystem::path GetReshadeDirectory() {
    wchar_t localappdata_path[MAX_PATH];
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, localappdata_path))) {
        return std::filesystem::path();
    }
    std::filesystem::path localappdata_dir(localappdata_path);
    return localappdata_dir / L"Programs" / L"Display_Commander" / L"Reshade";
}

// Convert full path to path relative to LocalAppData (masks username)
// Example: "C:\Users\Piotr\AppData\Local\Programs\Display_Commander\Reshade" -> "%localappdata%\\Programs\\Display
// Commander\\Reshade"
std::string GetPathRelativeToDocuments(const std::filesystem::path& full_path) {
    std::filesystem::path localappdata_dir = GetDocumentsDirectory();
    if (localappdata_dir.empty()) {
        return full_path.string();
    }

    // Convert to strings and normalize path separators to backslashes for Windows
    std::string full_str = full_path.string();
    std::string localappdata_str = localappdata_dir.string();
    std::replace(full_str.begin(), full_str.end(), '/', '\\');
    std::replace(localappdata_str.begin(), localappdata_str.end(), '/', '\\');

    // Check if full_path is exactly LocalAppData directory
    if (full_str == localappdata_str) {
        return "%localappdata%";
    }

    // Check if full_path is within LocalAppData directory
    if (full_str.length() > localappdata_str.length()) {
        // Check if it starts with localappdata_str followed by a path separator
        if (full_str.substr(0, localappdata_str.length()) == localappdata_str
            && (full_str[localappdata_str.length()] == '\\')) {
            // Remove the localappdata_dir part and the leading path separator
            std::string relative = full_str.substr(localappdata_str.length() + 1);
            // Prepend "%localappdata%\\" to maintain clarity
            return "%localappdata%\\" + relative;
        }
    }

    // Path is not under LocalAppData, return original
    return full_path.string();
}

// Check if Reshade64.dll exists in LocalAppData folder
bool Reshade64DllExists() {
    std::filesystem::path localappdata_dir = GetDocumentsDirectory();
    if (localappdata_dir.empty()) {
        return false;
    }
    std::filesystem::path reshade64_path =
        localappdata_dir / L"Programs" / L"Display_Commander" / L"Reshade" / L"Reshade64.dll";
    return std::filesystem::exists(reshade64_path);
}

// Check if Reshade32.dll exists in LocalAppData folder
bool Reshade32DllExists() {
    std::filesystem::path localappdata_dir = GetDocumentsDirectory();
    if (localappdata_dir.empty()) {
        return false;
    }
    std::filesystem::path reshade32_path =
        localappdata_dir / L"Programs" / L"Display_Commander" / L"Reshade" / L"Reshade32.dll";
    return std::filesystem::exists(reshade32_path);
}

// Get Reshade64.dll version from LocalAppData folder
std::string GetReshade64Version() {
    std::filesystem::path localappdata_dir = GetDocumentsDirectory();
    if (localappdata_dir.empty()) {
        return "";
    }
    std::filesystem::path reshade64_path =
        localappdata_dir / L"Programs" / L"Display_Commander" / L"Reshade" / L"Reshade64.dll";
    if (!std::filesystem::exists(reshade64_path)) {
        return "";
    }
    return GetDLLVersionString(reshade64_path.wstring());
}

// Get Reshade32.dll version from LocalAppData folder
std::string GetReshade32Version() {
    std::filesystem::path localappdata_dir = GetDocumentsDirectory();
    if (localappdata_dir.empty()) {
        return "";
    }
    std::filesystem::path reshade32_path =
        localappdata_dir / L"Programs" / L"Display_Commander" / L"Reshade" / L"Reshade32.dll";
    if (!std::filesystem::exists(reshade32_path)) {
        return "";
    }
    return GetDLLVersionString(reshade32_path.wstring());
}

// Find all currently loaded ReShade modules by checking for ReShadeRegisterAddon export
std::vector<std::pair<std::string, std::string>> GetLoadedReShadeVersions() {
    std::vector<std::pair<std::string, std::string>> results;  // {module_path, version}

    HMODULE modules[1024];
    DWORD num_modules = 0;
    HANDLE process = GetCurrentProcess();

    if (K32EnumProcessModules(process, modules, sizeof(modules), &num_modules) == 0) {
        return results;
    }

    DWORD module_count = num_modules / sizeof(HMODULE);
    for (DWORD i = 0; i < module_count; i++) {
        // Check if this module has the ReShadeRegisterAddon export (used by addons)
        FARPROC register_func = GetProcAddress(modules[i], "ReShadeRegisterAddon");
        if (register_func == nullptr) {
            continue;
        }

        // This is a ReShade module, get its path and version
        wchar_t module_path[MAX_PATH];
        if (GetModuleFileNameW(modules[i], module_path, MAX_PATH) == 0) {
            continue;
        }

        std::string version = GetDLLVersionString(module_path);
        if (version.empty() || version == "Unknown") {
            version = "Unknown";
        }

        std::string module_path_str;
        int size_needed = WideCharToMultiByte(CP_UTF8, 0, module_path, -1, nullptr, 0, nullptr, nullptr);
        if (size_needed > 0) {
            module_path_str.resize(size_needed - 1);
            WideCharToMultiByte(CP_UTF8, 0, module_path, -1, module_path_str.data(), size_needed, nullptr, nullptr);
        } else {
            module_path_str = "(path unavailable)";
        }

        results.push_back({module_path_str, version});
    }

    return results;
}

// Get enabled addons from DisplayCommander config (whitelist approach)
std::vector<std::string> GetEnabledAddons() {
    std::vector<std::string> enabled_addons;

    // Read EnabledAddons from DisplayCommander config
    display_commander::config::get_config_value("ADDONS", "EnabledAddons", enabled_addons);

    return enabled_addons;
}

// Set enabled addons in DisplayCommander config
void SetEnabledAddons(const std::vector<std::string>& enabled_addons) {
    display_commander::config::set_config_value("ADDONS", "EnabledAddons", enabled_addons);
    display_commander::config::save_config("Addon enabled state changed");
}

// Check if an addon is enabled (whitelist approach - default is disabled)
bool IsAddonEnabled(const std::string& addon_name, const std::string& addon_file) {
    std::vector<std::string> enabled = GetEnabledAddons();

    // Create identifier in format "name@file"
    std::string identifier = addon_name + "@" + addon_file;

    // Check if this addon is in the enabled list
    for (const auto& enabled_entry : enabled) {
        if (enabled_entry == identifier) {
            return true;
        }
    }

    return false;
}

// Enable or disable an addon
void SetAddonEnabled(const std::string& addon_name, const std::string& addon_file, bool enabled) {
    std::vector<std::string> enabled_list = GetEnabledAddons();

    // Create identifier in format "name@file"
    std::string identifier = addon_name + "@" + addon_file;

    // Remove existing entry if present
    enabled_list.erase(std::remove_if(enabled_list.begin(), enabled_list.end(),
                                      [&](const std::string& entry) { return entry == identifier; }),
                       enabled_list.end());

    // Add to enabled list if enabling
    if (enabled) {
        enabled_list.push_back(identifier);
    }

    SetEnabledAddons(enabled_list);
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
            info.is_enabled = IsAddonEnabled(info.name, info.file_name);

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
    // Reset warning flag on initialization (game restart)
    g_show_addon_restart_warning.store(false);
}

void RefreshAddonList() { g_addon_list_dirty.store(true); }

void DrawAddonsTab(display_commander::ui::IImGuiWrapper& imgui) {
    using namespace display_commander::ui::wrapper_flags;

    // Addons Subsection
    if (imgui.CollapsingHeader("Addons", TreeNodeFlags_None)) {
        imgui.Spacing();

        // Check if we need to refresh
        if (g_addon_list_dirty.load()) {
            RefreshAddonListInternal();
            g_addon_list_dirty.store(false);
        }

        // Refresh button
        ui::colors::PushIconColor(&imgui, ui::colors::ICON_ACTION);
        if (imgui.Button(ICON_FK_REFRESH " Refresh")) {
            RefreshAddonList();
        }
        ui::colors::PopIconColor(&imgui);
        if (imgui.IsItemHovered()) {
            imgui.SetTooltip("Refresh the list of available addons");
        }

        imgui.SameLine();

        // Enable All button
        ui::colors::PushIconColor(&imgui, ui::colors::ICON_ACTION);
        if (imgui.Button(ICON_FK_OK " Enable All")) {
            std::vector<std::string> enabled_list;
            for (const auto& addon : g_addon_list) {
                std::string identifier = addon.name + "@" + addon.file_name;
                enabled_list.push_back(identifier);
            }
            SetEnabledAddons(enabled_list);
            // Update local state
            for (auto& addon : g_addon_list) {
                addon.is_enabled = true;
            }
            g_addon_list_dirty.store(true);
            g_show_addon_restart_warning.store(true);
        }
        ui::colors::PopIconColor(&imgui);
        if (imgui.IsItemHovered()) {
            imgui.SetTooltip("Enable all addons");
        }

        imgui.SameLine();

        // Disable All button
        ui::colors::PushIconColor(&imgui, ui::colors::ICON_ACTION);
        if (imgui.Button(ICON_FK_CANCEL " Disable All")) {
            SetEnabledAddons(std::vector<std::string>());
            // Update local state
            for (auto& addon : g_addon_list) {
                addon.is_enabled = false;
            }
            g_addon_list_dirty.store(true);
            g_show_addon_restart_warning.store(true);
        }
        ui::colors::PopIconColor(&imgui);
        if (imgui.IsItemHovered()) {
            imgui.SetTooltip("Disable all addons");
        }

        imgui.SameLine();

        // Open Addons Folder button
        ui::colors::PushIconColor(&imgui, ui::colors::ICON_ACTION);
        if (imgui.Button(ICON_FK_FOLDER_OPEN " Open Addons Folder")) {
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
        ui::colors::PopIconColor(&imgui);
        if (imgui.IsItemHovered()) {
            imgui.SetTooltip("Open the global addons directory in Windows Explorer");
        }

        imgui.Spacing();
        imgui.Separator();
        imgui.Spacing();

        // Display addon list
        if (g_addon_list.empty()) {
            imgui.TextColored(ui::colors::TEXT_DIMMED, "No addons found in global directory.");
            imgui.Spacing();
            imgui.TextWrapped("Addons should be placed in: %s",
                               GetPathRelativeToDocuments(GetGlobalAddonsDirectory()).c_str());
        } else {
            // Create table for addon list
            const int table_flags = TableFlags_Borders | TableFlags_RowBg | TableFlags_Resizable;
            if (imgui.BeginTable("AddonsTable", 4, table_flags)) {
                imgui.TableSetupColumn("Enabled", TableColumnFlags_WidthFixed, 160.0f);
                imgui.TableSetupColumn("Name", TableColumnFlags_WidthStretch);
                imgui.TableSetupColumn("File", TableColumnFlags_WidthFixed, 500.0f);
                imgui.TableSetupColumn("Actions", TableColumnFlags_WidthFixed, 100.0f);
                imgui.TableHeadersRow();

                for (size_t i = 0; i < g_addon_list.size(); ++i) {
                    auto& addon = g_addon_list[i];

                    imgui.TableNextRow();

                    // Enabled checkbox
                    imgui.TableNextColumn();
                    bool enabled = addon.is_enabled;
                    if (imgui.Checkbox(("##Enabled" + std::to_string(i)).c_str(), &enabled)) {
                        SetAddonEnabled(addon.name, addon.file_name, enabled);
                        addon.is_enabled = enabled;
                        g_addon_list_dirty.store(true);
                        g_show_addon_restart_warning.store(true);
                    }
                    if (imgui.IsItemHovered()) {
                        imgui.SetTooltip("%s this addon", enabled ? "Disable" : "Enable");
                    }

                    // Name
                    imgui.TableNextColumn();
                    imgui.Text("%s", addon.name.c_str());
                    if (!addon.description.empty() && imgui.IsItemHovered()) {
                        imgui.SetTooltip("%s", addon.description.c_str());
                    }

                    // File name
                    imgui.TableNextColumn();
                    imgui.TextColored(ui::colors::TEXT_DIMMED, "%s", addon.file_name.c_str());

                    // Actions (Open Folder button)
                    imgui.TableNextColumn();
                    std::string folder_button_id = "Folder##" + std::to_string(i);
                    if (imgui.Button(folder_button_id.c_str())) {
                        std::filesystem::path addon_path(addon.file_path);
                        std::filesystem::path folder_path = addon_path.parent_path();

                        if (std::filesystem::exists(folder_path)) {
                            std::string folder_str = folder_path.string();
                            HINSTANCE result =
                                ShellExecuteA(nullptr, "explore", folder_str.c_str(), nullptr, nullptr, SW_SHOW);

                            if (reinterpret_cast<intptr_t>(result) <= 32) {
                                LogError("Failed to open folder: %s (Error: %ld)", folder_str.c_str(),
                                         reinterpret_cast<intptr_t>(result));
                            } else {
                                LogInfo("Opened folder: %s", folder_str.c_str());
                            }
                        }
                    }
                    if (imgui.IsItemHovered()) {
                        imgui.SetTooltip("Open the folder containing this addon");
                    }
                }

                imgui.EndTable();
            }

            imgui.Spacing();
            imgui.Separator();
            imgui.Spacing();

            // Warning message for addon enable/disable
            if (g_show_addon_restart_warning.load()) {
                imgui.TextColored(ui::colors::TEXT_WARNING,
                                   ICON_FK_WARNING " Warning: Game restart required for addon changes to take effect.");
                imgui.Spacing();
            }

            // Info text
            imgui.TextColored(
                ui::colors::TEXT_DIMMED,
                "Note: Addons are disabled by default. Enable addons to load them on next game restart.");
            imgui.TextColored(ui::colors::TEXT_DIMMED,
                               "Changes to addon enabled/disabled state require a game restart to take effect.");
            imgui.TextColored(ui::colors::TEXT_DIMMED, "Addons directory: %s",
                               GetPathRelativeToDocuments(GetGlobalAddonsDirectory()).c_str());
        }
    }

    imgui.Spacing();

    // Shaders Subsection
    if (imgui.CollapsingHeader("Shaders", TreeNodeFlags_None)) {
        imgui.Spacing();

        // Open Shaders Folder button
        ui::colors::PushIconColor(&imgui, ui::colors::ICON_ACTION);
        if (imgui.Button(ICON_FK_FOLDER_OPEN " Open Shaders Folder")) {
            std::filesystem::path shaders_dir = GetShadersDirectory();

            // Create directory if it doesn't exist
            if (!std::filesystem::exists(shaders_dir)) {
                try {
                    std::filesystem::create_directories(shaders_dir);
                } catch (const std::exception& e) {
                    LogError("Failed to create shaders directory: %s", e.what());
                }
            }

            if (std::filesystem::exists(shaders_dir)) {
                std::string shaders_dir_str = shaders_dir.string();
                HINSTANCE result =
                    ShellExecuteA(nullptr, "explore", shaders_dir_str.c_str(), nullptr, nullptr, SW_SHOW);

                if (reinterpret_cast<intptr_t>(result) <= 32) {
                    LogError("Failed to open shaders folder: %s (Error: %ld)", shaders_dir_str.c_str(),
                             reinterpret_cast<intptr_t>(result));
                } else {
                    LogInfo("Opened shaders folder: %s", shaders_dir_str.c_str());
                }
            }
        }
        ui::colors::PopIconColor(&imgui);
        if (imgui.IsItemHovered()) {
            imgui.SetTooltip("Open the shaders directory in Windows Explorer");
        }

        imgui.SameLine();

        // Open Textures Folder button
        ui::colors::PushIconColor(&imgui, ui::colors::ICON_ACTION);
        if (imgui.Button(ICON_FK_FOLDER_OPEN " Open Textures Folder")) {
            std::filesystem::path textures_dir = GetTexturesDirectory();

            // Create directory if it doesn't exist
            if (!std::filesystem::exists(textures_dir)) {
                try {
                    std::filesystem::create_directories(textures_dir);
                } catch (const std::exception& e) {
                    LogError("Failed to create textures directory: %s", e.what());
                }
            }

            if (std::filesystem::exists(textures_dir)) {
                std::string textures_dir_str = textures_dir.string();
                HINSTANCE result =
                    ShellExecuteA(nullptr, "explore", textures_dir_str.c_str(), nullptr, nullptr, SW_SHOW);

                if (reinterpret_cast<intptr_t>(result) <= 32) {
                    LogError("Failed to open textures folder: %s (Error: %ld)", textures_dir_str.c_str(),
                             reinterpret_cast<intptr_t>(result));
                } else {
                    LogInfo("Opened textures folder: %s", textures_dir_str.c_str());
                }
            }
        }
        ui::colors::PopIconColor(&imgui);
        if (imgui.IsItemHovered()) {
            imgui.SetTooltip("Open the textures directory in Windows Explorer");
        }

        imgui.Spacing();
        imgui.Separator();
        imgui.Spacing();

        // Info text
        imgui.TextColored(ui::colors::TEXT_DIMMED, "Shaders directory: %s",
                           GetPathRelativeToDocuments(GetShadersDirectory()).c_str());
        imgui.TextColored(ui::colors::TEXT_DIMMED, "Textures directory: %s",
                           GetPathRelativeToDocuments(GetTexturesDirectory()).c_str());
    }

    imgui.Spacing();

    // ReShade Config Subsection
    if (imgui.CollapsingHeader("ReShade Config", TreeNodeFlags_None)) {
        imgui.Spacing();

        // Suppress ReShade Clock setting
        bool suppress_clock = settings::g_reshadeTabSettings.suppress_reshade_clock.GetValue();
        if (imgui.Checkbox("Suppress ReShade Clock", &suppress_clock)) {
            settings::g_reshadeTabSettings.suppress_reshade_clock.SetValue(suppress_clock);
        }
        if (imgui.IsItemHovered()) {
            imgui.SetTooltip(
                "When enabled, suppresses ReShade's clock setting by setting ShowClock to 0.\n"
                "When disabled, does nothing (ReShade's clock setting is not modified).");
        }

        imgui.Spacing();
        imgui.Separator();
        imgui.Spacing();

        // Info text
        imgui.TextColored(ui::colors::TEXT_DIMMED,
                           "Note: Changes to ReShade config settings may require a game restart to take effect.");
    }

    // Global ReShade Subsection (only show if ReShade DLL exists in LocalAppData)
    bool reshade64_exists = Reshade64DllExists();
    bool reshade32_exists = Reshade32DllExists();

    if (reshade64_exists || reshade32_exists) {
        imgui.Spacing();

        if (imgui.CollapsingHeader("Global ReShade", TreeNodeFlags_None)) {
            imgui.Spacing();

            // Show status for each DLL with version
            if (reshade64_exists) {
                std::string version = GetReshade64Version();
                if (!version.empty() && version != "Unknown") {
                    imgui.TextColored(ui::colors::TEXT_SUCCESS, ICON_FK_OK " Reshade64.dll found (v%s)",
                                       version.c_str());
                } else {
                    imgui.TextColored(ui::colors::TEXT_SUCCESS, ICON_FK_OK " Reshade64.dll found");
                }
            }
            if (reshade32_exists) {
                std::string version = GetReshade32Version();
                if (!version.empty() && version != "Unknown") {
                    imgui.TextColored(ui::colors::TEXT_SUCCESS, ICON_FK_OK " Reshade32.dll found (v%s)",
                                       version.c_str());
                } else {
                    imgui.TextColored(ui::colors::TEXT_SUCCESS, ICON_FK_OK " Reshade32.dll found");
                }
            }

            // Show currently loaded ReShade versions (found by checking for ReShadeRegisterAddon export)
            std::vector<std::pair<std::string, std::string>> loaded_reshade = GetLoadedReShadeVersions();
            if (!loaded_reshade.empty()) {
                imgui.Spacing();
                imgui.TextColored(ui::colors::TEXT_DEFAULT, "Currently loaded ReShade modules:");
                imgui.Indent();
                for (const auto& [module_path, version] : loaded_reshade) {
                    std::filesystem::path path_obj(module_path);
                    std::string module_name = path_obj.filename().string();
                    imgui.TextColored(ui::colors::TEXT_DEFAULT, ICON_FK_OK " %s (v%s)", module_name.c_str(),
                                       version.c_str());
                    if (imgui.IsItemHovered()) {
                        std::filesystem::path module_path_obj(module_path);
                        imgui.SetTooltip("%s", GetPathRelativeToDocuments(module_path_obj).c_str());
                    }
                }
                imgui.Unindent();
            }

            imgui.Spacing();

            // Open Reshade Folder button
            ui::colors::PushIconColor(&imgui, ui::colors::ICON_ACTION);
            if (imgui.Button(ICON_FK_FOLDER_OPEN " Open Reshade Folder")) {
                std::filesystem::path reshade_dir = GetReshadeDirectory();

                // Create directory if it doesn't exist
                if (!std::filesystem::exists(reshade_dir)) {
                    try {
                        std::filesystem::create_directories(reshade_dir);
                    } catch (const std::exception& e) {
                        LogError("Failed to create Reshade directory: %s", e.what());
                    }
                }

                if (!reshade_dir.empty() && std::filesystem::exists(reshade_dir)) {
                    std::string reshade_dir_str = reshade_dir.string();
                    HINSTANCE result =
                        ShellExecuteA(nullptr, "explore", reshade_dir_str.c_str(), nullptr, nullptr, SW_SHOW);

                    if (reinterpret_cast<intptr_t>(result) <= 32) {
                        LogError("Failed to open Reshade folder: %s (Error: %ld)", reshade_dir_str.c_str(),
                                 reinterpret_cast<intptr_t>(result));
                    } else {
                        LogInfo("Opened Reshade folder: %s", reshade_dir_str.c_str());
                    }
                }
            }
            ui::colors::PopIconColor(&imgui);
            if (imgui.IsItemHovered()) {
                imgui.SetTooltip(
                    "Open the Reshade folder (containing reshade64.dll/reshade32.dll) in Windows Explorer");
            }

            imgui.Spacing();
            imgui.Separator();
            imgui.Spacing();

            // Info text
            imgui.TextColored(ui::colors::TEXT_DIMMED, "Reshade directory: %s",
                               GetPathRelativeToDocuments(GetReshadeDirectory()).c_str());
        }
    }

    imgui.Spacing();

    // Global ReShade Settings Subsection
    if (imgui.CollapsingHeader("Global ReShade Settings", TreeNodeFlags_None)) {
        DrawReShadeGlobalConfigSettings(imgui);
    }
}

void DrawReShadeGlobalConfigSettings(display_commander::ui::IImGuiWrapper& imgui) {
    using namespace display_commander::ui::wrapper_flags;
    imgui.Indent();

    static utils::ReShadeGlobalSettings currentSettings;
    static utils::ReShadeGlobalSettings globalSettings;
    static bool initialLoadDone = false;
    static std::string statusMessage;
    static ImVec4 statusColor = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);

    // Auto-load settings on first run
    if (!initialLoadDone) {
        // Always load current settings
        utils::ReadCurrentReShadeSettings(currentSettings);

        // Try to load global settings (may not exist, which is fine)
        utils::LoadGlobalSettings(globalSettings);

        initialLoadDone = true;
        LogInfo("Auto-loaded ReShade settings for comparison");
    }

    imgui.TextWrapped(
        "Manage global ReShade settings (EffectSearchPaths, TextureSearchPaths, keyboard shortcuts, etc.).");
    imgui.TextWrapped("Copy settings between current game and global profile.");

    imgui.Spacing();
    imgui.Separator();
    imgui.Spacing();

    // Display current ReShade.ini path info
    std::filesystem::path dcConfigPath = utils::GetDisplayCommanderConfigPath();
    std::string dcConfigPathStr = dcConfigPath.string();
    imgui.TextColored(ui::colors::TEXT_DIMMED, "Global profile location:");
    imgui.Indent();
    imgui.TextWrapped("%s", dcConfigPathStr.c_str());
    imgui.Unindent();

    imgui.Spacing();

    // Compare button
    imgui.TextColored(ui::colors::TEXT_DEFAULT, "Configuration comparison:");

    if (imgui.Button("Compare local config vs global config")) {
        // Reload both settings for fresh comparison
        bool currentLoaded = utils::ReadCurrentReShadeSettings(currentSettings);
        bool globalLoaded = utils::LoadGlobalSettings(globalSettings);

        if (currentLoaded && globalLoaded) {
            statusMessage = ICON_FK_OK " Reloaded both configurations for comparison";
            statusColor = ui::colors::TEXT_SUCCESS;
            LogInfo("Reloaded both current and global settings for comparison");
        } else if (currentLoaded) {
            statusMessage = ICON_FK_WARNING " Reloaded current settings, global profile not found";
            statusColor = ui::colors::TEXT_WARNING;
            LogInfo("Reloaded current settings, global profile not found");
        } else if (globalLoaded) {
            statusMessage = ICON_FK_WARNING " Reloaded global profile, current settings failed to load";
            statusColor = ui::colors::TEXT_WARNING;
            LogInfo("Reloaded global settings, current settings failed to load");
        } else {
            statusMessage = ICON_FK_CANCEL " Failed to reload both configurations";
            statusColor = ui::colors::TEXT_ERROR;
            LogInfo("Failed to reload both configurations");
        }
    }
    if (imgui.IsItemHovered()) {
        imgui.SetTooltip(
            "Reload and compare current game's ReShade settings with global profile\n(Useful if you edited either "
            "ReShade.ini or DisplayCommander.toml manually)");
    }

    imgui.Spacing();
    imgui.Separator();
    imgui.Spacing();

    // Unified comparison view (see docs/UI_STYLE_GUIDE.md for depth/indent rules)
    // Depth 1: Nested subsection with indentation and distinct colors
    imgui.Indent();                       // Indent nested header
    ui::colors::PushNestedHeaderColors(&imgui);  // Apply distinct colors for nested header
    if (imgui.CollapsingHeader("Configuration Comparison", TreeNodeFlags_None)) {
        imgui.Indent();  // Indent content inside subsection
        imgui.TextColored(ui::colors::TEXT_DEFAULT,
                           "Shows differences between local (current game) and global configurations:");
        imgui.Spacing();

        bool anyChanges = false;

        // Go through all sections in both settings
        std::set<std::string> allSections;
        for (const auto& [section, _] : currentSettings.additional_settings) {
            allSections.insert(section);
        }
        for (const auto& [section, _] : globalSettings.additional_settings) {
            allSections.insert(section);
        }

        for (const auto& section : allSections) {
            imgui.TextColored(ui::colors::TEXT_LABEL, "[%s]", section.c_str());
            imgui.Indent();

            auto currentSectionIt = currentSettings.additional_settings.find(section);
            auto globalSectionIt = globalSettings.additional_settings.find(section);

            // Get all keys in this section
            std::set<std::string> allKeys;
            if (currentSectionIt != currentSettings.additional_settings.end()) {
                for (const auto& [key, _] : currentSectionIt->second) {
                    allKeys.insert(key);
                }
            }
            if (globalSectionIt != globalSettings.additional_settings.end()) {
                for (const auto& [key, _] : globalSectionIt->second) {
                    allKeys.insert(key);
                }
            }

            bool sectionHasChanges = false;
            for (const auto& key : allKeys) {
                std::string currentValue;
                std::string globalValue;

                if (currentSectionIt != currentSettings.additional_settings.end()) {
                    auto keyIt = currentSectionIt->second.find(key);
                    if (keyIt != currentSectionIt->second.end()) {
                        currentValue = keyIt->second;
                    }
                }

                if (globalSectionIt != globalSettings.additional_settings.end()) {
                    auto keyIt = globalSectionIt->second.find(key);
                    if (keyIt != globalSectionIt->second.end()) {
                        globalValue = keyIt->second;
                    }
                }

                if (currentValue != globalValue) {
                    sectionHasChanges = true;
                    anyChanges = true;
                    imgui.TextColored(ui::colors::TEXT_LABEL, "%s:", key.c_str());
                    imgui.Indent();

                    // Show both values side by side for better comparison
                    imgui.TextColored(ui::colors::TEXT_DIMMED, "Local:  ");
                    imgui.SameLine();
                    if (currentValue.empty()) {
                        imgui.TextColored(ui::colors::TEXT_SUBTLE, "(empty)");
                    } else {
                        imgui.TextColored(ui::colors::TEXT_SUCCESS, "%s", currentValue.c_str());
                    }

                    imgui.TextColored(ui::colors::TEXT_DIMMED, "Global: ");
                    imgui.SameLine();
                    if (globalValue.empty()) {
                        imgui.TextColored(ui::colors::TEXT_SUBTLE, "(empty)");
                    } else {
                        imgui.TextColored(ui::colors::TEXT_WARNING, "%s", globalValue.c_str());
                    }

                    imgui.Unindent();
                }
            }

            if (!sectionHasChanges) {
                imgui.TextColored(ui::colors::TEXT_SUCCESS, "No differences");
            }

            imgui.Unindent();
            imgui.Spacing();
        }

        if (!anyChanges) {
            imgui.TextColored(ui::colors::TEXT_SUCCESS, "All settings are identical!");
        }

        imgui.Spacing();
        imgui.TextColored(ui::colors::TEXT_DIMMED,
                           "Legend: Local = Current game settings, Global = DisplayCommander.ini in user folder");
        imgui.Unindent();  // Unindent content
    }
    ui::colors::PopNestedHeaderColors(&imgui);  // Restore default header colors
    imgui.Unindent();                    // Unindent nested header section

    imgui.Spacing();
    imgui.Separator();
    imgui.Spacing();

    // Action buttons
    imgui.TextColored(ImVec4(1.0f, 1.0f, 0.6f, 1.0f), "Actions:");
    imgui.Spacing();

    // Apply current -> global
    if (imgui.Button("Apply: Current -> Global")) {
        // Refresh current settings before saving
        utils::ReadCurrentReShadeSettings(currentSettings);

        if (utils::SaveGlobalSettings(currentSettings)) {
            statusMessage = ICON_FK_OK " Copied current settings to global profile";
            statusColor = ui::colors::TEXT_SUCCESS;
            LogInfo("Saved current settings to global profile");

            // Reload global settings to reflect changes
            utils::LoadGlobalSettings(globalSettings);
        } else {
            statusMessage = ICON_FK_CANCEL " Failed to save to global profile";
            statusColor = ui::colors::TEXT_ERROR;
            LogInfo("Failed to save to global profile");
        }
    }
    if (imgui.IsItemHovered()) {
        imgui.SetTooltip("Copy current game's ReShade settings to global profile\n(Overwrites DisplayCommander.ini in user folder)");
    }

    imgui.SameLine();

    // Apply global -> current
    if (imgui.Button("Apply: Global -> Current")) {
        // Refresh global settings before applying
        if (utils::LoadGlobalSettings(globalSettings)) {
            if (utils::WriteCurrentReShadeSettings(globalSettings)) {
                statusMessage = ICON_FK_OK " Applied global profile to current game";
                statusColor = ui::colors::TEXT_SUCCESS;
                LogInfo("Applied global settings to current ReShade.ini");

                // Reload current settings to reflect changes
                utils::ReadCurrentReShadeSettings(currentSettings);
            } else {
                statusMessage = ICON_FK_CANCEL " Failed to apply global settings";
                statusColor = ui::colors::TEXT_ERROR;
                LogInfo("Failed to apply global settings");
            }
        } else {
            statusMessage = ICON_FK_CANCEL " No global profile found (create one first)";
            statusColor = ui::colors::TEXT_WARNING;
            LogInfo("No global settings file found");
        }
    }
    if (imgui.IsItemHovered()) {
        imgui.SetTooltip(
            "Apply global profile to current game's ReShade settings\n(Overwrites current game's ReShade.ini)");
    }
    // warn requires pressing reload button on Home page in reshade for settings to be visible
    imgui.TextColored(ImVec4(1.0f, 0.7f, 0.0f, 1.0f),
                       "Warning: Requires pressing 'RELOAD' button on Home page in ReShade for settings to be visible");

    // Status message
    if (!statusMessage.empty()) {
        imgui.Spacing();
        imgui.TextColored(statusColor, "%s", statusMessage.c_str());
    }

    imgui.Spacing();
    imgui.Separator();
    imgui.Spacing();

    // View current settings
    if (imgui.TreeNode("View Current Game Settings")) {
        for (const auto& [section, keys_values] : currentSettings.additional_settings) {
            imgui.TextColored(ImVec4(0.8f, 1.0f, 0.8f, 1.0f), "[%s]", section.c_str());
            if (keys_values.empty()) {
                imgui.Indent();
                imgui.TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "(empty)");
                imgui.Unindent();
            } else {
                for (const auto& [key, value] : keys_values) {
                    imgui.Indent();
                    imgui.TextColored(ImVec4(0.8f, 0.8f, 1.0f, 1.0f), "%s:", key.c_str());
                    imgui.SameLine();
                    imgui.TextWrapped("%s", value.c_str());
                    imgui.Unindent();
                }
            }
            imgui.Spacing();
        }

        imgui.TreePop();
    }

    // View global settings
    if (imgui.TreeNode("View Global Profile")) {
        if (globalSettings.additional_settings.empty()) {
            imgui.TextColored(ImVec4(1.0f, 0.7f, 0.0f, 1.0f),
                               "No global profile found. Create one using 'Apply: Current → Global'.");
        } else {
            for (const auto& [section, keys_values] : globalSettings.additional_settings) {
                imgui.TextColored(ImVec4(0.8f, 1.0f, 0.8f, 1.0f), "[%s]", section.c_str());
                if (keys_values.empty()) {
                    imgui.Indent();
                    imgui.TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "(empty)");
                    imgui.Unindent();
                } else {
                    for (const auto& [key, value] : keys_values) {
                        imgui.Indent();
                        imgui.TextColored(ImVec4(0.8f, 0.8f, 1.0f, 1.0f), "%s:", key.c_str());
                        imgui.SameLine();
                        imgui.TextWrapped("%s", value.c_str());
                        imgui.Unindent();
                    }
                }
                imgui.Spacing();
            }
        }

        imgui.TreePop();
    }

    imgui.Unindent();
}

}  // namespace ui::new_ui
