#include "audio_management.hpp"
#include "../addon.hpp"
#include "../utils.hpp"
#include <sstream>
#include <thread>

bool SetMuteForCurrentProcess(bool mute) {
  const DWORD target_pid = GetCurrentProcessId();
  HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  const bool did_init = SUCCEEDED(hr);
  if (!did_init && hr != RPC_E_CHANGED_MODE) {
    LogWarn("CoInitializeEx failed for audio mute control");
    return false;
  }

  bool success = false;
  IMMDeviceEnumerator* device_enumerator = nullptr;
  IMMDevice* device = nullptr;
  IAudioSessionManager2* session_manager = nullptr;
  IAudioSessionEnumerator* session_enumerator = nullptr;

  do {
    hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&device_enumerator));
    if (FAILED(hr) || device_enumerator == nullptr) break;
    hr = device_enumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, &device);
    if (FAILED(hr) || device == nullptr) break;
    hr = device->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(&session_manager));
    if (FAILED(hr) || session_manager == nullptr) break;
    hr = session_manager->GetSessionEnumerator(&session_enumerator);
    if (FAILED(hr) || session_enumerator == nullptr) break;
    int count = 0; session_enumerator->GetCount(&count);
    for (int i = 0; i < count; ++i) {
      IAudioSessionControl* session_control = nullptr;
      if (FAILED(session_enumerator->GetSession(i, &session_control)) || session_control == nullptr) continue;
      IAudioSessionControl2* session_control2 = nullptr;
      if (SUCCEEDED(session_control->QueryInterface(&session_control2)) && session_control2 != nullptr) {
        DWORD pid = 0; session_control2->GetProcessId(&pid);
        if (pid == target_pid) {
          ISimpleAudioVolume* simple_volume = nullptr;
          if (SUCCEEDED(session_control->QueryInterface(&simple_volume)) && simple_volume != nullptr) {
            simple_volume->SetMute(mute ? TRUE : FALSE, nullptr);
            simple_volume->Release();
            success = true;
          }
        }
        session_control2->Release();
      }
      session_control->Release();
    }
  } while(false);

  if (session_enumerator != nullptr) session_enumerator->Release();
  if (session_manager != nullptr) session_manager->Release();
  if (device != nullptr) device->Release();
  if (device_enumerator != nullptr) device_enumerator->Release();
  if (did_init && hr != RPC_E_CHANGED_MODE) CoUninitialize();

  std::ostringstream oss; oss << "BackgroundMute apply mute=" << (mute ? "1" : "0") << " success=" << (success ? "1" : "0");
  LogInfo(oss.str().c_str());
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
  IMMDeviceEnumerator* device_enumerator = nullptr;
  IMMDevice* device = nullptr;
  IAudioSessionManager2* session_manager = nullptr;
  IAudioSessionEnumerator* session_enumerator = nullptr;

  do {
    hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&device_enumerator));
    if (FAILED(hr) || device_enumerator == nullptr) break;
    hr = device_enumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, &device);
    if (FAILED(hr) || device == nullptr) break;
    hr = device->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(&session_manager));
    if (FAILED(hr) || session_manager == nullptr) break;
    hr = session_manager->GetSessionEnumerator(&session_enumerator);
    if (FAILED(hr) || session_enumerator == nullptr) break;
    int count = 0; session_enumerator->GetCount(&count);
    for (int i = 0; i < count; ++i) {
      IAudioSessionControl* session_control = nullptr;
      if (FAILED(session_enumerator->GetSession(i, &session_control)) || session_control == nullptr) continue;
      IAudioSessionControl2* session_control2 = nullptr;
      if (SUCCEEDED(session_control->QueryInterface(&session_control2)) && session_control2 != nullptr) {
        DWORD pid = 0; session_control2->GetProcessId(&pid);
        if (pid != 0 && pid != target_pid) {
          // Check state and volume/mute
          AudioSessionState state{};
          if (SUCCEEDED(session_control->GetState(&state)) && state == AudioSessionStateActive) {
            ISimpleAudioVolume* simple_volume = nullptr;
            if (SUCCEEDED(session_control->QueryInterface(&simple_volume)) && simple_volume != nullptr) {
              float vol = 0.0f; BOOL muted = FALSE;
              if (SUCCEEDED(simple_volume->GetMasterVolume(&vol)) && SUCCEEDED(simple_volume->GetMute(&muted))) {
                if (muted == FALSE && vol > 0.001f) {
                  other_active = true;
                  simple_volume->Release();
                  session_control2->Release();
                  session_control->Release();
                  break;
                }
              }
              simple_volume->Release();
            }
          }
        }
        session_control2->Release();
      }
      session_control->Release();
    }
  } while(false);

  if (session_enumerator != nullptr) session_enumerator->Release();
  if (session_manager != nullptr) session_manager->Release();
  if (device != nullptr) device->Release();
  if (device_enumerator != nullptr) device_enumerator->Release();
  if (did_init && hr != RPC_E_CHANGED_MODE) CoUninitialize();

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
  IMMDeviceEnumerator* device_enumerator = nullptr;
  IMMDevice* device = nullptr;
  IAudioSessionManager2* session_manager = nullptr;
  IAudioSessionEnumerator* session_enumerator = nullptr;

  do {
    hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&device_enumerator));
    if (FAILED(hr) || device_enumerator == nullptr) break;
    hr = device_enumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, &device);
    if (FAILED(hr) || device == nullptr) break;
    hr = device->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(&session_manager));
    if (FAILED(hr) || session_manager == nullptr) break;
    hr = session_manager->GetSessionEnumerator(&session_enumerator);
    if (FAILED(hr) || session_enumerator == nullptr) break;
    int count = 0; session_enumerator->GetCount(&count);
    for (int i = 0; i < count; ++i) {
      IAudioSessionControl* session_control = nullptr;
      if (FAILED(session_enumerator->GetSession(i, &session_control)) || session_control == nullptr) continue;
      IAudioSessionControl2* session_control2 = nullptr;
      if (SUCCEEDED(session_control->QueryInterface(&session_control2)) && session_control2 != nullptr) {
        DWORD pid = 0; session_control2->GetProcessId(&pid);
        if (pid == target_pid) {
          ISimpleAudioVolume* simple_volume = nullptr;
          if (SUCCEEDED(session_control->QueryInterface(&simple_volume)) && simple_volume != nullptr) {
            simple_volume->SetMasterVolume(scalar, nullptr);
            simple_volume->Release();
            success = true;
          }
        }
        session_control2->Release();
      }
      session_control->Release();
    }
  } while(false);

  if (session_enumerator != nullptr) session_enumerator->Release();
  if (session_manager != nullptr) session_manager->Release();
  if (device != nullptr) device->Release();
  if (device_enumerator != nullptr) device_enumerator->Release();
  if (did_init && hr != RPC_E_CHANGED_MODE) CoUninitialize();

  std::ostringstream oss; oss << "BackgroundVolume set percent=" << clamped << " success=" << (success ? "1" : "0");
  LogInfo(oss.str().c_str());
  return success;
}

void RunBackgroundAudioMonitor() {
  while (!g_shutdown.load()) {
    bool want_mute = false;
    
    // Check if manual mute is enabled - if so, always mute regardless of background state
    if (s_audio_mute >= 0.5f) {
      want_mute = true;
    }
    // Only apply background mute logic if manual mute is OFF
    else if (s_mute_in_background >= 0.5f || s_mute_in_background_if_other_audio >= 0.5f) {
      HWND hwnd = g_last_swapchain_hwnd.load();
      if (hwnd == nullptr) hwnd = GetForegroundWindow();
      // Use actual focus state for background audio (not spoofed)
      const bool is_background = (hwnd != nullptr && GetForegroundWindow() != hwnd);
      if (is_background) {
        if (s_mute_in_background_if_other_audio >= 0.5f) {
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
      if (SetMuteForCurrentProcess(want_mute)) {
        g_muted_applied.store(want_mute);
      }
    }

    // Background FPS limit handling moved to fps_limiter module
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
  }
}
