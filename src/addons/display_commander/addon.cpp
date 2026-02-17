#include "addon.hpp"
#include <windows.h>
#include <atomic>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <memory>
#include <reshade.hpp>
#include <string>
#include <vector>
#include "globals.hpp"
#include "ui/cli_detect_exe.hpp"
#include "ui/cli_standalone_ui.hpp"
#include "utils/detour_call_tracker.hpp"
#include "utils/logging.hpp"
#include "utils/timing.hpp"
#include "version.hpp"

// PE parsing for DetectExe: bitness and import DLL names (ReShade API detection)
namespace cli_detect_exe {

#ifndef IMAGE_DIRECTORY_ENTRY_IMPORT
#define IMAGE_DIRECTORY_ENTRY_IMPORT 1
#endif
#ifndef IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT
#define IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT 13
#endif

static DWORD get_import_dir_rva(const IMAGE_FILE_HEADER* fh, const void* optional_header) {
    if (fh->Machine == IMAGE_FILE_MACHINE_AMD64) {
        const auto* oh = static_cast<const IMAGE_OPTIONAL_HEADER64*>(optional_header);
        return oh->DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
    } else {
        const auto* oh = static_cast<const IMAGE_OPTIONAL_HEADER32*>(optional_header);
        return oh->DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
    }
}

static DWORD get_delay_import_dir_rva(const IMAGE_FILE_HEADER* fh, const void* optional_header) {
    if (fh->Machine == IMAGE_FILE_MACHINE_AMD64) {
        const auto* oh = static_cast<const IMAGE_OPTIONAL_HEADER64*>(optional_header);
        return oh->DataDirectory[IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT].VirtualAddress;
    } else {
        const auto* oh = static_cast<const IMAGE_OPTIONAL_HEADER32*>(optional_header);
        return oh->DataDirectory[IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT].VirtualAddress;
    }
}

// Delay-load descriptor (same layout as IMAGE_DELAYLOAD_DESCRIPTOR / ImgDelayDescr in delayimp.h)
struct DelayLoadDescr {
    DWORD Attributes;
    DWORD DllNameRVA;
    DWORD ModuleHandleRVA;
    DWORD ImportAddressTableRVA;
    DWORD ImportNameTableRVA;
    DWORD BoundImportAddressTableRVA;
    DWORD UnloadInformationTableRVA;
    DWORD TimeDateStamp;
};

// Convert RVA to file offset using section table. Returns 0 on failure.
static DWORD rva_to_file_offset(DWORD rva, const IMAGE_SECTION_HEADER* sections, WORD num_sections) {
    for (WORD i = 0; i < num_sections; ++i) {
        DWORD va = sections[i].VirtualAddress;
        DWORD size = sections[i].Misc.VirtualSize;
        if (size == 0) size = sections[i].SizeOfRawData;
        if (rva >= va && rva < va + size) return rva - va + sections[i].PointerToRawData;
    }
    return 0;
}

static void check_dll_name_and_set_flags(const char* name, size_t name_len, cli_detect_exe::DetectResult& out) {
    char lower[64];
    size_t i = 0;
    for (; i < sizeof(lower) - 1 && i < name_len && name[i]; ++i)
        lower[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(name[i])));
    lower[i] = '\0';
    if (strcmp(lower, "d3d9.dll") == 0)
        out.has_d3d9 = true;
    else if (strcmp(lower, "d3d11.dll") == 0)
        out.has_d3d11 = true;
    else if (strcmp(lower, "d3d12.dll") == 0)
        out.has_d3d12 = true;
    else if (strcmp(lower, "dxgi.dll") == 0)
        out.has_dxgi = true;
    else if (strcmp(lower, "opengl32.dll") == 0)
        out.has_opengl32 = true;
    else if (strstr(lower, "vulkan") != nullptr)
        out.has_vulkan = true;
}

static const char* reshade_dll_from_detect_impl(const cli_detect_exe::DetectResult& r) {
    if (r.has_d3d12) return "d3d12";
    if (r.has_d3d11 || r.has_dxgi) return "dxgi";
    if (r.has_d3d9) return "d3d9";
    if (r.has_opengl32) return "opengl32";
    if (r.has_vulkan) return "vulkan";
    if (r.is_64bit) return "dxgi";  // fallback: most modern 64-bit games use DX11/DX12
    return "unknown";
}

