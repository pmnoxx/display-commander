#include "general_utils.hpp"
#include "../latency/reflex_provider.hpp"
#include "../hooks/hook_suppression_manager.hpp"
#include "globals.hpp"
#include "logging.hpp"
#include "settings/advanced_tab_settings.hpp"
#include "settings/main_tab_settings.hpp"
#include <d3d9.h>
#include <MinHook.h>
#include <ShlObj.h>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <reshade.hpp>
#include <sstream>
#include <string>
#include <vector>

bool IsReflexAvailable() {
    if (!is_64_bit()) return false;
    if (g_native_reflex_detected.load(std::memory_order_acquire)) return true;
    ReflexProvider* p = g_reflexProvider.get();
    return p != nullptr && p->IsInitialized();
}

// Version.dll dynamic loading
namespace {
HMODULE s_version_dll = nullptr;

// Function pointers for version.dll functions
typedef DWORD(WINAPI* PFN_GetFileVersionInfoSizeW)(LPCWSTR lptstrFilename, LPDWORD lpdwHandle);
typedef BOOL(WINAPI* PFN_GetFileVersionInfoW)(LPCWSTR lptstrFilename, DWORD dwHandle, DWORD dwLen, LPVOID lpData);
typedef BOOL(WINAPI* PFN_VerQueryValueW)(LPCVOID pBlock, LPCWSTR lpSubBlock, LPVOID* lplpBuffer, PUINT puLen);

PFN_GetFileVersionInfoSizeW s_GetFileVersionInfoSizeW = nullptr;
PFN_GetFileVersionInfoW s_GetFileVersionInfoW = nullptr;
PFN_VerQueryValueW s_VerQueryValueW = nullptr;

// Load version.dll and get function pointers
// Always load from System32 to avoid circular dependency when Display Commander is loaded as version.dll
bool LoadVersionDLL() {
    if (s_version_dll != nullptr) {
        return true;  // Already loaded
    }

    // Load from System32 to avoid loading ourselves if we're loaded as version.dll
    WCHAR system_path[MAX_PATH];
    GetSystemDirectoryW(system_path, MAX_PATH);
    std::wstring version_path = std::wstring(system_path) + L"\\version.dll";

    s_version_dll = LoadLibraryW(version_path.c_str());
    if (s_version_dll == nullptr) {
        return false;
    }

    // Get function pointers
    s_GetFileVersionInfoSizeW =
        reinterpret_cast<PFN_GetFileVersionInfoSizeW>(GetProcAddress(s_version_dll, "GetFileVersionInfoSizeW"));
    s_GetFileVersionInfoW =
        reinterpret_cast<PFN_GetFileVersionInfoW>(GetProcAddress(s_version_dll, "GetFileVersionInfoW"));
    s_VerQueryValueW = reinterpret_cast<PFN_VerQueryValueW>(GetProcAddress(s_version_dll, "VerQueryValueW"));

    // Check if all functions were loaded successfully
    if (s_GetFileVersionInfoSizeW == nullptr || s_GetFileVersionInfoW == nullptr || s_VerQueryValueW == nullptr) {
        FreeLibrary(s_version_dll);
        s_version_dll = nullptr;
        s_GetFileVersionInfoSizeW = nullptr;
        s_GetFileVersionInfoW = nullptr;
        s_VerQueryValueW = nullptr;
        return false;
    }

    return true;
}
}  // anonymous namespace

// Constant definitions (used only by GetAspectByIndex in this file)
static const AspectRatio ASPECT_OPTIONS[] = {
    {3, 2},     // 1.5:1
    {4, 3},     // 1.333:1
    {16, 10},   // 1.6:1
    {16, 9},    // 1.778:1
    {19, 9},    // 2.111:1
    {195, 90},  // 2.167:1 (19.5:9)
    {21, 9},    // 2.333:1 (21:9)
    {43, 18},   // 2.389:1 (21.5:9)
    {32, 9},    // 3.556:1 (32:9)
};

// Helper function implementations
RECT RectFromWH(int width, int height) {
    RECT rect = {0, 0, width, height};
    return rect;
}

// Utility function implementations

AspectRatio GetAspectByIndex(AspectRatioType aspect_type) {
    int index = static_cast<int>(aspect_type);
    if (index >= 0 && index < 9) {
        return ASPECT_OPTIONS[index];
    }
    return {16, 9};  // Default to 16:9
}

// Helper function to get the actual width value based on the dropdown selection
int GetAspectWidthValue(int display_width) {
    const int width_index = settings::g_mainTabSettings.window_aspect_width.GetValue();

    // Width options: 0=Display Width, 1=3840, 2=2560, 3=1920, 4=1600, 5=1280, 6=1080, 7=900, 8=720
    int selected_width;
    switch (width_index) {
        case 0:  selected_width = display_width; break;  // Display Width
        case 1:  selected_width = 3840; break;
        case 2:  selected_width = 2560; break;
        case 3:  selected_width = 1920; break;
        case 4:  selected_width = 1600; break;
        case 5:  selected_width = 1280; break;
        case 6:  selected_width = 1080; break;
        case 7:  selected_width = 900; break;
        case 8:  selected_width = 720; break;
        default: selected_width = display_width; break;  // Fallback to display width
    }

    // Ensure the selected width doesn't exceed the display width
    return min(selected_width, display_width);
}

