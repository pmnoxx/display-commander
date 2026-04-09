// Source Code <Display Commander> // follow this order for includes in all files + add this comment at the top
#include "dll_process_attach.hpp"

#include "addon.hpp"
#include "config/display_commander_config.hpp"
#include "dll_boot_logging.hpp"
#include "exit_handler.hpp"
#include "globals.hpp"
#include "hooks/loadlibrary_hooks.hpp"
#include "hooks/vulkan/nvlowlatencyvk_hooks.hpp"
#include "hooks/vulkan/vulkan_loader_hooks.hpp"
#include "hooks/windows_hooks/api_hooks.hpp"
#include "hooks/windows_hooks/window_proc_hooks.hpp"
#include "init_without_hwnd.hpp"
#include "latency/gpu_completion_monitoring.hpp"
#include "latency/reflex_provider.hpp"
#include "latent_sync/refresh_rate_monitor_integration.hpp"
#include "reshade_addon_handlers.hpp"
#include "reshade_module_detection.hpp"
#include "settings/hook_suppression_settings.hpp"
#include "utils/dc_load_path.hpp"
#include "utils/display_commander_logger.hpp"
#include "utils/helper_exe_filter.hpp"
#include "utils/logging.hpp"
#include "utils/reshade_load_path.hpp"
#include "utils/timing.hpp"
#include "utils/version_check.hpp"
#include "version.hpp"

// Libraries <ReShade> / <imgui>
#include <reshade.hpp>

// Libraries <standard C++>
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <optional>
#include <set>
#include <string>
#include <vector>

// Libraries <Windows.h>
#include <Windows.h>

// Libraries <Windows>
#include <psapi.h>
#include <winver.h>

