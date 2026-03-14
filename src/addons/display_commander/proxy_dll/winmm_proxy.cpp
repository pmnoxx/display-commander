/*
 * winmm.dll proxy. Forwards all WinMM API calls to the system winmm.dll (or winmmHooked.dll).
 * Signatures from winmm_proxy.hpp (official Windows Multimedia API).
 * Reference: https://learn.microsoft.com/en-us/windows/win32/api/_multimedia/
 */

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

// Source Code <Display Commander>
#include "winmm_proxy.hpp"
#include "winmm_proxy_init.hpp"

// Libraries <standard C++>
#include <string>

static HMODULE g_winmm_module = nullptr;

static bool LoadRealWinMM() {
    if (g_winmm_module != nullptr) return true;
    HMODULE hSelf = GetModuleHandleW(L"winmm.dll");
    if (hSelf) {
        WCHAR self_path[MAX_PATH];
        if (GetModuleFileNameW(hSelf, self_path, MAX_PATH) != 0) {
            std::wstring dir(self_path);
            size_t last = dir.find_last_of(L"\\/");
            if (last != std::wstring::npos) dir.resize(last + 1);
            std::wstring hooked = dir + L"winmmHooked.dll";
            if (GetFileAttributesW(hooked.c_str()) != INVALID_FILE_ATTRIBUTES) {
                g_winmm_module = LoadLibraryW(hooked.c_str());
            }
        }
    }
    if (g_winmm_module == nullptr) {
        WCHAR system_path[MAX_PATH];
        if (GetSystemDirectoryW(system_path, MAX_PATH) != 0) {
            std::wstring path = std::wstring(system_path) + L"\\winmm.dll";
            g_winmm_module = LoadLibraryW(path.c_str());
        }
    }
    return g_winmm_module != nullptr;
}

void LoadRealWinMMFromDllMain() { (void)LoadRealWinMM(); }

extern "C" BOOL WINAPI PlaySoundA(LPCSTR pszSound, HMODULE hmod, DWORD fdwSound) {
    auto fn = (PFN_PlaySoundA)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "PlaySoundA") : nullptr);
    return fn ? fn(pszSound, hmod, fdwSound) : FALSE;
}

extern "C" UINT WINAPI WINMM_3(void) {
    return 0;
}

extern "C" UINT WINAPI WINMM_4(void) {
    return 0;
}

/* 1-20 */
extern "C" LRESULT WINAPI CloseDriver(void* hDriver, LPARAM lParam1, LPARAM lParam2) {
    auto fn = (PFN_CloseDriver)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "CloseDriver") : nullptr);
    return fn ? fn(hDriver, lParam1, lParam2) : 0;
}
extern "C" LRESULT WINAPI DefDriverProc(DWORD_PTR dwDriverIdentifier, void* hDrv, UINT uMsg,
                                        LPARAM lParam1, LPARAM lParam2) {
    auto fn = (PFN_DefDriverProc)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "DefDriverProc") : nullptr);
    return fn ? fn(dwDriverIdentifier, hDrv, uMsg, lParam1, lParam2) : 0;
}
extern "C" BOOL WINAPI DriverCallback(DWORD_PTR dwCallback, DWORD dwFlags, void* hDevice,
                                     DWORD dwMsg, DWORD_PTR dwUser, DWORD_PTR dwParam1, DWORD_PTR dwParam2) {
    auto fn = (PFN_DriverCallback)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "DriverCallback") : nullptr);
    return fn ? fn(dwCallback, dwFlags, hDevice, dwMsg, dwUser, dwParam1, dwParam2) : FALSE;
}
extern "C" LRESULT WINAPI DrvClose(void* hDriver, LPARAM lParam1, LPARAM lParam2) {
    auto fn = (PFN_DrvClose)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "DrvClose") : nullptr);
    return fn ? fn(hDriver, lParam1, lParam2) : 0;
}
extern "C" LRESULT WINAPI DrvDefDriverProc(DWORD_PTR dwDriverIdentifier, void* hDrv, UINT uMsg,
                                           LPARAM lParam1, LPARAM lParam2) {
    auto fn = (PFN_DrvDefDriverProc)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "DrvDefDriverProc") : nullptr);
    return fn ? fn(dwDriverIdentifier, hDrv, uMsg, lParam1, lParam2) : 0;
}
extern "C" HMODULE WINAPI DrvGetModuleHandle(void* hDriver) {
    auto fn = (PFN_DrvGetModuleHandle)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "DrvGetModuleHandle") : nullptr);
    return fn ? fn(hDriver) : nullptr;
}
extern "C" LRESULT WINAPI DrvOpen(LPCWSTR szDriverName, LPCWSTR szSectionName, LPARAM lParam) {
    auto fn = (PFN_DrvOpen)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "DrvOpen") : nullptr);
    return fn ? fn(szDriverName, szSectionName, lParam) : 0;
}
extern "C" LRESULT WINAPI DrvOpenA(LPCSTR szDriverName, LPCSTR szSectionName, LPARAM lParam) {
    auto fn = (PFN_DrvOpenA)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "DrvOpenA") : nullptr);
    return fn ? fn(szDriverName, szSectionName, lParam) : 0;
}
extern "C" LRESULT WINAPI DrvSendMessage(void* hDriver, UINT uMsg, LPARAM lParam1, LPARAM lParam2) {
    auto fn = (PFN_DrvSendMessage)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "DrvSendMessage") : nullptr);
    return fn ? fn(hDriver, uMsg, lParam1, lParam2) : 0;
}
extern "C" UINT WINAPI GetDriverFlags(void* hDriver) {
    auto fn = (PFN_GetDriverFlags)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "GetDriverFlags") : nullptr);
    return fn ? fn(hDriver) : 0;
}
extern "C" HMODULE WINAPI GetDriverModuleHandle(void* hDriver) {
    auto fn = (PFN_GetDriverModuleHandle)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "GetDriverModuleHandle") : nullptr);
    return fn ? fn(hDriver) : nullptr;
}
extern "C" void* WINAPI OpenDriver(LPCWSTR szDriverName, LPCWSTR szSectionName, LPARAM lParam) {
    auto fn = (PFN_OpenDriver)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "OpenDriver") : nullptr);
    return fn ? fn(szDriverName, szSectionName, lParam) : nullptr;
}
extern "C" void* WINAPI OpenDriverA(LPCSTR szDriverName, LPCSTR szSectionName, LPARAM lParam) {
    auto fn = (PFN_OpenDriverA)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "OpenDriverA") : nullptr);
    return fn ? fn(szDriverName, szSectionName, lParam) : nullptr;
}
extern "C" BOOL WINAPI PlaySound(LPCWSTR pszSound, HMODULE hmod, DWORD fdwSound) {
    auto fn = (PFN_PlaySound)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "PlaySound") : nullptr);
    return fn ? fn(pszSound, hmod, fdwSound) : FALSE;
}
extern "C" BOOL WINAPI PlaySoundW(LPCWSTR pszSound, HMODULE hmod, DWORD fdwSound) {
    auto fn = (PFN_PlaySoundW)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "PlaySoundW") : nullptr);
    return fn ? fn(pszSound, hmod, fdwSound) : FALSE;
}
extern "C" LRESULT WINAPI SendDriverMessage(void* hDriver, UINT uMsg, LPARAM lParam1, LPARAM lParam2) {
    auto fn = (PFN_SendDriverMessage)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "SendDriverMessage") : nullptr);
    return fn ? fn(hDriver, uMsg, lParam1, lParam2) : 0;
}
extern "C" UINT WINAPI auxGetDevCapsA(UINT_PTR uDeviceID, LPVOID pac, UINT cbac) {
    auto fn = (PFN_auxGetDevCapsA)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "auxGetDevCapsA") : nullptr);
    return fn ? fn(uDeviceID, pac, cbac) : 0;
}
extern "C" UINT WINAPI auxGetDevCapsW(UINT_PTR uDeviceID, LPVOID pac, UINT cbac) {
    auto fn = (PFN_auxGetDevCapsW)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "auxGetDevCapsW") : nullptr);
    return fn ? fn(uDeviceID, pac, cbac) : 0;
}
extern "C" UINT WINAPI auxGetNumDevs(void) {
    auto fn = (PFN_auxGetNumDevs)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "auxGetNumDevs") : nullptr);
    return fn ? fn() : 0;
}

