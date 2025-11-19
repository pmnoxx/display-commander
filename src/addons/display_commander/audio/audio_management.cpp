#include "audio_management.hpp"
#include "../addon.hpp"
#include "../settings/main_tab_settings.hpp"
#include "../utils.hpp"
#include "../utils/logging.hpp"
#include "../utils/timing.hpp"
#include "../globals.hpp"
#include <sstream>
#include <thread>

bool SetMuteForCurrentProcess(bool mute, bool trigger_notification) {
    const DWORD target_pid = GetCurrentProcessId();
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool did_init = SUCCEEDED(hr);
    if (!did_init && hr != RPC_E_CHANGED_MODE) {
        LogWarn("CoInitializeEx failed for audio mute control");
        return false;
    }

    bool success = false;
    IMMDeviceEnumerator *device_enumerator = nullptr;
    IMMDevice *device = nullptr;
    IAudioSessionManager2 *session_manager = nullptr;
    IAudioSessionEnumerator *session_enumerator = nullptr;

    do {
        hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&device_enumerator));
        if (FAILED(hr) || device_enumerator == nullptr)
            break;
        hr = device_enumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, &device);
        if (FAILED(hr) || device == nullptr)
            break;
        hr = device->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr,
                              reinterpret_cast<void **>(&session_manager));
        if (FAILED(hr) || session_manager == nullptr)
            break;
        hr = session_manager->GetSessionEnumerator(&session_enumerator);
        if (FAILED(hr) || session_enumerator == nullptr)
            break;
        int count = 0;
        session_enumerator->GetCount(&count);
        for (int i = 0; i < count; ++i) {
            IAudioSessionControl *session_control = nullptr;
            if (FAILED(session_enumerator->GetSession(i, &session_control)) || session_control == nullptr)
                continue;
            Microsoft::WRL::ComPtr<IAudioSessionControl2> session_control2 = nullptr;
            if (SUCCEEDED(session_control->QueryInterface(IID_PPV_ARGS(&session_control2))) && session_control2 != nullptr) {
                DWORD pid = 0;
                session_control2->GetProcessId(&pid);
                if (pid == target_pid) {
                    ISimpleAudioVolume *simple_volume = nullptr;
                    if (SUCCEEDED(session_control->QueryInterface(&simple_volume)) && simple_volume != nullptr) {
                        simple_volume->SetMute(mute ? TRUE : FALSE, nullptr);
                        simple_volume->Release();
                        success = true;
                    }
                }
            }
            session_control->Release();
        }
    } while (false);

    if (session_enumerator != nullptr)
        session_enumerator->Release();
    if (session_manager != nullptr)
        session_manager->Release();
    if (device != nullptr)
        device->Release();
    if (device_enumerator != nullptr)
        device_enumerator->Release();
    if (did_init && hr != RPC_E_CHANGED_MODE)
        CoUninitialize();

    std::ostringstream oss;
    oss << "BackgroundMute apply mute=" << (mute ? "1" : "0") << " success=" << (success ? "1" : "0");
    LogInfo(oss.str().c_str());

    // Trigger action notification for overlay display (only if requested, typically for user-initiated changes)
    if (success && trigger_notification) {
        ActionNotification notification;
        notification.type = ActionNotificationType::Mute;
        notification.timestamp_ns = utils::get_now_ns();
        notification.bool_value = mute;
        notification.float_value = 0.0f;
        g_action_notification.store(notification);
    }

    return success;
}

bool IsOtherAppPlayingAudio() {
    const DWORD target_pid = GetCurrentProcessId();
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool did_init = SUCCEEDED(hr);
    if (!did_init && hr != RPC_E_CHANGED_MODE) {
        LogWarn("CoInitializeEx failed for audio session query");
        return false;
    }

    bool other_active = false;
    IMMDeviceEnumerator *device_enumerator = nullptr;
    IMMDevice *device = nullptr;
    IAudioSessionManager2 *session_manager = nullptr;
    IAudioSessionEnumerator *session_enumerator = nullptr;

    do {
        hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&device_enumerator));
        if (FAILED(hr) || device_enumerator == nullptr)
            break;
        hr = device_enumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, &device);
        if (FAILED(hr) || device == nullptr)
            break;
        hr = device->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr,
                              reinterpret_cast<void **>(&session_manager));
        if (FAILED(hr) || session_manager == nullptr)
            break;
        hr = session_manager->GetSessionEnumerator(&session_enumerator);
        if (FAILED(hr) || session_enumerator == nullptr)
            break;
        int count = 0;
        session_enumerator->GetCount(&count);
        for (int i = 0; i < count; ++i) {
            IAudioSessionControl *session_control = nullptr;
            if (FAILED(session_enumerator->GetSession(i, &session_control)) || session_control == nullptr)
                continue;
            Microsoft::WRL::ComPtr<IAudioSessionControl2> session_control2{};
            if (SUCCEEDED(session_control->QueryInterface(IID_PPV_ARGS(&session_control2)))) {
                DWORD pid = 0;
                session_control2->GetProcessId(&pid);
                if (pid != 0 && pid != target_pid) {
                    // Check state and volume/mute
                    AudioSessionState state{};
                    if (SUCCEEDED(session_control->GetState(&state)) && state == AudioSessionStateActive) {
                        Microsoft::WRL::ComPtr<ISimpleAudioVolume> simple_volume{};
                        if (SUCCEEDED(session_control->QueryInterface(IID_PPV_ARGS(&simple_volume)))) {
                            float vol = 0.0f;
                            BOOL muted = FALSE;
                            if (SUCCEEDED(simple_volume->GetMasterVolume(&vol)) &&
                                SUCCEEDED(simple_volume->GetMute(&muted))) {
                                if (muted == FALSE && vol > 0.001f) {
                                    other_active = true;
                                    session_control->Release();
                                    break;
                                }
                            }
                        }
                    }
                }
            }
            session_control->Release();
        }
    } while (false);

    if (session_enumerator != nullptr)
        session_enumerator->Release();
    if (session_manager != nullptr)
        session_manager->Release();
    if (device != nullptr)
        device->Release();
    if (device_enumerator != nullptr)
        device_enumerator->Release();
    if (did_init && hr != RPC_E_CHANGED_MODE)
        CoUninitialize();

    return other_active;
}

