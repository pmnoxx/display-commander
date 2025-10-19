#include <iostream>
#include <memory>
#include <string>
#include <vector>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <GLFW/glfw3.h>
#include <windows.h>
#include <commdlg.h>
#include <shlobj.h>
#include <mutex>
#include <filesystem>
#include <algorithm>
#include <cctype>
#include "game_list.h"
#include "injector_service.h"

// Forward declarations
static void glfw_error_callback(int error, const char* description);
void renderMainWindow(GameListManager* gameList);
void renderAddGameDialog(GameListManager* gameList, bool* show_dialog);
void renderEditGameDialog(GameListManager* gameList, bool* show_dialog, int* editing_index);
void renderOptionsDialog(GameListManager* gameList, bool* show_dialog);
std::string openFileDialog(const char* filter = "Executable Files (*.exe)\0*.exe\0All Files (*.*)\0*.*\0");
std::string openFolderDialog();
bool shouldCloseModal();
void openTomlInNotepad(GameListManager* gameList);
void loadOptionsFromManager(GameListManager* gameList);
void saveOptionsToManager(GameListManager* gameList);
bool scanForRenoDXFiles(const std::string& gamePath);
void autoDetectRenoDXAndSetReshade();

// Global UI state
static bool show_add_game_dialog = false;
static bool show_edit_game_dialog = false;
static bool show_options_dialog = false;
static int editing_game_index = -1;

// Form data for add/edit dialogs
static char game_name[256] = "";
static char executable_path[512] = "";
static char working_directory[512] = "";
static char launch_arguments[256] = "";
static char icon_path[512] = "";
static bool is_steam_game = false;
static int steam_app_id = 0;
static bool enable_reshade = false;
static bool has_renodx_mod = false;
static bool use_local_injection = false;
static int proxy_dll_type = 0; // 0=None, 1=OpenGL32, 2=DXGI, 3=D3D11, 4=D3D12

// Global options data (will be synced with GameListManager)
static char reshade_path_32bit[512] = "";
static char reshade_path_64bit[512] = "";
static char display_commander_path[512] = "";
static bool override_shaders_path = false;
static char shaders_path[512] = "";
static bool override_textures_path = false;
static char textures_path[512] = "";

// Injector service options
static bool injector_service_enabled = false;
static bool injector_verbose_logging = true;

// Injector service
static std::unique_ptr<InjectorService> g_injector_service;

static void glfw_error_callback(int error, const char* description) {
    std::cerr << "GLFW Error " << error << ": " << description << std::endl;
}

void clearForm() {
    memset(game_name, 0, sizeof(game_name));
    memset(executable_path, 0, sizeof(executable_path));
    memset(working_directory, 0, sizeof(working_directory));
    memset(launch_arguments, 0, sizeof(launch_arguments));
    memset(icon_path, 0, sizeof(icon_path));
    is_steam_game = false;
    steam_app_id = 0;
    enable_reshade = false;
    has_renodx_mod = false;
    use_local_injection = false;
    proxy_dll_type = 0;
}

void loadGameIntoForm(const Game& game) {
    strncpy_s(game_name, sizeof(game_name), game.name.c_str(), _TRUNCATE);
    strncpy_s(executable_path, sizeof(executable_path), game.executable_path.c_str(), _TRUNCATE);
    strncpy_s(working_directory, sizeof(working_directory), game.working_directory.c_str(), _TRUNCATE);
    strncpy_s(launch_arguments, sizeof(launch_arguments), game.launch_arguments.c_str(), _TRUNCATE);
    strncpy_s(icon_path, sizeof(icon_path), game.icon_path.c_str(), _TRUNCATE);
    is_steam_game = game.is_steam_game;
    steam_app_id = static_cast<int>(game.steam_app_id);
    enable_reshade = game.enable_reshade;
    has_renodx_mod = game.has_renodx_mod;
    use_local_injection = game.use_local_injection;
    proxy_dll_type = static_cast<int>(game.proxy_dll_type);
}

