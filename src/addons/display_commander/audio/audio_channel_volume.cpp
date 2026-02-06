// Per-channel (left/right speaker) volume for the current process.
// Implemented in a separate translation unit to avoid Windows SDK include order issues
// (audioclient.h vs Functiondiscoverykeys_devpkey.h) in audio_management.cpp.

#include "audio_management.hpp"
#include "../utils/logging.hpp"
#include <audioclient.h>
#include <audiopolicy.h>
#include <mmdeviceapi.h>
#include <wrl/client.h>

namespace {

bool GetChannelVolumeControlForCurrentProcess(IChannelAudioVolume** out_volume, UINT* out_count) {
    if (out_volume == nullptr || out_count == nullptr) {
        return false;
    }
    *out_volume = nullptr;
    *out_count = 0;

    const DWORD target_pid = GetCurrentProcessId();
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool did_init = SUCCEEDED(hr);
    if (!did_init && hr != RPC_E_CHANGED_MODE) {
        LogWarn("CoInitializeEx failed for channel volume");
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
        hr = device->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr,
                              reinterpret_cast<void**>(&session_manager));
        if (FAILED(hr) || session_manager == nullptr) break;
        hr = session_manager->GetSessionEnumerator(&session_enumerator);
        if (FAILED(hr) || session_enumerator == nullptr) break;
        int session_count = 0;
        session_enumerator->GetCount(&session_count);
        for (int i = 0; i < session_count; ++i) {
            IAudioSessionControl* session_control = nullptr;
            if (FAILED(session_enumerator->GetSession(i, &session_control)) || session_control == nullptr) continue;
            Microsoft::WRL::ComPtr<IAudioSessionControl2> session_control2{};
            if (SUCCEEDED(session_control->QueryInterface(IID_PPV_ARGS(&session_control2)))) {
                DWORD pid = 0;
                session_control2->GetProcessId(&pid);
                if (pid == target_pid) {
                    Microsoft::WRL::ComPtr<IChannelAudioVolume> channel_volume{};
                    if (SUCCEEDED(session_control->QueryInterface(IID_PPV_ARGS(&channel_volume)))
                        && channel_volume != nullptr) {
                        UINT n = 0;
                        if (SUCCEEDED(channel_volume->GetChannelCount(&n)) && n > 0) {
                            *out_count = n;
                            channel_volume->AddRef();
                            *out_volume = channel_volume.Get();
                            success = true;
                        }
                    }
                }
            }
            session_control->Release();
            if (success) break;
        }
    } while (false);

    if (session_enumerator != nullptr) session_enumerator->Release();
    if (session_manager != nullptr) session_manager->Release();
    if (device != nullptr) device->Release();
    if (device_enumerator != nullptr) device_enumerator->Release();
    if (did_init && hr != RPC_E_CHANGED_MODE) CoUninitialize();

    return success;
}

}  // namespace

bool GetChannelVolumeCountForCurrentProcess(unsigned int* channel_count_out) {
    if (channel_count_out == nullptr) return false;
    IChannelAudioVolume* pv = nullptr;
    UINT n = 0;
    if (!GetChannelVolumeControlForCurrentProcess(&pv, &n)) {
        return false;
    }
    *channel_count_out = n;
    if (pv != nullptr) pv->Release();
    return true;
}

bool GetChannelVolumeForCurrentProcess(unsigned int channel_index, float* volume_0_1_out) {
    if (volume_0_1_out == nullptr) return false;
    IChannelAudioVolume* pv = nullptr;
    UINT n = 0;
    if (!GetChannelVolumeControlForCurrentProcess(&pv, &n)) {
        return false;
    }
    bool ok = false;
    if (channel_index < n && SUCCEEDED(pv->GetChannelVolume(channel_index, volume_0_1_out))) {
        ok = true;
    }
    if (pv != nullptr) pv->Release();
    return ok;
}

bool SetChannelVolumeForCurrentProcess(unsigned int channel_index, float volume_0_1) {
    IChannelAudioVolume* pv = nullptr;
    UINT n = 0;
    if (!GetChannelVolumeControlForCurrentProcess(&pv, &n)) {
        return false;
    }
    float clamped = (std::max)(0.0f, (std::min)(volume_0_1, 1.0f));
    bool ok = false;
    if (channel_index < n && SUCCEEDED(pv->SetChannelVolume(channel_index, clamped, nullptr))) {
        ok = true;
    }
    if (pv != nullptr) pv->Release();
    return ok;
}
