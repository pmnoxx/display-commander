#include "developer_new_tab.hpp"
#include "../../globals.hpp"
#include "../../nvapi/nvapi_fullscreen_prevention.hpp"
#include "../../res/forkawesome.h"
#include "../../settings/developer_tab_settings.hpp"
#include "../../utils.hpp"
#include "../../utils/reshade_global_config.hpp"
#include "settings_wrapper.hpp"

#include <atomic>
#include <iomanip>
#include <set>
#include <sstream>

#include <dxgi1_6.h>
#include <wrl/client.h>

static std::atomic<bool> s_restart_needed_nvapi(false);

namespace ui::new_ui {

void InitDeveloperNewTab() {
    // Ensure settings are loaded
    static bool settings_loaded = false;
    if (!settings_loaded) {
        settings::g_developerTabSettings.LoadAll();
        settings_loaded = true;
    }
}

void DrawDeveloperNewTab() {
    // Developer Settings Section
    if (ImGui::CollapsingHeader("Developer Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
        DrawDeveloperSettings();
    }

    ImGui::Spacing();

    // HDR and Display Settings Section
    if (ImGui::CollapsingHeader("HDR and Display Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
        DrawHdrDisplaySettings();
    }

    ImGui::Spacing();

    // NVAPI Settings Section
    if (ImGui::CollapsingHeader("NVAPI Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
        DrawNvapiSettings();
    }

    ImGui::Spacing();

    // Keyboard Shortcuts Section

    if (ImGui::CollapsingHeader("Keyboard Shortcuts", ImGuiTreeNodeFlags_DefaultOpen)) {
        DrawKeyboardShortcutsSettings();
    }

    ImGui::Spacing();

    // ReShade Global Config Section
    if (ImGui::CollapsingHeader("ReShade Global Config", ImGuiTreeNodeFlags_DefaultOpen)) {
        DrawReShadeGlobalConfigSettings();
    }

    ImGui::Spacing();
    ImGui::Separator();
}

void DrawDeveloperSettings() {
    // Prevent Fullscreen
    CheckboxSetting(settings::g_developerTabSettings.prevent_fullscreen, "Prevent Fullscreen");
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Prevent exclusive fullscreen; keep borderless/windowed for stability and HDR.");
    }
    if (CheckboxSetting(settings::g_developerTabSettings.prevent_always_on_top, "Prevent Always On Top")) {
        s_prevent_always_on_top.store(settings::g_developerTabSettings.prevent_always_on_top.GetValue());
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Prevents windows from becoming always on top, even if they are moved or resized.");
    }


    // Continue Rendering
    if (CheckboxSetting(settings::g_developerTabSettings.continue_rendering, "Continue Rendering in Background")) {
        s_continue_rendering.store(settings::g_developerTabSettings.continue_rendering.GetValue());
        LogInfo("Continue rendering in background %s",
                settings::g_developerTabSettings.continue_rendering.GetValue() ? "enabled" : "disabled");
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            "Prevent games from pausing or reducing performance when alt-tabbed. Blocks window focus "
            "messages to keep games running in background.");
    }

    ImGui::Spacing();
}

void DrawHdrDisplaySettings() {
    // Hide HDR Capabilities
    if (CheckboxSetting(settings::g_developerTabSettings.hide_hdr_capabilities,
                        "Hide game's native HDR")) {
        s_hide_hdr_capabilities.store(settings::g_developerTabSettings.hide_hdr_capabilities.GetValue());
        LogInfo("HDR hiding setting changed to: %s",
                settings::g_developerTabSettings.hide_hdr_capabilities.GetValue() ? "true" : "false");
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Hides HDR capabilities from applications by intercepting CheckColorSpaceSupport and GetDesc calls.\n"
                         "This can prevent games from detecting HDR support and force them to use SDR mode.");
    }

    // Enable Flip Chain
    if (CheckboxSetting(settings::g_developerTabSettings.enable_flip_chain,
                        "Enable flip chain")) {
        s_enable_flip_chain.store(settings::g_developerTabSettings.enable_flip_chain.GetValue());
        LogInfo("Enable flip chain setting changed to: %s",
                settings::g_developerTabSettings.enable_flip_chain.GetValue() ? "true" : "false");
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Forces games to use flip model swap chains (FLIP_DISCARD) for better performance.\n"
                         "This setting requires a game restart to take effect.\n"
                         "Only works with DirectX 10/11/12 (DXGI) games.");
    }

