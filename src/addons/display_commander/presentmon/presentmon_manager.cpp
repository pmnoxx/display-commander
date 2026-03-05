#include "presentmon_manager.hpp"
#include "../globals.hpp"
#include "../settings/advanced_tab_settings.hpp"
#include "../utils/logging.hpp"
#include "../utils/timing.hpp"

#include <evntrace.h>
#include <strsafe.h>
#include <tdh.h>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <memory>

namespace presentmon {

PresentMonManager g_presentMonManager;

void CreateAndStartPresentMon() {
    if (g_presentMonManager.IsRunning()) {
        return;
    }
    g_presentMonManager.StartWorker();
}

void StopAndDestroyPresentMon(PresentMonStopReason reason) {
    g_presentMonManager.StopWorker(reason);
}

namespace {

// NOTE: No std::mutex in this project. We use atomics + TLS pointer for callback routing.
thread_local PresentMonManager* t_active_manager = nullptr;

std::wstring GuidToWString(const GUID& guid) {
    wchar_t buf[64] = {};
    StringFromGUID2(guid, buf, static_cast<int>(std::size(buf)));
    return std::wstring(buf);
}

std::string Narrow(const std::wstring& ws) {
    if (ws.empty()) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (len <= 0) return {};
    std::string out;
    out.resize(static_cast<size_t>(len - 1));
    WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), -1, out.data(), len, nullptr, nullptr);
    return out;
}

// Parse PID from "DC_PresentMon_<pid>". Returns true and sets out_pid if format matches.
bool ParsePidFromDcPresentMonSessionName(const std::string& name, DWORD& out_pid) {
    const char prefix[] = "DC_PresentMon_";
    const size_t prefix_len = sizeof(prefix) - 1;
    if (name.size() <= prefix_len || name.compare(0, prefix_len, prefix) != 0) return false;
    const std::string suffix = name.substr(prefix_len);
    if (suffix.empty()) return false;
    char* end = nullptr;
    unsigned long pid = std::strtoul(suffix.c_str(), &end, 10);
    if (end == nullptr || end != suffix.c_str() + suffix.size() || pid == 0) return false;
    out_pid = static_cast<DWORD>(pid);
    return true;
}

// Returns true if the given process ID is still running.
bool IsProcessRunning(DWORD pid) {
    if (pid == 0) return false;
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (h == nullptr) return false;
    CloseHandle(h);
    return true;
}

bool ProviderGuidByName(const wchar_t* provider_name, GUID& out_guid) {
    ULONG size = 0;
    if (TdhEnumerateProviders(nullptr, &size) != ERROR_INSUFFICIENT_BUFFER) {
        return false;
    }

    std::unique_ptr<uint8_t[]> buf(new (std::nothrow) uint8_t[size]);
    if (!buf) return false;

    auto* providers = reinterpret_cast<PROVIDER_ENUMERATION_INFO*>(buf.get());
    ULONG status = TdhEnumerateProviders(providers, &size);
    if (status != ERROR_SUCCESS) return false;

    for (ULONG i = 0; i < providers->NumberOfProviders; ++i) {
        auto& p = providers->TraceProviderInfoArray[i];
        const wchar_t* name =
            reinterpret_cast<const wchar_t*>(reinterpret_cast<const uint8_t*>(providers) + p.ProviderNameOffset);
        if (name != nullptr && _wcsicmp(name, provider_name) == 0) {
            out_guid = p.ProviderGuid;
            return true;
        }
    }
    return false;
}

std::wstring Widen(const std::string& s) {
    if (s.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    if (len <= 0) return {};
    std::wstring out;
    out.resize(static_cast<size_t>(len - 1));
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, out.data(), len);
    return out;
}

bool StringContainsI(const std::string& haystack, const char* needle) {
    if (needle == nullptr || needle[0] == '\0') return false;
    auto tolower_ascii = [](char c) -> char {
        if (c >= 'A' && c <= 'Z') return static_cast<char>(c - 'A' + 'a');
        return c;
    };
    std::string n(needle);
    for (auto& c : n) c = tolower_ascii(c);

    std::string h = haystack;
    for (auto& c : h) c = tolower_ascii(c);

    return h.find(n) != std::string::npos;
}

PresentMonFlipMode MapPresentModeStringToFlip(const std::string& s) {
    if (StringContainsI(s, "overlay") || StringContainsI(s, "mpo")) {
        return PresentMonFlipMode::Overlay;
    }
    if (StringContainsI(s, "independent")) {
        return PresentMonFlipMode::IndependentFlip;
    }
    if (StringContainsI(s, "composed")) {
        return PresentMonFlipMode::Composed;
    }
    return PresentMonFlipMode::Unknown;
}

// Extract property value as UTF-8 string using TDH, if present.
bool TryGetEventPropertyString(PEVENT_RECORD event_record, const wchar_t* prop_name, std::string& out) {
    PROPERTY_DATA_DESCRIPTOR desc = {};
    desc.PropertyName = reinterpret_cast<ULONGLONG>(prop_name);
    desc.ArrayIndex = ULONG_MAX;

    ULONG size = 0;
    ULONG status = TdhGetPropertySize(event_record, 0, nullptr, 1, &desc, &size);
    if (status != ERROR_SUCCESS || size == 0) return false;

    std::unique_ptr<uint8_t[]> buf(new (std::nothrow) uint8_t[size + 2]);
    if (!buf) return false;

    status = TdhGetProperty(event_record, 0, nullptr, 1, &desc, size, buf.get());
    if (status != ERROR_SUCCESS) return false;

    // Heuristic: if it looks like UTF-16, convert; otherwise treat as ANSI/bytes.
    const wchar_t* w = reinterpret_cast<const wchar_t*>(buf.get());
    if (size >= sizeof(wchar_t) && w[0] != 0 && wcsnlen_s(w, size / sizeof(wchar_t)) > 0) {
        out = Narrow(std::wstring(w));
        return !out.empty();
    }

    const char* a = reinterpret_cast<const char*>(buf.get());
    if (a[0] != 0) {
        out.assign(a, a + strnlen_s(a, size));
        return !out.empty();
    }

    return false;
}

static bool StringContainsI_w(const std::wstring& haystack, const wchar_t* needle) {
    if (needle == nullptr || needle[0] == L'\0') return false;

    auto tolower_w = [](wchar_t c) -> wchar_t {
        if (c >= L'A' && c <= L'Z') return static_cast<wchar_t>(c - L'A' + L'a');
        return c;
    };

    std::wstring h = haystack;
    for (auto& c : h) c = tolower_w(c);

    std::wstring n(needle);
    for (auto& c : n) c = tolower_w(c);

    return h.find(n) != std::wstring::npos;
}

static std::wstring GetTraceEventInfoString(const TRACE_EVENT_INFO* info, ULONG offset_bytes) {
    if (info == nullptr || offset_bytes == 0) return {};
    const wchar_t* s = reinterpret_cast<const wchar_t*>(reinterpret_cast<const uint8_t*>(info) + offset_bytes);
    if (s == nullptr) return {};
    return std::wstring(s);
}

static bool TryGetEventPropertyU64(PEVENT_RECORD event_record, const wchar_t* prop_name, uint64_t& out) {
    PROPERTY_DATA_DESCRIPTOR desc = {};
    desc.PropertyName = reinterpret_cast<ULONGLONG>(prop_name);
    desc.ArrayIndex = ULONG_MAX;

    ULONG size = 0;
    ULONG status = TdhGetPropertySize(event_record, 0, nullptr, 1, &desc, &size);
    if (status != ERROR_SUCCESS || size == 0) return false;

    std::unique_ptr<uint8_t[]> buf(new (std::nothrow) uint8_t[size]);
    if (!buf) return false;

    status = TdhGetProperty(event_record, 0, nullptr, 1, &desc, size, buf.get());
    if (status != ERROR_SUCCESS) return false;

    // Interpret up to 8 bytes as little-endian integer
    out = 0;
    ULONG copy = (size > sizeof(uint64_t)) ? sizeof(uint64_t) : size;
    memcpy(&out, buf.get(), copy);
    return true;
}

static std::string FormatPropValueBestEffort(PEVENT_RECORD event_record, const std::wstring& prop_name,
                                             USHORT in_type) {
    std::string out;

    // Prefer known string/int extraction
    if (in_type == TDH_INTYPE_UNICODESTRING || in_type == TDH_INTYPE_ANSISTRING) {
        if (TryGetEventPropertyString(event_record, prop_name.c_str(), out)) return out;
        return {};
    }

    uint64_t v = 0;
    if (TryGetEventPropertyU64(event_record, prop_name.c_str(), v)) {
        char buf[64] = {};
        // Pointers and handles: show hex (TDH_INTYPE_POINTER is 14 in Windows SDK)
        const bool as_hex = (in_type == 14);
        StringCchPrintfA(buf, std::size(buf), as_hex ? "0x%llx" : "%llu",
                         static_cast<unsigned long long>(v));
        return std::string(buf);
    }
    return {};
}

// Build "name=value, name2=value2" for one event (one sample per field). Used for event-type tooltip.
static std::string BuildEventTypeSampleString(PEVENT_RECORD event_record, const TRACE_EVENT_INFO* info,
                                              size_t max_props) {
    if (!info || info->TopLevelPropertyCount == 0) return {};
    std::string out;
    size_t listed = 0;
    for (ULONG i = 0; i < info->TopLevelPropertyCount && listed < max_props; ++i) {
        const EVENT_PROPERTY_INFO& pi = info->EventPropertyInfoArray[i];
        std::wstring prop_name = GetTraceEventInfoString(info, pi.NameOffset);
        if (prop_name.empty()) continue;

        USHORT in_type = pi.nonStructType.InType;
        std::string value = FormatPropValueBestEffort(event_record, prop_name, in_type);
        if (value.empty()) continue;

        if (!out.empty()) out += ", ";
        out += Narrow(prop_name);
        out += "=";
        out += value;
        ++listed;
    }
    return out;
}

static std::string ProviderGuidToString(const GUID& guid) { return Narrow(GuidToWString(guid)); }

