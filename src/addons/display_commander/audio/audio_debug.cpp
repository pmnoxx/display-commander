#include "audio_debug.hpp"
#include "../addon.hpp"
#include "../utils.hpp"
#include <sstream>
#include <vector>
#include <string>
#include <iomanip> // Required for std::fixed and std::setprecision

void DebugAudioSessions() {
  HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  const bool did_init = SUCCEEDED(hr);
  if (!did_init && hr != RPC_E_CHANGED_MODE) {
    LogWarn("CoInitializeEx failed for audio debugging");
    return;
  }

  IMMDeviceEnumerator* device_enumerator = nullptr;
  IMMDevice* device = nullptr;
  IAudioSessionManager2* session_manager = nullptr;
  IAudioSessionEnumerator* session_enumerator = nullptr;

  do {
    hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&device_enumerator));
    if (FAILED(hr) || device_enumerator == nullptr) {
      LogWarn("Failed to create MMDeviceEnumerator for audio debugging");
      break;
    }
    
    hr = device_enumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, &device);
    if (FAILED(hr) || device == nullptr) {
      LogWarn("Failed to get default audio endpoint for audio debugging");
      break;
    }
    
    hr = device->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(&session_manager));
    if (FAILED(hr) || session_manager == nullptr) {
      LogWarn("Failed to activate IAudioSessionManager2 for audio debugging");
      break;
    }
    
    hr = session_manager->GetSessionEnumerator(&session_enumerator);
    if (FAILED(hr) || session_enumerator == nullptr) {
      LogWarn("Failed to get session enumerator for audio debugging");
      break;
    }
    
    int count = 0; 
    session_enumerator->GetCount(&count);
    std::ostringstream oss; oss << "Found " << count << " audio sessions"; LogInfo(oss.str().c_str());
    
    for (int i = 0; i < count; ++i) {
      IAudioSessionControl* session_control = nullptr;
      if (FAILED(session_enumerator->GetSession(i, &session_control)) || session_control == nullptr) {
        std::ostringstream oss; oss << "Failed to get audio session " << i; LogWarn(oss.str().c_str());
        continue;
      }
      
      LogAudioSessionInfo(session_control, i);
      session_control->Release();
    }
  } while(false);

  if (session_enumerator != nullptr) session_enumerator->Release();
  if (session_manager != nullptr) session_manager->Release();
  if (device != nullptr) device->Release();
  if (device_enumerator != nullptr) device_enumerator->Release();
  if (did_init && hr != RPC_E_CHANGED_MODE) CoUninitialize();
}

void DebugAudioSessionForProcess(DWORD process_id) {
  HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  const bool did_init = SUCCEEDED(hr);
  if (!did_init && hr != RPC_E_CHANGED_MODE) {
    LogWarn("CoInitializeEx failed for process audio debugging");
    return;
  }

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
    
    int count = 0; 
    session_enumerator->GetCount(&count);
    bool found_process = false;
    
    for (int i = 0; i < count; ++i) {
      IAudioSessionControl* session_control = nullptr;
      if (FAILED(session_enumerator->GetSession(i, &session_control)) || session_control == nullptr) continue;
      
      IAudioSessionControl2* session_control2 = nullptr;
      if (SUCCEEDED(session_control->QueryInterface(&session_control2)) && session_control2 != nullptr) {
        DWORD pid = 0; 
        session_control2->GetProcessId(&pid);
        if (pid == process_id) {
          found_process = true;
          std::ostringstream oss; oss << "Found audio session for process " << process_id << " (session " << i << ")"; LogInfo(oss.str().c_str());
          LogAudioSessionInfo(session_control, i);
        }
        session_control2->Release();
      }
      session_control->Release();
    }
    
    if (!found_process) {
      std::ostringstream oss; oss << "No audio session found for process " << process_id; LogWarn(oss.str().c_str());
    }
  } while(false);

  if (session_enumerator != nullptr) session_enumerator->Release();
  if (session_manager != nullptr) session_manager->Release();
  if (device != nullptr) device->Release();
  if (device_enumerator != nullptr) device_enumerator->Release();
  if (did_init && hr != RPC_E_CHANGED_MODE) CoUninitialize();
}

