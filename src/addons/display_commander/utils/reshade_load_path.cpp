#include "reshade_load_path.hpp"
#include "../config/display_commander_config.hpp"
#include <ShlObj.h>
#include <Windows.h>
#include <algorithm>
#include <filesystem>
#include <string>

namespace display_commander::utils {

namespace {
constexpr const char* RESHADE_SECTION = "DisplayCommander.ReShade";
constexpr const char* KEY_LOAD_SOURCE = "ReshadeLoadSource";
constexpr const char* KEY_SHARED_PATH = "ReshadeSharedPath";
constexpr const char* KEY_SELECTED_VERSION = "ReshadeSelectedVersion";

constexpr int DEFAULT_LOAD_SOURCE = 0;  // Local
constexpr const char* DEFAULT_VERSION = "6.7.3";

static const char* const RESHADE_VERSIONS[] = {"6.6.2", "6.7.3"};
static const size_t RESHADE_VERSIONS_COUNT = sizeof(RESHADE_VERSIONS) / sizeof(RESHADE_VERSIONS[0]);
}  // namespace

std::filesystem::path GetReshadeDirectoryForLoading() {
    using namespace display_commander::config;
    auto& config = DisplayCommanderConfigManager::GetInstance();

    int load_source = DEFAULT_LOAD_SOURCE;
    config.GetConfigValue(RESHADE_SECTION, KEY_LOAD_SOURCE, load_source);

    std::string shared_path;
    config.GetConfigValue(RESHADE_SECTION, KEY_SHARED_PATH, shared_path);

    std::string selected_version;
    config.GetConfigValue(RESHADE_SECTION, KEY_SELECTED_VERSION, selected_version);
    if (selected_version.empty()) {
        selected_version = DEFAULT_VERSION;
    }

    wchar_t localappdata_path[MAX_PATH];
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, localappdata_path))) {
        return std::filesystem::path();
    }
    std::filesystem::path base = std::filesystem::path(localappdata_path) / L"Programs" / L"Display_Commander"
                                / L"Reshade";

    switch (static_cast<ReshadeLoadSource>(load_source)) {
        case ReshadeLoadSource::Local:
            return base;
        case ReshadeLoadSource::SharedPath: {
            if (shared_path.empty()) {
                return base;
            }
            std::filesystem::path p(shared_path);
            std::error_code ec;
            std::filesystem::path abs = std::filesystem::absolute(p, ec);
            if (ec) {
                return base;
            }
            return abs;
        }
        case ReshadeLoadSource::SpecificVersion:
            return base / std::filesystem::path(selected_version);
        default:
            return base;
    }
}

ReshadeLoadSource GetReshadeLoadSourceFromConfig() {
    using namespace display_commander::config;
    int value = DEFAULT_LOAD_SOURCE;
    DisplayCommanderConfigManager::GetInstance().GetConfigValue(RESHADE_SECTION, KEY_LOAD_SOURCE, value);
    if (value < 0 || value > 2) {
        value = DEFAULT_LOAD_SOURCE;
    }
    return static_cast<ReshadeLoadSource>(value);
}

void SetReshadeLoadSourceInConfig(ReshadeLoadSource value) {
    using namespace display_commander::config;
    DisplayCommanderConfigManager::GetInstance().SetConfigValue(RESHADE_SECTION, KEY_LOAD_SOURCE,
                                                                static_cast<int>(value));
}

std::string GetReshadeSharedPathFromConfig() {
    using namespace display_commander::config;
    std::string value;
    DisplayCommanderConfigManager::GetInstance().GetConfigValue(RESHADE_SECTION, KEY_SHARED_PATH, value);
    return value;
}

void SetReshadeSharedPathInConfig(const std::string& path) {
    using namespace display_commander::config;
    DisplayCommanderConfigManager::GetInstance().SetConfigValue(RESHADE_SECTION, KEY_SHARED_PATH, path);
}

std::string GetReshadeSelectedVersionFromConfig() {
    using namespace display_commander::config;
    std::string value;
    DisplayCommanderConfigManager::GetInstance().GetConfigValue(RESHADE_SECTION, KEY_SELECTED_VERSION, value);
    if (value.empty()) {
        value = DEFAULT_VERSION;
    }
    return value;
}

void SetReshadeSelectedVersionInConfig(const std::string& version) {
    using namespace display_commander::config;
    DisplayCommanderConfigManager::GetInstance().SetConfigValue(RESHADE_SECTION, KEY_SELECTED_VERSION, version);
}

const char* const* GetReshadeVersionList(size_t* out_count) {
    *out_count = RESHADE_VERSIONS_COUNT;
    return RESHADE_VERSIONS;
}

}  // namespace display_commander::utils