void autoDetectRenoDXAndSetReshade() {
    // Auto-detect RenoDX files and set enable_reshade accordingly
    bool renodxFound = scanForRenoDXFiles(executable_path);
    has_renodx_mod = renodxFound;
    enable_reshade = renodxFound; // Set enable_reshade to true if RenoDX is found

    if (renodxFound) {
        std::cout << "Auto-detected RenoDX files - enabling ReShade for: " << executable_path << std::endl;
    }
}

bool scanForRenoDXFiles(const std::string& gamePath) {
    try {
        std::filesystem::path gameDir;

        // If we have a working directory, use that; otherwise use the executable's directory
        if (!working_directory[0]) {
            gameDir = std::filesystem::path(executable_path).parent_path();
        } else {
            gameDir = std::filesystem::path(working_directory);
        }

        if (!std::filesystem::exists(gameDir)) {
            return false;
        }

        // Look for common RenoDX file patterns
        std::vector<std::string> renodxPatterns = {
            "renodx.dll",
            "renodx.exe",
            "renodx.asi",
            "renodx",
            "RenoDX.dll",
            "RenoDX.exe",
            "RenoDX.asi",
            "RenoDX"
        };

        // Scan the game directory and subdirectories (max depth 2)
        for (const auto& entry : std::filesystem::recursive_directory_iterator(gameDir, std::filesystem::directory_options::skip_permission_denied)) {
            if (entry.is_regular_file()) {
                std::string filename = entry.path().filename().string();
                std::string filename_lower = filename;

                // Convert to lowercase for case-insensitive comparison
                std::transform(filename_lower.begin(), filename_lower.end(), filename_lower.begin(), ::tolower);

                for (const auto& pattern : renodxPatterns) {
                    std::string pattern_lower = pattern;
                    std::transform(pattern_lower.begin(), pattern_lower.end(), pattern_lower.begin(), ::tolower);

                    if (filename_lower.find(pattern_lower) != std::string::npos) {
                        return true;
                    }
                }
            }
        }

        return false;
    } catch (const std::exception& e) {
        std::cout << "Error scanning for RenoDX files: " << e.what() << std::endl;
        return false;
    }
}

