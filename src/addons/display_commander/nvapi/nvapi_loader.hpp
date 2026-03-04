#pragma once

// Source Code <Display Commander>
// nvapi_loader.hpp — load system nvapi64.dll at runtime (no static link to nvapi64.lib).

// Windows.h first so SAL (__in, __inout, etc.) is defined before NVAPI headers.
#include <Windows.h>

// Libraries <NVAPI> — types and constants only; no link dependency.
// Clang does not support Microsoft __success SAL; neutralize so NVAPI_INTERFACE parses.
#if defined(__clang__)
#define __success(x)
#endif
#include <nvapi.h>
#include <NvApiDriverSettings.h>
#if defined(__clang__)
#undef __success
#endif

namespace display_commander::nvapi_loader {

// Function pointer types for NVAPI functions we resolve via nvapi_QueryInterface.
using NvAPI_Initialize_pfn = NvAPI_Status(__cdecl*)(void);
using NvAPI_GetErrorMessage_pfn = NvAPI_Status(__cdecl*)(NvAPI_Status, NvAPI_ShortString);
using NvAPI_SYS_GetDriverAndBranchVersion_pfn = NvAPI_Status(__cdecl*)(NvU32* pDriverVersion, NvAPI_ShortString pBuildBranchString);
using NvAPI_DISP_GetAdaptiveSyncData_pfn = NvAPI_Status(__cdecl*)(NvU32 displayId, NV_GET_ADAPTIVE_SYNC_DATA* pGetAdaptiveSyncData);
using NvAPI_DISP_GetDisplayIdByDisplayName_pfn = NvAPI_Status(__cdecl*)(const char* displayName, NvU32* displayId);
using NvAPI_Disp_GetVRRInfo_pfn = NvAPI_Status(__cdecl*)(NvU32 displayId, NV_GET_VRR_INFO* pVrrInfo);

using NvAPI_DRS_CreateSession_pfn = NvAPI_Status(__cdecl*)(NvDRSSessionHandle* phSession);
using NvAPI_DRS_DestroySession_pfn = NvAPI_Status(__cdecl*)(NvDRSSessionHandle hSession);
using NvAPI_DRS_LoadSettings_pfn = NvAPI_Status(__cdecl*)(NvDRSSessionHandle hSession);
using NvAPI_DRS_SaveSettings_pfn = NvAPI_Status(__cdecl*)(NvDRSSessionHandle hSession);
using NvAPI_DRS_GetProfileInfo_pfn = NvAPI_Status(__cdecl*)(NvDRSSessionHandle hSession, NvDRSProfileHandle hProfile, NVDRS_PROFILE* pProfileInfo);
using NvAPI_DRS_CreateProfile_pfn = NvAPI_Status(__cdecl*)(NvDRSSessionHandle hSession, NVDRS_PROFILE* pProfileInfo, NvDRSProfileHandle* phProfile);
using NvAPI_DRS_DeleteProfile_pfn = NvAPI_Status(__cdecl*)(NvDRSSessionHandle hSession, NvDRSProfileHandle hProfile);
using NvAPI_DRS_FindApplicationByName_pfn = NvAPI_Status(__cdecl*)(NvDRSSessionHandle hSession, NvAPI_UnicodeString appName, NvDRSProfileHandle* phProfile, NVDRS_APPLICATION* pApplication);
using NvAPI_DRS_CreateApplication_pfn = NvAPI_Status(__cdecl*)(NvDRSSessionHandle hSession, NvDRSProfileHandle hProfile, NVDRS_APPLICATION* pApplication);
using NvAPI_DRS_GetSetting_pfn = NvAPI_Status(__cdecl*)(NvDRSSessionHandle hSession, NvDRSProfileHandle hProfile, NvU32 settingId, NVDRS_SETTING* pSetting);
using NvAPI_DRS_SetSetting_pfn = NvAPI_Status(__cdecl*)(NvDRSSessionHandle hSession, NvDRSProfileHandle hProfile, NVDRS_SETTING* pSetting);
using NvAPI_DRS_EnumSettings_pfn = NvAPI_Status(__cdecl*)(NvDRSSessionHandle hSession, NvDRSProfileHandle hProfile, NvU32 startIndex, NvU32* pSettingCount, NVDRS_SETTING* pSettings);
using NvAPI_DRS_EnumAvailableSettingIds_pfn = NvAPI_Status(__cdecl*)(NvU32* pSettingIds, NvU32* pSettingIdsCount);
using NvAPI_DRS_EnumAvailableSettingValues_pfn = NvAPI_Status(__cdecl*)(NvU32 settingId, NvU32* pMaxNumValues, NVDRS_SETTING_VALUES* pSettingValues);
using NvAPI_DRS_GetSettingIdFromName_pfn = NvAPI_Status(__cdecl*)(NvAPI_UnicodeString settingName, NvU32* pSettingId);
using NvAPI_DRS_GetSettingNameFromId_pfn = NvAPI_Status(__cdecl*)(NvU32 settingId, NvAPI_UnicodeString* pSettingName);
using NvAPI_DRS_DeleteProfileSetting_pfn = NvAPI_Status(__cdecl*)(NvDRSSessionHandle hSession, NvDRSProfileHandle hProfile, NvU32 settingId);

struct NvApiPtrs {
    NvAPI_Initialize_pfn Initialize = nullptr;
    NvAPI_GetErrorMessage_pfn GetErrorMessage = nullptr;
    NvAPI_SYS_GetDriverAndBranchVersion_pfn SYS_GetDriverAndBranchVersion = nullptr;
    NvAPI_DISP_GetAdaptiveSyncData_pfn DISP_GetAdaptiveSyncData = nullptr;
    NvAPI_DISP_GetDisplayIdByDisplayName_pfn DISP_GetDisplayIdByDisplayName = nullptr;
    NvAPI_Disp_GetVRRInfo_pfn Disp_GetVRRInfo = nullptr;

