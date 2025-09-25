#pragma once

#include <audiopolicy.h>
#include <mmdeviceapi.h>
#include <windows.h>

// Audio debugging functions
void DebugAudioSessions();
void DebugAudioSessionForProcess(DWORD process_id);
void LogAudioSessionInfo(IAudioSessionControl *session_control, int session_index);