void renderMainWindow(GameListManager* gameList) {
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);

    ImGui::Begin("Game Commander", nullptr,
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_MenuBar);

    // Menu bar
    if (ImGui::BeginMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Add Game", "Ctrl+N")) {
                show_add_game_dialog = true;
                clearForm();
            }
            if (ImGui::MenuItem("Reload Games")) {
                gameList->loadGames();
            }
            if (ImGui::MenuItem("Open TOML in Notepad")) {
                openTomlInNotepad(gameList);
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Options")) {
                show_options_dialog = true;
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Exit")) {
                glfwSetWindowShouldClose(glfwGetCurrentContext(), true);
            }
            ImGui::EndMenu();
        }
        ImGui::EndMenuBar();
    }

    // Main content
    ImGui::Text("Your Games (%zu)", gameList->getGameCount());
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "| Config: %s", gameList->getConfigPath().c_str());
    ImGui::Separator();

    if (ImGui::Button("+ Add Game")) {
        show_add_game_dialog = true;
        clearForm();
    }

    ImGui::SameLine();
    if (ImGui::Button("Reload")) {
        gameList->loadGames();
    }

    ImGui::SameLine();
    if (ImGui::Button("Open TOML in Notepad")) {
        openTomlInNotepad(gameList);
    }

    ImGui::SameLine();
    if (ImGui::Button("Options")) {
        show_options_dialog = true;
    }

    ImGui::SameLine();
    if (ImGui::Checkbox("Run Injector Service", &injector_service_enabled)) {
        if (injector_service_enabled) {
            if (!g_injector_service) {
                g_injector_service = std::make_unique<InjectorService>();
            }

            // Update target games from the game list
            g_injector_service->setTargetGames(gameList->getGames());

            // Debug: Count games with ReShade enabled
            int reshade_enabled_count = 0;
            for (const auto& game : gameList->getGames()) {
                if (game.enable_reshade) {
                    reshade_enabled_count++;
                }
            }
            std::cout << "Found " << reshade_enabled_count << " games with ReShade enabled" << std::endl;

            // Configure injector service
            const auto& options = gameList->getOptions();
            g_injector_service->setReShadeDllPaths(options.reshade_path_32bit, options.reshade_path_64bit);
            if (!options.display_commander_path.empty()) {
                g_injector_service->setDisplayCommanderPath(options.display_commander_path);
            }
            g_injector_service->setVerboseLogging(options.injector_verbose_logging);

            if (g_injector_service->start()) {
                std::cout << "Injector service started successfully" << std::endl;
            } else {
                std::cout << "Failed to start injector service - check console for error details" << std::endl;
                injector_service_enabled = false;
            }
        } else {
            if (g_injector_service) {
                g_injector_service->stop();
                std::cout << "Injector service stopped" << std::endl;
            }
        }
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Continuously monitor and inject ReShade into running games");
    }

    ImGui::Spacing();

    // Game list
    const auto& games = gameList->getGames();
    for (size_t i = 0; i < games.size(); ++i) {
        const Game& game = games[i];

        ImGui::PushID(static_cast<int>(i));

        // Game card 30 to keep eache ntry small and readable
        ImGui::BeginChild(("GameCard" + std::to_string(i)).c_str(),
            ImVec2(0,  35), true, ImGuiWindowFlags_NoScrollbar);

        // Executable path
        ImGui::Text("%s", game.executable_path.c_str());

        // Buttons
        ImGui::SameLine(ImGui::GetWindowWidth() - 300);

        if (ImGui::Button("Launch")) {
            auto game_name_or_path = game.name.empty() ? game.executable_path : game.name;
            if (gameList->launchGame(i)) {
                std::cout << "Launched game: " << game_name_or_path << std::endl;
            } else {
                std::cout << "Failed to launch game: " << game_name_or_path << std::endl;
            }
        }

        ImGui::SameLine();
        if (ImGui::Button("Edit")) {
            show_edit_game_dialog = true;
            editing_game_index = static_cast<int>(i);
            loadGameIntoForm(game);
        }

        ImGui::SameLine();
        if (ImGui::Button("Delete")) {
            gameList->removeGame(i);
        }
        ImGui::SameLine();
        if (ImGui::Checkbox("Reshade", &const_cast<Game&>(game).enable_reshade)) {
            // Update the game in the list when checkbox is toggled
            gameList->updateGame(i, game);
        }
        // Reshade checkbox
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Injects Reshade");
        }

    ImGui::SameLine();
    if (game.has_renodx_mod) {
        ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "[RenoDX]");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("This game has RenoDX mod installed (auto-detected)");
        }
    }


        ImGui::EndChild();
        ImGui::PopID();

        ImGui::Spacing();
    }

    ImGui::End();
}