namespace {
enum class ProcessAttachEarlyResult { Continue, RefuseLoad, EarlySuccess };

constexpr const char* kDisplayCommanderMinLoadVersion = "0.12.194";

void ProcessAttach_DetectReShadeInModules() {
    HMODULE modules[1024];
    DWORD num_modules_bytes = 0;
    if (K32EnumProcessModules(GetCurrentProcess(), modules, sizeof(modules), &num_modules_bytes) == 0) return;
    DWORD num_modules =
        (std::min<DWORD>)(num_modules_bytes / sizeof(HMODULE), static_cast<DWORD>(sizeof(modules) / sizeof(HMODULE)));
    for (DWORD i = 0; i < num_modules; i++) {
        if (modules[i] == nullptr) continue;
        FARPROC register_func = GetProcAddress(modules[i], "ReShadeRegisterAddon");
        if (register_func != nullptr) {
            HMODULE expected = nullptr;
            if (g_reshade_module.compare_exchange_strong(expected, modules[i]))
                OutputDebugStringA("ReShadeRegisterAddon found");
            break;
        }
    }
}

std::wstring GetFileProductNameW(const std::wstring& path_w) {
    DWORD ver_handle = 0;
    const DWORD size = GetFileVersionInfoSizeW(path_w.c_str(), &ver_handle);
    if (size == 0) return {};
    std::vector<char> buf(size);
    if (!GetFileVersionInfoW(path_w.c_str(), 0, size, buf.data())) return {};
    struct LANGANDCODEPAGE {
        WORD wLanguage;
        WORD wCodePage;
    };
    LANGANDCODEPAGE* p_trans = nullptr;
    UINT trans_len = 0;
    if (!VerQueryValueW(buf.data(), L"\\VarFileInfo\\Translation", reinterpret_cast<void**>(&p_trans), &trans_len)
        || !p_trans || trans_len < sizeof(LANGANDCODEPAGE))
        return {};

    auto read_product = [&buf](const wchar_t* sub_block) -> std::wstring {
        void* p_block = nullptr;
        UINT len_ignored = 0;
        if (!VerQueryValueW(buf.data(), sub_block, &p_block, &len_ignored) || !p_block) return {};
        const wchar_t* product = static_cast<const wchar_t*>(p_block);
        constexpr size_t kMaxChars = 512;
        size_t str_len = 0;
        while (str_len < kMaxChars && product[str_len] != L'\0') ++str_len;
        if (str_len == 0) return {};
        return std::wstring(product, str_len);
    };

    wchar_t sub_block[64];
    swprintf_s(sub_block, L"\\StringFileInfo\\%04x%04x\\ProductName", p_trans[0].wLanguage, p_trans[0].wCodePage);
    std::wstring result = read_product(sub_block);

    if (result.size() < 17) {
        const wchar_t* en_us_block = L"\\StringFileInfo\\040904b0\\ProductName";
        std::wstring en_result = read_product(en_us_block);
        if (en_result.size() > result.size()) result = std::move(en_result);
    }

    return result;
}

void RenameUnusedDcProxyDlls(HMODULE h_module) {
    if (h_module == nullptr) return;

    WCHAR module_path_w[MAX_PATH] = {};
    if (GetModuleFileNameW(h_module, module_path_w, MAX_PATH) == 0) return;

    std::filesystem::path self_path(module_path_w);
    if (!self_path.has_filename()) return;

    const std::wstring self_product = GetFileProductNameW(self_path.wstring());
    char self_product_narrow[256] = {};
    if (!self_product.empty()) {
        WideCharToMultiByte(CP_ACP, 0, self_product.c_str(), -1, self_product_narrow,
                            static_cast<int>(sizeof(self_product_narrow)), nullptr, nullptr);
    }
    LogInfo("[main_entry] RenameUnusedDcProxyDlls: self_product: %s", self_product_narrow);
    if (self_product.empty() || _wcsicmp(self_product.c_str(), L"Display Commander") != 0) {
        return;
    }
    char self_path_narrow[MAX_PATH] = {};
    WideCharToMultiByte(CP_ACP, 0, self_path.wstring().c_str(), -1, self_path_narrow, MAX_PATH, nullptr, nullptr);
    LogInfo("[main_entry] RenameUnusedDcProxyDlls: self_path: %s", self_path_narrow);

    std::filesystem::path dir = self_path.parent_path();
    if (dir.empty()) return;

    std::vector<std::filesystem::path> unused_paths;
    std::error_code ec;
    for (const auto& entry :
         std::filesystem::directory_iterator(dir, std::filesystem::directory_options::skip_permission_denied, ec)) {
        if (ec) break;
        if (!entry.is_regular_file(ec)) continue;

        std::filesystem::path path = entry.path();
        if (!path.has_filename()) continue;
        if (std::filesystem::equivalent(path, self_path, ec)) {
            ec.clear();
            continue;
        }

        std::wstring ext = path.extension().wstring();
        for (auto& c : ext) {
            if (c >= L'A' && c <= L'Z') c += (L'a' - L'A');
        }
        if (ext != L".dll") continue;

        const std::wstring product = GetFileProductNameW(path.wstring());
        char path_narrow[MAX_PATH] = {};
        char product_narrow[256] = {};
        WideCharToMultiByte(CP_ACP, 0, path.wstring().c_str(), -1, path_narrow, MAX_PATH, nullptr, nullptr);
        if (!product.empty()) {
            WideCharToMultiByte(CP_ACP, 0, product.c_str(), -1, product_narrow,
                                static_cast<int>(sizeof(product_narrow)), nullptr, nullptr);
        }
        LogInfo("[main_entry] RenameUnusedDcProxyDlls: path: %s, product: %s", path_narrow, product_narrow);
        if (product.empty() || _wcsicmp(product.c_str(), L"Display Commander") != 0) continue;

        std::filesystem::path new_path = path;
        new_path += L".unused";
        if (std::filesystem::exists(new_path, ec)) {
            ec.clear();
            continue;
        }
        unused_paths.push_back(path);
    }

    const size_t n = unused_paths.size();
    if (n == 0) {
        LogInfo("[main_entry] RenameUnusedDcProxyDlls: no unused DC proxy DLLs detected");
        return;
    }
    LogInfo("[main_entry] RenameUnusedDcProxyDlls: detected %zu unused DC proxy DLL(s)", n);
    for (size_t i = 0; i < n; i++) {
        char path_narrow[MAX_PATH] = {};
        WideCharToMultiByte(CP_ACP, 0, unused_paths[i].wstring().c_str(), -1, path_narrow, MAX_PATH, nullptr, nullptr);
        LogInfo("[main_entry] RenameUnusedDcProxyDlls: unused[%zu]: %s", i, path_narrow);
    }

    for (const std::filesystem::path& path : unused_paths) {
        std::filesystem::path new_path = path;
        new_path += L".unused";
        std::error_code rename_ec;
        std::filesystem::rename(path, new_path, rename_ec);
        if (!rename_ec) {
            char old_narrow[MAX_PATH] = {};
            char new_narrow[MAX_PATH] = {};
            WideCharToMultiByte(CP_ACP, 0, path.wstring().c_str(), -1, old_narrow, MAX_PATH, nullptr, nullptr);
            WideCharToMultiByte(CP_ACP, 0, new_path.wstring().c_str(), -1, new_narrow, MAX_PATH, nullptr, nullptr);
            LogInfo("[main_entry] Renamed unused DC proxy DLL: %s -> %s", old_narrow, new_narrow);
        }
    }
}

void ProcessAttach_LoadLocalAddonDlls(HMODULE h_module) {
    WCHAR addon_path[MAX_PATH];
    if (GetModuleFileNameW(h_module, addon_path, MAX_PATH) <= 0) return;
    std::filesystem::path addon_dir = std::filesystem::path(addon_path).parent_path();
#ifdef _WIN64
    const std::wstring ext_list[] = {L".dc64", L".dc", L".asi"};
#else
    const std::wstring ext_list[] = {L".dc32", L".dc", L".asi"};
#endif
    const std::set<std::wstring> ext_match(ext_list, ext_list + 3);
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(
             addon_dir, std::filesystem::directory_options::skip_permission_denied, ec)) {
        if (ec) break;
        if (!entry.is_regular_file()) continue;
        std::wstring ext = entry.path().extension().wstring();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
        if (!ext_match.contains(ext)) continue;
        const std::wstring path_w = entry.path().wstring();
        const std::wstring product = GetFileProductNameW(path_w);
        if (!product.empty() && _wcsicmp(product.c_str(), L"ReShade") == 0 && g_reshade_module != nullptr) continue;
        if (!product.empty() && _wcsicmp(product.c_str(), L"Display Commander") == 0) continue;
        HMODULE mod = LoadLibraryW(path_w.c_str());
        if (mod != nullptr) {
            std::string name = entry.path().filename().string();
            char msg[384];
            snprintf(msg, sizeof(msg), "[DisplayCommander] Loaded .dc64/.dc32/.dc/.asi DLL: %s\n", name.c_str());
            OutputDebugStringA(msg);
        }
    }
}

