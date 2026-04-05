// Source Code <Display Commander> // follow this order for includes in all files + add this comment at the top
#include "new_ui_tabs.hpp"
#include "../../globals.hpp"
#include "../../modules/module_registry.hpp"
#include "../../settings/main_tab_settings.hpp"
#include "../../ui/imgui_wrapper_reshade.hpp"
#include "../../ui/ui_scale.hpp"
#include "../../utils/detour_call_tracker.hpp"
#include "../../utils/logging.hpp"
#include "advanced_tab.hpp"
#if defined(DISPLAY_COMMANDER_DEBUG_TABS)
#include "debug/dxgi_refresh_rate_tab.hpp"
#include "debug/fps_limiter_debug_tab.hpp"
#if !defined(DC_LITE)
#include "debug/nvidia_profile_inspector_tab.hpp"
#endif
#include "debug/ngx_counters_tab.hpp"
#include "debug/reflex_pclstats_tab.hpp"
#include "debug/vulkan_tab.hpp"
#include "debug/window_messages_tab.hpp"
#endif
#include "hotkeys_tab.hpp"
#include "main_new_tab.hpp"

// Libraries <ReShade> / <imgui>
#include <imgui.h>
#include <reshade_imgui.hpp>

// Libraries <standard C++>
#include <algorithm>
#include <cstdio>

// Libraries <Windows.h> — before other Windows headers
#include <Windows.h>

// Current section of the rendering UI (for crash/stuck reporting). Global namespace to match globals.hpp extern.
std::atomic<const char*> g_rendering_ui_section{nullptr};

namespace ui::new_ui {

namespace {

constexpr int kFontScaledStyleVarCount = 9;

/** Font-relative style scaling + fixed max-width child so every tab shares the same client width. */
struct FontScaledUiLayoutScope {
    display_commander::ui::IImGuiWrapper& gui_;

    explicit FontScaledUiLayoutScope(display_commander::ui::IImGuiWrapper& g) : gui_(g) {
        const float s = display_commander::ui::get_ui_scale(g);
        const ImGuiStyle& st = g.GetStyle();
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(st.ItemSpacing.x * s, st.ItemSpacing.y * s));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(st.FramePadding.x * s, st.FramePadding.y * s));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing,
                            ImVec2(st.ItemInnerSpacing.x * s, st.ItemInnerSpacing.y * s));
        ImGui::PushStyleVar(ImGuiStyleVar_IndentSpacing, st.IndentSpacing * s);
        ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(st.CellPadding.x * s, st.CellPadding.y * s));
        ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, st.ScrollbarSize * s);
        ImGui::PushStyleVar(ImGuiStyleVar_GrabMinSize, st.GrabMinSize * s);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(st.WindowPadding.x * s, st.WindowPadding.y * s));
        ImGui::PushStyleVar(ImGuiStyleVar_TabRounding, st.TabRounding * s);

        const float cap_w = display_commander::ui::scale_px(g, display_commander::ui::kUiMaxContentWidthPx);
        const float avail_x = g.GetContentRegionAvail().x;
        float child_w = cap_w;
        if (avail_x > 0.0f) {
            child_w = (std::min)(cap_w, avail_x);
        }
        gui_.BeginChild("dc_ui_content_cap", ImVec2(child_w, 0.0f), false);
    }

    ~FontScaledUiLayoutScope() {
        gui_.EndChild();
        ImGui::PopStyleVar(kFontScaledStyleVarCount);
    }

    FontScaledUiLayoutScope(const FontScaledUiLayoutScope&) = delete;
    FontScaledUiLayoutScope& operator=(const FontScaledUiLayoutScope&) = delete;
};

}  // namespace

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