void renderAddGameDialog(GameListManager* gameList, bool* show_dialog) {
    if (!*show_dialog) return;

    // Create modal dialog
    ImGui::SetNextWindowSize(ImVec2(600, 500), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));

    ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_Modal;

    if (!ImGui::Begin("Add Game", show_dialog, window_flags)) {
        ImGui::End();
        return;
    }

    // Close dialog if clicking outside or pressing Escape
    if (ImGui::IsKeyPressed(ImGuiKey_Escape) || shouldCloseModal()) {
        *show_dialog = false;
        clearForm();
        ImGui::End();
        return;
    }

    ImGui::InputText("Game Name", game_name, sizeof(game_name));

    // Executable Path with file selector
    ImGui::InputText("Executable Path", executable_path, sizeof(executable_path));
    ImGui::SameLine();
    if (ImGui::Button("Browse##exe")) {
        std::string selectedFile = openFileDialog("Executable Files (*.exe)\0*.exe\0All Files (*.*)\0*.*\0");
        if (!selectedFile.empty()) {
            strncpy_s(executable_path, sizeof(executable_path), selectedFile.c_str(), _TRUNCATE);
            // Auto-detect RenoDX when executable path changes
            autoDetectRenoDXAndSetReshade();
        }
    }

    // Working Directory with folder selector
    ImGui::InputText("Working Directory", working_directory, sizeof(working_directory));
    ImGui::SameLine();
    if (ImGui::Button("Browse##dir")) {
        std::string selectedFolder = openFolderDialog();
        if (!selectedFolder.empty()) {
            strncpy_s(working_directory, sizeof(working_directory), selectedFolder.c_str(), _TRUNCATE);
        }
    }

    ImGui::InputText("Launch Arguments", launch_arguments, sizeof(launch_arguments));

    // Icon Path with file selector
    ImGui::InputText("Icon Path (optional)", icon_path, sizeof(icon_path));
    ImGui::SameLine();
    if (ImGui::Button("Browse##icon")) {
        std::string selectedFile = openFileDialog("Image Files (*.ico;*.png;*.jpg;*.jpeg)\0*.ico;*.png;*.jpg;*.jpeg\0All Files (*.*)\0*.*\0");
        if (!selectedFile.empty()) {
            strncpy_s(icon_path, sizeof(icon_path), selectedFile.c_str(), _TRUNCATE);
        }
    }

    ImGui::Checkbox("Steam Game", &is_steam_game);
    if (is_steam_game) {
        ImGui::InputInt("Steam App ID", &steam_app_id);
    }

    ImGui::Checkbox("Enable Reshade", &enable_reshade);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Injects Reshade when launching this game");
    }

    ImGui::Checkbox("Use Local Injection", &use_local_injection);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Copy ReShade DLL as a proxy DLL instead of injecting (useful when injection fails)");
    }

    if (use_local_injection) {
        ImGui::Indent();
        const char* proxy_types[] = { "None", "opengl32.dll", "dxgi.dll", "d3d11.dll", "d3d12.dll" };
        ImGui::Combo("Proxy DLL Type", &proxy_dll_type, proxy_types, 5);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Select which system DLL to replace with ReShade");
        }
        ImGui::Unindent();
    }

    ImGui::Checkbox("Has RenoDX Mod", &has_renodx_mod);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Mark this game as having RenoDX mod installed");
    }

    ImGui::SameLine();
    if (ImGui::Button("Auto-Detect RenoDX")) {
        autoDetectRenoDXAndSetReshade();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Scan game folder for RenoDX files and auto-enable ReShade if found");
    }

    // Show status if RenoDX was auto-detected
    if (has_renodx_mod && enable_reshade) {
        ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "✓ RenoDX detected - ReShade enabled");
    }

    ImGui::Spacing();

    if (ImGui::Button("Add Game")) {
        Game newGame;
        newGame.name = game_name;
        newGame.executable_path = executable_path;
        newGame.working_directory = working_directory;
        newGame.launch_arguments = launch_arguments;
        newGame.icon_path = icon_path;
        newGame.is_steam_game = is_steam_game;
        newGame.steam_app_id = static_cast<uint32_t>(steam_app_id);
        newGame.enable_reshade = enable_reshade;
        newGame.has_renodx_mod = has_renodx_mod;
        newGame.use_local_injection = use_local_injection;
        newGame.proxy_dll_type = static_cast<ProxyDllType>(proxy_dll_type);

        gameList->addGame(newGame);
        *show_dialog = false;
        clearForm();
    }

    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
        *show_dialog = false;
        clearForm();
    }

    ImGui::End();
}

