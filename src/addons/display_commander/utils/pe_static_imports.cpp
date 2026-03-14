// Source Code <Display Commander>
#include "pe_static_imports.hpp"

// Libraries <standard C++>
#include <string>

// Libraries <Windows.h>
#include <Windows.h>

#ifndef IMAGE_DIRECTORY_ENTRY_IMPORT
#define IMAGE_DIRECTORY_ENTRY_IMPORT 1
#endif

namespace {

std::string GetStaticImportDllNamesSingleLineImpl(HMODULE base) {
    if (base == nullptr) return {};
    const auto* const dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return {};
    const auto* const nt =
        reinterpret_cast<const IMAGE_NT_HEADERS*>(reinterpret_cast<const BYTE*>(base) + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return {};

    DWORD import_rva = 0;
    DWORD size_of_image = 0;
    if (nt->FileHeader.Machine == IMAGE_FILE_MACHINE_AMD64) {
        const auto* const oh = &nt->OptionalHeader;
        import_rva = oh->DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
        size_of_image = oh->SizeOfImage;
    } else {
        const auto* const oh = reinterpret_cast<const IMAGE_OPTIONAL_HEADER32*>(&nt->OptionalHeader);
        import_rva = oh->DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
        size_of_image = oh->SizeOfImage;
    }
    if (import_rva == 0 || size_of_image == 0) return {};

    const auto* desc =
        reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(reinterpret_cast<const BYTE*>(base) + import_rva);
    constexpr DWORD kMaxImports = 1024;
    std::string line;
    for (DWORD i = 0; i < kMaxImports && desc->Name != 0; ++i, ++desc) {
        DWORD const name_rva = desc->Name;
        if (name_rva >= size_of_image) continue;
        const char* const name = reinterpret_cast<const char*>(reinterpret_cast<const BYTE*>(base) + name_rva);
        size_t len = 0;
        while (len < 260 && name[len]) ++len;
        if (len == 0) continue;
        if (!line.empty()) line += ", ";
        line.append(name, len);
    }
    return line;
}

}  // namespace

std::string GetStaticImportDllNamesSingleLine(HMODULE base) {
    return GetStaticImportDllNamesSingleLineImpl(base);
}
