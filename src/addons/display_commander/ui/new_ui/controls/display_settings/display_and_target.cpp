// Source Code <Display Commander> // follow this order for includes in all files + add this comment at the top
#include "display_settings_internal.hpp"

namespace ui::new_ui {

void DrawDisplaySettings_DisplayAndTarget(display_commander::ui::IImGuiWrapper& imgui,
                                         reshade::api::effect_runtime* runtime) {
    (void)imgui;
    CALL_GUARD_NO_TS();
    {
        // Refresh target display from config so hotkey changes (Win+Left/Win+Right) are visible on the UI thread
        settings::g_mainTabSettings.selected_extended_display_device_id.Load();
        settings::g_mainTabSettings.target_extended_display_device_id.Load();

        // Target Display list and selection (needed for refresh rate fallback on same line as Game Render Resolution)
        auto display_info = display_cache::g_displayCache.GetDisplayInfoForUI();
        std::string current_device_id = settings::g_mainTabSettings.selected_extended_display_device_id.GetValue();
        int selected_index = 0;
        for (size_t i = 0; i < display_info.size(); ++i) {
            if (display_info[i].extended_device_id == current_device_id) {
                selected_index = static_cast<int>(i);
                break;
            }
        }
        CALL_GUARD_NO_TS();

        // Backbuffer size: from runtime when available, else from game render size
        uint32_t backbuffer_w = 0;
        uint32_t backbuffer_h = 0;
        reshade::api::format backbuffer_format = reshade::api::format::unknown;
        backbuffer_w = static_cast<uint32_t>(g_game_render_width.load());
        backbuffer_h = static_cast<uint32_t>(g_game_render_height.load());
        if (backbuffer_w == 0 || backbuffer_h == 0) {
            auto desc_ptr = g_last_swapchain_desc_post.load();
            if (desc_ptr != nullptr) {
                backbuffer_w = desc_ptr->back_buffer.texture.width;
                backbuffer_h = desc_ptr->back_buffer.texture.height;
                backbuffer_format = desc_ptr->back_buffer.texture.format;
            }
        } else {
            auto desc_ptr = g_last_swapchain_desc_post.load();
            if (desc_ptr != nullptr) {
                backbuffer_format = desc_ptr->back_buffer.texture.format;
            }
        }
        CALL_GUARD_NO_TS();

        if (backbuffer_w > 0 && backbuffer_h > 0) {
            imgui.TextColored(ui::colors::TEXT_LABEL, "Render resolution:");
            imgui.SameLine();
            imgui.Text("%ux%u", static_cast<unsigned>(backbuffer_w), static_cast<unsigned>(backbuffer_h));

            // Bit depth from runtime or swapchain desc (optional, in parens)
            const char* bit_depth_str = nullptr;
            switch (backbuffer_format) {
                case reshade::api::format::r8g8b8a8_unorm:
                case reshade::api::format::b8g8r8a8_unorm:     bit_depth_str = "8-bit"; break;
                case reshade::api::format::r10g10b10a2_unorm:  bit_depth_str = "10-bit"; break;
                case reshade::api::format::r16g16b16a16_float: bit_depth_str = "16-bit"; break;
                default:                                       break;
            }
            if (bit_depth_str != nullptr) {
                imgui.SameLine();
                imgui.TextColored(ui::colors::TEXT_DIMMED, " (%s)", bit_depth_str);
            }

            // Refresh rate on same line from selected display's configured rate (display cache)
            double refresh_hz = 0.0;
            if (selected_index >= 0 && selected_index < static_cast<int>(display_info.size())
                && !display_info[selected_index].current_refresh_rate.empty()) {
                std::string rate_str = display_info[selected_index].current_refresh_rate;
                try {
                    double parsed = std::stod(rate_str);
                    if (parsed >= 1.0 && parsed <= 500.0) {
                        refresh_hz = parsed;
                    }
                } catch (...) {
                }
            }
            imgui.SameLine();
            if (refresh_hz > 0.0) {
                imgui.TextColored(ui::colors::TEXT_LABEL, "Refresh rate:");
                imgui.SameLine();
                imgui.Text("%.1f Hz", refresh_hz);
            } else {
                imgui.TextColored(ui::colors::TEXT_LABEL, "Refresh rate:");
                imgui.SameLine();
                imgui.TextColored(ui::colors::TEXT_DIMMED, "—");
            }
            if (imgui.IsItemHovered()) {
                imgui.SetTooltipEx(
                    "Render resolution: the resolution the game requested (before any modifications). "
                    "Matches Special K's render_x/render_y.\n"
                    "Refresh rate: selected display's configured rate from the display list.");
            }

            // VRAM and RAM usage on one line under Render resolution
            uint64_t vram_used = 0;
            uint64_t vram_total = 0;
            if (display_commander::dxgi::GetVramInfo(&vram_used, &vram_total) && vram_total > 0) {
                const uint64_t used_mib = vram_used / (1024ULL * 1024ULL);
                const uint64_t total_mib = vram_total / (1024ULL * 1024ULL);
                imgui.TextColored(ui::colors::TEXT_LABEL, "VRAM:");
                imgui.SameLine();
                imgui.Text("%llu / %llu MiB", static_cast<unsigned long long>(used_mib),
                           static_cast<unsigned long long>(total_mib));
                if (imgui.IsItemHovered()) {
                    imgui.SetTooltipEx("GPU video memory used / budget (DXGI adapter memory budget).");
                }
            } else {
                imgui.TextColored(ui::colors::TEXT_LABEL, "VRAM:");
                imgui.SameLine();
                imgui.TextColored(ui::colors::TEXT_DIMMED, "N/A");
                if (imgui.IsItemHovered()) {
                    imgui.SetTooltipEx("VRAM unavailable (DXGI adapter or budget query failed).");
                }
            }

            // RAM (system memory) on the same line: X(Y)/Z = system used (current process used) / total
            imgui.SameLine();
            MEMORYSTATUSEX mem_status = {};
            mem_status.dwLength = sizeof(mem_status);
            if (GlobalMemoryStatusEx(&mem_status) != 0 && mem_status.ullTotalPhys > 0) {
                const uint64_t ram_used = mem_status.ullTotalPhys - mem_status.ullAvailPhys;
                const uint64_t ram_used_mib = ram_used / (1024ULL * 1024ULL);
                const uint64_t ram_total_mib = mem_status.ullTotalPhys / (1024ULL * 1024ULL);
                PROCESS_MEMORY_COUNTERS pmc = {};
                pmc.cb = sizeof(pmc);
                const bool have_process = (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc)) != 0);
                const uint64_t process_mib = have_process ? (pmc.WorkingSetSize / (1024ULL * 1024ULL)) : 0;
                imgui.TextColored(ui::colors::TEXT_LABEL, "RAM:");
                imgui.SameLine();
                if (have_process) {
                    imgui.Text("%llu (%llu) / %llu MiB", static_cast<unsigned long long>(ram_used_mib),
                               static_cast<unsigned long long>(process_mib),
                               static_cast<unsigned long long>(ram_total_mib));
                } else {
                    imgui.Text("%llu / %llu MiB", static_cast<unsigned long long>(ram_used_mib),
                               static_cast<unsigned long long>(ram_total_mib));
                }
                if (imgui.IsItemHovered()) {
                    imgui.SetTooltipEx(
                        "System RAM in use (this app working set) / total (GlobalMemoryStatusEx, "
                        "GetProcessMemoryInfo).");
                }
            } else {
                imgui.TextColored(ui::colors::TEXT_LABEL, "RAM:");
                imgui.SameLine();
                imgui.TextColored(ui::colors::TEXT_DIMMED, "N/A");
                if (imgui.IsItemHovered()) {
                    imgui.SetTooltipEx("System memory info unavailable.");
                }
            }
        }
        CALL_GUARD_NO_TS();

