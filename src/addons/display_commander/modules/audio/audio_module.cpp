// Source Code <Display Commander> // follow this order for includes in all files + add this comment at the top
#include "audio_module.hpp"

// Source Code <Display Commander>
#include "backend/audio_backend.hpp"
#include "../../globals.hpp"
#include "../../hooks/windows_hooks/api_hooks.hpp"
#include "ui/ui_colors.hpp"
#include "../../settings/main_tab_settings.hpp"
#include "../../ui/new_ui/main_new_tab.hpp"
#include "../../utils.hpp"
#include "../../utils/detour_call_tracker.hpp"
#include "../../utils/logging.hpp"

// Libraries <standard C++>
#include <algorithm>
#include <iomanip>
#include <cstdio>
#include <sstream>
#include <vector>

namespace modules::audio {
namespace {

// Returns a short label for an audio channel (L, R, C, LFE, etc.) for display in per-channel volume/VU UI.
const char* GetAudioChannelLabel(unsigned int channel_index, unsigned int channel_count) {
    static const char* stereo[] = {"L", "R"};
    static const char* five_one[] = {"L", "R", "C", "LFE", "RL", "RR"};
    static const char* seven_one[] = {"L", "R", "C", "LFE", "RL", "RR", "SL", "SR"};
    static char generic_buf[16];
    if (channel_count == 1 && channel_index == 0) return "M";
    if (channel_count == 2 && channel_index < 2) return stereo[channel_index];
    if (channel_count == 6 && channel_index < 6) return five_one[channel_index];
    if (channel_count == 8 && channel_index < 8) return seven_one[channel_index];
    (void)std::snprintf(generic_buf, sizeof(generic_buf), "Ch%u", channel_index);
    return generic_buf;
}

void DrawAudioSettingsInternal(display_commander::ui::IImGuiWrapper& imgui) {
    CALL_GUARD_NO_TS();;
    g_rendering_ui_section.store("ui:tab:main_new:audio:entry", std::memory_order_release);
    // Default output device format info (channel config, Hz, bits, format, extension, device name)
    g_rendering_ui_section.store("ui:tab:main_new:audio:device_info", std::memory_order_release);
    AudioDeviceFormatInfo device_info;
    if (GetDefaultAudioDeviceFormatInfo(&device_info)
        && (device_info.channel_count > 0 || device_info.sample_rate_hz > 0)) {
        const char* ext_str =
            device_info.format_extension_utf8.empty() ? "-" : device_info.format_extension_utf8.c_str();
        const char* name_str =
            device_info.device_friendly_name_utf8.empty() ? nullptr : device_info.device_friendly_name_utf8.c_str();
        if (name_str && *name_str) {
            imgui.TextColored(ui::colors::TEXT_DIMMED, "Device: %s", name_str);
            if (imgui.IsItemHovered()) {
                imgui.SetTooltipEx(
                    "Default render endpoint. Extension/codec (Dolby, DTS, PCM, etc.) shown on next line.\n\nRaw: %s",
                    device_info.raw_format_utf8.empty() ? "(none)" : device_info.raw_format_utf8.c_str());
            }
            imgui.TextColored(ui::colors::TEXT_DIMMED, "Format: %s, %u Hz, %u-bit, extension: %s",
                              device_info.channel_config_utf8.empty() ? "-" : device_info.channel_config_utf8.c_str(),
                              device_info.sample_rate_hz, device_info.bits_per_sample, ext_str);
        } else {
            imgui.TextColored(ui::colors::TEXT_DIMMED, "Device: %s, %u Hz, %u-bit, extension: %s",
                              device_info.channel_config_utf8.empty() ? "-" : device_info.channel_config_utf8.c_str(),
                              device_info.sample_rate_hz, device_info.bits_per_sample, ext_str);
        }
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx(
                "Source: Default output device mix format from WASAPI (IAudioClient::GetMixFormat).\n"
                "Extension: stream/codec type (e.g. PCM, Float, Dolby AC3, DTS). Device name shows endpoint (e.g. "
                "Dolby Atmos).\n\n"
                "Raw: %s",
                device_info.raw_format_utf8.empty() ? "(none)" : device_info.raw_format_utf8.c_str());
        }
        imgui.Spacing();
    }