/* 21-40 */
extern "C" UINT WINAPI auxGetVolume(UINT uDeviceID, LPDWORD pdwVolume) {
    auto fn = (PFN_auxGetVolume)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "auxGetVolume") : nullptr);
    return fn ? fn(uDeviceID, pdwVolume) : 0;
}
extern "C" DWORD WINAPI auxOutMessage(UINT uDeviceID, UINT uMsg, DWORD_PTR dwInstance,
                                      DWORD_PTR dwParam1, DWORD_PTR dwParam2) {
    auto fn = (PFN_auxOutMessage)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "auxOutMessage") : nullptr);
    return fn ? fn(uDeviceID, uMsg, dwInstance, dwParam1, dwParam2) : 0;
}
extern "C" VOID WINAPI auxSetVolume(UINT uDeviceID, DWORD dwVolume) {
    auto fn = (PFN_auxSetVolume)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "auxSetVolume") : nullptr);
    if (fn) fn(uDeviceID, dwVolume);
}
extern "C" VOID WINAPI joyConfigChanged(DWORD dwFlags) {
    auto fn = (PFN_joyConfigChanged)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "joyConfigChanged") : nullptr);
    if (fn) fn(dwFlags);
}
extern "C" UINT WINAPI joyGetDevCapsA(UINT_PTR uJoyID, LPVOID pjc, UINT cbjc) {
    auto fn = (PFN_joyGetDevCapsA)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "joyGetDevCapsA") : nullptr);
    return fn ? fn(uJoyID, pjc, cbjc) : 0;
}
extern "C" UINT WINAPI joyGetDevCapsW(UINT_PTR uJoyID, LPVOID pjc, UINT cbjc) {
    auto fn = (PFN_joyGetDevCapsW)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "joyGetDevCapsW") : nullptr);
    return fn ? fn(uJoyID, pjc, cbjc) : 0;
}
extern "C" UINT WINAPI joyGetNumDevs(void) {
    auto fn = (PFN_joyGetNumDevs)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "joyGetNumDevs") : nullptr);
    return fn ? fn() : 0;
}
extern "C" UINT WINAPI joyGetPos(UINT uJoyID, LPVOID pji) {
    auto fn = (PFN_joyGetPos)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "joyGetPos") : nullptr);
    return fn ? fn(uJoyID, pji) : 0;
}
extern "C" UINT WINAPI joyGetPosEx(UINT uJoyID, LPVOID pji) {
    auto fn = (PFN_joyGetPosEx)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "joyGetPosEx") : nullptr);
    return fn ? fn(uJoyID, pji) : 0;
}
extern "C" UINT WINAPI joyGetThreshold(UINT uJoyID, LPUINT puThreshold) {
    auto fn = (PFN_joyGetThreshold)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "joyGetThreshold") : nullptr);
    return fn ? fn(uJoyID, puThreshold) : 0;
}
extern "C" UINT WINAPI joyReleaseCapture(UINT uJoyID) {
    auto fn = (PFN_joyReleaseCapture)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "joyReleaseCapture") : nullptr);
    return fn ? fn(uJoyID) : 0;
}
extern "C" UINT WINAPI joySetCapture(HWND hwnd, UINT uJoyID, UINT uPeriod, BOOL fChanged) {
    auto fn = (PFN_joySetCapture)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "joySetCapture") : nullptr);
    return fn ? fn(hwnd, uJoyID, uPeriod, fChanged) : 0;
}
extern "C" UINT WINAPI joySetThreshold(UINT uJoyID, UINT uThreshold) {
    auto fn = (PFN_joySetThreshold)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "joySetThreshold") : nullptr);
    return fn ? fn(uJoyID, uThreshold) : 0;
}
extern "C" VOID WINAPI mciDriverNotify(HWND hwndCallback, UINT uDeviceID, UINT uStatus) {
    auto fn = (PFN_mciDriverNotify)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "mciDriverNotify") : nullptr);
    if (fn) fn(hwndCallback, uDeviceID, uStatus);
}
extern "C" BOOL WINAPI mciDriverYield(UINT uDeviceID) {
    auto fn = (PFN_mciDriverYield)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "mciDriverYield") : nullptr);
    return fn ? fn(uDeviceID) : FALSE;
}
extern "C" BOOL WINAPI mciExecute(LPCSTR pszCommand) {
    auto fn = (PFN_mciExecute)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "mciExecute") : nullptr);
    return fn ? fn(pszCommand) : FALSE;
}
extern "C" UINT WINAPI mciFreeCommandResource(UINT uResource) {
    auto fn = (PFN_mciFreeCommandResource)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "mciFreeCommandResource") : nullptr);
    return fn ? fn(uResource) : 0;
}
extern "C" HTASK WINAPI mciGetCreatorTask(UINT uDeviceID) {
    auto fn = (PFN_mciGetCreatorTask)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "mciGetCreatorTask") : nullptr);
    return fn ? fn(uDeviceID) : nullptr;
}
extern "C" UINT WINAPI mciGetDeviceIDA(LPCSTR pszElement) {
    auto fn = (PFN_mciGetDeviceIDA)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "mciGetDeviceIDA") : nullptr);
    return fn ? fn(pszElement) : 0;
}
extern "C" UINT WINAPI mciGetDeviceIDFromElementIDA(DWORD dwElementID, LPCSTR pszType) {
    auto fn = (PFN_mciGetDeviceIDFromElementIDA)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "mciGetDeviceIDFromElementIDA") : nullptr);
    return fn ? fn(dwElementID, pszType) : 0;
}

