// Source Code <Display Commander>

// Group 1 — Source Code (Display Commander)
#include "games_tab.hpp"
#include "../../res/ui_colors.hpp"
#include "../../utils/process_window_enumerator.hpp"
#include "../../utils/standalone_launcher.hpp"
#include "../../utils/steam_favorites.hpp"
#include "../../utils/steam_hidden_games.hpp"
#include "../../utils/steam_launch_history.hpp"
#include "../../utils/steam_library.hpp"
#include "../../utils/timing.hpp"
#include "../imgui_wrapper_base.hpp"
#include "../standalone_ui_settings_bridge.hpp"
#include "advanced_tab.hpp"

// Group 2 — ReShade / ImGui
#include <reshade_imgui.hpp>

// Group 3 — Standard C++
#include <algorithm>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

// Group 4 — Windows.h
#include <windows.h>

// Group 5 — Other Windows SDK
#include <Shellapi.h>

namespace ui::new_ui {

namespace {

struct GamesTabState {
    std::vector<display_commander::utils::RunningGameInfo> games;
    bool has_refresh_once = false;
    bool show_kill_modal = false;
    DWORD pending_kill_pid = 0;
    std::wstring pending_kill_title;
    LONGLONG last_refresh_ns = 0;
    bool show_details_modal = false;
    display_commander::utils::RunningGameInfo details_game;
    std::string last_launch_error;  // from TryInstallAddonToAppDataAndLaunchGamesUI
};

struct SteamLaunchState {
    std::vector<display_commander::steam_library::SteamGame> games;
    char search_buf[256] = {};
    bool has_loaded_once = false;
    bool show_details_modal = false;
    display_commander::steam_library::SteamGame details_steam_game;
};

GamesTabState& GetState() {
    static GamesTabState s_state;
    return s_state;
}

SteamLaunchState& GetSteamState() {
    static SteamLaunchState s_state;
    return s_state;
}

void RefreshGamesList() {
    auto& state = GetState();
    display_commander::utils::GetRunningGamesCache(state.games);
    state.has_refresh_once = true;
    state.last_refresh_ns = utils::get_now_ns();
}

void RequestGamesListRefresh() { display_commander::utils::RequestRunningGamesRefresh(); }

void DrawGamesTable(display_commander::ui::IImGuiWrapper& imgui) {
    auto& state = GetState();

    // Read from cache only (mutex discovery runs on monitoring thread). Re-read every 1s when tab visible.
    if (!state.has_refresh_once) {
        RefreshGamesList();
    } else {
        const LONGLONG now_ns = utils::get_now_ns();
        if (state.last_refresh_ns == 0 || (now_ns - state.last_refresh_ns) >= utils::SEC_TO_NS) {
            RefreshGamesList();
        }
    }

    /* if (imgui.Button("Refresh")) {
        RequestGamesListRefresh();
    }
    if (imgui.IsItemHovered()) {
        imgui.SetTooltipEx("Refresh the list of games that currently have Display Commander loaded.");
    }

    imgui.SameLine();
    */
    imgui.TextColored(::ui::colors::TEXT_DIMMED, "(Session-wide, based on Display Commander mutex)");
    ui::new_ui::DrawDcServiceIndicatorsOnLine(imgui, true);

    // Hide when already in standalone Games UI (exe or rundll32 Launcher).
    if (standalone_ui_settings::GetStandaloneUiHwnd() == nullptr) {
        if (imgui.Button("Open Games UI (standalone)")) {
            state.last_launch_error.clear();
            std::string err;
            if (!display_commander::utils::TryInstallAddonToAppDataAndLaunchGamesUI(&err)) {
                state.last_launch_error = err;
            }
        }
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx(
                "Copy addon to %%LocalAppData%%\\Programs\\Display_Commander and open the Games-only window (rundll32 "
                "Launcher).");
        }
        if (!state.last_launch_error.empty()) {
            imgui.TextColored(::ui::colors::TEXT_DIMMED, "Launch failed: %s", state.last_launch_error.c_str());
        }
    }

    imgui.Spacing();

    if (state.games.empty()) {
        imgui.TextColored(::ui::colors::TEXT_DIMMED,
                          "No running games with Display Commander detected in this Windows session.");
        return;
    }

