#include "new_ui_tabs.hpp"
#include <winbase.h>
#include <reshade_imgui.hpp>
#include "../../globals.hpp"
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
#include "updates_tab.hpp"
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

bool TabManager::HasTab(const std::string& id) const {
    auto current_tabs = tabs_.load();
    if (!current_tabs) {
        return false;
    }
    for (const auto& tab : *current_tabs) {
        if (tab.id == id) {
            return true;
        }
    }
    return false;
}

void TabManager::Draw(reshade::api::effect_runtime* runtime) {
    // Check if Streamline tab should be added dynamically (if DLL loads after initialization)
    // Check if the tab already exists
    auto current_tabs_check = tabs_.load();
    bool tab_exists = false;
    if (current_tabs_check) {
        for (const auto& tab : *current_tabs_check) {
            if (tab.id == "streamline") {
                tab_exists = true;
                break;
            }
        }
    }

    // If tab doesn't exist, check if experimental features are enabled and DLL is loaded, then add it
    if (!tab_exists && enabled_experimental_features && GetModuleHandleW(L"sl.interposer.dll") != nullptr) {
        AddTab(
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
        LogInfo("Streamline tab added dynamically after sl.interposer.dll loaded");
    }

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
            if (tab_id == "advanced") {
                tab_enabled = settings::g_mainTabSettings.show_developer_tab.GetValue();
            } else if (tab_id == "window_info") {
                tab_enabled = settings::g_mainTabSettings.show_window_info_tab.GetValue();
            } else if (tab_id == "swapchain") {
                tab_enabled = settings::g_mainTabSettings.show_swapchain_tab.GetValue();
            } else if (tab_id == "important_info") {
                tab_enabled = settings::g_mainTabSettings.show_important_info_tab.GetValue();
            } else if (tab_id == "controller") {
                tab_enabled = settings::g_mainTabSettings.show_controller_tab.GetValue();
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
                if (tab_id == "advanced") {
                    tab_enabled = settings::g_mainTabSettings.show_developer_tab.GetValue();
                } else if (tab_id == "window_info") {
                    tab_enabled = settings::g_mainTabSettings.show_window_info_tab.GetValue();
                } else if (tab_id == "swapchain") {
                    tab_enabled = settings::g_mainTabSettings.show_swapchain_tab.GetValue();
                } else if (tab_id == "important_info") {
                    tab_enabled = settings::g_mainTabSettings.show_important_info_tab.GetValue();
                } else if (tab_id == "controller") {
                    tab_enabled = settings::g_mainTabSettings.show_controller_tab.GetValue();
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

    // Ensure settings for main and advanced tabs are loaded at UI init time
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
        "Advanced", "advanced",
        [](reshade::api::effect_runtime* runtime) {
            try {
                ui::new_ui::DrawDeveloperNewTab();
            } catch (const std::exception& e) {
                LogError("Error drawing advanced tab: %s", e.what());
            } catch (...) {
                LogError("Unknown error drawing advanced tab");
            }
        },
        true);  // Advanced tab is advanced

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

    if (enabled_experimental_features) {
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
    }

    if (enabled_experimental_features) {
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
    }

    g_tab_manager.AddTab(
        "Controller", "controller",
        [](reshade::api::effect_runtime* runtime) {
            try {
                // Draw XInput widget first
                display_commander::widgets::xinput_widget::DrawXInputWidget();

                ImGui::Spacing();

                // Draw remapping widget below
                display_commander::widgets::remapping_widget::DrawRemappingWidget();
            } catch (const std::exception& e) {
                LogError("Error drawing Controller tab: %s", e.what());
            } catch (...) {
                LogError("Unknown error drawing Controller tab");
            }
        },
        true);  // Controller tab is advanced

    if (enabled_experimental_features) {
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
    }

    // Only add Streamline tab if experimental features are enabled and sl.interposer.dll is loaded
    if (enabled_experimental_features && GetModuleHandleW(L"sl.interposer.dll") != nullptr) {
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
    }

    if (enabled_experimental_features) {
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
    }

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

    // Add updates tab
    if (enabled_experimental_features) {
        g_tab_manager.AddTab(
            "Updates", "updates",
            [](reshade::api::effect_runtime* runtime) {
                try {
                    ui::new_ui::DrawUpdatesTab();
                } catch (const std::exception& e) {
                    LogError("Error drawing updates tab: %s", e.what());
                } catch (...) {
                    LogError("Unknown error drawing updates tab");
                }
            },
            false);  // Updates tab is not advanced (always visible)
    }
}

// Draw the new UI
void DrawNewUI(reshade::api::effect_runtime* runtime) { g_tab_manager.Draw(runtime); }

}  // namespace ui::new_ui