void ComputeDesiredSize(int display_width, int display_height, int& out_w, int& out_h) {
    const WindowMode mode = GetCurrentWindowMode();
    if (mode == WindowMode::kNoChanges || mode == WindowMode::kPreventFullscreenNoResize) {
        // No resize: return current display dimensions (kPreventFullscreenNoResize and kNoChanges)
        out_w = display_width;
        out_h = display_height;
        return;
    }

    if (mode == WindowMode::kFullscreen) {
        // kFullscreen: Borderless Fullscreen - use current monitor dimensions
        out_w = display_width;
        out_h = display_height;
        return;
    }

    // kAspectRatio: Borderless Windowed (Aspect Ratio) - aspect mode
    // Get the selected width from the dropdown
    const int want_w = GetAspectWidthValue(display_width);
    AspectRatio ar = GetAspectByIndex(s_aspect_index.load());
    // height = round(width * h / w)
    // prevent division by zero
    if (ar.w <= 0 || ar.h <= 0) {
        ar.h = 16;
        ar.w = 9;
    }
    out_w = want_w;
    out_h = want_w * ar.h / ar.w;

    // LogInfo("ComputeDesiredSize: out_w=%d, out_h=%d (width_index=%d)", out_w, out_h, s_aspect_width.load());
}

// XInput processing functions
// Map one signed axis: input [min_input, max_input] -> output [min_output, max_output]; below min_input -> 0
float MapStickAxisValue(float value, float min_input, float max_input, float min_output, float max_output) {
    float abs_val = std::abs(value);
    float sign_val = (value >= 0.0f) ? 1.0f : -1.0f;
    if (abs_val <= min_input) return 0.0f;
    if (max_input <= min_input) return 0.0f;  // avoid div by zero
    if (abs_val >= max_input) return sign_val * max_output;
    float t = (abs_val - min_input) / (max_input - min_input);
    return sign_val * (min_output + t * (max_output - min_output));
}

// Process stick input with radial mapping (one mapping applied to magnitude)
void ProcessStickInputRadial(float& x, float& y, float min_input, float max_input, float min_output, float max_output) {
    float magnitude = std::sqrt(x * x + y * y);
    if (magnitude < 0.0001f) {
        x = 0.0f;
        y = 0.0f;
        return;
    }
    // Map magnitude [min_input, max_input] -> [min_output, max_output]
    float out_mag;
    if (magnitude <= min_input) {
        out_mag = 0.0f;
    } else if (max_input <= min_input) {
        out_mag = 0.0f;
    } else if (magnitude >= max_input) {
        out_mag = max_output;
    } else {
        float t = (magnitude - min_input) / (max_input - min_input);
        out_mag = min_output + t * (max_output - min_output);
    }
    out_mag = std::clamp(out_mag, 0.0f, 1.0f);
    float scale = out_mag / magnitude;
    x = x * scale;
    y = y * scale;
}

// Process stick input with square mapping (separate min/max input and min/max output per axis)
void ProcessStickInputSquare(float& x, float& y, float min_in_x, float max_in_x, float min_out_x, float max_out_x,
                             float min_in_y, float max_in_y, float min_out_y, float max_out_y) {
    x = MapStickAxisValue(x, min_in_x, max_in_x, min_out_x, max_out_x);
    y = MapStickAxisValue(y, min_in_y, max_in_y, min_out_y, max_out_y);
}

// XInput thumbstick scaling helpers (handles asymmetric SHORT range: -32768 to 32767)
float ShortToFloat(SHORT value) {
    // Proper linear mapping from [-32768, 32767] to [-1.0f, 1.0f]
    // Using the full range: 32767 - (-32768) = 65535
    // Center point: (32767 + (-32768)) / 2 = -0.5
    // So we map: (value - (-32768)) / 65535 * 2.0f - 1.0f
    return (static_cast<float>(value) - (-32768.0f)) / 65535.0f * 2.0f - 1.0f;
}

SHORT FloatToShort(float value) {
    // Clamp to valid range
    value = max(-1.0f, min(1.0f, value));

    // Inverse mapping from [-1.0f, 1.0f] to [-32768, 32767]
    // (value + 1.0f) / 2.0f * 65535.0f + (-32768.0f)
    return static_cast<SHORT>((value + 1.0f) / 2.0f * 65535.0f + (-32768.0f));
}

// Get DLL version string (e.g., "570.6.2")
std::string GetDLLVersionString(const std::wstring& dllPath) {
    // Load version.dll dynamically if not already loaded
    if (!LoadVersionDLL()) {
        LogWarn("GetDLLVersionString: Failed to load version.dll");
        return "Unknown";
    }

    DWORD versionInfoSize = s_GetFileVersionInfoSizeW(dllPath.c_str(), nullptr);
    if (versionInfoSize == 0) {
        return "Unknown";
    }

    std::vector<BYTE> versionInfo(versionInfoSize);
    if (!s_GetFileVersionInfoW(dllPath.c_str(), 0, versionInfoSize, versionInfo.data())) {
        return "Unknown";
    }

    VS_FIXEDFILEINFO* fileInfo = nullptr;
    UINT fileInfoSize = 0;

    if (!s_VerQueryValueW(versionInfo.data(), L"\\", reinterpret_cast<LPVOID*>(&fileInfo), &fileInfoSize)) {
        return "Unknown";
    }

    if (fileInfo == nullptr || fileInfoSize == 0) {
        return "Unknown";
    }

    // Extract version numbers
    DWORD major = HIWORD(fileInfo->dwFileVersionMS);
    DWORD minor = LOWORD(fileInfo->dwFileVersionMS);
    DWORD build = HIWORD(fileInfo->dwFileVersionLS);
    DWORD revision = LOWORD(fileInfo->dwFileVersionLS);

    // Format as "major.minor.build.revision" (similar to Special-K)
    char versionStr[64];
    snprintf(versionStr, sizeof(versionStr), "%lu.%lu.%lu.%lu", major, minor, build, revision);

    return std::string(versionStr);
}