void LogAudioSessionInfo(IAudioSessionControl* session_control, int session_index) {
  if (session_control == nullptr) return;
  
  // Get session state
  AudioSessionState state;
  HRESULT hr = session_control->GetState(&state);
  if (SUCCEEDED(hr)) {
    std::string state_str;
    switch (state) {
      case AudioSessionStateInactive: state_str = "Inactive"; break;
      case AudioSessionStateActive: state_str = "Active"; break;
      case AudioSessionStateExpired: state_str = "Expired"; break;
      default: state_str = "Unknown"; break;
    }
    std::ostringstream oss; oss << "  Session " << session_index << ": State = " << state_str; LogInfo(oss.str().c_str());
  }
  
  // Get session display name
  LPWSTR display_name = nullptr;
  hr = session_control->GetDisplayName(&display_name);
  if (SUCCEEDED(hr) && display_name != nullptr) {
    // Convert wide string to narrow string for logging
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, display_name, -1, nullptr, 0, nullptr, nullptr);
    if (size_needed > 0) {
      std::vector<char> buffer(size_needed);
      WideCharToMultiByte(CP_UTF8, 0, display_name, -1, buffer.data(), size_needed, nullptr, nullptr);
      std::ostringstream oss; oss << "  Session " << session_index << ": Display Name = " << buffer.data(); LogInfo(oss.str().c_str());
    }
    CoTaskMemFree(display_name);
  }
  
  // Get session icon path
  LPWSTR icon_path = nullptr;
  hr = session_control->GetIconPath(&icon_path);
  if (SUCCEEDED(hr) && icon_path != nullptr) {
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, icon_path, -1, nullptr, 0, nullptr, nullptr);
    if (size_needed > 0) {
      std::vector<char> buffer(size_needed);
      WideCharToMultiByte(CP_UTF8, 0, icon_path, -1, buffer.data(), size_needed, nullptr, nullptr);
      std::ostringstream oss; oss << "  Session " << session_index << ": Icon Path = " << buffer.data(); LogInfo(oss.str().c_str());
    }
    CoTaskMemFree(icon_path);
  }
  
  // Try to get process ID
  IAudioSessionControl2* session_control2 = nullptr;
  if (SUCCEEDED(session_control->QueryInterface(&session_control2)) && session_control2 != nullptr) {
    DWORD pid = 0;
    hr = session_control2->GetProcessId(&pid);
    if (SUCCEEDED(hr)) {
      std::ostringstream oss; oss << "  Session " << session_index << ": Process ID = " << pid; LogInfo(oss.str().c_str());
    }
    
    // Get session identifier
    LPWSTR session_id = nullptr;
    hr = session_control2->GetSessionIdentifier(&session_id);
    if (SUCCEEDED(hr) && session_id != nullptr) {
      int size_needed = WideCharToMultiByte(CP_UTF8, 0, session_id, -1, nullptr, 0, nullptr, nullptr);
      if (size_needed > 0) {
        std::vector<char> buffer(size_needed);
        WideCharToMultiByte(CP_UTF8, 0, session_id, -1, buffer.data(), size_needed, nullptr, nullptr);
        std::ostringstream oss; oss << "  Session " << session_index << ": Session ID = " << buffer.data(); LogInfo(oss.str().c_str());
      }
      CoTaskMemFree(session_id);
    }
    
    session_control2->Release();
  }
  
  // Try to get volume information
  ISimpleAudioVolume* simple_volume = nullptr;
  if (SUCCEEDED(session_control->QueryInterface(&simple_volume)) && simple_volume != nullptr) {
    float volume = 0.0f;
    hr = simple_volume->GetMasterVolume(&volume);
    if (SUCCEEDED(hr)) {
      std::ostringstream oss; oss << "  Session " << session_index << ": Master Volume = " << std::fixed << std::setprecision(2) << volume; LogInfo(oss.str().c_str());
    }
    
    BOOL muted = FALSE;
    hr = simple_volume->GetMute(&muted);
    if (SUCCEEDED(hr)) {
      std::ostringstream oss; oss << "  Session " << session_index << ": Muted = " << (muted ? "Yes" : "No"); LogInfo(oss.str().c_str());
    }
    
    simple_volume->Release();
  }
}