    g_rendering_ui_section.store("ui:tab:main_new:audio:game_volume", std::memory_order_release);
    float volume = 0.0f;
    if (::GetVolumeForCurrentProcess(&volume)) {
        s_game_volume_percent.store(volume);
    } else {
        volume = s_game_volume_percent.load();
    }
    if (imgui.SliderFloat("Game Volume (%)", &volume, 0.0f, 100.0f, "%.0f%%")) {
        s_game_volume_percent.store(volume);
        if (::SetVolumeForCurrentProcess(volume)) {
            std::ostringstream oss;
            oss << "Game volume changed to " << static_cast<int>(volume) << "%";
            LogInfo(oss.str().c_str());
        } else {
            std::ostringstream oss;
            oss << "Failed to set game volume to " << static_cast<int>(volume) << "%";
            LogWarn(oss.str().c_str());
        }
    }
    if (imgui.IsItemHovered()) {
        imgui.SetTooltipEx(
            "Game audio volume control (0-100%%). When at 100%%, volume adjustments will affect system volume "
            "instead.");
    }
    g_rendering_ui_section.store("ui:tab:main_new:audio:system_volume", std::memory_order_release);
    float system_volume = 0.0f;
    if (::GetSystemVolume(&system_volume)) {
        s_system_volume_percent.store(system_volume);
    } else {
        system_volume = s_system_volume_percent.load();
    }
    if (imgui.SliderFloat("System Volume (%)", &system_volume, 0.0f, 100.0f, "%.0f%%")) {
        s_system_volume_percent.store(system_volume);
        if (!::SetSystemVolume(system_volume)) {
            std::ostringstream oss;
            oss << "Failed to set system volume to " << static_cast<int>(system_volume) << "%";
            LogWarn(oss.str().c_str());
        }
    }
    if (imgui.IsItemHovered()) {
        imgui.SetTooltipEx(
            "System master volume control (0-100%%). This adjusts the Windows system volume for the default output "
            "device.\n"
            "Note: System volume may also be adjusted automatically when game volume is at 100%% and you increase it.");
    }

    g_rendering_ui_section.store("ui:tab:main_new:audio:mute", std::memory_order_release);
    bool audio_mute = settings::g_mainTabSettings.audio_mute.GetValue();
    if (imgui.Checkbox("Mute", &audio_mute)) {
        settings::g_mainTabSettings.audio_mute.SetValue(audio_mute);

        if (::SetMuteForCurrentProcess(audio_mute)) {
            ::g_muted_applied.store(audio_mute);
            std::ostringstream oss;
            oss << "Audio " << (audio_mute ? "muted" : "unmuted") << " successfully";
            LogInfo(oss.str().c_str());
        } else {
            std::ostringstream oss;
            oss << "Failed to " << (audio_mute ? "mute" : "unmute") << " audio";
            LogWarn(oss.str().c_str());
        }
    }
    if (imgui.IsItemHovered()) {
        imgui.SetTooltipEx("Manually mute/unmute audio.");
    }

    g_rendering_ui_section.store("ui:tab:main_new:audio:vu_peaks", std::memory_order_release);
    static std::vector<float> s_vu_peaks;
    static std::vector<float> s_vu_smoothed;
    unsigned int meter_count = 0;
    unsigned int effective_meter_count = 0;
    if (::GetAudioMeterChannelCount(&meter_count) && meter_count > 0) {
        effective_meter_count = meter_count;
        if (s_vu_peaks.size() < meter_count) {
            s_vu_peaks.resize(meter_count);
            s_vu_smoothed.resize(meter_count, 0.0f);
        }
        if (::GetAudioMeterPeakValues(meter_count, s_vu_peaks.data())) {
            const float decay = 0.85f;
            for (unsigned int i = 0; i < meter_count; ++i) {
                float p = s_vu_peaks[i];
                float s = s_vu_smoothed[i];
                s_vu_smoothed[i] = (p > s) ? p : (s * decay);
            }
        } else if (meter_count > 6 && ::GetAudioMeterPeakValues(6, s_vu_peaks.data())) {
            effective_meter_count = 6;
            const float decay = 0.85f;
            for (unsigned int i = 0; i < 6; ++i) {
                float p = s_vu_peaks[i];
                float s = s_vu_smoothed[i];
                s_vu_smoothed[i] = (p > s) ? p : (s * decay);
            }
        } else if (meter_count > 2 && ::GetAudioMeterPeakValues(2, s_vu_peaks.data())) {
            effective_meter_count = 2;
            const float decay = 0.85f;
            for (unsigned int i = 0; i < 2; ++i) {
                float p = s_vu_peaks[i];
                float s = s_vu_smoothed[i];
                s_vu_smoothed[i] = (p > s) ? p : (s * decay);
            }
        }
    }

