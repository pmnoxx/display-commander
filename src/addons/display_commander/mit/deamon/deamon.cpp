// Source Code <Display Commander> // follow this order for includes in all files + add this comment at the top
#include "deamon.hpp"
#include "../../utils/general_utils.hpp"
#include "../../utils/logging.hpp"
#include "../../utils/srwlock_wrapper.hpp"
#include "../../utils/string_utils.hpp"

// Libraries <standard C++>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

// Libraries <Windows.h>
#include <Windows.h>

namespace display_commander::mit::deamon {
namespace {

#if defined(_WIN64)
constexpr wchar_t kDestDllName[] = L"dc_64.dll";
#else
constexpr wchar_t kDestDllName[] = L"dc_32.dll";
#endif

constexpr wchar_t kDaemonFolderName[] = L"daemon";
constexpr wchar_t kLogFileName[] = L"daemon.txt";
constexpr char kRundllEntry[] = "Daemon";

SRWLOCK g_status_lock = SRWLOCK_INIT;
DaemonStatus g_status;
std::atomic<bool> g_started{false};

std::wstring QueryCurrentExeFullPath() {
    wchar_t buf[4096] = {};
    DWORD n = static_cast<DWORD>(sizeof(buf) / sizeof(buf[0]));
    if (QueryFullProcessImageNameW(GetCurrentProcess(), 0, buf, &n) != 0) {
        return std::wstring(buf);
    }
    const DWORD err = GetLastError();
    if (err == ERROR_INSUFFICIENT_BUFFER && n > 1) {
        std::wstring grow(static_cast<size_t>(n), L'\0');
        DWORD n2 = n;
        if (QueryFullProcessImageNameW(GetCurrentProcess(), 0, grow.data(), &n2) != 0) {
            grow.resize(n2);
            return grow;
        }
    }
    wchar_t fallback[MAX_PATH] = {};
    if (GetModuleFileNameW(nullptr, fallback, MAX_PATH) != 0) {
        return fallback;
    }
    return {};
}

std::wstring GetModuleFullPath(HMODULE module) {
    DWORD cap = 1024;
    std::wstring buf(cap, L'\0');
    for (;;) {
        const DWORD n = GetModuleFileNameW(module, buf.data(), cap);
        if (n == 0) {
            return {};
        }
        if (n < cap - 1) {
            buf.resize(n);
            return buf;
        }
        cap *= 2;
        buf.assign(cap, L'\0');
    }
}

std::wstring GetRundll32Path() {
    wchar_t sysdir[MAX_PATH] = {};
    const UINT n = GetSystemDirectoryW(sysdir, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) {
        return L"rundll32.exe";
    }
    std::wstring path(sysdir);
    path += L"\\rundll32.exe";
    return path;
}

std::uint64_t Fnv1a64Bytes(const void* data, size_t size) {
    constexpr std::uint64_t kOffset = 14695981039346656037ull;
    constexpr std::uint64_t kPrime = 1099511628211ull;
    std::uint64_t hash = kOffset;
    const auto* bytes = static_cast<const unsigned char*>(data);
    for (size_t i = 0; i < size; ++i) {
        hash ^= bytes[i];
        hash *= kPrime;
    }
    return hash;
}

std::wstring HashToHex16(std::uint64_t hash) {
    wchar_t out[17] = {};
    swprintf_s(out, L"%016llx", static_cast<unsigned long long>(hash));
    return out;
}

bool IsInvalidPathChar(wchar_t c) {
    return c == L'<' || c == L'>' || c == L':' || c == L'"' || c == L'/' || c == L'\\' || c == L'|' || c == L'?'
           || c == L'*';
}

std::wstring SanitizeFileName(std::wstring name) {
    if (name.empty()) {
        return L"unknown.exe";
    }
    for (wchar_t& c : name) {
        if (IsInvalidPathChar(c) || c < 32) {
            c = L'_';
        }
    }
    return name;
}

std::wstring ExeFileNameFromPath(const std::wstring& exe_path) {
    const size_t slash = exe_path.find_last_of(L"\\/");
    if (slash == std::wstring::npos) {
        return SanitizeFileName(exe_path);
    }
    return SanitizeFileName(exe_path.substr(slash + 1));
}

std::wstring MakeDaemonFolderName(const std::wstring& exe_path) {
    const std::uint64_t hash = Fnv1a64Bytes(exe_path.data(), exe_path.size() * sizeof(wchar_t));
    return HashToHex16(hash) + L"_" + ExeFileNameFromPath(exe_path);
}

void StoreStatus(DaemonStatus status) {
    ::utils::SRWLockExclusive lock(g_status_lock);
    g_status = std::move(status);
}

bool AppendLogLine(const std::wstring& log_path, const char* line) {
    LogInfo("[Daemon] %s", line);
    HANDLE file = CreateFileW(log_path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                              OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        LogWarn("[Daemon] failed to append daemon.txt, error=%lu [Path] %s", GetLastError(),
                display_commander::utils::WideToUtf8(log_path).c_str());
        return false;
    }
    DWORD written = 0;
    const DWORD len = static_cast<DWORD>(strlen(line));
    WriteFile(file, line, len, &written, nullptr);
    WriteFile(file, "\n", 1, &written, nullptr);
    CloseHandle(file);
    return true;
}

bool WriteLogReset(const std::wstring& log_path, const char* line) {
    LogInfo("[Daemon] %s", line);
    HANDLE file = CreateFileW(log_path.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                              CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        LogWarn("[Daemon] failed to create daemon.txt, error=%lu [Path] %s", GetLastError(),
                display_commander::utils::WideToUtf8(log_path).c_str());
        return false;
    }
    DWORD written = 0;
    const DWORD len = static_cast<DWORD>(strlen(line));
    WriteFile(file, line, len, &written, nullptr);
    WriteFile(file, "\n", 1, &written, nullptr);
    CloseHandle(file);
    return true;
}

std::wstring ResolveDaemonTxtPath() {
    HMODULE mod = nullptr;
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(reinterpret_cast<const void*>(&ResolveDaemonTxtPath)), &mod)
            == 0
        || mod == nullptr) {
        return {};
    }
    const std::wstring dll_path = GetModuleFullPath(mod);
    if (dll_path.empty()) {
        return {};
    }
    return (std::filesystem::path(dll_path).parent_path() / kLogFileName).wstring();
}