static uint64_t HashEventTypeKey(const GUID& provider, uint16_t event_id, uint16_t task, uint8_t opcode) {
    // FNV-1a 64-bit
    uint64_t h = 1469598103934665603ULL;
    auto fnv = [&](uint8_t b) {
        h ^= b;
        h *= 1099511628211ULL;
    };
    const uint8_t* p = reinterpret_cast<const uint8_t*>(&provider);
    for (size_t i = 0; i < sizeof(GUID); ++i) fnv(p[i]);
    fnv(static_cast<uint8_t>(event_id & 0xFF));
    fnv(static_cast<uint8_t>((event_id >> 8) & 0xFF));
    fnv(static_cast<uint8_t>(task & 0xFF));
    fnv(static_cast<uint8_t>((task >> 8) & 0xFF));
    fnv(opcode);
    if (h == 0) h = 1;  // avoid sentinel
    return h;
}

static uint64_t HashSurfaceKey(uint64_t surface_luid) {
    // FNV-1a 64-bit over surface_luid
    uint64_t h = 1469598103934665603ULL;
    const uint8_t* p = reinterpret_cast<const uint8_t*>(&surface_luid);
    for (size_t i = 0; i < sizeof(surface_luid); ++i) {
        h ^= p[i];
        h *= 1099511628211ULL;
    }
    if (h == 0) h = 1;
    return h;
}

// Event IDs for "per draw" events (one event per Present() call). May vary by Windows version.
// See private_docs/tasks/surface_refresh_rate_and_etw_events.md.
static constexpr uint16_t k_dxgi_present_start_id = 64;     // Microsoft-Windows-DXGI Present_Start
static constexpr uint16_t k_dxgkrnl_present_info_id = 68;   // Microsoft-Windows-DxgKrnl Present_Info (has hWnd)
static constexpr uint16_t k_d3d9_present_start_id = 1;      // Microsoft-Windows-D3D9 Present_Start (often 1)

static uint64_t HashHwndKey(uint64_t hwnd) {
    uint64_t h = 1469598103934665603ULL;
    const uint8_t* p = reinterpret_cast<const uint8_t*>(&hwnd);
    for (size_t i = 0; i < sizeof(hwnd); ++i) {
        h ^= p[i];
        h *= 1099511628211ULL;
    }
    if (h == 0) h = 1;
    return h;
}

static std::string JoinPropNamesCSV(const TRACE_EVENT_INFO* info, size_t max_props) {
    if (!info || info->TopLevelPropertyCount == 0) return {};
    std::string out;
    size_t listed = 0;
    for (ULONG i = 0; i < info->TopLevelPropertyCount && listed < max_props; ++i) {
        const EVENT_PROPERTY_INFO& pi = info->EventPropertyInfoArray[i];
        std::wstring prop_name = GetTraceEventInfoString(info, pi.NameOffset);
        if (prop_name.empty()) continue;
        if (!out.empty()) out += ", ";
        out += Narrow(prop_name);
        ++listed;
    }
    return out;
}

static ULONGLONG GetProviderKeywordMaskBestEffort(const GUID& provider_guid) {
    // Enumerate provider keyword fields and OR all keyword values together.
    // Some providers appear to ignore/behave oddly with 0xFFFF.. masks; using only declared keyword bits can help.
    ULONG buffer_size = 0;
    GUID guid = provider_guid;  // TDH API takes non-const GUID*
    ULONG st = TdhEnumerateProviderFieldInformation(&guid, EventKeywordInformation, nullptr, &buffer_size);
    if (st != ERROR_INSUFFICIENT_BUFFER || buffer_size == 0) {
        // Fallback: match everything
        return 0xFFFFFFFFFFFFFFFFULL;
    }

    std::unique_ptr<uint8_t[]> buf(new (std::nothrow) uint8_t[buffer_size]);
    if (!buf) return 0xFFFFFFFFFFFFFFFFULL;

    auto* info = reinterpret_cast<PROVIDER_FIELD_INFOARRAY*>(buf.get());
    st = TdhEnumerateProviderFieldInformation(&guid, EventKeywordInformation, info, &buffer_size);
    if (st != ERROR_SUCCESS) {
        return 0xFFFFFFFFFFFFFFFFULL;
    }

    ULONGLONG mask = 0;
    for (ULONG i = 0; i < info->NumberOfElements; ++i) {
        mask |= info->FieldInfoArray[i].Value;
    }

    // If for some reason it's empty, don't filter out everything.
    if (mask == 0) mask = 0xFFFFFFFFFFFFFFFFULL;
    return mask;
}

}  // namespace

const char* PresentMonFlipModeToString(PresentMonFlipMode mode) {
    switch (mode) {
        case PresentMonFlipMode::Unset:           return "Unset";
        case PresentMonFlipMode::Composed:        return "Composed";
        case PresentMonFlipMode::Overlay:         return "Hardware Overlay (MPO)";
        case PresentMonFlipMode::IndependentFlip: return "Independent Flip";
        case PresentMonFlipMode::Unknown:         return "Unknown";
        default:                                  return "Unknown";
    }
}

const char* PresentMonStopReasonToString(PresentMonStopReason reason) {
    switch (reason) {
        case PresentMonStopReason::UserDisabled:             return "UserDisabled";
        case PresentMonStopReason::AddonShutdownExitHandler: return "AddonShutdownExitHandler";
        case PresentMonStopReason::AddonShutdownUnload:      return "AddonShutdownUnload";
        case PresentMonStopReason::Destructor:               return "Destructor";
        default:                                             return "Unknown";
    }
}

PresentMonManager::PresentMonManager()
    : m_running(false),
      m_should_stop(false),
      m_flip_mode(PresentMonFlipMode::Unset),
      m_flip_state_valid(false),
      m_flip_state_update_time(0),
      m_present_mode_str(std::make_shared<const std::string>("Unknown")),
      m_debug_info_str(std::make_shared<const std::string>("")),
      m_thread_started(false),
      m_etw_session_active(false),
      m_thread_status(std::make_shared<const std::string>("Not started")),
      m_etw_session_status(std::make_shared<const std::string>("Not initialized")),
      m_last_error(std::make_shared<const std::string>("")),
      m_events_processed(0),
      m_events_processed_for_current_pid(0),
      m_events_lost(0),
      m_last_event_time(0),
      m_last_event_pid(0),
      m_last_provider(std::make_shared<const std::string>("")),
      m_last_event_id(0),
      m_last_present_mode_value(std::make_shared<const std::string>("")),
      m_last_provider_name(std::make_shared<const std::string>("")),
      m_last_event_name(std::make_shared<const std::string>("")),
      m_last_interesting_props(std::make_shared<const std::string>("")),
      m_last_schema_update_time_ns(0),
      m_events_dxgkrnl(0),
      m_events_dxgi(0),
      m_events_dwm(0),
      m_events_d3d9(0),
      m_last_graphics_provider(std::make_shared<const std::string>("")),
      m_last_graphics_event_id(0),
      m_last_graphics_event_pid(0),
      m_last_graphics_provider_name(std::make_shared<const std::string>("")),
      m_last_graphics_event_name(std::make_shared<const std::string>("")),
      m_last_graphics_props(std::make_shared<const std::string>("")),
      m_last_graphics_schema_update_time_ns(0),
      m_flip_compat_valid(false),
      m_flip_compat_last_update_ns(0),
      m_flip_compat_surface_luid(0),
      m_flip_compat_surface_width(0),
      m_flip_compat_surface_height(0),
      m_flip_compat_pixel_format(0),
      m_flip_compat_flags(0),
      m_flip_compat_color_space(0),
      m_flip_compat_is_direct(0),
      m_flip_compat_is_adv_direct(0),
      m_flip_compat_is_overlay(0),
      m_flip_compat_is_overlay_required(0),
      m_flip_compat_no_overlapping(0),
      m_etw_session_handle(0),
      m_etw_trace_handle(0) {
    m_session_name[0] = 0;
    ZeroMemory(&m_guid_dxgkrnl, sizeof(m_guid_dxgkrnl));
    ZeroMemory(&m_guid_dxgi, sizeof(m_guid_dxgi));
    ZeroMemory(&m_guid_dwm, sizeof(m_guid_dwm));
    ZeroMemory(&m_guid_d3d9, sizeof(m_guid_d3d9));
    m_have_dxgkrnl = false;
    m_have_dxgi = false;
    m_have_dwm = false;
    m_have_d3d9 = false;
}

bool PresentMonManager::GetFlipCompatibility(PresentMonFlipCompatibility& out) const {
    if (!m_flip_compat_valid.load()) {
        return false;
    }

    out.is_valid = true;
    out.last_update_time_ns = m_flip_compat_last_update_ns.load();
    out.surface_luid = m_flip_compat_surface_luid.load();
    out.surface_width = m_flip_compat_surface_width.load();
    out.surface_height = m_flip_compat_surface_height.load();
    out.pixel_format = m_flip_compat_pixel_format.load();
    out.flags = m_flip_compat_flags.load();
    out.color_space = m_flip_compat_color_space.load();

    out.is_direct_flip_compatible = (m_flip_compat_is_direct.load() != 0);
    out.is_advanced_direct_flip_compatible = (m_flip_compat_is_adv_direct.load() != 0);
    out.is_overlay_compatible = (m_flip_compat_is_overlay.load() != 0);
    out.is_overlay_required = (m_flip_compat_is_overlay_required.load() != 0);
    out.no_overlapping_content = (m_flip_compat_no_overlapping.load() != 0);

    return true;
}

PresentMonManager::~PresentMonManager() {
    // Always stop worker and ETW session, even if StopWorker wasn't called explicitly
    // This ensures ETW sessions don't leak system-wide resources
    StopWorker(PresentMonStopReason::Destructor);

    // Double-check: if session name exists but handle is lost, try to stop by name
    // This handles edge cases where the destructor runs but StopWorker didn't fully clean up
    if (m_session_name[0] != 0) {
        uint64_t sh = m_etw_session_handle.load();
        if (sh == 0) {
            // Handle was lost, try to stop by name as last resort
            StopEtwSessionByName(m_session_name);
        }
    }

    // shared_ptrs release automatically when atomics are destroyed
}

