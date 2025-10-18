#include "game_list.h"
#include <fstream>
#include <iostream>
#include <filesystem>
#include <cstdlib>
#include <windows.h>

#ifdef _WIN32
#include <shlobj.h>
#endif

GameListManager::GameListManager() {
    config_path_ = getHomeDirectory() + "/.game_commander/games.toml";

    // Create config directory if it doesn't exist
    std::filesystem::path config_dir = std::filesystem::path(config_path_).parent_path();
    if (!std::filesystem::exists(config_dir)) {
        std::filesystem::create_directories(config_dir);
    }
}

GameListManager::~GameListManager() {
    saveGames();
}

std::string GameListManager::getHomeDirectory() {
#ifdef _WIN32
    char* user_profile = nullptr;
    size_t len = 0;
    if (_dupenv_s(&user_profile, &len, "USERPROFILE") == 0 && user_profile != nullptr) {
        std::string result(user_profile);
        free(user_profile);
        return result;
    }
    return "C:\\Users\\Default";
#else
    const char* home = std::getenv("HOME");
    return home ? home : "/tmp";
#endif
}

void GameListManager::loadGames() {
    std::ifstream file(config_path_);
    if (!file.is_open()) {
        std::cout << "Config file not found, creating default: " << config_path_ << std::endl;
        createDefaultConfig();
        return;
    }

    // std::cout << "Loading games from: " << config_path_ << std::endl;

    games_.clear();

    // Simple TOML parser for our needs
    std::string line;
    Game current_game;
    bool in_game_section = false;

    while (std::getline(file, line)) {
        // Remove leading/trailing whitespace
        line.erase(0, line.find_first_not_of(" \t"));
        line.erase(line.find_last_not_of(" \t") + 1);

        if (line.empty() || line[0] == '#') continue;

        if (line[0] == '[' && line.back() == ']') {
            // New game section - save previous game if it has content
            if (in_game_section) {
                if (!current_game.name.empty() || !current_game.executable_path.empty()) {
                    games_.push_back(current_game);
                    // std::cout << "Added game: name='" << current_game.name << "', path='" << current_game.executable_path << "'" << std::endl;
                } else {
                    std::cout << "Skipped empty game entry" << std::endl;
                }
            }
            current_game = Game();
            in_game_section = true;
            continue;
        }

        if (in_game_section) {
            size_t pos = line.find('=');
            if (pos != std::string::npos) {
                std::string key = line.substr(0, pos);
                std::string value = line.substr(pos + 1);

                // Trim whitespace from key and value
                key.erase(0, key.find_first_not_of(" \t"));
                key.erase(key.find_last_not_of(" \t") + 1);
                value.erase(0, value.find_first_not_of(" \t"));
                value.erase(value.find_last_not_of(" \t") + 1);

                // Remove quotes if present
                if (value.length() >= 2 && value[0] == '"' && value.back() == '"') {
                    value = value.substr(1, value.length() - 2);
                }

                // Debug: std::cout << "Parsing: key='" << key << "', value='" << value << "'" << std::endl;

                if (key == "name") current_game.name = value;
                else if (key == "executable_path") current_game.executable_path = value;
                else if (key == "working_directory") current_game.working_directory = value;
                else if (key == "launch_arguments") current_game.launch_arguments = value;
                else if (key == "icon_path") current_game.icon_path = value;
                else if (key == "is_steam_game") current_game.is_steam_game = (value == "true");
                else if (key == "steam_app_id") current_game.steam_app_id = std::stoul(value);
                else if (key == "enable_reshade") current_game.enable_reshade = (value == "true");
                else if (key == "has_renodx_mod") current_game.has_renodx_mod = (value == "true");
            }
        }
    }

    // Handle the last game if we were in a section
    if (in_game_section) {
        if (!current_game.name.empty() || !current_game.executable_path.empty()) {
            games_.push_back(current_game);
                    // std::cout << "Added game: name='" << current_game.name << "', path='" << current_game.executable_path << "'" << std::endl;
        } else {
            std::cout << "Skipped empty game entry" << std::endl;
        }
    }

    file.close();
    std::cout << "Loaded " << games_.size() << " games from config file." << std::endl;
}

void GameListManager::saveGames() {
    std::ofstream file(config_path_);
    if (!file.is_open()) {
        std::cerr << "Failed to save games to " << config_path_ << std::endl;
        return;
    }

    std::cout << "Saving " << games_.size() << " games to: " << config_path_ << std::endl;

    file << "# Game Commander Configuration\n";
    file << "# This file contains your game list\n\n";

    for (size_t i = 0; i < games_.size(); ++i) {
        const Game& game = games_[i];
        file << "[game_" << i << "]\n";
        file << "name = \"" << game.name << "\"\n";
        file << "executable_path = \"" << game.executable_path << "\"\n";
        file << "working_directory = \"" << game.working_directory << "\"\n";
        file << "launch_arguments = \"" << game.launch_arguments << "\"\n";
        file << "icon_path = \"" << game.icon_path << "\"\n";
        file << "is_steam_game = " << (game.is_steam_game ? "true" : "false") << "\n";
        file << "steam_app_id = " << game.steam_app_id << "\n";
        file << "enable_reshade = " << (game.enable_reshade ? "true" : "false") << "\n";
        file << "has_renodx_mod = " << (game.has_renodx_mod ? "true" : "false") << "\n\n";
    }

    file.close();
}