void ProcessAttach_LoadLocalAddonDllsAfterReShade(HMODULE h_module) {
    WCHAR addon_path[MAX_PATH];
    if (GetModuleFileNameW(h_module, addon_path, MAX_PATH) <= 0) return;
    std::filesystem::path addon_dir = std::filesystem::path(addon_path).parent_path();
#ifdef _WIN64
    const std::wstring ext_list[] = {L".dc64r", L".dcr"};
#else
    const std::wstring ext_list[] = {L".dc32r", L".dcr"};
#endif
    const std::set<std::wstring> ext_match(ext_list, ext_list + 2);
    std::error_code ec;
    std::vector<std::filesystem::path> to_load;
    for (const auto& entry : std::filesystem::directory_iterator(
             addon_dir, std::filesystem::directory_options::skip_permission_denied, ec)) {
        if (ec) break;
        if (!entry.is_regular_file()) continue;
        std::wstring ext = entry.path().extension().wstring();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
        if (!ext_match.contains(ext)) continue;
        to_load.push_back(entry.path());
    }
    if (ec || to_load.empty()) {
        return;
    }
    std::sort(to_load.begin(), to_load.end(),
              [](const std::filesystem::path& a, const std::filesystem::path& b) {
                  return a.filename().wstring() < b.filename().wstring();
              });
    for (const auto& path : to_load) {
        const std::wstring path_w = path.wstring();
        const std::wstring product = GetFileProductNameW(path_w);
        if (!product.empty() && _wcsicmp(product.c_str(), L"ReShade") == 0 && g_reshade_module != nullptr) continue;
        if (!product.empty() && _wcsicmp(product.c_str(), L"Display Commander") == 0) continue;
        HMODULE mod = LoadLibraryW(path_w.c_str());
        if (mod != nullptr) {
            std::string name = path.filename().string();
            char msg[384];
            snprintf(msg, sizeof(msg), "[DisplayCommander] Loaded .dc64r/.dc32r/.dcr DLL (after ReShade): %s\n",
                     name.c_str());
            OutputDebugStringA(msg);
        }
    }
}