std::string GetDLLProductNameUtf8(const std::wstring& dllPath) {
    if (!LoadVersionDLL()) return {};
    const DWORD size = s_GetFileVersionInfoSizeW(dllPath.c_str(), nullptr);
    if (size == 0) return {};
    std::vector<BYTE> buf(size);
    if (!s_GetFileVersionInfoW(dllPath.c_str(), 0, size, buf.data())) return {};
    struct LANGANDCODEPAGE {
        WORD wLanguage;
        WORD wCodePage;
    };
    LANGANDCODEPAGE* p_trans = nullptr;
    UINT trans_len = 0;
    if (!s_VerQueryValueW(buf.data(), L"\\VarFileInfo\\Translation", reinterpret_cast<void**>(&p_trans), &trans_len)
        || !p_trans || trans_len < sizeof(LANGANDCODEPAGE))
        return {};
    wchar_t sub_block[64];
    swprintf_s(sub_block, L"\\StringFileInfo\\%04x%04x\\ProductName", p_trans[0].wLanguage, p_trans[0].wCodePage);
    void* p_block = nullptr;
    UINT len = 0;
    if (!s_VerQueryValueW(buf.data(), sub_block, &p_block, &len) || !p_block || len < sizeof(wchar_t)) return {};
    const wchar_t* product = static_cast<const wchar_t*>(p_block);
    size_t max_chars = len / sizeof(wchar_t);
    size_t str_len = 0;
    while (str_len < max_chars && product[str_len] != L'\0') ++str_len;
    if (str_len == 0) return {};
    const int utf8_size =
        WideCharToMultiByte(CP_UTF8, 0, product, static_cast<int>(str_len), nullptr, 0, nullptr, nullptr);
    if (utf8_size <= 0) return {};
    std::string result(static_cast<size_t>(utf8_size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, product, static_cast<int>(str_len), result.data(), utf8_size, nullptr, nullptr);
    return result;
}

// Convert device API enum to readable string
const char* GetDeviceApiString(reshade::api::device_api api) {
    switch (api) {
        case reshade::api::device_api::d3d9:   return "Direct3D 9";
        case reshade::api::device_api::d3d10:  return "Direct3D 10";
        case reshade::api::device_api::d3d11:  return "Direct3D 11";
        case reshade::api::device_api::d3d12:  return "Direct3D 12";
        case reshade::api::device_api::opengl: return "OpenGL";
        case reshade::api::device_api::vulkan: return "Vulkan";
        default:                               return "Unknown";
    }
}

// Convert device API version to readable string with feature level
std::string GetDeviceApiVersionString(reshade::api::device_api api, uint32_t api_version) {
    if (api_version == 0) {
        return GetDeviceApiString(api);
    }

    char buffer[128];

    switch (api) {
        case reshade::api::device_api::d3d9:
            // Check if D3D9 was upgraded to D3D9Ex
            if (s_d3d9e_upgrade_successful.load()) {
                snprintf(buffer, sizeof(buffer), "Direct3D 9Ex");
            } else {
                snprintf(buffer, sizeof(buffer), "Direct3D 9");
            }
            break;
        case reshade::api::device_api::d3d10:
        case reshade::api::device_api::d3d11:
        case reshade::api::device_api::d3d12: {
            // D3D feature levels are encoded as hex values
            // D3D_FEATURE_LEVEL_10_0 = 0xa000 (10.0)
            // D3D_FEATURE_LEVEL_10_1 = 0xa100 (10.1)
            // D3D_FEATURE_LEVEL_11_0 = 0xb000 (11.0)
            // D3D_FEATURE_LEVEL_11_1 = 0xb100 (11.1)
            // D3D_FEATURE_LEVEL_12_0 = 0xc000 (12.0)
            // D3D_FEATURE_LEVEL_12_1 = 0xc100 (12.1)
            // D3D_FEATURE_LEVEL_12_2 = 0xc200 (12.2)
            int major = (api_version >> 12) & 0xF;
            int minor = (api_version >> 8) & 0xF;

            if (api == reshade::api::device_api::d3d10) {
                snprintf(buffer, sizeof(buffer), "Direct3D 10.%d", minor);
            } else if (api == reshade::api::device_api::d3d11) {
                snprintf(buffer, sizeof(buffer), "Direct3D 11.%d", minor);
            } else {
                snprintf(buffer, sizeof(buffer), "Direct3D 12.%d", minor);
            }
            break;
        }
        case reshade::api::device_api::opengl: {
            // OpenGL version is encoded as major << 12 | minor << 8
            int major = (api_version >> 12) & 0xF;
            int minor = (api_version >> 8) & 0xF;
            snprintf(buffer, sizeof(buffer), "OpenGL %d.%d", major, minor);
            break;
        }
        case reshade::api::device_api::vulkan: {
            // Vulkan version is encoded as major << 12 | minor << 8
            int major = (api_version >> 12) & 0xF;
            int minor = (api_version >> 8) & 0xF;
            snprintf(buffer, sizeof(buffer), "Vulkan %d.%d", major, minor);
            break;
        }
        default: snprintf(buffer, sizeof(buffer), "Unknown"); break;
    }

    return std::string(buffer);
}

// MinHook wrapper function that combines CreateHook and EnableHook with proper error handling
bool CreateAndEnableHook(LPVOID ptarget, LPVOID pdetour, LPVOID* ppOriginal, const char* hookName) {
    if (ptarget == nullptr || pdetour == nullptr) {
        LogError("CreateAndEnableHook: Invalid parameters for hook '%s' ptarget: %p, pdetour: %p",
                 hookName != nullptr ? hookName : "Unknown", ptarget, pdetour);
        return false;
    }

    // Create the hook
    MH_STATUS create_result = MH_CreateHook(ptarget, pdetour, ppOriginal);
    if (create_result != MH_OK) {
        LogError("CreateAndEnableHook: Failed to create hook '%s' (status: %s)",
                 hookName != nullptr ? hookName : "Unknown", MH_StatusToString(create_result));
        return false;
    }

    // Enable the hook
    MH_STATUS enable_result = MH_EnableHook(ptarget);
    if (enable_result != MH_OK) {
        LogError("CreateAndEnableHook: Failed to enable hook '%s' (status: %s), removing hook",
                 hookName != nullptr ? hookName : "Unknown", MH_StatusToString(enable_result));

        // Clean up the hook if enabling failed
        MH_STATUS remove_result = MH_RemoveHook(ptarget);
        if (remove_result != MH_OK) {
            LogError("CreateAndEnableHook: Failed to remove hook '%s' after enable failure (status: %s)",
                     hookName != nullptr ? hookName : "Unknown", MH_StatusToString(remove_result));
        }
        return false;
    }

    LogInfo("CreateAndEnableHook: Successfully created and enabled hook '%s'",
            hookName != nullptr ? hookName : "Unknown");
    return true;
}

// Create and enable hook by resolving proc from the given module
bool CreateAndEnableHookFromModule(HMODULE hModule, const char* procName, LPVOID pDetour, LPVOID* ppOriginal,
                                   const char* hookName) {
    if (hModule == nullptr || procName == nullptr) {
        LogError("CreateAndEnableHookFromModule: Invalid module or procName for hook '%s'",
                 hookName != nullptr ? hookName : "Unknown");
        return false;
    }
    FARPROC pTarget = GetProcAddress(hModule, procName);
    if (pTarget == nullptr) {
        LogError("CreateAndEnableHookFromModule: GetProcAddress(%s) failed for hook '%s'", procName,
                 hookName != nullptr ? hookName : "Unknown");
        return false;
    }
    return CreateAndEnableHook(reinterpret_cast<LPVOID>(pTarget), pDetour, ppOriginal,
                               hookName != nullptr ? hookName : procName);
}

// MinHook initialization wrapper that checks suppress_minhook setting
MH_STATUS SafeInitializeMinHook(display_commanderhooks::HookType hookType) {
    // Check if MinHook initialization is suppressed
    if (settings::g_advancedTabSettings.suppress_minhook.GetValue()) {
        LogInfo("MinHook initialization suppressed by suppress_minhook setting for %s hooks",
                display_commanderhooks::HookSuppressionManager::GetInstance().GetHookTypeName(hookType).c_str());
        return MH_ERROR_ALREADY_INITIALIZED;  // Return this to indicate "already initialized" to avoid errors
    }
    static bool minhook_initialized = false;
    if (minhook_initialized) {
        LogInfo("MinHook already initialized, proceeding with %s hooks",
                display_commanderhooks::HookSuppressionManager::GetInstance().GetHookTypeName(hookType).c_str());
        return MH_OK;
    }
    minhook_initialized = true;

    // Initialize MinHook (only if not already initialized)
    MH_STATUS init_status = MH_Initialize();
    if (init_status != MH_OK && init_status != MH_ERROR_ALREADY_INITIALIZED) {
        LogError("Failed to initialize MinHook for %s hooks - Status: %d",
                 display_commanderhooks::HookSuppressionManager::GetInstance().GetHookTypeName(hookType).c_str(),
                 init_status);
        return init_status;
    }

    LogInfo("MinHook initialized successfully for %s hooks",
            display_commanderhooks::HookSuppressionManager::GetInstance().GetHookTypeName(hookType).c_str());
    return init_status;
}

// Display Commander folder in Local App Data: %LocalAppData%\Programs\Display_Commander (shared across games).
// Creates the directory if it does not exist; returns empty path if creation fails.
std::filesystem::path GetDisplayCommanderAppDataFolder() {
    wchar_t localappdata_path[MAX_PATH];
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, localappdata_path))) {
        return std::filesystem::path();
    }
    std::filesystem::path base(localappdata_path);
    std::filesystem::path dc_folder = base / L"Programs" / L"Display_Commander";
    std::error_code ec;
    if (!std::filesystem::exists(dc_folder, ec)) {
        if (!std::filesystem::create_directories(dc_folder, ec)) {
            return std::filesystem::path();
        }
    }
    return dc_folder;
}

