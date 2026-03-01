#pragma once

#include <atomic>
#include <string>

namespace display_commander::utils {

// Status for the "Specific version" download (ReShade tab).
enum class ReshadeDownloadStatus : int {
    Idle = 0,
    Downloading = 1,
    Extracting = 2,
    Ready = 3,
    Error = 4
};

// Get current status and last error message (for UI). Error message is only set when status == Error.
ReshadeDownloadStatus GetReshadeDownloadStatus();
const char* GetReshadeDownloadStatusMessage();

// Start downloading and extracting the given ReShade version (e.g. "6.7.3") in a background thread.
// Downloads from https://reshade.me/downloads/ReShade_Setup_<version>_Addon.exe, extracts with tar.exe,
// copies Reshade64.dll and Reshade32.dll to %localappdata%\Programs\Display_Commander\Reshade\Dll\<version>\.
// No-op if already downloading. Uses atomics for status (no std::mutex).
void StartReshadeVersionDownload(const std::string& version);

}  // namespace display_commander::utils