        // Target Display dropdown (left-aligned with Render resolution / VRAM; flat frame — similar to DC folder buttons)
        std::vector<const char*> monitor_c_labels;
        monitor_c_labels.reserve(display_info.size());
        for (const auto& info : display_info) {
            monitor_c_labels.push_back(info.display_label.c_str());
        }
        CALL_GUARD_NO_TS();

        float preview_text_w = imgui.CalcTextSize("—").x;
        for (const char* lbl : monitor_c_labels) {
            preview_text_w = (std::max)(preview_text_w, imgui.CalcTextSize(lbl).x);
        }
        const ImGuiStyle& st = imgui.GetStyle();
        const float combo_ctrl_w =
            preview_text_w + (st.FramePadding.x * 2.f) + st.ItemInnerSpacing.x + imgui.GetTextLineHeight() + 4.f;

            CALL_GUARD_NO_TS();
        ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.5f, 0.5f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.f);
        const ImVec4 frame_bg_clear(0.f, 0.f, 0.f, 0.f);
        imgui.PushStyleColor(ImGuiCol_FrameBg, frame_bg_clear);
        imgui.PushStyleColor(ImGuiCol_FrameBgHovered, frame_bg_clear);
        imgui.PushStyleColor(ImGuiCol_FrameBgActive, frame_bg_clear);

        PushFpsLimiterSliderColumnAlign(imgui, GetMainTabCheckboxColumnGutter(imgui));
        imgui.BeginGroup();
        imgui.SetNextItemWidth(600.f);  // combo_ctrl_w);

        CALL_GUARD_NO_TS();
        static bool s_target_display_changed = false;
        if (imgui.Combo("##TargetDisplay", &selected_index, monitor_c_labels.data(),
                        static_cast<int>(monitor_c_labels.size()))) {
            if (selected_index >= 0 && selected_index < static_cast<int>(display_info.size())) {
                s_target_display_changed = true;
                // Store extended device ID so Win+Left/Right and window management use the same value.
                std::string new_device_id = display_info[selected_index].extended_device_id;
                settings::g_mainTabSettings.selected_extended_display_device_id.SetValue(new_device_id);
                settings::g_mainTabSettings.target_extended_display_device_id.SetValue(new_device_id);

                LogInfo("Target monitor changed to device ID: %s", new_device_id.c_str());
            }
        }
        CALL_GUARD_NO_TS();
        const bool target_display_combo_hovered = imgui.IsItemHovered();
        imgui.SameLine(0.f, st.ItemInnerSpacing.x);
        imgui.TextColored(ui::colors::TEXT_LABEL, "Target Display");
        const bool target_display_label_hovered = imgui.IsItemHovered();
        imgui.EndGroup();

        CALL_GUARD_NO_TS();
        imgui.PopStyleColor(3);
        ImGui::PopStyleVar(2);
        if (target_display_combo_hovered || target_display_label_hovered) {
            // Get the saved game window display device ID for tooltip
            std::string saved_device_id = settings::g_mainTabSettings.game_window_extended_display_device_id.GetValue();
            std::string tooltip_text =
                "Choose which monitor to apply size/pos to. The monitor corresponding to the "
                "game window is automatically selected.";
            if (!saved_device_id.empty() && saved_device_id != "No Window" && saved_device_id != "No Monitor"
                && saved_device_id != "Monitor Info Failed") {
                tooltip_text += "\n\nGame window is on: " + saved_device_id;
            }
            imgui.SetTooltipEx("%s", tooltip_text.c_str());
        }
        CALL_GUARD_NO_TS();
        // Warn if mode does not resize; moving to another display isn't implemented in those modes
        const WindowMode mode = GetCurrentWindowMode();
        if (s_target_display_changed
            && (mode == WindowMode::kNoChanges || mode == WindowMode::kPreventFullscreenNoResize)) {
            imgui.TextColored(ui::colors::TEXT_WARNING, ICON_FK_WARNING
                              "Warning: Moving to another display isn't implemented in this window mode.");
        }
    }
}

}  // namespace ui::new_ui

