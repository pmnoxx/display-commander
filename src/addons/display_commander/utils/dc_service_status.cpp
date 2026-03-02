// Source Code <Display Commander> // follow this order for includes in all files + add this comment at the top
// Headers <Display Commander>
#include "dc_service_status.hpp"
#include "dc_load_path.hpp"
#include "general_utils.hpp"

// Libraries <ReShade> / <imgui>

// Libraries <standard C++>
#include <filesystem>
#include <string>
#include <vector>

// Libraries <Windows.h> — before other Windows headers
#include <Windows.h>

// Libraries <Windows>

namespace display_commander::dc_service {

namespace {

// Named mutexes: enforce at most one service instance per architecture per session.
constexpr const wchar_t* kServiceMutexName32 = L"Local\\DisplayCommander_DCServiceMutex32";
constexpr const wchar_t* kServiceMutexName64 = L"Local\\DisplayCommander_DCServiceMutex64";

// Shared memory: expose simple status (version + running flag + creator PID).
constexpr const wchar_t* kServiceStateName32 = L"Local\\DisplayCommander_DCServiceState32";
constexpr const wchar_t* kServiceStateName64 = L"Local\\DisplayCommander_DCServiceState64";

struct DcServiceSharedState {
    std::uint32_t version;
    std::uint32_t running;
    std::uint32_t pid;
    std::uint32_t reserved;
};

constexpr std::uint32_t kSharedStateVersion = 1;

// Keep handles alive for the lifetime of the process so that the mutex and mapping
// remain valid while the service is running.
HANDLE g_service_mutex_handle = nullptr;
HANDLE g_service_mapping_handle = nullptr;

const wchar_t* GetMutexNameForCurrentArch() {
#ifdef _WIN64
    return kServiceMutexName64;
#else
    return kServiceMutexName32;
#endif
}

const wchar_t* GetStateNameForCurrentArch() {
#ifdef _WIN64
    return kServiceStateName64;
#else
    return kServiceStateName32;
#endif
}

const wchar_t* GetStateNameForArch(ServiceArchitecture arch) {
    return (arch == ServiceArchitecture::X64) ? kServiceStateName64 : kServiceStateName32;
}

constexpr const wchar_t* kAddon64 = L"zzz_display_commander.addon64";
constexpr const wchar_t* kAddon32 = L"zzz_display_commander.addon32";

}  // namespace

std::filesystem::path GetAddonPathForArch(ServiceArchitecture arch) {
    const wchar_t* name = (arch == ServiceArchitecture::X64) ? kAddon64 : kAddon32;
    std::error_code ec;

    auto try_dir = [&name, &ec](const std::filesystem::path& dir) -> std::filesystem::path {
        if (dir.empty()) return {};
        std::filesystem::path p = dir / name;
        if (std::filesystem::exists(p, ec)) return p;
        return {};
    };

    std::filesystem::path result = try_dir(display_commander::utils::GetDcDirectoryForLoading(nullptr));
    if (!result.empty()) return result;

    std::filesystem::path base = display_commander::utils::GetLocalDcDirectory();
    result = try_dir(base);
    if (!result.empty()) return result;

    result = try_dir(base / L"Reshade" / L"Addons");
    if (!result.empty()) return result;

    for (const wchar_t* sub : {L"stable", L"Debug"}) {
        std::filesystem::path sub_base = base / sub;
        if (!std::filesystem::exists(sub_base, ec)) continue;
        for (const auto& entry : std::filesystem::directory_iterator(sub_base, ec)) {
            if (ec || !entry.is_directory()) continue;
            result = try_dir(entry.path());
            if (!result.empty()) return result;
        }
    }
    return {};
}

bool StartService(ServiceArchitecture arch) {
    std::filesystem::path addon_path = GetAddonPathForArch(arch);
    if (addon_path.empty()) return false;

    wchar_t sysdir[MAX_PATH];
    if (GetSystemDirectoryW(sysdir, MAX_PATH) == 0) return false;
    std::filesystem::path rundll_path = std::filesystem::path(sysdir) / L"rundll32.exe";
    if (!std::filesystem::exists(rundll_path)) return false;

    std::wstring addon_str = addon_path.wstring();
    std::vector<wchar_t> cmd_buf;
    cmd_buf.reserve(addon_str.size() + 16);
    cmd_buf.push_back(L'\"');
    cmd_buf.insert(cmd_buf.end(), addon_str.begin(), addon_str.end());
    cmd_buf.push_back(L'\"');
    cmd_buf.push_back(L',');
    cmd_buf.push_back(L'S');
    cmd_buf.push_back(L't');
    cmd_buf.push_back(L'a');
    cmd_buf.push_back(L'r');
    cmd_buf.push_back(L't');
    cmd_buf.push_back(L'\0');

    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi = {};
    std::wstring rundll_w = rundll_path.wstring();
    BOOL ok = CreateProcessW(rundll_w.c_str(), cmd_buf.data(), nullptr, nullptr, FALSE,
                             CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    if (ok) {
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }
    return ok != FALSE;
}

bool StopService(ServiceArchitecture arch) {
    ServiceStatus st = QueryServiceStatus(arch);
    if (!st.running || st.pid == 0) return false;
    HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, st.pid);
    if (h == nullptr) return false;
    BOOL ok = TerminateProcess(h, 0);
    CloseHandle(h);
    return ok != FALSE;
}

bool InitializeServiceForCurrentProcess() {
    if (g_service_mutex_handle != nullptr) {
        // Already initialized in this process.
        return true;
    }

    const wchar_t* mutex_name = GetMutexNameForCurrentArch();
    const wchar_t* state_name = GetStateNameForCurrentArch();

    HANDLE mutex = CreateMutexW(nullptr, FALSE, mutex_name);
    if (mutex == nullptr) {
        // Failed to create/open mutex; do not start another service instance.
        return false;
    }

    DWORD last_error = GetLastError();
    if (last_error == ERROR_ALREADY_EXISTS) {
        // Another process already owns/created the mutex: service is already running for this arch.
        CloseHandle(mutex);
        return false;
    }

    // We are the first service instance for this architecture in this session.
    g_service_mutex_handle = mutex;

    // Create shared memory to publish simple status (running + PID).
    HANDLE mapping =
        CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(DcServiceSharedState), state_name);
    if (mapping == nullptr) {
        // Best-effort only: mutex is still enough to enforce single instance.
        return true;
    }