static bool read_imports(const std::vector<char>& buf, DWORD import_dir_rva, const IMAGE_SECTION_HEADER* sections,
                         WORD num_sections, cli_detect_exe::DetectResult& out) {
    DWORD id_offset = rva_to_file_offset(import_dir_rva, sections, num_sections);
    if (id_offset == 0 || id_offset + sizeof(IMAGE_IMPORT_DESCRIPTOR) > buf.size()) return false;

    const auto* desc = reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(buf.data() + id_offset);
    constexpr DWORD max_imports = 51200000;
    DWORD count = 0;
    while (desc->Name != 0 && count < max_imports) {
        ++count;
        DWORD name_offset = rva_to_file_offset(desc->Name, sections, num_sections);
        if (name_offset != 0 && name_offset < buf.size()) {
            const char* name = buf.data() + name_offset;
            size_t len = 0;
            while (len < 260 && name[len]) ++len;
            check_dll_name_and_set_flags(name, len, out);
        }
        ++desc;
        if (id_offset + (DWORD)((const char*)desc - (buf.data() + id_offset)) > buf.size()) break;
    }
    return true;
}

static bool read_delay_imports(const std::vector<char>& buf, DWORD delay_dir_rva, const IMAGE_SECTION_HEADER* sections,
                               WORD num_sections, cli_detect_exe::DetectResult& out) {
    DWORD doff = rva_to_file_offset(delay_dir_rva, sections, num_sections);
    if (doff == 0 || doff + sizeof(DelayLoadDescr) > buf.size()) return false;

    const auto* desc = reinterpret_cast<const DelayLoadDescr*>(buf.data() + doff);
    constexpr DWORD max_delay = 128;
    DWORD count = 0;
    while (desc->DllNameRVA != 0 && count < max_delay) {
        ++count;
        DWORD name_offset = rva_to_file_offset(desc->DllNameRVA, sections, num_sections);
        if (name_offset != 0 && name_offset < buf.size()) {
            const char* name = buf.data() + name_offset;
            size_t len = 0;
            while (len < 260 && name[len]) ++len;
            check_dll_name_and_set_flags(name, len, out);
        }
        ++desc;
        if (doff + (DWORD)((const char*)desc - (buf.data() + doff)) > buf.size()) break;
    }
    return true;
}

// Return true if the exe filename looks like a helper/crash handler, not the main game.
static bool is_helper_or_crash_handler_exe(const wchar_t* filename) {
    if (!filename || !filename[0]) return true;
    wchar_t lower[512];
    size_t i = 0;
    for (; i < 511 && filename[i]; ++i)
        lower[i] = (wchar_t)(filename[i] >= L'A' && filename[i] <= L'Z' ? filename[i] - L'A' + L'a' : filename[i]);
    lower[i] = L'\0';
    const wchar_t* needles[] = {
        L"unitycrashhandler", L"crashhandler", L"unityhelper",      L"unrealcefsubprocess",
        L"reportcrash",       L"bugtrap",      L"exceptionhandler", L"launcher",
    };
    for (const wchar_t* n : needles) {
        if (wcsstr(lower, n) != nullptr) return true;
    }
    return false;
}

// Find largest .exe in directory (by file size). Skips helper/crash-handler exes. Returns full path or empty on
// failure.
static std::string find_largest_exe_in_dir(const wchar_t* dir_wide) {
    std::wstring pattern(dir_wide);
    if (!pattern.empty() && pattern.back() != L'\\') pattern += L'\\';
    pattern += L"*.exe";

    WIN32_FIND_DATAW fd = {};
    HANDLE hFind = FindFirstFileW(pattern.c_str(), &fd);
    if (hFind == INVALID_HANDLE_VALUE) return {};

    std::wstring best_name;
    ULONGLONG best_size = 0;
    do {
        if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
        if (is_helper_or_crash_handler_exe(fd.cFileName)) continue;
        ULONGLONG size = (ULONGLONG)fd.nFileSizeHigh << 32 | fd.nFileSizeLow;
        if (size > best_size) {
            best_size = size;
            best_name = fd.cFileName;
        }
    } while (FindNextFileW(hFind, &fd));
    FindClose(hFind);

    if (best_name.empty()) return {};
    std::wstring full(dir_wide);
    if (!full.empty() && full.back() != L'\\') full += L'\\';
    full += best_name;

    int need = WideCharToMultiByte(CP_UTF8, 0, full.c_str(), (int)full.size(), nullptr, 0, nullptr, nullptr);
    if (need <= 0) return {};
    std::string result(need, 0);
    WideCharToMultiByte(CP_UTF8, 0, full.c_str(), (int)full.size(), &result[0], need, nullptr, nullptr);
    return result;
}

