#include "dlss_indicator_manager.hpp"
#include "../utils.hpp"

#include <fstream>
#include <sstream>
#include <shellapi.h>

namespace dlss {

bool DlssIndicatorManager::IsDlssIndicatorEnabled() {
    DWORD value = GetDlssIndicatorValue();
    return value == ENABLED_VALUE;
}

DWORD DlssIndicatorManager::GetDlssIndicatorValue() {
    HKEY h_key;
    LONG result = RegOpenKeyExA(HKEY_LOCAL_MACHINE, REGISTRY_KEY_PATH, 0, KEY_READ, &h_key);

    if (result != ERROR_SUCCESS) {
        LogInfo("DLSS Indicator: Failed to open registry key, error: %ld", result);
        return DISABLED_VALUE;
    }

    DWORD value = DISABLED_VALUE;
    DWORD value_size = sizeof(DWORD);
    DWORD value_type = REG_DWORD;

    result = RegQueryValueExA(h_key, REGISTRY_VALUE_NAME, nullptr, &value_type,
                             reinterpret_cast<BYTE*>(&value), &value_size);

    if (result != ERROR_SUCCESS) {
        LogInfo("DLSS Indicator: Failed to read registry value, error: %ld", result);
        value = DISABLED_VALUE;
    }

    RegCloseKey(h_key);
    return value;
}

std::string DlssIndicatorManager::GenerateEnableRegFile() {
    std::stringstream reg_content;
    reg_content << "Windows Registry Editor Version 5.00\n\n";
    reg_content << "[HKEY_LOCAL_MACHINE\\" << REGISTRY_KEY_PATH << "]\n";
    reg_content << "\"" << REGISTRY_VALUE_NAME << "\"=dword:" << std::hex << ENABLED_VALUE << "\n";
    return reg_content.str();
}

std::string DlssIndicatorManager::GenerateDisableRegFile() {
    std::stringstream reg_content;
    reg_content << "Windows Registry Editor Version 5.00\n\n";
    reg_content << "[HKEY_LOCAL_MACHINE\\" << REGISTRY_KEY_PATH << "]\n";
    reg_content << "\"" << REGISTRY_VALUE_NAME << "\"=dword:" << std::hex << DISABLED_VALUE << "\n";
    return reg_content.str();
}

bool DlssIndicatorManager::WriteRegFile(const std::string& content, const std::string& filename) {
    try {
        std::ofstream file(filename);
        if (!file.is_open()) {
            LogError("DLSS Indicator: Failed to create .reg file: %s", filename.c_str());
            return false;
        }

        file << content;
        file.close();

        LogInfo("DLSS Indicator: .reg file created successfully: %s", filename.c_str());
        return true;
    } catch (const std::exception& e) {
        LogError("DLSS Indicator: Exception while writing .reg file: %s", e.what());
        return false;
    }
}

bool DlssIndicatorManager::ExecuteRegFile(const std::string& filepath) {
    // Use ShellExecute with "runas" to request admin privileges
    HINSTANCE result = ShellExecuteA(nullptr, "runas", "regedit.exe",
                                   ("/s \"" + filepath + "\"").c_str(),
                                   nullptr, SW_HIDE);

    if (reinterpret_cast<INT_PTR>(result) <= 32) {
        LogError("DLSS Indicator: Failed to execute .reg file, error: %ld",
                reinterpret_cast<INT_PTR>(result));
        return false;
    }

    LogInfo("DLSS Indicator: .reg file executed successfully: %s", filepath.c_str());
    return true;
}

std::string DlssIndicatorManager::GetRegistryKeyPath() {
    return REGISTRY_KEY_PATH;
}

std::string DlssIndicatorManager::GetRegistryValueName() {
    return REGISTRY_VALUE_NAME;
}

} // namespace dlss
