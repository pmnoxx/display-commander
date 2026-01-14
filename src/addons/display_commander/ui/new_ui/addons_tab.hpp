#pragma once

#include <filesystem>
#include <string>
#include <vector>
#include <atomic>

namespace ui::new_ui {

// Structure to represent an addon
struct AddonInfo {
    std::string name;
    std::string file_path;
    std::string file_name;
    std::string description;
    std::string author;
    bool is_enabled = true;
    bool is_loaded = false;   // Whether it's currently loaded by ReShade
    bool is_external = true;  // Whether it's an external addon (not built-in)
};

// Structure to represent an available shader package
struct ShaderPackageInfo {
    std::string name;
    std::string description;
    std::string download_url;
    std::string repository_url;
    std::string install_path;
    std::string texture_install_path;
    bool required = false;
    bool enabled = false;
    std::vector<std::string> effect_files;
    std::vector<std::string> deny_effect_files;
};

// Initialize addons tab
void InitAddonsTab();

// Draw addons tab
void DrawAddonsTab();

// Refresh the addon list
void RefreshAddonList();

}  // namespace ui::new_ui