// Run detection on exe_path (UTF-8). Fills result and returns true on success.
static bool detect_exe_impl(const char* exe_path, cli_detect_exe::DetectResult& result) {
    result = {};
    result.exe_path = exe_path;

    int wlen = MultiByteToWideChar(CP_UTF8, 0, exe_path, -1, nullptr, 0);
    if (wlen <= 0) return false;
    std::vector<wchar_t> wpath(wlen);
    MultiByteToWideChar(CP_UTF8, 0, exe_path, -1, wpath.data(), wlen);

    HANDLE hFile = CreateFileW(wpath.data(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) return false;

    DWORD size_hi = 0, read_len = 0;
    DWORD size_lo = GetFileSize(hFile, &size_hi);
    if (size_lo == INVALID_FILE_SIZE && GetLastError() != NO_ERROR) {
        CloseHandle(hFile);
        return false;
    }
    size_t file_size = (size_t)size_lo | ((size_t)size_hi << 32);
    if (file_size < sizeof(IMAGE_DOS_HEADER) + sizeof(DWORD) + sizeof(IMAGE_FILE_HEADER)) {
        CloseHandle(hFile);
        return false;
    }

    std::vector<char> buf(file_size);
    if (!ReadFile(hFile, buf.data(), (DWORD)file_size, &read_len, nullptr) || read_len != file_size) {
        CloseHandle(hFile);
        return false;
    }
    CloseHandle(hFile);

    const IMAGE_DOS_HEADER* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(buf.data());
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
    size_t pe_off = dos->e_lfanew;
    if (pe_off + sizeof(DWORD) + sizeof(IMAGE_FILE_HEADER) > file_size) return false;

    const DWORD* sig = reinterpret_cast<const DWORD*>(buf.data() + pe_off);
    if (*sig != IMAGE_NT_SIGNATURE) return false;
    const IMAGE_FILE_HEADER* fh = reinterpret_cast<const IMAGE_FILE_HEADER*>(sig + 1);
    result.is_64bit = (fh->Machine == IMAGE_FILE_MACHINE_AMD64);

    size_t opt_header_offset = pe_off + sizeof(DWORD) + sizeof(IMAGE_FILE_HEADER);
    WORD opt_size = fh->SizeOfOptionalHeader;
    if (opt_header_offset + opt_size > file_size) return false;

    const void* opt = buf.data() + opt_header_offset;
    DWORD import_rva = get_import_dir_rva(fh, opt);
    DWORD delay_rva = get_delay_import_dir_rva(fh, opt);

    size_t sections_offset = opt_header_offset + opt_size;
    WORD num_sections = fh->NumberOfSections;
    if (sections_offset + num_sections * sizeof(IMAGE_SECTION_HEADER) > file_size) return true;

    const IMAGE_SECTION_HEADER* sections = reinterpret_cast<const IMAGE_SECTION_HEADER*>(buf.data() + sections_offset);
    if (import_rva != 0) read_imports(buf, import_rva, sections, num_sections, result);
    if (delay_rva != 0) read_delay_imports(buf, delay_rva, sections, num_sections, result);
    return true;
}

bool DetectExeForPath(const wchar_t* exe_path_wide, DetectResult* out) {
    if (!out || !exe_path_wide || !exe_path_wide[0]) return false;
    int len = WideCharToMultiByte(CP_UTF8, 0, exe_path_wide, -1, nullptr, 0, nullptr, nullptr);
    if (len <= 0) return false;
    std::string exe_utf8(static_cast<size_t>(len), 0);
    WideCharToMultiByte(CP_UTF8, 0, exe_path_wide, -1, &exe_utf8[0], len, nullptr, nullptr);
    return detect_exe_impl(exe_utf8.c_str(), *out);
}

const char* ReShadeDllFromDetect(const DetectResult& r) { return reshade_dll_from_detect_impl(r); }
}  // namespace cli_detect_exe