    // Auto Color Space checkbox
    bool auto_colorspace = settings::g_developerTabSettings.auto_colorspace.GetValue();
    if (ImGui::Checkbox("Auto color space", &auto_colorspace)) {
        settings::g_developerTabSettings.auto_colorspace.SetValue(auto_colorspace);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Automatically sets the appropriate color space on the game's swap chain based on the current format.\n"
                         "• HDR10 format (R10G10B10A2) → HDR10 color space (ST2084)\n"
                         "• FP16 format (R16G16B16A16) → scRGB color space (Linear)\n"
                         "• SDR format (R8G8B8A8) → sRGB color space (Non-linear)\n"
                         "Only works with DirectX 11/12 games.\n"
                         "Applied automatically in presentBefore.");
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // D3D9 to D3D9Ex Upgrade
    ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "Direct3D 9 Settings");

    if (CheckboxSetting(settings::g_developerTabSettings.enable_d3d9_upgrade,
                        "Enable D3D9 to D3D9Ex Upgrade")) {
        s_enable_d3d9_upgrade.store(settings::g_developerTabSettings.enable_d3d9_upgrade.GetValue());
        LogInfo("D3D9 to D3D9Ex upgrade setting changed to: %s",
                settings::g_developerTabSettings.enable_d3d9_upgrade.GetValue() ? "enabled" : "disabled");
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Automatically upgrades Direct3D 9 to Direct3D 9Ex for improved performance and features.\n"
                         "D3D9Ex provides better memory management, flip model presentation, and reduced latency.\n"
                         "This feature is enabled by default and works transparently with D3D9 games.");
    }

    // Show upgrade status
    if (s_d3d9_upgrade_successful.load()) {
        ImGui::Indent();
        ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), ICON_FK_OK " D3D9 upgraded to D3D9Ex successfully");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Direct3D 9 was successfully upgraded to Direct3D 9Ex.\n"
                             "Your game is now using the enhanced D3D9Ex API.");
        }
        ImGui::Unindent();
    } else if (settings::g_developerTabSettings.enable_d3d9_upgrade.GetValue()) {
        ImGui::Indent();
        ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "Waiting for D3D9 device creation...");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("The upgrade will occur when the game creates a Direct3D 9 device.\n"
                             "If the game is not using D3D9, this setting has no effect.");
        }
        ImGui::Unindent();
    }
}

