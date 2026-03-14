/*
 * Official WinMM API signatures for proxy forwarding.
 * Do not include mmeapi.h, mmiscapi.h, timeapi.h, joystickapi.h, or digitalv.h here
 * (conflict with extern "C" proxy symbols). Opaque handles (HDRVR, HMIDIIN, HMIDIOUT, etc.)
 * and WinMM-specific struct pointers as void* / LPVOID for ABI compatibility.
 * References: https://learn.microsoft.com/en-us/windows/win32/api/_multimedia/ ,
 * mmeapi.h, mmiscapi.h, timeapi.h, joystickapi.h, digitalv.h
 */
#ifndef DISPLAY_COMMANDER_WINMM_PROXY_HPP
#define DISPLAY_COMMANDER_WINMM_PROXY_HPP

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Function pointer types for forwarding to real winmm.dll.
 * Names match Microsoft headers; parameter types use void* for opaque handles and
 * WinMM struct pointers. Grouped in blocks of 20 for cross-reference with winmm_proxy.cpp.
 */

/* 1-20 */
typedef LRESULT (WINAPI* PFN_CloseDriver)(void* hDriver, LPARAM lParam1, LPARAM lParam2);
typedef LRESULT (WINAPI* PFN_DefDriverProc)(DWORD_PTR dwDriverIdentifier, void* hDrv, UINT uMsg,
                                            LPARAM lParam1, LPARAM lParam2);
typedef BOOL (WINAPI* PFN_DriverCallback)(DWORD_PTR dwCallback, DWORD dwFlags, void* hDevice,
                                          DWORD dwMsg, DWORD_PTR dwUser, DWORD_PTR dwParam1,
                                          DWORD_PTR dwParam2);
typedef LRESULT (WINAPI* PFN_DrvClose)(void* hDriver, LPARAM lParam1, LPARAM lParam2);
typedef LRESULT (WINAPI* PFN_DrvDefDriverProc)(DWORD_PTR dwDriverIdentifier, void* hDrv, UINT uMsg,
                                                LPARAM lParam1, LPARAM lParam2);
typedef HMODULE (WINAPI* PFN_DrvGetModuleHandle)(void* hDriver);
typedef LRESULT (WINAPI* PFN_DrvOpen)(LPCWSTR szDriverName, LPCWSTR szSectionName, LPARAM lParam);
typedef LRESULT (WINAPI* PFN_DrvOpenA)(LPCSTR szDriverName, LPCSTR szSectionName, LPARAM lParam);
typedef LRESULT (WINAPI* PFN_DrvSendMessage)(void* hDriver, UINT uMsg, LPARAM lParam1, LPARAM lParam2);
typedef UINT (WINAPI* PFN_GetDriverFlags)(void* hDriver);
typedef HMODULE (WINAPI* PFN_GetDriverModuleHandle)(void* hDriver);
typedef void* (WINAPI* PFN_OpenDriver)(LPCWSTR szDriverName, LPCWSTR szSectionName, LPARAM lParam);
typedef void* (WINAPI* PFN_OpenDriverA)(LPCSTR szDriverName, LPCSTR szSectionName, LPARAM lParam);
typedef BOOL (WINAPI* PFN_PlaySound)(LPCWSTR pszSound, HMODULE hmod, DWORD fdwSound);
typedef BOOL (WINAPI* PFN_PlaySoundA)(LPCSTR pszSound, HMODULE hmod, DWORD fdwSound);
typedef BOOL (WINAPI* PFN_PlaySoundW)(LPCWSTR pszSound, HMODULE hmod, DWORD fdwSound);
typedef LRESULT (WINAPI* PFN_SendDriverMessage)(void* hDriver, UINT uMsg, LPARAM lParam1, LPARAM lParam2);
typedef UINT (WINAPI* PFN_auxGetDevCapsA)(UINT_PTR uDeviceID, LPVOID pac, UINT cbac);
typedef UINT (WINAPI* PFN_auxGetDevCapsW)(UINT_PTR uDeviceID, LPVOID pac, UINT cbac);
typedef UINT (WINAPI* PFN_auxGetNumDevs)(void);
typedef UINT (WINAPI* PFN_auxGetVolume)(UINT uDeviceID, LPDWORD pdwVolume);
typedef DWORD (WINAPI* PFN_auxOutMessage)(UINT uDeviceID, UINT uMsg, DWORD_PTR dwInstance,
                                          DWORD_PTR dwParam1, DWORD_PTR dwParam2);
