/*
 * DInput8 Proxy Functions
 * Forwards DInput8 calls to the real system dinput8.dll
 */

// Source Code <Display Commander>
#include "../utils/logging.hpp"
#include "dinput8_proxy_init.hpp"

// Libraries <Windows.h>
#include <Windows.h>

// Libraries <Windows> (COM for DllGetClassObject)
#include <objbase.h>

// Libraries <standard C++>
#include <string>

namespace {
HMODULE g_dinput8_module = nullptr;

bool LoadRealDinput8() {
    if (g_dinput8_module != nullptr) return true;

    WCHAR system_path[MAX_PATH];
    if (GetSystemDirectoryW(system_path, MAX_PATH) == 0) {
        LogError("[dinput8_proxy] GetSystemDirectoryW failed");
        return false;
    }
    std::wstring path = std::wstring(system_path) + L"\\dinput8.dll";

    g_dinput8_module = LoadLibraryW(path.c_str());
    if (g_dinput8_module == nullptr) {
        LogError("[dinput8_proxy] Failed to load dinput8.dll from system directory");
        return false;
    }

    LogInfo("[dinput8_proxy] Loaded dinput8.dll (system)");
    return true;
}
}  // namespace

void LoadRealDinput8FromDllMain() { LoadRealDinput8(); }

// Signature matches DirectInput8Create(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN); pointer-sized first arg required for x64.
extern "C" HRESULT WINAPI DirectInput8Create(HINSTANCE p0, DWORD p1, LPVOID p2, LPVOID p3, LPVOID p4) {
    if (!LoadRealDinput8()) return E_FAIL;
    typedef HRESULT(WINAPI* PFN)(HINSTANCE, DWORD, LPVOID, LPVOID, LPVOID);
    auto fn = reinterpret_cast<PFN>(GetProcAddress(g_dinput8_module, "DirectInput8Create"));
    if (fn == nullptr) return E_FAIL;
    return fn(p0, p1, p2, p3, p4);
}

extern "C" HRESULT WINAPI DllCanUnloadNow(void) {
    if (!LoadRealDinput8()) return E_FAIL;
    typedef HRESULT(WINAPI* PFN)(void);
    auto fn = reinterpret_cast<PFN>(GetProcAddress(g_dinput8_module, "DllCanUnloadNow"));
    if (fn == nullptr) return E_FAIL;
    return fn();
}

extern "C" HRESULT WINAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, LPVOID* ppv) {
    if (!LoadRealDinput8()) return E_FAIL;
    typedef HRESULT(WINAPI* PFN)(REFCLSID, REFIID, LPVOID*);
    auto fn = reinterpret_cast<PFN>(GetProcAddress(g_dinput8_module, "DllGetClassObject"));
    if (fn == nullptr) return E_FAIL;
    return fn(rclsid, riid, ppv);
}

extern "C" HRESULT WINAPI DllRegisterServer(void) {
    if (!LoadRealDinput8()) return E_FAIL;
    typedef HRESULT(WINAPI* PFN)(void);
    auto fn = reinterpret_cast<PFN>(GetProcAddress(g_dinput8_module, "DllRegisterServer"));
    if (fn == nullptr) return E_FAIL;
    return fn();
}

extern "C" HRESULT WINAPI DllUnregisterServer(void) {
    if (!LoadRealDinput8()) return E_FAIL;
    typedef HRESULT(WINAPI* PFN)(void);
    auto fn = reinterpret_cast<PFN>(GetProcAddress(g_dinput8_module, "DllUnregisterServer"));
    if (fn == nullptr) return E_FAIL;
    return fn();
}
