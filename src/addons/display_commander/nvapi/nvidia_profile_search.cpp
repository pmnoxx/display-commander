#include "nvidia_profile_search.hpp"
#include <nvapi.h>
#include <NvApiDriverSettings.h>
#include <windows.h>
#include <algorithm>
#include <cstring>
#include <map>
#include <sstream>
#include <string>

namespace display_commander::nvapi {

namespace {

struct ImportantSettingDef {
    NvU32 id;
    const char* label;
    NvU32 default_value;  // NVIDIA driver default when not set in profile
};

static const ImportantSettingDef k_important_settings[] = {
    {VSYNCSMOOTHAFR_ID, "Smooth Motion (AFR)", VSYNCSMOOTHAFR_DEFAULT},
    {NGX_DLSS_SR_MODE_ID, "DLSS-SR mode", static_cast<NvU32>(NGX_DLSS_SR_MODE_DEFAULT)},
    {NGX_DLSS_SR_OVERRIDE_ID, "DLSS-SR override", static_cast<NvU32>(NGX_DLSS_SR_OVERRIDE_DEFAULT)},
    {NGX_DLSS_SR_OVERRIDE_RENDER_PRESET_SELECTION_ID, "DLSS-SR preset", static_cast<NvU32>(NGX_DLSS_SR_OVERRIDE_RENDER_PRESET_SELECTION_DEFAULT)},
    {NGX_DLSS_FG_OVERRIDE_ID, "DLSS-FG override", static_cast<NvU32>(NGX_DLSS_FG_OVERRIDE_DEFAULT)},
    {NGX_DLSS_RR_OVERRIDE_ID, "DLSS-RR override", static_cast<NvU32>(NGX_DLSS_RR_OVERRIDE_DEFAULT)},
    {NGX_DLSS_RR_MODE_ID, "DLSS-RR mode", static_cast<NvU32>(NGX_DLSS_RR_MODE_DEFAULT)},
    {NGX_DLSS_RR_OVERRIDE_RENDER_PRESET_SELECTION_ID, "DLSS-RR preset", static_cast<NvU32>(NGX_DLSS_RR_OVERRIDE_RENDER_PRESET_SELECTION_DEFAULT)},
    {NGX_DLAA_OVERRIDE_ID, "DLAA override", static_cast<NvU32>(NGX_DLAA_OVERRIDE_DEFAULT)},
    {VSYNCMODE_ID, "Vertical Sync", static_cast<NvU32>(VSYNCMODE_DEFAULT)},
    {VSYNCTEARCONTROL_ID, "Sync tear control", static_cast<NvU32>(VSYNCTEARCONTROL_DEFAULT)},
    {VRR_APP_OVERRIDE_ID, "G-SYNC / VRR", static_cast<NvU32>(VRR_APP_OVERRIDE_DEFAULT)},
    {VRR_MODE_ID, "G-SYNC mode", static_cast<NvU32>(VRR_MODE_DEFAULT)},
    {REFRESH_RATE_OVERRIDE_ID, "Preferred refresh rate", static_cast<NvU32>(REFRESH_RATE_OVERRIDE_DEFAULT)},
    {PRERENDERLIMIT_ID, "Max pre-rendered frames", static_cast<NvU32>(PRERENDERLIMIT_DEFAULT)},
    {PREFERRED_PSTATE_ID, "Power management", static_cast<NvU32>(PREFERRED_PSTATE_DEFAULT)},
};

static std::string FormatImportantValue(NvU32 settingId, NvU32 value) {
    switch (settingId) {
        case VSYNCSMOOTHAFR_ID:
            return (value == VSYNCSMOOTHAFR_ON) ? "On" : "Off";
        case NGX_DLSS_SR_OVERRIDE_ID:
            return (value == NGX_DLSS_SR_OVERRIDE_ON) ? "On" : "Off";
        case NGX_DLSS_FG_OVERRIDE_ID:
            return (value == NGX_DLSS_FG_OVERRIDE_ON) ? "On" : "Off";
        case NGX_DLSS_RR_OVERRIDE_ID:
            return (value == NGX_DLSS_RR_OVERRIDE_ON) ? "On" : "Off";
        case NGX_DLAA_OVERRIDE_ID:
            return (value == NGX_DLAA_OVERRIDE_DLAA_ON) ? "On" : "Default";
        case PRERENDERLIMIT_ID:
            if (value == PRERENDERLIMIT_APP_CONTROLLED) return "App controlled";
            {
                std::ostringstream o;
                o << value;
                return o.str();
            }
            break;
        case VRR_APP_OVERRIDE_ID:
            if (value == 0) return "Allow";
            if (value == 1) return "Force Off";
            if (value == 2) return "Disallow";
            if (value == 3) return "ULMB";
            if (value == 4) return "Fixed refresh";
            break;
        case VRR_MODE_ID:
            if (value == 0) return "Disabled";
            if (value == 1) return "Fullscreen only";
            if (value == 2) return "Fullscreen + windowed";
            break;
        case REFRESH_RATE_OVERRIDE_ID:
            if (value == REFRESH_RATE_OVERRIDE_APPLICATION_CONTROLLED) return "Application controlled";
            if (value == REFRESH_RATE_OVERRIDE_HIGHEST_AVAILABLE) return "Highest available";
            if ((value & REFRESH_RATE_OVERRIDE_LOW_LATENCY_RR_MASK) != 0) {
                std::ostringstream o;
                o << "Low latency (0x" << std::hex << value << ")";
                return o.str();
            }
            break;
        case NGX_DLSS_SR_MODE_ID:
            switch (value) {
                case 0: return "Performance";
                case 1: return "Balanced";
                case 2: return "Quality";
                case 3: return "Snippet controlled";
                case 4: return "DLAA";
                case 5: return "Ultra Performance";
                case 6: return "Custom";
                default: break;
            }
            break;
        case NGX_DLSS_RR_MODE_ID:
            switch (value) {
                case 0: return "Performance";
                case 1: return "Balanced";
                case 2: return "Quality";
                case 3: return "Snippet controlled";
                case 4: return "DLAA";
                case 5: return "Ultra Performance";
                case 6: return "Custom";
                default: break;
            }
            break;
        case NGX_DLSS_SR_OVERRIDE_RENDER_PRESET_SELECTION_ID:
        case NGX_DLSS_RR_OVERRIDE_RENDER_PRESET_SELECTION_ID: {
            if (value == 0) return "Off";
            if (value >= 1 && value <= 15) {
                const char presets[] = "ABCDEFGHIJKLMNO";
                return std::string("Preset ") + presets[value - 1];
            }
            if (value == 0x00ffffff) return "Latest";
            break;
        }
        case VSYNCMODE_ID:
            if (value == 0x60925292) return "Passive (app)";
            if (value == 0x08416747) return "Force Off";
            if (value == 0x47814940) return "Force On";
            if (value == 0x32610244) return "Flip 2";
            if (value == 0x71271021) return "Flip 3";
            if (value == 0x13245256) return "Flip 4";
            if (value == 0x18888888) return "Virtual";
            break;
        case VSYNCTEARCONTROL_ID:
            return (value == 0x99941284) ? "Enable" : "Disable";
        case PREFERRED_PSTATE_ID:
            if (value == 0) return "Adaptive";
            if (value == 1) return "Prefer max";
            if (value == 2) return "Driver controlled";
            if (value == 3) return "Consistent perf";
            if (value == 4) return "Prefer min";
            if (value == 5) return "Optimal power";
            break;
        default:
            break;
    }
    std::ostringstream oss;
    oss << "0x" << std::hex << value << " (" << std::dec << value << ")";
    return oss.str();
}

static void ReadImportantSettings(NvDRSSessionHandle hSession,
                                  NvDRSProfileHandle hProfile,
                                  std::vector<ImportantProfileSetting>& out) {
    for (const auto& def : k_important_settings) {
        ImportantProfileSetting entry;
        entry.label = def.label;
        NVDRS_SETTING s = {0};
        s.version = NVDRS_SETTING_VER;
        entry.default_value = def.default_value;
        if (NvAPI_DRS_GetSetting(hSession, hProfile, def.id, &s) != NVAPI_OK) {
            std::string defaultStr = FormatImportantValue(def.id, def.default_value);
            entry.value = "Not set (default: " + defaultStr + ")";
            entry.setting_id = def.id;
            entry.value_id = def.default_value;
            out.push_back(std::move(entry));
            continue;
        }
        if (s.settingType != NVDRS_DWORD_TYPE) {
            entry.value = "—";
            entry.setting_id = 0;
            entry.value_id = 0;
            out.push_back(std::move(entry));
            continue;
        }
        entry.value = FormatImportantValue(def.id, s.u32CurrentValue);
        entry.setting_id = def.id;
        entry.value_id = s.u32CurrentValue;
        out.push_back(std::move(entry));
    }
}

static std::string FormatSettingValue(const NVDRS_SETTING& s, std::string (*wideToUtf8)(const wchar_t*)) {
    if (s.settingType == NVDRS_DWORD_TYPE) {
        return FormatImportantValue(s.settingId, s.u32CurrentValue);
    }
    if (s.settingType == NVDRS_BINARY_TYPE) {
        std::ostringstream o;
        o << "(binary, " << s.binaryCurrentValue.valueLength << " bytes)";
        return o.str();
    }
    if (s.settingType == NVDRS_WSTRING_TYPE || s.settingType == NVDRS_STRING_TYPE) {
        const wchar_t* wsz = reinterpret_cast<const wchar_t*>(s.wszCurrentValue);
        std::string utf8 = wideToUtf8(wsz);
        return utf8.empty() ? "(empty)" : utf8;
    }
    return "—";
}

static std::string WideToUtf8(const wchar_t* wsz) {
    if (!wsz || !wsz[0]) {
        return {};
    }
    int len = ::WideCharToMultiByte(CP_UTF8, 0, wsz, -1, nullptr, 0, nullptr, nullptr);
    if (len <= 0) {
        return {};
    }
    std::string out(static_cast<size_t>(len), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, wsz, -1, &out[0], len, nullptr, nullptr);
    if (!out.empty() && out.back() == '\0') {
        out.pop_back();
    }
    return out;
}

static void ReadAllSettings(NvDRSSessionHandle hSession,
                            NvDRSProfileHandle hProfile,
                            std::vector<ImportantProfileSetting>& out) {
    constexpr NvU32 kBatchSize = 64;
    std::vector<NVDRS_SETTING> batch(kBatchSize);
    for (NvU32 startIndex = 0;; ) {
        for (auto& s : batch) {
            memset(&s, 0, sizeof(s));
            s.version = NVDRS_SETTING_VER;
        }
        NvU32 count = kBatchSize;
        if (NvAPI_DRS_EnumSettings(hSession, hProfile, startIndex, &count, batch.data()) != NVAPI_OK) {
            break;
        }
        if (count == 0) {
            break;
        }
        for (NvU32 i = 0; i < count; ++i) {
            const NVDRS_SETTING& s = batch[i];
            ImportantProfileSetting entry;
            entry.label = WideToUtf8(reinterpret_cast<const wchar_t*>(s.settingName));
            if (entry.label.empty()) {
                std::ostringstream o;
                o << "Setting 0x" << std::hex << s.settingId;
                entry.label = o.str();
            }
            entry.value = FormatSettingValue(s, WideToUtf8);
            out.push_back(std::move(entry));
        }
        startIndex += count;
        if (count < kBatchSize) {
            break;
        }
    }
}

// NvAPI_UnicodeString is NvU16[]; treat as UTF-16 and convert to narrow for comparison.
static std::wstring AppNameToWide(const NvAPI_UnicodeString& appName) {
    return reinterpret_cast<const wchar_t*>(appName);
}

// Copy wide string into NvAPI_UnicodeString (NvU16). Null-terminated, max NVAPI_UNICODE_STRING_MAX elements.
static void WideToNvApiUnicode(const std::wstring& src, NvAPI_UnicodeString& dest) {
    memset(&dest, 0, sizeof(dest));
    const size_t toCopy = (std::min)(src.size(), static_cast<size_t>(NVAPI_UNICODE_STRING_MAX - 1));
    if (toCopy > 0) {
        memcpy(dest, src.c_str(), toCopy * sizeof(NvU16));
    }
}

// Normalize for comparison: forward slashes, lowercase.
static std::wstring NormalizePath(const std::wstring& s) {
    std::wstring r = s;
    for (wchar_t& c : r) {
        if (c == L'\\') {
            c = L'/';
        }
        if (c >= L'A' && c <= L'Z') {
            c = c - L'A' + L'a';
        }
    }
    return r;
}

// True if profile app name matches current exe (path or base name).
static bool AppMatchesExe(const std::wstring& profileAppName,
                          const std::wstring& currentPathNorm,
                          const std::wstring& currentNameNorm) {
    if (profileAppName.empty()) {
        return false;
    }
    std::wstring appNorm = NormalizePath(profileAppName);
    if (appNorm == currentPathNorm) {
        return true;
    }
    // Profile might store only "game.exe".
    if (appNorm == currentNameNorm) {
        return true;
    }
    // Current path ends with profile app (e.g. profile has "game.exe", path ends with "\\game.exe").
    if (currentPathNorm.size() >= appNorm.size()) {
        size_t off = currentPathNorm.size() - appNorm.size();
        if (currentPathNorm.compare(off, appNorm.size(), appNorm) == 0) {
            if (off == 0 || currentPathNorm[off - 1] == L'/') {
                return true;
            }
        }
    }
    return false;
}

// Returns the first profile handle that contains the current process exe, or nullptr.
static NvDRSProfileHandle FindFirstMatchingProfile(NvDRSSessionHandle hSession) {
    wchar_t exePath[MAX_PATH] = {};
    if (::GetModuleFileNameW(nullptr, exePath, MAX_PATH) == 0) {
        return nullptr;
    }
    const wchar_t* base = wcsrchr(exePath, L'\\');
    std::wstring currentPathNorm = NormalizePath(exePath);
    std::wstring currentNameNorm = NormalizePath(base ? base + 1 : exePath);

    NvU32 numProfiles = 0;
    if (NvAPI_DRS_GetNumProfiles(hSession, &numProfiles) != NVAPI_OK) {
        return nullptr;
    }
    for (NvU32 i = 0; i < numProfiles; ++i) {
        NvDRSProfileHandle hProfile = nullptr;
        NvAPI_Status st = NvAPI_DRS_EnumProfiles(hSession, i, &hProfile);
        if (st == NVAPI_END_ENUMERATION || st != NVAPI_OK) {
            break;
        }
        NVDRS_PROFILE profileInfo = {0};
        profileInfo.version = NVDRS_PROFILE_VER;
        if (NvAPI_DRS_GetProfileInfo(hSession, hProfile, &profileInfo) != NVAPI_OK) {
            continue;
        }
        NvU32 appCount = profileInfo.numOfApps;
        if (appCount == 0) continue;
        if (appCount > 256) appCount = 256;
        std::vector<NVDRS_APPLICATION> apps(appCount);
        for (auto& a : apps) {
            memset(&a, 0, sizeof(a));
            a.version = NVDRS_APPLICATION_VER;
        }
        NvU32 returned = appCount;
        if (NvAPI_DRS_EnumApplications(hSession, hProfile, 0, &returned, apps.data()) != NVAPI_OK) {
            continue;
        }
        for (NvU32 a = 0; a < returned; ++a) {
            if (AppMatchesExe(AppNameToWide(apps[a].appName), currentPathNorm, currentNameNorm)) {
                return hProfile;
            }
        }
    }
    return nullptr;
}

}  // namespace

NvidiaProfileSearchResult SearchAllProfilesForCurrentExe() {
    NvidiaProfileSearchResult result;

    wchar_t exePath[MAX_PATH] = {};
    if (::GetModuleFileNameW(nullptr, exePath, MAX_PATH) == 0) {
        result.error = "GetModuleFileName failed";
        return result;
    }
    result.current_exe_path = WideToUtf8(exePath);
    const wchar_t* base = wcsrchr(exePath, L'\\');
    result.current_exe_name = WideToUtf8(base ? base + 1 : exePath);

    std::wstring currentPathNorm = NormalizePath(exePath);
    std::wstring currentNameNorm = NormalizePath(base ? base + 1 : exePath);

    NvDRSSessionHandle hSession = nullptr;
    NvAPI_Status status = NvAPI_DRS_CreateSession(&hSession);
    if (status != NVAPI_OK) {
        if (status == NVAPI_API_NOT_INITIALIZED) {
            result.error = "NVAPI not available (no NVIDIA GPU or not initialized)";
        } else {
            result.error = "DRS CreateSession failed";
        }
        return result;
    }

    status = NvAPI_DRS_LoadSettings(hSession);
    if (status != NVAPI_OK) {
        NvAPI_DRS_DestroySession(hSession);
        result.error = "DRS LoadSettings failed";
        return result;
    }

    NvU32 numProfiles = 0;
    status = NvAPI_DRS_GetNumProfiles(hSession, &numProfiles);
    if (status != NVAPI_OK) {
        NvAPI_DRS_DestroySession(hSession);
        result.error = "DRS GetNumProfiles failed";
        return result;
    }

    for (NvU32 i = 0; i < numProfiles; ++i) {
        NvDRSProfileHandle hProfile = nullptr;
        status = NvAPI_DRS_EnumProfiles(hSession, i, &hProfile);
        if (status == NVAPI_END_ENUMERATION || status != NVAPI_OK) {
            break;
        }

        NVDRS_PROFILE profileInfo = {0};
        profileInfo.version = NVDRS_PROFILE_VER;
        status = NvAPI_DRS_GetProfileInfo(hSession, hProfile, &profileInfo);
        std::string profileNameUtf8;
        if (status == NVAPI_OK) {
            profileNameUtf8 = WideToUtf8(reinterpret_cast<const wchar_t*>(profileInfo.profileName));
        } else {
            profileNameUtf8 = "(unknown)";
        }

        NvU32 appCount = profileInfo.numOfApps;
        if (appCount == 0) {
            continue;
        }
        if (appCount > 256) {
            appCount = 256;  // Sanity cap
        }
        std::vector<NVDRS_APPLICATION> apps(appCount);
        for (auto& a : apps) {
            memset(&a, 0, sizeof(a));
            a.version = NVDRS_APPLICATION_VER;
        }
        NvU32 returned = appCount;
        status = NvAPI_DRS_EnumApplications(hSession, hProfile, 0, &returned, apps.data());
        if (status != NVAPI_OK) {
            continue;
        }
        for (NvU32 a = 0; a < returned; ++a) {
            std::wstring appNameW = AppNameToWide(apps[a].appName);
            if (AppMatchesExe(appNameW, currentPathNorm, currentNameNorm)) {
                result.matching_profile_names.push_back(profileNameUtf8);
                if (result.matching_profile_names.size() == 1) {
                    ReadImportantSettings(hSession, hProfile, result.important_settings);
                    ReadAllSettings(hSession, hProfile, result.all_settings);
                }
                break;  // One match per profile is enough
            }
        }
    }

    NvAPI_DRS_DestroySession(hSession);
    result.success = true;
    return result;
}

static NvidiaProfileSearchResult s_cachedResult;
static std::string s_cachedExePath;
static bool s_cacheValid = false;

NvidiaProfileSearchResult GetCachedProfileSearchResult() {
    wchar_t exePath[MAX_PATH] = {};
    if (::GetModuleFileNameW(nullptr, exePath, MAX_PATH) == 0) {
        s_cacheValid = false;
        NvidiaProfileSearchResult r;
        r.error = "GetModuleFileName failed";
        return r;
    }
    std::string currentPath = WideToUtf8(exePath);
    if (s_cacheValid && s_cachedExePath == currentPath) {
        return s_cachedResult;
    }
    s_cachedResult = SearchAllProfilesForCurrentExe();
    s_cachedExePath = currentPath;
    s_cacheValid = true;
    return s_cachedResult;
}

void InvalidateProfileSearchCache() {
    s_cacheValid = false;
}

using ValueList = std::vector<std::pair<std::uint32_t, std::string>>;
static std::map<std::uint32_t, ValueList> s_availableValuesCache;

std::vector<std::pair<std::uint32_t, std::string>> GetSettingAvailableValues(std::uint32_t settingId) {
    {
        auto it = s_availableValuesCache.find(settingId);
        if (it != s_availableValuesCache.end()) {
            return it->second;
        }
    }
    ValueList list;
    NVDRS_SETTING_VALUES vals;
    memset(&vals, 0, sizeof(vals));
    vals.version = NVDRS_SETTING_VALUES_VER;
    NvU32 maxNum = NVAPI_SETTING_MAX_VALUES;
    if (NvAPI_DRS_EnumAvailableSettingValues(static_cast<NvU32>(settingId), &maxNum, &vals) != NVAPI_OK) {
        return list;
    }
    if (vals.settingType != NVDRS_DWORD_TYPE) {
        return list;
    }
    for (NvU32 i = 0; i < vals.numSettingValues && i < NVAPI_SETTING_MAX_VALUES; ++i) {
        NvU32 v = vals.settingValues[i].u32Value;
        list.push_back({static_cast<std::uint32_t>(v), FormatImportantValue(static_cast<NvU32>(settingId), v)});
    }
    s_availableValuesCache[settingId] = list;
    return list;
}

bool SetProfileSetting(std::uint32_t settingId, std::uint32_t value) {
    NvDRSSessionHandle hSession = nullptr;
    if (NvAPI_DRS_CreateSession(&hSession) != NVAPI_OK) {
        return false;
    }
    if (NvAPI_DRS_LoadSettings(hSession) != NVAPI_OK) {
        NvAPI_DRS_DestroySession(hSession);
        return false;
    }
    NvDRSProfileHandle hProfile = FindFirstMatchingProfile(hSession);
    if (!hProfile) {
        NvAPI_DRS_DestroySession(hSession);
        return false;
    }
    NVDRS_SETTING s;
    memset(&s, 0, sizeof(s));
    s.version = NVDRS_SETTING_VER;
    s.settingId = static_cast<NvU32>(settingId);
    s.settingType = NVDRS_DWORD_TYPE;
    s.u32CurrentValue = static_cast<NvU32>(value);
    NvAPI_Status st = NvAPI_DRS_SetSetting(hSession, hProfile, &s);
    if (st != NVAPI_OK) {
        NvAPI_DRS_DestroySession(hSession);
        return false;
    }
    if (NvAPI_DRS_SaveSettings(hSession) != NVAPI_OK) {
        NvAPI_DRS_DestroySession(hSession);
        return false;
    }
    NvAPI_DRS_DestroySession(hSession);
    InvalidateProfileSearchCache();
    return true;
}

std::pair<bool, std::string> CreateProfileForCurrentExe() {
    wchar_t exePath[MAX_PATH] = {};
    if (::GetModuleFileNameW(nullptr, exePath, MAX_PATH) == 0) {
        return {false, "GetModuleFileName failed"};
    }
    const wchar_t* base = wcsrchr(exePath, L'\\');
    const wchar_t* exeName = base ? base + 1 : exePath;
    std::wstring exeNameW(exeName);

    NvDRSSessionHandle hSession = nullptr;
    NvAPI_Status status = NvAPI_DRS_CreateSession(&hSession);
    if (status != NVAPI_OK) {
        if (status == NVAPI_API_NOT_INITIALIZED) {
            return {false, "NVAPI not available (NVIDIA GPU required)"};
        }
        return {false, "DRS CreateSession failed"};
    }
    if (NvAPI_DRS_LoadSettings(hSession) != NVAPI_OK) {
        NvAPI_DRS_DestroySession(hSession);
        return {false, "DRS LoadSettings failed"};
    }

    NvAPI_UnicodeString appNameBuf;
    WideToNvApiUnicode(exeNameW, appNameBuf);

    NVDRS_APPLICATION app = {0};
    app.version = NVDRS_APPLICATION_VER;
    WideToNvApiUnicode(exeNameW, app.appName);

    NvDRSProfileHandle hProfile = nullptr;
    status = NvAPI_DRS_FindApplicationByName(hSession, appNameBuf, &hProfile, &app);

    if (status == NVAPI_OK) {
        NvAPI_DRS_DestroySession(hSession);
        InvalidateProfileSearchCache();
        return {true, ""};  // Profile already exists
    }
    if (status != NVAPI_EXECUTABLE_NOT_FOUND) {
        NvAPI_DRS_DestroySession(hSession);
        std::ostringstream oss;
        oss << "FindApplication failed (status " << static_cast<int>(status) << ")";
        return {false, oss.str()};
    }

    // Create new profile named "Display Commander - <exe name>"
    std::wstring profileNameW = L"Display Commander - ";
    profileNameW += exeNameW;
    NVDRS_PROFILE profile = {0};
    profile.version = NVDRS_PROFILE_VER;
    profile.isPredefined = 0;
    WideToNvApiUnicode(profileNameW, profile.profileName);

    status = NvAPI_DRS_CreateProfile(hSession, &profile, &hProfile);
    if (status != NVAPI_OK) {
        NvAPI_DRS_DestroySession(hSession);
        return {false, "DRS CreateProfile failed"};
    }

    app.version = NVDRS_APPLICATION_VER;
    app.isPredefined = 0;
    app.isMetro = 0;
    WideToNvApiUnicode(exeNameW, app.appName);
    WideToNvApiUnicode(exeNameW, app.userFriendlyName);

    status = NvAPI_DRS_CreateApplication(hSession, hProfile, &app);
    if (status != NVAPI_OK) {
        NvAPI_DRS_DestroySession(hSession);
        return {false, "DRS CreateApplication failed"};
    }

    if (NvAPI_DRS_SaveSettings(hSession) != NVAPI_OK) {
        NvAPI_DRS_DestroySession(hSession);
        return {false, "DRS SaveSettings failed"};
    }
    NvAPI_DRS_DestroySession(hSession);
    InvalidateProfileSearchCache();
    return {true, ""};
}

}  // namespace display_commander::nvapi
