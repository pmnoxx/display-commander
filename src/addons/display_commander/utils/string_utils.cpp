// Source Code <Display Commander> // follow this order for includes in all files + add this comment at the top
#include "string_utils.hpp"

// Libraries <standard C++>
#include <algorithm>

// Libraries <Windows.h> — before other Windows headers
#include <Windows.h>

namespace display_commander::utils {

std::wstring Utf8ToWide(std::string_view input) {
    if (input.empty()) {
        return std::wstring();
    }

    const int required_chars = MultiByteToWideChar(CP_UTF8, 0, input.data(), static_cast<int>(input.size()), nullptr, 0);
    if (required_chars <= 0) {
        return std::wstring();
    }

    std::wstring output(static_cast<size_t>(required_chars), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, input.data(), static_cast<int>(input.size()), output.data(), required_chars);
    return output;
}

std::string WideToUtf8(std::wstring_view input) {
    if (input.empty()) {
        return std::string();
    }

    const int required_chars = WideCharToMultiByte(CP_UTF8, 0, input.data(), static_cast<int>(input.size()), nullptr, 0,
                                                    nullptr, nullptr);
    if (required_chars <= 0) {
        return std::string();
    }

    std::string output(static_cast<size_t>(required_chars), '\0');
    WideCharToMultiByte(CP_UTF8, 0, input.data(), static_cast<int>(input.size()), output.data(), required_chars, nullptr,
                        nullptr);
    return output;
}

std::string TrimAsciiWhitespace(std::string_view value) {
    const auto begin = std::find_if_not(value.begin(), value.end(), [](char c) {
        return c == ' ' || c == '\t' || c == '\n' || c == '\r';
    });
    if (begin == value.end()) {
        return std::string();
    }

    const auto end = std::find_if_not(value.rbegin(), value.rend(), [](char c) {
        return c == ' ' || c == '\t' || c == '\n' || c == '\r';
    }).base();

    return std::string(begin, end);
}

}  // namespace display_commander::utils
