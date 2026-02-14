#include "dlss_indicator_manager.hpp"
#include "../utils/logging.hpp"
#include <fstream>
#include <sstream>

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

bool DlssIndicatorManager::SetDlssIndicatorEnabled(bool enable) {
    HKEY h_key;
    LONG result = RegOpenKeyExA(HKEY_LOCAL_MACHINE, REGISTRY_KEY_PATH, 0, KEY_SET_VALUE, &h_key);

    if (result != ERROR_SUCCESS) {
        LogInfo("DLSS Indicator: Failed to open registry key for write, error: %ld (admin may be required)", result);
        return false;
    }

    const DWORD value = enable ? ENABLED_VALUE : DISABLED_VALUE;
    result = RegSetValueExA(h_key, REGISTRY_VALUE_NAME, 0, REG_DWORD,
                            reinterpret_cast<const BYTE*>(&value), sizeof(DWORD));

    RegCloseKey(h_key);

    if (result != ERROR_SUCCESS) {
        LogInfo("DLSS Indicator: Failed to write registry value, error: %ld", result);
        return false;
    }
    LogInfo("DLSS Indicator: Registry set to %s", enable ? "enabled" : "disabled");
    return true;
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


std::string DlssIndicatorManager::GetRegistryKeyPath() {
    return REGISTRY_KEY_PATH;
}

std::string DlssIndicatorManager::GetRegistryValueName() {
    return REGISTRY_VALUE_NAME;
}

} // namespace dlss