bool SetVolumeForCurrentProcess(float volume_0_100) {
    float clamped = (std::max)(0.0f, (std::min)(volume_0_100, 100.0f));
    const float scalar = clamped / 100.0f;
    const DWORD target_pid = GetCurrentProcessId();
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool did_init = SUCCEEDED(hr);
    if (!did_init && hr != RPC_E_CHANGED_MODE) {
        LogWarn("CoInitializeEx failed for audio volume control");
        return false;
    }

    bool success = false;
    IMMDeviceEnumerator *device_enumerator = nullptr;
    IMMDevice *device = nullptr;
    IAudioSessionManager2 *session_manager = nullptr;
    IAudioSessionEnumerator *session_enumerator = nullptr;

    do {
        hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&device_enumerator));
        if (FAILED(hr) || device_enumerator == nullptr)
            break;
        hr = device_enumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, &device);
        if (FAILED(hr) || device == nullptr)
            break;
        hr = device->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr,
                              reinterpret_cast<void **>(&session_manager));
        if (FAILED(hr) || session_manager == nullptr)
            break;
        hr = session_manager->GetSessionEnumerator(&session_enumerator);
        if (FAILED(hr) || session_enumerator == nullptr)
            break;
        int count = 0;
        session_enumerator->GetCount(&count);
        for (int i = 0; i < count; ++i) {
            IAudioSessionControl *session_control = nullptr;
            if (FAILED(session_enumerator->GetSession(i, &session_control)))
                continue;
            Microsoft::WRL::ComPtr<IAudioSessionControl2> session_control2{};
            if (SUCCEEDED(session_control->QueryInterface(IID_PPV_ARGS(&session_control2)))) {
                DWORD pid = 0;
                session_control2->GetProcessId(&pid);
                if (pid == target_pid) {
                    Microsoft::WRL::ComPtr<ISimpleAudioVolume> simple_volume{};
                    if (SUCCEEDED(session_control->QueryInterface(IID_PPV_ARGS(&simple_volume)))) {
                        simple_volume->SetMasterVolume(scalar, nullptr);
                        success = true;
                    }
                }
            }
            session_control->Release();
        }
    } while (false);

    if (session_enumerator != nullptr)
        session_enumerator->Release();
    if (session_manager != nullptr)
        session_manager->Release();
    if (device != nullptr)
        device->Release();
    if (device_enumerator != nullptr)
        device_enumerator->Release();
    if (did_init && hr != RPC_E_CHANGED_MODE)
        CoUninitialize();

    std::ostringstream oss;
    oss << "BackgroundVolume set percent=" << clamped << " success=" << (success ? "1" : "0");
    LogInfo(oss.str().c_str());
    return success;
}