/* 41-60 */
extern "C" UINT WINAPI mciGetDeviceIDFromElementIDW(DWORD dwElementID, LPCWSTR pszType) {
    auto fn = (PFN_mciGetDeviceIDFromElementIDW)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "mciGetDeviceIDFromElementIDW") : nullptr);
    return fn ? fn(dwElementID, pszType) : 0;
}
extern "C" UINT WINAPI mciGetDeviceIDW(LPCWSTR pszElement) {
    auto fn = (PFN_mciGetDeviceIDW)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "mciGetDeviceIDW") : nullptr);
    return fn ? fn(pszElement) : 0;
}
extern "C" DWORD_PTR WINAPI mciGetDriverData(UINT uDeviceID) {
    auto fn = (PFN_mciGetDriverData)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "mciGetDriverData") : nullptr);
    return fn ? fn(uDeviceID) : 0;
}
extern "C" BOOL WINAPI mciGetErrorStringA(DWORD fdwError, LPSTR pszText, UINT cchText) {
    auto fn = (PFN_mciGetErrorStringA)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "mciGetErrorStringA") : nullptr);
    return fn ? fn(fdwError, pszText, cchText) : FALSE;
}
extern "C" BOOL WINAPI mciGetErrorStringW(DWORD fdwError, LPWSTR pszText, UINT cchText) {
    auto fn = (PFN_mciGetErrorStringW)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "mciGetErrorStringW") : nullptr);
    return fn ? fn(fdwError, pszText, cchText) : FALSE;
}
extern "C" LPVOID WINAPI mciGetYieldProc(UINT uDeviceID, LPDWORD pdwYieldData) {
    auto fn = (PFN_mciGetYieldProc)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "mciGetYieldProc") : nullptr);
    return fn ? fn(uDeviceID, pdwYieldData) : nullptr;
}
extern "C" UINT WINAPI mciLoadCommandResource(HINSTANCE hInstance, LPCWSTR lpResId, UINT uType) {
    auto fn = (PFN_mciLoadCommandResource)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "mciLoadCommandResource") : nullptr);
    return fn ? fn(hInstance, lpResId, uType) : 0;
}
extern "C" UINT WINAPI mciSendCommandA(UINT uDeviceID, UINT uMsg, DWORD_PTR dwParam1, DWORD_PTR dwParam2) {
    auto fn = (PFN_mciSendCommandA)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "mciSendCommandA") : nullptr);
    return fn ? fn(uDeviceID, uMsg, dwParam1, dwParam2) : 0;
}
extern "C" UINT WINAPI mciSendCommandW(UINT uDeviceID, UINT uMsg, DWORD_PTR dwParam1, DWORD_PTR dwParam2) {
    auto fn = (PFN_mciSendCommandW)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "mciSendCommandW") : nullptr);
    return fn ? fn(uDeviceID, uMsg, dwParam1, dwParam2) : 0;
}
extern "C" UINT WINAPI mciSendStringA(LPCSTR lpszCommand, LPSTR lpszReturnString, UINT cchReturn,
                                      HANDLE hwndCallback) {
    auto fn = (PFN_mciSendStringA)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "mciSendStringA") : nullptr);
    return fn ? fn(lpszCommand, lpszReturnString, cchReturn, hwndCallback) : 0;
}
extern "C" UINT WINAPI mciSendStringW(LPCWSTR lpszCommand, LPWSTR lpszReturnString, UINT cchReturn,
                                      HANDLE hwndCallback) {
    auto fn = (PFN_mciSendStringW)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "mciSendStringW") : nullptr);
    return fn ? fn(lpszCommand, lpszReturnString, cchReturn, hwndCallback) : 0;
}
extern "C" UINT WINAPI mciSetDriverData(UINT uDeviceID, DWORD_PTR dwData) {
    auto fn = (PFN_mciSetDriverData)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "mciSetDriverData") : nullptr);
    return fn ? fn(uDeviceID, dwData) : 0;
}
extern "C" UINT WINAPI mciSetYieldProc(UINT uDeviceID, LPVOID fpYieldProc, DWORD dwYieldData) {
    auto fn = (PFN_mciSetYieldProc)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "mciSetYieldProc") : nullptr);
    return fn ? fn(uDeviceID, fpYieldProc, dwYieldData) : 0;
}
extern "C" UINT WINAPI midiConnect(void* hMidi, void* hmo, LPVOID pReserved) {
    auto fn = (PFN_midiConnect)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "midiConnect") : nullptr);
    return fn ? fn(hMidi, hmo, pReserved) : 0;
}
extern "C" UINT WINAPI midiDisconnect(void* hMidi, void* hmo, LPVOID pReserved) {
    auto fn = (PFN_midiDisconnect)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "midiDisconnect") : nullptr);
    return fn ? fn(hMidi, hmo, pReserved) : 0;
}
extern "C" UINT WINAPI midiInAddBuffer(void* hMidiIn, LPVOID pMidiInHdr, UINT cbMidiInHdr) {
    auto fn = (PFN_midiInAddBuffer)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "midiInAddBuffer") : nullptr);
    return fn ? fn(hMidiIn, pMidiInHdr, cbMidiInHdr) : 0;
}
extern "C" UINT WINAPI midiInClose(void* hMidiIn) {
    auto fn = (PFN_midiInClose)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "midiInClose") : nullptr);
    return fn ? fn(hMidiIn) : 0;
}
extern "C" UINT WINAPI midiInGetDevCapsA(UINT_PTR uDeviceID, LPVOID pmic, UINT cbmic) {
    auto fn = (PFN_midiInGetDevCapsA)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "midiInGetDevCapsA") : nullptr);
    return fn ? fn(uDeviceID, pmic, cbmic) : 0;
}
extern "C" UINT WINAPI midiInGetDevCapsW(UINT_PTR uDeviceID, LPVOID pmic, UINT cbmic) {
    auto fn = (PFN_midiInGetDevCapsW)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "midiInGetDevCapsW") : nullptr);
    return fn ? fn(uDeviceID, pmic, cbmic) : 0;
}
extern "C" BOOL WINAPI midiInGetErrorTextA(UINT uErr, LPSTR pszText, UINT cchText) {
    auto fn = (PFN_midiInGetErrorTextA)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "midiInGetErrorTextA") : nullptr);
    return fn ? fn(uErr, pszText, cchText) : FALSE;
}