void renderEditGameDialog(GameListManager* gameList, bool* show_dialog, int* editing_index) {
    if (*editing_index < 0 || !*show_dialog) {
        *show_dialog = false;
        return;
    }

    // Create modal dialog
    ImGui::SetNextWindowSize(ImVec2(600, 500), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));

    ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_Modal;

    if (!ImGui::Begin("Edit Game", show_dialog, window_flags)) {
        ImGui::End();
        return;
    }

    // Close dialog if clicking outside or pressing Escape
    if (ImGui::IsKeyPressed(ImGuiKey_Escape) || shouldCloseModal()) {
        *show_dialog = false;
        *editing_index = -1;
        clearForm();
        ImGui::End();
        return;
    }

    ImGui::InputText("Game Name", game_name, sizeof(game_name));

    // Executable Path with file selector
    ImGui::InputText("Executable Path", executable_path, sizeof(executable_path));
    ImGui::SameLine();
    if (ImGui::Button("Browse##exe_edit")) {
        std::string selectedFile = openFileDialog("Executable Files (*.exe)\0*.exe\0All Files (*.*)\0*.*\0");
        if (!selectedFile.empty()) {
            strncpy_s(executable_path, sizeof(executable_path), selectedFile.c_str(), _TRUNCATE);
            // Auto-detect RenoDX when executable path changes
            autoDetectRenoDXAndSetReshade();
        }
    }

    // Working Directory with folder selector
    ImGui::InputText("Working Directory", working_directory, sizeof(working_directory));
    ImGui::SameLine();
    if (ImGui::Button("Browse##dir_edit")) {
        std::string selectedFolder = openFolderDialog();
        if (!selectedFolder.empty()) {
            strncpy_s(working_directory, sizeof(working_directory), selectedFolder.c_str(), _TRUNCATE);
        }
    }

    ImGui::InputText("Launch Arguments", launch_arguments, sizeof(launch_arguments));

    // Icon Path with file selector
    ImGui::InputText("Icon Path (optional)", icon_path, sizeof(icon_path));
    ImGui::SameLine();
    if (ImGui::Button("Browse##icon_edit")) {
        std::string selectedFile = openFileDialog("Image Files (*.ico;*.png;*.jpg;*.jpeg)\0*.ico;*.png;*.jpg;*.jpeg\0All Files (*.*)\0*.*\0");
        if (!selectedFile.empty()) {
            strncpy_s(icon_path, sizeof(icon_path), selectedFile.c_str(), _TRUNCATE);
        }
    }

    ImGui::Checkbox("Steam Game", &is_steam_game);
    if (is_steam_game) {
        ImGui::InputInt("Steam App ID", &steam_app_id);
    }

    ImGui::Checkbox("Enable Reshade", &enable_reshade);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Injects Reshade when launching this game");
    }

    ImGui::Checkbox("Use Local Injection", &use_local_injection);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Copy ReShade DLL as a proxy DLL instead of injecting (useful when injection fails)");
    }

    if (use_local_injection) {
        ImGui::Indent();
        const char* proxy_types[] = { "None", "opengl32.dll", "dxgi.dll", "d3d11.dll", "d3d12.dll" };
        ImGui::Combo("Proxy DLL Type", &proxy_dll_type, proxy_types, 5);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Select which system DLL to replace with ReShade");
        }
        ImGui::Unindent();
    }

    ImGui::Checkbox("Has RenoDX Mod", &has_renodx_mod);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Mark this game as having RenoDX mod installed");
    }

    ImGui::SameLine();
    if (ImGui::Button("Auto-Detect RenoDX")) {
        autoDetectRenoDXAndSetReshade();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Scan game folder for RenoDX files and auto-enable ReShade if found");
    }

    // Show status if RenoDX was auto-detected
    if (has_renodx_mod && enable_reshade) {
        ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "✓ RenoDX detected - ReShade enabled");
    }

    ImGui::Spacing();

    if (ImGui::Button("Save Changes")) {
        Game updatedGame;
        updatedGame.name = game_name;
        updatedGame.executable_path = executable_path;
        updatedGame.working_directory = working_directory;
        updatedGame.launch_arguments = launch_arguments;
        updatedGame.icon_path = icon_path;
        updatedGame.is_steam_game = is_steam_game;
        updatedGame.steam_app_id = static_cast<uint32_t>(steam_app_id);
        updatedGame.enable_reshade = enable_reshade;
        updatedGame.has_renodx_mod = has_renodx_mod;
        updatedGame.use_local_injection = use_local_injection;
        updatedGame.proxy_dll_type = static_cast<ProxyDllType>(proxy_dll_type);

        gameList->updateGame(*editing_index, updatedGame);
        *show_dialog = false;
        *editing_index = -1;
        clearForm();
    }

    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
        *show_dialog = false;
        *editing_index = -1;
        clearForm();
    }

    ImGui::End();
}

