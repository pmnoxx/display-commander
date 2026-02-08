#include "audio_management.hpp"
#include <audioclient.h>
#include <endpointvolume.h>
#include <mmdeviceapi.h>
#include <mmreg.h>
#include <propvarutil.h>
#include <sstream>
#include <thread>
#include "../addon.hpp"
#include "../globals.hpp"
#include "../settings/main_tab_settings.hpp"
#include "../utils.hpp"
#include "../utils/logging.hpp"
#include "../utils/timing.hpp"
#include "audio_device_policy.hpp"

#include <windows.h>

#include <Functiondiscoverykeys_devpkey.h>

namespace {

// Helper to convert UTF-16 (wstring) to UTF-8 std::string
std::string WStringToUtf8(const std::wstring& wstr) {
    if (wstr.empty()) return std::string();

    int size = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (size <= 0) return std::string();

    std::string result(static_cast<size_t>(size - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, &result[0], size, nullptr, nullptr);
    return result;
}

// Dynamic Windows Runtime function typedefs (avoid extra link deps)
using RoGetActivationFactory_pfn = HRESULT(WINAPI*)(HSTRING activatableClassId, REFIID iid, void** factory);
using WindowsCreateStringReference_pfn = HRESULT(WINAPI*)(PCWSTR sourceString, UINT32 length,
                                                          HSTRING_HEADER* hstringHeader, HSTRING* string);
using WindowsDeleteString_pfn = HRESULT(WINAPI*)(HSTRING string);
using WindowsCreateString_pfn = HRESULT(WINAPI*)(PCWSTR sourceString, UINT32 length, HSTRING* string);
using WindowsGetStringRawBuffer_pfn = PCWSTR(WINAPI*)(HSTRING string, UINT32* length);

// Helper to build full MMDevice endpoint ID like Special K (short ID -> full ID)
std::wstring BuildFullAudioDeviceId(EDataFlow flow, const std::wstring& short_id) {
    static const wchar_t* DEVICE_PREFIX = LR"(\\?\SWD#MMDEVAPI#)";
    static const wchar_t* RENDER_POSTFIX = L"#{e6327cad-dcec-4949-ae8a-991e976a79d2}";
    static const wchar_t* CAPTURE_POSTFIX = L"#{2eef81be-33fa-4800-9670-1cd474972c3f}";

    const wchar_t* postfix = (flow == eRender) ? RENDER_POSTFIX : CAPTURE_POSTFIX;

    std::wstring full;
    full.reserve(wcslen(DEVICE_PREFIX) + short_id.size() + wcslen(postfix));
    full.append(DEVICE_PREFIX);
    full.append(short_id);
    full.append(postfix);

    return full;
}

// Get Windows Runtime string functions from combase.dll
bool GetWindowsRuntimeStringFunctions(WindowsCreateStringReference_pfn& createStringRef,
                                      WindowsDeleteString_pfn& deleteString, WindowsCreateString_pfn& createString,
                                      WindowsGetStringRawBuffer_pfn& getStringRawBuffer) {
    static bool s_tried = false;
    static bool s_success = false;
    static WindowsCreateStringReference_pfn s_createStringRef = nullptr;
    static WindowsDeleteString_pfn s_deleteString = nullptr;
    static WindowsCreateString_pfn s_createString = nullptr;
    static WindowsGetStringRawBuffer_pfn s_getStringRawBuffer = nullptr;

    if (s_tried) {
        createStringRef = s_createStringRef;
        deleteString = s_deleteString;
        createString = s_createString;
        getStringRawBuffer = s_getStringRawBuffer;
        return s_success;
    }

    s_tried = true;

    HMODULE combase_module = GetModuleHandleA("combase.dll");
    if (combase_module == nullptr) {
        combase_module = LoadLibraryA("combase.dll");
    }
    if (combase_module == nullptr) {
        LogWarn("AudioPolicyConfig: Failed to load combase.dll");
        return false;
    }

    s_createStringRef = reinterpret_cast<WindowsCreateStringReference_pfn>(
        GetProcAddress(combase_module, "WindowsCreateStringReference"));
    s_deleteString = reinterpret_cast<WindowsDeleteString_pfn>(GetProcAddress(combase_module, "WindowsDeleteString"));
    s_createString = reinterpret_cast<WindowsCreateString_pfn>(GetProcAddress(combase_module, "WindowsCreateString"));
    s_getStringRawBuffer =
        reinterpret_cast<WindowsGetStringRawBuffer_pfn>(GetProcAddress(combase_module, "WindowsGetStringRawBuffer"));

    if (s_createStringRef == nullptr || s_deleteString == nullptr || s_createString == nullptr
        || s_getStringRawBuffer == nullptr) {
        LogWarn("AudioPolicyConfig: Failed to load Windows Runtime string functions from combase.dll");
        s_success = false;
        return false;
    }

    s_success = true;
    createStringRef = s_createStringRef;
    deleteString = s_deleteString;
    createString = s_createString;
    getStringRawBuffer = s_getStringRawBuffer;
    return true;
}

IAudioPolicyConfigFactory* GetAudioPolicyConfigFactory() {
    static IAudioPolicyConfigFactory* s_factory = nullptr;
    static bool s_tried = false;

    if (s_tried) {
        return s_factory;
    }

    s_tried = true;

    WindowsCreateStringReference_pfn createStringRef = nullptr;
    WindowsDeleteString_pfn deleteString = nullptr;
    WindowsCreateString_pfn createString = nullptr;
    WindowsGetStringRawBuffer_pfn getStringRawBuffer = nullptr;

    if (!GetWindowsRuntimeStringFunctions(createStringRef, deleteString, createString, getStringRawBuffer)) {
        return nullptr;
    }

    HMODULE combase_module = GetModuleHandleA("combase.dll");
    if (combase_module == nullptr) {
        combase_module = LoadLibraryA("combase.dll");
    }
    if (combase_module == nullptr) {
        LogWarn("AudioPolicyConfig: Failed to load combase.dll");
        return nullptr;
    }

    auto ro_get_activation_factory =
        reinterpret_cast<RoGetActivationFactory_pfn>(GetProcAddress(combase_module, "RoGetActivationFactory"));
    if (ro_get_activation_factory == nullptr) {
        LogWarn("AudioPolicyConfig: RoGetActivationFactory not found in combase.dll");
        return nullptr;
    }

    const wchar_t* name = L"Windows.Media.Internal.AudioPolicyConfig";
    const UINT32 len = static_cast<UINT32>(wcslen(name));

    HSTRING hClassName = nullptr;
    HSTRING_HEADER header;
    HRESULT hr = createStringRef(name, len, &header, &hClassName);
    if (FAILED(hr) || hClassName == nullptr) {
        if (hClassName != nullptr) {
            deleteString(hClassName);
        }
        LogWarn("AudioPolicyConfig: WindowsCreateStringReference failed");
        return nullptr;
    }

    IAudioPolicyConfigFactory* factory = nullptr;
    hr = ro_get_activation_factory(hClassName, __uuidof(IAudioPolicyConfigFactory), reinterpret_cast<void**>(&factory));

    deleteString(hClassName);

    if (FAILED(hr) || factory == nullptr) {
        LogWarn("AudioPolicyConfig: RoGetActivationFactory failed (hr=0x%08lx)", hr);
        return nullptr;
    }

    LogInfo("AudioPolicyConfig: Successfully acquired IAudioPolicyConfigFactory");
    s_factory = factory;
    return s_factory;
}

std::wstring GetPersistedDefaultEndpointForCurrentProcess(EDataFlow flow) {
    IAudioPolicyConfigFactory* factory = GetAudioPolicyConfigFactory();
    if (factory == nullptr) {
        return std::wstring();
    }

    WindowsDeleteString_pfn deleteString = nullptr;
    WindowsGetStringRawBuffer_pfn getStringRawBuffer = nullptr;
    WindowsCreateStringReference_pfn createStringRef = nullptr;
    WindowsCreateString_pfn createString = nullptr;

    if (!GetWindowsRuntimeStringFunctions(createStringRef, deleteString, createString, getStringRawBuffer)) {
        return std::wstring();
    }

    HSTRING hDeviceId = nullptr;
    HRESULT hr =
        factory->GetPersistedDefaultAudioEndpoint(GetCurrentProcessId(), flow, eMultimedia | eConsole, &hDeviceId);
    if (FAILED(hr) || hDeviceId == nullptr) {
        return std::wstring();
    }

    UINT32 len = 0;
    const wchar_t* buffer = getStringRawBuffer(hDeviceId, &len);
    std::wstring result;
    if (buffer != nullptr && len > 0) {
        result.assign(buffer, buffer + len);
    }

    deleteString(hDeviceId);
    return result;
}

bool SetPersistedDefaultEndpointForCurrentProcess(EDataFlow flow, const std::wstring& device_id) {
    IAudioPolicyConfigFactory* factory = GetAudioPolicyConfigFactory();
    if (factory == nullptr) {
        return false;
    }

    WindowsDeleteString_pfn deleteString = nullptr;
    WindowsGetStringRawBuffer_pfn getStringRawBuffer = nullptr;
    WindowsCreateStringReference_pfn createStringRef = nullptr;
    WindowsCreateString_pfn createString = nullptr;

    if (!GetWindowsRuntimeStringFunctions(createStringRef, deleteString, createString, getStringRawBuffer)) {
        return false;
    }

    HSTRING hDeviceId = nullptr;
    if (!device_id.empty()) {
        HRESULT hr_create = createString(device_id.c_str(), static_cast<UINT32>(device_id.length()), &hDeviceId);
        if (FAILED(hr_create)) {
            LogWarn("AudioPolicyConfig: WindowsCreateString failed for device id");
            return false;
        }
    }

    const UINT pid = GetCurrentProcessId();

    HRESULT hr_console = factory->SetPersistedDefaultAudioEndpoint(pid, flow, eConsole, hDeviceId);
    HRESULT hr_multimedia = factory->SetPersistedDefaultAudioEndpoint(pid, flow, eMultimedia, hDeviceId);

    if (hDeviceId != nullptr) {
        deleteString(hDeviceId);
    }

    if (FAILED(hr_console) || FAILED(hr_multimedia)) {
        LogWarn("AudioPolicyConfig: SetPersistedDefaultAudioEndpoint failed (console=0x%08lx, multimedia=0x%08lx)",
                hr_console, hr_multimedia);
        return false;
    }

    return true;
}

}  // namespace

bool SetMuteForCurrentProcess(bool mute, bool trigger_notification) {
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
        hr = device->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr,
                              reinterpret_cast<void**>(&session_manager));
        if (FAILED(hr) || session_manager == nullptr) break;
        hr = session_manager->GetSessionEnumerator(&session_enumerator);
        if (FAILED(hr) || session_enumerator == nullptr) break;
        int count = 0;
        session_enumerator->GetCount(&count);
        for (int i = 0; i < count; ++i) {
            IAudioSessionControl* session_control = nullptr;
            if (FAILED(session_enumerator->GetSession(i, &session_control)) || session_control == nullptr) continue;
            Microsoft::WRL::ComPtr<IAudioSessionControl2> session_control2 = nullptr;
            if (SUCCEEDED(session_control->QueryInterface(IID_PPV_ARGS(&session_control2)))
                && session_control2 != nullptr) {
                DWORD pid = 0;
                session_control2->GetProcessId(&pid);
                if (pid == target_pid) {
                    Microsoft::WRL::ComPtr<ISimpleAudioVolume> simple_volume;
                    if (SUCCEEDED(session_control->QueryInterface(IID_PPV_ARGS(&simple_volume)))
                        && simple_volume != nullptr) {
                        simple_volume->SetMute(mute ? TRUE : FALSE, nullptr);
                        success = true;
                    }
                }
            }
            session_control->Release();
        }
    } while (false);

    if (session_enumerator != nullptr) session_enumerator->Release();
    if (session_manager != nullptr) session_manager->Release();
    if (device != nullptr) device->Release();
    if (device_enumerator != nullptr) device_enumerator->Release();
    if (did_init && hr != RPC_E_CHANGED_MODE) CoUninitialize();

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
        int count = 0;
        session_enumerator->GetCount(&count);
        for (int i = 0; i < count; ++i) {
            IAudioSessionControl* session_control = nullptr;
            if (FAILED(session_enumerator->GetSession(i, &session_control)) || session_control == nullptr) continue;
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
                            if (SUCCEEDED(simple_volume->GetMasterVolume(&vol))
                                && SUCCEEDED(simple_volume->GetMute(&muted))) {
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
        hr = device->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr,
                              reinterpret_cast<void**>(&session_manager));
        if (FAILED(hr) || session_manager == nullptr) break;
        hr = session_manager->GetSessionEnumerator(&session_enumerator);
        if (FAILED(hr) || session_enumerator == nullptr) break;
        int count = 0;
        session_enumerator->GetCount(&count);
        for (int i = 0; i < count; ++i) {
            IAudioSessionControl* session_control = nullptr;
            if (FAILED(session_enumerator->GetSession(i, &session_control))) continue;
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

    if (session_enumerator != nullptr) session_enumerator->Release();
    if (session_manager != nullptr) session_manager->Release();
    if (device != nullptr) device->Release();
    if (device_enumerator != nullptr) device_enumerator->Release();
    if (did_init && hr != RPC_E_CHANGED_MODE) CoUninitialize();

    std::ostringstream oss;
    oss << "BackgroundVolume set percent=" << clamped << " success=" << (success ? "1" : "0");
    LogInfo(oss.str().c_str());
    return success;
}

bool GetVolumeForCurrentProcess(float* volume_0_100_out) {
    g_continuous_monitoring_section.store("volume:game:entry", std::memory_order_release);
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
        int count = 0;
        session_enumerator->GetCount(&count);
        for (int i = 0; i < count; ++i) {
            IAudioSessionControl* session_control = nullptr;
            if (FAILED(session_enumerator->GetSession(i, &session_control))) continue;
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
            if (session_control != nullptr) {
                session_control->Release();
            }
        }
    } while (false);

    if (session_enumerator != nullptr) session_enumerator->Release();
    if (session_manager != nullptr) session_manager->Release();
    if (device != nullptr) device->Release();
    if (device_enumerator != nullptr) device_enumerator->Release();
    if (did_init && hr != RPC_E_CHANGED_MODE) CoUninitialize();

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

    // If game volume is at 100% and we're trying to increase, start increasing system volume instead
    bool adjusted_system_volume = false;
    if (new_volume >= 100.0f && percent_change > 0.0f && current_volume >= 100.0f) {
        // Game volume is already at max, adjust system volume instead
        float current_system_volume = 0.0f;
        if (GetSystemVolume(&current_system_volume)) {
            adjusted_system_volume = AdjustSystemVolume(percent_change);
            if (adjusted_system_volume) {
                s_system_volume_percent.store(current_system_volume + percent_change);
            }
        }
    }

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
        if (adjusted_system_volume) {
            oss << "Game volume at 100%, system volume adjusted by " << (percent_change >= 0.0f ? "+" : "")
                << percent_change << "%";
        } else {
            oss << "Volume adjusted by " << (percent_change >= 0.0f ? "+" : "") << percent_change << "% to "
                << new_volume << "%";
        }
        LogInfo(oss.str().c_str());
        return true;
    }

    return false;
}

