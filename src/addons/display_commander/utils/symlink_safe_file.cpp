// Source Code <Display Commander> // follow this order for includes in all files + add this comment at the top
#include "symlink_safe_file.hpp"
#include "logging.hpp"
#include "string_utils.hpp"

// Libraries <standard C++>
#include <fstream>
#include <string>

namespace display_commander::utils {

bool IsPathSymlink(const std::filesystem::path& path) {
    std::error_code ec;
    const std::filesystem::file_status st = std::filesystem::symlink_status(path, ec);
    if (ec) {
        return false;
    }
    return std::filesystem::is_symlink(st);
}

bool WriteTextFileReplace(const std::filesystem::path& path, std::string_view text, std::string_view log_tag) {
    const std::string path_utf8 = FilesystemPathToUtf8(path);
    const std::string tag(log_tag.empty() ? "File" : log_tag);

    if (IsPathSymlink(path)) {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        if (!file.is_open()) {
            LogWarn("[%s] symlink truncate failed: %s (open failed)", tag.c_str(), path_utf8.c_str());
            return false;
        }
        file.write(text.data(), static_cast<std::streamsize>(text.size()));
        file.close();
        if (!file) {
            LogWarn("[%s] symlink truncate failed: %s (write failed)", tag.c_str(), path_utf8.c_str());
            return false;
        }
        return true;
    }

    std::filesystem::path temp_path = path;
    temp_path += L".temp";
    {
        std::ofstream file(temp_path, std::ios::binary | std::ios::trunc);
        if (!file.is_open()) {
            return false;
        }
        file.write(text.data(), static_cast<std::streamsize>(text.size()));
        file.close();
        if (!file) {
            std::error_code ec;
            std::filesystem::remove(temp_path, ec);
            return false;
        }
    }

    std::error_code rename_ec;
    std::filesystem::rename(temp_path, path, rename_ec);
    if (rename_ec) {
        std::error_code remove_ec;
        std::filesystem::remove(temp_path, remove_ec);
        return false;
    }
    return true;
}

}  // namespace display_commander::utils