void renderOptionsDialog(GameListManager* gameList, bool* show_dialog) {
    if (!*show_dialog) return;

    // Create modal dialog
    ImGui::SetNextWindowSize(ImVec2(700, 300), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));

    ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_Modal;

    if (!ImGui::Begin("Options", show_dialog, window_flags)) {
        ImGui::End();
        return;
    }

    // Close dialog if clicking outside or pressing Escape
    if (ImGui::IsKeyPressed(ImGuiKey_Escape) || shouldCloseModal()) {
        *show_dialog = false;
        ImGui::End();
        return;
    }

    ImGui::Text("Global Settings");
    ImGui::Separator();

    // Reshade 32-bit Path
    ImGui::InputText("Reshade 32-bit Path", reshade_path_32bit, sizeof(reshade_path_32bit));
    ImGui::SameLine();
    if (ImGui::Button("Browse##reshade32")) {
        std::string selectedFile = openFileDialog("DLL Files (*.dll)\0*.dll\0All Files (*.*)\0*.*\0");
        if (!selectedFile.empty()) {
            strncpy_s(reshade_path_32bit, sizeof(reshade_path_32bit), selectedFile.c_str(), _TRUNCATE);
        }
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Path to Reshade 32-bit DLL (e.g., ReShade32.dll)");
    }

    // Reshade 64-bit Path
    ImGui::InputText("Reshade 64-bit Path", reshade_path_64bit, sizeof(reshade_path_64bit));
    ImGui::SameLine();
    if (ImGui::Button("Browse##reshade64")) {
        std::string selectedFile = openFileDialog("DLL Files (*.dll)\0*.dll\0All Files (*.*)\0*.*\0");
        if (!selectedFile.empty()) {
            strncpy_s(reshade_path_64bit, sizeof(reshade_path_64bit), selectedFile.c_str(), _TRUNCATE);
        }
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Path to Reshade 64-bit DLL (e.g., ReShade64.dll)");
    }

    // Display Commander Path
    ImGui::InputText("Display Commander Path", display_commander_path, sizeof(display_commander_path));
    ImGui::SameLine();
    if (ImGui::Button("Browse##display_commander")) {
        std::string selectedFile = openFileDialog("Executable Files (*.exe)\0*.exe\0All Files (*.*)\0*.*\0");
        if (!selectedFile.empty()) {
            strncpy_s(display_commander_path, sizeof(display_commander_path), selectedFile.c_str(), _TRUNCATE);
        }
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Path to Display Commander executable (e.g., zzz_display_commander.addon64)");
    }

    #if 0
    ImGui::Spacing();
    ImGui::Text("Reshade Settings");
    ImGui::Separator();

    // Override Shaders Path
    ImGui::Checkbox("Override Shaders Path", &override_shaders_path);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Override the default shaders path for all games");
    }

    // Shaders Path (only shown if override is enabled)
    if (override_shaders_path) {
        ImGui::InputText("Shaders Path", shaders_path, sizeof(shaders_path));
        ImGui::SameLine();
        if (ImGui::Button("Browse##shaders")) {
            std::string selectedFolder = openFolderDialog();
            if (!selectedFolder.empty()) {
                strncpy_s(shaders_path, sizeof(shaders_path), selectedFolder.c_str(), _TRUNCATE);
            }
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Custom path to shaders folder");
        }
    }

    // Override Textures Path
    ImGui::Checkbox("Override Textures Path", &override_textures_path);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Override the default textures path for all games");
    }

    // Textures Path (only shown if override is enabled)
    if (override_textures_path) {
        ImGui::InputText("Textures Path", textures_path, sizeof(textures_path));
        ImGui::SameLine();
        if (ImGui::Button("Browse##textures")) {
            std::string selectedFolder = openFolderDialog();
            if (!selectedFolder.empty()) {
                strncpy_s(textures_path, sizeof(textures_path), selectedFolder.c_str(), _TRUNCATE);
            }
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Custom path to textures folder");
        }
    }
    #endif

    ImGui::Spacing();
    ImGui::Text("Injector Service Settings");
    ImGui::Separator();

    // Injector Service Enabled
    ImGui::Checkbox("Enable Injector Service", &injector_service_enabled);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Enable the injector service to automatically inject ReShade into running games");
    }


    // Verbose Logging
    ImGui::Checkbox("Verbose Logging", &injector_verbose_logging);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Enable detailed logging for the injector service");
    }

    ImGui::Spacing();
    ImGui::Separator();

    // Buttons
    if (ImGui::Button("Save")) {
        // Save options to config file
        saveOptionsToManager(gameList);
        gameList->saveOptions();
        *show_dialog = false;
    }

    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
        *show_dialog = false;
    }

    ImGui::End();
}

