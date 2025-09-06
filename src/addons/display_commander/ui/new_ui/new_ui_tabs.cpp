#include "new_ui_tabs.hpp"
#include "swapchain_tab.hpp"
#include "window_info_tab.hpp"
#include "device_info_tab.hpp"
#include "developer_new_tab.hpp"
#include "main_new_tab.hpp"
#include "experimental_tab.hpp"
#include "../../xinput/xinput_tab.hpp"
#include "../../addon.hpp"
#include "../../utils/timing.hpp"

namespace ui::new_ui {

// Global tab manager instance
TabManager g_tab_manager;

// TabManager implementation
TabManager::TabManager() : active_tab_(0) {
    // Initialize with empty vector
    tabs_.store(std::make_shared<const std::vector<Tab>>(std::vector<Tab>{}));
}

LONGLONG first_draw_ui_ns = 0;

void TabManager::AddTab(const std::string& name, const std::string& id, std::function<void()> on_draw) {
    // Get current tabs atomically
    auto current_tabs = tabs_.load();

    // Create new vector with existing tabs plus the new one
    auto new_tabs = std::make_shared<std::vector<Tab>>(*current_tabs);
    new_tabs->push_back({name, id, on_draw, true});

    // Atomically replace the tabs with const version
    // This ensures thread-safe updates with copy-on-write semantics
    tabs_.store(std::shared_ptr<const std::vector<Tab>>(new_tabs));
}

void TabManager::Draw() {
    // Get current tabs atomically
    auto current_tabs = tabs_.load();

    // Safety check for null pointer (should never happen with proper initialization)
    if (!current_tabs || current_tabs->empty()) {
        return;
    }
    LONGLONG now_ns = utils::get_now_ns();
    if (now_ns < first_draw_ui_ns) {
        return;
    }

    // Draw tab bar
    if (ImGui::BeginTabBar("MainTabs", ImGuiTabBarFlags_None)) {
        for (size_t i = 0; i < current_tabs->size(); ++i) {
            if (!(*current_tabs)[i].is_visible) {
                continue;
            }

            if (ImGui::BeginTabItem((*current_tabs)[i].name.c_str())) {
                active_tab_ = static_cast<int>(i);

                // Draw tab content
                if ((*current_tabs)[i].on_draw) {
                    (*current_tabs)[i].on_draw();
                }

                ImGui::EndTabItem();
            }
        }
        ImGui::EndTabBar();
    }
}

// Initialize the new UI system
void InitializeNewUI(int debug_mode) {
    LogInfo("XXX Initializing new UI");
    if (debug_mode > 0 && debug_mode <= 4) { return; }

    // Ensure settings for main and developer tabs are loaded at UI init time
    ui::new_ui::InitMainNewTab();
    if (debug_mode > 0 && debug_mode <= 5) { return; }

    g_tab_manager.AddTab("Main", "main_new", []() {
        try {
            ui::new_ui::DrawMainNewTab();
        } catch (const std::exception& e) {
            LogError("Error drawing main new tab: %s", e.what());
        } catch (...) {
            LogError("Unknown error drawing main new tab");
        }
    });
    if (debug_mode > 0 && debug_mode <= 5) { return; }


    ui::new_ui::InitDeveloperNewTab();
    g_tab_manager.AddTab("Developer", "developer_new", []() {
        try {
            ui::new_ui::DrawDeveloperNewTab();
        } catch (const std::exception& e) {
            LogError("Error drawing developer new tab: %s", e.what());
        } catch (...) {
            LogError("Unknown error drawing developer new tab");
        }
    });
    if (debug_mode > 0 && debug_mode <= 6) { return; }


    g_tab_manager.AddTab("Device Info", "device_info", []() {
        try {
            ui::new_ui::DrawDeviceInfoTab();
        } catch (const std::exception& e) {
            LogError("Error drawing device info tab: %s", e.what());
        } catch (...) {
            LogError("Unknown error drawing device info tab");
        }
    });
    if (debug_mode > 0 && debug_mode <= 7) { return; }

    g_tab_manager.AddTab("Window Info", "window_info", []() {
        try {
            ui::new_ui::DrawWindowInfoTab();
        } catch (const std::exception& e) {
            LogError("Error drawing window info tab: %s", e.what());
        } catch (...) {
            LogError("Unknown error drawing window info tab");
        }
    });
    if (debug_mode > 0 && debug_mode <= 8) { return; }

    g_tab_manager.AddTab("Swapchain", "swapchain", []() {
        try {
            ui::new_ui::DrawSwapchainTab();
        } catch (const std::exception& e) {
            LogError("Error drawing swapchain tab: %s", e.what());
        } catch (...) {
            LogError("Unknown error drawing swapchain tab");
        }
    });
    if (debug_mode > 0 && debug_mode <= 9) { return; }

    g_tab_manager.AddTab("Important Info", "important_info", []() {
        try {
            ui::new_ui::DrawImportantInfo();
        } catch (const std::exception& e) {
            LogError("Error drawing important info tab: %s", e.what());
        } catch (...) {
            LogError("Unknown error drawing important info tab");
        }
    });
    if (debug_mode > 0 && debug_mode <= 10) { return; }

    // Initialize XInput tab
    display_commander::xinput::InitXInputTab();
    g_tab_manager.AddTab(display_commander::xinput::GetXInputTabName().c_str(),
                        display_commander::xinput::GetXInputTabId().c_str(), []() {
        try {
            display_commander::xinput::DrawXInputTab();
        } catch (const std::exception& e) {
            LogError("Error drawing XInput tab: %s", e.what());
        } catch (...) {
            LogError("Unknown error drawing XInput tab");
        }
    });
    if (debug_mode > 0 && debug_mode <= 10) { return; }

#if EXPERIMENTAL_TAB==1 || EXPERIMENTAL_TAB_PRIVATE==1
    g_tab_manager.AddTab("Experimental", "experimental", []() {
        try {
            ui::new_ui::DrawExperimentalTab();
        } catch (const std::exception& e) {
            LogError("Error drawing experimental tab: %s", e.what());
        } catch (...) {
            LogError("Unknown error drawing experimental tab");
        }
    });
#endif

}

// Draw the new UI
void DrawNewUI() {
    g_tab_manager.Draw();
}

} // namespace ui::new_ui
