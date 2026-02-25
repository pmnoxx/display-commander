/*
 * D3D9 Proxy Functions
 * Forwards D3D9 calls to the real system d3d9.dll
 */

#include <Windows.h>
#include <d3d9.h>
#include <unknwn.h>
#include <string>

#include "d3d9_proxy_init.hpp"
#include "../utils/logging.hpp"

// 9on12 types (avoid pulling in d3d9on12.h / d3d12.h)
#define MAX_D3D9ON12_QUEUES 2
typedef struct _D3D9ON12_ARGS_PROXY {
    BOOL Enable9On12;
    IUnknown* pD3D12Device;
    IUnknown* ppD3D12Queues[MAX_D3D9ON12_QUEUES];
    UINT NumQueues;
    UINT NodeMask;
} D3D9ON12_ARGS_PROXY;

typedef IDirect3D9* (WINAPI* PFN_Direct3DCreate9)(UINT SDKVersion);
typedef HRESULT(WINAPI* PFN_Direct3DCreate9Ex)(UINT SDKVersion, IDirect3D9Ex** ppD3D);
typedef IDirect3D9* (WINAPI* PFN_Direct3DCreate9On12)(UINT SDKVersion, D3D9ON12_ARGS_PROXY* pOverrideList,
                                                      UINT NumOverrideEntries);
typedef HRESULT(WINAPI* PFN_Direct3DCreate9On12Ex)(UINT SDKVersion, D3D9ON12_ARGS_PROXY* pOverrideList,
                                                   UINT NumOverrideEntries, IDirect3D9Ex** ppOutputInterface);
typedef int (WINAPI* PFN_D3DPERF_BeginEvent)(D3DCOLOR col, LPCWSTR wszName);
typedef int (WINAPI* PFN_D3DPERF_EndEvent)(void);
typedef DWORD(WINAPI* PFN_D3DPERF_GetStatus)(void);
typedef BOOL(WINAPI* PFN_D3DPERF_QueryRepeatFrame)(void);
typedef void (WINAPI* PFN_D3DPERF_SetMarker)(D3DCOLOR col, LPCWSTR wszName);
typedef void (WINAPI* PFN_D3DPERF_SetOptions)(DWORD dwOptions);
typedef void (WINAPI* PFN_D3DPERF_SetRegion)(D3DCOLOR col, LPCWSTR wszName);
typedef void (WINAPI* PFN_Direct3D9EnableMaximizedWindowedModeShim)(void);
// Ordinal-only exports (16-19, 22-23): forward by ordinal; assume no-arg shims
typedef void (WINAPI* PFN_OrdinalShim)(void);

static HMODULE g_d3d9_module = nullptr;

static bool LoadRealD3D9() {
    if (g_d3d9_module != nullptr) return true;

    WCHAR system_path[MAX_PATH];
    if (GetSystemDirectoryW(system_path, MAX_PATH) == 0) {
        LogError("[d3d9_proxy] GetSystemDirectoryW failed");
        return false;
    }
    std::wstring d3d9_path = std::wstring(system_path) + L"\\d3d9.dll";

    g_d3d9_module = LoadLibraryW(d3d9_path.c_str());
    if (g_d3d9_module == nullptr) {
        LogError("[d3d9_proxy] Failed to load d3d9.dll from system directory");
        return false;
    }

    LogInfo("[d3d9_proxy] Loaded d3d9.dll (system)");
    return true;
}

void LoadRealD3D9FromDllMain() { LoadRealD3D9(); }

static FARPROC GetD3D9ProcByOrdinal(UINT ordinal) {
    if (!LoadRealD3D9()) return nullptr;
    return GetProcAddress(g_d3d9_module, reinterpret_cast<LPCSTR>(static_cast<uintptr_t>(ordinal)));
}

extern "C" IDirect3D9* WINAPI Direct3DCreate9(UINT SDKVersion) {
    if (!LoadRealD3D9()) return nullptr;
    auto func = reinterpret_cast<PFN_Direct3DCreate9>(GetProcAddress(g_d3d9_module, "Direct3DCreate9"));
    if (func == nullptr) return nullptr;
    return func(SDKVersion);
}

extern "C" HRESULT WINAPI Direct3DCreate9Ex(UINT SDKVersion, IDirect3D9Ex** ppD3D) {
    if (!LoadRealD3D9()) return E_FAIL;
    auto func = reinterpret_cast<PFN_Direct3DCreate9Ex>(GetProcAddress(g_d3d9_module, "Direct3DCreate9Ex"));
    if (func == nullptr) return E_FAIL;
    return func(SDKVersion, ppD3D);
}

extern "C" IDirect3D9* WINAPI Direct3DCreate9On12(UINT SDKVersion, void* pOverrideList, UINT NumOverrideEntries) {
    if (!LoadRealD3D9()) return nullptr;
    auto func = reinterpret_cast<PFN_Direct3DCreate9On12>(GetProcAddress(g_d3d9_module, "Direct3DCreate9On12"));
    if (func == nullptr) return nullptr;
    return func(SDKVersion, static_cast<D3D9ON12_ARGS_PROXY*>(pOverrideList), NumOverrideEntries);
}

