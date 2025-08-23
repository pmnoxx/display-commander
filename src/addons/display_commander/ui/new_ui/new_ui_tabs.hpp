#pragma once

#include <deps/imgui/imgui.h>
#include <functional>
#include <string>
#include <vector>

namespace renodx::ui::new_ui {

// Tab structure for the new UI system
struct Tab {
    std::string name;
    std::string id;
    std::function<void()> on_draw;
    bool is_visible = true;
};

// Main tab manager class
class TabManager {
public:
    TabManager();
    ~TabManager() = default;
    
    // Add a new tab
    void AddTab(const std::string& name, const std::string& id, std::function<void()> on_draw);
    
    // Draw the tab bar and content
    void Draw();
    
    // Get current active tab
    int GetActiveTab() const { return active_tab_; }
    
    // Set active tab
    void SetActiveTab(int tab) { active_tab_ = tab; }

private:
    std::vector<Tab> tabs_;
    int active_tab_ = 0;
};

// Global tab manager instance
extern TabManager g_tab_manager;

// Initialize the new UI system
void InitializeNewUI();

// Draw the new UI
void DrawNewUI();

} // namespace renodx::ui::new_ui