std::string openFileDialog(const char* filter) {
    OPENFILENAMEA ofn;
    char szFile[260] = {0};

    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile);
    ofn.lpstrFilter = filter;
    ofn.nFilterIndex = 1;
    ofn.lpstrFileTitle = nullptr;
    ofn.nMaxFileTitle = 0;
    ofn.lpstrInitialDir = nullptr;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;

    if (GetOpenFileNameA(&ofn)) {
        return std::string(szFile);
    }

    return "";
}

std::string openFolderDialog() {
    BROWSEINFOA bi = {0};
    char szPath[MAX_PATH] = {0};

    bi.hwndOwner = nullptr;
    bi.pidlRoot = nullptr;
    bi.pszDisplayName = szPath;
    bi.lpszTitle = "Select Game Directory";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    bi.lpfn = nullptr;
    bi.lParam = 0;

    LPITEMIDLIST pidl = SHBrowseForFolderA(&bi);
    if (pidl != nullptr) {
        if (SHGetPathFromIDListA(pidl, szPath)) {
            CoTaskMemFree(pidl);
            return std::string(szPath);
        }
        CoTaskMemFree(pidl);
    }

    return "";
}

bool shouldCloseModal() {
    // Check if user clicked outside the modal dialog
    ImGuiIO& io = ImGui::GetIO();
    return (ImGui::IsMouseClicked(0) && !ImGui::IsAnyItemHovered() && !ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow));
}

void openTomlInNotepad(GameListManager* gameList) {
    // Get the config path from the game list manager
    std::string configPath = gameList->getConfigPath();

    // Open the TOML file in Notepad
    std::string command = "notepad.exe \"" + configPath + "\"";
    system(command.c_str());
}

void loadOptionsFromManager(GameListManager* gameList) {
    const auto& options = gameList->getOptions();

    strncpy_s(reshade_path_32bit, sizeof(reshade_path_32bit), options.reshade_path_32bit.c_str(), _TRUNCATE);
    strncpy_s(reshade_path_64bit, sizeof(reshade_path_64bit), options.reshade_path_64bit.c_str(), _TRUNCATE);
    strncpy_s(display_commander_path, sizeof(display_commander_path), options.display_commander_path.c_str(), _TRUNCATE);
    override_shaders_path = options.override_shaders_path;
    strncpy_s(shaders_path, sizeof(shaders_path), options.shaders_path.c_str(), _TRUNCATE);
    override_textures_path = options.override_textures_path;
    strncpy_s(textures_path, sizeof(textures_path), options.textures_path.c_str(), _TRUNCATE);

    // Injector service options
    injector_service_enabled = options.injector_service_enabled;
    injector_verbose_logging = options.injector_verbose_logging;
}