bool GetAudioOutputDevices(std::vector<std::string>& device_names_utf8, std::vector<std::wstring>& device_ids,
                           std::wstring& current_device_id) {
    device_names_utf8.clear();
    device_ids.clear();
    current_device_id.clear();

    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool did_init = SUCCEEDED(hr);
    if (!did_init && hr != RPC_E_CHANGED_MODE) {
        LogWarn("CoInitializeEx failed for audio device enumeration");
        return false;
    }

    bool success = false;
    IMMDeviceEnumerator* device_enumerator = nullptr;
    IMMDeviceCollection* device_collection = nullptr;
    IMMDevice* default_device = nullptr;
    std::wstring default_device_full_id;

    do {
        hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&device_enumerator));
        if (FAILED(hr) || device_enumerator == nullptr) break;

        // Get system default render endpoint (for annotation)
        hr = device_enumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, &default_device);
        if (SUCCEEDED(hr) && default_device != nullptr) {
            LPWSTR id = nullptr;
            if (SUCCEEDED(default_device->GetId(&id)) && id != nullptr) {
                std::wstring default_short_id(id);
                CoTaskMemFree(id);
                default_device_full_id = BuildFullAudioDeviceId(eRender, default_short_id);
            }
        }

        // Enumerate active render endpoints
        hr = device_enumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &device_collection);
        if (FAILED(hr) || device_collection == nullptr) break;

        const std::wstring persisted_full_id = GetPersistedDefaultEndpointForCurrentProcess(eRender);

        UINT count = 0;
        device_collection->GetCount(&count);

        for (UINT i = 0; i < count; ++i) {
            IMMDevice* device = nullptr;
            if (FAILED(device_collection->Item(i, &device)) || device == nullptr) continue;

            LPWSTR id = nullptr;
            if (FAILED(device->GetId(&id)) || id == nullptr) {
                device->Release();
                continue;
            }

            std::wstring id_ws(id);
            CoTaskMemFree(id);

            IPropertyStore* prop_store = nullptr;
            std::wstring friendly_name = L"Unknown device";

            if (SUCCEEDED(device->OpenPropertyStore(STGM_READ, &prop_store)) && prop_store != nullptr) {
                PROPVARIANT var_name;
                PropVariantInit(&var_name);
                if (SUCCEEDED(prop_store->GetValue(PKEY_Device_FriendlyName, &var_name)) && var_name.vt == VT_LPWSTR
                    && var_name.pwszVal != nullptr) {
                    friendly_name.assign(var_name.pwszVal);
                }
                PropVariantClear(&var_name);
                prop_store->Release();
            }

            // Mark system default device in display name (compare using full IDs)
            bool is_system_default = false;
            if (!default_device_full_id.empty()) {
                std::wstring full_id = BuildFullAudioDeviceId(eRender, id_ws);
                is_system_default = (default_device_full_id == full_id);
            }

            std::wstring display_name = friendly_name;
            if (is_system_default) {
                display_name.append(L" (System Default)");
            }

            // Store short IDs; convert to full IDs only when persisting
            if (!persisted_full_id.empty()) {
                std::wstring full_id = BuildFullAudioDeviceId(eRender, id_ws);
                if (current_device_id.empty() && full_id == persisted_full_id) {
                    current_device_id = id_ws;
                }
            }

            device_ids.emplace_back(std::move(id_ws));
            device_names_utf8.emplace_back(WStringToUtf8(display_name));

            device->Release();
        }

        success = true;
    } while (false);

    if (default_device != nullptr) default_device->Release();
    if (device_collection != nullptr) device_collection->Release();
    if (device_enumerator != nullptr) device_enumerator->Release();
    if (did_init && hr != RPC_E_CHANGED_MODE) CoUninitialize();

    return success;
}

