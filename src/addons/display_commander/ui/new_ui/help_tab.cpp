// Source Code <Display Commander> // follow this order for includes in all files + add this comment at the top
#include "help_tab.hpp"
#include "../../globals.hpp"
#include "../../res/forkawesome.h"
#include "../../res/legal_license_version.hpp"
#include "../../res/legal_rc_ids.hpp"
#include "../../res/ui_colors.hpp"
#include "../../settings/main_tab_settings.hpp"
#include "../../utils/logging.hpp"
#include "../imgui_wrapper_base.hpp"

// Libraries <ReShade> / <imgui>
#include <imgui.h>

// Libraries <standard C++>
#include <string>
#include <vector>

// Libraries <Windows.h> — before other Windows headers
#include <Windows.h>

namespace ui::new_ui {

namespace {

std::string LoadRcdataAsUtf8(HMODULE module, int resource_id) {
    if (module == nullptr) {
        return {};
    }
    HRSRC h_res = FindResourceA(module, MAKEINTRESOURCEA(resource_id), RT_RCDATA);
    if (h_res == nullptr) {
        return {};
    }
    HGLOBAL h_loaded = LoadResource(module, h_res);
    if (h_loaded == nullptr) {
        return {};
    }
    const void* data_ptr = LockResource(h_loaded);
    const DWORD size = SizeofResource(module, h_res);
    if (data_ptr == nullptr || size == 0) {
        return {};
    }
    return std::string(static_cast<const char*>(data_ptr), static_cast<size_t>(size));
}

void EnsureBuffer(std::vector<char>& buf, int resource_id, const char* fallback) {
    if (!buf.empty()) {
        return;
    }
    std::string text = LoadRcdataAsUtf8(g_hmodule, resource_id);
    if (text.empty()) {
        LogWarn("Help: embedded RCDATA %d not found or empty", resource_id);
        text = fallback;
    }
    buf.assign(text.begin(), text.end());
    buf.push_back('\0');
}

}  // namespace

void DrawMainTabLegalSection(display_commander::ui::IImGuiWrapper& imgui) {
    static std::vector<char> s_license_buf;
    static std::vector<char> s_third_party_buf;

    static bool s_license_modal_open = false;
    static bool s_third_party_modal_open = false;
    static bool s_pending_license_popup = false;
    static bool s_pending_third_party_popup = false;

    const int bundled_ver = display_commander::legal::kLicensePresentationVersion;
    const int acknowledged = settings::g_mainTabSettings.license_acknowledged_version.GetValue();
    const bool need_accept = acknowledged < bundled_ver;

    if (need_accept) {
        g_rendering_ui_section.store("ui:tab:main_new:legal", std::memory_order_release);
        imgui.TextWrapped(
            "Use of Display Commander requires that you read and accept the license and third-party notices below. "
            "Press Accept after you have reviewed them.");
        imgui.Spacing();

        if (imgui.Button(ICON_FK_FILE_CODE " Display Commander license")) {
            EnsureBuffer(s_license_buf, IDR_DC_LICENSE, "License text could not be loaded from embedded resources.");
            s_pending_license_popup = true;
        }
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx("Shows the LICENSE file for Display Commander (embedded in the binary).");
        }

        imgui.SameLine();

        if (imgui.Button(ICON_FK_FILE " Third-party notices")) {
            EnsureBuffer(s_third_party_buf, IDR_THIRD_PARTY_NOTICES,
                        "Third-party notices could not be loaded from embedded resources.");
            s_pending_third_party_popup = true;
        }
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx("Shows THIRD_PARTY_NOTICES.txt (embedded in the binary).");
        }

        imgui.Spacing();
        if (imgui.Button("Accept")) {
            settings::g_mainTabSettings.license_acknowledged_version.SetValue(bundled_ver);
            LogInfo("License: user accepted presentation version %d", bundled_ver);
        }
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx("Record acceptance of this license version and hide this prompt until the version is "
                               "raised again.");
        }
    }

    if (s_pending_license_popup) {
        imgui.SetNextWindowSize(ImVec2(720.0f, 520.0f), static_cast<int>(ImGuiCond_FirstUseEver));
        s_license_modal_open = true;
        s_pending_license_popup = false;
        imgui.OpenPopup("Display Commander License");
    }
    if (s_pending_third_party_popup) {
        imgui.SetNextWindowSize(ImVec2(960.0f, 720.0f), static_cast<int>(ImGuiCond_FirstUseEver));
        s_third_party_modal_open = true;
        s_pending_third_party_popup = false;
        imgui.OpenPopup("Third-party notices");
    }

    constexpr int k_modal_flags =
        static_cast<int>(ImGuiWindowFlags_AlwaysHorizontalScrollbar | ImGuiWindowFlags_AlwaysVerticalScrollbar);
    if (imgui.BeginPopupModal("Display Commander License", &s_license_modal_open, k_modal_flags)) {
        ImVec2 size = imgui.GetContentRegionAvail();
        size.y -= 32.0f;
        if (size.y < 120.0f) {
            size.y = 320.0f;
        }
        const int read_only = static_cast<int>(ImGuiInputTextFlags_ReadOnly);
        if (!s_license_buf.empty()) {
            imgui.InputTextMultiline("##dc_license", s_license_buf.data(), s_license_buf.size(), size, read_only);
        } else {
            imgui.TextDisabled("No content.");
        }
        imgui.Spacing();
        if (imgui.Button("Close")) {
            s_license_modal_open = false;
        }
        imgui.EndPopup();
    }

    if (imgui.BeginPopupModal("Third-party notices", &s_third_party_modal_open, k_modal_flags)) {
        ImVec2 size = imgui.GetContentRegionAvail();
        size.y -= 32.0f;
        if (size.y < 120.0f) {
            size.y = 480.0f;
        }
        const int read_only = static_cast<int>(ImGuiInputTextFlags_ReadOnly);
        if (!s_third_party_buf.empty()) {
            imgui.InputTextMultiline("##third_party", s_third_party_buf.data(), s_third_party_buf.size(), size,
                                     read_only);
        } else {
            imgui.TextDisabled("No content.");
        }
        imgui.Spacing();
        if (imgui.Button("Close")) {
            s_third_party_modal_open = false;
        }
        imgui.EndPopup();
    }
}

}  // namespace ui::new_ui