std::filesystem::path GetDisplayCommanderAppDataRootPathNoCreate() {
    wchar_t localappdata_path[MAX_PATH];
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, localappdata_path))) {
        return std::filesystem::path();
    }
    return std::filesystem::path(localappdata_path) / L"Programs" / L"Display_Commander";
}

// Display Commander ReShade root: contains Shaders and Textures subfolders used for
// EffectSearchPaths/TextureSearchPaths. Creates the directory if it does not exist; returns empty path if creation
// fails.
std::filesystem::path GetDisplayCommanderReshadeRootFolder() {
    std::filesystem::path base = GetDisplayCommanderAppDataFolder();
    if (base.empty()) {
        return base;
    }
    std::filesystem::path reshade_folder = base / L"Reshade";
    std::error_code ec;
    if (!std::filesystem::exists(reshade_folder, ec)) {
        if (!std::filesystem::create_directories(reshade_folder, ec)) {
            return std::filesystem::path();
        }
    }
    return reshade_folder;
}

std::filesystem::path GetDisplayCommanderAddonsFolder() {
    std::filesystem::path reshade_root = GetDisplayCommanderReshadeRootFolder();
    if (reshade_root.empty()) {
        return std::filesystem::path();
    }
    return reshade_root / L"Addons";
}

std::filesystem::path GetDisplayCommanderReshadeConfigsFolder() {
    std::filesystem::path reshade_root = GetDisplayCommanderReshadeRootFolder();
    if (reshade_root.empty()) {
        return std::filesystem::path();
    }
    return reshade_root / L"Configs";
}

