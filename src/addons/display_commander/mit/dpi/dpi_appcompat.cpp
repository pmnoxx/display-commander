// Source Code <Display Commander> // follow this order for includes in all files + add this comment at the top
#include "dpi_appcompat.hpp"
#include "../../utils/logging.hpp"
#include "../../utils/string_utils.hpp"

// Libraries <standard C++>
#include <string>
#include <string_view>
#include <vector>

// Libraries <Windows.h>
#include <Windows.h>

namespace display_commander::mit::dpi {
namespace {

constexpr wchar_t kLayersSubkey[] = L"Software\\Microsoft\\Windows NT\\CurrentVersion\\AppCompatFlags\\Layers";
constexpr wchar_t kHighDpiAwareToken[] = L"HIGHDPIAWARE";

wchar_t AsciiToUpper(wchar_t c) {
    if (c >= L'a' && c <= L'z') {
        return static_cast<wchar_t>(c - L'a' + L'A');
    }
    return c;
}

bool TokenEqualsI(std::wstring_view a, std::wstring_view b) {
    if (a.size() != b.size()) {
        return false;
    }
    for (size_t i = 0; i < a.size(); ++i) {
        if (AsciiToUpper(a[i]) != AsciiToUpper(b[i])) {
            return false;
        }
    }
    return true;
}

bool IsPrefixToken(std::wstring_view tok) {
    if (tok.empty()) {
        return false;
    }
    for (wchar_t c : tok) {
        if (c != L'~' && c != L'^' && c != L'$') {
            return false;
        }
    }
    return true;
}

bool IsDpiOverrideToken(std::wstring_view tok) {
    return TokenEqualsI(tok, kHighDpiAwareToken) || TokenEqualsI(tok, L"DPIUNAWARE")
           || TokenEqualsI(tok, L"GDIDPISCALING") || TokenEqualsI(tok, L"PERPROCESSSYSTEMDPIFORCEOFF")
           || TokenEqualsI(tok, L"PERPROCESSSYSTEMDPIFORCEON");
}

std::vector<std::wstring> SplitTokens(std::wstring_view s) {
    std::vector<std::wstring> tokens;
    size_t i = 0;
    while (i < s.size()) {
        while (i < s.size() && (s[i] == L' ' || s[i] == L'\t')) {
            ++i;
        }
        if (i >= s.size()) {
            break;
        }
        size_t j = i;
        while (j < s.size() && s[j] != L' ' && s[j] != L'\t') {
            ++j;
        }
        tokens.emplace_back(s.substr(i, j - i));
        i = j;
    }
    return tokens;
}

std::wstring JoinTokens(const std::vector<std::wstring>& tokens) {
    std::wstring out;
    for (size_t i = 0; i < tokens.size(); ++i) {
        if (i != 0) {
            out += L' ';
        }
        out += tokens[i];
    }
    return out;
}

bool TokensContainHighDpiAware(const std::vector<std::wstring>& tokens) {
    for (const std::wstring& tok : tokens) {
        if (TokenEqualsI(tok, kHighDpiAwareToken)) {
            return true;
        }
    }
    return false;
}

bool TokensContainConflictingDpi(const std::vector<std::wstring>& tokens) {
    for (const std::wstring& tok : tokens) {
        if (TokenEqualsI(tok, L"DPIUNAWARE") || TokenEqualsI(tok, L"GDIDPISCALING")
            || TokenEqualsI(tok, L"PERPROCESSSYSTEMDPIFORCEOFF")
            || TokenEqualsI(tok, L"PERPROCESSSYSTEMDPIFORCEON")) {
            return true;
        }
    }
    return false;
}

std::wstring BuildLayersValueWithHighDpiAware(std::wstring_view existing) {
    const std::vector<std::wstring> input = SplitTokens(existing);
    std::vector<std::wstring> prefix;
    std::vector<std::wstring> rest;
    bool seen_non_prefix = false;
    for (const std::wstring& tok : input) {
        if (!seen_non_prefix && IsPrefixToken(tok)) {
            prefix.push_back(tok);
            continue;
        }
        seen_non_prefix = true;
        if (IsDpiOverrideToken(tok)) {
            continue;
        }
        rest.push_back(tok);
    }
    if (prefix.empty()) {
        prefix.emplace_back(L"~");
    }

    std::vector<std::wstring> out;
    out.reserve(prefix.size() + rest.size() + 1);
    out.insert(out.end(), prefix.begin(), prefix.end());
    out.insert(out.end(), rest.begin(), rest.end());
    out.emplace_back(kHighDpiAwareToken);
    return JoinTokens(out);
}

std::wstring QueryCurrentExeFullPath() {
    wchar_t buf[4096] = {};
    DWORD n = static_cast<DWORD>(sizeof(buf) / sizeof(buf[0]));
    if (QueryFullProcessImageNameW(GetCurrentProcess(), 0, buf, &n) != 0) {
        return std::wstring(buf);
    }

    const DWORD err = GetLastError();
    if (err == ERROR_INSUFFICIENT_BUFFER && n > 1) {
        std::wstring grow(static_cast<size_t>(n), L'\0');
        DWORD n2 = n;
        if (QueryFullProcessImageNameW(GetCurrentProcess(), 0, grow.data(), &n2) != 0) {
            grow.resize(n2);
            return grow;
        }
    }

    wchar_t fallback[MAX_PATH] = {};
    if (GetModuleFileNameW(nullptr, fallback, MAX_PATH) != 0) {
        return fallback;
    }
    return {};
}

bool QueryLayersValue(HKEY key, const std::wstring& exe_path, std::wstring& out_data, DWORD& out_type, long& last_error) {
    DWORD type = 0;
    DWORD bytes = 0;
    LONG r = RegQueryValueExW(key, exe_path.c_str(), nullptr, &type, nullptr, &bytes);
    if (r == ERROR_FILE_NOT_FOUND) {
        out_data.clear();
        out_type = 0;
        last_error = r;
        return false;
    }
    if ((r != ERROR_SUCCESS && r != ERROR_MORE_DATA) || bytes == 0) {
        out_data.clear();
        out_type = type;
        last_error = r;
        return false;
    }

    std::vector<BYTE> raw(bytes + sizeof(wchar_t), 0);
    r = RegQueryValueExW(key, exe_path.c_str(), nullptr, &type, raw.data(), &bytes);
    if (r != ERROR_SUCCESS) {
        out_data.clear();
        out_type = type;
        last_error = r;
        return false;
    }
    if (type != REG_SZ && type != REG_EXPAND_SZ) {
        out_data.clear();
        out_type = type;
        last_error = ERROR_INVALID_DATA;
        return false;
    }

    const size_t wchar_count = bytes / sizeof(wchar_t);
    const wchar_t* wbuf = reinterpret_cast<const wchar_t*>(raw.data());
    size_t use_count = wchar_count;
    if (use_count > 0 && wbuf[use_count - 1] == L'\0') {
        --use_count;
    }
    out_data.assign(wbuf, use_count);
    out_type = type;
    last_error = ERROR_SUCCESS;
    return true;
}

}  // namespace

bool QueryCurrentExeDpiAppCompat(DpiAppCompatStatus& out_status) {
    out_status = {};
    out_status.exe_path = QueryCurrentExeFullPath();
    if (out_status.exe_path.empty()) {
        out_status.last_error = static_cast<long>(GetLastError());
        if (out_status.last_error == ERROR_SUCCESS) {
            out_status.last_error = ERROR_PATH_NOT_FOUND;
        }
        return false;
    }
    out_status.exe_path_ok = true;

    HKEY key = nullptr;
    const LONG open_rc = RegOpenKeyExW(HKEY_CURRENT_USER, kLayersSubkey, 0, KEY_READ, &key);
    if (open_rc == ERROR_FILE_NOT_FOUND) {
        out_status.last_error = ERROR_SUCCESS;
        return true;
    }
    if (open_rc != ERROR_SUCCESS) {
        out_status.last_error = open_rc;
        return false;
    }

    DWORD type = 0;
    out_status.value_exists = QueryLayersValue(key, out_status.exe_path, out_status.layers_data, type, out_status.last_error);
    RegCloseKey(key);
    if (out_status.value_exists) {
        out_status.high_dpi_aware = TokensContainHighDpiAware(SplitTokens(out_status.layers_data));
        out_status.last_error = ERROR_SUCCESS;
    } else if (out_status.last_error == ERROR_FILE_NOT_FOUND) {
        out_status.last_error = ERROR_SUCCESS;
    }
    return true;
}

bool EnsureCurrentExeHighDpiAware() {
    DpiAppCompatStatus status;
    QueryCurrentExeDpiAppCompat(status);
    if (!status.exe_path_ok) {
        LogWarn("[DpiAppCompat] cannot resolve current exe path, error=%ld", status.last_error);
        return false;
    }

    const std::string exe_utf8 = display_commander::utils::WideToUtf8(status.exe_path);
    const bool has_conflict = TokensContainConflictingDpi(SplitTokens(status.layers_data));
    if (status.high_dpi_aware && !has_conflict) {
        LogInfo("[DpiAppCompat] already HIGHDPIAWARE [Path] %s", exe_utf8.c_str());
        return true;
    }

    HKEY key = nullptr;
    DWORD disposition = 0;
    const LONG create_rc =
        RegCreateKeyExW(HKEY_CURRENT_USER, kLayersSubkey, 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_READ | KEY_SET_VALUE,
                        nullptr, &key, &disposition);
    if (create_rc != ERROR_SUCCESS) {
        LogWarn("[DpiAppCompat] failed to open Layers key, error=%ld", static_cast<long>(create_rc));
        return false;
    }

    const std::wstring new_data = BuildLayersValueWithHighDpiAware(status.layers_data);
    const DWORD bytes = static_cast<DWORD>((new_data.size() + 1) * sizeof(wchar_t));
    const LONG set_rc = RegSetValueExW(key, status.exe_path.c_str(), 0, REG_SZ,
                                       reinterpret_cast<const BYTE*>(new_data.c_str()), bytes);
    RegCloseKey(key);
    if (set_rc != ERROR_SUCCESS) {
        LogWarn("[DpiAppCompat] failed to write HIGHDPIAWARE, error=%ld [Path] %s", static_cast<long>(set_rc),
                exe_utf8.c_str());
        return false;
    }

    const std::string data_utf8 = display_commander::utils::WideToUtf8(new_data);
    LogInfo("[DpiAppCompat] set Layers=%s [Path] %s", data_utf8.c_str(), exe_utf8.c_str());
    return true;
}

}  // namespace display_commander::mit::dpi
