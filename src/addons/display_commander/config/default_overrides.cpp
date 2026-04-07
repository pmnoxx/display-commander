
// Source Code <Display Commander> // follow this order for includes in all files
#include "default_overrides.hpp"
#include "display_commander_config.hpp"
#include "../utils/logging.hpp"
#include "../utils/srwlock_wrapper.hpp"

// Libraries <standard C++>
#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

// Libraries <Windows.h> — before other Windows headers
#include <Windows.h>

// Libraries <Windows>

#undef what

namespace display_commander::config {

namespace {

struct OverrideRow {
    const char* exe_lower;
    const char* section;
    const char* key;
    const char* value;
};

// Flat table: linear scan (tiny row count — avoids nested std::map codegen in this TU).
static const OverrideRow k_override_rows[] = {
    {"re2.exe", "DisplayCommander", "AutoColorspace", "1"},
//    {"re2.exe", "DisplayCommander", "ContinueRendering", "1"}, causes mouse input to not be blocked TODO investigate
    {"re2.exe", "DisplayCommander", "WindowMode", "1"},
    {"re3.exe", "DisplayCommander", "AutoColorspace", "1"},
    {"re3.exe", "DisplayCommander", "ContinueRendering", "1"},
    {"re3.exe", "DisplayCommander", "WindowMode", "1"},
    {"re7.exe", "DisplayCommander", "AutoColorspace", "1"},
    {"re7.exe", "DisplayCommander", "ContinueRendering", "1"},
    {"re7.exe", "DisplayCommander", "WindowMode", "1"},
    {"re8.exe", "DisplayCommander", "AutoColorspace", "1"},
    {"re8.exe", "DisplayCommander", "ContinueRendering", "1"},
    {"re8.exe", "DisplayCommander", "WindowMode", "1"},
    {"sekiro.exe", "DisplayCommander", "AutoColorspace", "1"},
    {"sekiro.exe", "DisplayCommander", "ContinueRendering", "1"},
    {"sekiro.exe", "DisplayCommander", "WindowMode", "1"},
    {"eldenring.exe", "DisplayCommander", "AutoColorspace", "1"},
    {"eldenring.exe", "DisplayCommander", "ContinueRendering", "1"},
    {"eldenring.exe", "DisplayCommander", "WindowMode", "1"},
    {"armoredcore6.exe", "DisplayCommander", "AutoColorspace", "1"},
    {"armoredcore6.exe", "DisplayCommander", "ContinueRendering", "1"},
    {"armoredcore6.exe", "DisplayCommander", "WindowMode", "1"},
    {"hitman3.exe", "DisplayCommander", "AutoColorspace", "1"},
    {"hitman3.exe", "DisplayCommander", "ContinueRendering", "1"},
    {"hitman3.exe", "DisplayCommander", "WindowMode", "1"},
    {"devilmaycry5.exe", "DisplayCommander", "AutoColorspace", "1"},
    {"devilmaycry5.exe", "DisplayCommander", "ContinueRendering", "1"},
    {"devilmaycry5.exe", "DisplayCommander", "WindowMode", "1"},
};

struct KeyDisplayName {
    const char* key;
    const char* display;
};

static const KeyDisplayName k_key_display_names[] = {
    {"ContinueRendering", "Continue Rendering in Background (Fake Fullscreen)"},
    {"PreventMinimize", "Prevent Minimize"},
    {"PreventAlwaysOnTop", "Prevent Always on Top"},
    {"EnableFlipChain", "Enable Flip Chain"},
    {"ForceFlipDiscardUpgrade", "Force Flip Discard upgrade"},
    {"AutoColorspace", "Auto color space"},
    {"WindowMode", "Window Mode"},
};

const char* LookupDisplayNameForKey(const char* key) {
    if (key == nullptr) {
        return nullptr;
    }
    for (const KeyDisplayName& e : k_key_display_names) {
        if (std::strcmp(e.key, key) == 0) {
            return e.display;
        }
    }
    return nullptr;
}

bool ExeHasAnyOverrideRow(const char* exe_lower) {
    if (exe_lower == nullptr || exe_lower[0] == '\0') {
        return false;
    }
    for (const OverrideRow& r : k_override_rows) {
        if (std::strcmp(r.exe_lower, exe_lower) == 0) {
            return true;
        }
    }
    return false;
}

const char* FindOverrideValue(const char* exe_lower, const char* section, const char* key) {
    if (exe_lower == nullptr || section == nullptr || key == nullptr) {
        return nullptr;
    }
    for (const OverrideRow& r : k_override_rows) {
        if (std::strcmp(r.exe_lower, exe_lower) != 0) {
            continue;
        }
        if (std::strcmp(r.section, section) != 0) {
            continue;
        }
        if (std::strcmp(r.key, key) != 0) {
            continue;
        }
        return r.value;
    }
    return nullptr;
}

std::atomic<bool> g_exe_name_initialized{false};
std::string g_current_exe_lower;
SRWLOCK g_exe_name_srwlock = SRWLOCK_INIT;
// (section, key) pairs for which we returned an override during Load
std::vector<std::pair<std::string, std::string>> g_active_overrides;
SRWLOCK g_srwlock = SRWLOCK_INIT;

std::string ToLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

void EnsureLoaded() {
    if (g_exe_name_initialized.load(std::memory_order_acquire)) return;

    utils::SRWLockExclusive lock(g_exe_name_srwlock);
    if (g_exe_name_initialized.load(std::memory_order_relaxed)) return;

    g_current_exe_lower = GetCurrentExeNameLower();

    // Log exe check and result only once per process to avoid log spam.
    const std::string& exe = g_current_exe_lower;
    LogInfo("Game default overrides: checking against exe %.260s", exe.empty() ? "(unknown)" : exe.c_str());

    if (!exe.empty() && ExeHasAnyOverrideRow(exe.c_str())) {
        LogInfo("Game default override found for %.260s", exe.c_str());
    } else {
        LogInfo("No game default override for %.260s", exe.empty() ? "(unknown)" : exe.c_str());
    }

    g_exe_name_initialized.store(true, std::memory_order_release);
}

}  // namespace

std::string GetCurrentExeNameLower() {
    char exe_path[MAX_PATH];
    DWORD path_length = GetModuleFileNameA(nullptr, exe_path, MAX_PATH);
    if (path_length == 0) {
        GetCurrentDirectoryA(MAX_PATH, exe_path);
    }
    std::string path(exe_path);
    size_t slash = path.find_last_of("/\\");
    std::string filename = (slash != std::string::npos) ? path.substr(slash + 1) : path;
    return ToLower(filename);
}

bool GetDefaultOverride(const char* section, const char* key, std::string& out_value) {
    EnsureLoaded();
    if (section == nullptr || key == nullptr) return false;
    if (g_current_exe_lower.empty()) return false;

    const char* v = FindOverrideValue(g_current_exe_lower.c_str(), section, key);
    if (v == nullptr) return false;
    out_value = v;
    return true;
}

void MarkUsedOverride(const char* section, const char* key) {
    if (section == nullptr || key == nullptr) return;
    utils::SRWLockExclusive lock(g_srwlock);
    for (const auto& p : g_active_overrides) {
        if (p.first == section && p.second == key) {
            return;
        }
    }
    g_active_overrides.emplace_back(section, key);
}

bool HasActiveOverrides() {
    utils::SRWLockShared lock(g_srwlock);
    return !g_active_overrides.empty();
}

std::vector<DefaultOverrideEntry> GetActiveOverrideEntries() {
    EnsureLoaded();
    std::vector<DefaultOverrideEntry> result;
    const std::string& exe = g_current_exe_lower;
    if (exe.empty()) {
        return result;
    }

    utils::SRWLockShared lock(g_srwlock);
    for (const auto& active : g_active_overrides) {
        const char* v = FindOverrideValue(exe.c_str(), active.first.c_str(), active.second.c_str());
        if (v == nullptr) {
            continue;
        }
        DefaultOverrideEntry e;
        e.section = active.first;
        e.key = active.second;
        e.value = v;
        const char* dn = LookupDisplayNameForKey(active.second.c_str());
        e.display_name = (dn != nullptr) ? dn : active.second;
        result.push_back(std::move(e));
    }
    return result;
}

void ApplyDefaultOverridesToConfig() {
    std::vector<DefaultOverrideEntry> entries = GetActiveOverrideEntries();
    for (const auto& e : entries) {
        set_config_value(e.section.c_str(), e.key.c_str(), e.value);
    }
    if (!entries.empty()) {
        save_config("Apply default overrides to config");
        utils::SRWLockExclusive lock(g_srwlock);
        g_active_overrides.clear();
    }
}

}  // namespace display_commander::config
