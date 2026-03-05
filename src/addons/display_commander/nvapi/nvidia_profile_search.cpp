#include "nvidia_profile_search.hpp"
#include "../utils.hpp"
#include "nvapi_loader.hpp"
#include "nvpi_reference.hpp"

#include <nvapi.h>
#include <NvApiDriverSettings.h>

#include <Windows.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <cwchar>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace display_commander::nvapi {

namespace {

inline const display_commander::nvapi_loader::NvApiPtrs* NvApi() { return display_commander::nvapi_loader::Ptrs(); }

// Full metadata for a setting. Data source: NvidiaProfileInspectorRevamped CustomSettingNames.xml
// https://github.com/xHybred/NvidiaProfileInspectorRevamped/blob/master/nspector/CustomSettingNames.xml
// Example XML:
//   <CustomSetting>
//     <UserfriendlyName>RTX HDR - Enable</UserfriendlyName>
//     <HexSettingID>0x00DD48FB</HexSettingID>
//     <GroupName>0.2.1 - Graphic | HDR</GroupName>
//     <MinRequiredDriverVersion>0</MinRequiredDriverVersion>
//     <SettingValues>
//       <CustomSettingValue><UserfriendlyName>Off</UserfriendlyName><HexValue>0x00000000</HexValue></CustomSettingValue>
//       <CustomSettingValue><UserfriendlyName>On</UserfriendlyName><HexValue>0x00000001</HexValue></CustomSettingValue>
//     </SettingValues>
//   </CustomSetting>
// All members have defaults so designated initializers can omit fields.
// option_values: (hex_value, label) from SettingValues. When non-empty, used for presentation and combo.
// is_advanced: if true, shown in "Show advanced profile settings" only.
// requires_admin: if true, setting is hidden from Main tab (NVIDIA section) to avoid privilege errors; still shown in
// NVIDIA Profile tab.
struct SettingData {
    const char* user_friendly_name = nullptr;
    std::uint32_t hex_setting_id = 0;
    const char* group_name = nullptr;
    unsigned min_required_driver_version = 0;  // e.g. 57186 for 571.86
    std::uint32_t default_value = 0;
    bool is_bit_field = false;
    bool is_advanced = false;
    bool requires_admin = false;
    bool resolve_id_from_driver = false;  // If true, use NvApi()->DRS_GetSettingIdFromName at runtime (driver may use
                                          // different ID, e.g. RTX HDR in group 5).
    const wchar_t* driver_lookup_name_wide =
        nullptr;  // When set, use this for GetSettingIdFromName (NVAPI expects UTF-16); avoids UTF-8 conversion issues.
    std::vector<std::pair<std::uint32_t, const char*>> option_values = {};
};

// All important + advanced settings; first k_num_important_settings are important, rest are advanced.
static constexpr std::size_t k_num_important_settings = 25;
static const std::array<SettingData, 30> k_settings_data = {{
    // Important (order matches previous k_important_settings)
    {.user_friendly_name = "Smooth Motion - Allowed APIs [40 series]",
     .hex_setting_id = NVPI_SMOOTH_MOTION_ALLOWED_APIS_ID,
     .default_value = 0,
     .is_bit_field = true,
     .requires_admin = true,
     .option_values = {{0x00000000, "Allow - None"},
                       {0x00000001, "Allow - DX12"},
                       {0x00000002, "Allow - DX11"},
                       {0x00000003, "Allow - DX11/12"},
                       {0x00000004, "Allow - VK"},
                       {0x00000005, "Allow - DX12, VK"},
                       {0x00000006, "Allow - DX11, VK"},
                       {0x00000007, "Allow - All [DX11/12, VK]"}}},
    {.user_friendly_name = "Smooth Motion - Enable",
     .hex_setting_id = NVPI_SMOOTH_MOTION_ENABLE_50_ID,
     .default_value = 0x00000000,
     .option_values = {{0x00000000, "Off"}, {0x00000001, "On"}}},
    // Revamped CustomSettingNames.xml uses "RTX HDR - Enable"; driver can return "Enable TrueHDR Feature"
    // (that string is NOT in NvidiaProfileInspectorRevamped — it comes from NVAPI/driver on export).
    {.user_friendly_name = "RTX HDR - Enable",
     .hex_setting_id = NVPI_RTX_HDR_ENABLE_ID,
     .default_value = 0,
     .driver_lookup_name_wide = L"RTX HDR - Enable",  // NVAPI format (UTF-16); no UTF-8 conversion.
     .option_values = {{0x00000000, "Off"}, {0x00000001, "On"}}},
    {.user_friendly_name = "RTX HDR - Debanding",
     .hex_setting_id = NVPI_RTX_HDR_DEBANDING_ID,
     .default_value = 0x00000000,
     .requires_admin = true,
     .option_values = {{0x00000000, "N/A"},
                       {0x00000006, "No Debanding"},
                       {0x0000000A, "Low Debanding"},
                       {0x00000002, "High Debanding"},
                       {0x00000003, "High Debanding (Indicator)"},
                       {0x00000023, "High Debanding (Indicator + Debug)"}}},
    {.user_friendly_name = "RTX HDR - Allow",
     .hex_setting_id = NVPI_RTX_HDR_ALLOW_ID,
     .default_value = 0x00000000,
     .requires_admin = true,
     .option_values = {{0x00000000, "Disallow"}, {0x00000001, "Allow"}}},
    {.user_friendly_name = "RTX HDR - Contrast",
     .hex_setting_id = NVPI_RTX_HDR_CONTRAST_ID,
     .default_value = 0x00000064,
     .option_values = {{0x00000000, "0 | -100"},
                       {0x00000032, "50 | -50"},
                       {0x00000064, "100 | 0 [Default]"},
                       {0x0000007D, "125 | +25 (Gamma 2.2)"},
                       {0x00000096, "150 | +50 (Gamma 2.4)"},
                       {0x000000C8, "200 | +100"},
                       {0x000000C9, "Custom (0-200)"}}},
    {.user_friendly_name = "RTX HDR - Middle Grey",
     .hex_setting_id = NVPI_RTX_HDR_MIDDLE_GREY_ID,
     .default_value = 0x00000032,
     .option_values = {{0x00000000, "N/A"},
                       {0x0000000A, "10"},
                       {0x00000032, "50 [Default]"},
                       {0x00000064, "100"},
                       {0x00000065, "Custom (10-100)"}}},
    {.user_friendly_name = "RTX HDR - Peak Brightness",
     .hex_setting_id = NVPI_RTX_HDR_PEAK_BRIGHTNESS_ID,
     .default_value = 0x00000000,
     .option_values = {{0x00000000, "N/A"},
                       {0x00000190, "400"},
                       {0x00000258, "600"},
                       {0x00000320, "800"},
                       {0x000003E8, "1000"},
                       {0x000004B0, "1200"},
                       {0x000005DC, "1500"},
                       {0x000007D0, "2000"},
                       {0x00000802, "Custom (400-2000)"}}},
    {.user_friendly_name = "RTX HDR - Saturation",
     .hex_setting_id = NVPI_RTX_HDR_SATURATION_ID,
     .default_value = 0x00000064,
     .option_values = {{0x00000000, "0 | -100"},
                       {0x0000004B, "75 | -25 (Neutral)"},
                       {0x00000064, "100 | 0 [Default]"},
                       {0x0000007D, "125 | +25"},
                       {0x00000096, "150 | +50"},
                       {0x000000C8, "200 | +100"},
                       {0x000000C9, "Custom (0-200)"}}},
    {.user_friendly_name = "DLSS-SR mode",
     .hex_setting_id = NGX_DLSS_SR_MODE_ID,
     .default_value = static_cast<std::uint32_t>(NGX_DLSS_SR_MODE_DEFAULT),
     .option_values = {{0, "Performance"},
                       {1, "Balanced"},
                       {2, "Quality"},
                       {3, "Snippet controlled"},
                       {4, "DLAA"},
                       {5, "Ultra Performance"},
                       {6, "Custom"}}},
    {.user_friendly_name = "DLSS-SR override",
     .hex_setting_id = NGX_DLSS_SR_OVERRIDE_ID,
     .default_value = static_cast<std::uint32_t>(NGX_DLSS_SR_OVERRIDE_DEFAULT),
     .option_values = {{0, "Off"}, {1, "On"}}},
    {.user_friendly_name = "DLSS-SR preset",
     .hex_setting_id = NGX_DLSS_SR_OVERRIDE_RENDER_PRESET_SELECTION_ID,
     .default_value = static_cast<std::uint32_t>(NGX_DLSS_SR_OVERRIDE_RENDER_PRESET_SELECTION_DEFAULT),
     .option_values = {{0, "Off"},
                       {1, "Preset A"},
                       {2, "Preset B"},
                       {3, "Preset C"},
                       {4, "Preset D"},
                       {5, "Preset E"},
                       {6, "Preset F"},
                       {7, "Preset G"},
                       {8, "Preset H"},
                       {9, "Preset I"},
                       {10, "Preset J"},
                       {11, "Preset K"},
                       {12, "Preset L"},
                       {13, "Preset M"},
                       {14, "Preset N"},
                       {15, "Preset O"},
                       {0x00ffffff, "Latest"}}},
    {.user_friendly_name = "DLSS-FG override",
     .hex_setting_id = NGX_DLSS_FG_OVERRIDE_ID,
     .default_value = static_cast<std::uint32_t>(NGX_DLSS_FG_OVERRIDE_DEFAULT),
     .option_values = {{0, "Off"}, {1, "On"}}},
    {.user_friendly_name = "DLSS-RR override",
     .hex_setting_id = NGX_DLSS_RR_OVERRIDE_ID,
     .default_value = static_cast<std::uint32_t>(NGX_DLSS_RR_OVERRIDE_DEFAULT),
     .option_values = {{0, "Off"}, {1, "On"}}},
    {.user_friendly_name = "DLSS-RR mode",
     .hex_setting_id = NGX_DLSS_RR_MODE_ID,
     .default_value = static_cast<std::uint32_t>(NGX_DLSS_RR_MODE_DEFAULT),
     .option_values = {{0, "Performance"},
                       {1, "Balanced"},
                       {2, "Quality"},
                       {3, "Snippet controlled"},
                       {4, "DLAA"},
                       {5, "Ultra Performance"},
                       {6, "Custom"}}},
    {.user_friendly_name = "DLSS-RR preset",
     .hex_setting_id = NGX_DLSS_RR_OVERRIDE_RENDER_PRESET_SELECTION_ID,
     .default_value = static_cast<std::uint32_t>(NGX_DLSS_RR_OVERRIDE_RENDER_PRESET_SELECTION_DEFAULT),
     .option_values = {{0, "Off"},
                       {1, "Preset A"},
                       {2, "Preset B"},
                       {3, "Preset C"},
                       {4, "Preset D"},
                       {5, "Preset E"},
                       {6, "Preset F"},
                       {7, "Preset G"},
                       {8, "Preset H"},
                       {9, "Preset I"},
                       {10, "Preset J"},
                       {11, "Preset K"},
                       {12, "Preset L"},
                       {13, "Preset M"},
                       {14, "Preset N"},
                       {15, "Preset O"},
                       {0x00ffffff, "Latest"}}},
    {.user_friendly_name = "DLAA override",
     .hex_setting_id = NGX_DLAA_OVERRIDE_ID,
     .default_value = static_cast<std::uint32_t>(NGX_DLAA_OVERRIDE_DEFAULT),
     .option_values = {{0, "Default"}, {1, "On"}}},
    {.user_friendly_name = "Vertical Sync",
     .hex_setting_id = VSYNCMODE_ID,
     .default_value = static_cast<std::uint32_t>(VSYNCMODE_DEFAULT),
     .option_values = {{0x60925292, "Passive (app)"},
                       {0x08416747, "Force Off"},
                       {0x47814940, "Force On"},
                       {0x32610244, "Flip 2"},
                       {0x71271021, "Flip 3"},
                       {0x13245256, "Flip 4"},
                       {0x18888888, "Virtual"}}},
    {.user_friendly_name = "Sync tear control",
     .hex_setting_id = VSYNCTEARCONTROL_ID,
     .default_value = static_cast<std::uint32_t>(VSYNCTEARCONTROL_DEFAULT),
     .option_values = {{0x99941284, "Enable"}, {0x00000000, "Disable"}}},
    {.user_friendly_name = "G-SYNC / VRR",
     .hex_setting_id = VRR_APP_OVERRIDE_ID,
     .default_value = static_cast<std::uint32_t>(VRR_APP_OVERRIDE_DEFAULT),
     .option_values = {{0, "Allow"}, {1, "Force Off"}, {2, "Disallow"}, {3, "ULMB"}, {4, "Fixed refresh"}}},
    {.user_friendly_name = "G-SYNC mode",
     .hex_setting_id = VRR_MODE_ID,
     .default_value = static_cast<std::uint32_t>(VRR_MODE_DEFAULT),
     .option_values = {{0, "Disabled"}, {1, "Fullscreen only"}, {2, "Fullscreen + windowed"}}},
    {.user_friendly_name = "Preferred refresh rate",
     .hex_setting_id = REFRESH_RATE_OVERRIDE_ID,
     .default_value = static_cast<std::uint32_t>(REFRESH_RATE_OVERRIDE_DEFAULT),
     .option_values = {{REFRESH_RATE_OVERRIDE_APPLICATION_CONTROLLED, "Application controlled"},
                       {REFRESH_RATE_OVERRIDE_HIGHEST_AVAILABLE, "Highest available"}}},
    {.user_friendly_name = "Max pre-rendered frames",
     .hex_setting_id = PRERENDERLIMIT_ID,
     .default_value = static_cast<std::uint32_t>(PRERENDERLIMIT_DEFAULT),
     .option_values = {{0x00000000, "Use the 3D application setting"},
                       {0x00000001, "1"},
                       {0x00000002, "2"},
                       {0x00000003, "3"},
                       {0x00000004, "4"},
                       {0x00000005, "5"},
                       {0x00000006, "6"},
                       {0x00000007, "7"},
                       {0x00000008, "8"}}},
    {.user_friendly_name = "Power management",
     .hex_setting_id = PREFERRED_PSTATE_ID,
     .default_value = static_cast<std::uint32_t>(PREFERRED_PSTATE_DEFAULT),
     .option_values = {{0, "Adaptive"},
                       {1, "Prefer max"},
                       {2, "Driver controlled"},
                       {3, "Consistent perf"},
                       {4, "Prefer min"},
                       {5, "Optimal power"}}},
    {.user_friendly_name = "FPS Limiter V3",
     .hex_setting_id = FRL_FPS_ID,
     .default_value = static_cast<std::uint32_t>(FRL_FPS_DEFAULT),
     .option_values = {}},  // Built in GetSettingAvailableValues (Off + 20–1000 FPS)
    // Advanced
    {.user_friendly_name = "Ansel allow",
     .hex_setting_id = ANSEL_ALLOW_ID,
     .default_value = static_cast<std::uint32_t>(ANSEL_ALLOW_DEFAULT),
     .is_advanced = true,
     .option_values = {{0, "Disallowed"}, {1, "Allowed"}}},
    {.user_friendly_name = "Ansel allowlisted",
     .hex_setting_id = ANSEL_ALLOWLISTED_ID,
     .default_value = static_cast<std::uint32_t>(ANSEL_ALLOWLISTED_DEFAULT),
     .is_advanced = true,
     .option_values = {{0, "Disallowed"}, {1, "Allowed"}}},
    // Same driver setting as Ansel enable; NPI CustomSettingNames.xml uses "Freestyle Filters - Enable", group HDR.
    {.user_friendly_name = "Freestyle Filters - Enable",
     .hex_setting_id = ANSEL_ENABLE_ID,
     .group_name = "0.2.1 - Graphic | HDR",
     .default_value = static_cast<std::uint32_t>(ANSEL_ENABLE_DEFAULT),
     .is_advanced = true,
     .option_values = {{0x00000000, "Off"}, {0x00000001, "On"}}},
    {.user_friendly_name = "Ultra Low Latency - CPL State",
     .hex_setting_id = ULL_CPL_STATE_ID,
     .min_required_driver_version = 43000,  // 430.00
     .default_value = 0x00000000,
     .is_advanced = true,
     .option_values = {{0x00000000, "Off"}, {0x00000001, "On"}, {0x00000002, "Ultra"}}},
    {.user_friendly_name = "Ultra Low Latency - Enabled",
     .hex_setting_id = ULL_ENABLED_ID,
     .min_required_driver_version = 43000,  // 430.00
     .default_value = 0x00000000,
     .is_advanced = true,
     .option_values = {{0x00000000, "Off"}, {0x00000001, "On"}}},
}};

static std::uint32_t ResolveSettingIdByDriverName(const char* nameUtf8);
static std::uint32_t ResolveSettingIdByDriverName(const wchar_t* nameWide);

static const SettingData* FindSettingData(std::uint32_t settingId) {
    for (const auto& sd : k_settings_data) {
        if (sd.hex_setting_id == settingId) {
            return &sd;
        }
        if (sd.resolve_id_from_driver) {
            const std::uint32_t resolved = sd.driver_lookup_name_wide != nullptr
                                               ? ResolveSettingIdByDriverName(sd.driver_lookup_name_wide)
                                               : ResolveSettingIdByDriverName(sd.user_friendly_name);
            if (resolved == settingId) {
                return &sd;
            }
        }
    }
    return nullptr;
}

static std::string FormatImportantValue(NvU32 settingId, NvU32 value) {
    const SettingData* sd = FindSettingData(settingId);
    if (sd != nullptr && !sd->option_values.empty()) {
        if (sd->is_bit_field) {
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
        for (const auto& opt : sd->option_values) {
            if (opt.first == value) {
                return std::string(opt.second);
            }
        }
    }
    // Fallbacks for values not in option_values (e.g. PRERENDERLIMIT numeric, REFRESH_RATE low-latency)
    switch (settingId) {
        case FRL_FPS_ID:
            if (value == 0) {
                return "Off";
            }
            if (value >= 20 && value <= 1000) {
                std::ostringstream o;
                o << value << " FPS";
                return o.str();
            }
            break;
        case PRERENDERLIMIT_ID:
            if (value != PRERENDERLIMIT_APP_CONTROLLED) {
                std::ostringstream o;
                o << value;
                return o.str();
            }
            return "App controlled";
        case REFRESH_RATE_OVERRIDE_ID:
            if ((value & REFRESH_RATE_OVERRIDE_LOW_LATENCY_RR_MASK) != 0) {
                std::ostringstream o;
                o << "Low latency (0x" << std::hex << value << ")";
                return o.str();
            }
            break;
        default: break;
    }
    std::ostringstream oss;
    oss << "0x" << std::hex << value << " (" << std::dec << value << ")";
    return oss.str();
}

static void ReadImportantSettings(NvDRSSessionHandle hSession, NvDRSProfileHandle hProfile,
                                  std::vector<ImportantProfileSetting>& out) {
    for (std::size_t i = 0; i < k_num_important_settings; ++i) {
        const SettingData& def = k_settings_data[i];
        const std::uint32_t effective_id =
            def.resolve_id_from_driver
                ? (def.driver_lookup_name_wide != nullptr ? ResolveSettingIdByDriverName(def.driver_lookup_name_wide)
                                                          : ResolveSettingIdByDriverName(def.user_friendly_name))
                : def.hex_setting_id;
        ImportantProfileSetting entry;
        entry.label = def.user_friendly_name != nullptr ? def.user_friendly_name : "";
        entry.is_bit_field = def.is_bit_field;
        entry.requires_admin = def.requires_admin;
        entry.min_required_driver_version = def.min_required_driver_version;
        NVDRS_SETTING s = {0};
        s.version = NVDRS_SETTING_VER;
        entry.default_value = def.default_value;
        if (effective_id == 0 && def.resolve_id_from_driver) {
            std::ostringstream oss;
            oss << "Not available for this driver (queried name: \""
                << (def.user_friendly_name ? def.user_friendly_name : "") << "\", fallback ID: 0x" << std::hex
                << def.hex_setting_id << ")";
            entry.value = oss.str();
            entry.setting_id = def.hex_setting_id;  // Use fallback ID so tooltip and UI show the real key
            entry.value_id = def.default_value;
            out.push_back(std::move(entry));
            continue;
        }
        if (display_commander::nvapi_loader::DRS_GetSetting(NvApi(), hSession, hProfile, effective_id, &s)
            != NVAPI_OK) {
            std::string defaultStr = FormatImportantValue(effective_id, def.default_value);
            entry.value = "Use global - " + defaultStr + " (Default)";
            entry.setting_id = effective_id;
            entry.value_id = def.default_value;
            entry.set_in_profile = false;
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
        entry.value = FormatImportantValue(effective_id, s.u32CurrentValue);
        entry.setting_id = effective_id;
        entry.value_id = s.u32CurrentValue;
        entry.set_in_profile = true;
        out.push_back(std::move(entry));
    }
}

static void ReadAdvancedSettings(NvDRSSessionHandle hSession, NvDRSProfileHandle hProfile,
                                 std::vector<ImportantProfileSetting>& out) {
    for (std::size_t i = k_num_important_settings; i < k_settings_data.size(); ++i) {
        const SettingData& def = k_settings_data[i];
        const std::uint32_t effective_id =
            def.resolve_id_from_driver
                ? (def.driver_lookup_name_wide != nullptr ? ResolveSettingIdByDriverName(def.driver_lookup_name_wide)
                                                          : ResolveSettingIdByDriverName(def.user_friendly_name))
                : def.hex_setting_id;
        ImportantProfileSetting entry;
        entry.label = def.user_friendly_name != nullptr ? def.user_friendly_name : "";
        entry.is_bit_field = def.is_bit_field;
        entry.requires_admin = def.requires_admin;
        entry.min_required_driver_version = def.min_required_driver_version;
        NVDRS_SETTING s = {0};
        s.version = NVDRS_SETTING_VER;
        entry.default_value = def.default_value;
        if (effective_id == 0 && def.resolve_id_from_driver) {
            std::ostringstream oss;
            oss << "Not available for this driver (queried name: \""
                << (def.user_friendly_name ? def.user_friendly_name : "") << "\", fallback ID: 0x" << std::hex
                << def.hex_setting_id << ")";
            entry.value = oss.str();
            entry.setting_id = def.hex_setting_id;  // Use fallback ID so tooltip and UI show the real key
            entry.value_id = def.default_value;
            out.push_back(std::move(entry));
            continue;
        }
        if (display_commander::nvapi_loader::DRS_GetSetting(NvApi(), hSession, hProfile, effective_id, &s)
            != NVAPI_OK) {
            std::string defaultStr = FormatImportantValue(effective_id, def.default_value);
            entry.value = "Use global - " + defaultStr + " (Default)";
            entry.setting_id = effective_id;
            entry.value_id = def.default_value;
            entry.set_in_profile = false;
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
        entry.value = FormatImportantValue(effective_id, s.u32CurrentValue);
        entry.setting_id = effective_id;
        entry.value_id = s.u32CurrentValue;
        entry.set_in_profile = true;
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
        if (NvApi()->DRS_EnumSettings(hSession, hProfile, startIndex, &count, batch.data()) != NVAPI_OK) {
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
            const SettingData* sd = FindSettingData(static_cast<std::uint32_t>(s.settingId));
            entry.requires_admin = (sd != nullptr && sd->requires_admin);
            entry.min_required_driver_version = (sd != nullptr ? sd->min_required_driver_version : 0);
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

// Resolve setting ID from driver by name (NvApi()->DRS_GetSettingIdFromName). NVAPI expects NvAPI_UnicodeString
// (UTF-16). Prefer the wide-string overload (L"…") to avoid UTF-8→wide conversion issues. Returns 0 if not found.
static std::uint32_t ResolveSettingIdByDriverName(const wchar_t* nameWide) {
    if (nameWide == nullptr || nameWide[0] == L'\0') {
        return 0;
    }
    NvAPI_UnicodeString nameUnicode;
    WideToNvApiUnicode(std::wstring(nameWide), nameUnicode);
    NvU32 settingId = 0;
    if (NvApi()->DRS_GetSettingIdFromName(nameUnicode, &settingId) != NVAPI_OK) {
        return 0;
    }
    return static_cast<std::uint32_t>(settingId);
}

static std::uint32_t ResolveSettingIdByDriverName(const char* nameUtf8) {
    if (nameUtf8 == nullptr || nameUtf8[0] == '\0') {
        return 0;
    }
    int n = MultiByteToWideChar(CP_UTF8, 0, nameUtf8, -1, nullptr, 0);
    if (n <= 0) {
        return 0;
    }
    std::wstring nameW(static_cast<size_t>(n), L'\0');
    if (MultiByteToWideChar(CP_UTF8, 0, nameUtf8, -1, nameW.data(), n) == 0) {
        return 0;
    }
    nameW.resize(nameW.size() - 1);  // drop null
    return ResolveSettingIdByDriverName(nameW.c_str());
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
    if (NvApi()->GetErrorMessage(st, buf) == NVAPI_OK && buf[0] != '\0') {
        std::ostringstream o;
        o << step << ": " << buf << " (0x" << std::hex << static_cast<unsigned>(st) << ")";
        return o.str();
    }
    std::ostringstream o;
    o << step << ": NVAPI 0x" << std::hex << static_cast<unsigned>(st);
    return o.str();
}

// SetSetting failure: when NVAPI_SETTING_NOT_FOUND, append key and value so user knows what was tried.
static std::string MakeSetSettingError(std::uint32_t settingId, std::uint32_t value, NvAPI_Status st) {
    std::string msg = MakeNvapiError("SetSetting", st);
    if (st == NVAPI_SETTING_NOT_FOUND) {
        std::ostringstream o;
        o << msg << " [key 0x" << std::hex << settingId << ", value 0x" << value << "]";
        return o.str();
    }
    return msg;
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
    if (!NvApi()) {
        result.error = "NVAPI not loaded (call EnsureNvApiInitialized first).";
        return result;
    }
    std::wstring exePath = GetCurrentProcessPathW();
    if (exePath.empty()) {
        result.error = "GetModuleFileName failed";
        return result;
    }
    result.current_exe_path = WideToUtf8(exePath.c_str());
    const wchar_t* base = wcsrchr(exePath.c_str(), L'\\');
    result.current_exe_name = WideToUtf8(base ? base + 1 : exePath.c_str());

    NvDRSSessionHandle hSession = nullptr;
    NvAPI_Status st = NvApi()->DRS_CreateSession(&hSession);
    if (st != NVAPI_OK) {
        result.error = MakeNvapiError("CreateSession", st);
        return result;
    }
    st = NvApi()->DRS_LoadSettings(hSession);
    if (st != NVAPI_OK) {
        NvApi()->DRS_DestroySession(hSession);
        result.error = MakeNvapiError("LoadSettings", st);
        return result;
    }

    NvDRSProfileHandle hProfile = nullptr;
    NVDRS_APPLICATION app = {0};
    if (!FindApplicationByPathForCurrentExe(hSession, &hProfile, &app)) {
        result.success = true;
        NvApi()->DRS_DestroySession(hSession);
        return result;
    }

    NVDRS_PROFILE profileInfo = {0};
    profileInfo.version = NVDRS_PROFILE_VER;
    if (NvApi()->DRS_GetProfileInfo(hSession, hProfile, &profileInfo) == NVAPI_OK) {
        MatchedProfileEntry entry = MakeMatchedProfileEntry(profileInfo, app);
        result.matching_profiles.push_back(std::move(entry));
        result.matching_profile_names.push_back(WideToUtf8(reinterpret_cast<const wchar_t*>(profileInfo.profileName)));
    }
    ReadImportantSettings(hSession, hProfile, result.important_settings);
    ReadAdvancedSettings(hSession, hProfile, result.advanced_settings);
    ReadAllSettings(hSession, hProfile, result.all_settings);
    result.success = true;
    NvApi()->DRS_DestroySession(hSession);
    return result;
}

// Builds list of all driver settings with current profile value and driver default. Used by
// GetDriverSettingsWithProfileValues(). Also appends settings that are in the profile but not in the driver's
// recognized list (known_to_driver = false).
static std::vector<ImportantProfileSetting> GetDriverSettingsWithProfileValuesImpl() {
    std::vector<ImportantProfileSetting> out;
    std::vector<DriverAvailableSetting> driverList = GetDriverAvailableSettings();
    if (driverList.empty()) {
        return out;
    }
    std::set<std::uint32_t> driverIds;
    for (const DriverAvailableSetting& drv : driverList) {
        driverIds.insert(drv.setting_id);
    }

    NvDRSSessionHandle hSession = nullptr;
    NvAPI_Status st = NvApi()->DRS_CreateSession(&hSession);
    if (st != NVAPI_OK) {
        return out;
    }
    st = NvApi()->DRS_LoadSettings(hSession);
    if (st != NVAPI_OK) {
        NvApi()->DRS_DestroySession(hSession);
        return out;
    }
    NvDRSProfileHandle hProfile = nullptr;
    NVDRS_APPLICATION app = {0};
    const bool hasProfile = FindApplicationByPathForCurrentExe(hSession, &hProfile, &app);

    NVDRS_SETTING nvSetting;
    NVDRS_SETTING_VALUES vals;
    constexpr NvU32 kMaxVal = NVAPI_SETTING_MAX_VALUES;
    for (const DriverAvailableSetting& drv : driverList) {
        ImportantProfileSetting s;
        s.setting_id = drv.setting_id;
        s.label = drv.name;
        s.default_value = 0;
        s.is_bit_field = (drv.setting_id == NVPI_SMOOTH_MOTION_ALLOWED_APIS_ID);
        s.known_to_driver = true;
        const SettingData* sd = FindSettingData(drv.setting_id);
        s.requires_admin = (sd != nullptr && sd->requires_admin);
        s.min_required_driver_version = (sd != nullptr ? sd->min_required_driver_version : 0);

        memset(&vals, 0, sizeof(vals));
        vals.version = NVDRS_SETTING_VALUES_VER;
        NvU32 maxNum = kMaxVal;
        if (NvApi()->DRS_EnumAvailableSettingValues(drv.setting_id, &maxNum, &vals) == NVAPI_OK
            && vals.settingType == NVDRS_DWORD_TYPE) {
            s.default_value = vals.u32DefaultValue;
        }

        if (hasProfile && hProfile != nullptr) {
            memset(&nvSetting, 0, sizeof(nvSetting));
            nvSetting.version = NVDRS_SETTING_VER;
            if (display_commander::nvapi_loader::DRS_GetSetting(NvApi(), hSession, hProfile, drv.setting_id, &nvSetting)
                    == NVAPI_OK
                && nvSetting.settingType == NVDRS_DWORD_TYPE) {
                s.value_id = nvSetting.u32CurrentValue;
                s.value = FormatImportantValue(drv.setting_id, nvSetting.u32CurrentValue);
                s.set_in_profile = true;
            } else {
                s.value = "Use global - " + FormatImportantValue(drv.setting_id, s.default_value) + " (Default)";
                s.value_id = s.default_value;
                s.set_in_profile = false;
            }
        } else {
            s.value = "Use global - " + FormatImportantValue(drv.setting_id, s.default_value) + " (Default)";
            s.value_id = s.default_value;
            s.set_in_profile = false;
        }
        out.push_back(std::move(s));
    }

    // Append settings that are in the profile but not in the driver's recognized list (unknown keys with values).
    if (hasProfile && hProfile != nullptr) {
        constexpr NvU32 kBatchSize = 64;
        std::vector<NVDRS_SETTING> batch(kBatchSize);
        for (NvU32 startIndex = 0;;) {
            for (auto& b : batch) {
                memset(&b, 0, sizeof(b));
                b.version = NVDRS_SETTING_VER;
            }
            NvU32 count = kBatchSize;
            if (NvApi()->DRS_EnumSettings(hSession, hProfile, startIndex, &count, batch.data()) != NVAPI_OK) {
                break;
            }
            if (count == 0) {
                break;
            }
            for (NvU32 i = 0; i < count; ++i) {
                const NVDRS_SETTING& ps = batch[i];
                if (driverIds.count(static_cast<std::uint32_t>(ps.settingId)) != 0) {
                    continue;
                }
                ImportantProfileSetting s;
                s.setting_id = static_cast<std::uint32_t>(ps.settingId);
                s.label = WideToUtf8(reinterpret_cast<const wchar_t*>(ps.settingName));
                if (s.label.empty()) {
                    std::ostringstream o;
                    o << "0x" << std::hex << ps.settingId;
                    s.label = o.str();
                }
                s.value = FormatSettingValue(ps, WideToUtf8);
                s.value_id = (ps.settingType == NVDRS_DWORD_TYPE) ? static_cast<std::uint32_t>(ps.u32CurrentValue) : 0;
                s.default_value = 0;
                s.is_bit_field = false;
                s.known_to_driver = false;
                out.push_back(std::move(s));
            }
            startIndex += count;
            if (count < kBatchSize) {
                break;
            }
        }
    }

    NvApi()->DRS_DestroySession(hSession);
    return out;
}

}  // namespace

std::string GetSettingDriverDebugTooltip(std::uint32_t settingId, const std::string& displayNameUtf8) {
    std::ostringstream o;
    o << "Key: 0x" << std::hex << settingId << std::dec;
    NvAPI_UnicodeString nameBuf;
    memset(&nameBuf, 0, sizeof(nameBuf));
    if (display_commander::nvapi_loader::DRS_GetSettingNameFromId(NvApi(), static_cast<NvU32>(settingId), &nameBuf)
        == NVAPI_OK) {
        const wchar_t* wsz = reinterpret_cast<const wchar_t*>(nameBuf);
        std::string nameFromId;
        if (wsz && wsz[0]) {
            const int len = ::WideCharToMultiByte(CP_UTF8, 0, wsz, -1, nullptr, 0, nullptr, nullptr);
            if (len > 0) {
                nameFromId.resize(static_cast<size_t>(len) - 1);
                ::WideCharToMultiByte(CP_UTF8, 0, wsz, -1, &nameFromId[0], len, nullptr, nullptr);
            }
        }
        if (!nameFromId.empty()) {
            o << "\nGetSettingNameFromId: " << nameFromId;
        } else {
            o << "\nGetSettingNameFromId: (empty)";
        }
    } else {
        o << "\nGetSettingNameFromId: (failed)";
    }
    if (!displayNameUtf8.empty()) {
        int n = MultiByteToWideChar(CP_UTF8, 0, displayNameUtf8.c_str(), -1, nullptr, 0);
        if (n > 0) {
            std::wstring nameW(static_cast<size_t>(n), L'\0');
            if (MultiByteToWideChar(CP_UTF8, 0, displayNameUtf8.c_str(), -1, nameW.data(), n) != 0) {
                nameW.resize(nameW.size() - 1);
                NvAPI_UnicodeString nameUnicode;
                memset(&nameUnicode, 0, sizeof(nameUnicode));
                const size_t toCopy = (std::min)(nameW.size(), static_cast<size_t>(NVAPI_UNICODE_STRING_MAX - 1));
                if (toCopy > 0) {
                    memcpy(nameUnicode, nameW.c_str(), toCopy * sizeof(NvU16));
                }
                NvU32 idFromName = 0;
                if (NvApi()->DRS_GetSettingIdFromName(nameUnicode, &idFromName) == NVAPI_OK) {
                    o << "\nGetSettingIdFromName(\"" << displayNameUtf8 << "\"): 0x" << std::hex << idFromName
                      << std::dec;
                } else {
                    o << "\nGetSettingIdFromName(\"" << displayNameUtf8 << "\"): (failed)";
                }
            }
        }
    }
    return o.str();
}

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

// Single call site for NvApi()->DRS_FindApplicationByName: finds profile by current exe full path.
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
    NvAPI_Status st = NvApi()->DRS_FindApplicationByName(hSession, fullPathBuf, phProfile, pApp);
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

ProfileFpsLimitResult GetProfileFpsLimit() {
    ProfileFpsLimitResult out;
    NvidiaProfileSearchResult r = GetCachedProfileSearchResult();
    if (!r.success) {
        out.error = r.error;
        return out;
    }
    if (r.matching_profiles.empty()) {
        return out;
    }
    out.has_profile = true;
    out.profile_name =
        r.matching_profile_names.empty() ? r.matching_profiles[0].profile_name : r.matching_profile_names[0];
    for (const ImportantProfileSetting& s : r.important_settings) {
        if (s.setting_id == FRL_FPS_ID) {
            out.value = s.value_id;
            break;
        }
    }
    // If FRL_FPS not found in list, value stays 0 (Off)
    return out;
}

std::pair<bool, std::string> SetProfileFpsLimit(std::uint32_t value) { return SetProfileSetting(FRL_FPS_ID, value); }

std::vector<std::pair<std::uint32_t, std::string>> GetProfileFpsLimitOptions() {
    return GetSettingAvailableValues(FRL_FPS_ID);
}

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
    const SettingData* sd = FindSettingData(settingId);
    if (sd != nullptr && !sd->option_values.empty()) {
        ValueList list;
        list.reserve(sd->option_values.size());
        for (const auto& opt : sd->option_values) {
            list.push_back({opt.first, std::string(opt.second)});
        }
        return list;
    }
    if (settingId == NVPI_SMOOTH_MOTION_ALLOWED_APIS_ID) {
        return GetSmoothMotionAllowedApisValues();
    }
    if (settingId == FRL_FPS_ID) {
        ValueList list;
        list.reserve(982u);
        list.push_back({0, "Off"});
        for (std::uint32_t fps = 20; fps <= 1000; ++fps) {
            list.push_back({fps, std::to_string(fps) + " FPS"});
        }
        return list;
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
    if (NvApi()->DRS_EnumAvailableSettingValues(static_cast<NvU32>(settingId), &maxNum, &vals) != NVAPI_OK) {
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

// Helper for driver enumeration/dump: NvAPI_UnicodeString (UTF-16) to UTF-8. Used outside anonymous namespace.
static std::string SettingNameToUtf8(const NvAPI_UnicodeString& name) {
    const wchar_t* wsz = reinterpret_cast<const wchar_t*>(name);
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

std::vector<DriverAvailableSetting> GetDriverAvailableSettings() {
    if (!NvApi()) {
        return {};
    }
    std::vector<DriverAvailableSetting> out;
    constexpr NvU32 kMaxIds = 4096u;
    std::vector<NvU32> ids(kMaxIds);
    NvU32 count = kMaxIds;
    NvAPI_Status st = NvApi()->DRS_EnumAvailableSettingIds(ids.data(), &count);
    if (st == NVAPI_END_ENUMERATION) {
        // Buffer too small; retry with returned count if reasonable.
        if (count > 0u && count <= 65536u) {
            ids.resize(count);
            NvU32 c2 = count;
            st = NvApi()->DRS_EnumAvailableSettingIds(ids.data(), &c2);
            if (st == NVAPI_OK) {
                count = c2;
            } else {
                return out;
            }
        } else {
            return out;
        }
    } else if (st != NVAPI_OK) {
        return out;
    }
    out.reserve(count);
    NvAPI_UnicodeString nameBuf;
    for (NvU32 i = 0; i < count; ++i) {
        memset(&nameBuf, 0, sizeof(nameBuf));
        if (display_commander::nvapi_loader::DRS_GetSettingNameFromId(NvApi(), ids[i], &nameBuf) != NVAPI_OK) {
            // Fallback: ID only (e.g. setting not found in this driver)
            std::ostringstream o;
            o << "0x" << std::hex << ids[i];
            out.push_back({ids[i], o.str()});
            continue;
        }
        std::string nameUtf8 = SettingNameToUtf8(nameBuf);
        if (nameUtf8.empty()) {
            std::ostringstream o;
            o << "0x" << std::hex << ids[i];
            nameUtf8 = o.str();
        }
        out.push_back({ids[i], std::move(nameUtf8)});
    }
    return out;
}

static const char* SettingTypeToString(NVDRS_SETTING_TYPE t) {
    switch (t) {
        case NVDRS_DWORD_TYPE:   return "DWORD";
        case NVDRS_BINARY_TYPE:  return "BINARY";
        case NVDRS_STRING_TYPE:  return "STRING";
        case NVDRS_WSTRING_TYPE: return "WSTRING";
        default:                 return "?";
    }
}

std::pair<bool, std::string> DumpDriverSettingsToFile(const std::string& filePath) {
    std::vector<DriverAvailableSetting> settings = GetDriverAvailableSettings();
    if (settings.empty()) {
        return {false, "No driver settings enumerated (NVAPI not initialized or no NVIDIA GPU?)."};
    }
    std::ofstream f(filePath, std::ios::out | std::ios::trunc);
    if (!f.is_open() || !f) {
        return {false, "Could not open file for writing: " + filePath};
    }
    // Header
    f << "# NVIDIA Driver Settings Dump - all setting IDs recognized by the current driver.\n";
    f << "# Format: SettingID(hex)\tName\tType\tNumValues\tValue1(hex), Value2(hex), ...\n";
    f << "# One line per setting. Names may contain spaces; fields separated by tab.\n";

    NVDRS_SETTING_VALUES vals;
    constexpr NvU32 kMaxVal = NVAPI_SETTING_MAX_VALUES;
    for (const DriverAvailableSetting& s : settings) {
        // Escape name: replace tab and newline so one line per setting
        std::string nameEsc = s.name;
        for (char& c : nameEsc) {
            if (c == '\t') c = ' ';
            if (c == '\r' || c == '\n') c = ' ';
        }
        f << "0x" << std::hex << s.setting_id << "\t" << nameEsc << "\t";

        memset(&vals, 0, sizeof(vals));
        vals.version = NVDRS_SETTING_VALUES_VER;
        NvU32 maxNum = kMaxVal;
        if (NvApi()->DRS_EnumAvailableSettingValues(s.setting_id, &maxNum, &vals) != NVAPI_OK) {
            f << "?\t0\n";
            continue;
        }
        f << SettingTypeToString(vals.settingType) << "\t" << std::dec << vals.numSettingValues << "\t";
        if (vals.settingType == NVDRS_DWORD_TYPE && vals.numSettingValues > 0) {
            for (NvU32 i = 0; i < vals.numSettingValues && i < kMaxVal; ++i) {
                if (i != 0) f << ", ";
                f << "0x" << std::hex << vals.settingValues[i].u32Value;
            }
        }
        f << "\n";
    }
    if (!f) {
        return {false, "Write error while dumping to " + filePath};
    }
    return {true, ""};
}

std::vector<ImportantProfileSetting> GetDriverSettingsWithProfileValues() {
    return GetDriverSettingsWithProfileValuesImpl();
}

std::pair<bool, std::string> DeleteProfileSettingForCurrentExe(std::uint32_t settingId) {
    std::wstring path = GetCurrentProcessPathW();
    if (path.empty()) {
        return {false, "GetModuleFileName failed."};
    }
    return SetOrDeleteProfileSettingForExe(path, settingId, true, 0);
}

// Setting names for NVPI custom settings (not in NvApiDriverSettings.h). Required when creating the setting in a
// profile.
static const wchar_t k_smoothMotionAllowedApisName[] = L"Smooth Motion - Allowed APIs";
static const wchar_t k_rtxHdrEnableName[] = L"RTX HDR - Enable";

std::pair<bool, std::string> SetProfileSetting(std::uint32_t settingId, std::uint32_t value) {
    if (!NvApi()) {
        return {false, "NVAPI not loaded (call EnsureNvApiInitialized first)."};
    }
    NvDRSSessionHandle hSession = nullptr;
    NvAPI_Status st = NvApi()->DRS_CreateSession(&hSession);
    if (st != NVAPI_OK) {
        return {false, MakeNvapiError("CreateSession", st)};
    }
    st = NvApi()->DRS_LoadSettings(hSession);
    if (st != NVAPI_OK) {
        NvApi()->DRS_DestroySession(hSession);
        return {false, MakeNvapiError("LoadSettings", st)};
    }
    NvDRSProfileHandle hProfile = nullptr;
    NVDRS_APPLICATION app = {0};
    if (!FindApplicationByPathForCurrentExe(hSession, &hProfile, &app)) {
        NvApi()->DRS_DestroySession(hSession);
        return {false, "No profile matches current exe (add this game to a profile first)."};
    }
    NVDRS_SETTING s;
    memset(&s, 0, sizeof(s));
    s.version = NVDRS_SETTING_VER;
    s.settingId = static_cast<NvU32>(settingId);
    s.settingType = NVDRS_DWORD_TYPE;
    s.u32CurrentValue = static_cast<NvU32>(value);

    // Get existing setting so we pass a full struct (including settingName); driver may require it for SetSetting.
    if (display_commander::nvapi_loader::DRS_GetSetting(NvApi(), hSession, hProfile, static_cast<NvU32>(settingId), &s)
        == NVAPI_OK) {
        if (s.settingType == NVDRS_DWORD_TYPE) {
            s.u32CurrentValue = static_cast<NvU32>(value);
        }
    } else {
        // Setting not in profile yet: get name from driver (covers driver-resolved IDs e.g. RTX HDR); else known custom
        // names.
        if (display_commander::nvapi_loader::DRS_GetSettingNameFromId(NvApi(), static_cast<NvU32>(settingId),
                                                                      &s.settingName)
            != NVAPI_OK) {
            if (settingId == NVPI_SMOOTH_MOTION_ALLOWED_APIS_ID) {
                WideToNvApiUnicode(k_smoothMotionAllowedApisName, s.settingName);
            } else if (settingId == NVPI_RTX_HDR_ENABLE_ID) {
                WideToNvApiUnicode(k_rtxHdrEnableName, s.settingName);
            }
        }
    }

    st = display_commander::nvapi_loader::DRS_SetSetting(NvApi(), hSession, hProfile, &s);
    if (st != NVAPI_OK) {
        NvApi()->DRS_DestroySession(hSession);
        return {false, MakeSetSettingError(settingId, value, st)};
    }
    st = NvApi()->DRS_SaveSettings(hSession);
    if (st != NVAPI_OK) {
        NvApi()->DRS_DestroySession(hSession);
        return {false, MakeNvapiError("SaveSettings", st)};
    }
    NvApi()->DRS_DestroySession(hSession);
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

std::pair<bool, std::string> SetOrDeleteProfileSettingForExe(const std::wstring& exePath, std::uint32_t settingId,
                                                             bool deleteSetting, std::uint32_t valueIfSet) {
    if (exePath.empty()) {
        return {false, "Executable path is empty."};
    }
    if (!NvApi()) {
        return {false, "NVAPI not loaded (call EnsureNvApiInitialized first)."};
    }
    NvDRSSessionHandle hSession = nullptr;
    NvAPI_Status st = NvApi()->DRS_CreateSession(&hSession);
    if (st != NVAPI_OK) {
        return {false, MakeNvapiError("CreateSession", st)};
    }
    st = NvApi()->DRS_LoadSettings(hSession);
    if (st != NVAPI_OK) {
        NvApi()->DRS_DestroySession(hSession);
        return {false, MakeNvapiError("LoadSettings", st)};
    }

    std::wstring pathNorm = NormalizePath(exePath);
    NvAPI_UnicodeString pathUnicode;
    WideToNvApiUnicode(pathNorm, pathUnicode);

    NvDRSProfileHandle hProfile = nullptr;
    NVDRS_APPLICATION app = {0};
    memset(&app, 0, sizeof(app));
    app.version = NVDRS_APPLICATION_VER;
    st = NvApi()->DRS_FindApplicationByName(hSession, pathUnicode, &hProfile, &app);
    if (st != NVAPI_OK || hProfile == nullptr) {
        NvApi()->DRS_DestroySession(hSession);
        return {false, "No NVIDIA driver profile found for this executable. Add the game to a profile first."};
    }

    if (deleteSetting) {
        st = display_commander::nvapi_loader::DRS_DeleteProfileSetting(NvApi(), hSession, hProfile,
                                                                       static_cast<NvU32>(settingId));
        if (st != NVAPI_OK) {
            NvApi()->DRS_DestroySession(hSession);
            return {false, MakeNvapiError("DeleteProfileSetting", st)};
        }
    } else {
        NVDRS_SETTING s;
        memset(&s, 0, sizeof(s));
        s.version = NVDRS_SETTING_VER;
        s.settingId = static_cast<NvU32>(settingId);
        s.settingType = NVDRS_DWORD_TYPE;
        s.u32CurrentValue = static_cast<NvU32>(valueIfSet);

        if (display_commander::nvapi_loader::DRS_GetSetting(NvApi(), hSession, hProfile, static_cast<NvU32>(settingId),
                                                            &s)
            == NVAPI_OK) {
            if (s.settingType == NVDRS_DWORD_TYPE) {
                s.u32CurrentValue = static_cast<NvU32>(valueIfSet);
            }
        } else {
            if (display_commander::nvapi_loader::DRS_GetSettingNameFromId(NvApi(), static_cast<NvU32>(settingId),
                                                                          &s.settingName)
                != NVAPI_OK) {
                if (settingId == NVPI_SMOOTH_MOTION_ALLOWED_APIS_ID) {
                    WideToNvApiUnicode(k_smoothMotionAllowedApisName, s.settingName);
                } else if (settingId == NVPI_RTX_HDR_ENABLE_ID) {
                    WideToNvApiUnicode(k_rtxHdrEnableName, s.settingName);
                }
            }
        }

        st = display_commander::nvapi_loader::DRS_SetSetting(NvApi(), hSession, hProfile, &s);
        if (st != NVAPI_OK) {
            NvApi()->DRS_DestroySession(hSession);
            return {false, MakeSetSettingError(settingId, valueIfSet, st)};
        }
    }

    st = NvApi()->DRS_SaveSettings(hSession);
    if (st != NVAPI_OK) {
        NvApi()->DRS_DestroySession(hSession);
        return {false, MakeNvapiError("SaveSettings", st)};
    }
    NvApi()->DRS_DestroySession(hSession);
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
    NvAPI_Status status = NvApi()->DRS_CreateSession(&hSession);
    if (status != NVAPI_OK) {
        if (status == NVAPI_API_NOT_INITIALIZED) {
            return {false, "NVAPI not available (NVIDIA GPU required)"};
        }
        return {false, "DRS CreateSession failed"};
    }
    if (NvApi()->DRS_LoadSettings(hSession) != NVAPI_OK) {
        NvApi()->DRS_DestroySession(hSession);
        return {false, "DRS LoadSettings failed"};
    }

    NvDRSProfileHandle hProfile = nullptr;
    NVDRS_APPLICATION app = {0};
    if (FindApplicationByPathForCurrentExe(hSession, &hProfile, &app)) {
        NvApi()->DRS_DestroySession(hSession);
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

    NvAPI_Status createSt = NvApi()->DRS_CreateProfile(hSession, &profileData, &hProfile);
    if (createSt != NVAPI_OK) {
        NvApi()->DRS_DestroySession(hSession);
        return {false, "DRS CreateProfile failed"};
    }

    app.version = NVDRS_APPLICATION_VER;
    app.isPredefined = 0;
    app.isMetro = 0;
    WideToNvApiUnicode(fullPathNorm, app.appName);
    WideToNvApiUnicode(exeNameW, app.userFriendlyName);

    createSt = NvApi()->DRS_CreateApplication(hSession, hProfile, &app);
    if (createSt != NVAPI_OK) {
        NvApi()->DRS_DestroySession(hSession);
        return {false, "DRS CreateApplication failed"};
    }

    if (NvApi()->DRS_SaveSettings(hSession) != NVAPI_OK) {
        NvApi()->DRS_DestroySession(hSession);
        return {false, "DRS SaveSettings failed"};
    }
    NvApi()->DRS_DestroySession(hSession);
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
    NvAPI_Status st = NvApi()->DRS_CreateSession(&hSession);
    if (st != NVAPI_OK) {
        return {false, MakeNvapiError("CreateSession", st)};
    }
    st = NvApi()->DRS_LoadSettings(hSession);
    if (st != NVAPI_OK) {
        NvApi()->DRS_DestroySession(hSession);
        return {false, MakeNvapiError("LoadSettings", st)};
    }

    NvDRSProfileHandle hProfile = nullptr;
    NVDRS_APPLICATION app = {0};
    if (!FindApplicationByPathForCurrentExe(hSession, &hProfile, &app)) {
        NvApi()->DRS_DestroySession(hSession);
        return {false, "No profile found for current exe."};
    }
    NVDRS_PROFILE profileInfo = {0};
    profileInfo.version = NVDRS_PROFILE_VER;
    if (NvApi()->DRS_GetProfileInfo(hSession, hProfile, &profileInfo) != NVAPI_OK) {
        NvApi()->DRS_DestroySession(hSession);
        return {false, "GetProfileInfo failed."};
    }
    const wchar_t* profileNameW = reinterpret_cast<const wchar_t*>(profileInfo.profileName);
    const std::string profileNameUtf8 = WideToUtf8(profileNameW);
    const size_t prefixLen = sizeof(k_displayCommanderProfilePrefix) - 1;
    if (profileNameUtf8.size() < prefixLen
        || profileNameUtf8.compare(0, prefixLen, k_displayCommanderProfilePrefix) != 0) {
        NvApi()->DRS_DestroySession(hSession);
        return {false, "Display Commander profile not found for this exe (profile exists but is not ours)."};
    }

    st = NvApi()->DRS_DeleteProfile(hSession, hProfile);
    if (st != NVAPI_OK) {
        NvApi()->DRS_DestroySession(hSession);
        return {false, MakeNvapiError("DeleteProfile", st)};
    }
    st = NvApi()->DRS_SaveSettings(hSession);
    if (st != NVAPI_OK) {
        NvApi()->DRS_DestroySession(hSession);
        return {false, MakeNvapiError("SaveSettings", st)};
    }
    NvApi()->DRS_DestroySession(hSession);
    InvalidateProfileSearchCache();
    return {true, ""};
}

std::vector<std::uint32_t> GetRtxHdrSettingIds() {
    // Order: Smooth Motion first, then RTX HDR, then Max Pre-Rendered Frames (matches important_settings order).
    std::vector<std::uint32_t> ids = {
        NVPI_SMOOTH_MOTION_ALLOWED_APIS_ID,
        NVPI_SMOOTH_MOTION_ENABLE_50_ID,
        NVPI_RTX_HDR_ENABLE_ID,
        NVPI_RTX_HDR_DEBANDING_ID,
        NVPI_RTX_HDR_ALLOW_ID,
        NVPI_RTX_HDR_CONTRAST_ID,
        NVPI_RTX_HDR_MIDDLE_GREY_ID,
        NVPI_RTX_HDR_PEAK_BRIGHTNESS_ID,
        NVPI_RTX_HDR_SATURATION_ID,
        PRERENDERLIMIT_ID,  // Latency - Max Pre-Rendered Frames (NVIDIA); shown after RTX HDR on Main tab
        ULL_CPL_STATE_ID,   // Ultra Low Latency - CPL State (2 - Sync and Refresh); shown on Main tab NVIDIA Control
        ULL_ENABLED_ID,     // Ultra Low Latency - Enabled (2 - Sync and Refresh); shown on Main tab NVIDIA Control
    };
    // Hide requires_admin from Main tab except Smooth Motion - Allowed APIs (user requested to show it).
    ids.erase(std::remove_if(ids.begin(), ids.end(),
                             [](std::uint32_t id) {
                                 const SettingData* sd = FindSettingData(id);
                                 return sd != nullptr && sd->requires_admin && id != NVPI_SMOOTH_MOTION_ALLOWED_APIS_ID;
                             }),
              ids.end());
    return ids;
}

}  // namespace display_commander::nvapi
