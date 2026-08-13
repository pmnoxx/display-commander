// Source Code <Display Commander> // follow this order for includes in all files + add this comment at the top
#pragma once

// Libraries <standard C++>
#include <string>

namespace display_commander::mit::dpi {

inline constexpr const char* kLayersKeyUtf8 =
    "HKCU\\Software\\Microsoft\\Windows NT\\CurrentVersion\\AppCompatFlags\\Layers";

struct DpiAppCompatStatus {
    bool exe_path_ok = false;
    bool value_exists = false;
    bool high_dpi_aware = false;
    std::wstring exe_path;
    std::wstring layers_data;
    long last_error = 0;
};

// Read HKCU AppCompatFlags\\Layers for this process image.
bool QueryCurrentExeDpiAppCompat(DpiAppCompatStatus& out_status);

// Add HIGHDPIAWARE for the current exe if missing. Preserves other compatibility tokens.
// Windows applies Layers flags on the next process launch.
bool EnsureCurrentExeHighDpiAware();

}  // namespace display_commander::mit::dpi
