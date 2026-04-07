// Source Code <Display Commander> // follow this order for includes in all files + add this comment at the top
#include "nvidia_profile_inspector.hpp"

#include "../../nvapi/nvapi_init.hpp"
#include "../../nvapi/nvapi_loader.hpp"
#include "../../utils/general_utils.hpp"
#include "../../utils/srwlock_wrapper.hpp"
#include "../../utils/string_utils.hpp"

// Libraries <standard C++>
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>

// Libraries <Windows.h> — before other Windows headers
#include <Windows.h>

namespace display_commander::features::nvidia_profile_inspector {

namespace {

SRWLOCK g_refresh_lock = SRWLOCK_INIT;
SRWLOCK g_autocreate_status_lock = SRWLOCK_INIT;
std::atomic<unsigned long long> g_last_refresh_ms{0};
std::atomic<std::shared_ptr<const DriverDlssRenderPresetSnapshot>> g_snapshot{};
std::atomic<bool> g_auto_create_attempted{false};
std::atomic<bool> g_auto_create_succeeded{false};
std::atomic<bool> g_auto_create_created_profile{false};
std::string g_auto_create_message;

inline const nvapi_loader::NvApiPtrs* NvPtrs() { return nvapi_loader::Ptrs(); }

std::wstring NormalizePath(const std::wstring& s) {
    std::wstring r = s;
    for (wchar_t& c : r) {
        if (c == L'\\') {
            c = L'/';
        }
        if (c >= L'A' && c <= L'Z') {
            c = static_cast<wchar_t>(c - L'A' + L'a');
        }
    }
    return r;
}

std::wstring GetProfileNameFromExePath(const std::wstring& exe_path) {
    if (exe_path.empty()) {
        return L"Display Commander Auto";
    }
    size_t name_pos = exe_path.find_last_of(L"\\/");
    if (name_pos == std::wstring::npos) {
        name_pos = 0;
    } else {
        name_pos += 1;
    }
    std::wstring file_name = exe_path.substr(name_pos);
    if (file_name.empty()) {
        return L"Display Commander Auto";
    }
    const size_t dot_pos = file_name.find_last_of(L'.');
    if (dot_pos != std::wstring::npos && dot_pos > 0) {
        file_name.resize(dot_pos);
    }
    return file_name.empty() ? L"Display Commander Auto" : file_name;
}

void WideToNvApiUnicode(const std::wstring& src, NvAPI_UnicodeString& dest) {
    memset(&dest, 0, sizeof(dest));
    const size_t toCopy = (std::min)(src.size(), static_cast<size_t>(NVAPI_UNICODE_STRING_MAX - 1));
    if (toCopy > 0) {
        memcpy(dest, src.c_str(), toCopy * sizeof(NvU16));
    }
}

std::string FormatNvapiError(const nvapi_loader::NvApiPtrs* p, const char* step, NvAPI_Status st) {
    char hx[16];
    (void)snprintf(hx, sizeof(hx), "%x", static_cast<unsigned>(st));
    NvAPI_ShortString buf = {};
    if (p != nullptr && p->GetErrorMessage != nullptr && p->GetErrorMessage(st, buf) == NVAPI_OK && buf[0] != '\0') {
        return std::string(step) + ": " + std::string(reinterpret_cast<const char*>(buf)) + " (0x" + hx + ")";
    }
    return std::string(step) + ": NVAPI 0x" + hx;
}

const char* LabelRenderPresetValue(NvU32 v) {
    switch (v) {
        case NGX_DLSS_SR_OVERRIDE_RENDER_PRESET_SELECTION_OFF:
            return "Off";
        case NGX_DLSS_SR_OVERRIDE_RENDER_PRESET_SELECTION_RENDER_PRESET_A:
            return "Preset A";
        case NGX_DLSS_SR_OVERRIDE_RENDER_PRESET_SELECTION_RENDER_PRESET_B:
            return "Preset B";
        case NGX_DLSS_SR_OVERRIDE_RENDER_PRESET_SELECTION_RENDER_PRESET_C:
            return "Preset C";
        case NGX_DLSS_SR_OVERRIDE_RENDER_PRESET_SELECTION_RENDER_PRESET_D:
            return "Preset D";
        case NGX_DLSS_SR_OVERRIDE_RENDER_PRESET_SELECTION_RENDER_PRESET_E:
            return "Preset E";
        case NGX_DLSS_SR_OVERRIDE_RENDER_PRESET_SELECTION_RENDER_PRESET_F:
            return "Preset F";
        case NGX_DLSS_SR_OVERRIDE_RENDER_PRESET_SELECTION_RENDER_PRESET_G:
            return "Preset G";
        case NGX_DLSS_SR_OVERRIDE_RENDER_PRESET_SELECTION_RENDER_PRESET_H:
            return "Preset H";
        case NGX_DLSS_SR_OVERRIDE_RENDER_PRESET_SELECTION_RENDER_PRESET_I:
            return "Preset I";
        case NGX_DLSS_SR_OVERRIDE_RENDER_PRESET_SELECTION_RENDER_PRESET_J:
            return "Preset J";
        case NGX_DLSS_SR_OVERRIDE_RENDER_PRESET_SELECTION_RENDER_PRESET_K:
            return "Preset K";
        case NGX_DLSS_SR_OVERRIDE_RENDER_PRESET_SELECTION_RENDER_PRESET_L:
            return "Preset L";
        case NGX_DLSS_SR_OVERRIDE_RENDER_PRESET_SELECTION_RENDER_PRESET_M:
            return "Preset M";
        case NGX_DLSS_SR_OVERRIDE_RENDER_PRESET_SELECTION_RENDER_PRESET_N:
            return "Preset N";
        case NGX_DLSS_SR_OVERRIDE_RENDER_PRESET_SELECTION_RENDER_PRESET_O:
            return "Preset O";
        case NGX_DLSS_SR_OVERRIDE_RENDER_PRESET_SELECTION_RENDER_PRESET_Latest:
            return "Latest";
        default:
            return nullptr;
    }
}

std::string FormatPresetOrHex(NvU32 v) {
    const char* lab = LabelRenderPresetValue(v);
    if (lab != nullptr) {
        return std::string(lab);
    }
    char buf[48];
    snprintf(buf, sizeof(buf), "0x%08x (%u)", static_cast<unsigned>(v), static_cast<unsigned>(v));
    return std::string(buf);
}

bool FindApplicationByPathForCurrentExe(NvDRSSessionHandle hSession, NvDRSProfileHandle* phProfile,
                                        NVDRS_APPLICATION* pApp, const nvapi_loader::NvApiPtrs* p) {
    if (!phProfile || !pApp || p == nullptr || p->DRS_FindApplicationByName == nullptr) {
        return false;
    }
    *phProfile = nullptr;
    memset(pApp, 0, sizeof(*pApp));
    pApp->version = NVDRS_APPLICATION_VER;

    std::wstring exePath = GetCurrentProcessPathW();
    if (exePath.empty()) {
        return false;
    }
    NvAPI_UnicodeString fullPathBuf{};
    WideToNvApiUnicode(NormalizePath(exePath), fullPathBuf);
    NvAPI_Status st = p->DRS_FindApplicationByName(hSession, fullPathBuf, phProfile, pApp);
    return (st == NVAPI_OK && *phProfile != nullptr);
}

void SetAutoCreateStatus(bool succeeded, bool created_profile, const std::string& message) {
    g_auto_create_succeeded.store(succeeded, std::memory_order_release);
    g_auto_create_created_profile.store(created_profile, std::memory_order_release);
    {
        ::utils::SRWLockExclusive lock(g_autocreate_status_lock);
        g_auto_create_message = message;
    }
}

bool TryAutoCreateProfileForCurrentExe(std::string* out_message, bool* out_created_profile) {
    if (out_message == nullptr || out_created_profile == nullptr) {
        return false;
    }
    *out_message = "Unknown error";
    *out_created_profile = false;

    std::wstring exePath = GetCurrentProcessPathW();
    if (exePath.empty()) {
        *out_message = "GetModuleFileName failed";
        return false;
    }
    const std::wstring normalizedExePath = NormalizePath(exePath);

    if (!nvapi::EnsureNvApiInitialized()) {
        *out_message = "NVAPI not initialized";
        return false;
    }
    const nvapi_loader::NvApiPtrs* p = NvPtrs();
    if (p == nullptr || !nvapi_loader::IsLoaded()) {
        *out_message = "NVAPI not loaded";
        return false;
    }
    if (p->DRS_CreateSession == nullptr || p->DRS_DestroySession == nullptr || p->DRS_LoadSettings == nullptr
        || p->DRS_FindApplicationByName == nullptr || p->DRS_CreateProfile == nullptr
        || p->DRS_CreateApplication == nullptr || p->DRS_SaveSettings == nullptr) {
        *out_message = "NVAPI DRS create entry points missing";
        return false;
    }

    NvDRSSessionHandle hSession = nullptr;
    NvAPI_Status st = p->DRS_CreateSession(&hSession);
    if (st != NVAPI_OK) {
        *out_message = FormatNvapiError(p, "DRS_CreateSession", st);
        return false;
    }

    auto destroy_session = [&]() {
        if (hSession != nullptr) {
            p->DRS_DestroySession(hSession);
            hSession = nullptr;
        }
    };

    st = p->DRS_LoadSettings(hSession);
    if (st != NVAPI_OK) {
        *out_message = FormatNvapiError(p, "DRS_LoadSettings", st);
        destroy_session();
        return false;
    }

    NvDRSProfileHandle hExistingProfile = nullptr;
    NVDRS_APPLICATION existingApp{};
    if (FindApplicationByPathForCurrentExe(hSession, &hExistingProfile, &existingApp, p)) {
        *out_message = "Profile already exists for this executable";
        destroy_session();
        return true;
    }

    NVDRS_PROFILE profileInfo{};
    profileInfo.version = NVDRS_PROFILE_VER;
    const std::wstring profile_name = GetProfileNameFromExePath(exePath);
    WideToNvApiUnicode(profile_name, profileInfo.profileName);

    NvDRSProfileHandle hProfile = nullptr;
    st = p->DRS_CreateProfile(hSession, &profileInfo, &hProfile);
    if (st != NVAPI_OK || hProfile == nullptr) {
        *out_message = FormatNvapiError(p, "DRS_CreateProfile", st);
        destroy_session();
        return false;
    }

    NVDRS_APPLICATION app{};
    app.version = NVDRS_APPLICATION_VER;
    WideToNvApiUnicode(normalizedExePath, app.appName);
    st = p->DRS_CreateApplication(hSession, hProfile, &app);
    if (st != NVAPI_OK) {
        *out_message = FormatNvapiError(p, "DRS_CreateApplication", st);
        destroy_session();
        return false;
    }

    st = p->DRS_SaveSettings(hSession);
    if (st != NVAPI_OK) {
        *out_message = FormatNvapiError(p, "DRS_SaveSettings", st);
        destroy_session();
        return false;
    }

    destroy_session();
    *out_created_profile = true;
    *out_message = "Created DRS profile and app binding for current executable";
    return true;
}

std::shared_ptr<const DriverDlssRenderPresetSnapshot> BuildSnapshot() {
    auto out = std::make_shared<DriverDlssRenderPresetSnapshot>();
    std::wstring exePath = GetCurrentProcessPathW();
    if (exePath.empty()) {
        out->error_message = "GetModuleFileName failed";
        out->query_succeeded = false;
        return out;
    }
    out->current_exe_path_utf8 = display_commander::utils::WideToUtf8(exePath);

    if (!nvapi::EnsureNvApiInitialized()) {
        out->error_message = "NVAPI not initialized";
        out->query_succeeded = false;
        return out;
    }
    const nvapi_loader::NvApiPtrs* p = NvPtrs();
    if (p == nullptr || !nvapi_loader::IsLoaded()) {
        out->error_message = "NVAPI not loaded";
        out->query_succeeded = false;
        return out;
    }
    if (p->DRS_CreateSession == nullptr || p->DRS_DestroySession == nullptr || p->DRS_LoadSettings == nullptr
        || p->DRS_FindApplicationByName == nullptr) {
        out->error_message = "NVAPI DRS entry points missing";
        out->query_succeeded = false;
        return out;
    }

    NvDRSSessionHandle hSession = nullptr;
    NvAPI_Status st = p->DRS_CreateSession(&hSession);
    if (st != NVAPI_OK) {
        out->error_message = FormatNvapiError(p, "DRS_CreateSession", st);
        out->query_succeeded = false;
        return out;
    }
    st = p->DRS_LoadSettings(hSession);
    if (st != NVAPI_OK) {
        p->DRS_DestroySession(hSession);
        out->error_message = FormatNvapiError(p, "DRS_LoadSettings", st);
        out->query_succeeded = false;
        return out;
    }

    NvDRSProfileHandle hProfile = nullptr;
    NVDRS_APPLICATION app{};
    if (!FindApplicationByPathForCurrentExe(hSession, &hProfile, &app, p)) {
        p->DRS_DestroySession(hSession);
        out->query_succeeded = true;
        out->has_profile = false;
        out->sr_display = "Global default (Off)";
        out->rr_display = "Global default (Off)";
        out->sr_value_u32 = static_cast<std::uint32_t>(NGX_DLSS_SR_OVERRIDE_RENDER_PRESET_SELECTION_DEFAULT);
        out->rr_value_u32 = static_cast<std::uint32_t>(NGX_DLSS_RR_OVERRIDE_RENDER_PRESET_SELECTION_DEFAULT);
        return out;
    }

    NVDRS_PROFILE profileInfo{};
    profileInfo.version = NVDRS_PROFILE_VER;
    if (p->DRS_GetProfileInfo != nullptr
        && p->DRS_GetProfileInfo(hSession, hProfile, &profileInfo) == NVAPI_OK) {
        const wchar_t* wname = reinterpret_cast<const wchar_t*>(profileInfo.profileName);
        if (wname != nullptr && wname[0] != L'\0') {
            out->profile_name = display_commander::utils::WideToUtf8(wname);
        }
    }
    out->has_profile = true;

    auto readOne = [&](NvU32 settingId, NvU32 defaultVal, bool* defined_in_profile, std::uint32_t* value_out,
                       std::string* display_out, bool* is_override_out) {
        NVDRS_SETTING s{};
        s.version = NVDRS_SETTING_VER;
        NvAPI_Status gst = nvapi_loader::DRS_GetSetting(p, hSession, hProfile, settingId, &s);
        if (gst != NVAPI_OK || s.settingType != NVDRS_DWORD_TYPE) {
            *defined_in_profile = false;
            *value_out = static_cast<std::uint32_t>(defaultVal);
            *display_out = std::string("Global default (") + FormatPresetOrHex(defaultVal) + ")";
            *is_override_out = false;
            return;
        }
        *defined_in_profile = true;
        *value_out = static_cast<std::uint32_t>(s.u32CurrentValue);
        *display_out = FormatPresetOrHex(s.u32CurrentValue);
        *is_override_out = (static_cast<NvU32>(s.u32CurrentValue) != defaultVal);
    };

    readOne(NGX_DLSS_SR_OVERRIDE_RENDER_PRESET_SELECTION_ID, NGX_DLSS_SR_OVERRIDE_RENDER_PRESET_SELECTION_DEFAULT,
            &out->sr_defined_in_profile, &out->sr_value_u32, &out->sr_display, &out->sr_is_non_default_override);
    readOne(NGX_DLSS_RR_OVERRIDE_RENDER_PRESET_SELECTION_ID, NGX_DLSS_RR_OVERRIDE_RENDER_PRESET_SELECTION_DEFAULT,
            &out->rr_defined_in_profile, &out->rr_value_u32, &out->rr_display, &out->rr_is_non_default_override);

    p->DRS_DestroySession(hSession);
    out->query_succeeded = true;
    return out;
}

// Sticky cache: serve until InvalidateDriverDlssRenderPresetCache() or force_refresh (no time-based expiry).
bool ShouldServeCached(bool force, const std::shared_ptr<const DriverDlssRenderPresetSnapshot>& cur) {
    if (force) {
        return false;
    }
    if (!cur) {
        return false;
    }
    const unsigned long long last = g_last_refresh_ms.load(std::memory_order_relaxed);
    return last != 0ULL;
}

}  // namespace

std::shared_ptr<const DriverDlssRenderPresetSnapshot> GetDriverDlssRenderPresetSnapshot(bool force_refresh) {
    std::shared_ptr<const DriverDlssRenderPresetSnapshot> cur = g_snapshot.load(std::memory_order_acquire);
    if (ShouldServeCached(force_refresh, cur)) {
        return cur;
    }

    ::utils::SRWLockExclusive lock(g_refresh_lock);
    cur = g_snapshot.load(std::memory_order_acquire);
    if (ShouldServeCached(force_refresh, cur)) {
        return cur;
    }

    std::shared_ptr<const DriverDlssRenderPresetSnapshot> fresh = BuildSnapshot();
    if (fresh && fresh->query_succeeded && fresh->has_profile) {
        LogInfo("[DRS] Profile found [Path] %s [Profile] %s",
                fresh->current_exe_path_utf8.empty() ? "(unknown)" : fresh->current_exe_path_utf8.c_str(),
                fresh->profile_name.empty() ? "(unnamed)" : fresh->profile_name.c_str());
    }
    if (fresh && fresh->query_succeeded && !fresh->has_profile) {
        bool expected = false;
        if (g_auto_create_attempted.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
            std::string status_message;
            bool created_profile = false;
            const bool success = TryAutoCreateProfileForCurrentExe(&status_message, &created_profile);
            SetAutoCreateStatus(success, created_profile, status_message);
            if (success && created_profile) {
                LogInfo("[DRS] Auto-create succeeded [Path] %s [Status] %s",
                        fresh->current_exe_path_utf8.empty() ? "(unknown)" : fresh->current_exe_path_utf8.c_str(),
                        status_message.c_str());
                fresh = BuildSnapshot();
                if (fresh && fresh->query_succeeded && fresh->has_profile) {
                    LogInfo("[DRS] Profile found after auto-create [Path] %s [Profile] %s",
                            fresh->current_exe_path_utf8.empty() ? "(unknown)"
                                                                  : fresh->current_exe_path_utf8.c_str(),
                            fresh->profile_name.empty() ? "(unnamed)" : fresh->profile_name.c_str());
                }
            } else if (!success) {
                LogError("[DRS] Auto-create failed [Path] %s [Status] %s",
                         fresh->current_exe_path_utf8.empty() ? "(unknown)" : fresh->current_exe_path_utf8.c_str(),
                         status_message.c_str());
            }
        }
    }
    g_snapshot.store(fresh, std::memory_order_release);
    g_last_refresh_ms.store(GetTickCount64(), std::memory_order_relaxed);
    return fresh;
}

void InvalidateDriverDlssRenderPresetCache() { g_last_refresh_ms.store(0ULL, std::memory_order_relaxed); }

DriverDlssProfileAutoCreateStatus GetDriverDlssProfileAutoCreateStatus() {
    DriverDlssProfileAutoCreateStatus out{};
    out.attempted = g_auto_create_attempted.load(std::memory_order_acquire);
    out.succeeded = g_auto_create_succeeded.load(std::memory_order_acquire);
    out.created_profile = g_auto_create_created_profile.load(std::memory_order_acquire);
    {
        ::utils::SRWLockShared lock(g_autocreate_status_lock);
        out.message = g_auto_create_message;
    }
    return out;
}

}  // namespace display_commander::features::nvidia_profile_inspector