void ProcessAttach_DetectEntryPoint(HMODULE h_module, std::wstring& entry_point) {
    entry_point = L"addon";
    WCHAR module_path[MAX_PATH];
    if (GetModuleFileNameW(h_module, module_path, MAX_PATH) <= 0) {
        OutputDebugStringA("[DisplayCommander] Entry point detection: Failed to get module filename\n");
        return;
    }
    std::filesystem::path module_file_path(module_path);
    std::wstring module_name = module_file_path.stem().wstring();
    std::wstring module_name_full = module_file_path.filename().wstring();
    std::transform(module_name.begin(), module_name.end(), module_name.begin(), ::towlower);
    std::transform(module_name_full.begin(), module_name_full.end(), module_name_full.begin(), ::towlower);
    int module_utf8_size = WideCharToMultiByte(CP_UTF8, 0, module_name_full.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (module_utf8_size > 0) {
        std::string module_name_utf8(module_utf8_size - 1, '\0');
        WideCharToMultiByte(CP_UTF8, 0, module_name_full.c_str(), -1, module_name_utf8.data(), module_utf8_size,
                            nullptr, nullptr);
        char debug_msg[512];
        snprintf(debug_msg, sizeof(debug_msg),
                 "[DisplayCommander] DEBUG: module_name_full='%s', module_name (stem)='%ls'\n",
                 module_name_utf8.c_str(), module_name.c_str());
        OutputDebugStringA(debug_msg);
    }
    struct ProxyDllInfo {
        const wchar_t* name;
        const wchar_t* entry_point_val;
        const char* debug_msg;
        const char* log_msg;
    };
    const ProxyDllInfo proxy_dlls[] = {
        {L"dxgi", L"dxgi.dll", "[DisplayCommander] Entry point detected: dxgi.dll (proxy mode)\n",
         "Display Commander loaded as dxgi.dll proxy - DXGI functions will be forwarded to system dxgi.dll"},
        {L"d3d11", L"d3d11.dll", "[DisplayCommander] Entry point detected: d3d11.dll (proxy mode)\n",
         "Display Commander loaded as d3d11.dll proxy - D3D11 functions will be forwarded to system d3d11.dll"},
        {L"d3d12", L"d3d12.dll", "[DisplayCommander] Entry point detected: d3d12.dll (proxy mode)\n",
         "Display Commander loaded as d3d12.dll proxy - D3D12 functions will be forwarded to system d3d12.dll"},
        {L"version", L"version.dll", "[DisplayCommander] Entry point detected: version.dll (proxy mode)\n",
         "Display Commander loaded as version.dll proxy - Version functions will be forwarded to system version.dll"},
        {L"opengl32", L"opengl32.dll", "[DisplayCommander] Entry point detected: opengl32.dll (proxy mode)\n",
         "Display Commander loaded as opengl32.dll proxy - OpenGL/WGL functions will be forwarded to system "
         "opengl32.dll"},
        {L"dbghelp", L"dbghelp.dll", "[DisplayCommander] Entry point detected: dbghelp.dll (proxy mode)\n",
         "Display Commander loaded as dbghelp.dll proxy - DbgHelp functions will be forwarded to system dbghelp.dll"},
        {L"vulkan-1", L"vulkan-1.dll", "[DisplayCommander] Entry point detected: vulkan-1.dll (proxy mode)\n",
         "Display Commander loaded as vulkan-1.dll proxy - Vulkan functions will be forwarded to system vulkan-1.dll"}};
    for (const auto& proxy : proxy_dlls) {
        if (_wcsicmp(module_name.c_str(), proxy.name) == 0) {
            entry_point = proxy.entry_point_val;
            OutputDebugStringA(proxy.debug_msg);
            return;
        }
    }
    int module_utf8_size2 = WideCharToMultiByte(CP_UTF8, 0, module_name.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (module_utf8_size2 > 0) {
        std::string module_name_utf8(module_utf8_size2 - 1, '\0');
        WideCharToMultiByte(CP_UTF8, 0, module_name.c_str(), -1, module_name_utf8.data(), module_utf8_size2, nullptr,
                            nullptr);
        char debug_msg[512];
        snprintf(debug_msg, sizeof(debug_msg), "[DisplayCommander] Entry point detected: addon (module: %s)\n",
                 module_name_utf8.c_str());
        OutputDebugStringA(debug_msg);
    } else {
        OutputDebugStringA("[DisplayCommander] Entry point detected: addon\n");
    }
}

bool ProcessAttach_TryLoadReShadeWhenNotLoaded(HMODULE /*h_module*/) {
    OutputDebugStringA("ReShade not loaded");
    settings::g_hook_suppression_settings.LoadAll();
    WCHAR executable_path[MAX_PATH] = {0};
    GetModuleFileNameW(nullptr, executable_path, MAX_PATH);
    std::filesystem::path game_directory = std::filesystem::path(executable_path).parent_path();
    std::filesystem::path dc_reshade_dir = display_commander::utils::GetReshadeDirectoryForLoading(game_directory);
#ifdef _WIN64
    std::filesystem::path reshade_path = dc_reshade_dir / L"Reshade64.dll";
    const char* dll_name = "Reshade64.dll";
#else
    std::filesystem::path reshade_path = dc_reshade_dir / L"Reshade32.dll";
    const char* dll_name = "Reshade32.dll";
#endif
    auto path_exists = std::filesystem::exists(reshade_path);
    LogInfo("[reshade] path_exists = %d path = %s", path_exists, reshade_path.string().c_str());
    if (!path_exists) {
        return true;
    }
    HMODULE already_loaded = GetModuleHandleW(reshade_path.c_str());
    if (already_loaded != nullptr) {
        HMODULE expected = nullptr;
        if (g_reshade_module.compare_exchange_strong(expected, already_loaded)) {
            char path_narrow[MAX_PATH];
            WideCharToMultiByte(CP_ACP, 0, reshade_path.c_str(), -1, path_narrow, MAX_PATH, nullptr, nullptr);
            char msg[512];
            snprintf(msg, sizeof(msg), "%s already loaded from Documents folder: %s", dll_name, path_narrow);
            OutputDebugStringA(msg);
            return true;
        }
    }

    SetEnvironmentVariableW(L"RESHADE_DISABLE_LOADING_CHECK", L"1");
    display_commanderhooks::InstallLoadLibraryHooks();
    display_commanderhooks::g_hooked_before_reshade.store(true);
    HMODULE reshade_module = display_commanderhooks::LoadLibraryW_Direct(reshade_path.c_str());
    if (reshade_module != nullptr) {
        HMODULE expected = nullptr;
        if (g_reshade_module.compare_exchange_strong(expected, reshade_module) || expected != nullptr) {
            char path_narrow[MAX_PATH];
            WideCharToMultiByte(CP_ACP, 0, reshade_path.c_str(), -1, path_narrow, MAX_PATH, nullptr, nullptr);
            char msg[512];
            snprintf(msg, sizeof(msg), "%s loaded successfully from Documents folder: %s", dll_name, path_narrow);
            OutputDebugStringA(msg);
            return true;
        }
    }
    DWORD error = GetLastError();
    wchar_t error_msg[512] = {0};
    DWORD msg_len = FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, error,
                                   MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), error_msg,
                                   sizeof(error_msg) / sizeof(wchar_t), nullptr);
    char path_narrow[MAX_PATH];
    WideCharToMultiByte(CP_ACP, 0, reshade_path.c_str(), -1, path_narrow, MAX_PATH, nullptr, nullptr);
    char msg[1024];
    if (msg_len > 0) {
        while (msg_len > 0 && (error_msg[msg_len - 1] == L'\n' || error_msg[msg_len - 1] == L'\r'))
            error_msg[--msg_len] = L'\0';
        char error_msg_narrow[512];
        WideCharToMultiByte(CP_ACP, 0, error_msg, -1, error_msg_narrow, sizeof(error_msg_narrow), nullptr, nullptr);
        snprintf(msg, sizeof(msg), "Failed to load %s from Documents folder (error %lu: %s): %s %p", dll_name, error,
                 error_msg_narrow, path_narrow, reshade_module);
    } else {
        snprintf(msg, sizeof(msg), "Failed to load %s from Documents folder (error: %lu): %s", dll_name, error,
                 path_narrow);
    }
    OutputDebugStringA(msg);
    MessageBoxA(nullptr, msg, msg, MB_OK | MB_ICONWARNING | MB_TOPMOST);
    return false;
}

