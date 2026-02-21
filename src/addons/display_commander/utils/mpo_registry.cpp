#include "mpo_registry.hpp"
#include "logging.hpp"

#include <winreg.h>

#ifndef KEY_WOW64_64KEY
#define KEY_WOW64_64KEY 0x0100
#endif

namespace display_commander::utils {

static const wchar_t kDwmKey[] = L"SOFTWARE\\Microsoft\\Windows\\Dwm";
static const wchar_t kGraphicsDriversKey[] = L"SYSTEM\\CurrentControlSet\\Control\\GraphicsDrivers";
static const wchar_t kOverlayTestMode[] = L"OverlayTestMode";
static const wchar_t kDisableMPO[] = L"DisableMPO";
static const wchar_t kDisableOverlays[] = L"DisableOverlays";

static REGSAM GetRegSamRead() {
    REGSAM sam = KEY_READ;
#if defined(_WIN64)
    sam |= KEY_WOW64_64KEY;
#else
    sam |= KEY_WOW64_32KEY;
#endif
    return sam;
}

static REGSAM GetRegSamWrite() {
    REGSAM sam = KEY_SET_VALUE;
#if defined(_WIN64)
    sam |= KEY_WOW64_64KEY;
#else
    sam |= KEY_WOW64_32KEY;
#endif
    return sam;
}

bool MpoRegistryGetStatus(MpoRegistryStatus* out) {
    if (out == nullptr) {
        return false;
    }
    out->overlay_test_mode_5 = false;
    out->disable_mpo = false;
    out->disable_overlays = false;

    HKEY h_key = nullptr;
    LSTATUS st = RegOpenKeyExW(HKEY_LOCAL_MACHINE, kDwmKey, 0, GetRegSamRead(), &h_key);
    if (st == ERROR_SUCCESS && h_key != nullptr) {
        DWORD value = 0;
        DWORD value_size = sizeof(DWORD);
        DWORD type = REG_DWORD;
        if (RegQueryValueExW(h_key, kOverlayTestMode, nullptr, &type,
                             reinterpret_cast<BYTE*>(&value), &value_size) == ERROR_SUCCESS &&
            type == REG_DWORD && value == 5) {
            out->overlay_test_mode_5 = true;
        }
        RegCloseKey(h_key);
    }

    h_key = nullptr;
    st = RegOpenKeyExW(HKEY_LOCAL_MACHINE, kGraphicsDriversKey, 0, GetRegSamRead(), &h_key);
    if (st == ERROR_SUCCESS && h_key != nullptr) {
        DWORD value = 0;
        DWORD value_size = sizeof(DWORD);
        DWORD type = REG_DWORD;
        if (RegQueryValueExW(h_key, kDisableMPO, nullptr, &type,
                             reinterpret_cast<BYTE*>(&value), &value_size) == ERROR_SUCCESS &&
            type == REG_DWORD && value == 1) {
            out->disable_mpo = true;
        }
        value = 0;
        value_size = sizeof(DWORD);
        type = REG_DWORD;
        if (RegQueryValueExW(h_key, kDisableOverlays, nullptr, &type,
                             reinterpret_cast<BYTE*>(&value), &value_size) == ERROR_SUCCESS &&
            type == REG_DWORD && value == 1) {
            out->disable_overlays = true;
        }
        RegCloseKey(h_key);
    }
    return true;
}

bool MpoRegistrySetOverlayTestMode(bool disabled) {
    HKEY h_key = nullptr;
    LSTATUS st = RegOpenKeyExW(HKEY_LOCAL_MACHINE, kDwmKey, 0, GetRegSamWrite(), &h_key);
    if (st != ERROR_SUCCESS) {
        LogInfo("MPO Registry: Failed to open Dwm key for write, error: %ld (run as administrator?)", static_cast<long>(st));
        return false;
    }
    const DWORD value = disabled ? 5u : 0u;
    st = RegSetValueExW(h_key, kOverlayTestMode, 0, REG_DWORD,
                        reinterpret_cast<const BYTE*>(&value), sizeof(DWORD));
    RegCloseKey(h_key);
    if (st != ERROR_SUCCESS) {
        LogInfo("MPO Registry: Failed to set OverlayTestMode, error: %ld", static_cast<long>(st));
        return false;
    }
    LogInfo("MPO Registry: OverlayTestMode set to %u. Restart your computer for changes to take effect.", value);
    return true;
}

bool MpoRegistrySetDisableMPO(bool disabled) {
    HKEY h_key = nullptr;
    LSTATUS st = RegOpenKeyExW(HKEY_LOCAL_MACHINE, kGraphicsDriversKey, 0, GetRegSamWrite(), &h_key);
    if (st != ERROR_SUCCESS) {
        LogInfo("MPO Registry: Failed to open GraphicsDrivers key for write, error: %ld (run as administrator?)",
                static_cast<long>(st));
        return false;
    }
    const DWORD value = disabled ? 1u : 0u;
    st = RegSetValueExW(h_key, kDisableMPO, 0, REG_DWORD,
                        reinterpret_cast<const BYTE*>(&value), sizeof(DWORD));
    RegCloseKey(h_key);
    if (st != ERROR_SUCCESS) {
        LogInfo("MPO Registry: Failed to set DisableMPO, error: %ld", static_cast<long>(st));
        return false;
    }
    LogInfo("MPO Registry: DisableMPO set to %u. Restart your computer for changes to take effect.", value);
    return true;
}

bool MpoRegistrySetDisableOverlays(bool disabled) {
    HKEY h_key = nullptr;
    LSTATUS st = RegOpenKeyExW(HKEY_LOCAL_MACHINE, kGraphicsDriversKey, 0, GetRegSamWrite(), &h_key);
    if (st != ERROR_SUCCESS) {
        LogInfo("MPO Registry: Failed to open GraphicsDrivers key for write, error: %ld (run as administrator?)",
                static_cast<long>(st));
        return false;
    }
    const DWORD value = disabled ? 1u : 0u;
    st = RegSetValueExW(h_key, kDisableOverlays, 0, REG_DWORD,
                        reinterpret_cast<const BYTE*>(&value), sizeof(DWORD));
    RegCloseKey(h_key);
    if (st != ERROR_SUCCESS) {
        LogInfo("MPO Registry: Failed to set DisableOverlays, error: %ld", static_cast<long>(st));
        return false;
    }
    LogInfo("MPO Registry: DisableOverlays set to %u. Restart your computer for changes to take effect.", value);
    return true;
}

}  // namespace display_commander::utils
