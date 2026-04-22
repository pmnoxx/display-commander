// Source Code <Display Commander> // follow this order for includes in all files + add this comment at the top
#include "smooth_motion.hpp"

// Libraries <standard C++>
#include <algorithm>
#include <atomic>
#include <cwctype>
#include <filesystem>

namespace display_commander::features::smooth_motion {

namespace {
std::atomic<bool> s_smooth_motion_dll_loaded{false};
}  // namespace

void OnModuleLoaded(const std::wstring& module_name) {
    std::wstring filename = std::filesystem::path(module_name).filename().wstring();
    std::transform(filename.begin(), filename.end(), filename.begin(), ::towlower);
    if (filename == L"nvpresent64.dll" || filename == L"nvpresent32.dll") {
        s_smooth_motion_dll_loaded.store(true, std::memory_order_relaxed);
    }
}

bool IsSmoothMotionLoaded() {
    return s_smooth_motion_dll_loaded.load(std::memory_order_relaxed);
}

}  // namespace display_commander::features::smooth_motion
