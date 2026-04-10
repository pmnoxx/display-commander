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
#include "../../utils/exponential_smooth.hpp"
#include "../../utils/logging.hpp"

// Libraries <standard C++>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <iomanip>
#include <memory>
#include <sstream>
#include <thread>
#include <vector>

// Libraries <Windows.h>
#include <Windows.h>

// Libraries <Windows>
#include <objbase.h>

namespace modules::audio {
namespace {

std::atomic<bool> g_background_audio_monitor_started{false};
std::atomic<bool> g_audio_volume_sync_thread_started{false};
std::atomic<bool> g_audio_ui_sampler_thread_started{false};

struct AudioUiSamplerSnapshot {
    AudioDeviceFormatInfo device_info;
    bool device_info_valid = false;
    unsigned int meter_effective_count = 0;
    std::vector<float> meter_peaks_0_1;
    bool meter_valid = false;
    unsigned int session_channel_count = 0;
    std::vector<float> session_channel_volumes_0_1;
    bool session_channel_valid = false;
};

std::atomic<std::shared_ptr<const AudioUiSamplerSnapshot>> g_audio_ui_sampler_snapshot;

void RunAudioUiSamplerThread() {
    const HRESULT hr_com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (hr_com == RPC_E_CHANGED_MODE) {
        LogWarn("[AudioModule] audio UI sampler: CoInitializeEx RPC_E_CHANGED_MODE");
        return;
    }
    if (FAILED(hr_com)) {
        LogWarn("[AudioModule] audio UI sampler: CoInitializeEx failed (hr=0x%08lx)",
                static_cast<unsigned long>(hr_com));
        return;
    }

    LogInfo("[AudioModule] audio UI sampler thread running");
    unsigned tick = 0;
    while (!g_shutdown.load(std::memory_order_acquire)) {
        std::shared_ptr<const AudioUiSamplerSnapshot> prev =
            g_audio_ui_sampler_snapshot.load(std::memory_order_acquire);
        auto snap = std::make_shared<AudioUiSamplerSnapshot>();

        if ((tick % 40u) == 0u) {
            snap->device_info_valid = GetDefaultAudioDeviceFormatInfo_AssumeComInitialized(&snap->device_info);
        } else if (prev) {
            snap->device_info = prev->device_info;
            snap->device_info_valid = prev->device_info_valid;
        }

        snap->meter_valid = GetAudioMeterPeaksForUi_AssumeComInitialized(&snap->meter_effective_count,
                                                                           &snap->meter_peaks_0_1);

        if (!IsUsingWine() && ((tick % 5u) == 0u)) {
            snap->session_channel_valid = GetAllChannelVolumesForCurrentProcess_AssumeComInitialized(
                &snap->session_channel_volumes_0_1, &snap->session_channel_count);
        } else if (prev) {
            snap->session_channel_volumes_0_1 = prev->session_channel_volumes_0_1;
            snap->session_channel_count = prev->session_channel_count;
            snap->session_channel_valid = prev->session_channel_valid;
        }

        g_audio_ui_sampler_snapshot.store(snap, std::memory_order_release);
        tick++;
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }

    CoUninitialize();
}

void RunAudioVolumeSyncThread() {
    while (!g_shutdown.load(std::memory_order_acquire)) {
        float current_volume = 0.0f;
        if (::GetVolumeForCurrentProcess(&current_volume)) {
            s_game_volume_percent.store(current_volume, std::memory_order_release);
        }

        float system_volume = 0.0f;
        if (::GetSystemVolume(&system_volume)) {
            s_system_volume_percent.store(system_volume, std::memory_order_release);
        }

        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

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
    CALL_GUARD_NO_TS();
    g_rendering_ui_section.store("ui:tab:main_new:audio:entry", std::memory_order_release);
    std::shared_ptr<const AudioUiSamplerSnapshot> snap =
        g_audio_ui_sampler_snapshot.load(std::memory_order_acquire);

    // Default output device format info (channel config, Hz, bits, format, extension, device name)
    g_rendering_ui_section.store("ui:tab:main_new:audio:device_info", std::memory_order_release);
    const AudioDeviceFormatInfo* device_info_ptr = nullptr;
    if (snap && snap->device_info_valid) {
        device_info_ptr = &snap->device_info;
    }
    if (device_info_ptr != nullptr
        && (device_info_ptr->channel_count > 0 || device_info_ptr->sample_rate_hz > 0)) {
        const AudioDeviceFormatInfo& device_info = *device_info_ptr;
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
    float volume = s_game_volume_percent.load(std::memory_order_relaxed);
    imgui.SetNextItemWidth(400.0f);
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
    float system_volume = s_system_volume_percent.load(std::memory_order_relaxed);
    imgui.SetNextItemWidth(400.0f);
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
    static std::vector<float> s_vu_smoothed;
    unsigned int effective_meter_count = 0;
    if (snap && snap->meter_valid && snap->meter_effective_count > 0
        && snap->meter_peaks_0_1.size() == snap->meter_effective_count) {
        effective_meter_count = snap->meter_effective_count;
        if (s_vu_smoothed.size() < effective_meter_count) {
            s_vu_smoothed.resize(effective_meter_count, 0.0f);
        }
        const float decay = 0.85f;
        for (unsigned int i = 0; i < effective_meter_count; ++i) {
            float p = snap->meter_peaks_0_1[i];
            float s = s_vu_smoothed[i];
            s_vu_smoothed[i] = (p > s) ? p : (s * decay);
        }
    }

    if (!IsUsingWine()) {
        g_rendering_ui_section.store("ui:tab:main_new:audio:per_channel_volume", std::memory_order_release);
        unsigned int channel_count = 0;
        const bool have_channel_volume =
            snap && snap->session_channel_valid && snap->session_channel_count >= 1
            && snap->session_channel_volumes_0_1.size() == snap->session_channel_count;
        if (have_channel_volume) {
            channel_count = snap->session_channel_count;
            const std::vector<float>& channel_vols = snap->session_channel_volumes_0_1;
            if (imgui.TreeNodeEx("Per-channel volume", ImGuiTreeNodeFlags_DefaultOpen)) {
                static std::vector<float> s_perch_vu_display_smoothed;
                static unsigned int s_perch_vu_display_count_stored = 0;
                static uint64_t s_perch_vu_display_last_ns = 0;

                if (channel_count != s_perch_vu_display_count_stored) {
                    s_perch_vu_display_smoothed.assign(channel_count, 0.0f);
                    for (unsigned int i = 0; i < channel_count; ++i) {
                        if (i < effective_meter_count && i < s_vu_smoothed.size()) {
                            s_perch_vu_display_smoothed[i] = (std::min)(1.0f, s_vu_smoothed[i]);
                        }
                    }
                    s_perch_vu_display_count_stored = channel_count;
                    s_perch_vu_display_last_ns = 0;
                }

                const uint64_t now_perch_vu_ns = utils::get_now_ns();
                float dt_perch_vu_sec = (1.0f / 60.0f);
                if (s_perch_vu_display_last_ns != 0ULL) {
                    dt_perch_vu_sec =
                        static_cast<float>(static_cast<double>(now_perch_vu_ns - s_perch_vu_display_last_ns) * 1e-9);
                    dt_perch_vu_sec = (std::min)((std::max)(dt_perch_vu_sec, 1.0e-4f), 0.25f);
                }
                s_perch_vu_display_last_ns = now_perch_vu_ns;
                const float k_perch_vu_display_tau_sec = utils::first_order_tau_for_step_alpha(0.03f, 60.0f);

                const float row_vu_width = 14.0f;
                const float row_vu_height = 32.0f;
                const float vu_level_text_w = imgui.CalcTextSize("100.0%").x;

                float left_col_w = 0.0f;
                for (unsigned int i = 0; i < channel_count; ++i) {
                    const char* lab = GetAudioChannelLabel(i, channel_count);
                    float row_w = imgui.CalcTextSize(lab).x;
                    if (i < effective_meter_count && i < s_vu_smoothed.size()) {
                        row_w += row_vu_width + 4.0f + 6.0f + vu_level_text_w + 6.0f;
                    }
                    left_col_w = (std::max)(left_col_w, row_w);
                }

                imgui.Columns(2, "dc_audio_perch", false);
                if (left_col_w > 1.0f) {
                    imgui.SetColumnWidth(0, left_col_w);
                }

                for (unsigned int ch = 0; ch < channel_count; ++ch) {
                    const char* label = GetAudioChannelLabel(ch, channel_count);
                    float pct = channel_vols[ch] * 100.0f;

                    if (ch < effective_meter_count && ch < s_vu_smoothed.size()
                        && ch < s_perch_vu_display_smoothed.size()) {
                        const float vu_target = (std::min)(1.0f, s_vu_smoothed[ch]);
                        s_perch_vu_display_smoothed[ch] = utils::exponential_smooth_toward(
                            s_perch_vu_display_smoothed[ch], vu_target, dt_perch_vu_sec, k_perch_vu_display_tau_sec);
                        const float level = (std::min)(1.0f, s_perch_vu_display_smoothed[ch]);
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
                        imgui.TextColored(ui::colors::TEXT_DIMMED, "%s", label);
                        imgui.SameLine(0.0f, 6.0f);
                        imgui.TextColored(ui::colors::TEXT_DIMMED, "%.1f%%", level * 100.0f);
                    } else {
                        imgui.TextColored(ui::colors::TEXT_DIMMED, "%s", label);
                    }

                    imgui.NextColumn();

                    char slider_id[32];
                    (void)std::snprintf(slider_id, sizeof(slider_id), "##ch%u", ch);
                    const float slider_w = imgui.GetContentRegionAvail().x;
                //    if (slider_w > 1.0f) {
                 //       imgui.SetNextItemWidth(slider_w);
                 //   }

                    imgui.SetNextItemWidth(400.0f);
                    if (imgui.SliderFloat(slider_id, &pct, 0.0f, 100.0f, "%.0f%%")) {
                        if (::SetChannelVolumeForCurrentProcess(ch, pct / 100.0f)) {
                            LogInfo("Channel %u volume set", ch);
                        }
                    }
                    if (imgui.IsItemHovered()) {
                        imgui.SetTooltipEx("Volume for channel %u (%s), game audio session.", ch, label);
                    }

                    if (ch + 1u < channel_count) {
                        imgui.NextColumn();
                    }
                }

                imgui.Columns(1);
                imgui.TreePop();
            }
        } else if (device_info_ptr != nullptr && device_info_ptr->channel_count >= 6) {
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

void HotkeySystemVolumeUp() {
    float current_volume = 0.0f;
    if (!::GetSystemVolume(&current_volume)) {
        current_volume = ::s_system_volume_percent.load();
    }

    float step = 0.0f;
    if (current_volume <= 0.0f) {
        step = 1.0f;
    } else {
        step = (std::max)(1.0f, current_volume * 0.20f);
    }

    if (::AdjustSystemVolume(step)) {
        std::ostringstream oss;
        oss << "System volume increased by " << std::fixed << std::setprecision(1) << step << "% via module hotkey";
        LogInfo(oss.str().c_str());
    } else {
        LogWarn("Failed to increase system volume via module hotkey");
    }
}

void HotkeySystemVolumeDown() {
    float current_volume = 0.0f;
    if (!::GetSystemVolume(&current_volume)) {
        current_volume = ::s_system_volume_percent.load();
    }

    if (current_volume <= 0.0f) {
        return;
    }

    const float step = (std::max)(1.0f, current_volume * 0.20f);
    if (::AdjustSystemVolume(-step)) {
        std::ostringstream oss;
        oss << "System volume decreased by " << std::fixed << std::setprecision(1) << step << "% via module hotkey";
        LogInfo(oss.str().c_str());
    } else {
        LogWarn("Failed to decrease system volume via module hotkey");
    }
}

}  // namespace

void Initialize(ModuleConfigApi* config_api) {
    (void)config_api;
    // Restore persisted manual mute after main tab settings load (InitMainNewTab runs before module registry init).
    if (settings::g_mainTabSettings.audio_mute.GetValue()) {
        if (::SetMuteForCurrentProcess(true)) {
            ::g_muted_applied.store(true);
            LogInfo("Audio mute state loaded and applied from settings");
        } else {
            LogWarn("Failed to apply loaded mute state");
        }
    }
}

void OnEnabled() {
    bool background_expected = false;
    if (g_background_audio_monitor_started.compare_exchange_strong(background_expected, true,
                                                                   std::memory_order_acq_rel)) {
        LogInfo("[AudioModule] Starting RunBackgroundAudioMonitor thread");
        std::thread(RunBackgroundAudioMonitor).detach();
    }

    bool sync_expected = false;
    if (g_audio_volume_sync_thread_started.compare_exchange_strong(sync_expected, true, std::memory_order_acq_rel)) {
        LogInfo("[AudioModule] Starting audio volume sync thread");
        std::thread(RunAudioVolumeSyncThread).detach();
    }

    bool sampler_expected = false;
    if (g_audio_ui_sampler_thread_started.compare_exchange_strong(sampler_expected, true, std::memory_order_acq_rel)) {
        LogInfo("[AudioModule] Starting audio UI sampler thread");
        std::thread(RunAudioUiSamplerThread).detach();
    }
}

void DrawTab(display_commander::ui::IImGuiWrapper& imgui, reshade::api::effect_runtime* runtime) {
    (void)runtime;
    DrawAudioSettingsInternal(imgui);
}



void DrawOverlayVUBars(display_commander::ui::IImGuiWrapper& imgui, bool show_tooltips) {
    CALL_GUARD_NO_TS();
    std::shared_ptr<const AudioUiSamplerSnapshot> snap =
        g_audio_ui_sampler_snapshot.load(std::memory_order_acquire);
    if (snap == nullptr || !snap->meter_valid || snap->meter_effective_count == 0
        || snap->meter_peaks_0_1.size() != snap->meter_effective_count) {
        return;
    }
    static std::vector<float> s_overlay_vu_smoothed;
    const unsigned int effective_meter_count = snap->meter_effective_count;
    if (s_overlay_vu_smoothed.size() < effective_meter_count) {
        s_overlay_vu_smoothed.resize(effective_meter_count, 0.0f);
    }
    // Wall-clock first-order smoothing (same feel as α=0.05 per step at 60 Hz); see utils/exponential_smooth.hpp.
    static uint64_t s_overlay_vu_last_smooth_ns = 0;
    const uint64_t now_ns = utils::get_now_ns();
    float dt_sec = (1.0f / 60.0f);
    if (s_overlay_vu_last_smooth_ns != 0ULL) {
        dt_sec = static_cast<float>(static_cast<double>(now_ns - s_overlay_vu_last_smooth_ns) * 1e-9);
        dt_sec = (std::min)((std::max)(dt_sec, 1.0e-4f), 0.25f);
    }
    s_overlay_vu_last_smooth_ns = now_ns;
    const float k_vu_tau_sec = utils::first_order_tau_for_step_alpha(0.1f, 60.0f);
    for (unsigned int i = 0; i < effective_meter_count; ++i) {
        const float p = (std::min)(1.0f, snap->meter_peaks_0_1[i]);
        const float s = s_overlay_vu_smoothed[i];
        s_overlay_vu_smoothed[i] = utils::exponential_smooth_toward(s, p, dt_sec, k_vu_tau_sec);
    }
    const float bar_height = 96.0f;
    const float bar_width = 20.0f;
    // Column width from widest channel label (L, LFE, Ch0, …) so labels do not overlap.
    const float col_pad_x = 6.0f;
    float column_width = bar_width + (col_pad_x * 2.0f);
    for (unsigned int i = 0; i < effective_meter_count; ++i) {
        const char* ch_label = GetAudioChannelLabel(i, effective_meter_count);
        const float tw = imgui.CalcTextSize(ch_label).x;
        column_width = (std::max)(column_width, tw + (col_pad_x * 2.0f));
    }
    const float total_width = static_cast<float>(effective_meter_count) * column_width;
    auto draw_list = imgui.GetWindowDrawList();
    const ImVec2 cursor = imgui.GetCursorScreenPos();
    if (draw_list != nullptr) {
        for (unsigned int i = 0; i < effective_meter_count; ++i) {
            const float level = (std::min)(1.0f, s_overlay_vu_smoothed[i]);
            const float col_x = cursor.x + static_cast<float>(i) * column_width;
            const float x = col_x + ((column_width - bar_width) * 0.5f);
            const ImVec2 bg_min(x, cursor.y);
            const ImVec2 bg_max(x + bar_width, cursor.y + bar_height);
            const float fill_h = level * bar_height;
            const ImVec2 fill_min(x, cursor.y + bar_height - fill_h);
            const ImVec2 fill_max(x + bar_width, cursor.y + bar_height);
            draw_list->AddRectFilled(bg_min, bg_max, IM_COL32(35, 35, 35, 255));
            draw_list->AddRect(bg_min, bg_max, IM_COL32(60, 60, 60, 255), 0.0f, 0, 1.0f);
            draw_list->AddRectFilled(fill_min, fill_max, IM_COL32(80, 180, 80, 255));
        }
    }
    imgui.Dummy(ImVec2(total_width, bar_height));
    const float label_y = cursor.y + bar_height + 2.0f;
    const float line_height = imgui.GetTextLineHeightWithSpacing();
    for (unsigned int i = 0; i < effective_meter_count; ++i) {
        const char* ch_label = GetAudioChannelLabel(i, effective_meter_count);
        const float col_x = cursor.x + static_cast<float>(i) * column_width;
        const float text_w = imgui.CalcTextSize(ch_label).x;
        imgui.SetCursorScreenPos(ImVec2(col_x + ((column_width - text_w) * 0.5f), label_y));
        imgui.TextColored(ui::colors::TEXT_DIMMED, "%s", ch_label);
    }
    if (show_tooltips && imgui.IsItemHovered()) {
        imgui.SetTooltipEx(
            "Per-channel level (default output device). Smoothed with ~0.32 s time constant (exp decay by "
            "wall time, not raw frame count).");
    }
    imgui.SetCursorScreenPos(ImVec2(cursor.x, label_y + line_height));
    imgui.Dummy(ImVec2(total_width, line_height));
}

void DrawOverlay(display_commander::ui::IImGuiWrapper& imgui) {
    if (settings::g_mainTabSettings.show_overlay_vu_bars.GetValue()) {
        DrawOverlayVUBars(imgui, false);
    }
}

void DrawMainTabInline(display_commander::ui::IImGuiWrapper& imgui, reshade::api::effect_runtime* runtime) {
    (void)runtime;
    // Main tab optional panel "Audio Control" already embeds the same settings (DrawTab); avoid a second header.
    if (settings::g_mainTabSettings.show_main_tab_audio_control.GetValue()) {
        return;
    }
    imgui.Spacing();
    g_rendering_ui_section.store("ui:tab:main_new:audio", std::memory_order_release);
    ui::colors::PushHeaderColors(&imgui);
    const bool audio_control_open = imgui.CollapsingHeader("Audio Control", ImGuiTreeNodeFlags_None);
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
    hotkeys_out->push_back(ModuleHotkeySpec{
        "system_volume_up", "System Volume Up", "ctrl alt up",
        "Increase system master volume (percentage-based, min 1%)", &HotkeySystemVolumeUp});
    hotkeys_out->push_back(ModuleHotkeySpec{
        "system_volume_down", "System Volume Down", "ctrl alt down",
        "Decrease system master volume (percentage-based, min 1%)", &HotkeySystemVolumeDown});
}

void FillActions(std::vector<ModuleActionSpec>* actions_out) {
    if (actions_out == nullptr) {
        return;
    }

    actions_out->push_back(
        ModuleActionSpec{"mute/unmute audio", "Mute/Unmute Audio", "Toggle audio mute state", &HotkeyMuteToggle});
    actions_out->push_back(ModuleActionSpec{"increase volume", "Increase Volume",
                                            "Increase game volume (percentage-based, min 1%)", &HotkeyVolumeUp});
    actions_out->push_back(ModuleActionSpec{"decrease volume", "Decrease Volume",
                                            "Decrease game volume (percentage-based, min 1%)", &HotkeyVolumeDown});
    actions_out->push_back(ModuleActionSpec{"increase system volume", "Increase System Volume",
                                            "Increase system master volume (percentage-based, min 1%)",
                                            &HotkeySystemVolumeUp});
    actions_out->push_back(ModuleActionSpec{"decrease system volume", "Decrease System Volume",
                                            "Decrease system master volume (percentage-based, min 1%)",
                                            &HotkeySystemVolumeDown});
}

}  // namespace modules::audio