void PresentMonManager::StartWorker() {
    LogInfo("PresentMon: StartWorker() called (pid=%lu)", static_cast<unsigned long>(GetCurrentProcessId()));
    if (m_running.load()) {
        LogInfo("PresentMon: Worker already running, skipping duplicate start");
        return;
    }

    // Check if PresentMon is needed
    if (!IsNeeded()) {
        LogInfo("PresentMon: Not needed for current system/game configuration");
        return;
    }

    m_should_stop.store(false);
    m_running.store(true);
    m_thread_started.store(true);

    // Update thread status
    m_thread_status.store(std::make_shared<const std::string>("Starting..."));

    // Log all ETW sessions at start for debugging (e.g. why DC_ list may be empty in UI)
    LogAllEtwSessions();

    // Stop all DC_ ETW sessions so we can start ours clean (no PID check)
    StopAllDcEtwSessions();

    // Precompute session name (unique per process)
    DWORD pid = GetCurrentProcessId();
    StringCchPrintfW(m_session_name, std::size(m_session_name), L"DC_PresentMon_%lu", static_cast<unsigned long>(pid));

    // Start worker thread
    m_worker_thread = std::thread(&PresentMonManager::WorkerThread, this);

    // Start cleanup thread: every 10s, if no events for 10s, kill other DC_ sessions
    m_cleanup_thread = std::thread(&PresentMonManager::CleanupThread, this);

    LogInfo("PresentMon: Worker thread started");
}

void PresentMonManager::StopWorker(PresentMonStopReason reason) {
    // Always stop the ETW session when we have a name (exit/destructor paths). Ensures session is
    // cleared even if the worker already exited or crashed (!m_running), so restart works.
    if (m_session_name[0] != 0) {
        RequestStopEtw();
    }

    if (!m_running.load()) {
        return;
    }

    LogInfo("PresentMon: Stopping worker thread (reason: %s)", PresentMonStopReasonToString(reason));

    m_should_stop.store(true);

    // Wait for thread to finish (with timeout)
    if (m_worker_thread.joinable()) {
        // Get native handle for waiting
        HANDLE thread_handle = reinterpret_cast<HANDLE>(m_worker_thread.native_handle());
        if (thread_handle != nullptr) {
            DWORD wait_result = WaitForSingleObject(thread_handle, 2000);
            if (wait_result == WAIT_TIMEOUT) {
                LogWarn("PresentMon: Worker thread did not stop within timeout");
                // Note: We don't terminate the thread as it may be holding resources
            }
        }

        // Join the worker thread
        m_worker_thread.join();
    }

    if (m_cleanup_thread.joinable()) {
        m_cleanup_thread.join();
    }

    m_running.store(false);
    m_thread_started.store(false);

    // Update thread status
    m_thread_status.store(std::make_shared<const std::string>("Stopped"));

    LogInfo("PresentMon: Worker thread stopped");
}

bool PresentMonManager::IsRunning() const { return m_running.load(); }

bool PresentMonManager::IsNeeded() const {
    // PresentMon is needed for:
    // 1. D3D12 games (for VRR indicator)
    // 2. Non-NVIDIA hardware (for all graphics APIs)
    // 3. When ETW tracing is enabled

    // For now, always return true if enabled
    // This can be expanded later to check actual system state
    return true;
}

bool PresentMonManager::GetFlipState(PresentMonFlipState& flip_state) const {
    if (!m_flip_state_valid.load()) {
        return false;
    }

    flip_state.flip_mode = m_flip_mode.load();
    flip_state.is_valid = true;
    flip_state.last_update_time = m_flip_state_update_time.load();

    auto mode_str_ptr = m_present_mode_str.load();
    flip_state.present_mode_str = mode_str_ptr ? *mode_str_ptr : "Unknown";

    auto debug_str_ptr = m_debug_info_str.load();
    flip_state.debug_info = debug_str_ptr ? *debug_str_ptr : "";

    return true;
}

void PresentMonManager::GetDebugInfo(PresentMonDebugInfo& debug_info) const {
    debug_info.is_running = m_running.load();
    debug_info.thread_started = m_thread_started.load();
    debug_info.etw_session_active = m_etw_session_active.load();

    auto thread_status_ptr = m_thread_status.load();
    debug_info.thread_status = thread_status_ptr ? *thread_status_ptr : "Unknown";

    auto etw_status_ptr = m_etw_session_status.load();
    debug_info.etw_session_status = etw_status_ptr ? *etw_status_ptr : "Unknown";

    if (m_session_name[0] != 0) {
        debug_info.etw_session_name = Narrow(std::wstring(m_session_name));
    } else {
        debug_info.etw_session_name = "";
    }

    auto error_ptr = m_last_error.load();
    debug_info.last_error = error_ptr ? *error_ptr : "";

    debug_info.events_processed = m_events_processed.load();
    debug_info.events_processed_for_current_pid = m_events_processed_for_current_pid.load();
    debug_info.events_lost = m_events_lost.load();
    debug_info.last_event_time = m_last_event_time.load();
    debug_info.last_event_pid = m_last_event_pid.load();

    auto last_provider_ptr = m_last_provider.load();
    debug_info.last_provider = last_provider_ptr ? *last_provider_ptr : "";
    debug_info.last_event_id = m_last_event_id.load();
    auto last_pm_ptr = m_last_present_mode_value.load();
    debug_info.last_present_mode_value = last_pm_ptr ? *last_pm_ptr : "";

    auto last_provider_name_ptr = m_last_provider_name.load();
    debug_info.last_provider_name = last_provider_name_ptr ? *last_provider_name_ptr : "";
    auto last_event_name_ptr = m_last_event_name.load();
    debug_info.last_event_name = last_event_name_ptr ? *last_event_name_ptr : "";
    auto last_props_ptr = m_last_interesting_props.load();
    debug_info.last_interesting_props = last_props_ptr ? *last_props_ptr : "";

    debug_info.events_dxgkrnl = m_events_dxgkrnl.load();
    debug_info.events_dxgi = m_events_dxgi.load();
    debug_info.events_dwm = m_events_dwm.load();
    debug_info.events_d3d9 = m_events_d3d9.load();

    auto last_gprov_ptr = m_last_graphics_provider.load();
    debug_info.last_graphics_provider = last_gprov_ptr ? *last_gprov_ptr : "";
    debug_info.last_graphics_event_id = m_last_graphics_event_id.load();
    debug_info.last_graphics_event_pid = m_last_graphics_event_pid.load();
    auto last_gprov_name_ptr = m_last_graphics_provider_name.load();
    debug_info.last_graphics_provider_name = last_gprov_name_ptr ? *last_gprov_name_ptr : "";
    auto last_gevent_name_ptr = m_last_graphics_event_name.load();
    debug_info.last_graphics_event_name = last_gevent_name_ptr ? *last_gevent_name_ptr : "";
    auto last_gprops_ptr = m_last_graphics_props.load();
    debug_info.last_graphics_props = last_gprops_ptr ? *last_gprops_ptr : "";

    debug_info.etw_enumeration_error.clear();
    GetEtwSessionsWithPrefix(L"DC_", debug_info.dc_etw_sessions, &debug_info.etw_enumeration_error);
}

void PresentMonManager::UpdateFlipState(PresentMonFlipMode mode, const std::string& present_mode_str,
                                        const std::string& debug_info) {
    m_flip_mode.store(mode);
    m_flip_state_valid.store(true);
    m_flip_state_update_time.store(utils::get_now_ns());

    m_present_mode_str.store(std::make_shared<const std::string>(present_mode_str));
    m_debug_info_str.store(std::make_shared<const std::string>(debug_info));
}

void PresentMonManager::UpdateDebugInfo(const std::string& thread_status, const std::string& etw_status,
                                        const std::string& error, uint64_t events_processed, uint64_t events_lost) {
    m_thread_status.store(std::make_shared<const std::string>(thread_status));
    m_etw_session_status.store(std::make_shared<const std::string>(etw_status));

    if (!error.empty()) {
        m_last_error.store(std::make_shared<const std::string>(error));
    }

    m_events_processed.store(events_processed);
    m_events_lost.store(events_lost);
    m_last_event_time.store(utils::get_now_ns());

    m_etw_session_active.store(!etw_status.empty() && etw_status != "Not initialized" && etw_status != "Failed");
}

void PresentMonManager::WorkerThread(PresentMonManager* manager) {
    LogInfo("[PresentMon] Worker thread started");

    // Update thread status
    manager->UpdateDebugInfo("Running", "Starting ETW session...", "", 0, 0);

    // Set thread description for debugging (Windows 10+)
    typedef HRESULT(WINAPI * SetThreadDescriptionProc)(HANDLE, PCWSTR);
    static SetThreadDescriptionProc set_thread_description_func = nullptr;
    static bool checked = false;
    if (!checked) {
        HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
        if (kernel32 != nullptr) {
            set_thread_description_func =
                reinterpret_cast<SetThreadDescriptionProc>(GetProcAddress(kernel32, "SetThreadDescription"));
        }
        checked = true;
    }
    if (set_thread_description_func != nullptr) {
        set_thread_description_func(GetCurrentThread(), L"[DisplayCommander] PresentMon Worker");
    }

    // Run ETW collection loop
    t_active_manager = manager;
    int result = manager->PresentMonMain();
    t_active_manager = nullptr;

    auto err_ptr = manager->m_last_error.load();
    const char* err_str = (err_ptr && !err_ptr->empty()) ? err_ptr->c_str() : nullptr;
    if (err_str != nullptr) {
        LogWarn("[PresentMon] Worker thread exiting with code %d, last_error: %s", result, err_str);
    } else {
        LogInfo(
            "[PresentMon] Worker thread exiting with code %d (0=normal stop, 1=oom, 2=StartTrace failed, 3=no "
            "providers, 4=OpenTrace failed)",
            result);
    }

    // Update thread status
    manager->UpdateDebugInfo("Exited", "Stopped", "", manager->m_events_processed.load(),
                             manager->m_events_lost.load());

    manager->m_running.store(false);
}

void PresentMonManager::RequestStopEtw() {
    uint64_t sh = m_etw_session_handle.load();
    if (m_session_name[0] == 0) {
        return;
    }

    // Stop by handle when available (unblocks ProcessTrace quickly)
    if (sh != 0) {
        EVENT_TRACE_PROPERTIES props = {};
        props.Wnode.BufferSize = sizeof(EVENT_TRACE_PROPERTIES);
        ULONG status = ControlTraceW(static_cast<TRACEHANDLE>(sh), m_session_name, &props, EVENT_TRACE_CONTROL_STOP);
        if (status == ERROR_SUCCESS || status == ERROR_WMI_INSTANCE_NOT_FOUND) {
            m_etw_session_handle.store(0);
        }
    }

    // Always stop by name as well: handles process terminate (handle may be invalid or ControlTrace
    // didn't complete before process tear-down) and ensures session is cleared for next run.
    StopEtwSessionByName(m_session_name);
}