void ProcessAttach_NoReShadeModeInit(HMODULE h_module) {
    g_hmodule = h_module;
    auto dc_dir = g_dc_config_directory.load(std::memory_order_acquire);
    display_commander::config::DisplayCommanderConfigManager::GetInstance().Initialize(
        (dc_dir && !dc_dir->empty()) ? std::optional<std::wstring_view>(*dc_dir) : std::nullopt);
    utils::initialize_qpc_timing_constants();
    DoInitializationWithoutHwndSafe(h_module);
}

void ProcessAttach_RegisterAndPostInit(HMODULE h_module, const std::wstring& entry_point) {
    (void)entry_point;
    LogBootRegisterAndPostInitStage("enter");
    DetectMultipleReShadeVersions();
    LogBootRegisterAndPostInitStage("after DetectMultipleReShadeVersions");
    utils::initialize_qpc_timing_constants();
    LogBootRegisterAndPostInitStage("before DoInitializationWithoutHwndSafe");
    DoInitializationWithoutHwndSafe(h_module);
    LogBootRegisterAndPostInitStage("after DoInitializationWithoutHwndSafe");
    ProcessAttach_LoadLocalAddonDllsAfterReShade(h_module);
    LogBootRegisterAndPostInitStage("after ProcessAttach_LoadLocalAddonDllsAfterReShade");
    LoadAddonsFromPluginsDirectory();
    LogBootRegisterAndPostInitStage("after LoadAddonsFromPluginsDirectory");
    if (IsDisplayCommanderHookingInstance()) {
        LogBootRegisterAndPostInitStage("before InstallApiHooks");
        display_commanderhooks::InstallApiHooks();
        LogBootRegisterAndPostInitStage("after InstallApiHooks");
    } else {
        LogBootRegisterAndPostInitStage("skip InstallApiHooks (not hooking instance)");
    }
    LogBootRegisterAndPostInitStage("complete");
}

