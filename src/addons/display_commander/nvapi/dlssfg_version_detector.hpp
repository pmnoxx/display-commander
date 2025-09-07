#pragma once

#include <windows.h>
#include <string>
#include <sstream>

// DLSS-FG Version Detection Module
// Based on Special-K's implementation for detecting DLSS Frame Generation version
class DLSSFGVersionDetector {
public:
    // Constructor
    DLSSFGVersionDetector();

    // Destructor
    ~DLSSFGVersionDetector();

    // Initialize the detector
    bool Initialize();

    // Cleanup resources
    void Cleanup();

    // Check if DLSS-FG is available
    bool IsAvailable() const;

    // Get DLSS-FG version information
    struct VersionInfo {
        unsigned int major = 0;
        unsigned int minor = 0;
        unsigned int build = 0;
        unsigned int revision = 0;
        bool driver_override = false;
        std::string version_string;
        std::string dll_path;
        bool valid = false;

        // Check if this version is older than another
        bool isOlderThan(const VersionInfo& other) const {
            return other.major > major ||
                   (other.major == major && other.minor > minor) ||
                   (other.major == major && other.minor == minor && other.build > build) ||
                   (other.major == major && other.minor == minor && other.build == build && other.revision > revision);
        }

        // Get formatted version string
        std::string getFormattedVersion() const {
            if (!valid) return "Unknown";
            std::ostringstream oss;
            oss << major << "." << minor << "." << build << "." << revision;
            if (driver_override) {
                oss << " (Driver Override)";
            }
            return oss.str();
        }
    };

    // Get current DLSS-FG version
    const VersionInfo& GetVersion() const;

    // Force re-detection of DLSS-FG version
    bool RefreshVersion();

    // Get last error message
    const std::string& GetLastError() const;

private:
    // Internal state
    bool initialized = false;
    bool failed_to_initialize = false;
    VersionInfo current_version;
    std::string last_error;

    // DLL version detection functions (based on Special-K's approach)
    std::wstring GetDLLVersionStr(const wchar_t* wszName) const;
    std::wstring GetDLLVersionShort(const wchar_t* wszName) const;

    // Check if a DLL is actually a DLSS-G DLL
    bool IsDLSSGDLL(const std::wstring& version_string) const;

    // Parse version numbers from version string
    bool ParseVersionNumbers(const std::wstring& version_short, VersionInfo& version) const;

    // Common DLSS-G DLL names to check
    static constexpr const wchar_t* DLSSG_DLL_NAMES[] = {
        L"nvngx_dlssg.dll",
        L"nvngx_dlssg.bin"  // Driver override
    };
    static constexpr size_t DLSSG_DLL_COUNT = 2;
};

// Global instance
extern DLSSFGVersionDetector g_dlssfgVersionDetector;