typedef VOID (WINAPI* PFN_auxSetVolume)(UINT uDeviceID, DWORD dwVolume);

/* 21-40 */
typedef VOID (WINAPI* PFN_joyConfigChanged)(DWORD dwFlags);
typedef UINT (WINAPI* PFN_joyGetDevCapsA)(UINT_PTR uJoyID, LPVOID pjc, UINT cbjc);
typedef UINT (WINAPI* PFN_joyGetDevCapsW)(UINT_PTR uJoyID, LPVOID pjc, UINT cbjc);
typedef UINT (WINAPI* PFN_joyGetNumDevs)(void);
typedef UINT (WINAPI* PFN_joyGetPos)(UINT uJoyID, LPVOID pji);
typedef UINT (WINAPI* PFN_joyGetPosEx)(UINT uJoyID, LPVOID pji);
typedef UINT (WINAPI* PFN_joyGetThreshold)(UINT uJoyID, LPUINT puThreshold);
typedef UINT (WINAPI* PFN_joyReleaseCapture)(UINT uJoyID);
typedef UINT (WINAPI* PFN_joySetCapture)(HWND hwnd, UINT uJoyID, UINT uPeriod, BOOL fChanged);
typedef UINT (WINAPI* PFN_joySetThreshold)(UINT uJoyID, UINT uThreshold);
typedef VOID (WINAPI* PFN_mciDriverNotify)(HWND hwndCallback, UINT uDeviceID, UINT uStatus);
typedef BOOL (WINAPI* PFN_mciDriverYield)(UINT uDeviceID);
typedef BOOL (WINAPI* PFN_mciExecute)(LPCSTR pszCommand);
typedef UINT (WINAPI* PFN_mciFreeCommandResource)(UINT uResource);
typedef HTASK (WINAPI* PFN_mciGetCreatorTask)(UINT uDeviceID);
typedef UINT (WINAPI* PFN_mciGetDeviceIDA)(LPCSTR pszElement);
typedef UINT (WINAPI* PFN_mciGetDeviceIDFromElementIDA)(DWORD dwElementID, LPCSTR pszType);
typedef UINT (WINAPI* PFN_mciGetDeviceIDFromElementIDW)(DWORD dwElementID, LPCWSTR pszType);
typedef UINT (WINAPI* PFN_mciGetDeviceIDW)(LPCWSTR pszElement);
typedef DWORD_PTR (WINAPI* PFN_mciGetDriverData)(UINT uDeviceID);
typedef BOOL (WINAPI* PFN_mciGetErrorStringA)(DWORD fdwError, LPSTR pszText, UINT cchText);
typedef BOOL (WINAPI* PFN_mciGetErrorStringW)(DWORD fdwError, LPWSTR pszText, UINT cchText);
typedef LPVOID (WINAPI* PFN_mciGetYieldProc)(UINT uDeviceID, LPDWORD pdwYieldData);
typedef UINT (WINAPI* PFN_mciLoadCommandResource)(HINSTANCE hInstance, LPCWSTR lpResId, UINT uType);

/* 41-60 */
typedef UINT (WINAPI* PFN_mciSendCommandA)(UINT uDeviceID, UINT uMsg, DWORD_PTR dwParam1,
                                            DWORD_PTR dwParam2);
