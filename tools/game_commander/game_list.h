#pragma once

#include <string>
#include <vector>
#include <memory>

struct Game {
    std::string name;
    std::string executable_path;
    std::string working_directory;
    std::string launch_arguments;
    std::string icon_path;
    bool is_steam_game;
    uint32_t steam_app_id;
    bool enable_reshade;
    bool has_renodx_mod;

    Game() : is_steam_game(false), steam_app_id(0), enable_reshade(false), has_renodx_mod(false) {}
};

struct GlobalOptions {
    std::string reshade_path_32bit;
    std::string reshade_path_64bit;
    std::string display_commander_path;
    bool override_shaders_path;
    std::string shaders_path;
    bool override_textures_path;
    std::string textures_path;

    // Injector service settings
    bool injector_service_enabled;
    bool injector_verbose_logging;

    GlobalOptions() : override_shaders_path(false), override_textures_path(false),
                     injector_service_enabled(false), injector_verbose_logging(true) {}
};

class GameListManager {
public:
    GameListManager();
    ~GameListManager();

    // Game management
    void loadGames();
    void saveGames();
    void addGame(const Game& game);
    void removeGame(size_t index);
    void updateGame(size_t index, const Game& game);

    // Getters
    const std::vector<Game>& getGames() const { return games_; }
    Game* getGame(size_t index);
    size_t getGameCount() const { return games_.size(); }
    const std::string& getConfigPath() const { return config_path_; }

    // Game launching
    bool launchGame(size_t index);
    bool launchGame(const Game& game);

    // Options management
    void loadOptions();
    void saveOptions();
    GlobalOptions& getOptions() { return options_; }
    const GlobalOptions& getOptions() const { return options_; }

private:
    std::vector<Game> games_;
    std::string config_path_;
    GlobalOptions options_;

    void createDefaultConfig();
    std::string getHomeDirectory();
};