bool PresentMonManager::QueryEtwSessionByName(const wchar_t* session_name, TRACEHANDLE& out_handle) {
    out_handle = 0;
    if (session_name == nullptr || session_name[0] == 0) return false;

    // Query existing session by name
    ULONG props_size = sizeof(EVENT_TRACE_PROPERTIES) + 512;
    std::unique_ptr<uint8_t[]> props_buf(new (std::nothrow) uint8_t[props_size]);
    if (!props_buf) return false;

    ZeroMemory(props_buf.get(), props_size);
    auto* props = reinterpret_cast<EVENT_TRACE_PROPERTIES*>(props_buf.get());
    props->Wnode.BufferSize = props_size;
    props->LoggerNameOffset = sizeof(EVENT_TRACE_PROPERTIES);
    // Set GUID for query (can be zero GUID for private sessions)
    ZeroMemory(&props->Wnode.Guid, sizeof(GUID));

    // Query the session - use NULL handle with session name
    ULONG status = ControlTraceW(NULL, session_name, props, EVENT_TRACE_CONTROL_QUERY);
    if (status == ERROR_SUCCESS) {
        // Session exists, extract handle from Wnode.HistoricalContext
        // Note: HistoricalContext contains the session handle for controlling the session
        out_handle = static_cast<TRACEHANDLE>(props->Wnode.HistoricalContext);
        return true;
    }

    return false;
}

void PresentMonManager::StopEtwSessionByName(const wchar_t* session_name) {
    if (session_name == nullptr || session_name[0] == 0) return;

    // Try to stop the session by name (useful when handle is lost)
    ULONG props_size = sizeof(EVENT_TRACE_PROPERTIES) + 512;
    std::unique_ptr<uint8_t[]> props_buf(new (std::nothrow) uint8_t[props_size]);
    if (!props_buf) return;

    ZeroMemory(props_buf.get(), props_size);
    auto* props = reinterpret_cast<EVENT_TRACE_PROPERTIES*>(props_buf.get());
    props->Wnode.BufferSize = props_size;
    props->LoggerNameOffset = sizeof(EVENT_TRACE_PROPERTIES);

    // Use NULL handle with session name to stop by name
    ULONG status = ControlTraceW(NULL, session_name, props, EVENT_TRACE_CONTROL_STOP);
    // Ignore errors - session may not exist or may already be stopped
    (void)status;
}

void PresentMonManager::GetEtwSessionsWithPrefix(const wchar_t* prefix, std::vector<std::string>& out_session_names,
                                                 std::string* out_error) {
    out_session_names.clear();
    if (out_error) out_error->clear();
    if (prefix == nullptr) return;

    // Empty prefix (L"") means return all sessions; otherwise filter by prefix
    const size_t prefix_len = (prefix[0] != L'\0') ? wcslen(prefix) : 0;

    // QueryAllTracesW can return up to 64 sessions (or more on Windows 10+)
    constexpr ULONG max_sessions = 128;
    ULONG session_count = 0;

    // Allocate buffer for session properties
    // Each session needs space for EVENT_TRACE_PROPERTIES + session name + log file name
    constexpr ULONG props_size = sizeof(EVENT_TRACE_PROPERTIES) + 2048;  // Extra space for names
    std::vector<std::unique_ptr<uint8_t[]>> prop_buffers(max_sessions);
    std::vector<PEVENT_TRACE_PROPERTIES> prop_ptrs(max_sessions);

    for (ULONG i = 0; i < max_sessions; ++i) {
        prop_buffers[i] = std::unique_ptr<uint8_t[]>(new (std::nothrow) uint8_t[props_size]);
        if (!prop_buffers[i]) {
            if (out_error) *out_error = "Out of memory enumerating ETW sessions";
            return;
        }
        ZeroMemory(prop_buffers[i].get(), props_size);
        auto* props = reinterpret_cast<EVENT_TRACE_PROPERTIES*>(prop_buffers[i].get());
        props->Wnode.BufferSize = props_size;
        props->LoggerNameOffset = sizeof(EVENT_TRACE_PROPERTIES);
        props->LogFileNameOffset = sizeof(EVENT_TRACE_PROPERTIES) + 1024;  // Session name max ~1024 chars
        prop_ptrs[i] = props;
    }

    // Query all ETW sessions
    ULONG status = QueryAllTracesW(prop_ptrs.data(), max_sessions, &session_count);
    if (status != ERROR_SUCCESS && status != ERROR_MORE_DATA) {
        char msg[128] = {};
        StringCchPrintfA(msg, std::size(msg), "QueryAllTracesW failed: %lu (e.g. access denied)",
                         static_cast<unsigned long>(status));
        if (out_error) *out_error = msg;
        LogWarn("PresentMon: %s", msg);
        return;
    }

    if (status == ERROR_MORE_DATA) {
        // More sessions exist than max_sessions; we still have partial list
        session_count = max_sessions;
    }

    // Filter sessions by prefix (prefix_len 0 = include all)
    for (ULONG i = 0; i < session_count && i < max_sessions; ++i) {
        auto* props = prop_ptrs[i];
        if (props == nullptr) continue;

        // Extract session name from properties
        const wchar_t* session_name =
            reinterpret_cast<const wchar_t*>(reinterpret_cast<const uint8_t*>(props) + props->LoggerNameOffset);

        if (session_name != nullptr && _wcsnicmp(session_name, prefix, prefix_len) == 0) {
            // Session name starts with prefix, add it to output
            out_session_names.push_back(Narrow(std::wstring(session_name)));
        }
    }
}

void PresentMonManager::LogAllEtwSessions() {
    if (true) {
        return;
    }
    constexpr ULONG max_sessions = 128;
    ULONG session_count = 0;
    constexpr ULONG props_size = sizeof(EVENT_TRACE_PROPERTIES) + 2048;
    std::vector<std::unique_ptr<uint8_t[]>> prop_buffers(max_sessions);
    std::vector<PEVENT_TRACE_PROPERTIES> prop_ptrs(max_sessions);

    for (ULONG i = 0; i < max_sessions; ++i) {
        prop_buffers[i] = std::unique_ptr<uint8_t[]>(new (std::nothrow) uint8_t[props_size]);
        if (!prop_buffers[i]) {
            LogWarn("[PresentMon] LogAllEtwSessions: out of memory allocating buffer %lu",
                    static_cast<unsigned long>(i));
            return;
        }
        ZeroMemory(prop_buffers[i].get(), props_size);
        auto* props = reinterpret_cast<EVENT_TRACE_PROPERTIES*>(prop_buffers[i].get());
        props->Wnode.BufferSize = props_size;
        props->LoggerNameOffset = sizeof(EVENT_TRACE_PROPERTIES);
        props->LogFileNameOffset = sizeof(EVENT_TRACE_PROPERTIES) + 1024;
        prop_ptrs[i] = props;
    }

    ULONG status = QueryAllTracesW(prop_ptrs.data(), max_sessions, &session_count);
    if (status != ERROR_SUCCESS && status != ERROR_MORE_DATA) {
        LogWarn("[PresentMon] QueryAllTracesW failed: %lu (e.g. access denied)", static_cast<unsigned long>(status));
        return;
    }

    if (status == ERROR_MORE_DATA) {
        LogInfo("[PresentMon] ETW sessions: more than %u exist, listing first %u", max_sessions, max_sessions);
        session_count = max_sessions;
    } else {
        LogInfo("[PresentMon] ETW sessions on start (%u total):", static_cast<unsigned>(session_count));
    }

    // Scratch buffer for re-querying each session (detect orphaned/stale entries)
    constexpr ULONG query_props_size = sizeof(EVENT_TRACE_PROPERTIES) + 512;
    std::unique_ptr<uint8_t[]> query_buf(new (std::nothrow) uint8_t[query_props_size]);
    const bool can_validate = (query_buf != nullptr);

    for (ULONG i = 0; i < session_count && i < max_sessions; ++i) {
        auto* props = prop_ptrs[i];
        if (props == nullptr) continue;
        const wchar_t* session_name =
            reinterpret_cast<const wchar_t*>(reinterpret_cast<const uint8_t*>(props) + props->LoggerNameOffset);
        if (session_name == nullptr || session_name[0] == L'\0') {
            LogInfo("[PresentMon]   ETW session: (unnamed or invalid)");
            continue;
        }

        if (can_validate) {
            ZeroMemory(query_buf.get(), query_props_size);
            auto* q = reinterpret_cast<EVENT_TRACE_PROPERTIES*>(query_buf.get());
            q->Wnode.BufferSize = query_props_size;
            q->LoggerNameOffset = sizeof(EVENT_TRACE_PROPERTIES);
            q->LogFileNameOffset = sizeof(EVENT_TRACE_PROPERTIES) + 256;
            ULONG qstatus = ControlTraceW(NULL, session_name, q, EVENT_TRACE_CONTROL_QUERY);
            if (qstatus == ERROR_WMI_INSTANCE_NOT_FOUND) {
                LogInfo("[PresentMon]   ETW session: %ls (not running - possibly orphaned)", session_name);
                continue;
            }
            if (qstatus == ERROR_ACCESS_DENIED) {
                LogInfo("[PresentMon]   ETW session: %ls (query access denied)", session_name);
                continue;
            }
            if (qstatus == ERROR_SUCCESS && (q->EventsLost != 0 || q->RealTimeBuffersLost != 0)) {
                LogInfo("[PresentMon]   ETW session: %ls (EventsLost=%lu RealTimeBuffersLost=%lu)", session_name,
                        static_cast<unsigned long>(q->EventsLost), static_cast<unsigned long>(q->RealTimeBuffersLost));
                continue;
            }
        }
        LogInfo("[PresentMon]   ETW session: %ls", session_name);
    }
}

void PresentMonManager::StopAllDcEtwSessions() {
    const DWORD our_pid = GetCurrentProcessId();
    std::vector<std::string> sessions;
    GetEtwSessionsWithPrefix(L"DC_", sessions);

    for (const std::string& name : sessions) {
        DWORD session_pid = 0;
        if (ParsePidFromDcPresentMonSessionName(name, session_pid)) {
            if (session_pid == our_pid) {
                LogInfo("[PresentMon] Skip our own DC_ session at startup (pid %lu): %s",
                        static_cast<unsigned long>(our_pid), name.c_str());
                continue;
            }
            // Stop all other DC_ sessions so we can start clean (even if that PID is still running).
        }
        std::wstring wide_name = Widen(name);
        if (!wide_name.empty()) {
            StopEtwSessionByName(wide_name.c_str());
            LogInfo("PresentMon: Stopped DC_ ETW session %s (startup cleanup)", name.c_str());
        }
    }
}

