// Source Code <Display Commander> // follow this order for includes in all files + add this comment at the top
#include "dpi_scaling_ui.hpp"
#include "dpi_appcompat.hpp"
#include "../../ui/forkawesome.h"
#include "../../ui/ui_colors.hpp"
#include "../../utils/string_utils.hpp"

// Libraries <ReShade> / <imgui>
#include <imgui.h>

// Libraries <standard C++>
#include <string>

namespace display_commander::mit::dpi {

void DrawDpiScalingSection(display_commander::ui::IImGuiWrapper& imgui) {
    if (!imgui.CollapsingHeader("DPI Scaling", display_commander::ui::wrapper_flags::TreeNodeFlags_None)) {
        return;
    }

    imgui.Indent();

    DpiAppCompatStatus status;
    QueryCurrentExeDpiAppCompat(status);

    imgui.TextWrapped("Windows AppCompat Layers (%s)", kLayersKeyUtf8);
    imgui.TextWrapped("High DPI scaling performed by: Application (HIGHDPIAWARE). Takes effect on the next launch.");

    if (!status.exe_path_ok) {
        imgui.TextColored(::ui::colors::TEXT_ERROR, ICON_FK_WARNING " Could not resolve this process executable path (error %ld).",
                          status.last_error);
        imgui.Unindent();
        return;
    }

    const std::string exe_utf8 = display_commander::utils::WideToUtf8(status.exe_path);
    imgui.TextWrapped("Executable: %s", exe_utf8.c_str());

    if (status.last_error != 0 && !status.value_exists) {
        imgui.TextColored(::ui::colors::TEXT_WARNING, "Could not read Layers value (error %ld).", status.last_error);
    }

    if (status.high_dpi_aware) {
        imgui.TextColored(::ui::colors::TEXT_SUCCESS, ICON_FK_OK " High DPI aware (Application) is set for this exe.");
    } else {
        imgui.TextColored(::ui::colors::TEXT_WARNING, ICON_FK_WARNING " High DPI aware flag is not set for this exe.");
    }

    if (status.value_exists) {
        const std::string data_utf8 = display_commander::utils::WideToUtf8(status.layers_data);
        imgui.TextWrapped("Current Layers flags: %s", data_utf8.empty() ? "(empty)" : data_utf8.c_str());
    } else {
        imgui.TextColored(::ui::colors::TEXT_DIMMED, "No Layers value for this executable yet.");
    }

    if (!status.high_dpi_aware) {
        if (imgui.Button("Set High DPI Aware")) {
            EnsureCurrentExeHighDpiAware();
        }
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx(
                "Writes ~ HIGHDPIAWARE for this executable under the AppCompat Layers key.\n"
                "Other compatibility flags are kept. Restart the game for Windows to apply it.");
        }
    }

    imgui.Unindent();
}

}  // namespace display_commander::mit::dpi