std::string GetGameNameFromProcess() {
    WCHAR buf[MAX_PATH];
    if (::GetModuleFileNameW(nullptr, buf, MAX_PATH) == 0) {
        return std::string();
    }
    std::filesystem::path exe_path(buf);
    std::filesystem::path parent = exe_path.parent_path();
    std::string name = parent.filename().string();
    if (name.empty()) {
        return "Game";
    }
    return name;
}

std::filesystem::path GetGameFolderFromProcess() {
    WCHAR buf[MAX_PATH];
    if (::GetModuleFileNameW(nullptr, buf, MAX_PATH) == 0) {
        return std::filesystem::path();
    }
    return std::filesystem::path(buf).parent_path();
}

std::filesystem::path GetGameNotesFilePath() {
    std::filesystem::path base = GetDisplayCommanderAppDataFolder();
    if (base.empty()) {
        return base;
    }
    std::string game_name = GetGameNameFromProcess();
    if (game_name.empty()) {
        return std::filesystem::path();
    }
    return base / L"Games" / std::filesystem::path(game_name) / L"notes.txt";
}

// DefaultFiles folder: %LocalAppData%\Programs\Display_Commander\DefaultFiles. Does not create the directory.
std::filesystem::path GetDefaultFilesFolder() {
    std::filesystem::path base = GetDisplayCommanderAppDataFolder();
    if (base.empty()) {
        return base;
    }
    return base / L"DefaultFiles";
}

// Copy each file from DefaultFiles into game_dir only if missing. Flat files only; no overwrite.
void CopyDefaultFilesToGameFolder(const std::filesystem::path& game_dir) {
    std::filesystem::path default_files = GetDefaultFilesFolder();
    std::error_code ec;
    if (!std::filesystem::exists(default_files, ec) || !std::filesystem::is_directory(default_files, ec)) {
        return;
    }
    if (game_dir.empty() || !std::filesystem::is_directory(game_dir, ec)) {
        return;
    }
    for (const auto& entry :
         std::filesystem::directory_iterator(default_files, std::filesystem::directory_options::skip_permission_denied,
                                             ec)) {
        if (ec) {
            break;
        }
        if (!entry.is_regular_file(ec)) {
            continue;
        }
        std::filesystem::path dest = game_dir / entry.path().filename();
        if (std::filesystem::exists(dest, ec)) {
            continue;  // do not overwrite
        }
        if (std::filesystem::copy_file(entry.path(), dest, std::filesystem::copy_options::none, ec)) {
            LogInfo("DefaultFiles: copied %s to game folder.", entry.path().filename().string().c_str());
        } else {
            LogError("DefaultFiles: failed to copy %s to game folder: %s", entry.path().filename().string().c_str(),
                     ec.message().c_str());
        }
    }
}

void CopyGameIniFilesToReshadeConfigBackupFolder() {
    std::filesystem::path game_dir = GetGameFolderFromProcess();
    std::filesystem::path configs = GetDisplayCommanderReshadeConfigsFolder();
    std::string game_name = GetGameNameFromProcess();
    std::error_code ec;
    if (game_dir.empty() || !std::filesystem::is_directory(game_dir, ec)) {
        return;
    }
    if (configs.empty() || game_name.empty()) {
        return;
    }
    std::filesystem::path dest_dir = configs / std::filesystem::path(game_name);
    if (!std::filesystem::exists(dest_dir, ec)) {
        if (!std::filesystem::create_directories(dest_dir, ec)) {
            LogError("ReShade config backup: failed to create folder %s: %s", dest_dir.string().c_str(),
                     ec.message().c_str());
            return;
        }
    }
    for (const auto& entry :
         std::filesystem::directory_iterator(game_dir, std::filesystem::directory_options::skip_permission_denied, ec)) {
        if (ec) {
            break;
        }
        if (!entry.is_regular_file(ec)) {
            continue;
        }
        const std::filesystem::path& p = entry.path();
        std::string ext = p.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (ext != ".ini") {
            continue;
        }
        std::filesystem::path dest = dest_dir / p.filename();
        if (std::filesystem::exists(dest, ec)) {
            continue;  // do not overwrite
        }
        if (std::filesystem::copy_file(entry.path(), dest, std::filesystem::copy_options::none, ec)) {
            LogInfo("ReShade config backup: copied %s from game folder to %s", p.filename().string().c_str(),
                    dest_dir.string().c_str());
        } else {
            LogError("ReShade config backup: failed to copy %s: %s", p.filename().string().c_str(),
                     ec.message().c_str());
        }
    }
}

// Default DLSS override folder: AppData\Local\Programs\Display_Commander\dlss_override (centralized, shared across
// games). Creates the directory if it does not exist; returns empty path if creation fails.
std::filesystem::path GetDefaultDlssOverrideFolder() {
    std::filesystem::path base = GetDisplayCommanderAppDataFolder();
    if (base.empty()) {
        return base;
    }
    std::filesystem::path dlss_folder = base / L"dlss_override";
    std::error_code ec;
    if (!std::filesystem::exists(dlss_folder, ec)) {
        if (!std::filesystem::create_directories(dlss_folder, ec)) {
            return std::filesystem::path();
        }
    }
    return dlss_folder;
}

// Effective default path: base or base/subfolder (subfolder empty = base only).
// Creates the directory if it does not exist; returns empty path if creation fails.
std::filesystem::path GetEffectiveDefaultDlssOverrideFolder(const std::string& subfolder) {
    std::filesystem::path base = GetDefaultDlssOverrideFolder();
    if (base.empty()) {
        return base;
    }
    if (subfolder.empty()) {
        return base;
    }
    std::filesystem::path full = base / subfolder;
    std::error_code ec;
    if (!std::filesystem::exists(full, ec)) {
        if (!std::filesystem::create_directories(full, ec)) {
            return std::filesystem::path();
        }
    }
    return full;
}

