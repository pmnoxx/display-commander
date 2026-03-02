// Source Code <Display Commander>

// Group 1 — Source Code (Display Commander)
#include "games_tab.hpp"
#include "../../res/ui_colors.hpp"
#include "../../utils/process_window_enumerator.hpp"
#include "../../utils/timing.hpp"
#include "../imgui_wrapper_base.hpp"

// Group 2 — ReShade / ImGui
#include <reshade_imgui.hpp>

// Group 3 — Standard C++
#include <string>
#include <thread>
#include <vector>

// Group 4 — Windows.h
#include <windows.h>

// Group 5 — Other Windows SDK
// (none)

namespace ui::new_ui {

namespace {

struct GamesTabState {
    std::vector<display_commander::utils::RunningGameInfo> games;
    bool has_refresh_once = false;
    bool show_kill_modal = false;
    DWORD pending_kill_pid = 0;
    std::wstring pending_kill_title;
    LONGLONG last_refresh_ns = 0;
};

GamesTabState& GetState() {
    static GamesTabState s_state;
    return s_state;
}

void RefreshGamesList() {
    auto& state = GetState();
    display_commander::utils::GetRunningGamesCache(state.games);
    state.has_refresh_once = true;
    state.last_refresh_ns = utils::get_now_ns();
}

void RequestGamesListRefresh() {
    display_commander::utils::RequestRunningGamesRefresh();
}

void DrawGamesTable(display_commander::ui::IImGuiWrapper& imgui) {
    auto& state = GetState();

    // Read from cache only (mutex discovery runs on monitoring thread). Re-read every 1s when tab visible.
    if (!state.has_refresh_once) {
        RefreshGamesList();
    } else {
        const LONGLONG now_ns = utils::get_now_ns();
        if (state.last_refresh_ns == 0
            || (now_ns - state.last_refresh_ns) >= utils::SEC_TO_NS) {
            RefreshGamesList();
        }
    }

    if (imgui.Button("Refresh")) {
        RequestGamesListRefresh();
    }
    if (imgui.IsItemHovered()) {
        imgui.SetTooltip("Refresh the list of games that currently have Display Commander loaded.");
    }

    imgui.SameLine();
    imgui.TextColored(::ui::colors::TEXT_DIMMED, "(Session-wide, based on Display Commander mutex)");

    imgui.Spacing();

    if (state.games.empty()) {
        imgui.TextColored(::ui::colors::TEXT_DIMMED,
                          "No running games with Display Commander detected in this Windows session.");
        return;
    }

    if (imgui.BeginTable("##dc_games_tab_table",
                         5,
                         display_commander::ui::wrapper_flags::TableFlags_RowBg
                             | display_commander::ui::wrapper_flags::TableFlags_Borders
                             | display_commander::ui::wrapper_flags::TableFlags_SizingStretchProp,
                         ImVec2{0.0f, 0.0f})) {
        imgui.TableSetupColumn("PID", display_commander::ui::wrapper_flags::TableColumnFlags_WidthFixed, 80.0f);
        imgui.TableSetupColumn("Title / Executable");
        imgui.TableSetupColumn("Focus", display_commander::ui::wrapper_flags::TableColumnFlags_WidthFixed, 80.0f);
        imgui.TableSetupColumn("Minimize", display_commander::ui::wrapper_flags::TableColumnFlags_WidthFixed, 80.0f);
        imgui.TableSetupColumn("Kill", display_commander::ui::wrapper_flags::TableColumnFlags_WidthFixed, 80.0f);
        imgui.TableHeadersRow();

        const DWORD current_pid = GetCurrentProcessId();

        for (const auto& game : state.games) {
            imgui.TableNextRow();

            // PID
            imgui.TableSetColumnIndex(0);
            imgui.Text("%lu", static_cast<unsigned long>(game.pid));

            // Title
            imgui.TableSetColumnIndex(1);
            // Display UTF-16 strings directly via ImGui (ReShade wrapper expects UTF-8, but titles are simple).
            // For now, print a narrow placeholder when conversion helpers are unavailable.
            imgui.Text("%ls", game.display_title.c_str());
            if (!game.exe_path.empty() && imgui.IsItemHovered()) {
                imgui.SetTooltip("%ls", game.exe_path.c_str());
            }

            // Focus button
            imgui.TableSetColumnIndex(2);
            bool can_focus = game.main_window != nullptr;
            if (!can_focus) {
                imgui.BeginDisabled();
            }
            {
                char label[32];
                snprintf(label, sizeof(label), "Focus##%lu", static_cast<unsigned long>(game.pid));
                if (imgui.SmallButton(label) && can_focus) {
                    HWND hwnd = game.main_window;
                    if (hwnd != nullptr) {
                        std::thread([hwnd]() {
                            if (IsIconic(hwnd) != FALSE) {
                                ShowWindow(hwnd, SW_RESTORE);
                            }
                            SetForegroundWindow(hwnd);
                        }).detach();
                    }
                }
            }
            if (!can_focus) {
                imgui.EndDisabled();
            }
            if (imgui.IsItemHovered()) {
                if (can_focus) {
                    imgui.SetTooltip("Bring this game's main window to the foreground.");
                } else {
                    imgui.SetTooltip("No main window detected for this game.");
                }
            }

            // Minimize button
            imgui.TableSetColumnIndex(3);
            bool can_minimize = game.main_window != nullptr;
            if (!can_minimize) {
                imgui.BeginDisabled();
            }
            {
                char min_label[32];
                snprintf(min_label, sizeof(min_label), "Minimize##%lu", static_cast<unsigned long>(game.pid));
                if (imgui.SmallButton(min_label) && can_minimize) {
                    HWND hwnd = game.main_window;
                    if (hwnd != nullptr) {
                        std::thread([hwnd]() { ShowWindow(hwnd, SW_MINIMIZE); }).detach();
                    }
                }
            }
            if (!can_minimize) {
                imgui.EndDisabled();
            }
            if (imgui.IsItemHovered()) {
                if (can_minimize) {
                    imgui.SetTooltip("Minimize this game's main window to the taskbar.");
                } else {
                    imgui.SetTooltip("No main window detected for this game.");
                }
            }

            // Kill button
            imgui.TableSetColumnIndex(4);
            bool can_kill_here = game.can_terminate && game.pid != current_pid;
            if (!can_kill_here) {
                imgui.BeginDisabled();
            }
            char kill_label[32];
            snprintf(kill_label, sizeof(kill_label), "Kill##%lu", static_cast<unsigned long>(game.pid));
            if (imgui.SmallButton(kill_label) && can_kill_here) {
                state.pending_kill_pid = game.pid;
                state.pending_kill_title = game.display_title;
                state.show_kill_modal = true;
            }
            if (!can_kill_here) {
                imgui.EndDisabled();
            }
            if (imgui.IsItemHovered()) {
                if (game.pid == current_pid) {
                    imgui.SetTooltip("Cannot terminate the current Display Commander process.");
                } else if (!game.can_terminate) {
                    imgui.SetTooltip("Insufficient permissions to terminate this process.");
                } else {
                    imgui.SetTooltip("Terminate this game process.");
                }
            }
        }

        imgui.EndTable();
    }
}

void DrawKillConfirmationModal(display_commander::ui::IImGuiWrapper& imgui) {
    auto& state = GetState();
    if (state.pending_kill_pid == 0) {
        return;
    }
    // Open popup once when Kill was first clicked; keep drawing until user confirms or cancels
    if (state.show_kill_modal) {
        imgui.OpenPopup("Confirm Game Termination");
        state.show_kill_modal = false;
    }

    bool open = true;
    if (imgui.BeginPopupModal("Confirm Game Termination", &open, ImGuiWindowFlags_AlwaysAutoResize)) {
        imgui.TextColored(::ui::colors::TEXT_WARNING,
                          "Terminate game process PID %lu?",
                          static_cast<unsigned long>(state.pending_kill_pid));
        imgui.TextWrapped("%ls",
                          state.pending_kill_title.empty() ? L"(no title)" : state.pending_kill_title.c_str());
        imgui.Spacing();
        imgui.TextColored(::ui::colors::TEXT_WARNING,
                          "This will close the game immediately without saving progress.");

        imgui.Spacing();

        bool do_kill = false;
        if (imgui.Button("Yes, terminate", ImVec2(140.0f, 0.0f))) {
            do_kill = true;
        }
        imgui.SameLine();
        if (imgui.Button("Cancel", ImVec2(100.0f, 0.0f))) {
            open = false;
        }

        if (do_kill) {
            DWORD pid_to_kill = state.pending_kill_pid;
            state.pending_kill_pid = 0;
            state.pending_kill_title.clear();
            state.show_kill_modal = false;
            RequestGamesListRefresh();
            open = false;

            std::thread([pid_to_kill]() {
                HANDLE h_process = OpenProcess(PROCESS_TERMINATE, FALSE, pid_to_kill);
                if (h_process != nullptr) {
                    (void)TerminateProcess(h_process, 0);
                    CloseHandle(h_process);
                }
            }).detach();
        }

        if (!open) {
            state.show_kill_modal = false;
            if (state.pending_kill_pid != 0 && !do_kill) {
                state.pending_kill_pid = 0;
                state.pending_kill_title.clear();
            }
        }

        imgui.EndPopup();
    }
}

}  // namespace

void DrawGamesTab(display_commander::ui::IImGuiWrapper& imgui) {
    imgui.Indent();

    DrawGamesTable(imgui);
    DrawKillConfirmationModal(imgui);

    imgui.Unindent();
}

}  // namespace ui::new_ui

