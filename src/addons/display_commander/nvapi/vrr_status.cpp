#include "vrr_status.hpp"

namespace nvapi {

namespace {

bool EnsureNvApiInitialized() {
    static std::atomic<bool> g_inited{false};
    if (g_inited.load(std::memory_order_acquire)) {
        return true;
    }

    const NvAPI_Status st = NvAPI_Initialize();
    if (st != NVAPI_OK) {
        // Don't spam; the caller may query per frame in UI
        return false;
    }

    g_inited.store(true, std::memory_order_release);
    return true;
}

std::string WideToUtf8(const wchar_t *wstr) {
    if (wstr == nullptr) {
        return {};
    }

    const int len = static_cast<int>(wcslen(wstr));
    if (len == 0) {
        return {};
    }

    const int bytes_needed = WideCharToMultiByte(CP_UTF8, 0, wstr, len, nullptr, 0, nullptr, nullptr);
    if (bytes_needed <= 0) {
        return {};
    }

    std::string out;
    out.resize(static_cast<size_t>(bytes_needed));
    WideCharToMultiByte(CP_UTF8, 0, wstr, len, out.data(), bytes_needed, nullptr, nullptr);
    return out;
}

std::string NormalizeDxgiDeviceNameForNvapi(std::string name) {
    // DXGI: "\\.\DISPLAY1" -> NVAPI wants "\\DISPLAY1" (remove ".\")
    if (name.size() >= 4 && name[0] == '\\' && name[1] == '\\' && name[2] == '.' && name[3] == '\\') {
        name.erase(2, 2);
    }
    return name;
}

NvAPI_Status ResolveDisplayIdByNameWithReinit(const std::string &display_name, NvU32 &out_display_id) {
    out_display_id = 0;

    NvAPI_Status st = NvAPI_DISP_GetDisplayIdByDisplayName(display_name.c_str(), &out_display_id);
    if (st == NVAPI_API_NOT_INITIALIZED) {
        // NVAPI may have been unloaded by another feature; try to re-init and retry once.
        const NvAPI_Status init_st = NvAPI_Initialize();
        if (init_st == NVAPI_OK) {
            st = NvAPI_DISP_GetDisplayIdByDisplayName(display_name.c_str(), &out_display_id);
        } else {
            return init_st;
        }
    }
    return st;
}

} // anonymous namespace

bool TryQueryVrrStatusFromDxgiOutputDeviceName(const wchar_t *dxgi_output_device_name, VrrStatus &out_status) {
    out_status = VrrStatus{};

    if (!EnsureNvApiInitialized()) {
        out_status.nvapi_initialized = false;
        return false;
    }
    out_status.nvapi_initialized = true;

    // Try multiple name formats. NVAPI docs mention "\\DISPLAY1", while DXGI provides "\\.\DISPLAY1".
    const std::string raw_name = WideToUtf8(dxgi_output_device_name);
    const std::string nvapi_name = NormalizeDxgiDeviceNameForNvapi(raw_name);

    std::string stripped = raw_name;
    if (stripped.size() >= 4 && stripped[0] == '\\' && stripped[1] == '\\' && stripped[2] == '.' && stripped[3] == '\\') {
        stripped.erase(0, 4); // "DISPLAY1"
    } else if (stripped.size() >= 2 && stripped[0] == '\\' && stripped[1] == '\\') {
        stripped.erase(0, 2); // best-effort
    }

    const std::string candidates[] = {
        nvapi_name, // "\\DISPLAY1"
        raw_name,   // "\\.\DISPLAY1"
        stripped,   // "DISPLAY1"
    };

    NvU32 display_id = 0;
    NvAPI_Status resolve_st = NVAPI_ERROR;
    bool resolved = false;
    for (const auto &candidate : candidates) {
        if (candidate.empty()) {
            continue;
        }
        resolve_st = ResolveDisplayIdByNameWithReinit(candidate, display_id);
        if (resolve_st == NVAPI_OK) {
            out_status.nvapi_display_name = candidate;
            resolved = true;
            break;
        }
    }

    out_status.resolve_status = resolve_st;
    if (!resolved) {
        // Keep the most "NVAPI-like" name for debugging display
        out_status.nvapi_display_name = nvapi_name.empty() ? raw_name : nvapi_name;
        out_status.display_id_resolved = false;
        return false;
    }

    out_status.display_id_resolved = true;
    out_status.display_id = display_id;

    NV_GET_VRR_INFO vrr = {};
    vrr.version = NV_GET_VRR_INFO_VER;

    const NvAPI_Status query_st = NvAPI_Disp_GetVRRInfo(display_id, &vrr);
    out_status.query_status = query_st;
    out_status.vrr_info_queried = true;

    if (query_st != NVAPI_OK) {
        return false;
    }

    out_status.is_vrr_enabled = vrr.bIsVRREnabled != 0;
    out_status.is_vrr_possible = vrr.bIsVRRPossible != 0;
    out_status.is_vrr_requested = vrr.bIsVRRRequested != 0;
    out_status.is_vrr_indicator_enabled = vrr.bIsVRRIndicatorEnabled != 0;
    out_status.is_display_in_vrr_mode = vrr.bIsDisplayInVRRMode != 0;

    return true;
}

} // namespace nvapi