typedef UINT (WINAPI* PFN_mciSendCommandW)(UINT uDeviceID, UINT uMsg, DWORD_PTR dwParam1,
                                            DWORD_PTR dwParam2);
typedef UINT (WINAPI* PFN_mciSendStringA)(LPCSTR lpszCommand, LPSTR lpszReturnString,
                                           UINT cchReturn, HANDLE hwndCallback);
typedef UINT (WINAPI* PFN_mciSendStringW)(LPCWSTR lpszCommand, LPWSTR lpszReturnString,
                                           UINT cchReturn, HANDLE hwndCallback);
typedef UINT (WINAPI* PFN_mciSetDriverData)(UINT uDeviceID, DWORD_PTR dwData);
typedef UINT (WINAPI* PFN_mciSetYieldProc)(UINT uDeviceID, LPVOID fpYieldProc, DWORD dwYieldData);
typedef UINT (WINAPI* PFN_midiConnect)(void* hMidi, void* hmo, LPVOID pReserved);
typedef UINT (WINAPI* PFN_midiDisconnect)(void* hMidi, void* hmo, LPVOID pReserved);
typedef UINT (WINAPI* PFN_midiInAddBuffer)(void* hMidiIn, LPVOID pMidiInHdr, UINT cbMidiInHdr);
typedef UINT (WINAPI* PFN_midiInClose)(void* hMidiIn);
typedef UINT (WINAPI* PFN_midiInGetDevCapsA)(UINT_PTR uDeviceID, LPVOID pmic, UINT cbmic);
typedef UINT (WINAPI* PFN_midiInGetDevCapsW)(UINT_PTR uDeviceID, LPVOID pmic, UINT cbmic);
typedef BOOL (WINAPI* PFN_midiInGetErrorTextA)(UINT uErr, LPSTR pszText, UINT cchText);
typedef BOOL (WINAPI* PFN_midiInGetErrorTextW)(UINT uErr, LPWSTR pszText, UINT cchText);
typedef UINT (WINAPI* PFN_midiInGetID)(void* hMidiIn, LPUINT puDeviceID);
typedef UINT (WINAPI* PFN_midiInGetNumDevs)(void);
typedef DWORD (WINAPI* PFN_midiInMessage)(void* hMidiIn, UINT uMsg, DWORD_PTR dwParam1,
                                           DWORD_PTR dwParam2);
typedef UINT (WINAPI* PFN_midiInOpen)(void* phMidiIn, UINT uDeviceID, DWORD_PTR dwCallback,
                                       DWORD_PTR dwCallbackInstance, DWORD fdwOpen);
typedef UINT (WINAPI* PFN_midiInPrepareHeader)(void* hMidiIn, LPVOID pMidiInHdr, UINT cbMidiInHdr);
typedef UINT (WINAPI* PFN_midiInReset)(void* hMidiIn);
typedef UINT (WINAPI* PFN_midiInStart)(void* hMidiIn);
typedef UINT (WINAPI* PFN_midiInStop)(void* hMidiIn);
typedef UINT (WINAPI* PFN_midiInUnprepareHeader)(void* hMidiIn, LPVOID pMidiInHdr, UINT cbMidiInHdr);

/* 61-80 */
typedef UINT (WINAPI* PFN_midiOutCacheDrumPatches)(void* hMidiOut, UINT uPatch, LPVOID pwkya,
                                                    UINT uFlags);
