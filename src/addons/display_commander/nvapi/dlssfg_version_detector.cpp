#include "dlssfg_version_detector.hpp"
#include "../utils.hpp"
#include <shlwapi.h>
#include <winver.h>

#pragma comment(lib, "version.lib")

// Global instance
DLSSFGVersionDetector g_dlssfgVersionDetector;

DLSSFGVersionDetector::DLSSFGVersionDetector() {}

DLSSFGVersionDetector::~DLSSFGVersionDetector() { Cleanup(); }

bool DLSSFGVersionDetector::Initialize() {
    if (initialized || failed_to_initialize) {
        return initialized;
    }

    LogInfo("Initializing DLSS-FG Version Detector...");

    // Try to detect DLSS-FG version from common DLL locations
    bool version_found = false;

    for (size_t i = 0; i < DLSSG_DLL_COUNT; ++i) {
        const wchar_t *dll_name = DLSSG_DLL_NAMES[i];

        // Check if the DLL is loaded
        HMODULE hModule = GetModuleHandleW(dll_name);
        if (!hModule) {
            continue;
        }

        LogInfo("Found DLSS-G DLL: %ws", dll_name);

        // Get version information
        std::wstring version_str = GetDLLVersionStr(dll_name);
        if (version_str.empty() || version_str == L"N/A") {
            LogWarn("Could not get version string for %ws", dll_name);
            continue;
        }

        // Verify this is actually a DLSS-G DLL
        if (!IsDLSSGDLL(version_str)) {
            LogWarn("DLL %ws does not appear to be a DLSS-G DLL (version string: %ws)", dll_name, version_str.c_str());
            continue;
        }

        LogInfo("DLSS-G Version String (%ws): %ws", dll_name, version_str.c_str());

        // Parse version numbers
        std::wstring version_short = GetDLLVersionShort(dll_name);
        VersionInfo version;
        version.dll_path = std::string(dll_name, dll_name + wcslen(dll_name));
        version.driver_override = (wcscmp(PathFindExtensionW(dll_name), L".bin") == 0);

        if (ParseVersionNumbers(version_short, version)) {
            // Convert version string to UTF-8 for storage
            int size_needed = WideCharToMultiByte(CP_UTF8, 0, version_str.c_str(), -1, nullptr, 0, nullptr, nullptr);
            if (size_needed > 0) {
                version.version_string.resize(size_needed - 1);
                WideCharToMultiByte(CP_UTF8, 0, version_str.c_str(), -1, &version.version_string[0], size_needed,
                                    nullptr, nullptr);
            }

            version.valid = true;

            // Only update if this version is newer (like Special-K does)
            if (current_version.isOlderThan(version)) {
                current_version = version;
                version_found = true;
                LogInfo("Updated DLSS-G version to: %s", version.getFormattedVersion().c_str());
            }
        } else {
            LogWarn("Failed to parse version numbers for %ws", dll_name);
        }
    }

    if (version_found) {
        initialized = true;
        LogInfo("DLSS-FG Version Detector initialized successfully - Version: %s",
                current_version.getFormattedVersion().c_str());
    } else {
        LogInfo("DLSS-FG Version Detector initialized - No DLSS-G DLL found");
        initialized = false; // Mark as not initialized
    }

    return initialized;
}

void DLSSFGVersionDetector::Cleanup() {
    initialized = false;
    failed_to_initialize = false;
    current_version = VersionInfo();
    last_error.clear();
}

bool DLSSFGVersionDetector::IsAvailable() const { return initialized && current_version.valid; }

const DLSSFGVersionDetector::VersionInfo &DLSSFGVersionDetector::GetVersion() const { return current_version; }

bool DLSSFGVersionDetector::RefreshVersion() {
    if (!initialized) {
        return Initialize();
    }

    // Reset current version and re-detect
    current_version = VersionInfo();
    return Initialize();
}

const std::string &DLSSFGVersionDetector::GetLastError() const { return last_error; }

