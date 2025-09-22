#include "addon.hpp"
#include <cstdarg>

// Global state variables
namespace hide_hdr {
    std::atomic<bool> g_enabled{true};
    std::atomic<bool> g_show_ui{true};
    std::atomic<float> g_slider_value{0.5f};
    std::atomic<int> g_selected_option{0};
}

// Logging functions
namespace hide_hdr {
    void LogInfo(const char* format, ...) {
        va_list args;
        va_start(args, format);
        char buffer[1024];
        vsnprintf_s(buffer, sizeof(buffer), _TRUNCATE, format, args);
        va_end(args);
        reshade::log::message(reshade::log::level::info, buffer);
    }

    void LogWarn(const char* format, ...) {
        va_list args;
        va_start(args, format);
        char buffer[1024];
        vsnprintf_s(buffer, sizeof(buffer), _TRUNCATE, format, args);
        va_end(args);
        reshade::log::message(reshade::log::level::warning, buffer);
    }

    void LogError(const char* format, ...) {
        va_list args;
        va_start(args, format);
        char buffer[1024];
        vsnprintf_s(buffer, sizeof(buffer), _TRUNCATE, format, args);
        va_end(args);
        reshade::log::message(reshade::log::level::error, buffer);
    }
}

// Settings management
namespace hide_hdr {
    void LoadSettings() {
        // Load settings from ReShade config
        uint32_t enabled = 1;
        reshade::get_config_value(nullptr, "HIDE_HDR", "Enabled", enabled);
        g_enabled.store(enabled != 0);

        uint32_t show_ui = 1;
        reshade::get_config_value(nullptr, "HIDE_HDR", "ShowUI", show_ui);
        g_show_ui.store(show_ui != 0);

        float slider_value = 0.5f;
        reshade::get_config_value(nullptr, "HIDE_HDR", "SliderValue", slider_value);
        g_slider_value.store(slider_value);

        uint32_t selected_option = 0;
        reshade::get_config_value(nullptr, "HIDE_HDR", "SelectedOption", selected_option);
        g_selected_option.store(selected_option);

        LogInfo("Hide HDR: Settings loaded");
    }

    void SaveSettings() {
        // Save settings to ReShade config
        reshade::set_config_value(nullptr, "HIDE_HDR", "Enabled", g_enabled.load() ? 1u : 0u);
        reshade::set_config_value(nullptr, "HIDE_HDR", "ShowUI", g_show_ui.load() ? 1u : 0u);
        reshade::set_config_value(nullptr, "HIDE_HDR", "SliderValue", g_slider_value.load());
        reshade::set_config_value(nullptr, "HIDE_HDR", "SelectedOption", static_cast<uint32_t>(g_selected_option.load()));

        LogInfo("Hide HDR: Settings saved");
    }
}

// UI Implementation
namespace hide_hdr {
    void DrawMainTab() {
        if (ImGui::BeginTabItem("Main")) {
            ImGui::Text("Welcome to the Hide HDR Addon!");
            ImGui::Separator();

            // Enable/Disable toggle
            bool enabled = g_enabled.load();
            if (ImGui::Checkbox("Enable Addon", &enabled)) {
                g_enabled.store(enabled);
                SaveSettings();
            }

            if (enabled) {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "✓ Active");

                // Slider example
                float slider_value = g_slider_value.load();
                if (ImGui::SliderFloat("Example Slider", &slider_value, 0.0f, 1.0f, "%.3f")) {
                    g_slider_value.store(slider_value);
                    SaveSettings();
                }

                // Dropdown example
                const char* options[] = { "Option 1", "Option 2", "Option 3", "Option 4" };
                int selected = g_selected_option.load();
                if (ImGui::Combo("Example Combo", &selected, options, IM_ARRAYSIZE(options))) {
                    g_selected_option.store(selected);
                    SaveSettings();
                }

                // Button example
                if (ImGui::Button("Example Button")) {
                    LogInfo("Hide HDR: Button clicked! Slider: %.3f, Option: %d",
                           g_slider_value.load(), g_selected_option.load());
                }

                ImGui::SameLine();
                if (ImGui::Button("Reset Settings")) {
                    g_slider_value.store(0.5f);
                    g_selected_option.store(0);
                    SaveSettings();
                    LogInfo("Hide HDR: Settings reset to defaults");
                }
            } else {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "✗ Disabled");
            }

