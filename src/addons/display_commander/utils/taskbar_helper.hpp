// Source Code <Display Commander> // follow this order for includes in all files + add this comment at the top
#pragma once

namespace display_commander {
namespace utils {

// Taskbar hide/show feature removed. Keep no-op stubs so existing call sites remain harmless.
inline void UpdateTaskbarVisibility(bool in_foreground, int mode) {
    (void)in_foreground;
    (void)mode;
}

inline void RestoreTaskbarIfHidden() {}

}  // namespace utils
}  // namespace display_commander
