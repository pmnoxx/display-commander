#pragma once

#define ImTextureID ImU64
#define DEBUG_LEVEL_0
#define WIN32_LEAN_AND_MEAN

#include <windows.h>

#include <windef.h>

#include <MinHook.h>
#include <atomic>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

// Forward declaration for HookType enum
namespace display_commanderhooks {
enum class HookType;
}

// Structs needed for utility functions
struct AspectRatio {
    int w;
    int h;
};

struct MonitorInfo {
    HMONITOR handle;
    MONITORINFOEXW info;
};

// Constants
extern const int WIDTH_OPTIONS[];
extern const int HEIGHT_OPTIONS[];
extern const AspectRatio ASPECT_OPTIONS[];

// Forward declarations for utility functions
RECT RectFromWH(int width, int height);
// Window state detection
AspectRatio GetAspectByIndex(int index);
int GetAspectWidthValue(int display_width);

// Monitor enumeration callback
BOOL CALLBACK MonitorEnumProc(HMONITOR hmon, HDC hdc, LPRECT rect, LPARAM lparam);

// XInput processing functions
void ProcessStickInputRadial(float& x, float& y, float deadzone, float max_input, float min_output);
void ProcessStickInputSquare(float& x, float& y, float deadzone, float max_input, float min_output);
float ProcessStickInput(float value, float deadzone, float max_input, float min_output);

// XInput thumbstick scaling helpers (handles asymmetric SHORT range: -32768 to 32767)
float ShortToFloat(SHORT value);
SHORT FloatToShort(float value);

// DLL version information
std::string GetDLLVersionString(const std::wstring& dllPath);

// DLSS preset support functions
bool isBetween(int major, int minor, int patch, int minMajor, int minMinor, int minPatch, int maxMajor, int maxMinor,
               int maxPatch);
std::string GetSupportedDLSSSRPresets(int major, int minor, int patch);
std::string GetSupportedDLSSSRPresetsFromVersionString(const std::string& versionString);
std::string GetSupportedDLSSRRPresets(int major, int minor, int patch);
std::string GetSupportedDLSSRRPresetsFromVersionString(const std::string& versionString);
std::vector<std::string> GetDLSSPresetOptions(const std::string& supportedPresets);
int GetDLSSPresetValue(const std::string& presetString);
std::string ConvertRenderPresetToLetter(
    int preset_value);         // Convert render preset number to letter (0=Default, 1=A, 2=B, etc.)
void TestDLSSPresetSupport();  // Test function for debugging

// Addon directory utilities
std::filesystem::path GetAddonDirectory();

// Default DLSS override folder: AppData\Local\Programs\Display Commander\dlss_override (centralized location)
std::filesystem::path GetDefaultDlssOverrideFolder();

// Legacy DLSS override folder: addon directory/dlss_override (fallback for migration)
std::filesystem::path GetLegacyDlssOverrideFolder();

// Effective default path when using a subfolder: base (dlss_override) or base/subfolder (subfolder empty = base only)
std::filesystem::path GetEffectiveDefaultDlssOverrideFolder(const std::string& subfolder);

// Subfolder names under the default DLSS override folder (for UI dropdown). Returns directory names only.
std::vector<std::string> GetDlssOverrideSubfolderNames();

// Create a subfolder under dlss_override (e.g. "310.5.2"). Sanitizes name (no path separators). Returns true on
// success. out_error optional; set on failure.
bool CreateDlssOverrideSubfolder(const std::string& subfolder_name, std::string* out_error = nullptr);

// Per-DLL state in the override folder: name, present, and version string (empty if missing).
struct DlssOverrideDllEntry {
    std::string name;  // e.g. "nvngx_dlss.dll"
    bool present = false;
    std::string version;  // file version if present, else empty
};
// Status of all 3 DLSS DLLs in the override folder; all_required_present = every enabled override has its DLL.
struct DlssOverrideDllStatus {
    bool all_required_present = true;
    std::vector<std::string> missing_dlls;   // required (enabled) but missing
    std::vector<DlssOverrideDllEntry> dlls;  // always 3 entries: nvngx_dlss, nvngx_dlssd, nvngx_dlssg
};
DlssOverrideDllStatus GetDlssOverrideFolderDllStatus(const std::string& folder_path, bool override_dlss,
                                                     bool override_dlss_fg, bool override_dlss_rr);

// Forward declaration for ReShade API types
namespace reshade {
namespace api {
enum class device_api;
}
}  // namespace reshade

// Graphics API version string conversion
const char* GetDeviceApiString(reshade::api::device_api api);
std::string GetDeviceApiVersionString(reshade::api::device_api api, uint32_t api_version);

// Rolling average (exponential moving average) calculation
// Formula: (new_value + (alpha - 1) * old_value) / alpha
// Default alpha=64 provides good smoothing for frame timing metrics
template <typename T>
inline T UpdateRollingAverage(T new_value, T old_value, int alpha = 64) {
    return (new_value + (alpha - 1) * old_value) / alpha;
}

// MinHook wrapper functions
bool CreateAndEnableHook(LPVOID pTarget, LPVOID pDetour, LPVOID* ppOriginal, const char* hookName);
MH_STATUS SafeInitializeMinHook(display_commanderhooks::HookType hookType);

// D3D9 present mode and flags string conversion functions
const char* D3DSwapEffectToString(uint32_t swapEffect);
std::string D3DPresentFlagsToString(uint32_t presentFlags);

// Window style modification helper function
// Modifies window styles to prevent fullscreen/always-on-top behavior
template <typename T>
inline void ModifyWindowStyle(int nIndex, T& dwNewLong, bool prevent_always_on_top) {
    if (nIndex == GWL_STYLE) {
        // WS_POPUP added to fix godstrike
        dwNewLong &= ~(WS_CAPTION | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU | WS_POPUP);
    }
    if (nIndex == GWL_EXSTYLE) {
        dwNewLong &= ~(WS_EX_DLGMODALFRAME | WS_EX_WINDOWEDGE | WS_EX_CLIENTEDGE | WS_EX_STATICEDGE);

        if (prevent_always_on_top) {
            dwNewLong &= ~(WS_EX_TOPMOST | WS_EX_TOOLWINDOW);
        }
    }
}

// Game detection utilities
std::string GetCurrentProcessName();
bool IsGameInNvapiAutoEnableList(const std::string& processName);
std::string GetNvapiAutoEnableGameStatus();

// Architecture detection utility
// Returns true if code was built for 64-bit, false for 32-bit
inline bool Is64BitBuild() {
#ifdef _WIN64
    return true;
#else
    return false;
#endif
}

// Calling DLL detection utility (similar to Special-K's SK_GetCallingDLL)
// Returns the HMODULE of the DLL that contains the given address
// Default parameter uses _ReturnAddress() to get the caller's return address
#pragma intrinsic(_ReturnAddress)
HMODULE GetCallingDLL(LPCVOID pReturn = _ReturnAddress());

// External declarations needed by utility functions
extern std::atomic<std::shared_ptr<const std::vector<MonitorInfo>>> g_monitors;