// Subfolder names under the default DLSS override folder (for UI dropdown)
std::vector<std::string> GetDlssOverrideSubfolderNames() {
    std::vector<std::string> names;
    std::filesystem::path base = GetDefaultDlssOverrideFolder();
    std::error_code ec;
    if (!std::filesystem::exists(base, ec) || !std::filesystem::is_directory(base, ec)) {
        return names;
    }
    for (const auto& entry :
         std::filesystem::directory_iterator(base, std::filesystem::directory_options::skip_permission_denied, ec)) {
        if (entry.is_directory(ec)) {
            names.push_back(entry.path().filename().string());
        }
    }
    std::sort(names.begin(), names.end());
    return names;
}

// Create a subfolder under (centralized) Display_Commander/dlss_override. Rejects names with path separators or "." /
// "..".
bool CreateDlssOverrideSubfolder(const std::string& subfolder_name, std::string* out_error) {
    if (subfolder_name.empty()) {
        if (out_error) *out_error = "Folder name cannot be empty.";
        return false;
    }
    std::string sanitized;
    for (char c : subfolder_name) {
        if (c == '/' || c == '\\' || c == ':') {
            if (out_error) *out_error = "Folder name cannot contain path separators.";
            return false;
        }
        if (std::isspace(static_cast<unsigned char>(c))) {
            continue;
        }
        sanitized += c;
    }
    if (sanitized.empty()) {
        if (out_error) *out_error = "Folder name is invalid after sanitizing.";
        return false;
    }
    if (sanitized == "." || sanitized == ".." || sanitized.find("..") != std::string::npos) {
        if (out_error) *out_error = "Folder name cannot be \".\" or \"..\" or contain \"..\".";
        return false;
    }
    std::filesystem::path base = GetDefaultDlssOverrideFolder();
    if (base.empty()) {
        if (out_error) *out_error = "Could not determine DLSS override folder (LocalAppData unavailable).";
        return false;
    }
    std::filesystem::path full = base / sanitized;
    std::error_code ec;
    if (std::filesystem::exists(full, ec)) {
        if (std::filesystem::is_directory(full, ec)) {
            if (out_error) *out_error = "";  // already exists, treat as success
            return true;
        }
        if (out_error) *out_error = "A file with that name already exists.";
        return false;
    }
    if (!std::filesystem::create_directories(full, ec)) {
        if (out_error) *out_error = ec ? ec.message() : "Failed to create directory.";
        return false;
    }
    if (out_error) *out_error = "";
    return true;
}

DlssOverrideDllStatus GetDlssOverrideFolderDllStatus(const std::string& folder_path, bool override_dlss,
                                                     bool override_dlss_fg, bool override_dlss_rr) {
    DlssOverrideDllStatus status;
    status.all_required_present = true;
    struct Entry {
        bool enabled;
        const char* name;
    };
    const Entry entries[] = {
        {override_dlss, "nvngx_dlss.dll"},
        {override_dlss_fg, "nvngx_dlssd.dll"},
        {override_dlss_rr, "nvngx_dlssg.dll"},
    };
    std::error_code ec;
    const bool folder_exists = !folder_path.empty() && std::filesystem::exists(folder_path, ec)
                               && std::filesystem::is_directory(folder_path, ec);
    status.dlls.resize(3);
    for (size_t i = 0; i < 3; ++i) {
        DlssOverrideDllEntry& entry = status.dlls[i];
        entry.name = entries[i].name;
        entry.present = false;
        entry.version.clear();
        if (!folder_exists) {
            if (entries[i].enabled) status.missing_dlls.push_back(entries[i].name);
            continue;
        }
        std::filesystem::path dll_path = std::filesystem::path(folder_path) / entries[i].name;
        const bool exists = std::filesystem::exists(dll_path, ec) && std::filesystem::is_regular_file(dll_path, ec);
        if (exists) {
            entry.present = true;
            entry.version = GetDLLVersionString(dll_path.wstring());
            if (entry.version.empty()) entry.version = "?";
        } else {
            if (entries[i].enabled) status.missing_dlls.push_back(entries[i].name);
        }
    }
    status.all_required_present = status.missing_dlls.empty();
    return status;
}

// Helper function to check if a version is between two version ranges (inclusive)
bool isBetween(int major, int minor, int patch, int minMajor, int minMinor, int minPatch, int maxMajor, int maxMinor,
               int maxPatch) {
    // Convert version to comparable integer (major * 10000 + minor * 100 + patch)
    int version = (major * 10000) + (minor * 100) + patch;
    int minVersion = (minMajor * 10000) + (minMinor * 100) + minPatch;
    int maxVersion = (maxMajor * 10000) + (maxMinor * 100) + maxPatch;

    return version >= minVersion && version <= maxVersion;
}

