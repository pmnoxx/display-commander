#pragma once

#include <windows.h>

// Audio management functions
bool SetMuteForCurrentProcess(bool mute);
bool SetVolumeForCurrentProcess(float volume_0_100);
void RunBackgroundAudioMonitor();
// Returns true if any other process has an active, unmuted session with volume > 0
bool IsOtherAppPlayingAudio();