extern "C" HRESULT WINAPI Direct3DCreate9On12Ex(UINT SDKVersion, void* pOverrideList, UINT NumOverrideEntries,
                                               IDirect3D9Ex** ppOutputInterface) {
    if (!LoadRealD3D9()) return E_FAIL;
    auto func = reinterpret_cast<PFN_Direct3DCreate9On12Ex>(GetProcAddress(g_d3d9_module, "Direct3DCreate9On12Ex"));
    if (func == nullptr) return E_FAIL;
    return func(SDKVersion, static_cast<D3D9ON12_ARGS_PROXY*>(pOverrideList), NumOverrideEntries, ppOutputInterface);
}

extern "C" int WINAPI D3DPERF_BeginEvent(D3DCOLOR col, LPCWSTR wszName) {
    if (!LoadRealD3D9()) return -1;
    auto func = reinterpret_cast<PFN_D3DPERF_BeginEvent>(GetProcAddress(g_d3d9_module, "D3DPERF_BeginEvent"));
    if (func == nullptr) return -1;
    return func(col, wszName);
}

extern "C" int WINAPI D3DPERF_EndEvent(void) {
    if (!LoadRealD3D9()) return -1;
    auto func = reinterpret_cast<PFN_D3DPERF_EndEvent>(GetProcAddress(g_d3d9_module, "D3DPERF_EndEvent"));
    if (func == nullptr) return -1;
    return func();
}

extern "C" DWORD WINAPI D3DPERF_GetStatus(void) {
    if (!LoadRealD3D9()) return 0;
    auto func = reinterpret_cast<PFN_D3DPERF_GetStatus>(GetProcAddress(g_d3d9_module, "D3DPERF_GetStatus"));
    if (func == nullptr) return 0;
    return func();
}

extern "C" BOOL WINAPI D3DPERF_QueryRepeatFrame(void) {
    if (!LoadRealD3D9()) return FALSE;
    auto func = reinterpret_cast<PFN_D3DPERF_QueryRepeatFrame>(GetProcAddress(g_d3d9_module, "D3DPERF_QueryRepeatFrame"));
    if (func == nullptr) return FALSE;
    return func();
}

extern "C" void WINAPI D3DPERF_SetMarker(D3DCOLOR col, LPCWSTR wszName) {
    if (!LoadRealD3D9()) return;
    auto func = reinterpret_cast<PFN_D3DPERF_SetMarker>(GetProcAddress(g_d3d9_module, "D3DPERF_SetMarker"));
    if (func != nullptr) func(col, wszName);
}

extern "C" void WINAPI D3DPERF_SetOptions(DWORD dwOptions) {
    if (!LoadRealD3D9()) return;
    auto func = reinterpret_cast<PFN_D3DPERF_SetOptions>(GetProcAddress(g_d3d9_module, "D3DPERF_SetOptions"));
    if (func != nullptr) func(dwOptions);
}

extern "C" void WINAPI D3DPERF_SetRegion(D3DCOLOR col, LPCWSTR wszName) {
    if (!LoadRealD3D9()) return;
    auto func = reinterpret_cast<PFN_D3DPERF_SetRegion>(GetProcAddress(g_d3d9_module, "D3DPERF_SetRegion"));
    if (func != nullptr) func(col, wszName);
}

extern "C" void WINAPI Direct3D9EnableMaximizedWindowedModeShim(void) {
    if (!LoadRealD3D9()) return;
    auto func = reinterpret_cast<PFN_Direct3D9EnableMaximizedWindowedModeShim>(
        GetProcAddress(g_d3d9_module, "Direct3D9EnableMaximizedWindowedModeShim"));
    if (func != nullptr) func();
}

// Ordinal-only exports: resolve by ordinal and call (assume no-arg shims)
extern "C" void WINAPI Direct3D9ForceHybridEnumeration(void) {
    auto func = reinterpret_cast<PFN_OrdinalShim>(GetD3D9ProcByOrdinal(16));
    if (func != nullptr) func();
}
extern "C" void WINAPI Direct3D9SetMaximizedWindowedModeShim(void) {
    auto func = reinterpret_cast<PFN_OrdinalShim>(GetD3D9ProcByOrdinal(17));
    if (func != nullptr) func();
}
extern "C" void WINAPI Direct3D9SetSwapEffectUpgradeShim(void) {
    auto func = reinterpret_cast<PFN_OrdinalShim>(GetD3D9ProcByOrdinal(18));
    if (func != nullptr) func();
}
extern "C" void WINAPI Direct3D9Force9on12(void) {
    auto func = reinterpret_cast<PFN_OrdinalShim>(GetD3D9ProcByOrdinal(19));
    if (func != nullptr) func();
}
extern "C" void WINAPI Direct3D9SetMaximizedWindowHwndOverride(void) {
    auto func = reinterpret_cast<PFN_OrdinalShim>(GetD3D9ProcByOrdinal(22));
    if (func != nullptr) func();
}
extern "C" void WINAPI Direct3D9SetVendorIDLieFor9on12(void) {
    auto func = reinterpret_cast<PFN_OrdinalShim>(GetD3D9ProcByOrdinal(23));
    if (func != nullptr) func();
}
