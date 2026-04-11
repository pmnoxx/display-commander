// Source Code <Display Commander> // follow this order for includes in all files + add this comment at the top
#include "display_config_debug_tab.hpp"
#include "../../../utils/string_utils.hpp"

// Libraries <ReShade> / <imgui>
#include <imgui.h>

// Libraries <standard C++>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

// Libraries <Windows.h> — before other Windows headers
#include <Windows.h>

// Libraries <Windows>
#include <wingdi.h>

namespace ui::new_ui::debug {

namespace {

// Bit-packed union words follow DISPLAYCONFIG_DEVICE_INFO_HEADER in these structs; reading by offset avoids
// SDK/C++ differences around DUMMYUNIONNAME / member names and Windows header macro quirks.
std::uint32_t TargetDeviceNameFlagsWord(const DISPLAYCONFIG_TARGET_DEVICE_NAME& t) {
    static_assert(sizeof(DISPLAYCONFIG_DEVICE_INFO_HEADER) == 20, "DISPLAYCONFIG_DEVICE_INFO_HEADER size");
    return *reinterpret_cast<const std::uint32_t*>(reinterpret_cast<const unsigned char*>(&t)
                                                     + sizeof(DISPLAYCONFIG_DEVICE_INFO_HEADER));
}

std::uint32_t AdvancedColorInfoPackedWord(const DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO& a) {
    static_assert(sizeof(DISPLAYCONFIG_DEVICE_INFO_HEADER) == 20, "DISPLAYCONFIG_DEVICE_INFO_HEADER size");
    return *reinterpret_cast<const std::uint32_t*>(reinterpret_cast<const unsigned char*>(&a)
                                                     + sizeof(DISPLAYCONFIG_DEVICE_INFO_HEADER));
}

const char* ColorEncodingLabel(DISPLAYCONFIG_COLOR_ENCODING e) {
    switch (e) {
        case DISPLAYCONFIG_COLOR_ENCODING_RGB:
            return "RGB";
        case DISPLAYCONFIG_COLOR_ENCODING_YCBCR444:
            return "YCBCR444";
        case DISPLAYCONFIG_COLOR_ENCODING_YCBCR422:
            return "YCBCR422";
        case DISPLAYCONFIG_COLOR_ENCODING_YCBCR420:
            return "YCBCR420";
        case DISPLAYCONFIG_COLOR_ENCODING_INTENSITY:
            return "INTENSITY";
        default:
            return "?";
    }
}

void RowText2(display_commander::ui::IImGuiWrapper& imgui, const char* label, const char* value) {
    imgui.TableNextRow();
    imgui.TableNextColumn();
    imgui.TextUnformatted(label);
    imgui.TableNextColumn();
    imgui.TextUnformatted(value);
}

void RowText2(display_commander::ui::IImGuiWrapper& imgui, const char* label, const std::string& value) {
    RowText2(imgui, label, value.c_str());
}

void RowHr(display_commander::ui::IImGuiWrapper& imgui, const char* label, LONG hr) {
    imgui.TableNextRow();
    imgui.TableNextColumn();
    imgui.TextUnformatted(label);
    imgui.TableNextColumn();
    if (hr == ERROR_SUCCESS) {
        imgui.TextUnformatted("ERROR_SUCCESS");
    } else {
        imgui.Text("0x%08lX (%ld)", static_cast<unsigned long>(static_cast<unsigned long>(hr)),
                   static_cast<long>(hr));
    }
}

bool BeginKvTable(display_commander::ui::IImGuiWrapper& imgui, const char* table_id) {
    if (!imgui.BeginTable(table_id, 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
        return false;
    }
    imgui.TableSetupColumn("Field");
    imgui.TableSetupColumn("Value");
    imgui.TableHeadersRow();
    return true;
}

}  // namespace

void DrawDisplayConfigDebugTab(display_commander::ui::IImGuiWrapper& imgui) {
    imgui.TextWrapped(
        "Per active DisplayConfig path: results of DisplayConfigGetDeviceInfo (target name, source name, advanced "
        "color, SDR white level). Adapter LUID is path.sourceInfo.adapterId; target id is path.targetInfo.id. "
        "Build with DEBUG_TABS.");
    imgui.Spacing();

    UINT32 path_count = 0;
    UINT32 mode_count = 0;
    if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &path_count, &mode_count) != ERROR_SUCCESS) {
        imgui.TextUnformatted("GetDisplayConfigBufferSizes failed.");
        return;
    }
    if (path_count == 0 || mode_count == 0) {
        imgui.TextUnformatted("No active display paths.");
        return;
    }