void TabManager::Draw(reshade::api::effect_runtime* runtime, display_commander::ui::IImGuiWrapper& gui) {
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
            if (tab_id == "games") {
                tab_enabled = settings::g_mainTabSettings.show_games_tab.GetValue();
            } else if (tab_id == "hotkeys") {
                tab_enabled = settings::g_mainTabSettings.show_hotkeys_tab.GetValue();
            } else if (tab_id == "advanced") {
                tab_enabled = settings::g_mainTabSettings.show_advanced_tab.GetValue();
            } else if (tab_id == "controller") {
                tab_enabled = settings::g_mainTabSettings.show_controller_tab.GetValue();
            } else if (tab_id == "reshade") {
                tab_enabled = settings::g_mainTabSettings.show_reshade_tab.GetValue();
            }

            // Show tab if individual setting is enabled OR "Show All Tabs" is enabled
            should_show =
                should_show && (settings::g_mainTabSettings.advanced_settings_enabled.GetValue() || tab_enabled);
        }
        should_show = should_show && modules::IsModuleTabVisible((*current_tabs)[i].id);

        if (should_show) {
            visible_tab_count++;
            if (first_visible_tab_index == -1) {
                first_visible_tab_index = static_cast<int>(i);
            }
        }
    }

    {
        FontScaledUiLayoutScope scaled_layout(gui);

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
        if (gui.BeginTabBar("MainTabs", 0)) {
            for (size_t i = 0; i < current_tabs->size(); ++i) {
                // Check if tab should be visible
                bool should_show = (*current_tabs)[i].is_visible;

                // Check individual tab settings for advanced tabs
                if ((*current_tabs)[i].is_advanced_tab) {
                    bool tab_enabled = false;
                    const std::string& tab_id = (*current_tabs)[i].id;

                    // Check individual tab setting or fall back to "Show All Tabs"
                    if (tab_id == "games") {
                        tab_enabled = settings::g_mainTabSettings.show_games_tab.GetValue();
                    } else if (tab_id == "hotkeys") {
                        tab_enabled = settings::g_mainTabSettings.show_hotkeys_tab.GetValue();
                    } else if (tab_id == "advanced") {
                        tab_enabled = settings::g_mainTabSettings.show_advanced_tab.GetValue();
                    } else if (tab_id == "controller") {
                        tab_enabled = settings::g_mainTabSettings.show_controller_tab.GetValue();
                    } else if (tab_id == "reshade") {
                        tab_enabled = settings::g_mainTabSettings.show_reshade_tab.GetValue();
                    }

                    // Show tab if individual setting is enabled OR "Show All Tabs" is enabled
                    should_show =
                        should_show && (settings::g_mainTabSettings.advanced_settings_enabled.GetValue() || tab_enabled);
                }
                should_show = should_show && modules::IsModuleTabVisible((*current_tabs)[i].id);

                if (!should_show) {
                    continue;
                }

                if (gui.BeginTabItem((*current_tabs)[i].name.c_str(), nullptr, 0)) {
                    active_tab_ = static_cast<int>(i);

                    // Draw tab content
                    if ((*current_tabs)[i].on_draw) {
                        static thread_local char s_ui_section_buf[64];
                        snprintf(s_ui_section_buf, sizeof(s_ui_section_buf), "ui:tab:%s",
                                 (*current_tabs)[i].id.c_str());
                        g_rendering_ui_section.store(s_ui_section_buf, std::memory_order_release);
                        (*current_tabs)[i].on_draw(runtime);
                    }

                    gui.EndTabItem();
                }
            }
            gui.EndTabBar();
        }
    }
    g_rendering_ui_section.store("ui:draw:done", std::memory_order_release);
}

