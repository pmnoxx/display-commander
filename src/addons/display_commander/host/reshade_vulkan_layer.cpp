// Source Code <Display Commander> // follow this order for includes in all files + add this comment at the top
#include "reshade_vulkan_layer.hpp"

// Source Code <Display Commander>
#include "../utils/logging.hpp"
#include "../utils/string_utils.hpp"

// Libraries <standard C++>
#include <algorithm>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

// Libraries <Windows.h>
#include <Windows.h>

namespace host::reshade_vulkan_layer {

namespace {

constexpr const wchar_t* kImplicitLayersKey = L"SOFTWARE\\Khronos\\Vulkan\\ImplicitLayers";
constexpr const char* kReShadeLayerName = "VK_LAYER_reshade";

// Value of a JSON string member `"key": "value"` (first occurrence after `from`), unescaped. Empty if not found.
std::string FindJsonString(const std::string& text, const char* key, size_t from = 0) {
    const std::string needle = std::string("\"") + key + "\"";
    size_t pos = text.find(needle, from);
    if (pos == std::string::npos) return {};
    pos = text.find(':', pos + needle.size());
    if (pos == std::string::npos) return {};
    pos = text.find('"', pos + 1);
    if (pos == std::string::npos) return {};
    std::string value;
    for (size_t i = pos + 1; i < text.size(); ++i) {
        const char c = text[i];
        if (c == '"') return value;
        if (c == '\\' && i + 1 < text.size()) {
            const char e = text[++i];
            switch (e) {
                case 'n': value += '\n'; break;
                case 't': value += '\t'; break;
                case 'r': value += '\r'; break;
                default:  value += e; break;  // \\ \/ \"
            }
            continue;
        }
        value += c;
    }
    return {};
}

bool ReadSmallFile(const std::filesystem::path& path, std::string* out) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;
    out->assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    return out->size() < (1u << 20);
}

// True if the PE at path targets this process' architecture.
bool DllMatchesProcessBitness(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;
    IMAGE_DOS_HEADER dos = {};
    if (!file.read(reinterpret_cast<char*>(&dos), sizeof(dos)) || dos.e_magic != IMAGE_DOS_SIGNATURE) return false;
    file.seekg(dos.e_lfanew, std::ios::beg);
    DWORD signature = 0;
    IMAGE_FILE_HEADER header = {};
    if (!file.read(reinterpret_cast<char*>(&signature), sizeof(signature)) || signature != IMAGE_NT_SIGNATURE)
        return false;
    if (!file.read(reinterpret_cast<char*>(&header), sizeof(header))) return false;
#ifdef _WIN64
    return header.Machine == IMAGE_FILE_MACHINE_AMD64;
#else
    return header.Machine == IMAGE_FILE_MACHINE_I386;
#endif
}

bool EnvVarSet(const wchar_t* name, std::wstring* out_value = nullptr) {
    wchar_t buf[512] = {};
    const DWORD len = GetEnvironmentVariableW(name, buf, static_cast<DWORD>(std::size(buf)));
    if (len == 0 || len >= std::size(buf)) return false;
    if (out_value != nullptr) *out_value = buf;
    return true;
}

// Loader-level disables: the manifest's disable_environment variable and VK_LOADER_LAYERS_DISABLE.
bool LayerDisabledByEnvironment(const std::string& manifest) {
    // "disable_environment": { "DISABLE_VK_LAYER_reshade_1": "1" } -> the member name is the variable.
    const size_t block = manifest.find("\"disable_environment\"");
    if (block != std::string::npos) {
        const size_t open = manifest.find('{', block);
        const size_t name_start = (open != std::string::npos) ? manifest.find('"', open) : std::string::npos;
        const size_t name_end = (name_start != std::string::npos) ? manifest.find('"', name_start + 1) : std::string::npos;
        if (name_end != std::string::npos) {
            const std::string name = manifest.substr(name_start + 1, name_end - name_start - 1);
            if (EnvVarSet(display_commander::utils::Utf8ToWide(name).c_str())) return true;
        }
    }
    std::wstring layers_disable;
    if (EnvVarSet(L"VK_LOADER_LAYERS_DISABLE", &layers_disable)) {
        std::transform(layers_disable.begin(), layers_disable.end(), layers_disable.begin(), ::towlower);
        if (layers_disable.find(L"~all~") != std::wstring::npos
            || layers_disable.find(L"~implicit~") != std::wstring::npos
            || layers_disable.find(L"vk_layer_reshade") != std::wstring::npos) {
            return true;
        }
    }
    return false;
}

bool CheckManifest(const std::wstring& manifest_path, LayerInfo* out_info) {
    std::string manifest;
    if (!ReadSmallFile(manifest_path, &manifest)) return false;
    const size_t layer = manifest.find("\"layer\"");
    if (layer == std::string::npos) return false;  // "layers" (array) manifests are not used by ReShade
    if (FindJsonString(manifest, "name", layer) != kReShadeLayerName) return false;
    const std::string library = FindJsonString(manifest, "library_path", layer);
    if (library.empty()) return false;

    std::filesystem::path library_path(display_commander::utils::Utf8ToWide(library));
    if (library_path.is_relative()) {
        library_path = std::filesystem::path(manifest_path).parent_path() / library_path;
    }
    std::error_code ec;
    library_path = std::filesystem::weakly_canonical(library_path, ec);
    if (!std::filesystem::exists(library_path, ec) || !DllMatchesProcessBitness(library_path)) return false;
    if (LayerDisabledByEnvironment(manifest)) {
        LogInfo("[reshade_vulkan_layer] %ls is disabled by environment", manifest_path.c_str());
        return false;
    }
    if (out_info != nullptr) {
        out_info->manifest_path = manifest_path;
        out_info->library_path = library_path.wstring();
    }
    return true;
}

bool ScanRoot(HKEY root, LayerInfo* out_info) {
    HKEY key = nullptr;
    if (RegOpenKeyExW(root, kImplicitLayersKey, 0, KEY_READ, &key) != ERROR_SUCCESS) return false;
    bool found = false;
    std::vector<wchar_t> name(32768);
    for (DWORD index = 0; !found; ++index) {
        DWORD name_len = static_cast<DWORD>(name.size());
        DWORD type = 0;
        DWORD value = 1;
        DWORD value_size = sizeof(value);
        const LSTATUS status = RegEnumValueW(key, index, name.data(), &name_len, nullptr, &type,
                                             reinterpret_cast<BYTE*>(&value), &value_size);
        if (status == ERROR_NO_MORE_ITEMS) break;
        if (status != ERROR_SUCCESS && status != ERROR_MORE_DATA) continue;
        // Value name = manifest path, REG_DWORD 0 = enabled.
        if (status != ERROR_SUCCESS || type != REG_DWORD || value != 0) continue;
        found = CheckManifest(std::wstring(name.data(), name_len), out_info);
    }
    RegCloseKey(key);
    return found;
}

}  // namespace

bool FindEnabledReShadeLayer(LayerInfo* out_info) {
    // 32-bit processes on 64-bit Windows read HKLM\SOFTWARE\WOW6432Node through registry redirection, like the loader.
    return ScanRoot(HKEY_LOCAL_MACHINE, out_info) || ScanRoot(HKEY_CURRENT_USER, out_info);
}

}  // namespace host::reshade_vulkan_layer