void DrawNvapiSettings() {
    // HDR10 Colorspace Fix
    if (CheckboxSetting(settings::g_developerTabSettings.nvapi_fix_hdr10_colorspace,
                        "Set ReShade Effects Processing to HDR10 Colorspace")) {
        s_nvapi_fix_hdr10_colorspace.store(settings::g_developerTabSettings.nvapi_fix_hdr10_colorspace.GetValue());
        s_restart_needed_nvapi.store(true);
    }

    // NVAPI Fullscreen Prevention
    if (CheckboxSetting(settings::g_developerTabSettings.nvapi_fullscreen_prevention, "NVAPI Fullscreen Prevention")) {
        s_nvapi_fullscreen_prevention.store(settings::g_developerTabSettings.nvapi_fullscreen_prevention.GetValue());
        s_restart_needed_nvapi.store(true);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Use NVAPI to prevent fullscreen mode at the driver level.");
    }

    ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "NVAPI Auto-enable for Games");
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Automatically enable NVAPI features for specific games:\n"
                         "• NVAPI Fullscreen Prevention\n"
                         "• HDR10 Colorspace Fix\n\n"
                         "Note: DLDSR needs to be off for proper functionality\n\n"
                         "Supported games:\n"
                         "• Armored Core 6\n"
                         "• Devil May Cry 5\n"
                         "• Elden Ring\n"
                         "• Hitman\n"
                         "• Resident Evil 2\n"
                         "• Resident Evil 3\n"
                         "• Resident Evil 7\n"
                         "• Resident Evil 8\n"
                         "• Sekiro: Shadows Die Twice");
    }
    // Display restart-required notice if flagged
    if (s_restart_needed_nvapi.load()) {
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Game restart required to apply NVAPI changes.");
    }
    if (::g_nvapiFullscreenPrevention.IsAvailable()) {
        // Library loaded successfully
        ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "✁ENVAPI Library: Loaded");
    } else {
        // Library not loaded
        ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "✁ENVAPI Library: Not Loaded");
    }

    // Minimal NVIDIA Reflex Controls (device runtime dependent)
    if (ImGui::CollapsingHeader("NVIDIA Reflex (Minimal)", ImGuiTreeNodeFlags_DefaultOpen)) {

        // Native Reflex Status Indicator
        bool is_native_reflex_active = g_swapchain_event_counters[SWAPCHAIN_EVENT_NVAPI_D3D_SET_SLEEP_MODE].load() > 0;
        if (is_native_reflex_active) {
            ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), ICON_FK_OK " Native Reflex: ACTIVE");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(
                    "The game has native Reflex support and is actively using it. "
                    "Do not enable addon Reflex features to avoid conflicts.");
            }
        } else {
            ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), ICON_FK_MINUS " Native Reflex: INACTIVE");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(
                    "No native Reflex activity detected. "
                    "The game may not have Reflex support or it is disabled.");
            }
        }
        ImGui::Spacing();

        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Enabling Reflex when the game already has it can cause conflicts, instability, or "
                "performance issues. Check the game's graphics settings first.");
        }

        bool reflex_auto_configure = settings::g_developerTabSettings.reflex_auto_configure.GetValue();
        bool reflex_enable = settings::g_developerTabSettings.reflex_enable.GetValue();

        bool reflex_low_latency = settings::g_developerTabSettings.reflex_low_latency.GetValue();
        bool reflex_boost = settings::g_developerTabSettings.reflex_boost.GetValue();
        bool reflex_markers = settings::g_developerTabSettings.reflex_use_markers.GetValue();
        bool reflex_enable_sleep = settings::g_developerTabSettings.reflex_enable_sleep.GetValue();

        if (ImGui::Checkbox("Auto Configure Reflex", &reflex_auto_configure)) {
            settings::g_developerTabSettings.reflex_auto_configure.SetValue(reflex_auto_configure);
            s_reflex_auto_configure.store(reflex_auto_configure);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Automatically configure Reflex settings on startup");
        }
        if (reflex_auto_configure) {
            ImGui::BeginDisabled();
            ImGui::Text("Auto-configure is handled by continuous monitoring");
            ImGui::EndDisabled();
        }

        if (ImGui::Checkbox("Enable Reflex", &reflex_enable)) {
            settings::g_developerTabSettings.reflex_enable.SetValue(reflex_enable);
            s_reflex_enable.store(reflex_enable);
        }
        if (ImGui::Checkbox("Low Latency Mode", &reflex_low_latency)) {
            settings::g_developerTabSettings.reflex_low_latency.SetValue(reflex_low_latency);
            s_reflex_low_latency.store(reflex_low_latency);
        }
        if (ImGui::Checkbox("Low Latency Boost", &reflex_boost)) {
            settings::g_developerTabSettings.reflex_boost.SetValue(reflex_boost);
            s_reflex_boost.store(reflex_boost);
        }
        if (ImGui::Checkbox("Use Markers to Optimize", &reflex_markers)) {
            settings::g_developerTabSettings.reflex_use_markers.SetValue(reflex_markers);
            s_reflex_use_markers.store(reflex_markers);
        }
        // Warning about enabling Reflex when game already has it
        if (is_native_reflex_active && settings::g_developerTabSettings.reflex_use_markers.GetValue()) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.0f, 1.0f),
                            ICON_FK_WARNING " Warning: Do not enable 'Use Markers to Optimize' if the game already has built-in Reflex support!");
        }

        if (ImGui::Checkbox("Enable Reflex Sleep Mode", &reflex_enable_sleep)) {
            settings::g_developerTabSettings.reflex_enable_sleep.SetValue(reflex_enable_sleep);
            s_reflex_enable_sleep.store(reflex_enable_sleep);
        }
        if (is_native_reflex_active && settings::g_developerTabSettings.reflex_enable_sleep.GetValue()) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.0f, 1.0f),
                            ICON_FK_WARNING " Warning: Do not enable 'Enable Reflex Sleep Mode' if the game already has built-in Reflex support!");
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Enable Reflex sleep mode calls (disabled by default for safety).");
        }
        bool reflex_logging = settings::g_developerTabSettings.reflex_logging.GetValue();
        if (ImGui::Checkbox("Enable Reflex Logging", &reflex_logging)) {
            settings::g_developerTabSettings.reflex_logging.SetValue(reflex_logging);
            s_enable_reflex_logging.store(reflex_logging);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(                "Enable detailed logging of Reflex marker operations for debugging purposes.");
        }
        if (reflex_auto_configure) {
            ImGui::EndDisabled();
        }
    }
}

