// Source Code <Display Commander> // follow this order for includes in all files + add this comment at the top
#include "reflex_pclstats_tab.hpp"
#include "../../../globals.hpp"
#include "../../../hooks/nvidia/pclstats_etw_hooks.hpp"
#include "../../../latency/reflex_provider.hpp"
#include "../../../settings/main_tab_settings.hpp"
#include "../../../utils/string_utils.hpp"
#include "../../ui_colors.hpp"

// Libraries <ReShade> / <imgui>
#include <imgui.h>

// Libraries <standard C++>
#include <array>
#include <cinttypes>
#include <cstddef>
#include <cwchar>
#include <cstdio>
#include <string>
#include <vector>

// Libraries <Windows.h> — before other Windows headers
#include <Windows.h>

// Libraries <Windows>
#include <evntrace.h>

namespace ui::new_ui::debug {

namespace {

const char* PclStatsMarkerSlotLabel(std::size_t i) {
    static const char* const kKnown[] = {
        "SIMULATION_START",
        "SIMULATION_END",
        "RENDERSUBMIT_START",
        "RENDERSUBMIT_END",
        "PRESENT_START",
        "PRESENT_END",
        "INPUT_SAMPLE (deprecated)",
        "TRIGGER_FLASH",
        "PC_LATENCY_PING",
        "OUT_OF_BAND_RENDERSUBMIT_START",
        "OUT_OF_BAND_RENDERSUBMIT_END",
        "OUT_OF_BAND_PRESENT_START",
        "OUT_OF_BAND_PRESENT_END",
        "CONTROLLER_INPUT_SAMPLE",
        "DELTA_T_CALCULATION",
        "LATE_WARP_PRESENT_START",
        "LATE_WARP_PRESENT_END",
        "CAMERA_CONSTRUCTED",
        "LATE_WARP_SUBMIT_START",
        "LATE_WARP_SUBMIT_END",
    };
    constexpr std::size_t kKnownCount = sizeof(kKnown) / sizeof(kKnown[0]);
    if (i < kKnownCount) {
        return kKnown[i];
    }
    if (i == kPclStatsEtwMarkerSlotCount - 1) {
        return "clamped (marker id >= slot count)";
    }
    return "reserved";
}

using NvapiLatencyFrame = ReflexProvider::NvapiLatencyFrame;

struct LatencyMilestoneRow {
    const char* label = nullptr;
    bool valid = false;
    /** (stamp_ns - sim_start_ns) / 1e6; sim_start row is 0 when valid. */
    double delta_ms_from_sim_start = 0.0;
};

struct EtwSessionSnapshot {
    bool queried = false;
    ULONG status = ERROR_GEN_FAILURE;
    ULONG total_sessions = 0;
    ULONG dc_sessions_count = 0;
    std::vector<std::string> all_sessions;
};

struct QueryEtwSessionProps {
    EVENT_TRACE_PROPERTIES props{};
    wchar_t logger_name[128]{};
    wchar_t log_file_name[2]{};
};

ULONG StopEtwSessionByNameUtf8(const std::string_view session_name_utf8) {
    if (session_name_utf8.empty()) {
        return ERROR_INVALID_PARAMETER;
    }

    const std::wstring session_name_wide = display_commander::utils::Utf8ToWide(session_name_utf8);
    if (session_name_wide.empty()) {
        return ERROR_INVALID_PARAMETER;
    }

    QueryEtwSessionProps stop_props{};
    stop_props.props.Wnode.BufferSize = sizeof(stop_props);
    stop_props.props.LoggerNameOffset = static_cast<ULONG>(offsetof(QueryEtwSessionProps, logger_name));
    stop_props.props.LogFileNameOffset = static_cast<ULONG>(offsetof(QueryEtwSessionProps, log_file_name));
    std::wcsncpy(stop_props.logger_name, session_name_wide.c_str(), _countof(stop_props.logger_name) - 1);
    return ControlTraceW(0, stop_props.logger_name, &stop_props.props, EVENT_TRACE_CONTROL_STOP);
}

EtwSessionSnapshot QueryEtwSessionsContainingDcPrefix() {
    EtwSessionSnapshot out{};

    constexpr ULONG kMaxSessions = 64;
    std::array<QueryEtwSessionProps, kMaxSessions> query_props{};
    std::array<EVENT_TRACE_PROPERTIES*, kMaxSessions> props_ptrs{};

    for (ULONG i = 0; i < kMaxSessions; ++i) {
        auto& p = query_props[i];
        p.props.Wnode.BufferSize = sizeof(QueryEtwSessionProps);
        p.props.LoggerNameOffset = static_cast<ULONG>(offsetof(QueryEtwSessionProps, logger_name));
        p.props.LogFileNameOffset = static_cast<ULONG>(offsetof(QueryEtwSessionProps, log_file_name));
        props_ptrs[i] = &p.props;
    }

    ULONG session_count = kMaxSessions;
    const ULONG status = QueryAllTracesW(props_ptrs.data(), kMaxSessions, &session_count);
    out.queried = true;
    out.status = status;
    out.total_sessions = session_count;
    if (status != ERROR_SUCCESS) {
        return out;
    }

    out.all_sessions.reserve(session_count);
    ULONG all_named_sessions = 0;
    ULONG dc_named_sessions = 0;
    for (ULONG i = 0; i < session_count; ++i) {
        const wchar_t* name = query_props[i].logger_name;
        if (name == nullptr || name[0] == L'\0') {
            continue;
        }
        ++all_named_sessions;
        if (std::wcsstr(name, L"DC_") != nullptr) {
            ++dc_named_sessions;
        }
        out.all_sessions.push_back(display_commander::utils::WideToUtf8(name));
        if (std::wcsstr(name, L"DC_") == nullptr) {
            continue;
        }
    }
    out.dc_sessions_count = dc_named_sessions;
    return out;
}

void BuildLatencyMilestonesVsSimStart(const NvapiLatencyFrame& fr, std::vector<LatencyMilestoneRow>& out_rows) {
    out_rows.clear();
    const uint64_t sim0 = fr.sim_start_time_ns;
    static const struct {
        const char* label;
        uint64_t NvapiLatencyFrame::* stamp_ns;
    } kMilestones[] = {
        {"input_sample", &NvapiLatencyFrame::input_sample_time_ns},
        {"sim_start", &NvapiLatencyFrame::sim_start_time_ns},
        {"sim_end", &NvapiLatencyFrame::sim_end_time_ns},
        {"render_submit_start", &NvapiLatencyFrame::render_submit_start_time_ns},
        {"render_submit_end", &NvapiLatencyFrame::render_submit_end_time_ns},
        {"present_start", &NvapiLatencyFrame::present_start_time_ns},
        {"present_end", &NvapiLatencyFrame::present_end_time_ns},
        {"driver_start", &NvapiLatencyFrame::driver_start_time_ns},
        {"driver_end", &NvapiLatencyFrame::driver_end_time_ns},
        {"os_render_queue_start", &NvapiLatencyFrame::os_render_queue_start_time_ns},
        {"os_render_queue_end", &NvapiLatencyFrame::os_render_queue_end_time_ns},
        {"gpu_render_start", &NvapiLatencyFrame::gpu_render_start_time_ns},
        {"gpu_render_end", &NvapiLatencyFrame::gpu_render_end_time_ns},
    };

    for (const auto& m : kMilestones) {
        const uint64_t t = fr.*(m.stamp_ns);
        LatencyMilestoneRow row{};
        row.label = m.label;
        if (sim0 == 0 || t == 0) {
            row.valid = false;
            row.delta_ms_from_sim_start = 0.0;
        } else {
            row.valid = true;
            row.delta_ms_from_sim_start = static_cast<double>(t - sim0) / 1e6;
        }
        out_rows.push_back(row);
    }
}

/** First non-zero timestamp in NVAPI pipeline order (ns); 0 if all zero. */
uint64_t FirstNonzeroPipelineStampNs(const NvapiLatencyFrame& fr) {
    const uint64_t stamps[] = {fr.input_sample_time_ns,   fr.sim_start_time_ns,
                               fr.sim_end_time_ns,        fr.render_submit_start_time_ns,
                               fr.render_submit_end_time_ns, fr.present_start_time_ns,
                               fr.present_end_time_ns,    fr.driver_start_time_ns,
                               fr.driver_end_time_ns,     fr.os_render_queue_start_time_ns,
                               fr.os_render_queue_end_time_ns, fr.gpu_render_start_time_ns,
                               fr.gpu_render_end_time_ns};
    for (const uint64_t t : stamps) {
        if (t != 0) {
            return t;
        }
    }
    return 0;
}

}  // namespace

void DrawReflexPclstatsTab(display_commander::ui::IImGuiWrapper& imgui) {
    static EtwSessionSnapshot s_etw_session_snapshot{};
    static ULONG s_last_kill_status = ERROR_SUCCESS;
    static std::string s_last_killed_session_name{};
    static bool s_last_kill_attempted = false;

    imgui.TextColored(::ui::colors::TEXT_DIMMED,
                      "Build with DEBUG_TABS / bd.ps1 -DebugTabs. PCLStats ETW counts include ping + injected markers "
                      "when \"PCL stats for injected reflex\" is on.");

    imgui.Spacing();
    imgui.Separator();
    imgui.Spacing();

    imgui.TextColored(ImVec4{0.85f, 0.85f, 0.85f, 1.0f}, "Reflex provider");
    imgui.Indent();
    if (!g_reflexProvider) {
        imgui.TextColored(::ui::colors::TEXT_DIMMED, "g_reflexProvider: (null)");
    } else {
        imgui.Text("Initialized: %s", g_reflexProvider->IsInitialized() ? "yes" : "no");
        ReflexProvider::NvapiLatencyMetrics metrics{};
        if (g_reflexProvider->GetLatencyMetrics(metrics)) {
            imgui.Text("NVAPI latency (rolling): PC %.3f ms, GPU frame %.3f ms, FrameID %" PRIu64,
                       metrics.pc_latency_ms, metrics.gpu_frame_time_ms,
                       static_cast<unsigned long long>(metrics.frame_id));
        } else {
            imgui.TextColored(::ui::colors::TEXT_DIMMED, "NVAPI GetLatency metrics: (unavailable)");
        }
    }
    imgui.Unindent();

    imgui.Spacing();
    imgui.Separator();
    imgui.Spacing();

    imgui.TextColored(ImVec4{0.85f, 0.85f, 0.85f, 1.0f}, "Injected Reflex (NVAPI path counters)");
    imgui.Indent();
    imgui.Text("Sleep calls: %" PRIu32, g_reflex_sleep_count.load());
    imgui.Text("ApplySleepMode calls: %" PRIu32, g_reflex_apply_sleep_mode_count.load());
    imgui.Text("SIMULATION_START: %" PRIu32, g_reflex_marker_simulation_start_count.load());
    imgui.Text("SIMULATION_END: %" PRIu32, g_reflex_marker_simulation_end_count.load());
    imgui.Text("RENDERSUBMIT_START: %" PRIu32, g_reflex_marker_rendersubmit_start_count.load());
    imgui.Text("RENDERSUBMIT_END: %" PRIu32, g_reflex_marker_rendersubmit_end_count.load());
    imgui.Text("PRESENT_START: %" PRIu32, g_reflex_marker_present_start_count.load());
    imgui.Text("PRESENT_END: %" PRIu32, g_reflex_marker_present_end_count.load());
    imgui.Text("INPUT_SAMPLE: %" PRIu32, g_reflex_marker_input_sample_count.load());
    imgui.Unindent();

    imgui.Spacing();
    imgui.Separator();
    imgui.Spacing();

    imgui.TextColored(ImVec4{0.85f, 0.85f, 0.85f, 1.0f}, "PCLStats provider");
    imgui.Indent();
    imgui.Text("PCLStats ETW hooks (advapi32): %s", ArePCLStatsEtwHooksInstalled() ? "installed" : "not installed");
    const char* owned_dc_session = GetPCLStatsOwnedEtwSessionName();
    if (owned_dc_session != nullptr && owned_dc_session[0] != '\0') {
        imgui.Text("Owned DC_ ETW session name: %s", owned_dc_session);
    } else {
        imgui.TextColored(::ui::colors::TEXT_DIMMED, "Owned DC_ ETW session name: (not initialized)");
    }
    const ULONG cleanup_status = GetPCLStatsDcEtwCleanupStatus();
    imgui.Text("Startup DC_ cleanup: removed %u stale session(s)", GetPCLStatsDcEtwCleanupRemovedCount());
    if (cleanup_status != ERROR_SUCCESS) {
        imgui.TextColored(::ui::colors::TEXT_DIMMED, "Startup DC_ cleanup query status: %lu", cleanup_status);
    }
    imgui.Text("Foreign PCLStats init observed: %s (skips DC PCLSTATS_INIT)",
               PclStatsForeignInitObserved() ? "yes" : "no");
    const bool pcl_user = settings::g_mainTabSettings.pcl_stats_enabled.GetValue();
    imgui.Text("Setting \"PCL stats for injected reflex\": %s", pcl_user ? "on" : "off");
    imgui.Text("PCLStats initialized: %s", ReflexProvider::IsPCLStatsInitialized() ? "yes" : "no");
    imgui.Text("PCLSTATS_INIT success count (session): %" PRIu64,
               static_cast<unsigned long long>(g_pclstats_init_success_count.load()));
    const LONGLONG last_init = g_pclstats_last_init_time_ns.load();
    if (last_init != 0) {
        imgui.Text("Last PCLSTATS_INIT time (ns since epoch): %" PRId64, static_cast<int64_t>(last_init));
    } else {
        imgui.TextColored(::ui::colors::TEXT_DIMMED, "Last PCLSTATS_INIT: (never)");
    }
    imgui.Text("g_pclstats_frame_id: %" PRIu64,
               static_cast<unsigned long long>(g_pclstats_frame_id.load(std::memory_order_relaxed)));
    imgui.Unindent();

    imgui.Spacing();
    imgui.Separator();
    imgui.Spacing();

    imgui.TextColored(ImVec4{0.85f, 0.85f, 0.85f, 1.0f}, "ETW sessions (all; DC_ highlighted)");
    imgui.Indent();
    if (!s_etw_session_snapshot.queried || imgui.Button("Refresh DC_ ETW sessions")) {
        s_etw_session_snapshot = QueryEtwSessionsContainingDcPrefix();
    }
    if (!s_etw_session_snapshot.queried) {
        imgui.TextColored(::ui::colors::TEXT_DIMMED, "Not queried yet.");
    } else if (s_etw_session_snapshot.status != ERROR_SUCCESS) {
        imgui.TextColored(::ui::colors::TEXT_DIMMED, "QueryAllTracesW failed (status=%lu).",
                          s_etw_session_snapshot.status);
    } else {
        imgui.Text("Active ETW sessions: %lu", s_etw_session_snapshot.total_sessions);
        imgui.Text("Sessions containing \"DC_\": %lu", s_etw_session_snapshot.dc_sessions_count);
        if (s_last_kill_attempted) {
            if (s_last_kill_status == ERROR_SUCCESS) {
                imgui.Text("Last kill: stopped \"%s\".", s_last_killed_session_name.c_str());
            } else {
                imgui.TextColored(::ui::colors::TEXT_DIMMED, "Last kill failed for \"%s\" (status=%lu).",
                                  s_last_killed_session_name.c_str(), s_last_kill_status);
            }
        }
        if (s_etw_session_snapshot.all_sessions.empty()) {
            imgui.TextColored(::ui::colors::TEXT_DIMMED, "No active ETW sessions reported.");
        } else if (imgui.BeginTable("dc_etw_sessions", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
            imgui.TableSetupColumn("Index");
            imgui.TableSetupColumn("DC_");
            imgui.TableSetupColumn("Session name");
            imgui.TableSetupColumn("Action");
            imgui.TableHeadersRow();
            for (std::size_t i = 0; i < s_etw_session_snapshot.all_sessions.size(); ++i) {
                const bool is_dc = s_etw_session_snapshot.all_sessions[i].find("DC_") != std::string::npos;
                imgui.TableNextRow();
                imgui.TableNextColumn();
                imgui.Text("%zu", i);
                imgui.TableNextColumn();
                imgui.TextUnformatted(is_dc ? "yes" : "no");
                imgui.TableNextColumn();
                imgui.TextUnformatted(s_etw_session_snapshot.all_sessions[i].c_str());
                imgui.TableNextColumn();
                imgui.PushID(static_cast<int>(i));
                if (imgui.Button("Kill")) {
                    s_last_killed_session_name = s_etw_session_snapshot.all_sessions[i];
                    s_last_kill_status = StopEtwSessionByNameUtf8(s_last_killed_session_name);
                    s_last_kill_attempted = true;
                    s_etw_session_snapshot = QueryEtwSessionsContainingDcPrefix();
                }
                imgui.PopID();
            }
            imgui.EndTable();
        }
    }
    imgui.Unindent();

    imgui.Spacing();
    imgui.Separator();
    imgui.Spacing();

    imgui.TextColored(ImVec4{0.85f, 0.85f, 0.85f, 1.0f}, "PCLStats ETW (PCLStatsEvent) emit counts");
    imgui.Indent();
    const uint64_t etw_total = g_pclstats_etw_total_count.load(std::memory_order_relaxed);
    imgui.Text("Total emits: %" PRIu64, static_cast<unsigned long long>(etw_total));
    if (etw_total == 0) {
        imgui.TextColored(::ui::colors::TEXT_DIMMED, "No ETW marker emits recorded yet.");
    } else if (imgui.BeginTable("pclstats_etw_counts", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
        imgui.TableSetupColumn("Marker id");
        imgui.TableSetupColumn("Name");
        imgui.TableSetupColumn("Count");
        imgui.TableHeadersRow();
        for (std::size_t i = 0; i < kPclStatsEtwMarkerSlotCount; ++i) {
            const uint64_t c = g_pclstats_etw_by_marker[i].load(std::memory_order_relaxed);
            if (c == 0) {
                continue;
            }
            imgui.TableNextRow();
            imgui.TableNextColumn();
            imgui.Text("%zu", i);
            imgui.TableNextColumn();
            imgui.TextUnformatted(PclStatsMarkerSlotLabel(i));
            imgui.TableNextColumn();
            imgui.Text("%" PRIu64, static_cast<unsigned long long>(c));
        }
        imgui.EndTable();
    }
    imgui.Unindent();

    imgui.Spacing();
    imgui.Separator();
    imgui.Spacing();

    imgui.TextColored(ImVec4{0.85f, 0.85f, 0.85f, 1.0f}, "Recent NVAPI frame reports (up to 10)");
    imgui.Indent();
    static std::vector<ReflexProvider::NvapiLatencyFrame> s_frames;
    static int s_breakdown_frame_idx = 0;
    if (imgui.Button("Refresh latency frames") && g_reflexProvider) {
        (void)g_reflexProvider->GetRecentLatencyFrames(s_frames, 10);
        if (s_frames.empty()) {
            s_breakdown_frame_idx = 0;
        } else if (s_breakdown_frame_idx >= static_cast<int>(s_frames.size())) {
            s_breakdown_frame_idx = 0;
        }
    }
    if (g_reflexProvider && !s_frames.empty()) {
        if (s_breakdown_frame_idx < 0 || s_breakdown_frame_idx >= static_cast<int>(s_frames.size())) {
            s_breakdown_frame_idx = 0;
        }
        if (imgui.BeginTable("reflex_recent_frames", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
            imgui.TableSetupColumn("FrameID");
            imgui.TableSetupColumn("Sim start (ns)");
            imgui.TableSetupColumn("Present end (ns)");
            imgui.TableSetupColumn("GPU end (ns)");
            imgui.TableHeadersRow();
            for (const auto& fr : s_frames) {
                imgui.TableNextRow();
                imgui.TableNextColumn();
                imgui.Text("%" PRIu64, static_cast<unsigned long long>(fr.frame_id));
                imgui.TableNextColumn();
                imgui.Text("%" PRIu64, static_cast<unsigned long long>(fr.sim_start_time_ns));
                imgui.TableNextColumn();
                imgui.Text("%" PRIu64, static_cast<unsigned long long>(fr.present_end_time_ns));
                imgui.TableNextColumn();
                imgui.Text("%" PRIu64, static_cast<unsigned long long>(fr.gpu_render_end_time_ns));
            }
            imgui.EndTable();
        }

        imgui.Spacing();
        const int kBreakdownHeaderOpen = static_cast<int>(ImGuiTreeNodeFlags_DefaultOpen);
        if (imgui.CollapsingHeader("GetLatency segment breakdown (NvAPI_D3D_GetLatency)", kBreakdownHeaderOpen)) {
            imgui.TextColored(::ui::colors::TEXT_DIMMED,
                              "Same data as Refresh above: ReflexManager::GetLatency -> NvAPI_D3D_GetLatency_Direct. "
                              "One column: each stamp minus sim_start (ms). N/A if sim_start or that stamp is zero.");
#if defined(_M_AMD64) || defined(__x86_64__)
            std::vector<LatencyMilestoneRow> milestone_rows;
            const NvapiLatencyFrame& sel = s_frames[static_cast<std::size_t>(s_breakdown_frame_idx)];
            BuildLatencyMilestonesVsSimStart(sel, milestone_rows);

            char preview[112];
            std::snprintf(preview, sizeof(preview), "FrameID %" PRIu64,
                          static_cast<unsigned long long>(sel.frame_id));
            imgui.PushID("reflex_latency_br");
            if (imgui.BeginCombo("Frame", preview, 0)) {
                for (int i = 0; i < static_cast<int>(s_frames.size()); ++i) {
                    char lbl[112];
                    std::snprintf(lbl, sizeof(lbl), "FrameID %" PRIu64 " [%d]",
                                  static_cast<unsigned long long>(s_frames[static_cast<std::size_t>(i)].frame_id), i);
                    const bool selected = (i == s_breakdown_frame_idx);
                    if (imgui.Selectable(lbl, selected)) {
                        s_breakdown_frame_idx = i;
                    }
                    if (selected) {
                        imgui.SetItemDefaultFocus();
                    }
                }
                imgui.EndCombo();
            }
            imgui.PopID();

            if (imgui.BeginTable("reflex_latency_segments", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
                imgui.TableSetupColumn("Stamp");
                imgui.TableSetupColumn("Delta ms (vs sim_start)");
                imgui.TableHeadersRow();
                for (const auto& r : milestone_rows) {
                    imgui.TableNextRow();
                    imgui.TableNextColumn();
                    imgui.TextUnformatted(r.label);
                    imgui.TableNextColumn();
                    if (r.valid) {
                        imgui.Text("%.4f", r.delta_ms_from_sim_start);
                    } else {
                        imgui.TextColored(::ui::colors::TEXT_DIMMED, "N/A");
                    }
                }
                imgui.EndTable();
            }

            imgui.Text("gpuFrameTimeUs (NVAPI): %" PRIu32, sel.gpu_frame_time_us);
            if (imgui.IsItemHovered()) {
                imgui.SetTooltipEx("Difference between previous and current frame gpuRenderEndTime (microseconds, per "
                                   "NVAPI).");
            }
            imgui.Text("gpuActiveRenderTimeUs (NVAPI): %" PRIu32, sel.gpu_active_render_time_us);
            if (imgui.IsItemHovered()) {
                imgui.SetTooltipEx("Active GPU time between gpu render start and end, excluding idle in between "
                                   "(microseconds, per NVAPI).");
            }

            const uint64_t first_ns = FirstNonzeroPipelineStampNs(sel);
            const uint64_t gpu_end = sel.gpu_render_end_time_ns;
            if (first_ns != 0 && gpu_end != 0 && gpu_end >= first_ns) {
                const double span_ms = static_cast<double>(gpu_end - first_ns) / 1e6;
                imgui.Text("Span (first non-zero pipeline stamp to gpu_render_end): %.4f ms", span_ms);
            }
#else
            imgui.TextColored(::ui::colors::TEXT_DIMMED,
                              "Segment breakdown not available on 32-bit builds.");
#endif
        }
    } else {
        imgui.TextColored(::ui::colors::TEXT_DIMMED, "Press Refresh (requires initialized Reflex provider).");
    }
    imgui.Unindent();
}

}  // namespace ui::new_ui::debug
