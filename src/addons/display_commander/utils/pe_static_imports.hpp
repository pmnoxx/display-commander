#pragma once

// Source Code <Display Commander>

#include <string>

#include <Windows.h>

// Returns comma-separated list of statically imported DLL names for the given module base, or empty string on failure.
std::string GetStaticImportDllNamesSingleLine(HMODULE base);
