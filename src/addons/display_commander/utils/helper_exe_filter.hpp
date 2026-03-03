#pragma once

// Return true if the exe filename looks like a helper/crash handler (e.g. UnityCrashHandler64.exe,
// PlatformProcess.exe), not the main game. Used to refuse loading DC into these processes.
bool is_helper_or_crash_handler_exe(const wchar_t* filename);
