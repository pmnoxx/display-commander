#include "pclstats_etw_hooks.hpp"
#include "../globals.hpp"
#include "../settings/main_tab_settings.hpp"
#include "../swapchain_events.hpp"
#include "../utils/general_utils.hpp"
#include "../utils/logging.hpp"
#include "../utils/timing.hpp"
#include "dxgi/dxgi_present_hooks.hpp"

#include <MinHook.h>
#include <windows.h>

#include <evntprov.h>

#include <atomic>
#include <cstring>

namespace {

// PCLStats provider GUID from Streamline pclstats.h (PCLStatsTraceLoggingProvider)
static constexpr GUID kPCLStatsProviderId = {
    0x0d216f06, 0x82a6, 0x4d49, {0xbc, 0x4f, 0x8f, 0x38, 0xae, 0x56, 0xef, 0xab}};

static bool GuidEquals(const GUID* a, const GUID* b) { return std::memcmp(a, b, sizeof(GUID)) == 0; }

using EventRegister_pfn = decltype(&EventRegister);
using EventWriteTransfer_pfn = decltype(&EventWriteTransfer);

static EventRegister_pfn EventRegister_Original = nullptr;
static EventWriteTransfer_pfn EventWriteTransfer_Original = nullptr;

static std::atomic<bool> g_pclstats_etw_hooks_installed{false};
// Handle returned when a provider with PCLStats GUID is registered (game or us)
static std::atomic<REGHANDLE> g_pclstats_provider_handle{REGHANDLE(0)};

static std::atomic<uint64_t> g_count_pclstats_event{0};
static std::atomic<uint64_t> g_count_pclstats_event_v2{0};
static std::atomic<uint64_t> g_count_pclstats_event_v3{0};

// Per-marker counts (index = PCLStats marker type 0..19)
static std::atomic<uint64_t> g_count_pclstats_by_marker[kPCLStatsMarkerTypeCount] = {};

// Number of times PCLSTATS_MARKER was called from our code (injected reflex)
static std::atomic<uint64_t> g_count_pclstats_marker_calls{0};

// First 6 PCLStats markers (same as Reflex): 0=SIMULATION_START .. 5=PRESENT_END
constexpr int kPCLStatsMarkerFirstSixMax = 5;

/** Decoded PCLStats marker event. See docs/PCL_LATENCY_MARKERS_REPORTING.md "Format of Written Data". */
struct DecodedPclStatsEvent {
    int event_kind{0};  // 0 = not a marker event, 1 = PCLStatsEvent, 2 = V2, 3 = V3
    int marker{-1};     // 0..19 or -1
    uint64_t frame_id{0};
    bool is_marker_event() const { return marker >= 0 && marker < static_cast<int>(kPCLStatsMarkerTypeCount); }
};

// Search blob for PCLStats event name; return 3=V3, 2=V2, 1=PCLStatsEvent, 0=none.
static int ClassifyPclStatsEventName(const void* ptr, ULONG size) {
    if (ptr == nullptr || size < 13) return 0;
    const char* p = static_cast<const char*>(ptr);
    const char* end = p + size;
    for (const char* cur = p; cur + 13 <= end; ++cur) {
        if (std::memcmp(cur, "PCLStatsEvent", 13) != 0) continue;
        if (cur + 16 <= end && cur[13] == 'V' && cur[14] == '3') return 3;
        if (cur + 15 <= end && cur[13] == 'V' && cur[14] == '2') return 2;
        return 1;
    }
    return 0;
}

// Read 4-byte LE from descriptor; return true and set val if size==4 and value in [0, kPCLStatsMarkerTypeCount).
static bool ReadMarkerFromDescriptor(const EVENT_DATA_DESCRIPTOR* d, int* out_marker) {
    if (out_marker == nullptr || d == nullptr || d->Size != 4) return false;
    const void* ptr = reinterpret_cast<const void*>(static_cast<uintptr_t>(d->Ptr));
    __try {
        uint32_t val;
        std::memcpy(&val, ptr, 4);
        if (val < kPCLStatsMarkerTypeCount) {
            *out_marker = static_cast<int>(val);
            return true;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    return false;
}

// Read 8-byte LE FrameID from descriptor.
static bool ReadFrameIdFromDescriptor(const EVENT_DATA_DESCRIPTOR* d, uint64_t* out_frame_id) {
    if (out_frame_id == nullptr || d == nullptr || d->Size != 8) return false;
    const void* ptr = reinterpret_cast<const void*>(static_cast<uintptr_t>(d->Ptr));
    __try {
        std::memcpy(out_frame_id, ptr, 8);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

/**
 * Decode PCLStats marker event from EventWriteTransfer UserData using the documented layout.
 * Layout A: [metadata, Marker 4, FrameID 8] or [metadata, Marker, FrameID, Flags/Value 4].
 * Layout B: [Marker 4, FrameID 8] or [Marker 4, FrameID 8, Flags/Value 4].
 * Returns true if at least marker was decoded (is_marker_event() then true).
 */
static bool DecodePclStatsEvent(ULONG UserDataCount, PEVENT_DATA_DESCRIPTOR UserData, DecodedPclStatsEvent* out) {
    if (out == nullptr || UserData == nullptr || UserDataCount < 2) {
        return false;
    }
    out->event_kind = 0;
    out->marker = -1;
    out->frame_id = 0;

    // Find metadata descriptor (contains "PCLStatsEvent") and event kind
    int metadata_index = -1;
    for (ULONG i = 0; i < UserDataCount; ++i) {
        const EVENT_DATA_DESCRIPTOR* d = &UserData[i];
        if (d->Size == 0 || d->Size > 0x10000) continue;
        const void* ptr = reinterpret_cast<const void*>(static_cast<uintptr_t>(d->Ptr));
        __try {
            int kind = ClassifyPclStatsEventName(ptr, d->Size);
            if (kind != 0) {
                out->event_kind = kind;
                metadata_index = static_cast<int>(i);
                break;
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            continue;
        }
    }

    if (metadata_index >= 0) {
        // Layout A: metadata first. Find first 4-byte descriptor that is a valid marker (skip metadata), then 8-byte
        // for FrameID.
        for (ULONG i = 0; i < UserDataCount; ++i) {
            if (static_cast<int>(i) == metadata_index) continue;
            const EVENT_DATA_DESCRIPTOR* d = &UserData[i];
            if (d->Size == 4 && ReadMarkerFromDescriptor(d, &out->marker)) break;
        }
        for (ULONG i = 0; i < UserDataCount; ++i) {
            if (static_cast<int>(i) == metadata_index) continue;
            const EVENT_DATA_DESCRIPTOR* d = &UserData[i];
            if (d->Size == 8) {
                ReadFrameIdFromDescriptor(d, &out->frame_id);
                break;
            }
        }
    } else {
        // Layout B (or A without literal event name): payload only. [Marker 4, FrameID 8] or [Marker, FrameID, extra
        // 4].
        const EVENT_DATA_DESCRIPTOR* d0 = &UserData[0];
        const EVENT_DATA_DESCRIPTOR* d1 = &UserData[1];
        if (d0->Size == 4 && d1->Size == 8) {
            ReadMarkerFromDescriptor(d0, &out->marker);
            ReadFrameIdFromDescriptor(d1, &out->frame_id);
            if (out->event_kind == 0) out->event_kind = (UserDataCount >= 3) ? 2 : 1;  // 3 descriptors = V2 or V3
        } else if (UserDataCount >= 3 && d0->Size != 4 && d0->Size != 8 && d1->Size == 4 && UserData[2].Size == 8) {
            // Layout A without string: first descriptor is metadata (other size), then Marker, FrameID
            ReadMarkerFromDescriptor(d1, &out->marker);
            ReadFrameIdFromDescriptor(&UserData[2], &out->frame_id);
            if (out->event_kind == 0) out->event_kind = 1;
        } else {
            // Fallback: first 4-byte value in 0..19 that is not a metadata blob (no "PCLStatsEvent" in it)
            for (ULONG i = 0; i < UserDataCount; ++i) {
                const EVENT_DATA_DESCRIPTOR* d = &UserData[i];
                if (d->Size != 4) continue;
                const void* ptr = reinterpret_cast<const void*>(static_cast<uintptr_t>(d->Ptr));
                if (ClassifyPclStatsEventName(ptr, d->Size) != 0) continue;
                if (ReadMarkerFromDescriptor(d, &out->marker)) {
                    if (out->event_kind == 0) out->event_kind = 1;
                    break;
                }
            }
        }
    }

    return out->is_marker_event();
}

ULONG WINAPI EventRegister_Detour(LPCGUID ProviderId, PENABLECALLBACK EnableCallback, PVOID CallbackContext,
                                  PREGHANDLE RegHandle) {
    ULONG ret = EventRegister_Original(ProviderId, EnableCallback, CallbackContext, RegHandle);
    // Don't record our own registration so we keep the game's PCL provider handle for counting.
    HMODULE calling_module = GetCallingDLL();
    HMODULE our_module = g_module.load(std::memory_order_relaxed);
    if (calling_module != nullptr && our_module != nullptr && calling_module == our_module) {
        return ret;
    }
    if (ret == 0 && RegHandle != nullptr && ProviderId != nullptr && GuidEquals(ProviderId, &kPCLStatsProviderId)) {
        {
            static bool first_call = true;
            if (first_call) {
                first_call = false;
                LogInfo("PCLStats ETW: EventRegister_Detour called for kPCLStatsProviderId");
            }
        }
        g_pclstats_provider_handle.store(*RegHandle, std::memory_order_relaxed);

        /*
            REGHANDLE prev = g_pclstats_provider_handle.exchange(*RegHandle, std::memory_order_relaxed);
        if (prev != REGHANDLE(0) && prev != *RegHandle) {
            // Second registration (e.g. game and we both register) - keep first so we count both
            g_pclstats_provider_handle.store(prev, std::memory_order_relaxed);
        }*/
    }
    return ret;
}

ULONG WINAPI EventWriteTransfer_Detour(REGHANDLE RegHandle, PCEVENT_DESCRIPTOR EventDescriptor, LPCGUID ActivityId,
                                       LPCGUID RelatedActivityId, ULONG UserDataCount,
                                       PEVENT_DATA_DESCRIPTOR UserData) {
    // Ignore calls from our own module so we don't count or react to our own PCLStats events.
    HMODULE calling_module = GetCallingDLL();
    HMODULE our_module = g_module.load(std::memory_order_relaxed);
    if (calling_module != nullptr && our_module != nullptr && calling_module == our_module) {
        return EventWriteTransfer_Original(RegHandle, EventDescriptor, ActivityId, RelatedActivityId, UserDataCount,
                                           UserData);
    }

    REGHANDLE pcl_handle = g_pclstats_provider_handle.load(std::memory_order_relaxed);
    int marker = -1;
    if (pcl_handle != REGHANDLE(0) && RegHandle == pcl_handle && UserData != nullptr && UserDataCount > 0) {
        {
            static bool first_call = true;
            if (first_call) {
                first_call = false;
                LogInfo("PCLStats ETW: EventWriteTransfer_Detour called");
            }
        }
        DecodedPclStatsEvent decoded = {};
        if (DecodePclStatsEvent(UserDataCount, UserData, &decoded) && decoded.is_marker_event()) {
            marker = decoded.marker;
            switch (decoded.event_kind) {
                case 3:  g_count_pclstats_event_v3.fetch_add(1, std::memory_order_relaxed); break;
                case 2:  g_count_pclstats_event_v2.fetch_add(1, std::memory_order_relaxed); break;
                case 1:
                default: g_count_pclstats_event.fetch_add(1, std::memory_order_relaxed); break;
            }
            g_count_pclstats_by_marker[marker].fetch_add(1, std::memory_order_relaxed);
        }
    }

    // FPS limiter over PCLStats ETW (first 6 markers only), same as Reflex path
    if (marker >= 0 && marker <= kPCLStatsMarkerFirstSixMax) {
        const uint64_t now_ns = static_cast<uint64_t>(utils::get_now_ns());
        ChooseFpsLimiter(now_ns, FpsLimiterCallSite::reflex_marker_pclstats_etw);
        const bool use_fps_limiter = GetChosenFpsLimiter(FpsLimiterCallSite::reflex_marker_pclstats_etw);
        const bool native_pacing_sim_start_only = settings::g_mainTabSettings.native_pacing_sim_start_only.GetValue();
        if (use_fps_limiter) {
            if (native_pacing_sim_start_only) {
                if (marker == 0) {
                    OnPresentFlags2(false, true);
                    RecordNativeFrameTime();
                }
                if (marker == 0) {
                    display_commanderhooks::dxgi::HandlePresentAfter(true);
                }
            } else {
                if (marker == 4) {
                    OnPresentFlags2(false, true);
                    RecordNativeFrameTime();
                }
                if (marker == 5) {
                    display_commanderhooks::dxgi::HandlePresentAfter(true);
                }
            }
        }
    }

    return EventWriteTransfer_Original(RegHandle, EventDescriptor, ActivityId, RelatedActivityId, UserDataCount,
                                       UserData);
}

}  // namespace

bool InstallPCLStatsEtwHooks(HMODULE hModule) {
    if (g_pclstats_etw_hooks_installed.load(std::memory_order_acquire)) {
        return true;
    }
    if (hModule == nullptr) {
        LogInfo("PCLStats ETW: null module handle");
        return false;
    }
    auto* pEventRegister = reinterpret_cast<EventRegister_pfn>(GetProcAddress(hModule, "EventRegister"));
    auto* pEventWriteTransfer = reinterpret_cast<EventWriteTransfer_pfn>(GetProcAddress(hModule, "EventWriteTransfer"));
    if (pEventRegister == nullptr || pEventWriteTransfer == nullptr) {
        LogInfo("PCLStats ETW: EventRegister or EventWriteTransfer not found");
        return false;
    }
    if (!CreateAndEnableHook(reinterpret_cast<LPVOID>(pEventRegister), reinterpret_cast<LPVOID>(&EventRegister_Detour),
                             reinterpret_cast<LPVOID*>(&EventRegister_Original), "EventRegister")) {
        return false;
    }
    if (!CreateAndEnableHook(reinterpret_cast<LPVOID>(pEventWriteTransfer),
                             reinterpret_cast<LPVOID>(&EventWriteTransfer_Detour),
                             reinterpret_cast<LPVOID*>(&EventWriteTransfer_Original), "EventWriteTransfer")) {
        MH_DisableHook(pEventRegister);
        MH_RemoveHook(pEventRegister);
        EventRegister_Original = nullptr;
        return false;
    }
    g_pclstats_etw_hooks_installed.store(true, std::memory_order_release);
    LogInfo("PCLStats ETW: hooks installed (EventRegister + EventWriteTransfer); counting PCLStatsEvent / V2 / V3");
    return true;
}

void UninstallPCLStatsEtwHooks() {
    if (!g_pclstats_etw_hooks_installed.exchange(false, std::memory_order_acq_rel)) {
        return;
    }
    if (EventWriteTransfer_Original != nullptr) {
        MH_DisableHook(EventWriteTransfer_Original);
        MH_RemoveHook(EventWriteTransfer_Original);
        EventWriteTransfer_Original = nullptr;
    }
    if (EventRegister_Original != nullptr) {
        MH_DisableHook(EventRegister_Original);
        MH_RemoveHook(EventRegister_Original);
        EventRegister_Original = nullptr;
    }
    g_pclstats_provider_handle.store(REGHANDLE(0), std::memory_order_relaxed);
    LogInfo("PCLStats ETW: hooks uninstalled");
}

bool ArePCLStatsEtwHooksInstalled() { return g_pclstats_etw_hooks_installed.load(std::memory_order_acquire); }

bool PCLStatsReportingAllowed() {
    // Only allow PCLStats reporting for DXGI (D3D10/11/12); not D3D9, OpenGL, Vulkan
    const reshade::api::device_api api = g_last_reshade_device_api.load(std::memory_order_acquire);
    const bool is_dxgi = (api == reshade::api::device_api::d3d10 || api == reshade::api::device_api::d3d11
                          || api == reshade::api::device_api::d3d12);

    const bool no_game_pclstats = g_count_pclstats_event.load(std::memory_order_relaxed) == 0
                                  && g_count_pclstats_event_v2.load(std::memory_order_relaxed) == 0
                                  && g_count_pclstats_event_v3.load(std::memory_order_relaxed) == 0;
    const bool past_warmup = g_global_frame_id.load(std::memory_order_acquire) >= 500;
    return is_dxgi && no_game_pclstats && past_warmup;
}

bool PCLStatsReportingEnabled() {
    return PCLStatsReportingAllowed() && settings::g_mainTabSettings.pcl_stats_enabled.GetValue();
}

void RecordPCLStatsMarkerCall() { g_count_pclstats_marker_calls.fetch_add(1, std::memory_order_relaxed); }

uint64_t GetPCLStatsMarkerCallCount() { return g_count_pclstats_marker_calls.load(std::memory_order_relaxed); }

void GetPCLStatsEtwCounts(uint64_t* out_pclstats_event, uint64_t* out_pclstats_event_v2,
                          uint64_t* out_pclstats_event_v3) {
    if (out_pclstats_event) *out_pclstats_event = g_count_pclstats_event.load(std::memory_order_relaxed);
    if (out_pclstats_event_v2) *out_pclstats_event_v2 = g_count_pclstats_event_v2.load(std::memory_order_relaxed);
    if (out_pclstats_event_v3) *out_pclstats_event_v3 = g_count_pclstats_event_v3.load(std::memory_order_relaxed);
}

void GetPCLStatsEtwCountsByMarker(uint64_t* out_counts) {
    if (out_counts == nullptr) return;
    for (size_t i = 0; i < kPCLStatsMarkerTypeCount; ++i) {
        out_counts[i] = g_count_pclstats_by_marker[i].load(std::memory_order_relaxed);
    }
}

static const char* const kPCLStatsMarkerNames[] = {
    "SIMULATION_START",         // 0
    "SIMULATION_END",           // 1
    "RENDERSUBMIT_START",       // 2
    "RENDERSUBMIT_END",         // 3
    "PRESENT_START",            // 4
    "PRESENT_END",              // 5
    "INPUT_SAMPLE(depr)",       // 6
    "TRIGGER_FLASH",            // 7
    "PC_LATENCY_PING",          // 8
    "OOB_RENDERSUBMIT_START",   // 9
    "OOB_RENDERSUBMIT_END",     // 10
    "OOB_PRESENT_START",        // 11
    "OOB_PRESENT_END",          // 12
    "CONTROLLER_INPUT",         // 13
    "DELTA_T_CALCULATION",      // 14
    "LATE_WARP_PRESENT_START",  // 15
    "LATE_WARP_PRESENT_END",    // 16
    "CAMERA_CONSTRUCTED",       // 17
    "LATE_WARP_SUBMIT_START",   // 18
    "LATE_WARP_SUBMIT_END",     // 19
};

const char* GetPCLStatsMarkerTypeName(size_t index) {
    if (index >= kPCLStatsMarkerTypeCount) return "?";
    return kPCLStatsMarkerNames[index];
}

void ResetPCLStatsEtwCounts() {
    g_count_pclstats_event.store(0, std::memory_order_relaxed);
    g_count_pclstats_event_v2.store(0, std::memory_order_relaxed);
    g_count_pclstats_event_v3.store(0, std::memory_order_relaxed);
    g_count_pclstats_marker_calls.store(0, std::memory_order_relaxed);
    for (size_t i = 0; i < kPCLStatsMarkerTypeCount; ++i) {
        g_count_pclstats_by_marker[i].store(0, std::memory_order_relaxed);
    }
}