void PresentMonManager::StopOtherDcEtwSessions(const wchar_t* our_session_name) {
    if (our_session_name == nullptr || our_session_name[0] == L'\0') return;

    const DWORD our_pid = GetCurrentProcessId();
    std::string our_name_narrow = Narrow(std::wstring(our_session_name));
    if (our_name_narrow.empty()) return;

    std::vector<std::string> sessions;
    GetEtwSessionsWithPrefix(L"DC_", sessions);

    for (const std::string& name : sessions) {
        if (name == our_name_narrow) {
            LogInfo("[PresentMon] Skip our own DC_ session: %s", name.c_str());
            continue;
        }

        DWORD session_pid = 0;
        if (!ParsePidFromDcPresentMonSessionName(name, session_pid)) {
            LogInfo("[PresentMon] Skip DC_ session (unknown format, not stopping): %s", name.c_str());
            continue;
        }
        if (session_pid == our_pid) {
            LogInfo("[PresentMon] Skip our own DC_ session (by PID): %s", name.c_str());
            continue;
        }

        std::wstring wide_name = Widen(name);
        if (!wide_name.empty()) {
            StopEtwSessionByName(wide_name.c_str());
            if (IsProcessRunning(session_pid)) {
                LogInfo("PresentMon: Stopped other DC_ ETW session %s (pid %lu still running)", name.c_str(),
                        static_cast<unsigned long>(session_pid));
            } else {
                LogInfo("PresentMon: Stopped orphaned DC_ ETW session %s (pid %lu not running)", name.c_str(),
                        static_cast<unsigned long>(session_pid));
            }
        }
    }
}

void PresentMonManager::CleanupThread(PresentMonManager* manager) {
    constexpr int64_t k_no_events_interval_ns = 15LL * 1000000000LL;  // 15 seconds

    while (!manager->m_should_stop.load()) {
        for (int i = 0; i < 15; ++i) {
            if (manager->m_should_stop.load()) {
                return;
            }
            Sleep(1000);
        }

        // If we haven't seen any events for 10s, stop other DC_ ETW sessions (orphaned or not).
        // Stopping a session only ends the trace; it does not terminate the process.
        int64_t now_ns = static_cast<int64_t>(utils::get_now_ns());
        int64_t last_ns = static_cast<int64_t>(manager->m_last_event_time.load());
        if (last_ns > 0 && (now_ns - last_ns) >= k_no_events_interval_ns) {
            LogInfo("[PresentMon] No events for 10s; stopping other DC_ ETW sessions (our session: %ls)",
                    manager->m_session_name);
            StopOtherDcEtwSessions(manager->m_session_name);
        }
    }
}

void WINAPI PresentMonManager::EtwEventRecordCallback(PEVENT_RECORD event_record) {
    if (event_record == nullptr) return;
    // Route via TLS if possible; otherwise use global instance (if created)
    PresentMonManager* mgr = t_active_manager;
    if (mgr == nullptr) {
        mgr = &g_presentMonManager;
    }
    if (mgr != nullptr) {
        mgr->OnEtwEvent(event_record);
    }
}

void PresentMonManager::OnEtwEvent(PEVENT_RECORD event_record) {
    // Count all events (some relevant present/flip signals can come from DWM/system/kernel context)
    m_events_processed.fetch_add(1);
    m_last_event_time.store(utils::get_now_ns());
    m_last_event_pid.store(event_record->EventHeader.ProcessId);

    const bool is_current_pid = (event_record->EventHeader.ProcessId == GetCurrentProcessId());
    if (is_current_pid) {
        m_events_processed_for_current_pid.fetch_add(1);
    }

    // Store last provider + event id
    m_last_provider.store(
        std::make_shared<const std::string>(ProviderGuidToString(event_record->EventHeader.ProviderId)));
    m_last_event_id.store(event_record->EventHeader.EventDescriptor.Id);

    // Track graphics-relevant providers separately (DxgKrnl/DXGI/DWM/D3D9)
    const bool is_dxgkrnl = m_have_dxgkrnl && IsEqualGUID(event_record->EventHeader.ProviderId, m_guid_dxgkrnl);
    const bool is_dxgi = m_have_dxgi && IsEqualGUID(event_record->EventHeader.ProviderId, m_guid_dxgi);
    const bool is_dwm = m_have_dwm && IsEqualGUID(event_record->EventHeader.ProviderId, m_guid_dwm);
    const bool is_d3d9 = m_have_d3d9 && IsEqualGUID(event_record->EventHeader.ProviderId, m_guid_d3d9);
    const bool is_graphics_provider = (is_dxgkrnl || is_dxgi || is_dwm || is_d3d9);

    if (is_dxgkrnl) m_events_dxgkrnl.fetch_add(1);
    if (is_dxgi) m_events_dxgi.fetch_add(1);
    if (is_dwm) m_events_dwm.fetch_add(1);
    if (is_d3d9) m_events_d3d9.fetch_add(1);

    // Per-draw statistics: one event per Present() call
    const uint16_t event_id = event_record->EventHeader.EventDescriptor.Id;
    bool per_draw_global_incremented = false;
    if (is_dxgi && event_id == k_dxgi_present_start_id) {
        m_per_draw_global_count.fetch_add(1);
        per_draw_global_incremented = true;
    }
    if (is_d3d9 && event_id == k_d3d9_present_start_id) {
        m_per_draw_global_count.fetch_add(1);
        per_draw_global_incremented = true;
    }
    // 1s sliding window for rate: advance boundary when event timestamp crosses 1 second
    if (per_draw_global_incremented) {
        const uint64_t time_100ns = static_cast<uint64_t>(event_record->EventHeader.TimeStamp.QuadPart);
        const uint64_t boundary = m_per_draw_1s_boundary_100ns.load(std::memory_order_relaxed);
        if (boundary == 0 || (time_100ns - boundary) >= 10000000u) {  // 1 s = 10^7 * 100ns
            m_per_draw_global_count_at_1s_ago.store(m_per_draw_global_count.load(std::memory_order_relaxed),
                                                   std::memory_order_relaxed);
            m_per_draw_1s_boundary_100ns.store(time_100ns, std::memory_order_relaxed);
        }
    }
    if (is_dxgkrnl && event_id == k_dxgkrnl_present_info_id) {
        uint64_t hwnd = 0;
        if (TryGetEventPropertyU64(event_record, L"hWnd", hwnd)
            || TryGetEventPropertyU64(event_record, L"hwnd", hwnd)
            || TryGetEventPropertyU64(event_record, L"HWND", hwnd)) {
            if (hwnd != 0) {
                const uint64_t key = HashHwndKey(hwnd);
                size_t idx = static_cast<size_t>(key % k_per_draw_hwnd_cache_size);
                for (size_t probe = 0; probe < k_per_draw_hwnd_cache_size; ++probe) {
                    auto& e = m_per_draw_hwnd_cache[idx];
                    uint64_t existing = e.hwnd.load(std::memory_order_relaxed);
                    if (existing == hwnd) {
                        e.count.fetch_add(1);
                        break;
                    }
                    if (existing == 0) {
                        uint64_t expected = 0;
                        if (e.hwnd.compare_exchange_strong(expected, hwnd, std::memory_order_acq_rel)) {
                            e.count.store(1);
                            break;
                        }
                    }
                    idx = (idx + 1) % k_per_draw_hwnd_cache_size;
                }
            }
        }
    }

    if (is_graphics_provider) {
        m_last_graphics_provider.store(std::make_shared<const std::string>(
            ProviderGuidToString(event_record->EventHeader.ProviderId)));
        m_last_graphics_event_id.store(event_record->EventHeader.EventDescriptor.Id);
        m_last_graphics_event_pid.store(event_record->EventHeader.ProcessId);
    }

    // Opportunistically map surfaceLuid -> hwnd when both appear in any event
    if (is_dwm) {
        UpdateSurfaceWindowMappingFromEvent(event_record);
    }

    // Update DWM flip-compatibility snapshot from known DWM events (best-effort)
    if (is_dwm) {
        UpdateFlipCompatibilityFromDwmEvent(event_record);
    }

    // Always track event types (for UI exploration). This is rate-limited internally.
    TrackEventType(event_record, is_graphics_provider);

    // Occasionally introspect schema + interesting properties (rate-limited)
    {
        uint64_t now_ns = static_cast<uint64_t>(utils::get_now_ns());
        uint64_t last_ns =
            is_graphics_provider ? m_last_graphics_schema_update_time_ns.load() : m_last_schema_update_time_ns.load();
        const uint64_t one_sec_ns = 1000000000ULL;
        if (now_ns - last_ns > one_sec_ns) {
            if (is_graphics_provider) {
                m_last_graphics_schema_update_time_ns.store(now_ns);
            } else {
                // Do not spam schema from unrelated providers; keep last overall for fallback,
                // but prefer graphics provider schema in UI.
                m_last_schema_update_time_ns.store(now_ns);
            }

            ULONG info_size = 0;
            ULONG st = TdhGetEventInformation(event_record, 0, nullptr, nullptr, &info_size);
            if (st == ERROR_INSUFFICIENT_BUFFER && info_size > 0) {
                std::unique_ptr<uint8_t[]> info_buf(new (std::nothrow) uint8_t[info_size]);
                if (info_buf) {
                    auto* info = reinterpret_cast<TRACE_EVENT_INFO*>(info_buf.get());
                    st = TdhGetEventInformation(event_record, 0, nullptr, info, &info_size);
                    if (st == ERROR_SUCCESS) {
                        std::wstring provider_name = GetTraceEventInfoString(info, info->ProviderNameOffset);
                        std::wstring event_name = GetTraceEventInfoString(info, info->EventNameOffset);

                        if (is_graphics_provider) {
                            m_last_graphics_provider_name.store(
                                std::make_shared<const std::string>(Narrow(provider_name)));
                            m_last_graphics_event_name.store(
                                std::make_shared<const std::string>(Narrow(event_name)));
                        } else {
                            m_last_provider_name.store(
                                std::make_shared<const std::string>(Narrow(provider_name)));
                            m_last_event_name.store(
                                std::make_shared<const std::string>(Narrow(event_name)));
                        }

                        // Build a compact "interesting properties" summary
                        std::string summary;
                        {
                            // Always include core descriptor bits (helps mapping unknown events)
                            char hdr[256] = {};
                            StringCchPrintfA(
                                hdr, std::size(hdr), "task=%u opcode=%u level=%u keyword=0x%llx",
                                static_cast<unsigned int>(event_record->EventHeader.EventDescriptor.Task),
                                static_cast<unsigned int>(event_record->EventHeader.EventDescriptor.Opcode),
                                static_cast<unsigned int>(event_record->EventHeader.EventDescriptor.Level),
                                static_cast<unsigned long long>(event_record->EventHeader.EventDescriptor.Keyword));
                            summary = hdr;
                        }

                        int added = 0;
                        for (ULONG i = 0; i < info->TopLevelPropertyCount && added < 12; ++i) {
                            const EVENT_PROPERTY_INFO& pi = info->EventPropertyInfoArray[i];
                            std::wstring prop_name = GetTraceEventInfoString(info, pi.NameOffset);
                            if (prop_name.empty()) continue;

                            // Filter to properties likely to contain present/flip/composition information
                            if (!(StringContainsI_w(prop_name, L"present") || StringContainsI_w(prop_name, L"flip")
                                  || StringContainsI_w(prop_name, L"composition")
                                  || StringContainsI_w(prop_name, L"independent")
                                  || StringContainsI_w(prop_name, L"overlay") || StringContainsI_w(prop_name, L"dwm")
                                  || StringContainsI_w(prop_name, L"tearing")
                                  || StringContainsI_w(prop_name, L"sync"))) {
                                continue;
                            }

                            USHORT in_type = 0;
                            if (pi.Flags & PropertyStruct) {
                                // Still record property name (struct), but we don't try to decode value yet.
                                summary += " | ";
                                summary += Narrow(prop_name);
                                summary += "=(struct)";
                                ++added;
                                continue;
                            } else {
                                in_type = pi.nonStructType.InType;
                            }

                            std::string value = FormatPropValueBestEffort(event_record, prop_name, in_type);
                            if (value.empty()) {
                                // Record property name even if we can't decode value (helps iterating schemas)
                                summary += " | ";
                                summary += Narrow(prop_name);
                                summary += "=?";
                                ++added;
                                continue;
                            }

                            summary += " | ";
                            summary += Narrow(prop_name);
                            summary += "=";
                            summary += value;
                            ++added;
                        }

                        // If no interesting props matched, fall back to listing the first few property names
                        if (added == 0 && info->TopLevelPropertyCount > 0) {
                            int listed = 0;
                            for (ULONG i = 0; i < info->TopLevelPropertyCount && listed < 12; ++i) {
                                const EVENT_PROPERTY_INFO& pi = info->EventPropertyInfoArray[i];
                                std::wstring prop_name = GetTraceEventInfoString(info, pi.NameOffset);
                                if (prop_name.empty()) continue;
                                summary += " | ";
                                summary += Narrow(prop_name);
                                summary += "=?";
                                ++listed;
                            }
                        }

                        if (is_graphics_provider) {
                            m_last_graphics_props.store(std::make_shared<const std::string>(summary));
                        } else {
                            m_last_interesting_props.store(std::make_shared<const std::string>(summary));
                        }

                        // Try infer from common numeric/bool fields if present
                        uint64_t u = 0;
                        if (TryGetEventPropertyU64(event_record, L"IndependentFlip", u)
                            || TryGetEventPropertyU64(event_record, L"IsIndependentFlip", u)) {
                            if (u != 0)
                                UpdateFlipState(PresentMonFlipMode::IndependentFlip, "IndependentFlip=1",
                                                "ETW bool field");
                        }
                        if (TryGetEventPropertyU64(event_record, L"Overlay", u)
                            || TryGetEventPropertyU64(event_record, L"IsOverlay", u)) {
                            if (u != 0) UpdateFlipState(PresentMonFlipMode::Overlay, "Overlay=1", "ETW bool field");
                        }
                        if (TryGetEventPropertyU64(event_record, L"Composed", u)
                            || TryGetEventPropertyU64(event_record, L"IsComposed", u)) {
                            if (u != 0) UpdateFlipState(PresentMonFlipMode::Composed, "Composed=1", "ETW bool field");
                        }

                        // PresentMode numeric mapping (best-effort)
                        if (TryGetEventPropertyU64(event_record, L"PresentMode", u)) {
                            char buf[64] = {};
                            StringCchPrintfA(buf, std::size(buf), "PresentMode=%llu",
                                             static_cast<unsigned long long>(u));
                            m_last_present_mode_value.store(std::make_shared<const std::string>(buf));
                            if (u == 0)
                                UpdateFlipState(PresentMonFlipMode::Composed, buf, "ETW PresentMode numeric");
                            else if (u == 1)
                                UpdateFlipState(PresentMonFlipMode::Overlay, buf, "ETW PresentMode numeric");
                            else if (u == 2)
                                UpdateFlipState(PresentMonFlipMode::IndependentFlip, buf, "ETW PresentMode numeric");
                        }
                    }
                }
            }
        }
    }

    // Try extract present mode-like property from this event.
    // We intentionally use a best-effort approach based on property names, so we don't depend on
    // a copied manifest table.
    std::string present_mode;
    if (TryGetEventPropertyString(event_record, L"PresentMode", present_mode)
        || TryGetEventPropertyString(event_record, L"presentMode", present_mode)
        || TryGetEventPropertyString(event_record, L"Present_Mode", present_mode)
        || TryGetEventPropertyString(event_record, L"CompositionMode", present_mode)
        || TryGetEventPropertyString(event_record, L"compositionMode", present_mode)) {
        // Store last seen present mode-like value for UI/debugging
        m_last_present_mode_value.store(std::make_shared<const std::string>(present_mode));

        PresentMonFlipMode mode = MapPresentModeStringToFlip(present_mode);
        if (mode != PresentMonFlipMode::Unknown) {
            UpdateFlipState(mode, present_mode, "ETW property match");
        }
    }

    // Do not overwrite ETW status string here (it contains provider enable return codes).
}

