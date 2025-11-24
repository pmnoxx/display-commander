#pragma once

#include <windows.h>
#include <mmdeviceapi.h>
#include <audiopolicy.h>
#include <roapi.h>
#include <winstring.h>

// Minimal declaration of Windows internal IAudioPolicyConfigFactory
// Based on public reverse engineered information and Special K usage.
// Only the methods we use are actually called, the rest are placeholders
// to keep the vtable layout compatible.
interface DECLSPEC_UUID("ab3d4648-e242-459f-b02f-541c70306324") IAudioPolicyConfigFactory;

interface IAudioPolicyConfigFactory
{
public:
    // IInspectable (placeholders)
    virtual HRESULT STDMETHODCALLTYPE __incomplete__QueryInterface() = 0;
    virtual HRESULT STDMETHODCALLTYPE __incomplete__AddRef() = 0;
    virtual HRESULT STDMETHODCALLTYPE __incomplete__Release() = 0;
    virtual HRESULT STDMETHODCALLTYPE __incomplete__GetIids() = 0;
    virtual HRESULT STDMETHODCALLTYPE __incomplete__GetRuntimeClassName() = 0;
    virtual HRESULT STDMETHODCALLTYPE __incomplete__GetTrustLevel() = 0;

    // Unused members (placeholder only – keep order/slots)
    virtual HRESULT STDMETHODCALLTYPE __incomplete__add_CtxVolumeChange() = 0;
    virtual HRESULT STDMETHODCALLTYPE __incomplete__remove_CtxVolumeChanged() = 0;
    virtual HRESULT STDMETHODCALLTYPE __incomplete__add_RingerVibrateStateChanged() = 0;
    virtual HRESULT STDMETHODCALLTYPE __incomplete__remove_RingerVibrateStateChange() = 0;
    virtual HRESULT STDMETHODCALLTYPE __incomplete__SetVolumeGroupGainForId() = 0;
    virtual HRESULT STDMETHODCALLTYPE __incomplete__GetVolumeGroupGainForId() = 0;
    virtual HRESULT STDMETHODCALLTYPE __incomplete__GetActiveVolumeGroupForEndpointId() = 0;
    virtual HRESULT STDMETHODCALLTYPE __incomplete__GetVolumeGroupsForEndpoint() = 0;
    virtual HRESULT STDMETHODCALLTYPE __incomplete__GetCurrentVolumeContext() = 0;
    virtual HRESULT STDMETHODCALLTYPE __incomplete__SetVolumeGroupMuteForId() = 0;
    virtual HRESULT STDMETHODCALLTYPE __incomplete__GetVolumeGroupMuteForId() = 0;
    virtual HRESULT STDMETHODCALLTYPE __incomplete__SetRingerVibrateState() = 0;
    virtual HRESULT STDMETHODCALLTYPE __incomplete__GetRingerVibrateState() = 0;
    virtual HRESULT STDMETHODCALLTYPE __incomplete__SetPreferredChatApplication() = 0;
    virtual HRESULT STDMETHODCALLTYPE __incomplete__ResetPreferredChatApplication() = 0;
    virtual HRESULT STDMETHODCALLTYPE __incomplete__GetPreferredChatApplication() = 0;
    virtual HRESULT STDMETHODCALLTYPE __incomplete__GetCurrentChatApplications() = 0;
    virtual HRESULT STDMETHODCALLTYPE __incomplete__add_ChatContextChanged() = 0;
    virtual HRESULT STDMETHODCALLTYPE __incomplete__remove_ChatContextChanged() = 0;

    // Methods we actually use
    virtual HRESULT STDMETHODCALLTYPE SetPersistedDefaultAudioEndpoint(
        UINT      processId,
        EDataFlow flow,
        ERole     role,
        HSTRING   deviceId) = 0;

    virtual HRESULT STDMETHODCALLTYPE GetPersistedDefaultAudioEndpoint(
        UINT       processId,
        EDataFlow  flow,
        int        roleMask,
        _Outptr_result_maybenull_ _Result_nullonfailure_ HSTRING *deviceId) = 0;

    virtual HRESULT STDMETHODCALLTYPE ClearAllPersistedApplicationDefaultEndpoints() = 0;
};