bool GetVolumeForCurrentProcess(float *volume_0_100_out) {
    if (volume_0_100_out == nullptr) {
        return false;
    }

    const DWORD target_pid = GetCurrentProcessId();
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool did_init = SUCCEEDED(hr);
    if (!did_init && hr != RPC_E_CHANGED_MODE) {
        LogWarn("CoInitializeEx failed for audio volume query");
        return false;
    }

    bool success = false;
    IMMDeviceEnumerator *device_enumerator = nullptr;
    IMMDevice *device = nullptr;
    IAudioSessionManager2 *session_manager = nullptr;
    IAudioSessionEnumerator *session_enumerator = nullptr;

    do {
        hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&device_enumerator));
        if (FAILED(hr) || device_enumerator == nullptr)
            break;
        hr = device_enumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, &device);
        if (FAILED(hr) || device == nullptr)
            break;
        hr = device->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr,
                              reinterpret_cast<void **>(&session_manager));
        if (FAILED(hr) || session_manager == nullptr)
            break;
        hr = session_manager->GetSessionEnumerator(&session_enumerator);
        if (FAILED(hr) || session_enumerator == nullptr)
            break;
        int count = 0;
        session_enumerator->GetCount(&count);
        for (int i = 0; i < count; ++i) {
            IAudioSessionControl *session_control = nullptr;
            if (FAILED(session_enumerator->GetSession(i, &session_control)))
                continue;
            Microsoft::WRL::ComPtr<IAudioSessionControl2> session_control2{};
            if (SUCCEEDED(session_control->QueryInterface(IID_PPV_ARGS(&session_control2)))) {
                DWORD pid = 0;
                session_control2->GetProcessId(&pid);
                if (pid == target_pid) {
                    Microsoft::WRL::ComPtr<ISimpleAudioVolume> simple_volume{};
                    if (SUCCEEDED(session_control->QueryInterface(IID_PPV_ARGS(&simple_volume)))) {
                        float scalar = 0.0f;
                        if (SUCCEEDED(simple_volume->GetMasterVolume(&scalar))) {
                            *volume_0_100_out = scalar * 100.0f;
                            success = true;
                        }
                    }
                }
            }
            session_control->Release();
        }
    } while (false);

    if (session_enumerator != nullptr)
        session_enumerator->Release();
    if (session_manager != nullptr)
        session_manager->Release();
    if (device != nullptr)
        device->Release();
    if (device_enumerator != nullptr)
        device_enumerator->Release();
    if (did_init && hr != RPC_E_CHANGED_MODE)
        CoUninitialize();

    return success;
}

bool AdjustVolumeForCurrentProcess(float percent_change) {
    float current_volume = 0.0f;
    if (!GetVolumeForCurrentProcess(&current_volume)) {
        // If we can't get current volume, use the stored value
        current_volume = s_audio_volume_percent.load();
    }

    float new_volume = current_volume + percent_change;
    // Clamp to valid range
    new_volume = (std::max)(0.0f, (std::min)(new_volume, 100.0f));

    if (SetVolumeForCurrentProcess(new_volume)) {
        // Update stored value
        s_audio_volume_percent.store(new_volume);

        // Update overlay display tracking (legacy, for backward compatibility)
        g_volume_change_time_ns.store(utils::get_now_ns());
        g_volume_display_value.store(new_volume);

        // Trigger action notification for overlay display
        ActionNotification notification;
        notification.type = ActionNotificationType::Volume;
        notification.timestamp_ns = utils::get_now_ns();
        notification.float_value = new_volume;
        notification.bool_value = false;
        g_action_notification.store(notification);

        std::ostringstream oss;
        oss << "Volume adjusted by " << (percent_change >= 0.0f ? "+" : "") << percent_change
            << "% to " << new_volume << "%";
        LogInfo(oss.str().c_str());
        return true;
    }

    return false;
}

void RunBackgroundAudioMonitor() {
    // Wait for continuous monitoring to be ready before starting audio management
    while (!g_shutdown.load() && !g_monitoring_thread_running.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    LogInfo("BackgroundAudio: Continuous monitoring ready, starting audio management");

    while (!g_shutdown.load()) {
        bool want_mute = false;

        // Check if manual mute is enabled - if so, always mute regardless of background state
        if (s_audio_mute.load()) {
            want_mute = true;
        }
        // Only apply background mute logic if manual mute is OFF
        else if (s_mute_in_background.load() || s_mute_in_background_if_other_audio.load()) {
            // Use centralized background state from continuous monitoring system for consistency
            const bool is_background = g_app_in_background.load();

            // Log background muting decision for debugging
            static bool last_logged_background = false;
            if (is_background != last_logged_background) {
                std::ostringstream oss;
                oss << "BackgroundAudio: App background state changed to "
                    << (is_background ? "BACKGROUND" : "FOREGROUND")
                    << ", mute_in_background=" << (s_mute_in_background.load() ? "true" : "false")
                    << ", mute_in_background_if_other_audio="
                    << (s_mute_in_background_if_other_audio.load() ? "true" : "false");
                LogInfo(oss.str().c_str());
                last_logged_background = is_background;
            }

            if (is_background) {
                if (s_mute_in_background_if_other_audio.load()) {
                    // Only mute if some other app is outputting audio
                    want_mute = IsOtherAppPlayingAudio();
                } else {
                    want_mute = true;
                }
            } else {
                want_mute = false;
            }
        }

        const bool applied = g_muted_applied.load();
        if (want_mute != applied) {
            std::ostringstream oss;
            oss << "BackgroundAudio: Applying mute change from " << (applied ? "muted" : "unmuted") << " to "
                << (want_mute ? "muted" : "unmuted")
                << " (background=" << (g_app_in_background.load() ? "true" : "false") << ")";
            LogInfo(oss.str().c_str());

            if (SetMuteForCurrentProcess(want_mute, false)) {  // Don't trigger notification for background auto-mute
                g_muted_applied.store(want_mute);
            }
        }

        // Background FPS limit handling moved to fps_limiter module
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
    }
}