typedef UINT (WINAPI* PFN_midiOutCachePatches)(void* hMidiOut, UINT uBank, LPVOID pwpa, UINT uFlags);
typedef UINT (WINAPI* PFN_midiOutClose)(void* hMidiOut);
typedef UINT (WINAPI* PFN_midiOutGetDevCapsA)(UINT_PTR uDeviceID, LPVOID pmoc, UINT cbmoc);
typedef UINT (WINAPI* PFN_midiOutGetDevCapsW)(UINT_PTR uDeviceID, LPVOID pmoc, UINT cbmoc);
typedef BOOL (WINAPI* PFN_midiOutGetErrorTextA)(UINT uErr, LPSTR pszText, UINT cchText);
typedef BOOL (WINAPI* PFN_midiOutGetErrorTextW)(UINT uErr, LPWSTR pszText, UINT cchText);
typedef UINT (WINAPI* PFN_midiOutGetID)(void* hMidiOut, LPUINT puDeviceID);
typedef UINT (WINAPI* PFN_midiOutGetNumDevs)(void);
typedef UINT (WINAPI* PFN_midiOutGetVolume)(void* hMidiOut, LPDWORD pdwVolume);
typedef UINT (WINAPI* PFN_midiOutLongMsg)(void* hMidiOut, LPVOID pMidiOutHdr, UINT cbMidiOutHdr);
typedef DWORD (WINAPI* PFN_midiOutMessage)(void* hMidiOut, UINT uMsg, DWORD_PTR dwParam1,
                                            DWORD_PTR dwParam2);
typedef UINT (WINAPI* PFN_midiOutOpen)(void* phMidiOut, UINT uDeviceID, DWORD_PTR dwCallback,
                                        DWORD_PTR dwCallbackInstance, DWORD fdwOpen);
typedef UINT (WINAPI* PFN_midiOutPrepareHeader)(void* hMidiOut, LPVOID pMidiOutHdr, UINT cbMidiOutHdr);
typedef UINT (WINAPI* PFN_midiOutReset)(void* hMidiOut);
typedef UINT (WINAPI* PFN_midiOutSetVolume)(void* hMidiOut, DWORD dwVolume);
typedef UINT (WINAPI* PFN_midiOutShortMsg)(void* hMidiOut, DWORD dwMsg);
typedef UINT (WINAPI* PFN_midiOutUnprepareHeader)(void* hMidiOut, LPVOID pMidiOutHdr,
                                                   UINT cbMidiOutHdr);
typedef UINT (WINAPI* PFN_midiStreamClose)(void* hStream);
typedef UINT (WINAPI* PFN_midiStreamOpen)(void* phStream, PUINT puDeviceID, UINT cMidi,
                                          DWORD_PTR dwCallback, DWORD_PTR dwInstance, DWORD fdwOpen);
typedef UINT (WINAPI* PFN_midiStreamOut)(void* hMidiStream, LPVOID pMidiHdr, UINT cbMidiHdr);
typedef UINT (WINAPI* PFN_midiStreamPause)(void* hStream);
typedef UINT (WINAPI* PFN_midiStreamPosition)(void* hStream, LPVOID pmmt, UINT cbmmt);
typedef UINT (WINAPI* PFN_midiStreamProperty)(void* hStream, LPVOID lppropdata, DWORD dwProperty);
typedef UINT (WINAPI* PFN_midiStreamRestart)(void* hStream);
typedef UINT (WINAPI* PFN_midiStreamStop)(void* hStream);

/* 81-100 */
typedef UINT (WINAPI* PFN_mixerClose)(void* hmx);
typedef UINT (WINAPI* PFN_mixerGetControlDetailsA)(void* hmxObj, LPVOID pmxcd, DWORD fdwDetails);
typedef UINT (WINAPI* PFN_mixerGetControlDetailsW)(void* hmxObj, LPVOID pmxcd, DWORD fdwDetails);
typedef UINT (WINAPI* PFN_mixerGetDevCapsA)(UINT_PTR uMxId, LPVOID pmxcaps, UINT cbmxcaps);
typedef UINT (WINAPI* PFN_mixerGetDevCapsW)(UINT_PTR uMxId, LPVOID pmxcaps, UINT cbmxcaps);
typedef UINT (WINAPI* PFN_mixerGetID)(void* hmxobj, PUINT puMxId, DWORD fdwId);
typedef UINT (WINAPI* PFN_mixerGetLineControlsA)(void* hmxobj, LPVOID pmxlc, DWORD fdwControls);
typedef UINT (WINAPI* PFN_mixerGetLineControlsW)(void* hmxobj, LPVOID pmxlc, DWORD fdwControls);
typedef UINT (WINAPI* PFN_mixerGetLineInfoA)(void* hmxobj, LPVOID pmxl, DWORD fdwInfo);
typedef UINT (WINAPI* PFN_mixerGetLineInfoW)(void* hmxobj, LPVOID pmxl, DWORD fdwInfo);
typedef UINT (WINAPI* PFN_mixerGetNumDevs)(void);
typedef DWORD (WINAPI* PFN_mixerMessage)(void* hmx, UINT uMsg, DWORD_PTR dwParam1,
                                          DWORD_PTR dwParam2);