    std::vector<DISPLAYCONFIG_PATH_INFO> paths(path_count);
    std::vector<DISPLAYCONFIG_MODE_INFO> modes(mode_count);
    if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &path_count, paths.data(), &mode_count, modes.data(), nullptr)
        != ERROR_SUCCESS) {
        imgui.TextUnformatted("QueryDisplayConfig failed.");
        return;
    }

    UINT32 shown = 0;
    for (UINT32 path_idx = 0; path_idx < path_count; ++path_idx) {
        const DISPLAYCONFIG_PATH_INFO& path = paths[path_idx];
        if (!(path.flags & DISPLAYCONFIG_PATH_ACTIVE) || !(path.sourceInfo.statusFlags & DISPLAYCONFIG_SOURCE_IN_USE)) {
            continue;
        }

        const LUID adapter_luid = path.sourceInfo.adapterId;
        const UINT32 source_id = path.sourceInfo.id;
        const UINT32 target_id = path.targetInfo.id;

        DISPLAYCONFIG_TARGET_DEVICE_NAME get_target = {};
        get_target.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME;
        get_target.header.size = sizeof(DISPLAYCONFIG_TARGET_DEVICE_NAME);
        get_target.header.adapterId = adapter_luid;
        get_target.header.id = target_id;
        const LONG hr_target = DisplayConfigGetDeviceInfo(&get_target.header);

        DISPLAYCONFIG_SOURCE_DEVICE_NAME get_source = {};
        get_source.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
        get_source.header.size = sizeof(DISPLAYCONFIG_SOURCE_DEVICE_NAME);
        get_source.header.adapterId = adapter_luid;
        get_source.header.id = source_id;
        const LONG hr_source = DisplayConfigGetDeviceInfo(&get_source.header);

        DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO get_adv = {};
        get_adv.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_ADVANCED_COLOR_INFO;
        get_adv.header.size = sizeof(DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO);
        get_adv.header.adapterId = adapter_luid;
        get_adv.header.id = target_id;
        const LONG hr_adv = DisplayConfigGetDeviceInfo(&get_adv.header);

        DISPLAYCONFIG_SDR_WHITE_LEVEL get_sdr = {};
        get_sdr.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SDR_WHITE_LEVEL;
        get_sdr.header.size = sizeof(DISPLAYCONFIG_SDR_WHITE_LEVEL);
        get_sdr.header.adapterId = adapter_luid;
        get_sdr.header.id = target_id;
        const LONG hr_sdr = DisplayConfigGetDeviceInfo(&get_sdr.header);

        char tree_label[256];
        if (hr_target == ERROR_SUCCESS && get_target.monitorFriendlyDeviceName[0] != L'\0') {
            const std::string friendly =
                display_commander::utils::WideToUtf8(get_target.monitorFriendlyDeviceName);
            std::snprintf(tree_label, sizeof(tree_label), "Path %u: %s", static_cast<unsigned>(path_idx),
                          friendly.c_str());
        } else if (hr_source == ERROR_SUCCESS && get_source.viewGdiDeviceName[0] != L'\0') {
            const std::string gdi = display_commander::utils::WideToUtf8(get_source.viewGdiDeviceName);
            std::snprintf(tree_label, sizeof(tree_label), "Path %u: %s", static_cast<unsigned>(path_idx), gdi.c_str());
        } else {
            std::snprintf(tree_label, sizeof(tree_label), "Path %u (display)", static_cast<unsigned>(path_idx));
        }

        imgui.PushID(static_cast<int>(path_idx));
        if (imgui.TreeNode(tree_label)) {
            char buf[512];
            char sub_id[64];
            std::snprintf(sub_id, sizeof(sub_id), "path%u_summary", static_cast<unsigned>(path_idx));
            if (BeginKvTable(imgui, sub_id)) {
                std::snprintf(buf, sizeof(buf), "%lu:%lu", static_cast<unsigned long>(adapter_luid.HighPart),
                              static_cast<unsigned long>(adapter_luid.LowPart));
                RowText2(imgui, "Adapter LUID (High:Low)", buf);
                std::snprintf(buf, sizeof(buf), "%lu", static_cast<unsigned long>(source_id));
                RowText2(imgui, "Source id", buf);
                std::snprintf(buf, sizeof(buf), "%lu", static_cast<unsigned long>(target_id));
                RowText2(imgui, "Target id", buf);
                imgui.EndTable();
            }

            imgui.Spacing();
            imgui.TextUnformatted("GET_TARGET_NAME");
            std::snprintf(sub_id, sizeof(sub_id), "path%u_tgt", static_cast<unsigned>(path_idx));
            if (BeginKvTable(imgui, sub_id)) {
                RowHr(imgui, "DisplayConfigGetDeviceInfo", hr_target);
                if (hr_target == ERROR_SUCCESS) {
                    RowText2(imgui, "flags (value)",
                             std::to_string(static_cast<unsigned int>(TargetDeviceNameFlagsWord(get_target))));
                    std::snprintf(buf, sizeof(buf), "%u", static_cast<unsigned int>(get_target.outputTechnology));
                    RowText2(imgui, "outputTechnology", buf);
                    std::snprintf(buf, sizeof(buf), "0x%04X", static_cast<unsigned>(get_target.edidManufactureId));
                    RowText2(imgui, "edidManufactureId", buf);
                    std::snprintf(buf, sizeof(buf), "0x%04X", static_cast<unsigned>(get_target.edidProductCodeId));
                    RowText2(imgui, "edidProductCodeId", buf);
                    std::snprintf(buf, sizeof(buf), "%lu", static_cast<unsigned long>(get_target.connectorInstance));
                    RowText2(imgui, "connectorInstance", buf);
                    RowText2(imgui, "monitorFriendlyDeviceName",
                             display_commander::utils::WideToUtf8(get_target.monitorFriendlyDeviceName));
                    RowText2(imgui, "monitorDevicePath",
                             display_commander::utils::WideToUtf8(get_target.monitorDevicePath));
                }
                imgui.EndTable();
            }

            imgui.Spacing();
            imgui.TextUnformatted("GET_SOURCE_NAME");
            std::snprintf(sub_id, sizeof(sub_id), "path%u_src", static_cast<unsigned>(path_idx));
            if (BeginKvTable(imgui, sub_id)) {
                RowHr(imgui, "DisplayConfigGetDeviceInfo", hr_source);
                if (hr_source == ERROR_SUCCESS) {
                    RowText2(imgui, "viewGdiDeviceName",
                             display_commander::utils::WideToUtf8(get_source.viewGdiDeviceName));
                }
                imgui.EndTable();
            }

            imgui.Spacing();
            imgui.TextUnformatted("GET_ADVANCED_COLOR_INFO");
            std::snprintf(sub_id, sizeof(sub_id), "path%u_adv", static_cast<unsigned>(path_idx));
            if (BeginKvTable(imgui, sub_id)) {
                RowHr(imgui, "DisplayConfigGetDeviceInfo", hr_adv);
                if (hr_adv == ERROR_SUCCESS) {
                    const UINT32 packed = AdvancedColorInfoPackedWord(get_adv);
                    char buf_adv[128];
                    std::snprintf(buf_adv, sizeof(buf_adv), "0x%08X", static_cast<unsigned int>(packed));
                    RowText2(imgui, "value (raw)", buf_adv);
                    RowText2(imgui, "advancedColorSupported",
                             (packed & 1u) ? "true" : "false");
                    RowText2(imgui, "advancedColorEnabled",
                             (packed & 2u) ? "true" : "false");
                    RowText2(imgui, "wideColorEnforced",
                             (packed & 4u) ? "true" : "false");
                    RowText2(imgui, "advancedColorForceDisabled",
                             (packed & 8u) ? "true" : "false");
                    std::snprintf(buf_adv, sizeof(buf_adv), "%u (%s)", static_cast<unsigned int>(get_adv.colorEncoding),
                                  ColorEncodingLabel(get_adv.colorEncoding));
                    RowText2(imgui, "colorEncoding", buf_adv);
                    std::snprintf(buf_adv, sizeof(buf_adv), "%lu",
                                  static_cast<unsigned long>(get_adv.bitsPerColorChannel));
                    RowText2(imgui, "bitsPerColorChannel", buf_adv);
                }
                imgui.EndTable();
            }

            imgui.Spacing();
            imgui.TextUnformatted("GET_SDR_WHITE_LEVEL");
            std::snprintf(sub_id, sizeof(sub_id), "path%u_sdr", static_cast<unsigned>(path_idx));
            if (BeginKvTable(imgui, sub_id)) {
                RowHr(imgui, "DisplayConfigGetDeviceInfo", hr_sdr);
                if (hr_sdr == ERROR_SUCCESS) {
                    char buf_sdr[160];
                    const double nits = (static_cast<double>(get_sdr.SDRWhiteLevel) / 1000.0) * 80.0;
                    std::snprintf(buf_sdr, sizeof(buf_sdr), "%lu (~%.2f nits per SDK comment)",
                                  static_cast<unsigned long>(get_sdr.SDRWhiteLevel), nits);
                    RowText2(imgui, "SDRWhiteLevel", buf_sdr);
                }
                imgui.EndTable();
            }

            imgui.TreePop();
        }
        imgui.PopID();
        ++shown;
    }

    if (shown == 0) {
        imgui.TextUnformatted("No in-use active paths after filtering.");
    }
}

}  // namespace ui::new_ui::debug