/* 61-80 */
extern "C" BOOL WINAPI midiInGetErrorTextW(UINT uErr, LPWSTR pszText, UINT cchText) {
    auto fn = (PFN_midiInGetErrorTextW)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "midiInGetErrorTextW") : nullptr);
    return fn ? fn(uErr, pszText, cchText) : FALSE;
}
extern "C" UINT WINAPI midiInGetID(void* hMidiIn, LPUINT puDeviceID) {
    auto fn = (PFN_midiInGetID)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "midiInGetID") : nullptr);
    return fn ? fn(hMidiIn, puDeviceID) : 0;
}
extern "C" UINT WINAPI midiInGetNumDevs(void) {
    auto fn = (PFN_midiInGetNumDevs)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "midiInGetNumDevs") : nullptr);
    return fn ? fn() : 0;
}
extern "C" DWORD WINAPI midiInMessage(void* hMidiIn, UINT uMsg, DWORD_PTR dwParam1, DWORD_PTR dwParam2) {
    auto fn = (PFN_midiInMessage)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "midiInMessage") : nullptr);
    return fn ? fn(hMidiIn, uMsg, dwParam1, dwParam2) : 0;
}
extern "C" UINT WINAPI midiInOpen(void* phMidiIn, UINT uDeviceID, DWORD_PTR dwCallback,
                                  DWORD_PTR dwCallbackInstance, DWORD fdwOpen) {
    auto fn = (PFN_midiInOpen)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "midiInOpen") : nullptr);
    return fn ? fn(phMidiIn, uDeviceID, dwCallback, dwCallbackInstance, fdwOpen) : 0;
}
extern "C" UINT WINAPI midiInPrepareHeader(void* hMidiIn, LPVOID pMidiInHdr, UINT cbMidiInHdr) {
    auto fn = (PFN_midiInPrepareHeader)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "midiInPrepareHeader") : nullptr);
    return fn ? fn(hMidiIn, pMidiInHdr, cbMidiInHdr) : 0;
}
extern "C" UINT WINAPI midiInReset(void* hMidiIn) {
    auto fn = (PFN_midiInReset)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "midiInReset") : nullptr);
    return fn ? fn(hMidiIn) : 0;
}
extern "C" UINT WINAPI midiInStart(void* hMidiIn) {
    auto fn = (PFN_midiInStart)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "midiInStart") : nullptr);
    return fn ? fn(hMidiIn) : 0;
}
extern "C" UINT WINAPI midiInStop(void* hMidiIn) {
    auto fn = (PFN_midiInStop)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "midiInStop") : nullptr);
    return fn ? fn(hMidiIn) : 0;
}
extern "C" UINT WINAPI midiInUnprepareHeader(void* hMidiIn, LPVOID pMidiInHdr, UINT cbMidiInHdr) {
    auto fn = (PFN_midiInUnprepareHeader)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "midiInUnprepareHeader") : nullptr);
    return fn ? fn(hMidiIn, pMidiInHdr, cbMidiInHdr) : 0;
}
extern "C" UINT WINAPI midiOutCacheDrumPatches(void* hMidiOut, UINT uPatch, LPVOID pwkya, UINT uFlags) {
    auto fn = (PFN_midiOutCacheDrumPatches)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "midiOutCacheDrumPatches") : nullptr);
    return fn ? fn(hMidiOut, uPatch, pwkya, uFlags) : 0;
}
extern "C" UINT WINAPI midiOutCachePatches(void* hMidiOut, UINT uBank, LPVOID pwpa, UINT uFlags) {
    auto fn = (PFN_midiOutCachePatches)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "midiOutCachePatches") : nullptr);
    return fn ? fn(hMidiOut, uBank, pwpa, uFlags) : 0;
}
extern "C" UINT WINAPI midiOutClose(void* hMidiOut) {
    auto fn = (PFN_midiOutClose)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "midiOutClose") : nullptr);
    return fn ? fn(hMidiOut) : 0;
}
extern "C" UINT WINAPI midiOutGetDevCapsA(UINT_PTR uDeviceID, LPVOID pmoc, UINT cbmoc) {
    auto fn = (PFN_midiOutGetDevCapsA)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "midiOutGetDevCapsA") : nullptr);
    return fn ? fn(uDeviceID, pmoc, cbmoc) : 0;
}
extern "C" UINT WINAPI midiOutGetDevCapsW(UINT_PTR uDeviceID, LPVOID pmoc, UINT cbmoc) {
    auto fn = (PFN_midiOutGetDevCapsW)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "midiOutGetDevCapsW") : nullptr);
    return fn ? fn(uDeviceID, pmoc, cbmoc) : 0;
}
extern "C" BOOL WINAPI midiOutGetErrorTextA(UINT uErr, LPSTR pszText, UINT cchText) {
    auto fn = (PFN_midiOutGetErrorTextA)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "midiOutGetErrorTextA") : nullptr);
    return fn ? fn(uErr, pszText, cchText) : FALSE;
}
extern "C" BOOL WINAPI midiOutGetErrorTextW(UINT uErr, LPWSTR pszText, UINT cchText) {
    auto fn = (PFN_midiOutGetErrorTextW)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "midiOutGetErrorTextW") : nullptr);
    return fn ? fn(uErr, pszText, cchText) : FALSE;
}
extern "C" UINT WINAPI midiOutGetID(void* hMidiOut, LPUINT puDeviceID) {
    auto fn = (PFN_midiOutGetID)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "midiOutGetID") : nullptr);
    return fn ? fn(hMidiOut, puDeviceID) : 0;
}
extern "C" UINT WINAPI midiOutGetNumDevs(void) {
    auto fn = (PFN_midiOutGetNumDevs)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "midiOutGetNumDevs") : nullptr);
    return fn ? fn() : 0;
}
extern "C" UINT WINAPI midiOutGetVolume(void* hMidiOut, LPDWORD pdwVolume) {
    auto fn = (PFN_midiOutGetVolume)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "midiOutGetVolume") : nullptr);
    return fn ? fn(hMidiOut, pdwVolume) : 0;
}

/* 81-100 */
extern "C" UINT WINAPI midiOutLongMsg(void* hMidiOut, LPVOID pMidiOutHdr, UINT cbMidiOutHdr) {
    auto fn = (PFN_midiOutLongMsg)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "midiOutLongMsg") : nullptr);
    return fn ? fn(hMidiOut, pMidiOutHdr, cbMidiOutHdr) : 0;
}
extern "C" DWORD WINAPI midiOutMessage(void* hMidiOut, UINT uMsg, DWORD_PTR dwParam1, DWORD_PTR dwParam2) {
    auto fn = (PFN_midiOutMessage)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "midiOutMessage") : nullptr);
    return fn ? fn(hMidiOut, uMsg, dwParam1, dwParam2) : 0;
}
extern "C" UINT WINAPI midiOutOpen(void* phMidiOut, UINT uDeviceID, DWORD_PTR dwCallback,
                                   DWORD_PTR dwCallbackInstance, DWORD fdwOpen) {
    auto fn = (PFN_midiOutOpen)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "midiOutOpen") : nullptr);
    return fn ? fn(phMidiOut, uDeviceID, dwCallback, dwCallbackInstance, fdwOpen) : 0;
}
extern "C" UINT WINAPI midiOutPrepareHeader(void* hMidiOut, LPVOID pMidiOutHdr, UINT cbMidiOutHdr) {
    auto fn = (PFN_midiOutPrepareHeader)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "midiOutPrepareHeader") : nullptr);
    return fn ? fn(hMidiOut, pMidiOutHdr, cbMidiOutHdr) : 0;
}
extern "C" UINT WINAPI midiOutReset(void* hMidiOut) {
    auto fn = (PFN_midiOutReset)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "midiOutReset") : nullptr);
    return fn ? fn(hMidiOut) : 0;
}
extern "C" UINT WINAPI midiOutSetVolume(void* hMidiOut, DWORD dwVolume) {
    auto fn = (PFN_midiOutSetVolume)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "midiOutSetVolume") : nullptr);
    return fn ? fn(hMidiOut, dwVolume) : 0;
}
extern "C" UINT WINAPI midiOutShortMsg(void* hMidiOut, DWORD dwMsg) {
    auto fn = (PFN_midiOutShortMsg)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "midiOutShortMsg") : nullptr);
    return fn ? fn(hMidiOut, dwMsg) : 0;
}
extern "C" UINT WINAPI midiOutUnprepareHeader(void* hMidiOut, LPVOID pMidiOutHdr, UINT cbMidiOutHdr) {
    auto fn = (PFN_midiOutUnprepareHeader)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "midiOutUnprepareHeader") : nullptr);
    return fn ? fn(hMidiOut, pMidiOutHdr, cbMidiOutHdr) : 0;
}
extern "C" UINT WINAPI midiStreamClose(void* hStream) {
    auto fn = (PFN_midiStreamClose)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "midiStreamClose") : nullptr);
    return fn ? fn(hStream) : 0;
}
extern "C" UINT WINAPI midiStreamOpen(void* phStream, PUINT puDeviceID, UINT cMidi,
                                      DWORD_PTR dwCallback, DWORD_PTR dwInstance, DWORD fdwOpen) {
    auto fn = (PFN_midiStreamOpen)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "midiStreamOpen") : nullptr);
    return fn ? fn(phStream, puDeviceID, cMidi, dwCallback, dwInstance, fdwOpen) : 0;
}
extern "C" UINT WINAPI midiStreamOut(void* hMidiStream, LPVOID pMidiHdr, UINT cbMidiHdr) {
    auto fn = (PFN_midiStreamOut)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "midiStreamOut") : nullptr);
    return fn ? fn(hMidiStream, pMidiHdr, cbMidiHdr) : 0;
}
extern "C" UINT WINAPI midiStreamPause(void* hStream) {
    auto fn = (PFN_midiStreamPause)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "midiStreamPause") : nullptr);
    return fn ? fn(hStream) : 0;
}
extern "C" UINT WINAPI midiStreamPosition(void* hStream, LPVOID pmmt, UINT cbmmt) {
    auto fn = (PFN_midiStreamPosition)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "midiStreamPosition") : nullptr);
    return fn ? fn(hStream, pmmt, cbmmt) : 0;
}
extern "C" UINT WINAPI midiStreamProperty(void* hStream, LPVOID lppropdata, DWORD dwProperty) {
    auto fn = (PFN_midiStreamProperty)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "midiStreamProperty") : nullptr);
    return fn ? fn(hStream, lppropdata, dwProperty) : 0;
}
extern "C" UINT WINAPI midiStreamRestart(void* hStream) {
    auto fn = (PFN_midiStreamRestart)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "midiStreamRestart") : nullptr);
    return fn ? fn(hStream) : 0;
}
extern "C" UINT WINAPI midiStreamStop(void* hStream) {
    auto fn = (PFN_midiStreamStop)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "midiStreamStop") : nullptr);
    return fn ? fn(hStream) : 0;
}
extern "C" UINT WINAPI mixerClose(void* hmx) {
    auto fn = (PFN_mixerClose)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "mixerClose") : nullptr);
    return fn ? fn(hmx) : 0;
}
extern "C" UINT WINAPI mixerGetControlDetailsA(void* hmxObj, LPVOID pmxcd, DWORD fdwDetails) {
    auto fn = (PFN_mixerGetControlDetailsA)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "mixerGetControlDetailsA") : nullptr);
    return fn ? fn(hmxObj, pmxcd, fdwDetails) : 0;
}
extern "C" UINT WINAPI mixerGetControlDetailsW(void* hmxObj, LPVOID pmxcd, DWORD fdwDetails) {
    auto fn = (PFN_mixerGetControlDetailsW)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "mixerGetControlDetailsW") : nullptr);
    return fn ? fn(hmxObj, pmxcd, fdwDetails) : 0;
}
extern "C" UINT WINAPI mixerGetDevCapsA(UINT_PTR uMxId, LPVOID pmxcaps, UINT cbmxcaps) {
    auto fn = (PFN_mixerGetDevCapsA)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "mixerGetDevCapsA") : nullptr);
    return fn ? fn(uMxId, pmxcaps, cbmxcaps) : 0;
}