typedef UINT (WINAPI* PFN_mixerOpen)(void* phmx, UINT uMxId, DWORD_PTR dwCallback,
                                      DWORD_PTR dwInstance, DWORD fdwOpen);
typedef UINT (WINAPI* PFN_mixerSetControlDetails)(void* hmxobj, LPVOID pmxcd, DWORD fdwDetails);
typedef HTASK (WINAPI* PFN_mmGetCurrentTask)(void);
typedef LPVOID (WINAPI* PFN_mmTaskBlock)(DWORD_PTR hTask);
typedef DWORD (WINAPI* PFN_mmTaskCreate)(LPVOID lpfn, LPVOID pStack, DWORD dwFlags,
                                          LPDWORD lpdwThreadId);
typedef VOID (WINAPI* PFN_mmTaskSignal)(DWORD_PTR idTask);
typedef VOID (WINAPI* PFN_mmTaskYield)(void);
typedef LRESULT (WINAPI* PFN_mmioAdvance)(void* hmmio, LPVOID pmmioinfo, UINT fuAdvance);
typedef LRESULT (WINAPI* PFN_mmioAscend)(void* hmmio, LPVOID pmmck, UINT fuAscend);
typedef UINT (WINAPI* PFN_mmioClose)(void* hmmio, UINT fuClose);
typedef LRESULT (WINAPI* PFN_mmioCreateChunk)(void* hmmio, LPVOID pmmck, UINT fuCreate);
typedef LRESULT (WINAPI* PFN_mmioDescend)(void* hmmio, LPVOID pmmck, LPVOID pmmckParent, UINT fuDescend);
typedef LRESULT (WINAPI* PFN_mmioFlush)(void* hmmio, UINT fuFlush);
typedef LRESULT (WINAPI* PFN_mmioGetInfo)(void* hmmio, LPVOID pmmioinfo, UINT fuInfo);

/* 101-120 */
typedef LPVOID (WINAPI* PFN_mmioInstallIOProc16)(DWORD fccIOProc, LPVOID pIOProc, DWORD dwData);
typedef LPVOID (WINAPI* PFN_mmioInstallIOProcA)(DWORD fccIOProc, LPVOID pIOProc, DWORD dwData);
typedef LPVOID (WINAPI* PFN_mmioInstallIOProcW)(DWORD fccIOProc, LPVOID pIOProc, DWORD dwData);
typedef void* (WINAPI* PFN_mmioOpenA)(LPCSTR szFilename, LPVOID lpmmioinfo, DWORD fdwOpen);
typedef void* (WINAPI* PFN_mmioOpenW)(LPCWSTR szFilename, LPVOID lpmmioinfo, DWORD fdwOpen);
typedef LRESULT (WINAPI* PFN_mmioRead)(void* hmmio, LPSTR pch, LONG cch);
typedef LRESULT (WINAPI* PFN_mmioRenameA)(LPCSTR szFilename, LPCSTR szNewFilename, LPVOID lpmmioinfo,
                                           DWORD fdwRename);
typedef LRESULT (WINAPI* PFN_mmioRenameW)(LPCWSTR szFilename, LPCWSTR szNewFilename, LPVOID lpmmioinfo,
                                           DWORD fdwRename);
