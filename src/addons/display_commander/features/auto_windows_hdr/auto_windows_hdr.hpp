// Source Code <Display Commander> // Auto Windows HDR feature slice; see features/auto_windows_hdr/
#pragma once

// Libraries <Windows.h>
#include <Windows.h>

namespace display_commander::features::auto_windows_hdr {

// When Main tab "Auto enable Windows HDR" is on: enable Windows HDR for the game's monitor and record state for revert.
void OnSwapchainInitTryAutoEnableWindowsHdr(HWND hwnd);

// On swapchain destroy: if we auto-enabled HDR for this HWND's monitor, turn HDR off and clear state.
void OnSwapchainDestroyMaybeRevertAutoHdr(HWND hwnd);

// On process exit: if we still believe we auto-enabled HDR, turn it off using the stored monitor (no HWND).
void OnProcessExitRevertAutoHdrIfNeeded();

}  // namespace display_commander::features::auto_windows_hdr
