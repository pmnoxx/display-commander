#!/usr/bin/env python3
"""
Generate winmm_proxy.cpp: forwarding stubs for all winmm.dll exports.
Reads export names from Display Commander's exports.def (winmm section).
Run from repo root: python scripts/gen_winmm_proxy.py
Output: src/addons/display_commander/proxy_dll/winmm_proxy.cpp

Uses Windows ABI types (UINT = MMRESULT for most, DWORD, BOOL, LPVOID, etc.).
"""

import re
import os

# (return_type, [(param_type, param_name), ...]) per symbol. Types match Windows winmm ABI.
# Handles like HDRVR, HWAVEOUT, HMIDIIN are LPVOID in the stub to avoid pulling mmsystem.h.
SIGS = {
    "CloseDriver": ("LRESULT", [("LPVOID", "hDriver"), ("LPVOID", "lParam1"), ("LPVOID", "lParam2")]),
    "DefDriverProc": ("LRESULT", [("DWORD_PTR", "dwDriverIdentifier"), ("HANDLE", "hdrvr"), ("UINT", "uMsg"), ("LPARAM", "lParam1"), ("LPARAM", "lParam2")]),
    "DriverCallback": ("BOOL", [("DWORD_PTR", "dwCallback"), ("DWORD", "dwFlags"), ("HANDLE", "hDevice"), ("DWORD", "dwMsg"), ("DWORD_PTR", "dwUser"), ("DWORD_PTR", "dwParam1"), ("DWORD_PTR", "dwParam2")]),
    "DrvClose": ("LRESULT", [("LPVOID", "hDriver"), ("LPVOID", "lParam1"), ("LPVOID", "lParam2")]),
    "DrvDefDriverProc": ("LRESULT", [("DWORD_PTR", "dwDriverIdentifier"), ("HANDLE", "hdrvr"), ("UINT", "uMsg"), ("LPARAM", "lParam1"), ("LPARAM", "lParam2")]),
    "DrvGetModuleHandle": ("HMODULE", [("HANDLE", "hDrv")]),
    "DrvOpen": ("HDRVR", [("LPCWSTR", "szDriverName"), ("LPCWSTR", "szSectionName"), ("LPARAM", "lParam2")]),
    "DrvOpenA": ("HDRVR", [("LPCSTR", "szDriverName"), ("LPCSTR", "szSectionName"), ("LPARAM", "lParam2")]),
    "DrvSendMessage": ("LRESULT", [("HDRVR", "hDriver"), ("UINT", "message"), ("LPARAM", "lParam1"), ("LPARAM", "lParam2")]),
    "GetDriverFlags": ("DWORD", [("HDRVR", "hDriver")]),
    "GetDriverModuleHandle": ("HMODULE", [("HANDLE", "hDrv")]),
    "NotifyCallbackData": ("void", [("HANDLE", "hTask"), ("DWORD_PTR", "dwCallback"), ("DWORD_PTR", "dwCallbackData")]),
    "OpenDriver": ("HDRVR", [("LPCWSTR", "szDriverName"), ("LPCWSTR", "szSectionName"), ("LPARAM", "lParam2")]),
    "OpenDriverA": ("HDRVR", [("LPCSTR", "szDriverName"), ("LPCSTR", "szSectionName"), ("LPARAM", "lParam2")]),
    "PlaySound": ("BOOL", [("LPCSTR", "pszSound"), ("HMODULE", "hmod"), ("DWORD", "fdwSound")]),
    "PlaySoundA": ("BOOL", [("LPCSTR", "pszSound"), ("HMODULE", "hmod"), ("DWORD", "fdwSound")]),
    "PlaySoundW": ("BOOL", [("LPCWSTR", "pszSound"), ("HMODULE", "hmod"), ("DWORD", "fdwSound")]),
    "SendDriverMessage": ("LRESULT", [("HDRVR", "hDriver"), ("UINT", "message"), ("LPARAM", "lParam1"), ("LPARAM", "lParam2")]),
    "WOW32DriverCallback": ("DWORD", [("DWORD_PTR", "dwCallback"), ("DWORD", "dwFlags"), ("HANDLE", "hDevice"), ("DWORD", "dwMsg"), ("DWORD_PTR", "dwUser"), ("DWORD_PTR", "dwParam1"), ("DWORD_PTR", "dwParam2")]),
    "WOW32ResolveMultiMediaHandle": ("BOOL", [("HANDLE", "hDev"), ("HANDLE *", "phDev"), ("UINT", "cDevs")]),
    "WOWAppExit": ("void", []),
    "aux32Message": ("LRESULT", [("UINT", "uDeviceID"), ("UINT", "uMsg"), ("DWORD_PTR", "dwParam1"), ("DWORD_PTR", "dwParam2")]),
    "auxGetDevCapsA": ("UINT", [("UINT_PTR", "uDeviceID"), ("LPVOID", "pac"), ("UINT", "cbac")]),
    "auxGetDevCapsW": ("UINT", [("UINT_PTR", "uDeviceID"), ("LPVOID", "pac"), ("UINT", "cbac")]),
    "auxGetNumDevs": ("UINT", []),
    "auxGetVolume": ("UINT", [("UINT", "uDeviceID"), ("LPDWORD", "pdwVolume")]),
    "auxOutMessage": ("LRESULT", [("UINT", "uDeviceID"), ("UINT", "uMsg"), ("DWORD_PTR", "dwParam1"), ("DWORD_PTR", "dwParam2")]),
    "auxSetVolume": ("UINT", [("UINT", "uDeviceID"), ("DWORD", "dwVolume")]),
    "joy32Message": ("LRESULT", [("UINT", "uJoyID"), ("UINT", "uMsg"), ("DWORD_PTR", "dwParam1"), ("DWORD_PTR", "dwParam2")]),
    "joyConfigChanged": ("UINT", [("DWORD", "dwFlags")]),
    "joyGetDevCapsA": ("UINT", [("UINT_PTR", "uJoyID"), ("LPVOID", "pjc"), ("UINT", "cjc")]),
    "joyGetDevCapsW": ("UINT", [("UINT_PTR", "uJoyID"), ("LPVOID", "pjc"), ("UINT", "cjc")]),
    "joyGetNumDevs": ("UINT", []),
    "joyGetPos": ("UINT", [("UINT", "uJoyID"), ("LPVOID", "pji")]),
    "joyGetPosEx": ("UINT", [("UINT", "uJoyID"), ("LPVOID", "pji")]),
    "joyGetThreshold": ("UINT", [("UINT", "uJoyID"), ("LPUINT", "puThreshold")]),
    "joyReleaseCapture": ("UINT", [("UINT", "uJoyID")]),
    "joySetCapture": ("UINT", [("HWND", "hwnd"), ("UINT", "uJoyID"), ("UINT", "uPeriod"), ("BOOL", "fChanged")]),
    "joySetThreshold": ("UINT", [("UINT", "uJoyID"), ("UINT", "uThreshold")]),
    "mci32Message": ("LRESULT", [("UINT", "uDeviceID"), ("UINT", "uMsg"), ("DWORD_PTR", "dwParam1"), ("DWORD_PTR", "dwParam2")]),
    "mciDriverNotify": ("BOOL", [("HANDLE", "hCallback"), ("HANDLE", "hDevice"), ("UINT", "uStatus")]),
    "mciDriverYield": ("UINT", [("HANDLE", "hDevice")]),
    "mciExecute": ("BOOL", [("LPCSTR", "pszCommand")]),
    "mciFreeCommandResource": ("UINT", [("HANDLE", "hResource")]),
    "mciGetCreatorTask": ("HANDLE", [("HANDLE", "hDevice")]),
    "mciGetDeviceIDA": ("MCIDEVICEID", [("LPCSTR", "pszElement")]),
    "mciGetDeviceIDFromElementIDA": ("MCIDEVICEID", [("DWORD", "dwElementID"), ("LPSTR", "lpstrTypeName")]),
    "mciGetDeviceIDFromElementIDW": ("MCIDEVICEID", [("DWORD", "dwElementID"), ("LPWSTR", "lpstrTypeName")]),
    "mciGetDeviceIDW": ("MCIDEVICEID", [("LPCWSTR", "pszElement")]),
    "mciGetDriverData": ("DWORD_PTR", [("HANDLE", "hDevice")]),
    "mciGetErrorStringA": ("BOOL", [("UINT", "uError"), ("LPSTR", "pszText"), ("UINT", "cchText")]),
    "mciGetErrorStringW": ("BOOL", [("UINT", "uError"), ("LPWSTR", "pszText"), ("UINT", "cchText")]),
    "mciGetYieldProc": ("LPVOID", [("HANDLE", "hDevice"), ("LPDWORD", "pdwYieldData")]),
    "mciLoadCommandResource": ("HANDLE", [("HINSTANCE", "hInst"), ("LPCWSTR", "lpResId"), ("UINT", "wType")]),
    "mciSendCommandA": ("UINT", [("MCIDEVICEID", "mciId"), ("UINT", "uMsg"), ("DWORD_PTR", "dwParam1"), ("DWORD_PTR", "dwParam2")]),
    "mciSendCommandW": ("UINT", [("MCIDEVICEID", "mciId"), ("UINT", "uMsg"), ("DWORD_PTR", "dwParam1"), ("DWORD_PTR", "dwParam2")]),
    "mciSendStringA": ("UINT", [("LPCSTR", "pszCommand"), ("LPSTR", "pszReturn"), ("UINT", "cchReturn"), ("HANDLE", "hwndCallback")]),
    "mciSendStringW": ("UINT", [("LPCWSTR", "pszCommand"), ("LPWSTR", "pszReturn"), ("UINT", "cchReturn"), ("HANDLE", "hwndCallback")]),
    "mciSetDriverData": ("DWORD_PTR", [("HANDLE", "hDevice"), ("DWORD_PTR", "dwData")]),
    "mciSetYieldProc": ("void", [("HANDLE", "hDevice"), ("LPVOID", "fpYieldProc"), ("DWORD", "dwYieldData")]),
    "mid32Message": ("LRESULT", [("UINT", "uDeviceID"), ("UINT", "uMsg"), ("DWORD_PTR", "dwParam1"), ("DWORD_PTR", "dwParam2")]),
    "midiConnect": ("UINT", [("HANDLE", "hMidi"), ("HANDLE", "hMidiOut"), ("LPVOID", "pReserved")]),
    "midiDisconnect": ("UINT", [("HANDLE", "hMidi"), ("HANDLE", "hMidiOut"), ("LPVOID", "pReserved")]),
    "midiInAddBuffer": ("UINT", [("HANDLE", "hMidiIn"), ("LPVOID", "pMidiInHdr"), ("UINT", "cbMidiInHdr")]),
    "midiInClose": ("UINT", [("HANDLE", "hMidiIn")]),
    "midiInGetDevCapsA": ("UINT", [("UINT_PTR", "uDeviceID"), ("LPVOID", "pmic"), ("UINT", "cbmic")]),
    "midiInGetDevCapsW": ("UINT", [("UINT_PTR", "uDeviceID"), ("LPVOID", "pmic"), ("UINT", "cbmic")]),
    "midiInGetErrorTextA": ("UINT", [("UINT", "uError"), ("LPSTR", "pszText"), ("UINT", "cchText")]),
    "midiInGetErrorTextW": ("UINT", [("UINT", "uError"), ("LPWSTR", "pszText"), ("UINT", "cchText")]),
    "midiInGetID": ("UINT", [("HANDLE", "hMidiIn"), ("LPUINT", "puDeviceID")]),
    "midiInGetNumDevs": ("UINT", []),
    "midiInMessage": ("DWORD", [("HANDLE", "hMidiIn"), ("UINT", "uMsg"), ("DWORD_PTR", "dwParam1"), ("DWORD_PTR", "dwParam2")]),
    "midiInOpen": ("UINT", [("LPHANDLE", "phMidiIn"), ("UINT", "uDeviceID"), ("DWORD_PTR", "dwCallback"), ("DWORD_PTR", "dwCallbackInstance"), ("DWORD", "dwFlags")]),
    "midiInPrepareHeader": ("UINT", [("HANDLE", "hMidiIn"), ("LPVOID", "pMidiInHdr"), ("UINT", "cbMidiInHdr")]),
    "midiInReset": ("UINT", [("HANDLE", "hMidiIn")]),
    "midiInStart": ("UINT", [("HANDLE", "hMidiIn")]),
    "midiInStop": ("UINT", [("HANDLE", "hMidiIn")]),
    "midiInUnprepareHeader": ("UINT", [("HANDLE", "hMidiIn"), ("LPVOID", "pMidiInHdr"), ("UINT", "cbMidiInHdr")]),
    "midiOutCacheDrumPatches": ("UINT", [("HANDLE", "hMidiOut"), ("UINT", "uPatch"), ("LPVOID", "pwkya"), ("UINT", "uFlags")]),
    "midiOutCachePatches": ("UINT", [("HANDLE", "hMidiOut"), ("UINT", "uBank"), ("LPVOID", "pwpa"), ("UINT", "uFlags")]),
    "midiOutClose": ("UINT", [("HANDLE", "hMidiOut")]),
    "midiOutGetDevCapsA": ("UINT", [("UINT_PTR", "uDeviceID"), ("LPVOID", "pmoc"), ("UINT", "cbmoc")]),
    "midiOutGetDevCapsW": ("UINT", [("UINT_PTR", "uDeviceID"), ("LPVOID", "pmoc"), ("UINT", "cbmoc")]),
    "midiOutGetErrorTextA": ("UINT", [("UINT", "uError"), ("LPSTR", "pszText"), ("UINT", "cchText")]),
    "midiOutGetErrorTextW": ("UINT", [("UINT", "uError"), ("LPWSTR", "pszText"), ("UINT", "cchText")]),
    "midiOutGetID": ("UINT", [("HANDLE", "hMidiOut"), ("LPUINT", "puDeviceID")]),
    "midiOutGetNumDevs": ("UINT", []),
    "midiOutGetVolume": ("UINT", [("HANDLE", "hMidiOut"), ("LPDWORD", "pdwVolume")]),
    "midiOutLongMsg": ("UINT", [("HANDLE", "hMidiOut"), ("LPVOID", "pMidiOutHdr"), ("UINT", "cbMidiOutHdr")]),
    "midiOutMessage": ("DWORD", [("HANDLE", "hMidiOut"), ("UINT", "uMsg"), ("DWORD_PTR", "dwParam1"), ("DWORD_PTR", "dwParam2")]),
    "midiOutOpen": ("UINT", [("LPHANDLE", "phMidiOut"), ("UINT", "uDeviceID"), ("DWORD_PTR", "dwCallback"), ("DWORD_PTR", "dwCallbackInstance"), ("DWORD", "dwFlags")]),
    "midiOutPrepareHeader": ("UINT", [("HANDLE", "hMidiOut"), ("LPVOID", "pMidiOutHdr"), ("UINT", "cbMidiOutHdr")]),
    "midiOutReset": ("UINT", [("HANDLE", "hMidiOut")]),
    "midiOutSetVolume": ("UINT", [("HANDLE", "hMidiOut"), ("DWORD", "dwVolume")]),
    "midiOutShortMsg": ("UINT", [("HANDLE", "hMidiOut"), ("DWORD", "dwMsg")]),
    "midiOutUnprepareHeader": ("UINT", [("HANDLE", "hMidiOut"), ("LPVOID", "pMidiOutHdr"), ("UINT", "cbMidiOutHdr")]),
    "midiStreamClose": ("UINT", [("HANDLE", "hStream")]),
    "midiStreamOpen": ("UINT", [("LPHANDLE", "phStream"), ("LPUINT", "puDeviceID"), ("UINT", "cMidi"), ("DWORD_PTR", "dwCallback"), ("DWORD_PTR", "dwInstance"), ("DWORD", "fdwOpen")]),
    "midiStreamOut": ("UINT", [("HANDLE", "hMidiStream"), ("LPVOID", "pMidiHdr"), ("UINT", "cbMidiHdr")]),
    "midiStreamPause": ("UINT", [("HANDLE", "hStream")]),
    "midiStreamPosition": ("UINT", [("HANDLE", "hStream"), ("LPVOID", "pmmt"), ("UINT", "cbmmt")]),
    "midiStreamProperty": ("UINT", [("HANDLE", "hStream"), ("LPBYTE", "lpPropertyData"), ("DWORD", "dwProperty")]),
    "midiStreamRestart": ("UINT", [("HANDLE", "hStream")]),
    "midiStreamStop": ("UINT", [("HANDLE", "hStream")]),
    "mixerClose": ("UINT", [("HANDLE", "hmx")]),
    "mixerGetControlDetailsA": ("UINT", [("HANDLE", "hmxobj"), ("LPVOID", "pmxcd"), ("DWORD", "fdwDetails")]),
    "mixerGetControlDetailsW": ("UINT", [("HANDLE", "hmxobj"), ("LPVOID", "pmxcd"), ("DWORD", "fdwDetails")]),
    "mixerGetDevCapsA": ("UINT", [("UINT_PTR", "uMxId"), ("LPVOID", "pmxcaps"), ("UINT", "cbmxcaps")]),
    "mixerGetDevCapsW": ("UINT", [("UINT_PTR", "uMxId"), ("LPVOID", "pmxcaps"), ("UINT", "cbmxcaps")]),
    "mixerGetID": ("UINT", [("HANDLE", "hmxobj"), ("LPUINT", "puMxId"), ("DWORD", "fdwId")]),
    "mixerGetLineControlsA": ("UINT", [("HANDLE", "hmxobj"), ("LPVOID", "pmxlc"), ("DWORD", "fdwControls")]),
    "mixerGetLineControlsW": ("UINT", [("HANDLE", "hmxobj"), ("LPVOID", "pmxlc"), ("DWORD", "fdwControls")]),
    "mixerGetLineInfoA": ("UINT", [("HANDLE", "hmxobj"), ("LPVOID", "pmxl"), ("DWORD", "fdwInfo")]),
    "mixerGetLineInfoW": ("UINT", [("HANDLE", "hmxobj"), ("LPVOID", "pmxl"), ("DWORD", "fdwInfo")]),
    "mixerGetNumDevs": ("UINT", []),
    "mixerMessage": ("DWORD", [("HANDLE", "hmx"), ("UINT", "uMsg"), ("DWORD_PTR", "dwParam1"), ("DWORD_PTR", "dwParam2")]),
    "mixerOpen": ("UINT", [("LPHANDLE", "phmx"), ("UINT", "uMxId"), ("DWORD_PTR", "dwCallback"), ("DWORD_PTR", "dwInstance"), ("DWORD", "fdwOpen")]),
    "mixerSetControlDetails": ("UINT", [("HANDLE", "hmxobj"), ("LPVOID", "pmxcd"), ("DWORD", "fdwDetails")]),
    "mmDrvInstall": ("UINT", [("LPVOID", "pDriverInfo"), ("UINT", "uDriverID"), ("UINT", "uMsg")]),
    "mmGetCurrentTask": ("HANDLE", []),
    "mmTaskBlock": ("void", [("HANDLE", "hTask")]),
    "mmTaskCreate": ("HANDLE", [("LPVOID", "lpfn"), ("LPVOID", "lpParam"), ("UINT", "uPriority")]),
    "mmTaskSignal": ("DWORD", [("HANDLE", "hTask")]),
    "mmTaskYield": ("void", []),
    "mmioAdvance": ("LRESULT", [("HANDLE", "hmmio"), ("LPVOID", "pmmioinfo"), ("UINT", "fuAdvance")]),
    "mmioAscend": ("MMRESULT", [("HANDLE", "hmmio"), ("LPVOID", "pmmcki"), ("UINT", "fuAscend")]),
    "mmioClose": ("MMRESULT", [("HANDLE", "hmmio"), ("UINT", "fuClose")]),
    "mmioCreateChunk": ("MMRESULT", [("HANDLE", "hmmio"), ("LPVOID", "pmmcki"), ("UINT", "fuCreate")]),
    "mmioDescend": ("MMRESULT", [("HANDLE", "hmmio"), ("LPVOID", "pmmcki"), ("LPVOID", "pmmckiParent"), ("UINT", "fuDescend")]),
    "mmioFlush": ("MMRESULT", [("HANDLE", "hmmio"), ("UINT", "fuFlush")]),
    "mmioGetInfo": ("MMRESULT", [("HANDLE", "hmmio"), ("LPVOID", "pmmioinfo"), ("UINT", "fuInfo")]),
    "mmioInstallIOProc16": ("LPVOID", [("FOURCC", "fccIOProc"), ("LPVOID", "pIOProc"), ("DWORD", "dwFlags")]),
    "mmioInstallIOProcA": ("LPVOID", [("FOURCC", "fccIOProc"), ("LPVOID", "pIOProc"), ("DWORD", "dwFlags")]),
    "mmioInstallIOProcW": ("LPVOID", [("FOURCC", "fccIOProc"), ("LPVOID", "pIOProc"), ("DWORD", "dwFlags")]),
    "mmioOpenA": ("HANDLE", [("LPSTR", "szFilename"), ("LPVOID", "pmmioinfo"), ("DWORD", "dwOpenFlags")]),
    "mmioOpenW": ("HANDLE", [("LPWSTR", "szFilename"), ("LPVOID", "pmmioinfo"), ("DWORD", "dwOpenFlags")]),
    "mmioRead": ("LRESULT", [("HANDLE", "hmmio"), ("LPSTR", "pch"), ("LONG", "cch")]),
    "mmioRenameA": ("MMRESULT", [("LPCSTR", "szFilename"), ("LPCSTR", "szNewFilename"), ("LPVOID", "pmmioinfo"), ("DWORD", "dwRenameFlags")]),
    "mmioRenameW": ("MMRESULT", [("LPCWSTR", "szFilename"), ("LPCWSTR", "szNewFilename"), ("LPVOID", "pmmioinfo"), ("DWORD", "dwRenameFlags")]),
    "mmioSeek": ("LONG", [("HANDLE", "hmmio"), ("LONG", "lOffset"), ("int", "iOrigin")]),
    "mmioSendMessage": ("LRESULT", [("HANDLE", "hmmio"), ("UINT", "uMsg"), ("LPARAM", "lParam1"), ("LPARAM", "lParam2")]),
    "mmioSetBuffer": ("MMRESULT", [("HANDLE", "hmmio"), ("LPSTR", "pchBuffer"), ("LONG", "cchBuffer"), ("UINT", "fuBuffer")]),
    "mmioSetInfo": ("MMRESULT", [("HANDLE", "hmmio"), ("LPVOID", "pmmioinfo"), ("UINT", "fuInfo")]),
    "mmioStringToFOURCCA": ("FOURCC", [("LPSTR", "sz"), ("UINT", "cch")]),
    "mmioStringToFOURCCW": ("FOURCC", [("LPWSTR", "sz"), ("UINT", "cch")]),
    "mmioWrite": ("LRESULT", [("HANDLE", "hmmio"), ("LPCSTR", "pch"), ("LONG", "cch")]),
    "mmsystemGetVersion": ("UINT", []),
    "mod32Message": ("LRESULT", [("UINT", "uDeviceID"), ("UINT", "uMsg"), ("DWORD_PTR", "dwParam1"), ("DWORD_PTR", "dwParam2")]),
    "mxd32Message": ("LRESULT", [("UINT", "uDeviceID"), ("UINT", "uMsg"), ("DWORD_PTR", "dwParam1"), ("DWORD_PTR", "dwParam2")]),
    "sndPlaySoundA": ("BOOL", [("LPCSTR", "pszSound"), ("UINT", "fuSound")]),
    "sndPlaySoundW": ("BOOL", [("LPCWSTR", "pszSound"), ("UINT", "fuSound")]),
    "tid32Message": ("LRESULT", [("UINT", "uDeviceID"), ("UINT", "uMsg"), ("DWORD_PTR", "dwParam1"), ("DWORD_PTR", "dwParam2")]),
    "timeBeginPeriod": ("UINT", [("UINT", "uPeriod")]),
    "timeEndPeriod": ("UINT", [("UINT", "uPeriod")]),
    "timeGetDevCaps": ("UINT", [("LPVOID", "ptc"), ("UINT", "cbtc")]),
    "timeGetSystemTime": ("UINT", [("LPVOID", "pmmt"), ("UINT", "cbmmt")]),
    "timeGetTime": ("DWORD", []),
    "timeKillEvent": ("UINT", [("UINT", "uTimerID")]),
    "timeSetEvent": ("UINT", [("UINT", "uDelay"), ("UINT", "uResolution"), ("LPVOID", "lpTimeProc"), ("DWORD_PTR", "dwUser"), ("UINT", "fuEvent")]),
    "waveInAddBuffer": ("UINT", [("HANDLE", "hWaveIn"), ("LPVOID", "pWaveInHdr"), ("UINT", "cbWaveInHdr")]),
    "waveInClose": ("UINT", [("HANDLE", "hWaveIn")]),
    "waveInGetDevCapsA": ("UINT", [("UINT_PTR", "uDeviceID"), ("LPVOID", "pwic"), ("UINT", "cbwic")]),
    "waveInGetDevCapsW": ("UINT", [("UINT_PTR", "uDeviceID"), ("LPVOID", "pwic"), ("UINT", "cbwic")]),
    "waveInGetErrorTextA": ("UINT", [("UINT", "uError"), ("LPSTR", "pszText"), ("UINT", "cchText")]),
    "waveInGetErrorTextW": ("UINT", [("UINT", "uError"), ("LPWSTR", "pszText"), ("UINT", "cchText")]),
    "waveInGetID": ("UINT", [("HANDLE", "hWaveIn"), ("LPUINT", "puDeviceID")]),
    "waveInGetNumDevs": ("UINT", []),
    "waveInGetPosition": ("UINT", [("HANDLE", "hWaveIn"), ("LPVOID", "pmmt"), ("UINT", "cbmmt")]),
    "waveInMessage": ("DWORD", [("HANDLE", "hWaveIn"), ("UINT", "uMsg"), ("DWORD_PTR", "dwParam1"), ("DWORD_PTR", "dwParam2")]),
    "waveInOpen": ("UINT", [("LPHANDLE", "phWaveIn"), ("UINT", "uDeviceID"), ("LPVOID", "pwfx"), ("DWORD_PTR", "dwCallback"), ("DWORD_PTR", "dwCallbackInstance"), ("DWORD", "fdwOpen")]),
    "waveInPrepareHeader": ("UINT", [("HANDLE", "hWaveIn"), ("LPVOID", "pWaveInHdr"), ("UINT", "cbWaveInHdr")]),
    "waveInReset": ("UINT", [("HANDLE", "hWaveIn")]),
    "waveInStart": ("UINT", [("HANDLE", "hWaveIn")]),
    "waveInStop": ("UINT", [("HANDLE", "hWaveIn")]),
    "waveInUnprepareHeader": ("UINT", [("HANDLE", "hWaveIn"), ("LPVOID", "pWaveInHdr"), ("UINT", "cbWaveInHdr")]),
    "waveOutBreakLoop": ("UINT", [("HANDLE", "hWaveOut")]),
    "waveOutClose": ("UINT", [("HANDLE", "hWaveOut")]),
    "waveOutGetDevCapsA": ("UINT", [("UINT_PTR", "uDeviceID"), ("LPVOID", "pwoc"), ("UINT", "cbwoc")]),
    "waveOutGetDevCapsW": ("UINT", [("UINT_PTR", "uDeviceID"), ("LPVOID", "pwoc"), ("UINT", "cbwoc")]),
    "waveOutGetErrorTextA": ("UINT", [("UINT", "uError"), ("LPSTR", "pszText"), ("UINT", "cchText")]),
    "waveOutGetErrorTextW": ("UINT", [("UINT", "uError"), ("LPWSTR", "pszText"), ("UINT", "cchText")]),
    "waveOutGetID": ("UINT", [("HANDLE", "hWaveOut"), ("LPUINT", "puDeviceID")]),
    "waveOutGetNumDevs": ("UINT", []),
    "waveOutGetPitch": ("UINT", [("HANDLE", "hWaveOut"), ("LPDWORD", "pdwPitch")]),
    "waveOutGetPlaybackRate": ("UINT", [("HANDLE", "hWaveOut"), ("LPDWORD", "pdwRate")]),
    "waveOutGetPosition": ("UINT", [("HANDLE", "hWaveOut"), ("LPVOID", "pmmt"), ("UINT", "cbmmt")]),
    "waveOutGetVolume": ("UINT", [("HANDLE", "hWaveOut"), ("LPDWORD", "pdwVolume")]),
    "waveOutMessage": ("DWORD", [("HANDLE", "hWaveOut"), ("UINT", "uMsg"), ("DWORD_PTR", "dwParam1"), ("DWORD_PTR", "dwParam2")]),
    "waveOutOpen": ("UINT", [("LPHANDLE", "phWaveOut"), ("UINT", "uDeviceID"), ("LPVOID", "pwfx"), ("DWORD_PTR", "dwCallback"), ("DWORD_PTR", "dwCallbackInstance"), ("DWORD", "fdwOpen")]),
    "waveOutPause": ("UINT", [("HANDLE", "hWaveOut")]),
    "waveOutPrepareHeader": ("UINT", [("HANDLE", "hWaveOut"), ("LPVOID", "pWaveOutHdr"), ("UINT", "cbWaveOutHdr")]),
    "waveOutReset": ("UINT", [("HANDLE", "hWaveOut")]),
    "waveOutRestart": ("UINT", [("HANDLE", "hWaveOut")]),
    "waveOutSetPitch": ("UINT", [("HANDLE", "hWaveOut"), ("DWORD", "dwPitch")]),
    "waveOutSetPlaybackRate": ("UINT", [("HANDLE", "hWaveOut"), ("DWORD", "dwRate")]),
    "waveOutSetVolume": ("UINT", [("HANDLE", "hWaveOut"), ("DWORD", "dwVolume")]),
    "waveOutUnprepareHeader": ("UINT", [("HANDLE", "hWaveOut"), ("LPVOID", "pWaveOutHdr"), ("UINT", "cbWaveOutHdr")]),
    "waveOutWrite": ("UINT", [("HANDLE", "hWaveOut"), ("LPVOID", "pWaveOutHdr"), ("UINT", "cbWaveOutHdr")]),
    "wid32Message": ("LRESULT", [("UINT", "uDeviceID"), ("UINT", "uMsg"), ("DWORD_PTR", "dwParam1"), ("DWORD_PTR", "dwParam2")]),
    "wod32Message": ("LRESULT", [("UINT", "uDeviceID"), ("UINT", "uMsg"), ("DWORD_PTR", "dwParam1"), ("DWORD_PTR", "dwParam2")]),
}