ProcessAttachEarlyResult ProcessAttach_EarlyChecksAndInit(HMODULE h_module) {
    g_hmodule = h_module;
    g_dll_load_time_ns.store(utils::get_now_ns(), std::memory_order_release);
    g_display_commander_state.store(DisplayCommanderState::DC_STATE_UNDECIDED, std::memory_order_release);

    if (display_commander::utils::version_check::CompareVersions(DISPLAY_COMMANDER_VERSION_STRING,
                                                                 kDisplayCommanderMinLoadVersion)
        < 0) {
        char msg[384];
        snprintf(msg, sizeof(msg), "[DisplayCommander] Version %s is below minimum allowed %s - refusing to load.\n",
                 DISPLAY_COMMANDER_VERSION_STRING, kDisplayCommanderMinLoadVersion);
        OutputDebugStringA(msg);
        return ProcessAttachEarlyResult::RefuseLoad;
    }

    {
        WCHAR exe_path[MAX_PATH] = {};
        if (GetModuleFileNameW(nullptr, exe_path, MAX_PATH) > 0) {
            const wchar_t* last_slash = wcsrchr(exe_path, L'\\');
            const wchar_t* exe_name = (last_slash != nullptr) ? (last_slash + 1) : exe_path;
            if (is_helper_or_crash_handler_exe(exe_name)) {
                OutputDebugStringA("[DisplayCommander] Refusing to load in helper/crash-handler process.\n");
                return ProcessAttachEarlyResult::RefuseLoad;
            }
        }
    }

    g_display_commander_state.store(DisplayCommanderState::DC_STATE_HOOKED, std::memory_order_release);
    LPSTR command_line = GetCommandLineA();
    if (command_line != nullptr && command_line[0] != '\0') {
        OutputDebugStringA("[DisplayCommander] Command line: ");
        OutputDebugStringA(command_line);
        OutputDebugStringA("\n");
        if (strstr(command_line, "rundll32") != nullptr) {
            OutputDebugStringA("Run32DLL command line detected");
            return ProcessAttachEarlyResult::EarlySuccess;
        }
    } else {
        OutputDebugStringA("[DisplayCommander] Command line: (empty)\n");
    }
    g_shutdown.store(false);
    return ProcessAttachEarlyResult::Continue;
}
}  // namespace

