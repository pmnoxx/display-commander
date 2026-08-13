// Source Code <Display Commander> // follow this order for includes in all files + add this comment at the top
#pragma once

// Libraries <standard C++>
#include <cstdint>
#include <string>

namespace display_commander::mit::deamon {

struct DaemonStatus {
    bool attempted = false;
    bool copy_ok = false;
    bool process_started = false;
    std::uint32_t daemon_pid = 0;
    std::uint32_t target_pid = 0;
    std::uint32_t last_error = 0;
    std::wstring folder;
    std::wstring dll_path;
    std::wstring source_dll_path;
    std::string message;
};

// Copy this module to %LocalAppData%\Programs\Display_Commander\daemon\<hash>_<exe>\dc_32.dll or dc_64.dll
// and start rundll32 Daemon <current pid>. Safe to call more than once (first call wins).
void EnsureBackgroundDaemonStarted(void* h_module);

DaemonStatus GetDaemonStatus();

// True if the rundll32 watcher process is still alive.
bool IsDaemonProcessAlive();

// UTF-8 contents of daemon.txt in the daemon folder (empty if missing). Capped for UI.
std::string ReadDaemonLogUtf8();

}  // namespace display_commander::mit::deamon
