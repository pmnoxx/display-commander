#pragma once

#include <windows.h>
#include <string>

namespace dlss {

// DLSS indicator registry management
class DlssIndicatorManager {
public:
    // Check if DLSS indicator is currently enabled in registry
    static bool IsDlssIndicatorEnabled();

    // Get current registry value (0 = disabled, 1024 = enabled)
    static DWORD GetDlssIndicatorValue();

    // Set DLSS indicator in registry (enable or disable). Requires admin for HKLM write.
    // Returns true if write succeeded, false on access denied or error.
    static bool SetDlssIndicatorEnabled(bool enable);

    // Generate .reg file content for enabling DLSS indicator
    static std::string GenerateEnableRegFile();

    // Generate .reg file content for disabling DLSS indicator
    static std::string GenerateDisableRegFile();

    // Write .reg file to disk
    static bool WriteRegFile(const std::string& content, const std::string& filename);

    // Get the registry key path for DLSS indicator
    static std::string GetRegistryKeyPath();

    // Get the registry value name for DLSS indicator
    static std::string GetRegistryValueName();

private:
    static constexpr const char* REGISTRY_KEY_PATH = "SOFTWARE\\NVIDIA Corporation\\Global\\NGXCore";
    static constexpr const char* REGISTRY_VALUE_NAME = "ShowDlssIndicator";
    static constexpr DWORD ENABLED_VALUE = 1024;
    static constexpr DWORD DISABLED_VALUE = 0;
};

} // namespace dlss
