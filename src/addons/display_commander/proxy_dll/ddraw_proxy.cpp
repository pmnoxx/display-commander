/*
 * DDraw Proxy Functions
 * Forwards DirectDraw calls to the real system ddraw.dll
 */

#include <Windows.h>
#include <ddraw.h>
#include <string>

#include "ddraw_proxy_init.hpp"
#include "../utils/logging.hpp"

typedef HRESULT(WINAPI* PFN_DirectDrawCreate)(GUID* lpGUID, LPDIRECTDRAW* lplpDD, IUnknown* pUnkOuter);
typedef HRESULT(WINAPI* PFN_DirectDrawCreateEx)(GUID* lpGuid, LPVOID* lplpDD, REFIID iid, IUnknown* pUnkOuter);

static HMODULE g_ddraw_module = nullptr;

static bool LoadRealDDraw() {
    if (g_ddraw_module != nullptr) return true;

    WCHAR system_path[MAX_PATH];
    if (GetSystemDirectoryW(system_path, MAX_PATH) == 0) {
        LogError("[ddraw_proxy] GetSystemDirectoryW failed");
        return false;
    }
    std::wstring ddraw_path = std::wstring(system_path) + L"\\ddraw.dll";

    g_ddraw_module = LoadLibraryW(ddraw_path.c_str());
    if (g_ddraw_module == nullptr) {
        LogError("[ddraw_proxy] Failed to load ddraw.dll from system directory");
        return false;
    }

    LogInfo("[ddraw_proxy] Loaded ddraw.dll (system)");
    return true;
}

void LoadRealDDrawFromDllMain() { LoadRealDDraw(); }

extern "C" HRESULT WINAPI DirectDrawCreate(GUID* lpGUID, LPDIRECTDRAW* lplpDD, IUnknown* pUnkOuter) {
    if (!LoadRealDDraw()) return DDERR_GENERIC;

    auto func = reinterpret_cast<PFN_DirectDrawCreate>(GetProcAddress(g_ddraw_module, "DirectDrawCreate"));
    if (func == nullptr) return DDERR_GENERIC;

    return func(lpGUID, lplpDD, pUnkOuter);
}

extern "C" HRESULT WINAPI DirectDrawCreateEx(GUID* lpGuid, LPVOID* lplpDD, REFIID iid, IUnknown* pUnkOuter) {
    if (!LoadRealDDraw()) return DDERR_GENERIC;

    auto func = reinterpret_cast<PFN_DirectDrawCreateEx>(GetProcAddress(g_ddraw_module, "DirectDrawCreateEx"));
    if (func == nullptr) return DDERR_GENERIC;

    return func(lpGuid, lplpDD, iid, pUnkOuter);
}