    if (!IsUsingWine()) {
        g_rendering_ui_section.store("ui:tab:main_new:audio:per_channel_volume", std::memory_order_release);
        unsigned int channel_count = 0;
        const bool have_channel_volume = ::GetChannelVolumeCountForCurrentProcess(&channel_count) && channel_count >= 1;
        if (have_channel_volume) {
            std::vector<float> channel_vols;
            if (::GetAllChannelVolumesForCurrentProcess(&channel_vols) && channel_vols.size() == channel_count) {
                if (imgui.TreeNodeEx("Per-channel volume", ImGuiTreeNodeFlags_DefaultOpen)) {
                    const float row_vu_width = 14.0f;
                    const float row_vu_height = 32.0f;
                    for (unsigned int ch = 0; ch < channel_count; ++ch) {
                        float pct = channel_vols[ch] * 100.0f;
                        const char* label = GetAudioChannelLabel(ch, channel_count);
                        if (ch < effective_meter_count && ch < s_vu_smoothed.size()) {
                            const float level = (std::min)(1.0f, s_vu_smoothed[ch]);
                            auto draw_list = imgui.GetWindowDrawList();
                            const ImVec2 pos = imgui.GetCursorScreenPos();
                            const ImVec2 bg_min(pos.x, pos.y);
                            const ImVec2 bg_max(pos.x + row_vu_width, pos.y + row_vu_height);
                            const float fill_h = level * row_vu_height;
                            const ImVec2 fill_min(pos.x, pos.y + row_vu_height - fill_h);
                            const ImVec2 fill_max(pos.x + row_vu_width, pos.y + row_vu_height);
                            if (draw_list != nullptr) {
                                draw_list->AddRectFilled(bg_min, bg_max, IM_COL32(40, 40, 40, 255));
                                draw_list->AddRectFilled(fill_min, fill_max, IM_COL32(80, 180, 80, 255));
                            }
                            imgui.Dummy(ImVec2(row_vu_width + 4.0f, row_vu_height));
                            imgui.SameLine(0.0f, 0.0f);
                            imgui.TextColored(ui::colors::TEXT_DIMMED, "%.1f%%", level * 100.0f);
                            imgui.SameLine(0.0f, 6.0f);
                        }
                        char slider_id[32];
                        (void)std::snprintf(slider_id, sizeof(slider_id), "%s (%%)##ch%u", label, ch);
                        if (imgui.SliderFloat(slider_id, &pct, 0.0f, 100.0f, "%.0f%%")) {
                            if (::SetChannelVolumeForCurrentProcess(ch, pct / 100.0f)) {
                                LogInfo("Channel %u volume set", ch);
                            }
                        }
                        if (imgui.IsItemHovered()) {
                            imgui.SetTooltipEx("Volume for channel %u (%s), game audio session.", ch, label);
                        }
                    }
                    imgui.TreePop();
                }
            }
        } else if (device_info.channel_count >= 6) {
            imgui.TextColored(ui::colors::TEXT_DIMMED,
                              "Per-channel volume is not available for this output (e.g. Dolby Atmos PCM 7.1). "
                              "Switch Windows sound output to PCM 5.1 or Stereo for per-channel control.");
            if (imgui.IsItemHovered()) {
                imgui.SetTooltipEx(
                    "IChannelAudioVolume is not exposed by the game audio session on some outputs (e.g. Dolby Atmos).");
            }
        }
    }

    g_rendering_ui_section.store("ui:tab:main_new:audio:mute_in_bg", std::memory_order_release);
    bool mute_in_bg = settings::g_mainTabSettings.mute_in_background.GetValue();
    if (settings::g_mainTabSettings.audio_mute.GetValue()) {
        imgui.BeginDisabled();
    }
    if (imgui.Checkbox("Mute In Background", &mute_in_bg)) {
        settings::g_mainTabSettings.mute_in_background.SetValue(mute_in_bg);
        settings::g_mainTabSettings.mute_in_background_if_other_audio.SetValue(false);
        ::g_muted_applied.store(false);
        if (!settings::g_mainTabSettings.audio_mute.GetValue()) {
            HWND hwnd = g_last_swapchain_hwnd.load();
            bool want_mute =
                (mute_in_bg && hwnd != nullptr && display_commanderhooks::GetForegroundWindow_Direct() != hwnd);
            if (::SetMuteForCurrentProcess(want_mute)) {
                ::g_muted_applied.store(want_mute);
                std::ostringstream oss;
                oss << "Background mute " << (mute_in_bg ? "enabled" : "disabled");
                LogInfo(oss.str().c_str());
            }
        }
    }
    if (imgui.IsItemHovered()) {
        imgui.SetTooltipEx("Mute the game's audio when it is not the foreground window.");
    }
    if (settings::g_mainTabSettings.audio_mute.GetValue()) {
        imgui.EndDisabled();
    }