# Types that need typedef in generated code (not in Windows.h base)
TYPEDEFS = """
#ifndef _MMSYSTEM_TYPEDEFS_
#define _MMSYSTEM_TYPEDEFS_
typedef UINT MMRESULT;
typedef DWORD FOURCC;
typedef UINT MCIDEVICEID;
typedef void* HDRVR;
#endif
"""


def main():
    repo_root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    def_path = os.path.join(repo_root, "src", "addons", "display_commander", "proxy_dll", "exports.def")
    out_path = os.path.join(repo_root, "src", "addons", "display_commander", "proxy_dll", "winmm_proxy.cpp")

    names = []
    with open(def_path, "r", encoding="utf-8") as f:
        in_winmm = False
        for line in f:
            line_stripped = line.strip()
            if "; winmm.dll" in line_stripped:
                in_winmm = True
                continue
            if in_winmm:
                if line_stripped.startswith(";") and "ReShade" in line_stripped:
                    break
                if line_stripped.startswith(";"):
                    continue
                m = re.match(r"^(\w+)\s+", line_stripped)
                if m:
                    name = m.group(1)
                    if name != "PRIVATE":
                        names.append(name)

    # Dedupe and keep order
    seen = set()
    unique = []
    for n in names:
        if n not in seen:
            seen.add(n)
            unique.append(n)
    names = unique

    lines = [
        "/*",
        " * WinMM Proxy Functions",
        " * Forwards winmm calls to the real system winmm.dll (or winmmHooked.dll in same dir).",
        " * Generated by scripts/gen_winmm_proxy.py - do not edit by hand.",
        " */",
        "",
        "#ifndef WIN32_LEAN_AND_MEAN",
        "#define WIN32_LEAN_AND_MEAN",
        "#endif",
        "#include <Windows.h>",
        "#include <string>",
        "",
        "#include \"winmm_proxy_init.hpp\"",
        "",
        TYPEDEFS.strip(),
        "",
        "static HMODULE g_winmm_module = nullptr;",
        "",
        "static bool LoadRealWinMM() {",
        "    if (g_winmm_module != nullptr) return true;",
        "    HMODULE hSelf = GetModuleHandleW(L\"winmm.dll\");",
        "    if (!hSelf) return false;",
        "    WCHAR self_path[MAX_PATH];",
        "    if (GetModuleFileNameW(hSelf, self_path, MAX_PATH) == 0) return false;",
        "    std::wstring dir(self_path);",
        "    size_t last = dir.find_last_of(L\"\\\\/\");",
        "    if (last != std::wstring::npos) dir.resize(last + 1);",
        "    std::wstring hooked = dir + L\"winmmHooked.dll\";",
        "    if (GetFileAttributesW(hooked.c_str()) != INVALID_FILE_ATTRIBUTES) {",
        "        g_winmm_module = LoadLibraryW(hooked.c_str());",
        "    }",
        "    if (g_winmm_module == nullptr) {",
        "        WCHAR system_path[MAX_PATH];",
        "        if (GetSystemDirectoryW(system_path, MAX_PATH) == 0) return false;",
        "        std::wstring path = std::wstring(system_path) + L\"\\\\winmm.dll\";",
        "        g_winmm_module = LoadLibraryW(path.c_str());",
        "    }",
        "    return g_winmm_module != nullptr;",
        "}",
        "",
        "void LoadRealWinMMFromDllMain() { (void)LoadRealWinMM(); }",
        "",
    ]

    for name in names:
        if name not in SIGS:
            raise SystemExit("Missing signature for: " + name)
        ret, params = SIGS[name]
        # MMRESULT in SIGS we emit as UINT in generated code for ABI
        ret_emit = "UINT" if ret == "MMRESULT" else ret
        param_decl = ", ".join("%s %s" % (t, n) for t, n in params)
        param_names = ", ".join(n for _, n in params)
        if params:
            param_list = ", ".join(t for t, _ in params)
        else:
            param_list = "void"
        param_list_emit = param_list.replace("MMRESULT", "UINT")
        ret_void = ret == "void"
        if ret_void:
            call = "if (fn) fn(%s);" % param_names if param_names else "if (fn) fn();"
            ret_expr = ""
        else:
            call = "return fn(%s);" % param_names if param_names else "return fn();"
            ret_expr = "return (%s)0;" % ret_emit

        lines.append("extern \"C\" %s WINAPI %s(%s) {" % (ret_emit, name, param_decl))
        lines.append("    if (!LoadRealWinMM()) %s" % ("return;" if ret_void else ("return (%s)0;" % ret_emit)))
        lines.append("    typedef %s (WINAPI *PFN)(%s);" % (ret_emit, param_list_emit))
        lines.append("    PFN fn = (PFN)GetProcAddress(g_winmm_module, \"%s\");" % name)
        lines.append("    if (!fn) %s" % ("return;" if ret_void else ("return (%s)0;" % ret_emit)))
        lines.append("    %s" % call)
        lines.append("}")
        lines.append("")

    with open(out_path, "w", encoding="utf-8") as f:
        f.write("\n".join(lines))

    print("Wrote %s (%d stubs)" % (out_path, len(names)))


if __name__ == "__main__":
    main()
