// Source Code <Display Commander> // follow this order for includes in all files + add this comment at the top
#pragma once

namespace display_commander {
namespace utils {

// Update taskbar visibility based on foreground state and mode.
// mode: 0 = no change (do not hide), 1 = hide when in_foreground, 2 = always hide.
// Only changes window state when the desired visibility changes.
void UpdateTaskbarVisibility(bool in_foreground, int mode);

// Restore the taskbar if it was hidden by us (e.g. on addon unload). Call from exit handler.
void RestoreTaskbarIfHidden();

}  // namespace utils
}  // namespace display_commander