    if (imgui.BeginTable("##dc_games_tab_table", 7,
                         display_commander::ui::wrapper_flags::TableFlags_RowBg
                             | display_commander::ui::wrapper_flags::TableFlags_Borders
                             | display_commander::ui::wrapper_flags::TableFlags_SizingStretchProp,
                         ImVec2{0.0f, 0.0f})) {
        imgui.TableSetupColumn("PID", display_commander::ui::wrapper_flags::TableColumnFlags_WidthFixed, 80.0f);
        imgui.TableSetupColumn("Title / Executable");
        imgui.TableSetupColumn("Focus", display_commander::ui::wrapper_flags::TableColumnFlags_WidthFixed, 80.0f);
        imgui.TableSetupColumn("Mini", display_commander::ui::wrapper_flags::TableColumnFlags_WidthFixed, 55.0f);
        imgui.TableSetupColumn("Rest", display_commander::ui::wrapper_flags::TableColumnFlags_WidthFixed, 55.0f);
        imgui.TableSetupColumn("Stop", display_commander::ui::wrapper_flags::TableColumnFlags_WidthFixed, 55.0f);
        imgui.TableSetupColumn("Kill", display_commander::ui::wrapper_flags::TableColumnFlags_WidthFixed, 80.0f);
        imgui.TableHeadersRow();

        const DWORD current_pid = GetCurrentProcessId();

        for (const auto& game : state.games) {
            imgui.TableNextRow();

            // PID
            imgui.TableSetColumnIndex(0);
            imgui.Text("%lu", static_cast<unsigned long>(game.pid));

            // Title (right-click for context menu)
            imgui.TableSetColumnIndex(1);
            imgui.PushID(static_cast<int>(game.pid));
            imgui.Text("%ls", game.display_title.c_str());
            if (!game.exe_path.empty() && imgui.IsItemHovered()) {
                imgui.SetTooltipEx("%ls", game.exe_path.c_str());
            }
            if (imgui.BeginPopupContextItem("row_ctx")) {
                if (imgui.MenuItem("Open details")) {
                    state.details_game = game;
                    state.show_details_modal = true;
                }
                imgui.EndPopup();
            }
            imgui.PopID();

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
                    imgui.SetTooltipEx("Bring this game's main window to the foreground.");
                } else {
                    imgui.SetTooltipEx("No main window detected for this game.");
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
                snprintf(min_label, sizeof(min_label), "Mini##%lu", static_cast<unsigned long>(game.pid));
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
                    imgui.SetTooltipEx("Minimize this game's main window to the taskbar.");
                } else {
                    imgui.SetTooltipEx("No main window detected for this game.");
                }
            }

            // Rest (Restart) button — terminate then relaunch (or launch first then close for current process)
            imgui.TableSetColumnIndex(4);
            bool is_current = (game.pid == current_pid);
            bool can_restart =
                !game.exe_path.empty() && (is_current || (game.main_window != nullptr || game.can_terminate));
            if (!can_restart) {
                imgui.BeginDisabled();
            }
            {
                char rest_label[32];
                snprintf(rest_label, sizeof(rest_label), "Rest##%lu", static_cast<unsigned long>(game.pid));
                if (imgui.SmallButton(rest_label) && can_restart) {
                    DWORD pid_to_restart = game.pid;
                    std::wstring exe_path = game.exe_path;
                    HWND main_window = game.main_window;
                    bool can_terminate = game.can_terminate;
                    bool is_current_process = is_current;
                    RequestGamesListRefresh();
                    std::thread([pid_to_restart, exe_path, main_window, can_terminate, is_current_process]() {
                        auto do_launch = [&exe_path]() {
                            std::filesystem::path exe_p(exe_path);
                            if (!std::filesystem::exists(exe_p)) return false;
                            std::wstring working_dir = exe_p.parent_path().wstring();
                            std::vector<wchar_t> cmd_buf;
                            cmd_buf.reserve(exe_path.size() + 3);
                            cmd_buf.push_back(L'\"');
                            cmd_buf.insert(cmd_buf.end(), exe_path.begin(), exe_path.end());
                            cmd_buf.push_back(L'\"');
                            cmd_buf.push_back(L'\0');
                            STARTUPINFOW si = {};
                            si.cb = sizeof(si);
                            PROCESS_INFORMATION pi = {};
                            if (CreateProcessW(nullptr, cmd_buf.data(), nullptr, nullptr, FALSE, 0, nullptr,
                                               working_dir.empty() ? nullptr : working_dir.c_str(), &si, &pi)) {
                                CloseHandle(pi.hProcess);
                                CloseHandle(pi.hThread);
                                return true;
                            }
                            return false;
                        };
                        if (is_current_process) {
                            // Current app: launch new instance first, then close ourselves
                            if (do_launch()) {
                                Sleep(300);
                                if (main_window != nullptr) {
                                    PostMessageW(main_window, WM_CLOSE, 0, 0);
                                } else {
                                    TerminateProcess(GetCurrentProcess(), 0);
                                }
                            }
                        } else {
                            // Other process: terminate first, then relaunch
                            if (main_window != nullptr) {
                                PostMessageW(main_window, WM_CLOSE, 0, 0);
                            } else if (can_terminate) {
                                HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, pid_to_restart);
                                if (h != nullptr) {
                                    TerminateProcess(h, 0);
                                    CloseHandle(h);
                                }
                            } else {
                                return;
                            }
                            Sleep(500);
                            do_launch();
                        }
                    }).detach();
                }
            }
            if (!can_restart) {
                imgui.EndDisabled();
            }
            if (imgui.IsItemHovered()) {
                if (game.exe_path.empty()) {
                    imgui.SetTooltipEx("Cannot restart (executable path unknown).");
                } else if (!game.main_window && !game.can_terminate && !is_current) {
                    imgui.SetTooltipEx("Cannot restart (no window and cannot terminate process).");
                } else {
                    imgui.SetTooltipEx("Restart this game (close and relaunch).");
                }
            }

