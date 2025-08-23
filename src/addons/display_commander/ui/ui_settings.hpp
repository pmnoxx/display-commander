#pragma once

#include <windows.h>
#include <vector>
#include <string>

// Forward declarations
namespace renodx::utils::settings2 {
    struct Setting;
}

// External declarations for settings


extern float s_reflex_debug_output;
extern float s_remove_top_bar;
extern float s_suppress_maximize;

extern float s_auto_apply_enabled;
extern float s_auto_apply_delay_sec;
extern float s_windowed_width;
extern float s_windowed_height;
extern float s_window_mode;
extern float s_move_to_zero_if_out;

extern float s_nvapi_fullscreen_prevention;
extern float s_audio_volume_percent;
extern float s_audio_mute;
extern float s_mute_in_background;
extern float s_mute_in_background_if_other_audio;
extern float s_fps_limit_background;
extern float s_window_info_display;

// Settings vector declaration
extern std::vector<renodx::utils::settings2::Setting*> settings;

// Helper functions for labels
std::vector<std::string> MakeLabels(const std::vector<int>& options, int default_index);
std::vector<std::string> MakeMonitorLabels();
void ComputeDesiredSize(int& width, int& height);

// Window management functions
enum class WindowStyleMode;

// Audio debugging functions
void DebugAudioSessions();
void DebugAudioSessionForProcess(DWORD process_id);
