#pragma once

#include <windows.h>
#include <mmdeviceapi.h>
#include <audiopolicy.h>

// Audio debugging functions
void DebugAudioSessions();
void DebugAudioSessionForProcess(DWORD process_id);
void LogAudioSessionInfo(IAudioSessionControl* session_control, int session_index);
