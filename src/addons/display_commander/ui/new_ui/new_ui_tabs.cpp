#include "new_ui_tabs.hpp"
#include <winbase.h>
#include <cstdio>
#include <reshade_imgui.hpp>
#include "../../globals.hpp"
#include "../../settings/main_tab_settings.hpp"
#include "../../utils/logging.hpp"
#include "../../widgets/remapping_widget/remapping_widget.hpp"
#include "../../widgets/xinput_widget/xinput_widget.hpp"
#include "addons_tab.hpp"
#include "advanced_tab.hpp"
#include "experimental_tab.hpp"
#include "hotkeys_tab.hpp"
#include "main_new_tab.hpp"
#include "performance_tab.hpp"
#include "swapchain_tab.hpp"
#include "vulkan_tab.hpp"

// Current section of the rendering UI (for crash/stuck reporting). Global namespace to match globals.hpp extern.
std::atomic<const char*> g_rendering_ui_section{nullptr};

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
    g_rendering_ui_section.store("ui:draw:entry", std::memory_order_release);

    // Get current tabs atomically
    auto current_tabs = tabs_.load();

    // Safety check for null pointer (should never happen with proper initialization)
    if (!current_tabs || current_tabs->empty()) {
        g_rendering_ui_section.store(nullptr, std::memory_order_release);
        LogError("No tabs to draw");
        return;
    }

    g_rendering_ui_section.store("ui:draw:visible_count", std::memory_order_release);

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
                tab_enabled = settings::g_mainTabSettings.show_advanced_tab.GetValue();
            } else if (tab_id == "controller") {
                tab_enabled = settings::g_mainTabSettings.show_controller_tab.GetValue();
            } else if (tab_id == "experimental") {
                tab_enabled = settings::g_mainTabSettings.show_experimental_tab.GetValue();
            } else if (tab_id == "reshade") {
                tab_enabled = settings::g_mainTabSettings.show_reshade_tab.GetValue();
            } else if (tab_id == "performance") {
                tab_enabled = settings::g_mainTabSettings.show_performance_tab.GetValue();
            } else if (tab_id == "vulkan") {
                tab_enabled = settings::g_mainTabSettings.show_vulkan_tab.GetValue();
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
        if (first_visible_tab_index >= 0 && (*current_tabs)[first_visible_tab_index].on_draw) {
            static thread_local char s_ui_section_buf[64];
            snprintf(s_ui_section_buf, sizeof(s_ui_section_buf), "ui:tab:%s",
                     (*current_tabs)[first_visible_tab_index].id.c_str());
            g_rendering_ui_section.store(s_ui_section_buf, std::memory_order_release);
            (*current_tabs)[first_visible_tab_index].on_draw(runtime);
        }
        g_rendering_ui_section.store("ui:draw:done", std::memory_order_release);
        return;
    }

    g_rendering_ui_section.store("ui:draw:tab_bar", std::memory_order_release);

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
                    tab_enabled = settings::g_mainTabSettings.show_advanced_tab.GetValue();
                } else if (tab_id == "controller") {
                    tab_enabled = settings::g_mainTabSettings.show_controller_tab.GetValue();
                } else if (tab_id == "experimental") {
                    tab_enabled = settings::g_mainTabSettings.show_experimental_tab.GetValue();
                } else if (tab_id == "reshade") {
                    tab_enabled = settings::g_mainTabSettings.show_reshade_tab.GetValue();
                } else if (tab_id == "performance") {
                    tab_enabled = settings::g_mainTabSettings.show_performance_tab.GetValue();
                } else if (tab_id == "vulkan") {
                    tab_enabled = settings::g_mainTabSettings.show_vulkan_tab.GetValue();
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
                    static thread_local char s_ui_section_buf[64];
                    snprintf(s_ui_section_buf, sizeof(s_ui_section_buf), "ui:tab:%s", (*current_tabs)[i].id.c_str());
                    g_rendering_ui_section.store(s_ui_section_buf, std::memory_order_release);
                    (*current_tabs)[i].on_draw(runtime);
                }

                ImGui::EndTabItem();
            }
        }
        ImGui::EndTabBar();
    }
    g_rendering_ui_section.store("ui:draw:done", std::memory_order_release);
}

// Initialize the new UI system
void InitializeNewUI() {
    LogInfo("Initializing new UI");

    // Ensure settings for main and advanced tabs are loaded at UI init time
    ui::new_ui::InitMainNewTab();
    ui::new_ui::InitAdvancedTab();
    ui::new_ui::InitSwapchainTab();
    ui::new_ui::InitHotkeysTab();
    ui::new_ui::InitAddonsTab();
    ui::new_ui::InitVulkanTab();

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
                ui::new_ui::DrawAdvancedTab(runtime);
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

    // Add performance tab conditionally based on advanced settings
    g_tab_manager.AddTab(
        "Performance", "performance",
        [](reshade::api::effect_runtime* runtime) {
            try {
                ui::new_ui::DrawPerformanceTab();
            } catch (const std::exception& e) {
                LogError("Error drawing performance tab: %s", e.what());
            } catch (...) {
                LogError("Unknown error drawing performance tab");
            }
        },
        true);  // Performance tab is advanced

    // Vulkan (experimental) tab - Reflex / frame pacing for Vulkan
    g_tab_manager.AddTab(
        "Vulkan (Experimental)", "vulkan",
        [](reshade::api::effect_runtime* runtime) {
            try {
                ui::new_ui::DrawVulkanTab(runtime);
            } catch (const std::exception& e) {
                LogError("Error drawing Vulkan tab: %s", e.what());
            } catch (...) {
                LogError("Unknown error drawing Vulkan tab");
            }
        },
        true);  // Vulkan tab is advanced

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

    // NVIDIA Profile tab (always visible, not gated by experimental/Debug)
    g_tab_manager.AddTab(
        "NVIDIA Profile", "nvidia_profile",
        [](reshade::api::effect_runtime* runtime) {
            try {
                ui::new_ui::DrawNvidiaProfileTab(runtime);
            } catch (const std::exception& e) {
                LogError("Error drawing NVIDIA Profile tab: %s", e.what());
            } catch (...) {
                LogError("Unknown error drawing NVIDIA Profile tab");
            }
        },
        false);  // Not advanced - visible without "Show Debug Tab"

    // Add Debug tab last (experimental/debug features; id kept as "experimental" for settings)
    if (enabled_experimental_features) {
        g_tab_manager.AddTab(
            "Debug", "experimental",
            [](reshade::api::effect_runtime* runtime) {
                try {
                    ui::new_ui::DrawExperimentalTab(runtime);
                } catch (const std::exception& e) {
                    LogError("Error drawing debug tab: %s", e.what());
                } catch (...) {
                    LogError("Unknown error drawing debug tab");
                }
            },
            true);  // Debug tab is advanced
    }
}

// Draw the new UI
void DrawNewUI(reshade::api::effect_runtime* runtime) { g_tab_manager.Draw(runtime); }

}  // namespace ui::new_ui
