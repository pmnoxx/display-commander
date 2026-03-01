#include "xinput_widget.hpp"
#include <windows.h>
#include <algorithm>
#include <reshade_imgui.hpp>
#include <sstream>
#include <vector>
#include "../../config/display_commander_config.hpp"
#include "../../dualsense/dualsense_hid_wrapper.hpp"
#include "../../globals.hpp"
#include "../../hooks/hook_suppression_manager.hpp"
#include "../../hooks/input_activity_stats.hpp"
#include "../../hooks/timeslowdown_hooks.hpp"
#include "../../hooks/windows_gaming_input_hooks.hpp"
#include "../../hooks/windows_hooks/windows_message_hooks.hpp"
#include "../../hooks/xinput_hooks.hpp"
#include "../../res/ui_colors.hpp"
#include "../../settings/advanced_tab_settings.hpp"
#include "../remapping_widget/remapping_widget.hpp"
#include "../../settings/experimental_tab_settings.hpp"
#include "../../settings/hook_suppression_settings.hpp"
#include "../../ui/imgui_wrapper_base.hpp"
#include "../../utils/general_utils.hpp"
#include "../../utils/logging.hpp"
#include "../../utils/srwlock_wrapper.hpp"
#include "../../utils/timing.hpp"

namespace display_commander::widgets::xinput_widget {

namespace {
constexpr uint64_t VIBRATION_TEST_DURATION_NS = 10ULL * 1000000000ULL;  // 10 seconds
}

// Helper function to get original GetTickCount64 value (unhooked)
static ULONGLONG GetOriginalTickCount64() {
    if (enabled_experimental_features && display_commanderhooks::GetTickCount64_Original) {
        return display_commanderhooks::GetTickCount64_Original();
    } else {
        return GetTickCount64();
    }
}

// Global shared state
std::shared_ptr<XInputSharedState> XInputWidget::g_shared_state = std::make_shared<XInputSharedState>();

// Global widget instance
std::unique_ptr<XInputWidget> g_xinput_widget = nullptr;

XInputWidget::XInputWidget() {
    // Initialize shared state if not already done
    if (!g_shared_state) {
        // Initialize controller states
        for (int i = 0; i < XUSER_MAX_COUNT; ++i) {
            ZeroMemory(&g_shared_state->controller_states[i], sizeof(XINPUT_STATE));
            g_shared_state->controller_connected[i] = ControllerState::Uninitialized;
            g_shared_state->last_packet_numbers[i] = 0;
            g_shared_state->last_update_times[i] = 0;

            // Initialize battery information
            ZeroMemory(&g_shared_state->battery_info[i], sizeof(XINPUT_BATTERY_INFORMATION));
            g_shared_state->last_battery_update_times[i] = 0;
            g_shared_state->battery_info_valid[i] = false;
        }
    }
}

void XInputWidget::Initialize() {
    if (is_initialized_) return;

    LogInfo("XInputWidget::Initialize() - Starting XInput widget initialization");

    // Load settings
    LoadSettings();

    is_initialized_ = true;
    LogInfo("XInputWidget::Initialize() - XInput widget initialization complete");
}

void XInputWidget::Cleanup() {
    if (!is_initialized_) return;

    // Save settings
    SaveSettings();

    is_initialized_ = false;
}

void XInputWidget::OnDraw(display_commander::ui::IImGuiWrapper& imgui) {
    if (!is_initialized_) {
        Initialize();
    }

    if (!g_shared_state) {
        imgui.TextColored(::ui::colors::ICON_CRITICAL, "XInput shared state not initialized");
        imgui.Unindent();
        return;
    }

    // Auto-stop vibration test after 10s (runs whenever this tab is drawn)
    if (vibration_test_start_ns_ != 0) {
        const uint64_t now_ns = utils::get_now_ns();
        if (now_ns - vibration_test_start_ns_ >= VIBRATION_TEST_DURATION_NS) {
            StopVibration();
            vibration_test_start_ns_ = 0;
        }
    }

    if (imgui.CollapsingHeader("Settings", 0)) {
        imgui.Indent();
        DrawSettings(imgui);
        imgui.Unindent();
    }
    imgui.Spacing();

    if (imgui.CollapsingHeader("Event Counters", 0)) {
        imgui.Indent();
        DrawEventCounters(imgui);
        imgui.Unindent();
    }
    imgui.Spacing();

    if (imgui.CollapsingHeader("Vibration Test", 0)) {
        imgui.Indent();
        DrawVibrationTest(imgui);
        imgui.Unindent();
    }
    imgui.Spacing();

    if (imgui.CollapsingHeader("Autofire Settings", 0)) {
        imgui.Indent();
        DrawAutofireSettings(imgui);
        imgui.Unindent();
    }
    imgui.Spacing();

    DrawControllerSelector(imgui);
    imgui.Spacing();

    DrawControllerState(imgui);
}

void XInputWidget::DrawSettings(display_commander::ui::IImGuiWrapper& imgui) {
    // Enable XInput hooks (using HookSuppressionManager)
    bool suppress_hooks = display_commanderhooks::HookSuppressionManager::GetInstance().ShouldSuppressHook(
        display_commanderhooks::HookType::XINPUT);
    bool enable_hooks = !suppress_hooks;
    if (imgui.Checkbox("Enable XInput Hooks", &enable_hooks)) {
        settings::g_hook_suppression_settings.suppress_xinput_hooks.SetValue(!enable_hooks);
        display_commanderhooks::InstallXInputHooks(nullptr);
    }
    if (imgui.IsItemHovered()) {
        imgui.SetTooltip("Enable XInput API hooks for input processing and remapping");
    }

    const bool is_unity_player = (GetModuleHandleA("UnityPlayer.dll") != nullptr);
    const bool wgi_suppressed = is_unity_player
                                    ? settings::g_advancedTabSettings.suppress_wgi_for_unity.GetValue()
                                    : settings::g_advancedTabSettings.suppress_wgi_for_non_unity_games.GetValue();
    if (enable_hooks && !wgi_suppressed) {
        imgui.TextColored(
            ::ui::colors::ICON_WARNING,
            "Warning: XInput is enabled but Windows.Gaming.Input is not suppressed. Games that use "
            "WGI may not call XInput; enable the WGI suppression option below for XInput features to work.");
        if (imgui.IsItemHovered()) {
            imgui.SetTooltip(
                "Enable \"Suppress WGI for Unity games\" or \"Suppress WGI for non-Unity games\" so the game falls "
                "back to XInput instead of WGI.");
        }
    }

    // Per-game-type WGI suppression (only one visible depending on UnityPlayer)
    static bool restart_needed_to_apply_settings = false;
    if (is_unity_player) {
        bool suppress_wgi_unity = settings::g_advancedTabSettings.suppress_wgi_for_unity.GetValue();
        if (imgui.Checkbox("Suppress WGI for Unity games", &suppress_wgi_unity)) {
            settings::g_advancedTabSettings.suppress_wgi_for_unity.SetValue(suppress_wgi_unity);
            settings::g_advancedTabSettings.suppress_wgi_for_unity.Save();
            LogInfo("Suppress WGI for Unity games: %s", suppress_wgi_unity ? "enabled" : "disabled");
            restart_needed_to_apply_settings = true;
        }
        if (imgui.IsItemHovered()) {
            imgui.SetTooltip(
                "When enabled, block Windows.Gaming.Input factory requests so this Unity game uses XInput.");
        }
    } else {
        bool suppress_wgi_non_unity = settings::g_advancedTabSettings.suppress_wgi_for_non_unity_games.GetValue();
        if (imgui.Checkbox("Suppress WGI for non-Unity games (may cause crashes)", &suppress_wgi_non_unity)) {
            settings::g_advancedTabSettings.suppress_wgi_for_non_unity_games.SetValue(suppress_wgi_non_unity);
            settings::g_advancedTabSettings.suppress_wgi_for_non_unity_games.Save();
            LogInfo("Suppress WGI for non-Unity games: %s", suppress_wgi_non_unity ? "enabled" : "disabled");
            restart_needed_to_apply_settings = true;
        }
        if (imgui.IsItemHovered()) {
            imgui.SetTooltip("When enabled, block Windows.Gaming.Input factory requests so the game uses XInput.");
        }
    }

    if (restart_needed_to_apply_settings) {
        imgui.TextColored(::ui::colors::TEXT_ERROR, "Restart needed to apply settings");
    }

    imgui.Spacing();

    // Swap A/B buttons
    bool swap_buttons = g_shared_state->swap_a_b_buttons.load();
    if (imgui.Checkbox("Swap A/B Buttons", &swap_buttons)) {
        g_shared_state->swap_a_b_buttons.store(swap_buttons);
        SaveSettings();
    }
    if (imgui.IsItemHovered()) {
        imgui.SetTooltip("Swap the A and B button mappings");
    }

    // DualSense to XInput conversion
    bool dualsense_xinput = g_shared_state->enable_dualsense_xinput.load();
    if (imgui.Checkbox("DualSense to XInput", &dualsense_xinput)) {
        g_shared_state->enable_dualsense_xinput.store(dualsense_xinput);
        SaveSettings();
    }
    if (imgui.IsItemHovered()) {
        imgui.SetTooltip("Convert DualSense controller input to XInput format");
    }

    // Test gamepad suppression (zero XInputGetState output to game)
    bool test_suppression = g_shared_state->test_gamepad_suppression.load();
    if (imgui.Checkbox("Test gamepad suppression", &test_suppression)) {
        g_shared_state->test_gamepad_suppression.store(test_suppression);
        SaveSettings();
    }
    if (imgui.IsItemHovered()) {
        imgui.SetTooltip(
            "When enabled, zeroes all gamepad output (buttons, sticks, triggers) returned to the game by "
            "XInputGetState/XInputGetStateEx. Use to test that suppression works; the game will see no input.");
    }

    // HID suppression enable
    bool hid_suppression = settings::g_experimentalTabSettings.hid_suppression_enabled.GetValue();
    if (imgui.Checkbox("Enable HID Suppression", &hid_suppression)) {
        settings::g_experimentalTabSettings.hid_suppression_enabled.SetValue(hid_suppression);
        LogInfo("HID suppression %s", hid_suppression ? "enabled" : "disabled");
    }
    if (imgui.IsItemHovered()) {
        imgui.SetTooltip(
            "Suppress HID input reading for games to prevent them from detecting controllers.\nUseful for "
            "preventing games from interfering with controller input handling.");
    }

    // HID CreateFile counters
    imgui.Spacing();
    imgui.TextColored(::ui::colors::TEXT_DEFAULT, "HID CreateFile Detection:");
    uint64_t hid_total = g_shared_state->hid_createfile_total.load();
    uint64_t hid_dualsense = g_shared_state->hid_createfile_dualsense.load();
    imgui.Text("HID CreateFile Total: %llu", hid_total);
    imgui.Text("HID CreateFile DualSense: %llu", hid_dualsense);
    if (imgui.IsItemHovered()) {
        imgui.SetTooltip(
            "Shows how many times the game tried to open HID devices via CreateFile.\nDualSense counter shows "
            "specifically DualSense controller access attempts.");
    }

    imgui.TextColored(::ui::colors::TEXT_DIMMED, "Stick mapping: input range [min%%, max%%] -> output [min%%, max%%]");
    imgui.SameLine();
    if (imgui.IsItemHovered()) {
        imgui.SetTooltip("Example: input 30%%-70%% mapped to 10%%-80%%");
    }

    auto DrawStickMappingSliders =
        [this, &imgui](const char* stick_name, bool* same_axes, std::atomic<bool>& same_axes_atomic,
                       std::atomic<float>& min_in_x, std::atomic<float>& max_in_x, std::atomic<float>& min_out_x,
                       std::atomic<float>& max_out_x, std::atomic<float>& min_in_y, std::atomic<float>& max_in_y,
                       std::atomic<float>& min_out_y, std::atomic<float>& max_out_y) {
            if (imgui.Checkbox((std::string("Same for both axes (") + stick_name + ")").c_str(), same_axes)) {
                same_axes_atomic.store(*same_axes);
                if (*same_axes) {
                    min_in_y.store(min_in_x.load());
                    max_in_y.store(max_in_x.load());
                    min_out_y.store(min_out_x.load());
                    max_out_y.store(max_out_x.load());
                }
                SaveSettings();
            }
            if (imgui.IsItemHovered()) {
                imgui.SetTooltip("When on: one set of 4 sliders for X and Y. When off: 8 sliders (4 per axis).");
            }

            auto Slider4 = [&imgui, this](const char* label, float* v, float v_min, float v_max, const char* fmt,
                                          std::atomic<float>& store) {
                if (imgui.SliderFloat(label, v, v_min, v_max, fmt)) {
                    store.store(*v / 100.0f);
                    SaveSettings();
                }
            };

            if (*same_axes) {
                float mi = min_in_x.load() * 100.0f;
                float ma = max_in_x.load() * 100.0f;
                float mo = min_out_x.load() * 100.0f;
                float mx = max_out_x.load() * 100.0f;
                Slider4("Min Input %", &mi, 0.0f, 100.0f, "%.0f%%", min_in_x);
                if (imgui.IsItemHovered()) imgui.SetTooltip("Input below this is zero (deadzone)");
                Slider4("Max Input %", &ma, 0.0f, 100.0f, "%.0f%%", max_in_x);
                if (imgui.IsItemHovered()) imgui.SetTooltip("Input at/above this maps to Max Output");
                Slider4("Min Output (anti-deadzone) %", &mo, 0.0f, 100.0f, "%.0f%%", min_out_x);
                if (imgui.IsItemHovered()) imgui.SetTooltip("Output at Min Input threshold");
                Slider4("Max Output %", &mx, 0.0f, 100.0f, "%.0f%%", max_out_x);
                if (imgui.IsItemHovered()) imgui.SetTooltip("Output at Max Input");
                min_in_y.store(min_in_x.load());
                max_in_y.store(max_in_x.load());
                min_out_y.store(min_out_x.load());
                max_out_y.store(max_out_x.load());
            } else {
                imgui.Text("X axis:");
                float mix = min_in_x.load() * 100.0f, maxx = max_in_x.load() * 100.0f, mox = min_out_x.load() * 100.0f,
                      mxx = max_out_x.load() * 100.0f;
                Slider4("X Min Input %", &mix, 0.0f, 100.0f, "%.0f%%", min_in_x);
                Slider4("X Max Input %", &maxx, 0.0f, 100.0f, "%.0f%%", max_in_x);
                Slider4("X Min Output %", &mox, 0.0f, 100.0f, "%.0f%%", min_out_x);
                Slider4("X Max Output %", &mxx, 0.0f, 100.0f, "%.0f%%", max_out_x);
                imgui.Text("Y axis:");
                float miy = min_in_y.load() * 100.0f, may = max_in_y.load() * 100.0f, moy = min_out_y.load() * 100.0f,
                      mxy = max_out_y.load() * 100.0f;
                Slider4("Y Min Input %", &miy, 0.0f, 100.0f, "%.0f%%", min_in_y);
                Slider4("Y Max Input %", &may, 0.0f, 100.0f, "%.0f%%", max_in_y);
                Slider4("Y Min Output %", &moy, 0.0f, 100.0f, "%.0f%%", min_out_y);
                Slider4("Y Max Output %", &mxy, 0.0f, 100.0f, "%.0f%%", max_out_y);
            }
        };

    imgui.Text("Left Stick");
    bool left_same = g_shared_state->left_stick_same_axes.load();
    DrawStickMappingSliders("left", &left_same, g_shared_state->left_stick_same_axes,
                            g_shared_state->left_stick_x_min_input, g_shared_state->left_stick_x_max_input,
                            g_shared_state->left_stick_x_min_output, g_shared_state->left_stick_x_max_output,
                            g_shared_state->left_stick_y_min_input, g_shared_state->left_stick_y_max_input,
                            g_shared_state->left_stick_y_min_output, g_shared_state->left_stick_y_max_output);

    imgui.Spacing();
    imgui.Text("Right Stick");
    bool right_same = g_shared_state->right_stick_same_axes.load();
    DrawStickMappingSliders("right", &right_same, g_shared_state->right_stick_same_axes,
                            g_shared_state->right_stick_x_min_input, g_shared_state->right_stick_x_max_input,
                            g_shared_state->right_stick_x_min_output, g_shared_state->right_stick_x_max_output,
                            g_shared_state->right_stick_y_min_input, g_shared_state->right_stick_y_max_input,
                            g_shared_state->right_stick_y_min_output, g_shared_state->right_stick_y_max_output);

    imgui.Separator();
    imgui.Text("Stick Processing Mode");
    imgui.Text("Choose how X/Y axes are processed together (circular) or separately (square):");

    // Left stick processing mode toggle
    bool left_circular = g_shared_state->left_stick_circular.load();
    const char* left_mode_text = left_circular ? "Circular (Default)" : "Square";
    if (imgui.Checkbox("Left Stick: Circular Processing", &left_circular)) {
        g_shared_state->left_stick_circular.store(left_circular);
        SaveSettings();
    }
    imgui.SameLine();
    imgui.TextColored(::ui::colors::TEXT_DIMMED, "(%s)", left_mode_text);
    if (imgui.IsItemHovered()) {
        imgui.SetTooltip(
            "Circular: X/Y axes processed together (radial deadzone preserves direction)\n"
            "Square: X/Y axes processed separately (independent deadzone per axis)\n"
            "Affects deadzone, anti-deadzone, and sensitivity settings");
    }

    // Right stick processing mode toggle
    bool right_circular = g_shared_state->right_stick_circular.load();
    const char* right_mode_text = right_circular ? "Circular (Default)" : "Square";
    if (imgui.Checkbox("Right Stick: Circular Processing", &right_circular)) {
        g_shared_state->right_stick_circular.store(right_circular);
        SaveSettings();
    }
    imgui.SameLine();
    imgui.TextColored(::ui::colors::TEXT_DIMMED, "(%s)", right_mode_text);
    if (imgui.IsItemHovered()) {
        imgui.SetTooltip(
            "Circular: X/Y axes processed together (radial deadzone preserves direction)\n"
            "Square: X/Y axes processed separately (independent deadzone per axis)\n"
            "Affects deadzone, anti-deadzone, and sensitivity settings");
    }

    imgui.Separator();
    imgui.Text("Stick Center Calibration");
    imgui.Text("Adjust these values to recenter your analog sticks if they drift:");

    // Left stick center X setting
    float left_center_x = g_shared_state->left_stick_center_x.load();
    if (imgui.SliderFloat("Left Stick Center X", &left_center_x, -1.0f, 1.0f, "%.3f")) {
        g_shared_state->left_stick_center_x.store(left_center_x);
        SaveSettings();
    }
    if (imgui.IsItemHovered()) {
        imgui.SetTooltip("X-axis center offset for left stick (-1.0 to 1.0)");
    }

    // Left stick center Y setting
    float left_center_y = g_shared_state->left_stick_center_y.load();
    if (imgui.SliderFloat("Left Stick Center Y", &left_center_y, -1.0f, 1.0f, "%.3f")) {
        g_shared_state->left_stick_center_y.store(left_center_y);
        SaveSettings();
    }
    if (imgui.IsItemHovered()) {
        imgui.SetTooltip("Y-axis center offset for left stick (-1.0 to 1.0)");
    }

    // Right stick center X setting
    float right_center_x = g_shared_state->right_stick_center_x.load();
    if (imgui.SliderFloat("Right Stick Center X", &right_center_x, -1.0f, 1.0f, "%.3f")) {
        g_shared_state->right_stick_center_x.store(right_center_x);
        SaveSettings();
    }
    if (imgui.IsItemHovered()) {
        imgui.SetTooltip("X-axis center offset for right stick (-1.0 to 1.0)");
    }

    // Right stick center Y setting
    float right_center_y = g_shared_state->right_stick_center_y.load();
    if (imgui.SliderFloat("Right Stick Center Y", &right_center_y, -1.0f, 1.0f, "%.3f")) {
        g_shared_state->right_stick_center_y.store(right_center_y);
        SaveSettings();
    }
    if (imgui.IsItemHovered()) {
        imgui.SetTooltip("Y-axis center offset for right stick (-1.0 to 1.0)");
    }

    // Reset centers button
    if (imgui.Button("Reset Stick Centers")) {
        g_shared_state->left_stick_center_x.store(0.0f);
        g_shared_state->left_stick_center_y.store(0.0f);
        g_shared_state->right_stick_center_x.store(0.0f);
        g_shared_state->right_stick_center_y.store(0.0f);
        SaveSettings();
    }
    if (imgui.IsItemHovered()) {
        imgui.SetTooltip("Reset all stick center offsets to 0.0");
    }

    imgui.Separator();
    imgui.Text("Vibration Amplification");
    imgui.Text("Amplify controller vibration/rumble intensity:");

    // Vibration amplification setting
    float vibration_amp = g_shared_state->vibration_amplification.load();
    float vibration_amp_percent = vibration_amp * 100.0f;
    if (imgui.SliderFloat("Vibration Amplification", &vibration_amp_percent, 0.0f, 1000.0f, "%.0f%%")) {
        g_shared_state->vibration_amplification.store(vibration_amp_percent / 100.0f);
        SaveSettings();
    }
    if (imgui.IsItemHovered()) {
        imgui.SetTooltip(
            "Amplify controller vibration intensity (100%% = normal, 200%% = double, 500%% = maximum).\n"
            "This affects all vibration commands from the game.");
    }

    // Reset vibration amplification button
    if (imgui.Button("Reset Vibration Amplification")) {
        g_shared_state->vibration_amplification.store(1.0f);
        SaveSettings();
    }
    if (imgui.IsItemHovered()) {
        imgui.SetTooltip("Reset vibration amplification to 100%% (normal)");
    }
}

void XInputWidget::DrawEventCounters(display_commander::ui::IImGuiWrapper& imgui) {
    uint64_t total_events = g_shared_state->total_events.load();
    uint64_t button_events = g_shared_state->button_events.load();
    uint64_t stick_events = g_shared_state->stick_events.load();
    uint64_t trigger_events = g_shared_state->trigger_events.load();

    imgui.Text("Total Events: %llu", total_events);
    imgui.Text("Button Events: %llu", button_events);
    imgui.Text("Stick Events: %llu", stick_events);
    imgui.Text("Trigger Events: %llu", trigger_events);

    imgui.Spacing();
    imgui.Separator();
    imgui.TextColored(::ui::colors::TEXT_DEFAULT, "XInput Call Rate (Smooth)");

    // Display smooth call rate for XInputGetState
    uint64_t getstate_update_ns = g_shared_state->xinput_getstate_update_ns.load();
    if (getstate_update_ns > 0) {
        double getstate_rate_hz = 1000000000.0 / getstate_update_ns;  // Convert ns to Hz
        imgui.Text("XInputGetState Rate: %.1f Hz (%.2f ms)", getstate_rate_hz, getstate_update_ns / 1000000.0);
    } else {
        imgui.TextColored(::ui::colors::TEXT_DIMMED, "XInputGetState Rate: No data");
    }

    // Last XInputGetState(0) call duration (only tracked for dwUserIndex == 0)
    const std::uint64_t getstate0_last_duration_ns =
        display_commanderhooks::GetXInputGetStateUserIndexZeroLastDurationNs();
    if (getstate0_last_duration_ns > 0) {
        imgui.Text("XInputGetState(0) last duration: %.3f ms",
                  static_cast<double>(getstate0_last_duration_ns) / 1000000.0);
    }

    // Display smooth call rate for XInputGetStateEx
    uint64_t getstateex_update_ns = g_shared_state->xinput_getstateex_update_ns.load();
    if (getstateex_update_ns > 0) {
        double getstateex_rate_hz = 1000000000.0 / getstateex_update_ns;  // Convert ns to Hz
        imgui.Text("XInputGetStateEx Rate: %.1f Hz (%.2f ms)", getstateex_rate_hz, getstateex_update_ns / 1000000.0);
    } else {
        imgui.TextColored(::ui::colors::TEXT_DIMMED, "XInputGetStateEx Rate: No data");
    }

    imgui.Spacing();
    imgui.Separator();
    imgui.TextColored(::ui::colors::TEXT_DEFAULT, "XInputGetCapabilities Hook Statistics");

    // Display hook statistics for XInputGetCapabilities
    const auto& capabilities_stats =
        display_commanderhooks::GetHookStats(display_commanderhooks::HOOK_XInputGetCapabilities);
    uint64_t capabilities_total_calls = capabilities_stats.total_calls.load();
    uint64_t capabilities_unsuppressed_calls = capabilities_stats.unsuppressed_calls.load();
    uint64_t capabilities_suppressed_calls = capabilities_total_calls - capabilities_unsuppressed_calls;

    imgui.Text("XInputGetCapabilities_Detour Calls: %llu", capabilities_total_calls);
    imgui.Text("XInputGetCapabilities_Original Calls: %llu", capabilities_unsuppressed_calls);
    if (capabilities_suppressed_calls > 0) {
        imgui.TextColored(::ui::colors::TEXT_DIMMED, "Suppressed Calls: %llu", capabilities_suppressed_calls);
    }

    // Reset button
    if (imgui.Button("Reset Counters")) {
        g_shared_state->total_events.store(0);
        g_shared_state->button_events.store(0);
        g_shared_state->stick_events.store(0);
        g_shared_state->trigger_events.store(0);
        g_shared_state->xinput_getstate_update_ns.store(0);
        g_shared_state->xinput_getstateex_update_ns.store(0);
        g_shared_state->last_xinput_call_time_ns.store(0);
        g_shared_state->hid_createfile_total.store(0);
        g_shared_state->hid_createfile_dualsense.store(0);
    }
}

void XInputWidget::DrawVibrationTest(display_commander::ui::IImGuiWrapper& imgui) {
    imgui.Text("Test controller vibration motors:");
    imgui.Spacing();

    // Auto-stop after 10s and show timer
    if (vibration_test_start_ns_ != 0) {
        const uint64_t now_ns = utils::get_now_ns();
        const uint64_t elapsed_ns = now_ns - vibration_test_start_ns_;
        if (elapsed_ns >= VIBRATION_TEST_DURATION_NS) {
            StopVibration();
            vibration_test_start_ns_ = 0;
        } else {
            const float remaining_s = static_cast<float>(VIBRATION_TEST_DURATION_NS - elapsed_ns) / 1e9f;
            imgui.TextColored(::ui::colors::TEXT_DIMMED, "Stopping in %.1f s (auto-stop)", remaining_s);
            imgui.Spacing();
        }
    }

    // Show current controller selection
    imgui.Text("Testing Controller: %d", selected_controller_);
    imgui.Spacing();

    // Left motor test
    if (imgui.Button("Test Left Motor", ImVec2(120, 30))) {
        TestLeftMotor();
    }
    if (imgui.IsItemHovered()) {
        imgui.SetTooltip("Test the left (low-frequency) vibration motor");
    }

    imgui.SameLine();

    // Right motor test
    if (imgui.Button("Test Right Motor", ImVec2(120, 30))) {
        TestRightMotor();
    }
    if (imgui.IsItemHovered()) {
        imgui.SetTooltip("Test the right (high-frequency) vibration motor");
    }

    imgui.Spacing();

    // Stop vibration
    if (imgui.Button("Stop Vibration", ImVec2(120, 30))) {
        StopVibration();
    }
    if (imgui.IsItemHovered()) {
        imgui.SetTooltip("Stop all vibration on the selected controller");
    }

    imgui.SameLine();

    // Test both motors
    if (imgui.Button("Test Both Motors", ImVec2(120, 30))) {
        TestLeftMotor();
        TestRightMotor();
    }
    if (imgui.IsItemHovered()) {
        imgui.SetTooltip("Test both vibration motors simultaneously");
    }

    imgui.Spacing();
    imgui.TextColored(::ui::colors::TEXT_DIMMED, "Note: Vibration auto-stops after 10 s, or use Stop to end early.");
}

void XInputWidget::DrawControllerSelector(display_commander::ui::IImGuiWrapper& imgui) {
    imgui.Text("Controller (0-3):");
    imgui.SameLine();

    // XInput uses 0-based indices 0..3 for the four controller slots
    std::vector<std::string> controller_names;
    for (int i = 0; i < XUSER_MAX_COUNT; ++i) {
        std::string status = GetControllerStatus(i);
        controller_names.push_back("Controller " + std::to_string(i) + " - " + status);
    }

    imgui.PushID("controller_selector");
    if (imgui.BeginCombo("##controller", controller_names[selected_controller_].c_str())) {
        for (int i = 0; i < XUSER_MAX_COUNT; ++i) {
            const bool is_selected = (i == selected_controller_);
            if (imgui.Selectable(controller_names[i].c_str(), is_selected)) {
                selected_controller_ = i;
            }
            if (is_selected) {
                imgui.SetItemDefaultFocus();
            }
        }
        imgui.EndCombo();
    }
    if (imgui.IsItemHovered()) {
        imgui.SetTooltip("XInput controller indices 0-3 (first to fourth controller). Same numbering as Autofire.");
    }
    imgui.PopID();
}

void XInputWidget::DrawControllerState(display_commander::ui::IImGuiWrapper& imgui) {
    if (selected_controller_ < 0 || selected_controller_ >= XUSER_MAX_COUNT) {
        imgui.TextColored(::ui::colors::ICON_CRITICAL, "Invalid controller selected");
        imgui.Unindent();
        return;
    }

    // Get controller state (thread-safe read)
    utils::SRWLockShared lock(g_shared_state->state_lock);
    const XINPUT_STATE& state = g_shared_state->controller_states[selected_controller_];
    ControllerState controller_state = g_shared_state->controller_connected[selected_controller_];

    // Show XInput hook status so user can see if hooking is active (even when no controller data yet)
    const bool hooks_installed = display_commanderhooks::IsXInputHooksInstalled();
    if (hooks_installed) {
        imgui.TextColored(::ui::colors::STATUS_ACTIVE, "XInput hooks: active");
    } else {
        imgui.TextColored(::ui::colors::ICON_CRITICAL, "XInput hooks: not installed");
    }

    if (controller_state == ControllerState::Uninitialized) {
        imgui.TextColored(::ui::colors::TEXT_DIMMED, "Controller %d - Uninitialized", selected_controller_);
        const std::uint64_t getstate0_calls = display_commanderhooks::GetXInputGetStateUserIndexZeroCallCount();
        if (getstate0_calls == 0 && hooks_installed) {
            imgui.TextColored(::ui::colors::ICON_CRITICAL,
                              "No game calls to XInputGetState(0) yet. Game may use Windows.Gaming.Input or "
                              "DirectInput instead of XInput.");
            if (imgui.IsItemHovered()) {
                imgui.SetTooltip(
                    "Hollow Knight and some other games use Windows.Gaming.Input for controllers, so XInput is never "
                    "polled. Hooks are active on the XInput DLL when/if the game loads it.");
            }
        }
        return;
    } else if (controller_state == ControllerState::Unconnected) {
        imgui.TextColored(::ui::colors::TEXT_DIMMED, "Controller %d - Disconnected", selected_controller_);
        return;
    }

    // Draw controller info
    imgui.TextColored(::ui::colors::STATUS_ACTIVE, "Controller %d - Connected", selected_controller_);
    imgui.Text("Packet Number: %lu", state.dwPacketNumber);

    const std::uint64_t getstate0_calls = display_commanderhooks::GetXInputGetStateUserIndexZeroCallCount();
    imgui.Text("Game GetState(0) calls: %llu", static_cast<unsigned long long>(getstate0_calls));
    if (getstate0_calls == 0) {
        imgui.TextColored(
            ::ui::colors::ICON_CRITICAL,
            "No game calls to XInputGetState(0) detected. Game may use Windows.Gaming.Input or DirectInput instead.");
    }

    // Debug: Show raw button state
    imgui.Text("Raw Button State: 0x%04X", state.Gamepad.wButtons);
    imgui.Text("Home Button Constant: 0x%04X", XINPUT_GAMEPAD_GUIDE);

    // Get last update time
    uint64_t last_update = g_shared_state->last_update_times[selected_controller_].load();
    if (last_update > 0) {
        // Convert to milliseconds for display
        uint64_t now = GetOriginalTickCount64();
        uint64_t age_ms = now - last_update;
        imgui.Text("Last Update: %llu ms ago", age_ms);
    }

    imgui.Spacing();

    // Draw button states
    if (imgui.CollapsingHeader("Buttons", 0)) {
        imgui.Indent();
        DrawButtonStates(imgui, state.Gamepad);
        imgui.Unindent();
    }
    imgui.Spacing();

    // Draw stick states
    if (imgui.CollapsingHeader("Analog Sticks", 0)) {
        imgui.Indent();
        DrawStickStates(imgui, state.Gamepad);
        imgui.Unindent();
    }
    imgui.Spacing();

    // Draw trigger states
    if (imgui.CollapsingHeader("Triggers", 0)) {
        imgui.Indent();
        DrawTriggerStates(imgui, state.Gamepad);
        imgui.Unindent();
    }
    imgui.Spacing();

    // Draw battery status
    if (imgui.CollapsingHeader("Battery Status", 0)) {
        imgui.Indent();
        DrawBatteryStatus(imgui, selected_controller_);
        imgui.Unindent();
    }

    // Draw DualSense report if dualsense_to_xinput is enabled
    if (g_shared_state->enable_dualsense_xinput.load()) {
        imgui.Spacing();
        if (imgui.CollapsingHeader("DualSense Input Report", 0)) {
            imgui.Indent();
            DrawDualSenseReport(imgui, selected_controller_);
            imgui.Unindent();
        }
    }
}

void XInputWidget::DrawButtonStates(display_commander::ui::IImGuiWrapper& imgui, const XINPUT_GAMEPAD& gamepad) {
    // Create a grid of buttons
    const struct {
        WORD mask;
        const char* name;
    } buttons[] = {
        {XINPUT_GAMEPAD_A, "A"},
        {XINPUT_GAMEPAD_B, "B"},
        {XINPUT_GAMEPAD_X, "X"},
        {XINPUT_GAMEPAD_Y, "Y"},
        {XINPUT_GAMEPAD_LEFT_SHOULDER, "LB"},
        {XINPUT_GAMEPAD_RIGHT_SHOULDER, "RB"},
        {XINPUT_GAMEPAD_BACK, "View"},
        {XINPUT_GAMEPAD_START, "Menu"},
        {XINPUT_GAMEPAD_GUIDE, "Home"},
        {XINPUT_GAMEPAD_LEFT_THUMB, "LS"},
        {XINPUT_GAMEPAD_RIGHT_THUMB, "RS"},
        {XINPUT_GAMEPAD_DPAD_UP, "D-Up"},
        {XINPUT_GAMEPAD_DPAD_DOWN, "D-Down"},
        {XINPUT_GAMEPAD_DPAD_LEFT, "D-Left"},
        {XINPUT_GAMEPAD_DPAD_RIGHT, "D-Right"},
    };

    for (size_t i = 0; i < sizeof(buttons) / sizeof(buttons[0]); i += 2) {
        if (i + 1 < sizeof(buttons) / sizeof(buttons[0])) {
            // Draw two buttons per row
            bool pressed1 = IsButtonPressed(gamepad.wButtons, buttons[i].mask);
            bool pressed2 = IsButtonPressed(gamepad.wButtons, buttons[i + 1].mask);

            // Special styling for Home button
            if (buttons[i].mask == XINPUT_GAMEPAD_GUIDE) {
                imgui.PushStyleColor(ImGuiCol_Button,
                                     pressed1 ? ::ui::colors::ICON_WARNING : ::ui::colors::ICON_DARK_ORANGE);
            } else {
                imgui.PushStyleColor(ImGuiCol_Button,
                                     pressed1 ? ::ui::colors::STATUS_ACTIVE : ::ui::colors::ICON_DARK_GRAY);
            }
            imgui.Button(buttons[i].name, ImVec2(60, 30));
            imgui.PopStyleColor();

            imgui.SameLine();

            // Special styling for Home button
            if (buttons[i + 1].mask == XINPUT_GAMEPAD_GUIDE) {
                imgui.PushStyleColor(ImGuiCol_Button,
                                     pressed2 ? ::ui::colors::ICON_WARNING : ::ui::colors::ICON_DARK_ORANGE);
            } else {
                imgui.PushStyleColor(ImGuiCol_Button,
                                     pressed2 ? ::ui::colors::STATUS_ACTIVE : ::ui::colors::ICON_DARK_GRAY);
            }
            imgui.Button(buttons[i + 1].name, ImVec2(60, 30));
            imgui.PopStyleColor();
        } else {
            // Single button on last row
            bool pressed = IsButtonPressed(gamepad.wButtons, buttons[i].mask);

            // Special styling for Home button
            if (buttons[i].mask == XINPUT_GAMEPAD_GUIDE) {
                imgui.PushStyleColor(ImGuiCol_Button,
                                     pressed ? ::ui::colors::ICON_WARNING : ::ui::colors::ICON_DARK_ORANGE);
            } else {
                imgui.PushStyleColor(ImGuiCol_Button,
                                     pressed ? ::ui::colors::STATUS_ACTIVE : ::ui::colors::ICON_DARK_GRAY);
            }
            imgui.Button(buttons[i].name, ImVec2(60, 30));
            imgui.PopStyleColor();
        }
    }
}

void XInputWidget::DrawStickStates(display_commander::ui::IImGuiWrapper& imgui, const XINPUT_GAMEPAD& gamepad) {
    float lmin_in_x = g_shared_state->left_stick_x_min_input.load();
    float lmax_in_x = g_shared_state->left_stick_x_max_input.load();
    float lmin_out_x = g_shared_state->left_stick_x_min_output.load();
    float lmax_out_x = g_shared_state->left_stick_x_max_output.load();
    float lmin_in_y = g_shared_state->left_stick_y_min_input.load();
    float lmax_in_y = g_shared_state->left_stick_y_max_input.load();
    float lmin_out_y = g_shared_state->left_stick_y_min_output.load();
    float lmax_out_y = g_shared_state->left_stick_y_max_output.load();
    if (g_shared_state->left_stick_same_axes.load()) {
        lmin_in_y = lmin_in_x;
        lmax_in_y = lmax_in_x;
        lmin_out_y = lmin_out_x;
        lmax_out_y = lmax_out_x;
    }
    float rmin_in_x = g_shared_state->right_stick_x_min_input.load();
    float rmax_in_x = g_shared_state->right_stick_x_max_input.load();
    float rmin_out_x = g_shared_state->right_stick_x_min_output.load();
    float rmax_out_x = g_shared_state->right_stick_x_max_output.load();
    float rmin_in_y = g_shared_state->right_stick_y_min_input.load();
    float rmax_in_y = g_shared_state->right_stick_y_max_input.load();
    float rmin_out_y = g_shared_state->right_stick_y_min_output.load();
    float rmax_out_y = g_shared_state->right_stick_y_max_output.load();
    if (g_shared_state->right_stick_same_axes.load()) {
        rmin_in_y = rmin_in_x;
        rmax_in_y = rmax_in_x;
        rmin_out_y = rmin_out_x;
        rmax_out_y = rmax_out_x;
    }

    // Left stick
    imgui.Text("Left Stick:");
    float lx = ShortToFloat(gamepad.sThumbLX);
    float ly = ShortToFloat(gamepad.sThumbLY);

    float left_center_x = g_shared_state->left_stick_center_x.load();
    float left_center_y = g_shared_state->left_stick_center_y.load();
    float lx_recentered = lx - left_center_x;
    float ly_recentered = ly - left_center_y;

    float lx_final = lx_recentered;
    float ly_final = ly_recentered;
    bool left_circular = g_shared_state->left_stick_circular.load();
    if (left_circular) {
        ProcessStickInputRadial(lx_final, ly_final, lmin_in_x, lmax_in_x, lmin_out_x, lmax_out_x);
    } else {
        ProcessStickInputSquare(lx_final, ly_final, lmin_in_x, lmax_in_x, lmin_out_x, lmax_out_x, lmin_in_y, lmax_in_y,
                                lmin_out_y, lmax_out_y);
    }

    imgui.Text("X: %.3f (Raw) -> %.3f (Recentered) -> %.3f (Final) [Raw: %d]", lx, lx_recentered, lx_final,
               gamepad.sThumbLX);
    imgui.Text("Y: %.3f (Raw) -> %.3f (Recentered) -> %.3f (Final) [Raw: %d]", ly, ly_recentered, ly_final,
               gamepad.sThumbLY);

    // Visual representation
    imgui.Text("Position:");
    ImVec2 canvas_pos = imgui.GetCursorScreenPos();
    ImVec2 canvas_size = ImVec2(100, 100);
    display_commander::ui::IImDrawList* draw_list = imgui.GetWindowDrawList();

    // Draw circle
    ImVec2 center = ImVec2(canvas_pos.x + canvas_size.x * 0.5f, canvas_pos.y + canvas_size.y * 0.5f);
    draw_list->AddCircle(center, canvas_size.x * 0.4f, ImColor(100, 100, 100, 255), 32, 2.0f);

    // Draw crosshairs
    draw_list->AddLine(ImVec2(canvas_pos.x, center.y), ImVec2(canvas_pos.x + canvas_size.x, center.y),
                       ImColor(100, 100, 100, 255), 1.0f);
    draw_list->AddLine(ImVec2(center.x, canvas_pos.y), ImVec2(center.x, canvas_pos.y + canvas_size.y),
                       ImColor(100, 100, 100, 255), 1.0f);

    // Draw stick position (using final processed values for visual representation)
    ImVec2 stick_pos = ImVec2(center.x + lx_final * canvas_size.x * 0.4f, center.y - ly_final * canvas_size.y * 0.4f);
    draw_list->AddCircleFilled(stick_pos, 5.0f, ImColor(0, 255, 0, 255));

    imgui.Dummy(canvas_size);

    // Right stick
    imgui.Text("Right Stick:");
    float rx = ShortToFloat(gamepad.sThumbRX);
    float ry = ShortToFloat(gamepad.sThumbRY);

    // Apply center calibration (recenter the stick)
    float right_center_x = g_shared_state->right_stick_center_x.load();
    float right_center_y = g_shared_state->right_stick_center_y.load();
    float rx_recentered = rx - right_center_x;
    float ry_recentered = ry - right_center_y;

    float rx_final = rx_recentered;
    float ry_final = ry_recentered;
    bool right_circular = g_shared_state->right_stick_circular.load();
    if (right_circular) {
        ProcessStickInputRadial(rx_final, ry_final, rmin_in_x, rmax_in_x, rmin_out_x, rmax_out_x);
    } else {
        ProcessStickInputSquare(rx_final, ry_final, rmin_in_x, rmax_in_x, rmin_out_x, rmax_out_x, rmin_in_y, rmax_in_y,
                                rmin_out_y, rmax_out_y);
    }

    imgui.Text("X: %.3f (Raw) -> %.3f (Recentered) -> %.3f (Final) [Raw: %d]", rx, rx_recentered, rx_final,
               gamepad.sThumbRX);
    imgui.Text("Y: %.3f (Raw) -> %.3f (Recentered) -> %.3f (Final) [Raw: %d]", ry, ry_recentered, ry_final,
               gamepad.sThumbRY);

    // Visual representation for right stick
    imgui.Text("Position:");
    canvas_pos = imgui.GetCursorScreenPos();
    draw_list = imgui.GetWindowDrawList();

    // Draw circle
    center = ImVec2(canvas_pos.x + canvas_size.x * 0.5f, canvas_pos.y + canvas_size.y * 0.5f);
    draw_list->AddCircle(center, canvas_size.x * 0.4f, ImColor(100, 100, 100, 255), 32, 2.0f);

    // Draw crosshairs
    draw_list->AddLine(ImVec2(canvas_pos.x, center.y), ImVec2(canvas_pos.x + canvas_size.x, center.y),
                       ImColor(100, 100, 100, 255), 1.0f);
    draw_list->AddLine(ImVec2(center.x, canvas_pos.y), ImVec2(center.x, canvas_pos.y + canvas_size.y),
                       ImColor(100, 100, 100, 255), 1.0f);

    // Draw stick position (using final processed values for visual representation)
    stick_pos = ImVec2(center.x + rx_final * canvas_size.x * 0.4f, center.y - ry_final * canvas_size.y * 0.4f);
    draw_list->AddCircleFilled(stick_pos, 5.0f, ImColor(0, 255, 0, 255));

    imgui.Dummy(canvas_size);

    // Draw extended visualization with input/output curves
    if (imgui.CollapsingHeader("Input/Output Curves", 0)) {
        imgui.Indent();
        DrawStickStatesExtended(imgui, lmin_in_x, lmax_in_x, lmin_out_x, lmax_out_x, rmin_in_x, rmax_in_x, rmin_out_x,
                                rmax_out_x);
        imgui.Unindent();
    }
}

void XInputWidget::DrawStickStatesExtended(display_commander::ui::IImGuiWrapper& imgui, float left_min_in,
                                           float left_max_in, float left_min_out, float left_max_out,
                                           float right_min_in, float right_max_in, float right_min_out,
                                           float right_max_out) {
    imgui.TextColored(::ui::colors::TEXT_DEFAULT, "Visual representation of how stick input is processed");
    imgui.Spacing();

    // Generate curve data for both sticks
    const int curve_points = 400;
    std::vector<float> left_curve_x(curve_points);
    std::vector<float> left_curve_y(curve_points);
    std::vector<float> right_curve_x(curve_points);
    std::vector<float> right_curve_y(curve_points);
    std::vector<float> input_values(curve_points);

    // Generate input values from 0.0 to 1.0 (positive side only for clarity)
    for (int i = 0; i < curve_points; ++i) {
        input_values[i] = static_cast<float>(i) / (curve_points - 1);

        // For radial deadzone visualization, simulate moving stick from center to edge
        // This shows the magnitude transformation (radial deadzone preserves direction)
        float x = input_values[i];
        float y = 0.0f;  // Move along X axis for simplicity

        // Left stick - apply processing based on mode (use shared/X params for curve)
        float lx_test = x;
        float ly_test = y;
        bool left_circular = g_shared_state->left_stick_circular.load();
        if (left_circular) {
            ProcessStickInputRadial(lx_test, ly_test, left_min_in, left_max_in, left_min_out, left_max_out);
        } else {
            ProcessStickInputSquare(lx_test, ly_test, left_min_in, left_max_in, left_min_out, left_max_out, left_min_in,
                                    left_max_in, left_min_out, left_max_out);
        }
        left_curve_y[i] = std::sqrt(lx_test * lx_test + ly_test * ly_test);  // Show output magnitude

        // Right stick
        float rx_test = x;
        float ry_test = y;
        bool right_circular = g_shared_state->right_stick_circular.load();
        if (right_circular) {
            ProcessStickInputRadial(rx_test, ry_test, right_min_in, right_max_in, right_min_out, right_max_out);
        } else {
            ProcessStickInputSquare(rx_test, ry_test, right_min_in, right_max_in, right_min_out, right_max_out,
                                    right_min_in, right_max_in, right_min_out, right_max_out);
        }
        right_curve_y[i] = std::sqrt(rx_test * rx_test + ry_test * ry_test);  // Show output magnitude

        left_curve_x[i] = static_cast<float>(i);
        right_curve_x[i] = static_cast<float>(i);
    }

    // Left stick curve
    imgui.TextColored(::ui::colors::STATUS_ACTIVE, "Left Stick Input/Output Curve");
    imgui.Text("Input %.0f%%-%.0f%% -> Output %.0f%%-%.0f%%", left_min_in * 100.0f, left_max_in * 100.0f,
               left_min_out * 100.0f, left_max_out * 100.0f);

    // Create plot for left stick (0.0 to 1.0 input range)
    imgui.PlotLines("##LeftStickCurve", left_curve_y.data(), curve_points, 0, "Left Stick Output", 0.0f, 1.0f,
                    ImVec2(-1, 150));

    // Add reference lines
    display_commander::ui::IImDrawList* draw_list = imgui.GetWindowDrawList();
    ImVec2 plot_pos = imgui.GetItemRectMin();
    ImVec2 plot_size = imgui.GetItemRectSize();

    // Draw min input reference line (vertical)
    float min_in_x = plot_pos.x + left_min_in * plot_size.x;
    draw_list->AddLine(ImVec2(min_in_x, plot_pos.y), ImVec2(min_in_x, plot_pos.y + plot_size.y),
                       ImColor(255, 255, 0, 128), 2.0f);

    // Draw max input reference line (vertical)
    float max_in_x = plot_pos.x + left_max_in * plot_size.x;
    draw_list->AddLine(ImVec2(max_in_x, plot_pos.y), ImVec2(max_in_x, plot_pos.y + plot_size.y),
                       ImColor(255, 0, 255, 128), 2.0f);

    // Draw min output reference line (horizontal)
    float min_out_y = plot_pos.y + plot_size.y - left_min_out * plot_size.y;
    draw_list->AddLine(ImVec2(plot_pos.x, min_out_y), ImVec2(plot_pos.x + plot_size.x, min_out_y),
                       ImColor(0, 255, 255, 128), 2.0f);

    imgui.Spacing();

    // Right stick curve
    imgui.TextColored(::ui::colors::STATUS_ACTIVE, "Right Stick Input/Output Curve");
    imgui.Text("Input %.0f%%-%.0f%% -> Output %.0f%%-%.0f%%", right_min_in * 100.0f, right_max_in * 100.0f,
               right_min_out * 100.0f, right_max_out * 100.0f);

    // Create plot for right stick (0.0 to 1.0 input range)
    imgui.PlotLines("##RightStickCurve", right_curve_y.data(), curve_points, 0, "Right Stick Output", 0.0f, 1.0f,
                    ImVec2(-1, 150));

    // Add reference lines for right stick
    plot_pos = imgui.GetItemRectMin();
    plot_size = imgui.GetItemRectSize();

    // Draw min input reference line (vertical)
    float right_min_in_x = plot_pos.x + right_min_in * plot_size.x;
    draw_list->AddLine(ImVec2(right_min_in_x, plot_pos.y), ImVec2(right_min_in_x, plot_pos.y + plot_size.y),
                       ImColor(255, 255, 0, 128), 2.0f);

    // Draw max input reference line (vertical)
    float right_max_in_x = plot_pos.x + right_max_in * plot_size.x;
    draw_list->AddLine(ImVec2(right_max_in_x, plot_pos.y), ImVec2(right_max_in_x, plot_pos.y + plot_size.y),
                       ImColor(255, 0, 255, 128), 2.0f);

    // Draw min output reference line (horizontal)
    float right_min_out_y = plot_pos.y + plot_size.y - right_min_out * plot_size.y;
    draw_list->AddLine(ImVec2(plot_pos.x, right_min_out_y), ImVec2(plot_pos.x + plot_size.x, right_min_out_y),
                       ImColor(0, 255, 255, 128), 2.0f);

    imgui.Spacing();

    // Legend
    imgui.TextColored(::ui::colors::TEXT_VALUE, "Legend:");
    imgui.SameLine();
    imgui.TextColored(::ui::colors::TEXT_VALUE, "Yellow = Min Input (Vertical)");
    imgui.SameLine();
    imgui.TextColored(::ui::colors::ICON_SPECIAL, "Magenta = Max Input (Vertical)");
    imgui.SameLine();
    imgui.TextColored(::ui::colors::ICON_ANALYSIS, "Cyan = Min Output (Horizontal)");
    imgui.Spacing();
    imgui.TextColored(::ui::colors::TEXT_DIMMED, "Note: Radial deadzone preserves stick direction (circular deadzone)");
    imgui.Spacing();
    imgui.TextColored(::ui::colors::TEXT_DIMMED, "X-axis: Input (0.0 to 1.0) - Positive side only");
    imgui.TextColored(::ui::colors::TEXT_DIMMED, "Y-axis: Output (-1.0 to 1.0)");
}

void XInputWidget::DrawTriggerStates(display_commander::ui::IImGuiWrapper& imgui, const XINPUT_GAMEPAD& gamepad) {
    // Left trigger
    imgui.Text("Left Trigger: %u/255 (%.1f%%)", gamepad.bLeftTrigger,
               (static_cast<float>(gamepad.bLeftTrigger) / 255.0f) * 100.0f);

    // Visual bar for left trigger
    float left_trigger_norm = static_cast<float>(gamepad.bLeftTrigger) / 255.0f;
    imgui.ProgressBar(left_trigger_norm, ImVec2(-1, 0), "");

    // Right trigger
    imgui.Text("Right Trigger: %u/255 (%.1f%%)", gamepad.bRightTrigger,
               (static_cast<float>(gamepad.bRightTrigger) / 255.0f) * 100.0f);

    // Visual bar for right trigger
    float right_trigger_norm = static_cast<float>(gamepad.bRightTrigger) / 255.0f;
    imgui.ProgressBar(right_trigger_norm, ImVec2(-1, 0), "");
}

void XInputWidget::DrawBatteryStatus(display_commander::ui::IImGuiWrapper& imgui, int controller_index) {
    if (controller_index >= XUSER_MAX_COUNT || !g_shared_state) {
        return;
    }

    bool battery_valid = g_shared_state->battery_info_valid[controller_index].load();
    if (!battery_valid) {
        imgui.TextColored(::ui::colors::TEXT_DIMMED, "Battery information not available");
        return;
    }

    const XINPUT_BATTERY_INFORMATION& battery = g_shared_state->battery_info[controller_index];

    // Battery type
    std::string battery_type_str;
    ImVec4 type_color(1.0f, 1.0f, 1.0f, 1.0f);

    switch (battery.BatteryType) {
        case BATTERY_TYPE_DISCONNECTED:
            battery_type_str = "Disconnected";
            type_color = ::ui::colors::TEXT_DIMMED;
            break;
        case BATTERY_TYPE_WIRED:
            battery_type_str = "Wired (No Battery)";
            type_color = ::ui::colors::TEXT_INFO;
            break;
        case BATTERY_TYPE_ALKALINE:
            battery_type_str = "Alkaline Battery";
            type_color = ::ui::colors::TEXT_VALUE;
            break;
        case BATTERY_TYPE_NIMH:
            battery_type_str = "NiMH Battery";
            type_color = ::ui::colors::STATUS_ACTIVE;
            break;
        case BATTERY_TYPE_UNKNOWN:
            battery_type_str = "Unknown Battery Type";
            type_color = ::ui::colors::TEXT_DIMMED;
            break;
        default:
            battery_type_str = "Unknown";
            type_color = ::ui::colors::TEXT_DIMMED;
            break;
    }

    imgui.TextColored(type_color, "Type: %s", battery_type_str.c_str());

    // Battery level (only show for devices with actual batteries)
    if (battery.BatteryType != BATTERY_TYPE_DISCONNECTED && battery.BatteryType != BATTERY_TYPE_UNKNOWN
        && battery.BatteryType != BATTERY_TYPE_WIRED) {
        std::string level_str;
        ImVec4 level_color(1.0f, 1.0f, 1.0f, 1.0f);
        float level_progress = 0.0f;

        switch (battery.BatteryLevel) {
            case BATTERY_LEVEL_EMPTY:
                level_str = "Empty";
                level_color = ::ui::colors::ICON_CRITICAL;
                level_progress = 0.0f;
                break;
            case BATTERY_LEVEL_LOW:
                level_str = "Low";
                level_color = ::ui::colors::ICON_ORANGE;
                level_progress = 0.25f;
                break;
            case BATTERY_LEVEL_MEDIUM:
                level_str = "Medium";
                level_color = ::ui::colors::TEXT_VALUE;
                level_progress = 0.5f;
                break;
            case BATTERY_LEVEL_FULL:
                level_str = "Full";
                level_color = ::ui::colors::STATUS_ACTIVE;
                level_progress = 1.0f;
                break;
            default:
                level_str = "Unknown";
                level_color = ::ui::colors::TEXT_DIMMED;
                level_progress = 0.0f;
                break;
        }

        imgui.TextColored(level_color, "Level: %s", level_str.c_str());

        // Visual battery level bar
        imgui.PushStyleColor(ImGuiCol_PlotHistogram, level_color);
        imgui.ProgressBar(level_progress, ImVec2(-1, 0), "");
        imgui.PopStyleColor();
    } else if (battery.BatteryType == BATTERY_TYPE_WIRED) {
        // For wired devices, show a simple message that no battery level is available
        imgui.TextColored(::ui::colors::TEXT_INFO, "No battery level (Wired device)");
    } else {
        imgui.TextColored(::ui::colors::TEXT_DIMMED, "Battery level not available");
    }
}

std::string XInputWidget::GetButtonName(WORD button) const {
    switch (button) {
        case XINPUT_GAMEPAD_A:              return "A";
        case XINPUT_GAMEPAD_B:              return "B";
        case XINPUT_GAMEPAD_X:              return "X";
        case XINPUT_GAMEPAD_Y:              return "Y";
        case XINPUT_GAMEPAD_LEFT_SHOULDER:  return "LB";
        case XINPUT_GAMEPAD_RIGHT_SHOULDER: return "RB";
        case XINPUT_GAMEPAD_BACK:           return "View";
        case XINPUT_GAMEPAD_START:          return "Menu";
        case XINPUT_GAMEPAD_GUIDE:          return "Home";
        case XINPUT_GAMEPAD_LEFT_THUMB:     return "LS";
        case XINPUT_GAMEPAD_RIGHT_THUMB:    return "RS";
        case XINPUT_GAMEPAD_DPAD_UP:        return "D-Up";
        case XINPUT_GAMEPAD_DPAD_DOWN:      return "D-Down";
        case XINPUT_GAMEPAD_DPAD_LEFT:      return "D-Left";
        case XINPUT_GAMEPAD_DPAD_RIGHT:     return "D-Right";
        default:                            return "Unknown";
    }
}

std::string XInputWidget::GetControllerStatus(int controller_index) const {
    if (controller_index < 0 || controller_index >= XUSER_MAX_COUNT) {
        return "Invalid";
    }

    // Thread-safe read
    utils::SRWLockShared lock(g_shared_state->state_lock);
    ControllerState state = g_shared_state->controller_connected[controller_index];
    switch (state) {
        case ControllerState::Uninitialized: return "Uninitialized";
        case ControllerState::Connected:     return "Connected";
        case ControllerState::Unconnected:   return "Disconnected";
        default:                             return "Unknown";
    }
}

bool XInputWidget::IsButtonPressed(WORD buttons, WORD button) const { return (buttons & button) != 0; }

void XInputWidget::LoadSettings() {
    // Load swap A/B buttons setting
    bool swap_buttons;
    if (display_commander::config::get_config_value("DisplayCommander.XInputWidget", "SwapABButtons", swap_buttons)) {
        g_shared_state->swap_a_b_buttons.store(swap_buttons);
    }

    // Load DualSense to XInput conversion setting
    bool dualsense_xinput;
    if (display_commander::config::get_config_value("DisplayCommander.XInputWidget", "EnableDualSenseXInput",
                                                    dualsense_xinput)) {
        g_shared_state->enable_dualsense_xinput.store(dualsense_xinput);
    }

    // Load test gamepad suppression setting
    bool test_gamepad_suppression;
    if (display_commander::config::get_config_value("DisplayCommander.XInputWidget", "TestGamepadSuppression",
                                                    test_gamepad_suppression)) {
        g_shared_state->test_gamepad_suppression.store(test_gamepad_suppression);
    }

    // Load stick mapping (new 4 params per axis). Backward compat: migrate old 3 params to new 4.
    float left_min_in, left_max_in, left_min_out, left_max_out;
    if (display_commander::config::get_config_value("DisplayCommander.XInputWidget", "LeftStickXMinInput", left_min_in)
        && display_commander::config::get_config_value("DisplayCommander.XInputWidget", "LeftStickXMaxInput",
                                                       left_max_in)
        && display_commander::config::get_config_value("DisplayCommander.XInputWidget", "LeftStickXMinOutput",
                                                       left_min_out)
        && display_commander::config::get_config_value("DisplayCommander.XInputWidget", "LeftStickXMaxOutput",
                                                       left_max_out)) {
        g_shared_state->left_stick_x_min_input.store(left_min_in);
        g_shared_state->left_stick_x_max_input.store(left_max_in);
        g_shared_state->left_stick_x_min_output.store(left_min_out);
        g_shared_state->left_stick_x_max_output.store(left_max_out);
        float ly_min_in, ly_max_in, ly_min_out, ly_max_out;
        if (display_commander::config::get_config_value("DisplayCommander.XInputWidget", "LeftStickYMinInput",
                                                        ly_min_in)
            && display_commander::config::get_config_value("DisplayCommander.XInputWidget", "LeftStickYMaxInput",
                                                           ly_max_in)
            && display_commander::config::get_config_value("DisplayCommander.XInputWidget", "LeftStickYMinOutput",
                                                           ly_min_out)
            && display_commander::config::get_config_value("DisplayCommander.XInputWidget", "LeftStickYMaxOutput",
                                                           ly_max_out)) {
            g_shared_state->left_stick_y_min_input.store(ly_min_in);
            g_shared_state->left_stick_y_max_input.store(ly_max_in);
            g_shared_state->left_stick_y_min_output.store(ly_min_out);
            g_shared_state->left_stick_y_max_output.store(ly_max_out);
        } else {
            g_shared_state->left_stick_y_min_input.store(left_min_in);
            g_shared_state->left_stick_y_max_input.store(left_max_in);
            g_shared_state->left_stick_y_min_output.store(left_min_out);
            g_shared_state->left_stick_y_max_output.store(left_max_out);
        }
    } else {
        // Backward compat: old keys (deadzone %, sensitivity 0-1, min_output 0-1) -> new (min_in, max_in=1, min_out,
        // max_out=1)
        float old_deadzone_pct, old_sensitivity, old_min_out;
        if (display_commander::config::get_config_value("DisplayCommander.XInputWidget", "LeftStickMinInput",
                                                        old_deadzone_pct)) {
            g_shared_state->left_stick_x_min_input.store(old_deadzone_pct / 100.0f);
            g_shared_state->left_stick_y_min_input.store(old_deadzone_pct / 100.0f);
        }
        if (display_commander::config::get_config_value("DisplayCommander.XInputWidget", "LeftStickSensitivity",
                                                        old_sensitivity)) {
            g_shared_state->left_stick_x_max_input.store(old_sensitivity);
            g_shared_state->left_stick_y_max_input.store(old_sensitivity);
        }
        if (display_commander::config::get_config_value("DisplayCommander.XInputWidget", "LeftStickMaxOutput",
                                                        old_min_out)) {
            g_shared_state->left_stick_x_min_output.store(old_min_out);
            g_shared_state->left_stick_y_min_output.store(old_min_out);
        }
        g_shared_state->left_stick_x_max_output.store(1.0f);
        g_shared_state->left_stick_y_max_output.store(1.0f);
    }

    float right_min_in, right_max_in, right_min_out, right_max_out;
    if (display_commander::config::get_config_value("DisplayCommander.XInputWidget", "RightStickXMinInput",
                                                    right_min_in)
        && display_commander::config::get_config_value("DisplayCommander.XInputWidget", "RightStickXMaxInput",
                                                       right_max_in)
        && display_commander::config::get_config_value("DisplayCommander.XInputWidget", "RightStickXMinOutput",
                                                       right_min_out)
        && display_commander::config::get_config_value("DisplayCommander.XInputWidget", "RightStickXMaxOutput",
                                                       right_max_out)) {
        g_shared_state->right_stick_x_min_input.store(right_min_in);
        g_shared_state->right_stick_x_max_input.store(right_max_in);
        g_shared_state->right_stick_x_min_output.store(right_min_out);
        g_shared_state->right_stick_x_max_output.store(right_max_out);
        float ry_min_in, ry_max_in, ry_min_out, ry_max_out;
        if (display_commander::config::get_config_value("DisplayCommander.XInputWidget", "RightStickYMinInput",
                                                        ry_min_in)
            && display_commander::config::get_config_value("DisplayCommander.XInputWidget", "RightStickYMaxInput",
                                                           ry_max_in)
            && display_commander::config::get_config_value("DisplayCommander.XInputWidget", "RightStickYMinOutput",
                                                           ry_min_out)
            && display_commander::config::get_config_value("DisplayCommander.XInputWidget", "RightStickYMaxOutput",
                                                           ry_max_out)) {
            g_shared_state->right_stick_y_min_input.store(ry_min_in);
            g_shared_state->right_stick_y_max_input.store(ry_max_in);
            g_shared_state->right_stick_y_min_output.store(ry_min_out);
            g_shared_state->right_stick_y_max_output.store(ry_max_out);
        } else {
            g_shared_state->right_stick_y_min_input.store(right_min_in);
            g_shared_state->right_stick_y_max_input.store(right_max_in);
            g_shared_state->right_stick_y_min_output.store(right_min_out);
            g_shared_state->right_stick_y_max_output.store(right_max_out);
        }
    } else {
        float old_deadzone_pct, old_sensitivity, old_min_out;
        if (display_commander::config::get_config_value("DisplayCommander.XInputWidget", "RightStickMinInput",
                                                        old_deadzone_pct)) {
            g_shared_state->right_stick_x_min_input.store(old_deadzone_pct / 100.0f);
            g_shared_state->right_stick_y_min_input.store(old_deadzone_pct / 100.0f);
        }
        if (display_commander::config::get_config_value("DisplayCommander.XInputWidget", "RightStickSensitivity",
                                                        old_sensitivity)) {
            g_shared_state->right_stick_x_max_input.store(old_sensitivity);
            g_shared_state->right_stick_y_max_input.store(old_sensitivity);
        }
        if (display_commander::config::get_config_value("DisplayCommander.XInputWidget", "RightStickMaxOutput",
                                                        old_min_out)) {
            g_shared_state->right_stick_x_min_output.store(old_min_out);
            g_shared_state->right_stick_y_min_output.store(old_min_out);
        }
        g_shared_state->right_stick_x_max_output.store(1.0f);
        g_shared_state->right_stick_y_max_output.store(1.0f);
    }

    bool left_same_axes, right_same_axes;
    if (display_commander::config::get_config_value("DisplayCommander.XInputWidget", "LeftStickSameAxes",
                                                    left_same_axes)) {
        g_shared_state->left_stick_same_axes.store(left_same_axes);
    }
    if (display_commander::config::get_config_value("DisplayCommander.XInputWidget", "RightStickSameAxes",
                                                    right_same_axes)) {
        g_shared_state->right_stick_same_axes.store(right_same_axes);
    }

    // Load stick center calibration settings
    float left_center_x;
    if (display_commander::config::get_config_value("DisplayCommander.XInputWidget", "LeftStickCenterX",
                                                    left_center_x)) {
        g_shared_state->left_stick_center_x.store(left_center_x);
    }

    float left_center_y;
    if (display_commander::config::get_config_value("DisplayCommander.XInputWidget", "LeftStickCenterY",
                                                    left_center_y)) {
        g_shared_state->left_stick_center_y.store(left_center_y);
    }

    float right_center_x;
    if (display_commander::config::get_config_value("DisplayCommander.XInputWidget", "RightStickCenterX",
                                                    right_center_x)) {
        g_shared_state->right_stick_center_x.store(right_center_x);
    }

    float right_center_y;
    if (display_commander::config::get_config_value("DisplayCommander.XInputWidget", "RightStickCenterY",
                                                    right_center_y)) {
        g_shared_state->right_stick_center_y.store(right_center_y);
    }

    // Load vibration amplification setting
    float vibration_amp;
    if (display_commander::config::get_config_value("DisplayCommander.XInputWidget", "VibrationAmplification",
                                                    vibration_amp)) {
        g_shared_state->vibration_amplification.store(vibration_amp);
    }

    // Load stick processing mode settings
    bool left_circular;
    if (display_commander::config::get_config_value("DisplayCommander.XInputWidget", "LeftStickCircular",
                                                    left_circular)) {
        g_shared_state->left_stick_circular.store(left_circular);
    }

    bool right_circular;
    if (display_commander::config::get_config_value("DisplayCommander.XInputWidget", "RightStickCircular",
                                                    right_circular)) {
        g_shared_state->right_stick_circular.store(right_circular);
    }

    // Load autofire settings
    bool autofire_enabled;
    if (display_commander::config::get_config_value("DisplayCommander.XInputWidget", "AutofireEnabled",
                                                    autofire_enabled)) {
        g_shared_state->autofire_enabled.store(autofire_enabled);
    }

    // Load autofire frame settings (backward compatibility: try old name first, then new names)
    uint32_t autofire_frame_interval;
    if (display_commander::config::get_config_value("DisplayCommander.XInputWidget", "AutofireFrameInterval",
                                                    autofire_frame_interval)) {
        // Migrate old setting to new format (use as hold_down, set hold_up to same value)
        g_shared_state->autofire_hold_down_frames.store(autofire_frame_interval);
        g_shared_state->autofire_hold_up_frames.store(autofire_frame_interval);
    } else {
        // Load new settings
        uint32_t hold_down_frames;
        if (display_commander::config::get_config_value("DisplayCommander.XInputWidget", "AutofireHoldDownFrames",
                                                        hold_down_frames)) {
            g_shared_state->autofire_hold_down_frames.store(hold_down_frames);
        }
        uint32_t hold_up_frames;
        if (display_commander::config::get_config_value("DisplayCommander.XInputWidget", "AutofireHoldUpFrames",
                                                        hold_up_frames)) {
            g_shared_state->autofire_hold_up_frames.store(hold_up_frames);
        }
    }

    // Load autofire button list
    std::string autofire_buttons_str;
    if (display_commander::config::get_config_value("DisplayCommander.XInputWidget", "AutofireButtons",
                                                    autofire_buttons_str)) {
        // Parse comma-separated list of button masks
        std::istringstream iss(autofire_buttons_str);
        std::string token;
        while (std::getline(iss, token, ',')) {
            try {
                WORD button_mask = static_cast<WORD>(std::stoul(token, nullptr, 16));  // Parse as hex
                AddAutofireButton(button_mask);
            } catch (...) {
                // Ignore invalid entries
            }
        }
    }

    // Load autofire trigger list
    std::string autofire_triggers_str;
    if (display_commander::config::get_config_value("DisplayCommander.XInputWidget", "AutofireTriggers",
                                                    autofire_triggers_str)) {
        // Parse comma-separated list of trigger types (LT, RT)
        std::istringstream iss(autofire_triggers_str);
        std::string token;
        while (std::getline(iss, token, ',')) {
            // Trim whitespace
            size_t start = token.find_first_not_of(" \t");
            if (start != std::string::npos) {
                size_t end = token.find_last_not_of(" \t");
                token = token.substr(start, end - start + 1);

                if (token == "LT" || token == "lt" || token == "LeftTrigger") {
                    AddAutofireTrigger(XInputSharedState::TriggerType::LeftTrigger);
                } else if (token == "RT" || token == "rt" || token == "RightTrigger") {
                    AddAutofireTrigger(XInputSharedState::TriggerType::RightTrigger);
                }
            }
        }
    }
}

void XInputWidget::SaveSettings() {
    // Save swap A/B buttons setting
    display_commander::config::set_config_value("DisplayCommander.XInputWidget", "SwapABButtons",
                                                g_shared_state->swap_a_b_buttons.load());

    // Save DualSense to XInput conversion setting
    display_commander::config::set_config_value("DisplayCommander.XInputWidget", "EnableDualSenseXInput",
                                                g_shared_state->enable_dualsense_xinput.load());

    // Save test gamepad suppression setting
    display_commander::config::set_config_value("DisplayCommander.XInputWidget", "TestGamepadSuppression",
                                                g_shared_state->test_gamepad_suppression.load());

    // Save stick mapping (4 params per axis × 2 axes × 2 sticks)
    display_commander::config::set_config_value("DisplayCommander.XInputWidget", "LeftStickXMinInput",
                                                g_shared_state->left_stick_x_min_input.load());
    display_commander::config::set_config_value("DisplayCommander.XInputWidget", "LeftStickXMaxInput",
                                                g_shared_state->left_stick_x_max_input.load());
    display_commander::config::set_config_value("DisplayCommander.XInputWidget", "LeftStickXMinOutput",
                                                g_shared_state->left_stick_x_min_output.load());
    display_commander::config::set_config_value("DisplayCommander.XInputWidget", "LeftStickXMaxOutput",
                                                g_shared_state->left_stick_x_max_output.load());
    display_commander::config::set_config_value("DisplayCommander.XInputWidget", "LeftStickYMinInput",
                                                g_shared_state->left_stick_y_min_input.load());
    display_commander::config::set_config_value("DisplayCommander.XInputWidget", "LeftStickYMaxInput",
                                                g_shared_state->left_stick_y_max_input.load());
    display_commander::config::set_config_value("DisplayCommander.XInputWidget", "LeftStickYMinOutput",
                                                g_shared_state->left_stick_y_min_output.load());
    display_commander::config::set_config_value("DisplayCommander.XInputWidget", "LeftStickYMaxOutput",
                                                g_shared_state->left_stick_y_max_output.load());
    display_commander::config::set_config_value("DisplayCommander.XInputWidget", "RightStickXMinInput",
                                                g_shared_state->right_stick_x_min_input.load());
    display_commander::config::set_config_value("DisplayCommander.XInputWidget", "RightStickXMaxInput",
                                                g_shared_state->right_stick_x_max_input.load());
    display_commander::config::set_config_value("DisplayCommander.XInputWidget", "RightStickXMinOutput",
                                                g_shared_state->right_stick_x_min_output.load());
    display_commander::config::set_config_value("DisplayCommander.XInputWidget", "RightStickXMaxOutput",
                                                g_shared_state->right_stick_x_max_output.load());
    display_commander::config::set_config_value("DisplayCommander.XInputWidget", "RightStickYMinInput",
                                                g_shared_state->right_stick_y_min_input.load());
    display_commander::config::set_config_value("DisplayCommander.XInputWidget", "RightStickYMaxInput",
                                                g_shared_state->right_stick_y_max_input.load());
    display_commander::config::set_config_value("DisplayCommander.XInputWidget", "RightStickYMinOutput",
                                                g_shared_state->right_stick_y_min_output.load());
    display_commander::config::set_config_value("DisplayCommander.XInputWidget", "RightStickYMaxOutput",
                                                g_shared_state->right_stick_y_max_output.load());
    display_commander::config::set_config_value("DisplayCommander.XInputWidget", "LeftStickSameAxes",
                                                g_shared_state->left_stick_same_axes.load());
    display_commander::config::set_config_value("DisplayCommander.XInputWidget", "RightStickSameAxes",
                                                g_shared_state->right_stick_same_axes.load());

    // Save stick center calibration settings
    display_commander::config::set_config_value("DisplayCommander.XInputWidget", "LeftStickCenterX",
                                                g_shared_state->left_stick_center_x.load());

    display_commander::config::set_config_value("DisplayCommander.XInputWidget", "LeftStickCenterY",
                                                g_shared_state->left_stick_center_y.load());

    display_commander::config::set_config_value("DisplayCommander.XInputWidget", "RightStickCenterX",
                                                g_shared_state->right_stick_center_x.load());

    display_commander::config::set_config_value("DisplayCommander.XInputWidget", "RightStickCenterY",
                                                g_shared_state->right_stick_center_y.load());

    // Save vibration amplification setting
    display_commander::config::set_config_value("DisplayCommander.XInputWidget", "VibrationAmplification",
                                                g_shared_state->vibration_amplification.load());

    // Save stick processing mode settings
    display_commander::config::set_config_value("DisplayCommander.XInputWidget", "LeftStickCircular",
                                                g_shared_state->left_stick_circular.load());

    display_commander::config::set_config_value("DisplayCommander.XInputWidget", "RightStickCircular",
                                                g_shared_state->right_stick_circular.load());

    // Save autofire settings
    display_commander::config::set_config_value("DisplayCommander.XInputWidget", "AutofireEnabled",
                                                g_shared_state->autofire_enabled.load());

    display_commander::config::set_config_value(
        "DisplayCommander.XInputWidget", "AutofireHoldDownFrames",
        static_cast<uint32_t>(g_shared_state->autofire_hold_down_frames.load()));
    display_commander::config::set_config_value("DisplayCommander.XInputWidget", "AutofireHoldUpFrames",
                                                static_cast<uint32_t>(g_shared_state->autofire_hold_up_frames.load()));

    // Save autofire button list as comma-separated hex values
    utils::SRWLockExclusive lock(g_shared_state->autofire_lock);

    std::string autofire_buttons_str;
    for (const auto& af_button : g_shared_state->autofire_buttons) {
        if (!autofire_buttons_str.empty()) {
            autofire_buttons_str += ",";
        }
        char hex_str[16];
        sprintf_s(hex_str, "%04X", af_button.button_mask);
        autofire_buttons_str += hex_str;
    }

    display_commander::config::set_config_value("DisplayCommander.XInputWidget", "AutofireButtons",
                                                autofire_buttons_str);

    // Save autofire trigger list as comma-separated trigger names (LT, RT)
    std::string autofire_triggers_str;
    for (const auto& af_trigger : g_shared_state->autofire_triggers) {
        if (!autofire_triggers_str.empty()) {
            autofire_triggers_str += ",";
        }
        if (af_trigger.trigger_type == XInputSharedState::TriggerType::LeftTrigger) {
            autofire_triggers_str += "LT";
        } else if (af_trigger.trigger_type == XInputSharedState::TriggerType::RightTrigger) {
            autofire_triggers_str += "RT";
        }
    }

    display_commander::config::set_config_value("DisplayCommander.XInputWidget", "AutofireTriggers",
                                                autofire_triggers_str);
}

std::shared_ptr<XInputSharedState> XInputWidget::GetSharedState() { return g_shared_state; }

// Global functions for integration
void InitializeXInputWidget() {
    if (!g_xinput_widget) {
        g_xinput_widget = std::make_unique<XInputWidget>();
        g_xinput_widget->Initialize();

        // Initialize UI state - assume closed initially
        auto shared_state = XInputWidget::GetSharedState();
        if (shared_state) {
            shared_state->ui_overlay_open.store(false);
        }
    }
}

void CleanupXInputWidget() {
    if (g_xinput_widget) {
        g_xinput_widget->Cleanup();
        g_xinput_widget.reset();
    }
}

void DrawXInputWidget(display_commander::ui::IImGuiWrapper& imgui) {
    if (g_xinput_widget) {
        g_xinput_widget->OnDraw(imgui);
    }
}

namespace {
constexpr uint64_t kActiveInputApiWindowNs = 10ULL * 1000000000ULL;  // 10 seconds
}  // namespace

void DrawActiveInputApisSection(display_commander::ui::IImGuiWrapper& imgui) {
    const uint64_t now_ns = utils::get_now_ns();
    const uint64_t window_ns = kActiveInputApiWindowNs;
    std::vector<std::string> active_names =
        display_commanderhooks::InputActivityStats::GetInstance().GetActiveApiNames(now_ns, window_ns);

    if (imgui.CollapsingHeader("Active input APIs (last 10s)", ImGuiTreeNodeFlags_DefaultOpen)) {
        imgui.Indent();
        if (imgui.IsItemHovered()) {
            imgui.SetTooltip(
                "APIs the game has used recently (at least one call in the last 10 seconds). "
                "Similar to Special K input API display.");
        }
        if (active_names.empty()) {
            imgui.TextColored(::ui::colors::TEXT_DIMMED, "None (no input API calls in the last 10 seconds)");
        } else {
            for (size_t i = 0; i < active_names.size(); ++i) {
                if (i > 0) {
                    imgui.SameLine();
                    imgui.TextColored(::ui::colors::TEXT_SUBTLE, " | ");
                    imgui.SameLine();
                }
                imgui.TextColored(::ui::colors::TEXT_DEFAULT, "%s", active_names[i].c_str());
            }
        }
        if (display_commanderhooks::g_wgi_state.wgi_suppressed_ever.load()) {
            imgui.Spacing();
            imgui.TextColored(::ui::colors::ICON_WARNING, "WindowsGamingInput was suppressed");
            if (imgui.IsItemHovered()) {
                imgui.SetTooltip(
                    "The game requested Windows.Gaming.Input but it was blocked (E_NOTIMPL). Game may use XInput "
                    "instead.");
            }
        }
        imgui.Unindent();
    }
}

namespace {
// For GetState(0) polling rate: rolling 1s window
uint64_t g_last_getstate0_count = 0;
uint64_t g_last_getstate0_tick_ms = 0;
float g_getstate0_rate_hz = 0.0f;
}  // namespace

void DrawControllerPollingRatesSection(display_commander::ui::IImGuiWrapper& imgui) {
    if (!imgui.CollapsingHeader("Input polling rates", ImGuiTreeNodeFlags_DefaultOpen)) {
        return;
    }
    imgui.Indent();
    if (imgui.IsItemHovered()) {
        imgui.SetTooltip("XInput GetState(0) calls/sec (game polling) and DualSense HID report rate (addon reading).");
    }

    // XInput GetState(0) rate (use original tick so time-slowdown doesn't skew rate)
    const uint64_t getstate0_calls = display_commanderhooks::GetXInputGetStateUserIndexZeroCallCount();
    const uint64_t now_ms = GetOriginalTickCount64();
    if (g_last_getstate0_tick_ms == 0) {
        g_last_getstate0_tick_ms = now_ms;
        g_last_getstate0_count = getstate0_calls;
    }
    const uint64_t elapsed_ms = now_ms - g_last_getstate0_tick_ms;
    if (elapsed_ms >= 1000) {
        const uint64_t delta = (getstate0_calls >= g_last_getstate0_count) ? (getstate0_calls - g_last_getstate0_count) : 0;
        g_getstate0_rate_hz = (elapsed_ms > 0) ? (1000.0f * static_cast<float>(delta) / static_cast<float>(elapsed_ms)) : 0.0f;
        g_last_getstate0_count = getstate0_calls;
        g_last_getstate0_tick_ms = now_ms;
    }
    imgui.Text("XInput GetState(0): %.1f/sec (total: %llu)", g_getstate0_rate_hz,
               static_cast<unsigned long long>(getstate0_calls));
    if (imgui.IsItemHovered()) {
        imgui.SetTooltip("Game (or addon) calls to XInputGetState(0) per second.");
    }

    // DualSense HID report rate (first device if any)
    if (display_commander::dualsense::g_dualsense_hid_wrapper) {
        const auto& devices = display_commander::dualsense::g_dualsense_hid_wrapper->GetDevices();
        if (devices.empty()) {
            imgui.TextColored(::ui::colors::TEXT_DIMMED, "DualSense HID: no devices");
        } else {
            const auto& dev = devices[0];
            if (dev.packet_rate_ever_called) {
                imgui.Text("DualSense HID: %.1f reports/sec", dev.last_packet_rate_hz);
            } else {
                imgui.TextColored(::ui::colors::TEXT_DIMMED, "DualSense HID: never");
            }
            if (imgui.IsItemHovered()) {
                imgui.SetTooltip("Addon ReadFile rate for first DualSense (packet-number-changed branch).");
            }
        }
    } else {
        imgui.TextColored(::ui::colors::TEXT_DIMMED, "DualSense HID: not initialized");
    }

    imgui.Unindent();
}

void DrawControllerTab(display_commander::ui::IImGuiWrapper& imgui) {
    DrawActiveInputApisSection(imgui);
    ImGui::Spacing();
    DrawControllerPollingRatesSection(imgui);
    ImGui::Spacing();
    DrawXInputWidget(imgui);
    ImGui::Spacing();
    display_commander::widgets::remapping_widget::DrawRemappingWidget(imgui);
}

// Global functions for hooks to use
void UpdateXInputState(DWORD user_index, const XINPUT_STATE* state) {
    auto shared_state = XInputWidget::GetSharedState();
    if (!shared_state || user_index >= XUSER_MAX_COUNT || !state) {
        return;
    }

    // Thread-safe update
    utils::SRWLockExclusive lock(shared_state->state_lock);

    // Update controller state
    shared_state->controller_states[user_index] = *state;
    shared_state->controller_connected[user_index] = ControllerState::Connected;
    shared_state->last_packet_numbers[user_index] = state->dwPacketNumber;
    shared_state->last_update_times[user_index] = GetOriginalTickCount64();

    // Increment event counters
    shared_state->total_events.fetch_add(1);
}

void IncrementEventCounter(const std::string& event_type) {
    auto shared_state = XInputWidget::GetSharedState();
    if (!shared_state) return;

    if (event_type == "button") {
        shared_state->button_events.fetch_add(1);
    } else if (event_type == "stick") {
        shared_state->stick_events.fetch_add(1);
    } else if (event_type == "trigger") {
        shared_state->trigger_events.fetch_add(1);
    }
}

// Vibration test functions
void XInputWidget::TestLeftMotor() {
    if (selected_controller_ < 0 || selected_controller_ >= XUSER_MAX_COUNT) {
        LogError("XInputWidget::TestLeftMotor() - Invalid controller index: %d", selected_controller_);
        return;
    }

    display_commanderhooks::EnsureXInputSetStateForTest();

    XINPUT_VIBRATION vibration = {};
    vibration.wLeftMotorSpeed = 65535;  // Maximum intensity
    vibration.wRightMotorSpeed = 0;     // Right motor off

    // Use detour so DualSense-as-XInput (slot 0) gets HID rumble; otherwise uses original XInput.
    DWORD result = display_commanderhooks::XInputSetState_Detour(selected_controller_, &vibration);
    if (result == ERROR_SUCCESS) {
        vibration_test_start_ns_ = utils::get_now_ns();
        LogInfo("XInputWidget::TestLeftMotor() - Left motor test started for controller %d", selected_controller_);
    } else {
        LogError("XInputWidget::TestLeftMotor() - Failed to set vibration for controller %d, error: %lu",
                 selected_controller_, result);
    }
}

void XInputWidget::TestRightMotor() {
    if (selected_controller_ < 0 || selected_controller_ >= XUSER_MAX_COUNT) {
        LogError("XInputWidget::TestRightMotor() - Invalid controller index: %d", selected_controller_);
        return;
    }

    display_commanderhooks::EnsureXInputSetStateForTest();

    XINPUT_VIBRATION vibration = {};
    vibration.wLeftMotorSpeed = 0;       // Left motor off
    vibration.wRightMotorSpeed = 65535;  // Maximum intensity

    // Use detour so DualSense-as-XInput (slot 0) gets HID rumble; otherwise uses original XInput.
    DWORD result = display_commanderhooks::XInputSetState_Detour(selected_controller_, &vibration);
    if (result == ERROR_SUCCESS) {
        vibration_test_start_ns_ = utils::get_now_ns();
        LogInfo("XInputWidget::TestRightMotor() - Right motor test started for controller %d", selected_controller_);
    } else {
        LogError("XInputWidget::TestRightMotor() - Failed to set vibration for controller %d, error: %lu",
                 selected_controller_, result);
    }
}

void XInputWidget::StopVibration() {
    if (selected_controller_ < 0 || selected_controller_ >= XUSER_MAX_COUNT) {
        LogError("XInputWidget::StopVibration() - Invalid controller index: %d", selected_controller_);
        return;
    }

    display_commanderhooks::EnsureXInputSetStateForTest();

    XINPUT_VIBRATION vibration = {};
    vibration.wLeftMotorSpeed = 0;  // Both motors off
    vibration.wRightMotorSpeed = 0;

    vibration_test_start_ns_ = 0;  // Clear timer so UI stops showing countdown
    // Use detour so DualSense-as-XInput (slot 0) gets HID rumble stop; otherwise uses original XInput.
    DWORD result = display_commanderhooks::XInputSetState_Detour(selected_controller_, &vibration);
    if (result == ERROR_SUCCESS) {
        LogInfo("XInputWidget::StopVibration() - Vibration stopped for controller %d", selected_controller_);
    } else {
        LogError("XInputWidget::StopVibration() - Failed to stop vibration for controller %d, error: %lu",
                 selected_controller_, result);
    }
}

void CheckAndHandleScreenshot() {
    try {
        auto shared_state = XInputWidget::GetSharedState();
        if (!shared_state) return;

        // Check if screenshot should be triggered
        if (shared_state->trigger_screenshot.load()) {
            // Reset the flag
            shared_state->trigger_screenshot.store(false);

            // Get the ReShade runtime instance
            reshade::api::effect_runtime* runtime = GetFirstReShadeRuntime();

            if (runtime != nullptr) {
                // Use PrintScreen key simulation to trigger ReShade's built-in screenshot system
                // This is the safest and most reliable method
                try {
                    LogInfo("XXX Triggering ReShade screenshot via PrintScreen key simulation");

                    // Simulate PrintScreen key press to trigger ReShade's screenshot
                    INPUT input = {};
                    input.type = INPUT_KEYBOARD;
                    input.ki.wVk = VK_SNAPSHOT;  // PrintScreen key
                    input.ki.dwFlags = 0;        // Key down

                    // Send key down
                    UINT result = SendInput(1, &input, sizeof(INPUT));
                    if (result == 0) {
                        LogError("XXX SendInput failed for key down, error: %lu", GetLastError());
                    }

                    // Small delay to ensure the key press is registered
                    Sleep(50);

                    // Send key up
                    input.ki.dwFlags = KEYEVENTF_KEYUP;
                    result = SendInput(1, &input, sizeof(INPUT));
                    if (result == 0) {
                        LogError("XXX SendInput failed for key up, error: %lu", GetLastError());
                    }

                    LogInfo("XXX PrintScreen key simulation completed successfully");

                } catch (const std::exception& e) {
                    LogError("XXX Exception in PrintScreen simulation: %s", e.what());
                } catch (...) {
                    LogError("XXX Unknown exception in PrintScreen simulation");
                }
            } else {
                LogError("XXX ReShade runtime not available for screenshot");
            }
        }
    } catch (const std::exception& e) {
        LogError("XXX Exception in CheckAndHandleScreenshot: %s", e.what());
    } catch (...) {
        LogError("XXX Unknown exception in CheckAndHandleScreenshot");
    }
}

// Global function to update battery status for a controller
void UpdateBatteryStatus(DWORD user_index) {
    if (user_index >= XUSER_MAX_COUNT) {
        return;
    }

    auto shared_state = XInputWidget::GetSharedState();
    if (!shared_state) {
        return;
    }

    // Check if we need to update battery status (update every 5 seconds)
    auto current_time = GetOriginalTickCount64();
    auto last_update = shared_state->last_battery_update_times[user_index].load();

    if (current_time - last_update < 5000) {  // 5 seconds
        return;
    }

    // Update battery information for gamepad
    XINPUT_BATTERY_INFORMATION battery_info = {};
    DWORD result = display_commanderhooks::XInputGetBatteryInformation_Direct
                       ? display_commanderhooks::XInputGetBatteryInformation_Direct(user_index, BATTERY_DEVTYPE_GAMEPAD,
                                                                                    &battery_info)
                       : ERROR_DEVICE_NOT_CONNECTED;

    if (result == ERROR_SUCCESS) {
        shared_state->battery_info[user_index] = battery_info;
        shared_state->battery_info_valid[user_index] = true;
        shared_state->last_battery_update_times[user_index] = current_time;

        LogInfo("XXX Controller %lu battery: Type=%d, Level=%d", user_index, battery_info.BatteryType,
                battery_info.BatteryLevel);
    } else {
        // Mark battery info as invalid if we can't get it
        shared_state->battery_info_valid[user_index] = false;
        static int debug_count = 0;
        if (debug_count++ < 3) {
            LogWarn("XXX Failed to get battery info for controller %lu: %lu", user_index, result);
        }
    }
}

void XInputWidget::DrawDualSenseReport(display_commander::ui::IImGuiWrapper& imgui, int controller_index) {
    (void)controller_index;
    // Check if DualSense HID wrapper is available
    if (!display_commander::dualsense::g_dualsense_hid_wrapper) {
        imgui.TextColored(::ui::colors::TEXT_DIMMED, "DualSense HID wrapper not available");
        return;
    }

    // Get devices from HID wrapper
    const auto& devices = display_commander::dualsense::g_dualsense_hid_wrapper->GetDevices();
    if (devices.empty()) {
        imgui.TextColored(::ui::colors::TEXT_DIMMED, "No DualSense devices detected");
        return;
    }

    // Find the device that corresponds to the selected controller
    const auto& device = devices[0];
    if (!device.is_connected) {
        imgui.TextColored(::ui::colors::TEXT_DIMMED, "DualSense device not connected");
        return;
    }

    // Display basic device info
    imgui.TextColored(::ui::colors::STATUS_ACTIVE, "Device: %s",
                      device.device_name.empty() ? "DualSense Controller" : device.device_name.c_str());
    imgui.Text("Connection: %s", device.connection_type.c_str());
    imgui.Text("Vendor ID: 0x%04X", device.vendor_id);
    imgui.Text("Product ID: 0x%04X", device.product_id);

    // Display last update time
    if (device.last_update_time > 0) {
        DWORD now = GetTickCount();
        DWORD age_ms = now - device.last_update_time;
        imgui.Text("Last Update: %lu ms ago", age_ms);
    }

    imgui.Spacing();

    // Display input report size and first few bytes
    if (device.hid_device && device.hid_device->input_report.size() > 0) {
        imgui.Text("Input Report Size: %zu bytes", device.hid_device->input_report.size());

        // Show first 16 bytes in hex format
        const auto& inputReport = device.hid_device->input_report;
        std::string hex_string = "";
        for (size_t i = 0; i < (inputReport.size() < 16 ? inputReport.size() : 16); ++i) {
            char hex_byte[4];
            sprintf_s(hex_byte, "%02X ", inputReport[i]);
            hex_string += hex_byte;
        }
        imgui.Text("First 16 bytes: %s", hex_string.c_str());

        imgui.Spacing();

        // Display DualSense data if available
        if (imgui.CollapsingHeader("DualSense Data", 0)) {
            imgui.Indent();
            const auto& sk_data = device.sk_dualsense_data;

            // Basic input data
            if (imgui.CollapsingHeader("Input Data", 0)) {
                imgui.Indent();
                imgui.Columns(2, "SKInputColumns", false);

                // Sticks
                imgui.Text("Left Stick: X=%d, Y=%d", sk_data.LeftStickX, sk_data.LeftStickY);
                imgui.NextColumn();
                imgui.Text("Right Stick: X=%d, Y=%d", sk_data.RightStickX, sk_data.RightStickY);
                imgui.NextColumn();

                // Triggers
                imgui.Text("Left Trigger: %d", sk_data.TriggerLeft);
                imgui.NextColumn();
                imgui.Text("Right Trigger: %d", sk_data.TriggerRight);
                imgui.NextColumn();

                // D-pad
                const char* dpad_names[] = {"Up",        "Up-Right", "Right",   "Down-Right", "Down",
                                            "Down-Left", "Left",     "Up-Left", "None"};
                imgui.Text("D-Pad: %s", dpad_names[static_cast<int>(sk_data.DPad)]);
                imgui.NextColumn();
                imgui.Text("Sequence: %d", sk_data.SeqNo);
                imgui.NextColumn();

                imgui.Columns(1);
                imgui.Unindent();
            }

            // Button states
            if (imgui.CollapsingHeader("Button States", 0)) {
                imgui.Indent();
                imgui.Columns(3, "SKButtonColumns", false);

                // Face buttons
                imgui.Text("Square: %s", sk_data.ButtonSquare ? "PRESSED" : "Released");
                imgui.NextColumn();
                imgui.Text("Cross: %s", sk_data.ButtonCross ? "PRESSED" : "Released");
                imgui.NextColumn();
                imgui.Text("Circle: %s", sk_data.ButtonCircle ? "PRESSED" : "Released");
                imgui.NextColumn();
                imgui.Text("Triangle: %s", sk_data.ButtonTriangle ? "PRESSED" : "Released");
                imgui.NextColumn();

                // Shoulder buttons
                imgui.Text("L1: %s", sk_data.ButtonL1 ? "PRESSED" : "Released");
                imgui.NextColumn();
                imgui.Text("R1: %s", sk_data.ButtonR1 ? "PRESSED" : "Released");
                imgui.NextColumn();
                imgui.Text("L2: %s", sk_data.ButtonL2 ? "PRESSED" : "Released");
                imgui.NextColumn();
                imgui.Text("R2: %s", sk_data.ButtonR2 ? "PRESSED" : "Released");
                imgui.NextColumn();

                // System buttons
                imgui.Text("Create: %s", sk_data.ButtonCreate ? "PRESSED" : "Released");
                imgui.NextColumn();
                imgui.Text("Options: %s", sk_data.ButtonOptions ? "PRESSED" : "Released");
                imgui.NextColumn();
                imgui.Text("L3: %s", sk_data.ButtonL3 ? "PRESSED" : "Released");
                imgui.NextColumn();
                imgui.Text("R3: %s", sk_data.ButtonR3 ? "PRESSED" : "Released");
                imgui.NextColumn();
                imgui.Text("Home: %s", sk_data.ButtonHome ? "PRESSED" : "Released");
                imgui.NextColumn();
                imgui.Text("Touchpad: %s", sk_data.ButtonPad ? "PRESSED" : "Released");
                imgui.NextColumn();
                imgui.Text("Mute: %s", sk_data.ButtonMute ? "PRESSED" : "Released");
                imgui.NextColumn();

                // Edge controller buttons
                if (sk_data.ButtonLeftFunction || sk_data.ButtonRightFunction || sk_data.ButtonLeftPaddle
                    || sk_data.ButtonRightPaddle) {
                    imgui.Text("Left Function: %s", sk_data.ButtonLeftFunction ? "PRESSED" : "Released");
                    imgui.NextColumn();
                    imgui.Text("Right Function: %s", sk_data.ButtonRightFunction ? "PRESSED" : "Released");
                    imgui.NextColumn();
                    imgui.Text("Left Paddle: %s", sk_data.ButtonLeftPaddle ? "PRESSED" : "Released");
                    imgui.NextColumn();
                    imgui.Text("Right Paddle: %s", sk_data.ButtonRightPaddle ? "PRESSED" : "Released");
                    imgui.NextColumn();
                }

                imgui.Columns(1);
                imgui.Unindent();
            }

            // Motion sensors
            if (imgui.CollapsingHeader("Motion Sensors")) {
                imgui.Indent();
                imgui.Columns(2, "SKMotionColumns", false);

                imgui.Text("Angular Velocity X: %d", sk_data.AngularVelocityX);
                imgui.NextColumn();
                imgui.Text("Angular Velocity Y: %d", sk_data.AngularVelocityY);
                imgui.NextColumn();
                imgui.Text("Angular Velocity Z: %d", sk_data.AngularVelocityZ);
                imgui.NextColumn();
                imgui.Text("Accelerometer X: %d", sk_data.AccelerometerX);
                imgui.NextColumn();
                imgui.Text("Accelerometer Y: %d", sk_data.AccelerometerY);
                imgui.NextColumn();
                imgui.Text("Accelerometer Z: %d", sk_data.AccelerometerZ);
                imgui.NextColumn();
                imgui.Text("Temperature: %d°C", sk_data.Temperature);
                imgui.NextColumn();
                imgui.Text("Sensor Timestamp: %u", sk_data.SensorTimestamp);
                imgui.NextColumn();

                imgui.Columns(1);
                imgui.Unindent();
            }

            // Adaptive triggers
            if (imgui.CollapsingHeader("Adaptive Triggers")) {
                imgui.Indent();
                imgui.Columns(2, "SKTriggerColumns", false);

                imgui.Text("Left Trigger Status: %d", sk_data.TriggerLeftStatus);
                imgui.NextColumn();
                imgui.Text("Right Trigger Status: %d", sk_data.TriggerRightStatus);
                imgui.NextColumn();
                imgui.Text("Left Stop Location: %d", sk_data.TriggerLeftStopLocation);
                imgui.NextColumn();
                imgui.Text("Right Stop Location: %d", sk_data.TriggerRightStopLocation);
                imgui.NextColumn();
                imgui.Text("Left Effect: %d", sk_data.TriggerLeftEffect);
                imgui.NextColumn();
                imgui.Text("Right Effect: %d", sk_data.TriggerRightEffect);
                imgui.NextColumn();

                imgui.Columns(1);
                imgui.Unindent();
            }

            // Timestamps
            if (imgui.CollapsingHeader("Timestamps")) {
                imgui.Indent();
                imgui.Text("Host Timestamp: %u", sk_data.HostTimestamp);
                imgui.Text("Device Timestamp: %u", sk_data.DeviceTimeStamp);
                imgui.Text("Sensor Timestamp: %u", sk_data.SensorTimestamp);
                imgui.Unindent();
            }
            imgui.Unindent();
        }
    } else {
        imgui.TextColored(::ui::colors::TEXT_DIMMED, "No input report data available");
    }
}

// Autofire functions
void XInputWidget::DrawAutofireSettings(display_commander::ui::IImGuiWrapper& imgui) {
    auto shared_state = GetSharedState();
    if (!shared_state) {
        return;
    }

    // Master enable/disable
    bool autofire_enabled = shared_state->autofire_enabled.load();
    if (imgui.Checkbox("Enable Autofire", &autofire_enabled)) {
        shared_state->autofire_enabled.store(autofire_enabled);

        // When disabling autofire, reset all autofire button and trigger states
        if (!autofire_enabled) {
            utils::SRWLockExclusive lock(shared_state->autofire_lock);
            for (auto& af_button : shared_state->autofire_buttons) {
                af_button.is_holding_down.store(true);
                af_button.phase_start_frame_id.store(0);
            }
            for (auto& af_trigger : shared_state->autofire_triggers) {
                af_trigger.is_holding_down.store(true);
                af_trigger.phase_start_frame_id.store(0);
            }
        }

        SaveSettings();
    }
    if (imgui.IsItemHovered()) {
        imgui.SetTooltip(
            "Enable autofire for selected buttons. When a button is held, it will cycle between hold down and hold "
            "up phases.");
    }

    if (autofire_enabled) {
        imgui.Spacing();

        // Hold down frames setting
        uint32_t hold_down_frames = shared_state->autofire_hold_down_frames.load();
        int hold_down_frames_int = static_cast<int>(hold_down_frames);

        imgui.Text("Hold Down (frames): %d", hold_down_frames_int);
        imgui.SameLine();

        // Input field for precise control
        imgui.SetNextItemWidth(80);
        if (imgui.InputInt("##HoldDownFramesInput", &hold_down_frames_int, 1, 5,
                           ImGuiInputTextFlags_EnterReturnsTrue)) {
            if (hold_down_frames_int < 1) {
                hold_down_frames_int = 1;
            } else if (hold_down_frames_int > 1000) {
                hold_down_frames_int = 1000;
            }
            shared_state->autofire_hold_down_frames.store(static_cast<uint32_t>(hold_down_frames_int));
            SaveSettings();
        }
        if (imgui.IsItemHovered()) {
            imgui.SetTooltip("Number of frames to hold button down (1-1000). Enter a value or use slider below.");
        }

        // Slider for quick adjustment (1-60 range for common use)
        int slider_value_down = hold_down_frames_int;
        if (slider_value_down > 60) slider_value_down = 60;  // Clamp for slider display
        if (imgui.SliderInt("##HoldDownFramesSlider", &slider_value_down, 1, 60, "%d frames")) {
            shared_state->autofire_hold_down_frames.store(static_cast<uint32_t>(slider_value_down));
            SaveSettings();
        }
        if (imgui.IsItemHovered()) {
            imgui.SetTooltip("Quick adjustment slider (1-60 frames). Use input field above for values > 60.");
        }

        imgui.Spacing();

        // Hold up frames setting
        uint32_t hold_up_frames = shared_state->autofire_hold_up_frames.load();
        int hold_up_frames_int = static_cast<int>(hold_up_frames);

        imgui.Text("Hold Up (frames): %d", hold_up_frames_int);
        imgui.SameLine();

        // Input field for precise control
        imgui.SetNextItemWidth(80);
        if (imgui.InputInt("##HoldUpFramesInput", &hold_up_frames_int, 1, 5, ImGuiInputTextFlags_EnterReturnsTrue)) {
            if (hold_up_frames_int < 1) {
                hold_up_frames_int = 1;
            } else if (hold_up_frames_int > 1000) {
                hold_up_frames_int = 1000;
            }
            shared_state->autofire_hold_up_frames.store(static_cast<uint32_t>(hold_up_frames_int));
            SaveSettings();
        }
        if (imgui.IsItemHovered()) {
            imgui.SetTooltip("Number of frames to hold button up (1-1000). Enter a value or use slider below.");
        }

        // Slider for quick adjustment (1-60 range for common use)
        int slider_value_up = hold_up_frames_int;
        if (slider_value_up > 60) slider_value_up = 60;  // Clamp for slider display
        if (imgui.SliderInt("##HoldUpFramesSlider", &slider_value_up, 1, 60, "%d frames")) {
            shared_state->autofire_hold_up_frames.store(static_cast<uint32_t>(slider_value_up));
            SaveSettings();
        }
        if (imgui.IsItemHovered()) {
            imgui.SetTooltip("Quick adjustment slider (1-60 frames). Use input field above for values > 60.");
        }

        // Show effective rate information
        uint32_t total_cycle_frames = hold_down_frames + hold_up_frames;
        if (total_cycle_frames > 0) {
            imgui.TextColored(::ui::colors::TEXT_DIMMED,
                              "  Cycle: %u frames total | At 60 FPS: ~%.1f cycles/sec | At 120 FPS: ~%.1f cycles/sec",
                              total_cycle_frames, 60.0f / total_cycle_frames, 120.0f / total_cycle_frames);
        }

        imgui.Spacing();
        imgui.Separator();
        imgui.Text("Select buttons for autofire:");

        // Button selection checkboxes
        const struct {
            WORD mask;
            const char* name;
        } buttons[] = {
            {XINPUT_GAMEPAD_A, "A"},
            {XINPUT_GAMEPAD_B, "B"},
            {XINPUT_GAMEPAD_X, "X"},
            {XINPUT_GAMEPAD_Y, "Y"},
            {XINPUT_GAMEPAD_LEFT_SHOULDER, "LB"},
            {XINPUT_GAMEPAD_RIGHT_SHOULDER, "RB"},
            {XINPUT_GAMEPAD_BACK, "View"},
            {XINPUT_GAMEPAD_START, "Menu"},
            {XINPUT_GAMEPAD_LEFT_THUMB, "LS"},
            {XINPUT_GAMEPAD_RIGHT_THUMB, "RS"},
            {XINPUT_GAMEPAD_DPAD_UP, "D-Up"},
            {XINPUT_GAMEPAD_DPAD_DOWN, "D-Down"},
            {XINPUT_GAMEPAD_DPAD_LEFT, "D-Left"},
            {XINPUT_GAMEPAD_DPAD_RIGHT, "D-Right"},
        };

        // Helper lambda to check if button exists (thread-safe)
        auto is_autofire_button = [&shared_state](WORD button_mask) -> bool {
            utils::SRWLockShared lock(shared_state->autofire_lock);
            for (const auto& af_button : shared_state->autofire_buttons) {
                if (af_button.button_mask == button_mask) {
                    return true;
                }
            }
            return false;
        };

        // Display checkboxes in a grid
        for (size_t i = 0; i < sizeof(buttons) / sizeof(buttons[0]); i += 2) {
            if (i + 1 < sizeof(buttons) / sizeof(buttons[0])) {
                // Two buttons per row
                bool is_enabled1 = is_autofire_button(buttons[i].mask);
                bool is_enabled2 = is_autofire_button(buttons[i + 1].mask);

                if (imgui.Checkbox(buttons[i].name, &is_enabled1)) {
                    {
                        // Acquire lock only for modification
                        utils::SRWLockExclusive lock(shared_state->autofire_lock);
                        if (is_enabled1) {
                            // Add button
                            bool exists = false;
                            for (const auto& af_button : shared_state->autofire_buttons) {
                                if (af_button.button_mask == buttons[i].mask) {
                                    exists = true;
                                    break;
                                }
                            }
                            if (!exists) {
                                shared_state->autofire_buttons.push_back(
                                    XInputSharedState::AutofireButton(buttons[i].mask));
                                LogInfo("XInputWidget::DrawAutofireSettings() - Added autofire for button 0x%04X",
                                        buttons[i].mask);
                            }
                        } else {
                            // Remove button
                            auto it = std::remove_if(shared_state->autofire_buttons.begin(),
                                                     shared_state->autofire_buttons.end(),
                                                     [&buttons, i](const XInputSharedState::AutofireButton& af_button) {
                                                         return af_button.button_mask == buttons[i].mask;
                                                     });
                            shared_state->autofire_buttons.erase(it, shared_state->autofire_buttons.end());
                            LogInfo("XInputWidget::DrawAutofireSettings() - Removed autofire for button 0x%04X",
                                    buttons[i].mask);
                        }
                    }  // Lock released here
                    SaveSettings();
                }

                imgui.SameLine();

                if (imgui.Checkbox(buttons[i + 1].name, &is_enabled2)) {
                    {
                        // Acquire lock only for modification
                        utils::SRWLockExclusive lock(shared_state->autofire_lock);
                        if (is_enabled2) {
                            // Add button
                            bool exists = false;
                            for (const auto& af_button : shared_state->autofire_buttons) {
                                if (af_button.button_mask == buttons[i + 1].mask) {
                                    exists = true;
                                    break;
                                }
                            }
                            if (!exists) {
                                shared_state->autofire_buttons.push_back(
                                    XInputSharedState::AutofireButton(buttons[i + 1].mask));
                                LogInfo("XInputWidget::DrawAutofireSettings() - Added autofire for button 0x%04X",
                                        buttons[i + 1].mask);
                            }
                        } else {
                            // Remove button
                            auto it = std::remove_if(shared_state->autofire_buttons.begin(),
                                                     shared_state->autofire_buttons.end(),
                                                     [&buttons, i](const XInputSharedState::AutofireButton& af_button) {
                                                         return af_button.button_mask == buttons[i + 1].mask;
                                                     });
                            shared_state->autofire_buttons.erase(it, shared_state->autofire_buttons.end());
                            LogInfo("XInputWidget::DrawAutofireSettings() - Removed autofire for button 0x%04X",
                                    buttons[i + 1].mask);
                        }
                    }  // Lock released here
                    SaveSettings();
                }
            } else {
                // Single button on last row
                bool is_enabled = is_autofire_button(buttons[i].mask);
                if (imgui.Checkbox(buttons[i].name, &is_enabled)) {
                    {
                        // Acquire lock only for modification
                        utils::SRWLockExclusive lock(shared_state->autofire_lock);
                        if (is_enabled) {
                            // Add button
                            bool exists = false;
                            for (const auto& af_button : shared_state->autofire_buttons) {
                                if (af_button.button_mask == buttons[i].mask) {
                                    exists = true;
                                    break;
                                }
                            }
                            if (!exists) {
                                shared_state->autofire_buttons.push_back(
                                    XInputSharedState::AutofireButton(buttons[i].mask));
                                LogInfo("XInputWidget::DrawAutofireSettings() - Added autofire for button 0x%04X",
                                        buttons[i].mask);
                            }
                        } else {
                            // Remove button
                            auto it = std::remove_if(shared_state->autofire_buttons.begin(),
                                                     shared_state->autofire_buttons.end(),
                                                     [&buttons, i](const XInputSharedState::AutofireButton& af_button) {
                                                         return af_button.button_mask == buttons[i].mask;
                                                     });
                            shared_state->autofire_buttons.erase(it, shared_state->autofire_buttons.end());
                            LogInfo("XInputWidget::DrawAutofireSettings() - Removed autofire for button 0x%04X",
                                    buttons[i].mask);
                        }
                    }  // Lock released here
                    SaveSettings();
                }
            }
        }

        imgui.Spacing();
        imgui.Separator();
        imgui.Text("Select triggers for autofire:");

        // Trigger selection checkboxes
        bool is_lt_enabled = IsAutofireTrigger(XInputSharedState::TriggerType::LeftTrigger);
        bool is_rt_enabled = IsAutofireTrigger(XInputSharedState::TriggerType::RightTrigger);

        if (imgui.Checkbox("LT (Left Trigger)", &is_lt_enabled)) {
            if (is_lt_enabled) {
                AddAutofireTrigger(XInputSharedState::TriggerType::LeftTrigger);
                LogInfo("XInputWidget::DrawAutofireSettings() - Added autofire for LT");
            } else {
                RemoveAutofireTrigger(XInputSharedState::TriggerType::LeftTrigger);
                LogInfo("XInputWidget::DrawAutofireSettings() - Removed autofire for LT");
            }
            SaveSettings();
        }

        imgui.SameLine();

        if (imgui.Checkbox("RT (Right Trigger)", &is_rt_enabled)) {
            if (is_rt_enabled) {
                AddAutofireTrigger(XInputSharedState::TriggerType::RightTrigger);
                LogInfo("XInputWidget::DrawAutofireSettings() - Added autofire for RT");
            } else {
                RemoveAutofireTrigger(XInputSharedState::TriggerType::RightTrigger);
                LogInfo("XInputWidget::DrawAutofireSettings() - Removed autofire for RT");
            }
            SaveSettings();
        }
    }
}

void XInputWidget::AddAutofireButton(WORD button_mask) {
    auto shared_state = GetSharedState();
    if (!shared_state) {
        return;
    }

    // Thread-safe access
    utils::SRWLockExclusive lock(shared_state->autofire_lock);

    // Check if button already exists
    bool exists = false;
    for (const auto& af_button : shared_state->autofire_buttons) {
        if (af_button.button_mask == button_mask) {
            exists = true;
            break;
        }
    }

    if (!exists) {
        shared_state->autofire_buttons.push_back(XInputSharedState::AutofireButton(button_mask));
        LogInfo("XInputWidget::AddAutofireButton() - Added autofire for button 0x%04X", button_mask);
    }
}

void XInputWidget::RemoveAutofireButton(WORD button_mask) {
    auto shared_state = GetSharedState();
    if (!shared_state) {
        return;
    }

    // Thread-safe access
    utils::SRWLockExclusive lock(shared_state->autofire_lock);

    // Remove button from list
    auto it = std::remove_if(shared_state->autofire_buttons.begin(), shared_state->autofire_buttons.end(),
                             [button_mask](const XInputSharedState::AutofireButton& af_button) {
                                 return af_button.button_mask == button_mask;
                             });
    shared_state->autofire_buttons.erase(it, shared_state->autofire_buttons.end());

    LogInfo("XInputWidget::RemoveAutofireButton() - Removed autofire for button 0x%04X", button_mask);
}

bool XInputWidget::IsAutofireButton(WORD button_mask) const {
    auto shared_state = GetSharedState();
    if (!shared_state) {
        return false;
    }

    // Thread-safe access
    utils::SRWLockExclusive lock(shared_state->autofire_lock);

    bool found = false;
    for (const auto& af_button : shared_state->autofire_buttons) {
        if (af_button.button_mask == button_mask) {
            found = true;
            break;
        }
    }

    return found;
}

void XInputWidget::AddAutofireTrigger(XInputSharedState::TriggerType trigger_type) {
    auto shared_state = GetSharedState();
    if (!shared_state) {
        return;
    }

    // Thread-safe access
    utils::SRWLockExclusive lock(shared_state->autofire_lock);

    // Check if trigger already exists
    bool exists = false;
    for (const auto& af_trigger : shared_state->autofire_triggers) {
        if (af_trigger.trigger_type == trigger_type) {
            exists = true;
            break;
        }
    }

    if (!exists) {
        shared_state->autofire_triggers.push_back(XInputSharedState::AutofireTrigger(trigger_type));
        LogInfo("XInputWidget::AddAutofireTrigger() - Added autofire for trigger %s",
                trigger_type == XInputSharedState::TriggerType::LeftTrigger ? "LT" : "RT");
    }
}

void XInputWidget::RemoveAutofireTrigger(XInputSharedState::TriggerType trigger_type) {
    auto shared_state = GetSharedState();
    if (!shared_state) {
        return;
    }

    // Thread-safe access
    utils::SRWLockExclusive lock(shared_state->autofire_lock);

    // Remove trigger from list
    auto it = std::remove_if(shared_state->autofire_triggers.begin(), shared_state->autofire_triggers.end(),
                             [trigger_type](const XInputSharedState::AutofireTrigger& af_trigger) {
                                 return af_trigger.trigger_type == trigger_type;
                             });
    shared_state->autofire_triggers.erase(it, shared_state->autofire_triggers.end());

    LogInfo("XInputWidget::RemoveAutofireTrigger() - Removed autofire for trigger %s",
            trigger_type == XInputSharedState::TriggerType::LeftTrigger ? "LT" : "RT");
}

bool XInputWidget::IsAutofireTrigger(XInputSharedState::TriggerType trigger_type) const {
    auto shared_state = GetSharedState();
    if (!shared_state) {
        return false;
    }

    // Thread-safe access
    utils::SRWLockShared lock(shared_state->autofire_lock);

    bool found = false;
    for (const auto& af_trigger : shared_state->autofire_triggers) {
        if (af_trigger.trigger_type == trigger_type) {
            found = true;
            break;
        }
    }

    return found;
}

// Global function for hooks to use
void ProcessAutofire(DWORD user_index, XINPUT_STATE* pState) {
    if (!pState) {
        return;
    }

    auto shared_state = XInputWidget::GetSharedState();
    if (!shared_state) {
        return;
    }

    // Check if autofire is enabled
    bool autofire_enabled = shared_state->autofire_enabled.load();
    if (!autofire_enabled) {
        // When autofire is disabled, reset all autofire button and trigger states to prevent stale state
        utils::SRWLockExclusive lock(shared_state->autofire_lock);
        for (auto& af_button : shared_state->autofire_buttons) {
            af_button.is_holding_down.store(true);
            af_button.phase_start_frame_id.store(0);
        }
        for (auto& af_trigger : shared_state->autofire_triggers) {
            af_trigger.is_holding_down.store(true);
            af_trigger.phase_start_frame_id.store(0);
        }
        return;
    }

    // Get current frame ID
    uint64_t current_frame_id = g_global_frame_id.load();
    uint32_t hold_down_frames = shared_state->autofire_hold_down_frames.load();
    uint32_t hold_up_frames = shared_state->autofire_hold_up_frames.load();

    // Thread-safe access to autofire_buttons and autofire_triggers
    utils::SRWLockExclusive lock(shared_state->autofire_lock);

    // Store original button state before processing
    WORD original_buttons = pState->Gamepad.wButtons;
    BYTE original_left_trigger = pState->Gamepad.bLeftTrigger;
    BYTE original_right_trigger = pState->Gamepad.bRightTrigger;

    // Process each autofire button directly from shared state
    for (auto& af_button : shared_state->autofire_buttons) {
        WORD button_mask = af_button.button_mask;
        bool is_pressed = (original_buttons & button_mask) != 0;

        if (is_pressed) {
            // Button is held down, process autofire cycle
            uint64_t phase_start_frame = af_button.phase_start_frame_id.load();
            bool is_holding_down = af_button.is_holding_down.load();

            // Initialize phase if this is the first frame
            if (phase_start_frame == 0) {
                af_button.phase_start_frame_id.store(current_frame_id);
                af_button.is_holding_down.store(true);
                is_holding_down = true;
                phase_start_frame = current_frame_id;
            }

            uint64_t frames_in_current_phase = current_frame_id - phase_start_frame;

            if (is_holding_down) {
                // Currently holding down - check if we should switch to hold up phase
                if (frames_in_current_phase >= hold_down_frames) {
                    // Switch to hold up phase
                    af_button.is_holding_down.store(false);
                    af_button.phase_start_frame_id.store(current_frame_id);
                    // Turn button off
                    pState->Gamepad.wButtons &= ~button_mask;
                } else {
                    // Keep holding down
                    pState->Gamepad.wButtons |= button_mask;
                }
            } else {
                // Currently holding up - check if we should switch to hold down phase
                if (frames_in_current_phase >= hold_up_frames) {
                    // Switch to hold down phase
                    af_button.is_holding_down.store(true);
                    af_button.phase_start_frame_id.store(current_frame_id);
                    // Turn button on
                    pState->Gamepad.wButtons |= button_mask;
                } else {
                    // Keep holding up
                    pState->Gamepad.wButtons &= ~button_mask;
                }
            }
        } else {
            // Button is not pressed, reset state
            af_button.is_holding_down.store(true);
            af_button.phase_start_frame_id.store(0);
        }
    }

    // Process each autofire trigger directly from shared state
    // Use threshold of 30 to avoid accidental activation from slight trigger pressure
    const BYTE trigger_threshold = 30;
    for (auto& af_trigger : shared_state->autofire_triggers) {
        BYTE original_trigger_value = (af_trigger.trigger_type == XInputSharedState::TriggerType::LeftTrigger)
                                          ? original_left_trigger
                                          : original_right_trigger;
        bool is_pressed = original_trigger_value > trigger_threshold;

        if (is_pressed) {
            // Trigger is held down, process autofire cycle
            uint64_t phase_start_frame = af_trigger.phase_start_frame_id.load();
            bool is_holding_down = af_trigger.is_holding_down.load();

            // Initialize phase if this is the first frame
            if (phase_start_frame == 0) {
                af_trigger.phase_start_frame_id.store(current_frame_id);
                af_trigger.is_holding_down.store(true);
                is_holding_down = true;
                phase_start_frame = current_frame_id;
            }

            uint64_t frames_in_current_phase = current_frame_id - phase_start_frame;

            if (is_holding_down) {
                // Currently holding down - check if we should switch to hold up phase
                if (frames_in_current_phase >= hold_down_frames) {
                    // Switch to hold up phase
                    af_trigger.is_holding_down.store(false);
                    af_trigger.phase_start_frame_id.store(current_frame_id);
                    // Turn trigger off (set to 0)
                    if (af_trigger.trigger_type == XInputSharedState::TriggerType::LeftTrigger) {
                        pState->Gamepad.bLeftTrigger = 0;
                    } else {
                        pState->Gamepad.bRightTrigger = 0;
                    }
                } else {
                    // Keep holding down (set to full value 255)
                    if (af_trigger.trigger_type == XInputSharedState::TriggerType::LeftTrigger) {
                        pState->Gamepad.bLeftTrigger = 255;
                    } else {
                        pState->Gamepad.bRightTrigger = 255;
                    }
                }
            } else {
                // Currently holding up - check if we should switch to hold down phase
                if (frames_in_current_phase >= hold_up_frames) {
                    // Switch to hold down phase
                    af_trigger.is_holding_down.store(true);
                    af_trigger.phase_start_frame_id.store(current_frame_id);
                    // Turn trigger on (set to full value 255)
                    if (af_trigger.trigger_type == XInputSharedState::TriggerType::LeftTrigger) {
                        pState->Gamepad.bLeftTrigger = 255;
                    } else {
                        pState->Gamepad.bRightTrigger = 255;
                    }
                } else {
                    // Keep holding up (set to 0)
                    if (af_trigger.trigger_type == XInputSharedState::TriggerType::LeftTrigger) {
                        pState->Gamepad.bLeftTrigger = 0;
                    } else {
                        pState->Gamepad.bRightTrigger = 0;
                    }
                }
            }
        } else {
            // Trigger is not pressed, reset state
            af_trigger.is_holding_down.store(true);
            af_trigger.phase_start_frame_id.store(0);
        }
    }
}

}  // namespace display_commander::widgets::xinput_widget
