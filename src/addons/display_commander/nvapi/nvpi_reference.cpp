#include "nvpi_reference.hpp"
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace display_commander::nvapi {

namespace {

// Use a function in this module to get HMODULE of the addon DLL.
static HMODULE GetAddonModule() {
    HMODULE hMod = nullptr;
    if (::GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                             reinterpret_cast<LPCWSTR>(&GetAddonModule), &hMod)) {
        return hMod;
    }
    return nullptr;
}

// Built-in value list for "Smooth Motion - Allowed APIs" from NvidiaProfileInspectorRevamped Reference.xml.
static const std::vector<std::pair<std::uint32_t, std::string>>& GetSmoothMotionAllowedApisValuesFallback() {
    static const std::vector<std::pair<std::uint32_t, std::string>> kFallback = {
        {0x00000000, "None/All"},
        {0x00000001, "Allow DX12"},
        {0x00000002, "Allow DX11"},
        {0x00000004, "Allow Vulkan"},
    };
    return kFallback;
}

// Parse hex value from string "0xXXXXXXXX" or "XXXXXXXX".
static bool ParseHexValue(const std::string& s, std::uint32_t& out) {
    std::string t = s;
    while (!t.empty() && (t.front() == ' ' || t.front() == '\t')) {
        t.erase(0, 1);
    }
    if (t.size() >= 2 && (t[0] == '0' && (t[1] == 'x' || t[1] == 'X'))) {
        t = t.substr(2);
    }
    if (t.empty() || t.size() > 8) {
        return false;
    }
    out = 0;
    for (char c : t) {
        int d = 0;
        if (c >= '0' && c <= '9') {
            d = c - '0';
        } else if (c >= 'a' && c <= 'f') {
            d = c - 'a' + 10;
        } else if (c >= 'A' && c <= 'F') {
            d = c - 'A' + 10;
        } else {
            return false;
        }
        out = (out << 4) | static_cast<std::uint32_t>(d);
    }
    return true;
}

// Minimal XML-style extraction: find next <tag>content</tag> after start, return content; *end past </tag>.
static bool FindElement(const std::string& xml, const std::string& tag, size_t start, size_t* out_begin,
                        size_t* out_end) {
    std::string open = "<" + tag + ">";
    std::string close = "</" + tag + ">";
    size_t o = xml.find(open, start);
    if (o == std::string::npos) {
        return false;
    }
    size_t c = xml.find(close, o);
    if (c == std::string::npos) {
        return false;
    }
    *out_begin = o + open.size();
    *out_end = c + close.size();
    return true;
}

// Parse SettingValues block for a CustomSetting: collect (HexValue, UserfriendlyName) pairs.
// In Reference.xml each CustomSettingValue has UserfriendlyName then HexValue.
static void ParseSettingValuesBlock(const std::string& xml, size_t start, size_t end,
                                    std::vector<std::pair<std::uint32_t, std::string>>* out) {
    out->clear();
    size_t pos = start;
    while (pos < end) {
        size_t nb, ne;
        if (!FindElement(xml, "UserfriendlyName", pos, &nb, &ne)) {
            break;
        }
        std::string nameStr = xml.substr(nb, xml.find("</UserfriendlyName>", nb) - nb);
        pos = ne;
        size_t vb, ve;
        if (!FindElement(xml, "HexValue", pos, &vb, &ve)) {
            break;
        }
        std::string hexStr = xml.substr(vb, xml.find("</HexValue>", vb) - vb);
        pos = ve;
        std::uint32_t val = 0;
        if (ParseHexValue(hexStr, val)) {
            if (!nameStr.empty()) {
                out->push_back({val, nameStr});
            } else {
                out->push_back({val, "0x" + hexStr});
            }
        }
    }
}

}  // namespace

std::string GetAddonModuleDirectory() {
    HMODULE hMod = GetAddonModule();
    if (!hMod) {
        return {};
    }
    wchar_t path[MAX_PATH] = {};
    if (::GetModuleFileNameW(hMod, path, MAX_PATH) == 0) {
        return {};
    }
    std::filesystem::path p(path);
    std::filesystem::path dir = p.parent_path();
    return dir.string();
}

std::vector<std::pair<std::uint32_t, std::string>> GetSmoothMotionAllowedApisValues() {
    std::string dir = GetAddonModuleDirectory();
    if (dir.empty()) {
        return GetSmoothMotionAllowedApisValuesFallback();
    }
    std::filesystem::path xmlPath = std::filesystem::path(dir) / "Reference.xml";
    std::ifstream f(xmlPath);
    if (!f) {
        return GetSmoothMotionAllowedApisValuesFallback();
    }
    std::stringstream buf;
    buf << f.rdbuf();
    std::string xml = buf.str();
    f.close();

    const std::string marker = "Smooth Motion - Allowed APIs";
    size_t namePos = xml.find("<UserfriendlyName>");
    while (namePos != std::string::npos) {
        size_t contentStart = namePos + 18;
        size_t contentEnd = xml.find("</UserfriendlyName>", contentStart);
        if (contentEnd == std::string::npos) {
            break;
        }
        std::string name = xml.substr(contentStart, contentEnd - contentStart);
        if (name == marker) {
            // This CustomSetting block: find <SettingValues>...</SettingValues>
            size_t svOpen = xml.find("<SettingValues>", contentEnd);
            if (svOpen == std::string::npos) {
                break;
            }
            size_t svClose = xml.find("</SettingValues>", svOpen);
            if (svClose == std::string::npos) {
                break;
            }
            std::vector<std::pair<std::uint32_t, std::string>> parsed;
            ParseSettingValuesBlock(xml, svOpen + 15, svClose, &parsed);
            if (!parsed.empty()) {
                return parsed;
            }
            break;
        }
        namePos = xml.find("<UserfriendlyName>", contentEnd + 1);
    }
    return GetSmoothMotionAllowedApisValuesFallback();
}

std::vector<std::pair<std::uint32_t, std::string>> GetSmoothMotionAllowedApisFlags() {
    std::vector<std::pair<std::uint32_t, std::string>> out;
    for (const auto& p : GetSmoothMotionAllowedApisValues()) {
        if (p.first != 0) {
            out.push_back(p);
        }
    }
    return out;
}

}  // namespace display_commander::nvapi
