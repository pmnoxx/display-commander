// Source Code <Display Commander> // follow this order for includes in all files + add this comment at the top
#include "controller_tab.hpp"

// Source Code <Display Commander>
#include "../../globals.hpp"
#include "../../ui/imgui_wrapper_base.hpp"
#include "../../utils/timing.hpp"
#include "remapping_widget.hpp"
#include "xinput_hooks.hpp"
#include "xinput_widget.hpp"

// Libraries <ReShade> / <imgui>
#include <imgui.h>

// Libraries <standard C++>
#include <cstdint>

// Libraries <Windows.h>
#include <Windows.h>

namespace modules::controller {
namespace {

uint64_t g_last_getstate0_count = 0;
uint64_t g_last_getstate0_tick_ms = 0;
float g_getstate0_rate_hz = 0.0f;

uint64_t GetMonotonicTimeMs() {
    const LONGLONG ns = utils::get_time_ns();
    return (ns > 0) ? static_cast<uint64_t>(ns / utils::NS_TO_MS) : 0;
}

void DrawControllerPollingRatesSection(display_commander::ui::IImGuiWrapper& imgui) {
    if (!imgui.CollapsingHeader("Input polling rates", ImGuiTreeNodeFlags_DefaultOpen)) {
        return;
    }
    imgui.Indent();
    if (imgui.IsItemHovered()) {
        imgui.SetTooltipEx("XInput GetState(0) calls/sec (game polling).");
    }

    const uint64_t getstate0_calls = display_commanderhooks::GetXInputGetStateUserIndexZeroCallCount();
    const uint64_t now_ms = GetMonotonicTimeMs();
    if (g_last_getstate0_tick_ms == 0) {
        g_last_getstate0_tick_ms = now_ms;
        g_last_getstate0_count = getstate0_calls;
    }
    const uint64_t elapsed_ms = now_ms - g_last_getstate0_tick_ms;
    if (elapsed_ms >= 1000) {
        const uint64_t delta =
            (getstate0_calls >= g_last_getstate0_count) ? (getstate0_calls - g_last_getstate0_count) : 0;
        g_getstate0_rate_hz =
            (elapsed_ms > 0) ? (1000.0f * static_cast<float>(delta) / static_cast<float>(elapsed_ms)) : 0.0f;
        g_last_getstate0_count = getstate0_calls;
        g_last_getstate0_tick_ms = now_ms;
    }
    imgui.Text("XInput GetState(0): %.1f/sec (total: %llu)", g_getstate0_rate_hz,
               static_cast<unsigned long long>(getstate0_calls));
    if (imgui.IsItemHovered()) {
        imgui.SetTooltipEx("Game (or addon) calls to XInputGetState(0) per second.");
    }

    imgui.Unindent();
}

}  // namespace

void DrawControllerTab(display_commander::ui::IImGuiWrapper& imgui) {
    DrawControllerPollingRatesSection(imgui);
    imgui.Spacing();
    display_commander::widgets::xinput_widget::XInputWidget::DrawIfReady(imgui);
    imgui.Spacing();
    display_commander::widgets::remapping_widget::DrawRemappingWidget(imgui);
}

}  // namespace modules::controller
