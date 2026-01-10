# Version.dll Proxy Implementation Plan

## Overview
This document outlines the steps required to add `version.dll` proxy support to Display Commander, similar to the existing `dxgi.dll`, `d3d11.dll`, and `d3d12.dll` proxy implementations.

## Background
The Ultimate ASI Loader (already present in `external-src/Ultimate-ASI-Loader`) supports `version.dll` as a proxy DLL. Display Commander should support this as well to allow loading Display Commander as `version.dll` in game directories.

## Current State
- ✅ Entry point detection in `main_entry.cpp` already includes `version.dll` detection (added in user's changes)
- ✅ The `DetectEntryPoint()` function already logs `version.dll` as a detected entry point
- ❌ No proxy implementation exists for `version.dll` functions yet - **THIS IS THE MAIN TASK**

## Required Changes

### 1. Add version.dll Exports to exports.def

**File**: `src/addons/display_commander/proxy_dll/exports.def`

Add the following exports section (based on Ultimate ASI Loader's x64.def):

```
	; version.dll exports
	GetFileVersionInfoA PRIVATE
	GetFileVersionInfoByHandle PRIVATE
	GetFileVersionInfoExA PRIVATE
	GetFileVersionInfoExW PRIVATE
	GetFileVersionInfoSizeA PRIVATE
	GetFileVersionInfoSizeExA PRIVATE
	GetFileVersionInfoSizeExW PRIVATE
	GetFileVersionInfoSizeW PRIVATE
	GetFileVersionInfoW PRIVATE
	VerFindFileA PRIVATE
	VerFindFileW PRIVATE
	VerInstallFileA PRIVATE
	VerInstallFileW PRIVATE
	VerLanguageNameA PRIVATE
	VerLanguageNameW PRIVATE
	VerQueryValueA PRIVATE
	VerQueryValueW PRIVATE
```

### 2. Create version_proxy.cpp

**File**: `src/addons/display_commander/proxy_dll/version_proxy.cpp`

Create a new file following the pattern of `dxgi_proxy.cpp`, `d3d11_proxy.cpp`, and `d3d12_proxy.cpp`.

**Required Functions** (17 total):
1. `GetFileVersionInfoA` - Retrieves version information for a file (ANSI)
2. `GetFileVersionInfoByHandle` - Retrieves version information using a file handle
3. `GetFileVersionInfoExA` - Extended version info retrieval (ANSI)
4. `GetFileVersionInfoExW` - Extended version info retrieval (Unicode)
5. `GetFileVersionInfoSizeA` - Gets size of version info buffer (ANSI)
6. `GetFileVersionInfoSizeExA` - Extended size retrieval (ANSI)
7. `GetFileVersionInfoSizeExW` - Extended size retrieval (Unicode)
8. `GetFileVersionInfoSizeW` - Gets size of version info buffer (Unicode)
9. `GetFileVersionInfoW` - Retrieves version information for a file (Unicode)
10. `VerFindFileA` - Finds a file in the specified path (ANSI)
11. `VerFindFileW` - Finds a file in the specified path (Unicode)
12. `VerInstallFileA` - Installs a file using version information (ANSI)
13. `VerInstallFileW` - Installs a file using version information (Unicode)
14. `VerLanguageNameA` - Converts a language identifier to a language name (ANSI)
15. `VerLanguageNameW` - Converts a language identifier to a language name (Unicode)
16. `VerQueryValueA` - Retrieves version information from a version resource (ANSI)
17. `VerQueryValueW` - Retrieves version information from a version resource (Unicode)

**Implementation Pattern**:
- Load real `version.dll` from system directory (`%SystemRoot%\System32\version.dll`)
- Forward all function calls to the real DLL
- Use function pointer types matching Windows API signatures
- Handle both ANSI (A) and Unicode (W) variants

### 3. Update proxy_detection.hpp

**File**: `src/addons/display_commander/proxy_dll/proxy_detection.hpp`

Update `IsProxyDllMode()` to include `version.dll`:
```cpp
return module_name == L"dxgi.dll" || module_name == L"d3d11.dll" || module_name == L"d3d12.dll" || module_name == L"version.dll";
```

### 4. Update CMakeLists.txt

**File**: `src/addons/display_commander/CMakeLists.txt`

Add `version_proxy.cpp` to the source files list for `zzz_display_commander` target.

### 5. Verify Entry Point Detection

**File**: `src/addons/display_commander/main_entry.cpp`

✅ **Already Complete**: The entry point detection already includes `version.dll`:
- The detection code checks for `version.dll` and sets `entry_point = L"version.dll"`
- Logs appropriate messages when loaded as `version.dll` proxy
- Saves entry point to `DisplayCommander.ini`

**No changes needed** - the detection is already implemented in the user's recent changes.

## Windows API Reference

### Key Functions

**GetFileVersionInfoSizeW**:
```cpp
DWORD WINAPI GetFileVersionInfoSizeW(
  LPCWSTR lptstrFilename,
  LPDWORD lpdwHandle
);
```

**GetFileVersionInfoW**:
```cpp
BOOL WINAPI GetFileVersionInfoW(
  LPCWSTR lptstrFilename,
  DWORD dwHandle,
  DWORD dwLen,
  LPVOID lpData
);
```

**VerQueryValueW**:
```cpp
BOOL WINAPI VerQueryValueW(
  LPCVOID pBlock,
  LPCWSTR lpSubBlock,
  LPVOID *lplpBuffer,
  PUINT puLen
);
```

## Implementation Notes

1. **System DLL Location**: The real `version.dll` is located at `%SystemRoot%\System32\version.dll` (typically `C:\Windows\System32\version.dll`)

2. **Function Pointer Types**: Define proper function pointer types for all 17 functions, following the pattern used in `dxgi_proxy.cpp`

3. **Error Handling**: Return appropriate error codes if the real DLL cannot be loaded or if function pointers cannot be obtained

4. **Thread Safety**: The current proxy implementations use static module handles, which should be safe for this use case

5. **Unicode vs ANSI**: Ensure both ANSI (A) and Unicode (W) variants are implemented

## Testing

After implementation, test by:
1. Renaming `zzz_display_commander.addon64` to `version.dll`
2. Placing it in a game directory
3. Verifying that:
   - Display Commander loads correctly
   - Entry point is detected as `version.dll` in `DisplayCommander.ini`
   - Version API calls are forwarded correctly to the real `version.dll`
   - Games that use `version.dll` continue to work normally

## Files to Modify

1. ✅ `src/addons/display_commander/proxy_dll/exports.def` - Add version.dll exports
2. ✅ `src/addons/display_commander/proxy_dll/version_proxy.cpp` - Create new file
3. ✅ `src/addons/display_commander/proxy_dll/proxy_detection.hpp` - Update detection
4. ✅ `src/addons/display_commander/CMakeLists.txt` - Add source file
5. ✅ `src/addons/display_commander/main_entry.cpp` - Verify detection (already done)

## References

- Ultimate ASI Loader source: `external-src/Ultimate-ASI-Loader/source/x64.def` (lines 341-359)
- Ultimate ASI Loader implementation: `external-src/Ultimate-ASI-Loader/source/dllmain.h` (lines 772-815)
- Windows API Documentation: https://learn.microsoft.com/en-us/windows/win32/api/winver/

## Implementation Priority

**High Priority Functions** (most commonly used):
- `GetFileVersionInfoSizeW` / `GetFileVersionInfoSizeA`
- `GetFileVersionInfoW` / `GetFileVersionInfoA`
- `VerQueryValueW` / `VerQueryValueA`

**Medium Priority Functions**:
- `GetFileVersionInfoByHandle`
- `GetFileVersionInfoExW` / `GetFileVersionInfoExA`
- `GetFileVersionInfoSizeExW` / `GetFileVersionInfoSizeExA`

**Low Priority Functions** (rarely used):
- `VerFindFileW` / `VerFindFileA`
- `VerInstallFileW` / `VerInstallFileA`
- `VerLanguageNameW` / `VerLanguageNameA`

**Note**: All functions should be implemented for completeness, even if some are rarely used.
