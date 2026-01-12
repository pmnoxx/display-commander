#include "new_ui_tabs.hpp"
#include <winbase.h>
#include <reshade_imgui.hpp>
#include "../../settings/main_tab_settings.hpp"
#include "../../utils/logging.hpp"
#include "../../widgets/remapping_widget/remapping_widget.hpp"
#include "../../widgets/xinput_widget/xinput_widget.hpp"
#include "addons_tab.hpp"
#include "developer_new_tab.hpp"
#include "experimental_tab.hpp"
#include "hook_stats_tab.hpp"
#include "hotkeys_tab.hpp"
#include "main_new_tab.hpp"
#include "streamline_tab.hpp"
#include "swapchain_tab.hpp"
#include "window_info_tab.hpp"

namespace ui::new_ui {

// Global tab manager instance
TabManager g_tab_manager;

// TabManager implementation
TabManager::TabManager() : active_tab_(0) {
    // Initialize with empty vector
    tabs_.store(std::make_shared<const std::vector<Tab>>(std::vector<Tab>{}));
}

void TabManager::AddTab(const std::string& name, const std::string& id,
                        std::function<void(reshade::api::effect_runtime* runtime)> on_draw, bool is_advanced_tab) {
    // Get current tabs atomically
    auto current_tabs = tabs_.load();

    // Create new vector with existing tabs plus the new one
    auto new_tabs = std::make_shared<std::vector<Tab>>(*current_tabs);
    new_tabs->push_back({name, id, on_draw, true, is_advanced_tab});

    // Atomically replace the tabs with const version
    // This ensures thread-safe updates with copy-on-write semantics
    tabs_.store(std::shared_ptr<const std::vector<Tab>>(new_tabs));
}

void TabManager::Draw(reshade::api::effect_runtime* runtime) {
    // Get current tabs atomically
    auto current_tabs = tabs_.load();

    // Safety check for null pointer (should never happen with proper initialization)
    if (!current_tabs || current_tabs->empty()) {
        LogError("No tabs to draw");
        return;
    }

    // Count visible tabs first
    int visible_tab_count = 0;
    int first_visible_tab_index = -1;

    for (size_t i = 0; i < current_tabs->size(); ++i) {
        // Check if tab should be visible
        bool should_show = (*current_tabs)[i].is_visible;

        // Check individual tab settings for advanced tabs
        if ((*current_tabs)[i].is_advanced_tab) {
            bool tab_enabled = false;
            const std::string& tab_id = (*current_tabs)[i].id;

            // Check individual tab setting or fall back to "Show All Tabs"
            if (tab_id == "developer_new") {
                tab_enabled = settings::g_mainTabSettings.show_developer_tab.GetValue();
            } else if (tab_id == "window_info") {
                tab_enabled = settings::g_mainTabSettings.show_window_info_tab.GetValue();
            } else if (tab_id == "swapchain") {
                tab_enabled = settings::g_mainTabSettings.show_swapchain_tab.GetValue();
            } else if (tab_id == "important_info") {
                tab_enabled = settings::g_mainTabSettings.show_important_info_tab.GetValue();
            } else if (tab_id == "xinput") {
                tab_enabled = settings::g_mainTabSettings.show_xinput_tab.GetValue();
            } else if (tab_id == "remapping") {
                tab_enabled = settings::g_mainTabSettings.show_remapping_tab.GetValue();
            } else if (tab_id == "hook_stats") {
                tab_enabled = settings::g_mainTabSettings.show_hook_stats_tab.GetValue();
            } else if (tab_id == "streamline") {
                tab_enabled = settings::g_mainTabSettings.show_streamline_tab.GetValue();
            } else if (tab_id == "experimental") {
                tab_enabled = settings::g_mainTabSettings.show_experimental_tab.GetValue();
            } else if (tab_id == "reshade") {
                tab_enabled = settings::g_mainTabSettings.show_reshade_tab.GetValue();
            }

            // Show tab if individual setting is enabled OR "Show All Tabs" is enabled
            should_show =
                should_show && (settings::g_mainTabSettings.advanced_settings_enabled.GetValue() || tab_enabled);
        }

        if (should_show) {
            visible_tab_count++;
            if (first_visible_tab_index == -1) {
                first_visible_tab_index = static_cast<int>(i);
            }
        }
    }

    // If only one tab is visible, draw it directly without tab bar
    if (visible_tab_count == 1) {
        // Draw the single visible tab content directly
        if (first_visible_tab_index >= 0 && (*current_tabs)[first_visible_tab_index].on_draw) {
            (*current_tabs)[first_visible_tab_index].on_draw(runtime);
        }
        return;
    }

    // Draw tab bar only when multiple tabs are visible
    if (ImGui::BeginTabBar("MainTabs", ImGuiTabBarFlags_None)) {
        for (size_t i = 0; i < current_tabs->size(); ++i) {
            // Check if tab should be visible
            bool should_show = (*current_tabs)[i].is_visible;

            // Check individual tab settings for advanced tabs
            if ((*current_tabs)[i].is_advanced_tab) {
                bool tab_enabled = false;
                const std::string& tab_id = (*current_tabs)[i].id;

                // Check individual tab setting or fall back to "Show All Tabs"
                if (tab_id == "developer_new") {
                    tab_enabled = settings::g_mainTabSettings.show_developer_tab.GetValue();
                } else if (tab_id == "window_info") {
                    tab_enabled = settings::g_mainTabSettings.show_window_info_tab.GetValue();
                } else if (tab_id == "swapchain") {
                    tab_enabled = settings::g_mainTabSettings.show_swapchain_tab.GetValue();
                } else if (tab_id == "important_info") {
                    tab_enabled = settings::g_mainTabSettings.show_important_info_tab.GetValue();
                } else if (tab_id == "xinput") {
                    tab_enabled = settings::g_mainTabSettings.show_xinput_tab.GetValue();
                } else if (tab_id == "remapping") {
                    tab_enabled = settings::g_mainTabSettings.show_remapping_tab.GetValue();
                } else if (tab_id == "hook_stats") {
                    tab_enabled = settings::g_mainTabSettings.show_hook_stats_tab.GetValue();
                } else if (tab_id == "streamline") {
                    tab_enabled = settings::g_mainTabSettings.show_streamline_tab.GetValue();
                } else if (tab_id == "experimental") {
                    tab_enabled = settings::g_mainTabSettings.show_experimental_tab.GetValue();
                } else if (tab_id == "reshade") {
                    tab_enabled = settings::g_mainTabSettings.show_reshade_tab.GetValue();
                }

                // Show tab if individual setting is enabled OR "Show All Tabs" is enabled
                should_show =
                    should_show && (settings::g_mainTabSettings.advanced_settings_enabled.GetValue() || tab_enabled);
            }

            if (!should_show) {
                continue;
            }

            if (ImGui::BeginTabItem((*current_tabs)[i].name.c_str())) {
                active_tab_ = static_cast<int>(i);

                // Draw tab content
                if ((*current_tabs)[i].on_draw) {
                    (*current_tabs)[i].on_draw(runtime);
                }

                ImGui::EndTabItem();
            }
        }
        ImGui::EndTabBar();
    }
}

// Initialize the new UI system
void InitializeNewUI() {
    LogInfo("Initializing new UI");

    // Ensure settings for main and developer tabs are loaded at UI init time
    ui::new_ui::InitMainNewTab();
    ui::new_ui::InitDeveloperNewTab();
    ui::new_ui::InitSwapchainTab();
    ui::new_ui::InitHotkeysTab();
    ui::new_ui::InitAddonsTab();

    // Initialize XInput widget
    display_commander::widgets::xinput_widget::InitializeXInputWidget();

    // Initialize remapping widget
    display_commander::widgets::remapping_widget::InitializeRemappingWidget();

    g_tab_manager.AddTab(
        "Main", "main_new",
        [](reshade::api::effect_runtime* runtime) {
            try {
                ui::new_ui::DrawMainNewTab(runtime);
            } catch (const std::exception& e) {
                LogError("Error drawing main new tab: %s", e.what());
            } catch (...) {
                LogError("Unknown error drawing main new tab");
            }
        },
        false);  // Main tab is not advanced

    g_tab_manager.AddTab(
        "Developer", "developer_new",
        [](reshade::api::effect_runtime* runtime) {
            try {
                ui::new_ui::DrawDeveloperNewTab();
            } catch (const std::exception& e) {
                LogError("Error drawing developer new tab: %s", e.what());
            } catch (...) {
                LogError("Unknown error drawing developer new tab");
            }
        },
        true);  // Developer tab is advanced

    g_tab_manager.AddTab(
        "Hotkeys", "hotkeys",
        [](reshade::api::effect_runtime* runtime) {
            try {
                ui::new_ui::DrawHotkeysTab();
            } catch (const std::exception& e) {
                LogError("Error drawing hotkeys tab: %s", e.what());
            } catch (...) {
                LogError("Unknown error drawing hotkeys tab");
            }
        },
        false);  // Hotkeys tab is not advanced

    g_tab_manager.AddTab(
        "Window Info", "window_info",
        [](reshade::api::effect_runtime* runtime) {
            try {
                ui::new_ui::DrawWindowInfoTab();
            } catch (const std::exception& e) {
                LogError("Error drawing window info tab: %s", e.what());
            } catch (...) {
                LogError("Unknown error drawing window info tab");
            }
        },
        true);  // Window Info tab is not advanced

    g_tab_manager.AddTab(
        "Swapchain", "swapchain",
        [](reshade::api::effect_runtime* runtime) {
            try {
                ui::new_ui::DrawSwapchainTab(runtime);
            } catch (const std::exception& e) {
                LogError("Error drawing swapchain tab: %s", e.what());
            } catch (...) {
                LogError("Unknown error drawing swapchain tab");
            }
        },
        true);  // Swapchain tab is not advanced

    g_tab_manager.AddTab(
        "Important Info", "important_info",
        [](reshade::api::effect_runtime* runtime) {
            try {
                ui::new_ui::DrawImportantInfo();
            } catch (const std::exception& e) {
                LogError("Error drawing important info tab: %s", e.what());
            } catch (...) {
                LogError("Unknown error drawing important info tab");
            }
        },
        true);  // Important Info tab is not advanced

    g_tab_manager.AddTab(
        "XInput", "xinput",
        [](reshade::api::effect_runtime* runtime) {
            try {
                display_commander::widgets::xinput_widget::DrawXInputWidget();
            } catch (const std::exception& e) {
                LogError("Error drawing XInput widget: %s", e.what());
            } catch (...) {
                LogError("Unknown error drawing XInput widget");
            }
        },
        true);  // XInput tab is advanced

    g_tab_manager.AddTab(
        "Controller Remapping", "remapping",
        [](reshade::api::effect_runtime* runtime) {
            try {
                display_commander::widgets::remapping_widget::DrawRemappingWidget();
            } catch (const std::exception& e) {
                LogError("Error drawing remapping widget: %s", e.what());
            } catch (...) {
                LogError("Unknown error drawing remapping widget");
            }
        },
        true);  // Remapping tab is advanced

    g_tab_manager.AddTab(
        "Hook Statistics", "hook_stats",
        [](reshade::api::effect_runtime* runtime) {
            try {
                ui::new_ui::DrawHookStatsTab();
            } catch (const std::exception& e) {
                LogError("Error drawing hook stats tab: %s", e.what());
            } catch (...) {
                LogError("Unknown error drawing hook stats tab");
            }
        },
        true);  // Hook Statistics tab is advanced

    g_tab_manager.AddTab(
        "Streamline", "streamline",
        [](reshade::api::effect_runtime* runtime) {
            try {
                ui::new_ui::DrawStreamlineTab();
            } catch (const std::exception& e) {
                LogError("Error drawing streamline tab: %s", e.what());
            } catch (...) {
                LogError("Unknown error drawing streamline tab");
            }
        },
        true);  // Streamline tab is advanced

    // Add experimental tab conditionally based on advanced settings
    g_tab_manager.AddTab(
        "Experimental", "experimental",
        [](reshade::api::effect_runtime* runtime) {
            try {
                ui::new_ui::DrawExperimentalTab();
            } catch (const std::exception& e) {
                LogError("Error drawing experimental tab: %s", e.what());
            } catch (...) {
                LogError("Unknown error drawing experimental tab");
            }
        },
        true);  // Experimental tab is advanced

    // Add reshade tab
    g_tab_manager.AddTab(
        "ReShade", "reshade",
        [](reshade::api::effect_runtime* runtime) {
            try {
                ui::new_ui::DrawAddonsTab();
            } catch (const std::exception& e) {
                LogError("Error drawing reshade tab: %s", e.what());
            } catch (...) {
                LogError("Unknown error drawing reshade tab");
            }
        },
        true);  // ReShade tab is advanced
}

// Draw the new UI
void DrawNewUI(reshade::api::effect_runtime* runtime) { g_tab_manager.Draw(runtime); }

}  // namespace ui::new_ui
