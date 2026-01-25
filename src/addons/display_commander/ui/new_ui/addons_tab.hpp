#pragma once

#include <atomic>
#include <filesystem>
#include <string>
#include <vector>

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


// Initialize addons tab
void InitAddonsTab();

// Draw addons tab
void DrawAddonsTab();

// Refresh the addon list
void RefreshAddonList();

}  // namespace ui::new_ui
