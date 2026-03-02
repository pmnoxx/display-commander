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
// Copies to the global root (%%localappdata%%\Programs\Display_Commander\Reshade),
// overwriting the single global version. Per updates UI spec: Download button overwrites that folder.
void StartReshadeVersionDownloadToGlobalRoot(const std::string& version);

}  // namespace display_commander::utils