/* 101-120 */
extern "C" UINT WINAPI mixerGetDevCapsW(UINT_PTR uMxId, LPVOID pmxcaps, UINT cbmxcaps) {
    auto fn = (PFN_mixerGetDevCapsW)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "mixerGetDevCapsW") : nullptr);
    return fn ? fn(uMxId, pmxcaps, cbmxcaps) : 0;
}
extern "C" UINT WINAPI mixerGetID(void* hmxobj, PUINT puMxId, DWORD fdwId) {
    auto fn = (PFN_mixerGetID)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "mixerGetID") : nullptr);
    return fn ? fn(hmxobj, puMxId, fdwId) : 0;
}
extern "C" UINT WINAPI mixerGetLineControlsA(void* hmxobj, LPVOID pmxlc, DWORD fdwControls) {
    auto fn = (PFN_mixerGetLineControlsA)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "mixerGetLineControlsA") : nullptr);
    return fn ? fn(hmxobj, pmxlc, fdwControls) : 0;
}
extern "C" UINT WINAPI mixerGetLineControlsW(void* hmxobj, LPVOID pmxlc, DWORD fdwControls) {
    auto fn = (PFN_mixerGetLineControlsW)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "mixerGetLineControlsW") : nullptr);
    return fn ? fn(hmxobj, pmxlc, fdwControls) : 0;
}
extern "C" UINT WINAPI mixerGetLineInfoA(void* hmxobj, LPVOID pmxl, DWORD fdwInfo) {
    auto fn = (PFN_mixerGetLineInfoA)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "mixerGetLineInfoA") : nullptr);
    return fn ? fn(hmxobj, pmxl, fdwInfo) : 0;
}
extern "C" UINT WINAPI mixerGetLineInfoW(void* hmxobj, LPVOID pmxl, DWORD fdwInfo) {
    auto fn = (PFN_mixerGetLineInfoW)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "mixerGetLineInfoW") : nullptr);
    return fn ? fn(hmxobj, pmxl, fdwInfo) : 0;
}
extern "C" UINT WINAPI mixerGetNumDevs(void) {
    auto fn = (PFN_mixerGetNumDevs)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "mixerGetNumDevs") : nullptr);
    return fn ? fn() : 0;
}
extern "C" DWORD WINAPI mixerMessage(void* hmx, UINT uMsg, DWORD_PTR dwParam1, DWORD_PTR dwParam2) {
    auto fn = (PFN_mixerMessage)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "mixerMessage") : nullptr);
    return fn ? fn(hmx, uMsg, dwParam1, dwParam2) : 0;
}
extern "C" UINT WINAPI mixerOpen(void* phmx, UINT uMxId, DWORD_PTR dwCallback,
                                 DWORD_PTR dwInstance, DWORD fdwOpen) {
    auto fn = (PFN_mixerOpen)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "mixerOpen") : nullptr);
    return fn ? fn(phmx, uMxId, dwCallback, dwInstance, fdwOpen) : 0;
}
extern "C" UINT WINAPI mixerSetControlDetails(void* hmxobj, LPVOID pmxcd, DWORD fdwDetails) {
    auto fn = (PFN_mixerSetControlDetails)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "mixerSetControlDetails") : nullptr);
    return fn ? fn(hmxobj, pmxcd, fdwDetails) : 0;
}
extern "C" HTASK WINAPI mmGetCurrentTask(void) {
    auto fn = (PFN_mmGetCurrentTask)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "mmGetCurrentTask") : nullptr);
    return fn ? fn() : (HTASK)0;
}
extern "C" LPVOID WINAPI mmTaskBlock(DWORD_PTR hTask) {
    auto fn = (PFN_mmTaskBlock)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "mmTaskBlock") : nullptr);
    return fn ? fn(hTask) : nullptr;
}
extern "C" DWORD WINAPI mmTaskCreate(LPVOID lpfn, LPVOID pStack, DWORD dwFlags, LPDWORD lpdwThreadId) {
    auto fn = (PFN_mmTaskCreate)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "mmTaskCreate") : nullptr);
    return fn ? fn(lpfn, pStack, dwFlags, lpdwThreadId) : 0;
}
extern "C" void WINAPI mmTaskSignal(DWORD_PTR idTask) {
    auto fn = (PFN_mmTaskSignal)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "mmTaskSignal") : nullptr);
    if (fn) fn(idTask);
}
extern "C" void WINAPI mmTaskYield(void) {
    auto fn = (PFN_mmTaskYield)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "mmTaskYield") : nullptr);
    if (fn) fn();
}
extern "C" LRESULT WINAPI mmioAdvance(void* hmmio, LPVOID pmmioinfo, UINT fuAdvance) {
    auto fn = (PFN_mmioAdvance)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "mmioAdvance") : nullptr);
    return fn ? fn(hmmio, pmmioinfo, fuAdvance) : 0;
}
extern "C" LRESULT WINAPI mmioAscend(void* hmmio, LPVOID pmmck, UINT fuAscend) {
    auto fn = (PFN_mmioAscend)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "mmioAscend") : nullptr);
    return fn ? fn(hmmio, pmmck, fuAscend) : 0;
}
extern "C" UINT WINAPI mmioClose(void* hmmio, UINT fuClose) {
    auto fn = (PFN_mmioClose)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "mmioClose") : nullptr);
    return fn ? fn(hmmio, fuClose) : 0;
}
extern "C" LRESULT WINAPI mmioCreateChunk(void* hmmio, LPVOID pmmck, UINT fuCreate) {
    auto fn = (PFN_mmioCreateChunk)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "mmioCreateChunk") : nullptr);
    return fn ? fn(hmmio, pmmck, fuCreate) : 0;
}
extern "C" LRESULT WINAPI mmioDescend(void* hmmio, LPVOID pmmck, LPVOID pmmckParent, UINT fuDescend) {
    auto fn = (PFN_mmioDescend)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "mmioDescend") : nullptr);
    return fn ? fn(hmmio, pmmck, pmmckParent, fuDescend) : 0;
}

