// Source Code <Display Commander> // follow this order for includes in all files + add this comment at the top
#include "reshade_addon_handlers.hpp"

#include "config/display_commander_config.hpp"
#include "config/override_reshade_settings.hpp"
#include "globals.hpp"
#include "nvapi/nvapi_actual_refresh_rate_monitor.hpp"
#include "settings/main_tab_settings.hpp"
#include "swapchain_events.hpp"
#include "utils/detour_call_tracker.hpp"
#include "utils/logging.hpp"
#include "utils/timing.hpp"

// Libraries <standard C++>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <set>
#include <string>
#include <vector>

// Libraries <Windows.h>
#include <Windows.h>

// Libraries <Windows>
#include <shlobj.h>

namespace {
void OnInitEffectRuntime_StartRefreshRateMonitoringIfNeeded() {
    if (settings::g_mainTabSettings.show_actual_refresh_rate.GetValue()
        || settings::g_mainTabSettings.show_refresh_rate_frame_times.GetValue()) {
        display_commander::nvapi::StartNvapiActualRefreshRateMonitoring();
    }
}

void OnInitEffectRuntime_InitWithHwndOnce(reshade::api::effect_runtime* runtime) {
    static bool initialized_with_hwnd = false;
    if (initialized_with_hwnd) {
        return;
    }
    HWND game_window = static_cast<HWND>(runtime->get_hwnd());
    if (game_window == nullptr) {
        return;
    }
    initialized_with_hwnd = true;
    if (IsWindow(game_window) != 0) {
        LogInfo("[OnInitEffectRuntime] Game window detected - HWND: 0x%p", game_window);
        DoInitializationWithHwnd(game_window);
        LogInfo("[OnInitEffectRuntime] DoInitializationWithHwnd returned");
    } else {
        LogWarn("[OnInitEffectRuntime] ReShade runtime window is not valid - HWND: 0x%p", game_window);
    }
}

bool IsAddonEnabledForLoading(const std::string& addon_name, const std::string& addon_file) {
    if (g_reshade_module == nullptr) {
        return false;
    }
    std::vector<std::string> enabled_addons;
    display_commander::config::get_config_value("ADDONS", "EnabledAddons", enabled_addons);

    std::string identifier = addon_name + "@" + addon_file;

    for (const auto& enabled_entry : enabled_addons) {
        if (enabled_entry == identifier) {
            return true;
        }
    }

    return false;
}

bool LoadAddonsFromPluginsDirectory_EnsureAddonsDir(std::filesystem::path& out_addons_dir) {
    wchar_t localappdata_path[MAX_PATH];
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, localappdata_path))) {
        LogWarn("Failed to get LocalAppData folder path, skipping addon loading from Addons directory");
        return false;
    }
    out_addons_dir =
        std::filesystem::path(localappdata_path) / L"Programs" / L"Display_Commander" / L"Reshade" / L"Addons";
    try {
        std::error_code ec;
        std::filesystem::create_directories(out_addons_dir, ec);
        if (ec) {
            LogWarn("Failed to create Addons directory: %ls (error: %s)", out_addons_dir.c_str(), ec.message().c_str());
            return false;
        }
        LogInfo("Created/verified Addons directory: %ls", out_addons_dir.c_str());
        return true;
    } catch (const std::exception& e) {
        LogWarn("Exception while creating Addons directory: %s", e.what());
        return false;
    }
}