bool SetAudioOutputDeviceForCurrentProcess(const std::wstring& device_id) {
    // device_id is the short MMDevice ID from IMMDevice::GetId; convert to full ID
    std::wstring full_id;
    if (!device_id.empty()) {
        full_id = BuildFullAudioDeviceId(eRender, device_id);
    }

    const bool ok = SetPersistedDefaultEndpointForCurrentProcess(eRender, full_id);

    std::ostringstream oss;
    oss << "AudioOutputDevice: "
        << (device_id.empty() ? "Cleared override (System Default)" : "Set persisted default endpoint for process");
    LogInfo(oss.str().c_str());

    return ok;
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

bool SetSystemVolume(float volume_0_100) {
    float clamped = (std::max)(0.0f, (std::min)(volume_0_100, 100.0f));
    const float scalar = clamped / 100.0f;
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool did_init = SUCCEEDED(hr);
    if (!did_init && hr != RPC_E_CHANGED_MODE) {
        LogWarn("CoInitializeEx failed for system volume control");
        return false;
    }

    bool success = false;
    IMMDeviceEnumerator* device_enumerator = nullptr;
    IMMDevice* device = nullptr;
    IAudioEndpointVolume* endpoint_volume = nullptr;

    do {
        hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&device_enumerator));
        if (FAILED(hr) || device_enumerator == nullptr) break;
        hr = device_enumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, &device);
        if (FAILED(hr) || device == nullptr) break;
        hr = device->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, nullptr,
                              reinterpret_cast<void**>(&endpoint_volume));
        if (FAILED(hr) || endpoint_volume == nullptr) break;
        hr = endpoint_volume->SetMasterVolumeLevelScalar(scalar, nullptr);
        success = SUCCEEDED(hr);
    } while (false);

    if (endpoint_volume != nullptr) endpoint_volume->Release();
    if (device != nullptr) device->Release();
    if (device_enumerator != nullptr) device_enumerator->Release();
    if (did_init && hr != RPC_E_CHANGED_MODE) CoUninitialize();

    if (success) {
        std::ostringstream oss;
        oss << "System volume set to " << clamped << "%";
        LogInfo(oss.str().c_str());
    }
    return success;
}

