#pragma once

#include <string>
#include <vector>
#include <memory>

enum class ProxyDllType {
    None,
    OpenGL32,
    DXGI,
    D3D11,
    D3D12
};

// Helper function to get proxy DLL filename
inline std::string getProxyDllFilename(ProxyDllType type) {
    switch (type) {
        case ProxyDllType::OpenGL32: return "opengl32.dll";
        case ProxyDllType::DXGI: return "dxgi.dll";
        case ProxyDllType::D3D11: return "d3d11.dll";
        case ProxyDllType::D3D12: return "d3d12.dll";
        default: return "";
    }
}

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
    bool use_local_injection;
    ProxyDllType proxy_dll_type;

    Game() : is_steam_game(false), steam_app_id(0), enable_reshade(false), has_renodx_mod(false),
             use_local_injection(false), proxy_dll_type(ProxyDllType::None) {}
};

struct GlobalOptions {
    std::string reshade_path_32bit;
    std::string reshade_path_64bit;
    std::string display_commander_path_32bit;
    std::string display_commander_path_64bit;
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