// Forward declaration
void OnRegisterOverlayDisplayCommander(reshade::api::effect_runtime* runtime);

// Export addon information
extern "C" __declspec(dllexport) constexpr const char* NAME = "Display Commander";
extern "C" __declspec(dllexport) constexpr const char* DESCRIPTION =
    "RenoDX Display Commander - Advanced display and performance management.";

// Export version string function
extern "C" __declspec(dllexport) const char* GetDisplayCommanderVersion() { return DISPLAY_COMMANDER_VERSION_STRING; }

// Command-line handler for rundll32.exe invocation (e.g. PowerShell: & rundll32.exe
// zzz_DisplayCommander.addon64,CommandLine args) Output is written to CommandLine.log in the addon DLL directory
// (rundll32 often doesn't show stdout).
static void RunCommandLine(HINSTANCE hinst, LPSTR lpszCmdLine) {
    FILE* log_file = fopen("CommandLine.log", "w");
    auto out = [log_file](const char* msg) {
        if (msg && msg[0] != '\0' && log_file) {
            fputs(msg, log_file);
            fflush(log_file);
        }
    };
    auto out_line = [&out](const char* line) {
        out(line);
        out("\n");
    };

    // Trim leading/trailing whitespace from command line
    if (!lpszCmdLine) {
        out_line("Display Commander CLI - use 'help' for usage.");
        if (log_file) fclose(log_file);
        return;
    }
    const char* p = lpszCmdLine;
    while (*p == ' ' || *p == '\t') ++p;
    const char* start = p;
    while (*p != '\0') ++p;
    while (p > start && (p[-1] == ' ' || p[-1] == '\t')) --p;

    // First token = command (case-insensitive)
    const char* end = start;
    while (*end != '\0' && *end != ' ' && *end != '\t') ++end;

    auto cmd_equals = [start, end](const char* cmd) {
        size_t n = static_cast<size_t>(end - start);
        size_t clen = strlen(cmd);
        if (n != clen) return false;
        for (size_t i = 0; i < n; ++i) {
            if (std::tolower(static_cast<unsigned char>(start[i]))
                != std::tolower(static_cast<unsigned char>(cmd[i]))) {
                return false;
            }
        }
        return true;
    };

    if (end == start || cmd_equals("help") || cmd_equals("?") || cmd_equals("-h") || cmd_equals("--help")) {
        out_line("Display Commander - Command-line interface");
        out_line("Usage: rundll32.exe zzz_DisplayCommander.addon64,CommandLine <command> [args...]");
        out_line("");
        out_line("Commands:");
        out_line("  version    Print addon version (for scripts)");
        out_line("  DetectExe [dir]  Find largest .exe in directory, detect 32/64-bit and graphics API (ReShade DLL)");
        out_line("  SetupDC [script_dir]  Show standalone installer UI; script_dir = folder where installer script runs (default: addon dir)");
        out_line("  help       Show this help");
        out_line("");
        out_line("Output is written to CommandLine.log in this addon's directory.");
        if (log_file) fclose(log_file);
        return;
    }

    if (cmd_equals("version")) {
        out_line(GetDisplayCommanderVersion());
        if (log_file) fclose(log_file);
        return;
    }

    if (cmd_equals("SetupDC")) {
        const char* path_start = end;
        while (*path_start == ' ' || *path_start == '\t') ++path_start;
        const char* path_end = path_start;
        while (*path_end != '\0') ++path_end;
        while (path_end > path_start && (path_end[-1] == ' ' || path_end[-1] == '\t')) --path_end;
        if (path_start < path_end && *path_start == '"') {
            path_start++;
            if (path_end > path_start && path_end[-1] == '"') path_end--;
        }
        const char* script_dir = nullptr;
        std::string script_dir_utf8;
        if (path_start < path_end) {
            script_dir_utf8.assign(path_start, path_end - path_start);
            if (!script_dir_utf8.empty()) script_dir = script_dir_utf8.c_str();
        }
        if (log_file) fclose(log_file);
        RunStandaloneUI(hinst, script_dir);
        return;
    }

    if (cmd_equals("DetectExe")) {
        const char* path_start = end;
        while (*path_start == ' ' || *path_start == '\t') ++path_start;
        const char* path_end = path_start;
        while (*path_end != '\0') ++path_end;
        while (path_end > path_start && (path_end[-1] == ' ' || path_end[-1] == '\t')) --path_end;
        if (path_start < path_end && *path_start == '"') {
            path_start++;
            if (path_end > path_start && path_end[-1] == '"') path_end--;
        }
        std::string dir_utf8(path_start, path_end - path_start);
        if (dir_utf8.empty()) {
            out_line("DetectExe: missing directory path. Usage: DetectExe <directory>");
            if (log_file) fclose(log_file);
            return;
        }
        int wlen = MultiByteToWideChar(CP_UTF8, 0, dir_utf8.c_str(), (int)dir_utf8.size(), nullptr, 0);
        if (wlen <= 0) {
            out_line("DetectExe: invalid path encoding.");
            if (log_file) fclose(log_file);
            return;
        }
        std::vector<wchar_t> dir_wide(wlen + 1);
        MultiByteToWideChar(CP_UTF8, 0, dir_utf8.c_str(), (int)dir_utf8.size(), dir_wide.data(), wlen + 1);
        dir_wide[wlen] = L'\0';

        std::string exe_path = cli_detect_exe::find_largest_exe_in_dir(dir_wide.data());
        if (exe_path.empty()) {
            out_line("DetectExe: no .exe found in directory.");
            if (log_file) fclose(log_file);
            return;
        }
        cli_detect_exe::DetectResult dr;
        if (!cli_detect_exe::detect_exe_impl(exe_path.c_str(), dr)) {
            out_line("DetectExe: failed to read or parse PE.");
            if (log_file) fclose(log_file);
            return;
        }
        char line[1024];
        snprintf(line, sizeof(line), "Exe: %s", dr.exe_path.c_str());
        out_line(line);
        snprintf(line, sizeof(line), "Bitness: %s", dr.is_64bit ? "64-bit" : "32-bit");
        out_line(line);
        const char* api = cli_detect_exe::ReShadeDllFromDetect(dr);
        snprintf(line, sizeof(line), "ReShade DLL: %s", api);
        out_line(line);
        if (log_file) fclose(log_file);
        return;
    }

    out("Unknown command: ");
    for (; start != end; ++start) {
        if (log_file) fputc(*start, log_file);
    }
    out_line(". Use 'help' for usage.");
    if (log_file) fclose(log_file);
}

