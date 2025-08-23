#include "new_ui_tabs.hpp"
#include "swapchain_tab.hpp"
#include "window_info_tab.hpp"
#include "device_info_tab.hpp"
#include "developer_new_tab.hpp"
#include "main_new_tab.hpp"
#include "../../addon.hpp"

namespace renodx::ui::new_ui {

// Global tab manager instance
TabManager g_tab_manager;

// TabManager implementation
TabManager::TabManager() : active_tab_(0) {
}

void TabManager::AddTab(const std::string& name, const std::string& id, std::function<void()> on_draw) {
    tabs_.push_back({name, id, on_draw, true});
}

void TabManager::Draw() {
    if (tabs_.empty()) {
        return;
    }
    
    // Draw tab bar
    if (ImGui::BeginTabBar("MainTabs", ImGuiTabBarFlags_None)) {
        for (size_t i = 0; i < tabs_.size(); ++i) {
            if (!tabs_[i].is_visible) {
                continue;
            }
            
            if (ImGui::BeginTabItem(tabs_[i].name.c_str())) {
                active_tab_ = static_cast<int>(i);
                
                // Draw tab content
                if (tabs_[i].on_draw) {
                    tabs_[i].on_draw();
                }
                
                ImGui::EndTabItem();
            }
        }
        ImGui::EndTabBar();
    }
}

// Initialize the new UI system
void InitializeNewUI() {
    LogInfo("XXX Initializing new UI");
    // Ensure settings for main and developer tabs are loaded at UI init time
    renodx::ui::new_ui::InitMainNewTab();
    renodx::ui::new_ui::InitDeveloperNewTab();

    g_tab_manager.AddTab("Main", "main_new", []() {
        renodx::ui::new_ui::DrawMainNewTab();
    });
    g_tab_manager.AddTab("Developer", "developer_new", []() {
        renodx::ui::new_ui::DrawDeveloperNewTab();
    });
    
    g_tab_manager.AddTab("Device Info", "device_info", []() {
        renodx::ui::new_ui::DrawDeviceInfoTab();
    });
    
    g_tab_manager.AddTab("Window Info", "window_info", []() {
        renodx::ui::new_ui::DrawWindowInfoTab();
    });
    
    g_tab_manager.AddTab("Swapchain", "swapchain", []() {
        renodx::ui::new_ui::DrawSwapchainTab();
    });
    
}

// Draw the new UI
void DrawNewUI() {
    g_tab_manager.Draw();
}

} // namespace renodx::ui::new_ui