bool GetSystemVolume(float* volume_0_100_out) {
    g_continuous_monitoring_section.store("volume:system:entry", std::memory_order_release);
    if (volume_0_100_out == nullptr) {
        return false;
    }

    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool did_init = SUCCEEDED(hr);
    if (!did_init && hr != RPC_E_CHANGED_MODE) {
        LogWarn("CoInitializeEx failed for system volume query");
        return false;
    }

    bool success = false;
    IMMDeviceEnumerator* device_enumerator = nullptr;
    IMMDevice* device = nullptr;
    IAudioEndpointVolume* endpoint_volume = nullptr;

    do {
        hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&device_enumerator));
        if (FAILED(hr) || device_enumerator == nullptr) break;
        hr = device_enumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, &device);
        if (FAILED(hr) || device == nullptr) break;
        hr = device->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, nullptr,
                              reinterpret_cast<void**>(&endpoint_volume));
        if (FAILED(hr) || endpoint_volume == nullptr) break;
        float scalar = 0.0f;
        if (SUCCEEDED(endpoint_volume->GetMasterVolumeLevelScalar(&scalar))) {
            *volume_0_100_out = scalar * 100.0f;
            success = true;
        }
    } while (false);

    if (endpoint_volume != nullptr) endpoint_volume->Release();
    if (device != nullptr) device->Release();
    if (device_enumerator != nullptr) device_enumerator->Release();
    if (did_init && hr != RPC_E_CHANGED_MODE) CoUninitialize();

    return success;
}

