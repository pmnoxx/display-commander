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

static bool GuidEquals(const GUID* a, const GUID* b) {
    return std::memcmp(a, b, sizeof(GUID)) == 0;
}

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

// First 6 PCLStats markers (same as Reflex): 0=SIMULATION_START .. 5=PRESENT_END
constexpr int kPCLStatsMarkerFirstSixMax = 5;

// Search blob for PCLStats event name; return 3=V3, 2=V2, 1=PCLStatsEvent, 0=none.
// "PCLStatsEvent" = 13 chars; "PCLStatsEventV2" / "PCLStatsEventV3" = 15/16 chars.
static int ClassifyPclStatsEvent(const void* ptr, ULONG size) {
    if (ptr == nullptr || size < 13) return 0;
    const char* p = static_cast<const char*>(ptr);
    const char* end = p + size;
    for (const char* cur = p; cur + 13 <= end; ++cur) {
        if (std::memcmp(cur, "PCLStatsEvent", 13) != 0) continue;
        if (cur + 15 <= end && cur[13] == 'V' && cur[14] == '3') return 3;
        if (cur + 15 <= end && cur[13] == 'V' && cur[14] == '2') return 2;
        return 1;
    }
    return 0;
}

// Return true if blob contains "PCLStatsEvent" (metadata descriptor).
static bool DescriptorLooksLikeMetadata(const void* ptr, ULONG size) {
    return ClassifyPclStatsEvent(ptr, size) != 0;
}

// Parse Marker from TraceLogging payload. PCLStatsEvent has (Marker uint32, FrameID uint64).
// Scan descriptors for a 4-byte LE value in 0..(kPCLStatsMarkerTypeCount-1) that is not in the metadata blob.
// Returns marker 0..19 or -1 if not found.
static int ParsePclStatsMarkerFromDescriptors(ULONG UserDataCount, PEVENT_DATA_DESCRIPTOR UserData) {
    if (UserData == nullptr) return -1;
    for (ULONG i = 0; i < UserDataCount; ++i) {
        const EVENT_DATA_DESCRIPTOR* d = &UserData[i];
        ULONG len = d->Size;
        if (len < 4 || len > 0x10000) continue;
        const void* ptr = reinterpret_cast<const void*>(static_cast<uintptr_t>(d->Ptr));
        __try {
            if (DescriptorLooksLikeMetadata(ptr, len)) continue;
            uint32_t val;
            std::memcpy(&val, ptr, 4);
            if (val < kPCLStatsMarkerTypeCount) return static_cast<int>(val);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            continue;
        }
    }
    return -1;
}

ULONG WINAPI EventRegister_Detour(LPCGUID ProviderId,
                                  PENABLECALLBACK EnableCallback,
                                  PVOID CallbackContext,
                                  PREGHANDLE RegHandle) {
    ULONG ret = EventRegister_Original(ProviderId, EnableCallback, CallbackContext, RegHandle);
    // Don't record our own registration so we keep the game's PCL provider handle for counting.
    HMODULE calling_module = GetCallingDLL();
    HMODULE our_module = g_module.load(std::memory_order_relaxed);
    if (calling_module != nullptr && our_module != nullptr && calling_module == our_module) {
        return ret;
    }
    if (ret == 0 && RegHandle != nullptr && ProviderId != nullptr && GuidEquals(ProviderId, &kPCLStatsProviderId)) {
        REGHANDLE prev = g_pclstats_provider_handle.exchange(*RegHandle, std::memory_order_relaxed);
        if (prev != REGHANDLE(0) && prev != *RegHandle) {
            // Second registration (e.g. game and we both register) - keep first so we count both
            g_pclstats_provider_handle.store(prev, std::memory_order_relaxed);
        }
    }
    return ret;
}