typedef LRESULT (WINAPI* PFN_mmioSeek)(void* hmmio, LONG lOffset, int iOrigin);
typedef LRESULT (WINAPI* PFN_mmioSendMessage)(void* hmmio, UINT uMsg, LPARAM lParam1, LPARAM lParam2);
typedef LRESULT (WINAPI* PFN_mmioSetBuffer)(void* hmmio, LPSTR pchBuffer, LONG cchBuffer, UINT fuRead);
typedef LRESULT (WINAPI* PFN_mmioSetInfo)(void* hmmio, LPVOID pmmioinfo, UINT fuInfo);
typedef DWORD (WINAPI* PFN_mmioStringToFOURCCA)(LPCSTR sz, UINT fuFlags);
typedef DWORD (WINAPI* PFN_mmioStringToFOURCCW)(LPCWSTR sz, UINT fuFlags);
typedef LRESULT (WINAPI* PFN_mmioWrite)(void* hmmio, LPCSTR pch, LONG cch);
typedef UINT (WINAPI* PFN_mmsystemGetVersion)(void);
typedef BOOL (WINAPI* PFN_sndPlaySoundA)(LPCSTR pszSound, UINT fuSound);
typedef BOOL (WINAPI* PFN_sndPlaySoundW)(LPCWSTR pszSound, UINT fuSound);
typedef UINT (WINAPI* PFN_timeBeginPeriod)(UINT uPeriod);
typedef UINT (WINAPI* PFN_timeEndPeriod)(UINT uPeriod);
typedef UINT (WINAPI* PFN_timeGetDevCaps)(LPVOID ptc, UINT cbtc);
typedef UINT (WINAPI* PFN_timeGetSystemTime)(LPVOID pmmt, UINT cbmmt);
typedef DWORD (WINAPI* PFN_timeGetTime)(void);
typedef UINT (WINAPI* PFN_timeKillEvent)(UINT uID);
typedef UINT (WINAPI* PFN_timeSetEvent)(UINT uDelay, UINT uResolution, LPVOID lpTimeProc,
                                         UINT_PTR dwUser, UINT fuEvent);

/* 121-140 */
typedef UINT (WINAPI* PFN_waveInAddBuffer)(void* hWaveIn, LPVOID pWaveInHdr, UINT cbWaveInHdr);
typedef UINT (WINAPI* PFN_waveInClose)(void* hWaveIn);
typedef UINT (WINAPI* PFN_waveInGetDevCapsA)(UINT_PTR uDeviceID, LPVOID pwic, UINT cbwic);
typedef UINT (WINAPI* PFN_waveInGetDevCapsW)(UINT_PTR uDeviceID, LPVOID pwic, UINT cbwic);
typedef BOOL (WINAPI* PFN_waveInGetErrorTextA)(UINT uErr, LPSTR pszText, UINT cchText);
typedef BOOL (WINAPI* PFN_waveInGetErrorTextW)(UINT uErr, LPWSTR pszText, UINT cchText);
typedef UINT (WINAPI* PFN_waveInGetID)(void* hWaveIn, LPUINT puDeviceID);
typedef UINT (WINAPI* PFN_waveInGetNumDevs)(void);
typedef UINT (WINAPI* PFN_waveInGetPosition)(void* hWaveIn, LPVOID pmmt, UINT cbmmt);
typedef DWORD (WINAPI* PFN_waveInMessage)(void* hWaveIn, UINT uMsg, DWORD_PTR dwParam1,
                                          DWORD_PTR dwParam2);
typedef UINT (WINAPI* PFN_waveInOpen)(void* phWaveIn, UINT uDeviceID, LPVOID pwfx,
                                       DWORD_PTR dwCallback, DWORD_PTR dwCallbackInstance,
                                       DWORD fdwOpen);