bool GetAudioMeterChannelCount(unsigned int* channel_count_out) {
    if (channel_count_out == nullptr) {
        return false;
    }
    *channel_count_out = 0;

    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool did_init = SUCCEEDED(hr);
    if (!did_init && hr != RPC_E_CHANGED_MODE) {
        LogWarn("CoInitializeEx failed for audio meter");
        return false;
    }

    bool success = false;
    IMMDeviceEnumerator* device_enumerator = nullptr;
    IMMDevice* device = nullptr;
    IAudioMeterInformation* meter = nullptr;

    do {
        hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&device_enumerator));
        if (FAILED(hr) || device_enumerator == nullptr) break;
        hr = device_enumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, &device);
        if (FAILED(hr) || device == nullptr) break;
        hr = device->Activate(__uuidof(IAudioMeterInformation), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(&meter));
        if (FAILED(hr) || meter == nullptr) break;
        UINT32 n = 0;
        if (SUCCEEDED(meter->GetMeteringChannelCount(&n))) {
            *channel_count_out = n;
            success = true;
        }
    } while (false);

    if (meter != nullptr) meter->Release();
    if (device != nullptr) device->Release();
    if (device_enumerator != nullptr) device_enumerator->Release();
    if (did_init && hr != RPC_E_CHANGED_MODE) CoUninitialize();

    return success;
}

