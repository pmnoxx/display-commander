// Source Code <Display Commander> // follow this order for includes in all files + add this comment at the top
// Feature behavior is specified in src/addons/display_commander/docs/specs/installer_marker.md
#include "features/installer_marker/installer_marker.hpp"

#include "globals.hpp"

// Libraries <standard C++>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>

// Libraries <Windows.h>
#include <Windows.h>

namespace display_commander::features::installer_marker {

namespace {

void LogBootInstallerMarker(const std::string& text) { AppendDisplayCommanderBootLog(text); }

std::string WidePathToUtf8(std::wstring_view w) {
    if (w.empty()) return {};
    const int len =
        WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    if (len <= 0) return {};
    std::string out(static_cast<size_t>(len), '\0');
    if (WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), out.data(), len, nullptr, nullptr) == 0) {
        return {};
    }
    return out;
}

std::string EscapeJsonUtf8(std::string_view s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (unsigned char c : s) {
        switch (c) {
            case '\\':
                out += "\\\\";
                break;
            case '"':
                out += "\\\"";
                break;
            case '\b':
                out += "\\b";
                break;
            case '\f':
                out += "\\f";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                if (c < 0x20U) {
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned>(c));
                    out += buf;
                } else {
                    out += static_cast<char>(c);
                }
                break;
        }
    }
    return out;
}

bool Utf8PathToWide(std::string_view utf8, std::wstring& out_w) {
    if (utf8.empty()) {
        out_w.clear();
        return false;
    }
    const int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    if (n <= 0) return false;
    out_w.assign(static_cast<size_t>(n), L'\0');
    return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(), static_cast<int>(utf8.size()), out_w.data(), n) > 0;
}

// Strip UTF-8 BOM if present.
void StripUtf8Bom(std::string& s) {
    if (s.size() >= 3 && static_cast<unsigned char>(s[0]) == 0xEF && static_cast<unsigned char>(s[1]) == 0xBB
        && static_cast<unsigned char>(s[2]) == 0xBF) {
        s.erase(0, 3);
    }
}

}  // namespace

std::optional<std::wstring> TryReadInstallerMarkerConfigDirectory(bool dc_config_global_flag_present,
                                                                  const std::filesystem::path& marker_path) {
    if (!dc_config_global_flag_present) {
        return std::nullopt;
    }
    std::error_code ec;
    if (!std::filesystem::is_regular_file(marker_path, ec) || ec) {
        return std::nullopt;
    }
    std::ifstream in(marker_path, std::ios::binary);
    if (!in) {
        return std::nullopt;
    }
    std::string json((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    StripUtf8Bom(json);
    constexpr std::string_view kKey = "\"display_commander_config_directory\"";
    const size_t kpos = json.find(kKey);
    if (kpos == std::string::npos) {
        return std::nullopt;
    }
    size_t i = json.find(':', kpos + kKey.size());
    if (i == std::string::npos) {
        return std::nullopt;
    }
    ++i;
    while (i < json.size() && (json[i] == ' ' || json[i] == '\t' || json[i] == '\n' || json[i] == '\r')) {
        ++i;
    }
    if (i >= json.size() || json[i] != '"') {
        return std::nullopt;
    }
    ++i;
    std::string acc;
    while (i < json.size()) {
        const char c = json[i++];
        if (c == '"') {
            break;
        }
        if (c == '\\' && i < json.size()) {
            const char e = json[i++];
            if (e == '"' || e == '\\' || e == '/') {
                acc += e;
            } else if (e == 'n') {
                acc += '\n';
            } else if (e == 'r') {
                acc += '\r';
            } else if (e == 't') {
                acc += '\t';
            } else if (e == 'b') {
                acc += '\b';
            } else if (e == 'f') {
                acc += '\f';
            } else {
                acc += e;
            }
        } else {
            acc += c;
        }
    }
    if (acc.empty()) {
        return std::nullopt;
    }
    std::wstring w;
    if (!Utf8PathToWide(acc, w)) {
        return std::nullopt;
    }
    std::filesystem::path p(w);
    if (p.empty() || !p.is_absolute()) {
        return std::nullopt;
    }
    p = p.lexically_normal();
    if (p.empty()) {
        return std::nullopt;
    }
    return p.wstring();
}

void WriteInstallerMarkerJson(bool dc_config_global_flag_present, const std::filesystem::path& game_root,
                              const std::wstring& config_dir_w, const std::string& game_display_name,
                              bool config_path_from_marker) {
    if (!dc_config_global_flag_present) {
        return;
    }
    if (game_root.empty() || config_dir_w.empty()) {
        return;
    }
    const std::string path_utf8 = WidePathToUtf8(config_dir_w);
    if (path_utf8.empty()) {
        return;
    }
    const std::string game_root_utf8 = WidePathToUtf8(game_root.wstring());
    const std::string path_source = config_path_from_marker ? "marker" : "default";
    const std::string body = std::string("{\"format_version\":1,\"display_commander_config_directory\":\"")
                             + EscapeJsonUtf8(path_utf8) + std::string("\",\"game_display_name\":\"")
                             + EscapeJsonUtf8(game_display_name)
                             + std::string("\",\"display_commander_game_install_root\":\"")
                             + EscapeJsonUtf8(game_root_utf8)
                             + std::string("\",\"display_commander_games_folder_name\":\"")
                             + EscapeJsonUtf8(game_display_name)
                             + std::string("\",\"display_commander_config_path_source\":\"")
                             + EscapeJsonUtf8(path_source) + std::string("\"}\n");
    const std::filesystem::path marker = game_root / L".display_commander_installer_marker.json";
    const std::filesystem::path tmp = game_root / L".display_commander_installer_marker.json.tmp";
    std::error_code ec;
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) {
            return;
        }
        out << body;
        out.flush();
        if (!out) {
            std::filesystem::remove(tmp, ec);
            return;
        }
    }
    ec.clear();
    std::filesystem::rename(tmp, marker, ec);
    if (!ec) {
        return;
    }
    std::filesystem::remove(marker, ec);
    ec.clear();
    std::filesystem::rename(tmp, marker, ec);
    if (ec) {
        std::error_code ec_rm;
        std::filesystem::remove(tmp, ec_rm);
        LogBootInstallerMarker("[DC] installer marker: could not write .display_commander_installer_marker.json (permissions?)");
    }
}

}  // namespace display_commander::features::installer_marker