void PresentMonManager::UpdateSurfaceWindowMappingFromEvent(PEVENT_RECORD event_record) {
    // Some DWM events may include both a surface identifier and hwnd.
    uint64_t surface_luid = 0;
    uint64_t hwnd = 0;

    // Try common spellings
    bool has_surface = TryGetEventPropertyU64(event_record, L"surfaceLuid", surface_luid)
                       || TryGetEventPropertyU64(event_record, L"luidSurface", surface_luid)
                       || TryGetEventPropertyU64(event_record, L"luid", surface_luid);
    bool has_hwnd = TryGetEventPropertyU64(event_record, L"hwnd", hwnd)
                    || TryGetEventPropertyU64(event_record, L"hWnd", hwnd)
                    || TryGetEventPropertyU64(event_record, L"HWND", hwnd);

    if (!has_surface || !has_hwnd || surface_luid == 0 || hwnd == 0) {
        return;
    }

    const uint64_t key = HashSurfaceKey(surface_luid);
    size_t idx = static_cast<size_t>(key % k_surface_cache_size);
    for (size_t probe = 0; probe < k_surface_cache_size; ++probe) {
        auto& e = m_surface_cache[idx];
        uint64_t existing = e.key_hash.load(std::memory_order_relaxed);
        if (existing == key) {
            e.hwnd.store(hwnd);
            return;
        }
        if (existing == 0) {
            uint64_t expected = 0;
            if (e.key_hash.compare_exchange_strong(expected, key, std::memory_order_acq_rel)) {
                e.surface_luid.store(surface_luid);
                e.hwnd.store(hwnd);
                e.last_update_ns.store(static_cast<uint64_t>(utils::get_now_ns()));
                e.count.store(0);
                return;
            }
        }
        idx = (idx + 1) % k_surface_cache_size;
    }
}