    g_service_mapping_handle = mapping;

    void* view = MapViewOfFile(mapping, FILE_MAP_WRITE, 0, 0, sizeof(DcServiceSharedState));
    if (view == nullptr) {
        // Cannot publish state, but single-instance guarantee still holds.
        return true;
    }

    auto* state = static_cast<DcServiceSharedState*>(view);
    state->version = kSharedStateVersion;
    state->running = 1;
    state->pid = static_cast<std::uint32_t>(GetCurrentProcessId());
    state->reserved = 0;

    UnmapViewOfFile(view);
    return true;
}

ServiceStatus QueryServiceStatus(ServiceArchitecture arch) {
    ServiceStatus result{};

    const wchar_t* state_name = GetStateNameForArch(arch);

    HANDLE mapping = OpenFileMappingW(FILE_MAP_READ, FALSE, state_name);
    if (mapping == nullptr) {
        return result;  // Not running or state not available
    }

    void* view = MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, sizeof(DcServiceSharedState));
    if (view == nullptr) {
        CloseHandle(mapping);
        return result;
    }

    const auto* state = static_cast<const DcServiceSharedState*>(view);
    if (state->version == kSharedStateVersion && state->running != 0 && state->pid != 0) {
        result.running = true;
        result.pid = state->pid;
    }

    UnmapViewOfFile(view);
    CloseHandle(mapping);
    return result;
}

}  // namespace display_commander::dc_service