            ImGui::EndTabItem();
        }
    }

    void DrawSettingsTab() {
        if (ImGui::BeginTabItem("Settings")) {
            ImGui::Text("Addon Settings");
            ImGui::Separator();

            // UI visibility toggle
            bool show_ui = g_show_ui.load();
            if (ImGui::Checkbox("Show UI", &show_ui)) {
                g_show_ui.store(show_ui);
                SaveSettings();
            }

            ImGui::Text("Current Values:");
            ImGui::BulletText("Enabled: %s", g_enabled.load() ? "Yes" : "No");
            ImGui::BulletText("Show UI: %s", g_show_ui.load() ? "Yes" : "No");
            ImGui::BulletText("Slider Value: %.3f", g_slider_value.load());
            ImGui::BulletText("Selected Option: %d", g_selected_option.load());

            ImGui::Separator();
            ImGui::Text("Performance Info:");
            ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
            ImGui::Text("Frame Count: %d", ImGui::GetFrameCount());

            ImGui::EndTabItem();
        }
    }

    void DrawAboutTab() {
        if (ImGui::BeginTabItem("About")) {
            ImGui::Text("Hide HDR Addon v1.0.0");
            ImGui::Separator();

            ImGui::Text("This addon hides HDR capabilities from games to:");
            ImGui::BulletText("Prevent HDR mode detection");
            ImGui::BulletText("Force SDR rendering");
            ImGui::BulletText("Override HDR display modes");
            ImGui::BulletText("Compatible with DirectX and Vulkan");

            ImGui::Separator();
            ImGui::Text("Use this addon when games incorrectly detect HDR");
            ImGui::Text("or when you want to force SDR rendering.");

            ImGui::Separator();
            ImGui::Text("ReShade API Version: %s", "Unknown");

            ImGui::EndTabItem();
        }
    }

    void DrawUI() {
        if (!g_show_ui.load()) {
            return;
        }

        if (ImGui::Begin("Hide HDR", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            if (ImGui::BeginTabBar("HideHDRTabs")) {
                DrawMainTab();
                DrawSettingsTab();
                DrawAboutTab();
                ImGui::EndTabBar();
            }
        }
        ImGui::End();
    }
}

// ReShade event handlers
namespace {
    void OnInitEffectRuntime(reshade::api::effect_runtime* runtime) {
        if (runtime == nullptr) {
            return;
        }

        hide_hdr::LogInfo("Hide HDR: Effect runtime initialized");
        hide_hdr::LoadSettings();
    }

    void OnDestroyDevice(reshade::api::device* /*device*/) {
        hide_hdr::LogInfo("Hide HDR: Device destroyed - saving settings");
        hide_hdr::SaveSettings();
    }

    void OnRegisterOverlayHideHDR(reshade::api::effect_runtime* runtime) {
        hide_hdr::DrawUI();
    }
}

// DLL Entry Point
BOOL APIENTRY DllMain(HMODULE h_module, DWORD fdw_reason, LPVOID lpv_reserved) {
    switch (fdw_reason) {
    case DLL_PROCESS_ATTACH: {
        if (!reshade::register_addon(h_module)) {
            return FALSE;
        }

        hide_hdr::LogInfo("Hide HDR: DLL loaded successfully");

        // Register ReShade events
        reshade::register_event<reshade::addon_event::init_effect_runtime>(OnInitEffectRuntime);
        reshade::register_event<reshade::addon_event::destroy_device>(OnDestroyDevice);
        reshade::register_overlay("Hide HDR", OnRegisterOverlayHideHDR);

        break;
    }
    case DLL_PROCESS_DETACH: {
        hide_hdr::LogInfo("Hide HDR: DLL unloaded - saving settings");
        hide_hdr::SaveSettings();

        // Note: reshade::unregister_addon() will automatically unregister all events and overlays
        reshade::unregister_addon(h_module);
        break;
    }
    }

    return TRUE;
}