/* 121-140 */
extern "C" LRESULT WINAPI mmioFlush(void* hmmio, UINT fuFlush) {
    auto fn = (PFN_mmioFlush)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "mmioFlush") : nullptr);
    return fn ? fn(hmmio, fuFlush) : 0;
}
extern "C" LRESULT WINAPI mmioGetInfo(void* hmmio, LPVOID pmmioinfo, UINT fuInfo) {
    auto fn = (PFN_mmioGetInfo)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "mmioGetInfo") : nullptr);
    return fn ? fn(hmmio, pmmioinfo, fuInfo) : 0;
}
extern "C" LPVOID WINAPI mmioInstallIOProc16(DWORD fccIOProc, LPVOID pIOProc, DWORD dwData) {
    auto fn = (PFN_mmioInstallIOProc16)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "mmioInstallIOProc16") : nullptr);
    return fn ? fn(fccIOProc, pIOProc, dwData) : nullptr;
}
extern "C" LPVOID WINAPI mmioInstallIOProcA(DWORD fccIOProc, LPVOID pIOProc, DWORD dwData) {
    auto fn = (PFN_mmioInstallIOProcA)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "mmioInstallIOProcA") : nullptr);
    return fn ? fn(fccIOProc, pIOProc, dwData) : nullptr;
}
extern "C" LPVOID WINAPI mmioInstallIOProcW(DWORD fccIOProc, LPVOID pIOProc, DWORD dwData) {
    auto fn = (PFN_mmioInstallIOProcW)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "mmioInstallIOProcW") : nullptr);
    return fn ? fn(fccIOProc, pIOProc, dwData) : nullptr;
}
extern "C" void* WINAPI mmioOpenA(LPCSTR szFilename, LPVOID lpmmioinfo, DWORD fdwOpen) {
    auto fn = (PFN_mmioOpenA)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "mmioOpenA") : nullptr);
    return fn ? fn(szFilename, lpmmioinfo, fdwOpen) : nullptr;
}
extern "C" void* WINAPI mmioOpenW(LPCWSTR szFilename, LPVOID lpmmioinfo, DWORD fdwOpen) {
    auto fn = (PFN_mmioOpenW)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "mmioOpenW") : nullptr);
    return fn ? fn(szFilename, lpmmioinfo, fdwOpen) : nullptr;
}
extern "C" LRESULT WINAPI mmioRead(void* hmmio, LPSTR pch, LONG cch) {
    auto fn = (PFN_mmioRead)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "mmioRead") : nullptr);
    return fn ? fn(hmmio, pch, cch) : 0;
}
extern "C" UINT WINAPI mmioRenameA(LPCSTR szFilename, LPCSTR szNewFilename, LPVOID lpmmioinfo, DWORD fdwRename) {
    auto fn = (PFN_mmioRenameA)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "mmioRenameA") : nullptr);
    return fn ? fn(szFilename, szNewFilename, lpmmioinfo, fdwRename) : 0;
}
extern "C" LRESULT WINAPI mmioRenameW(LPCWSTR szFilename, LPCWSTR szNewFilename, LPVOID lpmmioinfo, DWORD fdwRename) {
    auto fn = (PFN_mmioRenameW)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "mmioRenameW") : nullptr);
    return fn ? fn(szFilename, szNewFilename, lpmmioinfo, fdwRename) : 0;
}
extern "C" LRESULT WINAPI mmioSeek(void* hmmio, LONG lOffset, int iOrigin) {
    auto fn = (PFN_mmioSeek)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "mmioSeek") : nullptr);
    return fn ? fn(hmmio, lOffset, iOrigin) : 0;
}
extern "C" LRESULT WINAPI mmioSendMessage(void* hmmio, UINT uMsg, LPARAM lParam1, LPARAM lParam2) {
    auto fn = (PFN_mmioSendMessage)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "mmioSendMessage") : nullptr);
    return fn ? fn(hmmio, uMsg, lParam1, lParam2) : 0;
}
extern "C" UINT WINAPI mmioSetBuffer(void* hmmio, LPSTR pchBuffer, LONG cchBuffer, UINT fuRead) {
    auto fn = (PFN_mmioSetBuffer)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "mmioSetBuffer") : nullptr);
    return fn ? fn(hmmio, pchBuffer, cchBuffer, fuRead) : 0;
}
extern "C" LRESULT WINAPI mmioSetInfo(void* hmmio, LPVOID pmmioinfo, UINT fuInfo) {
    auto fn = (PFN_mmioSetInfo)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "mmioSetInfo") : nullptr);
    return fn ? fn(hmmio, pmmioinfo, fuInfo) : 0;
}
extern "C" DWORD WINAPI mmioStringToFOURCCA(LPCSTR sz, UINT fuFlags) {
    auto fn = (PFN_mmioStringToFOURCCA)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "mmioStringToFOURCCA") : nullptr);
    return fn ? fn(sz, fuFlags) : 0;
}
extern "C" DWORD WINAPI mmioStringToFOURCCW(LPCWSTR sz, UINT fuFlags) {
    auto fn = (PFN_mmioStringToFOURCCW)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "mmioStringToFOURCCW") : nullptr);
    return fn ? fn(sz, fuFlags) : 0;
}
extern "C" LRESULT WINAPI mmioWrite(void* hmmio, LPCSTR pch, LONG cch) {
    auto fn = (PFN_mmioWrite)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "mmioWrite") : nullptr);
    return fn ? fn(hmmio, pch, cch) : 0;
}
extern "C" UINT WINAPI mmsystemGetVersion(void) {
    auto fn = (PFN_mmsystemGetVersion)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "mmsystemGetVersion") : nullptr);
    return fn ? fn() : 0;
}
extern "C" BOOL WINAPI sndPlaySoundA(LPCSTR pszSound, UINT fuSound) {
    auto fn = (PFN_sndPlaySoundA)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "sndPlaySoundA") : nullptr);
    return fn ? fn(pszSound, fuSound) : FALSE;
}
extern "C" BOOL WINAPI sndPlaySoundW(LPCWSTR pszSound, UINT fuSound) {
    auto fn = (PFN_sndPlaySoundW)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "sndPlaySoundW") : nullptr);
    return fn ? fn(pszSound, fuSound) : FALSE;
}