            // Stop button (graceful close via WM_CLOSE)
            imgui.TableSetColumnIndex(5);
            bool can_stop = game.main_window != nullptr;
            if (!can_stop) {
                imgui.BeginDisabled();
            }
            {
                char stop_label[32];
                snprintf(stop_label, sizeof(stop_label), "Stop##%lu", static_cast<unsigned long>(game.pid));
                if (imgui.SmallButton(stop_label) && can_stop) {
                    HWND hwnd = game.main_window;
                    if (hwnd != nullptr) {
                        std::thread([hwnd]() { PostMessageW(hwnd, WM_CLOSE, 0, 0); }).detach();
                    }
                }
            }
            if (!can_stop) {
                imgui.EndDisabled();
            }
            if (imgui.IsItemHovered()) {
                if (can_stop) {
                    imgui.SetTooltipEx("Request graceful close (sends WM_CLOSE to the game window).");
                } else {
                    imgui.SetTooltipEx("No main window detected for this game.");
                }
            }

            // Kill button
            imgui.TableSetColumnIndex(6);
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
                    imgui.SetTooltipEx("Cannot terminate the current Display Commander process.");
                } else if (!game.can_terminate) {
                    imgui.SetTooltipEx("Insufficient permissions to terminate this process.");
                } else {
                    imgui.SetTooltipEx("Terminate this game process.");
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
        imgui.TextColored(::ui::colors::TEXT_WARNING, "Terminate game process PID %lu?",
                          static_cast<unsigned long>(state.pending_kill_pid));
        imgui.TextWrapped("%ls", state.pending_kill_title.empty() ? L"(no title)" : state.pending_kill_title.c_str());
        imgui.Spacing();
        imgui.TextColored(::ui::colors::TEXT_WARNING, "This will close the game immediately without saving progress.");

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

void DrawRunningGameDetailsModal(display_commander::ui::IImGuiWrapper& imgui) {
    auto& state = GetState();
    if (state.show_details_modal) {
        imgui.OpenPopup("Game Details");
        state.show_details_modal = false;
    }
    bool open = true;
    if (imgui.BeginPopupModal("Game Details", &open, ImGuiWindowFlags_AlwaysAutoResize)) {
        const auto& g = state.details_game;
        imgui.Text("PID: %lu", static_cast<unsigned long>(g.pid));
        imgui.Text("Title: %ls", g.display_title.empty() ? L"(none)" : g.display_title.c_str());
        imgui.Text("Exe path: %ls", g.exe_path.empty() ? L"(none)" : g.exe_path.c_str());
        imgui.Text("Main window: %p", static_cast<void*>(g.main_window));
        imgui.Text("Can terminate: %s", g.can_terminate ? "Yes" : "No");
        imgui.Spacing();
        if (imgui.Button("Close")) open = false;
        imgui.EndPopup();
    }
}

static std::string ToLowerAscii(const std::string& s) {
    std::string out = s;
    for (char& c : out)
        if (c >= 'A' && c <= 'Z') c += 32;
    return out;
}

// Format Unix timestamp as "today", "this week", "this month", "X months ago", or "never".
static void FormatLastOpened(int64_t ts, char* out, size_t out_size) {
    if (ts <= 0) {
        snprintf(out, out_size, "never");
        return;
    }
    const time_t now = time(nullptr);
    const time_t t = static_cast<time_t>(ts);
    const long diff_sec = static_cast<long>(now) - static_cast<long>(t);
    const long diff_days = diff_sec / 86400;

    if (diff_days < 0) {
        snprintf(out, out_size, "never");
        return;
    }
    if (diff_days == 0) {
        snprintf(out, out_size, "today");
        return;
    }
    if (diff_days < 7) {
        snprintf(out, out_size, "this week");
        return;
    }
    if (diff_days < 30) {
        snprintf(out, out_size, "this month");
        return;
    }
    const int months = static_cast<int>((diff_days + 15) / 30);
    snprintf(out, out_size, "%d months ago", months);
}

void DrawSteamLaunchSection(display_commander::ui::IImGuiWrapper& imgui) {
    auto& state = GetSteamState();

    if (!state.has_loaded_once) {
        display_commander::steam_library::GetInstalledGames(state.games);
        state.has_loaded_once = true;
    }

    imgui.Spacing();
    imgui.Separator();
    imgui.Spacing();
    imgui.Text("Launch Steam game");
    imgui.Spacing();

    // Type-to-search: when keyboard is not captured (e.g. focus on table/list), any typing or Ctrl+A
    // focuses the search box so the user can quickly type the game name without clicking first.
    const ImGuiIO& io = imgui.GetIO();
    const bool has_type_ahead = (io.InputQueueCharacters.Size > 0);
    const bool ctrl_a_pressed = io.KeyCtrl && imgui.IsKeyPressed(static_cast<int>(ImGuiKey_A));
    const bool want_search_focus = !io.WantCaptureKeyboard && (has_type_ahead || ctrl_a_pressed);
    if (want_search_focus) {
        imgui.SetKeyboardFocusHere(0);
    }

    imgui.SetNextItemWidth(-1.0f);
    imgui.InputTextWithHint("##steam_launch_search", "Search installed Steam games...", state.search_buf,
                            sizeof(state.search_buf));

    imgui.Spacing();

    std::string searchLower = ToLowerAscii(state.search_buf);
    std::vector<std::pair<display_commander::steam_library::SteamGame, int64_t>> filtered;
    for (const auto& game : state.games) {
        if (display_commander::steam_hidden_games::IsSteamGameHidden(game.app_id)) continue;
        std::string nameLower = ToLowerAscii(game.name);
        if (!searchLower.empty() && nameLower.find(searchLower) == std::string::npos) continue;
        int64_t ts = display_commander::steam_launch_history::GetSteamLaunchTimestamp(game.app_id);
        filtered.push_back({game, ts});
    }

    std::sort(filtered.begin(), filtered.end(), [](const auto& a, const auto& b) {
        const bool a_fav = display_commander::steam_favorites::IsSteamGameFavorite(a.first.app_id);
        const bool b_fav = display_commander::steam_favorites::IsSteamGameFavorite(b.first.app_id);
        if (a_fav != b_fav) return a_fav;  // Favorites first
        if (a.second != b.second) return a.second > b.second;
        return a.first.name < b.first.name;
    });

    if (imgui.BeginChild("##steam_launch_list", ImVec2(0.0f, 0.0f), true)) {
        if (state.games.empty()) {
            imgui.TextColored(::ui::colors::TEXT_DIMMED, "No Steam library found or no Steam games installed.");
        } else if (filtered.empty()) {
            imgui.TextColored(::ui::colors::TEXT_DIMMED, "No games match search.");
        } else if (imgui.BeginTable("##steam_launch_table", 2,
                                    display_commander::ui::wrapper_flags::TableFlags_SizingStretchProp
                                        | display_commander::ui::wrapper_flags::TableFlags_Borders,
                                    ImVec2(0.0f, 0.0f))) {
            imgui.TableSetupColumn("Game");
            imgui.TableSetupColumn("Last opened", display_commander::ui::wrapper_flags::TableColumnFlags_WidthFixed,
                                   100.0f);
            for (const auto& [game, lastLaunch] : filtered) {
                imgui.TableNextRow();
                imgui.TableSetColumnIndex(0);
                imgui.PushID(static_cast<int>(game.app_id));
                const bool is_favorite = display_commander::steam_favorites::IsSteamGameFavorite(game.app_id);
                if (is_favorite) {
                    imgui.PushStyleColor(ImGuiCol_Text, ::ui::colors::TEXT_HIGHLIGHT);
                }
                std::string display_name;
                if (is_favorite) {
                    display_name = "\xE2\x98\x85 ";  // ★ (U+2605) UTF-8
                    display_name += game.name;
                }
                if (imgui.Selectable(is_favorite ? display_name.c_str() : game.name.c_str(), false)) {
                    display_commander::steam_library::LaunchSteamGame(game.app_id);
                }
                if (is_favorite) {
                    imgui.PopStyleColor();
                }
                if (imgui.IsItemHovered()) {
                    if (game.install_dir.empty()) {
                        imgui.SetTooltipEx("Launch this game via Steam.");
                    } else {
                        imgui.SetTooltipEx("Launch this game via Steam.\n%ls", game.install_dir.c_str());
                    }
                }
                if (imgui.BeginPopupContextItem("steam_row_ctx")) {
                    if (imgui.MenuItem("Open details")) {
                        auto& steam_state = GetSteamState();
                        steam_state.details_steam_game = game;
                        steam_state.show_details_modal = true;
                    }
                    if (display_commander::steam_favorites::IsSteamGameFavorite(game.app_id)) {
                        if (imgui.MenuItem("Remove from Favorites")) {
                            display_commander::steam_favorites::RemoveSteamGameFromFavorites(game.app_id);
                        }
                    } else {
                        if (imgui.MenuItem("Add to Favorites")) {
                            display_commander::steam_favorites::AddSteamGameToFavorites(game.app_id);
                        }
                    }
                    if (imgui.MenuItem("Hide Game")) {
                        display_commander::steam_hidden_games::AddSteamGameToHidden(game.app_id);
                    }
                    imgui.EndPopup();
                }
                imgui.TableSetColumnIndex(1);
                char lastOpened[32];
                FormatLastOpened(lastLaunch, lastOpened, sizeof(lastOpened));
                imgui.TextColored(::ui::colors::TEXT_DIMMED, "%s", lastOpened);
                imgui.PopID();
            }
            imgui.EndTable();
        }
        imgui.EndChild();
    }

    // Steam game details modal
    auto& steam_state = GetSteamState();
    if (steam_state.show_details_modal) {
        imgui.OpenPopup("Steam Game Details");
        steam_state.show_details_modal = false;
    }
    bool steam_open = true;
    if (imgui.BeginPopupModal("Steam Game Details", &steam_open, ImGuiWindowFlags_AlwaysAutoResize)) {
        const auto& g = steam_state.details_steam_game;
        imgui.Text("App ID: %u", g.app_id);
        imgui.Text("Name: %s", g.name.c_str());
        imgui.Text("Install dir: %ls", g.install_dir.empty() ? L"(none)" : g.install_dir.c_str());
        imgui.Spacing();
        if (!g.install_dir.empty()) {
            if (imgui.Button("Open folder##steam_details")) {
                ShellExecuteW(nullptr, L"explore", g.install_dir.c_str(), nullptr, nullptr, SW_SHOW);
            }
            if (imgui.IsItemHovered()) {
                imgui.SetTooltipEx("Open game folder in Explorer.");
            }
            imgui.SameLine();
        }
        if (imgui.Button("Close##steam_details")) steam_open = false;
        imgui.EndPopup();
    }
}

}  // namespace

void DrawGamesTab(display_commander::ui::IImGuiWrapper& imgui) {
    imgui.Indent();

    DrawGamesTable(imgui);
    DrawKillConfirmationModal(imgui);
    DrawRunningGameDetailsModal(imgui);
    DrawSteamLaunchSection(imgui);

    imgui.Unindent();
}

}  // namespace ui::new_ui