typedef UINT (WINAPI* PFN_waveInPrepareHeader)(void* hWaveIn, LPVOID pWaveInHdr, UINT cbWaveInHdr);
typedef UINT (WINAPI* PFN_waveInReset)(void* hWaveIn);
typedef UINT (WINAPI* PFN_waveInStart)(void* hWaveIn);
typedef UINT (WINAPI* PFN_waveInStop)(void* hWaveIn);
typedef UINT (WINAPI* PFN_waveInUnprepareHeader)(void* hWaveIn, LPVOID pWaveInHdr, UINT cbWaveInHdr);
typedef UINT (WINAPI* PFN_waveOutBreakLoop)(void* hWaveOut);
typedef UINT (WINAPI* PFN_waveOutClose)(void* hWaveOut);
typedef UINT (WINAPI* PFN_waveOutGetDevCapsA)(UINT_PTR uDeviceID, LPVOID pwoc, UINT cbwoc);
typedef UINT (WINAPI* PFN_waveOutGetDevCapsW)(UINT_PTR uDeviceID, LPVOID pwoc, UINT cbwoc);
typedef BOOL (WINAPI* PFN_waveOutGetErrorTextA)(UINT uErr, LPSTR pszText, UINT cchText);
typedef BOOL (WINAPI* PFN_waveOutGetErrorTextW)(UINT uErr, LPWSTR pszText, UINT cchText);
typedef UINT (WINAPI* PFN_waveOutGetID)(void* hWaveOut, LPUINT puDeviceID);
typedef UINT (WINAPI* PFN_waveOutGetNumDevs)(void);
typedef UINT (WINAPI* PFN_waveOutGetPitch)(void* hWaveOut, LPDWORD pdwPitch);
typedef UINT (WINAPI* PFN_waveOutGetPlaybackRate)(void* hWaveOut, LPDWORD pdwRate);
typedef UINT (WINAPI* PFN_waveOutGetPosition)(void* hWaveOut, LPVOID pmmt, UINT cbmmt);
typedef UINT (WINAPI* PFN_waveOutGetVolume)(void* hWaveOut, LPDWORD pdwVolume);
typedef DWORD (WINAPI* PFN_waveOutMessage)(void* hWaveOut, UINT uMsg, DWORD_PTR dwParam1,
                                            DWORD_PTR dwParam2);

/* 141-166 */
typedef UINT (WINAPI* PFN_waveOutOpen)(void* phWaveOut, UINT uDeviceID, LPVOID pwfx,
                                        DWORD_PTR dwCallback, DWORD_PTR dwCallbackInstance,
                                        DWORD fdwOpen);
typedef UINT (WINAPI* PFN_waveOutPause)(void* hWaveOut);
typedef UINT (WINAPI* PFN_waveOutPrepareHeader)(void* hWaveOut, LPVOID pWaveOutHdr,
                                                  UINT cbWaveOutHdr);
typedef UINT (WINAPI* PFN_waveOutReset)(void* hWaveOut);
typedef UINT (WINAPI* PFN_waveOutRestart)(void* hWaveOut);
typedef UINT (WINAPI* PFN_waveOutSetPitch)(void* hWaveOut, DWORD dwPitch);
typedef UINT (WINAPI* PFN_waveOutSetPlaybackRate)(void* hWaveOut, DWORD dwRate);
typedef UINT (WINAPI* PFN_waveOutSetVolume)(void* hWaveOut, DWORD dwVolume);
typedef UINT (WINAPI* PFN_waveOutUnprepareHeader)(void* hWaveOut, LPVOID pWaveOutHdr,
                                                    UINT cbWaveOutHdr);
typedef UINT (WINAPI* PFN_waveOutWrite)(void* hWaveOut, LPVOID pWaveOutHdr, UINT cbWaveOutHdr);

#ifdef __cplusplus
}
#endif

#endif /* DISPLAY_COMMANDER_WINMM_PROXY_HPP */