// Get supported DLSS Super Resolution presets based on DLL version
std::string GetSupportedDLSSSRPresets(int major, int minor, int patch) {
    std::string supported_presets;

    // 3.8.10-3.8.10
    if (isBetween(major, minor, patch, 3, 8, 10, 3, 8, 10)) {
        return "E,F";
    }

    // 3.1.30-310.4.0
    if (isBetween(major, minor, patch, 3, 1, 30, 310, 3, 999)) {
        supported_presets += "A,B,C,D";
    }

    // 3.7.0-310.3.999
    if (isBetween(major, minor, patch, 3, 7, 0, 310, 3, 999)) {
        if (!supported_presets.empty()) supported_presets += ",";
        supported_presets += "E";
    }
    // 3.7.0-999.999.999
    if (isBetween(major, minor, patch, 3, 7, 0, 999, 999, 999)) {
        if (!supported_presets.empty()) supported_presets += ",";
        supported_presets += "F";
    }

    // 310.2.0-999.999.999
    if (isBetween(major, minor, patch, 310, 2, 0, 999, 999, 999)) {
        if (!supported_presets.empty()) supported_presets += ",";
        supported_presets += "J,K";
    }

    // DLSS 4.5
    if (isBetween(major, minor, patch, 310, 5, 0, 999, 999, 999)) {
        if (!supported_presets.empty()) supported_presets += ",";
        supported_presets += "L,M";
    }

    // Who knows where those will be added
    if (isBetween(major, minor, patch, 310, 5, 0, 999, 999, 999)) {
        if (!supported_presets.empty()) supported_presets += ",";
        supported_presets += "N,O,P,Q,R,S,T,U,V,W,X,Y,Z";
    }
    return supported_presets;
}

// Get supported DLSS Ray Reconstruction presets based on DLL version
// RR supports A, B, C, D, E presets depending on version (A, B, C added in newer versions)
std::string GetSupportedDLSSRRPresets(int major, int minor, int patch) {
    // Ray Reconstruction was introduced in DLSS 3.5.0+
    if (!isBetween(major, minor, patch, 3, 5, 0, 999, 999, 999)) {
        return "";  // For older versions, RR is not supported
    }

    // 3.5.0-310.3.999: RR supports A, B, C, D, E presets
    if (isBetween(major, minor, patch, 3, 5, 0, 310, 3, 999)) {
        return "A,B,C,D,E";
    }

    // 310.4.0-999.999.999: RR supports A, B, C, D, E presets (and potentially more)
    if (isBetween(major, minor, patch, 310, 4, 0, 999, 999, 999)) {
        return "A,B,C,D,E";
    }

    // Fallback for other versions that support RR but with limited presets
    return "D,E";
}

// Parse version string and return supported SR presets
std::string GetSupportedDLSSSRPresetsFromVersionString(const std::string& versionString) {
    // Handle "Not loaded" or "Unknown" cases
    if (versionString == "Not loaded" || versionString == "Unknown" || versionString == "N/A") {
        return "N/A";
    }

    // Parse version string (format: "major.minor.build.revision" or "major.minor.patch")
    int major = 0, minor = 0, patch = 0;

    // Try to parse the version string
    size_t first_dot = versionString.find('.');
    if (first_dot != std::string::npos) {
        major = std::stoi(versionString.substr(0, first_dot));

        size_t second_dot = versionString.find('.', first_dot + 1);
        if (second_dot != std::string::npos) {
            minor = std::stoi(versionString.substr(first_dot + 1, second_dot - first_dot - 1));

            // Look for third dot (build.revision format) or use as patch
            size_t third_dot = versionString.find('.', second_dot + 1);
            if (third_dot != std::string::npos) {
                // Format: major.minor.build.revision - use build as patch
                patch = std::stoi(versionString.substr(second_dot + 1, third_dot - second_dot - 1));
            } else {
                // Format: major.minor.patch
                patch = std::stoi(versionString.substr(second_dot + 1));
            }
        }
    }

    return GetSupportedDLSSSRPresets(major, minor, patch);
}

// Parse version string and return supported RR presets
std::string GetSupportedDLSSRRPresetsFromVersionString(const std::string& versionString) {
    // Handle "Not loaded" or "Unknown" cases
    if (versionString == "Not loaded" || versionString == "Unknown" || versionString == "N/A") {
        return "N/A";
    }

    // Parse version string (format: "major.minor.build.revision" or "major.minor.patch")
    int major = 0, minor = 0, patch = 0;

    // Try to parse the version string
    size_t first_dot = versionString.find('.');
    if (first_dot != std::string::npos) {
        major = std::stoi(versionString.substr(0, first_dot));

        size_t second_dot = versionString.find('.', first_dot + 1);
        if (second_dot != std::string::npos) {
            minor = std::stoi(versionString.substr(first_dot + 1, second_dot - first_dot - 1));

            // Look for third dot (build.revision format) or use as patch
            size_t third_dot = versionString.find('.', second_dot + 1);
            if (third_dot != std::string::npos) {
                // Format: major.minor.build.revision - use build as patch
                patch = std::stoi(versionString.substr(second_dot + 1, third_dot - second_dot - 1));
            } else {
                // Format: major.minor.patch
                patch = std::stoi(versionString.substr(second_dot + 1));
            }
        }
    }

    return GetSupportedDLSSRRPresets(major, minor, patch);
}

// Generate DLSS preset options based on supported presets
std::vector<std::string> GetDLSSPresetOptions(const std::string& supportedPresets) {
    std::vector<std::string> options;

    // Always include Game Default and DLSS Default
    options.push_back("Game Default");
    options.push_back("DLSS Default");

    // Parse supported presets string (e.g., "A,B,C,D" or "E,F")
    if (supportedPresets != "N/A" && !supportedPresets.empty()) {
        std::stringstream ss(supportedPresets);
        std::string preset;

        while (std::getline(ss, preset, ',')) {
            // Trim whitespace
            preset.erase(0, preset.find_first_not_of(" \t"));
            preset.erase(preset.find_last_not_of(" \t") + 1);

            if (!preset.empty()) {
                std::string label = "Preset " + preset;
                // Mark presets N and beyond as (for future support) in the UI
                if (preset.size() == 1 && preset[0] >= 'N' && preset[0] <= 'Z') {
                    label += " (for future support)";
                }
                options.push_back(label);
            }
        }
    }

    return options;
}