void GameListManager::createDefaultConfig() {
    // Create some example games
    Game example1;
    example1.name = "Example Game 1";
    example1.executable_path = "C:\\Games\\Example\\game.exe";
    example1.working_directory = "C:\\Games\\Example";
    example1.launch_arguments = "-windowed";
    example1.is_steam_game = false;
    example1.enable_reshade = false;
    example1.has_renodx_mod = false;
    games_.push_back(example1);

    saveGames();
}

void GameListManager::addGame(const Game& game) {
    games_.push_back(game);
    saveGames();
}

void GameListManager::removeGame(size_t index) {
    if (index < games_.size()) {
        games_.erase(games_.begin() + index);
        saveGames();
    }
}

void GameListManager::updateGame(size_t index, const Game& game) {
    if (index < games_.size()) {
        games_[index] = game;
        saveGames();
    }
}

Game* GameListManager::getGame(size_t index) {
    if (index < games_.size()) {
        return &games_[index];
    }
    return nullptr;
}

bool GameListManager::launchGame(size_t index) {
    if (index >= games_.size()) return false;
    return launchGame(games_[index]);
}

bool GameListManager::launchGame(const Game& game) {
    if (game.is_steam_game) {
        // Launch via Steam
        std::string steam_url = "steam://run/" + std::to_string(game.steam_app_id);
        if (!game.launch_arguments.empty()) {
            steam_url += "//" + game.launch_arguments;
        }

        HINSTANCE result = ShellExecuteA(nullptr, "open", steam_url.c_str(), nullptr, nullptr, SW_SHOW);
        return reinterpret_cast<intptr_t>(result) > 32;
    } else {
        // Launch executable directly
        std::string command = "\"" + game.executable_path + "\"";
        if (!game.launch_arguments.empty()) {
            command += " " + game.launch_arguments;
        }

        STARTUPINFOA si = {};
        PROCESS_INFORMATION pi = {};
        si.cb = sizeof(si);

        BOOL success = CreateProcessA(
            nullptr,
            const_cast<char*>(command.c_str()),
            nullptr,
            nullptr,
            FALSE,
            0,
            nullptr,
            game.working_directory.empty() ? nullptr : game.working_directory.c_str(),
            &si,
            &pi
        );

        if (success) {
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
            return true;
        }

        return false;
    }
}

void GameListManager::loadOptions() {
    std::string options_path = getHomeDirectory() + "/.game_commander/options.toml";
    std::ifstream file(options_path);
    if (!file.is_open()) {
        std::cout << "Options file not found, using defaults: " << options_path << std::endl;
        return;
    }

    std::string line;
    while (std::getline(file, line)) {
        // Skip empty lines and comments
        if (line.empty() || line[0] == '#') continue;

        // Find the equals sign
        size_t pos = line.find('=');
        if (pos == std::string::npos) continue;

        // Extract key and value
        std::string key = line.substr(0, pos);
        std::string value = line.substr(pos + 1);

        // Trim whitespace
        key.erase(0, key.find_first_not_of(" \t"));
        key.erase(key.find_last_not_of(" \t") + 1);
        value.erase(0, value.find_first_not_of(" \t"));
        value.erase(value.find_last_not_of(" \t") + 1);

        // Remove quotes if present
        if (value.length() >= 2 && value[0] == '"' && value[value.length()-1] == '"') {
            value = value.substr(1, value.length() - 2);
        }

        // Set options based on key
        if (key == "reshade_path_32bit") {
            options_.reshade_path_32bit = value;
        } else if (key == "reshade_path_64bit") {
            options_.reshade_path_64bit = value;
        } else if (key == "display_commander_path") {
            options_.display_commander_path = value;
        } else if (key == "override_shaders_path") {
            options_.override_shaders_path = (value == "true");
        } else if (key == "shaders_path") {
            options_.shaders_path = value;
        } else if (key == "override_textures_path") {
            options_.override_textures_path = (value == "true");
        } else if (key == "textures_path") {
            options_.textures_path = value;
        } else if (key == "injector_service_enabled") {
            options_.injector_service_enabled = (value == "true");
        } else if (key == "injector_verbose_logging") {
            options_.injector_verbose_logging = (value == "true");
        }
    }

    file.close();
    std::cout << "Loaded options from: " << options_path << std::endl;
}

void GameListManager::saveOptions() {
    std::string options_path = getHomeDirectory() + "/.game_commander/options.toml";
    std::ofstream file(options_path);
    if (!file.is_open()) {
        std::cerr << "Failed to create options file: " << options_path << std::endl;
        return;
    }

    file << "# Game Commander Global Options\n";
    file << "# This file contains global settings for Game Commander\n\n";

    file << "reshade_path_32bit = \"" << options_.reshade_path_32bit << "\"\n";
    file << "reshade_path_64bit = \"" << options_.reshade_path_64bit << "\"\n";
    file << "display_commander_path = \"" << options_.display_commander_path << "\"\n";
    file << "override_shaders_path = " << (options_.override_shaders_path ? "true" : "false") << "\n";
    file << "shaders_path = \"" << options_.shaders_path << "\"\n";
    file << "override_textures_path = " << (options_.override_textures_path ? "true" : "false") << "\n";
    file << "textures_path = \"" << options_.textures_path << "\"\n";
    file << "injector_service_enabled = " << (options_.injector_service_enabled ? "true" : "false") << "\n";
    file << "injector_verbose_logging = " << (options_.injector_verbose_logging ? "true" : "false") << "\n";

    file.close();
    std::cout << "Saved options to: " << options_path << std::endl;
}
