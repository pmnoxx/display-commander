#include "nvidia_profile_search.hpp"
#include <nvapi.h>
#include <NvApiDriverSettings.h>
#include <windows.h>
#include <algorithm>
#include <cstring>
#include <cwchar>
#include <map>
#include <sstream>
#include <string>
#include "../utils.hpp"
#include "nvpi_reference.hpp"

namespace display_commander::nvapi {

namespace {

struct ImportantSettingDef {
    NvU32 id;
    const char* label;
    NvU32 default_value;  // NVIDIA driver default when not set in profile
    bool is_bit_field;    // Value is a bitmask; UI uses checkboxes per flag.
};

static const ImportantSettingDef k_important_settings[] = {
    {VSYNCSMOOTHAFR_ID, "Smooth Motion (AFR)", VSYNCSMOOTHAFR_DEFAULT, false},
    {NVPI_SMOOTH_MOTION_ALLOWED_APIS_ID, "Smooth Motion - Allowed APIs", 0, true},
    {NGX_DLSS_SR_MODE_ID, "DLSS-SR mode", static_cast<NvU32>(NGX_DLSS_SR_MODE_DEFAULT), false},
    {NGX_DLSS_SR_OVERRIDE_ID, "DLSS-SR override", static_cast<NvU32>(NGX_DLSS_SR_OVERRIDE_DEFAULT), false},
    {NGX_DLSS_SR_OVERRIDE_RENDER_PRESET_SELECTION_ID, "DLSS-SR preset",
     static_cast<NvU32>(NGX_DLSS_SR_OVERRIDE_RENDER_PRESET_SELECTION_DEFAULT), false},
    {NGX_DLSS_FG_OVERRIDE_ID, "DLSS-FG override", static_cast<NvU32>(NGX_DLSS_FG_OVERRIDE_DEFAULT), false},
    {NGX_DLSS_RR_OVERRIDE_ID, "DLSS-RR override", static_cast<NvU32>(NGX_DLSS_RR_OVERRIDE_DEFAULT), false},
    {NGX_DLSS_RR_MODE_ID, "DLSS-RR mode", static_cast<NvU32>(NGX_DLSS_RR_MODE_DEFAULT), false},
    {NGX_DLSS_RR_OVERRIDE_RENDER_PRESET_SELECTION_ID, "DLSS-RR preset",
     static_cast<NvU32>(NGX_DLSS_RR_OVERRIDE_RENDER_PRESET_SELECTION_DEFAULT), false},
    {NGX_DLAA_OVERRIDE_ID, "DLAA override", static_cast<NvU32>(NGX_DLAA_OVERRIDE_DEFAULT), false},
    {VSYNCMODE_ID, "Vertical Sync", static_cast<NvU32>(VSYNCMODE_DEFAULT), false},
    {VSYNCTEARCONTROL_ID, "Sync tear control", static_cast<NvU32>(VSYNCTEARCONTROL_DEFAULT), false},
    {VRR_APP_OVERRIDE_ID, "G-SYNC / VRR", static_cast<NvU32>(VRR_APP_OVERRIDE_DEFAULT), false},
    {VRR_MODE_ID, "G-SYNC mode", static_cast<NvU32>(VRR_MODE_DEFAULT), false},
    {REFRESH_RATE_OVERRIDE_ID, "Preferred refresh rate", static_cast<NvU32>(REFRESH_RATE_OVERRIDE_DEFAULT), false},
    {PRERENDERLIMIT_ID, "Max pre-rendered frames", static_cast<NvU32>(PRERENDERLIMIT_DEFAULT), false},
    {PREFERRED_PSTATE_ID, "Power management", static_cast<NvU32>(PREFERRED_PSTATE_DEFAULT), false},
};

// Advanced but useful settings — shown when user enables "Show advanced profile settings"
static const ImportantSettingDef k_advanced_settings[] = {
    {ANSEL_ALLOW_ID, "Ansel allow", static_cast<NvU32>(ANSEL_ALLOW_DEFAULT), false},
    {ANSEL_ALLOWLISTED_ID, "Ansel allowlisted", static_cast<NvU32>(ANSEL_ALLOWLISTED_DEFAULT), false},
    {ANSEL_ENABLE_ID, "Ansel enable", static_cast<NvU32>(ANSEL_ENABLE_DEFAULT), false},
};

static std::string FormatImportantValue(NvU32 settingId, NvU32 value) {
    switch (settingId) {
        case VSYNCSMOOTHAFR_ID:                  return (value == VSYNCSMOOTHAFR_ON) ? "On" : "Off";
        case NVPI_SMOOTH_MOTION_ALLOWED_APIS_ID: {
            const auto& flags = GetSmoothMotionAllowedApisFlags();
            if (value == 0) {
                return "None/All";
            }
            std::ostringstream o;
            const char* sep = "";
            for (const auto& p : flags) {
                if ((value & p.first) != 0) {
                    o << sep << p.second;
                    sep = ", ";
                }
            }
            std::string s = o.str();
            return s.empty() ? "None/All" : s;
        }
        case NGX_DLSS_SR_OVERRIDE_ID: return (value == NGX_DLSS_SR_OVERRIDE_ON) ? "On" : "Off";
        case NGX_DLSS_FG_OVERRIDE_ID: return (value == NGX_DLSS_FG_OVERRIDE_ON) ? "On" : "Off";
        case NGX_DLSS_RR_OVERRIDE_ID: return (value == NGX_DLSS_RR_OVERRIDE_ON) ? "On" : "Off";
        case NGX_DLAA_OVERRIDE_ID:    return (value == NGX_DLAA_OVERRIDE_DLAA_ON) ? "On" : "Default";
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
                case 0:  return "Performance";
                case 1:  return "Balanced";
                case 2:  return "Quality";
                case 3:  return "Snippet controlled";
                case 4:  return "DLAA";
                case 5:  return "Ultra Performance";
                case 6:  return "Custom";
                default: break;
            }
            break;
        case NGX_DLSS_RR_MODE_ID:
            switch (value) {
                case 0:  return "Performance";
                case 1:  return "Balanced";
                case 2:  return "Quality";
                case 3:  return "Snippet controlled";
                case 4:  return "DLAA";
                case 5:  return "Ultra Performance";
                case 6:  return "Custom";
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
        case VSYNCTEARCONTROL_ID: return (value == 0x99941284) ? "Enable" : "Disable";
        case PREFERRED_PSTATE_ID:
            if (value == 0) return "Adaptive";
            if (value == 1) return "Prefer max";
            if (value == 2) return "Driver controlled";
            if (value == 3) return "Consistent perf";
            if (value == 4) return "Prefer min";
            if (value == 5) return "Optimal power";
            break;
        case ANSEL_ALLOW_ID:       return (value == ANSEL_ALLOW_ALLOWED) ? "Allowed" : "Disallowed";
        case ANSEL_ALLOWLISTED_ID: return (value == ANSEL_ALLOWLISTED_ALLOWED) ? "Allowed" : "Disallowed";
        case ANSEL_ENABLE_ID:      return (value == ANSEL_ENABLE_ON) ? "On" : "Off";
        default:                   break;
    }
    std::ostringstream oss;
    oss << "0x" << std::hex << value << " (" << std::dec << value << ")";
    return oss.str();
}

static void ReadImportantSettings(NvDRSSessionHandle hSession, NvDRSProfileHandle hProfile,
                                  std::vector<ImportantProfileSetting>& out) {
    for (const auto& def : k_important_settings) {
        ImportantProfileSetting entry;
        entry.label = def.label;
        entry.is_bit_field = def.is_bit_field;
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

static void ReadAdvancedSettings(NvDRSSessionHandle hSession, NvDRSProfileHandle hProfile,
                                 std::vector<ImportantProfileSetting>& out) {
    for (const auto& def : k_advanced_settings) {
        ImportantProfileSetting entry;
        entry.label = def.label;
        entry.is_bit_field = def.is_bit_field;
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

static void ReadAllSettings(NvDRSSessionHandle hSession, NvDRSProfileHandle hProfile,
                            std::vector<ImportantProfileSetting>& out) {
    constexpr NvU32 kBatchSize = 64;
    std::vector<NVDRS_SETTING> batch(kBatchSize);
    for (NvU32 startIndex = 0;;) {
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
            entry.setting_id = static_cast<std::uint32_t>(s.settingId);
            entry.label = WideToUtf8(reinterpret_cast<const wchar_t*>(s.settingName));
            if (entry.label.empty()) {
                std::ostringstream o;
                o << "Setting 0x" << std::hex << s.settingId;
                entry.label = o.str();
            }
            entry.value = FormatSettingValue(s, WideToUtf8);
            if (s.settingType == NVDRS_DWORD_TYPE) {
                entry.value_id = static_cast<std::uint32_t>(s.u32CurrentValue);
            }
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

// Returns true if the NvAPI Unicode string is non-empty (has at least one non-zero character).
static bool NvApiUnicodeNonEmpty(const NvAPI_UnicodeString& s) { return s[0] != 0; }

// Character length of NvAPI_UnicodeString (number of code units until null).
static size_t NvApiUnicodeLen(const NvAPI_UnicodeString& s) {
    const wchar_t* p = reinterpret_cast<const wchar_t*>(s);
    return wcsnlen(p, NVAPI_UNICODE_STRING_MAX);
}

// Score: +1000 per non-empty field, +1 per character in string fields. Higher = more specific.
static int ScoreAppEntry(const NVDRS_APPLICATION& appEnt) {
    int score = 0;
    if (NvApiUnicodeNonEmpty(appEnt.appName)) score += 1000 + static_cast<int>(NvApiUnicodeLen(appEnt.appName));
    if (NvApiUnicodeNonEmpty(appEnt.fileInFolder))
        score += 1000 + static_cast<int>(NvApiUnicodeLen(appEnt.fileInFolder));
    if (NvApiUnicodeNonEmpty(appEnt.userFriendlyName))
        score += 1000 + static_cast<int>(NvApiUnicodeLen(appEnt.userFriendlyName));
    if (NvApiUnicodeNonEmpty(appEnt.launcher)) score += 1000 + static_cast<int>(NvApiUnicodeLen(appEnt.launcher));
    if (NvApiUnicodeNonEmpty(appEnt.commandLine)) score += 1000 + static_cast<int>(NvApiUnicodeLen(appEnt.commandLine));
    if (appEnt.isMetro != 0) score += 1000;
    if (appEnt.isCommandLine != 0) score += 1000;
    return score;
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

// Build error string: "step: NVAPI description (0xCODE)". Defined early for use in GetProfileDetailsForCurrentExe.
static std::string MakeNvapiError(const char* step, NvAPI_Status st) {
    NvAPI_ShortString buf = {};
    if (NvAPI_GetErrorMessage(st, buf) == NVAPI_OK && buf[0] != '\0') {
        std::ostringstream o;
        o << step << ": " << buf << " (0x" << std::hex << static_cast<unsigned>(st) << ")";
        return o.str();
    }
    std::ostringstream o;
    o << step << ": NVAPI 0x" << std::hex << static_cast<unsigned>(st);
    return o.str();
}

static MatchedProfileEntry MakeMatchedProfileEntry(const NVDRS_PROFILE& profileInfo, const NVDRS_APPLICATION& app) {
    MatchedProfileEntry entry;
    entry.profile_name = WideToUtf8(reinterpret_cast<const wchar_t*>(profileInfo.profileName));
    entry.app_name = WideToUtf8(reinterpret_cast<const wchar_t*>(app.appName));
    entry.user_friendly_name = WideToUtf8(reinterpret_cast<const wchar_t*>(app.userFriendlyName));
    entry.launcher = WideToUtf8(reinterpret_cast<const wchar_t*>(app.launcher));
    entry.file_in_folder = WideToUtf8(reinterpret_cast<const wchar_t*>(app.fileInFolder));
    entry.is_metro = (app.isMetro != 0);
    entry.is_command_line = (app.isCommandLine != 0);
    entry.command_line = WideToUtf8(reinterpret_cast<const wchar_t*>(app.commandLine));
    entry.score = ScoreAppEntry(app);
    return entry;
}

static NvidiaProfileSearchResult GetProfileDetailsForCurrentExe() {
    NvidiaProfileSearchResult result;
    std::wstring exePath = GetCurrentProcessPathW();
    if (exePath.empty()) {
        result.error = "GetModuleFileName failed";
        return result;
    }
    result.current_exe_path = WideToUtf8(exePath.c_str());
    const wchar_t* base = wcsrchr(exePath.c_str(), L'\\');
    result.current_exe_name = WideToUtf8(base ? base + 1 : exePath.c_str());

    NvDRSSessionHandle hSession = nullptr;
    NvAPI_Status st = NvAPI_DRS_CreateSession(&hSession);
    if (st != NVAPI_OK) {
        result.error = MakeNvapiError("CreateSession", st);
        return result;
    }
    st = NvAPI_DRS_LoadSettings(hSession);
    if (st != NVAPI_OK) {
        NvAPI_DRS_DestroySession(hSession);
        result.error = MakeNvapiError("LoadSettings", st);
        return result;
    }

    NvDRSProfileHandle hProfile = nullptr;
    NVDRS_APPLICATION app = {0};
    if (!FindApplicationByPathForCurrentExe(hSession, &hProfile, &app)) {
        result.success = true;
        NvAPI_DRS_DestroySession(hSession);
        return result;
    }

    NVDRS_PROFILE profileInfo = {0};
    profileInfo.version = NVDRS_PROFILE_VER;
    if (NvAPI_DRS_GetProfileInfo(hSession, hProfile, &profileInfo) == NVAPI_OK) {
        MatchedProfileEntry entry = MakeMatchedProfileEntry(profileInfo, app);
        result.matching_profiles.push_back(std::move(entry));
        result.matching_profile_names.push_back(
            WideToUtf8(reinterpret_cast<const wchar_t*>(profileInfo.profileName)));
    }
    ReadImportantSettings(hSession, hProfile, result.important_settings);
    ReadAdvancedSettings(hSession, hProfile, result.advanced_settings);
    ReadAllSettings(hSession, hProfile, result.all_settings);
    result.success = true;
    NvAPI_DRS_DestroySession(hSession);
    return result;
}

}  // namespace

// Fills fullPathBuf with current process exe path (normalized). Returns false if exe path unavailable.
static bool GetProfilePathForCurrentExe(NvAPI_UnicodeString& fullPathBuf) {
    std::wstring exePath = GetCurrentProcessPathW();
    if (exePath.empty()) {
        return false;
    }
    std::wstring currentPathNorm = NormalizePath(exePath);
    WideToNvApiUnicode(currentPathNorm, fullPathBuf);
    return true;
}

// Single call site for NvAPI_DRS_FindApplicationByName: finds profile by current exe full path.
// Caller owns hSession (must be created and loaded). Returns true if profile and app found.
bool FindApplicationByPathForCurrentExe(NvDRSSessionHandle hSession, NvDRSProfileHandle* phProfile,
                                        NVDRS_APPLICATION* pApp) {
    if (!phProfile || !pApp) {
        return false;
    }
    *phProfile = nullptr;
    memset(pApp, 0, sizeof(*pApp));
    pApp->version = NVDRS_APPLICATION_VER;

    NvAPI_UnicodeString fullPathBuf;
    if (!GetProfilePathForCurrentExe(fullPathBuf)) {
        return false;
    }
    NvAPI_Status st = NvAPI_DRS_FindApplicationByName(hSession, fullPathBuf, phProfile, pApp);
    return (st == NVAPI_OK && *phProfile != nullptr);
}

static NvidiaProfileSearchResult s_cachedResult;
static std::string s_cachedExePath;
static bool s_cacheValid = false;

NvidiaProfileSearchResult GetCachedProfileSearchResult() {
    std::wstring exePath = GetCurrentProcessPathW();
    if (exePath.empty()) {
        s_cacheValid = false;
        NvidiaProfileSearchResult r;
        r.error = "GetModuleFileName failed";
        return r;
    }
    std::string currentPath = WideToUtf8(exePath.c_str());
    if (s_cacheValid && s_cachedExePath == currentPath) {
        return s_cachedResult;
    }
    s_cachedResult = GetProfileDetailsForCurrentExe();
    s_cachedExePath = currentPath;
    s_cacheValid = true;
    return s_cachedResult;
}

void InvalidateProfileSearchCache() { s_cacheValid = false; }

DlssDriverPresetStatus GetDlssDriverPresetStatus() {
    DlssDriverPresetStatus out;
    NvidiaProfileSearchResult r = GetCachedProfileSearchResult();
    if (!r.success) {
        out.profile_error = r.error;
        return out;
    }
    if (r.matching_profile_names.empty()) {
        out.has_profile = false;
        return out;
    }
    out.has_profile = true;
    for (size_t i = 0; i < r.matching_profile_names.size(); ++i) {
        if (i != 0) {
            out.profile_names += ", ";
        }
        out.profile_names += r.matching_profile_names[i];
    }
    for (const auto& s : r.important_settings) {
        if (s.setting_id == NGX_DLSS_SR_OVERRIDE_RENDER_PRESET_SELECTION_ID) {
            out.sr_preset_value = s.value;
            out.sr_preset_is_override =
                (s.value_id != static_cast<std::uint32_t>(NGX_DLSS_SR_OVERRIDE_RENDER_PRESET_SELECTION_DEFAULT));
            continue;
        }
        if (s.setting_id == NGX_DLSS_RR_OVERRIDE_RENDER_PRESET_SELECTION_ID) {
            out.rr_preset_value = s.value;
            out.rr_preset_is_override =
                (s.value_id != static_cast<std::uint32_t>(NGX_DLSS_RR_OVERRIDE_RENDER_PRESET_SELECTION_DEFAULT));
            continue;
        }
    }
    return out;
}

using ValueList = std::vector<std::pair<std::uint32_t, std::string>>;
static std::map<std::uint32_t, ValueList> s_availableValuesCache;

std::vector<std::pair<std::uint32_t, std::string>> GetSettingAvailableValues(std::uint32_t settingId) {
    if (settingId == NVPI_SMOOTH_MOTION_ALLOWED_APIS_ID) {
        return GetSmoothMotionAllowedApisValues();
    }
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

// Setting name for NVPI custom setting (not in NvApiDriverSettings.h). Required when creating the setting in a profile.
static const wchar_t k_smoothMotionAllowedApisName[] = L"Smooth Motion - Allowed APIs";

std::pair<bool, std::string> SetProfileSetting(std::uint32_t settingId, std::uint32_t value) {
    NvDRSSessionHandle hSession = nullptr;
    NvAPI_Status st = NvAPI_DRS_CreateSession(&hSession);
    if (st != NVAPI_OK) {
        return {false, MakeNvapiError("CreateSession", st)};
    }
    st = NvAPI_DRS_LoadSettings(hSession);
    if (st != NVAPI_OK) {
        NvAPI_DRS_DestroySession(hSession);
        return {false, MakeNvapiError("LoadSettings", st)};
    }
    NvDRSProfileHandle hProfile = nullptr;
    NVDRS_APPLICATION app = {0};
    if (!FindApplicationByPathForCurrentExe(hSession, &hProfile, &app)) {
        NvAPI_DRS_DestroySession(hSession);
        return {false, "No profile matches current exe (add this game to a profile first)."};
    }
    NVDRS_SETTING s;
    memset(&s, 0, sizeof(s));
    s.version = NVDRS_SETTING_VER;
    s.settingId = static_cast<NvU32>(settingId);
    s.settingType = NVDRS_DWORD_TYPE;
    s.u32CurrentValue = static_cast<NvU32>(value);

    // Get existing setting so we pass a full struct (including settingName); driver may require it for SetSetting.
    if (NvAPI_DRS_GetSetting(hSession, hProfile, static_cast<NvU32>(settingId), &s) == NVAPI_OK) {
        if (s.settingType == NVDRS_DWORD_TYPE) {
            s.u32CurrentValue = static_cast<NvU32>(value);
        }
    } else {
        // Setting not in profile yet: fill settingName for known custom IDs so driver accepts the new setting.
        if (settingId == NVPI_SMOOTH_MOTION_ALLOWED_APIS_ID) {
            WideToNvApiUnicode(k_smoothMotionAllowedApisName, s.settingName);
        }
    }

    st = NvAPI_DRS_SetSetting(hSession, hProfile, &s);
    if (st != NVAPI_OK) {
        NvAPI_DRS_DestroySession(hSession);
        return {false, MakeNvapiError("SetSetting", st)};
    }
    st = NvAPI_DRS_SaveSettings(hSession);
    if (st != NVAPI_OK) {
        NvAPI_DRS_DestroySession(hSession);
        return {false, MakeNvapiError("SaveSettings", st)};
    }
    NvAPI_DRS_DestroySession(hSession);
    InvalidateProfileSearchCache();
    return {true, ""};
}

std::pair<bool, std::string> ClearDriverDlssPresetOverride() {
    auto sr = SetProfileSetting(NGX_DLSS_SR_OVERRIDE_RENDER_PRESET_SELECTION_ID,
                                static_cast<std::uint32_t>(NGX_DLSS_SR_OVERRIDE_RENDER_PRESET_SELECTION_DEFAULT));
    if (!sr.first) {
        return sr;
    }
    auto rr = SetProfileSetting(NGX_DLSS_RR_OVERRIDE_RENDER_PRESET_SELECTION_ID,
                                static_cast<std::uint32_t>(NGX_DLSS_RR_OVERRIDE_RENDER_PRESET_SELECTION_DEFAULT));
    return rr;
}

std::pair<bool, std::string> SetOrDeleteProfileSettingForExe(const std::wstring& exeName, std::uint32_t settingId,
                                                             bool deleteSetting, std::uint32_t valueIfSet) {
    if (exeName.empty()) {
        return {false, "Executable name is empty."};
    }
    // Only current process exe is supported (find by path). Reject if exeName is not current process.
    std::wstring currentPath = GetCurrentProcessPathW();
    if (currentPath.empty()) {
        return {false, "GetModuleFileName failed."};
    }
    const wchar_t* base = wcsrchr(currentPath.c_str(), L'\\');
    const wchar_t* baseName = base ? base + 1 : currentPath.c_str();
    std::wstring exeNorm = exeName;
    std::replace(exeNorm.begin(), exeNorm.end(), L'\\', L'/');
    const wchar_t* exeBase = wcsrchr(exeNorm.c_str(), L'/');
    std::wstring exeNameOnly(exeBase ? exeBase + 1 : exeNorm.c_str());
    if (_wcsicmp(baseName, exeNameOnly.c_str()) != 0) {
        return {false, "Only current process executable is supported. Run from the game process or use the profile UI."};
    }

    NvDRSSessionHandle hSession = nullptr;
    NvAPI_Status st = NvAPI_DRS_CreateSession(&hSession);
    if (st != NVAPI_OK) {
        return {false, MakeNvapiError("CreateSession", st)};
    }
    st = NvAPI_DRS_LoadSettings(hSession);
    if (st != NVAPI_OK) {
        NvAPI_DRS_DestroySession(hSession);
        return {false, MakeNvapiError("LoadSettings", st)};
    }

    NvDRSProfileHandle hProfile = nullptr;
    NVDRS_APPLICATION app = {0};
    if (!FindApplicationByPathForCurrentExe(hSession, &hProfile, &app)) {
        NvAPI_DRS_DestroySession(hSession);
        return {false, "No NVIDIA driver profile found for this executable. Add the game to a profile first."};
    }

    if (deleteSetting) {
        st = NvAPI_DRS_DeleteProfileSetting(hSession, hProfile, static_cast<NvU32>(settingId));
        if (st != NVAPI_OK) {
            NvAPI_DRS_DestroySession(hSession);
            return {false, MakeNvapiError("DeleteProfileSetting", st)};
        }
    } else {
        NVDRS_SETTING s;
        memset(&s, 0, sizeof(s));
        s.version = NVDRS_SETTING_VER;
        s.settingId = static_cast<NvU32>(settingId);
        s.settingType = NVDRS_DWORD_TYPE;
        s.u32CurrentValue = static_cast<NvU32>(valueIfSet);

        if (NvAPI_DRS_GetSetting(hSession, hProfile, static_cast<NvU32>(settingId), &s) == NVAPI_OK) {
            if (s.settingType == NVDRS_DWORD_TYPE) {
                s.u32CurrentValue = static_cast<NvU32>(valueIfSet);
            }
        } else {
            if (settingId == NVPI_SMOOTH_MOTION_ALLOWED_APIS_ID) {
                WideToNvApiUnicode(k_smoothMotionAllowedApisName, s.settingName);
            }
        }

        st = NvAPI_DRS_SetSetting(hSession, hProfile, &s);
        if (st != NVAPI_OK) {
            NvAPI_DRS_DestroySession(hSession);
            return {false, MakeNvapiError("SetSetting", st)};
        }
    }

    st = NvAPI_DRS_SaveSettings(hSession);
    if (st != NVAPI_OK) {
        NvAPI_DRS_DestroySession(hSession);
        return {false, MakeNvapiError("SaveSettings", st)};
    }
    NvAPI_DRS_DestroySession(hSession);
    InvalidateProfileSearchCache();
    return {true, ""};
}

std::pair<bool, std::string> CreateProfileForCurrentExe() {
    std::wstring exePath = GetCurrentProcessPathW();
    if (exePath.empty()) {
        return {false, "GetModuleFileName failed"};
    }
    const wchar_t* base = wcsrchr(exePath.c_str(), L'\\');
    const wchar_t* exeName = base ? base + 1 : exePath.c_str();
    std::wstring exeNameW(exeName);
    std::wstring fullPathNorm = NormalizePath(exePath);

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

    NvDRSProfileHandle hProfile = nullptr;
    NVDRS_APPLICATION app = {0};
    if (FindApplicationByPathForCurrentExe(hSession, &hProfile, &app)) {
        NvAPI_DRS_DestroySession(hSession);
        InvalidateProfileSearchCache();
        return {true, ""};  // Profile already exists
    }

    // Create new profile named "Display Commander - <exe name>"
    std::wstring profileNameW = L"Display Commander - ";
    profileNameW += exeNameW;
    NVDRS_PROFILE profileData = {0};
    profileData.version = NVDRS_PROFILE_VER;
    profileData.isPredefined = 0;
    WideToNvApiUnicode(profileNameW, profileData.profileName);

    NvAPI_Status createSt = NvAPI_DRS_CreateProfile(hSession, &profileData, &hProfile);
    if (createSt != NVAPI_OK) {
        NvAPI_DRS_DestroySession(hSession);
        return {false, "DRS CreateProfile failed"};
    }

    app.version = NVDRS_APPLICATION_VER;
    app.isPredefined = 0;
    app.isMetro = 0;
    WideToNvApiUnicode(fullPathNorm, app.appName);
    WideToNvApiUnicode(exeNameW, app.userFriendlyName);

    createSt = NvAPI_DRS_CreateApplication(hSession, hProfile, &app);
    if (createSt != NVAPI_OK) {
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

static const char k_displayCommanderProfilePrefix[] = "Display Commander - ";

bool HasDisplayCommanderProfile(const NvidiaProfileSearchResult& r) {
    const size_t prefixLen = sizeof(k_displayCommanderProfilePrefix) - 1;
    for (const std::string& name : r.matching_profile_names) {
        if (name.size() >= prefixLen && name.compare(0, prefixLen, k_displayCommanderProfilePrefix) == 0) {
            return true;
        }
    }
    return false;
}

std::pair<bool, std::string> DeleteDisplayCommanderProfileForCurrentExe() {
    NvDRSSessionHandle hSession = nullptr;
    NvAPI_Status st = NvAPI_DRS_CreateSession(&hSession);
    if (st != NVAPI_OK) {
        return {false, MakeNvapiError("CreateSession", st)};
    }
    st = NvAPI_DRS_LoadSettings(hSession);
    if (st != NVAPI_OK) {
        NvAPI_DRS_DestroySession(hSession);
        return {false, MakeNvapiError("LoadSettings", st)};
    }

    NvDRSProfileHandle hProfile = nullptr;
    NVDRS_APPLICATION app = {0};
    if (!FindApplicationByPathForCurrentExe(hSession, &hProfile, &app)) {
        NvAPI_DRS_DestroySession(hSession);
        return {false, "No profile found for current exe."};
    }
    NVDRS_PROFILE profileInfo = {0};
    profileInfo.version = NVDRS_PROFILE_VER;
    if (NvAPI_DRS_GetProfileInfo(hSession, hProfile, &profileInfo) != NVAPI_OK) {
        NvAPI_DRS_DestroySession(hSession);
        return {false, "GetProfileInfo failed."};
    }
    const wchar_t* profileNameW = reinterpret_cast<const wchar_t*>(profileInfo.profileName);
    const std::string profileNameUtf8 = WideToUtf8(profileNameW);
    const size_t prefixLen = sizeof(k_displayCommanderProfilePrefix) - 1;
    if (profileNameUtf8.size() < prefixLen ||
        profileNameUtf8.compare(0, prefixLen, k_displayCommanderProfilePrefix) != 0) {
        NvAPI_DRS_DestroySession(hSession);
        return {false, "Display Commander profile not found for this exe (profile exists but is not ours)."};
    }

    st = NvAPI_DRS_DeleteProfile(hSession, hProfile);
    if (st != NVAPI_OK) {
        NvAPI_DRS_DestroySession(hSession);
        return {false, MakeNvapiError("DeleteProfile", st)};
    }
    st = NvAPI_DRS_SaveSettings(hSession);
    if (st != NVAPI_OK) {
        NvAPI_DRS_DestroySession(hSession);
        return {false, MakeNvapiError("SaveSettings", st)};
    }
    NvAPI_DRS_DestroySession(hSession);
    InvalidateProfileSearchCache();
    return {true, ""};
}

}  // namespace display_commander::nvapi