bool IsPidAlive(DWORD pid) {
    if (pid == 0) {
        return false;
    }
    HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, pid);
    if (process == nullptr) {
        return GetLastError() == ERROR_ACCESS_DENIED;
    }
    const DWORD wait = WaitForSingleObject(process, 0);
    CloseHandle(process);
    return wait == WAIT_TIMEOUT;
}

bool CopyDllToDemonFolder(const std::wstring& source, const std::wstring& dest, DWORD& error_out, bool& copied) {
    copied = false;
    error_out = 0;
    if (source.empty() || dest.empty()) {
        error_out = ERROR_PATH_NOT_FOUND;
        return false;
    }
    if (_wcsicmp(source.c_str(), dest.c_str()) == 0) {
        copied = true;
        return true;
    }
    if (CopyFileW(source.c_str(), dest.c_str(), FALSE) != 0) {
        copied = true;
        return true;
    }
    error_out = GetLastError();
    if (error_out == ERROR_SHARING_VIOLATION && GetFileAttributesW(dest.c_str()) != INVALID_FILE_ATTRIBUTES) {
        copied = false;
        return true;
    }
    return false;
}

bool StartRundll32Daemon(const std::wstring& dest_dll, DWORD target_pid, DWORD& out_daemon_pid, DWORD& error_out) {
    out_daemon_pid = 0;
    error_out = 0;
    const std::filesystem::path dest_path(dest_dll);
    const std::wstring folder = dest_path.parent_path().wstring();
    const std::wstring dll_name = dest_path.filename().wstring();
    const std::wstring rundll = GetRundll32Path();
    // Relative DLL name + working directory avoids rundll32 mis-parsing folder names that contain ".exe".
    std::wstring cmd;
    cmd += L'"';
    cmd += rundll;
    cmd += L"\" .\\";
    cmd += dll_name;
    cmd += L',';
    for (const char* p = kRundllEntry; *p != '\0'; ++p) {
        cmd += static_cast<wchar_t>(*p);
    }
    cmd += L' ';
    cmd += std::to_wstring(target_pid);

    std::vector<wchar_t> cmd_buf(cmd.begin(), cmd.end());
    cmd_buf.push_back(L'\0');

    LogInfo("[Daemon] rundll32 cmdline: %s [Path] %s", display_commander::utils::WideToUtf8(cmd).c_str(),
            display_commander::utils::WideToUtf8(folder).c_str());

    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi = {};
    const BOOL ok = CreateProcessW(rundll.c_str(), cmd_buf.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr,
                                   folder.empty() ? nullptr : folder.c_str(), &si, &pi);
    if (ok == 0) {
        error_out = GetLastError();
        return false;
    }
    out_daemon_pid = pi.dwProcessId;
    if (pi.hThread != nullptr) {
        CloseHandle(pi.hThread);
    }
    if (pi.hProcess != nullptr) {
        CloseHandle(pi.hProcess);
    }
    return true;
}

}  // namespace

