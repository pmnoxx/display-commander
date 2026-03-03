// Source Code <Display Commander>
#include "init_stage_limit.hpp"

// Libraries <standard C++>
#include <cstring>
#include <filesystem>
#include <string>

// Libraries <Windows.h>
#include <Windows.h>

namespace display_commander::utils {

namespace {

constexpr const char* kDcMaxStagePrefix = ".DCMAX_STAGE.";

}  // namespace

std::atomic<int> g_init_stage{0};
int g_max_init_stage_allowed = 0;

void InitStageLimit_LoadFromFile() {
    g_init_stage.store(0, std::memory_order_release);
    g_max_init_stage_allowed = 0;

    wchar_t exe_path[MAX_PATH];
    if (GetModuleFileNameW(nullptr, exe_path, MAX_PATH) == 0) return;
    std::filesystem::path exe_dir = std::filesystem::path(exe_path).parent_path();
    std::error_code ec;
    if (!std::filesystem::is_directory(exe_dir, ec) || ec) return;

    for (const auto& entry : std::filesystem::directory_iterator(exe_dir, ec)) {
        if (ec) break;
        if (!entry.is_regular_file(ec)) continue;
        std::string name = entry.path().filename().string();
        if (name.size() <= strlen(kDcMaxStagePrefix)) continue;
        if (name.compare(0, strlen(kDcMaxStagePrefix), kDcMaxStagePrefix) != 0) continue;
        std::string suffix = name.substr(strlen(kDcMaxStagePrefix));
        if (suffix.empty()) continue;
        int value = 0;
        try {
            size_t pos = 0;
            value = std::stoi(suffix, &pos);
            if (pos != suffix.size() || value < 0) continue;
        } catch (...) {
            continue;
        }
        g_max_init_stage_allowed = value;
        {
            char msg[128];
            snprintf(msg, sizeof(msg), "[DisplayCommander] Init stage limit enabled: max_stage=%d (from %s)\n",
                     value, name.c_str());
            OutputDebugStringA(msg);
        }
        return;
    }
}

void NextInitStage() {
    g_init_stage.fetch_add(1, std::memory_order_release);
}

}  // namespace display_commander::utils
