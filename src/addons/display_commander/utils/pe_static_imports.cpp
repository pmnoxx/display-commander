// Source Code <Display Commander>
#include "pe_static_imports.hpp"

// Libraries <standard C++>
#include <algorithm>
#include <cctype>
#include <set>
#include <string>

// Libraries <Windows.h>
#include <Windows.h>

#ifndef IMAGE_DIRECTORY_ENTRY_IMPORT
#define IMAGE_DIRECTORY_ENTRY_IMPORT 1
#endif

namespace {

// Windows Known DLLs (HKLM\...\KnownDLLs): loader always uses system copy, never app directory.
// Asterisk (*) is appended next to these in the static-imports log.
const std::set<std::string>& GetKnownDllNamesLowercase() {
    static const std::set<std::string> kKnownDlls = {
        // manually added dlls
        "ntdll.dll",
        "kernelbase.dll",
        // known dlls
        "advapi32.dll",
        "clbcatq.dll",
        "combase.dll",
        "comdlg32.dll",
        "coml2.dll",
        "difxapi.dll",
        "gdi32.dll",
        "gdiplus.dll",
        "imagehlp.dll",
        "imm32.dll",
        "kernel32.dll",
        "msctf.dll",
        "mswsock.dll",
        "msvcrt.dll",
        "normaliz.dll",
        "nsi.dll",
        "ole32.dll",
        "oleaut32.dll",
        "psapi.dll",
        "rpcrt4.dll",
        "sechost.dll",
        "setupapi.dll",
        "shcore.dll",
        "shell32.dll",
        "shlwapi.dll",
        "user32.dll",
        "wldap32.dll",
        "wow64.dll",
        "wow64base.dll",
        "wow64con.dll",
        "wow64cpu.dll",
        "wow64win.dll",
        "wowarmhw.dll",
        "ws2_32.dll",
        "xtajit.dll",
        "xtajit64.dll",
        "xtajit64se.dll",
        "xtajitf.dll",
        "xtajitse.dll",
    };
    return kKnownDlls;
}

std::string GetStaticImportDllNamesSingleLineImpl(HMODULE base) {
    if (base == nullptr) return {};
    const auto* const dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return {};
    const auto* const nt =
        reinterpret_cast<const IMAGE_NT_HEADERS*>(reinterpret_cast<const BYTE*>(base) + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return {};

    // Optional header layout depends on PE machine type, not on build architecture.
    const void* const optional_header = &nt->OptionalHeader;
    const bool is_64bit_pe = (nt->FileHeader.Machine == IMAGE_FILE_MACHINE_AMD64);
    DWORD import_rva = 0;
    DWORD size_of_image = 0;
    if (is_64bit_pe) {
        const auto* const oh = static_cast<const IMAGE_OPTIONAL_HEADER64*>(optional_header);
        import_rva = oh->DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
        size_of_image = oh->SizeOfImage;
    } else {
        const auto* const oh = static_cast<const IMAGE_OPTIONAL_HEADER32*>(optional_header);
        import_rva = oh->DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
        size_of_image = oh->SizeOfImage;
    }
    if (import_rva == 0 || size_of_image == 0) return {};
    if (import_rva >= size_of_image) return {};

    const auto* desc =
        reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(reinterpret_cast<const BYTE*>(base) + import_rva);
    constexpr DWORD k_max_imports = 1024;
    const std::set<std::string>& known_dlls = GetKnownDllNamesLowercase();
    std::string line;
    for (DWORD i = 0; i < k_max_imports && desc->Name != 0; ++i, ++desc) {
        DWORD const name_rva = desc->Name;
        if (name_rva >= size_of_image) continue;
        const char* const name = reinterpret_cast<const char*>(reinterpret_cast<const BYTE*>(base) + name_rva);
        size_t len = 0;
        while (len < 260 && name[len] != '\0') ++len;
        if (len == 0) continue;
        if (!line.empty()) line += ", ";
        line.append(name, len);
        std::string name_lower(name, len);
        std::transform(name_lower.begin(), name_lower.end(), name_lower.begin(),
                       [](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); });
        if (known_dlls.contains(name_lower)) line += "*";
    }
    return line;
}

}  // namespace

bool IsKnownDllName(std::wstring_view module_name_or_path) {
    const std::set<std::string>& known = GetKnownDllNamesLowercase();
    const size_t sep = module_name_or_path.find_last_of(L"\\/");
    std::wstring_view filename =
        (sep != std::wstring_view::npos) ? module_name_or_path.substr(sep + 1) : module_name_or_path;
    std::string name_lower;
    name_lower.reserve(filename.size());
    for (wchar_t w : filename) {
        if (w > 127) return false;
        name_lower += static_cast<char>(std::tolower(static_cast<unsigned char>(static_cast<char>(w))));
    }
    return known.contains(name_lower);
}

std::string GetStaticImportDllNamesSingleLine(HMODULE base) { return GetStaticImportDllNamesSingleLineImpl(base); }