void LoadAddonsFromPluginsDirectory_IterateAndLoad(const std::filesystem::path& addons_dir) {
    try {
        std::error_code ec;
        int loaded_count = 0;
        int failed_count = 0;
        int skipped_count = 0;

        for (const auto& entry : std::filesystem::directory_iterator(
                 addons_dir, std::filesystem::directory_options::skip_permission_denied, ec)) {
            if (ec) {
                LogWarn("Error accessing Addons directory: %s", ec.message().c_str());
                continue;
            }
            if (!entry.is_regular_file()) continue;

            const auto& path = entry.path();
            const auto extension = path.extension();
            if (extension != L".addon64" && extension != L".addon32") continue;
#ifdef _WIN64
            if (extension != L".addon64") continue;
#else
            if (extension != L".addon32") continue;
#endif
            std::string addon_name = path.stem().string();
            std::string addon_file = path.filename().string();
            if (!IsAddonEnabledForLoading(addon_name, addon_file)) {
                LogInfo("Skipping disabled addon: %ls", path.c_str());
                skipped_count++;
                continue;
            }
            LogInfo("Loading enabled addon from Addons directory: %ls", path.c_str());
            HMODULE module = LoadLibraryExW(path.c_str(), nullptr,
                                            LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
            if (module == nullptr) {
                LogError("Failed to load addon from '%ls' (error: %lu)", path.c_str(), GetLastError());
                failed_count++;
            } else {
                LogInfo("Successfully loaded addon from '%ls'", path.c_str());
                loaded_count++;
                std::string lower = addon_file;
                std::transform(lower.begin(), lower.end(), lower.begin(),
                               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                if (lower.find("renodx") != std::string::npos && lower.find("renodx-devkit") == std::string::npos) {
                    g_is_renodx_loaded.store(true, std::memory_order_relaxed);
                }
            }
        }
        if (loaded_count > 0 || failed_count > 0 || skipped_count > 0) {
            LogInfo("Addon loading from Addons directory completed: %d loaded, %d failed, %d skipped (disabled)",
                    loaded_count, failed_count, skipped_count);
        }
    } catch (const std::exception& e) {
        LogWarn("Exception while loading addons from Addons directory: %s", e.what());
    }
}
}  // namespace

void OnInitCommandList(reshade::api::command_list* cmd_list) {
    CALL_GUARD_NO_TS();;
    if (cmd_list == nullptr) {
        return;
    }
}

void OnDestroyCommandList(reshade::api::command_list* cmd_list) {
    CALL_GUARD_NO_TS();;
    if (cmd_list == nullptr) {
        return;
    }
}

void OnInitCommandQueue(reshade::api::command_queue* queue) {
    CALL_GUARD_NO_TS();;
    if (queue == nullptr) {
        return;
    }
}

void OnDestroyCommandQueue(reshade::api::command_queue* queue) {
    CALL_GUARD_NO_TS();;
    if (queue == nullptr) {
        return;
    }
}

void OnExecuteCommandList(reshade::api::command_queue* queue, reshade::api::command_list* cmd_list) {
    CALL_GUARD_NO_TS();;
    if (queue == nullptr || cmd_list == nullptr) {
        return;
    }
}

void OnFinishPresent(reshade::api::command_queue* queue, reshade::api::swapchain* swapchain) {
    CALL_GUARD_NO_TS();;
    if (queue == nullptr || swapchain == nullptr) {
        return;
    }
}

void OnReShadeBeginEffects(reshade::api::effect_runtime* runtime, reshade::api::command_list* cmd_list,
                           reshade::api::resource_view rtv, reshade::api::resource_view rtv_srgb) {
    CALL_GUARD_NO_TS();;
    if (runtime == nullptr || cmd_list == nullptr) {
        return;
    }
    (void)rtv;
    (void)rtv_srgb;
}

void OnReShadeFinishEffects(reshade::api::effect_runtime* runtime, reshade::api::command_list* cmd_list,
                            reshade::api::resource_view rtv, reshade::api::resource_view rtv_srgb) {
    CALL_GUARD_NO_TS();;
    if (runtime == nullptr || cmd_list == nullptr) {
        return;
    }
    (void)rtv;
    (void)rtv_srgb;
}

void OnReShadePresent(reshade::api::effect_runtime* runtime) { (void)runtime; }

void OnInitEffectRuntime(reshade::api::effect_runtime* runtime) {
    LogInfo("[OnInitEffectRuntime] entry");
    CALL_GUARD_NO_TS();;
    if (runtime == nullptr) {
        LogInfo("[OnInitEffectRuntime] runtime is null, returning");
        return;
    }
    AddReShadeRuntime(runtime);
    LogInfo("[OnInitEffectRuntime] after AddReShadeRuntime");

    LogInfo("[OnInitEffectRuntime] ReShade effect runtime initialized - Input blocking now available");

    OnInitEffectRuntime_StartRefreshRateMonitoringIfNeeded();
    OnInitEffectRuntime_InitWithHwndOnce(runtime);

    OverrideReShadeSettings(runtime);

    LogInfo("[OnInitEffectRuntime] exit");
}

bool OnReShadeOverlayOpen(reshade::api::effect_runtime* runtime, bool open, reshade::api::input_source source) {
    CALL_GUARD_NO_TS();;
    (void)source;

    if (open) {
        LogInfo("ReShade overlay opened - Input blocking active");
        if (runtime != nullptr) {
            AddReShadeRuntime(runtime);
        }
    } else {
        LogInfo("ReShade overlay closed - Input blocking inactive");
    }

    return false;
}

void LoadAddonsFromPluginsDirectory() {
    OutputDebugStringA("Loading addons from Addons directory");
    std::filesystem::path addons_dir;
    if (!LoadAddonsFromPluginsDirectory_EnsureAddonsDir(addons_dir)) {
        return;
    }
    if (g_reshade_module == nullptr) {
        LogInfo("ReShade not loaded yet, skipping addon loading from Addons directory");
        return;
    }
    OutputDebugStringA("ReShade loaded, attempting to load addons from Addons directory");
    LoadAddonsFromPluginsDirectory_IterateAndLoad(addons_dir);
}
