#pragma once

// Source Code <Display Commander>

#include <string>
#include <string_view>

#include <Windows.h>

// Returns true if the given module name or path is a Windows Known DLL (filename part, case-insensitive).
bool IsKnownDllName(std::wstring_view module_name_or_path);

// Returns comma-separated list of statically imported DLL names for the given module base, or empty string on failure.
// Names that are in the Windows Known DLLs list (always loaded from system) are suffixed with '*'.
std::string GetStaticImportDllNamesSingleLine(HMODULE base);