namespace display_commander::dll_main {

void OnProcessAttach(HMODULE h_module) {
    ChooseAndSetDcConfigPath(h_module);
    CaptureDllLoadCallerPath(h_module);
    EnsureDisplayCommanderLogWithModulePath(h_module);
    LogBootDcConfigPath();
    LogBootDllMainStage("PROCESS_ATTACH: start (after module path log)");
    static const char* reason = "";
    auto set_process_attached_on_exit = [h_module]() {
        WCHAR current_module_path[MAX_PATH] = {0};
        if (GetModuleFileNameW(h_module, current_module_path, MAX_PATH) > 0) {
            char current_module_path_narrow[MAX_PATH];
            WideCharToMultiByte(CP_ACP, 0, current_module_path, -1, current_module_path_narrow, MAX_PATH, nullptr,
                                nullptr);
            LogInfo("[main_entry] DLL_PROCESS_ATTACH: current module path: %s", current_module_path_narrow);
        }
        LogInfo("[main_entry] DLL_PROCESS_ATTACH: DLL process attach reason: %s, state: %d", reason,
                static_cast<int>(g_display_commander_state.load(std::memory_order_acquire)));
    };
    struct ScopeGuard {
        std::function<void()> run_;
        explicit ScopeGuard(std::function<void()> fn) : run_(std::move(fn)) {}
        ~ScopeGuard() {
            if (run_) run_();
        }
    } guard(set_process_attached_on_exit);

    auto dc_dir = g_dc_config_directory.load(std::memory_order_acquire);
    display_commander::config::DisplayCommanderConfigManager::GetInstance().Initialize(
        (dc_dir && !dc_dir->empty()) ? std::optional<std::wstring_view>(*dc_dir) : std::nullopt);
    LogBootDllMainStage("PROCESS_ATTACH: after DisplayCommanderConfigManager::Initialize");

    if (display_commander::utils::IsLoadedWithDLLExtension(static_cast<void*>(h_module))) {
        LogInfo("[main_entry] DLL_PROCESS_ATTACH: RenameUnusedDcProxyDlls");
        RenameUnusedDcProxyDlls(h_module);
    }
    ProcessAttachEarlyResult early = ProcessAttach_EarlyChecksAndInit(h_module);
    if (early == ProcessAttachEarlyResult::RefuseLoad) {
        reason = "RefuseLoad";
        LogBootDllMainStage("PROCESS_ATTACH: return TRUE (RefuseLoad)");
        return;
    }
    if (early == ProcessAttachEarlyResult::EarlySuccess) {
        reason = "EarlySuccess";
        LogBootDllMainStage("PROCESS_ATTACH: return TRUE (EarlySuccess)");
        return;
    }
    LogBootDllMainStage("PROCESS_ATTACH: after ProcessAttach_EarlyChecksAndInit (continue)");
    g_vulkan1_loaded_during_process_attach_init.store(GetModuleHandleW(L"vulkan-1.dll") != nullptr,
                                                      std::memory_order_release);
    ProcessAttach_DetectReShadeInModules();
    ProcessAttach_LoadLocalAddonDlls(h_module);
    LogBootDllMainStage("PROCESS_ATTACH: after ReShade module detect and local addon DLL load");

    std::wstring entry_point;
    ProcessAttach_DetectEntryPoint(h_module, entry_point);

    if ((g_reshade_module == nullptr) && !g_no_reshade_mode.load()) {
        ProcessAttach_TryLoadReShadeWhenNotLoaded(h_module);
    }
    if (g_reshade_module == nullptr) {
        const bool was_no_reshade = g_no_reshade_mode.load();
        g_no_reshade_mode.store(true);
        if (!was_no_reshade) {
            OutputDebugStringA("[main_entry] ReShade not found - entering no-ReShade mode.\n");
        }
    }

    if (g_no_reshade_mode.load()) {
        LogInfo("[main_entry] DLL_PROCESS_ATTACH: No ReShade mode");
        LogBootDllMainStage("PROCESS_ATTACH: entering no-ReShade mode init");
        ProcessAttach_NoReShadeModeInit(h_module);
        g_dll_initialization_complete.store(true);
        reason = "NoReShadeMode: ReShade not loaded";
        LogBootDllMainStage("PROCESS_ATTACH: no-ReShade path complete");
        return;
    }

    if (!FinishAddonRegistration(h_module, nullptr, false)) {
        LogBootDllMainStage("PROCESS_ATTACH: FinishAddonRegistration failed (return TRUE)");
        CheckReShadeVersionCompatibility();
        {
            char msg[512];
            snprintf(msg, sizeof(msg), "g_module handle: 0x%p", g_hmodule);
            reshade::log::message(reshade::log::level::info, msg);
        }
        HMODULE modules[1024];
        DWORD num_modules_bytes = 0;
        if (K32EnumProcessModules(GetCurrentProcess(), modules, sizeof(modules), &num_modules_bytes) != 0) {
            DWORD num_modules = (std::min<DWORD>)(num_modules_bytes / sizeof(HMODULE),
                                                  static_cast<DWORD>(sizeof(modules) / sizeof(HMODULE)));
            for (DWORD i = 0; i < num_modules; i++) {
                char msg[512];
                wchar_t module_name[MAX_PATH];
                if (GetModuleFileNameW(modules[i], module_name, MAX_PATH) > 0) {
                    snprintf(msg, sizeof(msg), "Module %lu: 0x%p %ls", i, modules[i], module_name);
                } else {
                    snprintf(msg, sizeof(msg), "Module %lu: 0x%p (failed to get name)", i, modules[i]);
                }
                reshade::log::message(reshade::log::level::info, msg);
            }
        }
        reason = "ReShade register addon failed";
        return;
    }
    LogInfo("[main_entry] DLL_PROCESS_ATTACH: RegisterAndPostInit");
    LogBootDllMainStage("PROCESS_ATTACH: before RegisterAndPostInit");

    ProcessAttach_RegisterAndPostInit(h_module, entry_point);
    LogInfo("[main_entry] DLL_PROCESS_ATTACH: RegisterAndPostInit complete");
    g_dll_initialization_complete.store(true);
    reason = "RegisterAndPostInit complete";
    LogBootDllMainStage("PROCESS_ATTACH: complete (ReShade addon registered)");
}

void OnProcessDetach(HMODULE h_module) {
    LogBootDllMainStage("DLL_PROCESS_DETACH: entered");
    if (g_reshade_module == nullptr) {
        LogBootDllMainStage("DLL_PROCESS_DETACH: early return (ReShade module was never set)");
        return;
    }
    LogInfo("DLL_PROCESS_DETACH: DLL process detach");
    g_shutdown.store(true);

    exit_handler::OnHandleExit(exit_handler::ExitSource::DLL_PROCESS_DETACH_EVENT, "DLL process detach");

    display_commanderhooks::UninstallWindowProcHooks();

    display_commanderhooks::UninstallApiHooks();

    UninstallVulkanLoaderHooks();

    UninstallNvLowLatencyVkHooks();

    StopContinuousMonitoring();
    StopGPUCompletionMonitoring();

    dxgi::fps_limiter::StopRefreshRateMonitoring();

    if (g_reflexProvider) {
        g_reflexProvider->Shutdown();
    }

    if (g_module_pinned.load() && g_hmodule != nullptr) {
        if (FreeLibrary(g_hmodule) != 0) {
            LogInfo("Module unpinned successfully: 0x%p", g_hmodule);
        } else {
            DWORD error = GetLastError();
            LogWarn("Failed to unpin module: 0x%p, Error: %lu", g_hmodule, error);
        }
        g_hmodule = nullptr;
        g_module_pinned.store(false);
    }

    reshade::unregister_addon(h_module);
    LogBootDllMainStage("DLL_PROCESS_DETACH: before logger shutdown");
    display_commander::logger::Shutdown();
}

}  // namespace display_commander::dll_main