void EnsureBackgroundDaemonStarted(void* h_module) {
    bool expected = false;
    if (!g_started.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        return;
    }

    DaemonStatus status;
    status.attempted = true;
    status.target_pid = GetCurrentProcessId();

    HMODULE module = static_cast<HMODULE>(h_module);
    if (module == nullptr) {
        module = reinterpret_cast<HMODULE>(&EnsureBackgroundDaemonStarted);
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(module), &module);
    }

    status.source_dll_path = GetModuleFullPath(module);
    const std::wstring exe_path = QueryCurrentExeFullPath();
    if (status.source_dll_path.empty() || exe_path.empty()) {
        status.last_error = GetLastError();
        if (status.last_error == ERROR_SUCCESS) {
            status.last_error = ERROR_PATH_NOT_FOUND;
        }
        status.message = "Could not resolve current DLL or exe path";
        LogWarn("[Daemon] %s, error=%lu", status.message.c_str(), status.last_error);
        StoreStatus(status);
        return;
    }

    const std::filesystem::path dc_root = GetDisplayCommanderAppDataFolder();
    if (dc_root.empty()) {
        status.last_error = ERROR_PATH_NOT_FOUND;
        status.message = "Could not resolve Display Commander AppData folder";
        LogWarn("[Daemon] %s", status.message.c_str());
        StoreStatus(status);
        return;
    }

    const std::filesystem::path folder = dc_root / kDaemonFolderName / MakeDaemonFolderName(exe_path);
    std::error_code ec;
    std::filesystem::create_directories(folder, ec);
    if (ec) {
        status.last_error = static_cast<std::uint32_t>(ec.value());
        status.message = "Could not create daemon folder";
        LogWarn("[Daemon] %s, error=%lu [Path] %s", status.message.c_str(), status.last_error,
                display_commander::utils::WideToUtf8(folder.wstring()).c_str());
        StoreStatus(status);
        return;
    }

    status.folder = folder.wstring();
    status.dll_path = (folder / kDestDllName).wstring();

    DWORD copy_error = 0;
    bool copied = false;
    if (!CopyDllToDemonFolder(status.source_dll_path, status.dll_path, copy_error, copied)) {
        status.last_error = copy_error;
        status.message = "Failed to copy DLL into daemon folder";
        LogWarn("[Daemon] %s, error=%lu [Path] %s", status.message.c_str(), status.last_error,
                display_commander::utils::WideToUtf8(status.dll_path).c_str());
        StoreStatus(status);
        return;
    }
    status.copy_ok = copied;
    if (!copied) {
        LogInfo("[Daemon] DLL copy skipped (file in use), using existing copy [Path] %s",
                display_commander::utils::WideToUtf8(status.dll_path).c_str());
    }

    DWORD daemon_pid = 0;
    DWORD start_error = 0;
    if (!StartRundll32Daemon(status.dll_path, status.target_pid, daemon_pid, start_error)) {
        status.last_error = start_error;
        status.message = "Failed to start rundll32 daemon";
        LogWarn("[Daemon] %s, error=%lu pid=%lu [Path] %s", status.message.c_str(), status.last_error,
                status.target_pid, display_commander::utils::WideToUtf8(status.dll_path).c_str());
        StoreStatus(status);
        return;
    }

    status.process_started = true;
    status.daemon_pid = daemon_pid;
    status.message = "Daemon started";
    LogInfo("[Daemon] started rundll32 pid=%lu watching pid=%lu [Path] %s", status.daemon_pid, status.target_pid,
            display_commander::utils::WideToUtf8(status.dll_path).c_str());
    StoreStatus(status);
}

DaemonStatus GetDaemonStatus() {
    ::utils::SRWLockShared lock(g_status_lock);
    return g_status;
}

