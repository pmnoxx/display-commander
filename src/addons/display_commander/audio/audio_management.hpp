#pragma once

#include <windows.h>

// Audio management functions
bool SetMuteForCurrentProcess(bool mute, bool trigger_notification = true);
bool SetVolumeForCurrentProcess(float volume_0_100);
bool GetVolumeForCurrentProcess(float *volume_0_100_out);
bool AdjustVolumeForCurrentProcess(float percent_change);
void RunBackgroundAudioMonitor();
// Returns true if any other process has an active, unmuted session with volume > 0
bool IsOtherAppPlayingAudio();
