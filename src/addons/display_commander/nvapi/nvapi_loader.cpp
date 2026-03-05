// Source Code <Display Commander>
#include "nvapi_loader.hpp"
#include "../utils/logging.hpp"
#include "../utils/srwlock_wrapper.hpp"

#include <atomic>

#include <Windows.h>

namespace display_commander::nvapi_loader {

namespace {

using NvAPI_QueryInterface_pfn = void*(__cdecl*)(NvU32);

// IDs from external/nvapi/nvapi_interface.h
static constexpr NvU32 ID_Initialize = 0x0150e828;
static constexpr NvU32 ID_GetErrorMessage = 0x6c2d048c;
static constexpr NvU32 ID_SYS_GetDriverAndBranchVersion = 0x2926aaad;
static constexpr NvU32 ID_DISP_GetAdaptiveSyncData = 0xb73d1ee9;
static constexpr NvU32 ID_DISP_GetDisplayIdByDisplayName = 0xae457190;
static constexpr NvU32 ID_Disp_GetVRRInfo = 0xdf8fda57;
static constexpr NvU32 ID_DRS_CreateSession = 0x0694d52e;
static constexpr NvU32 ID_DRS_DestroySession = 0xdad9cff8;
static constexpr NvU32 ID_DRS_LoadSettings = 0x375dbd6b;
static constexpr NvU32 ID_DRS_SaveSettings = 0xfcbc7e14;
static constexpr NvU32 ID_DRS_GetProfileInfo = 0x61cd6fd6;
static constexpr NvU32 ID_DRS_CreateProfile = 0xcc176068;
static constexpr NvU32 ID_DRS_DeleteProfile = 0x17093206;
static constexpr NvU32 ID_DRS_FindApplicationByName = 0xeee566b2;
static constexpr NvU32 ID_DRS_CreateApplication = 0x4347a9de;
static constexpr NvU32 ID_DRS_GetSetting = 0x73bf8338;
static constexpr NvU32 ID_DRS_SetSetting = 0x577dd202;
static constexpr NvU32 ID_DRS_EnumSettings = 0xae3039da;
static constexpr NvU32 ID_DRS_EnumSettingsInternal = 0xCFD6983E;
static constexpr NvU32 ID_DRS_EnumAvailableSettingIds = 0xf020614a;
static constexpr NvU32 ID_DRS_EnumAvailableSettingIdsInternal = 0xE5DE48E5;
static constexpr NvU32 ID_DRS_EnumAvailableSettingValues = 0x2ec39f90;
static constexpr NvU32 ID_DRS_GetSettingIdFromName = 0xcb7309cd;
static constexpr NvU32 ID_DRS_GetSettingNameFromId = 0xd61cbe6e;
static constexpr NvU32 ID_DRS_DeleteProfileSetting = 0xe4a26362;

NvApiPtrs g_ptrs;
SRWLOCK g_load_lock = SRWLOCK_INIT;
std::atomic<bool> g_loaded{false};
std::atomic<bool> g_load_failed{false};

template <typename T>
static T Query(NvAPI_QueryInterface_pfn query, NvU32 id) {
    void* p = query(id);
    return reinterpret_cast<T>(p);
}

}  // namespace

bool Load() {
    if (g_loaded.load(std::memory_order_acquire)) {
        return true;
    }
    if (g_load_failed.load(std::memory_order_acquire)) {
        return false;
    }

    utils::SRWLockExclusive lock(g_load_lock);
    if (g_loaded.load(std::memory_order_relaxed)) {
        return true;
    }
    if (g_load_failed.load(std::memory_order_relaxed)) {
        return false;
    }

#if defined(_M_AMD64) || defined(__x86_64__)
    const char* dll_name = "nvapi64.dll";
#else
    const char* dll_name = "nvapi.dll";
#endif
    HMODULE mod = LoadLibraryA(dll_name);
    if (!mod) {
        LogWarn("[nvapi_loader] LoadLibraryA(%s) failed", dll_name);
        g_load_failed.store(true, std::memory_order_release);
        return false;
    }

    auto* query = reinterpret_cast<NvAPI_QueryInterface_pfn>(GetProcAddress(mod, "nvapi_QueryInterface"));
    if (!query) {
        LogWarn("[nvapi_loader] GetProcAddress(nvapi_QueryInterface) failed");
        FreeLibrary(mod);
        g_load_failed.store(true, std::memory_order_release);
        return false;
    }

    g_ptrs.Initialize = Query<NvAPI_Initialize_pfn>(query, ID_Initialize);
    g_ptrs.GetErrorMessage = Query<NvAPI_GetErrorMessage_pfn>(query, ID_GetErrorMessage);
    g_ptrs.SYS_GetDriverAndBranchVersion =
        Query<NvAPI_SYS_GetDriverAndBranchVersion_pfn>(query, ID_SYS_GetDriverAndBranchVersion);
    g_ptrs.DISP_GetAdaptiveSyncData = Query<NvAPI_DISP_GetAdaptiveSyncData_pfn>(query, ID_DISP_GetAdaptiveSyncData);
    g_ptrs.DISP_GetDisplayIdByDisplayName =
        Query<NvAPI_DISP_GetDisplayIdByDisplayName_pfn>(query, ID_DISP_GetDisplayIdByDisplayName);
    g_ptrs.Disp_GetVRRInfo = Query<NvAPI_Disp_GetVRRInfo_pfn>(query, ID_Disp_GetVRRInfo);
    g_ptrs.DRS_CreateSession = Query<NvAPI_DRS_CreateSession_pfn>(query, ID_DRS_CreateSession);
    g_ptrs.DRS_DestroySession = Query<NvAPI_DRS_DestroySession_pfn>(query, ID_DRS_DestroySession);
    g_ptrs.DRS_LoadSettings = Query<NvAPI_DRS_LoadSettings_pfn>(query, ID_DRS_LoadSettings);
    g_ptrs.DRS_SaveSettings = Query<NvAPI_DRS_SaveSettings_pfn>(query, ID_DRS_SaveSettings);
    g_ptrs.DRS_GetProfileInfo = Query<NvAPI_DRS_GetProfileInfo_pfn>(query, ID_DRS_GetProfileInfo);
    g_ptrs.DRS_CreateProfile = Query<NvAPI_DRS_CreateProfile_pfn>(query, ID_DRS_CreateProfile);
    g_ptrs.DRS_DeleteProfile = Query<NvAPI_DRS_DeleteProfile_pfn>(query, ID_DRS_DeleteProfile);
    g_ptrs.DRS_FindApplicationByName = Query<NvAPI_DRS_FindApplicationByName_pfn>(query, ID_DRS_FindApplicationByName);
    g_ptrs.DRS_CreateApplication = Query<NvAPI_DRS_CreateApplication_pfn>(query, ID_DRS_CreateApplication);
    g_ptrs.DRS_GetSetting = Query<NvAPI_DRS_GetSetting_pfn>(query, ID_DRS_GetSetting);
    g_ptrs.DRS_SetSetting = Query<NvAPI_DRS_SetSetting_pfn>(query, ID_DRS_SetSetting);
    // Prefer internal variants (may enumerate more settings, e.g. internal/encrypted); fallback to public.
    g_ptrs.DRS_EnumSettings = Query<NvAPI_DRS_EnumSettings_pfn>(query, ID_DRS_EnumSettingsInternal);
    if (!g_ptrs.DRS_EnumSettings) {
        g_ptrs.DRS_EnumSettings = Query<NvAPI_DRS_EnumSettings_pfn>(query, ID_DRS_EnumSettings);
    }
    g_ptrs.DRS_EnumAvailableSettingIds =
        Query<NvAPI_DRS_EnumAvailableSettingIds_pfn>(query, ID_DRS_EnumAvailableSettingIdsInternal);
    if (!g_ptrs.DRS_EnumAvailableSettingIds) {
        g_ptrs.DRS_EnumAvailableSettingIds =
            Query<NvAPI_DRS_EnumAvailableSettingIds_pfn>(query, ID_DRS_EnumAvailableSettingIds);
    }
    g_ptrs.DRS_EnumAvailableSettingValues =
        Query<NvAPI_DRS_EnumAvailableSettingValues_pfn>(query, ID_DRS_EnumAvailableSettingValues);
    g_ptrs.DRS_GetSettingIdFromName = Query<NvAPI_DRS_GetSettingIdFromName_pfn>(query, ID_DRS_GetSettingIdFromName);
    g_ptrs.DRS_GetSettingNameFromId = Query<NvAPI_DRS_GetSettingNameFromId_pfn>(query, ID_DRS_GetSettingNameFromId);
    g_ptrs.DRS_DeleteProfileSetting = Query<NvAPI_DRS_DeleteProfileSetting_pfn>(query, ID_DRS_DeleteProfileSetting);

    if (!g_ptrs.Initialize) {
        LogWarn("[nvapi_loader] NvAPI_Initialize not resolved");
        FreeLibrary(mod);
        g_load_failed.store(true, std::memory_order_release);
        return false;
    }

    NvAPI_Status st = g_ptrs.Initialize();
    if (st != NVAPI_OK) {
        LogWarn("[nvapi_loader] NvAPI_Initialize() failed: %d", st);
        FreeLibrary(mod);
        g_load_failed.store(true, std::memory_order_release);
        return false;
    }

    LogInfo("[nvapi_loader] Loaded %s and initialized NVAPI", dll_name);
    g_loaded.store(true, std::memory_order_release);
    return true;
}

bool IsLoaded() { return g_loaded.load(std::memory_order_acquire); }

const NvApiPtrs* Ptrs() { return g_loaded.load(std::memory_order_acquire) ? &g_ptrs : nullptr; }

}  // namespace display_commander::nvapi_loader