bool GetAudioMeterPeakValues(unsigned int channel_count, float* peak_values_0_1_out) {
    if (peak_values_0_1_out == nullptr || channel_count == 0) {
        return false;
    }

    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool did_init = SUCCEEDED(hr);
    if (!did_init && hr != RPC_E_CHANGED_MODE) {
        LogWarn("CoInitializeEx failed for audio meter peaks");
        return false;
    }

    bool success = false;
    IMMDeviceEnumerator* device_enumerator = nullptr;
    IMMDevice* device = nullptr;
    IAudioMeterInformation* meter = nullptr;

    do {
        hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&device_enumerator));
        if (FAILED(hr) || device_enumerator == nullptr) break;
        hr = device_enumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, &device);
        if (FAILED(hr) || device == nullptr) break;
        hr = device->Activate(__uuidof(IAudioMeterInformation), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(&meter));
        if (FAILED(hr) || meter == nullptr) break;
        hr = meter->GetChannelsPeakValues(channel_count, peak_values_0_1_out);
        success = SUCCEEDED(hr);
    } while (false);

    if (meter != nullptr) meter->Release();
    if (device != nullptr) device->Release();
    if (device_enumerator != nullptr) device_enumerator->Release();
    if (did_init && hr != RPC_E_CHANGED_MODE) CoUninitialize();

    return success;
}

bool AdjustSystemVolume(float percent_change) {
    float current_volume = 0.0f;
    if (!GetSystemVolume(&current_volume)) {
        return false;
    }

    float new_volume = current_volume + percent_change;
    // Clamp to valid range
    new_volume = (std::max)(0.0f, (std::min)(new_volume, 100.0f));

    if (SetSystemVolume(new_volume)) {
        std::ostringstream oss;
        oss << "System volume adjusted by " << (percent_change >= 0.0f ? "+" : "") << percent_change << "% to "
            << new_volume << "%";
        LogInfo(oss.str().c_str());
        return true;
    }

    return false;
}

// Speaker channel mask constants (same as KSAUDIO_SPEAKER_* in ksmedia.h) for decoding channel config.
namespace {
constexpr DWORD kSpeakerFrontLeft = 0x1;
constexpr DWORD kSpeakerFrontRight = 0x2;
constexpr DWORD kSpeakerFrontCenter = 0x4;
constexpr DWORD kSpeakerLowFrequency = 0x8;
constexpr DWORD kSpeakerBackLeft = 0x10;
constexpr DWORD kSpeakerBackRight = 0x20;
constexpr DWORD kSpeakerSideLeft = 0x200;
constexpr DWORD kSpeakerSideRight = 0x400;
constexpr DWORD kMaskStereo = kSpeakerFrontLeft | kSpeakerFrontRight;
constexpr DWORD kMask51 =
    kMaskStereo | kSpeakerFrontCenter | kSpeakerLowFrequency | kSpeakerBackLeft | kSpeakerBackRight;
constexpr DWORD kMask71 = kMask51 | kSpeakerSideLeft | kSpeakerSideRight;

// Format a GUID as "{xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx}" for raw format dump.
std::string FormatGuidUtf8(const GUID& g) {
    char buf[40];
    (void)std::snprintf(
        buf, sizeof(buf), "{%08lX-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}", static_cast<unsigned long>(g.Data1),
        static_cast<unsigned>(g.Data2), static_cast<unsigned>(g.Data3), static_cast<unsigned>(g.Data4[0]),
        static_cast<unsigned>(g.Data4[1]), static_cast<unsigned>(g.Data4[2]), static_cast<unsigned>(g.Data4[3]),
        static_cast<unsigned>(g.Data4[4]), static_cast<unsigned>(g.Data4[5]), static_cast<unsigned>(g.Data4[6]),
        static_cast<unsigned>(g.Data4[7]));
    return std::string(buf);
}
}  // namespace

// Maps format tag to a short label for UI (extension/codec: Dolby, DTS, PCM, Float, etc.).
static std::string FormatTagToExtensionDisplayString(DWORD tag) {
    switch (tag) {
        case WAVE_FORMAT_PCM:        return "PCM";
        case WAVE_FORMAT_ADPCM:      return "ADPCM";
        case WAVE_FORMAT_IEEE_FLOAT: return "Float";
        case WAVE_FORMAT_ALAW:       return "ALaw";
        case WAVE_FORMAT_MULAW:      return "MuLaw";
        case WAVE_FORMAT_EXTENSIBLE: return "Extensible";
        case 0x2000:  // WAVE_FORMAT_DOLBY_AC3
            return "Dolby AC3";
        case 0x2001:  // WAVE_FORMAT_DTS
            return "DTS";
        case 0x0011:  // WAVE_FORMAT_DVI_ADPCM / IMA_ADPCM
            return "IMA ADPCM";
        default: {
            char buf[24];
            (void)std::snprintf(buf, sizeof(buf), "0x%04lX", static_cast<unsigned long>(tag & 0xFFFFu));
            return std::string(buf);
        }
    }
}