void PresentMonManager::UpdateFlipCompatibilityFromDwmEvent(PEVENT_RECORD event_record) {
    // We key on the user-discovered event type: DWM-Core EventId=291 Task=207
    const auto& d = event_record->EventHeader.EventDescriptor;
    if (d.Id != 291 || d.Task != 207) {
        return;
    }

    // Required fields (best-effort reads)
    uint64_t surface_luid = 0;
    uint64_t surface_width = 0;
    uint64_t surface_height = 0;
    uint64_t pixel_format = 0;
    uint64_t flags = 0;
    uint64_t color_space = 0;

    uint64_t is_direct = 0;
    uint64_t is_adv_direct = 0;
    uint64_t is_overlay = 0;
    uint64_t is_overlay_required = 0;
    uint64_t no_overlapping = 0;

    // NOTE: property names taken from your ETW Event Type Explorer output.
    (void)TryGetEventPropertyU64(event_record, L"surfaceLuid", surface_luid);
    (void)TryGetEventPropertyU64(event_record, L"SurfaceWidth", surface_width);
    (void)TryGetEventPropertyU64(event_record, L"SurfaceHeight", surface_height);
    (void)TryGetEventPropertyU64(event_record, L"PixelFormat", pixel_format);
    (void)TryGetEventPropertyU64(event_record, L"Flags", flags);
    (void)TryGetEventPropertyU64(event_record, L"ColorSpace", color_space);

    (void)TryGetEventPropertyU64(event_record, L"IsDirectFlipCompatible", is_direct);
    (void)TryGetEventPropertyU64(event_record, L"IsAdvancedDirectFlipCompatible", is_adv_direct);
    (void)TryGetEventPropertyU64(event_record, L"IsOverlayCompatible", is_overlay);
    (void)TryGetEventPropertyU64(event_record, L"IsOverlayRequired", is_overlay_required);
    (void)TryGetEventPropertyU64(event_record, L"fNoOverlappingContent", no_overlapping);

    m_flip_compat_surface_luid.store(surface_luid);
    m_flip_compat_surface_width.store(static_cast<uint32_t>(surface_width));
    m_flip_compat_surface_height.store(static_cast<uint32_t>(surface_height));
    m_flip_compat_pixel_format.store(static_cast<uint32_t>(pixel_format));
    m_flip_compat_flags.store(static_cast<uint32_t>(flags));
    m_flip_compat_color_space.store(static_cast<uint32_t>(color_space));

    m_flip_compat_is_direct.store(static_cast<uint32_t>(is_direct != 0));
    m_flip_compat_is_adv_direct.store(static_cast<uint32_t>(is_adv_direct != 0));
    m_flip_compat_is_overlay.store(static_cast<uint32_t>(is_overlay != 0));
    m_flip_compat_is_overlay_required.store(static_cast<uint32_t>(is_overlay_required != 0));
    m_flip_compat_no_overlapping.store(static_cast<uint32_t>(no_overlapping != 0));

    m_flip_compat_last_update_ns.store(static_cast<uint64_t>(utils::get_now_ns()));
    m_flip_compat_valid.store(true);

    // Also update per-surface cache (last 10s UI)
    const uint64_t key = HashSurfaceKey(surface_luid);
    size_t idx = static_cast<size_t>(key % k_surface_cache_size);
    for (size_t probe = 0; probe < k_surface_cache_size; ++probe) {
        auto& e = m_surface_cache[idx];
        uint64_t existing = e.key_hash.load(std::memory_order_relaxed);
        if (existing == key) {
            e.surface_luid.store(surface_luid);
            e.surface_width.store(static_cast<uint32_t>(surface_width));
            e.surface_height.store(static_cast<uint32_t>(surface_height));
            e.pixel_format.store(static_cast<uint32_t>(pixel_format));
            e.flags.store(static_cast<uint32_t>(flags));
            e.color_space.store(static_cast<uint32_t>(color_space));

            e.is_direct.store(static_cast<uint32_t>(is_direct != 0));
            e.is_adv_direct.store(static_cast<uint32_t>(is_adv_direct != 0));
            e.is_overlay.store(static_cast<uint32_t>(is_overlay != 0));
            e.is_overlay_required.store(static_cast<uint32_t>(is_overlay_required != 0));
            e.no_overlapping.store(static_cast<uint32_t>(no_overlapping != 0));

            e.last_update_ns.store(static_cast<uint64_t>(utils::get_now_ns()));
            e.count.fetch_add(1);
            return;
        }
        if (existing == 0) {
            uint64_t expected = 0;
            if (e.key_hash.compare_exchange_strong(expected, key, std::memory_order_acq_rel)) {
                e.surface_luid.store(surface_luid);
                e.surface_width.store(static_cast<uint32_t>(surface_width));
                e.surface_height.store(static_cast<uint32_t>(surface_height));
                e.pixel_format.store(static_cast<uint32_t>(pixel_format));
                e.flags.store(static_cast<uint32_t>(flags));
                e.color_space.store(static_cast<uint32_t>(color_space));

                e.is_direct.store(static_cast<uint32_t>(is_direct != 0));
                e.is_adv_direct.store(static_cast<uint32_t>(is_adv_direct != 0));
                e.is_overlay.store(static_cast<uint32_t>(is_overlay != 0));
                e.is_overlay_required.store(static_cast<uint32_t>(is_overlay_required != 0));
                e.no_overlapping.store(static_cast<uint32_t>(no_overlapping != 0));

                e.last_update_ns.store(static_cast<uint64_t>(utils::get_now_ns()));
                e.count.store(1);
                return;
            }
        }
        idx = (idx + 1) % k_surface_cache_size;
    }
}

void PresentMonManager::GetRecentFlipCompatibilitySurfaces(std::vector<PresentMonSurfaceCompatibilitySummary>& out,
                                                           uint64_t within_ms) const {
    out.clear();
    const uint64_t now_ns = static_cast<uint64_t>(utils::get_now_ns());
    const uint64_t within_ns = within_ms * 1000000ULL;

    out.reserve(k_surface_cache_size);
    for (size_t i = 0; i < k_surface_cache_size; ++i) {
        const auto& e = m_surface_cache[i];
        uint64_t key = e.key_hash.load();
        if (key == 0) continue;

        uint64_t last_ns = e.last_update_ns.load();
        if (last_ns == 0) continue;
        if (now_ns - last_ns > within_ns) continue;

        PresentMonSurfaceCompatibilitySummary s;
        s.surface_luid = e.surface_luid.load();
        s.last_update_time_ns = last_ns;
        s.count = e.count.load();
        s.hwnd = e.hwnd.load();

        s.surface_width = e.surface_width.load();
        s.surface_height = e.surface_height.load();
        s.pixel_format = e.pixel_format.load();
        s.flags = e.flags.load();
        s.color_space = e.color_space.load();

        s.is_direct_flip_compatible = (e.is_direct.load() != 0);
        s.is_advanced_direct_flip_compatible = (e.is_adv_direct.load() != 0);
        s.is_overlay_compatible = (e.is_overlay.load() != 0);
        s.is_overlay_required = (e.is_overlay_required.load() != 0);
        s.no_overlapping_content = (e.no_overlapping.load() != 0);

        out.push_back(std::move(s));
    }

    std::sort(out.begin(), out.end(),
              [](const PresentMonSurfaceCompatibilitySummary& a, const PresentMonSurfaceCompatibilitySummary& b) {
                  return a.last_update_time_ns > b.last_update_time_ns;
              });
}

void PresentMonManager::GetPerDrawStats(PresentMonPerDrawStats& out, uint64_t hwnd_for_window) const {
    out.global_count = m_per_draw_global_count.load();
    out.count_for_window = 0;
    out.window_matched = false;
    out.rate_global_per_sec = 0.0;
    const uint64_t boundary_100ns = m_per_draw_1s_boundary_100ns.load(std::memory_order_relaxed);
    if (boundary_100ns != 0) {
        ULARGE_INTEGER ft;
        GetSystemTimePreciseAsFileTime(reinterpret_cast<FILETIME*>(&ft));
        const uint64_t now_100ns = ft.QuadPart;
        const uint64_t elapsed_100ns = now_100ns - boundary_100ns;
        const double elapsed_s = static_cast<double>(elapsed_100ns) / 1e7;
        if (elapsed_s >= 0.1) {
            const uint64_t count_1s_ago = m_per_draw_global_count_at_1s_ago.load(std::memory_order_relaxed);
            const uint64_t cur = m_per_draw_global_count.load(std::memory_order_relaxed);
            if (cur >= count_1s_ago) {
                out.rate_global_per_sec = static_cast<double>(cur - count_1s_ago) / elapsed_s;
            }
        }
    }
    if (hwnd_for_window == 0) {
        return;
    }
    const uint64_t key = HashHwndKey(hwnd_for_window);
    size_t idx = static_cast<size_t>(key % k_per_draw_hwnd_cache_size);
    for (size_t probe = 0; probe < k_per_draw_hwnd_cache_size; ++probe) {
        const auto& e = m_per_draw_hwnd_cache[idx];
        if (e.hwnd.load() == hwnd_for_window) {
            out.count_for_window = e.count.load();
            out.window_matched = true;
            return;
        }
        if (e.hwnd.load() == 0) {
            return;
        }
        idx = (idx + 1) % k_per_draw_hwnd_cache_size;
    }
}

void PresentMonManager::TrackEventType(PEVENT_RECORD event_record, bool is_graphics_provider) {
    (void)is_graphics_provider;

    const auto& d = event_record->EventHeader.EventDescriptor;
    const uint16_t event_id = d.Id;
    const uint16_t task = d.Task;
    const uint8_t opcode = d.Opcode;
    const uint8_t level = d.Level;
    const uint64_t keyword = d.Keyword;

    const uint64_t key = HashEventTypeKey(event_record->EventHeader.ProviderId, event_id, task, opcode);
    size_t idx = static_cast<size_t>(key % k_event_type_cache_size);

    EventTypeEntry* entry = nullptr;
    for (size_t probe = 0; probe < k_event_type_cache_size; ++probe) {
        auto& e = m_event_types[idx];
        uint64_t existing = e.key_hash.load(std::memory_order_relaxed);
        if (existing == key) {
            entry = &e;
            break;
        }
        if (existing == 0) {
            uint64_t expected = 0;
            if (e.key_hash.compare_exchange_strong(expected, key, std::memory_order_acq_rel)) {
                // Claimed
                e.event_id = event_id;
                e.task = task;
                e.opcode = opcode;
                e.level = level;
                e.keyword = keyword;

                e.provider_guid.store(
                    std::make_shared<const std::string>(ProviderGuidToString(event_record->EventHeader.ProviderId)));
                e.provider_name.store(std::make_shared<const std::string>(""));
                e.event_name.store(std::make_shared<const std::string>(""));
                e.props.store(std::make_shared<const std::string>(""));
                e.last_schema_update_ns.store(0);
                e.count.store(0);

                entry = &e;
                break;
            }
        }
        idx = (idx + 1) % k_event_type_cache_size;
    }
    if (!entry) return;

    entry->count.fetch_add(1);

    // Rate-limit schema lookup per entry (TDH calls are expensive)
    const uint64_t now_ns = static_cast<uint64_t>(utils::get_now_ns());
    const uint64_t last_ns = entry->last_schema_update_ns.load();
    const uint64_t five_sec_ns = 5000000000ULL;
    if (now_ns - last_ns < five_sec_ns) return;
    entry->last_schema_update_ns.store(now_ns);

    ULONG info_size = 0;
    ULONG st = TdhGetEventInformation(event_record, 0, nullptr, nullptr, &info_size);
    if (st != ERROR_INSUFFICIENT_BUFFER || info_size == 0) return;

    std::unique_ptr<uint8_t[]> info_buf(new (std::nothrow) uint8_t[info_size]);
    if (!info_buf) return;
    auto* info = reinterpret_cast<TRACE_EVENT_INFO*>(info_buf.get());
    st = TdhGetEventInformation(event_record, 0, nullptr, info, &info_size);
    if (st != ERROR_SUCCESS) return;

    std::wstring provider_name = GetTraceEventInfoString(info, info->ProviderNameOffset);
    std::wstring event_name = GetTraceEventInfoString(info, info->EventNameOffset);
    std::string props_csv = JoinPropNamesCSV(info, 64);

    entry->provider_name.store(std::make_shared<const std::string>(Narrow(provider_name)));
    entry->event_name.store(std::make_shared<const std::string>(Narrow(event_name)));
    entry->props.store(std::make_shared<const std::string>(props_csv));

    // Cache one sample per event type for tooltip (name=value for each field)
    std::shared_ptr<const std::string> expected_sample = entry->props_sample.load(std::memory_order_relaxed);
    if (expected_sample == nullptr) {
        std::string sample = BuildEventTypeSampleString(event_record, info, 32);
        if (!sample.empty()) {
            std::shared_ptr<const std::string> new_sample = std::make_shared<const std::string>(sample);
            if (!entry->props_sample.compare_exchange_strong(expected_sample, new_sample,
                                                            std::memory_order_acq_rel)) {
                // another thread stored first
            }
        }
    }
}

