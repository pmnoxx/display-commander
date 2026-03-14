// Source Code <Display Commander>
#include "reshade_version_download.hpp"
#include "../config/display_commander_config.hpp"
#include "reshade_load_path.hpp"
#include "safe_remove.hpp"
#include "version_check.hpp"

// Libraries <standard C++>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>

// Libraries <Windows.h>
#include <Windows.h>

// Libraries <Windows>
#include <ShlObj.h>

namespace display_commander::utils {

namespace {
std::atomic<ReshadeDownloadStatus> g_status{ReshadeDownloadStatus::Idle};
std::atomic<std::string*> g_last_error{nullptr};

void SetError(const std::string& msg) {
    std::string* old = g_last_error.exchange(new std::string(msg));
    delete old;
    g_status.store(ReshadeDownloadStatus::Error, std::memory_order_release);
}

void ClearError() {
    std::string* old = g_last_error.exchange(nullptr);
    delete old;
}

std::filesystem::path GetReshadeVersionFolder(const std::string& version) {
    wchar_t path[MAX_PATH];
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, path))) {
        return std::filesystem::path();
    }
    std::filesystem::path base(path);
    base /= L"Programs";
    base /= L"Display_Commander";
    base /= L"Reshade";
    base /= L"Dll";
    base /= std::filesystem::path(version);
    return base;
}

// If to_global_root true, dest_dir is the Reshade global root (overwrite single version); else Reshade\Dll\X.Y.Z.
static void ReshadeVersionDownloadWorker(std::string version, bool to_global_root) {
    ReshadeDownloadStatus expected = ReshadeDownloadStatus::Idle;
    if (!g_status.compare_exchange_strong(expected, ReshadeDownloadStatus::Downloading, std::memory_order_acq_rel)) {
        return;  // Already in progress
    }
    ClearError();

    wchar_t temp_path[MAX_PATH];
    if (GetTempPathW(static_cast<DWORD>(std::size(temp_path)), temp_path) == 0) {
        SetError("Could not get temp path.");
        return;
    }
    std::filesystem::path temp_dir = std::filesystem::path(temp_path) / L"dc_reshade_download";
    std::error_code ec;
    std::filesystem::create_directories(temp_dir, ec);
    if (ec) {
        SetError("Could not create temp directory.");
        g_status.store(ReshadeDownloadStatus::Idle, std::memory_order_release);
        return;
    }

    std::string url = "https://reshade.me/downloads/ReShade_Setup_" + version + "_Addon.exe";
    std::filesystem::path exe_path = temp_dir / "ReShade_Setup_Addon.exe";

    if (!display_commander::utils::version_check::DownloadBinaryFromUrl(url, exe_path)) {
        SetError("Download failed (check network or URL).");
        g_status.store(ReshadeDownloadStatus::Idle, std::memory_order_release);
        return;
    }

    g_status.store(ReshadeDownloadStatus::Extracting, std::memory_order_release);

    std::wstring cmd_line = L"tar.exe -xf \"";
    cmd_line += exe_path.wstring();
    cmd_line += L"\" ReShade64.dll ReShade32.dll";

    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi = {};
    if (!CreateProcessW(nullptr, &cmd_line[0], nullptr, nullptr, FALSE, 0, nullptr, temp_dir.c_str(), &si, &pi)) {
        SetError("Extract failed (need Windows 10+ tar.exe).");
        g_status.store(ReshadeDownloadStatus::Idle, std::memory_order_release);
        return;
    }
    CloseHandle(pi.hThread);
    WaitForSingleObject(pi.hProcess, 60000);
    CloseHandle(pi.hProcess);

    std::filesystem::path extracted64 = temp_dir / "ReShade64.dll";
    std::filesystem::path extracted32 = temp_dir / "ReShade32.dll";
    if (!std::filesystem::exists(extracted64) || !std::filesystem::exists(extracted32)) {
        SetError("Extraction did not produce DLLs.");
        g_status.store(ReshadeDownloadStatus::Idle, std::memory_order_release);
        return;
    }

    std::filesystem::path dest_dir =
        to_global_root ? GetGlobalReshadeDirectory() : GetReshadeVersionFolder(version);
    std::filesystem::create_directories(dest_dir, ec);
    if (ec) {
        SetError("Could not create destination directory.");
        g_status.store(ReshadeDownloadStatus::Idle, std::memory_order_release);
        return;
    }

    std::filesystem::copy_file(extracted64, dest_dir / "ReShade64.dll",
                               std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) {
        SetError("Could not copy ReShade64.dll.");
        g_status.store(ReshadeDownloadStatus::Idle, std::memory_order_release);
        return;
    }
    std::filesystem::copy_file(extracted32, dest_dir / "ReShade32.dll",
                               std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) {
        SetError("Could not copy ReShade32.dll.");
        g_status.store(ReshadeDownloadStatus::Idle, std::memory_order_release);
        return;
    }

    SafeRemoveAll(temp_dir, ec);
    g_status.store(ReshadeDownloadStatus::Ready, std::memory_order_release);
}

}  // namespace

ReshadeDownloadStatus GetReshadeDownloadStatus() { return g_status.load(std::memory_order_acquire); }

const char* GetReshadeDownloadStatusMessage() {
    std::string* msg = g_last_error.load(std::memory_order_acquire);
    return msg && !msg->empty() ? msg->c_str() : "";
}

void StartReshadeVersionDownloadToGlobalRoot(const std::string& version) {
    if (version.empty()) {
        return;
    }
    std::thread t(ReshadeVersionDownloadWorker, version, true);
    t.detach();
}

}  // namespace display_commander::utils