ULONG WINAPI EventWriteTransfer_Detour(REGHANDLE RegHandle,
                                        PCEVENT_DESCRIPTOR EventDescriptor,
                                        LPCGUID ActivityId,
                                        LPCGUID RelatedActivityId,
                                        ULONG UserDataCount,
                                        PEVENT_DATA_DESCRIPTOR UserData) {
    // Ignore calls from our own module so we don't count or react to our own PCLStats events.
    HMODULE calling_module = GetCallingDLL();
    HMODULE our_module = g_module.load(std::memory_order_relaxed);
    if (calling_module != nullptr && our_module != nullptr && calling_module == our_module) {
        return EventWriteTransfer_Original(RegHandle, EventDescriptor, ActivityId, RelatedActivityId, UserDataCount,
                                          UserData);
    }

    REGHANDLE pcl_handle = g_pclstats_provider_handle.load(std::memory_order_relaxed);
    int event_kind = 0;
    int marker = -1;
    if (pcl_handle != REGHANDLE(0) && RegHandle == pcl_handle && UserData != nullptr && UserDataCount > 0) {
        for (ULONG i = 0; i < UserDataCount; ++i) {
            const EVENT_DATA_DESCRIPTOR* d = &UserData[i];
            ULONG len = d->Size;
            if (len == 0 || len > 0x10000) continue;
            const void* ptr = reinterpret_cast<const void*>(static_cast<uintptr_t>(d->Ptr));
            __try {
                int kind = ClassifyPclStatsEvent(ptr, len);
                if (kind == 3) {
                    g_count_pclstats_event_v3.fetch_add(1, std::memory_order_relaxed);
                    event_kind = 3;
                    break;
                }
                if (kind == 2) {
                    g_count_pclstats_event_v2.fetch_add(1, std::memory_order_relaxed);
                    event_kind = 2;
                    break;
                }
                if (kind == 1) {
                    g_count_pclstats_event.fetch_add(1, std::memory_order_relaxed);
                    event_kind = 1;
                    break;
                }
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                break;
            }
        }
        if (event_kind != 0) {
            marker = ParsePclStatsMarkerFromDescriptors(UserDataCount, UserData);
            if (marker >= 0 && marker < static_cast<int>(kPCLStatsMarkerTypeCount)) {
                g_count_pclstats_by_marker[marker].fetch_add(1, std::memory_order_relaxed);
            }
        }
    }

    // FPS limiter over PCLStats ETW (first 6 markers only), same as Reflex path
    if (marker >= 0 && marker <= kPCLStatsMarkerFirstSixMax) {
        const uint64_t now_ns = static_cast<uint64_t>(utils::get_now_ns());
        ChooseFpsLimiter(now_ns, FpsLimiterCallSite::reflex_marker_pclstats_etw);
        const bool use_fps_limiter = GetChosenFpsLimiter(FpsLimiterCallSite::reflex_marker_pclstats_etw);
        const bool native_pacing_sim_start_only =
            settings::g_mainTabSettings.native_pacing_sim_start_only.GetValue();
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

bool InstallPCLStatsEtwHooks() {
    if (g_pclstats_etw_hooks_installed.load(std::memory_order_acquire)) {
        return true;
    }
    HMODULE advapi = GetModuleHandleW(L"advapi32.dll");
    if (advapi == nullptr) {
        LogInfo("PCLStats ETW: advapi32.dll not loaded");
        return false;
    }
    auto* pEventRegister = reinterpret_cast<EventRegister_pfn>(GetProcAddress(advapi, "EventRegister"));
    auto* pEventWriteTransfer = reinterpret_cast<EventWriteTransfer_pfn>(GetProcAddress(advapi, "EventWriteTransfer"));
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

void GetPCLStatsEtwCounts(uint64_t* out_pclstats_event,
                          uint64_t* out_pclstats_event_v2,
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
    "SIMULATION_START",      // 0
    "SIMULATION_END",        // 1
    "RENDERSUBMIT_START",    // 2
    "RENDERSUBMIT_END",      // 3
    "PRESENT_START",         // 4
    "PRESENT_END",           // 5
    "INPUT_SAMPLE(depr)",    // 6
    "TRIGGER_FLASH",         // 7
    "PC_LATENCY_PING",       // 8
    "OOB_RENDERSUBMIT_START",// 9
    "OOB_RENDERSUBMIT_END",  // 10
    "OOB_PRESENT_START",     // 11
    "OOB_PRESENT_END",       // 12
    "CONTROLLER_INPUT",      // 13
    "DELTA_T_CALCULATION",   // 14
    "LATE_WARP_PRESENT_START",// 15
    "LATE_WARP_PRESENT_END", // 16
    "CAMERA_CONSTRUCTED",    // 17
    "LATE_WARP_SUBMIT_START",// 18
    "LATE_WARP_SUBMIT_END",  // 19
};

const char* GetPCLStatsMarkerTypeName(size_t index) {
    if (index >= kPCLStatsMarkerTypeCount) return "?";
    return kPCLStatsMarkerNames[index];
}

void ResetPCLStatsEtwCounts() {
    g_count_pclstats_event.store(0, std::memory_order_relaxed);
    g_count_pclstats_event_v2.store(0, std::memory_order_relaxed);
    g_count_pclstats_event_v3.store(0, std::memory_order_relaxed);
    for (size_t i = 0; i < kPCLStatsMarkerTypeCount; ++i) {
        g_count_pclstats_by_marker[i].store(0, std::memory_order_relaxed);
    }
}
