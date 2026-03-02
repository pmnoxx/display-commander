// Source Code <Display Commander>

// Group 1 — Source Code (Display Commander)
#include "standalone_launcher.hpp"
#include "general_utils.hpp"
#include "globals.hpp"

// Group 2 — ReShade / ImGui
// (none)

// Group 3 — Standard C++
#include <filesystem>
#include <string>

// Group 4 — Windows.h
#include <windows.h>

// Group 5 — Other Windows SDK
#include <Shellapi.h>
#include <ShlObj.h>

namespace display_commander::utils {

namespace {

std::filesystem::path GetAddonSourcePath(std::string* out_error) {
    if (g_hmodule != nullptr) {
        WCHAR buf[MAX_PATH];
        if (GetModuleFileNameW(g_hmodule, buf, MAX_PATH) == 0) {
            if (out_error) *out_error = "GetModuleFileName failed.";
            return std::filesystem::path();
        }
        std::filesystem::path p(buf);
        std::error_code ec;
        if (!std::filesystem::exists(p, ec)) {
            if (out_error) *out_error = "Addon path does not exist.";
            return std::filesystem::path();
        }
        return p;
    }
    // Standalone exe: look for addon next to exe
    WCHAR exe_buf[MAX_PATH];
    if (GetModuleFileNameW(nullptr, exe_buf, MAX_PATH) == 0) {
        if (out_error) *out_error = "GetModuleFileName (exe) failed.";
        return std::filesystem::path();
    }
    std::filesystem::path exe_dir = std::filesystem::path(exe_buf).parent_path();
#ifdef _WIN64
    std::filesystem::path addon_path = exe_dir / L"zzz_display_commander.addon64";
#else
    std::filesystem::path addon_path = exe_dir / L"zzz_display_commander.addon32";
#endif
    std::error_code ec;
    if (!std::filesystem::exists(addon_path, ec)) {
        if (out_error) *out_error = "Addon not found next to exe (zzz_display_commander.addon64/32).";
        return std::filesystem::path();
    }
    return addon_path;
}

}  // namespace

bool TryInstallAddonToAppDataAndLaunchGamesUI(std::string* out_error) {
    std::string err;
    if (out_error) out_error->clear();

    std::filesystem::path source = GetAddonSourcePath(out_error ? &err : nullptr);
    if (source.empty()) {
        if (out_error) *out_error = err;
        return false;
    }

    std::filesystem::path target_dir = GetDisplayCommanderAppDataFolder();
    if (target_dir.empty()) {
        if (out_error) *out_error = "LocalAppData\\Programs\\Display_Commander unavailable.";
        return false;
    }

    std::error_code ec;
    if (!std::filesystem::create_directories(target_dir, ec)) {
        if (!std::filesystem::is_directory(target_dir, ec)) {
            if (out_error) *out_error = "Failed to create Display_Commander folder.";
            return false;
        }
    }

    std::filesystem::path target_file = target_dir / source.filename();
    bool copied = false;
    if (CreateHardLinkW(target_file.c_str(), source.c_str(), nullptr) != 0) {
        copied = true;
    } else {
        if (CopyFileW(source.c_str(), target_file.c_str(), FALSE) != 0) {
            copied = true;
        }
    }
    if (!copied) {
        if (out_error) *out_error = "Failed to copy/hardlink addon to AppData.";
        return false;
    }

    // rundll32.exe "path\to\addon.addon64",Launcher
    std::wstring args = L"\"" + target_file.native() + L"\",Launcher";
    HINSTANCE ret = ShellExecuteW(nullptr, L"open", L"rundll32.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
    if (reinterpret_cast<uintptr_t>(ret) <= 32) {
        if (out_error) *out_error = "ShellExecute rundll32 failed.";
        return false;
    }
    return true;
}

}  // namespace display_commander::utils