// Maps wFormatTag (WAVEFORMATEX) or SubFormat.Data1 (WAVEFORMATEXTENSIBLE) to full format constant name.
// Covers standard WAVE_FORMAT_* from mmreg.h / ksmedia.h.
static std::string FormatTagToDisplayString(DWORD tag) {
    switch (tag) {
        case WAVE_FORMAT_PCM:        return "WAVE_FORMAT_PCM";
        case WAVE_FORMAT_ADPCM:      return "WAVE_FORMAT_ADPCM";
        case WAVE_FORMAT_IEEE_FLOAT: return "WAVE_FORMAT_IEEE_FLOAT";
        case WAVE_FORMAT_ALAW:       return "WAVE_FORMAT_ALAW";
        case WAVE_FORMAT_MULAW:      return "WAVE_FORMAT_MULAW";
        case WAVE_FORMAT_EXTENSIBLE: return "WAVE_FORMAT_EXTENSIBLE";
        case 0x0008:  // WAVE_FORMAT_DRM
            return "WAVE_FORMAT_DRM";
        case 0x0009: return "WAVE_FORMAT_DRM";
        case 0x0010:  // WAVE_FORMAT_OKI_ADPCM
            return "WAVE_FORMAT_OKI_ADPCM";
        case 0x0011:  // WAVE_FORMAT_DVI_ADPCM / IMA_ADPCM
            return "WAVE_FORMAT_DVI_ADPCM";
        case 0x0012:  // WAVE_FORMAT_MEDIASPACE_ADPCM
            return "WAVE_FORMAT_MEDIASPACE_ADPCM";
        case 0x0013:  // WAVE_FORMAT_SIERRA_ADPCM
            return "WAVE_FORMAT_SIERRA_ADPCM";
        case 0x0014:  // WAVE_FORMAT_G723_ADPCM
            return "WAVE_FORMAT_G723_ADPCM";
        case 0x0015:  // WAVE_FORMAT_DIGISTD
            return "WAVE_FORMAT_DIGISTD";
        case 0x0016:  // WAVE_FORMAT_DIGIFIX
            return "WAVE_FORMAT_DIGIFIX";
        case 0x0017:  // WAVE_FORMAT_DIALOGIC_OKI_ADPCM
            return "WAVE_FORMAT_DIALOGIC_OKI_ADPCM";
        case 0x2000:  // WAVE_FORMAT_DOLBY_AC3
            return "WAVE_FORMAT_DOLBY_AC3";
        case 0x2001:  // WAVE_FORMAT_DTS
            return "WAVE_FORMAT_DTS";
        case 0x0000:  // WAVE_FORMAT_UNKNOWN
            return "WAVE_FORMAT_UNKNOWN";
        default: {
            char buf[40];
            (void)std::snprintf(buf, sizeof(buf), "WAVE_FORMAT_0x%04lX", static_cast<unsigned long>(tag & 0xFFFFu));
            return std::string(buf);
        }
    }
}

