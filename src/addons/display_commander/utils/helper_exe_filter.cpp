// Source Code <Display Commander>
#include "helper_exe_filter.hpp"

// Libraries <standard C++>
#include <cwchar>

bool is_helper_or_crash_handler_exe(const wchar_t* filename) {
    if (!filename || !filename[0]) return true;
    wchar_t lower[512];
    size_t i = 0;
    for (; i < 511 && filename[i]; ++i)
        lower[i] = (wchar_t)(filename[i] >= L'A' && filename[i] <= L'Z' ? filename[i] - L'A' + L'a' : filename[i]);
    lower[i] = L'\0';
    const wchar_t* needles[] = {L"unitycrashhandler",   L"crashhandler", L"unityhelper",
                                L"unrealcefsubprocess", L"reportcrash",  L"bugtrap",
                                L"exceptionhandler",    L"launcher",     L"platformprocess"};
    for (const wchar_t* n : needles) {
        if (wcsstr(lower, n) != nullptr) return true;
    }
    return false;
}