// Export function to notify other Display Commander instances about multiple versions
extern "C" __declspec(dllexport) void NotifyDisplayCommanderMultipleVersions(const char* caller_version) {
    if (caller_version == nullptr) {
        return;
    }

    // Store the other version in a global atomic variable
    // This will be displayed as a warning in the main tab UI
    // Create a shared string with the caller's version
    auto version_str = std::make_shared<const std::string>(caller_version);
    g_other_dc_version_detected.store(version_str);

    // Log to debug output
    char msg[256];
    snprintf(msg, sizeof(msg), "[DisplayCommander] Notified of multiple versions by another instance: v%s\n",
             caller_version);
    OutputDebugStringA(msg);
}

// Export function to get the DLL load timestamp in nanoseconds
// Used to resolve conflicts when multiple DLLs are loaded at the same time
extern "C" __declspec(dllexport) LONGLONG LoadedNs() { return g_dll_load_time_ns.load(std::memory_order_acquire); }

// Command-line entry point for rundll32.exe (PowerShell: & rundll32.exe zzz_DisplayCommander.addon64,CommandLine args)
extern "C" __declspec(dllexport) void CALLBACK CommandLine(HWND hwnd, HINSTANCE hinst, LPSTR lpszCmdLine,
                                                           int nCmdShow) {
    (void)hwnd;
    (void)nCmdShow;
    RunCommandLine(hinst, lpszCmdLine);
}

// Export addon initialization function
extern "C" __declspec(dllexport) bool AddonInit(HMODULE addon_module, HMODULE reshade_module) {
    RECORD_DETOUR_CALL(utils::get_now_ns());
    // Store ReShade module handle for unload detection
    g_reshade_module = reshade_module;
    LogInfo("AddonInit: Stored ReShade module handle: 0x%p", reshade_module);

    reshade::unregister_addon(addon_module);
    reshade::register_addon(addon_module);
    reshade::unregister_overlay("DC", OnRegisterOverlayDisplayCommander);
    reshade::register_overlay("DC", OnRegisterOverlayDisplayCommander);
    DoInitializationWithoutHwnd(addon_module);

    return true;
}