bool GetDefaultAudioDeviceFormatInfo(AudioDeviceFormatInfo* out) {
    if (out == nullptr) {
        return false;
    }
    *out = AudioDeviceFormatInfo{};

    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool did_init = SUCCEEDED(hr);
    if (!did_init && hr != RPC_E_CHANGED_MODE) {
        LogWarn("CoInitializeEx failed for audio device format info");
        return false;
    }

    bool success = false;
    IMMDeviceEnumerator* device_enumerator = nullptr;
    IMMDevice* device = nullptr;
    IAudioClient* audio_client = nullptr;
    WAVEFORMATEX* mix_format = nullptr;

    do {
        hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&device_enumerator));
        if (FAILED(hr) || device_enumerator == nullptr) break;
        hr = device_enumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, &device);
        if (FAILED(hr) || device == nullptr) break;

        // Default render device friendly name (e.g. "Speakers (Dolby Atmos)")
        IPropertyStore* prop_store = nullptr;
        if (SUCCEEDED(device->OpenPropertyStore(STGM_READ, &prop_store)) && prop_store != nullptr) {
            PROPVARIANT var_name;
            PropVariantInit(&var_name);
            if (SUCCEEDED(prop_store->GetValue(PKEY_Device_FriendlyName, &var_name)) && var_name.vt == VT_LPWSTR
                && var_name.pwszVal != nullptr) {
                out->device_friendly_name_utf8 = WStringToUtf8(var_name.pwszVal);
            }
            PropVariantClear(&var_name);
            prop_store->Release();
        }

        hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(&audio_client));
        if (FAILED(hr) || audio_client == nullptr) break;
        hr = audio_client->GetMixFormat(&mix_format);
        if (FAILED(hr) || mix_format == nullptr) break;

        out->channel_count = mix_format->nChannels;
        out->sample_rate_hz = mix_format->nSamplesPerSec;
        out->bits_per_sample = mix_format->wBitsPerSample;

        DWORD channel_mask = 0;
        bool is_extensible = (mix_format->wFormatTag == WAVE_FORMAT_EXTENSIBLE && mix_format->cbSize >= 22);
        if (is_extensible) {
            const WAVEFORMATEXTENSIBLE* we = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(mix_format);
            channel_mask = we->dwChannelMask;
            out->bits_per_sample =
                we->Samples.wValidBitsPerSample != 0 ? we->Samples.wValidBitsPerSample : mix_format->wBitsPerSample;
            out->format_tag_utf8 = FormatTagToDisplayString(we->SubFormat.Data1);
            out->format_extension_utf8 = FormatTagToExtensionDisplayString(we->SubFormat.Data1);
        } else {
            DWORD tag = static_cast<DWORD>(mix_format->wFormatTag);
            out->format_tag_utf8 = FormatTagToDisplayString(tag);
            out->format_extension_utf8 = FormatTagToExtensionDisplayString(tag);
            // No mask; infer from channel count
            if (out->channel_count == 1) {
                channel_mask = kSpeakerFrontCenter;
            } else if (out->channel_count == 2) {
                channel_mask = kMaskStereo;
            }
        }

        if (channel_mask == kMaskStereo || (out->channel_count == 2 && channel_mask == 0)) {
            out->channel_config_utf8 = "Stereo";
        } else if (channel_mask == kMask51 || (out->channel_count == 6 && channel_mask == 0)) {
            out->channel_config_utf8 = "5.1";
        } else if (channel_mask == kMask71 || (out->channel_count == 8 && channel_mask == 0)) {
            out->channel_config_utf8 = "7.1";
        } else if (out->channel_count == 1) {
            out->channel_config_utf8 = "Mono";
        } else {
            out->channel_config_utf8 = std::to_string(out->channel_count) + " ch";
        }

        // Raw format string for tooltip (WAVEFORMATEX / WAVEFORMATEXTENSIBLE field dump)
        std::ostringstream raw;
        raw << "nChannels=" << mix_format->nChannels << ", nSamplesPerSec=" << mix_format->nSamplesPerSec
            << ", wBitsPerSample=" << mix_format->wBitsPerSample << ", nBlockAlign=" << mix_format->nBlockAlign
            << ", nAvgBytesPerSec=" << mix_format->nAvgBytesPerSec << ", cbSize=" << mix_format->cbSize;
        if (is_extensible) {
            const WAVEFORMATEXTENSIBLE* we = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(mix_format);
            raw << ", wFormatTag=0x" << std::hex << mix_format->wFormatTag << std::dec
                << ", SubFormat=" << FormatGuidUtf8(we->SubFormat) << ", dwChannelMask=0x" << std::hex
                << we->dwChannelMask << std::dec << ", wValidBitsPerSample=" << we->Samples.wValidBitsPerSample;
            // WAVEFORMATEXTENSIBLE_IEC61937: cbSize 34 means 12 extra bytes after WAVEFORMATEXTENSIBLE
            if (mix_format->cbSize >= 34) {
                const auto* base = reinterpret_cast<const char*>(mix_format);
                const DWORD enc_sps = *reinterpret_cast<const DWORD*>(base + 40);
                const DWORD enc_ch = *reinterpret_cast<const DWORD*>(base + 44);
                const DWORD enc_bps = *reinterpret_cast<const DWORD*>(base + 48);
                raw << " [IEC61937] dwEncodedSamplesPerSec=" << enc_sps << ", dwEncodedChannelCount=" << enc_ch
                    << ", dwAverageBytesPerSec=" << enc_bps;
            }
        } else {
            raw << ", wFormatTag=0x" << std::hex << static_cast<unsigned>(mix_format->wFormatTag) << std::dec;
        }
        out->raw_format_utf8 = raw.str();

        success = true;
    } while (false);

    if (mix_format != nullptr) {
        CoTaskMemFree(mix_format);
    }
    if (audio_client != nullptr) audio_client->Release();
    if (device != nullptr) device->Release();
    if (device_enumerator != nullptr) device_enumerator->Release();
    if (did_init && hr != RPC_E_CHANGED_MODE) CoUninitialize();

    return success;
}
