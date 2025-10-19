cmake_minimum_required(VERSION 3.16)
project(game_commander)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Include directories
include_directories(${CMAKE_SOURCE_DIR}/external/reshade_includes)
include_directories(${CMAKE_SOURCE_DIR}/include)
include_directories(${CMAKE_SOURCE_DIR}/external/reshade/deps/imgui)
include_directories(${CMAKE_SOURCE_DIR}/external/reshade/deps/imgui/backends)
include_directories(${CMAKE_SOURCE_DIR}/external/reshade/deps/imgui/examples/libs/glfw/include)

# Source files
set(SOURCES
    main.cpp
    game_list.cpp
    steam_api.cpp
    injector_service.cpp
)

# ImGui source files
set(IMGUI_SOURCES
    ${CMAKE_SOURCE_DIR}/external/reshade/deps/imgui/imgui.cpp
    ${CMAKE_SOURCE_DIR}/external/reshade/deps/imgui/imgui_draw.cpp
    ${CMAKE_SOURCE_DIR}/external/reshade/deps/imgui/imgui_tables.cpp
    ${CMAKE_SOURCE_DIR}/external/reshade/deps/imgui/imgui_widgets.cpp
    ${CMAKE_SOURCE_DIR}/external/reshade/deps/imgui/backends/imgui_impl_glfw.cpp
    ${CMAKE_SOURCE_DIR}/external/reshade/deps/imgui/backends/imgui_impl_opengl3.cpp
)

# Create executable
add_executable(game_commander ${SOURCES} ${IMGUI_SOURCES})

# Windows specific
if(WIN32)
    # Choose the correct GLFW library based on target architecture
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(GLFW_LIB_DIR "lib-vc2010-64")
    else()
        set(GLFW_LIB_DIR "lib-vc2010-32")
    endif()

    target_link_libraries(game_commander
        user32
        shell32
        ole32
        advapi32
        version
        winmm
        imm32
        dwmapi
        setupapi
        wbemuuid
        gdi32
        comctl32
        opengl32
        ${CMAKE_SOURCE_DIR}/external/reshade/deps/imgui/examples/libs/glfw/${GLFW_LIB_DIR}/glfw3.lib
    )

    # Add legacy stdio definitions for VS2015+ compatibility
    target_compile_definitions(game_commander PRIVATE IMGUI_DISABLE_WIN32_FUNCTIONS=0)
    target_link_libraries(game_commander legacy_stdio_definitions)
endif()
