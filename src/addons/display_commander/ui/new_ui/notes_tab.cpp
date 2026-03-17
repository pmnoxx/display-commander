// Source Code <Display Commander> // follow this order for includes in all files + add this comment at the top
#include "notes_tab.hpp"
#include "../../res/forkawesome.h"
#include "../../utils/general_utils.hpp"
#include "../../utils/logging.hpp"
#include "../imgui_wrapper_base.hpp"

// Libraries <ReShade> / <imgui>
#include <imgui.h>

// Libraries <standard C++>
#include <filesystem>
#include <fstream>
#include <string>

// Libraries <Windows.h>
#include <Windows.h>

// Libraries <Windows>
#include <Shellapi.h>

namespace ui::new_ui {

namespace {

constexpr size_t k_notes_buffer_size = 262144;  // 256 KB max

// Persisted state: last game name we loaded for; buffer; dirty flag
std::string g_notes_last_game_name;
char g_notes_buffer[k_notes_buffer_size] = {};
bool g_notes_dirty = false;

void LoadNotesFromFile() {
    std::filesystem::path path = GetGameNotesFilePath();
    if (path.empty()) {
        g_notes_buffer[0] = '\0';
        g_notes_last_game_name.clear();
        return;
    }
    std::string game_name = GetGameNameFromProcess();
    if (game_name != g_notes_last_game_name) {
        g_notes_last_game_name = game_name;
        g_notes_dirty = false;
        g_notes_buffer[0] = '\0';
        std::error_code ec;
        if (!std::filesystem::is_regular_file(path, ec)) {
            return;
        }
        std::ifstream file(path, std::ios::in | std::ios::binary);
        if (!file.is_open()) {
            LogError("Notes: failed to open %s for read", path.string().c_str());
            return;
        }
        file.read(g_notes_buffer, static_cast<std::streamsize>(k_notes_buffer_size - 1));
        std::streamsize got = file.gcount();
        if (got >= 0) {
            g_notes_buffer[static_cast<size_t>(got)] = '\0';
        }
    }
}

bool SaveNotesToFile() {
    std::filesystem::path path = GetGameNotesFilePath();
    if (path.empty()) {
        return false;
    }
    std::filesystem::path dir = path.parent_path();
    std::error_code ec;
    if (!std::filesystem::exists(dir, ec)) {
        if (!std::filesystem::create_directories(dir, ec)) {
            LogError("Notes: failed to create directory %s: %s", dir.string().c_str(), ec.message().c_str());
            return false;
        }
    }
    std::ofstream file(path, std::ios::out | std::ios::binary | std::ios::trunc);
    if (!file.is_open()) {
        LogError("Notes: failed to open %s for write", path.string().c_str());
        return false;
    }
    const std::string content(g_notes_buffer);
    file.write(content.data(), static_cast<std::streamsize>(content.size()));
    if (!file.good()) {
        LogError("Notes: write failed for %s", path.string().c_str());
        return false;
    }
    g_notes_dirty = false;
    return true;
}

}  // namespace

void DrawNotesTab(display_commander::ui::IImGuiWrapper& imgui) {
    LoadNotesFromFile();

    std::filesystem::path notes_path = GetGameNotesFilePath();
    if (notes_path.empty()) {
        imgui.TextDisabled("Notes not available (no game folder detected).");
        return;
    }

    std::string game_name = GetGameNameFromProcess();
    imgui.Text("Game: %s", game_name.c_str());
    imgui.TextDisabled("Path: %s", notes_path.string().c_str());
    imgui.Spacing();

    if (imgui.Button(ICON_FK_FLOPPY " Save")) {
        SaveNotesToFile();
    }
    if (imgui.IsItemHovered()) {
        imgui.SetTooltipEx("Save notes to file. Also saved automatically when the input loses focus.");
    }
    imgui.SameLine();
    if (imgui.Button(ICON_FK_FILE " Open")) {
        std::error_code ec;
        if (std::filesystem::exists(notes_path, ec)) {
            std::string path_str = notes_path.string();
            HINSTANCE result = ShellExecuteA(nullptr, "open", path_str.c_str(), nullptr, nullptr, SW_SHOW);
            if (reinterpret_cast<intptr_t>(result) <= 32) {
                LogError("Failed to open notes file: %s (Error: %d)", path_str.c_str(),
                         static_cast<int>(reinterpret_cast<intptr_t>(result)));
            }
        } else {
            if (SaveNotesToFile()) {
                std::string path_str = notes_path.string();
                HINSTANCE result = ShellExecuteA(nullptr, "open", path_str.c_str(), nullptr, nullptr, SW_SHOW);
                if (reinterpret_cast<intptr_t>(result) <= 32) {
                    LogError("Failed to open notes file: %s (Error: %d)", path_str.c_str(),
                             static_cast<int>(reinterpret_cast<intptr_t>(result)));
                }
            }
        }
    }
    if (imgui.IsItemHovered()) {
        imgui.SetTooltipEx("Open the notes file in the default text editor.");
    }
    imgui.SameLine();
    std::filesystem::path notes_dir = notes_path.parent_path();
    if (imgui.Button(ICON_FK_FOLDER_OPEN " Open folder")) {
        std::error_code ec;
        if (!std::filesystem::exists(notes_dir, ec)) {
            std::filesystem::create_directories(notes_dir, ec);
        }
        if (!notes_dir.empty()) {
            std::string dir_str = notes_dir.string();
            HINSTANCE result = ShellExecuteA(nullptr, "explore", dir_str.c_str(), nullptr, nullptr, SW_SHOW);
            if (reinterpret_cast<intptr_t>(result) <= 32) {
                LogError("Failed to open notes folder: %s (Error: %d)", dir_str.c_str(),
                         static_cast<int>(reinterpret_cast<intptr_t>(result)));
            }
        }
    }
    if (imgui.IsItemHovered()) {
        imgui.SetTooltipEx("Open the notes folder in Windows Explorer (Games\\<game>).");
    }
    imgui.SameLine();
    if (g_notes_dirty) {
        imgui.TextDisabled("(unsaved changes)");
    }

    imgui.Spacing();

    ImVec2 size = imgui.GetContentRegionAvail();
    if (size.y < 100.0f) {
        size.y = 200.0f;
    }
    if (imgui.InputTextMultiline("##notes", g_notes_buffer, k_notes_buffer_size, size, 0)) {
        g_notes_dirty = true;
    }
    if (imgui.IsItemDeactivatedAfterEdit() && g_notes_dirty) {
        SaveNotesToFile();
    }
}

}  // namespace ui::new_ui