    NvAPI_DRS_CreateSession_pfn DRS_CreateSession = nullptr;
    NvAPI_DRS_DestroySession_pfn DRS_DestroySession = nullptr;
    NvAPI_DRS_LoadSettings_pfn DRS_LoadSettings = nullptr;
    NvAPI_DRS_SaveSettings_pfn DRS_SaveSettings = nullptr;
    NvAPI_DRS_GetProfileInfo_pfn DRS_GetProfileInfo = nullptr;
    NvAPI_DRS_CreateProfile_pfn DRS_CreateProfile = nullptr;
    NvAPI_DRS_DeleteProfile_pfn DRS_DeleteProfile = nullptr;
    NvAPI_DRS_FindApplicationByName_pfn DRS_FindApplicationByName = nullptr;
    NvAPI_DRS_CreateApplication_pfn DRS_CreateApplication = nullptr;
    NvAPI_DRS_GetSetting_pfn DRS_GetSetting = nullptr;
    NvAPI_DRS_SetSetting_pfn DRS_SetSetting = nullptr;
    NvAPI_DRS_EnumSettings_pfn DRS_EnumSettings = nullptr;
    NvAPI_DRS_EnumAvailableSettingIds_pfn DRS_EnumAvailableSettingIds = nullptr;
    NvAPI_DRS_EnumAvailableSettingValues_pfn DRS_EnumAvailableSettingValues = nullptr;
    NvAPI_DRS_GetSettingIdFromName_pfn DRS_GetSettingIdFromName = nullptr;
    NvAPI_DRS_GetSettingNameFromId_pfn DRS_GetSettingNameFromId = nullptr;
    NvAPI_DRS_DeleteProfileSetting_pfn DRS_DeleteProfileSetting = nullptr;
};

// Load nvapi64.dll (or nvapi.dll on x86), resolve nvapi_QueryInterface, call NvAPI_Initialize, fill NvApiPtrs.
// Thread-safe: uses internal sync; safe to call from multiple threads. Returns true if loaded and initialized.
bool Load();

// True if Load() succeeded.
bool IsLoaded();

// Valid only after Load() returns true. Do not call from multiple threads before Load() has completed.
const NvApiPtrs* Ptrs();

}  // namespace display_commander::nvapi_loader
