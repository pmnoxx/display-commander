#include "audio_management.hpp"
#include "audio_device_policy.hpp"
#include "../addon.hpp"
#include "../settings/main_tab_settings.hpp"
#include "../utils.hpp"
#include "../utils/logging.hpp"
#include "../utils/timing.hpp"
#include "../globals.hpp"
#include <Functiondiscoverykeys_devpkey.h>
#include <mmdeviceapi.h>
#include <propvarutil.h>
#include <sstream>
#include <thread>

namespace {

// Helper to convert UTF-16 (wstring) to UTF-8 std::string
std::string WStringToUtf8(const std::wstring &wstr) {
    if (wstr.empty())
        return std::string();

    int size = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (size <= 0)
        return std::string();

    std::string result(static_cast<size_t>(size - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, &result[0], size, nullptr, nullptr);
    return result;
}

// Dynamic Windows Runtime function typedefs (avoid extra link deps)
using RoGetActivationFactory_pfn = HRESULT(WINAPI *)(HSTRING activatableClassId, REFIID iid, void **factory);
using WindowsCreateStringReference_pfn = HRESULT(WINAPI *)(PCWSTR sourceString, UINT32 length, HSTRING_HEADER *hstringHeader, HSTRING *string);
using WindowsDeleteString_pfn = HRESULT(WINAPI *)(HSTRING string);
using WindowsCreateString_pfn = HRESULT(WINAPI *)(PCWSTR sourceString, UINT32 length, HSTRING *string);
using WindowsGetStringRawBuffer_pfn = PCWSTR(WINAPI *)(HSTRING string, UINT32 *length);

// Helper to build full MMDevice endpoint ID like Special K (short ID -> full ID)
std::wstring BuildFullAudioDeviceId(EDataFlow flow, const std::wstring &short_id) {
    static const wchar_t *DEVICE_PREFIX = LR"(\\?\SWD#MMDEVAPI#)";
    static const wchar_t *RENDER_POSTFIX = L"#{e6327cad-dcec-4949-ae8a-991e976a79d2}";
    static const wchar_t *CAPTURE_POSTFIX = L"#{2eef81be-33fa-4800-9670-1cd474972c3f}";

    const wchar_t *postfix = (flow == eRender) ? RENDER_POSTFIX : CAPTURE_POSTFIX;

    std::wstring full;
    full.reserve(wcslen(DEVICE_PREFIX) + short_id.size() + wcslen(postfix));
    full.append(DEVICE_PREFIX);
    full.append(short_id);
    full.append(postfix);

    return full;
}

// Get Windows Runtime string functions from combase.dll
bool GetWindowsRuntimeStringFunctions(WindowsCreateStringReference_pfn &createStringRef,
                                       WindowsDeleteString_pfn &deleteString,
                                       WindowsCreateString_pfn &createString,
                                       WindowsGetStringRawBuffer_pfn &getStringRawBuffer) {
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
    s_deleteString = reinterpret_cast<WindowsDeleteString_pfn>(
        GetProcAddress(combase_module, "WindowsDeleteString"));
    s_createString = reinterpret_cast<WindowsCreateString_pfn>(
        GetProcAddress(combase_module, "WindowsCreateString"));
    s_getStringRawBuffer = reinterpret_cast<WindowsGetStringRawBuffer_pfn>(
        GetProcAddress(combase_module, "WindowsGetStringRawBuffer"));

    if (s_createStringRef == nullptr || s_deleteString == nullptr || s_createString == nullptr ||
        s_getStringRawBuffer == nullptr) {
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

IAudioPolicyConfigFactory *GetAudioPolicyConfigFactory() {
    static IAudioPolicyConfigFactory *s_factory = nullptr;
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

    const wchar_t *name = L"Windows.Media.Internal.AudioPolicyConfig";
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

    IAudioPolicyConfigFactory *factory = nullptr;
    hr = ro_get_activation_factory(hClassName, __uuidof(IAudioPolicyConfigFactory),
                                   reinterpret_cast<void **>(&factory));

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
    IAudioPolicyConfigFactory *factory = GetAudioPolicyConfigFactory();
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
    HRESULT hr = factory->GetPersistedDefaultAudioEndpoint(GetCurrentProcessId(), flow,
                                                           eMultimedia | eConsole, &hDeviceId);
    if (FAILED(hr) || hDeviceId == nullptr) {
        return std::wstring();
    }

    UINT32 len = 0;
    const wchar_t *buffer = getStringRawBuffer(hDeviceId, &len);
    std::wstring result;
    if (buffer != nullptr && len > 0) {
        result.assign(buffer, buffer + len);
    }

    deleteString(hDeviceId);
    return result;
}

bool SetPersistedDefaultEndpointForCurrentProcess(EDataFlow flow, const std::wstring &device_id) {
    IAudioPolicyConfigFactory *factory = GetAudioPolicyConfigFactory();
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
        HRESULT hr_create =
            createString(device_id.c_str(), static_cast<UINT32>(device_id.length()), &hDeviceId);
        if (FAILED(hr_create)) {
            LogWarn("AudioPolicyConfig: WindowsCreateString failed for device id");
            return false;
        }
    }

    const UINT pid = GetCurrentProcessId();

    HRESULT hr_console =
        factory->SetPersistedDefaultAudioEndpoint(pid, flow, eConsole, hDeviceId);
    HRESULT hr_multimedia =
        factory->SetPersistedDefaultAudioEndpoint(pid, flow, eMultimedia, hDeviceId);

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

} // namespace

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

bool GetAudioOutputDevices(std::vector<std::string> &device_names_utf8,
                           std::vector<std::wstring> &device_ids,
                           std::wstring &current_device_id) {
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
    IMMDeviceEnumerator *device_enumerator = nullptr;
    IMMDeviceCollection *device_collection = nullptr;
    IMMDevice *default_device = nullptr;
    std::wstring default_device_full_id;

    do {
        hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                              IID_PPV_ARGS(&device_enumerator));
        if (FAILED(hr) || device_enumerator == nullptr)
            break;

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
        if (FAILED(hr) || device_collection == nullptr)
            break;

        const std::wstring persisted_full_id = GetPersistedDefaultEndpointForCurrentProcess(eRender);

        UINT count = 0;
        device_collection->GetCount(&count);

        for (UINT i = 0; i < count; ++i) {
            IMMDevice *device = nullptr;
            if (FAILED(device_collection->Item(i, &device)) || device == nullptr)
                continue;

            LPWSTR id = nullptr;
            if (FAILED(device->GetId(&id)) || id == nullptr) {
                device->Release();
                continue;
            }

            std::wstring id_ws(id);
            CoTaskMemFree(id);

            IPropertyStore *prop_store = nullptr;
            std::wstring friendly_name = L"Unknown device";

            if (SUCCEEDED(device->OpenPropertyStore(STGM_READ, &prop_store)) && prop_store != nullptr) {
                PROPVARIANT var_name;
                PropVariantInit(&var_name);
                if (SUCCEEDED(prop_store->GetValue(PKEY_Device_FriendlyName, &var_name)) &&
                    var_name.vt == VT_LPWSTR && var_name.pwszVal != nullptr) {
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

    if (default_device != nullptr)
        default_device->Release();
    if (device_collection != nullptr)
        device_collection->Release();
    if (device_enumerator != nullptr)
        device_enumerator->Release();
    if (did_init && hr != RPC_E_CHANGED_MODE)
        CoUninitialize();

    return success;
}

bool SetAudioOutputDeviceForCurrentProcess(const std::wstring &device_id) {
    // device_id is the short MMDevice ID from IMMDevice::GetId; convert to full ID
    std::wstring full_id;
    if (!device_id.empty()) {
        full_id = BuildFullAudioDeviceId(eRender, device_id);
    }

    const bool ok = SetPersistedDefaultEndpointForCurrentProcess(eRender, full_id);

    std::ostringstream oss;
    oss << "AudioOutputDevice: "
        << (device_id.empty() ? "Cleared override (System Default)"
                              : "Set persisted default endpoint for process");
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