std::wstring DLSSFGVersionDetector::GetDLLVersionStr(const wchar_t *wszName) const {
    UINT cbTranslatedBytes = 0;
    UINT cbProductBytes = 0;
    UINT cbVersionBytes = 0;

    uint8_t cbData[4097] = {};
    size_t dwSize = 4096;

    char *wszFileDescrip = nullptr;
    char *wszFileVersion = nullptr;

    struct LANGANDCODEPAGE {
        WORD wLanguage;
        WORD wCodePage;
    } *lpTranslate = nullptr;

    BOOL bRet = GetFileVersionInfoExW(FILE_VER_GET_NEUTRAL | FILE_VER_GET_PREFETCHED, wszName, 0x00,
                                      static_cast<DWORD>(dwSize), cbData);

    if (!bRet) {
        return L"N/A";
    }

    if (VerQueryValueA(cbData, "\\VarFileInfo\\Translation", reinterpret_cast<LPVOID *>(&lpTranslate),
                       &cbTranslatedBytes) &&
        cbTranslatedBytes && lpTranslate) {

        char wszPropName[64] = {};
        _snprintf_s(wszPropName, 63, "\\StringFileInfo\\%04x%04x\\FileDescription", lpTranslate[0].wLanguage,
                    lpTranslate[0].wCodePage);

        VerQueryValueA(cbData, wszPropName, reinterpret_cast<LPVOID *>(&wszFileDescrip), &cbProductBytes);

        if (cbProductBytes == 0) {
            _snprintf_s(wszPropName, 63, "\\StringFileInfo\\%04x%04x\\ProductName", lpTranslate[0].wLanguage,
                        lpTranslate[0].wCodePage);

            VerQueryValueA(cbData, wszPropName, reinterpret_cast<LPVOID *>(&wszFileDescrip), &cbProductBytes);
        }

        _snprintf_s(wszPropName, 63, "\\StringFileInfo\\%04x%04x\\FileVersion", lpTranslate[0].wLanguage,
                    lpTranslate[0].wCodePage);

        VerQueryValueA(cbData, wszPropName, reinterpret_cast<LPVOID *>(&wszFileVersion), &cbVersionBytes);

        if (cbVersionBytes == 0) {
            _snprintf_s(wszPropName, 63, "\\StringFileInfo\\%04x%04x\\ProductVersion", lpTranslate[0].wLanguage,
                        lpTranslate[0].wCodePage);

            VerQueryValueA(cbData, wszPropName, reinterpret_cast<LPVOID *>(&wszFileVersion), &cbVersionBytes);
        }
    }

    if (cbTranslatedBytes == 0 || (cbProductBytes == 0 && cbVersionBytes == 0)) {
        return L"  ";
    }

    std::wstring ret;

    if (cbProductBytes) {
        // Convert ANSI to wide string
        int size_needed = MultiByteToWideChar(CP_ACP, 0, wszFileDescrip, -1, nullptr, 0);
        if (size_needed > 0) {
            std::wstring wide_desc(size_needed - 1, 0);
            MultiByteToWideChar(CP_ACP, 0, wszFileDescrip, -1, &wide_desc[0], size_needed);
            ret.append(wide_desc);
        }
        ret.append(L"  ");
    }

    if (cbVersionBytes) {
        // Convert ANSI to wide string
        int size_needed = MultiByteToWideChar(CP_ACP, 0, wszFileVersion, -1, nullptr, 0);
        if (size_needed > 0) {
            std::wstring wide_ver(size_needed - 1, 0);
            MultiByteToWideChar(CP_ACP, 0, wszFileVersion, -1, &wide_ver[0], size_needed);
            ret.append(wide_ver);
        }
    }

    return ret;
}

std::wstring DLSSFGVersionDetector::GetDLLVersionShort(const wchar_t *wszName) const {
    UINT cbTranslatedBytes = 0;
    UINT cbProductBytes = 0;
    UINT cbVersionBytes = 0;

    uint8_t cbData[4097] = {};
    size_t dwSize = 4096;

    char *wszFileDescrip = nullptr;
    char *wszFileVersion = nullptr;

    struct LANGANDCODEPAGE {
        WORD wLanguage;
        WORD wCodePage;
    } *lpTranslate = nullptr;

    BOOL bRet = GetFileVersionInfoExW(FILE_VER_GET_NEUTRAL | FILE_VER_GET_PREFETCHED, wszName, 0x00,
                                      static_cast<DWORD>(dwSize), cbData);

    if (!bRet) {
        return L"N/A";
    }

    if (VerQueryValueA(cbData, "\\VarFileInfo\\Translation", reinterpret_cast<LPVOID *>(&lpTranslate),
                       &cbTranslatedBytes) &&
        cbTranslatedBytes && lpTranslate) {

        char wszPropName[64] = {};
        _snprintf_s(wszPropName, 63, "\\StringFileInfo\\%04x%04x\\FileVersion", lpTranslate[0].wLanguage,
                    lpTranslate[0].wCodePage);

        VerQueryValueA(cbData, wszPropName, reinterpret_cast<LPVOID *>(&wszFileVersion), &cbVersionBytes);

        if (cbVersionBytes == 0) {
            _snprintf_s(wszPropName, 63, "\\StringFileInfo\\%04x%04x\\ProductVersion", lpTranslate[0].wLanguage,
                        lpTranslate[0].wCodePage);

            VerQueryValueA(cbData, wszPropName, reinterpret_cast<LPVOID *>(&wszFileVersion), &cbVersionBytes);
        }
    }

    if (cbTranslatedBytes == 0 || cbVersionBytes == 0) {
        return L"N/A";
    }

    // Convert ANSI to wide string
    int size_needed = MultiByteToWideChar(CP_ACP, 0, wszFileVersion, -1, nullptr, 0);
    if (size_needed > 0) {
        std::wstring wide_ver(size_needed - 1, 0);
        MultiByteToWideChar(CP_ACP, 0, wszFileVersion, -1, &wide_ver[0], size_needed);
        return wide_ver;
    }
    return L"N/A";
}

bool DLSSFGVersionDetector::IsDLSSGDLL(const std::wstring &version_string) const {
    // Check for DLSS-G specific strings (same as Special-K)
    return version_string.find(L"NVIDIA DLSS-G -") != std::wstring::npos ||
           version_string.find(L"NVIDIA DLSS-G MFGLW -") != std::wstring::npos;
}

bool DLSSFGVersionDetector::ParseVersionNumbers(const std::wstring &version_short, VersionInfo &version) const {
    if (version_short.empty() || version_short == L"N/A") {
        return false;
    }

    // Parse version numbers using swscanf (same as Special-K)
    int result = swscanf_s(version_short.c_str(), L"%d,%d,%d,%d", &version.major, &version.minor, &version.build,
                           &version.revision);

    return result == 4;
}
