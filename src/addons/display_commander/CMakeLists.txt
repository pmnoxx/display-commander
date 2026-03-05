# Display Commander Addon CMakeLists.txt

# Sources (exclude cli_ui_exe - it is a separate executable target)
file(GLOB_RECURSE DISPLAY_COMMANDER_SOURCES
  CONFIGURE_DEPENDS
  ${CMAKE_CURRENT_LIST_DIR}/*.cpp
  ${CMAKE_CURRENT_LIST_DIR}/utils/*.cpp
)
list(FILTER DISPLAY_COMMANDER_SOURCES EXCLUDE REGEX ".*/cli_ui_exe/.*")
# ReShade ImGui wrapper is header-only (included from experimental_tab.cpp) to avoid undefined ImGui symbols at link
list(FILTER DISPLAY_COMMANDER_SOURCES EXCLUDE REGEX ".*/imgui_wrapper_reshade\\.cpp$")
# main_exe.cpp is only for the standalone .exe target; DLL must not link it (RunDisplayCommanderStandalone is exe-only).
set(DISPLAY_COMMANDER_DLL_SOURCES ${DISPLAY_COMMANDER_SOURCES})
list(FILTER DISPLAY_COMMANDER_DLL_SOURCES EXCLUDE REGEX ".*/main_exe\\.cpp$")
# Common sources compiled once into an object library and linked into both DLL and exe (main_entry.cpp differs by define).
set(DISPLAY_COMMANDER_COMMON_SOURCES ${DISPLAY_COMMANDER_DLL_SOURCES})
list(FILTER DISPLAY_COMMANDER_COMMON_SOURCES EXCLUDE REGEX ".*/main_entry\\.cpp$")
# jump_list.cpp is exe-only (uses DISPLAY_COMMANDER_BUILD_EXE for Win32 Jump List; not linked into DLL).
list(FILTER DISPLAY_COMMANDER_COMMON_SOURCES EXCLUDE REGEX ".*/jump_list\\.cpp$")

if(DISPLAY_COMMANDER_DLL_SOURCES STREQUAL "")
  message(FATAL_ERROR "No sources found under src/addons/display_commander")
endif()

# Version (single source of truth for addon and exe; used by common_options and version.rc)
set(DISPLAY_COMMANDER_VERSION_MAJOR 0)
set(DISPLAY_COMMANDER_VERSION_MINOR 12)
set(DISPLAY_COMMANDER_VERSION_PATCH 295)
set(DISPLAY_COMMANDER_VERSION_STRING "${DISPLAY_COMMANDER_VERSION_MAJOR}.${DISPLAY_COMMANDER_VERSION_MINOR}.${DISPLAY_COMMANDER_VERSION_PATCH}.${GIT_COMMIT_COUNT}")
if(CMAKE_SIZEOF_VOID_P EQUAL 4)
  set(DISPLAY_COMMANDER_BITNESS "32-bit")
else()
  set(DISPLAY_COMMANDER_BITNESS "64-bit")
endif()
configure_file(
  ${CMAKE_CURRENT_LIST_DIR}/proxy_dll/version.rc.in
  ${CMAKE_CURRENT_BINARY_DIR}/version.rc
  @ONLY
)

# Common compile options, definitions, and includes shared by DLL and exe (and by the object library).
# Reduces duplication and, with the object library below, compiles shared sources once for both targets.
add_library(display_commander_common_options INTERFACE)
target_compile_definitions(display_commander_common_options INTERFACE
  GIT_COMMIT_COUNT=${GIT_COMMIT_COUNT}
  ${BUILD_DATE_DEFINE}
  ${BUILD_TIME_DEFINE}
  DISPLAY_COMMANDER_VERSION_MAJOR=${DISPLAY_COMMANDER_VERSION_MAJOR}
  DISPLAY_COMMANDER_VERSION_MINOR=${DISPLAY_COMMANDER_VERSION_MINOR}
  DISPLAY_COMMANDER_VERSION_PATCH=${DISPLAY_COMMANDER_VERSION_PATCH}
)
if(EXPERIMENTAL_TAB)
  target_compile_definitions(display_commander_common_options INTERFACE EXPERIMENTAL_TAB=1)
endif()
if(EXPERIMENTAL_FEATURES)
  target_compile_definitions(display_commander_common_options INTERFACE EXPERIMENTAL_FEATURES=1)
endif()
target_include_directories(display_commander_common_options INTERFACE
  ${CMAKE_CURRENT_LIST_DIR}/../../../src
  ${CMAKE_CURRENT_LIST_DIR}/../../../src/addons/display_commander
  ${CMAKE_CURRENT_LIST_DIR}/../../../include
  SYSTEM
  $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/../../../external/reshade/include>
  $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/../../../external/reshade>
  $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/../../../external/reshade/deps/imgui>
  $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/../../../external/Streamline/include>
  $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/../../../external/nvapi>
  $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/../../../external/nvidia-dlss/include>
  $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/../../../external/minhook/include>
  $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/../../../external/vulkan-headers/include>
  $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/../../../external/tomlplusplus/include>
)
if(MSVC)
  target_compile_options(display_commander_common_options INTERFACE /bigobj /arch:SSE2 /arch:AVX /arch:AVX2 /wd4191 /utf-8 /D_ENABLE_EXTENDED_ALIGNED_STORAGE)
  target_compile_options(display_commander_common_options INTERFACE
    $<$<OR:$<CONFIG:Debug>,$<CONFIG:RelWithDebInfo>>:/Zi>
    $<$<CONFIG:Release>:/Zi>
  )
  target_link_options(display_commander_common_options INTERFACE
    $<$<OR:$<CONFIG:Debug>,$<CONFIG:RelWithDebInfo>>:/DEBUG>
    $<$<CONFIG:Release>:/DEBUG>
  )
endif()
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(display_commander_common_options INTERFACE -msse4.1 -msse4.2 -mavx -mavx2 -mmwaitx -Wno-microsoft-cast -Wno-microsoft-string-literal-from-predefined)
endif()

# Object library: all sources except main_entry.cpp (and main_exe.cpp). Compiled once and linked into both DLL and exe.
add_library(display_commander_objs OBJECT ${DISPLAY_COMMANDER_COMMON_SOURCES})
target_link_libraries(display_commander_objs PRIVATE display_commander_common_options)
if(MSVC)
  set_property(TARGET display_commander_objs PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
set_source_files_properties(${CMAKE_CURRENT_LIST_DIR}/ui/cli_standalone_ui.cpp PROPERTIES COMPILE_DEFINITIONS "ImGui=ImGuiStandalone;ImDrawList=ImDrawListStandalone")
set_source_files_properties(${CMAKE_CURRENT_LIST_DIR}/ui/imgui_wrapper_standalone.cpp PROPERTIES COMPILE_DEFINITIONS "ImGui=ImGuiStandalone;ImDrawList=ImDrawListStandalone")

# DLL target: entry + resources + object libraries (common code compiled once)
add_library(zzz_display_commander MODULE
  ${CMAKE_CURRENT_LIST_DIR}/main_entry.cpp
  ${CMAKE_CURRENT_BINARY_DIR}/version.rc
)
target_sources(zzz_display_commander PRIVATE
  $<TARGET_OBJECTS:display_commander_objs>
  ${CMAKE_CURRENT_LIST_DIR}/proxy_dll/exports.def
)
target_link_libraries(zzz_display_commander PRIVATE display_commander_common_options)

# DLL-specific: static runtime, PDB name (compile options come from display_commander_common_options)
if(MSVC)
  set_property(TARGET zzz_display_commander PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  set_target_properties(zzz_display_commander PROPERTIES
    PDB_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    PDB_NAME "zzz_display_commander"
  )
endif()

# System libraries removed (xinput, dinput8, winmm, dbghelp) to avoid DLL dependency issues - they are loaded dynamically
# Version library is needed for GetFileVersionInfo functions
# HID libraries are needed for HID device enumeration and management
# DXGI library is needed for DXGI interface IIDs
# ETW libraries (tdh/advapi32) are needed for PresentMon-style tracing
# WinInet library is needed for HTTP downloads (shader packages list)
# Note: Some toolchains (e.g. clang-cl without full Windows SDK lib path) may not have evntrace.lib on the link path.
# bcrypt for SHA256 (ReShade installer signature verification)
# opengl32 not linked: when used as opengl32.dll proxy we export gl*/wgl*; linking opengl32.lib would conflict.
# Standalone UI and imgui_impl_opengl3 use runtime loader (see opengl_loader and imgui embedded loader).
target_link_libraries(zzz_display_commander PRIVATE  setupapi tdh advapi32 wininet bcrypt)

# NVAPI: load system nvapi64.dll at runtime via nvapi_loader (no nvapi64.lib). Headers still from external/nvapi.
# MinHook library
if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/../../../external/minhook")
  # MinHook source files
  set(MINHOOK_SOURCES
    ${CMAKE_CURRENT_LIST_DIR}/../../../external/minhook/src/buffer.c
    ${CMAKE_CURRENT_LIST_DIR}/../../../external/minhook/src/hook.c
    ${CMAKE_CURRENT_LIST_DIR}/../../../external/minhook/src/trampoline.c
    ${CMAKE_CURRENT_LIST_DIR}/../../../external/minhook/src/hde/hde32.c
    ${CMAKE_CURRENT_LIST_DIR}/../../../external/minhook/src/hde/hde64.c
  )

  # Create MinHook static library
  add_library(minhook STATIC ${MINHOOK_SOURCES})

  # MinHook compile definitions
  target_compile_definitions(minhook PRIVATE MH_STATIC=1)

  # MinHook include directories
  target_include_directories(minhook PRIVATE ${CMAKE_CURRENT_LIST_DIR}/../../../external/minhook/include)

  # When using clang-cl, undefine _MSC_VER for MinHook C sources to force memcpy path instead of __movsb
  if(CMAKE_C_COMPILER_ID STREQUAL "Clang")
    set_source_files_properties(${MINHOOK_SOURCES} PROPERTIES COMPILE_OPTIONS "-U_MSC_VER")
  endif()

  # Use static runtime linking for MinHook as well
  if(MSVC)
    set_property(TARGET minhook PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  endif()

  # Link MinHook to display commander
  target_link_libraries(zzz_display_commander PRIVATE minhook)

  message(STATUS "MinHook library configured successfully")
else()
  message(FATAL_ERROR "MinHook not found at ${CMAKE_CURRENT_LIST_DIR}/../../../external/minhook. Run: git submodule update --init --recursive")
endif()

# Output naming
if(CMAKE_GENERATOR_PLATFORM STREQUAL "Win32" OR CMAKE_EXE_LINKER_FLAGS MATCHES "/machine:X86" OR CMAKE_SIZEOF_VOID_P EQUAL 4)
  set(TARGET_SUFFIX ".addon32")
else()
  set(TARGET_SUFFIX ".addon64")
endif()

set_target_properties(zzz_display_commander PROPERTIES
  SUFFIX "${TARGET_SUFFIX}"
  LINK_OPTIONS "$<$<C_COMPILER_ID:MSVC>:/DEF:${CMAKE_CURRENT_LIST_DIR}/proxy_dll/exports.def>"
)

# Copy NvidiaProfileInspectorRevamped Reference.xml next to the addon DLL so NVPI reference data can be loaded at runtime.
set(NVPI_REFERENCE_XML "${CMAKE_CURRENT_LIST_DIR}/../../../external/NvidiaProfileInspectorRevamped/nspector/Reference.xml")
if(EXISTS "${NVPI_REFERENCE_XML}")
  add_custom_command(TARGET zzz_display_commander POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${NVPI_REFERENCE_XML}" "$<TARGET_FILE_DIR:zzz_display_commander>/Reference.xml"
    COMMENT "Copy Reference.xml (NVPI) next to addon DLL"
  )
  message(STATUS "NVPI Reference.xml will be copied from NvidiaProfileInspectorRevamped submodule")
else()
  message(STATUS "NVPI Reference.xml not found (optional). Run: git submodule update --init external/NvidiaProfileInspectorRevamped")
endif()

# Embed Display Commander shader files and game_default_overrides.toml in the DLL (RCDATA).
set(DC_CONTROL_FX "${CMAKE_CURRENT_LIST_DIR}/res/effects/DisplayCommander_Control.fx")
set(DC_PERCEPTUALBOOST_FX "${CMAKE_CURRENT_LIST_DIR}/res/effects/DisplayCommander_PerceptualBoost.fx")
set(DC_COLOR_FXH "${CMAKE_CURRENT_LIST_DIR}/res/effects/color.fxh")
set(DC_RESHADE_FXH "${CMAKE_CURRENT_LIST_DIR}/res/effects/ReShade.fxh")
set(DC_GAME_DEFAULT_OVERRIDES_TOML "${CMAKE_CURRENT_LIST_DIR}/res/game_default_overrides.toml")
set(DC_SHADER_RC "${CMAKE_CURRENT_BINARY_DIR}/shader_effect.rc")
if(EXISTS "${DC_CONTROL_FX}")
  if(EXISTS "${DC_PERCEPTUALBOOST_FX}")
    set(DC_SHADER_RC_BYPRODUCTS "${CMAKE_CURRENT_BINARY_DIR}/DisplayCommander_Control.fx" "${CMAKE_CURRENT_BINARY_DIR}/color.fxh" "${CMAKE_CURRENT_BINARY_DIR}/ReShade.fxh" "${CMAKE_CURRENT_BINARY_DIR}/DisplayCommander_PerceptualBoost.fx" "${CMAKE_CURRENT_BINARY_DIR}/game_default_overrides.toml")
    set(DC_SHADER_RC_CONTENT "// Generated - embeds DisplayCommander shaders and game_default_overrides in DLL\n#pragma code_page(65001)\n#define IDR_CONTROL_FX 300\n#define IDR_COLOR_FXH 301\n#define IDR_RESHADE_FXH 302\n#define IDR_PERCEPTUALBOOST_FX 303\n#define IDR_GAME_DEFAULT_OVERRIDES 304\nIDR_CONTROL_FX RCDATA \"DisplayCommander_Control.fx\"\nIDR_COLOR_FXH RCDATA \"color.fxh\"\nIDR_RESHADE_FXH RCDATA \"ReShade.fxh\"\nIDR_PERCEPTUALBOOST_FX RCDATA \"DisplayCommander_PerceptualBoost.fx\"\nIDR_GAME_DEFAULT_OVERRIDES RCDATA \"game_default_overrides.toml\"\n")
  else()
    set(DC_SHADER_RC_BYPRODUCTS "${CMAKE_CURRENT_BINARY_DIR}/DisplayCommander_Control.fx" "${CMAKE_CURRENT_BINARY_DIR}/color.fxh" "${CMAKE_CURRENT_BINARY_DIR}/ReShade.fxh" "${CMAKE_CURRENT_BINARY_DIR}/game_default_overrides.toml")
    set(DC_SHADER_RC_CONTENT "// Generated - embeds DisplayCommander shaders and game_default_overrides in DLL\n#pragma code_page(65001)\n#define IDR_CONTROL_FX 300\n#define IDR_COLOR_FXH 301\n#define IDR_RESHADE_FXH 302\n#define IDR_GAME_DEFAULT_OVERRIDES 304\nIDR_CONTROL_FX RCDATA \"DisplayCommander_Control.fx\"\nIDR_COLOR_FXH RCDATA \"color.fxh\"\nIDR_RESHADE_FXH RCDATA \"ReShade.fxh\"\nIDR_GAME_DEFAULT_OVERRIDES RCDATA \"game_default_overrides.toml\"\n")
  endif()
  # Generate shader_effect.rc at build time (so it exists after clean); script is written at configure time.
  file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/write_shader_rc.cmake"
    "file(WRITE \"${DC_SHADER_RC}\" [[${DC_SHADER_RC_CONTENT}]])\n")
  add_custom_target(copy_shader_files_for_rc ALL
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${DC_CONTROL_FX}" "${CMAKE_CURRENT_BINARY_DIR}/DisplayCommander_Control.fx"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${DC_COLOR_FXH}" "${CMAKE_CURRENT_BINARY_DIR}/color.fxh"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${DC_RESHADE_FXH}" "${CMAKE_CURRENT_BINARY_DIR}/ReShade.fxh"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${DC_GAME_DEFAULT_OVERRIDES_TOML}" "${CMAKE_CURRENT_BINARY_DIR}/game_default_overrides.toml"
    COMMAND ${CMAKE_COMMAND} -P "${CMAKE_CURRENT_BINARY_DIR}/write_shader_rc.cmake"
    COMMENT "Copy DisplayCommander shader files and game_default_overrides.toml to build dir for resource embedding"
    BYPRODUCTS ${DC_SHADER_RC_BYPRODUCTS} ${DC_SHADER_RC}
  )
  if(EXISTS "${DC_PERCEPTUALBOOST_FX}")
    add_custom_command(TARGET copy_shader_files_for_rc POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different "${DC_PERCEPTUALBOOST_FX}" "${CMAKE_CURRENT_BINARY_DIR}/DisplayCommander_PerceptualBoost.fx"
    )
  endif()
  if(NOT EXISTS "${DC_GAME_DEFAULT_OVERRIDES_TOML}")
    message(WARNING "game_default_overrides.toml not found at ${DC_GAME_DEFAULT_OVERRIDES_TOML} - resource 304 will be missing")
  endif()
  target_sources(zzz_display_commander PRIVATE ${DC_SHADER_RC})
  add_dependencies(zzz_display_commander copy_shader_files_for_rc)
  set_source_files_properties(${DC_SHADER_RC} PROPERTIES GENERATED TRUE)
  if(NOT EXISTS "${DC_COLOR_FXH}")
    message(WARNING "color.fxh not found at ${DC_COLOR_FXH} - resource 301 will be missing")
  endif()
  if(NOT EXISTS "${DC_RESHADE_FXH}")
    message(WARNING "ReShade.fxh not found at ${DC_RESHADE_FXH} - resource 302 will be missing")
  endif()
  if(NOT EXISTS "${DC_PERCEPTUALBOOST_FX}")
    message(WARNING "DisplayCommander_PerceptualBoost.fx not found at ${DC_PERCEPTUALBOOST_FX} - resource 303 will be missing")
  endif()
endif()

# opengl32_proxy.cpp defines wgl* that wingdi.h declares as dllimport; suppress inconsistent-dllimport
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  set_source_files_properties(${CMAKE_CURRENT_LIST_DIR}/proxy_dll/opengl32_proxy.cpp PROPERTIES COMPILE_OPTIONS "-Wno-inconsistent-dllimport")
  set_source_files_properties(${CMAKE_CURRENT_LIST_DIR}/proxy_dll/winmm_proxy.cpp PROPERTIES COMPILE_OPTIONS "-Wno-inconsistent-dllimport")
  set_source_files_properties(${CMAKE_CURRENT_LIST_DIR}/proxy_dll/dbghelp_proxy.cpp PROPERTIES COMPILE_OPTIONS "-Wno-inconsistent-dllimport")
endif()

# Standalone UIs (RunStandaloneSettingsUI, RunStandaloneGamesOnlyUI) use a second ImGui in namespace
# ImGuiStandalone to avoid symbol clash with ReShade's ImGui. No separate .exe.
# Use an OBJECT library so standalone ImGui is compiled exactly once with ImGui=ImGuiStandalone and
# ImDrawList=ImDrawListStandalone, avoiding LNK2005 duplicate symbol errors: ReShade's reshade_overlay.hpp
# provides inline ImDrawList::AddLine/AddRect/AddCircle etc., while imgui_draw.cpp provides the real
# implementations; renaming ImDrawList for the standalone build keeps the two symbol sets distinct.
set(IMGUI_DIR "${CMAKE_CURRENT_LIST_DIR}/../../../external/reshade/deps/imgui")
set(IMGUI_STANDALONE_SOURCES
  ${IMGUI_DIR}/imgui.cpp
  ${IMGUI_DIR}/imgui_draw.cpp
  ${IMGUI_DIR}/imgui_tables.cpp
  ${IMGUI_DIR}/imgui_widgets.cpp
  ${IMGUI_DIR}/backends/imgui_impl_win32.cpp
  ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
)
add_library(imgui_standalone_obj OBJECT ${IMGUI_STANDALONE_SOURCES})
target_compile_definitions(imgui_standalone_obj PRIVATE ImGui=ImGuiStandalone ImDrawList=ImDrawListStandalone)
target_include_directories(imgui_standalone_obj
  PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}/../../../external/reshade/deps/imgui
  ${CMAKE_CURRENT_LIST_DIR}/../../../external/reshade/deps/imgui/backends
)
if(MSVC)
  set_property(TARGET imgui_standalone_obj PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
target_sources(zzz_display_commander PRIVATE $<TARGET_OBJECTS:imgui_standalone_obj>)
set_source_files_properties(${CMAKE_CURRENT_LIST_DIR}/ui/cli_standalone_ui.cpp PROPERTIES COMPILE_DEFINITIONS "ImGui=ImGuiStandalone;ImDrawList=ImDrawListStandalone")
set_source_files_properties(${CMAKE_CURRENT_LIST_DIR}/ui/imgui_wrapper_standalone.cpp PROPERTIES COMPILE_DEFINITIONS "ImGui=ImGuiStandalone;ImDrawList=ImDrawListStandalone")

# ---------------------------------------------------------------------------
# Launcher exe icon: generate .ico from vector (SVG) or .NET fallback, then embed via .rc
# ---------------------------------------------------------------------------
set(LAUNCHER_ICON_ICO "${CMAKE_CURRENT_LIST_DIR}/res/launcher_icon.ico")
add_custom_command(
  OUTPUT "${LAUNCHER_ICON_ICO}"
  COMMAND PowerShell -ExecutionPolicy Bypass -File "${CMAKE_CURRENT_LIST_DIR}/res/make_launcher_icon.ps1" -ResDir "${CMAKE_CURRENT_LIST_DIR}/res"
  WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}/res"
  DEPENDS "${CMAKE_CURRENT_LIST_DIR}/res/launcher_icon.svg" "${CMAKE_CURRENT_LIST_DIR}/res/make_launcher_icon.ps1"
  COMMENT "Generating launcher_icon.ico for exe"
)
add_custom_target(launcher_icon ALL DEPENDS "${LAUNCHER_ICON_ICO}")

# ---------------------------------------------------------------------------
# Standalone .exe target: same codebase as the DLL, different entry point.
# Uses display_commander_objs (common code compiled once) + main_entry.cpp (with BUILD_EXE) + main_exe.cpp.
# ---------------------------------------------------------------------------
add_executable(display_commander_exe WIN32
  ${CMAKE_CURRENT_LIST_DIR}/main_entry.cpp
  ${CMAKE_CURRENT_LIST_DIR}/main_exe.cpp
  ${CMAKE_CURRENT_BINARY_DIR}/version.rc
  ${CMAKE_CURRENT_LIST_DIR}/res/launcher_icon.rc
)
add_dependencies(display_commander_exe launcher_icon)
target_sources(display_commander_exe PRIVATE
  $<TARGET_OBJECTS:display_commander_objs>
  $<TARGET_OBJECTS:imgui_standalone_obj>
)
target_compile_definitions(display_commander_exe PRIVATE DISPLAY_COMMANDER_BUILD_EXE)
target_link_libraries(display_commander_exe PRIVATE display_commander_common_options setupapi tdh advapi32 wininet bcrypt shell32)
target_link_libraries(display_commander_exe PRIVATE minhook)
if(MSVC)
  set_property(TARGET display_commander_exe PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  set_target_properties(display_commander_exe PROPERTIES
    PDB_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    PDB_NAME "display_commander_exe"
  )
endif()
# Exe output name by platform (Launcher = Games + Settings window, same UI as rundll32 Launcher)
if(CMAKE_GENERATOR_PLATFORM STREQUAL "Win32" OR CMAKE_EXE_LINKER_FLAGS MATCHES "/machine:X86" OR CMAKE_SIZEOF_VOID_P EQUAL 4)
  set_target_properties(display_commander_exe PROPERTIES OUTPUT_NAME "Display Commander Launcher32")
else()
  set_target_properties(display_commander_exe PROPERTIES OUTPUT_NAME "Display Commander Launcher")
endif()
