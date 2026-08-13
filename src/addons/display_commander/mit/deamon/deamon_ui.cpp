// Source Code <Display Commander> // follow this order for includes in all files + add this comment at the top
#include "deamon_ui.hpp"
#include "deamon.hpp"
#include "../../ui/forkawesome.h"
#include "../../ui/ui_colors.hpp"
#include "../../utils/string_utils.hpp"

// Libraries <ReShade> / <imgui>
#include <imgui.h>

// Libraries <standard C++>
#include <string>

namespace display_commander::mit::deamon {

void DrawDaemonSection(display_commander::ui::IImGuiWrapper& imgui) {
    if (!imgui.CollapsingHeader("Daemon", display_commander::ui::wrapper_flags::TreeNodeFlags_None)) {
        return;
    }

    imgui.Indent();

    const DaemonStatus status = GetDaemonStatus();
    const bool alive = IsDaemonProcessAlive();

    imgui.TextWrapped(
        "Background watcher process (rundll32). Copied next to this game's folder under "
        "%%LocalAppData%%\\Programs\\Display_Commander\\daemon. Used later for other features.");

    if (!status.attempted) {
        imgui.TextColored(::ui::colors::TEXT_DIMMED, ICON_FK_MINUS " Daemon start has not been attempted yet.");
        imgui.Unindent();
        return;
    }

    if (status.process_started && alive) {
        imgui.TextColored(::ui::colors::TEXT_SUCCESS, ICON_FK_OK " Daemon is running.");
    } else if (status.process_started) {
        imgui.TextColored(::ui::colors::TEXT_WARNING, ICON_FK_WARNING " Daemon was started but is not running now.");
    } else {
        imgui.TextColored(::ui::colors::TEXT_ERROR, ICON_FK_WARNING " Daemon did not start.");
    }

    imgui.Text("Game PID: %lu", static_cast<unsigned long>(status.target_pid));
    imgui.Text("Daemon PID: %lu", static_cast<unsigned long>(status.daemon_pid));
    imgui.Text("DLL copy: %s", status.copy_ok ? "ok" : (status.process_started ? "skipped (in use)" : "failed"));

    if (!status.message.empty()) {
        imgui.TextWrapped("%s", status.message.c_str());
    }
    if (status.last_error != 0) {
        imgui.TextColored(::ui::colors::TEXT_WARNING, "Last error: %lu",
                          static_cast<unsigned long>(status.last_error));
    }

    if (!status.folder.empty()) {
        const std::string folder_utf8 = display_commander::utils::WideToUtf8(status.folder);
        imgui.TextWrapped("Folder: %s", folder_utf8.c_str());
    }
    if (!status.dll_path.empty()) {
        const std::string dll_utf8 = display_commander::utils::WideToUtf8(status.dll_path);
        imgui.TextWrapped("DLL: %s", dll_utf8.c_str());
    }

    const std::string log_text = ReadDaemonLogUtf8();
    imgui.Spacing();
    imgui.TextUnformatted("daemon.txt:");
    if (log_text.empty()) {
        imgui.TextColored(::ui::colors::TEXT_DIMMED, "(empty or not written yet)");
    } else {
        imgui.BeginChild("daemon_log", ImVec2(0.0f, imgui.GetTextLineHeightWithSpacing() * 6.0f), true);
        imgui.TextUnformatted(log_text.c_str());
        imgui.EndChild();
    }

    imgui.Unindent();
}

}  // namespace display_commander::mit::deamon