// Convert DLSS preset string to integer value
int GetDLSSPresetValue(const std::string& presetString) {
    if (presetString == "Game Default") {
        return -1;  // No override - don't change anything
    } else if (presetString == "DLSS Default") {
        return 0;  // Use DLSS default (value 0)
    } else if (presetString.substr(0, 7) == "Preset ") {
        // Extract the preset letter (e.g., "Preset A" -> "A" or "Preset N (for future support)" -> "N")
        std::string presetLetter = presetString.substr(7);
        if (!presetLetter.empty()) {
            char letter = presetLetter[0];
            if (letter >= 'A' && letter <= 'Z') {
                return letter - 'A' + 1;  // A=1, B=2, C=3, etc.
            }
        }
    }

    // Default to no override if string doesn't match expected format
    return -1;
}

// Convert DLSS quality preset string to NVSDK_NGX_PerfQuality_Value. Returns (NVSDK_NGX_PerfQuality_Value)-1 for "Game
// Default" (no override).
NVSDK_NGX_PerfQuality_Value GetDLSSQualityPresetValue(const std::string& presetString) {
    if (presetString == "Game Default") {
        return static_cast<NVSDK_NGX_PerfQuality_Value>(-1);
    }
    if (presetString == "Performance") {
        return NVSDK_NGX_PerfQuality_Value_MaxPerf;
    }
    if (presetString == "Balanced") {
        return NVSDK_NGX_PerfQuality_Value_Balanced;
    }
    if (presetString == "Quality") {
        return NVSDK_NGX_PerfQuality_Value_MaxQuality;
    }
    if (presetString == "Ultra Performance") {
        return NVSDK_NGX_PerfQuality_Value_UltraPerformance;
    }
    if (presetString == "Ultra Quality") {
        return NVSDK_NGX_PerfQuality_Value_UltraQuality;
    }
    if (presetString == "DLAA") {
        return NVSDK_NGX_PerfQuality_Value_DLAA;
    }
    return static_cast<NVSDK_NGX_PerfQuality_Value>(-1);
}

// Convert render preset number to letter string for display
// 0 = "Default", 1 = "A", 2 = "B", ..., 26 = "Z". Presets >= N (14) get "(for future support)" suffix.
std::string ConvertRenderPresetToLetter(int preset_value) {
    if (preset_value == 0) {
        return "Default";
    } else if (preset_value >= 1 && preset_value <= 26) {
        // Convert 1-26 to A-Z
        char letter = 'A' + (preset_value - 1);
        std::string result(1, letter);
        if (preset_value >= 14) {  // N=14, O=15, ...
            result += " (for future support)";
        }
        return result;
    } else {
        // Invalid or unknown preset value
        return "?";
    }
}

// D3D9 present mode and flags string conversion functions
const char* D3DSwapEffectToString(uint32_t swapEffect) {
    switch (swapEffect) {
        case 1:  return "D3DSWAPEFFECT_DISCARD";
        case 2:  return "D3DSWAPEFFECT_FLIP";
        case 3:  return "D3DSWAPEFFECT_COPY";
        case 4:  return "D3DSWAPEFFECT_OVERLAY";
        case 5:  return "D3DSWAPEFFECT_FLIPEX";
        default: return "UNKNOWN_SWAP_EFFECT";
    }
}

std::string D3DPresentFlagsToString(uint32_t presentFlags) {
    if (presentFlags == 0) {
        return "NONE";
    }

    std::string result;

    // D3DPRESENT flags (using actual D3D9 constants)
    if (presentFlags & D3DPRESENT_DONOTWAIT) result += "D3DPRESENT_DONOTWAIT | ";
    if (presentFlags & D3DPRESENT_LINEAR_CONTENT) result += "D3DPRESENT_LINEAR_CONTENT | ";
    if (presentFlags & D3DPRESENT_DONOTFLIP) result += "D3DPRESENT_DONOTFLIP | ";
    if (presentFlags & D3DPRESENT_FLIPRESTART) result += "D3DPRESENT_FLIPRESTART | ";
    if (presentFlags & D3DPRESENT_VIDEO_RESTRICT_TO_MONITOR) result += "D3DPRESENT_VIDEO_RESTRICT_TO_MONITOR | ";
    if (presentFlags & D3DPRESENT_UPDATEOVERLAYONLY) result += "D3DPRESENT_UPDATEOVERLAYONLY | ";
    if (presentFlags & D3DPRESENT_HIDEOVERLAY) result += "D3DPRESENT_HIDEOVERLAY | ";
    if (presentFlags & D3DPRESENT_UPDATECOLORKEY) result += "D3DPRESENT_UPDATECOLORKEY | ";
    if (presentFlags & D3DPRESENT_FORCEIMMEDIATE) result += "D3DPRESENT_FORCEIMMEDIATE | ";
    // Remove trailing " | " if present
    if (!result.empty() && result.length() >= 3) {
        result.erase(result.length() - 3);
    }

    // If no known flags were found, show the raw value
    if (result.empty()) {
        char buffer[32];
        snprintf(buffer, sizeof(buffer), "0x%08X", presentFlags);
        result = buffer;
    }

    return result;
}

// Current process path (fully qualified path of the main executable)
std::wstring GetCurrentProcessPathW() {
    wchar_t pathBuf[MAX_PATH] = {};
    if (::GetModuleFileNameW(nullptr, pathBuf, MAX_PATH) == 0) {
        return {};
    }
    return pathBuf;
}

// GetCallingDLL implementation (similar to Special-K's SK_GetCallingDLL)
HMODULE GetCallingDLL(LPCVOID pReturn) {
    HMODULE hCallingMod = nullptr;

    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT | GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                       static_cast<const wchar_t*>(pReturn), &hCallingMod);

    return hCallingMod;
}
