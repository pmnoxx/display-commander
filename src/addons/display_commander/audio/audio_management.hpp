#pragma once

#include <windows.h>
#include <string>
#include <vector>

// Audio management functions
bool SetMuteForCurrentProcess(bool mute, bool trigger_notification = true);
bool SetVolumeForCurrentProcess(float volume_0_100);
bool GetVolumeForCurrentProcess(float *volume_0_100_out);
bool AdjustVolumeForCurrentProcess(float percent_change);
void RunBackgroundAudioMonitor();
// Returns true if any other process has an active, unmuted session with volume > 0
bool IsOtherAppPlayingAudio();

// System volume management functions (master volume for the audio endpoint)
bool SetSystemVolume(float volume_0_100);
bool GetSystemVolume(float *volume_0_100_out);
bool AdjustSystemVolume(float percent_change);

// Audio output device helpers (per-application routing using Windows Audio Policy)
// - device_names_utf8: Friendly names for ImGui display
// - device_ids:        Stable WASAPI endpoint IDs (IMMDevice::GetId)
// - current_device_id: Persisted default endpoint for this process (empty = system default)
bool GetAudioOutputDevices(std::vector<std::string> &device_names_utf8,
                           std::vector<std::wstring> &device_ids,
                           std::wstring &current_device_id);

// Sets the preferred output device for the current process.
// Pass empty device_id to clear override and use system default.
bool SetAudioOutputDeviceForCurrentProcess(const std::wstring &device_id);

