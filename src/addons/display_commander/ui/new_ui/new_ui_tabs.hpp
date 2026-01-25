#pragma once

#include <imgui.h>
#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <vector>


#include <reshade.hpp>

namespace ui::new_ui {

// Tab structure for the new UI system
struct Tab {
    std::string name;
    std::string id;
    std::function<void(reshade::api::effect_runtime* runtime)> on_draw;
    bool is_visible = true;
    bool is_advanced_tab = false;
};

// Main tab manager class
// Thread-safe implementation using atomic shared_ptr for copy-on-write semantics
class TabManager {
   public:
    TabManager();
    ~TabManager() = default;

    // Add a new tab (thread-safe)
    void AddTab(const std::string& name, const std::string& id,
                std::function<void(reshade::api::effect_runtime* runtime)> on_draw, bool is_advanced_tab = false);

    // Draw the tab bar and content (thread-safe)
    void Draw(reshade::api::effect_runtime* runtime);

    // Get current active tab (thread-safe)
    int GetActiveTab() const { return active_tab_; }

    // Set active tab (thread-safe)
    void SetActiveTab(int tab) { active_tab_ = tab; }

    // Check if a tab with the given ID exists (thread-safe)
    bool HasTab(const std::string& id) const;

   private:
    std::atomic<std::shared_ptr<const std::vector<Tab>>> tabs_;
    int active_tab_ = 0;
};

// Global tab manager instance
extern TabManager g_tab_manager;

// Initialize the new UI system
void InitializeNewUI();

// Draw the new UI
void DrawNewUI(reshade::api::effect_runtime* runtime);

}  // namespace ui::new_ui
