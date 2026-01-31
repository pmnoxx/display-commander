#include "presentmon_manager.hpp"
#include "../globals.hpp"
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

// Returns true if a process with the given PID exists.
bool IsProcessRunning(DWORD pid) {
    if (pid == 0) return false;
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (h != nullptr) {
        CloseHandle(h);
        return true;
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

DxgiBypassMode MapPresentModeStringToFlip(const std::string& s) {
    if (StringContainsI(s, "overlay") || StringContainsI(s, "mpo")) {
        return DxgiBypassMode::kOverlay;
    }
    if (StringContainsI(s, "independent")) {
        return DxgiBypassMode::kIndependentFlip;
    }
    if (StringContainsI(s, "composed")) {
        return DxgiBypassMode::kComposed;
    }
    return DxgiBypassMode::kUnknown;
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
        StringCchPrintfA(buf, std::size(buf), "%llu", static_cast<unsigned long long>(v));
        return std::string(buf);
    }
    return {};
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

PresentMonManager::PresentMonManager()
    : m_running(false),
      m_should_stop(false),
      m_flip_mode(DxgiBypassMode::kUnset),
      m_flip_state_valid(false),
      m_flip_state_update_time(0),
      m_present_mode_str(new std::string("Unknown")),
      m_debug_info_str(new std::string("")),
      m_thread_started(false),
      m_etw_session_active(false),
      m_thread_status(new std::string("Not started")),
      m_etw_session_status(new std::string("Not initialized")),
      m_last_error(new std::string("")),
      m_events_processed(0),
      m_events_processed_for_current_pid(0),
      m_events_lost(0),
      m_last_event_time(0),
      m_last_event_pid(0),
      m_last_provider(new std::string("")),
      m_last_event_id(0),
      m_last_present_mode_value(new std::string("")),
      m_last_provider_name(new std::string("")),
      m_last_event_name(new std::string("")),
      m_last_interesting_props(new std::string("")),
      m_last_schema_update_time_ns(0),
      m_events_dxgkrnl(0),
      m_events_dxgi(0),
      m_events_dwm(0),
      m_last_graphics_provider(new std::string("")),
      m_last_graphics_event_id(0),
      m_last_graphics_event_pid(0),
      m_last_graphics_provider_name(new std::string("")),
      m_last_graphics_event_name(new std::string("")),
      m_last_graphics_props(new std::string("")),
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
    m_have_dxgkrnl = false;
    m_have_dxgi = false;
    m_have_dwm = false;
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
    StopWorker();

    // Double-check: if session name exists but handle is lost, try to stop by name
    // This handles edge cases where the destructor runs but StopWorker didn't fully clean up
    if (m_session_name[0] != 0) {
        uint64_t sh = m_etw_session_handle.load();
        if (sh == 0) {
            // Handle was lost, try to stop by name as last resort
            StopEtwSessionByName(m_session_name);
        }
    }

    // Clean up string pointers
    delete m_present_mode_str.load();
    delete m_debug_info_str.load();
    delete m_thread_status.load();
    delete m_etw_session_status.load();
    delete m_last_error.load();
    delete m_last_provider.load();
    delete m_last_present_mode_value.load();
    delete m_last_provider_name.load();
    delete m_last_event_name.load();
    delete m_last_interesting_props.load();
    delete m_last_graphics_provider.load();
    delete m_last_graphics_provider_name.load();
    delete m_last_graphics_event_name.load();
    delete m_last_graphics_props.load();
}

void PresentMonManager::StartWorker() {
    if (m_running.load()) {
        LogInfo("PresentMon: Worker thread already running");
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
    std::string* status = new std::string("Starting...");
    delete m_thread_status.exchange(status);

    // Close any orphaned DC_ ETW sessions (from previous crashed/exited instances) before starting ours
    CloseOrphanedDcEtwSessions();

    // Precompute session name (unique per process)
    DWORD pid = GetCurrentProcessId();
    StringCchPrintfW(m_session_name, std::size(m_session_name), L"DC_PresentMon_%lu", static_cast<unsigned long>(pid));

    // Start worker thread
    m_worker_thread = std::thread(&PresentMonManager::WorkerThread, this);

    // Start cleanup thread: every 10s close DC_ sessions whose process no longer exists
    m_cleanup_thread = std::thread(&PresentMonManager::CleanupThread, this);

    LogInfo("PresentMon: Worker thread started");
}

void PresentMonManager::StopWorker() {
    if (!m_running.load()) {
        return;
    }

    LogInfo("PresentMon: Stopping worker thread...");

    m_should_stop.store(true);

    // Stop ETW session to unblock ProcessTrace
    RequestStopEtw();

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
    std::string* status = new std::string("Stopped");
    delete m_thread_status.exchange(status);

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
    if (mode_str_ptr) {
        flip_state.present_mode_str = *mode_str_ptr;
    } else {
        flip_state.present_mode_str = "Unknown";
    }

    auto debug_str_ptr = m_debug_info_str.load();
    if (debug_str_ptr) {
        flip_state.debug_info = *debug_str_ptr;
    } else {
        flip_state.debug_info = "";
    }

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

    // Include session name
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

    // Enumerate ETW sessions starting with "DC_"
    GetEtwSessionsWithPrefix(L"DC_", debug_info.dc_etw_sessions);
}

void PresentMonManager::UpdateFlipState(DxgiBypassMode mode, const std::string& present_mode_str,
                                        const std::string& debug_info) {
    m_flip_mode.store(mode);
    m_flip_state_valid.store(true);
    m_flip_state_update_time.store(utils::get_now_ns());

    std::string* new_mode_str = new std::string(present_mode_str);
    delete m_present_mode_str.exchange(new_mode_str);

    std::string* new_debug_str = new std::string(debug_info);
    delete m_debug_info_str.exchange(new_debug_str);
}

void PresentMonManager::UpdateDebugInfo(const std::string& thread_status, const std::string& etw_status,
                                        const std::string& error, uint64_t events_processed, uint64_t events_lost) {
    std::string* new_thread_status = new std::string(thread_status);
    delete m_thread_status.exchange(new_thread_status);

    std::string* new_etw_status = new std::string(etw_status);
    delete m_etw_session_status.exchange(new_etw_status);

    if (!error.empty()) {
        std::string* new_error = new std::string(error);
        delete m_last_error.exchange(new_error);
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

    LogInfo("[PresentMon] Worker thread exiting with code %d", result);

    // Update thread status
    manager->UpdateDebugInfo("Exited", "Stopped", "", manager->m_events_processed.load(),
                             manager->m_events_lost.load());

    manager->m_running.store(false);
}

void PresentMonManager::RequestStopEtw() {
    uint64_t sh = m_etw_session_handle.load();
    if (m_session_name[0] == 0) return;

    // If we have a handle, use it; otherwise try to stop by name
    if (sh != 0) {
        // Stop session using handle
        EVENT_TRACE_PROPERTIES props = {};
        props.Wnode.BufferSize = sizeof(EVENT_TRACE_PROPERTIES);
        ULONG status = ControlTraceW(static_cast<TRACEHANDLE>(sh), m_session_name, &props, EVENT_TRACE_CONTROL_STOP);
        if (status == ERROR_SUCCESS || status == ERROR_WMI_INSTANCE_NOT_FOUND) {
            // Successfully stopped or already stopped
            m_etw_session_handle.store(0);
        }
    } else {
        // No handle available, try to stop by name (fallback for cleanup)
        StopEtwSessionByName(m_session_name);
    }
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

void PresentMonManager::GetEtwSessionsWithPrefix(const wchar_t* prefix, std::vector<std::string>& out_session_names) {
    out_session_names.clear();
    if (prefix == nullptr || prefix[0] == 0) return;

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
            // Out of memory, return what we have
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
        // Failed to query sessions (may not have permissions)
        return;
    }

    // Filter sessions by prefix
    const size_t prefix_len = wcslen(prefix);
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

void PresentMonManager::CloseOrphanedDcEtwSessions() {
    std::vector<std::string> sessions;
    GetEtwSessionsWithPrefix(L"DC_", sessions);

    for (const std::string& name : sessions) {
        // Session names are e.g. DC_PresentMon_12345; PID is the number after the last '_'
        size_t last_underscore = name.find_last_of('_');
        if (last_underscore == std::string::npos || last_underscore + 1 >= name.size()) {
            continue;
        }
        const std::string suffix = name.substr(last_underscore + 1);
        char* end = nullptr;
        const unsigned long pid = std::strtoul(suffix.c_str(), &end, 10);
        if (end == nullptr || end == suffix.c_str() || *end != '\0') {
            continue;  // Not a valid PID
        }
        if (pid == 0) {
            continue;  // PID 0 is invalid
        }
        if (IsProcessRunning(static_cast<DWORD>(pid))) {
            continue;  // Process still exists, keep session
        }
        std::wstring wide_name = Widen(name);
        if (!wide_name.empty()) {
            StopEtwSessionByName(wide_name.c_str());
            LogInfo("PresentMon: Stopped orphan ETW session %s (process %lu no longer exists)", name.c_str(),
                    static_cast<unsigned long>(pid));
        }
    }
}

void PresentMonManager::CleanupThread(PresentMonManager* manager) {
    while (!manager->m_should_stop.load()) {
        for (int i = 0; i < 10; ++i) {
            if (manager->m_should_stop.load()) {
                return;
            }
            Sleep(1000);
        }
        CloseOrphanedDcEtwSessions();
    }
}

void WINAPI PresentMonManager::EtwEventRecordCallback(PEVENT_RECORD event_record) {
    if (event_record == nullptr) return;
    // Route via TLS if possible; otherwise use global instance
    PresentMonManager* mgr = (t_active_manager != nullptr) ? t_active_manager : &g_presentMonManager;
    mgr->OnEtwEvent(event_record);
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
    {
        std::string* p = new std::string(ProviderGuidToString(event_record->EventHeader.ProviderId));
        delete m_last_provider.exchange(p);
        m_last_event_id.store(event_record->EventHeader.EventDescriptor.Id);
    }

    // Track graphics-relevant providers separately (DxgKrnl/DXGI/DWM)
    const bool is_dxgkrnl = m_have_dxgkrnl && IsEqualGUID(event_record->EventHeader.ProviderId, m_guid_dxgkrnl);
    const bool is_dxgi = m_have_dxgi && IsEqualGUID(event_record->EventHeader.ProviderId, m_guid_dxgi);
    const bool is_dwm = m_have_dwm && IsEqualGUID(event_record->EventHeader.ProviderId, m_guid_dwm);
    const bool is_graphics_provider = (is_dxgkrnl || is_dxgi || is_dwm);

    if (is_dxgkrnl) m_events_dxgkrnl.fetch_add(1);
    if (is_dxgi) m_events_dxgi.fetch_add(1);
    if (is_dwm) m_events_dwm.fetch_add(1);

    if (is_graphics_provider) {
        std::string* p = new std::string(ProviderGuidToString(event_record->EventHeader.ProviderId));
        delete m_last_graphics_provider.exchange(p);
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
                            delete m_last_graphics_provider_name.exchange(new std::string(Narrow(provider_name)));
                            delete m_last_graphics_event_name.exchange(new std::string(Narrow(event_name)));
                        } else {
                            delete m_last_provider_name.exchange(new std::string(Narrow(provider_name)));
                            delete m_last_event_name.exchange(new std::string(Narrow(event_name)));
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
                            delete m_last_graphics_props.exchange(new std::string(summary));
                        } else {
                            delete m_last_interesting_props.exchange(new std::string(summary));
                        }

                        // Try infer from common numeric/bool fields if present
                        uint64_t u = 0;
                        if (TryGetEventPropertyU64(event_record, L"IndependentFlip", u)
                            || TryGetEventPropertyU64(event_record, L"IsIndependentFlip", u)) {
                            if (u != 0)
                                UpdateFlipState(DxgiBypassMode::kIndependentFlip, "IndependentFlip=1",
                                                "ETW bool field");
                        }
                        if (TryGetEventPropertyU64(event_record, L"Overlay", u)
                            || TryGetEventPropertyU64(event_record, L"IsOverlay", u)) {
                            if (u != 0) UpdateFlipState(DxgiBypassMode::kOverlay, "Overlay=1", "ETW bool field");
                        }
                        if (TryGetEventPropertyU64(event_record, L"Composed", u)
                            || TryGetEventPropertyU64(event_record, L"IsComposed", u)) {
                            if (u != 0) UpdateFlipState(DxgiBypassMode::kComposed, "Composed=1", "ETW bool field");
                        }

                        // PresentMode numeric mapping (best-effort)
                        if (TryGetEventPropertyU64(event_record, L"PresentMode", u)) {
                            char buf[64] = {};
                            StringCchPrintfA(buf, std::size(buf), "PresentMode=%llu",
                                             static_cast<unsigned long long>(u));
                            delete m_last_present_mode_value.exchange(new std::string(buf));
                            if (u == 0)
                                UpdateFlipState(DxgiBypassMode::kComposed, buf, "ETW PresentMode numeric");
                            else if (u == 1)
                                UpdateFlipState(DxgiBypassMode::kOverlay, buf, "ETW PresentMode numeric");
                            else if (u == 2)
                                UpdateFlipState(DxgiBypassMode::kIndependentFlip, buf, "ETW PresentMode numeric");
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
        {
            std::string* v = new std::string(present_mode);
            delete m_last_present_mode_value.exchange(v);
        }

        DxgiBypassMode mode = MapPresentModeStringToFlip(present_mode);
        if (mode != DxgiBypassMode::kUnknown) {
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

                e.provider_guid.store(new std::string(ProviderGuidToString(event_record->EventHeader.ProviderId)));
                e.provider_name.store(new std::string(""));
                e.event_name.store(new std::string(""));
                e.props.store(new std::string(""));
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

    delete entry->provider_name.exchange(new std::string(Narrow(provider_name)));
    delete entry->event_name.exchange(new std::string(Narrow(event_name)));
    delete entry->props.exchange(new std::string(props_csv));
}

void PresentMonManager::GetEventTypeSummaries(std::vector<PresentMonEventTypeSummary>& out, bool graphics_only) const {
    out.clear();
    out.reserve(k_event_type_cache_size);

    for (size_t i = 0; i < k_event_type_cache_size; ++i) {
        const auto& e = m_event_types[i];
        uint64_t key = e.key_hash.load();
        if (key == 0) continue;

        auto* guid_ptr = e.provider_guid.load();
        auto* provider_name_ptr = e.provider_name.load();
        auto* event_name_ptr = e.event_name.load();
        auto* props_ptr = e.props.load();

        PresentMonEventTypeSummary s;
        s.provider_guid = guid_ptr ? *guid_ptr : "";
        s.provider_name = provider_name_ptr ? *provider_name_ptr : "";
        s.event_name = event_name_ptr ? *event_name_ptr : "";
        s.props = props_ptr ? *props_ptr : "";
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
                     || StringContainsI(s.provider_name, "dwm");
            } else {
                ok = (s.provider_guid == ProviderGuidToString(m_guid_dxgkrnl)
                      || s.provider_guid == ProviderGuidToString(m_guid_dxgi)
                      || s.provider_guid == ProviderGuidToString(m_guid_dwm));
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
            UpdateDebugInfo("Running", "Failed", err, 0, 0);
            return 2;
        }
    }

    m_etw_session_handle.store(static_cast<uint64_t>(session_handle));

    // Enable key providers by name (avoid hard-coded GUID tables)
    m_guid_dxgkrnl = {};
    m_guid_dxgi = {};
    m_guid_dwm = {};
    m_have_dxgkrnl = ProviderGuidByName(L"Microsoft-Windows-DxgKrnl", m_guid_dxgkrnl);
    m_have_dxgi = ProviderGuidByName(L"Microsoft-Windows-DXGI", m_guid_dxgi);
    m_have_dwm = ProviderGuidByName(L"Microsoft-Windows-Dwm-Core", m_guid_dwm);

    if (!m_have_dxgkrnl && !m_have_dxgi && !m_have_dwm) {
        UpdateDebugInfo("Running", "Failed", "Could not locate ETW providers via TDH", 0, 0);
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
            std::string* e = new std::string(msg);
            delete m_last_error.exchange(e);
        }
        return st;
    };

    ULONG st_dxgkrnl = m_have_dxgkrnl ? enable_provider(m_guid_dxgkrnl, L"Microsoft-Windows-DxgKrnl") : ERROR_NOT_FOUND;
    ULONG st_dxgi = m_have_dxgi ? enable_provider(m_guid_dxgi, L"Microsoft-Windows-DXGI") : ERROR_NOT_FOUND;
    ULONG st_dwm = m_have_dwm ? enable_provider(m_guid_dwm, L"Microsoft-Windows-Dwm-Core") : ERROR_NOT_FOUND;

    {
        char status_msg[256] = {};
        StringCchPrintfA(status_msg, std::size(status_msg), "ETW active (DxgKrnl=%lu, DXGI=%lu, DWM=%lu)", st_dxgkrnl,
                         st_dxgi, st_dwm);
        UpdateDebugInfo("Running", status_msg, "", 0, 0);
    }

    // Open trace
    EVENT_TRACE_LOGFILEW logfile = {};
    logfile.LoggerName = m_session_name;
    logfile.ProcessTraceMode = PROCESS_TRACE_MODE_REAL_TIME | PROCESS_TRACE_MODE_EVENT_RECORD;
    logfile.EventRecordCallback = &PresentMonManager::EtwEventRecordCallback;

    TRACEHANDLE trace_handle = OpenTraceW(&logfile);
    if (trace_handle == INVALID_PROCESSTRACE_HANDLE) {
        UpdateDebugInfo("Running", "Failed", "OpenTrace failed", 0, 0);
        RequestStopEtw();
        return 4;
    }
    m_etw_trace_handle.store(static_cast<uint64_t>(trace_handle));

    // Process events until session stops
    status = ProcessTrace(&trace_handle, 1, nullptr, nullptr);
    (void)status;

    CloseTrace(trace_handle);
    m_etw_trace_handle.store(0);
    RequestStopEtw();
    m_etw_session_handle.store(0);

    UpdateDebugInfo("Running", "Stopped", "", m_events_processed.load(), m_events_lost.load());
    return 0;
}

}  // namespace presentmon