void DrawKeyboardShortcutsSettings() {
    // Enable Mute/Unmute Shortcut (Ctrl+M)
    if (CheckboxSetting(settings::g_developerTabSettings.enable_mute_unmute_shortcut,
                        "Enable Mute/Unmute Shortcut (Ctrl+M)")) {
        ::s_enable_mute_unmute_shortcut.store(settings::g_developerTabSettings.enable_mute_unmute_shortcut.GetValue());
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            "Enable keyboard shortcut Ctrl+M to quickly mute/unmute audio. Only works when the game is "
            "in the foreground.");
    }

    // Info text for Ctrl+M
    if (settings::g_developerTabSettings.enable_mute_unmute_shortcut.GetValue()) {
        ImGui::Indent();
        ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "Press Ctrl+M to toggle audio mute state");
        ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "Shortcut works when game is in foreground");
        ImGui::Unindent();
    }

    // Enable Background Toggle Shortcut (Ctrl+R)
    if (CheckboxSetting(settings::g_developerTabSettings.enable_background_toggle_shortcut,
                        "Enable Background Toggle Shortcut (Ctrl+R)")) {
        ::s_enable_background_toggle_shortcut.store(
            settings::g_developerTabSettings.enable_background_toggle_shortcut.GetValue());
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            "Enable keyboard shortcut Ctrl+R to quickly toggle both 'No Render in Background' and 'No "
            "Present in Background' settings. Only works when the game is in the foreground.");
    }

    // Info text for Ctrl+R
    if (settings::g_developerTabSettings.enable_background_toggle_shortcut.GetValue()) {
        ImGui::Indent();
        ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f),
                           "Press Ctrl+R to toggle background rendering/present settings");
        ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "Shortcut works when game is in foreground");
        ImGui::Unindent();
    }

    // Enable Time Slowdown Shortcut (Ctrl+T)
    if (CheckboxSetting(settings::g_developerTabSettings.enable_timeslowdown_shortcut,
                        "Enable Time Slowdown Shortcut (Ctrl+T)")) {
        ::s_enable_timeslowdown_shortcut.store(settings::g_developerTabSettings.enable_timeslowdown_shortcut.GetValue());
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            "Enable keyboard shortcut Ctrl+T to quickly toggle Time Slowdown. Only works when the game is "
            "in the foreground.");
    }

    // Info text for Ctrl+T
    if (settings::g_developerTabSettings.enable_timeslowdown_shortcut.GetValue()) {
        ImGui::Indent();
        ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "Press Ctrl+T to toggle Time Slowdown");
        ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "Shortcut works when game is in foreground");
        ImGui::Unindent();
    }

    // Enable ADHD Toggle Shortcut (Ctrl+D)
    if (CheckboxSetting(settings::g_developerTabSettings.enable_adhd_toggle_shortcut,
                        "Enable ADHD Toggle Shortcut (Ctrl+D)")) {
        ::s_enable_adhd_toggle_shortcut.store(settings::g_developerTabSettings.enable_adhd_toggle_shortcut.GetValue());
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            "Enable keyboard shortcut Ctrl+D to quickly toggle ADHD Multi-Monitor Mode. Only works when the game is "
            "in the foreground.");
    }

    // Info text for Ctrl+D
    if (settings::g_developerTabSettings.enable_adhd_toggle_shortcut.GetValue()) {
        ImGui::Indent();
        ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "Press Ctrl+D to toggle ADHD Multi-Monitor Mode");
        ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "Shortcut works when game is in foreground");
        ImGui::Unindent();
    }

    // Enable Auto-Click Shortcut (Ctrl+P)
    if (CheckboxSetting(settings::g_developerTabSettings.enable_autoclick_shortcut,
                        "Enable Auto-Click Shortcut (Ctrl+P)")) {
        ::s_enable_autoclick_shortcut.store(settings::g_developerTabSettings.enable_autoclick_shortcut.GetValue());
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            "Enable keyboard shortcut Ctrl+P to quickly toggle Auto-Click sequences. Only works when the game is "
            "in the foreground.");
    }

    // Info text for Ctrl+P
    if (settings::g_developerTabSettings.enable_autoclick_shortcut.GetValue()) {
        ImGui::Indent();
        ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "Press Ctrl+P to toggle Auto-Click sequences");
        ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "Shortcut works when game is in foreground");
        ImGui::Unindent();
    }
}