void saveOptionsToManager(GameListManager* gameList) {
    auto& options = gameList->getOptions();

    options.reshade_path_32bit = std::string(reshade_path_32bit);
    options.reshade_path_64bit = std::string(reshade_path_64bit);
    options.display_commander_path = std::string(display_commander_path);
    options.override_shaders_path = override_shaders_path;
    options.shaders_path = std::string(shaders_path);
    options.override_textures_path = override_textures_path;
    options.textures_path = std::string(textures_path);

    // Injector service options
    options.injector_service_enabled = injector_service_enabled;
    options.injector_verbose_logging = injector_verbose_logging;
}

int main() {
    // Initialize COM for folder dialog
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

    // Setup GLFW
    glfwSetErrorCallback(glfw_error_callback);
    if (!glfwInit()) {
        std::cerr << "Failed to initialize GLFW" << std::endl;
        CoUninitialize();
        return -1;
    }

    // GLFW window hints
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    // Create window
    GLFWwindow* window = glfwCreateWindow(1200, 800, "Game Commander", nullptr, nullptr);
    if (!window) {
        std::cerr << "Failed to create GLFW window" << std::endl;
        glfwTerminate();
        return -1;
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1); // Enable vsync

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();

    // Setup Platform/Renderer backends
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330");

    // Initialize game list manager
    auto gameList = std::make_unique<GameListManager>();
    gameList->loadGames();
    gameList->loadOptions();
    loadOptionsFromManager(gameList.get());

    // Initialize injector service if enabled
    if (injector_service_enabled) {
        g_injector_service = std::make_unique<InjectorService>();
        g_injector_service->setTargetGames(gameList->getGames());
        // Set ReShade DLL paths from config
        g_injector_service->setReShadeDllPaths(reshade_path_32bit, reshade_path_64bit);
        if (display_commander_path[0]) {
            // Set display commander path if one is configured
            g_injector_service->setDisplayCommanderPath(display_commander_path);
        }
        g_injector_service->setVerboseLogging(injector_verbose_logging);

        if (g_injector_service->start()) {
            std::cout << "Injector service auto-started" << std::endl;
        } else {
            std::cout << "Failed to auto-start injector service - check console for error details" << std::endl;
            injector_service_enabled = false;
        }
    }

    // Main loop
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        // Start the Dear ImGui frame
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // Render UI
        renderMainWindow(gameList.get());

        // Render modal dialogs with overlay
        if (show_add_game_dialog || show_edit_game_dialog) {
            // Draw semi-transparent overlay
            ImGui::SetNextWindowPos(ImVec2(0, 0));
            ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
            ImGui::SetNextWindowBgAlpha(0.5f);
            ImGui::Begin("ModalOverlay", nullptr,
                ImGuiWindowFlags_NoTitleBar |
                ImGuiWindowFlags_NoResize |
                ImGuiWindowFlags_NoMove |
                ImGuiWindowFlags_NoCollapse |
                ImGuiWindowFlags_NoScrollbar |
                ImGuiWindowFlags_NoInputs);
            ImGui::End();
        }

        if (show_add_game_dialog) {
            renderAddGameDialog(gameList.get(), &show_add_game_dialog);
        }

        if (show_edit_game_dialog) {
            renderEditGameDialog(gameList.get(), &show_edit_game_dialog, &editing_game_index);
        }

        if (show_options_dialog) {
            renderOptionsDialog(gameList.get(), &show_options_dialog);
        }

        // Rendering
        ImGui::Render();
        int display_w, display_h;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(0.45f, 0.55f, 0.60f, 1.00f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);
    }

    // Cleanup injector service
    if (g_injector_service) {
        g_injector_service->stop();
        g_injector_service.reset();
    }

    // Cleanup
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();

    // Cleanup COM
    CoUninitialize();

    return 0;
}