/* 141-160 */
extern "C" UINT WINAPI timeBeginPeriod(UINT uPeriod) {
    auto fn = (PFN_timeBeginPeriod)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "timeBeginPeriod") : nullptr);
    return fn ? fn(uPeriod) : 0;
}
extern "C" UINT WINAPI timeEndPeriod(UINT uPeriod) {
    auto fn = (PFN_timeEndPeriod)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "timeEndPeriod") : nullptr);
    return fn ? fn(uPeriod) : 0;
}
extern "C" UINT WINAPI timeGetDevCaps(LPVOID ptc, UINT cbtc) {
    auto fn = (PFN_timeGetDevCaps)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "timeGetDevCaps") : nullptr);
    return fn ? fn(ptc, cbtc) : 0;
}
extern "C" UINT WINAPI timeGetSystemTime(LPVOID pmmt, UINT cbmmt) {
    auto fn = (PFN_timeGetSystemTime)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "timeGetSystemTime") : nullptr);
    return fn ? fn(pmmt, cbmmt) : 0;
}
extern "C" DWORD WINAPI timeGetTime(void) {
    auto fn = (PFN_timeGetTime)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "timeGetTime") : nullptr);
    return fn ? fn() : 0;
}
extern "C" UINT WINAPI timeKillEvent(UINT uID) {
    auto fn = (PFN_timeKillEvent)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "timeKillEvent") : nullptr);
    return fn ? fn(uID) : 0;
}
extern "C" UINT WINAPI timeSetEvent(UINT uDelay, UINT uResolution, LPVOID lpTimeProc,
                                    UINT_PTR dwUser, UINT fuEvent) {
    auto fn = (PFN_timeSetEvent)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "timeSetEvent") : nullptr);
    return fn ? fn(uDelay, uResolution, lpTimeProc, dwUser, fuEvent) : 0;
}
extern "C" UINT WINAPI waveInAddBuffer(void* hWaveIn, LPVOID pWaveInHdr, UINT cbWaveInHdr) {
    auto fn = (PFN_waveInAddBuffer)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "waveInAddBuffer") : nullptr);
    return fn ? fn(hWaveIn, pWaveInHdr, cbWaveInHdr) : 0;
}
extern "C" UINT WINAPI waveInClose(void* hWaveIn) {
    auto fn = (PFN_waveInClose)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "waveInClose") : nullptr);
    return fn ? fn(hWaveIn) : 0;
}
extern "C" UINT WINAPI waveInGetDevCapsA(UINT_PTR uDeviceID, LPVOID pwic, UINT cbwic) {
    auto fn = (PFN_waveInGetDevCapsA)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "waveInGetDevCapsA") : nullptr);
    return fn ? fn(uDeviceID, pwic, cbwic) : 0;
}
extern "C" UINT WINAPI waveInGetDevCapsW(UINT_PTR uDeviceID, LPVOID pwic, UINT cbwic) {
    auto fn = (PFN_waveInGetDevCapsW)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "waveInGetDevCapsW") : nullptr);
    return fn ? fn(uDeviceID, pwic, cbwic) : 0;
}
extern "C" BOOL WINAPI waveInGetErrorTextA(UINT uErr, LPSTR pszText, UINT cchText) {
    auto fn = (PFN_waveInGetErrorTextA)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "waveInGetErrorTextA") : nullptr);
    return fn ? fn(uErr, pszText, cchText) : FALSE;
}
extern "C" BOOL WINAPI waveInGetErrorTextW(UINT uErr, LPWSTR pszText, UINT cchText) {
    auto fn = (PFN_waveInGetErrorTextW)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "waveInGetErrorTextW") : nullptr);
    return fn ? fn(uErr, pszText, cchText) : FALSE;
}
extern "C" UINT WINAPI waveInGetID(void* hWaveIn, LPUINT puDeviceID) {
    auto fn = (PFN_waveInGetID)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "waveInGetID") : nullptr);
    return fn ? fn(hWaveIn, puDeviceID) : 0;
}
extern "C" UINT WINAPI waveInGetNumDevs(void) {
    auto fn = (PFN_waveInGetNumDevs)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "waveInGetNumDevs") : nullptr);
    return fn ? fn() : 0;
}
extern "C" UINT WINAPI waveInGetPosition(void* hWaveIn, LPVOID pmmt, UINT cbmmt) {
    auto fn = (PFN_waveInGetPosition)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "waveInGetPosition") : nullptr);
    return fn ? fn(hWaveIn, pmmt, cbmmt) : 0;
}
extern "C" DWORD WINAPI waveInMessage(void* hWaveIn, UINT uMsg, DWORD_PTR dwParam1, DWORD_PTR dwParam2) {
    auto fn = (PFN_waveInMessage)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "waveInMessage") : nullptr);
    return fn ? fn(hWaveIn, uMsg, dwParam1, dwParam2) : 0;
}
extern "C" UINT WINAPI waveInOpen(void* phWaveIn, UINT uDeviceID, LPVOID pwfx,
                                  DWORD_PTR dwCallback, DWORD_PTR dwCallbackInstance, DWORD fdwOpen) {
    auto fn = (PFN_waveInOpen)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "waveInOpen") : nullptr);
    return fn ? fn(phWaveIn, uDeviceID, pwfx, dwCallback, dwCallbackInstance, fdwOpen) : 0;
}
extern "C" UINT WINAPI waveInPrepareHeader(void* hWaveIn, LPVOID pWaveInHdr, UINT cbWaveInHdr) {
    auto fn = (PFN_waveInPrepareHeader)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "waveInPrepareHeader") : nullptr);
    return fn ? fn(hWaveIn, pWaveInHdr, cbWaveInHdr) : 0;
}
extern "C" UINT WINAPI waveInReset(void* hWaveIn) {
    auto fn = (PFN_waveInReset)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "waveInReset") : nullptr);
    return fn ? fn(hWaveIn) : 0;
}