    g_rendering_ui_section.store("ui:tab:main_new:audio:mute_in_bg_if_other", std::memory_order_release);
    bool mute_in_bg_if_other = settings::g_mainTabSettings.mute_in_background_if_other_audio.GetValue();
    if (settings::g_mainTabSettings.audio_mute.GetValue()) {
        imgui.BeginDisabled();
    }
    if (imgui.Checkbox("Mute In Background (only if other app has audio)", &mute_in_bg_if_other)) {
        settings::g_mainTabSettings.mute_in_background_if_other_audio.SetValue(mute_in_bg_if_other);
        settings::g_mainTabSettings.mute_in_background.SetValue(false);
        ::g_muted_applied.store(false);
        if (!settings::g_mainTabSettings.audio_mute.GetValue()) {
            HWND hwnd = g_last_swapchain_hwnd.load();
            bool is_background = (hwnd != nullptr && display_commanderhooks::GetForegroundWindow_Direct() != hwnd);
            bool want_mute = (mute_in_bg_if_other && is_background && ::IsOtherAppPlayingAudio());
            if (::SetMuteForCurrentProcess(want_mute)) {
                ::g_muted_applied.store(want_mute);
                std::ostringstream oss;
                oss << "Background mute (if other audio) " << (mute_in_bg_if_other ? "enabled" : "disabled");
                LogInfo(oss.str().c_str());
            }
        }
    }
    if (imgui.IsItemHovered()) {
        imgui.SetTooltipEx("Mute only if app is background AND another app outputs audio.");
    }
    if (settings::g_mainTabSettings.audio_mute.GetValue()) {
        imgui.EndDisabled();
    }

    imgui.Separator();
    g_rendering_ui_section.store("ui:tab:main_new:audio:output_device", std::memory_order_release);
    imgui.Text("Output Device");

    static std::vector<std::string> s_audio_device_names;
    static std::vector<std::wstring> s_audio_device_ids;
    static int s_selected_audio_device_index = 0;
    static bool s_audio_devices_initialized = false;

    auto refresh_audio_devices = []() {
        s_audio_device_names.clear();
        s_audio_device_ids.clear();
        s_selected_audio_device_index = 0;

        std::wstring current_device_id;
        if (GetAudioOutputDevices(s_audio_device_names, s_audio_device_ids, current_device_id)) {
            if (current_device_id.empty()) {
                s_selected_audio_device_index = 0;
            } else {
                int matched = 0;
                for (size_t i = 0; i < s_audio_device_ids.size(); ++i) {
                    if (s_audio_device_ids[i] == current_device_id) {
                        matched = static_cast<int>(i) + 1;
                        break;
                    }
                }
                s_selected_audio_device_index = matched;
            }
        }
    };

    if (!s_audio_devices_initialized) {
        refresh_audio_devices();
        s_audio_devices_initialized = true;
    }

    const char* current_label = "System Default";
    if (s_selected_audio_device_index > 0
        && static_cast<size_t>(s_selected_audio_device_index - 1) < s_audio_device_names.size()) {
        current_label = s_audio_device_names[s_selected_audio_device_index - 1].c_str();
    }

    g_rendering_ui_section.store("ui:tab:main_new:audio:output_device_combo", std::memory_order_release);
    if (imgui.BeginCombo("##AudioOutputDevice", current_label)) {
        bool selection_changed = false;
        bool selected_default = (s_selected_audio_device_index == 0);
        if (imgui.Selectable("System Default (use Windows setting)", selected_default)) {
            if (SetAudioOutputDeviceForCurrentProcess(L"")) {
                s_selected_audio_device_index = 0;
                selection_changed = true;
            }
        }
        if (selected_default) {
            imgui.SetItemDefaultFocus();
        }

        for (int i = 0; i < static_cast<int>(s_audio_device_names.size()); ++i) {
            bool selected = (s_selected_audio_device_index == i + 1);
            if (imgui.Selectable(s_audio_device_names[i].c_str(), selected)) {
                if (i >= 0 && static_cast<size_t>(i) < s_audio_device_ids.size()) {
                    if (SetAudioOutputDeviceForCurrentProcess(s_audio_device_ids[i])) {
                        s_selected_audio_device_index = i + 1;
                        selection_changed = true;
                    }
                }
            }
            if (selected) {
                imgui.SetItemDefaultFocus();
            }
        }

        imgui.EndCombo();

        if (selection_changed) {
            // No additional state to sync; Windows persists per-process routing itself.
        }
    }
    if (imgui.IsItemHovered()) {
        imgui.SetTooltipEx(
            "Select which audio output device this game should use.\n"
            "Uses Windows per-application audio routing (similar to 'App volume and device preferences').");
    }