bool IsDaemonProcessAlive() {
    const DaemonStatus status = GetDaemonStatus();
    return IsPidAlive(status.daemon_pid);
}

std::string ReadDaemonLogUtf8() {
    const DaemonStatus status = GetDaemonStatus();
    if (status.folder.empty()) {
        return {};
    }
    const std::wstring log_path = (std::filesystem::path(status.folder) / kLogFileName).wstring();
    HANDLE file = CreateFileW(log_path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return {};
    }
    LARGE_INTEGER size = {};
    if (GetFileSizeEx(file, &size) == 0 || size.QuadPart <= 0) {
        CloseHandle(file);
        return {};
    }
    constexpr DWORD kMaxBytes = 16384;
    const DWORD to_read = size.QuadPart > kMaxBytes ? kMaxBytes : static_cast<DWORD>(size.QuadPart);
    std::string out(to_read, '\0');
    DWORD read = 0;
    const BOOL ok = ReadFile(file, out.data(), to_read, &read, nullptr);
    CloseHandle(file);
    if (ok == 0) {
        return {};
    }
    out.resize(read);
    return out;
}

static void RunDaemonWatch(HINSTANCE hinst, LPSTR lpszCmdLine) {
    (void)hinst;
    const LPSTR full_cmd = GetCommandLineA();
    LogInfo("[Daemon] rundll32 entry GetCommandLine: %s", full_cmd != nullptr ? full_cmd : "(null)");
    LogInfo("[Daemon] rundll32 lpszCmdLine: %s", lpszCmdLine != nullptr ? lpszCmdLine : "(null)");

    std::wstring log_path = ResolveDaemonTxtPath();
    if (log_path.empty()) {
        LogWarn("[Daemon] could not resolve daemon.txt path from this DLL");
    }

    const DWORD self_pid = GetCurrentProcessId();
    char line[256];
    snprintf(line, sizeof(line), "pid=%lu", self_pid);
    if (!log_path.empty()) {
        WriteLogReset(log_path, line);
    }

    DWORD target_pid = 0;
    if (lpszCmdLine != nullptr && lpszCmdLine[0] != '\0') {
        target_pid = static_cast<DWORD>(strtoul(lpszCmdLine, nullptr, 10));
    }
    snprintf(line, sizeof(line), "target_pid=%lu", target_pid);
    if (!log_path.empty()) {
        AppendLogLine(log_path, line);
    } else {
        LogInfo("[Daemon] %s", line);
    }

    if (target_pid == 0) {
        if (!log_path.empty()) {
            AppendLogLine(log_path, "invalid target pid, exiting");
        }
        return;
    }

    HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, target_pid);
    if (process == nullptr) {
        snprintf(line, sizeof(line), "pid %lu terminated", target_pid);
        if (!log_path.empty()) {
            AppendLogLine(log_path, line);
        }
        return;
    }

    for (;;) {
        const DWORD wait = WaitForSingleObject(process, 1000);
        if (wait == WAIT_OBJECT_0) {
            snprintf(line, sizeof(line), "pid %lu terminated", target_pid);
            if (!log_path.empty()) {
                AppendLogLine(log_path, line);
            }
            break;
        }
        if (wait != WAIT_TIMEOUT) {
            snprintf(line, sizeof(line), "wait failed error=%lu", GetLastError());
            if (!log_path.empty()) {
                AppendLogLine(log_path, line);
            }
            break;
        }
    }
    CloseHandle(process);
}

}  // namespace display_commander::mit::deamon

extern "C" __declspec(dllexport) void CALLBACK Daemon(HWND hwnd, HINSTANCE hinst, LPSTR lpszCmdLine, int nCmdShow) {
    (void)hwnd;
    (void)nCmdShow;
    display_commander::mit::deamon::RunDaemonWatch(hinst, lpszCmdLine);
}

extern "C" __declspec(dllexport) void CALLBACK DaemonW(HWND hwnd, HINSTANCE hinst, LPWSTR lpszCmdLine, int nCmdShow) {
    (void)hwnd;
    (void)nCmdShow;
    char cmd_a[256] = {};
    if (lpszCmdLine != nullptr && lpszCmdLine[0] != L'\0') {
        WideCharToMultiByte(CP_ACP, 0, lpszCmdLine, -1, cmd_a, static_cast<int>(sizeof(cmd_a)), nullptr, nullptr);
    }
    display_commander::mit::deamon::RunDaemonWatch(hinst, cmd_a);
}