/* 161-180 */
extern "C" UINT WINAPI waveInStart(void* hWaveIn) {
    auto fn = (PFN_waveInStart)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "waveInStart") : nullptr);
    return fn ? fn(hWaveIn) : 0;
}
extern "C" UINT WINAPI waveInStop(void* hWaveIn) {
    auto fn = (PFN_waveInStop)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "waveInStop") : nullptr);
    return fn ? fn(hWaveIn) : 0;
}
extern "C" UINT WINAPI waveInUnprepareHeader(void* hWaveIn, LPVOID pWaveInHdr, UINT cbWaveInHdr) {
    auto fn = (PFN_waveInUnprepareHeader)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "waveInUnprepareHeader") : nullptr);
    return fn ? fn(hWaveIn, pWaveInHdr, cbWaveInHdr) : 0;
}
extern "C" UINT WINAPI waveOutBreakLoop(void* hWaveOut) {
    auto fn = (PFN_waveOutBreakLoop)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "waveOutBreakLoop") : nullptr);
    return fn ? fn(hWaveOut) : 0;
}
extern "C" UINT WINAPI waveOutClose(void* hWaveOut) {
    auto fn = (PFN_waveOutClose)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "waveOutClose") : nullptr);
    return fn ? fn(hWaveOut) : 0;
}
extern "C" UINT WINAPI waveOutGetDevCapsA(UINT_PTR uDeviceID, LPVOID pwoc, UINT cbwoc) {
    auto fn = (PFN_waveOutGetDevCapsA)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "waveOutGetDevCapsA") : nullptr);
    return fn ? fn(uDeviceID, pwoc, cbwoc) : 0;
}
extern "C" UINT WINAPI waveOutGetDevCapsW(UINT_PTR uDeviceID, LPVOID pwoc, UINT cbwoc) {
    auto fn = (PFN_waveOutGetDevCapsW)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "waveOutGetDevCapsW") : nullptr);
    return fn ? fn(uDeviceID, pwoc, cbwoc) : 0;
}
extern "C" BOOL WINAPI waveOutGetErrorTextA(UINT uErr, LPSTR pszText, UINT cchText) {
    auto fn = (PFN_waveOutGetErrorTextA)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "waveOutGetErrorTextA") : nullptr);
    return fn ? fn(uErr, pszText, cchText) : FALSE;
}
extern "C" BOOL WINAPI waveOutGetErrorTextW(UINT uErr, LPWSTR pszText, UINT cchText) {
    auto fn = (PFN_waveOutGetErrorTextW)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "waveOutGetErrorTextW") : nullptr);
    return fn ? fn(uErr, pszText, cchText) : FALSE;
}
extern "C" UINT WINAPI waveOutGetID(void* hWaveOut, LPUINT puDeviceID) {
    auto fn = (PFN_waveOutGetID)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "waveOutGetID") : nullptr);
    return fn ? fn(hWaveOut, puDeviceID) : 0;
}
extern "C" UINT WINAPI waveOutGetNumDevs(void) {
    auto fn = (PFN_waveOutGetNumDevs)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "waveOutGetNumDevs") : nullptr);
    return fn ? fn() : 0;
}
extern "C" UINT WINAPI waveOutGetPitch(void* hWaveOut, LPDWORD pdwPitch) {
    auto fn = (PFN_waveOutGetPitch)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "waveOutGetPitch") : nullptr);
    return fn ? fn(hWaveOut, pdwPitch) : 0;
}
extern "C" UINT WINAPI waveOutGetPlaybackRate(void* hWaveOut, LPDWORD pdwRate) {
    auto fn = (PFN_waveOutGetPlaybackRate)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "waveOutGetPlaybackRate") : nullptr);
    return fn ? fn(hWaveOut, pdwRate) : 0;
}
extern "C" UINT WINAPI waveOutGetPosition(void* hWaveOut, LPVOID pmmt, UINT cbmmt) {
    auto fn = (PFN_waveOutGetPosition)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "waveOutGetPosition") : nullptr);
    return fn ? fn(hWaveOut, pmmt, cbmmt) : 0;
}
extern "C" UINT WINAPI waveOutGetVolume(void* hWaveOut, LPDWORD pdwVolume) {
    auto fn = (PFN_waveOutGetVolume)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "waveOutGetVolume") : nullptr);
    return fn ? fn(hWaveOut, pdwVolume) : 0;
}
extern "C" DWORD WINAPI waveOutMessage(void* hWaveOut, UINT uMsg, DWORD_PTR dwParam1, DWORD_PTR dwParam2) {
    auto fn = (PFN_waveOutMessage)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "waveOutMessage") : nullptr);
    return fn ? fn(hWaveOut, uMsg, dwParam1, dwParam2) : 0;
}
extern "C" UINT WINAPI waveOutOpen(void* phWaveOut, UINT uDeviceID, LPVOID pwfx,
                                   DWORD_PTR dwCallback, DWORD_PTR dwCallbackInstance, DWORD fdwOpen) {
    auto fn = (PFN_waveOutOpen)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "waveOutOpen") : nullptr);
    return fn ? fn(phWaveOut, uDeviceID, pwfx, dwCallback, dwCallbackInstance, fdwOpen) : 0;
}
extern "C" UINT WINAPI waveOutPause(void* hWaveOut) {
    auto fn = (PFN_waveOutPause)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "waveOutPause") : nullptr);
    return fn ? fn(hWaveOut) : 0;
}
extern "C" UINT WINAPI waveOutPrepareHeader(void* hWaveOut, LPVOID pWaveOutHdr, UINT cbWaveOutHdr) {
    auto fn = (PFN_waveOutPrepareHeader)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "waveOutPrepareHeader") : nullptr);
    return fn ? fn(hWaveOut, pWaveOutHdr, cbWaveOutHdr) : 0;
}
extern "C" UINT WINAPI waveOutReset(void* hWaveOut) {
    auto fn = (PFN_waveOutReset)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "waveOutReset") : nullptr);
    return fn ? fn(hWaveOut) : 0;
}

extern "C" UINT WINAPI waveOutRestart(LONG p0) {
    if (!LoadRealWinMM()) return 0;
    typedef UINT (WINAPI *PFN)(LONG);
    PFN fn = (PFN)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "waveOutRestart") : nullptr);
    if (!fn) return 0;
    return fn(p0);
}

extern "C" UINT WINAPI waveOutSetPitch(LONG p0, LONG p1) {
    if (!LoadRealWinMM()) return 0;
    typedef UINT (WINAPI *PFN)(LONG, LONG);
    PFN fn = (PFN)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "waveOutSetPitch") : nullptr);
    if (!fn) return 0;
    return fn(p0, p1);
}

extern "C" UINT WINAPI waveOutSetPlaybackRate(LONG p0, LONG p1) {
    if (!LoadRealWinMM()) return 0;
    typedef UINT (WINAPI *PFN)(LONG, LONG);
    PFN fn = (PFN)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "waveOutSetPlaybackRate") : nullptr);
    if (!fn) return 0;
    return fn(p0, p1);
}

extern "C" UINT WINAPI waveOutSetVolume(LONG p0, LONG p1) {
    if (!LoadRealWinMM()) return 0;
    typedef UINT (WINAPI *PFN)(LONG, LONG);
    PFN fn = (PFN)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "waveOutSetVolume") : nullptr);
    if (!fn) return 0;
    return fn(p0, p1);
}

extern "C" UINT WINAPI waveOutUnprepareHeader(LONG p0, LPVOID p1, LONG p2) {
    if (!LoadRealWinMM()) return 0;
    typedef UINT (WINAPI *PFN)(LONG, LPVOID, LONG);
    PFN fn = (PFN)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "waveOutUnprepareHeader") : nullptr);
    if (!fn) return 0;
    return fn(p0, p1, p2);
}

extern "C" UINT WINAPI waveOutWrite(LONG p0, LPVOID p1, LONG p2) {
    if (!LoadRealWinMM()) return 0;
    typedef UINT (WINAPI *PFN)(LONG, LPVOID, LONG);
    PFN fn = (PFN)(LoadRealWinMM() ? GetProcAddress(g_winmm_module, "waveOutWrite") : nullptr);
    if (!fn) return 0;
    return fn(p0, p1, p2);
}