    g_rendering_ui_section.store("ui:tab:main_new:audio:refresh_devices", std::memory_order_release);
    imgui.SameLine();
    if (imgui.Button("Refresh Devices")) {
        refresh_audio_devices();
    }
    if (imgui.IsItemHovered()) {
        imgui.SetTooltipEx("Re-scan active audio output devices (use after plugging/unplugging audio hardware).");
    }
}

void HotkeyMuteToggle() {
    bool new_mute_state = !settings::g_mainTabSettings.audio_mute.GetValue();
    settings::g_mainTabSettings.audio_mute.SetValue(new_mute_state);
    if (::SetMuteForCurrentProcess(new_mute_state)) {
        ::g_muted_applied.store(new_mute_state);
        std::ostringstream oss;
        oss << "Audio " << (new_mute_state ? "muted" : "unmuted") << " via module hotkey";
        LogInfo(oss.str().c_str());
    }
}

void HotkeyVolumeUp() {
    float current_volume = 0.0f;
    if (!::GetVolumeForCurrentProcess(&current_volume)) {
        current_volume = ::s_game_volume_percent.load();
    }

    float step = 0.0f;
    if (current_volume <= 0.0f) {
        step = 1.0f;
    } else {
        step = (std::max)(1.0f, current_volume * 0.20f);
    }

    if (::AdjustVolumeForCurrentProcess(step)) {
        std::ostringstream oss;
        oss << "Volume increased by " << std::fixed << std::setprecision(1) << step << "% via module hotkey";
        LogInfo(oss.str().c_str());
    } else {
        LogWarn("Failed to increase volume via module hotkey");
    }
}

void HotkeyVolumeDown() {
    float current_volume = 0.0f;
    if (!::GetVolumeForCurrentProcess(&current_volume)) {
        current_volume = ::s_game_volume_percent.load();
    }

    if (current_volume <= 0.0f) {
        return;
    }

    const float step = (std::max)(1.0f, current_volume * 0.20f);
    if (::AdjustVolumeForCurrentProcess(-step)) {
        std::ostringstream oss;
        oss << "Volume decreased by " << std::fixed << std::setprecision(1) << step << "% via module hotkey";
        LogInfo(oss.str().c_str());
    } else {
        LogWarn("Failed to decrease volume via module hotkey");
    }
}

}  // namespace

void Initialize(ModuleConfigApi* config_api) {
    (void)config_api;
}

void DrawTab(display_commander::ui::IImGuiWrapper& imgui, reshade::api::effect_runtime* runtime) {
    (void)runtime;
    DrawAudioSettingsInternal(imgui);
}

void DrawOverlay(display_commander::ui::IImGuiWrapper& imgui) {
    if (settings::g_mainTabSettings.show_overlay_vu_bars.GetValue()) {
        ui::new_ui::DrawOverlayVUBars(imgui, false);
    }
}

void DrawMainTabInline(display_commander::ui::IImGuiWrapper& imgui, reshade::api::effect_runtime* runtime) {
    (void)runtime;
    imgui.Spacing();
    g_rendering_ui_section.store("ui:tab:main_new:audio", std::memory_order_release);
    ui::colors::PushHeaderColors(&imgui);
    const bool audio_control_open = imgui.CollapsingHeader("Audio control", ImGuiTreeNodeFlags_None);
    ui::colors::PopCollapsingHeaderColors(&imgui);
    if (audio_control_open) {
        imgui.Indent();
        DrawAudioSettingsInternal(imgui);
        imgui.Unindent();
    }
}

void FillHotkeys(std::vector<ModuleHotkeySpec>* hotkeys_out) {
    if (hotkeys_out == nullptr) {
        return;
    }

    hotkeys_out->push_back(ModuleHotkeySpec{
        "mute_unmute", "Mute/Unmute Audio", "ctrl shift m", "Toggle audio mute state", &HotkeyMuteToggle});
    hotkeys_out->push_back(ModuleHotkeySpec{
        "volume_up", "Volume Up", "ctrl shift up", "Increase audio volume (percentage-based, min 1%)",
        &HotkeyVolumeUp});
    hotkeys_out->push_back(ModuleHotkeySpec{
        "volume_down", "Volume Down", "ctrl shift down", "Decrease audio volume (percentage-based, min 1%)",
        &HotkeyVolumeDown});
}

}  // namespace modules::audio