// Initialize the new UI system
void InitializeNewUI() {
    CALL_GUARD_NO_TS();
    // call guard

    LogInfo("Initializing new UI");

    // Ensure settings for main and advanced tabs are loaded at UI init time
    ui::new_ui::InitMainNewTab();
    ui::new_ui::InitAdvancedTab();
    ui::new_ui::InitHotkeysTab();

    modules::InitializeModuleRegistry();

    g_tab_manager.AddTab(
        "Main", "main_new",
        [](reshade::api::effect_runtime* runtime) {
            try {
                display_commander::ui::ImGuiWrapperReshade wrapper;
                ui::new_ui::DrawMainNewTab(ui::new_ui::GetGraphicsApiFromRuntime(runtime), wrapper, runtime);
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
                display_commander::ui::GraphicsApi api = display_commander::ui::GraphicsApi::Unknown;
                if (runtime != nullptr && runtime->get_device() != nullptr) {
                    api = static_cast<display_commander::ui::GraphicsApi>(
                        static_cast<std::uint32_t>(runtime->get_device()->get_api()));
                }
                display_commander::ui::ImGuiWrapperReshade wrapper;
                ui::new_ui::DrawAdvancedTab(api, wrapper);
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
            (void)runtime;
            try {
                display_commander::ui::ImGuiWrapperReshade wrapper;
                ui::new_ui::DrawHotkeysTab(wrapper);
            } catch (const std::exception& e) {
                LogError("Error drawing hotkeys tab: %s", e.what());
            } catch (...) {
                LogError("Unknown error drawing hotkeys tab");
            }
        },
        true);  // Hotkeys tab: visibility gated by show_hotkeys_tab (default on)

#if defined(DISPLAY_COMMANDER_DEBUG_TABS)
    g_tab_manager.AddTab(
        "Debug Messages", "debug_messages",
        [](reshade::api::effect_runtime* runtime) {
            (void)runtime;
            try {
                display_commander::ui::ImGuiWrapperReshade wrapper;
                ui::new_ui::debug::DrawWindowMessagesTab(wrapper);
            } catch (const std::exception& e) {
                LogError("Error drawing debug messages tab: %s", e.what());
            } catch (...) {
                LogError("Unknown error drawing debug messages tab");
            }
        },
        false);  // Not an advanced-tab gated tab; only compile-time gated.

    g_tab_manager.AddTab(
        "Debug Vulkan", "debug_vulkan",
        [](reshade::api::effect_runtime* runtime) {
            (void)runtime;
            try {
                display_commander::ui::ImGuiWrapperReshade wrapper;
                ui::new_ui::debug::DrawVulkanTab(wrapper);
            } catch (const std::exception& e) {
                LogError("Error drawing debug vulkan tab: %s", e.what());
            } catch (...) {
                LogError("Unknown error drawing debug vulkan tab");
            }
        },
        false);  // Not an advanced-tab gated tab; only compile-time gated.

    g_tab_manager.AddTab(
        "Debug DXGI refresh", "debug_dxgi_refresh",
        [](reshade::api::effect_runtime* runtime) {
            (void)runtime;
            try {
                display_commander::ui::ImGuiWrapperReshade wrapper;
                ui::new_ui::debug::DrawDxgiRefreshRateTab(wrapper);
            } catch (const std::exception& e) {
                LogError("Error drawing debug DXGI refresh tab: %s", e.what());
            } catch (...) {
                LogError("Unknown error drawing debug DXGI refresh tab");
            }
        },
        false);  // Not an advanced-tab gated tab; only compile-time gated.

    g_tab_manager.AddTab(
        "Debug FPS limiter", "debug_fps_limiter_lite",
        [](reshade::api::effect_runtime* runtime) {
            (void)runtime;
            try {
                display_commander::ui::ImGuiWrapperReshade wrapper;
                ui::new_ui::debug::DrawFpsLimiterDebugTab(wrapper);
            } catch (const std::exception& e) {
                LogError("Error drawing debug FPS limiter tab: %s", e.what());
            } catch (...) {
                LogError("Unknown error drawing debug FPS limiter tab");
            }
        },
        false);

    g_tab_manager.AddTab(
        "Debug Reflex / PCLStats", "debug_reflex_pclstats",
        [](reshade::api::effect_runtime* runtime) {
            (void)runtime;
            try {
                display_commander::ui::ImGuiWrapperReshade wrapper;
                ui::new_ui::debug::DrawReflexPclstatsTab(wrapper);
            } catch (const std::exception& e) {
                LogError("Error drawing debug Reflex / PCLStats tab: %s", e.what());
            } catch (...) {
                LogError("Unknown error drawing debug Reflex / PCLStats tab");
            }
        },
        false);

    g_tab_manager.AddTab(
        "Debug NGX", "debug_ngx_counters",
        [](reshade::api::effect_runtime* runtime) {
            (void)runtime;
            try {
                display_commander::ui::ImGuiWrapperReshade wrapper;
                ui::new_ui::debug::DrawNGXCountersTab(wrapper);
            } catch (const std::exception& e) {
                LogError("Error drawing debug NGX tab: %s", e.what());
            } catch (...) {
                LogError("Unknown error drawing debug NGX tab");
            }
        },
        false);

#if !defined(DC_LITE)
    g_tab_manager.AddTab(
        "Debug NVIDIA profile", "debug_nvidia_profile",
        [](reshade::api::effect_runtime* runtime) {
            (void)runtime;
            try {
                display_commander::ui::ImGuiWrapperReshade wrapper;
                ui::new_ui::debug::DrawNvidiaProfileInspectorTab(wrapper);
            } catch (const std::exception& e) {
                LogError("Error drawing debug NVIDIA profile tab: %s", e.what());
            } catch (...) {
                LogError("Unknown error drawing debug NVIDIA profile tab");
            }
        },
        false);
#endif
#endif

    const std::vector<modules::ModuleDescriptor> modules_list = modules::GetModules();
    for (const modules::ModuleDescriptor& module : modules_list) {
        if (!module.has_tab || module.tab_id.empty()) {
            continue;
        }
        if (g_tab_manager.HasTab(module.tab_id)) {
            continue;
        }
        g_tab_manager.AddTab(
            module.tab_name, module.tab_id,
            [module_id = module.id](reshade::api::effect_runtime* runtime) {
                try {
                    display_commander::ui::ImGuiWrapperReshade wrapper;
                    modules::DrawModuleTabById(module_id, wrapper, runtime);
                } catch (const std::exception& e) {
                    LogError("Error drawing module tab '%s': %s", module_id.c_str(), e.what());
                } catch (...) {
                    LogError("Unknown error drawing module tab '%s'", module_id.c_str());
                }
            },
            module.is_advanced_tab);
    }
}

// Draw the new UI
void DrawNewUI(reshade::api::effect_runtime* runtime, display_commander::ui::IImGuiWrapper& gui) {
    g_tab_manager.Draw(runtime, gui);
}

}  // namespace ui::new_ui