void PresentMonManager::GetEventTypeSummaries(std::vector<PresentMonEventTypeSummary>& out, bool graphics_only) const {
    out.clear();
    out.reserve(k_event_type_cache_size);

    for (size_t i = 0; i < k_event_type_cache_size; ++i) {
        const auto& e = m_event_types[i];
        uint64_t key = e.key_hash.load();
        if (key == 0) continue;

        auto guid_ptr = e.provider_guid.load();
        auto provider_name_ptr = e.provider_name.load();
        auto event_name_ptr = e.event_name.load();
        auto props_ptr = e.props.load();
        auto props_sample_ptr = e.props_sample.load();

        PresentMonEventTypeSummary s;
        s.provider_guid = guid_ptr ? *guid_ptr : "";
        s.provider_name = provider_name_ptr ? *provider_name_ptr : "";
        s.event_name = event_name_ptr ? *event_name_ptr : "";
        s.props = props_ptr ? *props_ptr : "";
        s.props_sample = props_sample_ptr ? *props_sample_ptr : "";
        s.event_id = e.event_id;
        s.task = e.task;
        s.opcode = e.opcode;
        s.level = e.level;
        s.keyword = e.keyword;
        s.count = e.count.load();

        if (graphics_only) {
            // Filter by known provider names when available; fallback to GUID match
            bool ok = false;
            if (!s.provider_name.empty()) {
                ok = StringContainsI(s.provider_name, "dxgkrnl") || StringContainsI(s.provider_name, "dxgi")
                     || StringContainsI(s.provider_name, "dwm") || StringContainsI(s.provider_name, "d3d9");
            } else {
                ok = (s.provider_guid == ProviderGuidToString(m_guid_dxgkrnl)
                      || s.provider_guid == ProviderGuidToString(m_guid_dxgi)
                      || s.provider_guid == ProviderGuidToString(m_guid_dwm)
                      || s.provider_guid == ProviderGuidToString(m_guid_d3d9));
            }
            if (!ok) continue;
        }

        out.push_back(std::move(s));
    }

    std::sort(out.begin(), out.end(), [](const PresentMonEventTypeSummary& a, const PresentMonEventTypeSummary& b) {
        return a.count > b.count;
    });
}

int PresentMonManager::PresentMonMain() {
    LogInfo("[PresentMon] ETW session starting: %ls", m_session_name);

    // Start session
    // reserve some extra bytes for logger name offsets
    ULONG props_size = sizeof(EVENT_TRACE_PROPERTIES) + 512;
    std::unique_ptr<uint8_t[]> props_buf(new (std::nothrow) uint8_t[props_size]);
    if (!props_buf) {
        UpdateDebugInfo("Running", "Failed", "Out of memory allocating ETW properties", 0, 0);
        return 1;
    }

    ZeroMemory(props_buf.get(), props_size);
    auto* props = reinterpret_cast<EVENT_TRACE_PROPERTIES*>(props_buf.get());
    props->Wnode.BufferSize = props_size;
    props->Wnode.Flags = WNODE_FLAG_TRACED_GUID;
    props->Wnode.ClientContext = 1;  // QPC
    props->LogFileMode = EVENT_TRACE_REAL_TIME_MODE;
    props->LoggerNameOffset = sizeof(EVENT_TRACE_PROPERTIES);
    // Reasonable defaults (in KB / counts). Some systems behave better with explicit values.
    props->BufferSize = 256;  // 256 KB
    props->MinimumBuffers = 64;
    props->MaximumBuffers = 256;
    props->FlushTimer = 1;  // 1 second

    TRACEHANDLE session_handle = 0;
    ULONG status = StartTraceW(&session_handle, m_session_name, props);
    if (status != ERROR_SUCCESS) {
        // If session already exists, try to reuse it instead of stopping/recreating
        if (status == ERROR_ALREADY_EXISTS) {
            LogInfo("[PresentMon] ETW session already exists, attempting to reuse: %ls", m_session_name);
            if (QueryEtwSessionByName(m_session_name, session_handle)) {
                // Successfully queried existing session handle
                LogInfo("[PresentMon] Reusing existing ETW session handle: 0x%p",
                        reinterpret_cast<void*>(session_handle));
                status = ERROR_SUCCESS;
            } else {
                // Query failed, session might be in invalid state, stop and recreate
                LogWarn("[PresentMon] Failed to query existing session, stopping and recreating: %ls", m_session_name);
                StopEtwSessionByName(m_session_name);
                // Wait a bit for the session to fully stop
                Sleep(100);
                // Try again
                status = StartTraceW(&session_handle, m_session_name, props);
            }
        }

        if (status != ERROR_SUCCESS) {
            char err[128] = {};
            StringCchPrintfA(err, std::size(err), "StartTrace failed: %lu", status);
            LogWarn("[PresentMon] %s", err);
            UpdateDebugInfo("Running", "Failed", err, 0, 0);
            return 2;
        }
    }

    m_etw_session_handle.store(static_cast<uint64_t>(session_handle));

    // Enable key providers by name (avoid hard-coded GUID tables)
    m_guid_dxgkrnl = {};
    m_guid_dxgi = {};
    m_guid_dwm = {};
    m_guid_d3d9 = {};
    m_have_dxgkrnl = ProviderGuidByName(L"Microsoft-Windows-DxgKrnl", m_guid_dxgkrnl);
    m_have_dxgi = ProviderGuidByName(L"Microsoft-Windows-DXGI", m_guid_dxgi);
    m_have_dwm = ProviderGuidByName(L"Microsoft-Windows-Dwm-Core", m_guid_dwm);
    m_have_d3d9 = ProviderGuidByName(L"Microsoft-Windows-D3D9", m_guid_d3d9);

    const bool want_dxgkrnl = settings::g_advancedTabSettings.presentmon_provider_dxgkrnl.GetValue();
    const bool want_dxgi = settings::g_advancedTabSettings.presentmon_provider_dxgi.GetValue();
    const bool want_dwm = settings::g_advancedTabSettings.presentmon_provider_dwm.GetValue();
    const bool want_d3d9 = settings::g_advancedTabSettings.presentmon_provider_d3d9.GetValue();

    const bool enable_dxgkrnl = m_have_dxgkrnl && want_dxgkrnl;
    const bool enable_dxgi = m_have_dxgi && want_dxgi;
    const bool enable_dwm = m_have_dwm && want_dwm;
    const bool enable_d3d9 = m_have_d3d9 && want_d3d9;

    if (!enable_dxgkrnl && !enable_dxgi && !enable_dwm && !enable_d3d9) {
        LogWarn("[PresentMon] No ETW providers enabled (none selected or TDH lookup failed)");
        UpdateDebugInfo("Running", "Failed", "No ETW providers enabled", 0, 0);
        RequestStopEtw();
        return 3;
    }

    auto enable_provider = [&](const GUID& guid, const wchar_t* name) {
        ENABLE_TRACE_PARAMETERS params = {};
        params.Version = ENABLE_TRACE_PARAMETERS_VERSION;
        // Prefer declared keyword bits; fallback to match-all.
        ULONGLONG keyword_any = GetProviderKeywordMaskBestEffort(guid);
        // Use VERBOSE to avoid filtering out most graphics events (many are TRACE_LEVEL_VERBOSE).
        ULONG st = EnableTraceEx2(session_handle, &guid, EVENT_CONTROL_CODE_ENABLE_PROVIDER, TRACE_LEVEL_VERBOSE,
                                  keyword_any, 0, 0, &params);
        if (st != ERROR_SUCCESS) {
            char msg[256] = {};
            StringCchPrintfA(msg, std::size(msg), "EnableTraceEx2 failed for %ls: %lu", name, st);
            m_last_error.store(std::make_shared<const std::string>(msg));
        }
        return st;
    };

    ULONG st_dxgkrnl = enable_dxgkrnl ? enable_provider(m_guid_dxgkrnl, L"Microsoft-Windows-DxgKrnl") : ERROR_NOT_FOUND;
    ULONG st_dxgi = enable_dxgi ? enable_provider(m_guid_dxgi, L"Microsoft-Windows-DXGI") : ERROR_NOT_FOUND;
    ULONG st_dwm = enable_dwm ? enable_provider(m_guid_dwm, L"Microsoft-Windows-Dwm-Core") : ERROR_NOT_FOUND;
    ULONG st_d3d9 = enable_d3d9 ? enable_provider(m_guid_d3d9, L"Microsoft-Windows-D3D9") : ERROR_NOT_FOUND;

    {
        char status_msg[256] = {};
        StringCchPrintfA(status_msg, std::size(status_msg), "ETW active (DxgKrnl=%lu, DXGI=%lu, DWM=%lu, D3D9=%lu)",
                         st_dxgkrnl, st_dxgi, st_dwm, st_d3d9);
        UpdateDebugInfo("Running", status_msg, "", 0, 0);
    }

    // Open trace
    EVENT_TRACE_LOGFILEW logfile = {};
    logfile.LoggerName = m_session_name;
    logfile.ProcessTraceMode = PROCESS_TRACE_MODE_REAL_TIME | PROCESS_TRACE_MODE_EVENT_RECORD;
    logfile.EventRecordCallback = &PresentMonManager::EtwEventRecordCallback;

    TRACEHANDLE trace_handle = OpenTraceW(&logfile);
    if (trace_handle == INVALID_PROCESSTRACE_HANDLE) {
        DWORD open_err = GetLastError();
        LogWarn("[PresentMon] OpenTrace failed, GetLastError=%lu", static_cast<unsigned long>(open_err));
        UpdateDebugInfo("Running", "Failed", "OpenTrace failed", 0, 0);
        RequestStopEtw();
        return 4;
    }
    m_etw_trace_handle.store(static_cast<uint64_t>(trace_handle));

    // Process events until session stops
    LogInfo("[PresentMon] ProcessTrace started (session active)");
    status = ProcessTrace(&trace_handle, 1, nullptr, nullptr);
    LogInfo("[PresentMon] ProcessTrace returned, status=%lu", static_cast<unsigned long>(status));

    CloseTrace(trace_handle);
    m_etw_trace_handle.store(0);
    RequestStopEtw();
    m_etw_session_handle.store(0);

    UpdateDebugInfo("Running", "Stopped", "", m_events_processed.load(), m_events_lost.load());
    return 0;
}

}  // namespace presentmon
