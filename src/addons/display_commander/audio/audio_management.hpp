#pragma once

#include <windows.h>
#include <string>
#include <vector>

// Audio management functions
bool SetMuteForCurrentProcess(bool mute, bool trigger_notification = true);
bool SetVolumeForCurrentProcess(float volume_0_100);
bool GetVolumeForCurrentProcess(float* volume_0_100_out);
bool AdjustVolumeForCurrentProcess(float percent_change);
void RunBackgroundAudioMonitor();
// Returns true if any other process has an active, unmuted session with volume > 0
bool IsOtherAppPlayingAudio();

// System volume management functions (master volume for the audio endpoint)
bool SetSystemVolume(float volume_0_100);
bool GetSystemVolume(float* volume_0_100_out);
bool AdjustSystemVolume(float percent_change);

// Audio output device helpers (per-application routing using Windows Audio Policy)
// - device_names_utf8: Friendly names for ImGui display
// - device_ids:        Stable WASAPI endpoint IDs (IMMDevice::GetId)
// - current_device_id: Persisted default endpoint for this process (empty = system default)
bool GetAudioOutputDevices(std::vector<std::string>& device_names_utf8, std::vector<std::wstring>& device_ids,
                           std::wstring& current_device_id);

// Sets the preferred output device for the current process.
// Pass empty device_id to clear override and use system default.
bool SetAudioOutputDeviceForCurrentProcess(const std::wstring& device_id);

// Per-channel (e.g. left/right speaker) volume for the current process.
// Only available when the session supports IChannelAudioVolume (typically stereo or more).
bool GetChannelVolumeCountForCurrentProcess(unsigned int* channel_count_out);
bool GetChannelVolumeForCurrentProcess(unsigned int channel_index, float* volume_0_1_out);
bool SetChannelVolumeForCurrentProcess(unsigned int channel_index, float volume_0_1);
// Get all channel volumes in one call. out_volumes_0_1 is filled up to channel_count; returns true on success.
bool GetAllChannelVolumesForCurrentProcess(std::vector<float>* out_volumes_0_1);

// VU meter: per-channel peak levels (0.0–1.0) for the default render endpoint (mixed output).
// Use for level meters in the UI; channel count may differ from session channel count.
bool GetAudioMeterChannelCount(unsigned int* channel_count_out);
bool GetAudioMeterPeakValues(unsigned int channel_count, float* peak_values_0_1_out);