void DrawReShadeGlobalConfigSettings() {
    static utils::ReShadeGlobalSettings currentSettings;
    static utils::ReShadeGlobalSettings globalSettings;
    static bool initialLoadDone = false;
    static std::string statusMessage;
    static ImVec4 statusColor = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);

    // Auto-load settings on first run
    if (!initialLoadDone) {
        // Always load current settings
        utils::ReadCurrentReShadeSettings(currentSettings);

        // Try to load global settings (may not exist, which is fine)
        utils::LoadGlobalSettings(globalSettings);

        initialLoadDone = true;
        LogInfo("Auto-loaded ReShade settings for comparison");
    }

    ImGui::TextWrapped("Manage global ReShade settings (EffectSearchPaths, TextureSearchPaths, keyboard shortcuts, etc.).");
    ImGui::TextWrapped("Copy settings between current game and global profile.");

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Display current ReShade.ini path info
    std::filesystem::path dcConfigPath = utils::GetDisplayCommanderConfigPath();
    std::string dcConfigPathStr = dcConfigPath.string();
    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Global profile location:");
    ImGui::Indent();
    ImGui::TextWrapped("%s", dcConfigPathStr.c_str());
    ImGui::Unindent();

    ImGui::Spacing();

    // Reload buttons
    ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "Refresh settings:");

    if (ImGui::Button("Reload Current Settings")) {
        if (utils::ReadCurrentReShadeSettings(currentSettings)) {
            statusMessage = ICON_FK_OK " Reloaded current game settings";
            statusColor = ImVec4(0.4f, 1.0f, 0.4f, 1.0f);
            LogInfo("Reloaded current ReShade settings");
        } else {
            statusMessage = ICON_FK_CANCEL " Failed to reload current settings";
            statusColor = ImVec4(1.0f, 0.4f, 0.4f, 1.0f);
            LogInfo("Failed to reload current settings");
        }
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Reload current game's settings from ReShade.ini\n(Useful if you edited ReShade.ini manually)");
    }

    ImGui::SameLine();

    if (ImGui::Button("Reload Global Settings")) {
        if (utils::LoadGlobalSettings(globalSettings)) {
            statusMessage = ICON_FK_OK " Reloaded global profile";
            statusColor = ImVec4(0.4f, 1.0f, 0.4f, 1.0f);
            LogInfo("Reloaded global settings from DisplayCommander.ini");
        } else {
            statusMessage = ICON_FK_CANCEL " Failed to reload global profile (file may not exist)";
            statusColor = ImVec4(1.0f, 0.7f, 0.0f, 1.0f);
            LogInfo("Failed to reload global settings");
        }
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Reload global profile from DisplayCommander.ini\n(Useful if you edited DisplayCommander.ini manually)");
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Preview current -> global
    if (ImGui::CollapsingHeader("Preview: Current → Global", ImGuiTreeNodeFlags_None)) {
        ImGui::TextWrapped("Shows what would change in the global profile if you copy current settings:");
        ImGui::Spacing();

        bool anyChanges = false;

        // Go through all sections in both settings
        std::set<std::string> allSections;
        for (const auto& [section, _] : currentSettings.additional_settings) {
            allSections.insert(section);
        }
        for (const auto& [section, _] : globalSettings.additional_settings) {
            allSections.insert(section);
        }

        for (const auto& section : allSections) {
            ImGui::TextColored(ImVec4(0.8f, 1.0f, 0.8f, 1.0f), "[%s]", section.c_str());
            ImGui::Indent();

            auto currentSectionIt = currentSettings.additional_settings.find(section);
            auto globalSectionIt = globalSettings.additional_settings.find(section);

            // Get all keys in this section
            std::set<std::string> allKeys;
            if (currentSectionIt != currentSettings.additional_settings.end()) {
                for (const auto& [key, _] : currentSectionIt->second) {
                    allKeys.insert(key);
                }
            }
            if (globalSectionIt != globalSettings.additional_settings.end()) {
                for (const auto& [key, _] : globalSectionIt->second) {
                    allKeys.insert(key);
                }
            }

            bool sectionHasChanges = false;
            for (const auto& key : allKeys) {
                std::string currentValue;
                std::string globalValue;

                if (currentSectionIt != currentSettings.additional_settings.end()) {
                    auto keyIt = currentSectionIt->second.find(key);
                    if (keyIt != currentSectionIt->second.end()) {
                        currentValue = keyIt->second;
                    }
                }

                if (globalSectionIt != globalSettings.additional_settings.end()) {
                    auto keyIt = globalSectionIt->second.find(key);
                    if (keyIt != globalSectionIt->second.end()) {
                        globalValue = keyIt->second;
                    }
                }

                if (currentValue != globalValue) {
                    sectionHasChanges = true;
                    anyChanges = true;
                    ImGui::TextColored(ImVec4(0.8f, 0.8f, 1.0f, 1.0f), "%s:", key.c_str());
                    ImGui::Indent();
                    if (!globalValue.empty()) {
                        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "- %s", globalValue.c_str());
                    }
                    if (!currentValue.empty()) {
                        ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "+ %s", currentValue.c_str());
                    }
                    ImGui::Unindent();
                }
            }

            if (!sectionHasChanges) {
                ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "No changes");
            }

            ImGui::Unindent();
            ImGui::Spacing();
        }

        if (!anyChanges) {
            ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "All settings are identical!");
        }
    }

    ImGui::Spacing();

    // Preview global -> current
    if (ImGui::CollapsingHeader("Preview: Global → Current", ImGuiTreeNodeFlags_None)) {
        ImGui::TextWrapped("Shows what would change in current game settings if you apply global profile:");
        ImGui::Spacing();

        bool anyChanges = false;

        // Go through all sections in both settings
        std::set<std::string> allSections;
        for (const auto& [section, _] : currentSettings.additional_settings) {
            allSections.insert(section);
        }
        for (const auto& [section, _] : globalSettings.additional_settings) {
            allSections.insert(section);
        }

        for (const auto& section : allSections) {
            ImGui::TextColored(ImVec4(0.8f, 1.0f, 0.8f, 1.0f), "[%s]", section.c_str());
            ImGui::Indent();

            auto currentSectionIt = currentSettings.additional_settings.find(section);
            auto globalSectionIt = globalSettings.additional_settings.find(section);

            // Get all keys in this section
            std::set<std::string> allKeys;
            if (currentSectionIt != currentSettings.additional_settings.end()) {
                for (const auto& [key, _] : currentSectionIt->second) {
                    allKeys.insert(key);
                }
            }
            if (globalSectionIt != globalSettings.additional_settings.end()) {
                for (const auto& [key, _] : globalSectionIt->second) {
                    allKeys.insert(key);
                }
            }

            bool sectionHasChanges = false;
            for (const auto& key : allKeys) {
                std::string currentValue;
                std::string globalValue;

                if (currentSectionIt != currentSettings.additional_settings.end()) {
                    auto keyIt = currentSectionIt->second.find(key);
                    if (keyIt != currentSectionIt->second.end()) {
                        currentValue = keyIt->second;
                    }
                }

                if (globalSectionIt != globalSettings.additional_settings.end()) {
                    auto keyIt = globalSectionIt->second.find(key);
                    if (keyIt != globalSectionIt->second.end()) {
                        globalValue = keyIt->second;
                    }
                }

                if (currentValue != globalValue) {
                    sectionHasChanges = true;
                    anyChanges = true;
                    ImGui::TextColored(ImVec4(0.8f, 0.8f, 1.0f, 1.0f), "%s:", key.c_str());
                    ImGui::Indent();
                    if (!currentValue.empty()) {
                        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "- %s", currentValue.c_str());
                    }
                    if (!globalValue.empty()) {
                        ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "+ %s", globalValue.c_str());
                    }
                    ImGui::Unindent();
                }
            }

            if (!sectionHasChanges) {
                ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "No changes");
            }

            ImGui::Unindent();
            ImGui::Spacing();
        }

        if (!anyChanges) {
            ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "All settings are identical!");
        }
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Action buttons
    ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.6f, 1.0f), "Actions:");
    ImGui::Spacing();

    // Apply current -> global
    if (ImGui::Button("Apply: Current → Global")) {
        // Refresh current settings before saving
        utils::ReadCurrentReShadeSettings(currentSettings);

        if (utils::SaveGlobalSettings(currentSettings)) {
            statusMessage = ICON_FK_OK " Copied current settings to global profile";
            statusColor = ImVec4(0.4f, 1.0f, 0.4f, 1.0f);
            LogInfo("Saved current settings to global profile");

            // Reload global settings to reflect changes
            utils::LoadGlobalSettings(globalSettings);
        } else {
            statusMessage = ICON_FK_CANCEL " Failed to save to global profile";
            statusColor = ImVec4(1.0f, 0.4f, 0.4f, 1.0f);
            LogInfo("Failed to save to global profile");
        }
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Copy current game's ReShade settings to global profile\n(Overwrites DisplayCommander.ini)");
    }

    ImGui::SameLine();

    // Apply global -> current
    if (ImGui::Button("Apply: Global → Current")) {
        // Refresh global settings before applying
        if (utils::LoadGlobalSettings(globalSettings)) {
            if (utils::WriteCurrentReShadeSettings(globalSettings)) {
                statusMessage = ICON_FK_OK " Applied global profile to current game";
                statusColor = ImVec4(0.4f, 1.0f, 0.4f, 1.0f);
                LogInfo("Applied global settings to current ReShade.ini");

                // Reload current settings to reflect changes
                utils::ReadCurrentReShadeSettings(currentSettings);
            } else {
                statusMessage = ICON_FK_CANCEL " Failed to apply global settings";
                statusColor = ImVec4(1.0f, 0.4f, 0.4f, 1.0f);
                LogInfo("Failed to apply global settings");
            }
        } else {
            statusMessage = ICON_FK_CANCEL " No global profile found (create one first)";
            statusColor = ImVec4(1.0f, 0.7f, 0.0f, 1.0f);
            LogInfo("No global settings file found");
        }
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Apply global profile to current game's ReShade settings\n(Overwrites current game's ReShade.ini)");
    }

    // Status message
    if (!statusMessage.empty()) {
        ImGui::Spacing();
        ImGui::TextColored(statusColor, "%s", statusMessage.c_str());
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // View current settings
    if (ImGui::TreeNode("View Current Game Settings")) {
        for (const auto& [section, keys_values] : currentSettings.additional_settings) {
            ImGui::TextColored(ImVec4(0.8f, 1.0f, 0.8f, 1.0f), "[%s]", section.c_str());
            if (keys_values.empty()) {
                ImGui::Indent();
                ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "(empty)");
                ImGui::Unindent();
            } else {
                for (const auto& [key, value] : keys_values) {
                    ImGui::Indent();
                    ImGui::TextColored(ImVec4(0.8f, 0.8f, 1.0f, 1.0f), "%s:", key.c_str());
                    ImGui::SameLine();
                    ImGui::TextWrapped("%s", value.c_str());
                    ImGui::Unindent();
                }
            }
            ImGui::Spacing();
        }

        ImGui::TreePop();
    }

    // View global settings
    if (ImGui::TreeNode("View Global Profile")) {
        if (globalSettings.additional_settings.empty()) {
            ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.0f, 1.0f), "No global profile found. Create one using 'Apply: Current → Global'.");
        } else {
            for (const auto& [section, keys_values] : globalSettings.additional_settings) {
                ImGui::TextColored(ImVec4(0.8f, 1.0f, 0.8f, 1.0f), "[%s]", section.c_str());
                if (keys_values.empty()) {
                    ImGui::Indent();
                    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "(empty)");
                    ImGui::Unindent();
                } else {
                    for (const auto& [key, value] : keys_values) {
                        ImGui::Indent();
                        ImGui::TextColored(ImVec4(0.8f, 0.8f, 1.0f, 1.0f), "%s:", key.c_str());
                        ImGui::SameLine();
                        ImGui::TextWrapped("%s", value.c_str());
                        ImGui::Unindent();
                    }
                }
                ImGui::Spacing();
            }
        }

        ImGui::TreePop();
    }
}

}  // namespace ui::new_ui
