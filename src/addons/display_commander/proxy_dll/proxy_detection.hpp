#pragma once

#include <Windows.h>
#include <algorithm>
#include <string>

// Detect if Display Commander is being loaded as a proxy DLL (dxgi.dll, d3d11.dll, d3d12.dll, or version.dll)
// This checks the name of the Display Commander DLL itself, not the executable
inline bool IsProxyDllMode(HMODULE h_module = nullptr)
{
	if (h_module == nullptr) {
		// Get the module handle for the current DLL
		MEMORY_BASIC_INFORMATION mbi;
		if (VirtualQuery(IsProxyDllMode, &mbi, sizeof(mbi)) == 0) {
			return false;
		}
		h_module = static_cast<HMODULE>(mbi.AllocationBase);
	}

	WCHAR module_path[MAX_PATH];
	if (GetModuleFileNameW(h_module, module_path, MAX_PATH) == 0) {
		return false;
	}

	std::wstring module_name = module_path;
	size_t last_slash = module_name.find_last_of(L"\\/");
	if (last_slash != std::wstring::npos) {
		module_name = module_name.substr(last_slash + 1);
	}

	// Convert to lowercase for comparison
	std::transform(module_name.begin(), module_name.end(), module_name.begin(), ::towlower);

	// Check if we're being loaded as dxgi.dll, d3d11.dll, d3d12.dll, or version.dll
	return module_name == L"dxgi.dll" || module_name == L"d3d11.dll" || module_name == L"d3d12.dll" || module_name == L"version.dll";
}

// Get the module name (stem) to determine which proxy DLL we are
inline std::wstring GetProxyDllName(HMODULE h_module = nullptr)
{
	if (h_module == nullptr) {
		// Get the module handle for the current DLL
		MEMORY_BASIC_INFORMATION mbi;
		if (VirtualQuery(GetProxyDllName, &mbi, sizeof(mbi)) == 0) {
			return L"";
		}
		h_module = static_cast<HMODULE>(mbi.AllocationBase);
	}

	WCHAR module_path[MAX_PATH];
	if (GetModuleFileNameW(h_module, module_path, MAX_PATH) == 0) {
		return L"";
	}

	std::wstring module_name = module_path;
	size_t last_slash = module_name.find_last_of(L"\\/");
	if (last_slash != std::wstring::npos) {
		module_name = module_name.substr(last_slash + 1);
	}

	// Convert to lowercase for comparison
	std::transform(module_name.begin(), module_name.end(), module_name.begin(), ::towlower);

	return module_name;
}

